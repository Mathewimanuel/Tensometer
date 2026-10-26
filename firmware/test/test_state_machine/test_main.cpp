#include <unity.h>

#include "state_machine.h"

void setUp() {}
void tearDown() {}

using core_sm::Event;
using core_sm::Guards;
using core_sm::StateMachine;
using mod::CoreState;
using proto::LockOwner;

static void test_boot_to_idle_on_self_test_pass() {
    StateMachine sm;
    TEST_ASSERT_TRUE(sm.state() == CoreState::BOOT);
    TEST_ASSERT_TRUE(sm.apply(Event::SELF_TEST_PASS, {}));
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
}

static void test_boot_rejects_everything_else() {
    StateMachine sm;
    TEST_ASSERT_FALSE(sm.apply(Event::HOME, {}));
    TEST_ASSERT_FALSE(sm.apply(Event::START, {}));
    TEST_ASSERT_TRUE(sm.state() == CoreState::BOOT);
}

static StateMachine booted() {
    StateMachine sm;
    sm.apply(Event::SELF_TEST_PASS, {});
    return sm;
}

static void test_homing_round_trip() {
    StateMachine sm = booted();
    TEST_ASSERT_TRUE(sm.apply(Event::HOME, {}));
    TEST_ASSERT_TRUE(sm.state() == CoreState::HOMING);
    // Nothing else is accepted mid-homing.
    TEST_ASSERT_FALSE(sm.apply(Event::START, {}));
    TEST_ASSERT_TRUE(sm.apply(Event::HOMING_DONE, {}));
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
}

static void test_jog_tare_preload_rejected_when_not_homed() {
    StateMachine sm = booted();
    Guards g{};  // homed = false
    TEST_ASSERT_FALSE(sm.apply(Event::JOG_START, g));
    TEST_ASSERT_FALSE(sm.apply(Event::TARE_START, g));
    TEST_ASSERT_FALSE(sm.apply(Event::PRELOAD_START, g));
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
}

static void test_jog_round_trip_from_idle_and_ready() {
    StateMachine sm = booted();
    Guards g{};
    g.homed = true;
    g.position_valid = true;

    TEST_ASSERT_TRUE(sm.apply(Event::JOG_START, g));
    TEST_ASSERT_TRUE(sm.state() == CoreState::JOG);
    TEST_ASSERT_TRUE(sm.apply(Event::JOG_STOP, g));
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);  // returned to where it started

    // Get to READY and prove JOG returns there too.
    g.config_valid = true;
    TEST_ASSERT_TRUE(sm.apply(Event::ARM, g));
    TEST_ASSERT_TRUE(sm.apply(Event::L0_REACHED, g));
    TEST_ASSERT_TRUE(sm.state() == CoreState::READY);
    TEST_ASSERT_TRUE(sm.apply(Event::JOG_START, g));
    TEST_ASSERT_TRUE(sm.apply(Event::JOG_STOP, g));
    TEST_ASSERT_TRUE(sm.state() == CoreState::READY);
}

static void test_tare_round_trip() {
    StateMachine sm = booted();
    Guards g{};
    g.homed = true;
    g.position_valid = true;
    TEST_ASSERT_TRUE(sm.apply(Event::TARE_START, g));
    TEST_ASSERT_TRUE(sm.state() == CoreState::TARE);
    TEST_ASSERT_TRUE(sm.apply(Event::TARE_DONE, g));
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
}

static void test_preload_round_trip() {
    StateMachine sm = booted();
    Guards g{};
    g.homed = true;
    g.position_valid = true;
    TEST_ASSERT_TRUE(sm.apply(Event::PRELOAD_START, g));
    TEST_ASSERT_TRUE(sm.state() == CoreState::PRELOAD);
    TEST_ASSERT_TRUE(sm.apply(Event::PRELOAD_DONE, g));
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
}

static void test_arm_rejected_without_full_guards() {
    StateMachine sm = booted();
    Guards g{};
    g.homed = true;
    g.position_valid = true;
    // config_valid still false.
    TEST_ASSERT_FALSE(sm.apply(Event::ARM, g));
    TEST_ASSERT_FALSE(sm.arming());
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
}

static void test_arm_then_l0_reached_reaches_ready() {
    StateMachine sm = booted();
    Guards g{};
    g.homed = true;
    g.position_valid = true;
    g.config_valid = true;

    TEST_ASSERT_TRUE(sm.apply(Event::ARM, g));
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);  // no dedicated "arming" wire state
    TEST_ASSERT_TRUE(sm.arming());

    TEST_ASSERT_TRUE(sm.apply(Event::L0_REACHED, g));
    TEST_ASSERT_FALSE(sm.arming());
    TEST_ASSERT_TRUE(sm.state() == CoreState::READY);
}

static void test_l0_reached_rejected_without_a_pending_arm() {
    StateMachine sm = booted();
    TEST_ASSERT_FALSE(sm.apply(Event::L0_REACHED, {}));
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
}

static void test_arming_blocks_everything_but_l0_reached_and_safety_trip() {
    StateMachine sm = booted();
    Guards g{};
    g.homed = true;
    g.position_valid = true;
    g.config_valid = true;
    TEST_ASSERT_TRUE(sm.apply(Event::ARM, g));
    TEST_ASSERT_TRUE(sm.arming());

    // None of these may run while a move-to-L0 is still in flight: accepting any of them
    // would let a later, now-stale L0_REACHED jump straight to READY from wherever this
    // left the carriage.
    TEST_ASSERT_FALSE(sm.apply(Event::HOME, g));
    TEST_ASSERT_FALSE(sm.apply(Event::JOG_START, g));
    TEST_ASSERT_FALSE(sm.apply(Event::TARE_START, g));
    TEST_ASSERT_FALSE(sm.apply(Event::PRELOAD_START, g));
    TEST_ASSERT_FALSE(sm.apply(Event::ARM, g));  // repeat ARM also rejected
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
    TEST_ASSERT_TRUE(sm.arming());  // still pending, untouched by the rejections

    TEST_ASSERT_TRUE(sm.apply(Event::L0_REACHED, g));
    TEST_ASSERT_FALSE(sm.arming());
    TEST_ASSERT_TRUE(sm.state() == CoreState::READY);
}

static StateMachine ready() {
    StateMachine sm = booted();
    Guards g{};
    g.homed = true;
    g.position_valid = true;
    g.config_valid = true;
    sm.apply(Event::ARM, g);
    sm.apply(Event::L0_REACHED, g);
    return sm;
}

static void test_start_goes_to_run_or_cycle_per_guard() {
    StateMachine sm_run = ready();
    Guards g{};
    g.cycle_mode = false;
    TEST_ASSERT_TRUE(sm_run.apply(Event::START, g));
    TEST_ASSERT_TRUE(sm_run.state() == CoreState::RUN);

    StateMachine sm_cycle = ready();
    g.cycle_mode = true;
    TEST_ASSERT_TRUE(sm_cycle.apply(Event::START, g));
    TEST_ASSERT_TRUE(sm_cycle.state() == CoreState::CYCLE);
}

static void test_pause_from_run_locks_pc_and_resume_returns_to_run() {
    StateMachine sm = ready();
    sm.apply(Event::START, {});  // -> RUN (cycle_mode false)
    TEST_ASSERT_TRUE(sm.apply(Event::PAUSE, {}));
    TEST_ASSERT_TRUE(sm.state() == CoreState::HOLD);
    TEST_ASSERT_TRUE(sm.lock_owner() == LockOwner::PC);

    TEST_ASSERT_TRUE(sm.apply(Event::RESUME, {}));
    TEST_ASSERT_TRUE(sm.state() == CoreState::RUN);
    TEST_ASSERT_TRUE(sm.lock_owner() == LockOwner::NONE);
}

static void test_pause_from_cycle_resume_returns_to_cycle() {
    StateMachine sm = ready();
    Guards g{};
    g.cycle_mode = true;
    sm.apply(Event::START, g);  // -> CYCLE
    sm.apply(Event::PAUSE, {});
    TEST_ASSERT_TRUE(sm.apply(Event::RESUME, {}));
    TEST_ASSERT_TRUE(sm.state() == CoreState::CYCLE);
}

static void test_module_lock_blocks_resume_until_released() {
    StateMachine sm = ready();
    sm.apply(Event::START, {});
    sm.apply(Event::PAUSE, {});

    TEST_ASSERT_TRUE(sm.apply(Event::MODULE_LOCK_REQUEST, {}));
    TEST_ASSERT_TRUE(sm.lock_owner() == LockOwner::MODULE);
    TEST_ASSERT_FALSE(sm.apply(Event::RESUME, {}));  // not PC-locked
    TEST_ASSERT_TRUE(sm.state() == CoreState::HOLD);

    TEST_ASSERT_TRUE(sm.apply(Event::MODULE_LOCK_RELEASE, {}));
    TEST_ASSERT_TRUE(sm.lock_owner() == LockOwner::PC);
    TEST_ASSERT_TRUE(sm.apply(Event::RESUME, {}));
    TEST_ASSERT_TRUE(sm.state() == CoreState::RUN);
}

static void test_resume_blocked_by_module_inhibit_bit() {
    StateMachine sm = ready();
    sm.apply(Event::START, {});
    sm.apply(Event::PAUSE, {});

    Guards g{};
    g.module_inhibit_present = true;
    TEST_ASSERT_FALSE(sm.apply(Event::RESUME, g));
    TEST_ASSERT_TRUE(sm.state() == CoreState::HOLD);
}

static void test_substrate_break_from_run_and_cycle() {
    StateMachine sm_run = ready();
    sm_run.apply(Event::START, {});
    TEST_ASSERT_TRUE(sm_run.apply(Event::SUBSTRATE_BREAK, {}));
    TEST_ASSERT_TRUE(sm_run.state() == CoreState::BREAK_HOLD);

    StateMachine sm_cycle = ready();
    Guards g{};
    g.cycle_mode = true;
    sm_cycle.apply(Event::START, g);
    TEST_ASSERT_TRUE(sm_cycle.apply(Event::SUBSTRATE_BREAK, {}));
    TEST_ASSERT_TRUE(sm_cycle.state() == CoreState::BREAK_HOLD);
}

static void test_segment_done_from_run_holds_then_resumes_to_ready() {
    StateMachine sm = ready();
    sm.apply(Event::START, {});  // -> RUN
    TEST_ASSERT_TRUE(sm.apply(Event::SEGMENT_DONE, {}));
    TEST_ASSERT_TRUE(sm.state() == CoreState::HOLD);
    TEST_ASSERT_TRUE(sm.lock_owner() == LockOwner::PC);

    // A Bode/routing sweep can run here (the whole point of landing in a real HOLD).
    TEST_ASSERT_TRUE(sm.apply(Event::MODULE_LOCK_REQUEST, {}));
    TEST_ASSERT_TRUE(sm.apply(Event::MODULE_LOCK_RELEASE, {}));

    TEST_ASSERT_TRUE(sm.apply(Event::RESUME, {}));
    TEST_ASSERT_TRUE(sm.state() == CoreState::READY);  // not back in RUN
}

static void test_segment_done_from_cycle_holds_then_resumes_to_ready() {
    StateMachine sm = ready();
    Guards g{};
    g.cycle_mode = true;
    sm.apply(Event::START, g);  // -> CYCLE
    TEST_ASSERT_TRUE(sm.apply(Event::SEGMENT_DONE, {}));
    TEST_ASSERT_TRUE(sm.apply(Event::RESUME, {}));
    TEST_ASSERT_TRUE(sm.state() == CoreState::READY);  // not back in CYCLE
}

static void test_break_hold_exits_to_idle_on_reset_with_cause_cleared() {
    StateMachine sm = ready();
    sm.apply(Event::START, {});
    sm.apply(Event::SUBSTRATE_BREAK, {});
    TEST_ASSERT_TRUE(sm.state() == CoreState::BREAK_HOLD);

    // Nothing but RESET (and SAFETY_TRIP) gets out, and RESET needs cause_cleared.
    TEST_ASSERT_FALSE(sm.apply(Event::RESET, {}));
    TEST_ASSERT_FALSE(sm.apply(Event::HOME, {}));
    TEST_ASSERT_TRUE(sm.state() == CoreState::BREAK_HOLD);

    Guards g{};
    g.cause_cleared = true;
    TEST_ASSERT_TRUE(sm.apply(Event::RESET, g));
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
    TEST_ASSERT_TRUE(sm.lock_owner() == LockOwner::NONE);
}

static void test_safety_trip_wins_from_every_state() {
    // BOOT
    {
        StateMachine sm;
        TEST_ASSERT_TRUE(sm.apply(Event::SAFETY_TRIP, {}));
        TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
    }
    // Mid-homing
    {
        StateMachine sm = booted();
        sm.apply(Event::HOME, {});
        TEST_ASSERT_TRUE(sm.apply(Event::SAFETY_TRIP, {}));
        TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
    }
    // Mid-RUN, with a lock owner set -- must clear it.
    {
        StateMachine sm = ready();
        sm.apply(Event::START, {});
        sm.apply(Event::PAUSE, {});
        sm.apply(Event::MODULE_LOCK_REQUEST, {});
        TEST_ASSERT_TRUE(sm.apply(Event::SAFETY_TRIP, {}));
        TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
        TEST_ASSERT_TRUE(sm.lock_owner() == LockOwner::NONE);
    }
    // Idempotent from FAULT itself.
    {
        StateMachine sm;
        sm.apply(Event::SAFETY_TRIP, {});
        TEST_ASSERT_TRUE(sm.apply(Event::SAFETY_TRIP, {}));
        TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
    }
}

static void test_safety_trip_during_arming_clears_the_flag() {
    StateMachine sm = booted();
    Guards g{};
    g.homed = true;
    g.position_valid = true;
    g.config_valid = true;
    sm.apply(Event::ARM, g);
    TEST_ASSERT_TRUE(sm.arming());
    sm.apply(Event::SAFETY_TRIP, {});
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
    TEST_ASSERT_FALSE(sm.arming());
    // A stray L0_REACHED after the trip must not sneak the machine into READY.
    TEST_ASSERT_FALSE(sm.apply(Event::L0_REACHED, g));
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
}

static void test_reset_requires_cause_cleared() {
    StateMachine sm;
    sm.apply(Event::SAFETY_TRIP, {});
    TEST_ASSERT_FALSE(sm.apply(Event::RESET, {}));
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);

    Guards g{};
    g.cause_cleared = true;
    TEST_ASSERT_TRUE(sm.apply(Event::RESET, g));
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
    TEST_ASSERT_TRUE(sm.lock_owner() == LockOwner::NONE);
}

static void test_reset_rejected_outside_fault() {
    StateMachine sm = booted();
    Guards g{};
    g.cause_cleared = true;
    TEST_ASSERT_FALSE(sm.apply(Event::RESET, g));
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_boot_to_idle_on_self_test_pass);
    RUN_TEST(test_boot_rejects_everything_else);
    RUN_TEST(test_homing_round_trip);
    RUN_TEST(test_jog_tare_preload_rejected_when_not_homed);
    RUN_TEST(test_jog_round_trip_from_idle_and_ready);
    RUN_TEST(test_tare_round_trip);
    RUN_TEST(test_preload_round_trip);
    RUN_TEST(test_arm_rejected_without_full_guards);
    RUN_TEST(test_arm_then_l0_reached_reaches_ready);
    RUN_TEST(test_l0_reached_rejected_without_a_pending_arm);
    RUN_TEST(test_arming_blocks_everything_but_l0_reached_and_safety_trip);
    RUN_TEST(test_start_goes_to_run_or_cycle_per_guard);
    RUN_TEST(test_pause_from_run_locks_pc_and_resume_returns_to_run);
    RUN_TEST(test_pause_from_cycle_resume_returns_to_cycle);
    RUN_TEST(test_module_lock_blocks_resume_until_released);
    RUN_TEST(test_resume_blocked_by_module_inhibit_bit);
    RUN_TEST(test_substrate_break_from_run_and_cycle);
    RUN_TEST(test_segment_done_from_run_holds_then_resumes_to_ready);
    RUN_TEST(test_segment_done_from_cycle_holds_then_resumes_to_ready);
    RUN_TEST(test_break_hold_exits_to_idle_on_reset_with_cause_cleared);
    RUN_TEST(test_safety_trip_wins_from_every_state);
    RUN_TEST(test_safety_trip_during_arming_clears_the_flag);
    RUN_TEST(test_reset_requires_cause_cleared);
    RUN_TEST(test_reset_rejected_outside_fault);
    return UNITY_END();
}
