#include "nmea_gga.hpp"
#include <cstring>

namespace {
int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
}

namespace nmea {
GgaResult GgaParser::receive(char byte, uint8_t& satellites_used) {
    if (byte == '$') { used_ = 0; receiving_ = true; return GgaResult::none; }
    if (!receiving_) return GgaResult::none;
    if (byte == '\r' || byte == '\n') {
        receiving_ = false;
        line_[used_] = '\0';
        return parse(satellites_used);
    }
    if (byte < 0x20 || byte > 0x7e || used_ == sizeof(line_) - 1) {
        receiving_ = false;
        return GgaResult::none;
    }
    line_[used_++] = byte;
    return GgaResult::none;
}

GgaResult GgaParser::parse(uint8_t& satellites_used) {
    char* star = std::strchr(line_, '*');
    if (!star || std::strlen(star + 1) != 2) return GgaResult::none;
    const int high = hex(star[1]), low = hex(star[2]);
    if (high < 0 || low < 0) return GgaResult::none;
    uint8_t checksum = 0;
    for (const char* p = line_; p != star; ++p) checksum ^= *p;
    if (checksum != ((high << 4) | low)) return GgaResult::none;
    *star = '\0';
    const char* comma = std::strchr(line_, ',');
    const size_t id_length = comma ? static_cast<size_t>(comma - line_) : 0;
    if (id_length != 5 || line_[0] != 'G' ||
        (line_[1] != 'P' && line_[1] != 'N') || line_[2] != 'G' || line_[3] != 'G' || line_[4] != 'A')
        return GgaResult::none;

    char* fields[8] = {line_};
    size_t count = 1;
    for (char* p = line_; *p; ++p) {
        if (*p == ',') {
            *p = '\0';
            if (count < 8) fields[count++] = p + 1;
        }
    }
    // A valid GGA with no fix is meaningful and clears the displayed count.
    if (count < 8 || std::strlen(fields[6]) != 1 || fields[6][0] < '0' || fields[6][0] > '8' ||
        std::strlen(fields[7]) != 2 || fields[7][0] < '0' || fields[7][0] > '9' ||
        fields[7][1] < '0' || fields[7][1] > '9') return GgaResult::invalid_gga;
    if (fields[6][0] == '0') return GgaResult::invalid_gga;
    satellites_used = static_cast<uint8_t>((fields[7][0] - '0') * 10 + fields[7][1] - '0');
    return GgaResult::valid_gga;
}
}
