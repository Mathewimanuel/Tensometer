// Frame layer for the PC link (skill section 8):
//   payload = msg_id (u8) | seq (u8) | body | crc16 (u16, little-endian)
//   wire    = COBS(payload) followed by a single 0x00 delimiter
// CRC-16/CCITT-FALSE covers msg_id..body. Pure logic, no Arduino dependency.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cobs.h"

namespace frame {

constexpr size_t MAX_BODY_LEN = 240;
constexpr size_t HEADER_LEN = 2;  // msg_id, seq
constexpr size_t CRC_LEN = 2;
constexpr size_t MAX_PAYLOAD_LEN = HEADER_LEN + MAX_BODY_LEN + CRC_LEN;
// Encoded payload plus the 0x00 delimiter.
constexpr size_t MAX_WIRE_LEN = cobs::max_encoded_len(MAX_PAYLOAD_LEN) + 1;

enum class Result : uint8_t {
    OK = 0,
    BODY_TOO_LONG,
    NO_SPACE,
    COBS_ERROR,
    TOO_SHORT,
    BAD_CRC,
    BAD_ARG,
};

struct Frame {
    uint8_t msg_id;
    uint8_t seq;
    const uint8_t* body;  // points into the caller's scratch buffer
    size_t body_len;
};

// Build a complete wire frame (including the trailing 0x00) into `out`.
Result encode(uint8_t msg_id, uint8_t seq, const uint8_t* body, size_t body_len,
              uint8_t* out, size_t out_cap, size_t* out_len);

// Decode one wire frame. `wire` excludes the 0x00 delimiter. `scratch` receives the
// decoded payload and must be at least MAX_PAYLOAD_LEN bytes; `out->body` points into it.
Result decode(const uint8_t* wire, size_t wire_len, uint8_t* scratch, size_t scratch_cap,
              Frame* out);

// Accumulates bytes from the serial link and reports complete frames.
// Overlong frames are discarded up to the next delimiter and counted.
class Reader {
public:
    // Feed one byte. Returns true when `byte` was a delimiter ending a non-empty frame;
    // the frame bytes (without delimiter) are then available via data()/length() until
    // the next push().
    bool push(uint8_t byte);
    const uint8_t* data() const { return buf_; }
    size_t length() const { return frame_len_; }
    uint32_t overflow_count() const { return overflows_; }

private:
    uint8_t buf_[MAX_WIRE_LEN];
    size_t len_ = 0;
    size_t frame_len_ = 0;
    bool discarding_ = false;
    uint32_t overflows_ = 0;
};

}  // namespace frame
