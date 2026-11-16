"""High-level, human-units app API on top of Session/Transport. This is the one place
that knows how to turn "arm this test" or "jog in this direction" into the right wire
bytes and units -- a GUI (or the CLI, or a test script) calls Controller, never
transport/session/wire directly. Not a "dispatcher" in the firmware sense (that term
describes firmware-side command-handling code that doesn't exist yet) -- this is the
PC-side equivalent.

Owns the Transport it's given a port for; tracks the connection's current state
(CoreState, lock owner, inhibit mask, fault cause, latest telemetry) by watching the
same events it forwards to the caller.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass

from . import session as sess
from . import transport as tp
from . import units
from .wire import messages
from .wire import protocol as proto

EventCallback = Callable[[sess.SessionEvent], None]


class NotConnectedError(RuntimeError):
    """Raised when an action is attempted before a HELLO has been received."""


@dataclass
class ArmConfig:
    """Human-units input for CMD_ARM; Controller.arm() converts it to ArmBody's wire
    units (um/mN/permille)."""

    l0_mm: float
    speed_mm_per_min: float
    max_travel_mm: float
    force_limit_N: float = 0.0  # 0 = disabled (profile cutoff only)
    strain_limit_percent: float = 0.0  # 0 = disabled
    cycle_mode: bool = False
    cycle_strain_min_percent: float = 0.0
    cycle_strain_max_percent: float = 0.0
    cycle_count: int = 0


class Controller:
    def __init__(
        self, port: str, baudrate: int = 115200, on_event: EventCallback | None = None
    ) -> None:
        self._on_event = on_event or (lambda _event: None)
        self._transport = tp.Transport(port, baudrate=baudrate, on_event=self._handle_event)

        self.hello: proto.HelloBody | None = None
        self.state: proto.CoreState | None = None
        self.lock_owner: proto.LockOwner | None = None
        self.inhibit_mask: int = 0
        self.fault_cause: int = int(proto.EventType.NONE)
        self.latest_telemetry: proto.TelemetryBody | None = None

    # ---- lifecycle -----------------------------------------------------------------
    def connect(self) -> None:
        self._transport.start()

    def disconnect(self) -> None:
        self._transport.stop()

    @property
    def connected(self) -> bool:
        return self._transport.session.connected

    @property
    def command_pending(self) -> bool:
        return self._transport.session.has_pending_command()

    def _handle_event(self, event: sess.SessionEvent) -> None:
        if isinstance(event, sess.Connected):
            self.hello = event.hello
        elif isinstance(event, sess.HeartbeatReceived):
            self.state = event.body.state
            self.lock_owner = event.body.lock_owner
            self.inhibit_mask = event.body.inhibit_mask
            self.fault_cause = event.body.fault_cause
        elif isinstance(event, sess.TelemetryReceived):
            self.latest_telemetry = event.body
        self._on_event(event)

    def _ensure_connected(self) -> None:
        if not self.connected:
            raise NotConnectedError("not connected -- no HELLO received yet")

    def _send(self, msg_id: proto.MsgId, body: bytes = b"") -> int:
        self._ensure_connected()
        return self._transport.send_command(int(msg_id), body)

    # ---- actions (skill section 8 / protocol.h 0.5) ---------------------------------
    def select_profile(self, profile_id: proto.ProfileId, coupler_confirmed: bool) -> int:
        body = proto.SelectProfileBody(
            profile_id=int(profile_id), coupler_confirmed=int(coupler_confirmed)
        )
        return self._send(proto.MsgId.CMD_SELECT_PROFILE, messages.encode_select_profile(body))

    def home(self) -> int:
        return self._send(proto.MsgId.CMD_HOME)

    def jog_start(self, direction: proto.JogDir) -> int:
        body = proto.JogBody(direction=direction, action=0)
        return self._send(proto.MsgId.CMD_JOG, messages.encode_jog(body))

    def jog_stop(self) -> int:
        # direction is ignored by firmware when action == STOP (protocol.h's own
        # comment); TOWARD_LIM_FAR here is an arbitrary placeholder, not meaningful.
        body = proto.JogBody(direction=proto.JogDir.TOWARD_LIM_FAR, action=1)
        return self._send(proto.MsgId.CMD_JOG, messages.encode_jog(body))

    def tare(self) -> int:
        return self._send(proto.MsgId.CMD_TARE)

    def preload(self, threshold_N: float, approach_mm_per_min: float) -> int:
        body = proto.PreloadBody(
            threshold_mN=units.n_to_mN(threshold_N),
            approach_mm_per_min=round(approach_mm_per_min),
        )
        return self._send(proto.MsgId.CMD_PRELOAD, messages.encode_preload(body))

    def arm(self, config: ArmConfig) -> int:
        body = proto.ArmBody(
            l0_um=units.mm_to_um(config.l0_mm),
            speed_mm_per_min=round(config.speed_mm_per_min),
            force_limit_mN=units.n_to_mN(config.force_limit_N),
            max_travel_um=units.mm_to_um(config.max_travel_mm),
            strain_limit_permille=units.percent_to_permille(config.strain_limit_percent),
            cycle_mode=1 if config.cycle_mode else 0,
            reserved=0,
            cycle_strain_min_permille=units.percent_to_permille(config.cycle_strain_min_percent),
            cycle_strain_max_permille=units.percent_to_permille(config.cycle_strain_max_percent),
            cycle_count=config.cycle_count,
        )
        return self._send(proto.MsgId.CMD_ARM, messages.encode_arm(body))

    def start(self) -> int:
        return self._send(proto.MsgId.CMD_START)

    def pause(self) -> int:
        return self._send(proto.MsgId.CMD_PAUSE)

    def resume(self) -> int:
        return self._send(proto.MsgId.CMD_RESUME)

    def stop(self) -> int:
        """Emergency abort -- bypasses the one-command-at-a-time rule (never raises
        AlreadyPendingError). Connection must still be real (NotConnectedError if not),
        but a STOP must never wait behind an unrelated in-flight command (session.py's
        begin_emergency_command)."""
        self._ensure_connected()
        return self._transport.send_emergency_command(int(proto.MsgId.CMD_STOP))

    def reset(self) -> int:
        return self._send(proto.MsgId.CMD_RESET)
