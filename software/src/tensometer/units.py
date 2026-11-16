"""Unit conversions between human-friendly units (mm, N, %, mm/min) and the raw wire
units protocol.py's bodies use (um, mN, permille). Shared by controller.py and the
(not yet built) CSV logger, so the conversion lives in exactly one place.

STEPS_PER_MM mirrors firmware/include/config.h's cfg::STEPS_PER_MM (2000, derived there
from the DTGN 5:1 gearbox + 4 mm SFU1204 lead + 1/8 microstep -- see that file for the
derivation). Hand-mirrored, not shared, same discipline as the rest of software/
(docs/decisions.md 2026-11-16: protocol mirror stays hand-written).
"""

from __future__ import annotations

STEPS_PER_MM = 2000  # cfg::STEPS_PER_MM, firmware/include/config.h


def steps_to_mm(steps: int) -> float:
    return steps / STEPS_PER_MM


def mm_to_steps(mm: float) -> int:
    return round(mm * STEPS_PER_MM)


def mm_to_um(mm: float) -> int:
    return round(mm * 1000)


def um_to_mm(um: int) -> float:
    return um / 1000


def n_to_mN(newtons: float) -> int:
    return round(newtons * 1000)


def mN_to_n(milli_newtons: int) -> float:
    return milli_newtons / 1000


def percent_to_permille(percent: float) -> int:
    return round(percent * 10)


def permille_to_percent(permille: int) -> float:
    return permille / 10
