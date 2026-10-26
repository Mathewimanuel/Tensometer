#include "motion_controller.h"

#include "motion_guard.h"
#include "motion_profile.h"
#include "stepper.h"

namespace motion_controller {

namespace {

homing::Sequencer homing_seq;
motion::Planner planner;
bool position_valid_ = false;
motion_guard::DirectionTracker dir_tracker;

stepper::Dir to_stepper_dir(homing::Dir d) {
    return d == homing::Dir::TOWARD_LIM_FAR ? stepper::Dir::TOWARD_LIM_FAR
                                             : stepper::Dir::TOWARD_LIM_NEAR;
}

motion_guard::Dir to_guard_dir(stepper::Dir d) {
    return d == stepper::Dir::TOWARD_LIM_FAR ? motion_guard::Dir::A : motion_guard::Dir::B;
}

bool sequencing() {
    switch (homing_seq.phase()) {
        case homing::Phase::FAST_APPROACH:
        case homing::Phase::BACK_OFF:
        case homing::Phase::SLOW_REAPPROACH:
            return true;
        case homing::Phase::IDLE:
        case homing::Phase::DONE:
            return false;
    }
    return false;
}

// Abandons the current attempt: stops the stepper, resets the planner's rate state (it
// would otherwise still think the motor is at speed after a real, physical stop -- stale
// state that would make the next tick's accel/decel decision wrong), and resets homing_seq
// to IDLE so only a fresh start_homing() can resume (skill 3.4: "Abort with FAULT").
void stop_and_abort() {
    stepper::stop_immediate();
    planner.reset();
    homing_seq.abort();
    dir_tracker.reset();
}

}  // namespace

bool position_valid() { return position_valid_; }

void clear_position_valid() { position_valid_ = false; }

bool homing_in_progress() { return sequencing(); }

void start_homing(const homing::Params& params) {
    homing_seq.start(params);
    planner.reset();
    dir_tracker.reset();
}

TickResult tick(uint32_t dt_us, bool lim_far_active, bool force_within_homing_guard) {
    if (!sequencing()) {
        return TickResult::NOT_HOMING;
    }

    if (!force_within_homing_guard) {
        stop_and_abort();
        return TickResult::ABORTED;
    }

    const int32_t position = stepper::position_steps();
    const homing::Phase phase = homing_seq.tick(position, lim_far_active);

    if (phase == homing::Phase::DONE) {
        stepper::stop_immediate();
        stepper::zero_position();
        planner.reset();
        dir_tracker.reset();
        position_valid_ = true;
        return TickResult::DONE;
    }

    const stepper::Dir requested_dir = to_stepper_dir(homing_seq.direction());

    // A homing phase change can reverse direction while the stepper is still physically
    // moving the old way -- most notably FAST_APPROACH -> BACK_OFF, which happens on the
    // very same tick the limit switch trips, with the stepper still cruising toward it.
    // stepper::set_motion() refuses a direction change mid-move by design (DIR_SETUP_US
    // must always be honoured before the first step the other way), so the reversal must
    // be stopped first; otherwise set_motion() silently rejects the new direction and the
    // stepper keeps going the OLD way forever, while homing_seq's (direction-blind,
    // magnitude-only) distance tracking eventually declares BACK_OFF "complete" purely
    // from that wrong-direction travel -- a real bug found by firmware-reviewer: the
    // carriage would drive further into the limit switch instead of backing off, then
    // report a false, zeroed-at-the-wrong-place "successful" home. The decision itself
    // (motion_guard::DirectionTracker) is pulled out and natively tested so a future
    // regression here is caught by CI, not just by inspection.
    if (dir_tracker.needs_stop_before(to_guard_dir(requested_dir), stepper::moving())) {
        stepper::stop_immediate();
        planner.reset();
    }
    dir_tracker.record_commanded(to_guard_dir(requested_dir));

    motion::Params mp{};
    mp.cruise_hz = homing_seq.cruise_hz();
    mp.accel_hz_per_s = homing_seq.accel_hz_per_s();
    const uint32_t remaining = homing_seq.steps_remaining(position);
    const uint32_t rate_hz = planner.tick(dt_us, remaining, mp);

    if (!stepper::set_motion(requested_dir, rate_hz, remaining)) {
        // Should not happen given the stop-first logic above -- but never claim progress
        // happened if the driver still refuses (e.g. it was disabled out from under us).
        stop_and_abort();
        return TickResult::ABORTED;
    }
    return TickResult::IN_PROGRESS;
}

}  // namespace motion_controller
