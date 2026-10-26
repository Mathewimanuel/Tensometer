// Core state machine (skill section 5). Pure logic, no Arduino dependency: a function of
// (current state, event, guards) -> next state. It does not own homed/position_valid/
// config_valid/etc. itself -- those are safety-module and core-level facts (section 4,
// owned by whatever eventually reads force/limits/HX711, not yet built: #10, #11) that
// the caller tracks and passes in fresh for every apply() call.
//
// It also does not issue imperative "start this motion" commands. Instead, callers react
// to state changes the same way modules already do (module_api.h's on_state_change hook):
// see a transition into HOMING, start the homing motion; see arming(), start the move to
// L0; see a transition into FAULT, cut motion immediately. This matches the existing
// module architecture instead of inventing a second, parallel command protocol.
//
// One remaining interpretive call, not fully pinned by the skill text -- flagged here and
// in decisions.md:
//   ARM's "moved to L0" step has no dedicated visible state in the 12-state list. Modeled
//   here as an internal arming_ flag: ARM (guards pass) keeps state() at IDLE and sets
//   arming() true; the caller starts the move-to-L0 and signals completion with
//   Event::L0_REACHED, which is only accepted while arming() is true and advances
//   IDLE -> READY. While arming_ is true, every other event is rejected except L0_REACHED
//   and SAFETY_TRIP -- otherwise a stray/late L0_REACHED from a superseded move could land
//   the machine in READY from wherever a later JOG/TARE/etc. left the carriage, breaking
//   the "READY implies at L0" contract. There is currently no way to cancel a pending ARM
//   other than SAFETY_TRIP; confirm with Mathew whether one is needed.
//
// Resolved by Mathew (2026-10-26, see decisions.md):
//   - "RUN / CYCLE | target reached / cycles done | HOLD then READY" is a real HOLD, not a
//     direct jump to READY: Event::SEGMENT_DONE moves RUN/CYCLE -> HOLD (lock: PC) the same
//     way PAUSE does, but sets the RESUME target to READY instead of RUN/CYCLE, so a
//     post-test Bode/routing sweep has the HOLD window to run in before the machine resets.
//   - BREAK_HOLD gets its own exit: Event::RESET also accepts from BREAK_HOLD (not only
//     FAULT), gated by the same Guards::cause_cleared guard, moving straight to IDLE.
//   - "Motion requested while an inhibit bit is set" (skill 4, non-negotiable) is the
//     core-safety loop's job (main loop step 1, skill 3.3), not this state machine's: that
//     loop runs every pass and calls apply(SAFETY_TRIP, ...) itself as soon as it sees an
//     inhibit bit set while motion is active or being requested, for every motion-starting
//     event. This covers motion asserted mid-run (e.g. a module inhibits while RUN is
//     already underway), which a check wired only into apply() would miss. RESUME stays the
//     one exception: it's a soft NACK via Guards::module_inhibit_present (stay in HOLD, try
//     later), not a FAULT, matching the skill's existing RESUME wording.
#pragma once

#include <stdint.h>

#include "module_api.h"  // mod::CoreState
#include "protocol.h"     // proto::LockOwner

namespace core_sm {

enum class Event : uint8_t {
    SELF_TEST_PASS,  // BOOT -> IDLE
    HOME,            // IDLE -> HOMING. Caller checks the |force| < 2 N guard (skill 3.4)
                      // before sending this -- the state machine knows nothing about force.
    HOMING_DONE,     // HOMING -> IDLE (fast approach, back-off, slow re-approach, zero set)
    JOG_START,       // IDLE/READY -> JOG
    JOG_STOP,        // JOG -> the state it was entered from
    TARE_START,      // IDLE/READY -> TARE
    TARE_DONE,       // TARE -> the state it was entered from
    PRELOAD_START,   // IDLE/READY -> PRELOAD
    PRELOAD_DONE,    // PRELOAD -> the state it was entered from (threshold force reached)
    ARM,             // IDLE -> arming (see arming()); caller starts the move to L0
    L0_REACHED,      // arming -> READY. Rejected unless arming() is true.
    START,           // READY -> RUN or CYCLE, per Guards::cycle_mode
    PAUSE,           // RUN/CYCLE -> HOLD (lock: PC). Covers both a scheduled hold point
                      // and an explicit PC pause command -- the skill treats them as one
                      // transition regardless of which triggered it.
    MODULE_LOCK_REQUEST,  // HOLD -> HOLD (lock: MODULE)
    MODULE_LOCK_RELEASE,  // HOLD -> HOLD (lock: PC)
    RESUME,          // HOLD -> wherever this HOLD's RESUME target is (RUN/CYCLE if entered
                      // via PAUSE, READY if entered via SEGMENT_DONE). Rejected unless
                      // lock_owner() == PC and !Guards::module_inhibit_present.
    SUBSTRATE_BREAK, // RUN/CYCLE -> BREAK_HOLD
    SEGMENT_DONE,    // RUN/CYCLE -> HOLD (lock: PC), RESUME target READY (target reached /
                      // cycles done -- a real HOLD so a post-test Bode sweep has somewhere
                      // to run; see "Resolved by Mathew" above).
    SAFETY_TRIP,     // any state -> FAULT. Always accepted, including from FAULT itself.
    RESET,           // FAULT or BREAK_HOLD -> IDLE. Rejected unless Guards::cause_cleared.
};

struct Guards {
    bool homed = false;
    bool position_valid = false;
    bool config_valid = false;          // test config sections valid (checked at ARM)
    bool cycle_mode = false;             // START: false -> RUN, true -> CYCLE
    bool module_inhibit_present = false; // blocks RESUME (skill 1.3: any inhibit bit set)
    bool cause_cleared = false;          // RESET: the FAULT cause is actually gone
};

class StateMachine {
public:
    mod::CoreState state() const { return state_; }
    proto::LockOwner lock_owner() const { return lock_owner_; }
    // True between a guards-passing ARM and the matching L0_REACHED: state() still
    // reports IDLE (no dedicated wire state for "arming"), but motion toward L0 should
    // already be underway.
    bool arming() const { return arming_; }

    // Applies one event. Returns true if accepted (state may or may not have changed --
    // e.g. a lock request while already locked by that owner is accepted as a no-op);
    // false if rejected, meaning the caller should NACK (proto::Err::BAD_STATE is the
    // usual fit). Guards are read only for the events that need them; irrelevant fields
    // are ignored.
    bool apply(Event event, const Guards& guards);

private:
    mod::CoreState state_ = mod::CoreState::BOOT;
    proto::LockOwner lock_owner_ = proto::LockOwner::NONE;
    bool arming_ = false;
    // Where JOG/TARE/PRELOAD/HOLD return to on their "-> back" / RESUME transition.
    // Only ever IDLE, READY, RUN or CYCLE -- the only states those can be entered from.
    mod::CoreState return_state_ = mod::CoreState::IDLE;
};

}  // namespace core_sm
