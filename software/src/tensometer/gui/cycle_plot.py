"""Cycle (fatigue) test view: every cycle's loop overlaid on one force-vs-position plot,
colored by cycle number, plus a peak-force-per-cycle trend panel -- the classic
early-warning signal for fatigue softening before failure (docs/decisions.md
2026-11-16). Samples are grouped by TelemetryBody.cycle_index; each increment starts a
new loop. Swapped in for LivePlotWidget automatically whenever cycle_index is nonzero
(main_window.py's job, not this file's).
"""

from __future__ import annotations

from collections import deque

import pyqtgraph as pg
from PySide6 import QtCore, QtWidgets

from .. import units
from ..wire import protocol as proto

_REDRAW_INTERVAL_MS = 100  # the in-progress loop updates less urgently than the ramp plot
_MAX_FINISHED_LOOPS = 50


class CyclePlotWidget(QtWidgets.QWidget):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        layout = QtWidgets.QVBoxLayout(self)

        self._loop_plot = pg.PlotWidget()
        self._loop_plot.setLabel("left", "Force", units="N")
        self._loop_plot.setLabel("bottom", "Position", units="mm")
        self._loop_plot.showGrid(x=True, y=True, alpha=0.3)
        layout.addWidget(self._loop_plot, stretch=3)

        self._trend_plot = pg.PlotWidget()
        self._trend_plot.setLabel("left", "Peak force", units="N")
        self._trend_plot.setLabel("bottom", "Cycle")
        self._trend_plot.showGrid(x=True, y=True, alpha=0.3)
        layout.addWidget(self._trend_plot, stretch=1)
        self._trend_curve = self._trend_plot.plot(
            pen=None, symbol="o", symbolSize=5, symbolBrush="b"
        )

        self._current_cycle_index: int | None = None
        self._current_positions: list[float] = []
        self._current_forces: list[float] = []
        self._current_peak_force_N: float = float("-inf")
        self._live_curve: pg.PlotDataItem | None = None
        # Only the last N finished loops stay drawn; a 10 000-cycle fatigue run would
        # otherwise repaint 10 000 curves per frame. The peak trend below keeps all.
        self._finished_loops: deque[pg.PlotDataItem] = deque(maxlen=_MAX_FINISHED_LOOPS)

        # Append-only: avoids re-sorting/recomputing the whole trend every cycle, which
        # would be O(n^2) over a run of thousands of cycles. cycle_index arrives
        # non-decreasing from the firmware, so appending in arrival order is correct.
        self._trend_cycles: list[int] = []
        self._trend_peaks_N: list[float] = []

        self._timer = QtCore.QTimer(self)
        self._timer.timeout.connect(self._redraw_current_loop)
        self._timer.start(_REDRAW_INTERVAL_MS)

    def clear_data(self) -> None:
        self._loop_plot.clear()
        self._current_cycle_index = None
        self._current_positions = []
        self._current_forces = []
        self._current_peak_force_N = float("-inf")
        self._live_curve = None
        self._finished_loops.clear()  # items already removed by _loop_plot.clear()
        self._trend_cycles.clear()
        self._trend_peaks_N.clear()
        self._trend_curve.setData([], [])

    def add_telemetry(self, body: proto.TelemetryBody) -> None:
        position_mm = units.steps_to_mm(body.position_steps)
        force_N = units.mN_to_n(body.force_filtered_mN)

        if self._current_cycle_index is None:
            self._current_cycle_index = body.cycle_index

        if body.cycle_index != self._current_cycle_index:
            self._finish_loop(self._current_cycle_index)
            self._current_cycle_index = body.cycle_index
            self._current_positions = []
            self._current_forces = []
            self._current_peak_force_N = float("-inf")

        self._current_positions.append(position_mm)
        self._current_forces.append(force_N)
        self._current_peak_force_N = max(self._current_peak_force_N, force_N)

    def _finish_loop(self, cycle_index: int) -> None:
        if self._live_curve is not None:
            self._loop_plot.removeItem(self._live_curve)
            self._live_curve = None
        if self._current_positions:
            color = pg.intColor(cycle_index, hues=max(cycle_index + 1, 10))
            if len(self._finished_loops) == self._finished_loops.maxlen:
                self._loop_plot.removeItem(self._finished_loops[0])  # deque drops it next
            self._finished_loops.append(
                self._loop_plot.plot(
                    self._current_positions, self._current_forces, pen=pg.mkPen(color, width=1)
                )
            )
            self._trend_cycles.append(cycle_index)
            self._trend_peaks_N.append(self._current_peak_force_N)
            self._trend_curve.setData(self._trend_cycles, self._trend_peaks_N)

    def _redraw_current_loop(self) -> None:
        if self._current_cycle_index is None or not self._current_positions:
            return
        if self._live_curve is None:
            self._live_curve = self._loop_plot.plot(pen=pg.mkPen("w", width=2))
        self._live_curve.setData(self._current_positions, self._current_forces)

    def loop_count(self) -> int:
        return len(self._trend_cycles)
