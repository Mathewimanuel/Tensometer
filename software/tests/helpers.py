"""Shared test helpers for anything exercising a background thread (transport.py,
controller.py) against pyserial's loop:// virtual port, or a true two-party pipe
(test_integration_fake_firmware.py) where loop:// can't be used -- loop:// only ever
echoes what one side itself wrote back to that same side, it has no concept of "the
other party sent something", so it can't simulate PC <-> firmware as two distinct ends.
"""

import queue
import threading
import time

from tensometer import session as sess


class EventCollector:
    """Collects events from a background thread and lets a test wait for a specific
    one instead of sleeping an arbitrary guessed duration."""

    def __init__(self):
        self.events: list[sess.SessionEvent] = []
        self._lock = threading.Lock()
        self._got = threading.Event()

    def __call__(self, event: sess.SessionEvent) -> None:
        with self._lock:
            self.events.append(event)
        self._got.set()

    def mark(self) -> int:
        """A cursor into events(), for wait_for(after=...) -- call this right before
        triggering an action whose resulting event could be confused with an earlier,
        unrelated occurrence of something matching the same predicate (e.g. "state is
        IDLE" is also true before anything has happened at all, not just after the
        action completes). Caught for real in test_integration_fake_firmware.py
        (2026-11-16, docs/decisions.md): a stale pre-existing IDLE heartbeat satisfied a
        wait_for() meant to detect homing finishing, letting the test race ahead and
        send the next command while HOME was still pending."""
        with self._lock:
            return len(self.events)

    def wait_for(self, predicate, timeout: float = 2.0, after: int = 0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self._lock:
                match = next((e for e in self.events[after:] if predicate(e)), None)
            if match is not None:
                return match
            self._got.wait(timeout=0.05)
            self._got.clear()
        raise AssertionError(f"timed out waiting for an event matching {predicate}")


class PipeEndpoint:
    """One end of an in-memory bidirectional byte pipe. Duck-types enough of pyserial's
    interface (read/write/close) for Transport/FakeFirmwareDriver to use directly --
    doesn't need to subclass serial.SerialBase, since nothing checks the type at
    runtime, only calls these three methods."""

    def __init__(self, inbox: queue.Queue, outbox: queue.Queue) -> None:
        self._inbox = inbox
        self._outbox = outbox
        self._buf = bytearray()
        self.is_open = True

    def write(self, data: bytes) -> int:
        self._outbox.put(bytes(data))
        return len(data)

    def read(self, size: int) -> bytes:
        if not self._buf:
            try:
                self._buf.extend(self._inbox.get(timeout=0.05))
            except queue.Empty:
                return b""
        n = min(size, len(self._buf))
        result = bytes(self._buf[:n])
        del self._buf[:n]
        return result

    def close(self) -> None:
        self.is_open = False


def make_pipe_pair() -> tuple[PipeEndpoint, PipeEndpoint]:
    """Two connected endpoints: bytes written to one are read from the other, and vice
    versa -- a real two-party link, unlike loop://."""
    q_a_to_b: queue.Queue = queue.Queue()
    q_b_to_a: queue.Queue = queue.Queue()
    a = PipeEndpoint(inbox=q_b_to_a, outbox=q_a_to_b)
    b = PipeEndpoint(inbox=q_a_to_b, outbox=q_b_to_a)
    return a, b
