"""Thin pyserial driver wiring Session (pure logic, session.py) to a real serial port.

This is the hardware-adjacent thin-driver layer, same split as firmware's src/ vs lib/:
Session decides what everything means; this file only moves bytes and calls it. Not unit
tested the way session.py/wire/ are -- exercised structurally against pyserial's loop://
virtual port (test_transport.py) instead, since there is no real Teensy to test against
yet anyway (no command dispatcher exists on the firmware side -- docs/decisions.md).
"""

from __future__ import annotations

import threading
import time
from collections.abc import Callable
from dataclasses import dataclass

import serial

from . import session as sess
from .wire import frame as wire_frame
from .wire import protocol as proto

# How often the background thread calls Session.tick() between reads -- bounds how late
# a command timeout or heartbeat-ping/loss check can fire relative to when it's actually
# due. Not a protocol value, just a responsiveness/CPU tradeoff.
_POLL_INTERVAL_S = 0.02


@dataclass(frozen=True, slots=True)
class TransportError:
    """The background read/write loop hit a fatal I/O error (e.g. the device was
    unplugged mid-session) and has stopped. Session's own HeartbeatLost doesn't cover
    this case: HeartbeatLost is only noticed by Session.tick(), which is itself only
    ever called from inside this same loop -- once the loop dies, nothing would ever
    call tick() again, so HeartbeatLost would never fire either, leaving the app looking
    silently "connected" forever. Caught by hand, not a test: a real port-open failure
    during manual GUI testing (2026-11-16, docs/decisions.md) surfaced as an unhandled
    traceback before this existed."""

    message: str


EventCallback = Callable[[sess.SessionEvent | TransportError], None]


class Transport:
    """Owns one serial port and one Session. `on_event` is called from the background
    reader thread, not the caller's thread -- a Qt-based caller must marshal this back to
    the GUI thread itself (e.g. via a Qt signal), this class has no Qt dependency."""

    def __init__(
        self,
        port: str | serial.SerialBase,
        baudrate: int = 115200,
        on_event: EventCallback | None = None,
    ):
        # Accepts either a port name (real hardware, pyserial url schemes like
        # "loop://") or an already-open serial-like object, so tests can inject a
        # pre-constructed loop:// instance and write directly to it to simulate
        # incoming firmware bytes -- a port name alone can't do that, since each
        # serial_for_url("loop://") call is its own isolated virtual loopback.
        if isinstance(port, str):
            self._serial = serial.serial_for_url(port, baudrate=baudrate, timeout=_POLL_INTERVAL_S)
        else:
            self._serial = port
        self._session = sess.Session()
        self._reader = wire_frame.Reader()
        self._on_event = on_event or (lambda _event: None)
        self._write_lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True, name="tensometer-transport")

    @property
    def session(self) -> sess.Session:
        return self._session

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=1.0)
        self._serial.close()

    def send_command(self, msg_id: int, body: bytes = b"") -> int:
        """Raises sess.AlreadyPendingError if a command is already pending a response
        (Session's own one-at-a-time rule) -- the caller (GUI) is expected to keep
        action buttons disabled while that's true, not rely on this raising."""
        seq, wire = self._session.begin_command(msg_id, body, now=time.monotonic())
        self._write(wire)
        return seq

    def send_emergency_command(self, msg_id: int, body: bytes = b"") -> int:
        """For CMD_STOP only -- never raises AlreadyPendingError (session.py's
        begin_emergency_command)."""
        seq, wire = self._session.begin_emergency_command(msg_id, body, now=time.monotonic())
        self._write(wire)
        return seq

    def _write(self, data: bytes) -> None:
        with self._write_lock:
            self._serial.write(data)

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                data = self._serial.read(256)
            except (OSError, serial.SerialException) as exc:
                # A read failure (device unplugged, etc.) is fatal -- the loop cannot
                # usefully continue, so it reports the error and stops itself rather
                # than spinning on the same exception forever.
                self._on_event(TransportError(message=str(exc)))
                return
            now = time.monotonic()

            for byte in data:
                completed = self._reader.push(byte)
                if completed is None:
                    continue
                try:
                    f = wire_frame.decode(completed)
                except wire_frame.FrameError:
                    # A corrupted frame (bad CRC, truncated, etc.) is dropped silently,
                    # same as firmware's own comms layer would NACK-or-ignore malformed
                    # input rather than crash -- there is nothing meaningful to do with
                    # an unreadable frame except wait for the next one.
                    continue
                for event in self._session.feed_frame(f, now):
                    self._on_event(event)

            for event in self._session.tick(now):
                if isinstance(event, sess.SendHeartbeatPing):
                    try:
                        self._write(wire_frame.encode(int(proto.MsgId.HEARTBEAT), seq=0, body=b""))
                    except (OSError, serial.SerialException) as exc:
                        self._on_event(TransportError(message=str(exc)))
                        return
                self._on_event(event)
