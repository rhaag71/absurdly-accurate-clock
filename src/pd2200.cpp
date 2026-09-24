#include "pd2200.hpp"

namespace pd2200 {
namespace {
// Display text is printable ASCII. Commands/position bytes are encoded separately.
uint8_t visibleByte(char value) {
    const auto byte = static_cast<uint8_t>(value);
    return byte >= 0x20 && byte <= 0x7e ? byte : 0x20;
}
}
void Display::begin() {
    const uint8_t reset[] = {0x1B, 0x49}; // ESC I
    uart_.write(reset, sizeof(reset));
    delay(100); // Allow reset to settle before configuration.

    const uint8_t configure[] = {
        0x16,             // Cursor off.
        0x1B, 0x4C, 0x3F, // Preserve the bench-approved brightness bytes.
        0x0E,             // Clear does not home on this display.
        0x0C,             // Home explicitly.
    };
    uart_.write(configure, sizeof(configure));
}

void Display::clear() {
    const uint8_t command[] = {0x0E, 0x0C};
    uart_.write(command, sizeof(command));
}
bool Display::position(uint8_t row, uint8_t column) {
    if (row >= 2 || column >= columns) return false;
    const uint8_t command[] = {
        0x1B, 0x48, static_cast<uint8_t>(row * columns + column),
    };
    uart_.write(command, sizeof(command));
    return true;
}
void Display::writeField(uint8_t row, uint8_t column, const char* text, size_t length) {
    if (!text || !length || !text[0] || !position(row, column)) return;
    const size_t remaining = columns - column;
    if (length > remaining) length = remaining;
    for (size_t i = 0; i < length && text[i] != '\0'; ++i)
        uart_.write(visibleByte(text[i]));
}

void Display::writeRow(uint8_t row, const char (&text)[columns + 1]) {
    if (row >= 2) {
        return;
    }
    const uint8_t position[] = {
        0x1B, 0x48, static_cast<uint8_t>(row * columns),
    };
    uart_.write(position, sizeof(position));
    uint8_t content[columns];
    bool ended = false;
    for (size_t i = 0; i < columns; ++i) {
        // A short C string has a space-filled remainder, never NUL padding.
        // After its terminator, do not read any potentially uninitialized tail.
        if (!ended && text[i] == '\0') ended = true;
        content[i] = ended ? 0x20 : visibleByte(text[i]);
    }
    uart_.write(content, sizeof(content));
}
void Display::writeChar(uint8_t row, uint8_t column, char value) {
    if (row >= 2 || column >= columns) return;
    const uint8_t command[] = {
        0x1B, 0x48, static_cast<uint8_t>(row * columns + column),
        visibleByte(value),
    };
    uart_.write(command, sizeof(command));
}

}
