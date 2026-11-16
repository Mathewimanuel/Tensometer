"""Wire protocol shared with the firmware (firmware/include/protocol.h, skill section 8).
Mirrors protocol.h 0.7 field-for-field. Hand-mirrored by design, not generated from a
shared schema (docs/decisions.md 2026-11-16) -- any change to protocol.h must be mirrored
here by hand, and PROTOCOL_VERSION must match.

Struct layout here is for working with a message in Python; it is NOT itself the wire
layout (no struct padding concerns in Python, but field order/width still matters).
messages.py is the only place that actually packs/unpacks bytes.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum

VERSION_MAJOR = 0
VERSION_MINOR = 7

# Mirrors protocol.h's own independent MAX_BODY_LEN (same value as frame.MAX_BODY_LEN,
# but a separate constant there too -- protocol.h and frame.h each define their own copy
# in C++, not a shared include; mirrored here the same way rather than importing one
# from the other).
MAX_BODY_LEN = 240

# ---- Message ID ranges --------------------------------------------------------------
ID_CORE_FIRST = 0x00
ID_CORE_LAST = 0x3F
ID_CONTINUITY_FIRST = 0x40
ID_CONTINUITY_LAST = 0x5F
ID_ROUTING_FIRST = 0x60
ID_ROUTING_LAST = 0x6F


class MsgId(IntEnum):
    HELLO = 0x01
    ACK = 0x02
    NACK = 0x03
    HEARTBEAT = 0x04
    TELEMETRY = 0x10
    EVENT = 0x11
    CMD_ARM = 0x20
    CMD_START = 0x21
    CMD_PAUSE = 0x22
    CMD_RESUME = 0x23
    CMD_STOP = 0x24
    CMD_HOME = 0x25
    CMD_JOG = 0x26
    CMD_TARE = 0x27
    CMD_RESET = 0x28
    CMD_SELECT_PROFILE = 0x29
    CMD_PRELOAD = 0x2A


# ---- Capability bits (HELLO) --------------------------------------------------------
CAP_CORE = 1 << 0
CAP_CONTINUITY = 1 << 1
CAP_ROUTING = 1 << 2
CAP_SIM = 1 << 7


class Err(IntEnum):
    NONE = 0
    BAD_CRC = 1
    BAD_LENGTH = 2
    UNKNOWN_MSG = 3
    BAD_STATE = 4
    OUT_OF_RANGE = 5
    NOT_HOMED = 6
    MODULE_ABSENT = 7
    UNKNOWN_SECTION = 8
    COUPLER_NOT_CONFIRMED = 9
    PROFILE_NOT_SELECTABLE = 10
    MOTION_INHIBITED = 11


class LockOwner(IntEnum):
    NONE = 0
    PC = 1
    MODULE = 2


# ---- TELEMETRY flags -----------------------------------------------------------------
TELEM_POSITION_VALID = 1 << 0
TELEM_HOMED = 1 << 1
TELEM_COUPLER_CONFIRMED = 1 << 2
TELEM_ALLOW_UNHOMED = 1 << 3  # debug flag, skill section 4 rule 1


class EventType(IntEnum):
    NONE = 0
    # Core
    SUBSTRATE_BREAK = 1
    SLIP = 2
    MAX_FORCE = 3
    UNVERIFIED_CLASSIFICATION = 4
    LIMIT_STOP = 5
    FORCE_CUTOFF = 6
    ESTOP = 7
    HEARTBEAT_LOST = 8
    SENSOR_STALE = 9
    SLIP_SUSPECT = 10
    HOMING_FORCE = 11
    SYNC = 12
    LOG_OVERFLOW = 13
    MODULE_ABSENT = 14
    FAULT = 15
    RESET = 16
    # Continuity (phase 2)
    CONDUCTOR_DEGRADED = 17
    CONDUCTOR_OPEN = 18
    CONDUCTOR_RECLOSE = 19
    CONDUCTOR_SHORT = 20
    RANGE_SWITCH = 21
    # Routing (phase 2)
    BODE_SELECT = 22
    BODE_RELEASE = 23
    # Core, 0.3: a PC-commanded CMD_STOP, applied as SAFETY_TRIP directly.
    OPERATOR_STOP = 24
    # Core, 0.7 (appended): firmware stopped a jog itself, no KEEPALIVE within the deadman.
    JOG_DEADMAN = 25


class CoreState(IntEnum):
    """Mirrors mod::CoreState (module_api.h)."""

    BOOT = 0
    IDLE = 1
    HOMING = 2
    JOG = 3
    TARE = 4
    READY = 5
    RUN = 6
    HOLD = 7
    CYCLE = 8
    BREAK_HOLD = 9
    FAULT = 10
    PRELOAD = 11


class JogDir(IntEnum):
    """Named after the limit switches, not a +/- sign, matching protocol.h's own
    JogDir -- this file does not know which physical direction either value drives."""

    TOWARD_LIM_FAR = 0
    TOWARD_LIM_NEAR = 1


class ProfileId(IntEnum):
    """Mirrors limits::ProfileId (safety_limits.h) ID values only -- the actual limit
    numbers (cutoff_mN, safe_overload_mN, coupler_slip_mN) are NOT duplicated here.
    Firmware's NACK is the sole authority on whether a profile is valid/selectable;
    duplicating those safety numbers here would risk silent drift between the two
    sides (docs/decisions.md 2026-11-16: protocol mirror stays hand-written, same
    discipline applies to not re-deriving safety facts client-side)."""

    CELL_100KG = 0
    CELL_500KG = 1
    CELL_10KG = 2


# ---- Message bodies (core, 0.2) -------------------------------------------------------
# Wire sizes are the authoritative byte counts messages.py encodes/decodes.


@dataclass(frozen=True, slots=True)
class HelloBody:
    version_major: int
    version_minor: int
    build_id: int
    capabilities: int
    cell_profile: int
    cpu_hz: int


HELLO_WIRE_LEN = 1 + 1 + 4 + 1 + 1 + 4  # 12


@dataclass(frozen=True, slots=True)
class AckBody:
    acked_msg_id: int
    acked_seq: int


ACK_WIRE_LEN = 1 + 1  # 2


@dataclass(frozen=True, slots=True)
class NackBody:
    acked_msg_id: int
    acked_seq: int
    error_code: int  # Err


NACK_WIRE_LEN = 1 + 1 + 1  # 3


@dataclass(frozen=True, slots=True)
class HeartbeatBody:
    state: CoreState
    lock_owner: LockOwner
    inhibit_mask: int
    fault_cause: int  # EventType value, NONE (0) if not faulted -- raw int, matching
    # protocol.h: fault_cause is plain uint8_t, not proto::EventType-typed


HEARTBEAT_STATUS_WIRE_LEN = 1 + 1 + 4 + 1  # 7


@dataclass(frozen=True, slots=True)
class TelemetryBody:
    timestamp_us: int
    position_steps: int
    force_raw_counts: int
    force_raw_mN: int
    force_filtered_mN: int
    state: CoreState
    flags: int  # TELEM_* bits
    cycle_index: int  # appended 0.5; 0 when not in CYCLE mode


TELEMETRY_WIRE_LEN = 8 + 4 + 4 + 4 + 4 + 1 + 1 + 4  # 30


@dataclass(frozen=True, slots=True)
class EventBody:
    timestamp_us: int
    type: EventType
    aux: int
    position_steps: int
    force_mN: int


EVENT_WIRE_LEN = 8 + 1 + 1 + 4 + 4  # 18

# ---- Command bodies (0.3-0.5 -- docs/decisions.md) ------------------------------------
# HOME, TARE, START, PAUSE, RESUME, STOP and RESET all have empty bodies (0 bytes): the
# command itself, plus msg_id/seq, is the whole message. No dataclass needed for these.


@dataclass(frozen=True, slots=True)
class SelectProfileBody:
    profile_id: int  # ProfileId
    coupler_confirmed: int  # bool (0/1)


SELECT_PROFILE_WIRE_LEN = 1 + 1  # 2


@dataclass(frozen=True, slots=True)
class JogBody:
    direction: JogDir  # ignored when action == 1 (STOP)
    action: int  # 0 = START, 1 = STOP


JOG_WIRE_LEN = 1 + 1  # 2

# JogBody.action values (protocol.h JOG_ACTION_*). KEEPALIVE added 0.7.
JOG_ACTION_START = 0
JOG_ACTION_STOP = 1
JOG_ACTION_KEEPALIVE = 2

# 0.6/0.7 jog deadman (protocol.h's JOG comment): firmware stops a jog when no same-direction
# JOG KEEPALIVE arrives within this window (limits::JOG_KEEPALIVE_TIMEOUT_US,
# hand-mirrored, TODO(verify) on the firmware side). The PC re-sends well inside it.
JOG_KEEPALIVE_TIMEOUT_S = 0.25
JOG_KEEPALIVE_INTERVAL_S = 0.1


@dataclass(frozen=True, slots=True)
class ArmBody:
    l0_um: int
    speed_mm_per_min: int
    force_limit_mN: int  # 0 = disabled (profile cutoff only)
    max_travel_um: int
    strain_limit_permille: int  # 0 = disabled
    cycle_mode: int  # 0 = RUN, 1 = CYCLE
    reserved: int  # must be 0
    cycle_strain_min_permille: int  # ignored when cycle_mode == 0
    cycle_strain_max_permille: int  # ignored when cycle_mode == 0
    cycle_count: int  # ignored when cycle_mode == 0


ARM_WIRE_LEN = 4 + 2 + 4 + 4 + 2 + 1 + 1 + 2 + 2 + 4  # 26


@dataclass(frozen=True, slots=True)
class PreloadBody:
    threshold_mN: int
    approach_mm_per_min: int


PRELOAD_WIRE_LEN = 4 + 2  # 6
