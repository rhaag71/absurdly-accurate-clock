#include "clock_vfd.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using clock_display::Frame;
struct Sink : Print {
    std::vector<uint8_t> bytes;
    bool reject=false;
    size_t write(uint8_t b) override {if(reject)return 0;bytes.push_back(b);return 1;}
};
Frame hh(const char* digits) {
    Frame f;f.rows[0][7]=digits[0];f.rows[0][8]=digits[1];return f;
}
std::vector<uint8_t> pair(const char* digits) {
    return {0x1b,0x48,7,static_cast<uint8_t>(digits[0]),static_cast<uint8_t>(digits[1])};
}
void finish(clock_display::Output& out,const Frame& f) {
    for(unsigned i=0;i<200;++i)out.service(f,true);
    assert(std::memcmp(out.submitted().rows,f.rows,sizeof(f.rows))==0);
}
void transitions() {
    const char* from[]={"01","11","23","00","10","--","--"};
    const char* to[]  ={"00","10","00","--","--","00","10"};
    for(unsigned i=0;i<7;++i) {
        Sink sink;clock_display::Output out(sink);out.reset(hh(from[i]));
        finish(out,hh(to[i]));assert(sink.bytes==pair(to[i]));
    }
    // Full renderer timezone changes, not hand-formatted hours.
    for(unsigned utc : {5u,17u}) {
        clock_model::State s;s.utc.year=2026;s.utc.month=9;s.utc.day=25;s.utc.hour=utc;
        s.utc_valid=true;s.utc_seconds=clock_model::toUnix(s.utc);
        const auto before=clock_display::render(s);
        const auto zone=utc==5 ? presentation::DisplayZone::central : presentation::DisplayZone::pacific;
        const auto after=clock_display::render(s,{},0,zone);
        Sink sink;clock_display::Output out(sink);out.reset(before);finish(out,after);
        std::vector<uint8_t> expected=pair(utc==5 ? "00":"10");
        // Only label cells precede the final HH field.
        assert(sink.bytes.size()>=5);
        assert(std::vector<uint8_t>(sink.bytes.end()-5,sink.bytes.end())==expected);
        assert(s.utc.hour==utc);
    }
    // Boot's existing longer time field already positions at 07 and writes HH
    // contiguously; it must not introduce a separate 08 command.
    clock_model::State s;const auto initial=clock_display::render(s);
    Sink sink;pd2200::Display display(sink);clock_display::writeInitialFields(display,initial);
    assert(sink.bytes[6]==0x1b && sink.bytes[7]==0x48 && sink.bytes[8]==7);
    assert(sink.bytes[9]=='-' && sink.bytes[10]=='-');
}
void interrupted() {
    // Refresh or freeze tested at every byte boundary, including before ESC,
    // after each header byte, between digits, and after completion.
    for(unsigned split=0;split<=5;++split) {
        Sink sink;clock_display::Output out(sink);out.reset(hh("--"));
        auto desired=hh("10");
        clock_display::AcceptedCharacter event;
        for(unsigned i=0;i<split;++i)out.service(desired,true,&event);
        const auto saved=out.submitted();
        desired=hh("01");
        // Competing zone/indicator/status writes must not interrupt HH.
        desired.rows[0][3]='E';desired.rows[0][16]='8';desired.rows[1][3]='+';
        for(unsigned i=0;i<10;++i)out.service(desired,false,&event);
        assert(sink.bytes.size()==split && !event.valid);
        sink.reject=true;
        for(unsigned i=0;i<10;++i)out.service(desired,true,&event);
        assert(sink.bytes.size()==split && !event.valid);
        assert(std::memcmp(saved.rows,out.submitted().rows,sizeof(saved.rows))==0);
        sink.reject=false;finish(out,desired);
        // Skip complete ordinary commands; all HH commands must be whole pairs.
        std::vector<std::vector<uint8_t>> pairs;
        for(size_t i=0;i<sink.bytes.size();) {
            assert(sink.bytes[i]==0x1b && sink.bytes[i+1]==0x48);
            const auto address=sink.bytes[i+2];assert(address!=8);
            const size_t length=address==7 ? 5:4;
            assert(i+length<=sink.bytes.size());
            if(address==7)pairs.emplace_back(sink.bytes.begin()+i,sink.bytes.begin()+i+length);
            i+=length;
        }
        assert(pairs.back()==pair("01"));
        if(split<4)assert(pairs.size()==1);
        else {assert(pairs.size()==2);assert(pairs.front()==pair("10"));}
    }
    // Payload rejection at both digit boundaries; cache and completion records
    // reflect accepted bytes, not the requested newer hour.
    Sink sink;clock_display::Output out(sink);out.reset(hh("--"));
    clock_display::AcceptedCharacter e;
    for(unsigned i=0;i<3;++i)out.service(hh("10"),true,&e);
    sink.reject=true;out.service(hh("10"),true,&e);assert(!e.valid);
    sink.reject=false;out.service(hh("20"),true,&e);
    assert(e.valid && e.hh_pair && !e.complete && e.address==7 && e.payload=='2');
    assert(std::memcmp(out.submitted().rows[0]+7,"2-",2)==0);
    sink.reject=true;out.service(hh("01"),true,&e);assert(!e.valid);
    sink.reject=false;out.service(hh("01"),true,&e);
    assert(e.valid && e.hh_pair && e.complete && e.address==8 && e.payload=='0');
    assert(e.hh[0]=='2' && e.hh[1]=='0');
    assert(std::memcmp(out.submitted().rows[0]+7,"20",2)==0);
    assert(sink.bytes==pair("20"));
    finish(out,hh("01"));
    auto expected=pair("20"), second=pair("01");expected.insert(expected.end(),second.begin(),second.end());
    assert(sink.bytes==expected);
}
int main() {transitions();interrupted();puts("All contiguous HH pair/cache/backpressure tests passed");}
