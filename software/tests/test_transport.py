"""Structural tests against pyserial's loop:// virtual port -- there is no real Teensy
to test against (no command dispatcher exists on the firmware side yet), so these only
prove the read/write/threading plumbing works, not that firmware actually responds
correctly to anything (session.py's own tests already cover the protocol logic itself).
"""

import threading
import time

import serial

from tensometer import session as sess
from tensometer import transport as tp
from tensometer.wire import frame as wire_frame
from tensometer.wire import messages
from tensometer.wire import protocol as proto


class _EventCollector:
    """Collects events from the background thread and lets a test wait for a specific
    one instead of sleeping an arbitrary guessed duration."""

    def __init__(self):
        self.events: list[sess.SessionEvent] = []
        self._lock = threading.Lock()
        self._got = threading.Event()

    def __call__(self, event: sess.SessionEvent) -> None:
        with self._lock:
            self.events.append(event)
        self._got.set()

    def wait_for(self, predicate, timeout: float = 2.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self._lock:
                match = next((e for e in self.events if predicate(e)), None)
            if match is not None:
                return match
            self._got.wait(timeout=0.05)
            self._got.clear()
        raise AssertionError(f"timed out waiting for an event matching {predicate}")


def test_send_command_round_trips_through_loopback():
    collector = _EventCollector()
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
    collector = _EventCollector()
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
