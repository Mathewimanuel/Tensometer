# Firmware decisions log

Newest first. Each entry: date, decision, reason, status. Anything marked PROPOSED is not approved; ask Mathew before acting on it.

## 2026-10-14

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

## Proposed, awaiting approval

- **Motion rate update:** 1 kHz velocity planner in the loop sets the step period; the step ISR holds `steps_remaining` and stops exactly on target. Per-step interval (AVR446) and phase-accumulator methods rejected.
- **HX711 critical sections:** replace "every ISR < 20 us" with interrupts disabled only around each SCK-high phase (~1.5 us per bit), because USB and SDIO library ISRs cannot be guaranteed under 20 us.
