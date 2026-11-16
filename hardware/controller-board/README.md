# Controller board (rev 0.1, rough)

Phase 1 controller PCB: Teensy 4.1 socket, TB6600 interface, HX711 cable, limits,
e-stop sense, operator panel, OLED, video-sync LED, and a header for the phase 2
continuity board. **Placement only: not routed, not reviewed for manufacture.**

Open `tensometer-controller.kicad_pro` in KiCad 10. The files were generated with
KiCad 7 tooling (the newest available in the build environment); KiCad 10 opens them
and asks to save in its own format, which is fine.

| File | What |
|---|---|
| `tensometer-controller.kicad_sch` | Schematic, one A2 sheet, net labels per pin |
| `tensometer-controller.kicad_pcb` | 125 x 88 mm, 2 layers, parts placed, GND pour both sides, unrouted |
| `tensometer-controller-schematic.pdf` | Schematic printout |
| `renders/pcb-top-placement.png` | Top-side placement render (pour hidden) |
| `tensometer.kicad_sym`, `tensometer.pretty/` | Project library: Teensy 4.1 symbol and socket footprint |
| `tools/` | Generators: `design.py` is the parts/nets source; `build.sh` rebuilds and checks everything |

## Checks done

- The netlist KiCad exports from the schematic matches `tools/design.py` exactly (49 nets, 65 parts).
- Every PCB pad carries the same net as the schematic; footprints are linked to symbols by UUID, so *Update PCB from Schematic* recognises them.
- DRC: no courtyard overlaps, clearance or off-board problems. Remaining: unrouted connections (expected) and cosmetic silkscreen overlaps.
- Not done: ERC in KiCad (the CLI used had no ERC), routing, 3D/mechanical fit, a design review.

## Design choices that need Mathew's OK

1. **74AHCT244 buffer between the Teensy and the TB6600** (proposed, not approved; issue #18). The TB6600 optos sit at 5 V and the Teensy drives 3.3 V: a HIGH pin can leave an opto partly on, opto current (7-16 mA) is at or above the Teensy's 10 mA pin limit, and in reset the optos back-feed the Teensy pins. The buffer runs at 5 V, two gates per signal.
2. **Reset-safe pulls at the buffer inputs**: ENA 10k to GND (driver disabled), STEP/DIR 10k to 3.3 V. With the buffer they no longer depend on the TB6600's unknown internal resistor.
3. **E-stop sense polarity**: 24 V after the contactor lights the PC817, pulling `ESTOP_SENSE` LOW. E-stop pressed or wire broken reads HIGH (= `estop_active`, fail-safe).
4. **Logic supply from 24 V before the contactor** (J1), so the Teensy keeps logging and reports the e-stop when the motor supply is cut. J6 senses the contactor output.
5. **HX711 off-board on the carriage**, fed from its own TLV75533 LDO (3.3 VA) through J3.
6. **Teensy VUSB-VIN pad must be cut**; the board powers VIN through D4.

## Connector map

| Ref | Connector | Pins |
|---|---|---|
| J1 | 5.08 mm screw, 2 | +24 V (before contactor), 0 V |
| J2 | 5.08 mm screw, 6 | PUL+ PUL- DIR+ DIR- ENA+ ENA- (to TB6600; the + terminals are 5 V) |
| J3 | JST-XH 4 | 3V3A, GND, DOUT, SCK (to the HX711 module) |
| J4 / J5 | JST-XH 2 | LIM_NEAR / LIM_FAR: NC switch to GND |
| J6 | 5.08 mm screw, 2 | 24 V after the contactor, 0 V |
| J7 | IDC 2x5 | START, STOP, JOG+, JOG-, HOME, TARE, FAULT LED (330 R on board), 3V3, GND, GND |
| J8 | 1x4 header | OLED: GND, 3V3, SCL, SDA |
| J9 | JST-XH 2 | Sync LED: +5 V, LED cathode (100 R on board, ~20 mA) |
| J10 | 2x8 header | Phase 2: ADC_A0-3, ADC_B0-3, V_EXC_SENSE, I2C, 3V3, 5V, GND |

## Next steps

Approve or change the choices above, route (2 layers is enough), run ERC/DRC in KiCad 10, then a review before ordering.
