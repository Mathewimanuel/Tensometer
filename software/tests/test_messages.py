import pytest

from tensometer.wire import messages
from tensometer.wire import protocol as proto


def test_hello_round_trip():
    body = proto.HelloBody(
        version_major=proto.VERSION_MAJOR,
        version_minor=proto.VERSION_MINOR,
        build_id=0xDEADBEEF,
        capabilities=proto.CAP_CORE | proto.CAP_SIM,
        cell_profile=1,
        cpu_hz=600_000_000,
    )
    encoded = messages.encode_hello(body)
    assert len(encoded) == proto.HELLO_WIRE_LEN
    assert messages.decode_hello(encoded) == body


def test_ack_round_trip():
    body = proto.AckBody(acked_msg_id=int(proto.MsgId.CMD_ARM), acked_seq=42)
    encoded = messages.encode_ack(body)
    assert len(encoded) == proto.ACK_WIRE_LEN
    assert messages.decode_ack(encoded) == body


def test_nack_round_trip():
    body = proto.NackBody(
        acked_msg_id=int(proto.MsgId.CMD_START), acked_seq=7, error_code=int(proto.Err.NOT_HOMED)
    )
    encoded = messages.encode_nack(body)
    assert len(encoded) == proto.NACK_WIRE_LEN
    assert messages.decode_nack(encoded) == body


def test_heartbeat_status_round_trip():
    body = proto.HeartbeatBody(
        state=proto.CoreState.RUN,
        lock_owner=proto.LockOwner.PC,
        inhibit_mask=0x00000005,
        fault_cause=int(proto.EventType.NONE),
    )
    encoded = messages.encode_heartbeat_status(body)
    assert len(encoded) == proto.HEARTBEAT_STATUS_WIRE_LEN
    assert messages.decode_heartbeat_status(encoded) == body


def test_telemetry_round_trip():
    body = proto.TelemetryBody(
        timestamp_us=123456789012345,
        position_steps=-1000,
        force_raw_counts=8388608,
        force_raw_mN=-980000,
        force_filtered_mN=-975500,
        state=proto.CoreState.CYCLE,
        flags=proto.TELEM_POSITION_VALID | proto.TELEM_HOMED,
        cycle_index=4821,
    )
    encoded = messages.encode_telemetry(body)
    assert len(encoded) == proto.TELEMETRY_WIRE_LEN
    assert messages.decode_telemetry(encoded) == body


def test_telemetry_negative_force_round_trips():
    body = proto.TelemetryBody(
        timestamp_us=0,
        position_steps=-500,
        force_raw_counts=0,
        force_raw_mN=-12345,
        force_filtered_mN=-12000,
        state=proto.CoreState.IDLE,
        flags=0,
        cycle_index=0,
    )
    decoded = messages.decode_telemetry(messages.encode_telemetry(body))
    assert decoded.position_steps == -500
    assert decoded.force_raw_mN == -12345


def test_event_round_trip():
    body = proto.EventBody(
        timestamp_us=987654321098765,
        type=proto.EventType.SUBSTRATE_BREAK,
        aux=0,
        position_steps=50000,
        force_mN=1234567,
    )
    encoded = messages.encode_event(body)
    assert len(encoded) == proto.EVENT_WIRE_LEN
    assert messages.decode_event(encoded) == body


def test_operator_stop_event_type_round_trips():
    body = proto.EventBody(
        timestamp_us=1, type=proto.EventType.OPERATOR_STOP, aux=0, position_steps=0, force_mN=0
    )
    decoded = messages.decode_event(messages.encode_event(body))
    assert decoded.type == proto.EventType.OPERATOR_STOP


def test_select_profile_round_trip():
    body = proto.SelectProfileBody(profile_id=int(proto.ProfileId.CELL_100KG), coupler_confirmed=1)
    encoded = messages.encode_select_profile(body)
    assert len(encoded) == proto.SELECT_PROFILE_WIRE_LEN
    assert messages.decode_select_profile(encoded) == body


def test_jog_round_trip():
    body = proto.JogBody(direction=proto.JogDir.TOWARD_LIM_NEAR, action=0)
    encoded = messages.encode_jog(body)
    assert len(encoded) == proto.JOG_WIRE_LEN
    assert messages.decode_jog(encoded) == body


def test_jog_stop_round_trip():
    body = proto.JogBody(direction=proto.JogDir.TOWARD_LIM_FAR, action=1)
    assert messages.decode_jog(messages.encode_jog(body)) == body


def test_arm_round_trip():
    # RUN mode: cycle_mode and the cycle_* fields are all zero/ignored.
    body = proto.ArmBody(
        l0_um=50000,
        speed_mm_per_min=200,
        force_limit_mN=880000,
        max_travel_um=20000,
        strain_limit_permille=500,
        cycle_mode=0,
        reserved=0,
        cycle_strain_min_permille=0,
        cycle_strain_max_permille=0,
        cycle_count=0,
    )
    encoded = messages.encode_arm(body)
    assert len(encoded) == proto.ARM_WIRE_LEN
    assert messages.decode_arm(encoded) == body


def test_arm_cycle_mode_round_trip():
    body = proto.ArmBody(
        l0_um=50000,
        speed_mm_per_min=100,
        force_limit_mN=880000,
        max_travel_um=30000,
        strain_limit_permille=1000,
        cycle_mode=1,
        reserved=0,
        cycle_strain_min_permille=50,
        cycle_strain_max_permille=600,
        cycle_count=10000,
    )
    decoded = messages.decode_arm(messages.encode_arm(body))
    assert decoded == body


def test_arm_decode_rejects_truncated_buffer():
    body = proto.ArmBody(1, 2, 3, 4, 5, 0, 0, 0, 0, 0)
    encoded = messages.encode_arm(body)
    with pytest.raises(messages.DecodeError):
        messages.decode_arm(encoded[:-1])


def test_preload_round_trip():
    body = proto.PreloadBody(threshold_mN=150000, approach_mm_per_min=10)
    encoded = messages.encode_preload(body)
    assert len(encoded) == proto.PRELOAD_WIRE_LEN
    assert messages.decode_preload(encoded) == body


def test_preload_decode_rejects_truncated_buffer():
    body = proto.PreloadBody(threshold_mN=150000, approach_mm_per_min=10)
    encoded = messages.encode_preload(body)
    with pytest.raises(messages.DecodeError):
        messages.decode_preload(encoded[:-1])
