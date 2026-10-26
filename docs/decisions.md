# Firmware decisions log

Newest first. Each entry: date, decision, reason, status. Anything marked PROPOSED is not approved; ask Mathew before acting on it.

## 2026-10-26

### Panel/LED/guard pins finalized (issue #8, part 1 of 2)
Mathew delegated the pin choice ("you can choose the appropriate pins, i can move the pins around with no issue"). Checked the existing PROVISIONAL assignments (GUARD_SENSE 28, SYNC_LED 29, panel buttons 30-35, FAULT_LED 36) against the Teensy 4.1 datasheet pinout: all are plain GPIO in this block, none shares a pin with the reserved continuity-module pins (16, 17, 20-26), the I2C0 bus (18, 19), or any onboard peripheral (SD/QSPI/Ethernet/USB are on separate, non-overlapping pins). No conflicts found, so the assignments are kept exactly as they were and marked fixed rather than moved. **Approved.**

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
