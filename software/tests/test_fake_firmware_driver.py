"""Tests FakeFirmwareDriver's own resilience to I/O failures -- FakeFirmware's protocol
logic is covered by test_fake_firmware.py, the full end-to-end flow by
test_integration_fake_firmware.py. This file is specifically about the driver loop not
crashing when the other end of the connection disappears.
"""

from tensometer.sim.driver import FakeFirmwareDriver


class _PortThatFailsAfterFirstWrite:
    """Accepts the initial HELLO write, then raises on every write after that --
    models a client that connected and immediately vanished. This is exactly what broke
    the real standalone sim server (2026-11-16, docs/decisions.md): the background
    thread crashed with an unhandled ConnectionAbortedError traceback instead of
    returning cleanly, and the whole server process eventually stopped accepting new
    connections."""

    def __init__(self):
        self.write_count = 0

    def read(self, _size: int) -> bytes:
        return b""

    def write(self, data: bytes) -> int:
        self.write_count += 1
        if self.write_count > 1:
            raise ConnectionAbortedError("client gone")
        return len(data)

    def close(self) -> None:
        pass


def test_write_failure_stops_the_thread_cleanly_not_with_an_unhandled_exception():
    port = _PortThatFailsAfterFirstWrite()
    driver = FakeFirmwareDriver(port)
    driver.start()
    driver.join(timeout=2.0)
    assert driver._thread.is_alive() is False
    assert port.write_count >= 2  # HELLO succeeded; the next write (a heartbeat tick) failed


def test_write_all_reports_failure_without_raising():
    port = _PortThatFailsAfterFirstWrite()
    driver = FakeFirmwareDriver(port)
    assert driver._write_all([b"first"]) is True
    assert driver._write_all([b"second"]) is False  # must not raise
