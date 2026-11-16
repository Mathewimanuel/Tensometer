"""End-to-end: the real Controller/Transport/Session (production code) talking to
FakeFirmware (sim/, a protocol-level stand-in) over a genuine two-party in-memory pipe
(helpers.make_pipe_pair -- not loop://, which can't simulate two distinct parties).
This is what "lets us test if there's anything that needs doing while connecting the two
halves" actually meant (Mathew, 2026-11-16): catch integration bugs the unit tests
(which each test only one side) structurally cannot see.
"""

from helpers import EventCollector, make_pipe_pair

from tensometer import controller as ctrl
from tensometer import session as sess
from tensometer.sim.driver import FakeFirmwareDriver
from tensometer.wire import protocol as proto


def _connected_controller():
    pc_end, fw_end = make_pipe_pair()
    driver = FakeFirmwareDriver(fw_end)
    driver.start()

    collector = EventCollector()
    c = ctrl.Controller(pc_end, on_event=collector)
    c.connect()
    collector.wait_for(lambda e: isinstance(e, sess.Connected))
    return c, driver, collector


def test_connects_and_receives_hello():
    c, driver, _collector = _connected_controller()
    try:
        assert c.connected is True
        assert c.hello is not None
        assert c.hello.capabilities & proto.CAP_CORE
    finally:
        c.disconnect()
        driver.stop()


def test_full_ramp_test_flow_to_break_hold():
    c, driver, collector = _connected_controller()
    try:
        mark = collector.mark()
        c.select_profile(proto.ProfileId.CELL_100KG, coupler_confirmed=True)
        collector.wait_for(lambda e: isinstance(e, sess.CommandAcked), after=mark)

        mark = collector.mark()
        c.home()
        collector.wait_for(lambda e: isinstance(e, sess.CommandAcked), after=mark)
        collector.wait_for(
            lambda e: isinstance(e, sess.HeartbeatReceived) and e.body.state == proto.CoreState.IDLE,
            after=mark,
        )

        mark = collector.mark()
        c.arm(
            ctrl.ArmConfig(l0_mm=5.0, speed_mm_per_min=6000, max_travel_mm=200.0, force_limit_N=0)
        )
        collector.wait_for(lambda e: isinstance(e, sess.CommandAcked), after=mark)
        collector.wait_for(
            lambda e: isinstance(e, sess.HeartbeatReceived) and e.body.state == proto.CoreState.READY,
            after=mark,
        )

        mark = collector.mark()
        c.start()
        collector.wait_for(lambda e: isinstance(e, sess.CommandAcked), after=mark)

        saw_telemetry = False

        def is_break(e):
            nonlocal saw_telemetry
            if isinstance(e, sess.TelemetryReceived):
                saw_telemetry = True
            return (
                isinstance(e, sess.HeartbeatReceived) and e.body.state == proto.CoreState.BREAK_HOLD
            )

        collector.wait_for(is_break, timeout=10.0, after=mark)
        assert saw_telemetry is True
    finally:
        c.disconnect()
        driver.stop()


def test_stop_then_reset_round_trip():
    c, driver, collector = _connected_controller()
    try:
        # CMD_STOP must work even with no test armed/running -- it's accepted from any
        # state (session.py's begin_emergency_command, decisions.md 2026-11-16).
        mark = collector.mark()
        c.stop()
        collector.wait_for(
            lambda e: isinstance(e, sess.HeartbeatReceived) and e.body.state == proto.CoreState.FAULT,
            after=mark,
        )

        # IDLE was also already true before STOP was ever sent (FakeFirmware starts
        # there) -- must only match a fresh IDLE from after reset(), per mark()'s own
        # docstring on exactly this mistake.
        mark = collector.mark()
        c.reset()
        collector.wait_for(lambda e: isinstance(e, sess.CommandAcked), after=mark)
        collector.wait_for(
            lambda e: isinstance(e, sess.HeartbeatReceived) and e.body.state == proto.CoreState.IDLE,
            after=mark,
        )
    finally:
        c.disconnect()
        driver.stop()


def test_nack_surfaces_through_the_real_session():
    c, driver, collector = _connected_controller()
    try:
        # ARM before HOME must NACK (NOT_HOMED), exercising the real Err decode path
        # end to end, not just session.py's own unit tests with hand-built frames.
        c.arm(ctrl.ArmConfig(l0_mm=5.0, speed_mm_per_min=100, max_travel_mm=20.0))
        got = collector.wait_for(lambda e: isinstance(e, sess.CommandNacked))
        assert got.error == proto.Err.NOT_HOMED
    finally:
        c.disconnect()
        driver.stop()
