import pytest

from tensometer import datalog
from tensometer.gui.review_tab import ReviewTab
from tensometer.wire import protocol as proto


def _write_sample_test(tmp_path):
    meta = datalog.TestMetadata(
        test_name="fabric01",
        started_at_wall_clock="2026-11-16T12:00:00",
        material="knit fabric",
        l0_mm=50.0,
        cell_profile="DYLY-103 100 kg",
        arm_config={"speed_mm_per_min": 200},
    )
    with datalog.TestLogger(tmp_path, "fabric01", meta) as logger:
        logger.log_telemetry(
            proto.TelemetryBody(
                timestamp_us=0,
                position_steps=20_000,
                force_raw_counts=0,
                force_raw_mN=150_500,
                force_filtered_mN=150_000,
                state=proto.CoreState.RUN,
                flags=proto.TELEM_POSITION_VALID | proto.TELEM_HOMED,
                cycle_index=0,
            )
        )
        logger.log_event(
            proto.EventBody(
                timestamp_us=1_000,
                type=proto.EventType.SUBSTRATE_BREAK,
                aux=0,
                position_steps=20_000,
                force_mN=150_000,
            )
        )
    return tmp_path / "fabric01_metadata.json"


def test_load_test_populates_plot_and_metadata(qtbot, tmp_path):
    metadata_path = _write_sample_test(tmp_path)
    tab = ReviewTab()
    qtbot.addWidget(tab)

    tab.load_test(metadata_path)

    assert tab.plot.sample_count() == 1
    assert tab.loaded_label.text() == "fabric01"
    assert "knit fabric" in tab.metadata_view.toPlainText()
    assert len(tab.plot._event_markers) == 1


def test_initial_state_shows_no_test_loaded(qtbot):
    tab = ReviewTab()
    qtbot.addWidget(tab)
    assert tab.loaded_label.text() == "No test loaded"
    assert tab.plot.sample_count() == 0


def test_load_test_with_missing_file_raises_instead_of_crashing_silently(qtbot, tmp_path):
    # _on_open_clicked() is the one that catches this and shows a QMessageBox; load_test()
    # itself must keep raising so that boundary actually has something to catch.
    tab = ReviewTab()
    qtbot.addWidget(tab)
    with pytest.raises(OSError):
        tab.load_test(tmp_path / "does_not_exist_metadata.json")
