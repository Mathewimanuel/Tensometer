// Consistent Overhead Byte Stuffing. Encoded output contains no 0x00 bytes, so 0x00
// can delimit frames on the serial link. The delimiter is NOT written or expected here.
// Pure logic, no Arduino dependency.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cobs {

// Worst-case encoded size for `len` input bytes (excluding the 0x00 delimiter).
constexpr size_t max_encoded_len(size_t len) { return len + len / 254 + 1; }

enum class Result : uint8_t {
    OK = 0,
    NO_SPACE,       // destination buffer too small
    ZERO_IN_INPUT,  // decode: encoded data must not contain 0x00
    TRUNCATED,      // decode: a code byte points past the end of the input
    BAD_ARG,        // null pointer with non-zero length, or empty decode input
};

// Encode `len` bytes of `src` into `dst`. On OK, *out_len holds the encoded length
// (always >= 1). `src` and `dst` must not overlap.
Result encode(const uint8_t* src, size_t len, uint8_t* dst, size_t dst_cap, size_t* out_len);

// Decode `len` encoded bytes (no delimiter) into `dst`. On OK, *out_len holds the
// decoded length. `src` and `dst` must not overlap.
Result decode(const uint8_t* src, size_t len, uint8_t* dst, size_t dst_cap, size_t* out_len);

}  // namespace cobs
