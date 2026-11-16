# Controller board (rev 0.2): routed, JLCPCB-ready

Phase 1 controller PCB: Teensy 4.1 socket, TB6600 interface, HX711 cable, limits,
e-stop sense, operator panel, OLED, video-sync LED, and a header for the phase 2
continuity board. 125 x 88 mm, 2 layers, 1 oz, GND plane on the bottom.

Open `tensometer-controller.kicad_pro` in KiCad 10. The files are KiCad 7 format (the
newest KiCad available in the build environment); KiCad 10 opens them and asks to save
in its own format.

| Path | What |
|---|---|
| `tensometer-controller.kicad_sch` / `-schematic.pdf` | Schematic (one A2 sheet, a net label per pin) |
| `tensometer-controller.kicad_pcb` | Routed board: 0.25 mm signals, 0.4-0.6 mm power, 0.6/0.3 mm vias, GND pours both sides |
| `jlcpcb/gerbers.zip` | Gerbers + Excellon drill for JLCPCB |
| `jlcpcb/BOM.csv`, `jlcpcb/CPL.csv` | JLCPCB assembly BOM and pick-and-place (JLC column format) |
| `renders/` | Routed copper, and the Gerbers rendered back from the zip (gerbv) |
| `tensometer.kicad_sym`, `tensometer.pretty/` | Project library: Teensy 4.1 symbol and socket footprint |
| `tools/` | `design.py` (parts, nets, LCSC numbers) and the scripts that rebuild everything: `KRT_DIR=... bash tools/build.sh` |

## Checks done

- Schematic: the netlist KiCad exports matches `design.py` exactly (49 nets, 65 parts).
- PCB: every pad carries the schematic's net; footprints linked to symbols by UUID.
- KiCad DRC on the routed board: **0 unconnected, 0 clearance / width / via / edge errors**. Left: 4 silkscreen text overlaps and 1 silk-near-edge (cosmetic), and library warnings that only appear because this machine has no global KiCad library table.
- Gerbers rendered back with gerbv: outline, copper, drills and silk as expected.
- Every LCSC number checked against the JLC parts database (jlcparts snapshot; stock figures fetched between 2026-09-21 and 2026-10-28, so re-check stock when ordering).
- Not done: ERC (no CLI ERC in KiCad 7), 3D/mechanical fit, an independent design review.

## Parts changed for JLCPCB

| Ref | Was | Now (LCSC) | Why |
|---|---|---|---|
| U3 | 74AHCT244 SOIC-20W | 74AHCT244PW TSSOP-20 (C135583) | SOIC-20W not stocked at JLC; same pinout |
| U5 | PC817 DIP-4 | LTV-817S SMD-4 (C109227) | JLC **basic** part, SMD |
| U1 | RECOM R-78E5.0-1.0 | YLPTEC R-78E5.0-1.0 (C22371890) | The stocked R-78E at LCSC; 7-36 V in, 1 A |
| D2 | SMBJ30CA bidirectional | SMBJ28A unidirectional (C113997) | Clamps lower (45 V vs 48 V), input is already reverse-protected by D1 |
| C1 | 10 uF 50 V 1210 | 1206 (C13585) | JLC basic part |
| C2 | 22 uF 10 V 1206 | 22 uF 25 V 0805 (C45783) | JLC basic part |
| R23 | 100k (C17407 now out of stock) | C149504 | Current basic 100k |
| J2 | Fixed 6-way screw terminal | Pluggable 5.08 mm, right angle (C441208) | No 6-way fixed block stocked; the TB6600 cable now unplugs. **Order the mating plug C440884 separately** |
| J8 | Male 1x4 header | Female 1x4 (C541851) | The OLED module has male pins |

All 0805 resistors, the 0805/1206 capacitors, SS34, SS14, 1N4148W, the green LED, AO3400A
and the LTV-817S are **JLC basic** parts. Extended parts: F1, D2, U1, U3, U4 and the
connectors.

## Ordering at JLCPCB

1. PCB: upload `jlcpcb/gerbers.zip`. 2 layers, 1.6 mm, 1 oz, any colour/finish.
2. Assembly: top side. Upload `BOM.csv` and `CPL.csv`.
3. **Check every IC and diode orientation in JLC's placement preview** before paying. The CPL applies the usual KiCad-to-JLC rotation fixes (SOT-23 -90, TSSOP 270), but those are community values, not a JLC guarantee. U5 (LTV-817S) and U3 deserve a close look.
4. Cheapest route: let JLC place only the SMD parts and hand-solder the through-hole ones (U1 and all connectors): untick those BOM lines. Each extended part carries JLC's extended-part fee.
5. Not on the BOM: the Teensy itself; 2x 1x24 female headers for its socket (C41376163, hand-solder); the J2 mating plug (C440884).

## Before power-up

- **Cut the Teensy's VUSB-VIN pad**; the board feeds VIN through D4.
- Bench bring-up step 2 (firmware skill section 13): with the Teensy held in reset, the TB6600 must stay disabled.

## Design choices awaiting Mathew's OK (docs/decisions.md)

1. 74AHCT244 buffer between Teensy and TB6600 (3.3 V vs 5 V, 10 mA pin limit, reset back-feed); 10k reset-safe pulls at its inputs (issue #18).
2. E-stop sense via LTV-817S: LOW = 24 V present after the contactor, HIGH = e-stop pressed or wire broken.
3. Board logic powered from 24 V **before** the contactor (J1); J6 senses the contactor output.
4. HX711 off-board on the carriage, own TLV75533 LDO.

## Unverified, check against datasheets

- LTV-817S land pattern: uses KiCad's `SMDIP-4_W9.53mm` (standard gull-wing DIP-4).
- YLPTEC R-78E5.0-1.0 pinout assumed identical to RECOM (1 Vin, 2 GND, 3 Vout).
- KF2EDGR-5.08-6P and WJ500V-5.08-2P use Phoenix footprints with the same 5.08 mm pitch; body outlines differ slightly from the silkscreen.

## Connector map

| Ref | Connector | Pins |
|---|---|---|
| J1 | 5.08 mm screw, 2 | +24 V (before contactor), 0 V |
| J2 | 5.08 mm pluggable, 6 | PUL+ PUL- DIR+ DIR- ENA+ ENA- (to TB6600; the + terminals are 5 V) |
| J3 | JST-XH 4 | 3V3A, GND, DOUT, SCK (to the HX711 module) |
| J4 / J5 | JST-XH 2 | LIM_NEAR / LIM_FAR: NC switch to GND |
| J6 | 5.08 mm screw, 2 | 24 V after the contactor, 0 V |
| J7 | IDC 2x5 | START, STOP, JOG+, JOG-, HOME, TARE, FAULT LED (330 R on board), 3V3, GND, GND |
| J8 | 1x4 female | OLED: GND, 3V3, SCL, SDA |
| J9 | JST-XH 2 | Sync LED: +5 V, LED cathode (100 R on board, ~20 mA) |
| J10 | 2x8 header | Phase 2: ADC_A0-3, ADC_B0-3, V_EXC_SENSE, I2C, 3V3, 5V, GND |

## How it was routed

`tools/route_board.py` drives [KiCad Routing Tools](https://github.com/drandyhaas/KiCadRoutingTools)
(MIT): a GND plane on B.Cu, then every net at 0.25 mm / 0.2 mm clearance with power nets
0.4-0.6 mm and vias never below 0.6/0.3 mm. The router writes KiCad 9 syntax; the script
converts it to the KiCad 7 format used here, adds the F.Cu GND pour and refills.
