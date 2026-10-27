# Firmware decisions log

Newest first. Each entry: date, decision, reason, status. Anything marked PROPOSED is not approved; ask Mathew before acting on it.

## 2026-10-27

### `lib/core_safety` added: first slice of #11, two real gaps found and fixed
First code for issue #11 (safety module), scoped deliberately narrow: just the two rules blocking `main.cpp` wiring of #9's pieces (limit-switch-vs-homing carve-out, inhibit-mask-vs-motion). Everything else in skill section 4 (cutoff, e-stop, heartbeat, stale sensor, watchdog, slip-suspect, GUARD_SENSE, soft limits) is explicitly out of scope for this slice.

**`firmware-reviewer`: BLOCK, two CRITICAL findings, one MAJOR. All resolved:**

1. **Fixed directly (not a judgment call -- the first draft just didn't match the already-agreed design).** `Inputs.motion_requested` was a one-tick pulse at dispatch, so the inhibit-mask check only fired at the instant a command was issued, missing the exact scenario the 2026-10-26 state-machine decision said this check exists to catch: a module asserting an inhibit bit mid-RUN, well after dispatch. Renamed to `motion_active` with a caller contract that it stays true for the whole duration of any motion (including ARM's `arming()`, which `core_state` alone can't represent). New test for the mid-run case.
2. **Narrowed after review, with Mathew's input -- the original decision (2026-10-26) was too broad.** The first draft suspended the *whole* limit-switch rule (both LIM_NEAR and LIM_FAR) for all of `HOMING`, per the earlier "homing owns its own safety window entirely" decision. Review found `lib/homing`'s `BACK_OFF` phase drives *toward* LIM_NEAR using only a step count (`back_off_steps`, itself still unset) to detect overtravel -- with the global rule suspended, nothing anywhere would catch a LIM_NEAR overtravel during `BACK_OFF`. **Approved narrowing:** LIM_NEAR is never suspended, even during homing; only LIM_FAR gets the homing carve-out. Turned out simpler to implement than the broad version -- no homing-phase granularity needed in `core_safety` at all, just `core_state != HOMING`.
3. **Fixed with Mathew's input -- required a change to `lib/homing`, not just `core_safety`.** `SLOW_REAPPROACH` used to go straight to `DONE` the instant LIM_FAR tripped, meaning every successful homing cycle finished sitting on an actively-tripped switch. Since the LIM_FAR carve-out only applies while `core_state == HOMING`, the very next core-safety tick after `HOMING_DONE` moves state to IDLE would see the still-tripped switch with no carve-out left -- an near-guaranteed self-fault after every homing cycle, with no implemented recovery path (no FAULT-state jog event exists). **Approved fix:** added a fifth `homing::Phase::RELEASE` between `SLOW_REAPPROACH` and `DONE` -- backs off (`TOWARD_LIM_NEAR`, at `slow_hz`) until LIM_FAR reads inactive again, *then* reports `DONE`. This moves where "zero" physically ends up: the switch's release point, not its trip point -- a small, real, deliberate change to homing's physical behavior, not just an internal bookkeeping fix. 4 new `test_homing` cases cover the new phase; existing tests that reached `DONE` directly from a single LIM_FAR trip were updated to go through `RELEASE` first.

**Caught by `-Werror`, not by review:** adding `Phase::RELEASE` to the enum without updating `motion_controller.cpp`'s `sequencing()` switch would have silently stopped `motion_controller` from ever advancing `homing_seq` past `SLOW_REAPPROACH` (treating `RELEASE` as "not homing" and leaving the stepper running in whatever state it was in) -- `-Wswitch` on the Teensy build caught the missing case as a compile error before it could ship. A reminder that `homing::Phase`'s exhaustive switches live in multiple files now (`homing.cpp`, `motion_controller.cpp`) and need updating together.

**Re-review: OK.** All three fixes independently traced against the actual code (not this summary) and confirmed correct, including walking through the exact moment `HOMING_DONE` fires to confirm LIM_FAR is genuinely inactive by then -- the self-fault window is closed, not just moved later. Confirmed the new `SLOW_REAPPROACH -> RELEASE` direction reversal is caught correctly by the existing (phase-agnostic) `motion_guard::DirectionTracker` stop-before-reversal logic, same as the original bug's transitions. One MINOR note, addressed: `test_motion_guard`'s `test_full_homing_direction_sequence` -- the designated regression guard for this exact hazard class -- hadn't been extended to cover the new RELEASE transition. Extended it (now covers all five phases' direction changes).

126 native tests, cppcheck clean, both Teensy builds succeed.

### `lib/core_safety` slice 2: five more section-4 rules, two MAJOR findings fixed
Added e-stop, module-fault aggregation, GUARD_SENSE, soft travel limit, strain limit, and heartbeat-stale to `evaluate()`. Still out of scope: force cutoff, HX711-stale, slip-suspect (need #10) and watchdog-at-boot (a one-shot check, not a per-tick rule).

`firmware-reviewer`: CHANGES REQUESTED first round, fixed and re-confirmed OK:
- **GUARD_SENSE's `cfg::GUARD_INTERLOCK_FITTED` gate** was pushed to an untested caller as one pre-folded bool, inconsistent with how the heartbeat gate works. Fixed: `evaluate()` now applies the AND itself on two raw inputs (`guard_sense_active`, `run_to_break_armed`). Note: the "fires when fitted" branch has no test coverage and can't get any until the flag flips (it's compile-time false) -- add that test the day it changes.
- **"Soft travel limit" and "strain limit"** were one bool/`FaultReason`, losing which one actually tripped (skill rule 9 requires a specific logged cause). Split into two of each.
- Priority reordered: `GUARD_SENSE_OPEN` now checked before `MODULE_FAULT` (a bypassed guard interlock is more diagnostically relevant than a generic module fault).

Two things Mathew confirmed directly, not guessed: "armed" in the GUARD_SENSE rule means a test session is active (READY+), not `core_sm::arming()`'s narrower flag; and `HEARTBEAT_LOST` stays checked before `MOTION_WHILE_INHIBITED` (a stale heartbeat is its own serious concern regardless of what else is true that tick).

138 native tests, cppcheck clean, both Teensy builds succeed. Not yet wired into main.cpp -- `core_safety` isn't in either Teensy build's dependency graph yet since nothing in `src/` calls it.

## 2026-10-26

### Issue #9 closed; #11 unblocked
All of #9's stated scope landed and reviewed clean (entries below, newest first): state machine, motion planner (pre-existing), step ISR, homing sequencer, the `position_valid`-owning glue, and the CRITICAL bug found in it along the way, fixed and extracted into tested `lib/motion_guard`. 111 native tests, final `firmware-reviewer` round OK, CI green. Closed with a comment linking the commits (`2b80bc8`..`7fb8b41`). `#11` (safety module) had its `blocked` label removed per CLAUDE.md's rule and got a comment pointing at the two design decisions immediately below that it needs to implement. `main.cpp` wiring itself is not in #9's scope and is not done -- it waits on #11 existing, since the limit-switch carve-out (item 1 below) has to live somewhere before homing can run safely under a real core-safety loop.

### Open questions from the homing bug, resolved by Mathew (issue #9, part 3b)
Three questions raised by the direction-reversal bug and its reviews (see the entry below) were put to Mathew directly:

1. **Limit-switch carve-out for homing: Approved.** "Homing owns its own safety window entirely" -- while any homing phase is active, the core-safety loop's global rule (skill 4: any limit-switch trip -> immediate stop + FAULT) is suspended, and `homing::Sequencer` (which already watches LIM_FAR every tick) is trusted as the sole authority until `HOMING_DONE` or an explicit abort/fault from elsewhere (e.g. the force guard). This is a design note for whoever builds #11 (still blocked on #9) -- no code changes yet, since #11 doesn't exist. Note for that future work: this suspends the *whole* rule, not just LIM_FAR specifically, so a LIM_NEAR trip during homing (unexpected -- homing never approaches that end) would also go unflagged by the global rule under this reading, since `homing::Sequencer` never watches LIM_NEAR at all. Worth re-confirming this is intended when #11 is actually written, not just inferred from this note.
2. **Stop-on-every-start_homing(): Approved**, as already implemented. Every `start_homing()` forces a hard stop-and-reset on its first tick regardless of whether the stepper happens to already be moving the correct way (e.g. mid-jog toward LIM_FAR when HOME is pressed). Conservative by design; no code change needed.
3. **Extract the direction-reversal decision into `lib/`: Approved, done.** See `lib/motion_guard` below.

### `lib/motion_guard` added: the CRITICAL bug's fix, now natively tested (issue #9, part 3a)
Both reviews of the direction-reversal fix (previous entry) flagged the same gap: the fix lived entirely in `motion_controller.cpp`, a `src/` hardware-glue file with no native test suite, so a future regression (e.g. reordering the stop before the direction is recorded) would pass CI silently. Mathew confirmed extracting it. `lib/motion_guard`'s `DirectionTracker` now owns exactly the decision that was missing: given the last direction actually commanded and whether the stepper is still moving, does a newly requested direction need a stop-first before it's safe to issue. `motion_controller.cpp` is now a thin caller of it (`needs_stop_before()` / `record_commanded()` / `reset()`), same pattern as `homing`/`state_machine`/`messages`. 7 new native tests, including one that replays the exact FAST_APPROACH -> BACK_OFF -> SLOW_REAPPROACH sequence that produced the original bug. 111 native tests total, cppcheck clean, both Teensy builds succeed.

### Step ISR and homing sequencer added (issue #9, part 2)
`src/core/stepper.*` adds the step-pulse generator and position tracker named in #9's scope: an `IntervalTimer`-driven ISR that pulses STEP, busy-waits `STEP_PULSE_US` (the one allowed busy-wait, skill 3.2), updates position, and stops exactly on `steps_remaining`. `lib/homing` adds a pure-logic sequencer for the four homing phases (fast approach / back off / slow re-approach / done, skill 3.4), driven by the debounced LIM_FAR switch and the stepper's position -- 12 native tests.

Deliberately out of scope for both, left to whatever core-safety/main-loop code eventually calls them (not yet built -- eventually #9's remaining wiring, or #11):
- The `|force| < 2 N` homing precondition and `|force| > 5 N` mid-homing abort. Neither file knows about force; the caller is expected to monitor it continuously and trip `SAFETY_TRIP` on the state machine itself, the same resolution already applied to the inhibit-mask question (see the state-machine entry below).
- "Set zero" on homing completion: `stepper.h` has no position-reset function yet. Needs adding when main.cpp wiring happens.
- ENA/STEP polarity and the Dir-to-DIR-pin-level mapping are flagged TODO(verify) in the code, not assumed.

**`firmware-reviewer` on the stepper driver: CHANGES REQUESTED, both MAJOR findings fixed:**
- **Fixed.** `set_motion()`'s "continuing an existing move" branch had a time-of-check-to-time-of-use race: it read `moving_` once, then wrote `steps_remaining_` and called `timer.update()` without re-checking, so if `step_isr()` finished the move in that window (`steps_remaining_` hits 0, `timer.end()`, `moving_ = false`), `timer.update()` would silently no-op against an already-ended timer while `set_motion()` still reported success. Fixed: that branch now re-checks `moving_` inside a brief `noInterrupts()`/`interrupts()` section, and falls through to starting a fresh move if it finished in the meantime (factored into a new `start_fresh_move()` helper, used by both paths).
- **Fixed.** Skill 3.2 requires the step timer to be the *highest*-priority interrupt in the system; nothing called `IntervalTimer::priority()`, so it ran at the Teensy default (128). `start_fresh_move()` now calls `timer.priority(0)` every time it arms the timer (cheap and idempotent; the stored priority value persists across `begin()`/`end()` cycles in Teensyduino's implementation, so this is defensive, not load-bearing). **TODO(verify):** confirmed as a placeholder, not bench-verified -- there's no competing ISR (HX711, limit/e-stop) to test against yet; recheck once #10 lands.
- **Fixed (MINOR).** `dir_` is now `volatile` like the other ISR-shared fields, removing an undocumented reliance on `timer.begin()` acting as a compiler barrier.
- **Open question, not acted on.** `set_motion()`'s accept/reject decision logic (disabled -> reject, direction-change-while-moving -> reject, rate > cap -> reject) has no native tests, since it lives in a `src/` hardware driver file, not `lib/` (consistent with skill 12's layout rule). The reviewer asked whether this decision logic should be factored into a small, natively-tested `lib/` function with `stepper.cpp` left as a thin executor of whatever it decides. Not done here -- ask Mathew before restructuring.
- **Open, bench-only.** ISR budget (busy-wait + position/steps_remaining update, worst case at the 20 kHz cap) is argued safe but not measured. Confirm during bring-up step 3 (skill 13) with a cycle-counter or spare-pin toggle, not just the planned oscilloscope pulse/off-time check.

Re-verified after fixes: `pio test -e native` 97/97, cppcheck clean, `pio run -e teensy41` and `-e teensy41_sim` both SUCCESS with `-Werror` on our sources, memory budget OK on both.

### Homing-to-stepper glue added, then a CRITICAL bug found and fixed (issue #9, part 3)
`src/core/motion_controller.cpp` is the new glue: owns `position_valid`, drives `homing::Sequencer`'s phase through `motion::Planner` into `stepper::set_motion()`, takes `force_within_homing_guard` as a caller-supplied bool per tick (no HX711 driver yet, #10, so this file has no force reading of its own), and reports what happened via a `TickResult` enum rather than calling `core_sm::StateMachine` directly (one owner per piece of state, skill 3.1). `lib/homing` gained `steps_remaining()`/`accel_hz_per_s()` so the planner can actually drive BACK_OFF's fixed distance.

**`firmware-reviewer` on the first draft: BLOCK, one CRITICAL finding.** The FAST_APPROACH -> BACK_OFF transition reverses direction (TOWARD_LIM_FAR -> TOWARD_LIM_NEAR) on the exact tick the limit switch trips, while the stepper is still physically cruising the old way. `stepper::set_motion()` rejects a direction change while moving by design (to always honour `DIR_SETUP_US`) -- the first draft never checked that return value, so the rejection was silent: the stepper kept driving TOWARD_LIM_FAR (further into the switch) forever, while `homing::Sequencer`'s distance tracking is magnitude-only (direction-blind), so it eventually declared BACK_OFF "complete" purely from that wrong-direction travel, advanced to SLOW_REAPPROACH (which happened to match the stepper's actual direction, so it was accepted), saw the still-tripped switch, and declared `DONE` -- reporting a false, successful home with position zeroed at the wrong, overtravelled location. Every other safety check (soft limits, L0 move, RUN precondition) would have trusted that corrupted zero completely. Not a corner case: it is the normal way every homing cycle ends FAST_APPROACH.

**Fixed:**
- `motion_controller.cpp` now tracks the last direction it actually commanded. Before calling `set_motion()`, if the stepper is still `moving()` and the newly-requested direction differs (or none has been commanded yet this cycle), it calls `stepper::stop_immediate()` **and** `planner.reset()` first, then starts the new-direction move fresh. `planner.reset()` matters as much as the stop: `motion::Planner` carries its own `rate_hz_` across ticks, which would otherwise still read near the old cruise speed even though the stepper had just been physically halted, corrupting the next tick's accel/decel decision.
- `set_motion()`'s return value is now checked; a persistent rejection (should not happen given the fix above, but not assumed) calls a new `stop_and_abort()` helper and returns `ABORTED` rather than silently claiming `IN_PROGRESS`.
- Added `homing::Sequencer::abort()` (resets to IDLE only). Both the force-guard-false path and the persistent-rejection path now call it via `stop_and_abort()`, so a half-finished cycle can never be silently resumed by a stray `tick()` without an intervening `start_homing()` -- matches skill 3.4's "Abort with FAULT" wording (a hard stop, not something to casually continue past). 3 new `test_homing` cases.
- `TickResult::DONE` was overloaded for both "just finished this tick" and "nothing in progress, no-op". Split into a distinct `NOT_HOMING` value so a caller that fires `HOMING_DONE` on `DONE` doesn't fire it every idle tick too.

**Re-review: CHANGES REQUESTED, fix confirmed correct.** The reviewer independently traced all three phase transitions plus the first-start case against the actual code (not the summary) and confirmed the stop-and-reset logic is correct and complete for what it targets, and that `planner.reset()` alongside every stop is the right call, not a cover-up -- without it, the next tick would ramp the stepper to near-full cruise speed with zero ramp-up (worse than the original bug: silent stall/skipped steps, no encoder to catch it). Re-verified independently: 104/104 native tests, both Teensy builds, cppcheck clean.

**Open questions raised, not decided here (ask before acting on any of these):**
1. **MAJOR-flagged gap.** The fix lives entirely in `motion_controller.cpp`, a `src/` hardware-glue file with no native test suite by project convention -- so nothing would catch a future regression (e.g. someone reordering the direction-tracking write) via CI. The reviewer suggests extracting the stop-and-reset *decision* (given moving/have-last-dir/last-dir/requested-dir, should we stop first?) into a natively-tested `lib/` function, leaving `motion_controller.cpp` a thin executor. Same open question already on record for `stepper::set_motion()`'s own decision logic -- both now point the same way. Not restructured here per CLAUDE.md ("ask before deciding anything touching hardware, pins, safety behaviour").
2. Every `start_homing()` forces a hard stop-and-reset on its first tick even if the stepper happens to already be moving the correct way (e.g. mid-jog toward LIM_FAR when HOME is pressed). Believed intentional (conservative) by both reviews; confirm with Mathew.
3. `homing::Params.fast_hz`/`slow_hz` aren't validated against `cfg::STEP_RATE_MAX_HZ` anywhere. A misconfigured value above the cap wouldn't fail at homing start -- it would ramp fine until hitting the cap, then `stop_and_abort()` repeatedly mid-approach. Low severity while speeds are still OPEN and unset, but worth a bounds check once real numbers land.
4. **Still blocking main.cpp wiring**, raised by both reviews: the core-safety loop's skill-4 rule ("limit switch open -> immediate stop + FAULT latch") has no carve-out for the expected, single LIM_FAR trip during FAST_APPROACH. `motion_controller`/`homing` must not be wired into `main.cpp` until #11's design answers how it tells "expected homing trip, hand to BACK_OFF" apart from "real overtravel, FAULT".

Minor/pre-existing, not a new bug: `motion_controller::tick()` reads `stepper::position_steps()` once and reuses that snapshot for both the phase-transition decision and the back-off baseline; the step ISR can in principle advance position between that read and the later `stop_immediate()` call in the same tick. At 0.5 um/step against a back-off distance of hundreds of steps this is very likely immaterial, but unverified.

Not yet wired into main.cpp -- planner, stepper, homing, motion_controller and the state machine all exist now but nothing calls them together yet.

### Core state machine: interpretive calls resolved by Mathew (issue #9, part 1)
`lib/state_machine` implements skill section 5's transition table as pure logic (26 native tests, one per table row plus rejections). The table left a few places underspecified; `firmware-reviewer` reviewed the first draft (CHANGES REQUESTED) and Mathew then resolved the open questions:

1. **ARM's "moved to L0" step.** **Approved** (2026-10-26). L0 is the test's gauge length (sent by the PC in the config), not the fixed home position. No dedicated visible state exists for "moving there" in the 12-state list; modeled as an internal `arming_` flag: ARM (guards pass) keeps `state()` at IDLE and sets `arming()` true; the caller starts the move-to-L0 and signals completion with a new `L0_REACHED` event, which only advances IDLE -> READY while `arming()` is true. While `arming_` is true, every other event is rejected except `L0_REACHED`, a new `ARM_CANCEL` event, and `SAFETY_TRIP`. `ARM_CANCEL` lets the operator back out of a pending ARM with no fault (confirmed needed by Mathew) -- the caller stops the move-to-L0 motion itself; state returns to IDLE with `arming()` false.
2. **"RUN / CYCLE | target reached / cycles done | HOLD then READY."** **Approved as a real HOLD** (2026-10-26): a completed segment moves RUN/CYCLE -> HOLD (lock: PC) the same way PAUSE does, via a new `SEGMENT_DONE` event, but the RESUME target is READY instead of RUN/CYCLE -- so a post-test Bode/routing sweep has the HOLD window to run in before the machine resets for the next config. Reversed from the first draft's direct-to-READY jump after `firmware-reviewer` pushed back on it and Mathew confirmed the Bode-after-test case is needed.
3. **BREAK_HOLD's exit.** **Approved** (2026-10-26): `Event::RESET` now also accepts from BREAK_HOLD (not only FAULT), gated by the same `Guards::cause_cleared` guard, moving straight to IDLE. Same event, same guard, no new field.
4. **"Motion requested while an inhibit bit is set" (skill 4, non-negotiable).** **Approved** (2026-10-26): owned by the core-safety loop (main loop step 1, skill 3.3), not this state machine. That loop runs every pass and is expected to call `apply(SAFETY_TRIP, ...)` itself as soon as it sees an inhibit bit set while motion is active or being requested -- for every motion-starting event (`HOME`/`JOG_START`/`TARE_START`/`PRELOAD_START`/`ARM`/`START`), not just the ones this state machine happens to see. This also catches a module inhibiting mid-run, which a check wired only into `apply()` would miss. `RESUME` stays the one exception already in the state machine: a soft NACK via `Guards::module_inhibit_present` (stay in HOLD, try later), not a FAULT, matching the skill's existing RESUME wording. **Not yet implemented** -- the core-safety loop itself doesn't exist yet (#11, blocked on #9); this is the design to build it to when that lands.

Not yet wired into main.cpp -- that's the rest of #9 (step ISR, homing motion, wiring force/limit-switch facts into real Guards, and the core-safety loop above).

**`firmware-reviewer` findings on the first draft, and what was done about each:**

- **Fixed, was a real bug.** While `arming_` was true (ARM accepted, move-to-L0 in flight), the machine still accepted `HOME`/`JOG_START`/`TARE_START`/`PRELOAD_START`/a repeat `ARM`. A later, now-stale `L0_REACHED` from the superseded move would then jump straight to READY from wherever that other action left the carriage, silently breaking "READY implies at L0". Fixed as described in item 1 above. New test `test_arming_blocks_everything_but_l0_reached_and_safety_trip`.
- **Open question -> resolved.** Inhibit-mask ownership: resolved as item 4 above.
- **Independent second opinion -> adopted.** The reviewer's pushback on the direct-to-READY jump (item 2) is what prompted Mathew to confirm the real-HOLD reading.
- **Scope note, no action needed yet.** `Guards` has no field for the `ALLOW_UNHOMED` debug override (skill 4.1) or for the homing/preload force guards (skill 3.4) -- both are deliberately out of scope for this pure-logic slice (no force data reaches this class), but if `ALLOW_UNHOMED` is wanted later it will need a new `Guards` field, not just a caller-side change.

### Panel/LED/guard pins finalized (issue #8, part 1 of 2)
Mathew delegated the pin choice ("you can choose the appropriate pins, i can move the pins around with no issue"). Checked the existing PROVISIONAL assignments (GUARD_SENSE 28, SYNC_LED 29, panel buttons 30-35, FAULT_LED 36) against the Teensy 4.1 datasheet pinout: all are plain GPIO in this block, none shares a pin with the reserved continuity-module pins (16, 17, 20-26), the I2C0 bus (18, 19), or any onboard peripheral (SD/QSPI/Ethernet/USB are on separate, non-overlapping pins). No conflicts found, so the assignments are kept exactly as they were and marked fixed rather than moved. **Approved.**

### Driver interface corrected: no buffer, no reset-safe pulls (confirmed safety gap)
Section 2.1 wrongly documented a 74AHCT244 buffer between the Teensy and the TB6600. Mathew confirmed: no buffer chip exists; Teensy GPIO wires straight to the TB6600's opto inputs. Common-anode @ +5 V confirmed (board says "connect 5V+" on PUL+/DIR+/ENA+). No external current-limiting resistor was added -- relies entirely on whatever resistor the TB6600 board has internal (unknown, TODO(verify), folds into issue #3's bench check).

More importantly: **there is no pull-down resistor on ENA and no pull-up on STEP/DIR anywhere in the build.** The skill previously described these pulls living "on the buffer input" -- with no buffer, they do not exist. This means nothing physically guarantees the motor stays disabled between power-up and `setup()` running, or across a Teensy reset while the TB6600/5V side stays powered. `main.cpp`'s `drive_outputs_safe()` comment claimed this protection existed; it was false and has been corrected to document the gap instead.

**Not resolved.** This needs a physical pull-down added on ENA (toward GND) and pull-ups on STEP/DIR, sized once the TB6600's internal opto resistor value is known (issue #3). Bench bring-up step 2 (section 13, "Reset safety") is expected to fail until this is fixed -- do not proceed past that step, and do not trust "motor disabled on reset" until it passes. Treat this as safety-blocking for #9 (motion/step ISR) bring-up, though it does not block writing/reviewing the ISR code itself. Tracked in **issue #18**.

### Guard interlock: not fitted (issue #8, part 2 of 2)
Confirmed by Mathew: no physical guard/interlock on the machine. `GUARD_SENSE` (pin 28) stays reserved and wired to nothing. Added `cfg::GUARD_INTERLOCK_FITTED = false` in `config.h`; the safety module (issue #11, still blocked by #9) must gate the GUARD_SENSE rule (skill section 4) on this flag rather than assuming the sensor is present. **Approved.** Issue #8 fully resolved.

## 2026-10-14

### Agent guard rails and stricter CI
Flash-blocking hook plus permission deny rules; `firmware-reviewer` subagent for independent review; CI adds cppcheck, `-Werror` on project sources, and a memory budget (RAM1 >= 64 kB, RAM2 >= 256 kB free). Teensy envs set `test_ignore = *` so `pio test` can never upload. **Approved.**

### Drive hardware corrected (skill rev 4)
Skill revs 1 to 3 used an outdated handover (MG10 10:1, DM542, 36 V). Real parts: DTGN PX57 5:1, TB6600, 24 V. Derived numbers recomputed: 2000 steps/mm, 6.35 kN/N.m at the nut, 16.7 kHz at 500 mm/min, driver-minimum force ~0.9 to 1.3 kN. Step-rate cap set to 20 kHz and step pulse to 5 us until the TB6600 opto is identified. **Approved by Mathew:** pending (correction, not a new choice).

### Stage 1 agentic setup
Repository with PlatformIO, native unit tests (Unity) and GitHub Actions CI. Claude Code works from `CLAUDE.md` and the in-repo skill. Stage 2 (hardware-in-the-loop rig with a Pico emulating HX711, limits, step counting) deferred. Agents never flash or drive the real powered machine. **Approved.**

### Phased build
Phase 1 = stress-strain core. Continuity and routing are optional modules behind a fixed interface, added later without changing core files. A `sim_continuity` module exercises the seams from phase 1. **Approved.**

### No encoder
Step count is position truth. Safeguards: slip-suspect fault, `position_valid` flag, coupler confirmation at profile select. **Approved.**

### Bode two-wire
Scope channels are ground-referenced, so Bode is two-wire with PC-side open/short compensation. Accurate DC resistance comes from the four-wire continuity channel. **Approved.**

### Heartbeat
PC heartbeat loss > 1 s while moving -> FAULT. **Approved.**

### Microstepping
Fixed at 1/8. **Approved.** (With the 5:1 gearbox, 1/16 would only need 33 kHz at 500 mm/min; revisit only if low-speed vibration shows in the force signal.)

### Continuity monitor design
4 channels, four-wire via tail terminal blocks, ranges 330 R / 3.3 k / 33 k, separate measured excitation, DMA ADC 16 kHz averaged to 2 kHz, relative break thresholds (k x R0). **Approved** (phase 2 hardware).

### Toolchain and protocol
PlatformIO; own timer-interrupt step generator; COBS + CRC-16/CCITT-FALSE over USB with an ASCII debug mode. **Approved.**

## 2026-10-15

### Motion rate update
1 kHz velocity planner in the loop sets the step period; the step ISR holds `steps_remaining` (or runs open-ended for jog) and stops exactly on target. Per-step interval (AVR446) and phase-accumulator methods rejected. One planner serves all motion sources (test ramp, jog, preload, homing); the difference between them is only the stop condition (position reached / button released / force threshold / fault), reusing the existing `CONTROLLED` vs `IMMEDIATE` stop-type split (skill 3.4). **Approved.**

### Jog and homing speeds (new constants, values open)
Unloaded jog speed set equal to homing fast-approach speed (single shared constant). Both still need a concrete number — proposed starting point 200 mm/min fast/jog, 20 mm/min homing slow re-approach, `// TODO(verify)` pending bench tuning (skill 13, bring-up step 4). Loaded jog cap stays the already-decided 10 mm/min when \|force\| > 5% of profile cutoff. **Direction approved, exact values open.**

### Auto-preload (new feature)
Added as a **separate operator-triggered action**, not folded automatically into the ARM sequence. Reuses the homing-style force-guard pattern (creep at a slow speed, `CONTROLLED` stop when force reaches a threshold) rather than a new motion primitive. Needs: a new `PRELOAD` state (or sub-state) in the core state machine (skill 5), a threshold force and approach speed. **Direction approved (phase 1, separate action). Threshold force and speed still OPEN — pending HX711 noise-floor measurement (skill 13, bring-up step 5) and Mathew's numbers.** Candidate range discussed: ~1-3 N threshold (clear of HX711 noise floor on the 100 kg cell, well under the ~2.5-3 N break load of 36 AWG wire), slow creep ~5-10 mm/min.

## Proposed, awaiting approval

- **HX711 critical sections:** replace "every ISR < 20 us" with interrupts disabled only around each SCK-high phase (~1.5 us per bit), because USB and SDIO library ISRs cannot be guaranteed under 20 us.
