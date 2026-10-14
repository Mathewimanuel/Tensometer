// Wire protocol shared with the PC app (skill section 8). DRAFT: message bodies are
// defined when each module is written. Any change here must be mirrored on the
// Python side and bump PROTOCOL_VERSION (minor for additive, major for breaking).
#pragma once

#include <stdint.h>

namespace proto {

constexpr uint8_t VERSION_MAJOR = 0;
constexpr uint8_t VERSION_MINOR = 1;

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

}  // namespace proto
