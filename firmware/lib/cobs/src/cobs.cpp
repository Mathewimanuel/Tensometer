#include "cobs.h"

namespace cobs {

Result encode(const uint8_t* src, size_t len, uint8_t* dst, size_t dst_cap, size_t* out_len) {
    if (dst == nullptr || out_len == nullptr || (src == nullptr && len > 0)) {
        return Result::BAD_ARG;
    }
    if (dst_cap < 1) {
        return Result::NO_SPACE;
    }

    size_t code_idx = 0;  // where the current block's code byte goes
    size_t out = 1;
    uint8_t code = 1;     // 1 + number of non-zero bytes in the current block

    for (size_t i = 0; i < len; ++i) {
        if (src[i] == 0) {
            dst[code_idx] = code;
            if (out >= dst_cap) return Result::NO_SPACE;
            code_idx = out++;
            code = 1;
        } else {
            if (out >= dst_cap) return Result::NO_SPACE;
            dst[out++] = src[i];
            ++code;
            if (code == 0xFF) {  // block full: 254 data bytes
                dst[code_idx] = code;
                if (out >= dst_cap) return Result::NO_SPACE;
                code_idx = out++;
                code = 1;
            }
        }
    }
    dst[code_idx] = code;
    *out_len = out;
    return Result::OK;
}

Result decode(const uint8_t* src, size_t len, uint8_t* dst, size_t dst_cap, size_t* out_len) {
    if (src == nullptr || dst == nullptr || out_len == nullptr || len == 0) {
        return Result::BAD_ARG;
    }

    size_t i = 0;
    size_t out = 0;
    while (i < len) {
        const uint8_t code = src[i++];
        if (code == 0) return Result::ZERO_IN_INPUT;

        for (uint8_t n = 1; n < code; ++n) {
            if (i >= len) return Result::TRUNCATED;
            const uint8_t b = src[i++];
            if (b == 0) return Result::ZERO_IN_INPUT;
            if (out >= dst_cap) return Result::NO_SPACE;
            dst[out++] = b;
        }
        // A block shorter than 254 data bytes was terminated by a zero in the
        // original data, unless it is the final block.
        if (code < 0xFF && i < len) {
            if (out >= dst_cap) return Result::NO_SPACE;
            dst[out++] = 0;
        }
    }
    *out_len = out;
    return Result::OK;
}

}  // namespace cobs
