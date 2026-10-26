# Tensometer Project: Full Context Handover

> Copied from the claude.ai project doc `claude/tensometer_project_context.md` on 2026-10-14. This is the **current** version (DTGN 5:1 gearbox, TB6600 driver, 24 V). An older copy in the project (`tensometer_project_context.md`, MG10 10:1, DM542, 36 V) is outdated and must not be used. Firmware-specific decisions made after this handover live in `docs/decisions.md` and the firmware skill.

Purpose of this file: give a fresh chat everything decided, calculated and still open for the stepper-driven ball-screw tensometer, from the original requirement through to the latest decisions. Figures marked *(est.)* are estimates and should be checked on the real parts. Figures marked *(provisional)* are placeholders in the CAD that need a real measurement.

---

## 0. How to work with Mathew

- Product Design Engineer at Hippos Exoskeleton, UK. Sole engineer on this workstream.
- Casual, direct style. Wants verified answers, not guesses. Likes pictures and renders.
- **Act as a critical senior design reviewer.** Challenge assumptions, check margins, name failure modes, prefer correctness over agreement. For any recommendation use: Problem, Root cause, Consequences, Recommended solution, Alternatives, Why the final choice wins.
- **Talk to him before designing.** Ask first when a choice is his to make.
- Correct earlier mistakes openly. Several earlier numbers were revised during the project (see section 14).

---

## 1. What we are building and why

A stepper-driven ball-screw tensometer (universal testing machine) to replace a water-bucket UTM. It tests elastic substrates with embedded conductors, recording **stress-strain and concurrent impedance** at the same time.

Substrates and specimens:
- Knit garment fabric, elastic tapes, with wire (20 to 36 AWG) embedded in them. This is the main use.
- Also ABS and neoprene (solid and foam), for which dog-bone specimens are used.

The wider project is an instrumented stretchable undergarment for knee-injury risk. The tensometer exists to characterise the conductors and substrates used there. The project goal is an "almost industry standard" machine covering hardware, firmware and software.

---

## 2. Requirements

| Item | Requirement |
|---|---|
| Pull force | 1 kN or more. Working loads about 2 kN or less. Ceiling about 3 kN, set by the ball nut |
| Strain | 250 to 300% |
| Specimen size | 40 mm wide, 15 to 20 mm thick (stacked fabric or foam). Narrower dog-bones for ABS and solid neoprene |
| Cyclic testing | In scope |
| Speed | 50 to 200 mm/min. 500 mm/min is nice to have, not essential |
| Measurements | Force, displacement, strain (video for ABS), impedance sweep, continuity |
| Cell | **100 kg load cell for now.** Will switch to the 10 kg cell if firmware cannot cope with the resolution/noise |

---

## 3. Current decisions at a glance

| Topic | Decision |
|---|---|
| Linear guide | HGR15 rail, 600 mm, two HGH15CA carriages |
| Screw | SFU1204, 700 mm (ordered) |
| Motor | NEMA23, 56 mm body class, 23HS22-2904S (2.9 A, 1.2 N·m, 0.9 Ω, 2.6 mH). Real motor label still to be checked |
| Gearbox | **DTGN PX57 planetary, 5:1**: 57 × 57 mm flange, Ø38.1 × 3 pilot, Ø14 × 22 shaft with key, 4 × Ø5.5 through holes on a 47 mm square, 8 mm input bore, 4 × M5 motor screws, ≤15 arcmin, 3000 rpm input, about 72 mm body. Rated output torque not on the listing |
| Driver | **TB6600 stepper driver board**. Check the board's DIP current table, voltage rating and opto type |
| Controller | Teensy 4.x |
| Load cell | Dayang DYLY-103, 100 kg, S/N A26606087, with an HX711 |
| Frame | 1000 mm 4040 aluminium extrusion (moving-side layout). Fixed-side layout needs 1150 mm |
| Conductor break policy | **Log it and keep pulling until the substrate breaks.** Stop-on-conductor-break kept as an optional per-test checkbox, default off |
| Impedance measurement | Siglent SDS812X HD scope with Bode plot, at hold points. Separate fast continuity monitor on the Teensy |
| Dog-bones | ASTM D638 Type IV for ABS, ASTM D412 Die C for neoprene |
| Layout | Moving-side cell is the baseline (v3). Fixed-side cell is an option studied, **not yet chosen** |

---

## 4. Mechanical design

### 4.1 Hardware

- **Rail and carriages:** HGR15, 600 mm long. Two HGH15CA carriages, 59 mm long, 34.3 mm wide, 26 × 26 mm M4 hole pattern. Carriage height assumed 28 mm *(provisional)*.
- **Ball screw:** SFU1204, 700 mm. Nut D1 22, flange D3 42 with flats (H1 30), PCD 32, 6 × Ø4.8, B 8, L1 35, G M6, Ca 4 kN, Coa 6.7 kN. Measure the real nut on arrival.
- **Gearbox:** DTGN PX57 5:1 planetary (from the seller drawing): Ø14 output shaft, 22 mm long from the pilot face, Ø38.1 × 3 mm pilot, 4 × Ø5.5 through holes on a 47 × 47 mm square (bolt with M5 from the motor side into the holder), body about 72 mm, ≤15 arcmin backlash. Flange thickness, round-body diameter and rear adapter size are assumed in the CAD and need measuring.
- **Extrusion:** 4040. Slot width and T-nut size still to be measured.

### 4.2 Layouts

**Moving-side layout (v2 / v3, the baseline).**
- Stroke 331 mm, fits a 1000 mm extrusion.
- The load cell sits between the moving clamp block and the nut plate.
- Carriage-1 friction and the clamp inertia appear in the force reading.
- The cell cable moves with the carriage.

**Fixed-side layout (asm3, studied only).**
- The cell sits between a pedestal and a hung, stationary jaw block.
- Stroke 403 mm, needs a 1150 mm extrusion.
- No friction in the reading and no moving cell cable.
- Cautions: the hung jaw block (about 0.5 to 0.6 kg) creates sag and about 0.3 N·m of moment at 55 mm on the cell. Noise equals hung mass times vibration acceleration. The cell is in series with every test.
- Fixes: lower mass, a 5 to 10 Hz force filter, a flexure support (about 1% parallel stiffness), and avoiding a sliding guide.

### 4.3 Key CAD coordinates (moving-side layout)

X runs along the screw (0 = floating end, + toward the motor). Y across, Z up from the extrusion top.

Extrusion x −85 to 915. Rail 52 to 652. Screw 0 to 700 (thread 24 to 650). Floating block −6 to 22. Stationary jaws −80 to −10. Jaw gap G0 = 80 mm. Moving jaws 70 to 140. Clamp block 70 to 152. Cell 152 to 262 *(provisional placeholder)*. Nut-plate wall at x = 262. Carriage centres 111 and 291.5. Fixed block 655 to 683. Coupler centre 703. Gearbox face x = 728 (v3b, DTGN 5:1). Motor 800 to 854.5 (about 54.5 mm body). Updated CAD: `step_v3b_dtgn_gearbox`.

Jaws and blocks are 76 mm wide (bolt rows at ±29.5, M8). The floating boss is R15. A 5 mm clearance was added.

### 4.4 Printed vs aluminium parts

Eleven parts are exported as STEP/STL. The motor holder and saddle are recommended in aluminium. Full list in `hole_schedule.md` (earlier session).

### 4.5 Bearings

- **Fixed end:** a matched universal-ground pair of 7000-series angular contact bearings (10 × 26 × 8, 15° contact), back to back, shoulder on the load-cell side. Listed Cr 6.7 kN, C0 2.49 kN, static safety factor about 1.8 at 3 kN. Plus an M10 × 1 lock nut.
- **Floating end:** 608ZZ with an 8 mm circlip.

### 4.6 Stroke and specimen fit

Stroke 331 mm with an 80 mm jaw gap allows about 414% strain. The fixed-side layout (403 mm) allows about 134 mm free length at 300%.

---

## 5. Drive, forces and protection

- **Force at the nut:** F = 2πηT / lead, lead 4 mm, η about 0.9. About **6.35 kN per N·m** of motor torque through the 5:1 gearbox. Working loads (2 kN or less) need about 0.31 N·m at the motor and the 3 kN ceiling about 0.47 N·m, against 1.2 N·m holding torque, so the margin is about 2.5 to 4× at low speed. Check the motor's torque-speed curve at 625 rpm (500 mm/min).
- **Do not run the motor at its rated 2.9 A.** That would give about 7.6 kN at the nut and about 5.4 N·m at the gearbox output (rating not listed).
- **Driver minimum current.** TB6600 lowest DIP setting is typically 0.5 A, giving roughly **0.9 to 1.3 kN at the nut** *(est.)*. Just under the 100 kg cell's 1.47 kN safe overload: thin and unverified, so the torque-limiting coupler and software cutoff are still required.
- **Protection layers:** (1) software cutoff (about 880 N for the 100 kg cell, 3 kN for the 500 kg cell), (2) limit switches, (3) DC-rated e-stop contactor, (4) torque-limiting coupler at about 0.8 N·m at the screw, about 1.1 kN.
- **Never run the stall test on the 100 kg cell.** Use the 500 kg cell and a steel dummy.
- **Nut life (L10):** 256 km at 1 kN, 32 km at 2 kN, 9.5 km at 3 kN.
- **Buckling:** about 4.5 kN fixed-supported, about 2.2 kN pinned.
- **Back-drive:** about 0.05 N·m at 1 kN, near detent. Keep the motor energised while holding; run a creep test.
- **Speed:** about 0.8 × motor rpm in mm/min. 200 mm/min ≈ 250 rpm (6.7 kHz at 1/8). 500 mm/min ≈ 625 rpm (16.7 kHz).

---

## 6. Load cell and measurement

| Item | Value |
|---|---|
| Model | Dayang DYLY-103, 100 kg, S/N A26606087, calibrated 2026-07-01 |
| Class | C3 |
| Safe overload | 150 kg (about 1.47 kN) |
| Sensitivity | 1.99813 mV/V |
| Excitation | 5 to 15 V DC |
| Impedance | Input 384 ± 5 Ω, output 350 ± 1 Ω |
| Wires | Red +exc, black −exc, green +sig, white −sig, yellow shield |
| Type | S-beam with female-threaded bores |

Not known yet: overall length, thread size and depth, bore spacing, body dimensions, moment rating. HX711 at 3.3 V and 80 SPS, on the moving assembly. C3 accuracy about ±0.2 N. Use rod ends or spherical joints at both ends of the cell. 500 kg PSD-S1 kept for stall and coupler tests; 10 kg cell is the fallback for fabrics.

---

## 7. Electronics (handover state; firmware decisions supersede where they differ)

- Teensy 4.1. **No 74AHCT244 buffer actually fitted (confirmed 2026-10-26, decisions.md, issue #18) -- this line was wrong.** Teensy GPIO wires directly to the TB6600 opto inputs, common-anode @ +5 V, active-low. No pull-down on ENA, no pull-up on STEP/DIR anywhere: a confirmed, unfixed safety gap.
- **TB6600 (verify on the board):** no fault output; no automatic idle-current reduction; ENA often works inverted (asserting releases the motor, unconnected leaves it enabled); optos may limit pulse rate to tens of kHz.
- **Supply:** 24 V through a DC-rated e-stop contactor. Keep motor return off logic ground.
- Limit switches NC. Shielded motor cable, shield at the driver end.
- Pin map: see the firmware skill (supersedes the handover pin list).

---

## 8 to 11. Firmware, PC, impedance, specimens

Superseded for firmware purposes by the firmware skill and `docs/decisions.md`. Key specimen facts:
- Wire break loads: 20 AWG ~100 N, 24 AWG 40 to 50 N, 28 AWG 15 to 20 N, 30 AWG ~10 N, 32 to 34 AWG 4 to 7 N, 36 AWG 2.5 to 3 N. Knit fabric 40 mm wide at 100% strain: 10 to 60 N.
- Keep specimen failure load under ~800 N on the 100 kg cell.
- Dog-bones: ASTM D638 Type IV (ABS), ASTM D412 Die C (neoprene); dimensions from memory, verify against the standards.
- Standard fabric specimen ~100 mm free length × 40 mm wide; scale crosshead speed with length to hold strain rate.

---

## 13. Open items (mechanical / hardware)

- Load cell dimensions; Siglent AWG option; 4040 slot and T-nut; carriage height; nut measurements; gearbox flange details; the real coupler; motor label and shaft.
- Moving-side vs fixed-side layout.
- Specimen free length (~100 mm suggested).
- TB6600 DIP current table, voltage rating, opto type, ENA polarity.

## 14. Corrections made along the way

- Stroke 269 → 221 → 331 mm (v3).
- 0.5 A current cap assumption invalidated earlier; coupler and software cutoff added.
- Nut flange hole angles fixed to 0/60/120°.
- Scope is 12-bit, not 8-bit.
- Driver is a TB6600, not a DM-series unit.
- Gearbox is a DTGN PX57 5:1, not an MG10 10:1; force, speed and step-rate numbers halved or doubled accordingly.
