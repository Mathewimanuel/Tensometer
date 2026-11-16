"""Fake firmware: a protocol-level stand-in for the real Teensy, used to exercise the
software stack end-to-end before any real command dispatcher exists in firmware's
main.cpp (docs/decisions.md 2026-11-16). Pure logic -- given incoming frames and a
simulated clock (`now`, a plain float the caller advances; never reads a real clock
itself), decides what to send back.

Deliberately NOT a physics or timing simulator: no attempt to match real step rates,
force curves, or homing duration. Its only job is to exercise every message type's
request/response/stream shape so integration bugs on the software side surface now, not
whenever a real dispatcher eventually gets written. Where it simplifies past what real
firmware actually guards (e.g. profile coupler-slip validity, strain/travel-limit
cross-checks), that's a deliberate scope cut for this stand-in, not a claim about what
the real dispatcher will do -- see each handler's own comment.
"""

from __future__ import annotations

from dataclasses import dataclass

from .. import units
from ..wire import frame as wire_frame
from ..wire import messages
from ..wire import protocol as proto

_HEARTBEAT_INTERVAL_S = 0.2
_TELEMETRY_INTERVAL_S = 1.0 / 80
_HOMING_DURATION_S = 1.0
_ARMING_DURATION_S = 0.3
_TARE_DURATION_S = 0.2
_FORCE_PER_STEP_MN = 50  # arbitrary -- just needs to visibly climb on the plot
_BREAK_TRAVEL_MM = 20.0  # simulated ramp "breaks" after this much simulated travel


@dataclass
class _ArmedConfig:
    l0_steps: int
    speed_steps_per_s: float
    force_limit_mN: int
    max_travel_steps: int
    cycle_mode: bool
    cycle_min_steps: int
    cycle_max_steps: int
    cycle_count: int


class FakeFirmware:
    def __init__(self, build_id: int = 0xF00D) -> None:
        self._build_id = build_id
        self.state = proto.CoreState.BOOT
        self.homed = False
        self.coupler_confirmed = False
        self.position_steps = 0
        self.force_mN = 0
        self.cycle_index = 0
        self._fault_cause = proto.EventType.NONE
        self._armed: _ArmedConfig | None = None
        self._return_state = proto.CoreState.IDLE
        self._phase_started_at: float | None = None
        self._jog_direction: proto.JogDir | None = None
        self._preload_threshold_mN = 0
        self._preload_speed_steps_per_s = 0.0
        self._cycle_going_up = True
        self._last_heartbeat_at: float | None = None
        self._last_telemetry_at: float | None = None

    # ---- outgoing frame helpers --------------------------------------------------
    def _frame(self, msg_id: proto.MsgId, body: bytes = b"") -> bytes:
        return wire_frame.encode(int(msg_id), seq=0, body=body)

    def _ack(self, msg_id: int, seq: int) -> bytes:
        return self._frame(proto.MsgId.ACK, messages.encode_ack(proto.AckBody(msg_id, seq)))

    def _nack(self, msg_id: int, seq: int, err: proto.Err) -> bytes:
        return self._frame(
            proto.MsgId.NACK, messages.encode_nack(proto.NackBody(msg_id, seq, int(err)))
        )

    def hello_frame(self) -> bytes:
        body = proto.HelloBody(
            version_major=proto.VERSION_MAJOR,
            version_minor=proto.VERSION_MINOR,
            build_id=self._build_id,
            capabilities=proto.CAP_CORE | proto.CAP_SIM,
            cell_profile=0,
            cpu_hz=600_000_000,
        )
        return self._frame(proto.MsgId.HELLO, messages.encode_hello(body))

    def _heartbeat_frame(self) -> bytes:
        body = proto.HeartbeatBody(
            state=self.state,
            lock_owner=proto.LockOwner.NONE,
            inhibit_mask=0,
            fault_cause=int(self._fault_cause),
        )
        return self._frame(proto.MsgId.HEARTBEAT, messages.encode_heartbeat_status(body))

    def _telemetry_frame(self, now: float) -> bytes:
        flags = (proto.TELEM_POSITION_VALID if self.homed else 0) | (
            proto.TELEM_HOMED if self.homed else 0
        )
        body = proto.TelemetryBody(
            timestamp_us=int(now * 1_000_000),
            position_steps=self.position_steps,
            force_raw_counts=0,
            force_raw_mN=self.force_mN,
            force_filtered_mN=self.force_mN,
            state=self.state,
            flags=flags,
            cycle_index=self.cycle_index,
        )
        return self._frame(proto.MsgId.TELEMETRY, messages.encode_telemetry(body))

    def _event_frame(self, event_type: proto.EventType, aux: int = 0) -> bytes:
        body = proto.EventBody(
            timestamp_us=0,
            type=event_type,
            aux=aux,
            position_steps=self.position_steps,
            force_mN=self.force_mN,
        )
        return self._frame(proto.MsgId.EVENT, messages.encode_event(body))

    # ---- incoming frames ----------------------------------------------------------
    def on_frame(self, f: wire_frame.Frame, now: float) -> list[bytes]:
        """One incoming frame in, zero or more outgoing frames out (always starting
        with the ACK/NACK for whatever was sent, same as real firmware's "every command
        gets ACK or NACK" rule)."""
        try:
            body = f.body
            if f.msg_id == int(proto.MsgId.CMD_SELECT_PROFILE):
                return self._on_select_profile(f.seq, body)
            if f.msg_id == int(proto.MsgId.CMD_HOME):
                return self._on_home(f.seq, now)
            if f.msg_id == int(proto.MsgId.CMD_JOG):
                return self._on_jog(f.seq, body)
            if f.msg_id == int(proto.MsgId.CMD_TARE):
                return self._on_tare(f.seq, now)
            if f.msg_id == int(proto.MsgId.CMD_PRELOAD):
                return self._on_preload(f.seq, body, now)
            if f.msg_id == int(proto.MsgId.CMD_ARM):
                return self._on_arm(f.seq, body, now)
            if f.msg_id == int(proto.MsgId.CMD_START):
                return self._on_start(f.seq)
            if f.msg_id == int(proto.MsgId.CMD_PAUSE):
                return self._on_pause(f.seq)
            if f.msg_id == int(proto.MsgId.CMD_RESUME):
                return self._on_resume(f.seq)
            if f.msg_id == int(proto.MsgId.CMD_STOP):
                return self._on_stop(f.seq)
            if f.msg_id == int(proto.MsgId.CMD_RESET):
                return self._on_reset(f.seq)
            if f.msg_id == int(proto.MsgId.HEARTBEAT):
                return []  # the PC's own empty-body keep-alive ping; nothing to send back
        except messages.DecodeError:
            return [self._nack(f.msg_id, f.seq, proto.Err.BAD_LENGTH)]
        return [self._nack(f.msg_id, f.seq, proto.Err.UNKNOWN_MSG)]

    def _on_select_profile(self, seq: int, body: bytes) -> list[bytes]:
        parsed = messages.decode_select_profile(body)
        # Scope cut vs. real firmware: every profile is treated as selectable here --
        # safety_limits.h's actual cutoff/coupler-slip numbers are deliberately never
        # duplicated into software/ (docs/decisions.md 2026-11-16), so this stand-in has
        # no way to replicate PROFILE_NOT_SELECTABLE and doesn't pretend to.
        if not parsed.coupler_confirmed:
            return [self._nack(int(proto.MsgId.CMD_SELECT_PROFILE), seq, proto.Err.COUPLER_NOT_CONFIRMED)]
        self.coupler_confirmed = True
        return [self._ack(int(proto.MsgId.CMD_SELECT_PROFILE), seq)]

    def _on_home(self, seq: int, now: float) -> list[bytes]:
        if self.state != proto.CoreState.IDLE:
            return [self._nack(int(proto.MsgId.CMD_HOME), seq, proto.Err.BAD_STATE)]
        self.state = proto.CoreState.HOMING
        self._phase_started_at = now
        return [self._ack(int(proto.MsgId.CMD_HOME), seq)]

    def _on_jog(self, seq: int, body: bytes) -> list[bytes]:
        parsed = messages.decode_jog(body)
        if parsed.action == proto.JOG_ACTION_KEEPALIVE:
            # 0.7: refresh only, same direction, only while jogging; never starts a jog.
            # Scope cut: no deadman timer here (the real firmware stops the jog itself).
            if self.state != proto.CoreState.JOG or parsed.direction != self._jog_direction:
                return [self._nack(int(proto.MsgId.CMD_JOG), seq, proto.Err.BAD_STATE)]
            return [self._ack(int(proto.MsgId.CMD_JOG), seq)]
        if parsed.action == proto.JOG_ACTION_START:
            if self.state == proto.CoreState.JOG:  # 0.7: no implicit keep-alive
                return [self._nack(int(proto.MsgId.CMD_JOG), seq, proto.Err.BAD_STATE)]
            if self.state not in (proto.CoreState.IDLE, proto.CoreState.READY) or not self.homed:
                return [self._nack(int(proto.MsgId.CMD_JOG), seq, proto.Err.NOT_HOMED)]
            self._return_state = self.state
            self.state = proto.CoreState.JOG
            self._jog_direction = parsed.direction
        else:  # STOP
            if self.state != proto.CoreState.JOG:
                return [self._nack(int(proto.MsgId.CMD_JOG), seq, proto.Err.BAD_STATE)]
            self.state = self._return_state
            self._jog_direction = None
        return [self._ack(int(proto.MsgId.CMD_JOG), seq)]

    def _on_tare(self, seq: int, now: float) -> list[bytes]:
        if self.state not in (proto.CoreState.IDLE, proto.CoreState.READY):
            return [self._nack(int(proto.MsgId.CMD_TARE), seq, proto.Err.BAD_STATE)]
        self._return_state = self.state
        self.state = proto.CoreState.TARE
        self._phase_started_at = now
        self.force_mN = 0
        return [self._ack(int(proto.MsgId.CMD_TARE), seq)]

    def _on_preload(self, seq: int, body: bytes, now: float) -> list[bytes]:
        if self.state not in (proto.CoreState.IDLE, proto.CoreState.READY) or not self.homed:
            return [self._nack(int(proto.MsgId.CMD_PRELOAD), seq, proto.Err.NOT_HOMED)]
        parsed = messages.decode_preload(body)
        self._return_state = self.state
        self.state = proto.CoreState.PRELOAD
        self._preload_threshold_mN = parsed.threshold_mN
        self._preload_speed_steps_per_s = units.mm_to_steps(parsed.approach_mm_per_min) / 60.0
        self._phase_started_at = now
        return [self._ack(int(proto.MsgId.CMD_PRELOAD), seq)]

    def _on_arm(self, seq: int, body: bytes, now: float) -> list[bytes]:
        if self.state != proto.CoreState.IDLE:
            return [self._nack(int(proto.MsgId.CMD_ARM), seq, proto.Err.BAD_STATE)]
        if not self.homed:
            return [self._nack(int(proto.MsgId.CMD_ARM), seq, proto.Err.NOT_HOMED)]
        parsed = messages.decode_arm(body)
        l0_steps = units.mm_to_steps(units.um_to_mm(parsed.l0_um))
        self._armed = _ArmedConfig(
            l0_steps=l0_steps,
            speed_steps_per_s=units.mm_to_steps(parsed.speed_mm_per_min) / 60.0,
            force_limit_mN=parsed.force_limit_mN,
            max_travel_steps=units.mm_to_steps(units.um_to_mm(parsed.max_travel_um)),
            cycle_mode=bool(parsed.cycle_mode),
            cycle_min_steps=l0_steps
            + round(l0_steps * parsed.cycle_strain_min_permille / 1000),
            cycle_max_steps=l0_steps
            + round(l0_steps * parsed.cycle_strain_max_permille / 1000),
            cycle_count=parsed.cycle_count,
        )
        self._phase_started_at = now
        # Stays at IDLE here, same as real firmware (skill 3.4: ARM moves to L0 before
        # reporting READY) -- _advance_phase's IDLE branch does the simulated move and
        # flips to READY once _ARMING_DURATION_S has passed, same pattern as HOMING.
        return [self._ack(int(proto.MsgId.CMD_ARM), seq)]

    def _on_start(self, seq: int) -> list[bytes]:
        if self.state != proto.CoreState.READY or self._armed is None:
            return [self._nack(int(proto.MsgId.CMD_START), seq, proto.Err.BAD_STATE)]
        self.state = proto.CoreState.CYCLE if self._armed.cycle_mode else proto.CoreState.RUN
        self.cycle_index = 0
        self._cycle_going_up = True
        return [self._ack(int(proto.MsgId.CMD_START), seq)]

    def _on_pause(self, seq: int) -> list[bytes]:
        if self.state not in (proto.CoreState.RUN, proto.CoreState.CYCLE):
            return [self._nack(int(proto.MsgId.CMD_PAUSE), seq, proto.Err.BAD_STATE)]
        self._return_state = self.state
        self.state = proto.CoreState.HOLD
        return [self._ack(int(proto.MsgId.CMD_PAUSE), seq)]

    def _on_resume(self, seq: int) -> list[bytes]:
        if self.state != proto.CoreState.HOLD:
            return [self._nack(int(proto.MsgId.CMD_RESUME), seq, proto.Err.BAD_STATE)]
        self.state = self._return_state
        return [self._ack(int(proto.MsgId.CMD_RESUME), seq)]

    def _on_stop(self, seq: int) -> list[bytes]:
        self.state = proto.CoreState.FAULT
        self._fault_cause = proto.EventType.OPERATOR_STOP
        return [self._ack(int(proto.MsgId.CMD_STOP), seq), self._event_frame(proto.EventType.OPERATOR_STOP)]

    def _on_reset(self, seq: int) -> list[bytes]:
        if self.state not in (proto.CoreState.FAULT, proto.CoreState.BREAK_HOLD):
            return [self._nack(int(proto.MsgId.CMD_RESET), seq, proto.Err.BAD_STATE)]
        self.state = proto.CoreState.IDLE
        self._fault_cause = proto.EventType.NONE
        self._armed = None
        return [self._ack(int(proto.MsgId.CMD_RESET), seq)]

    # ---- spontaneous frames (heartbeat/telemetry/phase progress) -----------------
    def tick(self, now: float) -> list[bytes]:
        out: list[bytes] = []
        self._advance_phase(now)

        if self._last_heartbeat_at is None or now - self._last_heartbeat_at >= _HEARTBEAT_INTERVAL_S:
            self._last_heartbeat_at = now
            out.append(self._heartbeat_frame())

        streaming_states = (
            proto.CoreState.HOMING, proto.CoreState.JOG, proto.CoreState.TARE,
            proto.CoreState.PRELOAD, proto.CoreState.RUN, proto.CoreState.CYCLE,
        )
        if self.state in streaming_states and (
            self._last_telemetry_at is None or now - self._last_telemetry_at >= _TELEMETRY_INTERVAL_S
        ):
            self._last_telemetry_at = now
            out.append(self._telemetry_frame(now))

        return out

    def _advance_phase(self, now: float) -> None:
        if self.state == proto.CoreState.HOMING:
            if self._phase_started_at is not None and now - self._phase_started_at >= _HOMING_DURATION_S:
                self.position_steps = 0
                self.homed = True
                self.state = proto.CoreState.IDLE
                self._phase_started_at = None

        elif self.state == proto.CoreState.TARE:
            if self._phase_started_at is not None and now - self._phase_started_at >= _TARE_DURATION_S:
                self.state = self._return_state
                self._phase_started_at = None

        elif self.state == proto.CoreState.IDLE and self._armed is not None and self._phase_started_at is not None:
            # Simulated move-to-L0 after ARM (see _on_arm's comment).
            if now - self._phase_started_at >= _ARMING_DURATION_S:
                self.position_steps = self._armed.l0_steps
                self.state = proto.CoreState.READY
                self._phase_started_at = None

        elif self.state == proto.CoreState.JOG and self._jog_direction is not None:
            step = 50 if self._jog_direction == proto.JogDir.TOWARD_LIM_FAR else -50
            self.position_steps += step

        elif self.state == proto.CoreState.PRELOAD:
            self.position_steps += round(self._preload_speed_steps_per_s * 0.05)
            self.force_mN = max(0, self.position_steps) * _FORCE_PER_STEP_MN
            if self.force_mN >= self._preload_threshold_mN:
                self.state = self._return_state

        elif self.state == proto.CoreState.RUN and self._armed is not None:
            self.position_steps += round(self._armed.speed_steps_per_s * 0.0125)  # ~80 Hz tick
            self.force_mN = max(0, self.position_steps - self._armed.l0_steps) * _FORCE_PER_STEP_MN
            travel_mm = units.steps_to_mm(self.position_steps - self._armed.l0_steps)
            if travel_mm >= _BREAK_TRAVEL_MM or (
                self._armed.force_limit_mN and self.force_mN >= self._armed.force_limit_mN
            ):
                self.state = proto.CoreState.BREAK_HOLD
                self._fault_cause = proto.EventType.NONE

        elif self.state == proto.CoreState.CYCLE and self._armed is not None:
            step = round(self._armed.speed_steps_per_s * 0.0125)
            if self._cycle_going_up:
                self.position_steps += step
                if self.position_steps >= self._armed.cycle_max_steps:
                    self._cycle_going_up = False
            else:
                self.position_steps -= step
                if self.position_steps <= self._armed.cycle_min_steps:
                    self._cycle_going_up = True
                    self.cycle_index += 1
            self.force_mN = max(0, self.position_steps - self._armed.l0_steps) * _FORCE_PER_STEP_MN
