#include "messages.h"

#include <string.h>

namespace messages {

namespace {

// Little-endian byte-cursor writer/reader. Internal: every message-body encode/decode
// function below is built from these so the wire layout is defined in exactly one place
// per field width, independent of any struct's in-memory layout or padding.
class Writer {
public:
    Writer(uint8_t* buf, size_t cap) : buf_(buf), cap_(cap) {}

    bool put_u8(uint8_t v) { return put_bytes(&v, 1); }
    bool put_u16(uint16_t v) {
        const uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
        return put_bytes(b, 2);
    }
    bool put_u32(uint32_t v) {
        const uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
                               static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
        return put_bytes(b, 4);
    }
    bool put_u64(uint64_t v) {
        uint8_t b[8];
        for (int i = 0; i < 8; ++i) b[i] = static_cast<uint8_t>(v >> (8 * i));
        return put_bytes(b, 8);
    }
    bool put_i32(int32_t v) { return put_u32(static_cast<uint32_t>(v)); }

    size_t len() const { return len_; }

private:
    bool put_bytes(const uint8_t* src, size_t n) {
        if (len_ + n > cap_) return false;
        memcpy(buf_ + len_, src, n);
        len_ += n;
        return true;
    }
    uint8_t* buf_;
    size_t cap_;
    size_t len_ = 0;
};

class Reader {
public:
    Reader(const uint8_t* buf, size_t len) : buf_(buf), len_(len) {}

    bool get_u8(uint8_t* v) { return get_bytes(v, 1); }
    bool get_u16(uint16_t* v) {
        uint8_t b[2];
        if (!get_bytes(b, 2)) return false;
        *v = static_cast<uint16_t>(b[0]) | static_cast<uint16_t>(static_cast<uint16_t>(b[1]) << 8);
        return true;
    }
    bool get_u32(uint32_t* v) {
        uint8_t b[4];
        if (!get_bytes(b, 4)) return false;
        *v = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
             (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
        return true;
    }
    bool get_u64(uint64_t* v) {
        uint8_t b[8];
        if (!get_bytes(b, 8)) return false;
        uint64_t r = 0;
        for (int i = 0; i < 8; ++i) r |= static_cast<uint64_t>(b[i]) << (8 * i);
        *v = r;
        return true;
    }
    bool get_i32(int32_t* v) {
        uint32_t u;
        if (!get_u32(&u)) return false;
        *v = static_cast<int32_t>(u);
        return true;
    }

private:
    bool get_bytes(void* dst, size_t n) {
        if (pos_ + n > len_) return false;
        memcpy(dst, buf_ + pos_, n);
        pos_ += n;
        return true;
    }
    const uint8_t* buf_;
    size_t len_;
    size_t pos_ = 0;
};

}  // namespace

Result encode_hello(const proto::HelloBody& body, uint8_t* out, size_t out_cap,
                     size_t* out_len) {
    if (out == nullptr || out_len == nullptr) return Result::BAD_ARG;
    Writer w(out, out_cap);
    bool ok = w.put_u8(body.version_major) && w.put_u8(body.version_minor) &&
              w.put_u32(body.build_id) && w.put_u8(body.capabilities) &&
              w.put_u8(body.cell_profile) && w.put_u32(body.cpu_hz);
    if (!ok) return Result::NO_SPACE;
    *out_len = w.len();
    return Result::OK;
}

Result decode_hello(const uint8_t* in, size_t in_len, proto::HelloBody* out) {
    if (in == nullptr || out == nullptr) return Result::BAD_ARG;
    if (in_len != proto::HELLO_WIRE_LEN) return Result::TOO_SHORT;
    Reader r(in, in_len);
    uint8_t cap, profile;
    bool ok = r.get_u8(&out->version_major) && r.get_u8(&out->version_minor) &&
              r.get_u32(&out->build_id) && r.get_u8(&cap) && r.get_u8(&profile) &&
              r.get_u32(&out->cpu_hz);
    if (!ok) return Result::TOO_SHORT;
    out->capabilities = cap;
    out->cell_profile = profile;
    return Result::OK;
}

Result encode_ack(const proto::AckBody& body, uint8_t* out, size_t out_cap, size_t* out_len) {
    if (out == nullptr || out_len == nullptr) return Result::BAD_ARG;
    Writer w(out, out_cap);
    const bool ok = w.put_u8(body.acked_msg_id) && w.put_u8(body.acked_seq);
    if (!ok) return Result::NO_SPACE;
    *out_len = w.len();
    return Result::OK;
}

Result decode_ack(const uint8_t* in, size_t in_len, proto::AckBody* out) {
    if (in == nullptr || out == nullptr) return Result::BAD_ARG;
    if (in_len != proto::ACK_WIRE_LEN) return Result::TOO_SHORT;
    Reader r(in, in_len);
    const bool ok = r.get_u8(&out->acked_msg_id) && r.get_u8(&out->acked_seq);
    if (!ok) return Result::TOO_SHORT;
    return Result::OK;
}

Result encode_nack(const proto::NackBody& body, uint8_t* out, size_t out_cap, size_t* out_len) {
    if (out == nullptr || out_len == nullptr) return Result::BAD_ARG;
    Writer w(out, out_cap);
    const bool ok =
        w.put_u8(body.acked_msg_id) && w.put_u8(body.acked_seq) && w.put_u8(body.error_code);
    if (!ok) return Result::NO_SPACE;
    *out_len = w.len();
    return Result::OK;
}

Result decode_nack(const uint8_t* in, size_t in_len, proto::NackBody* out) {
    if (in == nullptr || out == nullptr) return Result::BAD_ARG;
    if (in_len != proto::NACK_WIRE_LEN) return Result::TOO_SHORT;
    Reader r(in, in_len);
    const bool ok =
        r.get_u8(&out->acked_msg_id) && r.get_u8(&out->acked_seq) && r.get_u8(&out->error_code);
    if (!ok) return Result::TOO_SHORT;
    return Result::OK;
}

Result encode_heartbeat_status(const proto::HeartbeatBody& body, uint8_t* out, size_t out_cap,
                                size_t* out_len) {
    if (out == nullptr || out_len == nullptr) return Result::BAD_ARG;
    Writer w(out, out_cap);
    const bool ok = w.put_u8(static_cast<uint8_t>(body.state)) &&
                    w.put_u8(static_cast<uint8_t>(body.lock_owner)) &&
                    w.put_u32(body.inhibit_mask) && w.put_u8(body.fault_cause);
    if (!ok) return Result::NO_SPACE;
    *out_len = w.len();
    return Result::OK;
}

Result decode_heartbeat_status(const uint8_t* in, size_t in_len, proto::HeartbeatBody* out) {
    if (in == nullptr || out == nullptr) return Result::BAD_ARG;
    if (in_len != proto::HEARTBEAT_STATUS_WIRE_LEN) return Result::TOO_SHORT;
    Reader r(in, in_len);
    uint8_t state, lock_owner;
    const bool ok = r.get_u8(&state) && r.get_u8(&lock_owner) && r.get_u32(&out->inhibit_mask) &&
                    r.get_u8(&out->fault_cause);
    if (!ok) return Result::TOO_SHORT;
    out->state = static_cast<mod::CoreState>(state);
    out->lock_owner = static_cast<proto::LockOwner>(lock_owner);
    return Result::OK;
}

Result encode_telemetry(const proto::TelemetryBody& body, uint8_t* out, size_t out_cap,
                         size_t* out_len) {
    if (out == nullptr || out_len == nullptr) return Result::BAD_ARG;
    Writer w(out, out_cap);
    const bool ok = w.put_u64(body.timestamp_us) && w.put_i32(body.position_steps) &&
                    w.put_i32(body.force_raw_counts) && w.put_i32(body.force_raw_mN) &&
                    w.put_i32(body.force_filtered_mN) &&
                    w.put_u8(static_cast<uint8_t>(body.state)) && w.put_u8(body.flags);
    if (!ok) return Result::NO_SPACE;
    *out_len = w.len();
    return Result::OK;
}

Result decode_telemetry(const uint8_t* in, size_t in_len, proto::TelemetryBody* out) {
    if (in == nullptr || out == nullptr) return Result::BAD_ARG;
    if (in_len != proto::TELEMETRY_WIRE_LEN) return Result::TOO_SHORT;
    Reader r(in, in_len);
    uint8_t state;
    const bool ok = r.get_u64(&out->timestamp_us) && r.get_i32(&out->position_steps) &&
                    r.get_i32(&out->force_raw_counts) && r.get_i32(&out->force_raw_mN) &&
                    r.get_i32(&out->force_filtered_mN) && r.get_u8(&state) &&
                    r.get_u8(&out->flags);
    if (!ok) return Result::TOO_SHORT;
    out->state = static_cast<mod::CoreState>(state);
    return Result::OK;
}

Result encode_event(const proto::EventBody& body, uint8_t* out, size_t out_cap,
                     size_t* out_len) {
    if (out == nullptr || out_len == nullptr) return Result::BAD_ARG;
    Writer w(out, out_cap);
    const bool ok = w.put_u64(body.timestamp_us) && w.put_u8(static_cast<uint8_t>(body.type)) &&
                    w.put_u8(body.aux) && w.put_i32(body.position_steps) &&
                    w.put_i32(body.force_mN);
    if (!ok) return Result::NO_SPACE;
    *out_len = w.len();
    return Result::OK;
}

Result decode_event(const uint8_t* in, size_t in_len, proto::EventBody* out) {
    if (in == nullptr || out == nullptr) return Result::BAD_ARG;
    if (in_len != proto::EVENT_WIRE_LEN) return Result::TOO_SHORT;
    Reader r(in, in_len);
    uint8_t type;
    const bool ok = r.get_u64(&out->timestamp_us) && r.get_u8(&type) && r.get_u8(&out->aux) &&
                    r.get_i32(&out->position_steps) && r.get_i32(&out->force_mN);
    if (!ok) return Result::TOO_SHORT;
    out->type = static_cast<proto::EventType>(type);
    return Result::OK;
}

Result encode_select_profile(const proto::SelectProfileBody& body, uint8_t* out, size_t out_cap,
                              size_t* out_len) {
    if (out == nullptr || out_len == nullptr) return Result::BAD_ARG;
    Writer w(out, out_cap);
    const bool ok = w.put_u8(body.profile_id) && w.put_u8(body.coupler_confirmed);
    if (!ok) return Result::NO_SPACE;
    *out_len = w.len();
    return Result::OK;
}

Result decode_select_profile(const uint8_t* in, size_t in_len, proto::SelectProfileBody* out) {
    if (in == nullptr || out == nullptr) return Result::BAD_ARG;
    if (in_len != proto::SELECT_PROFILE_WIRE_LEN) return Result::TOO_SHORT;
    Reader r(in, in_len);
    const bool ok = r.get_u8(&out->profile_id) && r.get_u8(&out->coupler_confirmed);
    if (!ok) return Result::TOO_SHORT;
    return Result::OK;
}

Result encode_jog(const proto::JogBody& body, uint8_t* out, size_t out_cap, size_t* out_len) {
    if (out == nullptr || out_len == nullptr) return Result::BAD_ARG;
    Writer w(out, out_cap);
    const bool ok = w.put_u8(static_cast<uint8_t>(body.direction)) && w.put_u8(body.action);
    if (!ok) return Result::NO_SPACE;
    *out_len = w.len();
    return Result::OK;
}

Result decode_jog(const uint8_t* in, size_t in_len, proto::JogBody* out) {
    if (in == nullptr || out == nullptr) return Result::BAD_ARG;
    if (in_len != proto::JOG_WIRE_LEN) return Result::TOO_SHORT;
    Reader r(in, in_len);
    uint8_t dir;
    const bool ok = r.get_u8(&dir) && r.get_u8(&out->action);
    if (!ok) return Result::TOO_SHORT;
    out->direction = static_cast<proto::JogDir>(dir);
    return Result::OK;
}

Result encode_arm(const proto::ArmBody& body, uint8_t* out, size_t out_cap, size_t* out_len) {
    if (out == nullptr || out_len == nullptr) return Result::BAD_ARG;
    Writer w(out, out_cap);
    const bool ok = w.put_u32(body.l0_um) && w.put_u16(body.speed_mm_per_min) &&
                    w.put_u32(body.force_limit_mN) && w.put_u32(body.max_travel_um) &&
                    w.put_u16(body.strain_limit_permille) && w.put_u8(body.cycle_mode) &&
                    w.put_u8(body.reserved);
    if (!ok) return Result::NO_SPACE;
    *out_len = w.len();
    return Result::OK;
}

Result decode_arm(const uint8_t* in, size_t in_len, proto::ArmBody* out) {
    if (in == nullptr || out == nullptr) return Result::BAD_ARG;
    if (in_len != proto::ARM_WIRE_LEN) return Result::TOO_SHORT;
    Reader r(in, in_len);
    const bool ok = r.get_u32(&out->l0_um) && r.get_u16(&out->speed_mm_per_min) &&
                    r.get_u32(&out->force_limit_mN) && r.get_u32(&out->max_travel_um) &&
                    r.get_u16(&out->strain_limit_permille) && r.get_u8(&out->cycle_mode) &&
                    r.get_u8(&out->reserved);
    if (!ok) return Result::TOO_SHORT;
    return Result::OK;
}

}  // namespace messages
