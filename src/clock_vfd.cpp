#include "clock_vfd.hpp"

namespace clock_display {
void writeInitialFields(pd2200::Display& display, const Frame& initial) {
    display.writeField(0, 0, initial.rows[0], 3);       // UTC
    display.writeField(0, 4, initial.rows[0] + 4, 10); // HH:MM:SS.X
    display.writeField(1, 0, initial.rows[1], 3);       // GPS
    display.writeChar(1, 4, initial.rows[1][4]);
    display.writeField(1, 6, initial.rows[1] + 6, 3);  // PPS
    display.writeChar(1, 10, initial.rows[1][10]);
}
void Output::reset(const Frame& displayed) {
    submitted_ = displayed;
    size_ = next_ = 0;
}
size_t Output::write(uint8_t byte) {
    if (size_ == sizeof(command_)) return 0;
    command_[size_++] = byte;
    return 1;
}
void Output::service(const Frame& desired, bool writable) {
    if (!writable) return; // No command selection/queueing while backpressured.
    if (next_ == 0) {
        const auto update = difference(submitted_, desired);
        bool found = false;
        // Clock digits before indicator, then status. Static labels never change.
        for (uint8_t row = 0; row < 2 && !found; ++row) {
            for (uint8_t col = 0; col < columns; ++col) {
                if (update.positions[row] & (1u << col)) {
                    row_ = row;
                    column_ = col;
                    found = true;
                    break;
                }
            }
        }
        if (!found) return;
        size_ = 0;
        encoder_.writeChar(row_, column_, desired.rows[row_][column_]);
    }
    // If an edge/phase changed while the position header was in flight, send
    // the CURRENT character, not the value selected before the delay.
    if (next_ == 3) {
        const char value = desired.rows[row_][column_];
        if (value == submitted_.rows[row_][column_] || value == ' ') {
            // ESC H position is already a complete command. If phase returned
            // to the displayed glyph, omit its now-redundant data byte safely.
            size_ = next_ = 0;
            return;
        }
        command_[3] = static_cast<uint8_t>(value);
    }
    if (uart_.write(command_[next_]) != 1) return;
    if (++next_ == size_) {
        submitted_.rows[row_][column_] = static_cast<char>(command_[3]);
        size_ = next_ = 0;
    }
}
}
