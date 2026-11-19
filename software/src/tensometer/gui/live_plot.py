"""Live plot widget: force vs. position (default) or vs. time, filtered (default) or
raw, with cutoff/force-limit reference lines and break/fault event markers -- the design
settled in docs/decisions.md 2026-11-16 ("software/ PC app: plotting and logging
design"). Backed by pyqtgraph, not matplotlib, specifically because matplotlib's
animation layer isn't built for the 80 Hz TELEMETRY rate (same entry).

Redraw is decoupled from the data rate: add_telemetry() just appends to an internal
buffer (cheap, safe to call at 80 Hz); an internal QTimer redraws from that buffer at a
capped rate (~30 fps), not every sample -- redrawing every sample would burn CPU for no
visible benefit and risks UI stutter on a long test (same entry).
"""

from __future__ import annotations

from typing import Literal

import pyqtgraph as pg
from PySide6 import QtCore

from .. import units
from ..wire import protocol as proto

_REDRAW_INTERVAL_MS = 33  # ~30 fps


class LivePlotWidget(pg.PlotWidget):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setLabel("left", "Force", units="N")
        self.setLabel("bottom", "Position", units="mm")
        self.showGrid(x=True, y=True, alpha=0.3)

        # Four parallel lists, not a list of sample objects: _redraw() hands them to
        # setData() as-is instead of walking a million objects per tick.
        # shortcut: setData still copies O(n) per redraw; switch to a windowed/ring
        # store if a multi-hour run visibly drags the GUI.
        self._time_s: list[float] = []
        self._position_mm: list[float] = []
        self._force_raw_N: list[float] = []
        self._force_filtered_N: list[float] = []
        self._x_mode: Literal["position", "time"] = "position"
        self._show_raw = False
        self._t0_s: float | None = None
        self._dirty = False

        self._curve = self.plot(pen=pg.mkPen(width=2))
        self._curve.setDownsampling(auto=True, method="peak")  # 80 Hz x hours stays drawable
        self._cutoff_line: pg.InfiniteLine | None = None
        self._force_limit_line: pg.InfiniteLine | None = None
        self._event_markers: list[pg.TextItem] = []
        self._event_xs: list[float] = []
        self._event_ys: list[float] = []
        self._event_scatter = pg.ScatterPlotItem(size=10, brush=pg.mkBrush("r"))
        self.addItem(self._event_scatter)

        self._timer = QtCore.QTimer(self)
        self._timer.timeout.connect(self._redraw)
        self._timer.start(_REDRAW_INTERVAL_MS)

    def clear_data(self) -> None:
        self._time_s.clear()
        self._position_mm.clear()
        self._force_raw_N.clear()
        self._force_filtered_N.clear()
        self._t0_s = None
        for item in self._event_markers:
            self.removeItem(item)
        self._event_markers.clear()
        self._event_xs.clear()
        self._event_ys.clear()
        self._event_scatter.setData([], [])
        self._curve.setData([], [])
        self._dirty = False

    def set_x_axis(self, mode: Literal["position", "time"]) -> None:
        self._x_mode = mode
        self.setLabel("bottom", "Position" if mode == "position" else "Time",
                       units="mm" if mode == "position" else "s")
        self._dirty = True

    def set_show_raw(self, show_raw: bool) -> None:
        self._show_raw = show_raw
        self._dirty = True

    def set_cutoff_lines(self, cutoff_N: float | None, force_limit_N: float | None) -> None:
        if self._cutoff_line is not None:
            self.removeItem(self._cutoff_line)
            self._cutoff_line = None
        if self._force_limit_line is not None:
            self.removeItem(self._force_limit_line)
            self._force_limit_line = None
        if cutoff_N is not None:
            self._cutoff_line = pg.InfiniteLine(
                pos=cutoff_N,
                angle=0,
                pen=pg.mkPen("r", style=QtCore.Qt.PenStyle.DashLine),
                label="cutoff",
                labelOpts={"position": 0.95},
            )
            self.addItem(self._cutoff_line)
        if force_limit_N is not None:
            self._force_limit_line = pg.InfiniteLine(
                pos=force_limit_N,
                angle=0,
                pen=pg.mkPen("y", style=QtCore.Qt.PenStyle.DashLine),
                label="force limit",
                labelOpts={"position": 0.90},
            )
            self.addItem(self._force_limit_line)

    def add_telemetry(self, body: proto.TelemetryBody) -> None:
        time_s = body.timestamp_us / 1_000_000
        if self._t0_s is None:
            self._t0_s = time_s
        self._time_s.append(time_s - self._t0_s)
        self._position_mm.append(units.steps_to_mm(body.position_steps))
        self._force_raw_N.append(units.mN_to_n(body.force_raw_mN))
        self._force_filtered_N.append(units.mN_to_n(body.force_filtered_mN))
        self._dirty = True

    def add_event(self, body: proto.EventBody) -> None:
        x = (
            units.steps_to_mm(body.position_steps)
            if self._x_mode == "position"
            else (body.timestamp_us / 1_000_000 - (self._t0_s or 0.0))
        )
        y = units.mN_to_n(body.force_mN)
        label = pg.TextItem(body.type.name, anchor=(0.5, 1.2), color="r")
        label.setPos(x, y)
        self.addItem(label)
        self._event_markers.append(label)
        self._event_xs.append(x)
        self._event_ys.append(y)
        self._event_scatter.setData(self._event_xs, self._event_ys)

    def sample_count(self) -> int:
        return len(self._time_s)

    def force_redraw(self) -> None:
        """Draws immediately instead of waiting for the next throttled timer tick --
        for Review's load-and-replot, where the data is static and should be visible the
        moment it's loaded, not up to one timer period later."""
        self._dirty = True
        self._redraw()

    def _redraw(self) -> None:
        if not self._dirty or not self._time_s:
            return
        xs = self._position_mm if self._x_mode == "position" else self._time_s
        ys = self._force_raw_N if self._show_raw else self._force_filtered_N
        self._curve.setData(xs, ys)
        self._dirty = False
