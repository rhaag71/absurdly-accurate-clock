#include "network_protocol.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace clock_network;
void serialization() {
    assert(crc32(reinterpret_cast<const uint8_t*>("123456789"),9)==0xcbf43926);
    Snapshot s;s.sequence=0x12345678;s.boundary=0x01020304;s.epoch=0x100000002LL;
    s.flags=0x6f;s.sync_sequence=9;s.sync_delay=123;s.satellites=12;
    // Independent Python struct.pack + zlib vector, shared as JSON with future ESP32.
    const Packet golden={0x41,0x43,0x54,0x31,0x01,0x28,0x6f,0x00,0x78,0x56,0x34,0x12,0x04,0x03,0x02,0x01,0x02,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x7b,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x97,0xfb,0xa1,0xd6};
    const auto expected=golden;
    assert(encode(s)==expected);
    Snapshot decoded;assert(decode(expected,decoded));assert(decoded.epoch==s.epoch);
    assert(decoded.flags==s.flags && decoded.sequence==s.sequence && decoded.satellites==12);
    for(unsigned byte=0;byte<40;++byte)for(unsigned bit=0;bit<8;++bit) {
        auto corrupt=expected;corrupt[byte]^=1u<<bit;assert(!decode(corrupt,decoded));
    }
    // Header/reserved validation independently of CRC failure.
    for(unsigned pos : {0u,4u,5u,33u,34u,35u}) {
        auto bad=expected;bad[pos]^=1;
        const auto crc=crc32(bad.data(),36);
        for(unsigned i=0;i<4;++i)bad[36+i]=crc>>(8*i);
        assert(!decode(bad,decoded));
    }
    Snapshot invalid;invalid.sequence=1;
    const Packet invalid_golden={0x41,0x43,0x54,0x31,1,40,0,0,1,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0xff,0xff,0xff,0xff,0xff,0,0,0,0x6c,0xe7,0xd7,0xc1};
    assert(encode(invalid)==invalid_golden);assert(decode(invalid_golden,decoded));
    s.flags|=holdover;assert(!decode(encode(s),decoded));s.flags&=~holdover;
    for(int64_t epoch : {INT64_C(2147483648),INT64_C(4294967296),INT64_MAX,INT64_C(-1),INT64_MIN}) {
        s.epoch=epoch;assert(decode(encode(s),decoded));assert(decoded.epoch==epoch);
    }
}
void snapshots() {
    Publisher p;clock_model::State s;clock_model::Pulse pulse;
    assert(p.update(s,pulse));assert(p.snapshot().sequence==1 && p.snapshot().epoch==0);
    assert(p.snapshot().satellites==255 && p.snapshot().sync_delay==UINT32_MAX);
    assert(!p.update(s,pulse));
    s.gps_valid=true;assert(p.update(s,pulse));assert(p.snapshot().flags==gps_valid);
    pulse.seen=true;pulse.sequence=1;pulse.at_us=UINT32_MAX-100;
    s.pps_present=true;
    assert(!p.needsSync(s,pulse,pulse.at_us+10));assert(p.update(s,pulse));
    assert(p.snapshot().flags==(gps_valid|pps_present));
    s.utc_valid=s.pps_locked=true;s.utc_seconds=2200000000LL;
    ++pulse.sequence;pulse.at_us+=1000000;
    assert(p.needsSync(s,pulse,pulse.at_us+123));p.emitted(pulse.sequence,123,s.utc_seconds);assert(p.update(s,pulse));
    assert(p.snapshot().flags==(gps_valid|pps_present|pps_locked|utc_valid|sync_valid));
    assert(p.snapshot().epoch==s.utc_seconds && p.snapshot().sync_sequence==1);
    assert(!p.needsSync(s,pulse,pulse.at_us+124));assert(!p.update(s,pulse));
    s.satellites.valid=true;s.satellites.used=0;assert(p.update(s,pulse));
    assert(p.snapshot().flags&sat_valid);assert(p.snapshot().satellites==0);
    const auto epoch=s.utc_seconds;
    Mailbox mailbox;mailbox.publish(encode(p.snapshot()));const auto in_flight=mailbox.latch();
    for(unsigned i=0;i<1000;++i) {
        ++pulse.sequence;++s.utc_seconds;pulse.at_us+=1000000;
        assert(p.needsSync(s,pulse,pulse.at_us+20));p.emitted(pulse.sequence,20,s.utc_seconds);assert(p.update(s,pulse));
        mailbox.publish(encode(p.snapshot()));Snapshot d;assert(decode(mailbox.latch(),d));
        assert(d.epoch==epoch+i+1 && d.boundary==pulse.sequence && !(d.flags&holdover));
        assert(decode(in_flight,d));assert(d.epoch==epoch);
    }
    ++pulse.sequence;pulse.at_us+=1000000;++s.utc_seconds;
    assert(!p.needsSync(s,pulse,pulse.at_us+5001));assert(p.skipped()==1);assert(p.update(s,pulse));
    assert(!(p.snapshot().flags&sync_valid));
    s.utc_valid=false;s.pps_locked=false;assert(p.update(s,pulse));
    assert(p.snapshot().epoch==0 && p.snapshot().sync_delay==UINT32_MAX);
    assert(!p.needsSync(s,pulse,pulse.at_us+6000));
    // Boundary counter and microsecond timestamp wrap do not duplicate pulses.
    Publisher wrapped;s.utc_valid=s.pps_locked=true;
    pulse.sequence=UINT32_MAX;pulse.at_us=UINT32_MAX-10;
    assert(wrapped.needsSync(s,pulse,9));wrapped.emitted(pulse.sequence,20,s.utc_seconds);wrapped.update(s,pulse);
    pulse.sequence=0;pulse.at_us+=1000000;++s.utc_seconds;
    assert(wrapped.needsSync(s,pulse,pulse.at_us+20));wrapped.emitted(pulse.sequence,20,s.utc_seconds);wrapped.update(s,pulse);
    assert(wrapped.snapshot().boundary==0 && wrapped.snapshot().sync_sequence==2);
    assert(!wrapped.needsSync(s,pulse,pulse.at_us+21));
}
void authoritative() {
    clock_model::Timebase clock;clock_model::Pulse pulse;Publisher observer;
    nmea::Utc utc;utc.year=2040;utc.month=1;utc.day=1;
    const auto base=clock_model::toUnix(utc);
    for(unsigned edge=1;edge<=10;++edge) {
        pulse.seen=true;pulse.sequence=edge;pulse.at_us=edge*1000000;
        clock.poll(pulse,pulse.at_us);
        const auto before=clock.state();
        if(observer.needsSync(before,pulse,pulse.at_us+20))observer.emitted(pulse.sequence,20,before.utc_seconds);
        observer.update(before,pulse);
        assert(clock.state().utc_seconds==before.utc_seconds);
        assert(clock.state().utc_valid==before.utc_valid);
        if(edge>=4) {assert(observer.snapshot().epoch==base+edge-2);assert(observer.snapshot().flags&sync_valid);}
        clock_model::Reception r;r.usable=true;r.sequence=edge;r.start_us=pulse.at_us+100000;
        clock.receive(nmea::Result::valid_rmc,clock_model::fromUnix(base+edge-2),'A',r,pulse.at_us+200000);
    }
    clock.poll(pulse,pulse.at_us+clock_model::Timebase::pps_timeout_us);
    observer.update(clock.state(),pulse);assert(!(observer.snapshot().flags&utc_valid));
}
int main(){serialization();snapshots();authoritative();puts("All network v1 CRC/layout/snapshot/authoritative-boundary tests passed");}
