# Firmware decisions log

Newest first. Each entry: date, decision, reason, status. Anything marked PROPOSED is not approved; ask Mathew before acting on it.

## 2026-10-26

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
