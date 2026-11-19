# Hole schedule (v3e: through-bolts with captured nuts on the M5 joints)

Checked against the STEP geometry on 2026-10-26 (cylindrical faces counted per part). Corrections from that check are marked REVIEW.

| Part | Feature | Qty | Note |
|---|---|---|---|
| Stationary jaw plates (Al, x2) | 6x M8 clearance (O9.0) through | 6 | bolt rows y=+/-29.5, x = x0+10, mid, x1-10 |
| Moving jaw plates (Al, x2) | 6x M8 clearance (O9.0) through | 6 | bolt rows y=+/-29.5, x = x0+10, mid, x1-10 |
| Stationary clamp block | 6x O10.5 x 14 deep insert pocket (M8 heat-set) | 6 | top face, matches jaw bolts |
| Stationary clamp block | 4x O6.6 side holes (M6 to T-nut) | 4 | through skirts at z=-20, slot width TBC |
| Clamp block on carriage 1 | 4x M4 clearance O4.5 + O8.4 counterbore 3 deep | 4 | 26x26 pattern to HGH15CA; M4x10; fit BEFORE the lower jaw plate |
| Clamp block on carriage 1 | 6x O10.5 x 14 deep insert pocket (M8 heat-set) | 6 | top of legs, matches jaw bolts |
| Clamp block on carriage 1 | O13.5 clearance hole in the end wall (cell stud) | 1 | replaces the O17 placeholder |
| Nut plate on carriage 2 (alu above ~1 kN) | 4x M4 clearance O4.5 + O8.4 counterbore 3 deep | 4 | 26x26 to HGH15CA; M4x10; two have O9.4 tool slots through the wall |
| Nut plate on carriage 2 | O13.5 clearance hole in the wall (cell stud) | 1 | replaces the O17 placeholder |
| Nut plate on carriage 2 (alu above ~1 kN) | 6x O5.5 through (M5) + hex nut pockets 8.6 AF x 9.7 across corners x 5.2 deep on the +x face | 6 | PCD 32 at 0/45/135/180/225/315 deg; M5x25 from the -x side with a nut captured on the +x face; the four 45 deg nuts sit in two vertical slots (y=+/-11.3) |
| Floating-end block | O22.1 x 7.3 deep bearing pocket (608ZZ) | 1 | +x face; bore O10.4 through; boss grown R14->R15 for wall (3.95 mm) |
| Floating-end block | 4x O6.6 side holes (M6 to T-nut) | 4 | through skirts at z=-20 |
| Fixed (thrust) block BK10 | O13 bore, 12 mm shoulder on the -x side | 1 | stops outer rings under screw tension |
| Fixed (thrust) block BK10 | O26.2 x 16 deep seat for 2x 7000 angular contact, back to back | 1 | from the +x face; retained by an Al cap |
| Fixed (thrust) block BK10 | 4x O4.2 x 8 deep (M3 heat-set) on PCD 35 | 4 | for the 4 mm Al retaining cap (cap not modelled) |
| Fixed (thrust) block BK10 | 4x O6.6 side holes (M6 to T-nut) | 4 | through skirts at z=-20 |
| Motor holder (alu recommended) | O38.6 x 3.4 deep pilot recess (gearbox pilot O38.1 x 3 proud) | 1 | +x face |
| Motor holder (alu recommended) | O31 through bore | 1 | shaft and coupler clearance |
| Motor holder (alu recommended) | 4x O6.6 side holes (M6 to T-nut) | 4 | skirts at FACE_X-36..-4 |
| Motor holder (alu recommended) | 4x O5.5 through (M5) + hex nut pocket 8.6 AF x 9.7 across corners x 5.2 deep on the -x face, on a 47.14 mm square | 4 | DTGN gearbox: M5x30 bolts pass through the gearbox O5.5 flange holes from the motor side and the holder; nut captured on the -x face; alu version: tap M5 instead |
| Gearbox saddle (suggested support) | 4x O6.6 side holes (M6 to T-nut) | 4 | new skirts |

## Fasteners implied
M4x10 (carriage to block/plate, x8, with washers), M5x25 + M5 nuts (nut flange, x6, low-head or ball-end key because the head is close to the nut body), M5x30 + M5 nuts (gearbox to holder, x4, length depends on the flange thickness); M12 studs + 8 nuts for the cell (CAD stud lengths 57.8 and 60.8 mm; see the thread-depth warning below), M8x55 (jaw bolts, x12) + M8 heat-set inserts (x12), M6 + side-slot T-nuts (x20), M3 (bearing cap, x4).

## Provisional / open
- SFU1204 flange pattern is read from a photo (6 holes at 0/45/135/180/225/315 deg on PCD about 32, clearance about O5.5 for M5, flats about 30 across). Hole diameter, PCD, flange thickness and flats distance should be confirmed with calipers.
- Gearbox is the DTGN PX57 5:1 planetary (not the MG10). Holder holes now follow the seller drawing: 4x M5 on a 47.14 mm square, O38.1 x 3 pilot, O14 x 22 shaft. Flange thickness, round-body diameter and rear adapter size are ASSUMED in the CAD; measure them.
- Holder moved 12 mm toward the screw because the shaft is 22 mm long (was 34); coupler is still modelled as O26 x 30 with the shaft tip at its centre. Confirm the real coupler.
- Load cell now uses the measured body 76.24 x 50.89 x 19 (assumed 76.24 along the pull, 50.89 across, 19 vertical) with M12 female bores (measured O10.33, i.e. M12; pitch 1.75 vs 1.5 and thread depth still to confirm, 18 mm assumed).
- The cell is joined with separate M12 studs and jam nuts: O13.5 clearance holes in the clamp block end wall and in the nut plate wall (replacing the old O17 M16 placeholders). Outer nut on the clamp-block side sits in the jaw gap and is reached with a thin spanner from the side; clamped specimen tab must stay under about 59 mm.
- 4040 slot width (8 or 10 mm) fixes the M6 T-nut choice.
- Floating-end boss grown R14 -> R15 (bearing-pocket wall); specimen underside clearance is now 5 mm, not 6.
- Clamp blocks and jaws widened 68 -> 76 mm to fit M8 inserts outside the specimen.
- Nut-flange hole angles are 0/45/135/180/225/315 deg (an earlier 60 deg layout put two holes outside the flats; that is fixed in this version).

## Features in the CAD that were missing from the schedule (REVIEW, 2026-10-26)
| Part | Feature | Note |
|---|---|---|
| Nut plate | O15 through bore along X | screw clearance (screw is O12) |
| Clamp block on carriage 1 | O16 through bore along X | screw clearance (screw is O12) |
| Floating-end block | O30 bore/boss along X | boss around the bearing pocket |
| Fixed (thrust) block BK10 | O44 bore/boss along X | check against the real BK10 housing |

## Review warnings (REVIEW, 2026-10-26)
- **Cell stud thread depth:** the studs engage 18.0 mm into each cell bore, equal to the ASSUMED full thread depth. Shorten the studs by about 3 mm or measure the real depth before ordering.
- **Stroke margin:** the 334.76 mm stroke puts carriage 2 (317.24 at start) at exactly x = 652.0, the rail end, with 0 mm margin and 3.2 mm to the BK10 block. Carriage length in CAD is 59 mm; confirm with calipers (the HGH15CA catalogue figure is about 61.4 mm, from memory).
- **Axial load path:** the stationary block, BK10 and motor holder are held to the 4040 by M6 side bolts and T-nuts; along the slot this is friction only. A positive stop is recommended (estimate, not tested).
- **Heat-set inserts still in the CAD:** 12x O10.5 (M8) in the two clamp blocks and 4x O4.2 (M3) in BK10. Decision to convert the M8 ones to through-bolts is pending.
- **Bearing pocket:** O22.1 for a 608ZZ (O22). Printed holes usually shrink; test-print before committing to this size.
