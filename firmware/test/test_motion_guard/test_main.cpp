#include <unity.h>

#include "motion_guard.h"

void setUp() {}
void tearDown() {}

using motion_guard::Dir;
using motion_guard::DirectionTracker;

static void test_fresh_tracker_assumes_a_stop_is_needed_while_moving() {
    DirectionTracker t;
    TEST_ASSERT_TRUE(t.needs_stop_before(Dir::A, true));
    TEST_ASSERT_TRUE(t.needs_stop_before(Dir::B, true));
}

static void test_fresh_tracker_needs_no_stop_while_not_moving() {
    DirectionTracker t;
    TEST_ASSERT_FALSE(t.needs_stop_before(Dir::A, false));
    TEST_ASSERT_FALSE(t.needs_stop_before(Dir::B, false));
}

static void test_same_direction_while_moving_needs_no_stop() {
    DirectionTracker t;
    t.record_commanded(Dir::A);
    TEST_ASSERT_FALSE(t.needs_stop_before(Dir::A, true));
}

static void test_direction_change_while_moving_needs_a_stop() {
    DirectionTracker t;
    t.record_commanded(Dir::A);
    TEST_ASSERT_TRUE(t.needs_stop_before(Dir::B, true));
}

static void test_direction_change_while_not_moving_needs_no_stop() {
    DirectionTracker t;
    t.record_commanded(Dir::A);
    // Nothing is actually in motion to interrupt, regardless of the direction mismatch.
    TEST_ASSERT_FALSE(t.needs_stop_before(Dir::B, false));
}

static void test_reset_forgets_the_tracked_direction() {
    DirectionTracker t;
    t.record_commanded(Dir::A);
    TEST_ASSERT_FALSE(t.needs_stop_before(Dir::A, true));

    t.reset();
    // Same direction as before, but the tracker no longer knows that -- conservative
    // default is to require a stop until record_commanded() runs again.
    TEST_ASSERT_TRUE(t.needs_stop_before(Dir::A, true));
}

// Mirrors the exact sequence that produced the CRITICAL bug, extended to all five homing
// phases: FAST_APPROACH (Dir::A) -> BACK_OFF (Dir::B) -> SLOW_REAPPROACH (Dir::A) ->
// RELEASE (Dir::B) -> DONE, each transition commanded without the caller stopping in
// between unless this class says to. RELEASE was added after this test was first written
// (docs/decisions.md 2026-10-27) specifically to fix homing finishing on an actively-
// tripped switch; it is itself another direction reversal (same hazard class), so it
// belongs in the same regression guard as the original BACK_OFF/SLOW_REAPPROACH ones --
// this is the only thing in CI that would catch a future motion_controller.cpp change that
// special-cased RELEASE to skip the stop-before-reversal check.
static void test_full_homing_direction_sequence() {
    DirectionTracker t;

    // start_homing(): reset() called, stepper not yet moving.
    t.reset();
    TEST_ASSERT_FALSE(t.needs_stop_before(Dir::A, false));  // FAST_APPROACH, first tick
    t.record_commanded(Dir::A);

    // Still FAST_APPROACH, now actually moving: same direction, no stop.
    TEST_ASSERT_FALSE(t.needs_stop_before(Dir::A, true));
    t.record_commanded(Dir::A);

    // Limit trips -> BACK_OFF (Dir::B), stepper still physically moving the old way: this
    // is the exact transition the original bug silently ignored.
    TEST_ASSERT_TRUE(t.needs_stop_before(Dir::B, true));
    // Caller stops, planner resets, then issues the new move and records it.
    t.record_commanded(Dir::B);

    // BACK_OFF continues: same direction, no stop.
    TEST_ASSERT_FALSE(t.needs_stop_before(Dir::B, true));
    t.record_commanded(Dir::B);

    // BACK_OFF -> SLOW_REAPPROACH (Dir::A): another reversal, same handling required.
    TEST_ASSERT_TRUE(t.needs_stop_before(Dir::A, true));
    t.record_commanded(Dir::A);

    // SLOW_REAPPROACH continues: same direction, no stop.
    TEST_ASSERT_FALSE(t.needs_stop_before(Dir::A, true));
    t.record_commanded(Dir::A);

    // LIM_FAR trips again -> RELEASE (Dir::B): yet another reversal, while still
    // physically moving the SLOW_REAPPROACH way -- must be caught exactly like the others.
    TEST_ASSERT_TRUE(t.needs_stop_before(Dir::B, true));
    t.record_commanded(Dir::B);

    // RELEASE continues (backing off) until the switch clears: same direction, no stop.
    TEST_ASSERT_FALSE(t.needs_stop_before(Dir::B, true));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fresh_tracker_assumes_a_stop_is_needed_while_moving);
    RUN_TEST(test_fresh_tracker_needs_no_stop_while_not_moving);
    RUN_TEST(test_same_direction_while_moving_needs_no_stop);
    RUN_TEST(test_direction_change_while_moving_needs_a_stop);
    RUN_TEST(test_direction_change_while_not_moving_needs_no_stop);
    RUN_TEST(test_reset_forgets_the_tracked_direction);
    RUN_TEST(test_full_homing_direction_sequence);
    return UNITY_END();
}
