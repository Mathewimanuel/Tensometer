"""Encode/decode for the message bodies defined in protocol.py (mirrors protocol.h
0.5 and firmware/lib/messages). Every function below packs/unpacks the exact
little-endian wire layout documented next to each struct in protocol.py.

Unlike firmware/lib/messages' byte-cursor Writer/Reader (needed in C++ to avoid struct
padding and because there's no bounds-checked native binary codec), Python's `struct`
module already does exact-width little-endian packing natively, so each body gets a
single `struct.pack`/`struct.unpack` call with a format string -- verified against each
body's *_WIRE_LEN constant by the module-level assertions below, so a mismatched format
string fails at import time, not silently at runtime.
"""

from __future__ import annotations

import struct

from . import protocol as proto


class DecodeError(ValueError):
    """Wrong number of bytes for the body being decoded."""


def _check_len(data: bytes, expected: int, name: str) -> None:
    if len(data) != expected:
        raise DecodeError(f"{name} body must be {expected} bytes, got {len(data)}")


# ---- HELLO -----------------------------------------------------------------------
_HELLO_FMT = "<BBIBBI"
assert struct.calcsize(_HELLO_FMT) == proto.HELLO_WIRE_LEN


def encode_hello(body: proto.HelloBody) -> bytes:
    return struct.pack(
        _HELLO_FMT,
        body.version_major,
        body.version_minor,
        body.build_id,
        body.capabilities,
        body.cell_profile,
        body.cpu_hz,
    )


def decode_hello(data: bytes) -> proto.HelloBody:
    _check_len(data, proto.HELLO_WIRE_LEN, "HELLO")
    fields = struct.unpack(_HELLO_FMT, data)
    return proto.HelloBody(*fields)


# ---- ACK / NACK --------------------------------------------------------------------
_ACK_FMT = "<BB"
assert struct.calcsize(_ACK_FMT) == proto.ACK_WIRE_LEN


def encode_ack(body: proto.AckBody) -> bytes:
    return struct.pack(_ACK_FMT, body.acked_msg_id, body.acked_seq)


def decode_ack(data: bytes) -> proto.AckBody:
    _check_len(data, proto.ACK_WIRE_LEN, "ACK")
    return proto.AckBody(*struct.unpack(_ACK_FMT, data))


_NACK_FMT = "<BBB"
assert struct.calcsize(_NACK_FMT) == proto.NACK_WIRE_LEN


def encode_nack(body: proto.NackBody) -> bytes:
    return struct.pack(_NACK_FMT, body.acked_msg_id, body.acked_seq, body.error_code)


def decode_nack(data: bytes) -> proto.NackBody:
    _check_len(data, proto.NACK_WIRE_LEN, "NACK")
    return proto.NackBody(*struct.unpack(_NACK_FMT, data))


# ---- HEARTBEAT (firmware -> PC status body) -----------------------------------------
_HEARTBEAT_FMT = "<BBIB"
assert struct.calcsize(_HEARTBEAT_FMT) == proto.HEARTBEAT_STATUS_WIRE_LEN


def encode_heartbeat_status(body: proto.HeartbeatBody) -> bytes:
    return struct.pack(
        _HEARTBEAT_FMT,
        int(body.state),
        int(body.lock_owner),
        body.inhibit_mask,
        body.fault_cause,
    )


def decode_heartbeat_status(data: bytes) -> proto.HeartbeatBody:
    _check_len(data, proto.HEARTBEAT_STATUS_WIRE_LEN, "HEARTBEAT status")
    state, lock_owner, inhibit_mask, fault_cause = struct.unpack(_HEARTBEAT_FMT, data)
    return proto.HeartbeatBody(
        state=proto.CoreState(state),
        lock_owner=proto.LockOwner(lock_owner),
        inhibit_mask=inhibit_mask,
        fault_cause=fault_cause,
    )


# ---- TELEMETRY -----------------------------------------------------------------------
_TELEMETRY_FMT = "<QiiiiBBI"
assert struct.calcsize(_TELEMETRY_FMT) == proto.TELEMETRY_WIRE_LEN


def encode_telemetry(body: proto.TelemetryBody) -> bytes:
    return struct.pack(
        _TELEMETRY_FMT,
        body.timestamp_us,
        body.position_steps,
        body.force_raw_counts,
        body.force_raw_mN,
        body.force_filtered_mN,
        int(body.state),
        body.flags,
        body.cycle_index,
    )


def decode_telemetry(data: bytes) -> proto.TelemetryBody:
    _check_len(data, proto.TELEMETRY_WIRE_LEN, "TELEMETRY")
    (
        timestamp_us,
        position_steps,
        force_raw_counts,
        force_raw_mN,
        force_filtered_mN,
        state,
        flags,
        cycle_index,
    ) = struct.unpack(_TELEMETRY_FMT, data)
    return proto.TelemetryBody(
        timestamp_us=timestamp_us,
        position_steps=position_steps,
        force_raw_counts=force_raw_counts,
        force_raw_mN=force_raw_mN,
        force_filtered_mN=force_filtered_mN,
        state=proto.CoreState(state),
        flags=flags,
        cycle_index=cycle_index,
    )


# ---- EVENT -----------------------------------------------------------------------
_EVENT_FMT = "<QBBii"
assert struct.calcsize(_EVENT_FMT) == proto.EVENT_WIRE_LEN


def encode_event(body: proto.EventBody) -> bytes:
    return struct.pack(
        _EVENT_FMT,
        body.timestamp_us,
        int(body.type),
        body.aux,
        body.position_steps,
        body.force_mN,
    )


def decode_event(data: bytes) -> proto.EventBody:
    _check_len(data, proto.EVENT_WIRE_LEN, "EVENT")
    timestamp_us, type_, aux, position_steps, force_mN = struct.unpack(_EVENT_FMT, data)
    return proto.EventBody(
        timestamp_us=timestamp_us,
        type=proto.EventType(type_),
        aux=aux,
        position_steps=position_steps,
        force_mN=force_mN,
    )


# ---- Command bodies (0.3-0.5) --------------------------------------------------------
_SELECT_PROFILE_FMT = "<BB"
assert struct.calcsize(_SELECT_PROFILE_FMT) == proto.SELECT_PROFILE_WIRE_LEN


def encode_select_profile(body: proto.SelectProfileBody) -> bytes:
    return struct.pack(_SELECT_PROFILE_FMT, body.profile_id, body.coupler_confirmed)


def decode_select_profile(data: bytes) -> proto.SelectProfileBody:
    _check_len(data, proto.SELECT_PROFILE_WIRE_LEN, "SELECT_PROFILE")
    return proto.SelectProfileBody(*struct.unpack(_SELECT_PROFILE_FMT, data))


_JOG_FMT = "<BB"
assert struct.calcsize(_JOG_FMT) == proto.JOG_WIRE_LEN


def encode_jog(body: proto.JogBody) -> bytes:
    return struct.pack(_JOG_FMT, int(body.direction), body.action)


def decode_jog(data: bytes) -> proto.JogBody:
    _check_len(data, proto.JOG_WIRE_LEN, "JOG")
    direction, action = struct.unpack(_JOG_FMT, data)
    return proto.JogBody(direction=proto.JogDir(direction), action=action)


_ARM_FMT = "<IHIIHBBHHI"
assert struct.calcsize(_ARM_FMT) == proto.ARM_WIRE_LEN


def encode_arm(body: proto.ArmBody) -> bytes:
    return struct.pack(
        _ARM_FMT,
        body.l0_um,
        body.speed_mm_per_min,
        body.force_limit_mN,
        body.max_travel_um,
        body.strain_limit_permille,
        body.cycle_mode,
        body.reserved,
        body.cycle_strain_min_permille,
        body.cycle_strain_max_permille,
        body.cycle_count,
    )


def decode_arm(data: bytes) -> proto.ArmBody:
    _check_len(data, proto.ARM_WIRE_LEN, "ARM")
    fields = struct.unpack(_ARM_FMT, data)
    return proto.ArmBody(*fields)


_PRELOAD_FMT = "<IH"
assert struct.calcsize(_PRELOAD_FMT) == proto.PRELOAD_WIRE_LEN


def encode_preload(body: proto.PreloadBody) -> bytes:
    return struct.pack(_PRELOAD_FMT, body.threshold_mN, body.approach_mm_per_min)


def decode_preload(data: bytes) -> proto.PreloadBody:
    _check_len(data, proto.PRELOAD_WIRE_LEN, "PRELOAD")
    return proto.PreloadBody(*struct.unpack(_PRELOAD_FMT, data))
