#include "motion_profile.h"

namespace motion {

namespace {

// Move `current` toward `target` by at most `delta`, without overshoot, in either
// direction. Used both for ramping up toward cruise and ramping down (to cruise, if it
// dropped mid-move, or to 0 when decelerating to a stop).
uint32_t ramp_toward(uint32_t current, uint32_t target, uint32_t delta) {
    if (current < target) {
        const uint32_t next = current + delta;
        return (next > target) ? target : next;
    }
    if (current > target) {
        return (current - target > delta) ? (current - delta) : target;
    }
    return current;
}

}  // namespace

uint32_t steps_to_stop(uint32_t rate_hz, uint32_t accel_hz_per_s) {
    if (rate_hz == 0) {
        return 0;
    }
    const uint64_t v2 = static_cast<uint64_t>(rate_hz) * rate_hz;
    const uint64_t denom = 2ull * accel_hz_per_s;
    const uint64_t d = (v2 + denom - 1) / denom;  // round up: never brake late
    return (d > UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(d);
}

uint32_t Planner::tick(uint32_t dt_us, uint32_t remaining_steps, const Params& params) {
    if (remaining_steps == 0) {
        rate_hz_ = 0;
        accel_remainder_us_ = 0;
        return rate_hz_;
    }

    // Decide fresh every tick (not phase-tracked), so a change to params or
    // remaining_steps mid-move is picked up immediately and a short move naturally
    // falls out as a triangular profile with no special case.
    const uint32_t stop_dist = steps_to_stop(rate_hz_, params.accel_hz_per_s);
    const bool must_decelerate = stop_dist >= remaining_steps;

    // Rate delta for this tick, carrying the sub-Hz remainder (denominator 1e6 us) so
    // a small accel/dt product still ramps instead of rounding to a permanent stall.
    const uint64_t num = static_cast<uint64_t>(params.accel_hz_per_s) * dt_us + accel_remainder_us_;
    const uint32_t delta_hz = static_cast<uint32_t>(num / 1000000ull);
    accel_remainder_us_ = num % 1000000ull;

    rate_hz_ = must_decelerate ? ramp_toward(rate_hz_, 0, delta_hz)
                                : ramp_toward(rate_hz_, params.cruise_hz, delta_hz);
    return rate_hz_;
}

void Planner::reset() {
    rate_hz_ = 0;
    accel_remainder_us_ = 0;
}

}  // namespace motion
