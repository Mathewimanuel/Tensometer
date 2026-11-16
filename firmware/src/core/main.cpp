// Phase 1 firmware entry point: the skill section 3.3 main loop. Each numbered stage below
// matches the skill's list. Stages with no driver yet are explicit STUBs, never invented.
//
// THIS BUILD CANNOT MOVE THE MOTOR: force fails closed (no HX711 driver, #10; Mathew
// 2026-11-16), so lib/dispatcher NACKs every motion-starting command, nothing ever calls
// stepper::enable(), and the motion stage below FAULTs if a motion state is ever entered
// anyway (no motion executor is wired: homing/jog/preload/test speeds are still OPEN).
#include <Arduino.h>

#include "comms.h"
#include "config.h"
#include "core_safety.h"
#include "dispatcher.h"
#include "module_api.h"
#include "motion_controller.h"
#include "protocol.h"
#include "safety_limits.h"
#include "self_test.h"
#include "state_machine.h"
#include "stepper.h"

namespace {

// Drive the driver interface to its safe state before anything else runs.
// ENA: LOW at the Teensy -> opto ON -> TB6600 DISABLED (skill 2.1; TODO(verify) polarity
// on the bench). pinMode(OUTPUT) leaves the pin LOW, which is already the safe level;
// the explicit write documents intent.
//
// SAFETY GAP (confirmed 2026-10-26, skill 2.1): there is no 74AHCT244 buffer and no
// pull-down resistor anywhere on ENA (nor pull-ups on STEP/DIR) -- the Teensy pins
// wire straight to the TB6600's common-anode (+5V) opto inputs. Nothing holds ENA
// disabled between power-up and this line running, or during a reset while the 5V/
// TB6600 side stays powered. This code cannot close that gap; it needs a physical
// pull-down added on the ENA line (see docs/decisions.md 2026-10-26, issue #18).
void drive_outputs_safe() {
    pinMode(cfg::pin::ENA, OUTPUT);
    digitalWriteFast(cfg::pin::ENA, LOW);

    // STEP idles HIGH (opto off). pinMode(OUTPUT) briefly drives LOW for well under
    // 1 us before the write below: shorter than any opto's minimum pulse, and the
    // driver is already disabled, so no step can be taken.
    pinMode(cfg::pin::STEP, OUTPUT);
    digitalWriteFast(cfg::pin::STEP, HIGH);
    pinMode(cfg::pin::DIR, OUTPUT);
    digitalWriteFast(cfg::pin::DIR, HIGH);
}

void configure_inputs() {
    // Limits and e-stop use external pull-ups and RC filters (skill 2.6): plain INPUT.
    pinMode(cfg::pin::LIM_NEAR, INPUT);
    pinMode(cfg::pin::LIM_FAR, INPUT);
    pinMode(cfg::pin::ESTOP_SENSE, INPUT);
    pinMode(cfg::pin::HX711_DOUT, INPUT);
    pinMode(cfg::pin::HX711_SCK, OUTPUT);
    digitalWriteFast(cfg::pin::HX711_SCK, LOW);  // SCK high > 60 us powers the HX711 down
}

uint8_t capabilities() {
    uint8_t caps = proto::CAP_CORE;
    for (uint8_t i = 0; i < mod::MODULE_COUNT; ++i) {
        if (mod::MODULES[i] != nullptr && mod::MODULES[i]->capability != nullptr) {
            caps |= mod::MODULES[i]->capability();
        }
    }
    return caps;
}

// 64-bit monotonic us (skill 3.5). micros() wraps every ~71 min; the loop runs far more
// often than that, so one wrap check per pass is enough.
uint64_t now_us() {
    static uint32_t last = 0;
    static uint64_t high = 0;
    const uint32_t t = micros();
    if (t < last) high += (uint64_t{1} << 32);
    last = t;
    return high | t;
}

core_sm::StateMachine sm;
dispatch::Dispatcher dispatcher;
// Both arguments are constant-initialised in modules.cpp, so no static-init-order issue.
self_test::Latch self_test_latch{mod::MODULES, mod::MODULE_COUNT};
mod::CoreState last_state = mod::CoreState::BOOT;

// STUB (force, #10 blocked): no HX711 driver exists. valid stays false so every
// force-dependent command NACKs (fail closed, Mathew 2026-11-16). Never set a fake value.
struct ForceSample {
    bool valid = false;
    int32_t raw_counts = 0;
    int32_t mN = 0;
    int32_t filtered_mN = 0;
};
ForceSample force;

// Set on HOMING_DONE once a homing executor is wired (needs the OPEN homing::Params).
bool homed = false;

bool is_motion_state(mod::CoreState s) {
    switch (s) {
        case mod::CoreState::HOMING:
        case mod::CoreState::JOG:
        case mod::CoreState::PRELOAD:
        case mod::CoreState::RUN:
        case mod::CoreState::CYCLE:
            return true;
        default:
            return false;
    }
}

bool motion_active() {
    return stepper::moving() || sm.arming() || is_motion_state(sm.state());
}

// Emits a fault EVENT the dispatcher decided on (it owns fault_cause), after an IMMEDIATE
// stop (skill 3.4). NONE = nothing new to report.
void report_fault(uint64_t t, proto::EventType event) {
    if (event == proto::EventType::NONE) return;
    stepper::stop_immediate();
    comms::send_event(t, event, stepper::position_steps());
}

// Stage 5 reaction to whatever state the machine is in now (state changes come from the
// safety stage and from comms). Idempotent; also run right after comms so a CMD_STOP
// acts this pass, not next.
void react(uint64_t t) {
    const mod::CoreState s = sm.state();
    if (s == mod::CoreState::FAULT || s == mod::CoreState::BREAK_HOLD) {
        stepper::stop_immediate();
        motion_controller::abort();  // no half-finished homing can resume after a fault
    }
    // Fail closed: no motion/tare executor exists yet (OPEN speeds/params, force #10). If
    // one of those states is ever entered anyway, that is a dispatcher bug -> FAULT.
    if (is_motion_state(s) || s == mod::CoreState::TARE || sm.arming()) {
        report_fault(t, dispatcher.trip(sm, proto::EventType::FAULT));
    }
    if (sm.state() != last_state) {
        for (uint8_t i = 0; i < mod::MODULE_COUNT; ++i) {
            if (mod::MODULES[i] != nullptr && mod::MODULES[i]->on_state_change != nullptr) {
                mod::MODULES[i]->on_state_change(last_state, sm.state());
            }
        }
        last_state = sm.state();
    }
}

}  // namespace

void setup() {
    drive_outputs_safe();
    configure_inputs();
    pinMode(cfg::pin::STATUS_LED, OUTPUT);
    comms::begin();

    // STUB (watchdog): no watchdog driver yet; "watchdog reset detected at boot" (skill 4)
    // is not checked. TODO: add with the watchdog driver.

    // Self-test: module init() (ABSENT is never a fault, skill 1.4). A failure latches
    // and goes straight to FAULT; RESET re-runs init() on the failed modules and reaches
    // IDLE only if they now pass (decision 4, Mathew 2026-11-16).
    if (self_test_latch.run_all()) {
        sm.apply(core_sm::Event::SELF_TEST_PASS, {});
    } else {
        dispatcher.trip(sm, proto::EventType::FAULT);  // no link yet: cause is in the heartbeat
    }
}

void loop() {
    const uint64_t t = now_us();

    // 1. Core safety evaluation, including every module's safety_check()/inhibit_bits().
    uint32_t inhibit_mask = 0;
    bool module_fault = false;
    for (uint8_t i = 0; i < mod::MODULE_COUNT; ++i) {
        const mod::Module* m = mod::MODULES[i];
        if (m == nullptr) continue;
        if (m->inhibit_bits != nullptr) inhibit_mask |= m->inhibit_bits();
        if (m->safety_check != nullptr && m->safety_check() != 0) module_fault = true;
    }
    core_safety::Inputs in;
    in.core_state = sm.state();
    in.inhibit_mask = inhibit_mask;
    in.motion_active = motion_active();
    // NC switches to GND with external pull-ups (skill 2.6): open/tripped or a broken wire
    // reads HIGH. TODO(verify) wiring polarity on the bench. Polled here, not the
    // pin-change ISR skill 3.2 requires: acceptable only while no motion is possible.
    in.lim_near_active = digitalReadFast(cfg::pin::LIM_NEAR) == HIGH;
    in.lim_far_active = digitalReadFast(cfg::pin::LIM_FAR) == HIGH;
    // STUB / TODO(verify): ESTOP_SENSE opto output polarity is not documented anywhere, so
    // it is not read. Must be resolved (with the pin-change ISR) before any motion exists.
    in.estop_active = false;
    in.guard_sense_active = false;  // not fitted (cfg::GUARD_INTERLOCK_FITTED)
    in.run_to_break_armed = false;
    in.module_fault_present = module_fault;
    // Soft travel/strain limits need a frozen test running; none can start in this build.
    in.soft_travel_limit_exceeded = false;
    in.strain_limit_exceeded = false;
    in.heartbeat_stale = comms::heartbeat_stale(t);
    // Relieving jog out of FAULT: suppress only the switch being backed off, only until it
    // has read clear continuously for RELIEF_CLEAR_DEBOUNCE_US (timed with `t`), and fault on
    // overtravel past the cumulative per-FAULT cap (lib/dispatcher, decisions.md 2026-11-16).
    dispatcher.apply_relief(in, sm, stepper::position_steps(), t);
    const core_safety::FaultReason reason = core_safety::evaluate(in);
    if (reason == core_safety::FaultReason::ESTOP) {
        stepper::disable();  // skill 4: e-stop also drops ENA and position_valid
        motion_controller::clear_position_valid();
        homed = false;
    }
    // Trips, or (already in FAULT) reports a changed cause; never repeats one (decision 2).
    report_fault(t, dispatcher.safety_tick(sm, reason));
    // Still-missing section 4 rules: force cutoff, HX711 stale, slip suspect (#10).

    // 2. Force: STUB, see ForceSample above.

    // 3. Module poll(), table order.
    for (uint8_t i = 0; i < mod::MODULE_COUNT; ++i) {
        if (mod::MODULES[i] != nullptr && mod::MODULES[i]->poll != nullptr) {
            mod::MODULES[i]->poll();
        }
    }

    // 4. Break detection: STUB. break_detect::Classifier needs force samples (#10); the
    // conductor-event queue is left undrained until then.

    // 5. State machine and motion. Jog deadman: no KEEPALIVE in time -> JOG_STOP (back to
    // IDLE/READY, or FAULT for a relieving jog) and a JOG_DEADMAN event.
    const proto::EventType deadman = dispatcher.tick(t, sm);
    if (deadman != proto::EventType::NONE) {
        comms::send_event(t, deadman, stepper::position_steps());
    }
    react(t);

    // 6. Comms (dispatcher) and module emit().
    dispatch::Context ctx;
    ctx.homed = homed;
    ctx.position_valid = motion_controller::position_valid();
    ctx.force_valid = force.valid;
    ctx.force_mN = force.mN;
    ctx.now_us = t;
    ctx.position_steps = stepper::position_steps();
    // The dispatcher re-evaluates RESET's cause-cleared from this with nothing suppressed,
    // so the stage-1 relief suppression can't leak into a same-pass RESET (reviewer B).
    ctx.safety = in;
    ctx.self_test = &self_test_latch;
    comms::poll(t, dispatcher, ctx, sm, stepper::position_steps());  // owns its own EVENTs
    react(t);  // comms dispatches one command per pass, so this sees every transition

    uint8_t flags = 0;
    if (motion_controller::position_valid()) flags |= proto::TELEM_POSITION_VALID;
    if (homed) flags |= proto::TELEM_HOMED;
    if (dispatcher.coupler_confirmed()) flags |= proto::TELEM_COUPLER_CONFIRMED;
    const comms::Status status{sm.state(),
                               sm.lock_owner(),
                               inhibit_mask,
                               sm.state() == mod::CoreState::FAULT ? dispatcher.fault_cause()
                                                                   : proto::EventType::NONE,
                               stepper::position_steps(),
                               force.valid,
                               force.raw_counts,
                               force.mN,
                               force.filtered_mN,
                               flags,
                               0,  // cycle_index: no cycle executor yet
                               capabilities(),
                               static_cast<uint8_t>(dispatcher.profile())};
    comms::service(t, status);
    for (uint8_t i = 0; i < mod::MODULE_COUNT; ++i) {
        if (mod::MODULES[i] != nullptr && mod::MODULES[i]->emit != nullptr) {
            mod::MODULES[i]->emit();
        }
    }

    // 7. SD: STUB, no SD log driver yet (skill 9).
    // 8. Panel and OLED: STUB, no panel/OLED drivers yet (skill 10). Note the panel STOP
    //    button is therefore not read.
    // 9. Watchdog feed: STUB, no watchdog driver yet (skill 4 rule 5).
}
