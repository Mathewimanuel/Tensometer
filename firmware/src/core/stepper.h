// Step pulse generator and position tracker (skill sections 2.1, 3.2, 3.4; issue #9). The
// only hardware-timed motion primitive in the firmware: an IntervalTimer-driven ISR that
// asserts STEP, holds the pulse for cfg::STEP_PULSE_US (a busy-wait -- the one place in the
// whole firmware allowed one, skill 3.2), releases, updates position, and stops exactly on
// steps_remaining rather than waiting for the next 1 kHz main-loop pass to notice it
// overshot (skill 3.2, decisions.md 2026-10-15).
//
// This file only generates pulses already decided for it. The main loop (not here) runs
// the trapezoidal planner (lib/motion_profile) and calls set_motion() every tick with the
// rate and remaining distance it computes.
//
// Pin setup (pinMode, idle levels) is main.cpp's drive_outputs_safe()/configure_inputs()
// job, done once at the very top of setup() before anything else runs (issue #18's safety
// gap note applies to that code, not this file). This module does not touch pins at static
// init or on its own; enable()/disable() are the only functions that write ENA, and
// set_motion() the only one that writes STEP/DIR. All internal state defaults to "stopped,
// disabled, position 0", matching the pin levels drive_outputs_safe() already leaves them
// in -- there is no window where this module's state and the physical pins disagree.
//
// TODO(verify) / open, flagged for firmware-reviewer and the bench (not resolved here):
//   - ENA/STEP polarity (skill 2.1: Teensy LOW -> opto ON -> driver DISABLED; confirmed no
//     buffer chip exists, issue #18). enable() drives ENA HIGH per the skill's own wording
//     ("motor_enable() drives ENA HIGH"); unverified on this specific board.
//   - IntervalTimer vs. the HX711 DOUT-edge ISR priority ordering is NOT set here. The step
//     ISR's ~5 us busy wait must not block the HX711 edge ISR (skill 2.3, <2 us budget) or
//     vice versa; needs an explicit NVIC priority check on the bench before trusting both
//     running together (folds into issue #18's bring-up, or a new issue once #10 exists).
#pragma once

#include <stdint.h>

namespace stepper {

// Named after the limit switches (skill 2.6: LIM_NEAR = stationary-jaw end, minimum gap;
// LIM_FAR = motor end, maximum gap; homing targets LIM_FAR) rather than an invented
// "extend/retract" convention, since this file does not know which DIR pin level drives
// which physical direction -- see the DIR polarity TODO(verify) above.
enum class Dir : uint8_t { TOWARD_LIM_FAR, TOWARD_LIM_NEAR };

void enable();   // ENA HIGH (skill 2.1 wording). Does not itself start any motion.
void disable();  // Halts stepping immediately, then ENA LOW.
bool enabled();

// Position truth: signed step count, positive counts TOWARD_LIM_FAR steps, negative counts
// TOWARD_LIM_NEAR. Safe to read from the main loop at any time; the ISR only ever advances
// it by one step per pulse. Zero is wherever the counter happened to be at boot or the last
// reset -- homing (not implemented here) is what gives it a real, calibrated meaning.
int32_t position_steps();

bool moving();

// Starts or updates a move in direction `dir` at `rate_hz`, with `steps_remaining` steps
// left before this move's target (motion::UNBOUNDED_STEPS for an open-ended jog/preload
// move). Call once per main-loop tick, after motion::Planner::tick() has produced the new
// rate_hz. Returns false and changes nothing if: the driver is not enabled() (stepping
// while disabled would desync position_steps() from the real, unpowered motor); `dir`
// differs from the direction already in progress (a reversal must stop first --
// rate_hz 0 -- and start a fresh move, so DIR_SETUP_US is always honoured); or `rate_hz`
// exceeds cfg::STEP_RATE_MAX_HZ (the caller's planner/profile should never produce this;
// treat a false return here as a bug upstream, not something to silently clamp).
//
// rate_hz == 0 stops stepping (equivalent to stop_immediate()) and is always accepted.
//
// Starting a move from a stopped state applies DIR_SETUP_US as a one-time busy-wait here
// (main-loop context, not the ISR) before the first step -- negligible against the 1 ms
// loop budget and only paid once per move, not once per tick.
bool set_motion(Dir dir, uint32_t rate_hz, uint32_t steps_remaining);

// Immediate stop (skill 3.4 IMMEDIATE stop type): halts stepping right away, independent
// of steps_remaining. Does not touch ENA -- the caller decides disable()/FAULT handling
// separately (skill 3.4: motor stays enabled in HOLD, BREAK_HOLD and FAULT unless e-stop).
void stop_immediate();

}  // namespace stepper
