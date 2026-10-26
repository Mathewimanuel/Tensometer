#include <unity.h>

#include "homing.h"

void setUp() {}
void tearDown() {}

using homing::Dir;
using homing::Params;
using homing::Phase;
using homing::Sequencer;

static Params params() {
    Params p{};
    p.back_off_steps = 100;
    p.fast_hz = 3000;
    p.slow_hz = 500;
    return p;
}

static void test_tick_before_start_stays_idle() {
    Sequencer s;
    TEST_ASSERT_TRUE(s.phase() == Phase::IDLE);
    TEST_ASSERT_TRUE(s.tick(12345, false) == Phase::IDLE);
    TEST_ASSERT_FALSE(s.done());
    TEST_ASSERT_TRUE(s.cruise_hz() == 0);
}

static void test_start_enters_fast_approach() {
    Sequencer s;
    s.start(params());
    TEST_ASSERT_TRUE(s.phase() == Phase::FAST_APPROACH);
    TEST_ASSERT_TRUE(s.direction() == Dir::TOWARD_LIM_FAR);
    TEST_ASSERT_TRUE(s.cruise_hz() == 3000);
}

static void test_fast_approach_waits_for_limit_regardless_of_position() {
    Sequencer s;
    s.start(params());
    // Position changing without the limit switch must not advance the phase.
    TEST_ASSERT_TRUE(s.tick(0, false) == Phase::FAST_APPROACH);
    TEST_ASSERT_TRUE(s.tick(500, false) == Phase::FAST_APPROACH);
    TEST_ASSERT_TRUE(s.tick(5000, false) == Phase::FAST_APPROACH);
}

static void test_fast_approach_to_back_off_on_limit() {
    Sequencer s;
    s.start(params());
    s.tick(5000, false);
    TEST_ASSERT_TRUE(s.tick(5000, true) == Phase::BACK_OFF);
    TEST_ASSERT_TRUE(s.direction() == Dir::TOWARD_LIM_NEAR);
    TEST_ASSERT_TRUE(s.cruise_hz() == 3000);  // fast_hz, same as approach
}

static void test_already_at_limit_on_first_tick_still_transitions() {
    Sequencer s;
    s.start(params());
    TEST_ASSERT_TRUE(s.tick(0, true) == Phase::BACK_OFF);
}

static void test_back_off_waits_for_full_distance() {
    Sequencer s;
    s.start(params());
    s.tick(10000, true);  // -> BACK_OFF, baseline position = 10000
    TEST_ASSERT_TRUE(s.phase() == Phase::BACK_OFF);

    // Position decreases while backing off (TOWARD_LIM_NEAR). Not far enough yet.
    TEST_ASSERT_TRUE(s.tick(9950, false) == Phase::BACK_OFF);   // moved 50 of 100
    TEST_ASSERT_TRUE(s.tick(9901, false) == Phase::BACK_OFF);   // moved 99 of 100
    // Exactly at the threshold.
    TEST_ASSERT_TRUE(s.tick(9900, false) == Phase::SLOW_REAPPROACH);
}

static void test_back_off_overshoot_still_transitions() {
    Sequencer s;
    s.start(params());
    s.tick(10000, true);
    // A single tick moves further than back_off_steps -- still accepted (>=, not ==).
    TEST_ASSERT_TRUE(s.tick(9800, false) == Phase::SLOW_REAPPROACH);
}

static void test_back_off_ignores_lim_far_reasserting() {
    Sequencer s;
    s.start(params());
    s.tick(10000, true);
    // LIM_FAR flickering true again mid-back-off must not matter; only distance moved does.
    TEST_ASSERT_TRUE(s.tick(9950, true) == Phase::BACK_OFF);
}

static void test_slow_reapproach_direction_and_rate() {
    Sequencer s;
    s.start(params());
    s.tick(10000, true);     // -> BACK_OFF
    s.tick(9900, false);     // -> SLOW_REAPPROACH
    TEST_ASSERT_TRUE(s.phase() == Phase::SLOW_REAPPROACH);
    TEST_ASSERT_TRUE(s.direction() == Dir::TOWARD_LIM_FAR);
    TEST_ASSERT_TRUE(s.cruise_hz() == 500);  // slow_hz
}

static void test_slow_reapproach_to_done_on_limit() {
    Sequencer s;
    s.start(params());
    s.tick(10000, true);
    s.tick(9900, false);
    TEST_ASSERT_TRUE(s.tick(9950, false) == Phase::SLOW_REAPPROACH);  // not there yet
    TEST_ASSERT_TRUE(s.tick(10000, true) == Phase::DONE);
    TEST_ASSERT_TRUE(s.done());
    TEST_ASSERT_TRUE(s.cruise_hz() == 0);
}

static void test_done_is_sticky_until_restart() {
    Sequencer s;
    s.start(params());
    s.tick(10000, true);
    s.tick(9900, false);
    s.tick(10000, true);
    TEST_ASSERT_TRUE(s.done());

    TEST_ASSERT_TRUE(s.tick(10000, false) == Phase::DONE);
    TEST_ASSERT_TRUE(s.tick(99999, true) == Phase::DONE);
}

static void test_restart_from_done_resets_baseline() {
    Sequencer s;
    s.start(params());
    s.tick(10000, true);
    s.tick(9900, false);
    s.tick(10000, true);
    TEST_ASSERT_TRUE(s.done());

    s.start(params());
    TEST_ASSERT_TRUE(s.phase() == Phase::FAST_APPROACH);
    // Back-off baseline must be re-captured from the new entry position, not stale from
    // the previous cycle.
    TEST_ASSERT_TRUE(s.tick(500, true) == Phase::BACK_OFF);
    TEST_ASSERT_TRUE(s.tick(450, false) == Phase::BACK_OFF);    // moved 50 of 100
    TEST_ASSERT_TRUE(s.tick(400, false) == Phase::SLOW_REAPPROACH);  // moved 100 of 100
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_tick_before_start_stays_idle);
    RUN_TEST(test_start_enters_fast_approach);
    RUN_TEST(test_fast_approach_waits_for_limit_regardless_of_position);
    RUN_TEST(test_fast_approach_to_back_off_on_limit);
    RUN_TEST(test_already_at_limit_on_first_tick_still_transitions);
    RUN_TEST(test_back_off_waits_for_full_distance);
    RUN_TEST(test_back_off_overshoot_still_transitions);
    RUN_TEST(test_back_off_ignores_lim_far_reasserting);
    RUN_TEST(test_slow_reapproach_direction_and_rate);
    RUN_TEST(test_slow_reapproach_to_done_on_limit);
    RUN_TEST(test_done_is_sticky_until_restart);
    RUN_TEST(test_restart_from_done_resets_baseline);
    return UNITY_END();
}
