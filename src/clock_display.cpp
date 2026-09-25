#include "clock_display.hpp"
#include <cstring>

namespace clock_display {
Frame::Frame() {
    for (auto& row : rows) {
        std::memset(row, 0x20, columns);
        row[columns] = '\0';
    }
}
char rollingDecade(const clock_model::State& state, const clock_model::Pulse& pulse,
                   uint32_t now_us) {
    if (!state.utc_valid || !state.pps_locked || !state.pps_present || !pulse.seen)
        return '-';
    const uint32_t elapsed = now_us - pulse.at_us; // Wrap-safe; no free-running modulo.
    if (elapsed >= clock_model::Timebase::pps_timeout_us) return '-';
    // Arrive at HH:MM:SS.0, rest for half a second, then wind toward the next PPS.
    if (elapsed < 500000) return '0';
    const uint32_t step = 1 + (elapsed - 500000) / decade_step_us;
    return static_cast<char>('0' + (step < 9 ? step : 9));
}
Frame render(const clock_model::State& state, const clock_model::Pulse& pulse,
             uint32_t now_us) {
    Frame frame;
    std::memcpy(frame.rows[0] + 3, "UTC", 3);
    std::memcpy(frame.rows[0] + 7, "--:--:--.", 9);
    frame.rows[decade_row][decade_column] = rollingDecade(state, pulse, now_us);
    std::memcpy(frame.rows[1], "GPS", 3);
    frame.rows[1][3] = state.gps_valid ? '+' : '-';
    std::memcpy(frame.rows[1] + 6, "PPS", 3);
    frame.rows[1][9] = state.pps_present ? '+' : '-';
    std::memcpy(frame.rows[1] + 12, "SAT ", 4);
    frame.rows[1][16] = state.satellites.valid ? static_cast<char>('0' + state.satellites.used / 10) : '-';
    frame.rows[1][17] = state.satellites.valid ? static_cast<char>('0' + state.satellites.used % 10) : '-';
    if (state.utc_valid) {
        const unsigned values[] = {state.utc.hour, state.utc.minute, state.utc.second};
        for (unsigned i = 0; i < 3; ++i) {
            frame.rows[0][7 + i * 3] = '0' + values[i] / 10;
            frame.rows[0][8 + i * 3] = '0' + values[i] % 10;
        }
    }
    return frame;
}
Update difference(const Frame& before, const Frame& after) {
    Update update;
    for (unsigned row = 0; row < 2; ++row) {
        for (unsigned col = 0; col < columns; ++col) {
            // Layout occupancy is fixed even when time/status becomes invalid.
            // A future layout change must clear once and reset the output cache.
            if (after.rows[row][col] != ' ' && before.rows[row][col] != after.rows[row][col])
                update.positions[row] |= 1u << col;
        }
    }
    return update;
}
}
