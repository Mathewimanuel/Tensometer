import pytest
import serial
from helpers import EventCollector

from tensometer import controller as ctrl
from tensometer import session as sess
from tensometer.wire import frame as wire_frame
from tensometer.wire import messages
from tensometer.wire import protocol as proto


def _hello_wire(seq: int = 0) -> bytes:
    body = proto.HelloBody(
        version_major=proto.VERSION_MAJOR,
        version_minor=proto.VERSION_MINOR,
        build_id=1,
        capabilities=proto.CAP_CORE,
        cell_profile=0,
        cpu_hz=600_000_000,
    )
    return wire_frame.encode(int(proto.MsgId.HELLO), seq=seq, body=messages.encode_hello(body))


def _connected_controller(collector: EventCollector) -> tuple[ctrl.Controller, serial.SerialBase]:
    """A Controller wired to a loop:// port the test can also write to directly, already
    past the HELLO handshake -- so action tests don't each have to repeat the inject-and-
    wait boilerplate."""
    fake_port = serial.serial_for_url("loop://", timeout=0.05)
    c = ctrl.Controller(fake_port, on_event=collector)
    c.connect()
    fake_port.write(_hello_wire())
    collector.wait_for(lambda e: isinstance(e, sess.Connected))
    return c, fake_port


def test_actions_raise_before_connected():
    c = ctrl.Controller("loop://")
    c.connect()
    try:
        with pytest.raises(ctrl.NotConnectedError):
            c.home()
    finally:
        c.disconnect()


def test_connect_updates_hello_and_allows_actions():
    fake_port = serial.serial_for_url("loop://", timeout=0.05)
    collector = EventCollector()
    c = ctrl.Controller(fake_port, on_event=collector)
    c.connect()
    try:
        fake_port.write(_hello_wire())
        collector.wait_for(lambda e: isinstance(e, sess.Connected))
        assert c.connected is True
        assert c.hello is not None
        assert c.hello.capabilities == proto.CAP_CORE

        seq = c.home()  # must not raise now
        got = collector.wait_for(lambda e: isinstance(e, sess.UnknownMessage) and e.seq == seq)
        assert got.msg_id == int(proto.MsgId.CMD_HOME)
    finally:
        c.disconnect()


def test_tracked_state_updates_from_heartbeat():
    fake_port = serial.serial_for_url("loop://", timeout=0.05)
    collector = EventCollector()
    c = ctrl.Controller(fake_port, on_event=collector)
    c.connect()
    try:
        fake_port.write(_hello_wire())
        collector.wait_for(lambda e: isinstance(e, sess.Connected))

        hb = proto.HeartbeatBody(
            state=proto.CoreState.RUN,
            lock_owner=proto.LockOwner.PC,
            inhibit_mask=0x02,
            fault_cause=int(proto.EventType.NONE),
        )
        wire = wire_frame.encode(
            int(proto.MsgId.HEARTBEAT), seq=1, body=messages.encode_heartbeat_status(hb)
        )
        fake_port.write(wire)
        collector.wait_for(lambda e: isinstance(e, sess.HeartbeatReceived))

        assert c.state == proto.CoreState.RUN
        assert c.lock_owner == proto.LockOwner.PC
        assert c.inhibit_mask == 0x02
    finally:
        c.disconnect()


def test_arm_config_converts_units_correctly_round_trip():
    collector = EventCollector()
    c, _fake_port = _connected_controller(collector)
    try:
        config = ctrl.ArmConfig(
            l0_mm=50.0,
            speed_mm_per_min=100.0,
            max_travel_mm=30.0,
            force_limit_N=880.0,
            strain_limit_percent=100.0,
            cycle_mode=True,
            cycle_strain_min_percent=5.0,
            cycle_strain_max_percent=60.0,
            cycle_count=10000,
        )
        seq = c.arm(config)
        got = collector.wait_for(lambda e: isinstance(e, sess.UnknownMessage) and e.seq == seq)
        decoded = messages.decode_arm(got.body)
        assert decoded.l0_um == 50_000
        assert decoded.speed_mm_per_min == 100
        assert decoded.force_limit_mN == 880_000
        assert decoded.max_travel_um == 30_000
        assert decoded.strain_limit_permille == 1000
        assert decoded.cycle_mode == 1
        assert decoded.cycle_strain_min_permille == 50
        assert decoded.cycle_strain_max_permille == 600
        assert decoded.cycle_count == 10000
    finally:
        c.disconnect()


def _inject_ack(fake_port: serial.SerialBase, collector: EventCollector, msg_id: int, seq: int) -> None:
    """Simulates firmware ACKing the given command -- a real response is required before
    Controller will allow the next command (Session's one-at-a-time rule)."""
    ack = proto.AckBody(acked_msg_id=msg_id, acked_seq=seq)
    fake_port.write(wire_frame.encode(int(proto.MsgId.ACK), seq=0, body=messages.encode_ack(ack)))
    collector.wait_for(lambda e: isinstance(e, sess.CommandAcked) and e.seq == seq)


def test_jog_start_and_stop_send_correct_bodies():
    collector = EventCollector()
    c, fake_port = _connected_controller(collector)
    try:
        seq = c.jog_start(proto.JogDir.TOWARD_LIM_NEAR)
        got = collector.wait_for(lambda e: isinstance(e, sess.UnknownMessage) and e.seq == seq)
        decoded = messages.decode_jog(got.body)
        assert decoded.direction == proto.JogDir.TOWARD_LIM_NEAR
        assert decoded.action == 0
        _inject_ack(fake_port, collector, int(proto.MsgId.CMD_JOG), seq)

        seq2 = c.jog_stop()
        got2 = collector.wait_for(lambda e: isinstance(e, sess.UnknownMessage) and e.seq == seq2)
        assert messages.decode_jog(got2.body).action == 1
    finally:
        c.disconnect()


def test_second_command_raises_while_one_pending():
    collector = EventCollector()
    c, _fake_port = _connected_controller(collector)
    try:
        c.home()  # pending, nothing will ACK it on a bare loopback
        with pytest.raises(sess.AlreadyPendingError):
            c.tare()
    finally:
        c.disconnect()


def test_stop_does_not_wait_behind_a_pending_command():
    collector = EventCollector()
    c, _fake_port = _connected_controller(collector)
    try:
        c.home()  # left pending, never ACKed
        seq = c.stop()  # must not raise AlreadyPendingError
        got = collector.wait_for(lambda e: isinstance(e, sess.UnknownMessage) and e.seq == seq)
        assert got.msg_id == int(proto.MsgId.CMD_STOP)
    finally:
        c.disconnect()


def test_jog_keepalive_and_stop_do_not_wait_behind_a_pending_jog_start():
    # protocol 0.6 deadman: keep-alives must flow while the previous one is still pending,
    # and a release must never be blocked behind one (it goes the same urgent path as STOP).
    collector = EventCollector()
    c, _fake_port = _connected_controller(collector)
    try:
        c.jog_start(proto.JogDir.TOWARD_LIM_FAR)  # left pending, never ACKed
        seq = c.jog_keepalive(proto.JogDir.TOWARD_LIM_FAR)  # must not raise
        got = collector.wait_for(lambda e: isinstance(e, sess.UnknownMessage) and e.seq == seq)
        decoded = messages.decode_jog(got.body)
        assert decoded.direction == proto.JogDir.TOWARD_LIM_FAR
        assert decoded.action == proto.JOG_ACTION_KEEPALIVE  # 0.7: never a second START

        seq2 = c.jog_stop()  # still pending keep-alive: must not raise either
        got2 = collector.wait_for(lambda e: isinstance(e, sess.UnknownMessage) and e.seq == seq2)
        assert messages.decode_jog(got2.body).action == 1
    finally:
        c.disconnect()


def test_stop_still_requires_a_real_connection():
    c = ctrl.Controller("loop://")
    c.connect()
    try:
        with pytest.raises(ctrl.NotConnectedError):
            c.stop()
    finally:
        c.disconnect()
