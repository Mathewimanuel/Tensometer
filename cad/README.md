# CAD

Mechanical layout of the ball-screw tensometer. Current version: **v3e** (`cad/v3e/`). All dimensions in mm, STEP (AP214) plus STL per part, all in assembly coordinates, so any set of parts drops into place together.

## Coordinate system

X runs along the screw, 0 at the floating end, + toward the motor. Y is across the machine (centre 0). Z is up from the top of the 4040 extrusion. Screw axis: Z = 48. Specimen / load line: Z = 78.

Key positions: extrusion X -85..915, rail 52..652 (HGR15, 600 mm), screw 0..700, floating block -6..22, stationary jaws -80..-10, minimum jaw gap G0 = 80, moving jaws 70..140, cell 167..243, nut plate 258..317, BK10 thrust block 655..683, coupler centre 703, gearbox face 728, motor 800..854.

## Files in `v3e/`

| File | What it is | Made by |
|---|---|---|
| `full_assembly_layout.step` | 24 solids: extrusion, rail, screw, carriages, ball nut, coupler, gearbox, motor, specimen model and every part below | reference only |
| `stationary_clamp_block`, `stationary_upper_jaw_al`, `stationary_lower_jaw_al` | fixed clamp end | print / Al |
| `clamp_block_on_carriage_1`, `moving_upper_jaw_al`, `moving_lower_jaw_al` | moving clamp end | print / Al |
| `nut_plate_on_carriage_2_alu_above_1_kn` | holds the SFU1204 flange (6x M5 through-bolts with captured nuts) | Al |
| `s_beam_load_cell_dyly_103_measured_76_24x50_89x19_m12` | DYLY-103 100 kg cell, measured body, M12 female bores | purchased (model) |
| `m12_studs_nuts_cell_connection` | M12 studs and jam nuts joining the cell to both blocks | purchased (model) |
| `floating_end_block` | 608ZZ bearing support, floating end | print |
| `fixed_thrust_block_bk10` | 2x 7000 angular contact seat, takes the screw thrust | print / Al |
| `motor_holder_alu_recommended` | DTGN PX57 gearbox flange mount, 4x M5 through-bolts | Al |
| `gearbox_saddle_suggested_support` | support under the gearbox rear adapter | print |
| `hole_schedule.md` | every hole, fastener and open item (checked against the geometry) | |
| `images/` | sections of the cell connection and the M5 nut pockets | |

## Provisional values (placeholders in the CAD, need a real measurement)

Gearbox flange thickness, round-body diameter and rear adapter size; coupler (modelled O26 x 30); SFU1204 flange pattern (read from a photo); cell thread pitch, depth and cable exit; 4040 slot width and T-nut size; HGH15CA carriage length and height.

## Open review items (2026-10-26)

1. **Stroke has no margin.** 334.76 mm travel puts carriage 2 exactly at the rail end. Set the usable stroke to about 320 mm and put the limit switches inside it.
2. **Stud engagement equals the assumed thread depth** (18 mm). Shorten the studs about 3 mm or measure the real depth.
3. **Axial loads rely on T-nut friction** for the stationary block, BK10 and motor holder. Add a positive stop (estimate, not tested).
4. **Heat-set inserts remain** (12x M8 in the clamp blocks, 4x M3 in BK10). Conversion of the M8 ones to through-bolts is awaiting a decision.
5. **608ZZ pocket is O22.1** and will print undersize; test-print first.

## Not in the repo

The parametric build scripts (build123d) were lost when the working session reset, so only exported geometry is here. Edit the STEP files directly, or rebuild the model from `hole_schedule.md` and the coordinates above.
