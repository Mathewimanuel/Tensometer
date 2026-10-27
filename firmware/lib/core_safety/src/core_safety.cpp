#include "core_safety.h"

namespace core_safety {

FaultReason evaluate(const Inputs& in) {
    // LIM_NEAR is never suspended, even while homing: lib/homing's BACK_OFF phase drives
    // toward it with nothing but a step count watching for overtravel (Mathew, 2026-10-27,
    // narrowing the original "suspend both switches" decision after a real gap was found
    // -- see docs/decisions.md). Only LIM_FAR gets a homing carve-out, since that's the
    // only switch lib/homing actually watches itself.
    if (in.lim_near_active) {
        return FaultReason::LIMIT_SWITCH_OPEN;
    }

    const bool far_rule_active = (in.core_state != mod::CoreState::HOMING);
    if (far_rule_active && in.lim_far_active) {
        return FaultReason::LIMIT_SWITCH_OPEN;
    }

    if (in.motion_active && in.inhibit_mask != 0) {
        return FaultReason::MOTION_WHILE_INHIBITED;
    }

    return FaultReason::NONE;
}

}  // namespace core_safety
