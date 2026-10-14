#include "crc16.h"

namespace crc16 {

// Bitwise implementation: frames are short (< 250 bytes, ~100 per second), so the
// 512-byte lookup table is not worth the RAM.
uint16_t update(uint16_t crc, const uint8_t* data, size_t len) {
    if (data == nullptr) {
        return crc;
    }
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(static_cast<uint16_t>(data[i]) << 8);
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) ? static_cast<uint16_t>((crc << 1) ^ 0x1021u)
                                  : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

}  // namespace crc16
