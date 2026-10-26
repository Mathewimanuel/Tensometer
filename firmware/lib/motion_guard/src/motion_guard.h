// Direction-reversal guard for single-direction-at-a-time motion sources. Pure logic, no
// Arduino dependency.
//
// Exists because this exact gap was a CRITICAL bug (decisions.md 2026-10-26, issue #9):
// motion_controller.cpp requested a direction change (homing's FAST_APPROACH ->
// BACK_OFF) without stopping the stepper first. stepper::set_motion() refuses a direction
// change while still moving, by design, to always honour DIR_SETUP_US -- but the caller
// never checked that rejection, so the stepper kept driving the OLD way forever while the
// rest of the system believed the new move was underway. The fix (stop first, then
// restart) is a two-line decision, but it is exactly the two lines that were missing, so
// it is pulled out here and natively tested rather than left inline in an untested src/
// driver file -- a future regression (e.g. reordering the stop before the direction is
// recorded) is now caught by CI instead of by inspection alone.
#pragma once

namespace motion_guard {

// Generic two-way direction, decoupled from stepper::Dir / homing::Dir (the caller
// converts), same pattern those two already use to stay Arduino-free.
enum class Dir { A, B };

class DirectionTracker {
public:
    // True iff `currently_moving` and the direction last recorded via record_commanded()
    // differs from `requested` -- including when nothing has been recorded yet, which is
    // treated the same as "differs": the conservative default when the tracked state
    // doesn't actually know what direction is in progress. False whenever
    // `currently_moving` is false: nothing is in motion to interrupt, so no stop is
    // needed regardless of what was last recorded.
    bool needs_stop_before(Dir requested, bool currently_moving) const {
        return currently_moving && (!have_last_ || requested != last_);
    }

    // Records `dir` as the direction actually commanded this tick. Call once per tick a
    // move is (re)issued, after acting on needs_stop_before()'s answer for that same
    // `dir`.
    void record_commanded(Dir dir) {
        last_ = dir;
        have_last_ = true;
    }

    // Forgets the tracked direction. Call whenever the caller knows motion has fully
    // stopped and what comes next should not be compared against stale history -- e.g.
    // starting a brand new cycle, or right after issuing a stop on completion/abort. The
    // next needs_stop_before() then answers conservatively (true, if still moving) rather
    // than trusting direction history that may no longer apply.
    void reset() { have_last_ = false; }

private:
    Dir last_ = Dir::A;
    bool have_last_ = false;
};

}  // namespace motion_guard
