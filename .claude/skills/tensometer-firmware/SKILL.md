---
name: "tensometer-firmware"
description: "Rules, hardware facts and review checklist for writing or reviewing Teensy 4.1 firmware for Mathew's ball-screw tensometer (motion, load cell, continuity, safety, protocol)."
---

# Tensometer firmware skill

Use this for ANY firmware work on the tensometer: new modules, edits, reviews, debugging, protocol changes, test code. This file is the single source of truth. If code and this file disagree, stop and flag it; do not silently pick one.

Revision 3 (2026-10-14). Rev 2: design review applied (ENA polarity and reset-safe pulls, continuity ranges and separate excitation, DMA ADC, HX711 timestamping, per-segment break peaks, both-break rule, coupler/profile coupling, no encoder with software slip check, heartbeat 1 s, non-blocking SD and OLED, two-wire Bode). Rev 3: phased build. Phase 1 = stress-strain core only. Continuity and routing (impedance) are optional plug-in modules added later without changing core files.

**Revision 4 (2026-10-14): drive hardware corrected.** Revs 1 to 3 were written from an outdated handover that listed an MG10 10:1 gearbox, a DM542 driver and 36 V. The real parts are a **DTGN PX57 5:1 planetary gearbox**, a **TB6600 driver board** and a **24 V** supply. Every derived drive number below was recomputed: 2000 steps/mm (was 4000), 6.35 kN per N.m at the nut (was 12.7), 16.7 kHz at 500 mm/min (was 33 kHz), driver-minimum force ~0.9 to 1.3 kN (was ~3.7 kN). Source of truth for mechanical facts: `docs/handover.md` in the repo (the `claude/tensometer_project_context.md` project doc). The repo copy of this skill (`.claude/skills/tensometer-firmware/SKILL.md`) is kept identical.

**Revision 5 (2026-10-15): motion planner architecture and preload.** Motion rate-update method approved (section 3.4): one 1 kHz trapezoidal planner serves every motion source (test ramp, jog, homing, preload); sources differ only in what stop condition they hand the planner, reusing the existing `CONTROLLED`/`IMMEDIATE` stop-type split. New `PRELOAD` state added (section 5): a separate operator-triggered action, not folded into ARM, that reuses the homing-style force-guard pattern (creep at a slow speed, controlled stop when force reaches a threshold) rather than a new motion primitive. Jog (unloaded) speed is now defined as equal to the homing fast-approach speed (one shared constant) — direction decided, exact mm/min value still open. Preload threshold force and approach speed are open, pending an HX711 noise-floor measurement. See `docs/decisions.md` (2026-10-15) for the full discussion.

**Revision 6 (2026-10-26): panel/LED/guard pins finalized; guard interlock not fitted.** Mathew delegated the panel/LED/guard pin choice (issue #8); the previously PROVISIONAL assignments (section 2.6) were checked against the Teensy 4.1 datasheet pinout, found conflict-free, and kept as-is: no move needed. The guard interlock is confirmed **not fitted**: `GUARD_SENSE` (pin 28) stays reserved and wired to nothing, and the safety module (section 4) must not enforce the GUARD_SENSE rule while `cfg::GUARD_INTERLOCK_FITTED` is false. Issue #8 fully resolved. See `docs/decisions.md` (2026-10-26).

**Revision 7 (2026-10-26): no driver buffer; reset-safe pulls missing.** Section 2.1 was wrong: there is no 74AHCT244 (or any) buffer between the Teensy and the TB6600 -- GPIO wires straight to the opto inputs, common-anode @ +5 V confirmed, no external current-limiting resistor added. Mathew confirmed there is **no pull-down on ENA and no pull-up on STEP/DIR anywhere**. This is a confirmed, unfixed safety gap: nothing guarantees the motor stays disabled across a reset or before `setup()` runs. `main.cpp`'s drive-safe comment, which wrongly claimed this protection existed, is corrected. Bench bring-up step 2 (section 13) is expected to fail until a physical pull-down is added. Tracked in issue #18. See `docs/decisions.md` (2026-10-26).

## 0. How to work

- Act as a critical senior firmware reviewer. Name failure modes, check timing and margins, prefer correctness over agreement. Do not just agree; double check against this file and the physics.
- Ask before deciding anything marked PROVISIONAL or OPEN, or anything that changes hardware, pins, safety behaviour or the protocol.
- Never invent hardware facts. If a value is not in this file, ask Mathew or mark it `// TODO(verify):` in code.
- Correct earlier mistakes openly and say what changed.
- Deliver code as files (PlatformIO layout, section 12), not long pasted blocks. Run the checklist in section 14 before handing anything over, and state the result.

## 1. Phasing and module architecture

### 1.1 Phases

- **Phase 1 (build now): stress-strain core.** Motion, force, safety, state machine, break detection, comms, SD, panel and OLED.
- **Phase 2 (later): optional modules.**
  - `continuity`: 4-channel DC four-wire conductor monitor (section 2.4).
  - `routing`: Bode relays that switch one conductor to the scope. The Bode sweep itself is PC-side SCPI, never firmware.
  - The two modules are independent: either can exist without the other.
- **Simulation module** `sim_continuity` (build flag, bench and native tests only): injects synthetic conductor events through the same seam, so the module interface is exercised from phase 1.

### 1.2 Module interface

Every optional module implements:

| Hook | Purpose |
|---|---|
| `init()` | Set up; return status. Includes hardware presence check |
| `capability()` | Capability bit(s) it provides, or 0 if hardware absent |
| `poll()` | Called once per main-loop pass; must not block |
| `validate_config(section)` | Validate its own test-config section at ARM |
| `safety_check()` | Return a fault code or 0 |
| `inhibit_bits()` | Current motion-inhibit bits it asserts |
| `on_state_change(from, to)` | React to core state changes |
| `conductor_events()` | (continuity, sim only) drain events into the break-detection seam |
| `emit()` | Push its stream and event frames into comms and SD queues |

Rules:
- Modules are listed in one compile-time table (`modules.cpp`). No dynamic allocation, no registration at runtime.
- The core never includes or calls module code directly. It only calls the table and reads the seams in 1.3.
- **Isolation test:** adding or removing a module may change only `modules.cpp`, build flags, `protocol.h` (module ID range) and the module's own files. Any other core change is a design failure to flag.

### 1.3 Core seams (built in phase 1)

- **Motion inhibit mask:** `uint32_t` of reasons motion is not allowed. Core safety ORs in every module's `inhibit_bits()`. Any bit set -> no motion; requesting motion -> FAULT.
- **HOLD lock owner:** HOLD may be held by a lock owner (none, PC pause, routing). RESUME is refused while a non-PC lock is held. Bode is "HOLD with the routing lock", not a separate state.
- **Conductor event source:** break detection reads a conductor-event queue. In phase 1 it is empty. Continuity or sim modules fill it.
- **Capability bitmask:** reported in HELLO. Bit 0 core, bit 1 continuity, bit 2 routing, bit 7 sim.
- **Config sections:** test config = core section + optional module sections (section 8).
- **Reserved resources:** pins 16, 17, 20 to 26 unused in phase 1; ADC DMA interrupt priority slot and loop time budgeted; SD record types pre-assigned.

### 1.4 Build and runtime switches

- Build flags: `FEATURE_CONTINUITY`, `FEATURE_ROUTING`, `FEATURE_SIM_CONTINUITY` (0 or 1). Phase 1 build: all 0, or sim only for testing.
- Runtime: a compiled-in module whose hardware is absent reports capability 0, logs it, and the machine runs core tests normally. Absence is never a fault.
- Phase 1 hardware: fit a connector on the controller board for the future continuity front end (8 analog inputs, V_EXC sense, I2C, power).

## 2. Hardware facts

### 2.1 Controller and drive (core)

Stock Teensy 4.1 board specs (pin count, voltages, memory, peripherals) come from the manufacturer datasheet at `docs/datasheets/TEENSY41-L_DATASHEET-EN.pdf`; check it for anything about the bare board not covered below, rather than guessing. This project's own pin assignments stay in section 2.6, not the datasheet.

| Item | Value |
|---|---|
| MCU | Teensy 4.1 (600 MHz, FPU). **3.3 V I/O, NOT 5 V tolerant** |
| Driver | **TB6600 board.** Current set by DIP switches, NOT by firmware. No fault output (firmware cannot detect driver overtemperature or stall). No automatic idle-current reduction (holds at DIP current). Lowest DIP setting typically 0.5 A |
| Driver interface | **No buffer chip (corrected 2026-10-26; was wrongly documented as a 74AHCT244).** Teensy GPIO wired directly to the TB6600 opto inputs. Common anode: PUL+, DIR+, ENA+ tied to +5 V; Teensy sinks on PUL-, DIR-, ENA-. No external current-limiting resistor added (Mathew confirmed); relies entirely on whatever the TB6600 board has internal, TODO(verify) |
| Opto logic | Teensy LOW -> buffer LOW -> opto ON. Teensy HIGH -> opto OFF |
| STEP | Idle HIGH (opto off). Pulse = LOW for `STEP_PULSE_US`, default **5 us**, off-time >= 5 us. TB6600 boards use optos of unknown speed (fast 6N137 or slow PC817-class); 5 us is a conservative default. TODO(verify) the opto part, then tighten |
| ENA | **Opto ON = driver DISABLED** (on many TB6600 boards asserting ENA releases the motor and an unconnected ENA leaves it enabled). VERIFY on the bench. `motor_enable()` drives ENA HIGH, `motor_disable()` drives ENA LOW |
| Reset safety | **CONFIRMED GAP, not yet fixed (2026-10-26):** no pull-down on ENA and no pull-up on STEP/DIR exist anywhere -- nothing holds the driver disabled between power-up and firmware running, or across a Teensy reset while the 5 V/TB6600 side stays powered. Needs a physical pull-down added on ENA (toward GND) and pull-ups on STEP/DIR, sized against the TB6600's internal opto resistor (unknown, see issue #3). Until fixed, treat "motor disabled on reset" as unverified |
| DIR setup | >= 5 us before the next step edge after a direction change |
| Max step rate | Firmware cap **20 kHz** (TB6600 opto limit unknown, can be tens of kHz). 500 mm/min needs 16.7 kHz. Raise the cap only after scope-verifying the opto |
| Motor | 23HS22-2904S, 200 steps/rev, 1.2 N.m holding, 2.9 A rated |
| Gearbox | **DTGN PX57 planetary, 5:1**, <= 15 arcmin, 3000 rpm max input. Rated output torque NOT known |
| Screw | SFU1204, lead 4 mm, eta ~ 0.9 |
| Microstepping | **1/8, fixed** (driver DIP). One config constant |
| Encoder | **None fitted.** Position truth is the step count; see slip safeguards in section 4 |

Derived:
- Steps per mm = 200 x 8 x 5 / 4 = **2000 steps/mm** (0.5 um per step, 500 nm).
- 200 mm/min = 6 667 steps/s (250 rpm motor). 500 mm/min = 16 667 steps/s (625 rpm motor), within the 20 kHz cap.
- Force at nut ~ **6.35 kN per N.m** of motor torque (F = 2 pi eta T x 5 / lead).
- Working loads <= 2 kN need ~0.31 N.m at the motor; 3 kN ceiling ~0.47 N.m. Check the motor torque-speed curve at 625 rpm before relying on 500 mm/min.
- Gearbox backlash 15 arcmin ~ 2.8 um at the screw (screw-side, before the nut).
- Driver current must never be set to the motor's rated 2.9 A (~7.6 kN at the nut, ~5.4 N.m at the gearbox output, gearbox rating unknown). Commissioning check, not firmware.
- Driver minimum current 0.5 A gives roughly **0.9 to 1.3 kN at the nut** (est., depends on whether the DIP value is peak or RMS). That is near the 100 kg cell's 1.47 kN safe overload: thin and unverified, so the coupler and firmware cutoff remain required.

### 2.2 Load cell profiles and the coupler (core)

Firmware selects a cell profile at boot or by command. Limits come from the profile, never hard-coded elsewhere.

| Profile | Rated | Software cutoff | Coupler slip setting required | Notes |
|---|---|---|---|---|
| DYLY-103 100 kg (default) | 981 N | 880 N | ~1.1 kN (0.8 N.m at screw) | Safe overload 1471 N. Sensitivity 1.99813 mV/V |
| PSD-S1 500 kg | 4.9 kN | 3000 N | TBD, must be > cutoff (~3.3 kN) | Stall test and coupler calibration only |
| 10 kg (fallback) | 98 N | 88 N | ~1.1 kN (cell protected by cutoff only) | If 100 kg is too coarse for fabric |

Rules:
- The coupler is mechanical and per-cell. Selecting a profile requires a `COUPLER_CONFIRMED` flag from the PC/panel. No flag -> NACK.
- Validate at profile select: cutoff < coupler slip force for that profile. If not, refuse.
- The TB6600 at its lowest DIP current can still push ~0.9 to 1.3 kN at the nut (est.), so the firmware cutoff is a real safety layer. Set the DIP current to the lowest setting that meets the test's force needs.
- 10 kg note: the coupler cannot protect a 98 N cell. The cutoff and a reduced max speed are the only protection; flag this whenever the 10 kg profile is used.

### 2.3 HX711 (core)

- Supply 3.3 V from a **dedicated low-noise LDO** (not the Teensy 3.3 V rail). Gain 128, channel A. **80 SPS** (RATE pin high; verify on the board).
- Expect 1 to 2 fewer usable bits at 80 SPS than at 10 SPS. Measure the noise floor; do not assume 16 bits.
- Wiring: red E+, black E-, green A+, white A-, shield to AGND at the HX711 end only.
- **Power-down trap:** SCK high > 60 us powers the chip down and corrupts the read. Do NOT disable interrupts for the whole 25-bit read. Keep each SCK-high phase ~1 us and guarantee every ISR finishes in < 20 us.
- Timestamp each sample with a DOUT falling-edge interrupt (timestamp only). Disable that interrupt while reading, because DOUT toggles during the shift.
- Each sample averages the previous 12.5 ms. Effective sample time = DOUT timestamp - 6.25 ms.
- Calibration factor comes from hanging known masses, never from the datasheet alone.

### 2.4 Continuity monitor (MODULE `continuity`, phase 2)

Purpose: detect conductor open/degradation during motion and give continuous DC four-wire resistance vs strain.

Per channel (n = 0 to 3), four-wire, connected through tail terminal blocks on each jaw (clamp grips fabric only). Each channel has its own F- return wire to a single star point at AGND.
- Excitation: separate precision 3.3 V supply V_EXC (not the Teensy rail), through a selectable reference resistor **R_ref in {330 ohm, 3.3 kohm, 33 kohm}** (0.1%) via an analog switch (>= 20 mA, Ron < 5 ohm). Max 10 mA per channel.
- V_EXC measured on its own ADC input every scan; resistance maths ratiometric in software.
- ADC_A[n]: two-wire voltage at F+ (filtered, series resistor + clamp diodes). Open detect and current.
- ADC_B[n]: S+/S- through a low-offset rail-to-rail instrumentation amp, gain ~100. Resistance trend.
- Per-range calibration constants measured on the bench with precision resistors, stored in EEPROM.
- Sampling: timer-triggered ADC with DMA, 16 kHz per input, averaged 8:1 in software to 2 kHz per channel.

Resistance: I = (V_EXC - V_A) / R_ref_cal; R = V_B / (G x I). Report in mOhm (uint32), open = `UINT32_MAX`. Static offsets largely cancel in R/R0; do not over-trust absolute mOhm on copper.

Classification:
- R0 per channel: average 200 samples (0.1 s) at test start after tare.
- Auto-range at ARM: R_ref that puts V_A between 5% and 80% of V_EXC; reject the channel if none does.
- Open: V_A > 0.95 x V_EXC, or R > k_open x R0 (default 10), for >= 2 consecutive samples (1 ms).
- Degraded: R > k_deg x R0 (defaults 2 and 5), once per threshold.
- Reclose: after open, R < k_open x R0 for >= 4 consecutive samples (2 ms). Log every reclose and re-open.
- Short test (HOLD only): change one channel's range, check whether others' V_A move beyond noise. Log `CONDUCTOR_SHORT` with the pair.
- Ignore samples for 20 ms after any relay or range switch (routing notifies continuity through a shared blanking timestamp in the module layer, not the core).

Safety: `safety_check()` faults if the ADC DMA stalls.

### 2.5 Routing (MODULE `routing`, phase 2)

- Two DPDT signal relays per channel (gold/bifurcated contacts), 8 total, via MCP23017 + ULN2803 with flyback diodes.
- De-energised = conductor on the continuity side. Energised = **all four lines disconnected from the Teensy side**, F+/F- on the scope reference-resistor network. **At most one channel energised at a time.**
- Bode is **two-wire** (scope channels are ground-referenced). PC runs open/short compensation sweeps per setup. Accurate DC resistance comes from the continuity module.
- Routing takes the HOLD lock and asserts its motion-inhibit bit whenever any relay is energised. Release: de-energise, wait 20 ms, drop the bit and lock.
- Hardware presence: MCP23017 answers on I2C at boot. An MCP23017 failure while a relay is energised is a fault.

### 2.6 Pin map (Teensy 4.1)

| Function | Pin | Phase / status |
|---|---|---|
| STEP / DIR / ENA | 2 / 3 / 4 | Core, fixed |
| HX711 DOUT / SCK | 5 / 6 | Core, fixed |
| LIM_NEAR / LIM_FAR (NC) | 7 / 8 | Core, fixed. External 2.2 kohm pull-ups to 3.3 V + ~100 us RC. Pin mode INPUT |
| ESTOP_SENSE | 9 | Core, fixed. Optocoupler from the contactor OUTPUT (24 V present) |
| Pins 14 / 15 | free | Encoder dropped; reserve |
| I2C SDA / SCL (OLED, MCP23017) | 18 / 19 | Core, fixed |
| ADC_A[0..3] | 16, 17, 20, 21 | Continuity, RESERVED now, PROVISIONAL |
| ADC_B[0..3] | 22, 23, 24, 25 | Continuity, RESERVED now, PROVISIONAL |
| V_EXC sense | 26 (A12) | Continuity, RESERVED now, PROVISIONAL |
| Panel: START, STOP, JOG+, JOG-, HOME, TARE | 30-35 | Core, fixed (2026-10-26) |
| FAULT LED | 36 | Core, fixed (2026-10-26) |
| SYNC LED (video sync, in camera view) | 29 | Core, fixed (2026-10-26) |
| GUARD_SENSE (not fitted) | 28 | Core, fixed (2026-10-26). Reserved, wired to nothing; guard interlock confirmed not fitted |
| LED_BUILTIN | 13 | Status LED. SPI unused |

LIM_NEAR = end near the stationary jaws (minimum gap). LIM_FAR = motor end (maximum gap).

### 2.7 Electronics

**Phase 1:** Teensy 4.1 + industrial microSD; USB isolator (full-speed fine); TB6600 driver board, wired direct to Teensy GPIO with no buffer and **no pull resistors yet** (confirmed gap, 2026-10-26 -- see section 2.1 Reset safety); **24 V** PSU (TB6600 boards are typically rated 9 to 42 V; check the board); DC-rated contactor + NC e-stop; 2 NC limit switches with external pull-ups and RC; HX711 (80 SPS) on its own LDO; 6 panel buttons; SSD1306 OLED; fault LED; sync LED; 5 V buck; analogue 3.3 V LDO; ESTOP_SENSE optocoupler; Teensy VUSB/VIN separation if externally powered; connector for the future continuity board. No guard interlock (confirmed not fitted, 2026-10-26).

**Phase 2 (continuity + routing board):** precision resistors 330 / 3.3k / 33k x4; analog switches x4; instrumentation amps x4; filters, series resistors, clamp diodes; precision V_EXC supply; 8 DPDT gold-contact signal relays; MCP23017; ULN2803; 2 x 8-way tail terminal blocks; shielded twisted pairs; Bode reference resistor + BNCs.

## 3. Architecture and timing

### 3.1 Split of responsibilities

- **Teensy (real time):** step generation, force sampling, safety, state machine, break detection, timestamps, telemetry, heartbeat, SD backup, panel/OLED, and any enabled modules.
- **PC (Python):** UI, sequencing, strain-rate scaling (strain rate -> mm/min), OpenCV strain, SCPI to the Siglent (phase 2), primary logging.
- The Teensy must be safe with the PC unplugged mid-test.
- No RTOS. Bare-metal main loop plus prioritised interrupts.
- One owner per piece of state: ISRs only produce samples or set flags; only the state machine changes mode or motion targets; PC and panel commands become queued events.
- Test configuration validated and frozen at ARM.

### 3.2 Interrupts and priorities (highest first)

| Source | Rate | Budget | Job | Phase |
|---|---|---|---|---|
| Step timer (IntervalTimer) | up to 20 kHz | < 7 us | Assert STEP, hold `STEP_PULSE_US` (5 us), release, update position, load next period | Core |
| Limit / e-stop pin change | event | < 5 us | Confirm with second read, set stop flag, stop steps, timestamp | Core |
| ADC DMA complete | 2 kHz block | < 10 us | Swap buffers, set flag | Continuity (slot reserved now) |
| HX711 DOUT falling | 80 Hz | < 2 us | Timestamp only | Core |

Rules:
- No ISR longer than 20 us (HX711 power-down trap).
- The step ISR is the only place allowed a busy-wait (<= `STEP_PULSE_US`, 5 us). Nowhere else. At 20 kHz this is ~10% CPU.
- OPEN (proposed, not yet approved): replace the "every ISR < 20 us" rule with interrupt-free critical sections only around each HX711 SCK-high phase (~1.5 us), because library ISRs (USB, SDIO) cannot be guaranteed under 20 us.
- ISRs never allocate, print, call Serial, touch SD or I2C, or do floating-point division.
- Shared ISR/loop data: `volatile`; multi-word reads with interrupts briefly off or a sequence counter. Ring buffers single-producer single-consumer.

### 3.3 Main loop order (every pass, target < 1 ms, worst case < 2 ms including phase 2 budget)

1. Core safety evaluation, including every module's `safety_check()` and `inhibit_bits()`.
2. Force: HX711 read if ready.
3. Module `poll()` for each enabled module (table order).
4. Break detection (reads the conductor-event queue).
5. State machine and motion planner; module `on_state_change()` on transitions.
6. Comms (non-blocking), including module `emit()`.
7. SD (one block, only if card not busy).
8. Panel and OLED (one 128-byte page per pass, refresh <= 5 Hz).
9. Watchdog feed if core and all enabled modules report alive.

No step may block. Anything slow is split into slices.

### 3.4 Motion

- Position truth = signed int32 step count; position in nm derived (steps x 500), never stored separately. `position_valid` cleared on boot, e-stop, slip-suspect fault, and any fault that may have moved the screw; RUN requires it set.
- **Trapezoidal profile (approved, rev 5):** one 1 kHz planner (`lib/motion_profile`) serves every motion source (test ramp, jog, homing, preload). Each tick it is handed the steps remaining before the current target and recomputes the commanded step rate (accelerate / cruise / decelerate), which it writes for the step ISR to execute; the ISR holds `steps_remaining` as its own down-counter and stops exactly on target regardless of the commanded rate. The planner itself knows nothing about force, buttons or positions in mm — only step rates and step counts — so it does not change per motion source:
  - **Position-target move** (test segment, homing slow re-approach, move-to-L0): caller feeds the real, shrinking distance to target.
  - **Open-ended move** (jog, preload creep): caller feeds `UNBOUNDED_STEPS` until it decides to stop (button release, force threshold reached), then switches to feeding `steps_to_stop()` at the current rate so the planner decelerates cleanly from whatever speed it is at — this collapses an open-ended move into an ordinary position-target decel, with no special case in the planner.
  - Short moves (target closer than the accel distance) naturally produce a triangular profile from the same per-tick logic; no separate case needed.
- Direction change: velocity to zero, set DIR, wait >= 5 us, then step.
- Stop types: `CONTROLLED` (decel ramp, via the planner above) for normal stops; `IMMEDIATE` (bypasses the planner, used only by the safety path) for faults. Log both.
- Holding: motor stays enabled in HOLD, BREAK_HOLD and FAULT (unless e-stop).
- Homing: only with |force| < 2 N. Abort with FAULT if |force| > 5 N during homing. Home to LIM_FAR: fast approach, back off, slow re-approach, set zero. Fast-approach and slow-re-approach speeds: OPEN, no value set (section 15).
- Gauge length: jaw gap at home is a calibrated constant in `config.h`. PC sends L0 in the config; firmware moves to L0 before ARM completes.
- Jog speed: unloaded jog speed = homing fast-approach speed (one shared constant, value OPEN). Loaded jog capped at 10 mm/min whenever |force| > 5% of the profile cutoff (unchanged).
- **Preload (new, rev 5):** separate operator-triggered action (state `PRELOAD`, section 5), not automatic on ARM. Creeps at a slow approach speed in the extend direction and takes a `CONTROLLED` stop (via the open-ended-move pattern above) once force reaches a threshold; reuses the homing force-guard pattern rather than a new motion primitive. Threshold force and approach speed: OPEN, pending an HX711 noise-floor measurement (section 13 bring-up step 5); candidate range discussed in `docs/decisions.md` (2026-10-15) is ~1-3 N threshold, ~5-10 mm/min creep — not approved values, do not hard-code them.

### 3.5 Time base

- All samples and events carry a us timestamp from one monotonic 64-bit counter.
- Video sync: SYNC LED on for 100 ms at test start and every 10 s; log the exact on-edge as a `SYNC` event.

## 4. Safety rules (non-negotiable, core)

1. Boot with the motor disabled (ENA LOW), state IDLE, not homed, `position_valid` false. RUN requires homed and valid (unless an explicit `ALLOW_UNHOMED` debug flag is set and logged).
2. Immediate stop and FAULT latch on any of:
   - force > profile cutoff (raw force, every HX711 sample, every state);
   - limit switch open (NC: broken wire also trips);
   - e-stop sense (also disable ENA; clear `position_valid`);
   - soft travel limit or strain limit reached;
   - PC heartbeat missing > **1 s** while moving;
   - HX711 sample stale > 50 ms while moving or loaded;
   - watchdog reset detected at boot (report, stay IDLE);
   - motion requested while any motion-inhibit bit is set;
   - any module `safety_check()` fault;
   - GUARD_SENSE open while a run-to-break test is armed, **only if `cfg::GUARD_INTERLOCK_FITTED` is true** (currently false — not fitted, decided 2026-10-26);
   - **slip suspect:** while extending, force > 80% of the profile's coupler slip force and force change < 1% over >= 2 mm commanded travel (clear `position_valid`);
   - homing force guard (3.4).
3. FAULT clears only by explicit RESET after the cause is gone. Never auto-restart motion. If `position_valid` was cleared, re-home before RUN.
4. After an overload or limit fault, only jog in the relieving direction until reset.
5. Hardware watchdog enabled, fed only from the main loop when core and modules are alive.
6. Validate every command parameter (speed <= 500 mm/min, force limit <= profile cutoff, travel within soft limits, L0 within stroke, module sections only for present modules). NACK with an error code; never clamp silently.
7. Profile select requires `COUPLER_CONFIRMED` and cutoff < coupler slip force.
8. No stall or overload test may run on the 100 kg or 10 kg profile. Stall tests require the 500 kg profile and a confirmation flag.
9. Every fault and stop is an event with timestamp, cause, position and force.

Known margin (keep valid): force can rise ~120 N in one 12.5 ms HX711 period on a stiff ABS Type IV dog-bone at 500 mm/min, so the 880 N cutoff overshoots to ~1 kN, under the 1471 N safe overload. Re-check if speed limits, cutoff or sample rate change.

## 5. State machine (core)

States: `BOOT, IDLE, HOMING, JOG, TARE, PRELOAD, READY, RUN, HOLD, CYCLE, BREAK_HOLD, FAULT`. (No BODE state: Bode = HOLD with the routing lock.)

`PRELOAD` (rev 5): operator-triggered, not automatic on ARM (section 3.4). Available from IDLE or READY, homed and valid required (same precondition as JOG/TARE). Creeps in the extend direction at the (open) preload approach speed; a `CONTROLLED` stop on reaching the (open) preload force threshold returns to the state it was entered from. Any hard safety trip (section 4) wins as usual -> FAULT.

| From | Event | To |
|---|---|---|
| BOOT | self-test pass | IDLE |
| IDLE | HOME (force guard ok) | HOMING -> IDLE (homed, valid) |
| IDLE / READY | JOG (hold-to-run) | JOG -> back |
| IDLE / READY | TARE | TARE -> back |
| IDLE / READY (homed, valid) | PRELOAD | PRELOAD -> back (on threshold reached) |
| IDLE (homed, valid) | ARM, all config sections valid, moved to L0 | READY |
| READY | START | RUN or CYCLE |
| RUN / CYCLE | hold point or PAUSE | HOLD (lock: PC) |
| HOLD | module requests lock (e.g. routing) | HOLD (lock: module) |
| HOLD | module releases lock | HOLD (lock: PC) |
| HOLD | RESUME, no module lock, no inhibit bits | RUN / CYCLE (segment peak reset) |
| RUN / CYCLE | SUBSTRATE_BREAK | BREAK_HOLD |
| RUN / CYCLE | target reached / cycles done | HOLD then READY |
| any | safety trip | FAULT |
| FAULT | RESET, cause cleared | IDLE |

Logging continues in every state.

## 6. Force measurement (core)

- Store and log raw counts and calibrated force in mN (int32), with the DOUT timestamp.
- Two paths: raw (median of 3 at most) for safety and break detection; filtered (IIR, 5 to 10 Hz) for display only.
- Tare: average >= 40 samples (0.5 s) while stationary; reject if noise > threshold.

## 7. Break detection (core, with conductor seam)

Policy: **log conductor breaks, continue until the substrate breaks.** `STOP_ON_CONDUCTOR_BREAK` per-test flag, default off (only meaningful with continuity).

Conductor events arrive through the conductor-event queue (empty in phase 1). The classifier code is identical with or without a continuity module.

`conductors_present` (core config flag): if set and no conductor-event source is available, firmware ACKs but marks every break event `UNVERIFIED_CLASSIFICATION` (a thick-wire break will look like a substrate break). The PC warns.

Parameters (per test, sent by PC, these defaults):

| Name | Default |
|---|---|
| `drop_frac` | 0.30 (drop from segment peak) |
| `drop_window` | 3 force samples |
| `coinc_window` | -1 to +3 force samples around the conductor event, using effective sample time |
| `both_frac` | 0.60 (coincident drop this large = conductor AND substrate) |
| `residual_frac` | 0.07 (force below this x segment peak, held) |
| `residual_hold` | 8 force samples |
| `slip_frac` | 0.03 to 0.30 drop recovering to the previous peak within `slip_recover_mm` (default 1 mm) |
| `min_peak_N` | 0.5 N |

Logic (only while extending):
1. **Segment peak** reset at START, every RESUME, every cycle reload, and after a conductor-classified drop. Never carried across cycles.
2. Track test **maximum force** with position and timestamp separately; always log it (`MAX_FORCE`).
3. Ring buffer of the last 16 force samples (timestamp, position).
4. Drop >= `drop_frac` within `drop_window`:
   - coincident conductor OPEN and drop < `both_frac` -> `CONDUCTOR_OPEN` (if not already logged), reset segment peak, continue;
   - coincident OPEN and drop >= `both_frac` -> `CONDUCTOR_OPEN` and `SUBSTRATE_BREAK`;
   - no coincident OPEN -> `SUBSTRATE_BREAK`.
5. Force < `residual_frac` x segment peak for `residual_hold` samples -> `SUBSTRATE_BREAK`.
6. Slip-band drop that recovers -> `SLIP`.
7. Failure point = pre-drop peak from the ring buffer. Log energy to break (trapezoid integral of force x displacement, int64).
8. On `SUBSTRATE_BREAK`: immediate stop, BREAK_HOLD, keep logging.

Event types: core `SUBSTRATE_BREAK, SLIP, MAX_FORCE, UNVERIFIED_CLASSIFICATION, LIMIT_STOP, FORCE_CUTOFF, ESTOP, HEARTBEAT_LOST, SENSOR_STALE, SLIP_SUSPECT, HOMING_FORCE, SYNC, LOG_OVERFLOW, MODULE_ABSENT, FAULT, RESET`; continuity `CONDUCTOR_DEGRADED, CONDUCTOR_OPEN, CONDUCTOR_RECLOSE, CONDUCTOR_SHORT, RANGE_SWITCH`; routing `BODE_SELECT, BODE_RELEASE`.

Hard stops (section 4) always win.

## 8. Comms protocol

- Transport: USB serial (works through a full-speed USB isolator). COBS frames, 0x00 delimiter.
- Payload: `msg_id (u8) | seq (u8) | body | crc16 (u16)`. CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over msg_id..body. Little-endian.
- **Message ID ranges:** core 0x00-0x3F, continuity 0x40-0x5F, routing 0x60-0x6F, reserved 0x70-0xFF.
- `PROTOCOL_VERSION` = major.minor. Major bump = breaking core change. Adding a module or module messages = minor bump only. Old PC software must still run core tests against newer firmware.
- HELLO carries version, firmware build, capability bitmask, cell profile, Teensy clock.
- Every command gets ACK or NACK (error code) echoing `seq`.
- Heartbeat: PC every 100 ms; loss > 1 s while moving -> FAULT. Firmware sends its own with state, lock owner, inhibit mask, fault flags.
- **Test config sections:** core section (always) + optional `continuity` and `routing` sections, each with its own section ID and length. Unknown or unsupported section -> NACK. Missing optional section -> module idle for that test.
- Streams: core TELEMETRY at 80 Hz (timestamp, steps, force raw, mN, filtered mN, state, flags); EVENT frames; module streams (e.g. continuity batches 20 samples x 4 channels, 100 frames/s). PC ignores stream IDs it does not know.
- ASCII debug mode: build flag or command, never during a real test.
- All IDs and structs in `protocol.h`, mirrored by Python.

## 9. SD backup log

- Same binary frames as USB, from the main loop via a 64 kB buffer in DMAMEM. Record types pre-assigned per module.
- Pre-allocated contiguous file. One 512-byte block per loop pass, **only when the card reports not busy**. Never a blocking write.
- On overflow: drop log data, raise `LOG_OVERFLOW`, keep running.

## 10. Panel, OLED and I2C

- Buttons on direct pins, debounced in the loop; STOP acted on immediately.
- OLED: one 128-byte page per pass, full refresh <= 5 Hz. Never a full-frame blocking write.
- Shared I2C (OLED, later MCP23017): module commands have priority over display traffic. Implement I2C bus recovery.

## 11. Coding standards

- C++17, Arduino framework on PlatformIO. No `delay()` in normal paths. No `new`/`malloc`/`String`/allocating STL.
- Fixed-width types. Units in names: `_steps`, `_um`, `_mN`, `_mohm`, `_us`, `_hz`.
- Constants in `config.h` (hardware) and `safety_limits.h` (safety). No magic numbers.
- Pure logic (CRC, COBS, break detection, continuity classification, motion profile maths, slip check, config-section parsing) in `lib/` with no Arduino dependency.
- Hardware access only in thin driver files.
- Every function that can fail returns a status; callers check it.
- Comments explain why, not what. Mark unverified hardware values `// TODO(verify):`.

## 12. Project layout

```
firmware/
  platformio.ini        # envs: teensy41 (phase 1), teensy41_sim, native
  include/  config.h  safety_limits.h  protocol.h  module_api.h
  src/core/     main.cpp  stepper.cpp  hx711.cpp  safety.cpp  state_machine.cpp
                comms.cpp  sdlog.cpp  panel.cpp  oled.cpp  modules.cpp
  src/modules/  continuity/  routing/  sim_continuity/
  lib/          crc16/  cobs/  frame/  messages/  break_detect/  continuity_logic/  motion_profile/
                slip_check/  config_sections/
  test/         test_crc16/  test_cobs/  test_frame/  test_messages/  test_break_detect/  test_continuity/
                test_profile/  test_slip/  test_config_sections/  test_module_seam/
```

Pin library versions in `platformio.ini`.

## 13. Testing and bring-up

Unit tests (native, Unity): CRC vectors; COBS round trips with zeros; frame encode/decode and corruption rejection; break classifier on synthetic traces (thick-wire break, thin-wire break, fabric tearing through wire, gradual tear, slip saw-tooth, substrate break, noise, multi-cycle with Mullins softening, resume after relaxation), **each run with and without conductor events**; continuity open/reclose/degrade; profile timing; slip check; config sections (missing, unknown, module absent); **module seam test with sim_continuity**.

Phase 1 bench bring-up (each step passes before the next):
1. Logic only, no driver power: pins, panel, limits and e-stop (with RC), watchdog, protocol, HELLO capability = core only.
2. Reset safety: driver powered, motor disconnected. Hold the Teensy in reset, then unplug it: driver stays disabled, no STEP edges. Confirm ENA polarity on the TB6600. **Known gap (2026-10-26): no pull-down exists on ENA yet, so this test is expected to fail / show undefined behaviour until a pull-down is physically added. Do not proceed past this step until it passes.**
3. Driver timing at the opto output side (scope across the TB6600 opto): pulse and off-time clean at 20 kHz with the 5 us pulse, DIR setup >= 5 us. Identify the opto part. Only then consider raising the cap or shortening the pulse.
4. Motor, no specimen, low current: direction, steps/mm against a dial gauge, homing and force guard, limits; watch limit and e-stop inputs for noise with the motor running.
5. HX711 noise floor on its own LDO, hanging-mass calibration, polarity, timestamp jitter.
6. SD and OLED under load: loop worst case < 2 ms.
7. sim_continuity build: injected conductor events classify correctly; inhibit and lock seams work.
8. Dummy specimen, cutoff trip at a reduced limit, slip-suspect check.
9. Real specimens.

Phase 2 bring-up: continuity on precision resistors (0.1 ohm, 10 ohm, 1 kohm, 10 kohm, open) per range, store calibration, noise with motor running; routing switching, blanking, inhibit, lock; confirm no core file changed beyond the isolation rule.

Stall and coupler tests only on the 500 kg profile with a steel dummy.

## 14. Pre-delivery checklist (run and report every time)

- [ ] Matches section 2 facts; no invented values; unverified ones marked.
- [ ] ENA semantics correct (HIGH = enabled); boot leaves the motor disabled.
- [ ] No ISR over budget; no busy-wait outside the step ISR; HX711 trap respected.
- [ ] No blocking call in the loop (SD, I2C, Serial).
- [ ] Shared data volatile and read atomically.
- [ ] Every safety rule in section 4 enforced; no path to motion bypassing the inhibit mask; `position_valid` handled.
- [ ] Commands and config sections validated; NACK on bad input; no silent clamping.
- [ ] Units named; overflow checked (steps, us, mN x um energy).
- [ ] Break peaks reset per segment; max force tracked; classifier tested with and without conductor events.
- [ ] **Module isolation rule held** (core does not call module code directly; adding a module touches only the allowed files).
- [ ] Protocol changes in `protocol.h`, correct ID range, version bumped (minor for additive).
- [ ] Pure logic has native tests; tests updated.
- [ ] Compiles for all envs (or say clearly what was not compiled).
- [ ] Open questions listed at the end of the reply.

## 15. Open items

- Continuity-module PROVISIONAL pins (phase 2, section 2.4/2.6), MCP23017, analog switch and instrumentation amp parts, amp gain — await the phase 2 schematic (issue #17). Core panel/LED/guard pins are finalized (rev 6).
- TB6600 board: ENA polarity, opto part and speed, DIP current table (peak or RMS), printed voltage rating.
- Motor torque at 625 rpm (500 mm/min) and the gearbox rated output torque.
- Proposed HX711 critical-section rule (section 3.2) awaiting approval.
- Coupler slip setting for the 500 kg profile.
- Jaw gap at home position (calibration constant).
- Continuity front-end schematic and phase 2 board connector pinout.
- Siglent AWG option (PC side).
- Ramp acceleration for the motion planner: no moving-assembly inertia data exists yet, so no value is set anywhere; propose tuning conservatively at bench bring-up step 4 (section 13), not inventing a number now.
- Homing fast-approach / slow-re-approach speed, and unloaded jog speed (= homing fast-approach, rev 5 decision): direction decided, mm/min value open.
- Preload threshold force and approach speed (rev 5 decision: feature approved, values open pending HX711 noise-floor measurement).
- `CMD_PRELOAD` (or equivalent) not yet added to `protocol.h`: the `PRELOAD` state (section 5) needs a way to be triggered from the PC/panel. Add it with a minor version bump when the message bodies are written (section 8); until then `PRELOAD` exists only as a `CoreState` value with no wire path to reach it.