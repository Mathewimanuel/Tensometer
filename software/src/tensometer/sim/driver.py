"""Drives a FakeFirmware instance against any serial-like port object (duck-typed:
read()/write()/close() -- a real pyserial port, a socket-backed one, or a simple
in-memory pipe for testing). Shared by the standalone runner (run_fake_firmware.py) and
the integration test (test_integration_fake_firmware.py), so the actual
read/decode/feed/tick/write loop is written and reviewed exactly once, not duplicated.
"""

from __future__ import annotations

import threading
import time

from ..wire import frame as wire_frame
from ..wire import protocol as proto
from .fake_firmware import FakeFirmware


class FakeFirmwareDriver:
    def __init__(self, port, firmware: FakeFirmware | None = None) -> None:
        self._serial = port
        self.firmware = firmware or FakeFirmware()
        self._reader = wire_frame.Reader()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True, name="fake-firmware-driver")

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=1.0)

    def join(self, timeout: float | None = None) -> None:
        """Blocks until the background thread exits on its own (e.g. a read failure
        when the other end disconnects), without signaling stop() -- for a caller that
        just wants to know when the session ended, not to end it itself."""
        self._thread.join(timeout=timeout)

    def _write_all(self, wires: list[bytes]) -> bool:
        """Returns False (instead of raising) on a write failure -- the other end
        disconnecting is an expected way for this loop to end, not a bug. Caught for
        real (2026-11-16, docs/decisions.md): writes here were unguarded while reads
        already were, so a disconnect mid-tick crashed this thread with an unhandled
        ConnectionAbortedError traceback instead of returning cleanly like a read
        failure already did."""
        try:
            for w in wires:
                self._serial.write(w)
        except Exception:  # noqa: BLE001 -- a dev/test tool, not production code
            return False
        return True

    def _run(self) -> None:
        # Firmware announces itself unprompted once "booted" -- there is no CMD_HELLO,
        # the PC only ever listens for it (skill section 8). The real BOOT -> IDLE
        # self-test sequence isn't modeled; this stand-in just starts already past it.
        self.firmware.state = proto.CoreState.IDLE
        if not self._write_all([self.firmware.hello_frame()]):
            return

        while not self._stop.is_set():
            try:
                data = self._serial.read(256)
            except Exception:  # noqa: BLE001 -- a dev/test tool, not production code;
                return          # any I/O failure here just means "stop driving"
            now = time.monotonic()

            for byte in data:
                completed = self._reader.push(byte)
                if completed is None:
                    continue
                try:
                    f = wire_frame.decode(completed)
                    replies = self.firmware.on_frame(f, now)
                except ValueError:  # FrameError / CobsError / DecodeError: drop the frame
                    continue
                if not self._write_all(replies):
                    return

            if not self._write_all(self.firmware.tick(now)):
                return
