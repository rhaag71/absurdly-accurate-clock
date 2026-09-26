#include "zone_button.hpp"

namespace presentation {
void ZoneButton::begin(bool pressed, uint32_t now_ms) {
    zone_ = DisplayZone::utc;
    raw_ = pressed;
    stable_ = true;
    armed_ = false;
    changed_ms_ = now_ms;
}
bool ZoneButton::poll(bool pressed, uint32_t now_ms) {
    if (pressed != raw_) {
        raw_ = pressed;
        changed_ms_ = now_ms;
    }
    if (raw_ == stable_ || uint32_t(now_ms - changed_ms_) < debounce_ms) return false;
    stable_ = raw_;
    if (!stable_) { armed_ = true; return false; }
    if (!armed_) return false;
    armed_ = false;
    zone_ = nextZone(zone_);
    return true;
}
}
