#include "clock_display.hpp"
#include <cstring>

namespace clock_display {
Frame render(const clock_model::State& state) {
    Frame frame;
    // Literal spaces, not printf numeric padding; each row has exactly 20 cells.
    std::memcpy(frame.rows[0], "UTC --:--:-- GPS N/A", 21);
    std::memcpy(frame.rows[1], "PPS:NO SYNC:NO      ", 21);
    if (state.gps_valid) std::memcpy(frame.rows[0] + 17, "OK ", 3);
    if (state.pps_present) std::memcpy(frame.rows[1] + 4, "OK", 2);
    if (state.pps_locked) std::memcpy(frame.rows[1] + 12, "OK", 2);
    if (state.utc_valid) {
        const unsigned values[] = {state.utc.hour, state.utc.minute, state.utc.second};
        for (unsigned i = 0; i < 3; ++i) {
            frame.rows[0][4 + i * 3] = '0' + values[i] / 10;
            frame.rows[0][5 + i * 3] = '0' + values[i] % 10;
        }
    }
    return frame;
}
Update difference(const Frame& before, const Frame& after) {
    Update update;
    update.full = std::strcmp(before.rows[1], after.rows[1]) != 0 ||
                  std::strcmp(before.rows[0] + 12, after.rows[0] + 12) != 0 ||
                  (before.rows[0][4] == '-') != (after.rows[0][4] == '-');
    if (!update.full) {
        for (unsigned i = 4; i < 12; ++i)
            if (before.rows[0][i] != after.rows[0][i]) update.time_positions |= 1u << i;
    }
    return update;
}
}
