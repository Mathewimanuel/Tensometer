"""Consistent Overhead Byte Stuffing. Encoded output contains no 0x00 bytes, so 0x00
can delimit frames on the serial link. The delimiter is NOT written or expected here.

Mirrors firmware/lib/cobs (C++) algorithm exactly; the Python API is exception-based
rather than C's Result-enum-plus-out-param style (no fixed-capacity buffers to manage
in Python), but every byte it produces/consumes must match the C++ side bit-for-bit.
"""

from __future__ import annotations


def max_encoded_len(length: int) -> int:
    """Worst-case encoded size for `length` input bytes (excluding the 0x00 delimiter)."""
    return length + length // 254 + 1


class CobsError(ValueError):
    """Base for COBS decode errors."""


class ZeroInInputError(CobsError):
    """Encoded data must not contain a 0x00 byte."""


class TruncatedError(CobsError):
    """A code byte points past the end of the input."""


def encode(data: bytes) -> bytes:
    out = bytearray()
    out.append(0)  # placeholder for the first block's code byte
    code_idx = 0
    code = 1  # 1 + number of non-zero bytes in the current block

    for byte in data:
        if byte == 0:
            out[code_idx] = code
            code_idx = len(out)
            out.append(0)  # placeholder
            code = 1
        else:
            out.append(byte)
            code += 1
            if code == 0xFF:  # block full: 254 data bytes
                out[code_idx] = code
                code_idx = len(out)
                out.append(0)
                code = 1
    out[code_idx] = code
    return bytes(out)


def decode(data: bytes) -> bytes:
    if not data:
        raise CobsError("empty COBS input")

    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        code = data[i]
        i += 1
        if code == 0:
            raise ZeroInInputError("zero byte in COBS-encoded input")

        for _ in range(1, code):
            if i >= n:
                raise TruncatedError("COBS code byte points past the end of input")
            b = data[i]
            i += 1
            if b == 0:
                raise ZeroInInputError("zero byte in COBS-encoded input")
            out.append(b)

        # A block shorter than 254 data bytes was terminated by a zero in the
        # original data, unless it is the final block.
        if code < 0xFF and i < n:
            out.append(0)

    return bytes(out)
