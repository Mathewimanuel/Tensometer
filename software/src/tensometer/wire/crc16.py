"""CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR.

Mirrors firmware/lib/crc16 (C++) bit-for-bit -- same algorithm, not reimplemented
from the spec independently, so the two sides can never silently diverge on this.
Check value: compute(b"123456789") == 0x29B1 (see test_crc16.py).
"""

from __future__ import annotations

INIT: int = 0xFFFF
_POLY: int = 0x1021
_MASK: int = 0xFFFF


def update(crc: int, data: bytes) -> int:
    """Continue a running CRC over `data`. Pass INIT for a fresh CRC."""
    for byte in data:
        crc ^= (byte << 8) & _MASK
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ _POLY) & _MASK
            else:
                crc = (crc << 1) & _MASK
    return crc


def compute(data: bytes) -> int:
    return update(INIT, data)
