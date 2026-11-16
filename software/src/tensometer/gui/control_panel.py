"""Left control panel: buttons gated by the actual state-machine guards, not always-on
(docs/decisions.md GUI-layout entry, 2026-11-16). Emits Qt signals for each requested
action -- never calls Controller directly, so it's testable standalone and MainWindow
(not yet built) owns all the wiring.

STOP is the one exception to every other rule here: it stays enabled whenever connected,
regardless of CoreState or whether another command is still pending a response --
matching Controller.stop()'s own bypass of the one-command-at-a-time rule (an abort must
never wait behind an unrelated in-flight command, caught while building this file,
2026-11-16).
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
    tare_requested = QtCore.Signal()
    preload_requested = QtCore.Signal(float, float)  # threshold_N, approach_mm_per_min
    arm_requested = QtCore.Signal()  # opens the ARM config dialog (main_window's job)
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
        self.jog_neg_button.pressed.connect(
            lambda: self.jog_start_requested.emit(proto.JogDir.TOWARD_LIM_NEAR)
        )
        self.jog_pos_button.pressed.connect(
            lambda: self.jog_start_requested.emit(proto.JogDir.TOWARD_LIM_FAR)
        )
        self.jog_neg_button.released.connect(self.jog_stop_requested)
        self.jog_pos_button.released.connect(self.jog_stop_requested)
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

        self.arm_button = QtWidgets.QPushButton("Arm...")
        self.arm_button.clicked.connect(self.arm_requested)
        layout.addWidget(self.arm_button)

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
            self.jog_neg_button: {CoreState.IDLE, CoreState.READY},
            self.jog_pos_button: {CoreState.IDLE, CoreState.READY},
            self.tare_button: {CoreState.IDLE, CoreState.READY},
            self.preload_button: {CoreState.IDLE, CoreState.READY},
            self.arm_button: {CoreState.IDLE},
            self.start_button: {CoreState.READY},
            self.pause_button: {CoreState.RUN, CoreState.CYCLE},
            self.resume_button: {CoreState.HOLD},
            self.reset_button: {CoreState.FAULT, CoreState.BREAK_HOLD},
        }

        self.update_state(None, command_pending=False, connected=False)

    def _on_select_profile(self) -> None:
        self.select_profile_requested.emit(
            self.profile_combo.currentData(), self.coupler_checkbox.isChecked()
        )

    def update_state(self, state: CoreState | None, command_pending: bool, connected: bool) -> None:
        self.stop_button.setEnabled(connected)
        for button, valid_states in self._buttons_by_states.items():
            if not connected or command_pending:
                button.setEnabled(False)
            elif valid_states is None:
                button.setEnabled(True)
            else:
                button.setEnabled(state in valid_states)
