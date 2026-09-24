#pragma once
#include <cstddef>
#include <cstdint>
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t byte) = 0;
    size_t write(const uint8_t* bytes, size_t count) {
        size_t written=0;
        while(written<count && write(bytes[written])==1) ++written;
        return written;
    }
};
inline void delay(unsigned long) {}
