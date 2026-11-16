"""Shared test helpers for anything exercising a background thread (transport.py,
controller.py) against pyserial's loop:// virtual port."""

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
