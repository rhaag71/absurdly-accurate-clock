#pragma once
#include <cstddef>
#include <cstdint>

namespace nmea {
struct Utc {
    bool fractional = false; // Nonzero fraction: valid RMC, not a whole-second PPS label.
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
};
enum class Result { none, invalid_rmc, valid_rmc };
// Fixed storage, no allocation. Only complete, checksum-verified GPRMC is used.
class RmcParser {
public:
    Result receive(char byte, Utc& utc, char& status);
private:
    Result parse(Utc& utc, char& status);
    char line_[128] = {};
    size_t used_ = 0;
    bool receiving_ = false;
};
}
