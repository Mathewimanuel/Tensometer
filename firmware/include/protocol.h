// Wire protocol shared with the PC app (skill section 8). Core message bodies (HELLO,
// ACK/NACK, HEARTBEAT, TELEMETRY, EVENT) are defined below (0.2); CMD_* bodies and the
// test-config sections are still DRAFT, defined when their consumer is written. Any
// change here must be mirrored on the Python side and bump PROTOCOL_VERSION (minor for
// additive, major for breaking).
//
// Struct layout here is for working with a message in code; it is NOT the wire layout
// (no guarantee against compiler padding). The wire layout is always little-endian,
// field-by-field, exactly as listed per struct below; lib/messages is the only place
// that actually packs/unpacks bytes, so struct padding here is never a correctness
// concern.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "module_api.h"  // mod::CoreState

namespace proto {

constexpr uint8_t VERSION_MAJOR = 0;
constexpr uint8_t VERSION_MINOR = 2;

// Frame payload (before COBS): msg_id u8 | seq u8 | body | crc16 u16 (little-endian).
constexpr uint16_t MAX_BODY_LEN = 240;

// ---- Message ID ranges --------------------------------------------------------------
constexpr uint8_t ID_CORE_FIRST = 0x00, ID_CORE_LAST = 0x3F;
constexpr uint8_t ID_CONTINUITY_FIRST = 0x40, ID_CONTINUITY_LAST = 0x5F;
constexpr uint8_t ID_ROUTING_FIRST = 0x60, ID_ROUTING_LAST = 0x6F;

// ---- Core message IDs (draft) ----------------------------------------------------------
enum class MsgId : uint8_t {
    HELLO = 0x01,
    ACK = 0x02,
    NACK = 0x03,
    HEARTBEAT = 0x04,
    TELEMETRY = 0x10,
    EVENT = 0x11,
    CMD_ARM = 0x20,
    CMD_START = 0x21,
    CMD_PAUSE = 0x22,
    CMD_RESUME = 0x23,
    CMD_STOP = 0x24,
    CMD_HOME = 0x25,
    CMD_JOG = 0x26,
    CMD_TARE = 0x27,
    CMD_RESET = 0x28,
    CMD_SELECT_PROFILE = 0x29,
};

// ---- Capability bits (HELLO) --------------------------------------------------------
constexpr uint8_t CAP_CORE = 1u << 0;
constexpr uint8_t CAP_CONTINUITY = 1u << 1;
constexpr uint8_t CAP_ROUTING = 1u << 2;
constexpr uint8_t CAP_SIM = 1u << 7;

// ---- NACK error codes (draft) -----------------------------------------------------------
enum class Err : uint8_t {
    NONE = 0,
    BAD_CRC = 1,
    BAD_LENGTH = 2,
    UNKNOWN_MSG = 3,
    BAD_STATE = 4,
    OUT_OF_RANGE = 5,
    NOT_HOMED = 6,
    MODULE_ABSENT = 7,
    UNKNOWN_SECTION = 8,
    COUPLER_NOT_CONFIRMED = 9,
    PROFILE_NOT_SELECTABLE = 10,
    MOTION_INHIBITED = 11,
};

// ---- HOLD lock owner (skill 1.3) -----------------------------------------------------
enum class LockOwner : uint8_t { NONE = 0, PC = 1, MODULE = 2 };

// ---- TELEMETRY flags (skill section 4/section 8) -------------------------------------
constexpr uint8_t TELEM_POSITION_VALID = 1u << 0;
constexpr uint8_t TELEM_HOMED = 1u << 1;
constexpr uint8_t TELEM_COUPLER_CONFIRMED = 1u << 2;
constexpr uint8_t TELEM_ALLOW_UNHOMED = 1u << 3;  // debug flag, skill section 4 rule 1

// ---- EVENT types (skill section 7). One tag space for every module: EVENT (0x11) is
// core-ranged, but its type field covers continuity/routing events too, same as the
// rest of this file (single source, mirrored by Python; section 8). NOT wire-compatible
// with break_detect::EventType (lib/break_detect) -- the two are maintained
// independently and already diverge past MAX_FORCE; never static_cast between them, use
// an explicit mapping function when wiring the classifier into comms/SD. -------------------
enum class EventType : uint8_t {
    NONE = 0,
    // Core
    SUBSTRATE_BREAK,
    SLIP,
    MAX_FORCE,
    UNVERIFIED_CLASSIFICATION,
    LIMIT_STOP,
    FORCE_CUTOFF,
    ESTOP,
    HEARTBEAT_LOST,
    SENSOR_STALE,
    SLIP_SUSPECT,
    HOMING_FORCE,
    SYNC,
    LOG_OVERFLOW,
    MODULE_ABSENT,
    FAULT,
    RESET,
    // Continuity (phase 2)
    CONDUCTOR_DEGRADED,
    CONDUCTOR_OPEN,
    CONDUCTOR_RECLOSE,
    CONDUCTOR_SHORT,
    RANGE_SWITCH,
    // Routing (phase 2)
    BODE_SELECT,
    BODE_RELEASE,
};

// ---- Message bodies (core, 0.2) -------------------------------------------------------
// Wire sizes below are the authoritative byte counts lib/messages encodes/decodes;
// sizeof() on these structs is not used for that (see the note at the top of this file).

struct HelloBody {
    uint8_t version_major;
    uint8_t version_minor;
    uint32_t build_id;    // populated by the caller (e.g. compile timestamp); opaque here
    uint8_t capabilities;  // CAP_* bits
    uint8_t cell_profile;  // limits::ProfileId
    uint32_t cpu_hz;       // Teensy core clock (e.g. F_CPU)
};
constexpr size_t HELLO_WIRE_LEN = 1 + 1 + 4 + 1 + 1 + 4;  // 12

// Echoes the command frame's own msg_id/seq (section 8: "echoing seq").
struct AckBody {
    uint8_t acked_msg_id;
    uint8_t acked_seq;
};
constexpr size_t ACK_WIRE_LEN = 1 + 1;  // 2

struct NackBody {
    uint8_t acked_msg_id;
    uint8_t acked_seq;
    uint8_t error_code;  // Err
};
constexpr size_t NACK_WIRE_LEN = 1 + 1 + 1;  // 3

// Firmware -> PC status (skill 8). The PC -> firmware keep-alive is the same msg_id
// with an empty body; only the firmware's own heartbeat carries this struct.
struct HeartbeatBody {
    mod::CoreState state;
    LockOwner lock_owner;
    uint32_t inhibit_mask;
    uint8_t fault_cause;  // EventType that caused the current FAULT, NONE if not faulted
};
constexpr size_t HEARTBEAT_STATUS_WIRE_LEN = 1 + 1 + 4 + 1;  // 7

struct TelemetryBody {
    uint64_t timestamp_us;
    int32_t position_steps;
    int32_t force_raw_counts;    // raw HX711 ADC counts
    int32_t force_raw_mN;        // calibrated, raw path (section 6)
    int32_t force_filtered_mN;   // calibrated, filtered path (section 6)
    mod::CoreState state;
    uint8_t flags;  // TELEM_* bits
};
constexpr size_t TELEMETRY_WIRE_LEN = 8 + 4 + 4 + 4 + 4 + 1 + 1;  // 26

// Generic event frame. `aux` meaning depends on `type` (e.g. a conductor channel index
// for CONDUCTOR_*, an Err code for FAULT) -- documented at each call site that fills it,
// not exhaustively enumerated here, since it varies per type and the layout does not
// change as meanings are assigned.
struct EventBody {
    uint64_t timestamp_us;
    EventType type;
    uint8_t aux;
    int32_t position_steps;
    int32_t force_mN;
};
constexpr size_t EVENT_WIRE_LEN = 8 + 1 + 1 + 4 + 4;  // 18

}  // namespace proto
