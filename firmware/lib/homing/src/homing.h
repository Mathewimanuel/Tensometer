// Homing sequencer (skill section 3.4: "Home to LIM_FAR: fast approach, back off, slow
// re-approach, set zero"). Pure logic, no Arduino dependency: sequences the phases by
// watching the debounced LIM_FAR switch state and the stepper's absolute position, and
// tells the caller which way to drive and at what rate. It issues no hardware commands
// itself (same "callers react, this only decides" pattern as lib/state_machine).
//
// A fifth phase, RELEASE, was added 2026-10-27 (not in the skill's literal four-phase
// wording) after review: SLOW_REAPPROACH used to go straight to DONE the instant LIM_FAR
// tripped, meaning homing habitually finished sitting on an actively-tripped switch. The
// core-safety loop's LIM_FAR carve-out (lib/core_safety) only applies while core state is
// HOMING -- the instant HOMING_DONE moves the state machine to IDLE, that carve-out lifts,
// and an immediately-still-tripped LIM_FAR would self-fault on the very next core-safety
// tick, with no implemented recovery path. RELEASE backs off (TOWARD_LIM_NEAR, at
// slow_hz, same as SLOW_REAPPROACH) until LIM_FAR reads inactive again, *then* reports
// DONE -- so "zero" is now defined at the switch's release point, not its trip point, a
// small but real change to where position 0 physically ends up. See docs/decisions.md
// 2026-10-27.
//
// Deliberately out of scope here, left to the caller (core-safety loop, skill 3.4/4):
//   - The |force| < 2 N precondition and the |force| > 5 N mid-homing abort. This class
//     knows nothing about force; the caller is expected to keep monitoring it independently
//     and call apply(SAFETY_TRIP, ...) on the core state machine (not anything here) if it
//     trips -- consistent with how "motion requested while inhibited" was resolved for
//     lib/state_machine (decisions.md 2026-10-26): continuous safety checks live in the
//     core-safety loop, not inside whichever motion sub-sequence happens to be running.
//   - "Set zero": once done() is true, the caller must reset the stepper's position
//     counter to the calibrated zero itself (no such reset exists in stepper.h yet --
//     pending, part of wiring this in).
//   - Converting Dir to whatever the real stepper driver's direction enum is (stepper::Dir
//     today), and picking real back_off_steps/fast_hz/slow_hz values -- homing/jog speeds
//     are still OPEN (decisions.md 2026-10-15), so this takes them as caller-supplied
//     Params rather than inventing numbers.
#pragma once

#include <stdint.h>

#include "motion_profile.h"  // motion::UNBOUNDED_STEPS

namespace homing {

// Named after the limit switches, matching stepper::Dir's convention and for the same
// reason: this file does not know which physical direction either value drives.
enum class Dir : uint8_t { TOWARD_LIM_FAR, TOWARD_LIM_NEAR };

enum class Phase : uint8_t { IDLE, FAST_APPROACH, BACK_OFF, SLOW_REAPPROACH, RELEASE, DONE };

struct Params {
    uint32_t back_off_steps;  // distance to withdraw from LIM_FAR before re-approaching
    uint32_t fast_hz;         // cruise rate for FAST_APPROACH and BACK_OFF
    uint32_t slow_hz;         // cruise rate for SLOW_REAPPROACH
    uint32_t accel_hz_per_s;  // ramp rate handed to motion::Planner alongside cruise_hz();
                              // homing's own value, not necessarily the test-move one.
};

class Sequencer {
public:
    Phase phase() const { return phase_; }
    bool done() const { return phase_ == Phase::DONE; }

    // Direction to drive during the current phase. Meaningless during IDLE/DONE -- the
    // caller must not be commanding motion then, so the value returned in those phases is
    // an arbitrary, safe default, never read for anything.
    Dir direction() const;

    // Cruise rate for the current phase (fast_hz for FAST_APPROACH/BACK_OFF, slow_hz for
    // SLOW_REAPPROACH/RELEASE, 0 during IDLE/DONE).
    uint32_t cruise_hz() const;

    // Ramp rate for the current phase, for the caller's motion::Planner (params_.
    // accel_hz_per_s while actually homing, 0 during IDLE/DONE -- meaningless then, same
    // as cruise_hz()).
    uint32_t accel_hz_per_s() const;

    // Steps left before the current phase's own sub-target, meant as the `remaining_steps`
    // argument to the caller's motion::Planner::tick(). motion::UNBOUNDED_STEPS for
    // FAST_APPROACH/SLOW_REAPPROACH/RELEASE (all three are limit-switch terminated, not
    // distance-terminated); the back-off distance still to go for BACK_OFF; 0 for
    // IDLE/DONE. `position_steps` is the same absolute position tick() is called with.
    uint32_t steps_remaining(int32_t position_steps) const;

    // Begins a homing cycle from any phase, including re-starting one already DONE.
    void start(const Params& params);

    // Abandons an in-progress cycle (e.g. the caller's force guard tripped, skill 3.4:
    // "Abort with FAULT"), resetting to IDLE. Only a fresh start() can resume homing after
    // this -- a stray tick() call after abort() (without an intervening start()) stays IDLE
    // and does nothing, same as before any cycle ever began. Safe to call from any phase,
    // including IDLE/DONE (a no-op then).
    void abort();

    // Advance one tick. `position_steps` is the stepper's current absolute position
    // (read-only truth from outside this class, e.g. stepper::position_steps());
    // `lim_far_active` is the debounced LIM_FAR switch state. Returns phase() after this
    // tick. A tick() before any start() is a no-op that stays IDLE.
    Phase tick(int32_t position_steps, bool lim_far_active);

private:
    Phase phase_ = Phase::IDLE;
    Params params_{};
    int32_t phase_start_position_ = 0;
};

}  // namespace homing
