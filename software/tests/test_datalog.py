from tensometer import datalog
from tensometer.wire import protocol as proto


def _telemetry_body(flags: int = 0, cycle_index: int = 0) -> proto.TelemetryBody:
    return proto.TelemetryBody(
        timestamp_us=12_345,
        position_steps=20_000,  # 10.0 mm at 2000 steps/mm
        force_raw_counts=8_000_000,
        force_raw_mN=150_500,
        force_filtered_mN=150_000,
        state=proto.CoreState.RUN,
        flags=flags,
        cycle_index=cycle_index,
    )


def _event_body() -> proto.EventBody:
    return proto.EventBody(
        timestamp_us=99_999,
        type=proto.EventType.SUBSTRATE_BREAK,
        aux=0,
        position_steps=30_000,  # 15.0 mm
        force_mN=200_000,
    )


def _metadata(test_name: str = "fabric01") -> datalog.TestMetadata:
    return datalog.TestMetadata(
        test_name=test_name,
        started_at_wall_clock="2026-11-16T12:00:00",
        material="knit fabric",
        l0_mm=50.0,
        cell_profile="DYLY-103 100 kg",
        arm_config={"speed_mm_per_min": 200, "max_travel_mm": 20.0},
        specimen_width_mm=10.0,
        specimen_thickness_mm=0.5,
        operator_notes="first sample of the batch",
    )


def test_telemetry_row_decodes_flags_correctly():
    flags = proto.TELEM_POSITION_VALID | proto.TELEM_HOMED
    row = datalog.TelemetryRow.from_body(_telemetry_body(flags=flags))
    assert row.position_valid is True
    assert row.homed is True
    assert row.coupler_confirmed is False
    assert row.position_mm == 10.0
    assert row.state == "RUN"


def test_event_row_converts_position_to_mm():
    row = datalog.EventRow.from_body(_event_body())
    assert row.position_mm == 15.0
    assert row.type == "SUBSTRATE_BREAK"


def test_logger_writes_and_reloads_telemetry_and_events(tmp_path):
    meta = _metadata()
    with datalog.TestLogger(tmp_path, "fabric01", meta) as logger:
        logger.log_telemetry(_telemetry_body(flags=proto.TELEM_POSITION_VALID))
        logger.log_telemetry(_telemetry_body(flags=0, cycle_index=5))
        logger.log_event(_event_body())

    telemetry = datalog.load_telemetry(tmp_path / "fabric01_telemetry.csv")
    assert len(telemetry) == 2
    assert telemetry[0].position_valid is True
    assert telemetry[1].cycle_index == 5

    events = datalog.load_events(tmp_path / "fabric01_events.csv")
    assert len(events) == 1
    assert events[0].type == "SUBSTRATE_BREAK"


def test_metadata_written_immediately_and_round_trips(tmp_path):
    meta = _metadata()
    logger = datalog.TestLogger(tmp_path, "fabric01", meta)
    try:
        # Written before any telemetry/event is logged -- a crash right here must not
        # lose it (docs/decisions.md: "a crash or disconnect mid-test must not lose it").
        loaded = datalog.load_metadata(tmp_path / "fabric01_metadata.json")
        assert loaded == meta
    finally:
        logger.close()


def test_creates_output_directory_if_missing(tmp_path):
    target = tmp_path / "nested" / "tests"
    logger = datalog.TestLogger(target, "x", _metadata("x"))
    logger.close()
    assert (target / "x_telemetry.csv").exists()


def test_files_are_readable_immediately_after_close(tmp_path):
    logger = datalog.TestLogger(tmp_path, "fabric01", _metadata())
    logger.log_telemetry(_telemetry_body())
    logger.close()
    # Not using the context manager here -- confirms close() alone flushes properly.
    telemetry = datalog.load_telemetry(tmp_path / "fabric01_telemetry.csv")
    assert len(telemetry) == 1
