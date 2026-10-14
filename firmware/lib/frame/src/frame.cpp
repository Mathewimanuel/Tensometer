#include "frame.h"

#include <string.h>

#include "crc16.h"

namespace frame {

Result encode(uint8_t msg_id, uint8_t seq, const uint8_t* body, size_t body_len,
              uint8_t* out, size_t out_cap, size_t* out_len) {
    if (out == nullptr || out_len == nullptr || (body == nullptr && body_len > 0)) {
        return Result::BAD_ARG;
    }
    if (body_len > MAX_BODY_LEN) {
        return Result::BODY_TOO_LONG;
    }

    uint8_t payload[MAX_PAYLOAD_LEN];
    payload[0] = msg_id;
    payload[1] = seq;
    if (body_len > 0) {
        memcpy(&payload[HEADER_LEN], body, body_len);
    }
    const size_t crc_at = HEADER_LEN + body_len;
    const uint16_t crc = crc16::compute(payload, crc_at);
    payload[crc_at] = static_cast<uint8_t>(crc & 0xFF);
    payload[crc_at + 1] = static_cast<uint8_t>(crc >> 8);

    if (out_cap < 1) {
        return Result::NO_SPACE;
    }
    size_t enc_len = 0;
    // Reserve one byte for the delimiter.
    const cobs::Result r = cobs::encode(payload, crc_at + CRC_LEN, out, out_cap - 1, &enc_len);
    if (r == cobs::Result::NO_SPACE) return Result::NO_SPACE;
    if (r != cobs::Result::OK) return Result::COBS_ERROR;

    out[enc_len] = 0x00;
    *out_len = enc_len + 1;
    return Result::OK;
}

Result decode(const uint8_t* wire, size_t wire_len, uint8_t* scratch, size_t scratch_cap,
              Frame* out) {
    if (wire == nullptr || scratch == nullptr || out == nullptr || wire_len == 0) {
        return Result::BAD_ARG;
    }
    size_t len = 0;
    const cobs::Result r = cobs::decode(wire, wire_len, scratch, scratch_cap, &len);
    if (r == cobs::Result::NO_SPACE) return Result::NO_SPACE;
    if (r != cobs::Result::OK) return Result::COBS_ERROR;
    if (len < HEADER_LEN + CRC_LEN) return Result::TOO_SHORT;
    if (len > MAX_PAYLOAD_LEN) return Result::BODY_TOO_LONG;

    const size_t crc_at = len - CRC_LEN;
    const uint16_t rx_crc =
        static_cast<uint16_t>(scratch[crc_at] | (static_cast<uint16_t>(scratch[crc_at + 1]) << 8));
    if (crc16::compute(scratch, crc_at) != rx_crc) return Result::BAD_CRC;

    out->msg_id = scratch[0];
    out->seq = scratch[1];
    out->body = &scratch[HEADER_LEN];
    out->body_len = crc_at - HEADER_LEN;
    return Result::OK;
}

bool Reader::push(uint8_t byte) {
    if (byte == 0x00) {
        const bool was_discarding = discarding_;
        discarding_ = false;
        frame_len_ = len_;
        len_ = 0;
        return !was_discarding && frame_len_ > 0;
    }
    frame_len_ = 0;
    if (discarding_) {
        return false;
    }
    if (len_ >= sizeof(buf_)) {
        discarding_ = true;
        len_ = 0;
        ++overflows_;
        return false;
    }
    buf_[len_++] = byte;
    return false;
}

}  // namespace frame
