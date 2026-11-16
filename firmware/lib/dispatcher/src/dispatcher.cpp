#include "dispatcher.h"

#include <type_traits>

#include "messages.h"

namespace dispatch {

using core_sm::Event;
using proto::Err;
using proto::MsgId;

namespace {

Reply ack(proto::EventType event = proto::EventType::NONE) {
    Reply r;
    r.ack = true;
    r.event = event;
    return r;
}

Reply nack(Err err) {
    Reply r;
    r.err = err;
    return r;
}

// motion_gate() trial-applies events on a by-value copy; that is only sound while the
// state machine is plain data.
static_assert(std::is_trivially_copyable<core_sm::StateMachine>::value,
              "motion_gate copies StateMachine to probe it");

// The switch a relieving jog in `dir` may back off, or NONE if it is not allowed (skill 4
// rule 4, reviewer A): only out of a limit-only FAULT that is not blocked, with a clean
// self-test latch, exactly one switch tripped, moving away from it, not already past the
// episode's travel cap (reviewer minor #5), and nothing else in core_safety firing.
core_safety::Switch relief_allowed(proto::JogDir dir, const Context& ctx,
                                   proto::EventType cause, bool blocked,
                                   const core_safety::ReliefMonitor& relief) {
    // TODO(#10, reviewer #8): also allow it out of an overload FAULT (FORCE_CUTOFF,
    // relieving = retract) once force exists; skill 4 rule 4 covers both.
    if (cause != proto::EventType::LIMIT_STOP || blocked) return core_safety::Switch::NONE;
    if (relief.overtravel(ctx.position_steps)) return core_safety::Switch::NONE;
    if (ctx.self_test != nullptr && !ctx.self_test->ok()) return core_safety::Switch::NONE;
    const bool near = ctx.safety.lim_near_active;
    if (near == ctx.safety.lim_far_active) return core_safety::Switch::NONE;
    const core_safety::Switch sw = near ? core_safety::Switch::LIM_NEAR
                                        : core_safety::Switch::LIM_FAR;
    if (relieved_switch(dir) != sw) return core_safety::Switch::NONE;
    core_safety::Inputs in = ctx.safety;
    in.core_state = mod::CoreState::JOG;
    in.relieving_switch = sw;
    in.relief_overtravel = false;
    return core_safety::evaluate(in) == core_safety::FaultReason::NONE
               ? sw
               : core_safety::Switch::NONE;
}

// A relieving jog out of FAULT is in progress (round 5: any fault arising now ends relief).
bool relieving(const core_sm::StateMachine& sm) {
    return sm.state() == mod::CoreState::JOG && sm.return_state() == mod::CoreState::FAULT;
}

// RESET's "cause cleared" (reviewer B): evaluated here with nothing suppressed and the
// machine's real state, so a switch a relieving jog was ignoring still blocks RESET even
// if the caller's stage-1 snapshot was taken mid-jog.
bool cause_cleared(const Context& ctx, const core_sm::StateMachine& sm) {
    core_safety::Inputs in = ctx.safety;
    in.core_state = sm.state();
    in.relieving_switch = core_safety::Switch::NONE;
    in.relief_overtravel = false;
    return core_safety::evaluate(in) == core_safety::FaultReason::NONE;
}

// Pre-checks for a motion-starting event, in contract order: BAD_STATE, NOT_HOMED,
// MOTION_INHIBITED. Probes a copy of the state machine so the state/homed rules stay
// lib/state_machine's alone: if the event would be refused even with homed/valid forced
// true it is a state problem, otherwise a refusal with the real guards is NOT_HOMED.
Err motion_gate(Event e, const core_sm::Guards& g, const core_sm::StateMachine& sm,
                uint32_t inhibit_mask) {
    core_sm::StateMachine probe = sm;
    core_sm::Guards as_if_homed = g;
    as_if_homed.homed = true;
    as_if_homed.position_valid = true;
    if (!probe.apply(e, as_if_homed)) return Err::BAD_STATE;
    probe = sm;
    if (!probe.apply(e, g)) return Err::NOT_HOMED;
    if (inhibit_mask != 0) return Err::MOTION_INHIBITED;  // decisions.md 2026-10-26 item 4
    return Err::NONE;
}

// Final step for every gated command: force fail-closed check, then the real apply().
Reply commit(Event e, const core_sm::Guards& g, core_sm::StateMachine& sm, bool force_valid) {
    if (!force_valid) return nack(Err::BAD_STATE);  // fail closed until #10 (2026-11-16)
    return sm.apply(e, g) ? ack() : nack(Err::BAD_STATE);
}

bool abs_below(int32_t v, int32_t limit) { return v > -limit && v < limit; }

}  // namespace

Reply Dispatcher::handle(uint8_t msg_id, const uint8_t* body, size_t body_len,
                         const Context& ctx, core_sm::StateMachine& sm) {
    core_sm::Guards g;
    g.homed = ctx.homed;
    g.position_valid = ctx.position_valid;
    g.module_inhibit_present = ctx.safety.inhibit_mask != 0;

    switch (static_cast<MsgId>(msg_id)) {
        case MsgId::CMD_STOP: {
            // Decision 1: stop first, judge the body after. Always accepted (protocol.h).
            // Rounds 5-6: a STOP that enters FAULT (from anywhere, including a relieving
            // jog) blocks relief until RESET; a STOP while already in FAULT changes nothing.
            if (sm.state() != mod::CoreState::FAULT) relief_blocked_ = true;
            sm.apply(Event::SAFETY_TRIP, g);
            // Never overwrites an existing cause (reviewer #5): e.g. a STOP during a
            // relieving jog keeps LIMIT_STOP and the relief episode.
            if (cause_ == proto::EventType::NONE) cause_ = proto::EventType::OPERATOR_STOP;
            if (body_len == 0) return ack(proto::EventType::OPERATOR_STOP);
            Reply r = nack(Err::BAD_LENGTH);
            r.event = proto::EventType::OPERATOR_STOP;  // it did stop: still log it
            return r;
        }

        case MsgId::CMD_RESET: {
            if (body_len != 0) return nack(Err::BAD_LENGTH);
            const bool resettable = sm.state() == mod::CoreState::FAULT ||
                                    sm.state() == mod::CoreState::BREAK_HOLD;
            g.cause_cleared = cause_cleared(ctx, sm);
            // Decision 4: a latched self-test failure is part of "cause cleared".
            if (g.cause_cleared && resettable && ctx.self_test != nullptr) {
                g.cause_cleared = ctx.self_test->retry();
            }
            if (!sm.apply(Event::RESET, g)) return nack(Err::BAD_STATE);
            cause_ = proto::EventType::NONE;
            relief_blocked_ = false;
            relief_.reset_episode();
            return ack(proto::EventType::RESET);
        }

        case MsgId::CMD_PAUSE:
            if (body_len != 0) return nack(Err::BAD_LENGTH);
            return sm.apply(Event::PAUSE, g) ? ack() : nack(Err::BAD_STATE);

        case MsgId::CMD_SELECT_PROFILE: {
            proto::SelectProfileBody b{};
            if (messages::decode_select_profile(body, body_len, &b) != messages::Result::OK) {
                return nack(Err::BAD_LENGTH);
            }
            // IDLE only: from READY on, a frozen ARM config was validated against the
            // current profile's cutoff (skill 3.1), so switching under it is not allowed.
            if (sm.state() != mod::CoreState::IDLE || sm.arming()) return nack(Err::BAD_STATE);
            if (b.profile_id >= static_cast<uint8_t>(limits::ProfileId::COUNT) ||
                b.coupler_confirmed > 1) {
                return nack(Err::OUT_OF_RANGE);  // never clamp (skill 4 rule 6)
            }
            if (b.coupler_confirmed == 0) return nack(Err::COUPLER_NOT_CONFIRMED);
            if (!limits::profile_selectable(limits::PROFILES[b.profile_id])) {
                return nack(Err::PROFILE_NOT_SELECTABLE);
            }
            profile_ = static_cast<limits::ProfileId>(b.profile_id);
            coupler_confirmed_ = true;
            return ack();
        }

        case MsgId::CMD_HOME: {
            if (body_len != 0) return nack(Err::BAD_LENGTH);
            const Err e = motion_gate(Event::HOME, g, sm, ctx.safety.inhibit_mask);
            if (e != Err::NONE) return nack(e);
            // Skill 3.4 guard; only meaningful with a real reading (commit() fails closed).
            if (ctx.force_valid && !abs_below(ctx.force_mN, limits::HOMING_ALLOWED_MAX_mN)) {
                return nack(Err::OUT_OF_RANGE);
            }
            return commit(Event::HOME, g, sm, ctx.force_valid);
        }

        case MsgId::CMD_JOG: {
            proto::JogBody b{};
            if (messages::decode_jog(body, body_len, &b) != messages::Result::OK) {
                return nack(Err::BAD_LENGTH);
            }
            // Fields first (reviewer #6/C): direction is ignored only for STOP.
            if (b.action > proto::JOG_ACTION_KEEPALIVE ||
                (b.action != proto::JOG_ACTION_STOP && static_cast<uint8_t>(b.direction) > 1)) {
                return nack(Err::OUT_OF_RANGE);
            }
            if (b.action == proto::JOG_ACTION_STOP) {
                // Stopping is never gated on force or inhibit: it ends motion.
                return sm.apply(Event::JOG_STOP, g) ? ack() : nack(Err::BAD_STATE);
            }
            if (b.action == proto::JOG_ACTION_KEEPALIVE) {
                // Decision 1 (0.7): refresh only; never starts or redirects motion.
                if (sm.state() != mod::CoreState::JOG || b.direction != jog_dir_ ||
                    !ctx.force_valid) {
                    return nack(Err::BAD_STATE);
                }
                last_jog_us_ = ctx.now_us;
                return ack();
            }
            const bool from_fault = sm.state() == mod::CoreState::FAULT;
            const core_safety::Switch relief =
                from_fault ? relief_allowed(b.direction, ctx, cause_, relief_blocked_, relief_)
                           : core_safety::Switch::NONE;
            g.jog_relieving = relief != core_safety::Switch::NONE;
            const Err e = motion_gate(Event::JOG_START, g, sm, ctx.safety.inhibit_mask);
            if (e != Err::NONE) return nack(e);
            // Loaded-jog speed cap is |force|-dependent (protocol.h), so JOG fails closed.
            const Reply r = commit(Event::JOG_START, g, sm, ctx.force_valid);
            if (r.ack) {
                jog_dir_ = b.direction;
                last_jog_us_ = ctx.now_us;
                if (from_fault) {
                    relief_.start(relief, ctx.position_steps, limits::RELIEF_MAX_TRAVEL_STEPS,
                                  limits::RELIEF_CLEAR_DEBOUNCE_US);
                }
            }
            return r;
        }

        case MsgId::CMD_TARE: {
            if (body_len != 0) return nack(Err::BAD_LENGTH);
            const Err e = motion_gate(Event::TARE_START, g, sm, ctx.safety.inhibit_mask);
            if (e != Err::NONE) return nack(e);
            return commit(Event::TARE_START, g, sm, ctx.force_valid);  // zeroes the cell
        }

        case MsgId::CMD_PRELOAD: {
            proto::PreloadBody b{};
            if (messages::decode_preload(body, body_len, &b) != messages::Result::OK) {
                return nack(Err::BAD_LENGTH);
            }
            const Err e = motion_gate(Event::PRELOAD_START, g, sm, ctx.safety.inhibit_mask);
            if (e != Err::NONE) return nack(e);
            // Profile active NOW, not whenever SELECT_PROFILE was sent (decisions.md 2026-11-13).
            if (b.threshold_mN == 0 ||
                b.threshold_mN > static_cast<uint32_t>(profile_limits().cutoff_mN) ||
                b.approach_mm_per_min == 0 ||
                b.approach_mm_per_min > cfg::JOG_LOADED_MAX_MM_PER_MIN) {
                return nack(Err::OUT_OF_RANGE);
            }
            const Reply r = commit(Event::PRELOAD_START, g, sm, ctx.force_valid);
            if (r.ack) preload_ = b;
            return r;
        }

        case MsgId::CMD_ARM: {
            proto::ArmBody b{};
            if (messages::decode_arm(body, body_len, &b) != messages::Result::OK) {
                return nack(Err::BAD_LENGTH);
            }
            g.config_valid = true;  // the field checks below are what make it so
            const Err e = motion_gate(Event::ARM, g, sm, ctx.safety.inhibit_mask);
            if (e != Err::NONE) return nack(e);
            // The coupler is per-cell and mechanical (skill 2.2): the boot-default profile
            // was never confirmed by anyone, so a test may not run on it unconfirmed.
            if (!coupler_confirmed_) return nack(Err::COUPLER_NOT_CONFIRMED);
            if (!arm_fields_valid(b)) return nack(Err::OUT_OF_RANGE);
            const Reply r = commit(Event::ARM, g, sm, ctx.force_valid);
            if (r.ack) arm_ = b;  // frozen at ARM (skill 3.1)
            return r;
        }

        case MsgId::CMD_START: {
            if (body_len != 0) return nack(Err::BAD_LENGTH);
            g.cycle_mode = arm_.cycle_mode == 1;
            const Err e = motion_gate(Event::START, g, sm, ctx.safety.inhibit_mask);
            if (e != Err::NONE) return nack(e);
            // lib/state_machine does not re-check this for START; skill 4 rule 1 does.
            if (!(ctx.homed && ctx.position_valid)) return nack(Err::NOT_HOMED);
            return commit(Event::START, g, sm, ctx.force_valid);
        }

        case MsgId::CMD_RESUME:
            if (body_len != 0) return nack(Err::BAD_LENGTH);
            // Inhibit is lib/state_machine's soft guard here -> BAD_STATE (protocol.h).
            // Resuming RUN/CYCLE needs the cutoff and break detection: fail closed.
            return commit(Event::RESUME, g, sm, ctx.force_valid);

        default:
            return nack(Err::UNKNOWN_MSG);
    }
}

proto::EventType Dispatcher::safety_tick(core_sm::StateMachine& sm,
                                         core_safety::FaultReason reason) {
    if (reason == core_safety::FaultReason::NONE) return proto::EventType::NONE;
    const proto::EventType ev = fault_event(reason);
    if (sm.state() != mod::CoreState::FAULT) return trip(sm, ev);
    if (ev == cause_) return proto::EventType::NONE;  // same condition still present
    // A different cause arose while already in FAULT (skill 4 rule 9): report it. Only a
    // RESET to IDLE clears the block or the episode; this overwrite never does.
    cause_ = ev;
    if (ev != proto::EventType::LIMIT_STOP) relief_blocked_ = true;
    return ev;
}

proto::EventType Dispatcher::trip(core_sm::StateMachine& sm, proto::EventType cause) {
    // Rounds 5-6 (Mathew): ANY fault during a relieving jog, and any cause other than
    // LIMIT_STOP, ends relief until RESET reaches IDLE; the episode is kept, never restarted.
    if (relieving(sm) || cause != proto::EventType::LIMIT_STOP) relief_blocked_ = true;
    sm.apply(Event::SAFETY_TRIP, {});
    cause_ = cause;
    return cause;
}

proto::EventType Dispatcher::tick(uint64_t now_us, core_sm::StateMachine& sm) {
    if (sm.state() != mod::CoreState::JOG || !jog_deadman_expired(now_us)) {
        return proto::EventType::NONE;
    }
    sm.apply(Event::JOG_STOP, {});
    relief_.stop();
    return proto::EventType::JOG_DEADMAN;
}

void Dispatcher::apply_relief(core_safety::Inputs& in, const core_sm::StateMachine& sm,
                              int32_t position_steps, uint64_t now_us) {
    if (sm.state() != mod::CoreState::JOG || sm.return_state() != mod::CoreState::FAULT) {
        relief_.stop();
        return;
    }
    // Order matters: suppressed() records the first clear before overtravel() is asked.
    in.relieving_switch = relief_.suppressed(in.lim_near_active, in.lim_far_active, now_us);
    in.relief_overtravel = relief_.overtravel(position_steps);
}

bool Dispatcher::arm_fields_valid(const proto::ArmBody& b) const {
    // TODO(verify): "L0 within stroke" (skill 4 rule 6) cannot be checked until
    // cfg::JAW_GAP_AT_HOME_UM is measured (#6); l0_um > 0 only guards the strain division.
    if (b.l0_um == 0 || b.max_travel_um == 0) return false;
    if (b.speed_mm_per_min == 0 || b.speed_mm_per_min > cfg::SPEED_MAX_MM_PER_MIN) return false;
    if (b.force_limit_mN > static_cast<uint32_t>(profile_limits().cutoff_mN)) return false;
    if (b.cycle_mode > 1) return false;
    if (b.cycle_mode == 0) return true;
    if (b.cycle_count == 0) return false;
    if (b.cycle_strain_min_permille >= b.cycle_strain_max_permille) return false;
    if (b.strain_limit_permille != 0 && b.cycle_strain_max_permille > b.strain_limit_permille) {
        return false;
    }
    const uint64_t peak_travel_um =
        static_cast<uint64_t>(b.l0_um) * b.cycle_strain_max_permille / 1000u;
    return peak_travel_um <= b.max_travel_um;
}

core_safety::Switch relieved_switch(proto::JogDir dir) {
    return dir == proto::JogDir::TOWARD_LIM_FAR ? core_safety::Switch::LIM_NEAR
                                                : core_safety::Switch::LIM_FAR;
}

proto::EventType fault_event(core_safety::FaultReason reason) {
    using core_safety::FaultReason;
    switch (reason) {
        case FaultReason::NONE: return proto::EventType::NONE;
        case FaultReason::ESTOP: return proto::EventType::ESTOP;
        case FaultReason::LIMIT_SWITCH_OPEN: return proto::EventType::LIMIT_STOP;
        case FaultReason::HEARTBEAT_LOST: return proto::EventType::HEARTBEAT_LOST;
        // No dedicated wire EventType for these (open question, decisions.md 2026-11-16).
        case FaultReason::GUARD_SENSE_OPEN:
        case FaultReason::MODULE_FAULT:
        case FaultReason::SOFT_TRAVEL_LIMIT:
        case FaultReason::STRAIN_LIMIT:
        case FaultReason::MOTION_WHILE_INHIBITED:
        case FaultReason::RELIEF_OVERTRAVEL:
            return proto::EventType::FAULT;
    }
    return proto::EventType::FAULT;
}

}  // namespace dispatch
