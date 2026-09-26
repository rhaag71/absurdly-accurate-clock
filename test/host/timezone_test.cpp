#include "clock_vfd.hpp"
#include "zone_button.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using presentation::DisplayZone;
using presentation::convertUtcForDisplay;
nmea::Utc date(unsigned y,unsigned m,unsigned d,unsigned h,unsigned min=0,unsigned sec=0) {
    nmea::Utc u; u.year=y;u.month=m;u.day=d;u.hour=h;u.minute=min;u.second=sec;return u;
}
void same(const nmea::Utc& a,const nmea::Utc& b) {
    assert(a.year==b.year && a.month==b.month && a.day==b.day);
    assert(a.hour==b.hour && a.minute==b.minute && a.second==b.second);
}
void conversions() {
    const auto utc=date(2026,1,15,12,34,56);
    const auto unchanged=convertUtcForDisplay(utc,DisplayZone::utc);
    same(unchanged.civil,utc); assert(!unchanged.daylight && unchanged.offset_hours==0);
    assert(std::strcmp(unchanged.label,"UTC")==0);
    const char* std_labels[]={"EST","CST","MST","PST"};
    const char* dst_labels[]={"EDT","CDT","MDT","PDT"};
    for(unsigned i=0;i<4;++i) {
        const auto zone=static_cast<DisplayZone>(i+1);
        auto local=convertUtcForDisplay(utc,zone);
        same(local.civil,date(2026,1,15,7-i,34,56));
        assert(!local.daylight && local.offset_hours==-5-static_cast<int>(i));
        assert(std::strcmp(local.label,std_labels[i])==0);
        local=convertUtcForDisplay(date(2026,7,15,12,34,56),zone);
        same(local.civil,date(2026,7,15,8-i,34,56));
        assert(local.daylight && local.offset_hours==-4-static_cast<int>(i));
        assert(std::strcmp(local.label,dst_labels[i])==0);
        same(convertUtcForDisplay(date(2026,1,1,2,3,4),zone).civil,date(2025,12,31,21-i,3,4));
        same(convertUtcForDisplay(date(2024,3,1,2,3,4),zone).civil,date(2024,2,29,21-i,3,4));
        same(convertUtcForDisplay(date(2025,3,1,2,3,4),zone).civil,date(2025,2,28,21-i,3,4));
        same(convertUtcForDisplay(date(2026,7,15,2,3,4),zone).civil,date(2026,7,14,22-i,3,4));
        // Fixed independently calculated calendar dates and UTC transition hours:
        // 2024 March 10/November 3; 2025 March 9/November 2;
        // 2026 March 8/November 1; 2027 March 14/November 7.
        const unsigned years[]={2024,2025,2026,2027};
        const unsigned march[]={10,9,8,14}, november[]={3,2,1,7};
        for(unsigned y=0;y<4;++y) {
            const auto start=clock_model::toUnix(date(years[y],3,march[y],7+i));
            const auto end=clock_model::toUnix(date(years[y],11,november[y],6+i));
            for(int delta=-1;delta<=1;++delta) {
                local=convertUtcForDisplay(clock_model::fromUnix(start+delta),zone);
                assert(local.daylight==(delta>=0));
                assert(std::strcmp(local.label,delta<0 ? std_labels[i]:dst_labels[i])==0);
                same(local.civil,delta<0 ? date(years[y],3,march[y],1,59,59):
                                         date(years[y],3,march[y],3,0,delta));
                local=convertUtcForDisplay(clock_model::fromUnix(end+delta),zone);
                assert(local.daylight==(delta<0));
                assert(std::strcmp(local.label,delta<0 ? dst_labels[i]:std_labels[i])==0);
                same(local.civil,delta<0 ? date(years[y],11,november[y],1,59,59):
                                         date(years[y],11,november[y],1,0,delta));
            }
        }
    }
}
void buttons() {
    presentation::ZoneButton b;
    b.begin(false,0); assert(b.zone()==DisplayZone::utc);
    assert(!b.poll(false,30));
    // Press bounce, exact threshold, long hold, release bounce.
    assert(!b.poll(true,100)); assert(!b.poll(false,110));
    assert(!b.poll(true,115)); assert(!b.poll(true,144));
    assert(b.poll(true,145)); assert(b.zone()==DisplayZone::eastern);
    for(unsigned t=146;t<10000;++t) assert(!b.poll(true,t));
    assert(!b.poll(false,10000)); assert(!b.poll(true,10010));
    assert(!b.poll(false,10020)); assert(!b.poll(false,10049));
    assert(!b.poll(false,10050));
    uint32_t now=10100;
    for(const auto expected : {DisplayZone::central,DisplayZone::mountain,
                              DisplayZone::pacific,DisplayZone::utc}) {
        assert(!b.poll(true,now)); assert(b.poll(true,now+30));
        assert(b.zone()==expected);
        assert(!b.poll(false,now+100)); assert(!b.poll(false,now+130)); now+=200;
    }
    b.begin(true,0);
    assert(!b.poll(true,10000)); assert(b.zone()==DisplayZone::utc);
    assert(!b.poll(false,10001)); assert(!b.poll(false,10031));
    assert(!b.poll(true,10032)); assert(b.poll(true,10062));
    b.begin(false,UINT32_MAX-50);
    assert(!b.poll(false,UINT32_MAX-20));
    assert(!b.poll(true,UINT32_MAX-10)); assert(!b.poll(true,18));
    assert(b.poll(true,19)); // 30 ms across wrap.
}
struct Screen : Print {
    clock_display::Frame frame;
    unsigned phase=0,address=0;
    bool reject=false;
    std::vector<uint8_t> bytes;
    size_t write(uint8_t b) override {
        if(reject) return 0;
        bytes.push_back(b);
        if(phase==1) { assert(b==0x48);phase=2; }
        else if(phase==2) { assert(b<40);address=b;phase=0; }
        else if(b==0x1b) phase=1;
        else { assert(b>0x20 && b<=0x7e);frame.rows[address/20][address%20]=b;address=(address+1)%40; }
        return 1;
    }
};
void integration() {
    clock_model::State s; s.utc=date(2026,7,15,3,49,12);
    s.utc_seconds=clock_model::toUnix(s.utc);
    s.utc_valid=s.gps_valid=s.pps_present=s.pps_locked=true;
    s.satellites.valid=true;s.satellites.used=8;
    const auto epoch=s.utc_seconds; const auto utc=s.utc;
    clock_model::Pulse p;p.seen=true;p.at_us=1000000;
    Screen wire;clock_display::Output out(wire);
    auto initial=clock_display::render(s,p,p.at_us);
    pd2200::Display display(wire);clock_display::writeInitialFields(display,initial);out.reset(initial);
    const char* labels[]={"UTC","EDT","CDT","MDT","PDT","UTC"};
    const char* hours[]={"03","23","22","21","20","03"};
    for(unsigned step=0;step<6;++step) {
        const auto zone=static_cast<DisplayZone>(step%5);
        for(unsigned phase=0;phase<1000000;phase+=10000) {
            const auto f=clock_display::render(s,p,p.at_us+phase,zone);
            const auto base=clock_display::render(s,p,p.at_us+phase);
            assert(std::memcmp(f.rows[0]+3,labels[step],3)==0);
            assert(std::memcmp(f.rows[0]+7,hours[step],2)==0);
            assert(std::memcmp(f.rows[0]+9,base.rows[0]+9,12)==0);
            assert(std::memcmp(f.rows[1],base.rows[1],21)==0);
            for(unsigned tick=0;tick<160;++tick) {
                wire.reject=tick%7==0;
                const auto before=wire.bytes.size();
                clock_display::AcceptedCharacter accepted;
                accepted.valid=true; // Must be reset even on non-writable calls.
                out.service(f,tick%5!=0,&accepted);
                if(accepted.valid) {
                    assert(wire.bytes.size()==before+1 && wire.phase==0);
                    assert(wire.bytes.back()==accepted.payload);
                    assert(accepted.address==(wire.address+39)%40);
                    assert(out.submitted().rows[accepted.address/20][accepted.address%20]==accepted.payload);
                } else if(wire.reject || tick%5==0) assert(wire.bytes.size()==before);
            }
            assert(std::memcmp(wire.frame.rows,f.rows,sizeof(f.rows))==0);
            assert(s.utc_seconds==epoch); same(s.utc,utc);
        }
    }
    // Dynamic label/payload replacement in every partial-command phase.
    for(unsigned split=0;split<4;++split) {
        wire.reject=false; wire.phase=0;wire.frame=initial;out.reset(initial);
        const auto east=clock_display::render(s,p,p.at_us,DisplayZone::eastern);
        const auto west=clock_display::render(s,p,p.at_us,DisplayZone::pacific);
        for(unsigned i=0;i<split;++i) out.service(east,true);
        for(unsigned i=0;i<200;++i) out.service(west,true);
        assert(std::memcmp(wire.frame.rows,west.rows,sizeof(west.rows))==0);
    }
    // DST label/hour changes also converge through the real output abstraction.
    for(unsigned z=1;z<=4;++z) {
        const auto zone=static_cast<DisplayZone>(z);
        for(unsigned month : {3u,11u}) {
            const auto boundary=clock_model::toUnix(date(2026,month,month==3?8:1,
                                                       (month==3?6:5)+z));
            for(int delta=-1;delta<=1;++delta) {
                s.utc_seconds=boundary+delta;s.utc=clock_model::fromUnix(s.utc_seconds);
                const auto desired=clock_display::render(s,p,p.at_us,zone);
                const auto expected=convertUtcForDisplay(s.utc,zone);
                for(unsigned i=0;i<200;++i) out.service(desired,true);
                assert(std::memcmp(wire.frame.rows,desired.rows,sizeof(desired.rows))==0);
                assert(std::memcmp(wire.frame.rows[0]+3,expected.label,3)==0);
                assert(s.utc_seconds==boundary+delta);
                same(s.utc,clock_model::fromUnix(boundary+delta));
            }
        }
    }
    // Optional acceptance instrumentation must not change the transmitted stream,
    // including a canceled payload after a fully accepted position header.
    Screen silent, observed;
    clock_display::Output a(silent), b(observed);
    silent.frame=observed.frame=initial;a.reset(initial);b.reset(initial);
    auto changed=initial;changed.rows[0][7]='2';
    clock_display::AcceptedCharacter event;
    for(unsigned i=0;i<3;++i) {
        a.service(changed,true);b.service(changed,true,&event);assert(!event.valid);
    }
    a.service(initial,true);b.service(initial,true,&event);assert(!event.valid);
    for(unsigned i=0;i<1000;++i) {
        changed.rows[0][7]=static_cast<char>('0'+(i/17)%3);
        changed.rows[0][8]=static_cast<char>('0'+(i/11)%10);
        silent.reject=observed.reject=i%13==0;
        a.service(changed,i%7!=0);b.service(changed,i%7!=0,&event);
        assert(silent.bytes==observed.bytes);
        assert(std::memcmp(a.submitted().rows,b.submitted().rows,sizeof(initial.rows))==0);
    }
    // Invalid UTC must not enter calendar conversion, nor disturb lower row.
    s.utc_valid=false; s.utc={};
    for(unsigned z=0;z<5;++z) {
        const auto f=clock_display::render(s,p,p.at_us,static_cast<DisplayZone>(z));
        assert(std::memcmp(f.rows[0]+7,"--:--:--.-",10)==0);
        assert(std::memcmp(f.rows[1],initial.rows[1],21)==0);
        assert(f.rows[0][3]=="UECMP"[z]);
    }
}
int main() {
    conversions();buttons();integration();
    puts("All timezone/DST/button/display integration tests passed");
}
