#include "pd2200.hpp"
#include "clock_display.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
struct Sink : Print {
    std::vector<uint8_t> bytes;
    size_t write(uint8_t value) override { bytes.push_back(value); return 1; }
};
int main() {
    Sink sink; pd2200::Display display(sink);
    clock_model::State state;
    state.utc_valid=state.gps_valid=state.pps_present=state.pps_locked=true;
    state.utc.hour=3; state.utc.minute=1;
    for(unsigned second=0; second<60; ++second) {
        state.utc.second=second;
        const auto before=clock_display::render(state);
        state.utc.second=(second+1)%60;
        state.utc.minute=second==59 ? 2 : 1;
        const auto after=clock_display::render(state);
        const auto changes=clock_display::difference(before,after);
        sink.bytes.clear();
        for(uint8_t column=4; column<12; ++column)
            if(changes.time_positions & (1u<<column)) display.writeChar(0,column,after.rows[0][column]);
        assert(sink.bytes.size()==(second==59 ? 12u : second%10==9 ? 8u : 4u));
        for(size_t offset=0; offset<sink.bytes.size(); offset+=4) {
            assert(sink.bytes[offset]==0x1b && sink.bytes[offset+1]==0x48);
            const auto column=sink.bytes[offset+2];
            assert(column>=4 && column<=11);
            assert(sink.bytes[offset+3]==after.rows[0][column]);
            assert(before.rows[0][column]!=after.rows[0][column]);
        }
    }
    sink.bytes.clear();
    const auto frame=clock_display::render(state);
    display.writeRow(0,frame.rows[0]); display.writeRow(1,frame.rows[1]);
    assert(sink.bytes.size()==46);
    assert(sink.bytes[2]==0 && sink.bytes[25]==20);
    for(size_t i=0;i<20;++i) assert(sink.bytes[26+i]==frame.rows[1][i]);
    display.writeChar(2,0,'X'); display.writeChar(0,20,'X');
    assert(sink.bytes.size()==46);
    puts("All VFD command tests passed");
}
