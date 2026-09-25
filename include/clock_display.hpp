#pragma once
#include "clock_state.hpp"

namespace clock_display {
constexpr unsigned columns = 20;
constexpr uint8_t decade_row = 0;
constexpr uint8_t decade_column = 16; // Physical column 17, after literal decimal point.
constexpr uint32_t decade_step_us = 50000;
struct Frame {
    Frame(); // Spaces describe cleared cells; they are not sent by normal output.
    char rows[2][columns + 1];
};
// PPS-synchronized rolling decade indicator: expendable visual output, NOT UTC.
// The caller must first poll the timebase with this same captured pulse.
char rollingDecade(const clock_model::State& state, const clock_model::Pulse& pulse,
                   uint32_t now_us);
Frame render(const clock_model::State& state, const clock_model::Pulse& pulse = {},
             uint32_t now_us = 0);
struct Update {
    uint32_t positions[2] = {}; // Changed occupied cells only; never padding.
};
Update difference(const Frame& before, const Frame& after);
}
