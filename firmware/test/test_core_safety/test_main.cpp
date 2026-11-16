#include <stdint.h>
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

// ---- Relieving jog out of FAULT (Mathew 2026-11-16) ----------------------------------

static void test_relieving_jog_suppresses_only_the_tripped_switch() {
    Inputs in{};
    in.core_state = CoreState::JOG;
    in.motion_active = true;
    in.relieving_switch = core_safety::Switch::LIM_NEAR;
    in.lim_near_active = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::NONE);
    in.lim_far_active = true;  // the other switch still trips
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::LIMIT_SWITCH_OPEN);

    in = Inputs{};
    in.core_state = CoreState::JOG;
    in.relieving_switch = core_safety::Switch::LIM_FAR;
    in.lim_far_active = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::NONE);
    in.lim_near_active = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::LIMIT_SWITCH_OPEN);
}

static void test_relieving_jog_keeps_every_other_rule_live() {
    Inputs in{};
    in.core_state = CoreState::JOG;
    in.motion_active = true;
    in.relieving_switch = core_safety::Switch::LIM_NEAR;
    in.lim_near_active = true;
    in.heartbeat_stale = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::HEARTBEAT_LOST);
    in.heartbeat_stale = false;
    in.inhibit_mask = 1;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::MOTION_WHILE_INHIBITED);
    in.estop_active = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::ESTOP);
}

static void test_relieving_switch_ignored_outside_jog() {
    Inputs in{};
    in.core_state = CoreState::IDLE;
    in.relieving_switch = core_safety::Switch::LIM_NEAR;
    in.lim_near_active = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::LIMIT_SWITCH_OPEN);
}

// ---- ReliefMonitor: decisions 2a/2b (Mathew 2026-11-16) ------------------------------

static void test_relief_suppresses_only_until_the_switch_first_reads_clear() {
    core_safety::ReliefMonitor m;
    m.start(core_safety::Switch::LIM_NEAR, 100, 50, 0);
    TEST_ASSERT_TRUE(m.suppressed(true, false, 0) == core_safety::Switch::LIM_NEAR);
    TEST_ASSERT_TRUE(m.suppressed(false, false, 0) == core_safety::Switch::NONE);  // cleared
    // Re-armed: a re-trip is no longer suppressed, so evaluate() faults on it.
    TEST_ASSERT_TRUE(m.suppressed(true, false, 0) == core_safety::Switch::NONE);
    m.stop();
    TEST_ASSERT_FALSE(m.active());
    TEST_ASSERT_TRUE(m.suppressed(true, false, 0) == core_safety::Switch::NONE);
}

static void test_relief_overtravel_only_while_not_yet_cleared_either_direction() {
    core_safety::ReliefMonitor m;
    m.start(core_safety::Switch::LIM_FAR, 1000, 50, 0);
    TEST_ASSERT_FALSE(m.overtravel(1050));
    TEST_ASSERT_TRUE(m.overtravel(1051));
    TEST_ASSERT_FALSE(m.overtravel(950));
    TEST_ASSERT_TRUE(m.overtravel(949));
    m.suppressed(false, false, 0);  // switch cleared: the cap no longer applies
    TEST_ASSERT_FALSE(m.overtravel(5000));
}

static void test_relief_overtravel_is_a_fault_reason() {
    Inputs in{};
    in.core_state = CoreState::JOG;
    in.relief_overtravel = true;
    TEST_ASSERT_TRUE(evaluate(in) == FaultReason::RELIEF_OVERTRAVEL);
}

// ---- ReliefMonitor round 4: per-FAULT-episode cap (decision 1), extremes, chatter ------

static void test_relief_baseline_and_cleared_latch_persist_across_presses() {
    core_safety::ReliefMonitor m;
    m.start(core_safety::Switch::LIM_NEAR, 0, 50, 0);
    TEST_ASSERT_FALSE(m.overtravel(30));
    m.stop();  // released: jog over, episode kept
    TEST_ASSERT_FALSE(m.active());
    TEST_ASSERT_TRUE(m.overtravel(80));  // episode-level: 80 > 50 even between presses
    m.start(core_safety::Switch::LIM_NEAR, 30, 50, 0);  // second press: baseline stays 0
    TEST_ASSERT_FALSE(m.overtravel(50));
    TEST_ASSERT_TRUE(m.overtravel(51));  // cumulative 51 > 50
}

static void test_relief_reset_episode_takes_a_fresh_baseline() {
    core_safety::ReliefMonitor m;
    m.start(core_safety::Switch::LIM_NEAR, 0, 50, 0);
    m.suppressed(false, false, 0);  // cleared
    m.stop();
    m.reset_episode();  // new trip or RESET
    m.start(core_safety::Switch::LIM_NEAR, 1000, 50, 0);
    TEST_ASSERT_TRUE(m.suppressed(true, false, 0) == core_safety::Switch::LIM_NEAR);  // latch reset
    TEST_ASSERT_FALSE(m.overtravel(1050));
    TEST_ASSERT_TRUE(m.overtravel(1051));
}

static void test_relief_overtravel_at_int32_extremes() {
    core_safety::ReliefMonitor m;
    m.start(core_safety::Switch::LIM_FAR, INT32_MIN, 50, 0);
    TEST_ASSERT_FALSE(m.overtravel(INT32_MIN + 50));
    TEST_ASSERT_TRUE(m.overtravel(INT32_MAX));  // 2^32 - 1 steps: no overflow, still caught
    m.reset_episode();
    m.start(core_safety::Switch::LIM_FAR, INT32_MAX, 50, 0);
    TEST_ASSERT_FALSE(m.overtravel(INT32_MAX - 50));
    TEST_ASSERT_TRUE(m.overtravel(INT32_MAX - 51));
    TEST_ASSERT_TRUE(m.overtravel(INT32_MIN));
}

static void test_relief_switch_chatter_stays_rearmed() {
    core_safety::ReliefMonitor m;
    m.start(core_safety::Switch::LIM_NEAR, 0, 50, 0);
    TEST_ASSERT_TRUE(m.suppressed(true, false, 0) == core_safety::Switch::LIM_NEAR);
    TEST_ASSERT_TRUE(m.suppressed(false, false, 0) == core_safety::Switch::NONE);  // clear
    TEST_ASSERT_TRUE(m.suppressed(true, false, 0) == core_safety::Switch::NONE);   // re-trip
    TEST_ASSERT_TRUE(m.suppressed(false, false, 0) == core_safety::Switch::NONE);  // clear
    TEST_ASSERT_TRUE(m.suppressed(true, false, 0) == core_safety::Switch::NONE);   // re-trip
    TEST_ASSERT_FALSE(m.overtravel(1000));  // cleared once: the cap no longer applies
}

static void test_relief_clear_is_debounced_in_time_and_a_trip_restarts_it() {
    // Round 6: the switch must read clear continuously for debounce_us before it counts.
    core_safety::ReliefMonitor m;
    m.start(core_safety::Switch::LIM_NEAR, 0, 50, 1000);
    TEST_ASSERT_TRUE(m.suppressed(true, false, 0) == core_safety::Switch::LIM_NEAR);
    TEST_ASSERT_TRUE(m.suppressed(false, false, 100) == core_safety::Switch::LIM_NEAR);
    TEST_ASSERT_TRUE(m.suppressed(false, false, 1099) == core_safety::Switch::LIM_NEAR);
    TEST_ASSERT_TRUE(m.suppressed(true, false, 1200) == core_safety::Switch::LIM_NEAR);  // reset
    TEST_ASSERT_TRUE(m.overtravel(51));  // not cleared: the cap still applies
    TEST_ASSERT_TRUE(m.suppressed(false, false, 2000) == core_safety::Switch::LIM_NEAR);
    TEST_ASSERT_TRUE(m.suppressed(false, false, 2999) == core_safety::Switch::LIM_NEAR);
    TEST_ASSERT_TRUE(m.suppressed(false, false, 3000) == core_safety::Switch::NONE);  // 1 ms
    TEST_ASSERT_FALSE(m.overtravel(5000));
    TEST_ASSERT_TRUE(m.suppressed(true, false, 3100) == core_safety::Switch::NONE);  // re-armed
}

static void test_relief_clear_timing_restarts_on_every_press() {
    // Reviewer minor #2 (repro F): clear time from a previous press must not carry over.
    core_safety::ReliefMonitor m;
    m.start(core_safety::Switch::LIM_NEAR, 0, 50, 1000);
    TEST_ASSERT_TRUE(m.suppressed(false, false, 0) == core_safety::Switch::LIM_NEAR);
    m.stop();
    m.start(core_safety::Switch::LIM_NEAR, 0, 50, 1000);
    TEST_ASSERT_TRUE(m.suppressed(false, false, 5000) == core_safety::Switch::LIM_NEAR);  // fresh
    TEST_ASSERT_TRUE(m.suppressed(false, false, 6000) == core_safety::Switch::NONE);
}

static void test_relief_overtravel_is_judged_per_episode_even_between_presses() {
    // Reviewer minor #5: the dispatcher refuses a press that would start already past the cap.
    core_safety::ReliefMonitor m;
    m.start(core_safety::Switch::LIM_NEAR, 0, 50, 0);
    m.stop();
    TEST_ASSERT_TRUE(m.overtravel(51));
    m.reset_episode();
    TEST_ASSERT_FALSE(m.overtravel(51));  // no episode: nothing to judge
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_relief_clear_is_debounced_in_time_and_a_trip_restarts_it);
    RUN_TEST(test_relief_clear_timing_restarts_on_every_press);
    RUN_TEST(test_relief_overtravel_is_judged_per_episode_even_between_presses);
    RUN_TEST(test_relief_baseline_and_cleared_latch_persist_across_presses);
    RUN_TEST(test_relief_reset_episode_takes_a_fresh_baseline);
    RUN_TEST(test_relief_overtravel_at_int32_extremes);
    RUN_TEST(test_relief_switch_chatter_stays_rearmed);
    RUN_TEST(test_relief_suppresses_only_until_the_switch_first_reads_clear);
    RUN_TEST(test_relief_overtravel_only_while_not_yet_cleared_either_direction);
    RUN_TEST(test_relief_overtravel_is_a_fault_reason);
    RUN_TEST(test_relieving_jog_suppresses_only_the_tripped_switch);
    RUN_TEST(test_relieving_jog_keeps_every_other_rule_live);
    RUN_TEST(test_relieving_switch_ignored_outside_jog);
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
