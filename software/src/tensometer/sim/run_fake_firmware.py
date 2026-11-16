"""Standalone runner: serves FakeFirmware over a TCP socket so the real tensometer-gui
(or tensometer-cli) can connect to it as if it were a real Teensy, using pyserial's
socket:// URL scheme -- no virtual-COM-port driver needed on any platform.

    tensometer-sim                 # listens on 127.0.0.1:9000
    tensometer-gui                 # then connect to: socket://127.0.0.1:9000

Not a physics simulator (see fake_firmware.py's own docstring) -- just enough to click
through the real GUI against a plausible counterpart before any real command dispatcher
exists in firmware's main.cpp (docs/decisions.md 2026-11-16).
"""

from __future__ import annotations

import argparse
import socket
import sys

from .driver import FakeFirmwareDriver


class SocketPort:
    """Duck-types read()/write()/close() over a plain TCP socket, so FakeFirmwareDriver
    (which only needs that interface) can drive it exactly like a real serial port."""

    def __init__(self, sock: socket.socket, timeout: float = 0.1) -> None:
        self._sock = sock
        self._sock.settimeout(timeout)

    def read(self, size: int) -> bytes:
        try:
            return self._sock.recv(size)
        except TimeoutError:
            return b""
        except OSError:
            return b""

    def write(self, data: bytes) -> int:
        self._sock.sendall(data)
        return len(data)

    def close(self) -> None:
        try:
            self._sock.close()
        except OSError:
            pass


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="tensometer-sim", description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    args = parser.parse_args(argv)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.host, args.port))
        server.listen(1)
        print(f"Listening on {args.host}:{args.port}")
        print(f"Point the GUI/CLI at: socket://{args.host}:{args.port}")
        try:
            while True:
                conn, addr = server.accept()
                print(f"Connected: {addr}")
                driver = FakeFirmwareDriver(SocketPort(conn))
                driver.start()
                driver.join()  # until the client disconnects (its read() then fails)
                conn.close()  # not left for GC to get around to eventually
                print("Disconnected -- waiting for a new connection.")
        except KeyboardInterrupt:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
