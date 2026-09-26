#pragma once
#include <cstddef>
#include <cstdint>
#include "clock_state.hpp"
namespace clock_network {
void begin();
// Main loop only, immediately after polling the same captured pulse.
void service(const clock_model::State&,const clock_model::Pulse&);
// Bounded, one startup line and then at most one summary per minute.
bool diagnostic(char* buffer,size_t size,uint32_t now_ms);
}
