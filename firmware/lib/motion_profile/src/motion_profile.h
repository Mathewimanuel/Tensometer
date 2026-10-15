// Trapezoidal step-rate planner (skill section 3.4, decisions.md 2026-10-15).
// Advanced one tick at a time (nominally 1 kHz from the main loop); produces the step
// rate the step ISR should run at. Pure logic, no Arduino dependency.
//
// One instance serves every motion source (test move, jog, homing, preload): the
// planner itself knows nothing about force, buttons, or absolute position in mm, only
// step rates and a step count counted down to the current target. The caller decides
// what "remaining_steps" means for its move:
//   - Position-target move: the real, shrinking distance to the target.
//   - Open-ended move (jog, preload creep): UNBOUNDED_STEPS until the caller decides to
//     stop, then steps_to_stop() at the current rate, so the planner decelerates
//     cleanly from whatever speed it is at. This turns an open-ended move into an
//     ordinary position-target decel with no special case in the planner.
// A short position-target move (closer than the accel distance) naturally produces a
// triangular profile from the same per-tick logic.
#pragma once

#include <stdint.h>

namespace motion {

// Sentinel remaining-steps value for an open-ended move: large enough that it never
// triggers a forced deceleration at any rate/accel this machine can reach.
constexpr uint32_t UNBOUNDED_STEPS = UINT32_MAX;

struct Params {
    uint32_t cruise_hz;       // target/cruise step rate. Should be > 0.
    uint32_t accel_hz_per_s;  // ramp rate, steps/s^2, same for accel and decel. Should be > 0:
                              // a caller should never hand this a real move with accel == 0,
                              // but 0 is handled safely (see steps_to_stop), not UB, since the
                              // real acceleration constant is still open (skill 15) and this is
                              // a plausible uninitialized/misconfigured value to arrive with.
};

// Steps needed to decelerate from rate_hz to a stop at accel_hz_per_s (d = v^2 / 2a),
// rounded up: conservative, so a caller that starts decelerating at this distance never
// finds out one tick later that it needed to start sooner. accel_hz_per_s == 0 (no
// deceleration capability) returns UINT32_MAX rather than dividing by zero; combined with
// tick()'s own zero-accel handling, this freezes the planner at its current rate (0 if not
// already moving) instead of crashing or free-running.
uint32_t steps_to_stop(uint32_t rate_hz, uint32_t accel_hz_per_s);

class Planner {
public:
    // Step rate in Hz commanded as of the last tick(). 0 before the first tick() and
    // once a move has finished (remaining_steps reached 0).
    uint32_t rate_hz() const { return rate_hz_; }
    bool moving() const { return rate_hz_ > 0; }

    // Advance the profile by one tick. `dt_us` is the tick period (nominally 1000 for
    // the 1 kHz loop). `remaining_steps` is however many steps are left before the
    // move's current target (UNBOUNDED_STEPS for an open-ended move). Decides each
    // tick, from scratch, whether to accelerate, cruise or decelerate, so it tolerates
    // `params` or `remaining_steps` changing mid-move. Returns the new rate_hz().
    uint32_t tick(uint32_t dt_us, uint32_t remaining_steps, const Params& params);

    // Force the rate to 0 immediately: an IMMEDIATE stop (skill 3.4), not a controlled
    // decel. The caller (core safety path) is responsible for cutting the step ISR and
    // ENA separately; this only resets the planner's own state so the next tick() call
    // starts a fresh move from rest.
    void reset();

private:
    uint32_t rate_hz_ = 0;
    uint64_t accel_remainder_us_ = 0;  // sub-Hz carry, denominator 1e6, avoids rounding
                                        // the ramp to a stall at low accel/dt products.
};

}  // namespace motion
