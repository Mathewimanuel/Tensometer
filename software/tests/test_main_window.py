from dataclasses import dataclass, field

from PySide6 import QtWidgets

from tensometer import datalog
from tensometer import session as sess
from tensometer.controller import ArmConfig
from tensometer.gui.dialogs import ArmConfigDialog, MetadataDialog
from tensometer.gui.main_window import MainWindow
from tensometer.wire import protocol as proto


@dataclass
class _FakeController:
    """Minimal stand-in exposing only what MainWindow reads/calls -- avoids needing a
    real Transport/Session/loop:// thread just to test banner/button refresh logic and
    the ARM flow's own bookkeeping (logger lifecycle), as opposed to the real wire
    protocol (already covered by test_controller.py)."""

    connected: bool = True
    state: proto.CoreState | None = None
    command_pending: bool = False
    hello: proto.HelloBody | None = None
    armed_with: list[ArmConfig] = field(default_factory=list)

    def arm(self, config: ArmConfig) -> int:
        self.armed_with.append(config)
        return 0

    def disconnect(self) -> None:
        pass


def _telemetry(cycle_index: int = 0, position_steps: int = 0, force_mN: int = 1000) -> proto.TelemetryBody:
    return proto.TelemetryBody(
        timestamp_us=0,
        position_steps=position_steps,
        force_raw_counts=0,
        force_raw_mN=force_mN,
        force_filtered_mN=force_mN,
        state=proto.CoreState.RUN,
        flags=0,
        cycle_index=cycle_index,
    )


def test_window_constructs_disconnected(qtbot):
    window = MainWindow()
    qtbot.addWidget(window)
    assert window.state_banner.text() == "DISCONNECTED"
    assert window.hello_label.text() == "Not connected"
    assert window.connect_button.text() == "Connect"


def test_connected_event_updates_hello_label(qtbot):
    window = MainWindow()
    qtbot.addWidget(window)
    hello = proto.HelloBody(
        version_major=0, version_minor=5, build_id=42, capabilities=proto.CAP_CORE,
        cell_profile=0, cpu_hz=600_000_000,
    )
    window._on_event(sess.Connected(hello=hello))
    assert "v0.5" in window.hello_label.text()
    assert "caps=0x01" in window.hello_label.text()


def test_banner_reflects_state_color_when_connected(qtbot):
    window = MainWindow()
    qtbot.addWidget(window)
    window._controller = _FakeController(connected=True, state=proto.CoreState.RUN)
    window._refresh_ui()
    assert window.state_banner.text() == "RUN"


def test_heartbeat_lost_overrides_state_banner(qtbot):
    window = MainWindow()
    qtbot.addWidget(window)
    window._controller = _FakeController(connected=True, state=proto.CoreState.RUN)
    window._on_event(sess.HeartbeatLost())
    assert window.state_banner.text() == "HEARTBEAT LOST"


def test_heartbeat_received_clears_heartbeat_lost(qtbot):
    window = MainWindow()
    qtbot.addWidget(window)
    window._controller = _FakeController(connected=True, state=proto.CoreState.RUN)
    window._on_event(sess.HeartbeatLost())
    assert window.state_banner.text() == "HEARTBEAT LOST"

    hb = proto.HeartbeatBody(
        state=proto.CoreState.RUN, lock_owner=proto.LockOwner.PC, inhibit_mask=0,
        fault_cause=int(proto.EventType.NONE),
    )
    window._on_event(sess.HeartbeatReceived(body=hb))
    assert window.state_banner.text() == "RUN"


def test_command_nacked_shows_status_message(qtbot):
    window = MainWindow()
    qtbot.addWidget(window)
    window._controller = _FakeController()
    window._on_event(
        sess.CommandNacked(
            seq=0, msg_id=int(proto.MsgId.CMD_ARM), error=proto.Err.COUPLER_NOT_CONFIRMED
        )
    )
    assert "COUPLER_NOT_CONFIRMED" in window.statusBar().currentMessage()


def test_command_timed_out_shows_status_message(qtbot):
    window = MainWindow()
    qtbot.addWidget(window)
    window._controller = _FakeController()
    window._on_event(sess.CommandTimedOut(seq=0, msg_id=int(proto.MsgId.CMD_HOME)))
    assert "timed out" in window.statusBar().currentMessage()


def test_telemetry_without_cycling_goes_to_live_plot(qtbot):
    window = MainWindow()
    qtbot.addWidget(window)
    window._controller = _FakeController()
    window._on_event(sess.TelemetryReceived(body=_telemetry(cycle_index=0)))
    assert window.plot_stack.currentWidget() is window.live_plot
    assert window.live_plot.sample_count() == 1


def test_telemetry_with_cycling_switches_to_cycle_plot(qtbot):
    window = MainWindow()
    qtbot.addWidget(window)
    window._controller = _FakeController()
    window._on_event(sess.TelemetryReceived(body=_telemetry(cycle_index=1)))
    assert window.plot_stack.currentWidget() is window.cycle_plot


def test_test_event_appends_to_event_log(qtbot):
    window = MainWindow()
    qtbot.addWidget(window)
    window._controller = _FakeController()
    event = proto.EventBody(
        timestamp_us=0, type=proto.EventType.SUBSTRATE_BREAK, aux=0, position_steps=0, force_mN=0
    )
    window._on_event(sess.EventReceived(body=event))
    assert window.event_log.count() == 1
    assert "SUBSTRATE_BREAK" in window.event_log.item(0).text()


def test_disconnect_resets_ui(qtbot):
    window = MainWindow()
    qtbot.addWidget(window)
    window.port_combo.setCurrentText("loop://")
    window._on_connect_clicked()  # connect
    assert window.connect_button.text() == "Disconnect"

    window._on_connect_clicked()  # disconnect
    assert window.connect_button.text() == "Connect"
    assert window.hello_label.text() == "Not connected"
    assert window._controller is None


def test_disconnect_closes_any_open_logger(qtbot, tmp_path):
    window = MainWindow()
    qtbot.addWidget(window)
    meta = datalog.TestMetadata(
        test_name="x", started_at_wall_clock="t", material="m", l0_mm=1.0,
        cell_profile="p", arm_config={},
    )
    window._logger = datalog.TestLogger(tmp_path, "x", meta)
    window.port_combo.setCurrentText("loop://")
    window._on_connect_clicked()  # connect
    window._on_connect_clicked()  # disconnect
    assert window._logger is None
    # The file must actually be closed (readable), not just the reference dropped.
    assert len(datalog.load_telemetry(tmp_path / "x_telemetry.csv")) == 0


def test_arm_flow_closes_previous_logger_before_opening_a_new_one(qtbot, tmp_path, monkeypatch):
    monkeypatch.setattr(MetadataDialog, "exec", lambda self: QtWidgets.QDialog.DialogCode.Accepted)
    monkeypatch.setattr(ArmConfigDialog, "exec", lambda self: QtWidgets.QDialog.DialogCode.Accepted)

    window = MainWindow()
    qtbot.addWidget(window)
    window.output_dir = tmp_path
    window._controller = _FakeController(
        hello=proto.HelloBody(0, 5, 1, proto.CAP_CORE, 0, 600_000_000)
    )

    old_meta = datalog.TestMetadata(
        test_name="old", started_at_wall_clock="t", material="m", l0_mm=1.0,
        cell_profile="p", arm_config={},
    )
    old_logger = datalog.TestLogger(tmp_path, "old", old_meta)
    window._logger = old_logger

    window._on_arm_requested()

    assert len(window._controller.armed_with) == 1
    assert window._logger is not old_logger
    # The old logger's files must be closed (readable), not leaked open.
    assert len(datalog.load_telemetry(tmp_path / "old_telemetry.csv")) == 0
    # A new logger was opened for the (blank, since dialogs weren't filled in) new test.
    assert (tmp_path / "test_metadata.json").exists()
