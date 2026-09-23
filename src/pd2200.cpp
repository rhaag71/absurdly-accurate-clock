#include "pd2200.hpp"

namespace pd2200 {
void Display::begin() {
    const uint8_t reset[] = {0x1B, 0x49}; // ESC I
    uart_.write(reset, sizeof(reset));
    delay(100); // Allow reset to settle before configuration.

    const uint8_t configure[] = {
        0x16,             // Cursor off.
        0x1B, 0x4C, 0x3F, // ESC L ?: minimum brightness.
        0x0E,             // Clear does not home on this display.
        0x0C,             // Home explicitly.
    };
    uart_.write(configure, sizeof(configure));
}

void Display::writeRow(uint8_t row, const char (&text)[columns + 1]) {
    if (row >= 2) {
        return;
    }
    const uint8_t position[] = {
        0x1B, 0x48, static_cast<uint8_t>(row * columns),
    };
    uart_.write(position, sizeof(position));
    uart_.write(reinterpret_cast<const uint8_t*>(text), columns);
}
}
