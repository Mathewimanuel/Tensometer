from tensometer.sim.fake_firmware import (
    _ARMING_DURATION_S,
    _HOMING_DURATION_S,
    FakeFirmware,
)
from tensometer.wire import frame as wire_frame
from tensometer.wire import messages
from tensometer.wire import protocol as proto


def _cmd_frame(msg_id: proto.MsgId, seq: int, body: bytes = b"") -> wire_frame.Frame:
    return wire_frame.Frame(msg_id=int(msg_id), seq=seq, body=body)


def _decode_all(wires: list[bytes]) -> list[wire_frame.Frame]:
    return [wire_frame.decode(w[:-1]) for w in wires]


def test_starts_in_boot():
    fw = FakeFirmware()
    assert fw.state == proto.CoreState.BOOT


def test_home_from_idle_acks_and_eventually_reports_homed(monkeypatch):
    fw = FakeFirmware()
    fw.state = proto.CoreState.IDLE
    responses = _decode_all(fw.on_frame(_cmd_frame(proto.MsgId.CMD_HOME, 1), now=0.0))
    assert len(responses) == 1
    ack = messages.decode_ack(responses[0].body)
    assert ack.acked_msg_id == int(proto.MsgId.CMD_HOME)
    assert fw.state == proto.CoreState.HOMING

    fw.tick(now=_HOMING_DURATION_S + 0.1)
    assert fw.state == proto.CoreState.IDLE
    assert fw.homed is True
    assert fw.position_steps == 0


def test_home_rejected_when_not_idle():
    fw = FakeFirmware()
    fw.state = proto.CoreState.RUN
    responses = _decode_all(fw.on_frame(_cmd_frame(proto.MsgId.CMD_HOME, 2), now=0.0))
    nack = messages.decode_nack(responses[0].body)
    assert nack.error_code == int(proto.Err.BAD_STATE)


def test_select_profile_nacks_without_coupler_confirmed():
    fw = FakeFirmware()
    body = messages.encode_select_profile(
        proto.SelectProfileBody(profile_id=0, coupler_confirmed=0)
    )
    responses = _decode_all(fw.on_frame(_cmd_frame(proto.MsgId.CMD_SELECT_PROFILE, 1, body), now=0.0))
    nack = messages.decode_nack(responses[0].body)
    assert nack.error_code == int(proto.Err.COUPLER_NOT_CONFIRMED)
    assert fw.coupler_confirmed is False


def test_arm_requires_homed():
    fw = FakeFirmware()
    fw.state = proto.CoreState.IDLE
    fw.homed = False
    body = messages.encode_arm(proto.ArmBody(50_000, 100, 0, 20_000, 0, 0, 0, 0, 0, 0))
    responses = _decode_all(fw.on_frame(_cmd_frame(proto.MsgId.CMD_ARM, 1, body), now=0.0))
    nack = messages.decode_nack(responses[0].body)
    assert nack.error_code == int(proto.Err.NOT_HOMED)


def _home_and_arm(fw: FakeFirmware, now: float = 0.0, **arm_kwargs) -> None:
    fw.state = proto.CoreState.IDLE
    fw.on_frame(_cmd_frame(proto.MsgId.CMD_HOME, 1), now=now)
    fw.tick(now=now + _HOMING_DURATION_S + 0.1)
    assert fw.homed is True

    defaults = {
        "l0_um": 50_000, "speed_mm_per_min": 600, "force_limit_mN": 0, "max_travel_um": 200_000,
        "strain_limit_permille": 0, "cycle_mode": 0, "reserved": 0,
        "cycle_strain_min_permille": 0, "cycle_strain_max_permille": 0, "cycle_count": 0,
    }
    defaults.update(arm_kwargs)
    body = messages.encode_arm(proto.ArmBody(**defaults))
    responses = _decode_all(fw.on_frame(_cmd_frame(proto.MsgId.CMD_ARM, 2, body), now=now))
    ack = messages.decode_ack(responses[0].body)
    assert ack.acked_msg_id == int(proto.MsgId.CMD_ARM)
    fw.tick(now=now + _ARMING_DURATION_S + 0.1)
    assert fw.state == proto.CoreState.READY


def test_arm_then_start_enters_run_and_streams_telemetry_until_break():
    fw = FakeFirmware()
    _home_and_arm(fw)

    responses = _decode_all(fw.on_frame(_cmd_frame(proto.MsgId.CMD_START, 3), now=1.0))
    ack = messages.decode_ack(responses[0].body)
    assert ack.acked_msg_id == int(proto.MsgId.CMD_START)
    assert fw.state == proto.CoreState.RUN

    saw_telemetry = False
    saw_break = False
    t = 1.0
    for _ in range(5000):
        t += 1.0 / 80
        for wire in fw.tick(now=t):
            frame = wire_frame.decode(wire[:-1])
            if frame.msg_id == int(proto.MsgId.TELEMETRY):
                saw_telemetry = True
        if fw.state == proto.CoreState.BREAK_HOLD:
            saw_break = True
            break
    assert saw_telemetry is True
    assert saw_break is True


def test_cycle_mode_increments_cycle_index():
    fw = FakeFirmware()
    _home_and_arm(
        fw,
        cycle_mode=1,
        cycle_strain_min_permille=0,
        cycle_strain_max_permille=100,
        cycle_count=3,
        speed_mm_per_min=6000,  # fast, so the test doesn't need many ticks
    )
    fw.on_frame(_cmd_frame(proto.MsgId.CMD_START, 3), now=1.0)
    assert fw.state == proto.CoreState.CYCLE

    t = 1.0
    for _ in range(2000):
        t += 1.0 / 80
        fw.tick(now=t)
        if fw.cycle_index >= 2:
            break
    assert fw.cycle_index >= 2


def test_stop_faults_from_any_state_and_requires_reset():
    fw = FakeFirmware()
    fw.state = proto.CoreState.RUN
    responses = _decode_all(fw.on_frame(_cmd_frame(proto.MsgId.CMD_STOP, 1), now=0.0))
    ack = messages.decode_ack(responses[0].body)
    assert ack.acked_msg_id == int(proto.MsgId.CMD_STOP)
    assert fw.state == proto.CoreState.FAULT

    reset_responses = _decode_all(fw.on_frame(_cmd_frame(proto.MsgId.CMD_RESET, 2), now=0.0))
    ack2 = messages.decode_ack(reset_responses[0].body)
    assert ack2.acked_msg_id == int(proto.MsgId.CMD_RESET)
    assert fw.state == proto.CoreState.IDLE


def test_pause_resume_round_trip():
    fw = FakeFirmware()
    fw.state = proto.CoreState.RUN
    fw.on_frame(_cmd_frame(proto.MsgId.CMD_PAUSE, 1), now=0.0)
    assert fw.state == proto.CoreState.HOLD
    fw.on_frame(_cmd_frame(proto.MsgId.CMD_RESUME, 2), now=0.0)
    assert fw.state == proto.CoreState.RUN


def test_unknown_message_nacks_instead_of_crashing():
    fw = FakeFirmware()
    responses = _decode_all(fw.on_frame(_cmd_frame(proto.MsgId.CMD_SELECT_PROFILE, 1, b""), now=0.0))
    # An empty body for SELECT_PROFILE is a decode failure, not a valid command.
    nack = messages.decode_nack(responses[0].body)
    assert nack.error_code == int(proto.Err.BAD_LENGTH)


def test_heartbeat_ping_from_pc_gets_no_response():
    fw = FakeFirmware()
    assert fw.on_frame(_cmd_frame(proto.MsgId.HEARTBEAT, 0, b""), now=0.0) == []
