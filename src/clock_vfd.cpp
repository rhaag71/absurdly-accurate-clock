#include "clock_vfd.hpp"

namespace clock_display {
void writeInitialFields(pd2200::Display& display, const Frame& initial) {
    display.writeField(0, 3, initial.rows[0] + 3, 3);  // centered UTC
    display.writeField(0, 7, initial.rows[0] + 7, 10); // HH:MM:SS.X
    display.writeField(1, 0, initial.rows[1], 3);       // GPS
    display.writeChar(1, 3, initial.rows[1][3]);
    display.writeField(1, 6, initial.rows[1] + 6, 3);  // PPS
    display.writeChar(1, 9, initial.rows[1][9]);
    display.writeField(1, 12, initial.rows[1] + 12, 3); // SAT label
    display.writeChar(1, 16, initial.rows[1][16]);
    display.writeChar(1, 17, initial.rows[1][17]);
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
void Output::service(const Frame& desired, bool writable, AcceptedCharacter* accepted) {
    if (accepted) accepted->valid = false;
    if (!writable) return; // No command selection/queueing while backpressured.
    if (next_ == 0) {
        const auto update = difference(submitted_, desired);
        bool found = false;
        // Zone label and clock digits before indicator, then status.
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
        if (row_ == 0 && (column_ == 7 || column_ == 8)) {
            // PD-2200 accommodation: never directly address HH ones at 08.
            // The physical x0 -> x8 root cause remains unproven.
            column_ = 7;
            encoder_.writeField(0, 7, desired.rows[0] + 7, 2);
        } else {
            encoder_.writeChar(row_, column_, desired.rows[row_][column_]);
        }
    }
    const bool hh_pair = size_ == 5;
    // Refresh before the first payload is accepted, including on retries.
    // Once HH tens is accepted, freeze BOTH payloads until ones is accepted.
    // A later desired change is picked up as another complete pair afterward.
    if (next_ == 3) {
        const char value = desired.rows[row_][column_];
        const char ones = hh_pair ? desired.rows[0][8] : 0;
        const bool unchanged = value == submitted_.rows[row_][column_] &&
                               (!hh_pair || ones == submitted_.rows[0][8]);
        if (unchanged || value == ' ' || (hh_pair && ones == ' ')) {
            // The position header is complete; no payload has been accepted.
            size_ = next_ = 0;
            return;
        }
        command_[3] = static_cast<uint8_t>(value);
        if (hh_pair) command_[4] = static_cast<uint8_t>(ones);
    }
    if (uart_.write(command_[next_]) != 1) return;
    if (next_ >= 3) {
        const uint8_t column = column_ + next_ - 3;
        // Cache each byte actually accepted, including a partially sent HH pair.
        submitted_.rows[row_][column] = static_cast<char>(command_[next_]);
        if (accepted) {
            accepted->valid = true;
            accepted->address = row_ * columns + column;
            accepted->payload = command_[next_];
            accepted->hh_pair = hh_pair;
            accepted->complete = next_ + 1 == size_;
            if (hh_pair) {
                accepted->hh[0] = command_[3];
                accepted->hh[1] = command_[4];
            }
        }
    }
    if (++next_ == size_) size_ = next_ = 0;
}
}
