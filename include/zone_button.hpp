#pragma once
#include "display_time.hpp"

namespace presentation {
// Active-low hardware is translated to pressed=true by the caller. Both edges
// must remain stable for 30 ms. No repeat, delays, interrupts, or persistence.
class ZoneButton {
public:
    static constexpr uint32_t debounce_ms = 30;
    // Start disarmed: even a button held during boot must be released stably
    // before it can select a zone. The selected zone always starts at UTC.
    void begin(bool pressed, uint32_t now_ms);
    bool poll(bool pressed, uint32_t now_ms); // True once per accepted press.
    DisplayZone zone() const { return zone_; }
private:
    DisplayZone zone_ = DisplayZone::utc;
    bool raw_ = true, stable_ = true, armed_ = false;
    uint32_t changed_ms_ = 0;
};
}
