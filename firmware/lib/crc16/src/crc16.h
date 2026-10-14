// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR.
// Check value: crc16("123456789") == 0x29B1. Pure logic, no Arduino dependency.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace crc16 {

constexpr uint16_t INIT = 0xFFFF;

// Continue a running CRC over `len` bytes. Pass INIT for a fresh CRC.
uint16_t update(uint16_t crc, const uint8_t* data, size_t len);

inline uint16_t compute(const uint8_t* data, size_t len) { return update(INIT, data, len); }

}  // namespace crc16
