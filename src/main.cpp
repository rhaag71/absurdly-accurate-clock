#include <Arduino.h>
#include "clock_state.hpp"
#include "hardware.hpp"

namespace {
constexpr unsigned long gps_baud = 9600; // Confirm against the GPS configuration.
constexpr unsigned long vfd_baud = 9600; // Confirm against the PD-2200 switches.
clock_model::State clock_state;
}

void setup() {
    Serial.begin(115200); // USB diagnostics; never wait for a connected host.
    hardware::begin(gps_baud, vfd_baud);
}

void loop() {
    // TODO: parse NMEA/UBX, associate UTC with PPS, then render Noritake commands.
    // No fabricated clock output or peripheral commands during initial bring-up.
    (void)clock_state;
    delay(1);
}
