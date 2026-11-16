"""Main window: top bar (port/connect, HELLO readout, state banner), left control
panel, Live/Review tabs (docs/decisions.md GUI-layout entry, 2026-11-16).

Controller's on_event callback fires from a background thread (transport.py's own
docstring warns of this) -- this file never touches a Qt widget directly from there.
Instead the callback just emits a Qt signal (_event_signal); Qt auto-marshals that to
the GUI thread via a queued connection because emitter and receiver live in different
threads. The actual widget updates all happen in _on_event(), which only ever runs on
the GUI thread.
"""

from __future__ import annotations

import datetime
from pathlib import Path

import serial.tools.list_ports
from PySide6 import QtCore, QtWidgets

from .. import datalog
from .. import session as sess
from ..controller import ArmConfig, Controller
from ..transport import TransportError
from ..wire import protocol as proto
from .control_panel import ControlPanel
from .cycle_plot import CyclePlotWidget
from .dialogs import ArmConfigDialog, MetadataDialog
from .live_plot import LivePlotWidget
from .review_tab import ReviewTab

_STATE_COLORS = {
    proto.CoreState.BOOT: "#7f8c8d",
    proto.CoreState.IDLE: "#27ae60",
    proto.CoreState.READY: "#27ae60",
    proto.CoreState.HOMING: "#2980b9",
    proto.CoreState.JOG: "#2980b9",
    proto.CoreState.TARE: "#2980b9",
    proto.CoreState.PRELOAD: "#2980b9",
    proto.CoreState.RUN: "#2980b9",
    proto.CoreState.CYCLE: "#2980b9",
    proto.CoreState.HOLD: "#f39c12",
    proto.CoreState.BREAK_HOLD: "#c0392b",
    proto.CoreState.FAULT: "#c0392b",
}

_BANNER_STYLE = "background-color: {color}; color: white; font-weight: bold; padding: 6px;"


class MainWindow(QtWidgets.QMainWindow):
    _event_signal = QtCore.Signal(object)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Tensometer")
        self._controller: Controller | None = None
        self._logger: datalog.TestLogger | None = None
        # Settable (no settings dialog exists yet to change it from the UI) rather than
        # hardcoded inline, specifically so tests can redirect it to a tmp_path instead
        # of writing into wherever the app happens to be launched from.
        self.output_dir = Path("tests_out")
        self._heartbeat_lost = False
        # Specimen metadata carried forward for Re-arm (Mathew, 2026-11-16): reusing the
        # last Arm...'d specimen's material/L0/dimensions/notes without re-showing
        # MetadataDialog. Reset on disconnect/TransportError -- a new connection has no
        # business assuming the previous session's specimen is still on the machine.
        self._last_metadata_fields: dict | None = None
        self._base_test_name: str | None = None
        self._rearm_count = 0

        self._event_signal.connect(self._on_event)

        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        outer = QtWidgets.QVBoxLayout(central)
        outer.addWidget(self._build_top_bar())

        body = QtWidgets.QHBoxLayout()
        outer.addLayout(body)

        self.control_panel = ControlPanel()
        self._wire_control_panel()
        body.addWidget(self.control_panel, stretch=0)

        self.tabs = QtWidgets.QTabWidget()
        body.addWidget(self.tabs, stretch=1)

        self.live_plot = LivePlotWidget()
        self.cycle_plot = CyclePlotWidget()
        self.plot_stack = QtWidgets.QStackedWidget()
        self.plot_stack.addWidget(self.live_plot)
        self.plot_stack.addWidget(self.cycle_plot)

        live_tab = QtWidgets.QWidget()
        live_layout = QtWidgets.QVBoxLayout(live_tab)
        live_layout.addWidget(self.plot_stack, stretch=3)
        self.event_log = QtWidgets.QListWidget()
        live_layout.addWidget(self.event_log, stretch=1)
        self.tabs.addTab(live_tab, "Live")

        self.review_tab = ReviewTab()
        self.tabs.addTab(self.review_tab, "Review")

        self.statusBar()
        self._refresh_ui()

    # ---- top bar ---------------------------------------------------------------
    def _build_top_bar(self) -> QtWidgets.QWidget:
        bar = QtWidgets.QWidget()
        layout = QtWidgets.QHBoxLayout(bar)

        self.port_combo = QtWidgets.QComboBox()
        self.port_combo.setEditable(True)
        self._refresh_ports()
        layout.addWidget(self.port_combo)

        refresh_button = QtWidgets.QPushButton("Refresh ports")
        refresh_button.clicked.connect(self._refresh_ports)
        layout.addWidget(refresh_button)

        self.connect_button = QtWidgets.QPushButton("Connect")
        self.connect_button.clicked.connect(self._on_connect_clicked)
        layout.addWidget(self.connect_button)

        self.hello_label = QtWidgets.QLabel("Not connected")
        layout.addWidget(self.hello_label, stretch=1)

        self.state_banner = QtWidgets.QLabel("DISCONNECTED")
        self.state_banner.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        self.state_banner.setStyleSheet(_BANNER_STYLE.format(color="#7f8c8d"))
        layout.addWidget(self.state_banner, stretch=1)

        return bar

    def _refresh_ports(self) -> None:
        current = self.port_combo.currentText()
        self.port_combo.clear()
        for p in serial.tools.list_ports.comports():
            self.port_combo.addItem(p.device)
        if current:
            self.port_combo.setCurrentText(current)

    # ---- connection lifecycle ----------------------------------------------------
    def _on_connect_clicked(self) -> None:
        if self._controller is None:
            port = self.port_combo.currentText()
            if not port:
                QtWidgets.QMessageBox.warning(self, "No port", "Select a serial port first.")
                return
            # Opening a bad/busy/disconnected port raises synchronously here, on the GUI
            # thread -- caught live during manual testing (2026-11-16, docs/decisions.md):
            # an unhandled SerialException crashed straight through this method before
            # this try/except existed.
            try:
                controller = Controller(port, on_event=self._event_signal.emit)
                controller.connect()
            except Exception as exc:  # noqa: BLE001 -- surfaced to the user, not swallowed
                QtWidgets.QMessageBox.critical(
                    self, "Could not connect", f"Could not open {port!r}:\n{exc}"
                )
                return
            self._controller = controller
            self.connect_button.setText("Disconnect")
        else:
            self._controller.disconnect()
            self._controller = None
            self._heartbeat_lost = False
            self.hello_label.setText("Not connected")
            self.connect_button.setText("Connect")
            if self._logger is not None:
                self._logger.close()
                self._logger = None
            self._forget_last_specimen()
        self._refresh_ui()

    def _forget_last_specimen(self) -> None:
        """A new connection has no business assuming the previous session's specimen
        is still on the machine -- Re-arm must not be offered to reuse it."""
        self._last_metadata_fields = None
        self._base_test_name = None
        self._rearm_count = 0

    def closeEvent(self, event) -> None:
        """Disconnects the serial port and closes any open test log cleanly on window
        close -- without this, closing the window mid-test would leave the background
        reader thread running and the log files never flushed/closed."""
        # A held jog button never gets its release once the window is gone: stop the jog
        # explicitly first (the firmware deadman would catch it anyway, ~250 ms later).
        self.control_panel.stop_active_jog()
        if self._controller is not None:
            self._controller.disconnect()
        if self._logger is not None:
            self._logger.close()
        super().closeEvent(event)

    # ---- control panel wiring ------------------------------------------------------
    def _wire_control_panel(self) -> None:
        cp = self.control_panel
        cp.select_profile_requested.connect(
            self._guarded(lambda pid, cc: self._controller.select_profile(proto.ProfileId(pid), cc))
        )
        cp.home_requested.connect(self._guarded(lambda: self._controller.home()))
        cp.jog_start_requested.connect(self._guarded(lambda d: self._controller.jog_start(d)))
        cp.jog_keepalive_requested.connect(
            self._guarded(lambda d: self._controller.jog_keepalive(d))
        )
        cp.jog_stop_requested.connect(self._guarded(lambda: self._controller.jog_stop()))
        cp.tare_requested.connect(self._guarded(lambda: self._controller.tare()))
        cp.preload_requested.connect(self._guarded(lambda t, s: self._controller.preload(t, s)))
        cp.arm_requested.connect(self._on_arm_requested)
        cp.rearm_requested.connect(self._on_rearm_requested)
        cp.start_requested.connect(self._guarded(lambda: self._controller.start()))
        cp.pause_requested.connect(self._guarded(lambda: self._controller.pause()))
        cp.resume_requested.connect(self._guarded(lambda: self._controller.resume()))
        cp.stop_requested.connect(self._guarded(lambda: self._controller.stop()))
        cp.reset_requested.connect(self._guarded(lambda: self._controller.reset()))

    def _guarded(self, fn):
        """Wraps an action so AlreadyPendingError/NotConnectedError (or anything else)
        show up as a status-bar message instead of an unhandled exception. The control
        panel is supposed to prevent both by disabling buttons; this is defense in
        depth for a stale click queued right as state changes, not the primary gate."""

        def wrapped(*args):
            if self._controller is None:
                return
            try:
                fn(*args)
            except Exception as exc:  # noqa: BLE001 -- surfaced to the user, not swallowed
                self.statusBar().showMessage(f"Action failed: {exc}", 5000)

        return wrapped

    def _on_arm_requested(self) -> None:
        """Full flow: specimen metadata, then machine settings. Starts a fresh
        Re-arm count -- the next Re-arm will be "<test_name>_run2", not stacked on top
        of whatever a previous connection's Re-arms had counted up to."""
        if self._controller is None:
            return
        meta_dialog = MetadataDialog(self)
        if meta_dialog.exec() != QtWidgets.QDialog.DialogCode.Accepted:
            return
        metadata_fields = meta_dialog.result_fields()
        self._last_metadata_fields = metadata_fields
        self._base_test_name = metadata_fields["test_name"]
        self._rearm_count = 0
        self._arm_with_metadata(metadata_fields, metadata_fields["test_name"])

    def _on_rearm_requested(self) -> None:
        """Reuses the last Arm...'d specimen metadata (material/L0/dimensions/notes) --
        only the machine-settings (ArmConfigDialog) step repeats. Each Re-arm gets its
        own log file (the base test name plus a run number), since every run needs its
        own CSV regardless of how the specimen was described."""
        if self._controller is None:
            return
        if self._last_metadata_fields is None:
            self.statusBar().showMessage("No previous test to reuse -- use Arm... first.", 5000)
            return
        self._rearm_count += 1
        test_name = f"{self._base_test_name}_run{self._rearm_count + 1}"
        self._arm_with_metadata(self._last_metadata_fields, test_name)

    def _arm_with_metadata(self, metadata_fields: dict, test_name: str) -> None:
        """Shared by _on_arm_requested and _on_rearm_requested: show the ARM config
        dialog, send CMD_ARM, open a new TestLogger for `test_name` using
        `metadata_fields` as the specimen description."""
        if self._controller is None:
            return
        arm_dialog = ArmConfigDialog(self, l0_mm=metadata_fields["l0_mm"])
        if arm_dialog.exec() != QtWidgets.QDialog.DialogCode.Accepted:
            return
        config: ArmConfig = arm_dialog.arm_config()

        hello = self._controller.hello
        metadata = datalog.TestMetadata(
            test_name=test_name,
            started_at_wall_clock=datetime.datetime.now().isoformat(),
            material=metadata_fields["material"],
            l0_mm=config.l0_mm,  # the value the firmware actually uses, not the dialog default
            cell_profile=self.control_panel.profile_combo.currentText(),
            arm_config=config.__dict__,
            specimen_width_mm=metadata_fields["specimen_width_mm"],
            specimen_thickness_mm=metadata_fields["specimen_thickness_mm"],
            operator_notes=metadata_fields["operator_notes"],
            firmware_build_id=hello.build_id if hello else None,
            protocol_version=f"{hello.version_major}.{hello.version_minor}" if hello else None,
        )
        if self._logger is not None:
            # Re-arming without disconnecting first would otherwise leak the previous
            # test's open file handles indefinitely (caught during review, 2026-11-16).
            self._logger.close()
            self._logger = None
        # Open the log before sending ARM: a logger failure (bad name, existing files)
        # must not leave the machine armed with nothing recording the test.
        try:
            self._logger = datalog.TestLogger(self.output_dir, test_name, metadata)
        except OSError as exc:
            self.statusBar().showMessage(f"Cannot open test log: {exc}", 8000)
            return
        try:
            self._controller.arm(config)
        except Exception as exc:  # noqa: BLE001
            self._logger.discard()
            self._logger = None
            self.statusBar().showMessage(f"ARM failed: {exc}", 5000)
            return
        self.live_plot.clear_data()
        self.cycle_plot.clear_data()
        self.event_log.clear()
        self.plot_stack.setCurrentWidget(self.live_plot)

    # ---- event handling (GUI thread only -- see module docstring) -------------------
    def _on_event(self, event: sess.SessionEvent | TransportError) -> None:
        if isinstance(event, sess.Connected):
            hello = event.hello
            self.hello_label.setText(
                f"HELLO v{hello.version_major}.{hello.version_minor} "
                f"caps=0x{hello.capabilities:02X} profile={hello.cell_profile}"
            )
        elif isinstance(event, sess.HeartbeatReceived):
            self._heartbeat_lost = False
        elif isinstance(event, sess.HeartbeatLost):
            self._heartbeat_lost = True
        elif isinstance(event, sess.CommandNacked):
            if event.msg_id == int(proto.MsgId.CMD_JOG):
                # Any JOG NACK (the START, or a keep-alive) means this jog can't be kept
                # alive: stop the timer. Not matched by seq on purpose -- an urgent
                # keep-alive replaces the pending START, so a late START NACK never arrives
                # here as such. A spurious cancel only lets the firmware deadman end the jog.
                self.control_panel.cancel_jog_keepalive()
            self.statusBar().showMessage(
                f"{proto.MsgId(event.msg_id).name} rejected: {event.error.name}", 5000
            )
        elif isinstance(event, sess.CommandTimedOut):
            self.statusBar().showMessage(
                f"{proto.MsgId(event.msg_id).name} timed out waiting for a response", 5000
            )
        elif isinstance(event, sess.TelemetryReceived):
            self._on_telemetry(event.body)
        elif isinstance(event, sess.EventReceived):
            self._on_test_event(event.body)
        elif isinstance(event, TransportError):
            self._on_transport_error(event)

        self._refresh_ui()

    def _on_transport_error(self, event: TransportError) -> None:
        """The background reader thread has already stopped itself (transport.py's own
        TransportError docstring) -- this resets the UI the same way a manual Disconnect
        would, since nothing will arrive on this connection again."""
        QtWidgets.QMessageBox.critical(self, "Connection lost", event.message)
        if self._controller is not None:
            self._controller.disconnect()
            self._controller = None
        self._heartbeat_lost = False
        self.hello_label.setText("Not connected")
        self.connect_button.setText("Connect")
        if self._logger is not None:
            self._logger.close()
            self._logger = None
        self._forget_last_specimen()

    def _on_telemetry(self, body: proto.TelemetryBody) -> None:
        if body.cycle_index > 0 and self.plot_stack.currentWidget() is not self.cycle_plot:
            self.plot_stack.setCurrentWidget(self.cycle_plot)
        if self.plot_stack.currentWidget() is self.cycle_plot:
            self.cycle_plot.add_telemetry(body)
        else:
            self.live_plot.add_telemetry(body)
        if self._logger is not None:
            self._logger.log_telemetry(body)

    def _on_test_event(self, body: proto.EventBody) -> None:
        self.live_plot.add_event(body)
        self.event_log.addItem(
            f"{body.timestamp_us} {body.type.name} pos={body.position_steps} force={body.force_mN}mN"
        )
        if self._logger is not None:
            self._logger.log_event(body)

    # ---- UI refresh ------------------------------------------------------------
    def _refresh_ui(self) -> None:
        connected = self._controller is not None and self._controller.connected
        state = self._controller.state if self._controller else None
        pending = self._controller.command_pending if self._controller else False

        self.control_panel.update_state(state, command_pending=pending, connected=connected)

        if self._controller is None:
            self.state_banner.setText("DISCONNECTED")
            self.state_banner.setStyleSheet(_BANNER_STYLE.format(color="#7f8c8d"))
        elif self._heartbeat_lost:
            self.state_banner.setText("HEARTBEAT LOST")
            self.state_banner.setStyleSheet(_BANNER_STYLE.format(color="#e67e22"))
        elif state is not None:
            self.state_banner.setText(state.name)
            self.state_banner.setStyleSheet(
                _BANNER_STYLE.format(color=_STATE_COLORS.get(state, "#7f8c8d"))
            )
        else:
            self.state_banner.setText("CONNECTING...")
            self.state_banner.setStyleSheet(_BANNER_STYLE.format(color="#7f8c8d"))
