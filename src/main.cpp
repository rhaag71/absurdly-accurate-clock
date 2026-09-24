#include <Arduino.h>
#include <cstring>
#include <cstdio>
#include "clock_state.hpp"
#include "clock_display.hpp"
#include "pins.hpp"
#include "hardware.hpp"
#include "pd2200.hpp"

namespace {
constexpr unsigned long gps_baud = 9600; // Confirm against the GPS configuration.
constexpr unsigned long vfd_baud = 9600; // Confirm against the PD-2200 switches.
clock_model::Timebase timebase;
nmea::RmcParser parser;
volatile uint32_t pps_count = 0;
volatile uint32_t pps_at_us = 0;
volatile bool pps_seen = false;
uint32_t last_heartbeat_ms = 0;
bool heartbeat_on = true;

void onPpsRise() {
    pps_at_us = micros();
    ++pps_count;
    pps_seen = true;
}
clock_model::Pulse snapshot(uint32_t& now_us) {
    noInterrupts();
    clock_model::Pulse pulse;
    pulse.sequence = pps_count;
    pulse.at_us = pps_at_us;
    pulse.seen = pps_seen;
    now_us = micros(); // Same snapshot: cannot precede the captured edge.
    interrupts();
    return pulse;
}

// Bounded non-blocking diagnostic queue. Drop complete messages if a host
// remains disconnected through too many transitions; never stall GPS reception.
char usb_queue[512];
size_t usb_head = 0, usb_tail = 0, usb_used = 0;
void diagnostic(const char* message) {
    const size_t length = std::strlen(message);
    if (length > sizeof(usb_queue) - usb_used) return;
    for (size_t i = 0; i < length; ++i) {
        usb_queue[usb_head] = message[i];
        usb_head = (usb_head + 1) % sizeof(usb_queue);
    }
    usb_used += length;
}
void reportTransitions() {
    static bool gps = false, locked = false;
    const auto& state = timebase.state();
    if (state.gps_valid && !gps) diagnostic("GPS UTC acquired\r\n");
    if (!state.gps_valid && gps) diagnostic("GPS UTC lost\r\n");
    if (state.pps_locked && !locked) {
        char message[96];
        snprintf(message, sizeof(message),
                 "PPS synchronization acquired; RMC end +%lu ms (preceding PPS)\r\n",
                 static_cast<unsigned long>(state.rmc_phase_us / 1000));
        diagnostic(message);
    }
    if (!state.pps_locked && locked) diagnostic("PPS synchronization lost\r\n");
    gps = state.gps_valid;
    locked = state.pps_locked;
}

// Keep the proven display encoder unchanged. Queue one pair of rows, then
// feed UART1 only when writable: SerialUART's bulk write otherwise blocks.
class VfdQueue : public Print {
public:
    size_t write(uint8_t byte) override {
        if (end_ == sizeof(bytes_)) {
            return 0;
        }
        bytes_[end_++] = byte;
        return 1;
    }
    bool empty() const { return next_ == end_; }
    void service() {
        if (!empty() && Serial2.availableForWrite() > 0) {
            Serial2.write(bytes_[next_++]);
            if (empty()) {
                next_ = end_ = 0;
            }
        }
    }
private:
    uint8_t bytes_[2 * (3 + pd2200::columns)] = {};
    size_t next_ = 0;
    size_t end_ = 0;
};
VfdQueue vfd_queue;
pd2200::Display status_display(vfd_queue);
pd2200::Display display(Serial2);
clock_display::Frame displayed = clock_display::render(timebase.state());
uint32_t last_service_us = 0;
bool discard_rx = true; // Startup delays buffered bytes without arrival timestamps.
clock_model::Reception reception;

}

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH); // Early indication that setup() was reached.
    last_heartbeat_ms = millis();
    Serial.begin(115200); // USB diagnostics; never wait for a connected host.
    // Retain GPS bytes arriving during the existing VFD startup delays.
    Serial1.setFIFOSize(1024);
    hardware::begin(gps_baud, vfd_baud);
    attachInterrupt(digitalPinToInterrupt(pins::gps_pps), onPpsRise, RISING);
    delay(500); // Short power-up allowance for the separately powered VFD.
    display.begin();
    display.writeRow(0, displayed.rows[0]);
    display.writeRow(1, displayed.rows[1]);
    Serial2.flush();
    diagnostic("GPS/PPS UTC clock; RMC labels preceding PPS\r\n");
    last_service_us = micros();

}

void loop() {
    uint32_t now_us;
    auto pulse = snapshot(now_us);
    // A stalled main loop cannot safely timestamp already-buffered UART bytes.
    if (uint32_t(now_us - last_service_us) > 20000) {
        discard_rx = true;
        parser = nmea::RmcParser{};
        timebase.discardAssociation();
        reportTransitions();
    }
    last_service_us = now_us;
    timebase.poll(pulse, now_us);
    reportTransitions();

    // Bound work so incoming UART traffic cannot starve pulse/display handling.
    for (unsigned i = 0; i < 64 && Serial1.available() > 0; ++i) {
        const int received = Serial1.read();
        if (received < 0) break;
        if (discard_rx) continue;
        pulse = snapshot(now_us);
        timebase.poll(pulse, now_us);
        reportTransitions();
        if (received == '$') {
            reception.sequence = pulse.sequence;
            reception.start_us = now_us;
            reception.usable = pulse.seen;
        }
        nmea::Utc utc;
        char status = '?';
        const auto result = parser.receive(static_cast<char>(received), utc, status);
        timebase.receive(result, utc, status, reception, now_us);
        reportTransitions();
    }
    if (discard_rx && Serial1.available() == 0) discard_rx = false;

    for (unsigned i = 0; i < 64 && usb_used > 0; ++i) {
        if (!Serial || Serial.availableForWrite() <= 0 ||
            Serial.write(static_cast<uint8_t>(usb_queue[usb_tail])) != 1) break;
        usb_tail = (usb_tail + 1) % sizeof(usb_queue);
        --usb_used;
    }
    vfd_queue.service();
    const uint32_t now = millis();
    if (now - last_heartbeat_ms >= 500) {
        last_heartbeat_ms = now;
        heartbeat_on = !heartbeat_on;
        digitalWrite(LED_BUILTIN, heartbeat_on ? HIGH : LOW);
    }
    if (vfd_queue.empty()) {
        const auto next = clock_display::render(timebase.state());
        const auto update = clock_display::difference(displayed, next);
        if (update.full) {
            status_display.writeRow(0, next.rows[0]);
            status_display.writeRow(1, next.rows[1]);
        } else {
            // Usually column 11 alone; column 10 changes at each ten-second roll.
            // Minute/hour rollover uses the same authoritative UTC digit diff.
            for (uint8_t column = 4; column < 12; ++column)
                if (update.time_positions & (1u << column))
                    status_display.writeChar(0, column, next.rows[0][column]);
        }
        displayed = next;
    }
}
