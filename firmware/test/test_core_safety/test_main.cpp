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

static void test_estop_trips() {
    Inputs in{};
    in.core_state = CoreState::RUN;
    in.estop_active = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::ESTOP);
}

// E-stop wins over everything, including a limit switch tripping at the same instant.
static void test_estop_takes_priority_over_everything() {
    Inputs in{};
    in.core_state = CoreState::RUN;
    in.estop_active = true;
    in.lim_far_active = true;
    in.guard_sense_active = true;
    in.run_to_break_armed = true;
    in.module_fault_present = true;
    in.soft_travel_limit_exceeded = true;
    in.strain_limit_exceeded = true;
    in.motion_active = true;
    in.heartbeat_stale = true;
    in.inhibit_mask = 0x1;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::ESTOP);
}

// E-stop is never suspended by the homing carve-out either -- that carve-out is specific
// to the limit-switch rule.
static void test_estop_trips_during_homing() {
    Inputs in{};
    in.core_state = CoreState::HOMING;
    in.estop_active = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::ESTOP);
}

static void test_module_fault_trips() {
    Inputs in{};
    in.core_state = CoreState::IDLE;
    in.module_fault_present = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::MODULE_FAULT);
}

static void test_module_fault_takes_priority_over_travel_and_strain_limits() {
    Inputs in{};
    in.core_state = CoreState::IDLE;
    in.module_fault_present = true;
    in.soft_travel_limit_exceeded = true;
    in.strain_limit_exceeded = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::MODULE_FAULT);
}

// GUARD_INTERLOCK_FITTED is a compile-time constexpr, currently false, applied inside
// evaluate() itself (not folded into a caller bool -- firmware-reviewer flagged the
// earlier design, where the caller had to remember the AND, as exactly the kind of gap
// that stays invisible until the day the interlock is actually fitted). This is the
// regression test for that: even with both inputs that would otherwise mean "guard open
// during an armed test" set true, nothing trips while the interlock isn't fitted.
static void test_guard_sense_never_trips_while_interlock_not_fitted() {
    Inputs in{};
    in.core_state = CoreState::IDLE;
    in.guard_sense_active = true;
    in.run_to_break_armed = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::NONE);
}

static void test_soft_travel_limit_trips() {
    Inputs in{};
    in.core_state = CoreState::RUN;
    in.soft_travel_limit_exceeded = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::SOFT_TRAVEL_LIMIT);
}

static void test_strain_limit_trips() {
    Inputs in{};
    in.core_state = CoreState::RUN;
    in.strain_limit_exceeded = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::STRAIN_LIMIT);
}

static void test_soft_travel_limit_takes_priority_over_strain_limit() {
    Inputs in{};
    in.core_state = CoreState::RUN;
    in.soft_travel_limit_exceeded = true;
    in.strain_limit_exceeded = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::SOFT_TRAVEL_LIMIT);
}

static void test_heartbeat_stale_trips_while_moving() {
    Inputs in{};
    in.core_state = CoreState::RUN;
    in.motion_active = true;
    in.heartbeat_stale = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::HEARTBEAT_LOST);
}

// Skill 4 rule 2 says "while moving" -- a stale heartbeat with nothing in motion must not
// trip (e.g. the PC is slow to reconnect while the machine just sits in IDLE).
static void test_heartbeat_stale_does_not_trip_without_motion() {
    Inputs in{};
    in.core_state = CoreState::IDLE;
    in.motion_active = false;
    in.heartbeat_stale = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::NONE);
}

static void test_heartbeat_takes_priority_over_inhibit_mask() {
    Inputs in{};
    in.core_state = CoreState::RUN;
    in.motion_active = true;
    in.heartbeat_stale = true;
    in.inhibit_mask = 0x1;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::HEARTBEAT_LOST);
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
    RUN_TEST(test_estop_trips);
    RUN_TEST(test_estop_takes_priority_over_everything);
    RUN_TEST(test_estop_trips_during_homing);
    RUN_TEST(test_module_fault_trips);
    RUN_TEST(test_module_fault_takes_priority_over_travel_and_strain_limits);
    RUN_TEST(test_guard_sense_never_trips_while_interlock_not_fitted);
    RUN_TEST(test_soft_travel_limit_trips);
    RUN_TEST(test_strain_limit_trips);
    RUN_TEST(test_soft_travel_limit_takes_priority_over_strain_limit);
    RUN_TEST(test_heartbeat_stale_trips_while_moving);
    RUN_TEST(test_heartbeat_stale_does_not_trip_without_motion);
    RUN_TEST(test_heartbeat_takes_priority_over_inhibit_mask);
    return UNITY_END();
}
