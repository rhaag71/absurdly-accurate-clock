#include "nmea_rmc.hpp"
#include <cstring>

namespace {
int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
bool digits(const char* text, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (text[i] < '0' || text[i] > '9') return false;
    return true;
}
uint8_t pair(const char* text) {
    return static_cast<uint8_t>((text[0] - '0') * 10 + text[1] - '0');
}
}

namespace nmea {
Result RmcParser::receive(char byte, Utc& utc, char& status) {
    if (byte == '$') {
        used_ = 0;
        receiving_ = true;
        return Result::none;
    }
    if (!receiving_) return Result::none;
    if (byte == '\r' || byte == '\n') {
        receiving_ = false;
        line_[used_] = '\0';
        return parse(utc, status);
    }
    if (byte < 0x20 || byte > 0x7e || used_ == sizeof(line_) - 1) {
        receiving_ = false; // Discard damaged/overlong input until next '$'.
        return Result::none;
    }
    line_[used_++] = byte;
    return Result::none;
}

Result RmcParser::parse(Utc& utc, char& status) {
    char* star = std::strchr(line_, '*');
    if (!star || std::strlen(star + 1) != 2) return Result::none;
    const int high = hex(star[1]), low = hex(star[2]);
    if (high < 0 || low < 0) return Result::none;
    uint8_t checksum = 0;
    for (const char* p = line_; p != star; ++p) checksum ^= *p;
    if (checksum != ((high << 4) | low)) return Result::none;
    *star = '\0';
    if (std::strncmp(line_, "GPRMC,", 6) != 0) return Result::none;

    // Preserve empty fields; date is field 9, regardless of position content.
    char* fields[10] = {line_};
    size_t count = 1;
    for (char* p = line_; *p; ++p) {
        if (*p == ',') {
            *p = '\0';
            if (count < 10) fields[count++] = p + 1;
        }
    }
    status = count > 2 && std::strlen(fields[2]) == 1 ? fields[2][0] : '?';
    if (count < 10 || status != 'A') return Result::invalid_rmc;
    const char* time = fields[1];
    const size_t length = std::strlen(time);
    if (length < 6 || !digits(time, 6) ||
        (length > 6 && (time[6] != '.' || length == 7 || !digits(time + 7, length - 7))))
        return Result::invalid_rmc;
    const char* date = fields[9];
    if (std::strlen(date) != 6 || !digits(date, 6)) return Result::invalid_rmc;
    Utc parsed;
    for (size_t i = 7; i < length; ++i) parsed.fractional |= time[i] != '0';
    parsed.hour = pair(time);
    parsed.minute = pair(time + 2);
    parsed.second = pair(time + 4);
    parsed.day = pair(date);
    parsed.month = pair(date + 2);
    // Explicit century policy for the receiver's two-digit RMC year.
    parsed.year = 2000 + pair(date + 4);
    if (parsed.hour > 23 || parsed.minute > 59 || parsed.second > 59 ||
        parsed.month < 1 || parsed.month > 12 || parsed.day < 1)
        return Result::invalid_rmc;
    const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap = parsed.year % 4 == 0 &&
                      (parsed.year % 100 != 0 || parsed.year % 400 == 0);
    const unsigned limit = days[parsed.month - 1] + (parsed.month == 2 && leap ? 1 : 0);
    if (parsed.day > limit) return Result::invalid_rmc;
    utc = parsed;
    return Result::valid_rmc;
}
}
