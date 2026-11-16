#include <unity.h>

#include "cycle_sequencer.h"

void setUp() {}
void tearDown() {}

using cycle::Params;
using cycle::Phase;
using cycle::Sequencer;

static Params params() {
    Params p{};
    p.target_min_steps = 1000;
    p.target_max_steps = 5000;
    p.cruise_hz = 3000;
    p.accel_hz_per_s = 6000;
    p.cycle_count = 3;
    return p;
}

static void test_tick_before_start_stays_idle() {
    Sequencer s;
    TEST_ASSERT_TRUE(s.phase() == Phase::IDLE);
    TEST_ASSERT_TRUE(s.tick(12345) == Phase::IDLE);
    TEST_ASSERT_FALSE(s.done());
    TEST_ASSERT_TRUE(s.cruise_hz() == 0);
    TEST_ASSERT_EQUAL_UINT32(0, s.cycles_completed());
}

static void test_start_enters_to_floor() {
    Sequencer s;
    s.start(params());
    TEST_ASSERT_TRUE(s.phase() == Phase::TO_FLOOR);
    TEST_ASSERT_EQUAL_INT32(1000, s.target_steps());
    TEST_ASSERT_TRUE(s.cruise_hz() == 3000);
    TEST_ASSERT_TRUE(s.accel_hz_per_s() == 6000);
}

static void test_to_floor_waits_for_target() {
    Sequencer s;
    s.start(params());
    TEST_ASSERT_TRUE(s.tick(0) == Phase::TO_FLOOR);
    TEST_ASSERT_TRUE(s.tick(500) == Phase::TO_FLOOR);
    TEST_ASSERT_TRUE(s.tick(999) == Phase::TO_FLOOR);
}

static void test_to_floor_to_to_max_on_reaching_floor() {
    Sequencer s;
    s.start(params());
    TEST_ASSERT_TRUE(s.tick(1000) == Phase::TO_MAX);
    TEST_ASSERT_EQUAL_INT32(5000, s.target_steps());
    TEST_ASSERT_EQUAL_UINT32(0, s.cycles_completed());  // TO_FLOOR never counts as a cycle
}

static void test_to_max_to_to_min_on_reaching_peak() {
    Sequencer s;
    s.start(params());
    s.tick(1000);  // -> TO_MAX
    TEST_ASSERT_TRUE(s.tick(3000) == Phase::TO_MAX);  // not there yet
    TEST_ASSERT_TRUE(s.tick(5000) == Phase::TO_MIN);
    TEST_ASSERT_EQUAL_INT32(1000, s.target_steps());
}

static void test_to_min_completes_one_cycle_and_returns_to_to_max() {
    Sequencer s;
    s.start(params());
    s.tick(1000);  // -> TO_MAX
    s.tick(5000);  // -> TO_MIN
    TEST_ASSERT_TRUE(s.tick(3000) == Phase::TO_MIN);  // not back yet
    TEST_ASSERT_EQUAL_UINT32(0, s.cycles_completed());
    TEST_ASSERT_TRUE(s.tick(1000) == Phase::TO_MAX);  // one full cycle done
    TEST_ASSERT_EQUAL_UINT32(1, s.cycles_completed());
}

static void test_done_after_cycle_count_reached() {
    Sequencer s;
    s.start(params());  // cycle_count = 3
    s.tick(1000);        // -> TO_MAX (TO_FLOOR done)
    for (uint32_t i = 0; i < 3; ++i) {
        s.tick(5000);   // -> TO_MIN
        Phase p = s.tick(1000);  // -> TO_MAX, or DONE on the 3rd
        if (i < 2) {
            TEST_ASSERT_TRUE(p == Phase::TO_MAX);
        } else {
            TEST_ASSERT_TRUE(p == Phase::DONE);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(3, s.cycles_completed());
    TEST_ASSERT_TRUE(s.done());
    TEST_ASSERT_TRUE(s.cruise_hz() == 0);  // meaningless-but-safe once DONE
}

static void test_overshoot_past_target_still_advances() {
    // A one-step (or more) overshoot from the motion planner's own deceleration rounding
    // must still advance the phase, not stall waiting for an exact position match.
    Sequencer s;
    s.start(params());
    TEST_ASSERT_TRUE(s.tick(1010) == Phase::TO_MAX);   // overshot the floor by 10
    TEST_ASSERT_TRUE(s.tick(5050) == Phase::TO_MIN);    // overshot the peak by 50
    TEST_ASSERT_TRUE(s.tick(990) == Phase::TO_MAX);     // undershot back past the floor
}

static void test_steps_remaining_tracks_current_phase_target() {
    Sequencer s;
    s.start(params());
    TEST_ASSERT_EQUAL_UINT32(1000, s.steps_remaining(0));  // TO_FLOOR, target 1000
    s.tick(1000);  // -> TO_MAX, target 5000
    TEST_ASSERT_EQUAL_UINT32(2000, s.steps_remaining(3000));
}

static void test_abort_resets_to_idle() {
    Sequencer s;
    s.start(params());
    s.tick(1000);
    s.abort();
    TEST_ASSERT_TRUE(s.phase() == Phase::IDLE);
    TEST_ASSERT_TRUE(s.tick(5000) == Phase::IDLE);  // no-op without a fresh start()
}

static void test_restart_after_done_resets_cycles_completed() {
    Sequencer s;
    Params p = params();
    p.cycle_count = 1;
    s.start(p);
    s.tick(1000);  // -> TO_MAX
    s.tick(5000);  // -> TO_MIN
    TEST_ASSERT_TRUE(s.tick(1000) == Phase::DONE);
    TEST_ASSERT_EQUAL_UINT32(1, s.cycles_completed());

    s.start(p);  // restart from DONE
    TEST_ASSERT_TRUE(s.phase() == Phase::TO_FLOOR);
    TEST_ASSERT_EQUAL_UINT32(0, s.cycles_completed());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_tick_before_start_stays_idle);
    RUN_TEST(test_start_enters_to_floor);
    RUN_TEST(test_to_floor_waits_for_target);
    RUN_TEST(test_to_floor_to_to_max_on_reaching_floor);
    RUN_TEST(test_to_max_to_to_min_on_reaching_peak);
    RUN_TEST(test_to_min_completes_one_cycle_and_returns_to_to_max);
    RUN_TEST(test_done_after_cycle_count_reached);
    RUN_TEST(test_overshoot_past_target_still_advances);
    RUN_TEST(test_steps_remaining_tracks_current_phase_target);
    RUN_TEST(test_abort_resets_to_idle);
    RUN_TEST(test_restart_after_done_resets_cycles_completed);
    return UNITY_END();
}
