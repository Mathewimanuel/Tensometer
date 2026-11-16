"""Left control panel: buttons gated by the actual state-machine guards, not always-on
(docs/decisions.md GUI-layout entry, 2026-11-16). Emits Qt signals for each requested
action -- never calls Controller directly, so it's testable standalone and MainWindow
(not yet built) owns all the wiring.

STOP is the one exception to every other rule here: it stays enabled whenever connected,
regardless of CoreState or whether another command is still pending a response --
matching Controller.stop()'s own bypass of the one-command-at-a-time rule (an abort must
never wait behind an unrelated in-flight command, caught while building this file,
2026-11-16).

Jog (protocol 0.7 deadman): a press sends one JOG START; while held, jog_keepalive_requested
(JOG KEEPALIVE) re-fires every proto.JOG_KEEPALIVE_INTERVAL_S, and the firmware stops the
jog itself if keep-alives stop. The keep-alives stop on release, on cancel_jog_keepalive()
(MainWindow: a JOG START or KEEPALIVE was NACKed), once the state has been JOG and then leaves it, and on
disconnect.
Jog buttons stay enabled in JOG (and FAULT, for the relieving jog), and a held button is
never disabled -- otherwise Qt drops the release and JOG stop is never sent.
"""

from __future__ import annotations

from PySide6 import QtCore, QtWidgets

from ..wire import protocol as proto

CoreState = proto.CoreState


class ControlPanel(QtWidgets.QWidget):
    select_profile_requested = QtCore.Signal(int, bool)  # profile_id, coupler_confirmed
    home_requested = QtCore.Signal()
    jog_start_requested = QtCore.Signal(object)  # proto.JogDir
    jog_stop_requested = QtCore.Signal()
    jog_keepalive_requested = QtCore.Signal(object)  # proto.JogDir, while a jog button is held
    tare_requested = QtCore.Signal()
    preload_requested = QtCore.Signal(float, float)  # threshold_N, approach_mm_per_min
    arm_requested = QtCore.Signal()  # opens the metadata + ARM config dialogs (main_window's job)
    rearm_requested = QtCore.Signal()  # reuses last specimen metadata, only re-opens ARM config
    start_requested = QtCore.Signal()
    pause_requested = QtCore.Signal()
    resume_requested = QtCore.Signal()
    stop_requested = QtCore.Signal()
    reset_requested = QtCore.Signal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        layout = QtWidgets.QVBoxLayout(self)

        profile_group = QtWidgets.QGroupBox("Cell profile")
        profile_layout = QtWidgets.QHBoxLayout(profile_group)
        self.profile_combo = QtWidgets.QComboBox()
        for p in proto.ProfileId:
            self.profile_combo.addItem(p.name, p.value)
        self.coupler_checkbox = QtWidgets.QCheckBox("Coupler confirmed")
        self.select_profile_button = QtWidgets.QPushButton("Select")
        self.select_profile_button.clicked.connect(self._on_select_profile)
        profile_layout.addWidget(self.profile_combo)
        profile_layout.addWidget(self.coupler_checkbox)
        profile_layout.addWidget(self.select_profile_button)
        layout.addWidget(profile_group)

        self.home_button = QtWidgets.QPushButton("Home")
        self.home_button.clicked.connect(self.home_requested)
        layout.addWidget(self.home_button)

        jog_group = QtWidgets.QGroupBox("Jog (press and hold)")
        jog_layout = QtWidgets.QHBoxLayout(jog_group)
        self.jog_neg_button = QtWidgets.QPushButton("- toward near")
        self.jog_pos_button = QtWidgets.QPushButton("+ toward far")
        self._jog_dir: proto.JogDir | None = None
        self._jog_seen_jog = False  # state reached JOG since this press
        self._jog_timer = QtCore.QTimer(self)
        self._jog_timer.setInterval(round(proto.JOG_KEEPALIVE_INTERVAL_S * 1000))
        self._jog_timer.timeout.connect(self._on_jog_keepalive)
        self.jog_neg_button.pressed.connect(lambda: self._on_jog_pressed(proto.JogDir.TOWARD_LIM_NEAR))
        self.jog_pos_button.pressed.connect(lambda: self._on_jog_pressed(proto.JogDir.TOWARD_LIM_FAR))
        self.jog_neg_button.released.connect(self._on_jog_released)
        self.jog_pos_button.released.connect(self._on_jog_released)
        jog_layout.addWidget(self.jog_neg_button)
        jog_layout.addWidget(self.jog_pos_button)
        layout.addWidget(jog_group)

        self.tare_button = QtWidgets.QPushButton("Tare")
        self.tare_button.clicked.connect(self.tare_requested)
        layout.addWidget(self.tare_button)

        preload_group = QtWidgets.QGroupBox("Preload")
        preload_layout = QtWidgets.QHBoxLayout(preload_group)
        self.preload_threshold_spin = QtWidgets.QDoubleSpinBox()
        self.preload_threshold_spin.setRange(0, 10_000)
        self.preload_threshold_spin.setSuffix(" N")
        self.preload_speed_spin = QtWidgets.QDoubleSpinBox()
        self.preload_speed_spin.setRange(0, 100)
        self.preload_speed_spin.setSuffix(" mm/min")
        self.preload_button = QtWidgets.QPushButton("Preload")
        self.preload_button.clicked.connect(
            lambda: self.preload_requested.emit(
                self.preload_threshold_spin.value(), self.preload_speed_spin.value()
            )
        )
        preload_layout.addWidget(self.preload_threshold_spin)
        preload_layout.addWidget(self.preload_speed_spin)
        preload_layout.addWidget(self.preload_button)
        layout.addWidget(preload_group)

        arm_layout = QtWidgets.QHBoxLayout()
        self.arm_button = QtWidgets.QPushButton("Arm...")
        self.arm_button.clicked.connect(self.arm_requested)
        arm_layout.addWidget(self.arm_button)
        self.rearm_button = QtWidgets.QPushButton("Re-arm...")
        self.rearm_button.setToolTip(
            "Reuses the last specimen metadata; only re-opens the machine-settings dialog."
        )
        self.rearm_button.clicked.connect(self.rearm_requested)
        arm_layout.addWidget(self.rearm_button)
        layout.addLayout(arm_layout)

        self.start_button = QtWidgets.QPushButton("Start")
        self.start_button.clicked.connect(self.start_requested)
        layout.addWidget(self.start_button)

        pause_resume_layout = QtWidgets.QHBoxLayout()
        self.pause_button = QtWidgets.QPushButton("Pause")
        self.pause_button.clicked.connect(self.pause_requested)
        self.resume_button = QtWidgets.QPushButton("Resume")
        self.resume_button.clicked.connect(self.resume_requested)
        pause_resume_layout.addWidget(self.pause_button)
        pause_resume_layout.addWidget(self.resume_button)
        layout.addLayout(pause_resume_layout)

        self.stop_button = QtWidgets.QPushButton("STOP")
        self.stop_button.setStyleSheet(
            "background-color: #c0392b; color: white; font-weight: bold;"
        )
        self.stop_button.clicked.connect(self.stop_requested)
        layout.addWidget(self.stop_button)

        self.reset_button = QtWidgets.QPushButton("Reset")
        self.reset_button.clicked.connect(self.reset_requested)
        layout.addWidget(self.reset_button)

        layout.addStretch(1)

        # None means "enabled in any state" (subject to connected/command_pending);
        # stop_button is handled as a hard special case in update_state(), not via
        # this table, since it bypasses command_pending too, not just state.
        self._buttons_by_states: dict[QtWidgets.QAbstractButton, set[CoreState] | None] = {
            self.select_profile_button: {CoreState.IDLE},
            self.home_button: {CoreState.IDLE},
            # JOG: keep-alives/release happen while jogging. FAULT: relieving jog (0.6).
            self.jog_neg_button: {CoreState.IDLE, CoreState.READY, CoreState.JOG, CoreState.FAULT},
            self.jog_pos_button: {CoreState.IDLE, CoreState.READY, CoreState.JOG, CoreState.FAULT},
            self.tare_button: {CoreState.IDLE, CoreState.READY},
            self.preload_button: {CoreState.IDLE, CoreState.READY},
            self.arm_button: {CoreState.IDLE},
            self.rearm_button: {CoreState.IDLE},
            self.start_button: {CoreState.READY},
            self.pause_button: {CoreState.RUN, CoreState.CYCLE},
            self.resume_button: {CoreState.HOLD},
            self.reset_button: {CoreState.FAULT, CoreState.BREAK_HOLD},
        }

        self.update_state(None, command_pending=False, connected=False)

    def jog_active(self) -> bool:
        """True while a jog button press is in progress (START sent, not yet released)."""
        return self._jog_dir is not None

    def cancel_jog_keepalive(self) -> None:
        """Stops keep-alives without emitting jog_stop (the release still will)."""
        self._jog_timer.stop()

    def _on_jog_pressed(self, direction: proto.JogDir) -> None:
        self._jog_dir = direction
        self._jog_seen_jog = False
        self.jog_start_requested.emit(direction)
        self._jog_timer.start()

    def _on_jog_keepalive(self) -> None:
        if self._jog_dir is not None:
            self.jog_keepalive_requested.emit(self._jog_dir)

    def _on_jog_released(self) -> None:
        self._jog_timer.stop()
        if self._jog_dir is None:
            return  # already ended (e.g. stop_active_jog() on window close)
        self._jog_dir = None
        self.jog_stop_requested.emit()

    def _on_select_profile(self) -> None:
        self.select_profile_requested.emit(
            self.profile_combo.currentData(), self.coupler_checkbox.isChecked()
        )

    def stop_active_jog(self) -> bool:
        """Ends an in-progress jog press as if released: stops keep-alives and emits
        jog_stop. Returns whether one was active. For MainWindow's closeEvent."""
        if self._jog_dir is None:
            return False
        self._on_jog_released()
        return True

    def update_state(self, state: CoreState | None, command_pending: bool, connected: bool) -> None:
        if self._jog_dir is not None:
            if state == CoreState.JOG:
                self._jog_seen_jog = True
            elif not connected or self._jog_seen_jog:
                self.cancel_jog_keepalive()  # disconnected, or the jog ended (STOP, deadman...)
        self.stop_button.setEnabled(connected)
        for button, valid_states in self._buttons_by_states.items():
            if button.isDown():
                continue  # never disable a held button: its release must still fire
            if not connected or command_pending:
                button.setEnabled(False)
            elif valid_states is None:
                button.setEnabled(True)
            else:
                button.setEnabled(state in valid_states)
