"""Per-test logging and reload, per the schema settled in docs/decisions.md 2026-11-16
("software/ PC app: plotting and logging design"): three files per test, not one --
`<name>_telemetry.csv` (one row per 80 Hz TELEMETRY sample), `<name>_events.csv` (one
row per EVENT), `<name>_metadata.json` (test-level facts: specimen, config, wall-clock
start time -- unrecoverable later if not captured at test time).

Telemetry stays purely tabular at a fixed rate, not mixed with event rows, specifically
so it's easy to reload and plot (the Review-tab design, same entry). TELEM_* flags are
decoded into named boolean columns, not left as a raw bitmask, so the CSV reads cleanly
in a spreadsheet without needing protocol.py to interpret it.
"""

from __future__ import annotations

import csv
import dataclasses
import json
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path
from typing import Self

from . import units
from .wire import protocol as proto

TELEMETRY_FIELDS = [
    "timestamp_us",
    "position_steps",
    "position_mm",
    "force_raw_mN",
    "force_filtered_mN",
    "state",
    "cycle_index",
    "position_valid",
    "homed",
    "coupler_confirmed",
]

EVENT_FIELDS = ["timestamp_us", "type", "aux", "position_mm", "force_mN"]


@dataclass(frozen=True, slots=True)
class TelemetryRow:
    timestamp_us: int
    position_steps: int
    position_mm: float
    force_raw_mN: int
    force_filtered_mN: int
    state: str
    cycle_index: int
    position_valid: bool
    homed: bool
    coupler_confirmed: bool

    @classmethod
    def from_body(cls, body: proto.TelemetryBody) -> TelemetryRow:
        return cls(
            timestamp_us=body.timestamp_us,
            position_steps=body.position_steps,
            position_mm=units.steps_to_mm(body.position_steps),
            force_raw_mN=body.force_raw_mN,
            force_filtered_mN=body.force_filtered_mN,
            state=body.state.name,
            cycle_index=body.cycle_index,
            position_valid=bool(body.flags & proto.TELEM_POSITION_VALID),
            homed=bool(body.flags & proto.TELEM_HOMED),
            coupler_confirmed=bool(body.flags & proto.TELEM_COUPLER_CONFIRMED),
        )


@dataclass(frozen=True, slots=True)
class EventRow:
    timestamp_us: int
    type: str
    aux: int
    position_mm: float
    force_mN: int

    @classmethod
    def from_body(cls, body: proto.EventBody) -> EventRow:
        return cls(
            timestamp_us=body.timestamp_us,
            type=body.type.name,
            aux=body.aux,
            position_mm=units.steps_to_mm(body.position_steps),
            force_mN=body.force_mN,
        )


@dataclass
class TestMetadata:
    """Test-level facts captured once, before ARM (docs/decisions.md GUI-layout entry:
    metadata is its own step, separate from the ARM config form)."""

    test_name: str
    started_at_wall_clock: str  # ISO 8601 -- the only place a real-time mapping exists;
    # TelemetryBody.timestamp_us is firmware-relative (microseconds since Teensy boot)
    material: str
    l0_mm: float
    cell_profile: str
    arm_config: dict  # controller.ArmConfig's fields, as a plain dict (json-serializable)
    specimen_width_mm: float | None = None
    specimen_thickness_mm: float | None = None
    operator_notes: str = ""
    firmware_build_id: int | None = None
    protocol_version: str | None = None


class TestLogger:
    """Opens the three files for one test and writes rows as telemetry/events arrive.
    Metadata is written once, immediately -- normally before ARM, per the GUI design --
    not deferred until the test finishes (a crash or disconnect mid-test must not lose
    it)."""

    def __init__(self, directory: Path | str, test_name: str, metadata: TestMetadata) -> None:
        directory = Path(directory)
        directory.mkdir(parents=True, exist_ok=True)
        self._telemetry_path = directory / f"{test_name}_telemetry.csv"
        self._events_path = directory / f"{test_name}_events.csv"
        self._metadata_path = directory / f"{test_name}_metadata.json"
        for path in (self._telemetry_path, self._events_path, self._metadata_path):
            if path.exists():
                raise FileExistsError(f"{path} exists; refusing to overwrite a previous test")

        self._metadata_path.write_text(json.dumps(dataclasses.asdict(metadata), indent=2))

        # buffering=1: each row reaches the OS as it is written, so a crash loses at most
        # the row in progress, not the ~8 kB buffer holding the break event.
        self._telemetry_file = self._telemetry_path.open(
            "w", buffering=1, newline="", encoding="utf-8"
        )
        self._telemetry_writer = csv.DictWriter(self._telemetry_file, fieldnames=TELEMETRY_FIELDS)
        self._telemetry_writer.writeheader()

        self._events_file = self._events_path.open("w", buffering=1, newline="", encoding="utf-8")
        self._events_writer = csv.DictWriter(self._events_file, fieldnames=EVENT_FIELDS)
        self._events_writer.writeheader()

    def log_telemetry(self, body: proto.TelemetryBody) -> None:
        self._telemetry_writer.writerow(dataclasses.asdict(TelemetryRow.from_body(body)))

    def log_event(self, body: proto.EventBody) -> None:
        self._events_writer.writerow(dataclasses.asdict(EventRow.from_body(body)))

    def close(self) -> None:
        self._telemetry_file.close()
        self._events_file.close()

    def discard(self) -> None:
        """Close and delete the files: for a test that never started (ARM refused), so
        its name stays free and no empty logs are left behind."""
        self.close()
        for path in (self._telemetry_path, self._events_path, self._metadata_path):
            path.unlink(missing_ok=True)

    def __enter__(self) -> Self:
        return self

    def __exit__(self, *exc_info: object) -> None:
        self.close()


def _str_to_bool(value: str) -> bool:
    return value == "True"


def _complete_rows(f) -> Iterator[dict[str, str]]:
    """Skips a row cut short by a crash mid-write (DictReader pads missing fields with
    None, a partial field reads as '') so one bad last line can't make the whole test
    unloadable."""
    for row in csv.DictReader(f):
        if None not in row.values() and "" not in row.values():
            yield row


def load_telemetry(path: Path | str) -> list[TelemetryRow]:
    with Path(path).open(newline="", encoding="utf-8") as f:
        return [
            TelemetryRow(
                timestamp_us=int(row["timestamp_us"]),
                position_steps=int(row["position_steps"]),
                position_mm=float(row["position_mm"]),
                force_raw_mN=int(row["force_raw_mN"]),
                force_filtered_mN=int(row["force_filtered_mN"]),
                state=row["state"],
                cycle_index=int(row["cycle_index"]),
                position_valid=_str_to_bool(row["position_valid"]),
                homed=_str_to_bool(row["homed"]),
                coupler_confirmed=_str_to_bool(row["coupler_confirmed"]),
            )
            for row in _complete_rows(f)
        ]


def load_events(path: Path | str) -> list[EventRow]:
    with Path(path).open(newline="", encoding="utf-8") as f:
        return [
            EventRow(
                timestamp_us=int(row["timestamp_us"]),
                type=row["type"],
                aux=int(row["aux"]),
                position_mm=float(row["position_mm"]),
                force_mN=int(row["force_mN"]),
            )
            for row in _complete_rows(f)
        ]


def load_metadata(path: Path | str) -> TestMetadata:
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    return TestMetadata(**data)
