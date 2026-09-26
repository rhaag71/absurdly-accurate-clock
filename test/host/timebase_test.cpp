#include "clock_state.hpp"
#include "clock_display.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
using namespace clock_model;
using nmea::Result;

nmea::Utc date(unsigned y, unsigned m, unsigned d, unsigned h, unsigned min, unsigned s) {
    nmea::Utc u;
    u.year=y; u.month=m; u.day=d; u.hour=h; u.minute=min; u.second=s;
    return u;
}
struct Rig {
    Timebase clock;
    Pulse pulse;
    uint32_t now = 0;
    void edge() {
        now += 1000000;
        ++pulse.sequence; pulse.at_us=now; pulse.seen=true;
        clock.poll(pulse, now);
    }
    void label(const nmea::Utc& utc, unsigned offset = 200000) {
        Reception rx;
        rx.sequence=pulse.sequence; rx.start_us=now+100000; rx.usable=true;
        clock.receive(Result::valid_rmc, utc, 'A', rx, now+offset);
    }
    void acquire(const nmea::Utc& utc) {
        edge(); // Establish pulse stream.
        edge(); label(utc);
        assert(clock.state().gps_valid && !clock.state().pps_locked && !clock.state().utc_valid);
        edge(); label(fromUnix(toUnix(utc)+1));
        assert(!clock.state().pps_locked); // Two labels alone never change display time.
        edge();
        assert(clock.state().pps_locked && clock.state().utc_valid);
        assert(clock.state().utc_seconds == toUnix(utc)+2);
    }
};
void rollovers() {
    const auto initial = date(2026,9,23,3,0,58);
    Rig rig; rig.acquire(initial); // 03:01:00
    for (unsigned i=0; i<130; ++i) {
        const auto before = rig.clock.state();
        const auto frame = clock_display::render(before);
        rig.label(before.utc);
        assert(rig.clock.state().utc_seconds == before.utc_seconds);
        rig.edge();
        const auto after=rig.clock.state();
        assert(after.utc_seconds == before.utc_seconds+1);
        const auto next=clock_display::render(after);
        const auto diff=clock_display::difference(frame,next);
        assert(diff.positions[0] & (1u<<14));
        assert(bool(diff.positions[0] & (1u<<13)) == (before.utc.second%10==9));
        if (before.utc.second%10 != 9) assert(diff.positions[0] == (1u<<14));
        if (before.utc.second==59) assert(diff.positions[0] & (1u<<11));
        assert(std::strlen(next.rows[0])==20 && std::strlen(next.rows[1])==20);
    }
    for (const auto& u : {date(2026,9,23,3,59,57), date(2026,9,30,23,59,57),
                           date(2026,12,31,23,59,57),date(2024,2,28,23,59,57),
                           date(2024,2,29,23,59,57),date(2025,2,28,23,59,57)}) {
        Rig r; r.acquire(u); r.label(r.clock.state().utc); r.edge();
        const auto& v=r.clock.state().utc;
        assert(v.second==0 && v.minute==0);
        if(u.hour==23) { assert(v.hour==0); assert(v.day!=u.day); }
        else assert(v.hour==4);
        assert(toUnix(v)==toUnix(u)+3);
    }
    assert(fromUnix(toUnix(date(2024,2,28,23,59,59))+1).day==29);
    assert(fromUnix(toUnix(date(2025,2,28,23,59,59))+1).month==3);
    assert(fromUnix(toUnix(date(2026,12,31,23,59,59))+1).year==2027);
}
void association() {
    auto u=date(2026,9,23,3,1,0);
    Rig r; r.edge(); r.label(u); r.edge();
    assert(!r.clock.state().pps_locked); // No qualified cadence at first RMC.
    r.label(u); r.edge(); r.label(u); r.edge();
    assert(!r.clock.state().pps_locked); // Repeated/frozen time is not a mapping.
    r.label(fromUnix(toUnix(u)+1)); r.edge();
    assert(r.clock.state().pps_locked);
    const auto old=r.clock.state().utc_seconds;
    r.label(fromUnix(old+10));
    assert(!r.clock.state().pps_locked && r.clock.state().utc_seconds==old);
    r.edge(); r.label(fromUnix(old+11));
    assert(!r.clock.state().pps_locked);
    r.edge(); assert(r.clock.state().utc_seconds==old+12 && r.clock.state().pps_locked);

    Rig late; late.edge(); late.edge(); late.label(u,950000); late.edge();
    late.label(fromUnix(toUnix(u)+1),950000); late.edge();
    assert(late.clock.state().gps_valid && !late.clock.state().pps_locked);
    Reception crossed; crossed.sequence=late.pulse.sequence-1;
    crossed.start_us=late.now-100000; crossed.usable=true;
    late.clock.receive(Result::valid_rmc,u,'A',crossed,late.now+100000);
    late.edge(); assert(!late.clock.state().pps_locked);
    u.fractional=true;
    late.label(u); late.edge(); late.label(u); late.edge();
    assert(!late.clock.state().pps_locked);

    Rig early; early.edge(); early.edge();
    Reception near_edge; near_edge.sequence=early.pulse.sequence;
    near_edge.start_us=early.now+10000; near_edge.usable=true;
    early.clock.receive(Result::valid_rmc,date(2026,9,23,3,1,0),'A',near_edge,early.now+200000);
    early.edge(); early.label(date(2026,9,23,3,1,1)); early.edge();
    assert(!early.clock.state().pps_locked);

    Rig no_pps;
    Reception rx;
    no_pps.clock.receive(Result::valid_rmc,u,'A',rx,100000);
    assert(no_pps.clock.state().gps_valid && !no_pps.clock.state().pps_present && !no_pps.clock.state().utc_valid);
}
void losses() {
    Rig r; r.acquire(date(2026,9,23,3,1,0));
    const auto saved=r.clock.state().utc_seconds;
    r.clock.poll(r.pulse,r.now+Timebase::pps_timeout_us);
    assert(!r.clock.state().pps_present && !r.clock.state().pps_locked && !r.clock.state().utc_valid);
    assert(r.clock.state().gps_valid && r.clock.state().utc_seconds==saved);
    // A stale edge must not reappear after the 32-bit timer wraps.
    r.clock.poll(r.pulse,r.pulse.at_us+1);
    assert(!r.clock.state().pps_present);
    r.now+=2000000; r.edge(); r.label(date(2026,9,23,3,1,5));
    assert(!r.clock.state().pps_locked);
    r.edge(); r.label(date(2026,9,23,3,1,6));
    r.edge(); r.label(date(2026,9,23,3,1,7)); r.edge();
    assert(r.clock.state().pps_locked);
    r.edge(); r.edge(); r.edge();
    assert(r.clock.state().pps_present && !r.clock.state().gps_valid && !r.clock.state().pps_locked);

    Rig unusable; unusable.acquire(date(2026,9,23,3,1,0));
    for (unsigned i=0; i<4; ++i) {
        unusable.label(fromUnix(unusable.clock.state().utc_seconds),950000);
        unusable.edge();
    }
    assert(unusable.clock.state().gps_valid && unusable.clock.state().pps_present &&
           !unusable.clock.state().pps_locked);

    Rig invalid; invalid.acquire(date(2026,9,23,3,1,0));
    invalid.clock.receive(Result::invalid_rmc,{},'V',{},invalid.now+200000);
    assert(!invalid.clock.state().gps_valid && !invalid.clock.state().pps_locked);
    Rig skipped; skipped.acquire(date(2026,9,23,3,1,0));
    skipped.pulse.sequence+=2; skipped.now+=2000000; skipped.pulse.at_us=skipped.now;
    skipped.clock.poll(skipped.pulse,skipped.now);
    assert(!skipped.clock.state().pps_locked);
    Rig glitch; glitch.acquire(date(2026,9,23,3,1,0));
    ++glitch.pulse.sequence; glitch.pulse.at_us+=10000;
    glitch.clock.poll(glitch.pulse,glitch.pulse.at_us);
    assert(!glitch.clock.state().pps_locked);
    Rig stalled; stalled.acquire(date(2026,9,23,3,1,0));
    stalled.clock.discardAssociation(); assert(!stalled.clock.state().pps_locked);
    Rig wrap; wrap.now=UINT32_MAX-2500000; wrap.pulse.sequence=UINT32_MAX-2;
    wrap.acquire(date(2026,9,23,3,1,0));
    assert(wrap.clock.state().pps_locked);
}
std::string sentence(const std::string& body) {
    unsigned sum=0; for(char c:body) sum^=c;
    char end[8]; snprintf(end,sizeof(end),"*%02X\r\n",sum);
    return "$"+body+end;
}
void parserTests() {
    nmea::RmcParser parser;
    nmea::Utc utc; char status='?';
    auto feed=[&](const std::string& text) {
        auto result=Result::none;
        for(char c:text) {auto next=parser.receive(c,utc,status); if(next!=Result::none) result=next;}
        return result;
    };
    assert(feed(sentence("GPRMC,030127.00,A,,,,,,,230926"))==Result::valid_rmc);
    assert(!utc.fractional && utc.second==27 && utc.year==2026);
    assert(feed(sentence("GPRMC,030127.001,A,,,,,,,230926"))==Result::valid_rmc && utc.fractional);
    assert(feed(sentence("GNRMC,030127,A,,,,,,,230926"))==Result::none);
    assert(feed(sentence("GPRMC,030127,V,,,,,,,230926"))==Result::invalid_rmc);
    assert(feed(sentence("GPRMC,030127,A,,,,,,,290226"))==Result::invalid_rmc);
    assert(feed("$GPRMC,030127,A,,,,,,,230926*ZZ\r\n")==Result::none);
    auto bad=sentence("GPRMC,030127,A,,,,,,,230926"); bad[8]='9';
    assert(feed(bad)==Result::none);
    assert(feed("$"+std::string(200,'x')+"\n")==Result::none);
    assert(feed("$partial"+sentence("GPRMC,030127,A,,,,,,,230926"))==Result::valid_rmc);
}
void hourRangeTests() {
    // Every representable two-digit RMC hour, including observed 28 and 84.
    for(unsigned h=0;h<100;++h) {
        char body[80]; snprintf(body,sizeof(body),"GPRMC,%02u1601,A,,,,,,,250926",h);
        nmea::RmcParser parser; nmea::Utc utc; char status='?';
        auto result=Result::none;
        for(char c:sentence(body)) {
            const auto r=parser.receive(c,utc,status);
            if(r!=Result::none) result=r;
        }
        assert(result==(h<24 ? Result::valid_rmc : Result::invalid_rmc));
        if(h<24) assert(utc.hour==h);
    }
    // All hour edges through real association and authoritative epoch advance.
    for(unsigned h=0;h<24;++h) {
        Rig r; r.acquire(date(2026,9,25,h,59,57));
        assert(r.clock.state().utc.hour==h);
        r.label(r.clock.state().utc); r.edge();
        assert(r.clock.state().utc.hour==(h+1)%24);
        const auto f=clock_display::render(r.clock.state());
        assert(f.rows[0][7]==static_cast<char>('0'+((h+1)%24)/10));
        assert(f.rows[0][8]==static_cast<char>('0'+((h+1)%24)%10));
    }
    // Every day/hour of the parser's 2000..2099 century; date round trips.
    const auto end=toUnix(date(2100,1,1,0,0,0));
    for(auto t=toUnix(date(2000,1,1,0,0,0));t<end;t+=3600) {
        const auto u=fromUnix(t);
        assert(u.hour<24 && u.minute==0 && u.second==0);
        assert(toUnix(u)==t);
    }
    // Public helper has no negative-epoch contract/guard: signed remainder
    // narrows to uint8_t. This is unreachable from accepted RMC dates.
    assert(fromUnix(-3600).hour==255);
}
void satelliteTests() {
    nmea::GgaParser parser;
    uint8_t count=0;
    auto feed=[&](const std::string& text) {
        auto result=nmea::GgaResult::none;
        for(char c:text) { auto next=parser.receive(c,count); if(next!=nmea::GgaResult::none) result=next; }
        return result;
    };
    assert(feed(sentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"))==nmea::GgaResult::valid_gga && count==8);
    assert(feed(sentence("GNGGA,123520,,,,,1,12,1.0,0,M,0,M,,"))==nmea::GgaResult::valid_gga && count==12);
    assert(feed(sentence("GPGGA,123521,,,,,1,09,1.0,0,M,0,M,,"))==nmea::GgaResult::valid_gga && count==9);
    assert(feed(sentence("GPGGA,123522,,,,,0,00,1.0,0,M,0,M,,"))==nmea::GgaResult::invalid_gga);
    assert(feed(sentence("GPGGA,123522,,,,,1,xx,1.0,0,M,0,M,,"))==nmea::GgaResult::invalid_gga);
    assert(feed(sentence("GPGGA,123522,,,,,1,,1.0,0,M,0,M,,"))==nmea::GgaResult::invalid_gga);
    auto bad=sentence("GPGGA,123519,,,,,1,08,1.0,0,M,0,M,,"); bad[8]='9';
    assert(feed(bad)==nmea::GgaResult::none);

    clock_model::SatelliteStatus status;
    status.receive(nmea::GgaResult::valid_gga,8,1000);
    assert(status.valid && status.used==8);
    status.poll(1000+clock_model::SatelliteStatus::stale_after_us-1); assert(status.valid);
    status.poll(1000+clock_model::SatelliteStatus::stale_after_us); assert(!status.valid);
    status.receive(nmea::GgaResult::valid_gga,12,5000000); status.poll(5000000);
    status.receive(nmea::GgaResult::none,0,5000001); assert(status.valid && status.used==12);
    status.receive(nmea::GgaResult::invalid_gga,0,5000002); assert(!status.valid);

    Rig rig; rig.acquire(date(2026,9,23,3,1,0));
    const int64_t before=rig.clock.state().utc_seconds;
    const bool locked=rig.clock.state().pps_locked;
    status.receive(nmea::GgaResult::valid_gga,8,rig.now);
    assert(rig.clock.state().utc_seconds==before && rig.clock.state().pps_locked==locked);
}
void displayTests() {
    State s;
    auto f=clock_display::render(s);
    assert(std::strcmp(f.rows[0],"   UTC --:--:--.-   ")==0);
    assert(std::strcmp(f.rows[1],"GPS-  PPS-  SAT --  ")==0);
    s.utc=date(2026,9,23,3,1,27); s.utc_valid=s.gps_valid=s.pps_present=s.pps_locked=true;
    Pulse pulse; pulse.seen=true; pulse.at_us=1000000;
    auto next=clock_display::render(s,pulse,pulse.at_us);
    assert(std::strcmp(next.rows[0],"   UTC 03:01:27.0   ")==0);
    assert(std::strcmp(next.rows[1],"GPS+  PPS+  SAT --  ")==0);
    s.satellites.valid=true; s.satellites.used=8;
    const auto with_sat=clock_display::render(s,pulse,pulse.at_us);
    assert(std::strcmp(with_sat.rows[1],"GPS+  PPS+  SAT 08  ")==0);
    assert(with_sat.rows[1][16]=='0' && with_sat.rows[1][17]=='8');
    const auto same=clock_display::difference(next,next);
    assert(!same.positions[0] && !same.positions[1]);
    const auto changed=clock_display::difference(f,next);
    assert(changed.positions[1]==((1u<<3)|(1u<<9)));
}
void paddingTests() {
    using namespace clock_display;
    static_assert(columns == 20, "Physical width must remain 20");
    static_assert(decade_row == 0 && decade_column == 16,
                  "Physical column 17 is the rolling decade indicator");
    const Frame blank;
    for (const auto& row : blank.rows) {
        for(unsigned col=0;col<20;++col) assert(row[col]==0x20);
        assert(row[20]=='\0');
    }
    Pulse pulse; pulse.seen=true; pulse.at_us=1000000;
    for(unsigned flags=0;flags<16;++flags) {
        State state;
        state.utc_valid=flags&1; state.gps_valid=flags&2;
        state.pps_present=flags&4; state.pps_locked=flags&8;
        for(unsigned second=0;second<60;++second) {
            state.utc=date(2026,9,23,3,1,second);
            const auto frame=render(state,pulse,pulse.at_us);
            for(unsigned row=0;row<2;++row) {
                assert(std::strlen(frame.rows[row])==20 && frame.rows[row][20]=='\0');
                for(unsigned col=0;col<20;++col) {
                    const auto value=frame.rows[row][col];
                    assert(value>=0x20 && value<=0x7e);
                    const bool digit=row==0 && (col==7 || col==8 || col==10 || col==11 || col==13 || col==14 || col==16);
                    if(!digit) assert(value!='0');
                    const bool padding=row==0 ? (col<3 || col>=17) : (col==4 || col==5 || col==10 || col==11 || col==15 || col>=18);
                    if(padding) assert(value==0x20);
                }
            }
        }
    }
}
void decadeTests() {
    using namespace clock_display;
    Rig rig; rig.acquire(date(2026,9,23,3,48,1)); // Authoritative 03:48:03
    const auto epoch=rig.clock.state().utc_seconds;
    const auto base=render(rig.clock.state(),rig.pulse,rig.now);
    assert(std::strcmp(base.rows[0],"   UTC 03:48:03.0   ")==0);
    const unsigned starts[]={0,500000,550000,600000,650000,700000,750000,800000,850000,900000};
    const unsigned ends[]={499999,549999,599999,649999,699999,749999,799999,849999,899999,999999};
    for(unsigned digit=0;digit<10;++digit) {
        for(unsigned phase : {starts[digit],ends[digit]}) {
            const auto frame=render(rig.clock.state(),rig.pulse,rig.now+phase);
            assert(frame.rows[0][16]==static_cast<char>('0'+digit));
            assert(std::memcmp(base.rows[0],frame.rows[0],16)==0);
            assert(rig.clock.state().utc_seconds==epoch);
        }
    }
    // Explicit requested millisecond checkpoints, in addition to microsecond edges.
    const unsigned milliseconds[]={0,499,500,549,550,900,999};
    const char expected[]={'0','0','1','1','2','9','9'};
    for(unsigned i=0;i<sizeof(expected);++i)
        assert(rollingDecade(rig.clock.state(),rig.pulse,rig.now+milliseconds[i]*1000)==expected[i]);
    for(unsigned phase : {1000000u,1499999u})
        assert(rollingDecade(rig.clock.state(),rig.pulse,rig.now+phase)=='9');
    assert(rollingDecade(rig.clock.state(),rig.pulse,rig.now+1500000)=='-');
    // A missed series of loop calls jumps to the current phase, without catch-up.
    assert(rollingDecade(rig.clock.state(),rig.pulse,rig.now+1000)=='0');
    assert(rollingDecade(rig.clock.state(),rig.pulse,rig.now+327000)=='0');
    assert(rollingDecade(rig.clock.state(),rig.pulse,rig.now+710000)=='5');
    rig.label(rig.clock.state().utc,140000); // NMEA arrival must not restart phase.
    assert(rollingDecade(rig.clock.state(),rig.pulse,rig.now+140000)=='0');
    rig.edge();
    assert(rig.clock.state().utc_seconds==epoch+1);
    assert(rollingDecade(rig.clock.state(),rig.pulse,rig.now)=='0');
    const auto next=render(rig.clock.state(),rig.pulse,rig.now);
    assert(std::memcmp(next.rows[0]+7,"03:48:04.0",10)==0);
    // Timestamp wrap does not shift phase.
    Pulse wrapped=rig.pulse; wrapped.at_us=UINT32_MAX-25000;
    assert(rollingDecade(rig.clock.state(),wrapped,wrapped.at_us+500000)=='1');
    auto invalid=rig.clock.state(); invalid.pps_locked=false;
    assert(rollingDecade(invalid,rig.pulse,rig.now)=='-');
    invalid=rig.clock.state(); invalid.utc_valid=false;
    assert(rollingDecade(invalid,rig.pulse,rig.now)=='-');
    invalid=rig.clock.state(); invalid.pps_present=false;
    assert(rollingDecade(invalid,rig.pulse,rig.now)=='-');
    assert(rollingDecade(rig.clock.state(),{},rig.now)=='-');
}
int main() {
    parserTests(); hourRangeTests(); satelliteTests(); rollovers(); association(); losses(); displayTests(); paddingTests(); decadeTests();
    puts("All clock/parser/display host tests passed");
}
