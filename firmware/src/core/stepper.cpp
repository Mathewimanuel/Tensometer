#include "stepper.h"

#include <Arduino.h>

#include "config.h"
#include "motion_profile.h"

namespace stepper {

namespace {

IntervalTimer timer;

volatile int32_t position_steps_ = 0;
volatile uint32_t steps_remaining_ = 0;  // motion::UNBOUNDED_STEPS for open-ended moves
volatile bool moving_ = false;
bool enabled_ = false;  // only touched from the main loop (enable/disable/set_motion), not the ISR
// volatile along with the others: read every step by step_isr(), written only while
// stopped by set_motion(). The logic is race-free either way (the ISR never runs while
// moving_ is false, and dir_ only changes in that window), but nothing stops the compiler
// treating a non-volatile store and an ISR read as unrelated without this (found by
// firmware-reviewer).
volatile Dir dir_ = Dir::TOWARD_LIM_FAR;

void step_isr() {
    digitalWriteFast(cfg::pin::STEP, LOW);
    delayMicroseconds(cfg::STEP_PULSE_US);
    digitalWriteFast(cfg::pin::STEP, HIGH);

    position_steps_ += (dir_ == Dir::TOWARD_LIM_FAR) ? 1 : -1;

    if (steps_remaining_ != motion::UNBOUNDED_STEPS) {
        --steps_remaining_;
        if (steps_remaining_ == 0) {
            timer.end();
            moving_ = false;
        }
    }
}

// Starts a fresh move from a stopped state: sets DIR (TODO(verify) polarity, see header),
// waits DIR_SETUP_US once, arms the timer, and (re-)asserts the priority skill 3.2 requires
// for this ISR (see the header's priority TODO(verify) -- the stored value survives across
// begin()/end() cycles, so re-asserting it here is cheap insurance, not a new state to track).
void start_fresh_move(Dir dir, uint32_t period_us, uint32_t steps_remaining) {
    dir_ = dir;
    // DIR pin level <-> physical direction is unverified (TODO(verify), header note): HIGH
    // is an arbitrary placeholder for TOWARD_LIM_FAR until checked on the bench.
    digitalWriteFast(cfg::pin::DIR, dir == Dir::TOWARD_LIM_FAR ? HIGH : LOW);
    delayMicroseconds(cfg::DIR_SETUP_US);  // one-time per move, not per tick
    steps_remaining_ = steps_remaining;
    moving_ = true;
    timer.begin(step_isr, period_us);
    // Highest priority (skill 3.2: the step ISR must not be held off, including by its own
    // 5 us busy-wait stalling the future HX711 DOUT-edge ISR). TODO(verify) once that ISR
    // (and the limit/e-stop one) exist: confirm this ordering on the bench (issue #18's
    // bring-up, or a new issue), per firmware-reviewer.
    timer.priority(0);
}

}  // namespace

void enable() {
    digitalWriteFast(cfg::pin::ENA, HIGH);
    enabled_ = true;
}

void disable() {
    stop_immediate();
    digitalWriteFast(cfg::pin::ENA, LOW);
    enabled_ = false;
}

bool enabled() { return enabled_; }

int32_t position_steps() { return position_steps_; }

bool moving() { return moving_; }

bool set_motion(Dir dir, uint32_t rate_hz, uint32_t steps_remaining) {
    if (rate_hz == 0) {
        stop_immediate();
        return true;
    }
    if (!enabled_ || rate_hz > cfg::STEP_RATE_MAX_HZ) {
        return false;
    }
    if (moving_ && dir != dir_) {
        return false;  // reversal must stop first; DIR must not change mid-pulse-train
    }

    const uint32_t period_us = 1000000u / rate_hz;

    if (!moving_) {
        start_fresh_move(dir, period_us, steps_remaining);
        return true;
    }

    // Already moving: step_isr() can asynchronously finish the move (steps_remaining_
    // hits 0, timer.end(), moving_ = false) at any point, including between the moving_
    // check above and here. Re-check under a brief critical section so steps_remaining_
    // and timer.update() are only ever applied while the timer is confirmed still
    // running -- otherwise timer.update() would silently no-op against an already-ended
    // timer while this function still reported success (found by firmware-reviewer).
    noInterrupts();
    const bool still_moving = moving_;
    if (still_moving) {
        steps_remaining_ = steps_remaining;
        timer.update(period_us);
    }
    interrupts();

    // The move finished in that window: this request is still legitimate (the planner
    // handed us a real rate/distance), so start it fresh rather than silently dropping it.
    if (!still_moving) {
        start_fresh_move(dir, period_us, steps_remaining);
    }
    return true;
}

void stop_immediate() {
    timer.end();
    moving_ = false;
    steps_remaining_ = 0;
}

}  // namespace stepper
