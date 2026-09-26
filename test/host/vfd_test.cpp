#include "pd2200.hpp"
#include "clock_vfd.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
struct Sink : Print {
    std::vector<uint8_t> bytes;
    size_t write(uint8_t value) override { bytes.push_back(value); return 1; }
};
using clock_display::Frame;
using clock_display::Output;
// Every runtime update must consist ONLY of positioned characters: no clear,
// full-row writes, padding, or NUL content. Binary position 0 is valid.
void checkUpdates(const std::vector<uint8_t>& bytes) {
    for(size_t i=0;i<bytes.size();) {
        assert(i+4<=bytes.size());
        assert(bytes[i]==0x1b && bytes[i+1]==0x48 && bytes[i+2]<40);
        assert(bytes[i+3]>0x20 && bytes[i+3]<=0x7e);
        const unsigned address=bytes[i+2];
        assert(address!=8); // HH is always a pair from 07.
        const unsigned length=address==7 ? 5 : 4;
        assert(i+length<=bytes.size());
        if(length==5) assert(bytes[i+4]>0x20 && bytes[i+4]<=0x7e);
        i+=length;
        assert((address>=3 && address<=5) || (address>=7 && address<=16) ||
               (address>=20 && address<=23) ||
               (address>=26 && address<=29) ||
               (address>=32 && address<=34) || address==36 || address==37);
    }
}
void finish(Output& output, const Frame& target) {
    for(unsigned i=0;i<200;++i) output.service(target,true);
}
int main() {
    Sink sink; pd2200::Display display(sink);
    display.begin();
    const std::vector<uint8_t> expected_init={0x1b,0x49,0x16,0x1b,0x4c,0x3f,0x0e,0x0c};
    assert(sink.bytes==expected_init); // Preserve exact brightness/init command bytes.
    sink.bytes.clear();
    clock_model::State state;
    state.utc_valid=state.gps_valid=state.pps_present=state.pps_locked=true;
    state.utc.hour=3; state.utc.minute=1;
    clock_model::Pulse pulse; pulse.seen=true; pulse.at_us=1000000;
    Frame frame=clock_display::render(state,pulse,pulse.at_us);
    clock_display::writeInitialFields(display,frame);
    // Emulate hardware clear memory, interpreting spaces as '0' if sent. The
    // initialized screen must nevertheless match the intended 2x20 frame.
    Frame screen;
    const unsigned lengths[]={3,10,3,1,3,1,3,1,1};
    const unsigned addresses[]={3,7,20,23,26,29,32,36,37};
    size_t offset=0;
    for(unsigned field=0;field<9;++field) {
        assert(sink.bytes[offset++]==0x1b && sink.bytes[offset++]==0x48);
        assert(sink.bytes[offset++]==addresses[field]);
        for(unsigned c=0;c<lengths[field];++c) {
            const auto byte=sink.bytes[offset++];
            assert(byte>0x20 && byte<=0x7e);
            unsigned address=addresses[field]+c;
            screen.rows[address/20][address%20]=byte==0x20 ? '0' : byte;
        }
    }
    assert(offset==sink.bytes.size());
    assert(std::memcmp(screen.rows,frame.rows,sizeof(frame.rows))==0);

    Output output(sink);
    for(unsigned second=0;second<60;++second) {
        state.utc.second=second; state.utc.minute=1;
        const auto before=clock_display::render(state,pulse,pulse.at_us+900000);
        output.reset(before);
        state.utc.second=(second+1)%60;
        state.utc.minute=second==59 ? 2 : 1;
        const auto after=clock_display::render(state,pulse,pulse.at_us); // Next PPS phase zero.
        sink.bytes.clear(); finish(output,after);
        checkUpdates(sink.bytes);
        // Existing clock writes, plus exactly one 9->0 decade character.
        assert(sink.bytes.size()==(second==59 ? 16u : second%10==9 ? 12u : 8u));
        for(size_t i=0;i<sink.bytes.size();i+=4) {
            const auto col=sink.bytes[i+2];
            assert(col>=7 && col<=16);
            assert(before.rows[0][col]!=after.rows[0][col]);
            assert(sink.bytes[i+3]==after.rows[0][col]);
        }
        assert(sink.bytes[sink.bytes.size()-2]==16); // UTC before animation.
        const auto written=sink.bytes.size(); finish(output,after);
        assert(sink.bytes.size()==written); // Unchanged characters suppressed.
    }
    // All status transitions overwrite just the necessary one-character flags.
    state.utc.second=3;
    frame=clock_display::render(state,pulse,pulse.at_us);
    output.reset(frame);
    state.gps_valid=false;
    auto changed=clock_display::render(state,pulse,pulse.at_us);
    sink.bytes.clear(); finish(output,changed);
    assert((sink.bytes==std::vector<uint8_t>{0x1b,0x48,23,'-'}));
    state.pps_present=false; state.pps_locked=false; state.utc_valid=false;
    changed=clock_display::render(state,pulse,pulse.at_us);
    sink.bytes.clear(); finish(output,changed);
    checkUpdates(sink.bytes);
    assert(sink.bytes.size()==5+6*4); // HH pair, four other UTC digits, decade '-', and PPS '-'.

    state.gps_valid=state.pps_present=state.pps_locked=state.utc_valid=true;
    frame=clock_display::render(state,pulse,pulse.at_us);
    output.reset(frame); sink.bytes.clear();
    // UART unavailable through multiple phases: never queue history.
    for(unsigned step=1;step<7;++step)
        output.service(clock_display::render(state,pulse,pulse.at_us+450000+step*50000),false);
    assert(sink.bytes.empty());
    changed=clock_display::render(state,pulse,pulse.at_us+777000);
    finish(output,changed);
    assert((sink.bytes==std::vector<uint8_t>{0x1b,0x48,16,'6'}));
    // A partial header delayed past several phases sends the current glyph.
    output.reset(frame); sink.bytes.clear();
    changed=clock_display::render(state,pulse,pulse.at_us+500000);
    output.service(changed,true); output.service(changed,true);
    finish(output,clock_display::render(state,pulse,pulse.at_us+900000));
    assert((sink.bytes==std::vector<uint8_t>{0x1b,0x48,16,'9'}));
    // The next PPS also replaces an in-flight old-second animation payload.
    output.reset(frame); sink.bytes.clear();
    for(unsigned i=0;i<3;++i) output.service(changed,true);
    pulse.at_us+=1000000;
    output.service(clock_display::render(state,pulse,pulse.at_us),true);
    // Position command already sent, but redundant '0' is omitted. Cursor is off.
    assert((sink.bytes==std::vector<uint8_t>{0x1b,0x48,16}));
    sink.bytes.clear();
    finish(output,clock_display::render(state,pulse,pulse.at_us+550000));
    assert((sink.bytes==std::vector<uint8_t>{0x1b,0x48,16,'2'}));
    // Hold .0 without traffic for 500 ms, then produce nine changes and hold .9.
    output.reset(clock_display::render(state,pulse,pulse.at_us)); sink.bytes.clear();
    for(unsigned ms=0;ms<1000;++ms) {
        finish(output,clock_display::render(state,pulse,pulse.at_us+ms*1000));
        const unsigned writes=ms<500 ? 0 : (ms<900 ? 1+(ms-500)/50 : 9);
        assert(sink.bytes.size()==writes*4);
    }
    checkUpdates(sink.bytes);
    assert(sink.bytes.size()==9*4);
    for(unsigned digit=1;digit<=9;++digit) assert(sink.bytes[(digit-1)*4+3]=='0'+digit);

    state.satellites.valid=true; state.satellites.used=8;
    frame=clock_display::render(state,pulse,pulse.at_us); output.reset(frame);
    state.satellites.used=9;
    auto sat_changed=clock_display::render(state,pulse,pulse.at_us);
    sink.bytes.clear(); finish(output,sat_changed);
    assert((sink.bytes==std::vector<uint8_t>{0x1b,0x48,37,'9'}));
    output.reset(sat_changed); state.satellites.used=12;
    sat_changed=clock_display::render(state,pulse,pulse.at_us);
    sink.bytes.clear(); finish(output,sat_changed);
    assert((sink.bytes==std::vector<uint8_t>{0x1b,0x48,36,'1',0x1b,0x48,37,'2'}));
    output.reset(sat_changed); state.satellites.valid=false;
    sat_changed=clock_display::render(state,pulse,pulse.at_us);
    sink.bytes.clear(); finish(output,sat_changed);
    assert((sink.bytes==std::vector<uint8_t>{0x1b,0x48,36,'-',0x1b,0x48,37,'-'}));

    // Keep and test the full-row utility, although normal operation never uses it.
    sink.bytes.clear();
    display.writeRow(0,frame.rows[0]); display.writeRow(1,frame.rows[1]);
    assert(sink.bytes.size()==46);
    for(unsigned row=0;row<2;++row)
        for(unsigned col=0;col<20;++col)
            assert(sink.bytes[row*23+3+col]==frame.rows[row][col]);
    char short_row[21]; for(auto& c:short_row)c='0'; short_row[0]='A'; short_row[1]='\0';
    sink.bytes.clear(); display.writeRow(0,short_row);
    assert(sink.bytes.size()==23 && sink.bytes[3]=='A');
    for(unsigned i=4;i<23;++i)assert(sink.bytes[i]==0x20);
    sink.bytes.clear(); display.writeField(0,18,"ABC",3);
    assert((sink.bytes==std::vector<uint8_t>{0x1b,0x48,18,'A','B'}));
    sink.bytes.clear(); display.writeField(1,0,"GPS",20);
    assert((sink.bytes==std::vector<uint8_t>{0x1b,0x48,20,'G','P','S'}));
    sink.bytes.clear();
    assert(!display.position(2,0)); display.writeChar(0,20,'X');
    display.writeField(0,0,nullptr,3); display.writeField(0,0,"",0);
    assert(sink.bytes.empty());
    display.clear(); assert((sink.bytes==std::vector<uint8_t>{0x0e,0x0c}));
    puts("All VFD command/output host tests passed");
}
