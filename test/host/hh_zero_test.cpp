#include "clock_vfd.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

using clock_display::Frame;
// Independent streaming decoder. Parameters, including control-valued address
// 08, are consumed before interpreting standalone control/printable bytes.
struct Decoder {
    Frame screen;
    unsigned phase=0, cursor=0;
    bool force_d3=false, drop_zero=false;
    unsigned faults=0;
    bool feed(uint8_t byte) {
        if(phase==1) { assert(byte==0x48); phase=2; return false; }
        if(phase==2) { assert(byte<40 && byte!=8);cursor=byte;phase=0;return false; }
        if(byte==0x1b) { phase=1;return false; }
        assert(byte>0x20 && byte<=0x7e);
        if(cursor==8 && drop_zero && byte==0x30) ++faults;
        else {
            if(cursor==8 && force_d3) {byte|=0x08;++faults;}
            screen.rows[cursor/20][cursor%20]=byte;
        }
        cursor=(cursor+1)%40;
        return true;
    }
};
struct UART : Print {
    Decoder accepted, physical;
    std::deque<uint8_t> fifo;
    std::vector<uint8_t> bytes;
    unsigned capacity=32;
    bool reject=false;
    size_t write(uint8_t b) override {
        if(reject || fifo.size()>=capacity) return 0;
        bytes.push_back(b);accepted.feed(b);fifo.push_back(b);return 1;
    }
    void tick() { if(!fifo.empty()) {physical.feed(fifo.front());fifo.pop_front();} }
    void drain() {while(!fifo.empty())tick();}
};
Frame render(unsigned hour,bool valid=true,unsigned activity=0,
             presentation::DisplayZone zone=presentation::DisplayZone::utc) {
    clock_model::State state;
    nmea::Utc u;u.year=2026;u.month=9;u.day=25;u.hour=hour;u.minute=34;
    u.second=activity%60;
    state.utc_seconds=clock_model::toUnix(u);state.utc=u;
    state.utc_valid=state.pps_locked=valid;
    state.gps_valid=(activity%3)!=1;state.pps_present=valid;
    state.satellites.valid=activity%7!=1;state.satellites.used=activity%100;
    clock_model::Pulse pulse;pulse.seen=true;pulse.at_us=UINT32_MAX-100000;
    return clock_display::render(state,pulse,pulse.at_us+(activity%20)*50000,zone);
}
struct Rig {
    UART uart;
    clock_display::Output output;
    bool pair_pending=false;
    uint8_t frozen_ones=0;
    explicit Rig(const Frame& initial):output(uart) {
        output.reset(initial);uart.accepted.screen=uart.physical.screen=initial;
    }
    void step(const Frame& desired,bool writable=true) {
        clock_display::AcceptedCharacter event;
        const auto size=uart.bytes.size();
        const auto phase=uart.accepted.phase;
        const auto address=uart.accepted.cursor;
        const bool was_pending=pair_pending;
        output.service(desired,writable,&event);
        assert(uart.bytes.size()<=size+1);
        if(was_pending && uart.bytes.size()!=size) {
            assert(address==8 && uart.bytes.back()==frozen_ones);
        }
        if(event.valid) {
            assert(uart.bytes.size()==size+1 && phase==0);
            assert(event.address==address && event.payload==uart.bytes.back());
            if(event.hh_pair && event.address==8) {
                assert(pair_pending && event.complete && event.payload==frozen_ones);
                pair_pending=false;
            } else {
                assert(event.payload==static_cast<uint8_t>(desired.rows[address/20][address%20]));
                if(event.hh_pair) {
                    assert(event.address==7 && !event.complete && !pair_pending);
                    pair_pending=true;frozen_ones=event.hh[1];
                }
            }
        } else if(uart.bytes.size()!=size) {
            assert(phase!=0 || uart.bytes.back()==0x1b);
        }
        // Stronger than final convergence: the cache must always agree with
        // independently interpreting every byte the API has accepted so far.
        assert(std::memcmp(output.submitted().rows,uart.accepted.screen.rows,sizeof(Frame::rows))==0);
    }
    void settle(const Frame& target,bool physical_equal=true) {
        uart.reject=false;
        for(unsigned i=0;i<400;++i) {step(target);uart.tick();}
        uart.drain();
        assert(uart.accepted.phase==0 && uart.physical.phase==0);
        assert(std::memcmp(output.submitted().rows,target.rows,sizeof(target.rows))==0);
        if(physical_equal) assert(std::memcmp(uart.physical.screen.rows,target.rows,sizeof(target.rows))==0);
    }
};
void exactBytes() {
    for(unsigned hour : {0u,1u,10u,11u,20u,21u}) {
        const auto target=render(hour);
        auto before=target;before.rows[0][7]=before.rows[0][8]='-';
        Rig r(before);r.settle(target);
        const std::vector<uint8_t> expected={0x1b,0x48,0x07,
            static_cast<uint8_t>('0'+hour/10),
            static_cast<uint8_t>('0'+hour%10)};
        assert(r.uart.bytes==expected);
        std::printf("HH %02u:",hour);
        for(auto b:r.uart.bytes)std::printf(" %02X",b);
        std::puts("");
    }
    // The specific 01->00 selection emits one contiguous pair from 07.
    Rig r(render(1));r.settle(render(0));
    assert((r.uart.bytes==std::vector<uint8_t>{0x1b,0x48,0x07,0x30,0x30}));
}
void lossReacquisition() {
    unsigned cases=0;
    for(unsigned hour : {0u,1u,10u,11u,20u,21u})
    for(unsigned cell : {3u,4u,5u,7u,8u,10u,11u,13u,14u,16u,23u,29u,36u,37u})
    for(unsigned split=0;split<4;++split)
    for(unsigned capacity : {1u,4u,32u}) {
        auto valid=render(hour,true,37); // activity includes an indicator '8'.
        Rig r(valid);r.uart.capacity=capacity;
        auto pending=valid;pending.rows[cell/20][cell%20]='?';
        for(unsigned i=0;i<split;++i) {r.step(pending);r.uart.tick();}
        // Loss during another cell's header, long transport gap, then fully --.
        auto lost=render(hour,false,39);
        for(unsigned i=0;i<100;++i)r.step(lost,false);
        r.settle(lost);
        assert(std::memcmp(r.uart.physical.screen.rows[0]+7,"--",2)==0);
        // -- -> x0/x1, with failures even when service sees writable.
        for(unsigned i=0;i<300;++i) {
            r.uart.reject=i%11==0;
            r.step(render(hour,true,i),i%7!=0);
            if(i%3==0)r.uart.tick();
        }
        r.settle(valid);
        assert(r.uart.physical.screen.rows[0][8]==static_cast<char>('0'+hour%10));
        ++cases;
    }
    std::printf("HH zero: %u partial-command loss/reacquisition cases passed\n",cases);
    // Hardware examples: the condition follows the civil hour across zones.
    const auto central=presentation::DisplayZone::central;
    const auto mountain=presentation::DisplayZone::mountain;
    const auto pacific=presentation::DisplayZone::pacific;
    Rig r(render(5,true,0,central));
    for(unsigned repeat=0;repeat<100;++repeat) {
        for(auto zone : {central,mountain,pacific}) {
            const unsigned utc=zone==central?5:zone==mountain?6:17;
            r.settle(render(utc,false,repeat,zone));
            r.settle(render(utc,true,repeat,zone));
            assert(std::memcmp(r.uart.physical.screen.rows[0]+7,zone==pacific?"10":"00",2)==0);
        }
    }
}
void fuzz() {
    Rig r(render(0,false));
    uint32_t random=0xd3300830;
    auto next=[&]() {random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    const unsigned hours[]={0,1,10,11,20,21,5,6,17};
    for(unsigned i=0;i<500000;++i) {
        const auto bits=next();
        const auto desired=render(hours[(bits>>8)%9],(bits&3)!=0,bits%1000,
                                  static_cast<presentation::DisplayZone>((bits>>16)%5));
        r.uart.capacity=1+(bits>>24)%32;
        r.uart.reject=(bits&31)==0;
        r.step(desired,(bits&7)!=0);
        if(bits&8)r.uart.tick();
        if(i%251==0)r.settle(desired);
    }
    r.settle(render(0));
    puts("HH zero: 500000 seeded randomized service calls passed");
}
void injectedFaults() {
    // Deliberate downstream faults only: these are NOT lossless reproductions.
    for(unsigned hour : {0u,10u,20u}) {
        const auto target=render(hour);
        auto before=target;before.rows[0][7]=before.rows[0][8]='-';
        Rig lost(before);lost.uart.physical.drop_zero=true;lost.settle(target,false);
        assert(lost.uart.physical.screen.rows[0][8]=='-'); // Loss alone is not '8'.
        Rig bit(before);bit.uart.physical.force_d3=true;bit.settle(target,false);
        assert(bit.uart.physical.screen.rows[0][8]=='8');
        assert(bit.output.submitted().rows[0][8]=='0');
        assert(bit.uart.physical.faults==1);
        // A blanket D3-high/address-OR hypothesis also predicts 1 -> 9.
        bit.settle(render(hour+1),false);
        assert(bit.uart.physical.screen.rows[0][8]=='9');
    }
    puts("Injected faults only: dropped 30 leaves '-', D3-high gives x8 and predicts x1->x9");
}
int main() {exactBytes();lossReacquisition();fuzz();injectedFaults();}
