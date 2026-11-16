"""Frame layer for the firmware link (protocol.h/frame.h, skill section 8):
    payload = msg_id (u8) | seq (u8) | body | crc16 (u16, little-endian)
    wire    = COBS(payload) followed by a single 0x00 delimiter
CRC-16/CCITT-FALSE covers msg_id..body. Mirrors firmware/lib/frame.
"""

from __future__ import annotations

from dataclasses import dataclass

from . import cobs, crc16

MAX_BODY_LEN = 240
HEADER_LEN = 2  # msg_id, seq
CRC_LEN = 2
MAX_PAYLOAD_LEN = HEADER_LEN + MAX_BODY_LEN + CRC_LEN
# Encoded payload plus the 0x00 delimiter.
MAX_WIRE_LEN = cobs.max_encoded_len(MAX_PAYLOAD_LEN) + 1


class FrameError(ValueError):
    """Base for frame decode errors."""


class BodyTooLongError(FrameError):
    pass


class TooShortError(FrameError):
    pass


class BadCrcError(FrameError):
    pass


@dataclass(frozen=True, slots=True)
class Frame:
    msg_id: int
    seq: int
    body: bytes


def encode(msg_id: int, seq: int, body: bytes = b"") -> bytes:
    """Build a complete wire frame, including the trailing 0x00 delimiter."""
    if len(body) > MAX_BODY_LEN:
        raise BodyTooLongError(f"body length {len(body)} exceeds {MAX_BODY_LEN}")

    payload = bytearray((msg_id, seq))
    payload.extend(body)
    crc = crc16.compute(bytes(payload))
    payload.append(crc & 0xFF)
    payload.append((crc >> 8) & 0xFF)

    return cobs.encode(bytes(payload)) + b"\x00"


def decode(wire: bytes) -> Frame:
    """Decode one wire frame. `wire` must exclude the 0x00 delimiter."""
    payload = cobs.decode(wire)  # raises cobs.CobsError on bad input
    if len(payload) < HEADER_LEN + CRC_LEN:
        raise TooShortError("frame shorter than header+crc")
    if len(payload) > MAX_PAYLOAD_LEN:
        raise BodyTooLongError("frame longer than MAX_PAYLOAD_LEN")

    crc_at = len(payload) - CRC_LEN
    rx_crc = payload[crc_at] | (payload[crc_at + 1] << 8)
    if crc16.compute(payload[:crc_at]) != rx_crc:
        raise BadCrcError("CRC mismatch")

    return Frame(msg_id=payload[0], seq=payload[1], body=bytes(payload[HEADER_LEN:crc_at]))


class Reader:
    """Accumulates bytes from the serial link and yields complete (still COBS-encoded,
    still undelimited) frames for decode(). Mirrors frame::Reader (C++): push() consumes
    one byte at a time and returns the completed frame bytes when a 0x00 delimiter ends a
    non-empty, non-overflowed accumulation, else None. Overlong frames are discarded up
    to the next delimiter and counted in overflow_count, not raised as an error -- a
    malformed/noisy byte stream must not be able to wedge the reader.
    """

    def __init__(self, max_wire_len: int = MAX_WIRE_LEN) -> None:
        self._max_wire_len = max_wire_len
        self._buf = bytearray()
        self._discarding = False
        self.overflow_count = 0

    def push(self, byte: int) -> bytes | None:
        if byte == 0x00:
            was_discarding = self._discarding
            self._discarding = False
            frame = bytes(self._buf) if (not was_discarding and self._buf) else None
            self._buf.clear()
            return frame

        if self._discarding:
            return None
        if len(self._buf) >= self._max_wire_len:
            self._discarding = True
            self._buf.clear()
            self.overflow_count += 1
            return None
        self._buf.append(byte)
        return None
