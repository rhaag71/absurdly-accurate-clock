#include "network_protocol.hpp"
namespace clock_network {
namespace {
void put(Packet& p,size_t pos,uint64_t n,unsigned count) {
    for(unsigned i=0;i<count;++i) {p[pos+i]=static_cast<uint8_t>(n);n>>=8;}
}
uint64_t get(const Packet& p,size_t pos,unsigned count) {
    uint64_t n=0;for(unsigned i=0;i<count;++i)n|=uint64_t(p[pos+i])<<(8*i);return n;
}
}
uint32_t crc32(const uint8_t* data,size_t length) {
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<length;++i) {
        crc^=data[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320u:0);
    }
    return crc^UINT32_MAX;
}
Packet encode(const Snapshot& s) {
    Packet p={};p[0]='A';p[1]='C';p[2]='T';p[3]='1';p[4]=1;p[5]=packet_size;
    put(p,6,s.flags,2);put(p,8,s.sequence,4);put(p,12,s.boundary,4);
    put(p,16,static_cast<uint64_t>(s.epoch),8);put(p,24,s.sync_sequence,4);
    put(p,28,s.sync_delay,4);p[32]=s.satellites;put(p,36,crc32(p.data(),36),4);return p;
}
bool decode(const Packet& p,Snapshot& s) {
    if(p[0]!='A'||p[1]!='C'||p[2]!='T'||p[3]!='1'||p[4]!=1||p[5]!=packet_size ||
       p[33]||p[34]||p[35]||get(p,36,4)!=crc32(p.data(),36))return false;
    Snapshot x;x.flags=get(p,6,2);
    if(x.flags & (holdover | ~uint16_t(127)))return false;
    x.sequence=get(p,8,4);x.boundary=get(p,12,4);
    const uint64_t raw=get(p,16,8);
    x.epoch=(raw>>63) ? -1-static_cast<int64_t>(~raw):static_cast<int64_t>(raw);
    x.sync_sequence=get(p,24,4);x.sync_delay=get(p,28,4);x.satellites=p[32];
    if(!(x.flags&utc_valid) && x.epoch!=0)return false;
    if((x.flags&sat_valid) ? x.satellites>99 : x.satellites!=255)return false;
    if(x.flags&sync_valid) {
        if((x.flags&(utc_valid|pps_locked|pps_present))!=(utc_valid|pps_locked|pps_present) || x.sync_delay>5000)return false;
    } else if(x.sync_delay!=UINT32_MAX)return false;
    s=x;return true;
}
void Mailbox::publish(const Packet& p) {
    const uint8_t next=active_.load(std::memory_order_relaxed)^1;
    slots_[next]=p;active_.store(next,std::memory_order_release);
}
Packet Mailbox::latch() const {return slots_[active_.load(std::memory_order_acquire)];}
bool Publisher::needsSync(const clock_model::State& s,const clock_model::Pulse& p,uint32_t now) {
    if(!p.seen || (seen_ && examined_==p.sequence))return false;
    seen_=true;examined_=p.sequence;
    if(!s.utc_valid||!s.pps_locked||!s.pps_present)return false;
    if(uint32_t(now-p.at_us)>5000) {++skipped_;return false;}
    sync_boundary_=p.sequence;sync_epoch_=s.utc_seconds;return true;
}
void Publisher::emitted(uint32_t delay) {++sync_count_;delay_=delay;}
bool Publisher::update(const clock_model::State& s,const clock_model::Pulse& p) {
    Snapshot next;
    next.flags=(s.gps_valid?gps_valid:0)|(s.pps_present?pps_present:0)|
               (s.pps_locked?pps_locked:0)|(s.utc_valid?utc_valid:0)|
               (s.satellites.valid?sat_valid:0);
    next.epoch=s.utc_valid?s.utc_seconds:0;next.boundary=p.seen?p.sequence:0;
    next.satellites=s.satellites.valid?s.satellites.used:255;next.sync_sequence=sync_count_;
    if(delay_!=UINT32_MAX && s.utc_valid && s.pps_locked && s.pps_present && p.seen &&
       p.sequence==sync_boundary_ && s.utc_seconds==sync_epoch_) {
        next.flags|=sync_valid;next.sync_delay=delay_;
    }
    next.sequence=current_.sequence;
    if(initialized_ && next.flags==current_.flags && next.epoch==current_.epoch &&
       next.boundary==current_.boundary && next.satellites==current_.satellites &&
       next.sync_sequence==current_.sync_sequence && next.sync_delay==current_.sync_delay)return false;
    ++next.sequence;current_=next;initialized_=true;return true;
}
}
