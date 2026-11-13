// Encode/decode for the core message bodies defined in protocol.h (skill section 8).
// Pure logic, no Arduino dependency. Each function writes/reads the exact little-endian
// wire layout documented next to its struct in protocol.h, independent of that struct's
// in-memory padding (protocol.h structs are for working with the data; this is the only
// place that actually packs/unpacks bytes onto the wire).
//
// The PC -> firmware HEARTBEAT ping has an empty body (its arrival alone resets the
// watchdog, skill section 8); only the firmware -> PC heartbeat status carries a body,
// so there is no encode/decode pair for the ping -- callers just use frame::encode()
// directly with body_len == 0 for HEARTBEAT, and treat a received HEARTBEAT frame with
// an empty body as the ping.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

namespace messages {

enum class Result : uint8_t {
    OK = 0,
    NO_SPACE,   // out buffer too small to hold the encoded body
    TOO_SHORT,  // in buffer is not exactly the expected wire length (short OR long: a
                // decode never silently ignores trailing bytes from a mismatched sender)
    BAD_ARG,    // null pointer
};

Result encode_hello(const proto::HelloBody& body, uint8_t* out, size_t out_cap, size_t* out_len);
Result decode_hello(const uint8_t* in, size_t in_len, proto::HelloBody* out);

Result encode_ack(const proto::AckBody& body, uint8_t* out, size_t out_cap, size_t* out_len);
Result decode_ack(const uint8_t* in, size_t in_len, proto::AckBody* out);

Result encode_nack(const proto::NackBody& body, uint8_t* out, size_t out_cap, size_t* out_len);
Result decode_nack(const uint8_t* in, size_t in_len, proto::NackBody* out);

Result encode_heartbeat_status(const proto::HeartbeatBody& body, uint8_t* out, size_t out_cap,
                                size_t* out_len);
Result decode_heartbeat_status(const uint8_t* in, size_t in_len, proto::HeartbeatBody* out);

Result encode_telemetry(const proto::TelemetryBody& body, uint8_t* out, size_t out_cap,
                         size_t* out_len);
Result decode_telemetry(const uint8_t* in, size_t in_len, proto::TelemetryBody* out);

Result encode_event(const proto::EventBody& body, uint8_t* out, size_t out_cap, size_t* out_len);
Result decode_event(const uint8_t* in, size_t in_len, proto::EventBody* out);

// ---- Command bodies (0.3, slice 1 of 3). HOME/TARE/START/PAUSE/RESUME/STOP/RESET have
// empty bodies and need no encode/decode pair -- see protocol.h's command-body comment.
//
// The decode_* functions below only check wire shape (exact length, non-null), same as
// every decode_* above them -- they do NOT enforce the NACK contracts documented next to
// each body in protocol.h (e.g. Err::OUT_OF_RANGE for an invalid profile_id or a nonzero
// ArmBody.cycle_mode, Err::NOT_HOMED for JOG). That enforcement belongs to whatever
// command dispatcher eventually calls these (not written yet -- nothing in src/ref-
// erences any CMD_* ID or these structs today, per the slice-1 firmware-reviewer pass,
// 2026-11-13). Do not wire a dispatcher straight off these decode functions without
// adding that validation layer first.
Result encode_select_profile(const proto::SelectProfileBody& body, uint8_t* out, size_t out_cap,
                              size_t* out_len);
Result decode_select_profile(const uint8_t* in, size_t in_len, proto::SelectProfileBody* out);

Result encode_jog(const proto::JogBody& body, uint8_t* out, size_t out_cap, size_t* out_len);
Result decode_jog(const uint8_t* in, size_t in_len, proto::JogBody* out);

Result encode_arm(const proto::ArmBody& body, uint8_t* out, size_t out_cap, size_t* out_len);
Result decode_arm(const uint8_t* in, size_t in_len, proto::ArmBody* out);

// ---- Command bodies (0.4, slice 2 of 3). Same caveat as the slice-1 comment above:
// decode_preload checks wire shape only, not PreloadBody's NACK contract (documented in
// protocol.h next to the struct) -- not yet enforced anywhere, no dispatcher exists yet.
Result encode_preload(const proto::PreloadBody& body, uint8_t* out, size_t out_cap,
                       size_t* out_len);
Result decode_preload(const uint8_t* in, size_t in_len, proto::PreloadBody* out);

}  // namespace messages
