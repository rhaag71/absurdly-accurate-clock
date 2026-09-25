#pragma once
#include <cstddef>
#include <cstdint>

namespace nmea {
enum class GgaResult { none, invalid_gga, valid_gga };

// Supplemental parser for satellites used in the fix (GGA field 7).
class GgaParser {
public:
    GgaResult receive(char byte, uint8_t& satellites_used);
private:
    GgaResult parse(uint8_t& satellites_used);
    char line_[128] = {};
    size_t used_ = 0;
    bool receiving_ = false;
};
}
