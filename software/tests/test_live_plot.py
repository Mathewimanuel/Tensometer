from tensometer.gui.live_plot import LivePlotWidget
from tensometer.wire import protocol as proto


def _telemetry(timestamp_us: int, position_steps: int, force_filtered_mN: int) -> proto.TelemetryBody:
    return proto.TelemetryBody(
        timestamp_us=timestamp_us,
        position_steps=position_steps,
        force_raw_counts=0,
        force_raw_mN=force_filtered_mN + 500,
        force_filtered_mN=force_filtered_mN,
        state=proto.CoreState.RUN,
        flags=0,
        cycle_index=0,
    )


def test_widget_constructs(qtbot):
    widget = LivePlotWidget()
    qtbot.addWidget(widget)
    assert widget.sample_count() == 0


def test_add_telemetry_buffers_without_crashing(qtbot):
    widget = LivePlotWidget()
    qtbot.addWidget(widget)
    for i in range(10):
        widget.add_telemetry(_telemetry(i * 12_500, i * 100, 1000 * i))
    assert widget.sample_count() == 10


def test_redraw_updates_curve_data(qtbot):
    widget = LivePlotWidget()
    qtbot.addWidget(widget)
    widget.add_telemetry(_telemetry(0, 2000, 5000))  # 1.0 mm, 5.0 N (filtered)
    widget._redraw()
    xs, ys = widget._curve.getData()
    assert list(xs) == [1.0]
    assert list(ys) == [5.0]


def test_show_raw_toggle_switches_series(qtbot):
    widget = LivePlotWidget()
    qtbot.addWidget(widget)
    widget.add_telemetry(_telemetry(0, 0, 5000))  # raw = 5500, filtered = 5000
    widget._redraw()
    _, filtered_ys = widget._curve.getData()
    assert list(filtered_ys) == [5.0]

    widget.set_show_raw(True)
    widget._redraw()
    _, raw_ys = widget._curve.getData()
    assert list(raw_ys) == [5.5]


def test_x_axis_toggle_switches_between_position_and_time(qtbot):
    widget = LivePlotWidget()
    qtbot.addWidget(widget)
    widget.add_telemetry(_telemetry(1_000_000, 4000, 1000))  # t=1.0s (first sample -> t=0), x=2.0mm
    widget._redraw()
    xs, _ = widget._curve.getData()
    assert list(xs) == [2.0]  # default: position

    widget.set_x_axis("time")
    widget._redraw()
    xs, _ = widget._curve.getData()
    assert list(xs) == [0.0]  # first sample defines t0


def test_add_event_creates_marker_without_crashing(qtbot):
    widget = LivePlotWidget()
    qtbot.addWidget(widget)
    event = proto.EventBody(
        timestamp_us=0, type=proto.EventType.SUBSTRATE_BREAK, aux=0, position_steps=10_000, force_mN=200_000
    )
    widget.add_event(event)
    assert len(widget._event_markers) == 1
    xs, ys = widget._event_scatter.getData()
    assert list(xs) == [5.0]
    assert list(ys) == [200.0]


def test_clear_data_resets_everything(qtbot):
    widget = LivePlotWidget()
    qtbot.addWidget(widget)
    widget.add_telemetry(_telemetry(0, 0, 1000))
    widget.add_event(
        proto.EventBody(timestamp_us=0, type=proto.EventType.SLIP, aux=0, position_steps=0, force_mN=0)
    )
    widget.clear_data()
    assert widget.sample_count() == 0
    assert widget._event_markers == []
    xs, _ys = widget._curve.getData()
    assert xs is None or len(xs) == 0


def test_cutoff_lines_can_be_set_and_cleared(qtbot):
    widget = LivePlotWidget()
    qtbot.addWidget(widget)
    widget.set_cutoff_lines(cutoff_N=880.0, force_limit_N=500.0)
    assert widget._cutoff_line is not None
    assert widget._force_limit_line is not None

    widget.set_cutoff_lines(cutoff_N=None, force_limit_N=None)
    assert widget._cutoff_line is None
    assert widget._force_limit_line is None
