"""Protocol session state: HELLO handshake, one-command-at-a-time seq correlation
(ACK/NACK/timeout), and heartbeat health. Pure logic -- no serial I/O, no threads, no Qt;
testable with injected frames and a fake clock. `transport.py` is the thin driver that
feeds this from a real pyserial port (not written yet).

One command in flight at a time, not pipelined: this project's GUI and physical panel
both work by "press one button, wait for the result" (docs/decisions.md 2026-11-16 GUI
layout entry -- buttons gated by state), so there is never a reason to have two commands
racing for the same seq.
"""

from __future__ import annotations

from dataclasses import dataclass

from .wire import frame as wire_frame
from .wire import messages
from .wire import protocol as proto

HEARTBEAT_PING_INTERVAL_S = 0.1  # PC sends its own keep-alive every 100 ms (skill section 8)
HEARTBEAT_TIMEOUT_S = 1.0  # matches limits::HEARTBEAT_TIMEOUT_US (firmware, 1 s)
# Not itself a protocol requirement (no round-trip time is specified anywhere) -- generous
# given the firmware main loop's own <2 ms worst-case budget (skill section 3.3).
COMMAND_TIMEOUT_S = 1.0


# ---- Events returned by feed_frame()/tick() for the caller (transport.py, later the
# GUI) to react to. A plain dataclass-per-event union, not a single flexible struct, so a
# caller's `match`/isinstance dispatch is exhaustive-checkable rather than keying off a
# string/enum field. ----------------------------------------------------------------
@dataclass(frozen=True, slots=True)
class Connected:
    hello: proto.HelloBody


@dataclass(frozen=True, slots=True)
class CommandAcked:
    seq: int
    msg_id: int


@dataclass(frozen=True, slots=True)
class CommandNacked:
    seq: int
    msg_id: int
    error: proto.Err


@dataclass(frozen=True, slots=True)
class CommandTimedOut:
    seq: int
    msg_id: int


@dataclass(frozen=True, slots=True)
class TelemetryReceived:
    body: proto.TelemetryBody


@dataclass(frozen=True, slots=True)
class EventReceived:
    body: proto.EventBody


@dataclass(frozen=True, slots=True)
class HeartbeatReceived:
    body: proto.HeartbeatBody


@dataclass(frozen=True, slots=True)
class HeartbeatLost:
    """Firmware's own HEARTBEAT hasn't arrived within HEARTBEAT_TIMEOUT_S -- distinct
    from a deliberate disconnect (docs/decisions.md 2026-11-16: the UI must tell these
    apart, not show the same thing for both)."""


@dataclass(frozen=True, slots=True)
class SendHeartbeatPing:
    """The caller should encode and write an empty-body HEARTBEAT frame now."""


@dataclass(frozen=True, slots=True)
class UnknownMessage:
    msg_id: int
    seq: int
    body: bytes


SessionEvent = (
    Connected
    | CommandAcked
    | CommandNacked
    | CommandTimedOut
    | TelemetryReceived
    | EventReceived
    | HeartbeatReceived
    | HeartbeatLost
    | SendHeartbeatPing
    | UnknownMessage
)


@dataclass
class _PendingCommand:
    seq: int
    msg_id: int
    sent_at: float


class AlreadyPendingError(RuntimeError):
    """Raised by begin_command() when a command is already awaiting ACK/NACK/timeout."""


class Session:
    def __init__(self) -> None:
        self._next_seq = 0
        self._pending: _PendingCommand | None = None
        self._connected = False
        self._last_rx_heartbeat: float | None = None
        self._heartbeat_lost_declared = False
        self._last_tx_heartbeat_ping: float | None = None

    @property
    def connected(self) -> bool:
        """True once a HELLO has been received. Nothing before that should be treated
        as a live connection -- a serial port can open successfully onto a device that
        never sends HELLO at all."""
        return self._connected

    def has_pending_command(self) -> bool:
        return self._pending is not None

    def begin_command(self, msg_id: int, body: bytes, now: float) -> tuple[int, bytes]:
        """Allocates the next seq and returns (seq, encoded_wire_frame) for a new
        outgoing command. Raises AlreadyPendingError if one is already in flight --
        the caller (GUI) must wait for CommandAcked/CommandNacked/CommandTimedOut
        before sending another, same as the state-gated button design."""
        if self._pending is not None:
            raise AlreadyPendingError("a command is already pending a response")
        seq = self._next_seq
        self._next_seq = (self._next_seq + 1) % 256
        self._pending = _PendingCommand(seq=seq, msg_id=msg_id, sent_at=now)
        return seq, wire_frame.encode(msg_id, seq, body)

    def begin_emergency_command(self, msg_id: int, body: bytes, now: float) -> tuple[int, bytes]:
        """Like begin_command(), but never raises AlreadyPendingError -- replaces
        whatever was pending (if anything) rather than queuing behind it. For CMD_STOP
        only: the protocol accepts it from any state as an immediate abort (skill
        section 10, "STOP acted on immediately"), and an operator must never have to
        wait for an unrelated in-flight command to resolve before an emergency stop
        goes out. Caught while designing the GUI's control panel (2026-11-16) --
        begin_command()'s one-at-a-time rule would otherwise block exactly this.

        A stale ACK/NACK for whatever was replaced, if it arrives later, is safely
        ignored by feed_frame() (its seq no longer matches self._pending) -- same
        already-tested mismatched-seq handling as any other case."""
        seq = self._next_seq
        self._next_seq = (self._next_seq + 1) % 256
        self._pending = _PendingCommand(seq=seq, msg_id=msg_id, sent_at=now)
        return seq, wire_frame.encode(msg_id, seq, body)

    def feed_frame(self, f: wire_frame.Frame, now: float) -> list[SessionEvent]:
        """Call once per complete frame decoded from the incoming byte stream."""
        events: list[SessionEvent] = []

        if f.msg_id == int(proto.MsgId.HELLO):
            events.append(Connected(hello=messages.decode_hello(f.body)))
            self._connected = True

        elif f.msg_id == int(proto.MsgId.ACK):
            ack = messages.decode_ack(f.body)
            if self._pending is not None and self._pending.seq == ack.acked_seq:
                events.append(CommandAcked(seq=ack.acked_seq, msg_id=ack.acked_msg_id))
                self._pending = None

        elif f.msg_id == int(proto.MsgId.NACK):
            nack = messages.decode_nack(f.body)
            if self._pending is not None and self._pending.seq == nack.acked_seq:
                events.append(
                    CommandNacked(
                        seq=nack.acked_seq,
                        msg_id=nack.acked_msg_id,
                        error=proto.Err(nack.error_code),
                    )
                )
                self._pending = None

        elif f.msg_id == int(proto.MsgId.HEARTBEAT):
            # Only the firmware's own heartbeat (carrying a status body) is meaningful
            # here -- the PC's own empty-body ping is never echoed back, so a non-empty
            # body is the only kind this side should ever receive.
            if f.body:
                events.append(HeartbeatReceived(body=messages.decode_heartbeat_status(f.body)))
                self._last_rx_heartbeat = now
                self._heartbeat_lost_declared = False

        elif f.msg_id == int(proto.MsgId.TELEMETRY):
            events.append(TelemetryReceived(body=messages.decode_telemetry(f.body)))

        elif f.msg_id == int(proto.MsgId.EVENT):
            events.append(EventReceived(body=messages.decode_event(f.body)))

        else:
            events.append(UnknownMessage(msg_id=f.msg_id, seq=f.seq, body=f.body))

        return events

    def tick(self, now: float) -> list[SessionEvent]:
        """Call periodically (e.g. every 20-50 ms) to check command timeouts, heartbeat
        health, and whether it's time to send another heartbeat ping. Never sends bytes
        itself -- SendHeartbeatPing just tells the caller (transport.py) to do it."""
        events: list[SessionEvent] = []

        if self._pending is not None and now - self._pending.sent_at > COMMAND_TIMEOUT_S:
            events.append(CommandTimedOut(seq=self._pending.seq, msg_id=self._pending.msg_id))
            self._pending = None

        if (
            self._connected
            and self._last_rx_heartbeat is not None
            and not self._heartbeat_lost_declared
            and now - self._last_rx_heartbeat > HEARTBEAT_TIMEOUT_S
        ):
            self._heartbeat_lost_declared = True
            events.append(HeartbeatLost())

        if self._connected and (
            self._last_tx_heartbeat_ping is None
            or now - self._last_tx_heartbeat_ping >= HEARTBEAT_PING_INTERVAL_S
        ):
            self._last_tx_heartbeat_ping = now
            events.append(SendHeartbeatPing())

        return events
