#pragma once
#include "nmea_rmc.hpp"

namespace presentation {
enum class DisplayZone : uint8_t { utc, eastern, central, mountain, pacific };
struct DisplayTime {
    nmea::Utc civil;
    const char* label;
    int offset_hours;
    bool daylight;
};
DisplayZone nextZone(DisplayZone zone);
const char* zoneName(DisplayZone zone);
// Presentation only. Contemporary U.S. rules (2007 onward), not historical
// timezone data. Mountain means U.S. DST-observing Mountain, not Arizona.
// Input is a valid UTC calendar date from the authoritative timebase.
DisplayTime convertUtcForDisplay(const nmea::Utc& utc, DisplayZone zone);
}
