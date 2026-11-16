"""Review tab: File > Open loads a saved test's telemetry/events/metadata back into its
own LivePlotWidget instance, read-only (docs/decisions.md GUI-layout entry: "Each tab
owns its own plot-widget instance... not a single shared widget repointed at different
data sources"). Reuses the exact same LivePlotWidget rendering code as the Live tab, by
reconstructing synthetic TelemetryBody/EventBody instances from the loaded rows -- not a
separate plotting implementation.
"""

from __future__ import annotations

import dataclasses
import json
from pathlib import Path

from PySide6 import QtWidgets

from .. import datalog, units
from ..wire import protocol as proto
from .live_plot import LivePlotWidget


def _telemetry_body_from_row(row: datalog.TelemetryRow) -> proto.TelemetryBody:
    flags = 0
    if row.position_valid:
        flags |= proto.TELEM_POSITION_VALID
    if row.homed:
        flags |= proto.TELEM_HOMED
    if row.coupler_confirmed:
        flags |= proto.TELEM_COUPLER_CONFIRMED
    return proto.TelemetryBody(
        timestamp_us=row.timestamp_us,
        position_steps=row.position_steps,
        force_raw_counts=0,
        force_raw_mN=row.force_raw_mN,
        force_filtered_mN=row.force_filtered_mN,
        state=proto.CoreState[row.state],
        flags=flags,
        cycle_index=row.cycle_index,
    )


def _event_body_from_row(row: datalog.EventRow) -> proto.EventBody:
    # EventRow only kept position_mm (docs/decisions.md events-column schema), so the
    # reconstructed position_steps is a round-trip-through-float approximation, not the
    # original raw step count -- fine for display, which is all this is used for.
    return proto.EventBody(
        timestamp_us=row.timestamp_us,
        type=proto.EventType[row.type],
        aux=row.aux,
        position_steps=units.mm_to_steps(row.position_mm),
        force_mN=row.force_mN,
    )


class ReviewTab(QtWidgets.QWidget):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        layout = QtWidgets.QVBoxLayout(self)

        top = QtWidgets.QHBoxLayout()
        self.open_button = QtWidgets.QPushButton("Open test...")
        self.open_button.clicked.connect(self._on_open_clicked)
        top.addWidget(self.open_button)
        self.loaded_label = QtWidgets.QLabel("No test loaded")
        top.addWidget(self.loaded_label, stretch=1)
        layout.addLayout(top)

        body = QtWidgets.QHBoxLayout()
        layout.addLayout(body)

        self.plot = LivePlotWidget()
        body.addWidget(self.plot, stretch=3)

        self.metadata_view = QtWidgets.QPlainTextEdit()
        self.metadata_view.setReadOnly(True)
        body.addWidget(self.metadata_view, stretch=1)

    def _on_open_clicked(self) -> None:
        path, _filter = QtWidgets.QFileDialog.getOpenFileName(
            self, "Open test metadata", "", "Test metadata (*_metadata.json)"
        )
        if not path:
            return
        try:
            self.load_test(path)
        except (OSError, KeyError, ValueError, TypeError) as exc:
            # A missing sibling CSV, a malformed row, or an unknown enum name (e.g. a
            # file logged under a different protocol version) must show a clear error,
            # not crash the Review tab silently (same "no silent failure" principle as
            # NACK surfacing in main_window.py).
            QtWidgets.QMessageBox.critical(self, "Could not load test", str(exc))

    def load_test(self, metadata_path: str | Path) -> None:
        metadata_path = Path(metadata_path)
        stem = metadata_path.name.removesuffix("_metadata.json")
        directory = metadata_path.parent

        metadata = datalog.load_metadata(metadata_path)
        telemetry = datalog.load_telemetry(directory / f"{stem}_telemetry.csv")
        events = datalog.load_events(directory / f"{stem}_events.csv")

        self.plot.clear_data()
        for row in telemetry:
            self.plot.add_telemetry(_telemetry_body_from_row(row))
        for row in events:
            self.plot.add_event(_event_body_from_row(row))
        self.plot.force_redraw()

        self.loaded_label.setText(metadata.test_name)
        self.metadata_view.setPlainText(json.dumps(dataclasses.asdict(metadata), indent=2))
