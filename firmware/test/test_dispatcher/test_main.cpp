#include <unity.h>

#include "dispatcher.h"
#include "messages.h"

void setUp() {}
void tearDown() {}

using core_sm::StateMachine;
using dispatch::Context;
using dispatch::Dispatcher;
using dispatch::Reply;
using mod::CoreState;
using proto::Err;
using proto::EventType;
using proto::MsgId;

static uint8_t id(MsgId m) { return static_cast<uint8_t>(m); }

static StateMachine booted() {
    StateMachine sm;
    sm.apply(core_sm::Event::SELF_TEST_PASS, {});
    return sm;
}

static void assert_nack(const Reply& r, Err err) {
    TEST_ASSERT_FALSE(r.ack);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(err), static_cast<uint8_t>(r.err));
}

// ---- STOP / RESET / unknown ---------------------------------------------------------

static void test_stop_faults_from_any_state_and_reports_operator_stop() {
    Dispatcher d;
    StateMachine sm;  // BOOT: even here
    const Reply r = d.handle(id(MsgId::CMD_STOP), nullptr, 0, {}, sm);
    TEST_ASSERT_TRUE(r.ack);
    TEST_ASSERT_TRUE(r.event == EventType::OPERATOR_STOP);
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
}

static void test_stop_with_a_bad_body_still_stops_then_nacks_bad_length() {
    // Decision 1 (Mathew 2026-11-16): any CRC-valid CMD_STOP stops, whatever its body.
    Dispatcher d;
    StateMachine sm = booted();
    const uint8_t junk = 0;
    const Reply r = d.handle(id(MsgId::CMD_STOP), &junk, 1, {}, sm);
    assert_nack(r, Err::BAD_LENGTH);
    TEST_ASSERT_TRUE(r.event == EventType::OPERATOR_STOP);
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
}

static void test_other_empty_body_command_with_a_body_is_bad_length() {
    Dispatcher d;
    StateMachine sm = booted();
    const uint8_t junk = 0;
    assert_nack(d.handle(id(MsgId::CMD_PAUSE), &junk, 1, {}, sm), Err::BAD_LENGTH);
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
}

static void test_non_command_ids_are_unknown() {
    Dispatcher d;
    StateMachine sm = booted();
    assert_nack(d.handle(id(MsgId::TELEMETRY), nullptr, 0, {}, sm), Err::UNKNOWN_MSG);
    assert_nack(d.handle(0x7F, nullptr, 0, {}, sm), Err::UNKNOWN_MSG);
}

static void test_reset_needs_cause_cleared_and_reports_reset() {
    Dispatcher d;
    StateMachine sm = booted();
    d.handle(id(MsgId::CMD_STOP), nullptr, 0, {}, sm);
    Context ctx;
    ctx.safety.lim_near_active = true;  // a cause still present
    assert_nack(d.handle(id(MsgId::CMD_RESET), nullptr, 0, ctx, sm), Err::BAD_STATE);
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
    ctx.safety.lim_near_active = false;
    const Reply r = d.handle(id(MsgId::CMD_RESET), nullptr, 0, ctx, sm);
    TEST_ASSERT_TRUE(r.ack);
    TEST_ASSERT_TRUE(r.event == EventType::RESET);
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
}

static void test_reset_outside_fault_is_bad_state() {
    Dispatcher d;
    StateMachine sm = booted();
    assert_nack(d.handle(id(MsgId::CMD_RESET), nullptr, 0, {}, sm), Err::BAD_STATE);
}

static void test_pause_outside_run_is_bad_state() {
    Dispatcher d;
    StateMachine sm = booted();
    assert_nack(d.handle(id(MsgId::CMD_PAUSE), nullptr, 0, {}, sm), Err::BAD_STATE);
}

// ---- SELECT_PROFILE -----------------------------------------------------------------

static Reply select(Dispatcher& d, StateMachine& sm, uint8_t profile, uint8_t confirmed) {
    uint8_t buf[proto::SELECT_PROFILE_WIRE_LEN];
    size_t len = 0;
    messages::encode_select_profile({profile, confirmed}, buf, sizeof(buf), &len);
    return d.handle(id(MsgId::CMD_SELECT_PROFILE), buf, len, {}, sm);
}

static void test_select_profile_happy_path_sets_profile_and_coupler() {
    Dispatcher d;
    StateMachine sm = booted();
    TEST_ASSERT_FALSE(d.coupler_confirmed());  // never assumed at boot
    TEST_ASSERT_TRUE(select(d, sm, 2, 1).ack);  // 10 kg: selectable
    TEST_ASSERT_TRUE(d.profile() == limits::ProfileId::CELL_10KG);
    TEST_ASSERT_TRUE(d.coupler_confirmed());
}

static void test_select_profile_nacks_per_protocol_contract() {
    Dispatcher d;
    StateMachine sm = booted();
    assert_nack(select(d, sm, 3, 1), Err::OUT_OF_RANGE);           // no such profile
    assert_nack(select(d, sm, 0, 2), Err::OUT_OF_RANGE);           // bool must be 0/1
    assert_nack(select(d, sm, 0, 0), Err::COUPLER_NOT_CONFIRMED);
    assert_nack(select(d, sm, 1, 1), Err::PROFILE_NOT_SELECTABLE);  // 500 kg: coupler TBD
    TEST_ASSERT_TRUE(d.profile() == limits::ProfileId::CELL_100KG);  // unchanged
    TEST_ASSERT_FALSE(d.coupler_confirmed());
    const uint8_t short_body = 0;
    assert_nack(d.handle(id(MsgId::CMD_SELECT_PROFILE), &short_body, 1, {}, sm),
                Err::BAD_LENGTH);
}

static void test_select_profile_only_from_idle() {
    // A frozen ARM config was validated against the old profile's cutoff (skill 3.1), so
    // the profile cannot change from READY onward. FAULT is not IDLE either.
    Dispatcher d;
    StateMachine sm = booted();
    sm.apply(core_sm::Event::SAFETY_TRIP, {});
    assert_nack(select(d, sm, 0, 1), Err::BAD_STATE);
}

// ---- Motion-starting commands: HOME / JOG / TARE ---------------------------------

static Context homed_ctx(bool force_valid) {
    Context c;
    c.homed = true;
    c.position_valid = true;
    c.force_valid = force_valid;
    return c;
}

static Reply home(Dispatcher& d, StateMachine& sm, const Context& c) {
    return d.handle(id(MsgId::CMD_HOME), nullptr, 0, c, sm);
}

static void test_home_fails_closed_without_force() {
    Dispatcher d;
    StateMachine sm = booted();
    assert_nack(home(d, sm, {}), Err::BAD_STATE);  // default Context: force_valid false
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
}

static void test_home_force_guard_is_out_of_range_both_signs() {
    Dispatcher d;
    StateMachine sm = booted();
    Context c;
    c.force_valid = true;
    c.force_mN = 2000;  // |force| < 2 N is required, so exactly 2 N fails
    assert_nack(home(d, sm, c), Err::OUT_OF_RANGE);
    c.force_mN = -2000;
    assert_nack(home(d, sm, c), Err::OUT_OF_RANGE);
    c.force_mN = -1999;
    TEST_ASSERT_TRUE(home(d, sm, c).ack);
    TEST_ASSERT_TRUE(sm.state() == CoreState::HOMING);
}

static void test_home_inhibited_and_wrong_state() {
    Dispatcher d;
    StateMachine sm = booted();
    Context c;
    c.force_valid = true;
    c.safety.inhibit_mask = 0x4;
    assert_nack(home(d, sm, c), Err::MOTION_INHIBITED);
    c.safety.inhibit_mask = 0;
    TEST_ASSERT_TRUE(home(d, sm, c).ack);
    assert_nack(home(d, sm, c), Err::BAD_STATE);  // already HOMING
}

static Reply jog(Dispatcher& d, StateMachine& sm, const Context& c, uint8_t dir,
                 uint8_t action) {
    uint8_t buf[proto::JOG_WIRE_LEN];
    size_t len = 0;
    messages::encode_jog({static_cast<proto::JogDir>(dir), action}, buf, sizeof(buf), &len);
    return d.handle(id(MsgId::CMD_JOG), buf, len, c, sm);
}

static void test_jog_start_nack_order_and_fail_closed() {
    Dispatcher d;
    StateMachine sm = booted();
    Context c;  // not homed
    c.safety.inhibit_mask = 1;
    assert_nack(jog(d, sm, c, 0, 0), Err::NOT_HOMED);
    c = homed_ctx(false);
    c.safety.inhibit_mask = 1;
    assert_nack(jog(d, sm, c, 0, 0), Err::MOTION_INHIBITED);
    c.safety.inhibit_mask = 0;
    assert_nack(jog(d, sm, c, 2, 0), Err::OUT_OF_RANGE);  // bad direction
    assert_nack(jog(d, sm, c, 0, 3), Err::OUT_OF_RANGE);  // bad action
    assert_nack(jog(d, sm, c, 0, 2), Err::BAD_STATE);     // KEEPALIVE outside JOG
    assert_nack(jog(d, sm, c, 0, 0), Err::BAD_STATE);     // force invalid: fail closed
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
}

static void test_jog_round_trip_and_stop_is_never_force_gated() {
    Dispatcher d;
    StateMachine sm = booted();
    TEST_ASSERT_TRUE(jog(d, sm, homed_ctx(true), 1, 0).ack);
    TEST_ASSERT_TRUE(sm.state() == CoreState::JOG);
    TEST_ASSERT_TRUE(d.jog_dir() == proto::JogDir::TOWARD_LIM_NEAR);
    // Force goes invalid mid-jog: stopping must still be accepted.
    TEST_ASSERT_TRUE(jog(d, sm, homed_ctx(false), 0, 1).ack);
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
    assert_nack(jog(d, sm, homed_ctx(true), 0, 1), Err::BAD_STATE);  // not jogging
}

static void test_tare_needs_homed_and_force() {
    Dispatcher d;
    StateMachine sm = booted();
    Context c;
    c.force_valid = true;
    assert_nack(d.handle(id(MsgId::CMD_TARE), nullptr, 0, c, sm), Err::NOT_HOMED);
    assert_nack(d.handle(id(MsgId::CMD_TARE), nullptr, 0, homed_ctx(false), sm),
                Err::BAD_STATE);
    TEST_ASSERT_TRUE(d.handle(id(MsgId::CMD_TARE), nullptr, 0, homed_ctx(true), sm).ack);
    TEST_ASSERT_TRUE(sm.state() == CoreState::TARE);
}

// ---- PRELOAD --------------------------------------------------------------------------

static Reply preload(Dispatcher& d, StateMachine& sm, const Context& c, uint32_t thr_mN,
                     uint16_t mm_per_min) {
    uint8_t buf[proto::PRELOAD_WIRE_LEN];
    size_t len = 0;
    messages::encode_preload({thr_mN, mm_per_min}, buf, sizeof(buf), &len);
    return d.handle(id(MsgId::CMD_PRELOAD), buf, len, c, sm);
}

static void test_preload_field_ranges() {
    Dispatcher d;
    StateMachine sm = booted();
    const Context c = homed_ctx(true);
    assert_nack(preload(d, sm, c, 0, 5), Err::OUT_OF_RANGE);
    assert_nack(preload(d, sm, c, 880001, 5), Err::OUT_OF_RANGE);  // > 100 kg cutoff
    assert_nack(preload(d, sm, c, 2000, 0), Err::OUT_OF_RANGE);
    assert_nack(preload(d, sm, c, 2000, 11), Err::OUT_OF_RANGE);   // > loaded-jog cap
    TEST_ASSERT_TRUE(preload(d, sm, c, 880000, 10).ack);  // both bounds inclusive
    TEST_ASSERT_TRUE(sm.state() == CoreState::PRELOAD);
    TEST_ASSERT_EQUAL_UINT32(880000, d.preload().threshold_mN);
}

static void test_preload_fails_closed_without_force() {
    Dispatcher d;
    StateMachine sm = booted();
    assert_nack(preload(d, sm, homed_ctx(false), 2000, 5), Err::BAD_STATE);
    assert_nack(preload(d, sm, {}, 2000, 5), Err::NOT_HOMED);
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
}

static void test_preload_threshold_checked_against_profile_active_now() {
    // decisions.md 2026-11-13: validate against the profile active when PRELOAD is
    // processed, not whichever was active earlier.
    Dispatcher d;
    StateMachine sm = booted();
    TEST_ASSERT_TRUE(select(d, sm, 2, 1).ack);  // 10 kg: cutoff 88 N
    assert_nack(preload(d, sm, homed_ctx(true), 100000, 5), Err::OUT_OF_RANGE);
    TEST_ASSERT_TRUE(select(d, sm, 0, 1).ack);  // back to 100 kg: same body is fine
    TEST_ASSERT_TRUE(preload(d, sm, homed_ctx(true), 100000, 5).ack);
}

// ---- ARM / START / RESUME -----------------------------------------------------------

static proto::ArmBody good_arm() {
    proto::ArmBody b{};
    b.l0_um = 50000;
    b.speed_mm_per_min = 50;
    b.force_limit_mN = 0;
    b.max_travel_um = 100000;
    b.strain_limit_permille = 0;
    b.cycle_mode = 0;
    return b;
}

static Reply arm(Dispatcher& d, StateMachine& sm, const Context& c, const proto::ArmBody& b) {
    uint8_t buf[proto::ARM_WIRE_LEN];
    size_t len = 0;
    messages::encode_arm(b, buf, sizeof(buf), &len);
    return d.handle(id(MsgId::CMD_ARM), buf, len, c, sm);
}

// Booted, coupler confirmed on the default 100 kg profile.
static StateMachine confirmed(Dispatcher& d) {
    StateMachine sm = booted();
    select(d, sm, 0, 1);
    return sm;
}

static void test_arm_needs_homed_then_coupler_confirmed() {
    Dispatcher d;
    StateMachine sm = booted();
    assert_nack(arm(d, sm, {}, good_arm()), Err::NOT_HOMED);
    assert_nack(arm(d, sm, homed_ctx(true), good_arm()), Err::COUPLER_NOT_CONFIRMED);
}

static void test_arm_run_mode_field_ranges() {
    Dispatcher d;
    StateMachine sm = confirmed(d);
    const Context c = homed_ctx(true);
    proto::ArmBody b = good_arm();
    b.speed_mm_per_min = 501;
    assert_nack(arm(d, sm, c, b), Err::OUT_OF_RANGE);
    b = good_arm();
    b.speed_mm_per_min = 0;
    assert_nack(arm(d, sm, c, b), Err::OUT_OF_RANGE);
    b = good_arm();
    b.force_limit_mN = 880001;
    assert_nack(arm(d, sm, c, b), Err::OUT_OF_RANGE);
    b = good_arm();
    b.max_travel_um = 0;
    assert_nack(arm(d, sm, c, b), Err::OUT_OF_RANGE);
    b = good_arm();
    b.l0_um = 0;  // strain divides by L0
    assert_nack(arm(d, sm, c, b), Err::OUT_OF_RANGE);
    b = good_arm();
    b.cycle_mode = 2;
    assert_nack(arm(d, sm, c, b), Err::OUT_OF_RANGE);
    TEST_ASSERT_FALSE(sm.arming());
}

static void test_arm_cycle_mode_field_ranges() {
    Dispatcher d;
    StateMachine sm = confirmed(d);
    const Context c = homed_ctx(true);
    proto::ArmBody b = good_arm();
    b.cycle_mode = 1;
    b.cycle_strain_min_permille = 0;  // floor at L0: valid
    b.cycle_strain_max_permille = 100;
    b.cycle_count = 0;
    assert_nack(arm(d, sm, c, b), Err::OUT_OF_RANGE);
    b.cycle_count = 10;
    b.cycle_strain_min_permille = 100;  // min >= max
    assert_nack(arm(d, sm, c, b), Err::OUT_OF_RANGE);
    b.cycle_strain_min_permille = 0;
    b.strain_limit_permille = 99;  // peak above the general strain backstop
    assert_nack(arm(d, sm, c, b), Err::OUT_OF_RANGE);
    b.strain_limit_permille = 0;
    // Implied travel 4e9 um * 1000 / 1000 overflows 32-bit maths; must still NACK.
    b.l0_um = 4000000000u;
    b.cycle_strain_max_permille = 1000;
    b.max_travel_um = 1000000000u;
    assert_nack(arm(d, sm, c, b), Err::OUT_OF_RANGE);
    b.l0_um = 50000;
    b.cycle_strain_max_permille = 100;  // 5000 um <= 100000 um
    TEST_ASSERT_TRUE(arm(d, sm, c, b).ack);
    TEST_ASSERT_TRUE(sm.arming());
    TEST_ASSERT_EQUAL_UINT32(10, d.armed_config().cycle_count);
}

static void test_arm_fails_closed_without_force_and_respects_inhibit() {
    Dispatcher d;
    StateMachine sm = confirmed(d);
    Context c = homed_ctx(false);
    assert_nack(arm(d, sm, c, good_arm()), Err::BAD_STATE);
    c.safety.inhibit_mask = 2;
    assert_nack(arm(d, sm, c, good_arm()), Err::MOTION_INHIBITED);
    TEST_ASSERT_FALSE(sm.arming());
}

// READY with the given cycle mode (force valid throughout).
static StateMachine ready(Dispatcher& d, bool cycle_mode) {
    StateMachine sm = confirmed(d);
    proto::ArmBody b = good_arm();
    if (cycle_mode) {
        b.cycle_mode = 1;
        b.cycle_strain_max_permille = 100;
        b.cycle_count = 1;
    }
    arm(d, sm, homed_ctx(true), b);
    sm.apply(core_sm::Event::L0_REACHED, {});
    return sm;
}

static Reply start(Dispatcher& d, StateMachine& sm, const Context& c) {
    return d.handle(id(MsgId::CMD_START), nullptr, 0, c, sm);
}

static void test_start_checks_and_mode() {
    Dispatcher d;
    StateMachine sm = booted();
    assert_nack(start(d, sm, homed_ctx(true)), Err::BAD_STATE);  // not READY
    sm = ready(d, false);
    TEST_ASSERT_TRUE(sm.state() == CoreState::READY);
    assert_nack(start(d, sm, {}), Err::NOT_HOMED);  // skill 4 rule 1: RUN needs homed+valid
    Context c = homed_ctx(true);
    c.safety.inhibit_mask = 1;
    assert_nack(start(d, sm, c), Err::MOTION_INHIBITED);
    assert_nack(start(d, sm, homed_ctx(false)), Err::BAD_STATE);
    TEST_ASSERT_TRUE(start(d, sm, homed_ctx(true)).ack);
    TEST_ASSERT_TRUE(sm.state() == CoreState::RUN);

    Dispatcher d2;
    StateMachine sm2 = ready(d2, true);
    TEST_ASSERT_TRUE(start(d2, sm2, homed_ctx(true)).ack);
    TEST_ASSERT_TRUE(sm2.state() == CoreState::CYCLE);
}

static void test_pause_resume_and_resume_fails_closed() {
    Dispatcher d;
    StateMachine sm = ready(d, false);
    start(d, sm, homed_ctx(true));
    TEST_ASSERT_TRUE(d.handle(id(MsgId::CMD_PAUSE), nullptr, 0, {}, sm).ack);
    TEST_ASSERT_TRUE(sm.state() == CoreState::HOLD);
    Context c = homed_ctx(true);
    c.safety.inhibit_mask = 1;  // protocol.h: soft NACK, BAD_STATE, no fault
    assert_nack(d.handle(id(MsgId::CMD_RESUME), nullptr, 0, c, sm), Err::BAD_STATE);
    assert_nack(d.handle(id(MsgId::CMD_RESUME), nullptr, 0, homed_ctx(false), sm),
                Err::BAD_STATE);
    TEST_ASSERT_TRUE(sm.state() == CoreState::HOLD);
    TEST_ASSERT_TRUE(d.handle(id(MsgId::CMD_RESUME), nullptr, 0, homed_ctx(true), sm).ack);
    TEST_ASSERT_TRUE(sm.state() == CoreState::RUN);
}

static void test_fault_event_mapping() {
    using core_safety::FaultReason;
    TEST_ASSERT_TRUE(dispatch::fault_event(FaultReason::NONE) == EventType::NONE);
    TEST_ASSERT_TRUE(dispatch::fault_event(FaultReason::ESTOP) == EventType::ESTOP);
    TEST_ASSERT_TRUE(dispatch::fault_event(FaultReason::LIMIT_SWITCH_OPEN) ==
                     EventType::LIMIT_STOP);
    TEST_ASSERT_TRUE(dispatch::fault_event(FaultReason::HEARTBEAT_LOST) ==
                     EventType::HEARTBEAT_LOST);
    TEST_ASSERT_TRUE(dispatch::fault_event(FaultReason::MOTION_WHILE_INHIBITED) ==
                     EventType::FAULT);
    TEST_ASSERT_TRUE(dispatch::fault_event(FaultReason::STRAIN_LIMIT) == EventType::FAULT);
}

// ---- Jog deadman keep-alive (decision 2) ------------------------------------------

static Context at(Context c, uint64_t now_us) {
    c.now_us = now_us;
    return c;
}

static void test_jog_keepalive_refreshes_deadman_and_expiry_is_strict() {
    Dispatcher d;
    StateMachine sm = booted();
    const uint64_t T = limits::JOG_KEEPALIVE_TIMEOUT_US;
    TEST_ASSERT_TRUE(jog(d, sm, at(homed_ctx(true), 1000), 0, 0).ack);
    TEST_ASSERT_FALSE(d.jog_deadman_expired(1000 + T));
    TEST_ASSERT_TRUE(d.jog_deadman_expired(1000 + T + 1));
    // 0.7: only an explicit KEEPALIVE refreshes; a START while jogging is BAD_STATE.
    assert_nack(jog(d, sm, at(homed_ctx(true), 150000), 0, 0), Err::BAD_STATE);
    TEST_ASSERT_TRUE(jog(d, sm, at(homed_ctx(true), 200000), 0, 2).ack);
    TEST_ASSERT_TRUE(sm.state() == CoreState::JOG);
    TEST_ASSERT_FALSE(d.jog_deadman_expired(200000 + T));
    TEST_ASSERT_TRUE(d.jog_deadman_expired(200000 + T + 1));
}

static void test_jog_keepalive_cannot_reverse_and_does_not_refresh_without_force() {
    Dispatcher d;
    StateMachine sm = booted();
    const uint64_t T = limits::JOG_KEEPALIVE_TIMEOUT_US;
    TEST_ASSERT_TRUE(jog(d, sm, at(homed_ctx(true), 0), 0, 0).ack);
    assert_nack(jog(d, sm, at(homed_ctx(true), 100000), 1, 2), Err::BAD_STATE);
    assert_nack(jog(d, sm, at(homed_ctx(false), 100000), 0, 2), Err::BAD_STATE);
    assert_nack(jog(d, sm, at(homed_ctx(true), 100000), 2, 2), Err::OUT_OF_RANGE);  // C
    TEST_ASSERT_TRUE(d.jog_deadman_expired(T + 1));  // neither refreshed it
    TEST_ASSERT_TRUE(d.jog_dir() == proto::JogDir::TOWARD_LIM_FAR);
}

// ---- Relieving jog out of FAULT (decision 3) ---------------------------------------

// Switch readings in the safety snapshot, as main.cpp would pass them during a limit FAULT
// (the dispatcher itself owns fault_cause: put it in LIMIT_STOP with d.trip()).
static Context tripped(bool near, bool far, bool force_valid) {
    Context c;
    c.safety.core_state = CoreState::FAULT;
    c.safety.lim_near_active = near;
    c.safety.lim_far_active = far;
    c.force_valid = force_valid;
    return c;
}

static void test_relieving_jog_direction_rules() {
    Dispatcher d;
    StateMachine sm = booted();
    d.trip(sm, EventType::LIMIT_STOP);
    // LIM_NEAR tripped: only TOWARD_LIM_FAR (0) relieves it. Not homed is fine.
    assert_nack(jog(d, sm, tripped(true, false, true), 1, 0), Err::BAD_STATE);
    assert_nack(jog(d, sm, tripped(true, true, true), 0, 0), Err::BAD_STATE);    // both
    assert_nack(jog(d, sm, tripped(false, false, true), 0, 0), Err::BAD_STATE);  // neither
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
    TEST_ASSERT_TRUE(jog(d, sm, tripped(true, false, true), 0, 0).ack);
    TEST_ASSERT_TRUE(sm.state() == CoreState::JOG);
    TEST_ASSERT_TRUE(sm.return_state() == CoreState::FAULT);
    TEST_ASSERT_TRUE(dispatch::relieved_switch(d.jog_dir()) == core_safety::Switch::LIM_NEAR);
    TEST_ASSERT_TRUE(jog(d, sm, {}, 0, 1).ack);  // JOG stop
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
}

static void test_relieving_jog_from_lim_far_and_fails_closed_without_force() {
    Dispatcher d;
    StateMachine sm = booted();
    d.trip(sm, EventType::LIMIT_STOP);
    assert_nack(jog(d, sm, tripped(false, true, false), 1, 0), Err::BAD_STATE);  // this build
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
    Context c = tripped(false, true, true);
    c.safety.inhibit_mask = 1;
    assert_nack(jog(d, sm, c, 1, 0), Err::MOTION_INHIBITED);
    TEST_ASSERT_TRUE(jog(d, sm, tripped(false, true, true), 1, 0).ack);
    TEST_ASSERT_TRUE(dispatch::relieved_switch(d.jog_dir()) == core_safety::Switch::LIM_FAR);
}

// ---- RESET re-runs a failed self-test (decision 4) -----------------------------------

namespace {
mod::Status fake_init_status = mod::Status::ERROR;
mod::Status fake_init() { return fake_init_status; }
mod::Module fake_module{"fake", fake_init, nullptr, nullptr, nullptr, nullptr,
                        nullptr, nullptr, nullptr, nullptr};
const mod::Module* const FAKE_TABLE[] = {&fake_module};
}  // namespace

static void test_reset_retries_failed_self_test_and_stays_faulted_until_it_passes() {
    fake_init_status = mod::Status::ERROR;
    self_test::Latch latch(FAKE_TABLE, 1);
    TEST_ASSERT_FALSE(latch.run_all());
    Dispatcher d;
    StateMachine sm;  // BOOT; main trips straight to FAULT on a failed self-test
    sm.apply(core_sm::Event::SAFETY_TRIP, {});
    d.handle(id(MsgId::CMD_STOP), nullptr, 0, {}, sm);  // STOP+RESET must not bypass it
    Context c;
    c.self_test = &latch;
    assert_nack(d.handle(id(MsgId::CMD_RESET), nullptr, 0, c, sm), Err::BAD_STATE);
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
    fake_init_status = mod::Status::OK;
    TEST_ASSERT_TRUE(d.handle(id(MsgId::CMD_RESET), nullptr, 0, c, sm).ack);
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
}

// ---- Reviewer #10 gaps ---------------------------------------------------------------

static void test_everything_but_stop_is_refused_while_arming() {
    Dispatcher d;
    StateMachine sm = confirmed(d);
    TEST_ASSERT_TRUE(arm(d, sm, homed_ctx(true), good_arm()).ack);
    TEST_ASSERT_TRUE(sm.arming());
    assert_nack(home(d, sm, homed_ctx(true)), Err::BAD_STATE);
    assert_nack(select(d, sm, 0, 1), Err::BAD_STATE);
    assert_nack(arm(d, sm, homed_ctx(true), good_arm()), Err::BAD_STATE);
    assert_nack(jog(d, sm, homed_ctx(true), 0, 0), Err::BAD_STATE);
    assert_nack(d.handle(id(MsgId::CMD_TARE), nullptr, 0, homed_ctx(true), sm),
                Err::BAD_STATE);
    assert_nack(preload(d, sm, homed_ctx(true), 2000, 5), Err::BAD_STATE);
    assert_nack(start(d, sm, homed_ctx(true)), Err::BAD_STATE);
    TEST_ASSERT_TRUE(sm.arming());
    TEST_ASSERT_TRUE(d.handle(id(MsgId::CMD_STOP), nullptr, 0, {}, sm).ack);
    TEST_ASSERT_FALSE(sm.arming());
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
}

static void test_arm_from_ready_is_bad_state() {
    Dispatcher d;
    StateMachine sm = ready(d, false);
    assert_nack(arm(d, sm, homed_ctx(true), good_arm()), Err::BAD_STATE);
}

static void test_tare_with_inhibit_bit_is_motion_inhibited() {
    Dispatcher d;
    StateMachine sm = booted();
    Context c = homed_ctx(true);
    c.safety.inhibit_mask = 0x8;
    assert_nack(d.handle(id(MsgId::CMD_TARE), nullptr, 0, c, sm), Err::MOTION_INHIBITED);
}

static void test_resume_refused_while_module_holds_the_lock() {
    Dispatcher d;
    StateMachine sm = ready(d, false);
    start(d, sm, homed_ctx(true));
    d.handle(id(MsgId::CMD_PAUSE), nullptr, 0, {}, sm);
    sm.apply(core_sm::Event::MODULE_LOCK_REQUEST, {});
    assert_nack(d.handle(id(MsgId::CMD_RESUME), nullptr, 0, homed_ctx(true), sm),
                Err::BAD_STATE);
    TEST_ASSERT_TRUE(sm.state() == CoreState::HOLD);
}

static void test_fault_event_generic_reasons() {
    using core_safety::FaultReason;
    TEST_ASSERT_TRUE(dispatch::fault_event(FaultReason::GUARD_SENSE_OPEN) == EventType::FAULT);
    TEST_ASSERT_TRUE(dispatch::fault_event(FaultReason::MODULE_FAULT) == EventType::FAULT);
    TEST_ASSERT_TRUE(dispatch::fault_event(FaultReason::SOFT_TRAVEL_LIMIT) == EventType::FAULT);
}

// ---- Round 3 (reviewer A/B/C, decisions 1-3, Mathew 2026-11-16) ----------------------

static void test_relieving_jog_refused_unless_cause_is_limit_stop() {
    const EventType causes[] = {EventType::FAULT, EventType::OPERATOR_STOP,
                                EventType::HEARTBEAT_LOST};  // FAULT = MODULE_FAULT on the wire
    for (EventType cause : causes) {
        Dispatcher d;
        StateMachine sm = booted();
        d.trip(sm, cause);
        Context c = tripped(true, false, true);
        assert_nack(jog(d, sm, c, 0, 0), Err::BAD_STATE);
        TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
    }
}

static void test_relieving_jog_refused_with_failed_self_test_latch() {
    fake_init_status = mod::Status::ERROR;
    self_test::Latch latch(FAKE_TABLE, 1);
    latch.run_all();
    Dispatcher d;
    StateMachine sm = booted();
    d.trip(sm, EventType::LIMIT_STOP);
    Context c = tripped(true, false, true);
    c.self_test = &latch;
    assert_nack(jog(d, sm, c, 0, 0), Err::BAD_STATE);
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
}

static void test_relieving_jog_refused_while_any_other_safety_rule_fires() {
    Dispatcher d;
    StateMachine sm = booted();
    d.trip(sm, EventType::LIMIT_STOP);
    Context c = tripped(true, false, true);
    c.safety.estop_active = true;
    assert_nack(jog(d, sm, c, 0, 0), Err::BAD_STATE);
    c.safety.estop_active = false;
    c.safety.module_fault_present = true;
    assert_nack(jog(d, sm, c, 0, 0), Err::BAD_STATE);
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
}

// Starts a relieving jog off LIM_NEAR at position 100, t = 0.
static StateMachine relieving(Dispatcher& d) {
    StateMachine sm = booted();
    d.trip(sm, EventType::LIMIT_STOP);
    Context c = tripped(true, false, true);
    c.position_steps = 100;
    jog(d, sm, c, 0, 0);
    return sm;
}

static void test_relief_suppression_rearms_after_debounced_clear() {
    Dispatcher d;
    StateMachine sm = relieving(d);
    TEST_ASSERT_TRUE(sm.state() == CoreState::JOG);
    core_safety::Inputs in = tripped(true, false, true).safety;
    in.core_state = CoreState::JOG;
    d.apply_relief(in, sm, 100, 0);
    TEST_ASSERT_TRUE(in.relieving_switch == core_safety::Switch::LIM_NEAR);
    TEST_ASSERT_TRUE(core_safety::evaluate(in) == core_safety::FaultReason::NONE);
    in.lim_near_active = false;
    d.apply_relief(in, sm, 100, 0);  // switch released...
    d.apply_relief(in, sm, 100, limits::RELIEF_CLEAR_DEBOUNCE_US);  // ...clear long enough
    in.lim_near_active = true;  // ...and re-trips
    d.apply_relief(in, sm, 100, limits::RELIEF_CLEAR_DEBOUNCE_US + 1);
    TEST_ASSERT_TRUE(in.relieving_switch == core_safety::Switch::NONE);
    TEST_ASSERT_TRUE(core_safety::evaluate(in) == core_safety::FaultReason::LIMIT_SWITCH_OPEN);
}

static void test_relief_overtravel_faults_if_switch_never_clears() {
    Dispatcher d;
    StateMachine sm = relieving(d);
    core_safety::Inputs in = tripped(true, false, true).safety;
    in.core_state = CoreState::JOG;
    d.apply_relief(in, sm, 100 + static_cast<int32_t>(limits::RELIEF_MAX_TRAVEL_STEPS), 0);
    TEST_ASSERT_FALSE(in.relief_overtravel);
    d.apply_relief(in, sm, 101 + static_cast<int32_t>(limits::RELIEF_MAX_TRAVEL_STEPS), 0);
    TEST_ASSERT_TRUE(in.relief_overtravel);
    TEST_ASSERT_TRUE(core_safety::evaluate(in) == core_safety::FaultReason::RELIEF_OVERTRAVEL);
}

static void test_reset_refused_while_relieved_switch_still_tripped() {
    // Reviewer B: the switch suppressed for the jog must still block RESET.
    Dispatcher d;
    StateMachine sm = relieving(d);
    TEST_ASSERT_TRUE(jog(d, sm, {}, 0, 1).ack);  // jog stop -> FAULT
    Context c = tripped(true, false, true);
    c.safety.relieving_switch = core_safety::Switch::LIM_NEAR;  // stale stage-1 snapshot
    c.safety.core_state = CoreState::JOG;
    assert_nack(d.handle(id(MsgId::CMD_RESET), nullptr, 0, c, sm), Err::BAD_STATE);
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
}

static void test_deadman_tick_stops_jog_and_reports_jog_deadman() {
    Dispatcher d;
    StateMachine sm = relieving(d);  // started at t = 0
    const uint64_t T = limits::JOG_KEEPALIVE_TIMEOUT_US;
    TEST_ASSERT_TRUE(d.tick(T, sm) == EventType::NONE);
    TEST_ASSERT_TRUE(sm.state() == CoreState::JOG);
    TEST_ASSERT_TRUE(d.tick(T + 1, sm) == EventType::JOG_DEADMAN);
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);  // relieving jog returns to FAULT
    TEST_ASSERT_TRUE(d.tick(T + 2, sm) == EventType::NONE);  // only once
}

static void test_keepalive_never_starts_a_jog_from_idle_or_fault() {
    Dispatcher d;
    StateMachine sm = booted();
    assert_nack(jog(d, sm, homed_ctx(true), 0, 2), Err::BAD_STATE);
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
    d.trip(sm, EventType::LIMIT_STOP);
    assert_nack(jog(d, sm, tripped(true, false, true), 0, 2), Err::BAD_STATE);
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
}

static void test_operator_stop_during_relieving_jog_with_button_held_stays_fault() {
    Dispatcher d;
    StateMachine sm = relieving(d);
    TEST_ASSERT_TRUE(d.handle(id(MsgId::CMD_STOP), nullptr, 0, {}, sm).ack);
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
    for (int i = 0; i < 3; ++i) {  // the PC keeps sending keep-alives while held
        assert_nack(jog(d, sm, at(tripped(true, false, true), 100000u * (i + 1)), 0, 2),
                    Err::BAD_STATE);
        TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
    }
    // Round 5: a STOP during a relieving jog ends relief until RESET, like any fault.
    assert_nack(jog(d, sm, tripped(true, false, true), 0, 0), Err::BAD_STATE);
}

// ---- Round 4: per-episode relief cap (decision 1), faults while in FAULT (decision 2) ----

// Step arithmetic below adds the cap to small positions in int32; keep it valid if the
// measured cap is ever entered as a large number.
static_assert(limits::RELIEF_MAX_TRAVEL_STEPS <= 1000000000u,
              "relief tests assume the cap fits comfortably in int32 arithmetic");
static constexpr int32_t CAP = static_cast<int32_t>(limits::RELIEF_MAX_TRAVEL_STEPS);

static core_safety::Inputs jog_inputs(bool near, bool far) {
    core_safety::Inputs in = tripped(near, far, true).safety;
    in.core_state = CoreState::JOG;
    return in;
}

static Reply relieve(Dispatcher& d, StateMachine& sm, int32_t position_steps) {
    Context c = tripped(true, false, true);
    c.position_steps = position_steps;
    return jog(d, sm, c, 0, 0);
}

static void test_jog_stop_command_ends_the_press_through_apply_relief() {
    Dispatcher d;
    StateMachine sm = booted();
    d.trip(sm, EventType::LIMIT_STOP);
    TEST_ASSERT_TRUE(relieve(d, sm, 100).ack);
    TEST_ASSERT_TRUE(jog(d, sm, {}, 0, 1).ack);  // JOG STOP, not the deadman tick()
    TEST_ASSERT_TRUE(d.relief_jog_active());     // not yet noticed
    core_safety::Inputs in = jog_inputs(true, false);
    in.core_state = CoreState::FAULT;
    d.apply_relief(in, sm, 100, 0);
    TEST_ASSERT_FALSE(d.relief_jog_active());
    TEST_ASSERT_TRUE(in.relieving_switch == core_safety::Switch::NONE);
}

static void test_safety_tick_trips_once_and_does_not_spam_the_same_cause() {
    Dispatcher d;
    StateMachine sm = booted();
    TEST_ASSERT_TRUE(d.safety_tick(sm, core_safety::FaultReason::NONE) == EventType::NONE);
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
    TEST_ASSERT_TRUE(d.safety_tick(sm, core_safety::FaultReason::LIMIT_SWITCH_OPEN) ==
                     EventType::LIMIT_STOP);
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
    TEST_ASSERT_TRUE(d.fault_cause() == EventType::LIMIT_STOP);
    for (int i = 0; i < 3; ++i) {  // switch still open every pass: no repeat event
        TEST_ASSERT_TRUE(d.safety_tick(sm, core_safety::FaultReason::LIMIT_SWITCH_OPEN) ==
                         EventType::NONE);
    }
}

static void test_estop_during_limit_fault_reports_and_blocks_relief_until_reset() {
    Dispatcher d;
    StateMachine sm = booted();
    d.safety_tick(sm, core_safety::FaultReason::LIMIT_SWITCH_OPEN);
    TEST_ASSERT_TRUE(d.safety_tick(sm, core_safety::FaultReason::ESTOP) == EventType::ESTOP);
    TEST_ASSERT_TRUE(d.fault_cause() == EventType::ESTOP);
    TEST_ASSERT_TRUE(d.safety_tick(sm, core_safety::FaultReason::ESTOP) == EventType::NONE);
    assert_nack(relieve(d, sm, 0), Err::BAD_STATE);
    // E-stop released, switch still open: the cause reads LIMIT_STOP again, but relief
    // stays blocked until RESET (decision 2).
    TEST_ASSERT_TRUE(d.safety_tick(sm, core_safety::FaultReason::LIMIT_SWITCH_OPEN) ==
                     EventType::LIMIT_STOP);
    assert_nack(relieve(d, sm, 0), Err::BAD_STATE);
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
}

static void test_reset_clears_cause_and_block_then_a_new_limit_trip_allows_relief() {
    Dispatcher d;
    StateMachine sm = booted();
    d.safety_tick(sm, core_safety::FaultReason::LIMIT_SWITCH_OPEN);
    d.safety_tick(sm, core_safety::FaultReason::ESTOP);
    TEST_ASSERT_TRUE(d.handle(id(MsgId::CMD_RESET), nullptr, 0, {}, sm).ack);  // all clear
    TEST_ASSERT_TRUE(d.fault_cause() == EventType::NONE);
    d.safety_tick(sm, core_safety::FaultReason::LIMIT_SWITCH_OPEN);
    TEST_ASSERT_TRUE(relieve(d, sm, 0).ack);
}

static void test_stop_never_overwrites_an_existing_cause() {
    Dispatcher d;
    StateMachine sm = booted();
    TEST_ASSERT_TRUE(d.handle(id(MsgId::CMD_STOP), nullptr, 0, {}, sm).ack);
    TEST_ASSERT_TRUE(d.fault_cause() == EventType::OPERATOR_STOP);

    Dispatcher d2;
    StateMachine sm2 = booted();
    d2.trip(sm2, EventType::LIMIT_STOP);
    TEST_ASSERT_TRUE(relieve(d2, sm2, 0).ack);
    TEST_ASSERT_TRUE(d2.handle(id(MsgId::CMD_STOP), nullptr, 0, {}, sm2).ack);
    TEST_ASSERT_TRUE(d2.fault_cause() == EventType::LIMIT_STOP);
}

// ---- Round 5: ANY fault during a relieving jog ends relief until RESET (Mathew) ------------
// Driven through stage1(), exactly as main.cpp's stage 1 runs: relief bookkeeping, then
// core_safety::evaluate(), then safety_tick(). Scenarios A-D are the reviewer's repro.

static EventType stage1(Dispatcher& d, StateMachine& sm, bool near, int32_t pos,
                        bool hb_stale = false, bool estop = false) {
    core_safety::Inputs in;
    in.core_state = sm.state();
    in.lim_near_active = near;
    in.estop_active = estop;
    in.motion_active = sm.state() == CoreState::JOG;
    in.heartbeat_stale = hb_stale;
    // Each call is one 1 ms main-loop pass (1 kHz); only time differences matter.
    static uint64_t now_us = 0;
    now_us += 1000;
    d.apply_relief(in, sm, pos, now_us);
    return d.safety_tick(sm, core_safety::evaluate(in));
}

// IDLE -> limit FAULT through stage 1.
static StateMachine limit_fault(Dispatcher& d) {
    StateMachine sm = booted();
    TEST_ASSERT_TRUE(stage1(d, sm, true, 0) == EventType::LIMIT_STOP);
    return sm;
}

static void test_relief_cap_is_cumulative_across_presses_in_one_episode() {
    Dispatcher d;
    StateMachine sm = limit_fault(d);
    TEST_ASSERT_TRUE(relieve(d, sm, 100).ack);              // baseline 100
    TEST_ASSERT_TRUE(stage1(d, sm, true, 100 + CAP) == EventType::NONE);  // at the cap
    TEST_ASSERT_TRUE(jog(d, sm, {}, 0, 1).ack);             // release
    TEST_ASSERT_TRUE(stage1(d, sm, true, 100 + CAP) == EventType::NONE);
    TEST_ASSERT_TRUE(relieve(d, sm, 100 + CAP).ack);        // press again further along
    // Cumulative from 100, not from this press: overtravel -> FAULT.
    TEST_ASSERT_TRUE(stage1(d, sm, true, 101 + CAP) == EventType::FAULT);
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
    TEST_ASSERT_TRUE(stage1(d, sm, true, 101 + CAP) == EventType::LIMIT_STOP);  // reported
    assert_nack(relieve(d, sm, 101 + CAP), Err::BAD_STATE);  // and not re-allowed
}

static void test_scenario_a_overtravel_then_press_again_is_refused() {
    Dispatcher d;
    StateMachine sm = limit_fault(d);
    TEST_ASSERT_TRUE(relieve(d, sm, 0).ack);
    TEST_ASSERT_TRUE(stage1(d, sm, true, CAP + 1) == EventType::FAULT);  // RELIEF_OVERTRAVEL
    stage1(d, sm, true, CAP + 1);  // LIMIT_STOP overwrites the cause...
    TEST_ASSERT_TRUE(d.fault_cause() == EventType::LIMIT_STOP);
    for (int i = 0; i < 5; ++i) {  // ...but can't re-enable relief: total travel stays capped
        assert_nack(relieve(d, sm, CAP + 1), Err::BAD_STATE);
        TEST_ASSERT_TRUE(stage1(d, sm, true, CAP + 1) == EventType::NONE);
    }
}

static void test_scenario_b_estop_during_relief_blocks_until_reset_and_a_new_trip() {
    Dispatcher d;
    StateMachine sm = limit_fault(d);
    TEST_ASSERT_TRUE(relieve(d, sm, 0).ack);
    TEST_ASSERT_TRUE(stage1(d, sm, true, 0, false, true) == EventType::ESTOP);
    TEST_ASSERT_TRUE(stage1(d, sm, true, 0) == EventType::LIMIT_STOP);  // e-stop released
    assert_nack(relieve(d, sm, 0), Err::BAD_STATE);
    // Only RESET reaching IDLE, then a brand-new limit trip, re-enables relief.
    TEST_ASSERT_TRUE(d.handle(id(MsgId::CMD_RESET), nullptr, 0, {}, sm).ack);
    TEST_ASSERT_TRUE(stage1(d, sm, true, 0) == EventType::LIMIT_STOP);
    TEST_ASSERT_TRUE(relieve(d, sm, 0).ack);
}

static void test_scenario_c_heartbeat_loss_during_relief_blocks_until_reset() {
    Dispatcher d;
    StateMachine sm = limit_fault(d);
    TEST_ASSERT_TRUE(relieve(d, sm, 0).ack);
    TEST_ASSERT_TRUE(stage1(d, sm, true, 0, true) == EventType::HEARTBEAT_LOST);
    TEST_ASSERT_TRUE(stage1(d, sm, true, 0, true) == EventType::LIMIT_STOP);
    assert_nack(relieve(d, sm, 0), Err::BAD_STATE);
}

static void test_scenario_d_chatter_keeps_the_cap_and_never_restarts_the_episode() {
    static_assert(limits::RELIEF_CLEAR_DEBOUNCE_US > 1000,
                  "scenario D needs one 1 ms clear pass to fall short of the debounce");
    Dispatcher d;
    StateMachine sm = limit_fault(d);
    TEST_ASSERT_TRUE(relieve(d, sm, 0).ack);
    TEST_ASSERT_TRUE(stage1(d, sm, false, 0) == EventType::NONE);  // one clear read
    // Re-trip before the debounce: still the original trip, so the cap still applies.
    TEST_ASSERT_TRUE(stage1(d, sm, true, CAP + 1) == EventType::FAULT);  // RELIEF_OVERTRAVEL
    stage1(d, sm, true, CAP + 1);
    assert_nack(relieve(d, sm, CAP + 1), Err::BAD_STATE);
}

static void test_retrip_after_a_debounced_clear_ends_relief_until_reset() {
    Dispatcher d;
    StateMachine sm = limit_fault(d);
    TEST_ASSERT_TRUE(relieve(d, sm, 0).ack);
    for (uint32_t i = 0; i <= limits::RELIEF_CLEAR_DEBOUNCE_US / 1000; ++i) {  // 1 ms passes
        TEST_ASSERT_TRUE(stage1(d, sm, false, 0) == EventType::NONE);
    }
    TEST_ASSERT_TRUE(stage1(d, sm, true, 0) == EventType::LIMIT_STOP);  // re-armed: trips
    TEST_ASSERT_TRUE(sm.state() == CoreState::FAULT);
    assert_nack(relieve(d, sm, 0), Err::BAD_STATE);  // same cause, but no fresh episode
}

// ---- Round 6: any non-limit cause blocks; time debounce restarts per press -----------------

static void test_estop_before_the_limit_trip_blocks_relief() {  // repro H
    Dispatcher d;
    StateMachine sm = booted();
    TEST_ASSERT_TRUE(stage1(d, sm, true, 0, false, true) == EventType::ESTOP);
    TEST_ASSERT_TRUE(stage1(d, sm, true, 0) == EventType::LIMIT_STOP);  // e-stop released
    assert_nack(relieve(d, sm, 0), Err::BAD_STATE);
}

static void test_cmd_stop_from_idle_then_limit_blocks_relief() {  // repro G
    Dispatcher d;
    StateMachine sm = booted();
    TEST_ASSERT_TRUE(d.handle(id(MsgId::CMD_STOP), nullptr, 0, {}, sm).ack);
    TEST_ASSERT_TRUE(stage1(d, sm, true, 0) == EventType::LIMIT_STOP);
    assert_nack(relieve(d, sm, 0), Err::BAD_STATE);
}

static void test_fresh_limit_only_trip_still_relieves_and_resets() {  // repro E
    Dispatcher d;
    StateMachine sm = limit_fault(d);
    TEST_ASSERT_TRUE(relieve(d, sm, 0).ack);
    for (uint32_t i = 0; i <= limits::RELIEF_CLEAR_DEBOUNCE_US / 1000; ++i) {
        TEST_ASSERT_TRUE(stage1(d, sm, false, 0) == EventType::NONE);
    }
    TEST_ASSERT_TRUE(jog(d, sm, {}, 0, 1).ack);
    TEST_ASSERT_TRUE(stage1(d, sm, false, 0) == EventType::NONE);
    TEST_ASSERT_TRUE(d.handle(id(MsgId::CMD_RESET), nullptr, 0, {}, sm).ack);
    TEST_ASSERT_TRUE(sm.state() == CoreState::IDLE);
}

static void test_debounce_progress_does_not_carry_over_between_presses() {  // repro F
    static_assert(limits::RELIEF_CLEAR_DEBOUNCE_US >= 3000, "needs room for 2+ clear passes");
    Dispatcher d;
    StateMachine sm = limit_fault(d);
    TEST_ASSERT_TRUE(relieve(d, sm, 0).ack);
    for (uint32_t i = 0; i + 1 < limits::RELIEF_CLEAR_DEBOUNCE_US / 1000; ++i) {
        stage1(d, sm, false, 0);  // almost debounced...
    }
    TEST_ASSERT_TRUE(jog(d, sm, {}, 0, 1).ack);  // ...released
    stage1(d, sm, true, 0);                      // switch re-reads tripped in FAULT
    TEST_ASSERT_TRUE(relieve(d, sm, 0).ack);     // press 2
    stage1(d, sm, false, 0);                     // one clear pass: not enough on its own
    TEST_ASSERT_TRUE(stage1(d, sm, false, CAP + 1) == EventType::FAULT);  // cap still applies
}

static void test_press_refused_when_already_past_the_cap() {  // reviewer minor #5
    Dispatcher d;
    StateMachine sm = limit_fault(d);
    TEST_ASSERT_TRUE(relieve(d, sm, 0).ack);
    TEST_ASSERT_TRUE(jog(d, sm, {}, 0, 1).ack);
    stage1(d, sm, true, 0);
    assert_nack(relieve(d, sm, CAP + 1), Err::BAD_STATE);  // e.g. moved by hand between presses
    TEST_ASSERT_TRUE(relieve(d, sm, CAP).ack);             // at the cap is still allowed
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_estop_before_the_limit_trip_blocks_relief);
    RUN_TEST(test_cmd_stop_from_idle_then_limit_blocks_relief);
    RUN_TEST(test_fresh_limit_only_trip_still_relieves_and_resets);
    RUN_TEST(test_debounce_progress_does_not_carry_over_between_presses);
    RUN_TEST(test_press_refused_when_already_past_the_cap);
    RUN_TEST(test_relief_cap_is_cumulative_across_presses_in_one_episode);
    RUN_TEST(test_scenario_a_overtravel_then_press_again_is_refused);
    RUN_TEST(test_scenario_b_estop_during_relief_blocks_until_reset_and_a_new_trip);
    RUN_TEST(test_scenario_c_heartbeat_loss_during_relief_blocks_until_reset);
    RUN_TEST(test_scenario_d_chatter_keeps_the_cap_and_never_restarts_the_episode);
    RUN_TEST(test_retrip_after_a_debounced_clear_ends_relief_until_reset);
    RUN_TEST(test_jog_stop_command_ends_the_press_through_apply_relief);
    RUN_TEST(test_safety_tick_trips_once_and_does_not_spam_the_same_cause);
    RUN_TEST(test_estop_during_limit_fault_reports_and_blocks_relief_until_reset);
    RUN_TEST(test_reset_clears_cause_and_block_then_a_new_limit_trip_allows_relief);
    RUN_TEST(test_stop_never_overwrites_an_existing_cause);
    RUN_TEST(test_relieving_jog_refused_unless_cause_is_limit_stop);
    RUN_TEST(test_relieving_jog_refused_with_failed_self_test_latch);
    RUN_TEST(test_relieving_jog_refused_while_any_other_safety_rule_fires);
    RUN_TEST(test_relief_suppression_rearms_after_debounced_clear);
    RUN_TEST(test_relief_overtravel_faults_if_switch_never_clears);
    RUN_TEST(test_reset_refused_while_relieved_switch_still_tripped);
    RUN_TEST(test_deadman_tick_stops_jog_and_reports_jog_deadman);
    RUN_TEST(test_keepalive_never_starts_a_jog_from_idle_or_fault);
    RUN_TEST(test_operator_stop_during_relieving_jog_with_button_held_stays_fault);
    RUN_TEST(test_jog_keepalive_refreshes_deadman_and_expiry_is_strict);
    RUN_TEST(test_jog_keepalive_cannot_reverse_and_does_not_refresh_without_force);
    RUN_TEST(test_relieving_jog_direction_rules);
    RUN_TEST(test_relieving_jog_from_lim_far_and_fails_closed_without_force);
    RUN_TEST(test_reset_retries_failed_self_test_and_stays_faulted_until_it_passes);
    RUN_TEST(test_everything_but_stop_is_refused_while_arming);
    RUN_TEST(test_arm_from_ready_is_bad_state);
    RUN_TEST(test_tare_with_inhibit_bit_is_motion_inhibited);
    RUN_TEST(test_resume_refused_while_module_holds_the_lock);
    RUN_TEST(test_fault_event_generic_reasons);
    RUN_TEST(test_preload_field_ranges);
    RUN_TEST(test_preload_fails_closed_without_force);
    RUN_TEST(test_preload_threshold_checked_against_profile_active_now);
    RUN_TEST(test_arm_needs_homed_then_coupler_confirmed);
    RUN_TEST(test_arm_run_mode_field_ranges);
    RUN_TEST(test_arm_cycle_mode_field_ranges);
    RUN_TEST(test_arm_fails_closed_without_force_and_respects_inhibit);
    RUN_TEST(test_start_checks_and_mode);
    RUN_TEST(test_pause_resume_and_resume_fails_closed);
    RUN_TEST(test_fault_event_mapping);
    RUN_TEST(test_home_fails_closed_without_force);
    RUN_TEST(test_home_force_guard_is_out_of_range_both_signs);
    RUN_TEST(test_home_inhibited_and_wrong_state);
    RUN_TEST(test_jog_start_nack_order_and_fail_closed);
    RUN_TEST(test_jog_round_trip_and_stop_is_never_force_gated);
    RUN_TEST(test_tare_needs_homed_and_force);
    RUN_TEST(test_reset_needs_cause_cleared_and_reports_reset);
    RUN_TEST(test_reset_outside_fault_is_bad_state);
    RUN_TEST(test_pause_outside_run_is_bad_state);
    RUN_TEST(test_select_profile_happy_path_sets_profile_and_coupler);
    RUN_TEST(test_select_profile_nacks_per_protocol_contract);
    RUN_TEST(test_select_profile_only_from_idle);
    RUN_TEST(test_stop_faults_from_any_state_and_reports_operator_stop);
    RUN_TEST(test_stop_with_a_bad_body_still_stops_then_nacks_bad_length);
    RUN_TEST(test_other_empty_body_command_with_a_body_is_bad_length);
    RUN_TEST(test_non_command_ids_are_unknown);
    return UNITY_END();
}
