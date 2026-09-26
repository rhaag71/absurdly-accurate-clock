#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include "clock_state.hpp"
namespace clock_network {
constexpr size_t packet_size=40;
constexpr uint16_t gps_valid=1, pps_present=2, pps_locked=4, utc_valid=8,
                   holdover=16, sync_valid=32, sat_valid=64;
using Packet=std::array<uint8_t,packet_size>;
struct Snapshot {
    uint32_t sequence=0, boundary=0, sync_sequence=0, sync_delay=UINT32_MAX;
    int64_t epoch=0;
    uint16_t flags=0;
    uint8_t satellites=255;
};
uint32_t crc32(const uint8_t* data,size_t length);
Packet encode(const Snapshot& snapshot);
bool decode(const Packet& packet,Snapshot& snapshot);
// Single main-loop writer; reader is an ISR on the SAME core, not a second core.
class Mailbox {
public:
    void publish(const Packet& packet);
    Packet latch() const;
private:
    Packet slots_[2] = {};
    std::atomic<uint8_t> active_{0};
};
// Pure state observer. No mutation of, or feedback into, the timebase.
class Publisher {
public:
    bool needsSync(const clock_model::State&,const clock_model::Pulse&,uint32_t now_us);
    // Commit synchronization identity only after the TIME_SYNC GPIO edge was emitted.
    void emitted(uint32_t boundary,uint32_t delay_us,int64_t epoch);
    // Count a candidate rejected by the final, immediately-before-edge deadline check.
    void suppressed();
    bool update(const clock_model::State&,const clock_model::Pulse&);
    const Snapshot& snapshot() const {return current_;}
    uint32_t skipped() const {return skipped_;}
private:
    Snapshot current_;
    bool initialized_=false,seen_=false;
    uint32_t examined_=0,sync_boundary_=0,sync_count_=0,delay_=UINT32_MAX,skipped_=0;
    int64_t sync_epoch_=0;
};
}
