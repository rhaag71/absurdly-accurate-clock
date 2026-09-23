#include <Arduino.h>
#include "clock_state.hpp"
#include "hardware.hpp"
#include "pd2200.hpp"

namespace {
constexpr unsigned long gps_baud = 9600; // Confirm against the GPS configuration.
constexpr unsigned long vfd_baud = 9600; // Confirm against the PD-2200 switches.
clock_model::State clock_state;
pd2200::Display display(Serial2);
constexpr char first_row[] = "ABSURD CLOCK        ";
constexpr char second_row[] = "PICO 2 ONLINE       ";
static_assert(sizeof(first_row) - 1 == pd2200::columns, "VFD row must be 20 characters");
static_assert(sizeof(second_row) - 1 == pd2200::columns, "VFD row must be 20 characters");
}

void setup() {
    Serial.begin(115200); // USB diagnostics; never wait for a connected host.
    hardware::begin(gps_baud, vfd_baud);
    delay(500); // Short power-up allowance for the separately powered VFD.
    display.begin();
    display.writeRow(0, first_row);
    display.writeRow(1, second_row);
    Serial2.flush();
    Serial.println("PD-2200 VFD initialization and startup message sent.");
}

void loop() {
    // Initial VFD bring-up only; future clock behavior remains unimplemented.
    (void)clock_state;
    delay(1);
}
