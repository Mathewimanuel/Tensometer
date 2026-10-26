#include <unity.h>

#include "break_detect.h"

void setUp() {}
void tearDown() {}

namespace {

using break_detect::Classifier;
using break_detect::Event;
using break_detect::EventType;
using break_detect::ForceSample;
using break_detect::Params;

constexpr uint32_t POS_PER_SAMPLE = 100;  // arbitrary steps/sample while extending at a steady rate

// Collects every event emitted across a whole synthetic trace, in order, into a small
// fixed-size log the tests can scan. 64 events is far more than any test trace produces.
struct EventLog {
    Event events[64];
    uint8_t count = 0;

    void add(const Event* e, uint8_t n) {
        for (uint8_t i = 0; i < n && count < 64; ++i) events[count++] = e[i];
    }
    uint8_t count_of(EventType type) const {
        uint8_t c = 0;
        for (uint8_t i = 0; i < count; ++i) {
            if (events[i].type == type) ++c;
        }
        return c;
    }
};

// Feeds one force value as the next sample (steady position advance, 1 ms spacing).
void feed(Classifier& c, EventLog& log, const Params& params, uint64_t& t_us, int32_t& pos,
          int32_t force_mN, bool extending = true) {
    ForceSample s{t_us, pos, force_mN};
    Event out[break_detect::MAX_EVENTS_PER_SAMPLE];
    const uint8_t n = c.process_sample(s, extending, params, out, break_detect::MAX_EVENTS_PER_SAMPLE);
    log.add(out, n);
    t_us += 12500;  // ~80 Hz HX711 cadence
    pos += POS_PER_SAMPLE;
}

// A plain break with no coincident conductor event is deliberately deferred: the
// classifier waits up to coinc_window_post samples before concluding nothing arrived.
// Tests that expect a timed-out SUBSTRATE_BREAK must feed at least this many samples
// after the triggering sample.
void feed_n(Classifier& c, EventLog& log, const Params& params, uint64_t& t_us, int32_t& pos,
            int32_t force_mN, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) feed(c, log, params, t_us, pos, force_mN);
}

// Declines force by a fixed percentage per sample (r/100 of the previous value), so the
// relative drop over any fixed-length window stays constant regardless of the absolute
// level -- unlike a fixed mN step, which looks like an ever-larger fraction of a
// shrinking reference and would eventually misread as a sudden drop. Models a real
// gradual tear, which slows down in absolute mN/sample as the remaining force drops.
// Returns the next value in the decay sequence (one more 5%-ish step past the last
// sample actually fed, not itself fed yet), so a caller can continue flat at that same
// level -- the transition is just one more ordinary decay step, not a jump -- to
// exercise e.g. the residual-hold path in isolation with no spurious final drop.
int32_t feed_decay(Classifier& c, EventLog& log, const Params& params, uint64_t& t_us,
                    int32_t& pos, int32_t from_mN, int32_t to_mN, int32_t percent_per_step) {
    int32_t f = from_mN;
    while (f > to_mN) {
        feed(c, log, params, t_us, pos, f);
        f = f * percent_per_step / 100;
    }
    return f > 0 ? f : 1;  // the last (unfed) value, as a safe flat-hold level (never <= 0)
}

}  // namespace

static void test_steady_rise_then_sudden_break_no_conductor() {
    Classifier c;
    c.start_test(false, false);
    Params p;
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    for (int32_t f = 100; f <= 2000; f += 100) feed(c, log, p, t, pos, f);
    // Sudden snap: well past both_frac relative to the local peak, no conductor event.
    // No source, so resolution happens by timeout: needs coinc_window_post+1 samples.
    feed_n(c, log, p, t, pos, 100, p.coinc_window_post + 2);

    TEST_ASSERT_EQUAL_UINT8(1, log.count_of(EventType::SUBSTRATE_BREAK));
    TEST_ASSERT_EQUAL_UINT8(0, log.count_of(EventType::CONDUCTOR_OPEN));
    TEST_ASSERT_EQUAL_UINT8(0, log.count_of(EventType::UNVERIFIED_CLASSIFICATION));
    TEST_ASSERT_TRUE(c.broken());
}

static void test_thick_wire_break_small_coincident_drop_logs_conductor_open_only() {
    Classifier c;
    c.start_test(true, true);
    Params p;
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    for (int32_t f = 100; f <= 2000; f += 100) feed(c, log, p, t, pos, f);
    // A thick wire alone carries real but partial load: drop is past drop_frac (30%) so
    // it's classified at all, but well under both_frac (60%).
    c.push_conductor_open(t);
    feed(c, log, p, t, pos, 1000);  // 50% drop from the 2000 mN local peak
    feed(c, log, p, t, pos, 1000);

    TEST_ASSERT_EQUAL_UINT8(1, log.count_of(EventType::CONDUCTOR_OPEN));
    TEST_ASSERT_EQUAL_UINT8(0, log.count_of(EventType::SUBSTRATE_BREAK));
    TEST_ASSERT_FALSE(c.broken());
}

static void test_thin_wire_break_large_coincident_drop_logs_both() {
    Classifier c;
    c.start_test(true, true);
    Params p;
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    for (int32_t f = 100; f <= 2000; f += 100) feed(c, log, p, t, pos, f);
    // A thin wire snapping right as the substrate also lets go: a near-total drop.
    c.push_conductor_open(t);
    feed(c, log, p, t, pos, 50);  // 97.5% drop: well over both_frac
    feed(c, log, p, t, pos, 50);

    TEST_ASSERT_EQUAL_UINT8(1, log.count_of(EventType::CONDUCTOR_OPEN));
    TEST_ASSERT_EQUAL_UINT8(1, log.count_of(EventType::SUBSTRATE_BREAK));
    TEST_ASSERT_TRUE(c.broken());
}

static void test_gradual_tear_no_false_drop_then_residual_break() {
    Classifier c;
    c.start_test(false, false);
    Params p;
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    for (int32_t f = 100; f <= 2000; f += 100) feed(c, log, p, t, pos, f);
    // Decline by a fixed 5%/sample: well under drop_frac (30%) over any drop_window-
    // sized window, so the sudden-drop path must never fire here, however low force gets.
    const int32_t settled = feed_decay(c, log, p, t, pos, /*from=*/2000, /*to=*/100,
                                        /*percent_per_step=*/95);
    TEST_ASSERT_EQUAL_UINT8(0, log.count_of(EventType::SUBSTRATE_BREAK));
    TEST_ASSERT_TRUE_MESSAGE(settled * 1000 <= p.residual_frac_permille * 2000,
                              "test setup: settled value must be below the residual threshold");

    // Now hold flat (no further drop) long enough to cross residual_hold.
    feed_n(c, log, p, t, pos, settled, p.residual_hold + 2);

    TEST_ASSERT_EQUAL_UINT8(1, log.count_of(EventType::SUBSTRATE_BREAK));
    TEST_ASSERT_TRUE(c.broken());
}

static void test_fabric_tearing_through_wire_conductor_event_during_slow_decline() {
    // A conductor event that doesn't coincide with any sudden drop (because there isn't
    // one) must not corrupt the residual-path classification: it simply never resolves
    // into an event of its own.
    Classifier c;
    c.start_test(true, true);
    Params p;
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    for (int32_t f = 100; f <= 2000; f += 100) feed(c, log, p, t, pos, f);
    feed_decay(c, log, p, t, pos, /*from=*/2000, /*to=*/1000, /*percent_per_step=*/95);
    c.push_conductor_open(t);  // mid-decline, no coincident drop to attach to
    const int32_t settled =
        feed_decay(c, log, p, t, pos, /*from=*/1000, /*to=*/100, /*percent_per_step=*/95);
    feed_n(c, log, p, t, pos, settled, p.residual_hold + 2);

    TEST_ASSERT_EQUAL_UINT8(1, log.count_of(EventType::SUBSTRATE_BREAK));
    TEST_ASSERT_EQUAL_UINT8(0, log.count_of(EventType::CONDUCTOR_OPEN));
}

static void test_slip_saw_tooth_recovers_and_continues() {
    Classifier c;
    c.start_test(false, false);
    Params p;
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    for (int32_t f = 100; f <= 2000; f += 100) feed(c, log, p, t, pos, f);
    // Dip 15% (within the 3-30% slip band) then recover to the pre-dip peak within a
    // short travel -- a coupler/grip slip, not a break.
    feed(c, log, p, t, pos, 1700);  // 15% dip from 2000
    feed(c, log, p, t, pos, 2000);  // recovers immediately (well under slip_recover_position)

    TEST_ASSERT_EQUAL_UINT8(1, log.count_of(EventType::SLIP));
    TEST_ASSERT_EQUAL_UINT8(0, log.count_of(EventType::SUBSTRATE_BREAK));
    TEST_ASSERT_FALSE(c.broken());

    // Test continues normally afterward: a later real break still classifies correctly.
    feed_n(c, log, p, t, pos, 100, p.coinc_window_post + 2);
    TEST_ASSERT_EQUAL_UINT8(1, log.count_of(EventType::SUBSTRATE_BREAK));
}

// Same scenario, but with conductors present and a source feeding an event that is
// nowhere near the dip: slip detection does not consult the conductor-event stream at
// all, so this must classify identically (skill 13: every synthetic trace is tested
// with and without conductor events).
static void test_slip_saw_tooth_with_conductor_events_present() {
    Classifier c;
    c.start_test(true, true);
    Params p;
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    for (int32_t f = 100; f <= 2000; f += 100) feed(c, log, p, t, pos, f);
    c.push_conductor_open(t);  // well before the dip, irrelevant to it
    feed(c, log, p, t, pos, 1700);
    feed(c, log, p, t, pos, 2000);

    TEST_ASSERT_EQUAL_UINT8(1, log.count_of(EventType::SLIP));
    TEST_ASSERT_EQUAL_UINT8(0, log.count_of(EventType::SUBSTRATE_BREAK));
    TEST_ASSERT_FALSE(c.broken());

    feed_n(c, log, p, t, pos, 100, p.coinc_window_post + 2);
    TEST_ASSERT_EQUAL_UINT8(1, log.count_of(EventType::SUBSTRATE_BREAK));
    // Source was available and nothing coincided with the real break: not unverified.
    TEST_ASSERT_EQUAL_UINT8(0, log.count_of(EventType::UNVERIFIED_CLASSIFICATION));
}

static void test_noise_no_false_trigger() {
    Classifier c;
    c.start_test(false, false);
    Params p;
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    for (int32_t f = 100; f <= 2000; f += 100) feed(c, log, p, t, pos, f);
    // Small +-2% fluctuations around 2000 mN: far under every threshold.
    const int32_t noise[] = {1970, 2030, 1990, 2010, 1980, 2020, 1995, 2005, 1985, 2015};
    for (int32_t f : noise) feed(c, log, p, t, pos, f);

    TEST_ASSERT_EQUAL_UINT8(0, log.count_of(EventType::SUBSTRATE_BREAK));
    TEST_ASSERT_EQUAL_UINT8(0, log.count_of(EventType::SLIP));
    TEST_ASSERT_FALSE(c.broken());
}

static void test_below_min_peak_gate_suppresses_everything() {
    Classifier c;
    c.start_test(false, false);
    Params p;  // min_peak_mN = 500 by default
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    feed(c, log, p, t, pos, 100);
    feed(c, log, p, t, pos, 10);  // a huge relative drop, but peak never left the noise floor
    feed(c, log, p, t, pos, 10);

    TEST_ASSERT_EQUAL_UINT8(0, log.count_of(EventType::SUBSTRATE_BREAK));
    TEST_ASSERT_FALSE(c.broken());
}

static void test_multi_cycle_mullins_softening_no_false_break_across_cycles() {
    Classifier c;
    c.start_test(false, false);
    Params p;
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    // Each cycle peaks lower than the last (Mullins softening), with its own segment
    // reset at cycle start -- must not be read as a break relative to an earlier,
    // higher-peaking cycle.
    const int32_t cycle_peaks[] = {2000, 1700, 1500};
    for (int32_t peak : cycle_peaks) {
        c.start_segment();
        for (int32_t f = 100; f <= peak; f += 100) feed(c, log, p, t, pos, f);
        // Partial unload at cycle end, not a full ramp to zero: a fixed mN step would
        // otherwise become an ever-larger fraction of the shrinking force near the
        // bottom and misread as a sudden drop -- unrelated to what this test checks.
        for (int32_t f = peak; f >= peak * 6 / 10; f -= 100) feed(c, log, p, t, pos, f);
    }

    TEST_ASSERT_EQUAL_UINT8(0, log.count_of(EventType::SUBSTRATE_BREAK));
    TEST_ASSERT_EQUAL_INT32(2000, c.test_max_mN());
}

// Same cycles, with conductors present: a stray event each cycle (nowhere near a drop,
// since none of these partial unloads cross drop_frac) must not introduce a false break.
static void test_multi_cycle_mullins_softening_with_conductor_events_present() {
    Classifier c;
    c.start_test(true, true);
    Params p;
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    const int32_t cycle_peaks[] = {2000, 1700, 1500};
    for (int32_t peak : cycle_peaks) {
        c.start_segment();
        for (int32_t f = 100; f <= peak; f += 100) feed(c, log, p, t, pos, f);
        c.push_conductor_open(t);
        for (int32_t f = peak; f >= peak * 6 / 10; f -= 100) feed(c, log, p, t, pos, f);
    }

    TEST_ASSERT_EQUAL_UINT8(0, log.count_of(EventType::SUBSTRATE_BREAK));
    TEST_ASSERT_EQUAL_UINT8(0, log.count_of(EventType::CONDUCTOR_OPEN));
    TEST_ASSERT_EQUAL_INT32(2000, c.test_max_mN());
}

static void test_resume_after_relaxation_no_false_break() {
    Classifier c;
    c.start_test(false, false);
    Params p;
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    for (int32_t f = 100; f <= 2000; f += 100) feed(c, log, p, t, pos, f);
    // HOLD: not extending. Samples fed here must be fully ignored.
    for (int i = 0; i < 20; ++i) feed(c, log, p, t, pos, 1200, /*extending=*/false);
    // RESUME: fresh segment, force starts back up from the relaxed value.
    c.start_segment();
    for (int32_t f = 1200; f <= 2200; f += 100) feed(c, log, p, t, pos, f);

    TEST_ASSERT_EQUAL_UINT8(0, log.count_of(EventType::SUBSTRATE_BREAK));
    TEST_ASSERT_EQUAL_INT32(2200, c.test_max_mN());
}

// Same resume scenario, with conductors present: a stray event pushed right after
// RESUME must not be misread against the cleared ring (reset_segment_state() clears
// cond_ring_ too) or cause a false break.
static void test_resume_after_relaxation_with_conductor_events_present() {
    Classifier c;
    c.start_test(true, true);
    Params p;
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    for (int32_t f = 100; f <= 2000; f += 100) feed(c, log, p, t, pos, f);
    for (int i = 0; i < 20; ++i) feed(c, log, p, t, pos, 1200, /*extending=*/false);
    c.start_segment();
    c.push_conductor_open(t);
    for (int32_t f = 1200; f <= 2200; f += 100) feed(c, log, p, t, pos, f);

    TEST_ASSERT_EQUAL_UINT8(0, log.count_of(EventType::SUBSTRATE_BREAK));
    TEST_ASSERT_EQUAL_UINT8(0, log.count_of(EventType::CONDUCTOR_OPEN));
    TEST_ASSERT_EQUAL_INT32(2200, c.test_max_mN());
}

static void test_unverified_classification_when_conductors_present_but_no_source() {
    Classifier c;
    c.start_test(true, false);  // conductors_present, but nothing will ever feed events
    Params p;
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    for (int32_t f = 100; f <= 2000; f += 100) feed(c, log, p, t, pos, f);
    feed_n(c, log, p, t, pos, 100, p.coinc_window_post + 2);

    TEST_ASSERT_EQUAL_UINT8(1, log.count_of(EventType::SUBSTRATE_BREAK));
    TEST_ASSERT_EQUAL_UINT8(1, log.count_of(EventType::UNVERIFIED_CLASSIFICATION));
}

static void test_not_extending_is_fully_ignored() {
    Classifier c;
    c.start_test(false, false);
    Params p;
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    feed(c, log, p, t, pos, 5000, /*extending=*/false);
    feed(c, log, p, t, pos, 10, /*extending=*/false);

    TEST_ASSERT_EQUAL_UINT8(0, log.count);
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, c.test_max_mN());
}

static void test_broken_suppresses_further_processing() {
    Classifier c;
    c.start_test(false, false);
    Params p;
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    for (int32_t f = 100; f <= 2000; f += 100) feed(c, log, p, t, pos, f);
    feed_n(c, log, p, t, pos, 100, p.coinc_window_post + 2);
    TEST_ASSERT_TRUE(c.broken());
    const uint8_t count_after_break = log.count;

    // Further samples (e.g. a stray call before the state machine stops) are a no-op.
    feed(c, log, p, t, pos, 3000);
    feed(c, log, p, t, pos, 3000);
    TEST_ASSERT_EQUAL_UINT8(count_after_break, log.count);
}

static void test_max_force_fires_on_every_new_peak() {
    Classifier c;
    c.start_test(false, false);
    Params p;
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    feed(c, log, p, t, pos, 600);
    feed(c, log, p, t, pos, 700);
    feed(c, log, p, t, pos, 700);  // not a new peak
    feed(c, log, p, t, pos, 650);  // not a new peak
    feed(c, log, p, t, pos, 800);

    TEST_ASSERT_EQUAL_UINT8(3, log.count_of(EventType::MAX_FORCE));
    TEST_ASSERT_EQUAL_INT32(800, c.test_max_mN());
}

// Pins down the MAX_EVENTS_PER_SAMPLE = 3 boundary: a single sample that is both a new
// test-max AND the deferred resolution of an earlier both_frac drop produces exactly
// MAX_FORCE + CONDUCTOR_OPEN + SUBSTRATE_BREAK, all three, in that order.
static void test_three_events_fire_together_on_one_sample() {
    Classifier c;
    c.start_test(true, true);
    Params p;
    EventLog log;
    uint64_t t = 0;
    int32_t pos = 0;

    for (int32_t f = 100; f <= 2000; f += 100) feed(c, log, p, t, pos, f);  // test_max = 2000
    feed(c, log, p, t, pos, 50);   // creates a pending both_frac drop (97.5% from ~2000)
    feed(c, log, p, t, pos, 100);  // still waiting, no conductor event yet
    c.push_conductor_open(t);      // lands in the pending drop's coincidence window
    // This sample resolves the pending drop (CONDUCTOR_OPEN + SUBSTRATE_BREAK) and is
    // itself a new test max (MAX_FORCE) -- an implausible force jump physically, but a
    // deliberate adversarial construction to exercise the full 3-event budget.
    const uint8_t count_before = log.count;
    feed(c, log, p, t, pos, 9000);

    // Exactly 3 new events landed from this one sample, and they're MAX_FORCE +
    // CONDUCTOR_OPEN + SUBSTRATE_BREAK in that order -- proving all three fired
    // together on a single process_sample() call, not spread across several.
    TEST_ASSERT_EQUAL_UINT8(3, static_cast<uint8_t>(log.count - count_before));
    TEST_ASSERT_TRUE(log.events[count_before + 0].type == EventType::MAX_FORCE);
    TEST_ASSERT_TRUE(log.events[count_before + 1].type == EventType::CONDUCTOR_OPEN);
    TEST_ASSERT_TRUE(log.events[count_before + 2].type == EventType::SUBSTRATE_BREAK);
    TEST_ASSERT_EQUAL_UINT8(1, log.count_of(EventType::CONDUCTOR_OPEN));
    TEST_ASSERT_EQUAL_UINT8(1, log.count_of(EventType::SUBSTRATE_BREAK));
    TEST_ASSERT_TRUE(c.broken());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_steady_rise_then_sudden_break_no_conductor);
    RUN_TEST(test_thick_wire_break_small_coincident_drop_logs_conductor_open_only);
    RUN_TEST(test_thin_wire_break_large_coincident_drop_logs_both);
    RUN_TEST(test_gradual_tear_no_false_drop_then_residual_break);
    RUN_TEST(test_fabric_tearing_through_wire_conductor_event_during_slow_decline);
    RUN_TEST(test_slip_saw_tooth_recovers_and_continues);
    RUN_TEST(test_noise_no_false_trigger);
    RUN_TEST(test_below_min_peak_gate_suppresses_everything);
    RUN_TEST(test_multi_cycle_mullins_softening_no_false_break_across_cycles);
    RUN_TEST(test_resume_after_relaxation_no_false_break);
    RUN_TEST(test_unverified_classification_when_conductors_present_but_no_source);
    RUN_TEST(test_not_extending_is_fully_ignored);
    RUN_TEST(test_broken_suppresses_further_processing);
    RUN_TEST(test_max_force_fires_on_every_new_peak);
    RUN_TEST(test_three_events_fire_together_on_one_sample);
    RUN_TEST(test_slip_saw_tooth_with_conductor_events_present);
    RUN_TEST(test_multi_cycle_mullins_softening_with_conductor_events_present);
    RUN_TEST(test_resume_after_relaxation_with_conductor_events_present);
    return UNITY_END();
}
