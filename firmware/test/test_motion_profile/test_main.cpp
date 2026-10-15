#include <unity.h>

#include "motion_profile.h"

void setUp() {}
void tearDown() {}

namespace {

// Stands in for the step ISR: turns a commanded rate into an integer number of steps
// taken over one tick, carrying the sub-step remainder exactly like the real hardware
// would (steps happen at a fixed period, not fractionally). Test-only.
struct StepSim {
    uint64_t remainder_us = 0;
    uint32_t step(uint32_t rate_hz, uint32_t dt_us) {
        if (rate_hz == 0) return 0;
        const uint64_t num = static_cast<uint64_t>(rate_hz) * dt_us + remainder_us;
        const uint32_t steps = static_cast<uint32_t>(num / 1000000ull);
        remainder_us = num % 1000000ull;
        return steps;
    }
};

struct MoveResult {
    uint32_t max_rate_hz;
    uint32_t final_rate_hz;
    uint32_t final_remaining;
    bool overshot;
};

// Drives a fresh Planner through a full position-target move with StepSim standing in
// for the ISR, so tests can check the shape of the produced profile (reaches cruise or
// not, ends exactly at the target, never overshoots) without reimplementing hardware
// timing.
MoveResult run_move(uint32_t target_steps, const motion::Params& params, uint32_t dt_us = 1000,
                     uint32_t max_ticks = 100000) {
    motion::Planner planner;
    StepSim sim;
    uint32_t remaining = target_steps;
    uint32_t max_rate = 0;
    bool overshot = false;
    uint32_t ticks = 0;
    while (remaining > 0 && ticks < max_ticks) {
        const uint32_t rate = planner.tick(dt_us, remaining, params);
        if (rate > max_rate) max_rate = rate;
        const uint32_t stepped = sim.step(rate, dt_us);
        if (stepped >= remaining) {
            if (stepped > remaining) overshot = true;
            remaining = 0;
        } else {
            remaining -= stepped;
        }
        ++ticks;
    }
    TEST_ASSERT_TRUE_MESSAGE(ticks < max_ticks, "move did not finish within max_ticks");
    const uint32_t final_rate = planner.tick(dt_us, remaining, params);  // observe remaining == 0
    return {max_rate, final_rate, remaining, overshot};
}

}  // namespace

static void test_steps_to_stop_exact_division() {
    // v^2 / 2a = 1000*1000 / (2*2000) = 250, divides evenly.
    TEST_ASSERT_EQUAL_UINT32(250, motion::steps_to_stop(1000, 2000));
}

static void test_steps_to_stop_rounds_up() {
    // v^2 / 2a = 1,000,000 / 6 = 166666.67 -> must round up, never down (never brake late).
    TEST_ASSERT_EQUAL_UINT32(166667, motion::steps_to_stop(1000, 3));
}

static void test_steps_to_stop_zero_rate_is_zero() {
    TEST_ASSERT_EQUAL_UINT32(0, motion::steps_to_stop(0, 2000));
}

static void test_accelerates_monotonically_and_holds_cruise() {
    const motion::Params params{500, 1000};  // cruise 500 Hz, accel 1000 steps/s^2
    motion::Planner planner;
    uint32_t prev = 0;
    bool reached_cruise = false;
    for (int i = 0; i < 2000; ++i) {
        const uint32_t r = planner.tick(1000, motion::UNBOUNDED_STEPS, params);
        TEST_ASSERT_TRUE_MESSAGE(r >= prev, "rate must not decrease while accelerating");
        TEST_ASSERT_TRUE_MESSAGE(r <= params.cruise_hz, "rate must never exceed cruise");
        if (r == params.cruise_hz) reached_cruise = true;
        if (reached_cruise) TEST_ASSERT_EQUAL_UINT32(params.cruise_hz, r);  // holds once reached
        prev = r;
    }
    TEST_ASSERT_TRUE(reached_cruise);
}

static void test_long_move_reaches_cruise_and_ends_exactly_on_target() {
    const motion::Params params{1000, 2000};  // accel distance = steps_to_stop(1000,2000) = 250
    const MoveResult r = run_move(2000, params);  // well over 2x the accel distance
    TEST_ASSERT_EQUAL_UINT32(params.cruise_hz, r.max_rate_hz);  // trapezoidal: cruise reached
    TEST_ASSERT_EQUAL_UINT32(0, r.final_remaining);
    TEST_ASSERT_EQUAL_UINT32(0, r.final_rate_hz);
    TEST_ASSERT_FALSE(r.overshot);
}

static void test_short_move_is_triangular_and_ends_exactly_on_target() {
    const motion::Params params{1000, 2000};  // accel distance = 250
    const MoveResult r = run_move(100, params);  // well under the accel distance
    TEST_ASSERT_TRUE_MESSAGE(r.max_rate_hz < params.cruise_hz, "short move must not reach cruise");
    TEST_ASSERT_EQUAL_UINT32(0, r.final_remaining);
    TEST_ASSERT_EQUAL_UINT32(0, r.final_rate_hz);
    TEST_ASSERT_FALSE(r.overshot);
}

static void test_reset_restores_fresh_state() {
    const motion::Params params{1000, 2000};
    motion::Planner used;
    // An odd dt so the sub-Hz remainder carry is non-trivial, to make sure reset()
    // really clears it rather than just the rate.
    for (int i = 0; i < 37; ++i) used.tick(777, motion::UNBOUNDED_STEPS, params);
    used.reset();
    TEST_ASSERT_EQUAL_UINT32(0, used.rate_hz());
    TEST_ASSERT_FALSE(used.moving());

    motion::Planner fresh;
    for (int i = 0; i < 20; ++i) {
        const uint32_t a = used.tick(777, motion::UNBOUNDED_STEPS, params);
        const uint32_t b = fresh.tick(777, motion::UNBOUNDED_STEPS, params);
        TEST_ASSERT_EQUAL_UINT32(b, a);
    }
}

// The pattern the header documents for jog/preload: run open-ended until the caller
// decides to stop, then hand steps_to_stop() at the current rate to the planner as the
// remaining distance so it decelerates cleanly, with no special case in the planner.
static void test_open_ended_move_decelerates_cleanly_after_stop_request() {
    const motion::Params params{1000, 2000};
    motion::Planner planner;
    uint32_t rate = 0;
    for (int i = 0; i < 2000 && rate < params.cruise_hz; ++i) {
        rate = planner.tick(1000, motion::UNBOUNDED_STEPS, params);
    }
    TEST_ASSERT_EQUAL_UINT32(params.cruise_hz, rate);

    const uint32_t handoff = motion::steps_to_stop(planner.rate_hz(), params.accel_hz_per_s);
    StepSim sim;
    uint32_t remaining = handoff;
    uint32_t ticks = 0;
    while (remaining > 0 && ticks < 100000) {
        const uint32_t r = planner.tick(1000, remaining, params);
        const uint32_t stepped = sim.step(r, 1000);
        TEST_ASSERT_TRUE_MESSAGE(stepped <= remaining, "must not overshoot the handed-off target");
        remaining -= stepped;
        ++ticks;
    }
    TEST_ASSERT_TRUE(ticks < 100000);
    planner.tick(1000, remaining, params);
    TEST_ASSERT_EQUAL_UINT32(0, remaining);
    TEST_ASSERT_EQUAL_UINT32(0, planner.rate_hz());
}

static void test_zero_remaining_stops_immediately_even_mid_cruise() {
    const motion::Params params{1000, 2000};
    motion::Planner planner;
    for (int i = 0; i < 2000 && planner.rate_hz() < params.cruise_hz; ++i) {
        planner.tick(1000, motion::UNBOUNDED_STEPS, params);
    }
    TEST_ASSERT_TRUE(planner.moving());
    const uint32_t r = planner.tick(1000, 0, params);
    TEST_ASSERT_EQUAL_UINT32(0, r);
    TEST_ASSERT_FALSE(planner.moving());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_steps_to_stop_exact_division);
    RUN_TEST(test_steps_to_stop_rounds_up);
    RUN_TEST(test_steps_to_stop_zero_rate_is_zero);
    RUN_TEST(test_accelerates_monotonically_and_holds_cruise);
    RUN_TEST(test_long_move_reaches_cruise_and_ends_exactly_on_target);
    RUN_TEST(test_short_move_is_triangular_and_ends_exactly_on_target);
    RUN_TEST(test_reset_restores_fresh_state);
    RUN_TEST(test_open_ended_move_decelerates_cleanly_after_stop_request);
    RUN_TEST(test_zero_remaining_stops_immediately_even_mid_cruise);
    return UNITY_END();
}
