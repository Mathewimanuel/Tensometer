"""Structural tests against pyserial's loop:// virtual port -- there is no real Teensy
to test against (no command dispatcher exists on the firmware side yet), so these only
prove the read/write/threading plumbing works, not that firmware actually responds
correctly to anything (session.py's own tests already cover the protocol logic itself).
"""

import serial
from helpers import EventCollector

from tensometer import session as sess
from tensometer import transport as tp
from tensometer.wire import frame as wire_frame
from tensometer.wire import messages
from tensometer.wire import protocol as proto


def test_send_command_round_trips_through_loopback():
    collector = EventCollector()
    t = tp.Transport("loop://", on_event=collector)
    t.start()
    try:
        seq = t.send_command(int(proto.MsgId.CMD_HOME), b"")
        # CMD_HOME isn't one of the types Session knows how to interpret as *incoming*
        # (those are HELLO/ACK/NACK/HEARTBEAT/TELEMETRY/EVENT) -- looped back, it proves
        # the full encode -> write -> read -> frame.Reader -> decode -> Session pipeline
        # works, by arriving as an UnknownMessage with the same msg_id/seq we sent.
        got = collector.wait_for(
            lambda e: isinstance(e, sess.UnknownMessage) and e.seq == seq
        )
        assert got.msg_id == int(proto.MsgId.CMD_HOME)
    finally:
        t.stop()


def test_injected_hello_bytes_produce_connected_event():
    # A pre-opened loop:// instance, shared with the test, so bytes written here are
    # read by the Transport's own background thread -- simulating "firmware sent this".
    fake_port = serial.serial_for_url("loop://", timeout=0.05)
    collector = EventCollector()
    t = tp.Transport(fake_port, on_event=collector)
    t.start()
    try:
        hello = proto.HelloBody(
            version_major=proto.VERSION_MAJOR,
            version_minor=proto.VERSION_MINOR,
            build_id=1,
            capabilities=proto.CAP_CORE,
            cell_profile=0,
            cpu_hz=600_000_000,
        )
        wire = wire_frame.encode(int(proto.MsgId.HELLO), seq=0, body=messages.encode_hello(hello))
        fake_port.write(wire)

        got = collector.wait_for(lambda e: isinstance(e, sess.Connected))
        assert got.hello.capabilities == proto.CAP_CORE
        assert t.session.connected is True
    finally:
        t.stop()


def test_stop_closes_the_port_and_joins_the_thread():
    t = tp.Transport("loop://")
    t.start()
    t.stop()
    assert t._serial.is_open is False
    assert t._thread.is_alive() is False


class _FailingSerial:
    """A minimal serial-like stand-in whose read() raises -- simulates a device being
    unplugged mid-session, which caught a real gap: the background thread had no error
    handling at all before this (docs/decisions.md 2026-11-16)."""

    def read(self, _size: int) -> bytes:
        raise OSError("device disconnected")

    def write(self, _data: bytes) -> int:
        return 0

    def close(self) -> None:
        pass


def test_read_failure_emits_transport_error_and_stops_the_thread():
    collector = EventCollector()
    t = tp.Transport(_FailingSerial(), on_event=collector)
    t.start()
    got = collector.wait_for(lambda e: isinstance(e, tp.TransportError))
    assert "disconnected" in got.message
    # The thread must have stopped itself (no infinite exception loop), not still be
    # spinning on the same read() error.
    t._thread.join(timeout=1.0)
    assert t._thread.is_alive() is False
