#pragma once
#include <cstdint>

namespace appliance {
constexpr uint32_t watchdog_timeout_ms = 4000;

// Boot diagnostic only: independent of acquisition and time-quality state.
constexpr uint32_t heartbeat_interval_ms(bool watchdog_boot) {
    return watchdog_boot ? 250 : 500;
}
inline bool heartbeat_due(uint32_t now, uint32_t last, bool watchdog_boot) {
    return uint32_t(now - last) >= heartbeat_interval_ms(watchdog_boot);
}
}
