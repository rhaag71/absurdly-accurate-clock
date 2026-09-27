#include "watchdog_policy.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    assert(appliance::watchdog_timeout_ms == 4000);
    assert(appliance::heartbeat_interval_ms(false) == 500);
    assert(appliance::heartbeat_interval_ms(true) == 250);
    for (bool watchdog_boot : {false, true}) {
        const uint32_t interval = appliance::heartbeat_interval_ms(watchdog_boot);
        uint32_t last = UINT32_MAX - 100; // Cross millis() wrap on first toggle.
        for (unsigned i = 0; i < 10000; ++i) {
            assert(!appliance::heartbeat_due(last, last, watchdog_boot));
            assert(!appliance::heartbeat_due(last + interval - 1, last, watchdog_boot));
            assert(appliance::heartbeat_due(last + interval, last, watchdog_boot));
            assert(appliance::heartbeat_due(last + interval + 100, last, watchdog_boot));
            last += interval;
        }
    }
    for (bool watchdog_boot : {false, true}) {
        appliance::WatchdogDiagnostic reports;
        for (unsigned line = 1; line <= 10000; ++line)
            assert(reports.next(watchdog_boot) == (watchdog_boot && line % 5 == 0));
    }
    // A new boot starts a fresh count and does not retain the previous cause.
    appliance::WatchdogDiagnostic normal_boot;
    for (unsigned line = 0; line < 10; ++line) assert(!normal_boot.next(false));
    appliance::WatchdogDiagnostic watchdog_boot;
    for (unsigned line = 0; line < 4; ++line) assert(!watchdog_boot.next(true));
    assert(watchdog_boot.next(true));
    std::puts("Watchdog heartbeat/diagnostic policy tests passed");
}
