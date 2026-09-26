#include "network_interface.hpp"
#include "network_protocol.hpp"
#include "fake_sdk.hpp"
#include "pins.hpp"
#include <cstdio>
#include <cstring>
namespace fake {
Hardware hw;uint32_t now=0,time_reads=0,advance_on_read=0,advance_by=0;bool levels[30]={};
void (*callbacks[30])()={};void (*irq)()=nullptr;unsigned rises=0;
}
void select(bool low) {fake::levels[9]=!low;fake::callbacks[9]();}
uint8_t clockByte(uint8_t mosi=0xa5) {
    assert(fake::hw.cr1 & SPI_SSPCR1_SSE_BITS);
    assert(!fake::hw.dr.tx.empty());
    const auto b=fake::hw.dr.tx.front();fake::hw.dr.tx.pop_front();
    if(fake::hw.dr.rx.size()<8)fake::hw.dr.rx.push_back(mosi);
    else fake::hw.ris|=SPI_SSPRIS_RORRIS_BITS;
    if(fake::hw.imsc && (fake::hw.dr.tx.size()<=4 || fake::hw.dr.rx.size()>=4))fake::irq();
    return b;
}
clock_network::Packet read() {
    clock_network::Packet p;select(true);for(auto& b:p)b=clockByte();select(false);return p;
}
int main() {
    using namespace clock_network;
    static_assert(pins::esp_spi_rx==8 && pins::esp_spi_tx==11,"Native SPI pin directions");
    begin();assert(fake::hw.cr0==0x87);assert(!fake::levels[12]);
    assert(!fake::callbacks[13]);
    Snapshot d;assert(decode(read(),d));assert(d.epoch==0 && d.flags==0);
    clock_model::State s;s.utc_valid=s.pps_present=s.pps_locked=s.gps_valid=true;
    s.utc_seconds=2200000000LL;s.satellites.valid=true;s.satellites.used=12;
    clock_model::Pulse p;p.seen=true;p.sequence=1;p.at_us=1000000;
    fake::now=p.at_us+20;service(s,p);assert(fake::rises==1 && fake::levels[12]);
    auto successful=read();Snapshot prior;assert(decode(successful,prior));
    assert(prior.flags&sync_valid);assert(prior.sync_sequence==1 && prior.sync_delay==20);
    // The next boundary passes needsSync()'s initial check, then crosses the
    // deadline on service()'s final time read. It must not inherit this edge.
    ++p.sequence;++s.utc_seconds;p.at_us+=1000000;fake::now=p.at_us+4990;
    fake::advance_on_read=fake::time_reads+2;fake::advance_by=20;
    const auto before_race_edges=fake::rises;
    service(s,p);fake::advance_on_read=0;
    assert(fake::rises==before_race_edges && !fake::levels[12]);
    assert(decode(read(),d));assert(d.boundary==p.sequence && d.epoch==s.utc_seconds);
    assert(!(d.flags&sync_valid));assert(d.sync_delay==UINT32_MAX);
    assert(d.sync_sequence==prior.sync_sequence);
    char text[224];assert(diagnostic(text,sizeof(text),0));assert(std::strstr(text,"NET v1"));
    assert(diagnostic(text,sizeof(text),60000));assert(std::strstr(text,"skipped=1"));
    fake::now+=200;service(s,p);assert(fake::rises==1 && !fake::levels[12]);
    for(unsigned split=0;split<=40;++split) {
        Packet frame;select(true);
        for(unsigned i=0;i<split;++i)frame[i]=clockByte();
        const auto epoch=s.utc_seconds;
        ++p.sequence;p.at_us+=1000000;++s.utc_seconds;fake::now=p.at_us+30;
        service(s,p); // Publish while CS active: selected packet stays frozen.
        for(unsigned i=split;i<40;++i)frame[i]=clockByte();
        select(false);assert(decode(frame,d));assert(d.epoch==epoch);
        assert(decode(read(),d));assert(d.epoch==s.utc_seconds && d.boundary==p.sequence);
    }
    // Every short byte length discards prefetched FIFO data at next CS.
    for(unsigned n=0;n<40;++n) {select(true);for(unsigned i=0;i<n;++i)clockByte();select(false);assert(decode(read(),d));}
    select(true);for(unsigned i=0;i<48;++i)clockByte();assert(fake::hw.imsc==0);select(false);
    assert(decode(read(),d));
    // A stalled transaction never prevents main-loop time publication.
    select(true);s.utc_valid=false;s.pps_locked=false;
    fake::now+=200;service(s,p);select(false);
    assert(decode(read(),d));assert(d.epoch==0 && !(d.flags&(utc_valid|sync_valid)));
    const auto edges=fake::rises;
    ++p.sequence;p.at_us+=1000000;fake::now=p.at_us+100;service(s,p);assert(fake::rises==edges);
    s.utc_valid=s.pps_locked=true;++p.sequence;p.at_us+=1000000;fake::now=p.at_us+6000;
    service(s,p);assert(fake::rises==edges);assert(decode(read(),d));assert(!(d.flags&sync_valid));
    // Model receiver overrun: diagnostic only, then reset/recover at next CS.
    select(true);fake::hw.dr.rx.assign(8,0x5a);fake::hw.ris=SPI_SSPRIS_RORRIS_BITS;
    select(false);assert(decode(read(),d));
    assert(!diagnostic(text,sizeof(text),119999));assert(diagnostic(text,sizeof(text),120000));
    assert(std::strstr(text,"short=42") && std::strstr(text,"extra=1") && std::strstr(text,"over=1") && std::strstr(text,"skipped=2"));
    puts("Native SPI1 register-model framing/abort/snapshot/TIME_SYNC tests passed");
}
