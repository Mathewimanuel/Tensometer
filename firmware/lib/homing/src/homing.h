// Homing sequencer (skill section 3.4: "Home to LIM_FAR: fast approach, back off, slow
// re-approach, set zero"). Pure logic, no Arduino dependency: sequences the four phases by
// watching the debounced LIM_FAR switch state and the stepper's absolute position, and
// tells the caller which way to drive and at what rate. It issues no hardware commands
// itself (same "callers react, this only decides" pattern as lib/state_machine).
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

namespace homing {

// Named after the limit switches, matching stepper::Dir's convention and for the same
// reason: this file does not know which physical direction either value drives.
enum class Dir : uint8_t { TOWARD_LIM_FAR, TOWARD_LIM_NEAR };

enum class Phase : uint8_t { IDLE, FAST_APPROACH, BACK_OFF, SLOW_REAPPROACH, DONE };

struct Params {
    uint32_t back_off_steps;  // distance to withdraw from LIM_FAR before re-approaching
    uint32_t fast_hz;         // cruise rate for FAST_APPROACH and BACK_OFF
    uint32_t slow_hz;         // cruise rate for SLOW_REAPPROACH
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
    // SLOW_REAPPROACH, 0 during IDLE/DONE).
    uint32_t cruise_hz() const;

    // Begins a homing cycle from any phase, including re-starting one already DONE.
    void start(const Params& params);

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
