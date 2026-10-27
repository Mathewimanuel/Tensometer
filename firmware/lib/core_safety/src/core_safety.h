// Core-safety evaluation (skill section 3.3 step 1, section 4; issue #11, slice 1). Pure
// logic, no Arduino dependency: given this tick's state/sensor snapshot, decides whether a
// SAFETY_TRIP is due and why. The caller (main loop, not built yet) is the one place that
// actually calls core_sm::StateMachine::apply(Event::SAFETY_TRIP, ...) -- this file only
// decides; it never touches the state machine or hardware itself (skill 3.1: one owner per
// piece of state).
//
// Scope for this slice, decided 2026-10-26/27 (docs/decisions.md): only the two rules that
// were blocking main.cpp wiring of #9's pieces. Every other skill-4 rule (force cutoff,
// e-stop sense, heartbeat, stale sensor, watchdog, slip-suspect, GUARD_SENSE, soft travel
// limits) is explicitly OUT OF SCOPE here and not checked by evaluate() at all -- this is
// not a complete safety module, just its first two rules. Do not treat evaluate() returning
// NONE as "safe to move"; it only means these two specific rules didn't fire.
//
//   1. Limit-switch-open -> FAULT (skill 4 rule 2), EXCEPT LIM_FAR while homing is active.
//      Homing deliberately drives the carriage into LIM_FAR as part of normal operation
//      (lib/homing's FAST_APPROACH/SLOW_REAPPROACH phases) and lib/homing already watches
//      it every tick. LIM_NEAR is NEVER suspended, even during homing (narrowed 2026-10-27
//      after review: lib/homing's BACK_OFF phase drives toward LIM_NEAR using only a step
//      count to detect overtravel, with no switch watching it at all -- an earlier version
//      of this carve-out suspended both switches for all of HOMING and left that path with
//      no safety net, the same bug class as the direction-reversal CRITICAL bug found
//      earlier in #9). See docs/decisions.md 2026-10-27.
//   2. Motion active (or about to become active) while any inhibit-mask bit is set ->
//      FAULT (skill 4 rule 2). This is NOT suppressed during homing -- the carve-out above
//      is specifically about the limit-switch rule, not a blanket pass on every safety
//      rule while homing. Caller contract matters here (see Inputs::motion_active below):
//      this must stay true for the ENTIRE duration motion is in progress, not just the
//      instant a command is dispatched, so a module asserting an inhibit bit mid-RUN (or
//      during any other already-in-progress move) still trips -- the exact scenario
//      decisions.md 2026-10-26 ("Core state machine... item 4") calls out as the reason
//      this check lives in the core-safety loop instead of lib/state_machine::apply().
#pragma once

#include <stdint.h>

#include "module_api.h"  // mod::CoreState

namespace core_safety {

enum class FaultReason : uint8_t {
    NONE,
    LIMIT_SWITCH_OPEN,
    MOTION_WHILE_INHIBITED,
};

struct Inputs {
    mod::CoreState core_state = mod::CoreState::BOOT;
    uint32_t inhibit_mask = 0;        // combined from every enabled module's inhibit_bits()
    // True for every tick motion is in progress OR a motion-starting event (HOME/
    // JOG_START/TARE_START/PRELOAD_START/ARM/START) is about to be dispatched -- the
    // caller must hold this true for the whole duration of a move, not just the dispatch
    // instant (see the header note above). core_state alone cannot stand in for this: it
    // doesn't cover ARM's move-to-L0 (core_sm::StateMachine::arming(), which keeps
    // core_state at IDLE), so the caller is the one place with enough information to set
    // this correctly every tick.
    bool motion_active = false;
    bool lim_near_active = false;     // debounced NC limit switch reading (true = tripped)
    bool lim_far_active = false;
};

// Evaluates this tick's two implemented rules. Returns the first one that fires (limit
// switch checked before inhibit mask; both map to the same SAFETY_TRIP action, so the
// order only matters for which cause gets logged) or NONE if neither applies.
FaultReason evaluate(const Inputs& in);

}  // namespace core_safety
