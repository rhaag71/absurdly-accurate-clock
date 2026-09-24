#pragma once
#include "clock_state.hpp"

namespace clock_display {
struct Frame {
    char rows[2][21];
};
Frame render(const clock_model::State& state);
struct Update {
    bool full = false;
    uint32_t time_positions = 0; // Bit N means write column N in row 0.
};
Update difference(const Frame& before, const Frame& after);
}
