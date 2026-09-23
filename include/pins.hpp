#pragma once
#include <cstdint>

// GPIO numbers, not physical header pin numbers. Directions are Pico-relative.
namespace pins {
constexpr uint8_t gps_tx = 0;
constexpr uint8_t gps_rx = 1;
constexpr uint8_t gps_pps = 2;
constexpr uint8_t vfd_tx = 4;
constexpr uint8_t vfd_rx_reserved = 5;
constexpr uint8_t ui_button = 6;
constexpr uint8_t esp_spi_miso = 8;
constexpr uint8_t esp_spi_cs = 9;
constexpr uint8_t esp_spi_sck = 10;
constexpr uint8_t esp_spi_mosi = 11;
constexpr uint8_t time_sync = 12;
constexpr uint8_t esp_control_irq = 13;
// GP14 through GP22 remain unassigned for future expansion.
}
