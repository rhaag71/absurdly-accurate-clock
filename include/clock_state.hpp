#pragma once
#include <cstdint>

namespace clock_model {
// Future GPS/PPS fusion produces this snapshot. Never display zero as valid time.
// UTC Unix seconds; leap-second handling and PPS/message association are TODO.
struct State {
    int64_t utc_seconds = 0;
    bool utc_valid = false;
    bool pps_locked = false;
};
}
