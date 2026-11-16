from PySide6 import QtCore

from tensometer.gui.control_panel import ControlPanel
from tensometer.wire import protocol as proto


def test_widget_constructs_with_everything_disabled(qtbot):
    panel = ControlPanel()
    qtbot.addWidget(panel)
    assert panel.home_button.isEnabled() is False
    assert panel.stop_button.isEnabled() is False


def test_all_buttons_disabled_when_not_connected(qtbot):
    panel = ControlPanel()
    qtbot.addWidget(panel)
    panel.update_state(proto.CoreState.IDLE, command_pending=False, connected=False)
    assert panel.home_button.isEnabled() is False
    assert panel.stop_button.isEnabled() is False


def test_home_enabled_only_in_idle(qtbot):
    panel = ControlPanel()
    qtbot.addWidget(panel)
    panel.update_state(proto.CoreState.IDLE, command_pending=False, connected=True)
    assert panel.home_button.isEnabled() is True

    panel.update_state(proto.CoreState.READY, command_pending=False, connected=True)
    assert panel.home_button.isEnabled() is False


def test_start_enabled_only_in_ready(qtbot):
    panel = ControlPanel()
    qtbot.addWidget(panel)
    panel.update_state(proto.CoreState.READY, command_pending=False, connected=True)
    assert panel.start_button.isEnabled() is True
    panel.update_state(proto.CoreState.IDLE, command_pending=False, connected=True)
    assert panel.start_button.isEnabled() is False


def test_stop_enabled_regardless_of_state_once_connected(qtbot):
    panel = ControlPanel()
    qtbot.addWidget(panel)
    for state in proto.CoreState:
        panel.update_state(state, command_pending=False, connected=True)
        assert panel.stop_button.isEnabled() is True


def test_stop_stays_enabled_while_a_command_is_pending(qtbot):
    panel = ControlPanel()
    qtbot.addWidget(panel)
    panel.update_state(proto.CoreState.IDLE, command_pending=True, connected=True)
    assert panel.stop_button.isEnabled() is True  # the one exception
    assert panel.home_button.isEnabled() is False  # every other button is not


def test_select_profile_signal_emits_combo_and_checkbox_state(qtbot):
    panel = ControlPanel()
    qtbot.addWidget(panel)
    panel.update_state(proto.CoreState.IDLE, command_pending=False, connected=True)
    panel.profile_combo.setCurrentIndex(1)
    panel.coupler_checkbox.setChecked(True)

    with qtbot.waitSignal(panel.select_profile_requested, timeout=1000) as blocker:
        panel.select_profile_button.click()
    assert blocker.args == [panel.profile_combo.currentData(), True]


def test_jog_press_and_release_emit_start_and_stop_signals(qtbot):
    panel = ControlPanel()
    qtbot.addWidget(panel)
    panel.update_state(proto.CoreState.IDLE, command_pending=False, connected=True)

    with qtbot.waitSignal(panel.jog_start_requested, timeout=1000) as blocker:
        qtbot.mousePress(panel.jog_pos_button, QtCore.Qt.MouseButton.LeftButton)
    assert blocker.args == [proto.JogDir.TOWARD_LIM_FAR]

    with qtbot.waitSignal(panel.jog_stop_requested, timeout=1000):
        qtbot.mouseRelease(panel.jog_pos_button, QtCore.Qt.MouseButton.LeftButton)


def test_preload_signal_emits_spin_box_values(qtbot):
    panel = ControlPanel()
    qtbot.addWidget(panel)
    panel.update_state(proto.CoreState.IDLE, command_pending=False, connected=True)
    panel.preload_threshold_spin.setValue(150.0)
    panel.preload_speed_spin.setValue(10.0)

    with qtbot.waitSignal(panel.preload_requested, timeout=1000) as blocker:
        panel.preload_button.click()
    assert blocker.args == [150.0, 10.0]
