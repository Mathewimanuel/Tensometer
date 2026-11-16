from tensometer.gui.cycle_plot import CyclePlotWidget
from tensometer.wire import protocol as proto


def _telemetry(position_steps: int, force_filtered_mN: int, cycle_index: int) -> proto.TelemetryBody:
    return proto.TelemetryBody(
        timestamp_us=0,
        position_steps=position_steps,
        force_raw_counts=0,
        force_raw_mN=force_filtered_mN,
        force_filtered_mN=force_filtered_mN,
        state=proto.CoreState.CYCLE,
        flags=0,
        cycle_index=cycle_index,
    )


def test_widget_constructs(qtbot):
    widget = CyclePlotWidget()
    qtbot.addWidget(widget)
    assert widget.loop_count() == 0


def test_cycle_index_increment_finishes_a_loop_and_adds_a_trend_point(qtbot):
    widget = CyclePlotWidget()
    qtbot.addWidget(widget)
    # Cycle 0: peak force 100 N.
    widget.add_telemetry(_telemetry(0, 50_000, cycle_index=0))
    widget.add_telemetry(_telemetry(10_000, 100_000, cycle_index=0))
    widget.add_telemetry(_telemetry(0, 60_000, cycle_index=0))
    # Cycle 1 begins -- cycle 0's loop must be finished and trended now.
    widget.add_telemetry(_telemetry(0, 70_000, cycle_index=1))

    assert widget.loop_count() == 1
    cycles, peaks = widget._trend_cycles, widget._trend_peaks_N
    assert cycles == [0]
    assert peaks == [100.0]


def test_multiple_cycles_accumulate_trend_in_order(qtbot):
    widget = CyclePlotWidget()
    qtbot.addWidget(widget)
    for cycle in range(3):
        peak_mN = (cycle + 1) * 100_000
        widget.add_telemetry(_telemetry(0, 10_000, cycle_index=cycle))
        widget.add_telemetry(_telemetry(5_000, peak_mN, cycle_index=cycle))
    # One more sample on a new cycle to flush the last completed one.
    widget.add_telemetry(_telemetry(0, 10_000, cycle_index=3))

    assert widget.loop_count() == 3
    assert widget._trend_cycles == [0, 1, 2]
    assert widget._trend_peaks_N == [100.0, 200.0, 300.0]


def test_in_progress_loop_does_not_count_as_a_completed_cycle(qtbot):
    widget = CyclePlotWidget()
    qtbot.addWidget(widget)
    widget.add_telemetry(_telemetry(0, 10_000, cycle_index=0))
    widget.add_telemetry(_telemetry(5_000, 50_000, cycle_index=0))
    assert widget.loop_count() == 0  # cycle 0 never finished


def test_clear_data_resets_everything(qtbot):
    widget = CyclePlotWidget()
    qtbot.addWidget(widget)
    widget.add_telemetry(_telemetry(0, 10_000, cycle_index=0))
    widget.add_telemetry(_telemetry(0, 10_000, cycle_index=1))
    assert widget.loop_count() == 1
    widget.clear_data()
    assert widget.loop_count() == 0
    assert widget._current_cycle_index is None


def test_redraw_current_loop_does_not_crash_with_no_data(qtbot):
    widget = CyclePlotWidget()
    qtbot.addWidget(widget)
    widget._redraw_current_loop()  # no-op, must not raise
