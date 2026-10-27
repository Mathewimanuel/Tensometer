// Core-safety evaluation (skill section 3.3 step 1, section 4; issue #11). Pure logic, no
// Arduino dependency: given this tick's state/sensor snapshot, decides whether a
// SAFETY_TRIP is due and why. The caller (main loop, not built yet) is the one place that
// actually calls core_sm::StateMachine::apply(Event::SAFETY_TRIP, ...) -- this file only
// decides; it never touches the state machine or hardware itself (skill 3.1: one owner per
// piece of state).
//
// Still out of scope (docs/decisions.md): force cutoff, HX711-stale, and slip-suspect --
// all three need real force data, which has no driver yet (#10, blocked). Watchdog-reset-
// at-boot is a one-shot boot check, not a per-tick rule, and belongs in whatever runs
// setup(), not here. Do not treat evaluate() returning NONE as "safe to move"; it only
// means none of the rules below fired.
//
// Rules implemented, in the order evaluate() checks them (all map to the same SAFETY_TRIP
// action; the order only decides which cause gets logged when more than one is true at
// once):
//   1. E-stop sense -> FAULT. The caller must also disable ENA and clear position_valid
//      on this specific reason (skill 4 rule 2) -- evaluate() only reports the cause.
//   2. Limit-switch-open -> FAULT, EXCEPT LIM_FAR while homing is active. Homing
//      deliberately drives the carriage into LIM_FAR as part of normal operation
//      (lib/homing's FAST_APPROACH/SLOW_REAPPROACH phases) and lib/homing already watches
//      it every tick. LIM_NEAR is NEVER suspended, even during homing (narrowed 2026-10-27
//      after review: lib/homing's BACK_OFF phase drives toward LIM_NEAR using only a step
//      count to detect overtravel, with no switch watching it at all). See
//      docs/decisions.md 2026-10-27.
//   3. GUARD_SENSE open while a run-to-break test is armed (confirmed by Mathew,
//      2026-10-27: armed here means "a test session is active, READY and beyond" -- NOT
//      core_sm::StateMachine::arming()'s narrower ARM-to-L0_REACHED flag; the two are
//      unrelated despite the shared word) -> FAULT, only if cfg::GUARD_INTERLOCK_FITTED is
//      true (currently false, so this can never fire today). Unlike rule 4/5/6 below, this AND is applied HERE, inside
//      evaluate(), not folded into a single caller-supplied bool: GUARD_INTERLOCK_FITTED
//      is a pure constexpr already available to this file (config.h has no Arduino
//      dependency), so there is no reason to push that specific AND out to an untested
//      caller and every reason to keep it next to the only tests that exercise it.
//      Checked before rule 4 (module fault) on the reasoning that a bypassed physical
//      guard interlock is more safety-relevant to have as the logged cause than a generic
//      module fault, if the two ever fire on the same tick once the interlock exists.
//   4. Any module's safety_check() faulting -> FAULT. Caller ORs every enabled module's
//      nonzero safety_check() result into one bool; the specific fault code is a logging
//      concern elsewhere, not something evaluate() needs.
//   5. Soft travel limit OR strain limit reached -> FAULT, as two separate reasons/inputs,
//      not one. The skill lists them together ("soft travel limit or strain limit
//      reached") but they are different quantities -- travel is an absolute stroke bound,
//      strain is test-config-specific ((position - L0)/L0 against a per-test bound) --
//      and skill rule 9 requires every fault to log a specific cause, so collapsing them
//      into one FaultReason would have thrown away which one actually tripped. Caller
//      precomputes both (position-vs-bounds arithmetic doesn't belong here, same reasoning
//      as the direction-tracking logic that was pulled into lib/motion_guard rather than
//      guessed inline).
//   6. PC heartbeat stale -> FAULT, only while motion_active (skill 4 rule 2: "while
//      moving"). Caller reports raw staleness; evaluate() applies the motion_active gate
//      itself, consistent with how inhibit-mask enforcement (rule 7) already works.
//   7. Motion active (or about to become active) while any inhibit-mask bit is set ->
//      FAULT. NOT suppressed during homing -- rule 2's carve-out is specific to the
//      limit-switch rule. Caller contract matters here (see Inputs::motion_active below):
//      this must stay true for the ENTIRE duration motion is in progress, not just the
//      instant a command is dispatched, so a module asserting an inhibit bit mid-RUN still
//      trips -- the exact scenario decisions.md 2026-10-26 ("Core state machine... item 4")
//      calls out as the reason this check lives in the core-safety loop instead of
//      lib/state_machine::apply(). Rule 6 is deliberately checked before this one (Mathew,
//      2026-10-27): if a stale heartbeat and an inhibit-mid-run both land on the same
//      tick, the logged cause is HEARTBEAT_LOST -- a stale heartbeat is its own serious,
//      independent concern (the PC may be gone entirely) and is kept as the reported
//      cause regardless of what else is also true that tick.
#pragma once

#include <stdint.h>

#include "config.h"       // cfg::GUARD_INTERLOCK_FITTED
#include "module_api.h"  // mod::CoreState

namespace core_safety {

enum class FaultReason : uint8_t {
    NONE,
    ESTOP,
    LIMIT_SWITCH_OPEN,
    GUARD_SENSE_OPEN,
    MODULE_FAULT,
    SOFT_TRAVEL_LIMIT,
    STRAIN_LIMIT,
    HEARTBEAT_LOST,
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
    bool estop_active = false;        // debounced ESTOP_SENSE reading (true = e-stop pressed)
    bool guard_sense_active = false;  // debounced GUARD_SENSE reading (true = guard open);
                                        // raw, NOT pre-ANDed with GUARD_INTERLOCK_FITTED
    bool run_to_break_armed = false;  // true while a run-to-break test session is active
                                        // (READY and beyond) -- see rule 3's note on "armed"
    bool module_fault_present = false;  // true if any enabled module's safety_check() != 0
    bool soft_travel_limit_exceeded = false;  // caller already did the position-vs-bounds math
    bool strain_limit_exceeded = false;       // caller already did the strain-vs-bounds math
    bool heartbeat_stale = false;     // time since last PC heartbeat exceeds the timeout;
                                        // only trips while motion_active (see rule 6 above)
};

// Evaluates this tick against every rule listed above, in that order, returning the first
// one that fires or NONE if none apply.
FaultReason evaluate(const Inputs& in);

}  // namespace core_safety
