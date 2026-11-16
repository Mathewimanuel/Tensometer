// Fatigue-cycle sequencer (skill section 5 CYCLE state; protocol.h 0.5, slice 3 of 3 --
// docs/decisions.md). Pure logic, no Arduino dependency: mirrors lib/homing's shape
// (phase(), done(), start(), abort(), tick()) but reciprocates between two absolute step
// targets instead of homing to a limit switch. It issues no hardware commands itself,
// same "callers react, this only decides" pattern as lib/homing and lib/state_machine.
//
// One cycle = extend from the floor (target_min_steps) to the peak (target_max_steps),
// then retract back to the floor (Mathew, 2026-11-13: "a stretch and back to start
// position"). The very first move, from wherever CYCLE was entered (L0/READY) out to the
// floor, is its own phase (TO_FLOOR) and does not count as part of any cycle.
//
// Direction convention, same as the rest of this firmware's strain maths (ArmBody.l0_um,
// strain_permille = (position - L0) / L0): position_steps increases with extension away
// from L0. target_max_steps must be > target_min_steps, and target_min_steps must be >=
// the position CYCLE was entered at -- both are validated by the caller (whatever decodes
// ARM's config, not yet written) before calling start(), not by this class.
//
// Deliberately out of scope here, left to the caller (core-safety loop / motion
// controller, not yet built), same division of responsibility as lib/homing:
//   - The caller must call abort() on FAULT entry (e.g. CMD_STOP), same as homing's own
//     abort() contract -- this class does not watch for faults itself.
//   - Converting target_steps()/cruise_hz()/accel_hz_per_s() into actual motor motion via
//     motion::Planner and the stepper driver.
//   - Reporting cycles_completed() to the PC (TelemetryBody.cycle_index, 0.5).
#pragma once

#include <stdint.h>

namespace cycle {

enum class Phase : uint8_t { IDLE, TO_FLOOR, TO_MAX, TO_MIN, DONE };

struct Params {
    int32_t target_min_steps;  // floor bound each cycle returns to
    int32_t target_max_steps;  // peak bound each cycle; must be > target_min_steps
    uint32_t cruise_hz;        // cruise rate for every phase
    uint32_t accel_hz_per_s;   // ramp rate handed to motion::Planner alongside cruise_hz()
    uint32_t cycle_count;       // total repetitions; must be > 0
};

class Sequencer {
public:
    Phase phase() const { return phase_; }
    bool done() const { return phase_ == Phase::DONE; }

    // Full cycles completed so far (a cycle = TO_MAX then TO_MIN, not TO_FLOOR). For
    // TelemetryBody.cycle_index (0.5): the PC's only progress signal for a test that may
    // run for hours.
    uint32_t cycles_completed() const { return cycles_completed_; }

    // Absolute step target for the current phase: target_min_steps during TO_FLOOR/TO_MIN,
    // target_max_steps during TO_MAX. Meaningless during IDLE/DONE -- the caller must not
    // be commanding motion then, same convention as homing::Sequencer::direction().
    int32_t target_steps() const;

    // Cruise rate for the current phase (params_.cruise_hz while active, 0 during IDLE/DONE).
    uint32_t cruise_hz() const;

    // Ramp rate for the current phase, for the caller's motion::Planner (params_.
    // accel_hz_per_s while active, 0 during IDLE/DONE -- meaningless then, same as
    // cruise_hz()).
    uint32_t accel_hz_per_s() const;

    // Distance to the current phase's target, meant as the `remaining_steps` argument to
    // the caller's motion::Planner::tick(). 0 during IDLE/DONE.
    uint32_t steps_remaining(int32_t position_steps) const;

    // Begins a cycle run from any phase, including one already DONE. Resets
    // cycles_completed() to 0.
    void start(const Params& params);

    // Abandons an in-progress run (e.g. the caller's CMD_STOP/FAULT path), resetting to
    // IDLE. Only a fresh start() can resume cycling after this -- a stray tick() call
    // after abort() (without an intervening start()) stays IDLE and does nothing, same as
    // before any run ever began. Safe to call from any phase, including IDLE/DONE (a
    // no-op then).
    void abort();

    // Advance one tick. `position_steps` is the stepper's current absolute position
    // (read-only truth from outside this class, e.g. stepper::position_steps()). Returns
    // phase() after this tick. A tick() before any start() is a no-op that stays IDLE.
    //
    // "Reached" is checked with >=/<= against the absolute target, not ==, so a one-step
    // overshoot from the motion planner's own deceleration rounding still advances the
    // phase instead of stalling indefinitely waiting for an exact position match (same
    // robustness reasoning as homing::Sequencer::BACK_OFF's own distance check).
    Phase tick(int32_t position_steps);

private:
    Phase phase_ = Phase::IDLE;
    Params params_{};
    uint32_t cycles_completed_ = 0;
};

}  // namespace cycle
