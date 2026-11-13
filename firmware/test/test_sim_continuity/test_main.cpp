#include <unity.h>

#include "break_detect.h"
#include "protocol.h"
#include "sim_continuity.h"

void setUp() {}
void tearDown() {}

using mod::ConductorEvent;
using sim_continuity::EventQueue;
using sim_continuity::QUEUE_CAPACITY;

static void test_empty_queue_drains_nothing() {
    EventQueue q;
    ConductorEvent out[4];
    TEST_ASSERT_EQUAL_UINT8(0, q.drain(out, 4));
}

static void test_inject_then_drain_is_fifo_order() {
    EventQueue q;
    TEST_ASSERT_TRUE(q.inject_open(0, 100));
    TEST_ASSERT_TRUE(q.inject_open(1, 200));
    TEST_ASSERT_TRUE(q.inject_open(2, 300));
    TEST_ASSERT_EQUAL_UINT8(3, q.pending());

    ConductorEvent out[4] = {};
    const uint8_t n = q.drain(out, 4);
    TEST_ASSERT_EQUAL_UINT8(3, n);
    TEST_ASSERT_EQUAL_UINT64(100, out[0].timestamp_us);
    TEST_ASSERT_EQUAL_UINT8(0, out[0].channel);
    TEST_ASSERT_EQUAL_UINT64(200, out[1].timestamp_us);
    TEST_ASSERT_EQUAL_UINT8(1, out[1].channel);
    TEST_ASSERT_EQUAL_UINT64(300, out[2].timestamp_us);
    TEST_ASSERT_EQUAL_UINT8(2, out[2].channel);
    TEST_ASSERT_EQUAL_UINT8(0, q.pending());
}

// Mirrors mod::Module::conductor_events()'s cap contract: never writes more than `cap`,
// and whatever is left over stays pending for the next drain call.
static void test_drain_respects_cap_and_leaves_the_rest_pending() {
    EventQueue q;
    q.inject_open(0, 1);
    q.inject_open(0, 2);
    q.inject_open(0, 3);

    ConductorEvent out[2] = {};
    TEST_ASSERT_EQUAL_UINT8(2, q.drain(out, 2));
    TEST_ASSERT_EQUAL_UINT64(1, out[0].timestamp_us);
    TEST_ASSERT_EQUAL_UINT64(2, out[1].timestamp_us);
    TEST_ASSERT_EQUAL_UINT8(1, q.pending());

    TEST_ASSERT_EQUAL_UINT8(1, q.drain(out, 2));
    TEST_ASSERT_EQUAL_UINT64(3, out[0].timestamp_us);
}

static void test_queue_rejects_injection_once_full() {
    EventQueue q;
    for (uint8_t i = 0; i < QUEUE_CAPACITY; ++i) {
        TEST_ASSERT_TRUE(q.inject_open(0, i));
    }
    TEST_ASSERT_FALSE(q.inject_open(0, 999));
    TEST_ASSERT_EQUAL_UINT8(QUEUE_CAPACITY, q.pending());
}

// Proves wraparound after a drain frees space -- the ring buffer must not just fill up
// once and stay stuck, which a naive head/tail-without-modulo implementation would do.
static void test_queue_wraps_around_after_partial_drain() {
    EventQueue q;
    for (uint8_t i = 0; i < QUEUE_CAPACITY; ++i) q.inject_open(0, i);

    ConductorEvent out[QUEUE_CAPACITY] = {};
    TEST_ASSERT_EQUAL_UINT8(3, q.drain(out, 3));  // frees 3 slots at the head

    TEST_ASSERT_TRUE(q.inject_open(0, 100));
    TEST_ASSERT_TRUE(q.inject_open(0, 101));
    TEST_ASSERT_TRUE(q.inject_open(0, 102));
    TEST_ASSERT_EQUAL_UINT8(QUEUE_CAPACITY, q.pending());

    const uint8_t n = q.drain(out, QUEUE_CAPACITY);
    TEST_ASSERT_EQUAL_UINT8(QUEUE_CAPACITY, n);
    // The 3 original events that were never drained (indices 3..7), then the 3 new ones.
    TEST_ASSERT_EQUAL_UINT64(3, out[0].timestamp_us);
    TEST_ASSERT_EQUAL_UINT64(7, out[4].timestamp_us);
    TEST_ASSERT_EQUAL_UINT64(100, out[5].timestamp_us);
    TEST_ASSERT_EQUAL_UINT64(102, out[7].timestamp_us);
}

static void test_queue_preserves_channel_across_wraparound() {
    EventQueue q;
    for (uint8_t i = 0; i < QUEUE_CAPACITY; ++i) q.inject_open(i, 1000 + i);
    ConductorEvent out[QUEUE_CAPACITY] = {};
    q.drain(out, 3);
    q.inject_open(9, 9000);

    const uint8_t n = q.drain(out, QUEUE_CAPACITY);
    TEST_ASSERT_EQUAL_UINT8(6, n);  // channels 3..7 survive (5 of the original 8), plus the new one
    TEST_ASSERT_EQUAL_UINT8(3, out[0].channel);
    TEST_ASSERT_EQUAL_UINT8(7, out[4].channel);
    TEST_ASSERT_EQUAL_UINT8(9, out[5].channel);
}

// Module-seam test (skill 14 checklist item 7): goes through the REAL mod::Module
// singleton -- sim_continuity::inject_open() (the documented bench/test entry point)
// feeding sim_continuity::MODULE.conductor_events() (the actual function pointer the
// main loop will eventually call) -- not a bypassed local EventQueue. This is the
// difference between proving the queue logic works (the tests above) and proving the
// module's vtable wiring is correct (this one): a bug wiring conductor_events to the
// wrong queue, or a positional mix-up in the MODULE initializer list, would pass every
// test above while failing this one.
static void test_module_singleton_drains_through_the_real_hook_to_correct_classification() {
    using break_detect::Classifier;
    using break_detect::Event;
    using break_detect::EventType;
    using break_detect::ForceSample;
    using break_detect::Params;

    TEST_ASSERT_NOT_NULL(sim_continuity::MODULE.conductor_events);

    Classifier c;
    c.start_test(/*conductors_present=*/true, /*conductor_source_available=*/true);
    Params p;
    uint64_t t = 0;
    int32_t pos = 0;

    auto feed = [&](int32_t force_mN) {
        ForceSample s{t, pos, force_mN};
        Event out[break_detect::MAX_EVENTS_PER_SAMPLE];
        c.process_sample(s, /*extending=*/true, p, out, break_detect::MAX_EVENTS_PER_SAMPLE);
        t += 12500;
        pos += 100;
    };

    for (int32_t f = 100; f <= 2000; f += 100) feed(f);

    // A thin wire snapping right as the substrate lets go: inject through the public
    // bench entry point, then drain through the real MODULE.conductor_events hook --
    // exactly what main.cpp will eventually call each pass (skill 3.3 step 4).
    TEST_ASSERT_TRUE(sim_continuity::inject_open(/*channel=*/2, t));
    ConductorEvent drained[4] = {};
    const uint8_t n = sim_continuity::MODULE.conductor_events(drained, 4);
    TEST_ASSERT_EQUAL_UINT8(1, n);
    TEST_ASSERT_EQUAL_UINT8(2, drained[0].channel);
    c.push_conductor_open(drained[0].timestamp_us);

    Event out[break_detect::MAX_EVENTS_PER_SAMPLE];
    ForceSample s1{t, pos, 50};  // 97.5% drop: well over both_frac
    uint8_t total = c.process_sample(s1, true, p, out, break_detect::MAX_EVENTS_PER_SAMPLE);
    t += 12500;
    pos += 100;
    ForceSample s2{t, pos, 50};
    total += c.process_sample(s2, true, p, out, break_detect::MAX_EVENTS_PER_SAMPLE);

    bool saw_conductor_open = false;
    bool saw_substrate_break = false;
    for (uint8_t i = 0; i < total; ++i) {
        if (out[i].type == EventType::CONDUCTOR_OPEN) saw_conductor_open = true;
        if (out[i].type == EventType::SUBSTRATE_BREAK) saw_substrate_break = true;
    }
    TEST_ASSERT_TRUE(saw_conductor_open);
    TEST_ASSERT_TRUE(saw_substrate_break);
    TEST_ASSERT_TRUE(c.broken());
}

// Covers every hook except poll/emit swap order: both are intentionally no-ops for this
// software-only stand-in, so calling either in the wrong slot would still pass this test
// (`firmware-reviewer`, 2026-11-13 re-review). Not worth faking distinct side effects on
// two hooks that genuinely do nothing yet -- revisit once continuity/routing give poll/
// emit real, distinguishable bodies to test against.
static void test_module_other_hooks_are_safe_noops() {
    TEST_ASSERT_TRUE(sim_continuity::MODULE.init() == mod::Status::OK);
    TEST_ASSERT_EQUAL_UINT8(proto::CAP_SIM, sim_continuity::MODULE.capability());
    TEST_ASSERT_EQUAL_UINT16(0, sim_continuity::MODULE.safety_check());
    TEST_ASSERT_EQUAL_UINT32(0, sim_continuity::MODULE.inhibit_bits());
    sim_continuity::MODULE.poll();
    sim_continuity::MODULE.emit();
    sim_continuity::MODULE.on_state_change(mod::CoreState::IDLE, mod::CoreState::RUN);
    mod::ConfigSection section{0, 0, nullptr};
    TEST_ASSERT_TRUE(sim_continuity::MODULE.validate_config(section) == mod::Status::OK);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_empty_queue_drains_nothing);
    RUN_TEST(test_inject_then_drain_is_fifo_order);
    RUN_TEST(test_drain_respects_cap_and_leaves_the_rest_pending);
    RUN_TEST(test_queue_rejects_injection_once_full);
    RUN_TEST(test_queue_wraps_around_after_partial_drain);
    RUN_TEST(test_queue_preserves_channel_across_wraparound);
    RUN_TEST(test_module_singleton_drains_through_the_real_hook_to_correct_classification);
    RUN_TEST(test_module_other_hooks_are_safe_noops);
    return UNITY_END();
}
