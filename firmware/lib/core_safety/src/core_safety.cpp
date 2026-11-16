#include "core_safety.h"

namespace core_safety {

FaultReason evaluate(const Inputs& in) {
    if (in.estop_active) {
        return FaultReason::ESTOP;
    }

    // LIM_NEAR is never suspended, even while homing: lib/homing's BACK_OFF phase drives
    // toward it with nothing but a step count watching for overtravel (Mathew, 2026-10-27,
    // narrowing the original "suspend both switches" decision after a real gap was found
    // -- see docs/decisions.md). Only LIM_FAR gets a homing carve-out, since that's the
    // only switch lib/homing actually watches itself.
    const bool relieving = (in.core_state == mod::CoreState::JOG);
    const bool near_suppressed = relieving && in.relieving_switch == Switch::LIM_NEAR;
    if (in.lim_near_active && !near_suppressed) {
        return FaultReason::LIMIT_SWITCH_OPEN;
    }

    const bool far_suppressed = relieving && in.relieving_switch == Switch::LIM_FAR;
    const bool far_rule_active = (in.core_state != mod::CoreState::HOMING) && !far_suppressed;
    if (far_rule_active && in.lim_far_active) {
        return FaultReason::LIMIT_SWITCH_OPEN;
    }

    if (in.relief_overtravel) {
        return FaultReason::RELIEF_OVERTRAVEL;
    }

    // GUARD_INTERLOCK_FITTED is applied here, not folded into a caller-supplied bool: it's
    // a pure constexpr with no Arduino dependency, so there's no reason to push this AND
    // out to an untested caller (found by firmware-reviewer -- inconsistent with how every
    // other gate in this file is handled).
    if (cfg::GUARD_INTERLOCK_FITTED && in.run_to_break_armed && in.guard_sense_active) {
        return FaultReason::GUARD_SENSE_OPEN;
    }

    if (in.module_fault_present) {
        return FaultReason::MODULE_FAULT;
    }

    if (in.soft_travel_limit_exceeded) {
        return FaultReason::SOFT_TRAVEL_LIMIT;
    }

    if (in.strain_limit_exceeded) {
        return FaultReason::STRAIN_LIMIT;
    }

    if (in.motion_active && in.heartbeat_stale) {
        return FaultReason::HEARTBEAT_LOST;
    }

    if (in.motion_active && in.inhibit_mask != 0) {
        return FaultReason::MOTION_WHILE_INHIBITED;
    }

    return FaultReason::NONE;
}

Switch ReliefMonitor::suppressed(bool lim_near_active, bool lim_far_active, uint64_t now_us) {
    if (!active() || cleared_) return Switch::NONE;
    const bool tripped = (switch_ == Switch::LIM_NEAR) ? lim_near_active : lim_far_active;
    if (tripped) {
        clearing_ = false;  // any tripped read restarts the debounce
        return switch_;
    }
    if (!clearing_) {
        clearing_ = true;
        clear_since_us_ = now_us;
    }
    if (now_us - clear_since_us_ < debounce_us_) return switch_;  // not clear for long enough
    cleared_ = true;  // re-armed from now on (2a)
    return Switch::NONE;
}

bool ReliefMonitor::overtravel(int32_t position_steps) const {
    if (!episode_ || cleared_) return false;
    const int64_t d = static_cast<int64_t>(position_steps) - start_;
    return static_cast<uint64_t>(d < 0 ? -d : d) > max_steps_;
}

}  // namespace core_safety
