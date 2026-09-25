#pragma once
#include <cstdint>
#include "nmea_rmc.hpp"
#include "nmea_gga.hpp"

namespace clock_model {
struct SatelliteStatus {
    static constexpr uint32_t stale_after_us = 3000000;
    bool valid = false;
    uint8_t used = 0;
    uint32_t updated_us = 0;
    void receive(nmea::GgaResult result, uint8_t count, uint32_t now_us);
    void poll(uint32_t now_us);
};
struct State {
    int64_t utc_seconds = 0; // Authoritative local Unix UTC, updated only on PPS.
    nmea::Utc utc;
    char rmc_status = '?';
    uint32_t rmc_phase_us = 0; // Last usable RMC completion offset from its PPS.
    bool utc_valid = false;
    bool gps_valid = false;
    bool pps_present = false;
    bool pps_locked = false;
    SatelliteStatus satellites;
};

struct Pulse {
    uint32_t sequence = 0;
    uint32_t at_us = 0;
    bool seen = false;
};
struct Reception {
    uint32_t sequence = 0;
    uint32_t start_us = 0;
    bool usable = false;
};

int64_t toUnix(const nmea::Utc& utc);
nmea::Utc fromUnix(int64_t seconds);

class Timebase {
public:
    static constexpr uint32_t gps_timeout_us = 3000000;
    static constexpr uint32_t pps_timeout_us = 1500000;
    // Receiver contract: 1 Hz navigation, whole-second RMC following its PPS.
    static constexpr uint32_t rmc_min_us = 20000;
    static constexpr uint32_t rmc_max_us = 900000;
    const State& state() const { return state_; }
    SatelliteStatus& satelliteStatus() { return state_.satellites; }
    void poll(const Pulse& pulse, uint32_t now_us);
    void receive(nmea::Result result, const nmea::Utc& utc, char status,
                 const Reception& reception, uint32_t now_us);
    // A UART servicing gap makes buffered sentence timing untrustworthy.
    void discardAssociation();
private:
    void unlock();
    State state_;
    Pulse pulse_;
    uint32_t last_gps_us_ = 0;
    uint32_t last_association_us_ = 0;
    bool cadence_ok_ = false;
    bool candidate_ = false;
    bool confirmed_ = false;
    uint32_t candidate_sequence_ = 0;
    int64_t candidate_utc_ = 0;
};
}
