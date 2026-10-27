#include <unity.h>

#include "core_safety.h"

void setUp() {}
void tearDown() {}

using core_safety::evaluate;
using core_safety::FaultReason;
using core_safety::Inputs;
using mod::CoreState;

static void test_no_trip_when_nothing_is_wrong() {
    Inputs in{};
    in.core_state = CoreState::RUN;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::NONE);
}

static void test_lim_far_trips_outside_homing() {
    Inputs in{};
    in.core_state = CoreState::RUN;
    in.lim_far_active = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::LIMIT_SWITCH_OPEN);
}

static void test_lim_near_trips_outside_homing() {
    Inputs in{};
    in.core_state = CoreState::JOG;
    in.lim_near_active = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::LIMIT_SWITCH_OPEN);
}

static void test_lim_far_does_not_trip_during_homing() {
    Inputs in{};
    in.core_state = CoreState::HOMING;
    in.lim_far_active = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::NONE);
}

// LIM_NEAR is never suspended, even during homing -- narrowed after review found BACK_OFF
// drives toward it with no other software watch at all (see the header comment).
static void test_lim_near_still_trips_during_homing() {
    Inputs in{};
    in.core_state = CoreState::HOMING;
    in.lim_near_active = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::LIMIT_SWITCH_OPEN);
}

// Both switches active at once during homing: LIM_NEAR's always-on rule must still catch
// it even though LIM_FAR's homing carve-out would otherwise suppress that one alone.
static void test_lim_near_trips_during_homing_even_with_lim_far_also_active() {
    Inputs in{};
    in.core_state = CoreState::HOMING;
    in.lim_near_active = true;
    in.lim_far_active = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::LIMIT_SWITCH_OPEN);
}

static void test_leaving_homing_restores_the_limit_rule() {
    Inputs in{};
    in.core_state = CoreState::IDLE;  // HOMING_DONE already happened
    in.lim_far_active = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::LIMIT_SWITCH_OPEN);
}

static void test_motion_while_inhibited_trips_at_dispatch() {
    Inputs in{};
    in.core_state = CoreState::IDLE;
    in.motion_active = true;  // the dispatch-instant case
    in.inhibit_mask = 0x1;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::MOTION_WHILE_INHIBITED);
}

// The case decisions.md specifically requires this check to catch: a module (e.g.
// routing) asserts its inhibit bit mid-RUN, well after the move was dispatched, with no
// new motion-starting event anywhere in sight. motion_active must still be true every one
// of these ticks for the caller's contract to hold -- this is NOT the same as "a command
// was just issued".
static void test_motion_while_inhibited_trips_mid_run_not_just_at_dispatch() {
    Inputs in{};
    in.core_state = CoreState::RUN;  // already moving, well past dispatch
    in.motion_active = true;
    in.inhibit_mask = 0x2;  // a module just asserted this bit mid-run
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::MOTION_WHILE_INHIBITED);
}

static void test_inhibit_mask_alone_does_not_trip_without_motion() {
    Inputs in{};
    in.core_state = CoreState::IDLE;
    in.motion_active = false;
    in.inhibit_mask = 0x1;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::NONE);
}

static void test_motion_alone_does_not_trip_without_inhibit() {
    Inputs in{};
    in.core_state = CoreState::IDLE;
    in.motion_active = true;
    in.inhibit_mask = 0;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::NONE);
}

// The homing carve-out is specifically about the limit-switch rule, not a blanket pass on
// every rule while homing -- inhibit-mask enforcement still applies.
static void test_inhibit_mask_still_trips_during_homing() {
    Inputs in{};
    in.core_state = CoreState::HOMING;
    in.motion_active = true;
    in.inhibit_mask = 0x4;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::MOTION_WHILE_INHIBITED);
}

static void test_limit_switch_takes_priority_when_both_fire() {
    Inputs in{};
    in.core_state = CoreState::RUN;
    in.lim_far_active = true;
    in.motion_active = true;
    in.inhibit_mask = 0x1;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::LIMIT_SWITCH_OPEN);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_no_trip_when_nothing_is_wrong);
    RUN_TEST(test_lim_far_trips_outside_homing);
    RUN_TEST(test_lim_near_trips_outside_homing);
    RUN_TEST(test_lim_far_does_not_trip_during_homing);
    RUN_TEST(test_lim_near_still_trips_during_homing);
    RUN_TEST(test_lim_near_trips_during_homing_even_with_lim_far_also_active);
    RUN_TEST(test_leaving_homing_restores_the_limit_rule);
    RUN_TEST(test_motion_while_inhibited_trips_at_dispatch);
    RUN_TEST(test_motion_while_inhibited_trips_mid_run_not_just_at_dispatch);
    RUN_TEST(test_inhibit_mask_alone_does_not_trip_without_motion);
    RUN_TEST(test_motion_alone_does_not_trip_without_inhibit);
    RUN_TEST(test_inhibit_mask_still_trips_during_homing);
    RUN_TEST(test_limit_switch_takes_priority_when_both_fire);
    return UNITY_END();
}
