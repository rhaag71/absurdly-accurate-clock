#include <Arduino.h>
#include "hardware.hpp"
#include "pins.hpp"

namespace hardware {
void begin(unsigned long gps_baud, unsigned long vfd_baud) {
    // Arduino-Pico Serial1 = UART0; Serial2 = UART1.
    Serial1.setTX(pins::gps_tx);
    Serial1.setRX(pins::gps_rx);
    Serial1.begin(gps_baud, SERIAL_8N1);

    Serial2.setTX(pins::vfd_tx);
    Serial2.setRX(-1); // GP5 reserved; display path is transmit-only for now.
    Serial2.begin(vfd_baud, SERIAL_8N1);

    pinMode(pins::gps_pps, INPUT);
    pinMode(pins::ui_button, INPUT_PULLUP); // Active-low GP6 (physical pin 9) button to GND; internal pull-up.
    // SPI1/TIME_SYNC are initialized by clock_network::begin; GP13 stays reserved.
}
}
