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

}  // namespace messages
