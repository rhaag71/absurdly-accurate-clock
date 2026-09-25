#include "clock_state.hpp"

namespace clock_model {
void SatelliteStatus::receive(nmea::GgaResult result, uint8_t count, uint32_t now_us) {
    if (result == nmea::GgaResult::none) return;
    if (result == nmea::GgaResult::invalid_gga) { valid = false; return; }
    used = count;
    updated_us = now_us;
    valid = true;
}
void SatelliteStatus::poll(uint32_t now_us) {
    if (valid && static_cast<uint32_t>(now_us - updated_us) >= stale_after_us) valid = false;
}
namespace {
bool leap(unsigned year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}
unsigned daysInMonth(unsigned year, unsigned month) {
    const unsigned days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month - 1] + (month == 2 && leap(year) ? 1 : 0);
}
}
int64_t toUnix(const nmea::Utc& utc) {
    int64_t days = 0;
    for (unsigned y = 1970; y < utc.year; ++y) days += leap(y) ? 366 : 365;
    for (unsigned m = 1; m < utc.month; ++m) days += daysInMonth(utc.year, m);
    days += utc.day - 1;
    return ((days * 24 + utc.hour) * 60 + utc.minute) * 60 + utc.second;
}
nmea::Utc fromUnix(int64_t seconds) {
    nmea::Utc utc;
    utc.second = seconds % 60;
    seconds /= 60;
    utc.minute = seconds % 60;
    seconds /= 60;
    utc.hour = seconds % 24;
    int64_t days = seconds / 24;
    utc.year = 1970;
    while (days >= (leap(utc.year) ? 366 : 365)) {
        days -= leap(utc.year) ? 366 : 365;
        ++utc.year;
    }
    utc.month = 1;
    while (days >= daysInMonth(utc.year, utc.month)) {
        days -= daysInMonth(utc.year, utc.month);
        ++utc.month;
    }
    utc.day = days + 1;
    return utc;
}
void Timebase::unlock() {
    state_.pps_locked = false;
    state_.utc_valid = false;
    candidate_ = confirmed_ = false;
}
void Timebase::discardAssociation() {
    unlock();
}
void Timebase::poll(const Pulse& pulse, uint32_t now_us) {
    if (state_.gps_valid && uint32_t(now_us - last_gps_us_) >= gps_timeout_us) {
        state_.gps_valid = false;
        unlock();
    }
    if (state_.pps_locked && uint32_t(now_us - last_association_us_) >= gps_timeout_us)
        unlock();
    // Once timed out, a stale capture cannot become present again at micros wrap.
    if (pulse_.seen && pulse.sequence == pulse_.sequence && !state_.pps_present) return;
    const bool present = pulse.seen && uint32_t(now_us - pulse.at_us) < pps_timeout_us;
    if (!present) {
        state_.pps_present = false;
        cadence_ok_ = false;
        pulse_ = pulse;
        unlock();
        return;
    }
    state_.pps_present = true;
    if (!pulse_.seen || pulse.sequence != pulse_.sequence) {
        const uint32_t period = pulse.at_us - pulse_.at_us;
        cadence_ok_ = pulse_.seen && uint32_t(pulse.sequence - pulse_.sequence) == 1 &&
                      period >= 900000 && period <= 1100000;
        if (!cadence_ok_) unlock();
        if (cadence_ok_ && state_.gps_valid) {
            // Apply labels/corrections only when processing a NEW captured edge.
            if (confirmed_ && uint32_t(pulse.sequence - candidate_sequence_) == 1) {
                state_.utc_seconds = candidate_utc_ + 1;
                state_.pps_locked = state_.utc_valid = true;
            } else if (state_.pps_locked) {
                ++state_.utc_seconds;
            }
            if (state_.utc_valid) state_.utc = fromUnix(state_.utc_seconds);
        }
        pulse_ = pulse;
    }
}
void Timebase::receive(nmea::Result result, const nmea::Utc& utc, char status,
                       const Reception& reception, uint32_t now_us) {
    if (result == nmea::Result::none) return;
    state_.rmc_status = status;
    state_.gps_valid = result == nmea::Result::valid_rmc;
    if (!state_.gps_valid) {
        unlock();
        return;
    }
    last_gps_us_ = now_us;
    // This is a label for the preceding PPS, NOT a timestamp of UART arrival.
    // Reject edge-straddling, late, fractional-epoch or unqualified receptions.
    if (!reception.usable || !state_.pps_present || !cadence_ok_ || utc.fractional ||
        reception.sequence != pulse_.sequence ||
        uint32_t(reception.start_us - pulse_.at_us) < rmc_min_us ||
        uint32_t(now_us - pulse_.at_us) > rmc_max_us) {
        candidate_ = confirmed_ = false;
        return;
    }
    last_association_us_ = now_us;
    state_.rmc_phase_us = now_us - pulse_.at_us;
    const int64_t label = toUnix(utc);
    if (state_.pps_locked && label != state_.utc_seconds) {
        // Keep the local epoch unchanged; reacquire from two coherent labels.
        unlock();
    }
    if (candidate_ && reception.sequence == candidate_sequence_) {
        if (label != candidate_utc_) unlock(); // Conflicting duplicate.
        return;
    }
    confirmed_ = candidate_ && uint32_t(reception.sequence - candidate_sequence_) == 1 &&
                 label == candidate_utc_ + 1;
    candidate_ = true;
    candidate_sequence_ = reception.sequence;
    candidate_utc_ = label;
}
}
