# Controller board (rev 0.4): reviewed, routed, JLCPCB-ready

Phase 1 controller PCB: Teensy 4.1 socket, TB6600 interface, HX711 cable, limits,
e-stop sense, operator panel, OLED, video-sync LED, and a keyed header for the phase 2
continuity board. **65 x 72 mm** (rev 0.2 was 125 x 88), 2 layers, 1.6 mm, 1 oz, GND
poured on both layers.

Open `tensometer-controller.kicad_pro` in KiCad 10. The files are KiCad 7 format (the
newest KiCad available in the build environment); KiCad 10 opens them and asks to save
in its own format.

| Path | What |
|---|---|
| `tensometer-controller.kicad_sch` / `-schematic.pdf` | Schematic (one A2 sheet, a net label per pin) |
| `tensometer-controller.kicad_pcb` | Routed board (rules below) |
| `jlcpcb/gerbers.zip` | Gerbers + Excellon drill (plated and non-plated holes in separate files) |
| `jlcpcb/BOM.csv`, `jlcpcb/CPL.csv` | JLCPCB assembly BOM and pick-and-place (JLC column format) |
| `renders/` | Routed copper, and the Gerbers rendered back from the zip (gerbv) |
| `tensometer.kicad_sym`, `tensometer.pretty/` | Project library: Teensy 4.1 symbol and socket footprint |
| `tools/` | `design.py` (parts, nets, LCSC numbers, MPNs) and the scripts that rebuild everything: `KRT_DIR=... bash tools/build.sh` |

## What changed in rev 0.4 (independent review + requirements pass)

| Item | Rev 0.3 | Rev 0.4 | Why |
|---|---|---|---|
| 5 V converter U1 | YLPTEC R-78E5.0-1.0, 7-36 V in | **RECOM R-78HB5.0-0.5, 9-72 V in (75 V max)**, 5 V 0.5 A, 3-pin SIP on KiCad's exact footprint | The SMBJ28A TVS only starts conducting at 31 V and clamps at up to ~45 V; a contactor-coil kick could exceed the old converter's 36 V rating. (A TRACO TSR 1-4850WI was tried first; the re-review caught that its vertical version has two extra case pins the footprint lacks.) Not stocked at LCSC: buy from Farnell/RS/Mouser and hand-solder |
| STEP/DIR pull-ups | to the Teensy's +3V3 | to **3V3_PU from U6**, a second TLV75533 on +5V | The Teensy's 3V3 is missing when it is unplugged, not yet up or switched off; STEP/DIR then sat LOW (opto ON) and gave a STEP edge at power-up/unplug, failing bring-up step 2 |
| Input bulk cap | none | **C16 100 uF 50 V** electrolytic on VIN_24 | Damps cable/contactor ringing; a ceramic alone rings |
| F1 | 0.5 A / 30 V PTC | 0.5 A / **60 V** PTC | A tripped PTC holds the full supply voltage; 30 V had no margin over 24 V + transients |
| 24 V tracks | 0.5 mm, 0.2 mm clearance | **1.0 mm** (+24V, 24V_F, VIN_24), **0.5 mm clearance** for the 24 V supply nets (+24V, 24V_F, VIN_24) and **0.3 mm** for the 24 V sense nets (24V_SW, ESTOP_RTN, OPTO_A) | Inrush, fuse-trip and TVS currents; a solder bridge or debris between 24 V and a 3.3 V signal is the failure that kills the Teensy |
| E-stop sense return | J6 pin 2 tied to board GND | **Own net ESTOP_RTN** (J6.2, U5 LED cathode, D5) | The opto now actually isolates. Swapped J6 wires only reverse-bias the LED (D5 carries ~2.4 mA through R11) instead of putting post-contactor 24 V onto GND |
| J10 expansion | Unshrouded 2x8 pins | **Keyed 2x8 box header** (C492443) + **1k in series** on all 9 analog lines (RN5, RN6, R25) + **100R** on SDA/SCL (R26, R27) | A reversed plug put +5 V on an ADC pin; the 1k limits clamp current to ~1.7 mA if the phase 2 board ever drives above 3.3 V |
| Panel buttons | Straight to Teensy pins | **1k in series** from the cable (RN1, RN2, next to J7); 10k pull-ups (RN3, RN4) and 100n at the pin | ESD / miswiring current limit on a cable that leaves the box |
| OLED J8 | 1x4 socket the module plugged into (it could not physically sit there) | **JST-PH 4 cable connector**; mount the OLED on the operator panel | PH (2.0 mm) so it cannot be mated with the HX711's JST-XH 4 by mistake |
| microSD access | J4/J5 cables rose right in front of the Teensy's card slot | Limits and sync LED moved to the right edge; only flat test pads in front of the slot | The firmware logs to SD (skill section 9) |
| Decoupling | C1 14.7 mm from U1, C4 5 mm from U3, C6 6.8 mm from U4 | C1 and C2 directly under U1's pins; C4 beside U3 pin 20; C5/C6 at U4's pins | Reviewer findings |
| Test points | none | TP1-TP7: VIN_24, +5V, +3V3, +3.3VA, ENA_N, 2x GND (bare pads, not assembled) | Bench bring-up step 2 (ENA state with the Teensy in reset) |
| Drill file | PTH and NPTH merged | Separate files | Otherwise JLC may plate the M3 holes |
| Placement rotations | SOT-23, TSSOP only | Also SMDIP-4, resistor arrays, electrolytic (see checklist below) | |
| Silkscreen | DRC ignored silk overlaps | Counted in the routed-board DRC (0 now); connector labels on the board | |
| Board | 57.5 x 72 mm | 65 x 72 mm | Two box headers and a column of cable connectors on the right; still 57% smaller than rev 0.2 |

All of it is **PROPOSED** in `docs/decisions.md`, like the earlier board choices.
Ordering this board effectively approves them, and the firmware skill (section 2.1
still describes the unbuffered wiring) must be updated when they are approved.

## Clearance under the Teensy

The Teensy sits in two 8.5 mm female headers (HC-PM254-8.5H, C41376163). With its own
2.5 mm pin spacer that puts the bottom of the Teensy PCB about **11 mm** above this
board. The tallest parts underneath are D4 (SMA, 2.4 mm max) and the 0603x4 arrays
(0.5 mm); the optional PSRAM/flash chips on the Teensy's underside hang ~1.8 mm, leaving
~7 mm. **Do not solder the Teensy flush** (straight to the board) or use low-profile
sockets: the parts under it would collide. The VUSB-VIN cut is on the Teensy's
underside; do it before plugging it in.

## Layout rules

| Item | Rule | Why |
|---|---|---|
| Signal tracks | 0.25 mm, 0.2 mm clearance | Inside JLC's standard tolerance with margin. IPC-2221 B4 (coated, up to 50 V) needs 0.13 mm |
| 24 V field nets | 0.5 mm clearance to everything for +24V, 24V_F, VIN_24 (net class Field24V); 0.3 mm for 24V_SW, ESTOP_RTN, OPTO_A (Field24VSense) | Margin against bridges/debris, see above. The sense nets carry a few mA behind R11, and 0.5 mm on all six nets would not route on this board; 0.3 mm is still above IPC-2221 B4 (0.13 mm up to 50 V) |
| Power tracks | 1.0 mm: +24V, 24V_F, VIN_24; 0.5 mm: +5V, VIN_T, GND; 0.4 mm: +3.3VA, 24V_SW, ESTOP_RTN; 0.25 mm: +3V3 | IPC-2221, 1 oz, 10 C rise: 1.0 mm ~2.5 A, 0.5 mm ~1.5 A, 0.25 mm ~0.8 A. The 5 V rail is under 0.5 A; +3V3 only feeds pull-ups, the OLED, the panel LED and phase 2 logic |
| Vias | 0.6 mm pad / 0.3 mm drill, tented | JLC standard, no surcharge |
| Via-in-pad | None | Unfilled vias wick solder |
| GND | Pour on both layers, 0.3 mm clearance, thermal reliefs (0.5 mm spokes), islands removed; stitching vias on a 5 mm grid plus a ring 1.6 mm in from the edge; a dedicated via at every two-pad SMD GND pad; stitching vias that would only tie floating pour scraps together are removed | One low-impedance ground; decoupling caps get the shortest return; no floating copper |
| Edge | Copper 0.5 mm from the board edge | |
| Fiducials | 3 (1 mm copper, 2 mm mask opening) | For the SMT line |

Tracks neck down to the pad width where they enter a small pad (a fraction of a mm).

## Phase 2 expansion (J10): how it ties in

J10 is the only link to the future continuity/routing board (firmware skill sections
2.4-2.5). Pin 1 = ADC_A0 ... pin 8 = ADC_B3 (each through 1k to Teensy pins 16, 17, 20,
21, 22, 23, 24, 25), pin 9 = V_EXC_SENSE (1k to pin 26), 10/15/16 = GND, 11/12 = I2C
SDA/SCL (via 100R; shared with the OLED, 4k7 pull-ups on this board), 13 = +3V3, 14 = +5V.

- The phase 2 board does all its own analog: precision V_EXC supply, reference
  resistors, analog switches, instrumentation amps, clamps. This board only carries the
  finished 0-3.3 V signals to the ADC pins.
- Relays (via MCP23017 + ULN2803 on the phase 2 board) are driven over the shared I2C.
  The MCP23017 must run at 3.3 V (the bus pull-ups go to +3V3).
- Power budget: +5V comes from the 0.5 A R-78HB. This board uses ~0.25 A (Teensy, opto
  drive, sync LED, HX711 LDO), leaving ~0.25 A for phase 2: relays (at most two
  energised), V_EXC regulator, amps. That is enough for signal relays, but if phase 2
  needs more it should take its own supply. Total capacitance on +5V must stay under
  100 uF (converter limit for a normal start-up); this board has ~35 uF plus the Teensy's own input capacitance. +3V3 comes from the Teensy's ~250 mA regulator, which already
  feeds pull-ups, the OLED and the panel: phase 2 should use it for logic levels only and
  make its own 3.3 V for anything else.
- No phase 2 board plugged in: the ADC pins float through 1k; the firmware treats the
  continuity module as absent (module presence check, skill section 1.2).

## Checks done

- Schematic: the netlist KiCad exports matches `design.py` exactly (68 nets, 81 parts).
- PCB: every pad carries the schematic's net; footprints linked to symbols by UUID.
- KiCad DRC on the routed board, silkscreen included, with the 0.5 mm 24 V class: **0 errors, 0 unconnected**. Left: library warnings that only appear because this machine has no global KiCad library table.
- Separate GND check: every GND pad, track, via and pour fragment is one connected copper island.
- Mask Gerber has no openings at vias (tented).
- Gerbers rendered back with gerbv: outline, copper, drills and silk as expected.
- kicad-happy (github.com/aklofas/kicad-happy) analyzers and an independent reviewer agent on rev 0.3 and again on rev 0.4 (results in `docs/decisions.md`).
- Every LCSC number checked against the JLC parts database (jlcparts snapshot; stock fetched 2026-09-15..10-28, so re-check stock when ordering). U1 (R-78HB5.0-0.5) is not stocked at LCSC: Farnell, RS and Mouser carry it.
- Not done: ERC (no CLI ERC in KiCad 7), a 3D model fit check, SPICE.

## Ordering at JLCPCB

1. PCB: upload `jlcpcb/gerbers.zip`. 2 layers, 1.6 mm, 1 oz, any colour/finish.
2. Assembly: top side. Upload `BOM.csv` and `CPL.csv`.
3. **Check every polarised part in JLC's placement preview** before paying (table below). A diode or IC rotated 180 degrees still fits its pads, so the preview is the only check. D2 backwards shorts 24 V and trips F1; U3 backwards dies.
4. Cheapest route: let JLC place only the SMD parts and hand-solder the through-hole ones (U1 and all connectors): untick those BOM lines (U1 must be bought separately anyway). Each extended part carries JLC's extended-part fee.
5. Not on the BOM: the Teensy itself; 2x 1x24 female headers for its socket (C41376163, hand-solder); the J2 mating plug (C440884); test points (bare pads).

| Part | What the preview must show (board viewed from the top, USB edge up) |
|---|---|
| D1 SS34 | Cathode band on the **right** |
| D2 SMBJ28A | Cathode band at the **bottom** (towards U3) |
| D3 LED, D4 SS14, D5 1N4148W | Cathode mark on the **left** |
| Q1 AO3400A, U4 TLV75533 | Pin 1 at the **top-left** (two pins on the left for Q1, three for U4) |
| U3 74AHCT244 | Pin 1 dot at the **top-left** |
| U5 LTV-817S | Pin 1 dot at the **top-left** (its rotation is the least certain; see `gen_fab.py`) |
| C16 electrolytic | **+** at the **bottom** |
| RN1-RN6 arrays | Not polarised, but must sit square on the 8 pads |

## Before power-up

- **Cut the Teensy's VUSB-VIN pad**; the board feeds VIN through D4.
- Bench bring-up step 2 (firmware skill section 13): with the Teensy held in reset, ENA_N (TP5) must sit at the level that keeps the TB6600 disabled.
- Known, by TB6600 design: a broken ENA wire or a dead +5 V rail leaves the driver **enabled** (no step pulses, so it holds position). The contactor/e-stop is what removes motor power.
- +5V and +3V3 leave the box unfused on J7, J9 and J10. A shorted cable takes the rail down (and with it the ENA opto, see above). Acceptable for a bench machine with an e-stop; add PTCs on the cables if that changes.
- Run the I2C bus at 100 kHz: the OLED is now on a cable and phase 2 adds more.
- JLC may want panel rails because parts sit close to the edges; check when quoting.

## Design choices awaiting Mathew's OK (docs/decisions.md)

1. 74AHCT244 buffer between Teensy and TB6600 (3.3 V vs 5 V, 10 mA pin limit, reset back-feed); 10k reset-safe pulls at its inputs (issue #18).
2. E-stop sense via LTV-817S with an isolated return: LOW = 24 V present after the contactor, HIGH = e-stop pressed or wire broken.
3. Board logic powered from 24 V **before** the contactor (J1); J6 senses the contactor output.
4. HX711 off-board on the carriage, own TLV75533 LDO.
5. Rev 0.4 changes in the table above.

## Unverified, check against datasheets

- LTV-817S land pattern: uses KiCad's `SMDIP-4_W9.53mm` (standard gull-wing DIP-4).
- R-78HB5.0-0.5: pinout 1 +Vin, 2 GND, 3 +Vout, 9-72 V in, 0.5 A, 11.5 x 8.5 x 17.5 mm (RECOM datasheet); KiCad's footprint for exactly this part. Buy the vertical (non-"L") version.
- 0603x4 arrays use KiCad's convex footprint; UNI-ROYAL 4D03 is the convex type (TODO(verify) against its datasheet).
- KF2EDGR-5.08-6P and WJ500V-5.08-2P use Phoenix footprints with the same 5.08 mm pitch; body outlines differ slightly from the silkscreen.

## Connector map

| Ref | Connector | Pins |
|---|---|---|
| J1 | 5.08 mm screw, 2 | +24 V (before contactor), 0 V |
| J2 | 5.08 mm pluggable, 6 | PUL+ PUL- DIR+ DIR- ENA+ ENA- (to TB6600; the + terminals are 5 V) |
| J3 | JST-XH 4 | 3V3A, GND, DOUT, SCK (to the HX711 module) |
| J4 / J5 | JST-XH 2 | LIM_NEAR / LIM_FAR: NC switch to GND (labelled on the board) |
| J6 | 5.08 mm screw, 2 | 24 V after the contactor, that supply's 0 V (isolated from board GND by the opto) |
| J7 | IDC 2x5 box | START, STOP, JOG+, JOG-, HOME, TARE, FAULT LED (330 R on board), 3V3, GND, GND |
| J8 | JST-PH 4 | OLED: GND, 3V3, SCL, SDA |
| J9 | JST-XH 2 | Sync LED: +5 V, LED cathode (100 R on board, ~20 mA) |
| J10 | IDC 2x8 box, keyed | Phase 2: ADC_A0-3, ADC_B0-3, V_EXC_SENSE (all via 1k), GND, I2C (via 100R), 3V3, 5V, GND, GND. The phase 2 board's cable must use the same notch-to-pin-1 convention |

## How it was routed

`tools/route_board.py` drives [KiCad Routing Tools](https://github.com/drandyhaas/KiCadRoutingTools)
(MIT, commit 6ae347ba): a GND plane on B.Cu, then every net at the widths above with
B.Cu made costly so the plane stays whole. The router writes KiCad 9 syntax; the script
converts it to KiCad 7, pours GND on F.Cu, fills, drops stitching vias where both pours
are solid (and into any walled-in pour fragment), refills with island removal, removes
stitching vias that only join floating scraps, and refills again. On a board this dense
the router's result swings with its settings, so the script retries a short list of
them until KiCad's DRC reports zero unconnected items. `tools/build.sh` fails if the
final DRC has any error.
