// Homing orchestration (skill 3.4; issue #9, part 3): the thin glue that drives
// lib/homing's sequencer through lib/motion_profile's trapezoidal planner and into
// stepper.h, and owns position_valid. Pure decisions (what phase, what rate, when done)
// all come from homing::Sequencer and motion::Planner, already natively tested; this file
// only plumbs their outputs into the stepper and reacts to the result -- no new motion
// logic lives here.
//
// Deliberately out of scope, left to whatever calls this (not yet built -- the rest of #9
// / main.cpp wiring):
//   - Reading and debouncing the LIM_FAR pin, and reading force. Both are passed in by the
//     caller every tick(); this file has no pin access and no HX711 driver to read (#10,
//     blocked). `force_within_homing_guard` is the caller's answer to skill 3.4's
//     |force| < 2 N precondition / |force| > 5 N mid-homing abort -- never assumed true.
//   - Firing core_sm::StateMachine events (HOMING_DONE / SAFETY_TRIP). This file reports
//     what happened via TickResult; translating that into a state-machine event is the
//     caller's job, so there is still exactly one place that calls StateMachine::apply()
//     (skill 3.1: one owner per piece of state).
//   - Clearing position_valid on e-stop or a slip-suspect fault -- those sources (the
//     core-safety loop) don't exist yet either; clear_position_valid() is ready for them
//     to call.
//   - Real homing speeds/accel/back-off distance: still OPEN (decisions.md 2026-10-15),
//     supplied by the caller via homing::Params, not invented here.
#pragma once

#include <stdint.h>

#include "homing.h"

namespace motion_controller {

// True once a homing cycle has completed successfully (since boot, or since the last
// clear_position_valid()). Starts false: position truth is never assumed good, it must be
// earned by a real homing cycle (skill 3.4).
bool position_valid();

// Clears position_valid. For the core-safety loop to call on e-stop / slip-suspect fault /
// any fault that may have moved the screw (skill 3.4) -- not called from within this file.
void clear_position_valid();

bool homing_in_progress();

// Begins a homing cycle. `params` are caller-supplied (see the header note on OPEN
// values). Safe to call again to restart, same as homing::Sequencer::start().
void start_homing(const homing::Params& params);

enum class TickResult : uint8_t {
    NOT_HOMING,   // no cycle in progress; this call was a no-op (distinct from DONE so a
                  // caller firing HOMING_DONE on DONE doesn't fire it every idle tick too)
    IN_PROGRESS,  // still homing; the stepper has been (re)commanded this tick
    DONE,         // just finished this tick: stepper stopped, position zeroed,
                  // position_valid() true
    ABORTED,      // either force_within_homing_guard was false this tick, or the stepper
                  // driver refused to execute the requested move (not enabled(), or
                  // persistently rejected after a stop-and-retry) -- stepper stopped
                  // immediately, position_valid() left false, homing_seq reset to IDLE via
                  // homing::Sequencer::abort() (skill 3.4: "Abort with FAULT" -- only a
                  // fresh start_homing() can resume, a stray tick() afterward cannot)
};

// Advance one tick (nominally 1 kHz, matching the motion planner's expected rate; `dt_us`
// is the actual elapsed time since the last tick(), for the planner's own ramp math --
// not assumed to be exactly 1000). `lim_far_active` is the debounced LIM_FAR switch state;
// `force_within_homing_guard` is skill 3.4's force guard, computed by the caller. A tick()
// with no homing in progress (homing_in_progress() false) is a no-op returning NOT_HOMING.
TickResult tick(uint32_t dt_us, bool lim_far_active, bool force_within_homing_guard);

}  // namespace motion_controller
