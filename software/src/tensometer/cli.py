"""Minimal runnable entry point: opens a serial port, connects, and prints every
decoded event to stdout. Exists so there is something to actually execute before the
GUI is built -- not the PC app itself, a debug console. Run with:

    tensometer-cli <port>
    tensometer-cli --list

Against real hardware, this will show HELLO/Connected the moment the Teensy sends its
HELLO, but nothing beyond that yet -- main.cpp has no command dispatcher, so sending any
CMD_* from here will not get an ACK/NACK back until that's built.
"""

from __future__ import annotations

import argparse
import sys
import time

import serial.tools.list_ports

from . import session as sess
from . import transport as tp


def _print_event(event: sess.SessionEvent) -> None:
    print(f"{time.strftime('%H:%M:%S')}  {event}")


def _list_ports() -> None:
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return
    for p in ports:
        print(f"{p.device}  {p.description}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="tensometer-cli", description=__doc__)
    parser.add_argument("port", nargs="?", help="Serial port, e.g. COM5 or /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--list", action="store_true", help="List available serial ports and exit")
    args = parser.parse_args(argv)

    if args.list or not args.port:
        _list_ports()
        if not args.port:
            return 0 if args.list else 2
        return 0

    print(f"Connecting to {args.port} at {args.baud} baud. Ctrl+C to quit.")
    transport = tp.Transport(args.port, baudrate=args.baud, on_event=_print_event)
    transport.start()
    try:
        while True:
            time.sleep(0.2)
    except KeyboardInterrupt:
        pass
    finally:
        transport.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
