#pragma once
#include <Arduino.h>

// PD-2200 configured externally for Noritake emulation, 9600 baud, 8N1.
namespace pd2200 {
constexpr size_t columns = 20;

class Display {
public:
    explicit Display(Print& uart) : uart_(uart) {}
    // UART must already be initialized and display power stable.
    void begin();
    // Zero-based row; exactly 20 characters plus a terminator in the buffer.
    // Sends only the 20 characters, never the terminator or a newline.
    void writeRow(uint8_t row, const char (&text)[columns + 1]);

private:
    Print& uart_;
};
}
