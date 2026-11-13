// Wire protocol shared with the PC app (skill section 8). Core message bodies (HELLO,
// ACK/NACK, HEARTBEAT, TELEMETRY, EVENT) are defined below (0.2). CMD_* bodies, slice 1
// of 3 (0.3, docs/decisions.md): SELECT_PROFILE, HOME, JOG, TARE, ARM (RUN mode only),
// START, PAUSE, RESUME, STOP, RESET. Slice 2 (0.4): PRELOAD. Still DRAFT: ARM's CYCLE
// mode + its config fields (slice 3). Any change here must be mirrored on the Python
// side and bump PROTOCOL_VERSION (minor for additive, major for breaking).
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
constexpr uint8_t VERSION_MINOR = 4;

// Frame payload (before COBS): msg_id u8 | seq u8 | body | crc16 u16 (little-endian).
constexpr uint16_t MAX_BODY_LEN = 240;

// ---- Message ID ranges --------------------------------------------------------------
constexpr uint8_t ID_CORE_FIRST = 0x00, ID_CORE_LAST = 0x3F;
constexpr uint8_t ID_CONTINUITY_FIRST = 0x40, ID_CONTINUITY_LAST = 0x5F;
constexpr uint8_t ID_ROUTING_FIRST = 0x60, ID_ROUTING_LAST = 0x6F;

// ---- Core message IDs. All bodies defined (0.4) -------------------------------------
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
    CMD_PRELOAD = 0x2A,
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
    // Core, added 0.3 (docs/decisions.md): a PC-commanded CMD_STOP, applied as
    // Event::SAFETY_TRIP directly rather than through core_safety::evaluate() -- this is
    // a commanded fault, not a sensed one, so it has no FaultReason counterpart. Appended
    // at the end rather than grouped with the other "Core" entries above, so no existing
    // value's numeric encoding shifts (same append-only convention as mod::CoreState and
    // mod::ConductorEvent).
    OPERATOR_STOP,
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

// ---- Command bodies (0.3, slice 1 of 3 -- docs/decisions.md) -------------------------
// HOME, TARE, START, PAUSE, RESUME, STOP and RESET all have empty bodies (0 bytes): the
// command itself, plus msg_id/seq, is the whole message. Same pattern as the PC's
// HEARTBEAT ping (lib/messages' own top comment) -- callers use frame::encode() directly
// with body_len == 0, no encode/decode pair needed for these.
//
// CMD_STOP always applies Event::SAFETY_TRIP directly (accepted from any state, logged
// with EventType::OPERATOR_STOP) -- an operator abort, not a sensed core-safety fault,
// so it needs RESET afterward like any other FAULT. CMD_HOME is NACKed (Err::OUT_OF_RANGE)
// if the firmware's own |force| < 2 N guard (skill 3.4) isn't met; it does not take a
// body to express that -- the guard is evaluated against live force, not a PC-supplied
// value. CMD_START/PAUSE/RESUME/RESET NACK with Err::BAD_STATE per lib/state_machine's
// own guards (RESUME's inhibit-mask check is a soft NACK, not a FAULT, per skill 1.3).

struct SelectProfileBody {
    uint8_t profile_id;         // limits::ProfileId; NACK Err::OUT_OF_RANGE if invalid
    uint8_t coupler_confirmed;  // bool (0/1); NACK Err::COUPLER_NOT_CONFIRMED if 0, or
                                 // Err::PROFILE_NOT_SELECTABLE if the profile's own
                                 // coupler setting is unknown (safety_limits.h)
};
constexpr size_t SELECT_PROFILE_WIRE_LEN = 1 + 1;  // 2

// Named after the limit switches, not a +/- sign, matching homing::Dir's own convention
// in lib/homing/src/homing.h and for the same reason: this file does not know which
// physical direction either value drives. Kept as protocol.h's own copy rather than
// depending on lib/homing, to keep core/module isolation (skill 1.2).
enum class JogDir : uint8_t { TOWARD_LIM_FAR = 0, TOWARD_LIM_NEAR = 1 };

struct JogBody {
    JogDir direction;  // ignored when action == 1 (STOP)
    uint8_t action;     // 0 = START, 1 = STOP
};
constexpr size_t JOG_WIRE_LEN = 1 + 1;  // 2
// JOG_START NACKs: Err::NOT_HOMED (state_machine's homed_and_valid guard fails),
// Err::BAD_STATE (not IDLE/READY), Err::MOTION_INHIBITED (inhibit mask set -- JOG_START
// is on #11's per-event inhibit-enforcement list). JOG_STOP NACKs Err::BAD_STATE if not
// currently JOG. Jog speed itself is never sent by the PC: unloaded jog runs at the
// homing fast-approach speed, auto-capped to cfg::JOG_LOADED_MAX_MM_PER_MIN whenever
// |force| exceeds limits::JOG_LOADED_THRESHOLD_PERMILLE of the active profile's cutoff,
// re-evaluated every tick -- entirely firmware-internal (skill 3.4/decisions.md 2026-10-15).

// ARM's core config section (skill 8: "core section (always) + optional continuity and
// routing sections"). cycle_mode must be 0 (RUN) until CYCLE support lands in slice 3 --
// NACK Err::OUT_OF_RANGE on any other value for now.
struct ArmBody {
    uint32_t l0_um;                 // gauge length from home; FW moves here before ARM
                                      // completes (arming(), skill 3.4/decisions.md 2026-10-26).
                                      // NOTE: "L0 within stroke" cannot be enforced yet --
                                      // cfg::JAW_GAP_AT_HOME_UM is still 0/unmeasured (#6).
    uint16_t speed_mm_per_min;        // test ramp speed; NACK OUT_OF_RANGE if >
                                        // cfg::SPEED_MAX_MM_PER_MIN
    uint32_t force_limit_mN;          // 0 = disabled (profile cutoff only); else must be
                                        // <= the active profile's cutoff_mN
    uint32_t max_travel_um;           // soft travel ceiling from L0; required > 0
    uint16_t strain_limit_permille;   // 0 = disabled
    uint8_t cycle_mode;                 // 0 = RUN (only supported value until slice 3)
    uint8_t reserved;                   // must be 0; reject nonzero once a use is assigned
};
constexpr size_t ARM_WIRE_LEN = 4 + 2 + 4 + 4 + 2 + 1 + 1;  // 18

// PRELOAD (0.4, slice 2 of 3 -- docs/decisions.md). Same homed_and_valid/IDLE-READY
// guard as JOG/TARE (skill rev 5: "same precondition as JOG/TARE"); reuses the
// homing-style force-guard pattern (creep at a slow speed, controlled stop at a force
// threshold) rather than a new motion primitive.
struct PreloadBody {
    uint32_t threshold_mN;          // > 0; NACK OUT_OF_RANGE if > the active profile's cutoff_mN
    uint16_t approach_mm_per_min;   // > 0; NACK OUT_OF_RANGE if > cfg::JOG_LOADED_MAX_MM_PER_MIN
                                      // -- reusing that cap rather than a dedicated constant:
                                      // preload approach is the same kind of slow, loaded
                                      // creep as a loaded jog (Mathew, 2026-11-13)
};
constexpr size_t PRELOAD_WIRE_LEN = 4 + 2;  // 6
// PRELOAD_START NACKs: Err::NOT_HOMED, Err::BAD_STATE (not IDLE/READY),
// Err::MOTION_INHIBITED (on #11's per-event inhibit-enforcement list), Err::OUT_OF_RANGE
// (threshold_mN or approach_mm_per_min out of range, per field comments above). Threshold
// reached / PRELOAD_DONE is caller logic (future motion_controller), same as homing's own
// force guard -- this file only defines the wire contract.

}  // namespace proto
