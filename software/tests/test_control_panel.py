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


def test_rearm_gated_the_same_as_arm(qtbot):
    panel = ControlPanel()
    qtbot.addWidget(panel)
    panel.update_state(proto.CoreState.IDLE, command_pending=False, connected=True)
    assert panel.rearm_button.isEnabled() is True
    panel.update_state(proto.CoreState.READY, command_pending=False, connected=True)
    assert panel.rearm_button.isEnabled() is False


def test_rearm_button_emits_signal(qtbot):
    panel = ControlPanel()
    qtbot.addWidget(panel)
    panel.update_state(proto.CoreState.IDLE, command_pending=False, connected=True)
    with qtbot.waitSignal(panel.rearm_requested, timeout=1000):
        panel.rearm_button.click()


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


def test_jog_held_resends_keepalive_until_released(qtbot):
    # protocol 0.6 deadman: firmware stops the jog without a keep-alive every <250 ms.
    panel = ControlPanel()
    qtbot.addWidget(panel)
    panel.update_state(proto.CoreState.IDLE, command_pending=False, connected=True)
    qtbot.mousePress(panel.jog_neg_button, QtCore.Qt.MouseButton.LeftButton)
    with qtbot.waitSignal(panel.jog_keepalive_requested, timeout=1000) as blocker:
        pass
    assert blocker.args == [proto.JogDir.TOWARD_LIM_NEAR]
    qtbot.mouseRelease(panel.jog_neg_button, QtCore.Qt.MouseButton.LeftButton)
    with qtbot.assertNotEmitted(panel.jog_keepalive_requested, wait=300):
        pass


def test_keepalive_stops_when_state_leaves_jog_while_still_held(qtbot):
    panel = ControlPanel()
    qtbot.addWidget(panel)
    panel.update_state(proto.CoreState.IDLE, command_pending=False, connected=True)
    qtbot.mousePress(panel.jog_pos_button, QtCore.Qt.MouseButton.LeftButton)
    panel.update_state(proto.CoreState.JOG, command_pending=False, connected=True)
    panel.update_state(proto.CoreState.FAULT, command_pending=False, connected=True)  # e.g. STOP
    with qtbot.assertNotEmitted(panel.jog_keepalive_requested, wait=300):
        pass
    qtbot.mouseRelease(panel.jog_pos_button, QtCore.Qt.MouseButton.LeftButton)


def test_keepalive_stops_on_disconnect_and_on_cancel(qtbot):
    panel = ControlPanel()
    qtbot.addWidget(panel)
    panel.update_state(proto.CoreState.IDLE, command_pending=False, connected=True)
    qtbot.mousePress(panel.jog_pos_button, QtCore.Qt.MouseButton.LeftButton)
    panel.update_state(None, command_pending=False, connected=False)
    with qtbot.assertNotEmitted(panel.jog_keepalive_requested, wait=300):
        pass
    qtbot.mouseRelease(panel.jog_pos_button, QtCore.Qt.MouseButton.LeftButton)

    panel.update_state(proto.CoreState.IDLE, command_pending=False, connected=True)
    qtbot.mousePress(panel.jog_pos_button, QtCore.Qt.MouseButton.LeftButton)
    panel.cancel_jog_keepalive()  # MainWindow calls this when the JOG start is NACKed
    with qtbot.assertNotEmitted(panel.jog_keepalive_requested, wait=300):
        pass
    qtbot.mouseRelease(panel.jog_pos_button, QtCore.Qt.MouseButton.LeftButton)


def test_jog_buttons_stay_enabled_in_jog_and_fault(qtbot):
    panel = ControlPanel()
    qtbot.addWidget(panel)
    for state in (proto.CoreState.JOG, proto.CoreState.FAULT):
        panel.update_state(state, command_pending=False, connected=True)
        assert panel.jog_pos_button.isEnabled() is True


def test_held_jog_button_is_never_disabled_so_release_still_sends_stop(qtbot):
    panel = ControlPanel()
    qtbot.addWidget(panel)
    panel.update_state(proto.CoreState.IDLE, command_pending=False, connected=True)
    qtbot.mousePress(panel.jog_pos_button, QtCore.Qt.MouseButton.LeftButton)
    panel.update_state(proto.CoreState.IDLE, command_pending=True, connected=True)
    assert panel.jog_pos_button.isEnabled() is True
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
