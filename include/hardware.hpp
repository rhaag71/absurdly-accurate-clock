#pragma once

namespace hardware {
// UART rates are explicit bring-up assumptions, not detected device settings.
void begin(unsigned long gps_baud, unsigned long vfd_baud);
}
