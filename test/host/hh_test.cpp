#include "clock_vfd.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>

// Streaming ESC H interpreter, independent of Output's cache and command
// boundaries. A position command needs no payload before another ESC H.
struct Wire : Print {
    clock_display::Frame screen;
    std::deque<uint8_t> fifo;
    unsigned phase=0, cursor=0, capacity=4;
    bool reject=false, drop_next_payload=false;
    size_t write(uint8_t b) override {
        if(reject || fifo.size()>=capacity) return 0;
        fifo.push_back(b); return 1;
    }
    void tick() {
        if(fifo.empty()) return;
        const auto b=fifo.front(); fifo.pop_front();
        if(phase==1) { assert(b==0x48); phase=2; }
        else if(phase==2) { assert(b<40); cursor=b; phase=0; }
        else if(b==0x1b) phase=1;
        else {
            assert(b>0x20 && b<=0x7e);
            if(drop_next_payload) drop_next_payload=false;
            else screen.rows[cursor/20][cursor%20]=b;
            cursor=(cursor+1)%40;
        }
    }
    void drain() { while(!fifo.empty()) tick(); }
};
clock_display::Frame frame(unsigned hour, unsigned activity=0) {
    clock_model::State s;
    nmea::Utc u; u.year=2026; u.month=9; u.day=25; u.hour=hour;
    u.minute=16; u.second=1;
    s.utc_seconds=clock_model::toUnix(u);
    s.utc=clock_model::fromUnix(s.utc_seconds);
    assert(s.utc.hour==hour && s.utc.hour<24);
    s.utc_valid=s.pps_locked=true;
    s.gps_valid=(activity%3)!=1; s.pps_present=(activity%5)!=1;
    s.satellites.valid=activity%7!=1; s.satellites.used=activity%100;
    clock_model::Pulse p; p.seen=true; p.at_us=1000000;
    return clock_display::render(s,p,p.at_us+(activity%20)*50000);
}
void settle(clock_display::Output& out, Wire& wire, const clock_display::Frame& f) {
    wire.reject=false;
    for(unsigned i=0;i<400;++i) { out.service(f,true); wire.tick(); }
    wire.drain();
    assert(wire.phase==0);
    assert(std::memcmp(wire.screen.rows,f.rows,sizeof(f.rows))==0);
    assert(std::memcmp(out.submitted().rows,f.rows,sizeof(f.rows))==0);
    assert(wire.screen.rows[0][7]==f.rows[0][7]);
    assert(wire.screen.rows[0][8]==f.rows[0][8]);
}
void transitions() {
    unsigned cases=0;
    // All hour pairs include requested sequential edges and every 1x/2x->0x.
    // Interrupt any HH/decade/status command at each of its four boundaries.
    for(unsigned from=0;from<24;++from) for(unsigned to=0;to<24;++to)
    for(unsigned cell : {7u,8u,16u,23u,29u,36u,37u})
    for(unsigned split=0;split<4;++split) {
        Wire w; w.capacity=1+split;
        clock_display::Output out(w);
        const auto start=frame(from); w.screen=start; out.reset(start);
        auto pending=start;
        auto& c=pending.rows[cell/20][cell%20]; c=c=='9'?'0':'9';
        for(unsigned i=0;i<split;++i) { out.service(pending,true); w.tick(); }
        for(unsigned i=0;i<320;++i) {
            const auto target=frame(to,i);
            // Long complete stall, single-byte FIFO, failed write despite
            // writable indication, and changing desired payload during retry.
            w.reject=i%11==0;
            out.service(target,i>=40 && i%7!=0);
            if(i>=80 && i%3==0) w.tick();
        }
        settle(out,w,frame(to,319)); ++cases;
    }
    // Repeated transitions without resetting either cache or rendered screen.
    Wire w; clock_display::Output out(w);
    auto f=frame(23); w.screen=f; out.reset(f);
    const unsigned hours[]={8,9,10,18,19,20,23,0,1};
    uint32_t random=0x12345678;
    for(unsigned n=0;n<10000;++n) {
        const unsigned h=hours[n%9];
        for(unsigned i=0;i<80;++i) {
            random=random*1664525u+1013904223u;
            f=frame(h,random%1000);
            w.reject=(random&15)==0;
            out.service(f,(random&7)!=0);
            if(random&1) w.tick();
        }
        settle(out,w,f);
    }
    std::printf("HH rendered-screen stress passed: %u split/interleaving cases, 10000 repeated transitions\n",cases);
}
void authoritativeEdges() {
    for(unsigned h=0;h<24;++h) {
        clock_model::Timebase clock;
        clock_model::Pulse pulse;
        nmea::Utc u; u.year=2026; u.month=9; u.day=25;
        u.hour=h; u.minute=59; u.second=57;
        const auto epoch=clock_model::toUnix(u);
        Wire w; clock_display::Output out(w);
        const auto initial=clock_display::render(clock.state());
        // Exercise actual startup fields through the same stream interpreter.
        w.capacity=128; pd2200::Display display(w);
        clock_display::writeInitialFields(display,initial); w.drain();
        out.reset(initial); w.capacity=2;
        for(unsigned edge=1;edge<=5;++edge) {
            pulse.seen=true; pulse.sequence=edge; pulse.at_us=edge*1000000;
            clock.poll(pulse,pulse.at_us);
            if(edge>=2) {
                clock_model::Reception rx;
                rx.usable=true; rx.sequence=edge; rx.start_us=pulse.at_us+100000;
                clock.receive(nmea::Result::valid_rmc,
                    clock_model::fromUnix(epoch+edge-2),'A',rx,pulse.at_us+200000);
            }
            for(unsigned ms=0;ms<1000;++ms) {
                clock.satelliteStatus().receive(nmea::GgaResult::valid_gga,
                    (ms/50)%32,pulse.at_us+ms*1000);
                const auto desired=clock_display::render(clock.state(),pulse,pulse.at_us+ms*1000);
                out.service(desired,ms%13!=0);
                if(ms%2==0) w.tick();
            }
            const auto desired=clock_display::render(clock.state(),pulse,pulse.at_us+999000);
            settle(out,w,desired);
            if(edge>=4) {
                const auto expected=edge==4 ? h : (h+1)%24;
                assert(clock.state().utc.hour==expected);
                assert(w.screen.rows[0][7]==static_cast<char>('0'+expected/10));
                assert(w.screen.rows[0][8]==static_cast<char>('0'+expected%10));
            }
        }
    }
    puts("All 24 authoritative PPS hour edges passed rendered-screen checks");
}
void lostPhysicalWrite() {
    // Fault injection demonstrates a limitation, NOT a reproduction with a
    // lossless UART: an accepted but physically lost leading-zero payload.
    Wire w; clock_display::Output out(w);
    const auto before=frame(20), after=frame(8);
    w.screen=before; out.reset(before); w.drop_next_payload=true;
    for(unsigned i=0;i<400;++i) { out.service(after,true); w.tick(); }
    assert(std::memcmp(w.screen.rows[0]+7,"28:16:01.0",10)==0);
    assert(std::memcmp(out.submitted().rows,after.rows,sizeof(after.rows))==0);
    for(unsigned i=0;i<1000;++i) {
        out.service(frame(8,i),true); w.tick();
        assert(w.screen.rows[0][7]=='2');
    }
    puts("Injected physical payload loss leaves 28 while cache/desired say 08 (expected limitation)");
}
int main() { transitions(); authoritativeEdges(); lostPhysicalWrite(); }
