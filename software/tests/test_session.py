import pytest

from tensometer import session as sess
from tensometer.wire import frame as wire_frame
from tensometer.wire import messages
from tensometer.wire import protocol as proto


def _hello_frame(seq: int = 0) -> wire_frame.Frame:
    body = proto.HelloBody(
        version_major=proto.VERSION_MAJOR,
        version_minor=proto.VERSION_MINOR,
        build_id=1,
        capabilities=proto.CAP_CORE,
        cell_profile=0,
        cpu_hz=600_000_000,
    )
    return wire_frame.Frame(msg_id=int(proto.MsgId.HELLO), seq=seq, body=messages.encode_hello(body))


def test_not_connected_before_hello():
    s = sess.Session()
    assert s.connected is False


def test_hello_connects_and_emits_event():
    s = sess.Session()
    events = s.feed_frame(_hello_frame(), now=0.0)
    assert s.connected is True
    assert len(events) == 1
    assert isinstance(events[0], sess.Connected)
    assert events[0].hello.capabilities == proto.CAP_CORE


def test_begin_command_returns_incrementing_seq_and_valid_frame():
    s = sess.Session()
    seq, wire = s.begin_command(int(proto.MsgId.CMD_HOME), b"", now=0.0)
    assert seq == 0
    decoded = wire_frame.decode(wire[:-1])
    assert decoded.msg_id == int(proto.MsgId.CMD_HOME)
    assert decoded.seq == 0
    assert s.has_pending_command() is True


def test_begin_command_raises_when_already_pending():
    s = sess.Session()
    s.begin_command(int(proto.MsgId.CMD_HOME), b"", now=0.0)
    with pytest.raises(sess.AlreadyPendingError):
        s.begin_command(int(proto.MsgId.CMD_TARE), b"", now=0.1)


def test_ack_resolves_pending_command():
    s = sess.Session()
    seq, _ = s.begin_command(int(proto.MsgId.CMD_HOME), b"", now=0.0)
    ack_body = proto.AckBody(acked_msg_id=int(proto.MsgId.CMD_HOME), acked_seq=seq)
    f = wire_frame.Frame(msg_id=int(proto.MsgId.ACK), seq=0, body=messages.encode_ack(ack_body))
    events = s.feed_frame(f, now=0.01)
    assert len(events) == 1
    assert isinstance(events[0], sess.CommandAcked)
    assert events[0].seq == seq
    assert s.has_pending_command() is False


def test_ack_with_mismatched_seq_is_ignored():
    s = sess.Session()
    s.begin_command(int(proto.MsgId.CMD_HOME), b"", now=0.0)
    ack_body = proto.AckBody(acked_msg_id=int(proto.MsgId.CMD_HOME), acked_seq=99)
    f = wire_frame.Frame(msg_id=int(proto.MsgId.ACK), seq=0, body=messages.encode_ack(ack_body))
    events = s.feed_frame(f, now=0.01)
    assert events == []
    assert s.has_pending_command() is True  # the real pending command is untouched


def test_nack_resolves_pending_command_with_error():
    s = sess.Session()
    seq, _ = s.begin_command(int(proto.MsgId.CMD_ARM), b"", now=0.0)
    nack_body = proto.NackBody(
        acked_msg_id=int(proto.MsgId.CMD_ARM),
        acked_seq=seq,
        error_code=int(proto.Err.COUPLER_NOT_CONFIRMED),
    )
    f = wire_frame.Frame(msg_id=int(proto.MsgId.NACK), seq=0, body=messages.encode_nack(nack_body))
    events = s.feed_frame(f, now=0.01)
    assert len(events) == 1
    assert isinstance(events[0], sess.CommandNacked)
    assert events[0].error == proto.Err.COUPLER_NOT_CONFIRMED
    assert s.has_pending_command() is False


def test_command_times_out_via_tick():
    s = sess.Session()
    seq, _ = s.begin_command(int(proto.MsgId.CMD_HOME), b"", now=0.0)
    assert s.tick(now=sess.COMMAND_TIMEOUT_S + 0.5) == [
        sess.CommandTimedOut(seq=seq, msg_id=int(proto.MsgId.CMD_HOME))
    ]
    assert s.has_pending_command() is False


def test_command_does_not_time_out_before_the_timeout():
    s = sess.Session()
    s.begin_command(int(proto.MsgId.CMD_HOME), b"", now=0.0)
    events = s.tick(now=sess.COMMAND_TIMEOUT_S - 0.5)
    assert sess.CommandTimedOut(seq=0, msg_id=int(proto.MsgId.CMD_HOME)) not in events
    assert s.has_pending_command() is True


def test_heartbeat_received_updates_state_and_emits_event():
    s = sess.Session()
    s.feed_frame(_hello_frame(), now=0.0)
    hb_body = proto.HeartbeatBody(
        state=proto.CoreState.RUN,
        lock_owner=proto.LockOwner.PC,
        inhibit_mask=0,
        fault_cause=int(proto.EventType.NONE),
    )
    f = wire_frame.Frame(
        msg_id=int(proto.MsgId.HEARTBEAT), seq=0, body=messages.encode_heartbeat_status(hb_body)
    )
    events = s.feed_frame(f, now=1.0)
    assert len(events) == 1
    assert isinstance(events[0], sess.HeartbeatReceived)
    assert events[0].body.state == proto.CoreState.RUN


def test_pc_ping_heartbeat_empty_body_is_not_mistaken_for_firmware_status():
    s = sess.Session()
    s.feed_frame(_hello_frame(), now=0.0)
    f = wire_frame.Frame(msg_id=int(proto.MsgId.HEARTBEAT), seq=0, body=b"")
    assert s.feed_frame(f, now=1.0) == []


def test_heartbeat_lost_declared_after_timeout():
    s = sess.Session()
    s.feed_frame(_hello_frame(), now=0.0)
    hb_body = proto.HeartbeatBody(
        state=proto.CoreState.RUN,
        lock_owner=proto.LockOwner.PC,
        inhibit_mask=0,
        fault_cause=int(proto.EventType.NONE),
    )
    f = wire_frame.Frame(
        msg_id=int(proto.MsgId.HEARTBEAT), seq=0, body=messages.encode_heartbeat_status(hb_body)
    )
    s.feed_frame(f, now=0.0)
    events = s.tick(now=sess.HEARTBEAT_TIMEOUT_S + 0.5)
    assert sess.HeartbeatLost() in events


def test_heartbeat_lost_only_declared_once():
    s = sess.Session()
    s.feed_frame(_hello_frame(), now=0.0)
    hb_body = proto.HeartbeatBody(
        state=proto.CoreState.RUN,
        lock_owner=proto.LockOwner.PC,
        inhibit_mask=0,
        fault_cause=int(proto.EventType.NONE),
    )
    f = wire_frame.Frame(
        msg_id=int(proto.MsgId.HEARTBEAT), seq=0, body=messages.encode_heartbeat_status(hb_body)
    )
    s.feed_frame(f, now=0.0)
    first = s.tick(now=sess.HEARTBEAT_TIMEOUT_S + 0.5)
    second = s.tick(now=sess.HEARTBEAT_TIMEOUT_S + 0.6)
    assert sess.HeartbeatLost() in first
    assert sess.HeartbeatLost() not in second


def test_send_heartbeat_ping_only_before_connected_is_never_emitted():
    s = sess.Session()
    assert s.tick(now=1.0) == []  # not connected yet -- nothing to ping


def test_send_heartbeat_ping_emitted_once_per_interval():
    s = sess.Session()
    s.feed_frame(_hello_frame(), now=0.0)
    first = s.tick(now=0.0)
    assert sess.SendHeartbeatPing() in first
    # Too soon for another ping.
    second = s.tick(now=0.05)
    assert sess.SendHeartbeatPing() not in second
    # A full interval later, due again.
    third = s.tick(now=0.1 + sess.HEARTBEAT_PING_INTERVAL_S)
    assert sess.SendHeartbeatPing() in third


def test_telemetry_frame_decoded_and_emitted():
    s = sess.Session()
    body = proto.TelemetryBody(
        timestamp_us=1,
        position_steps=0,
        force_raw_counts=0,
        force_raw_mN=0,
        force_filtered_mN=0,
        state=proto.CoreState.RUN,
        flags=0,
        cycle_index=0,
    )
    f = wire_frame.Frame(msg_id=int(proto.MsgId.TELEMETRY), seq=0, body=messages.encode_telemetry(body))
    events = s.feed_frame(f, now=0.0)
    assert len(events) == 1
    assert isinstance(events[0], sess.TelemetryReceived)
    assert events[0].body == body


def test_event_frame_decoded_and_emitted():
    s = sess.Session()
    body = proto.EventBody(
        timestamp_us=1,
        type=proto.EventType.SUBSTRATE_BREAK,
        aux=0,
        position_steps=0,
        force_mN=0,
    )
    f = wire_frame.Frame(msg_id=int(proto.MsgId.EVENT), seq=0, body=messages.encode_event(body))
    events = s.feed_frame(f, now=0.0)
    assert len(events) == 1
    assert isinstance(events[0], sess.EventReceived)
    assert events[0].body.type == proto.EventType.SUBSTRATE_BREAK


def test_unknown_message_id_reported_not_raised():
    s = sess.Session()
    f = wire_frame.Frame(msg_id=0x7F, seq=3, body=b"\x01\x02")
    events = s.feed_frame(f, now=0.0)
    assert len(events) == 1
    assert isinstance(events[0], sess.UnknownMessage)
    assert events[0].msg_id == 0x7F
