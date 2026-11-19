"""Limit-switch mounts for the v3e frame (LIM_NEAR and LIM_FAR).

One bracket design, printed twice. It bolts to the -Y side slot of the 4040 extrusion
(M6 + side-slot T-nut at z = -20, same convention as the other blocks) and holds a
roller-lever microswitch (KW12 / V-156 class) under the overhang of the moving parts:

  LIM_NEAR: tripped by the underside edge of the clamp block on carriage 1 (-X face, x = 70
            at G0) about 3 mm past the minimum gap, i.e. at a jaw gap of about 77 mm.
  LIM_FAR:  tripped by the underside edge of the nut plate on carriage 2 (+X face) after
            320 mm of travel, 14.8 mm before carriage 2 reaches the rail end (CAD README
            open item 1: usable stroke about 320 mm, switches inside it).

The roller rides on the flat underside of the block after it trips, so there is no hard
collision with the switch body during overtravel (82 mm of clamp-block underside and
59 mm of nut-plate underside are available, against 48 mm and 14.8 mm of overtravel).

Coordinates are the v3e assembly coordinates (see cad/README.md). All mm.
Run: python3 build.py   (needs build123d; writes STEP/STL here)
"""
from pathlib import Path

from build123d import Align, Box, Compound, Cylinder, Pos, Rot, export_step, export_stl

OUT = Path(__file__).parent

# ---------------------------------------------------------------- switch (placeholder)
# TODO(verify): every value in this block is a typical KW12-3 roller-lever figure, not a
# measurement of Mathew's switch. Measure the real part and update; the slots below give
# +/-3 mm of height adjustment and +/-5 mm along X to absorb the difference.
SW_L, SW_H, SW_T = 27.8, 15.9, 10.3       # body length (X), height (Z), thickness (Y)
SW_HOLE_D, SW_HOLE_PITCH = 3.1, 22.2       # 2x M3 through the body
SW_HOLE_Z = 5.1                            # hole centre above the body bottom
ROLLER_R, ROLLER_W = 2.4, 4.8              # roller radius and width
ROLLER_FREE_Z = 11.0                       # roller centre above the body top, lever free

# ---------------------------------------------------------------- frame interface
EXTR_Y = -20.0          # -Y side face of the 4040
SLOT_Z = -20.0          # side slot centre (same as the other blocks' M6 side holes)
STRIKE_Z = 28.0         # underside of the clamp block / nut plate
INTERFERENCE = 1.3      # roller top this far above STRIKE_Z with the lever free
                        # TODO(verify): must be less than the switch's pre-travel + overtravel

# ---------------------------------------------------------------- bracket
PL_T = 6.0              # plate thickness (Y)
PL_L = 40.0             # plate length (X)
PL_Z0, PL_Z1 = -32.0, 20.0
M6_D, M6_ADJ = 6.6, 10.0   # T-nut slots: O6.6, 10 mm long along X
M6_DX = 12.0               # two T-nut slots at +/-12 from the plate centre
M3_D, M3_ADJ = 3.4, 6.0    # switch slots: O3.4, 6 mm long along Z (+/-3)

body_bottom_z = STRIKE_Z + INTERFERENCE - ROLLER_R - ROLLER_FREE_Z - SW_H


def _slot_y(x0, z0, length, d, along):
    """Through-slot in Y (rounded ends), length overall along X or Z."""
    r = d / 2
    a = (length - d) / 2
    dx, dz = (a, 0) if along == "x" else (0, a)
    s = Pos(x0 - dx, 0, z0 - dz) * Rot(90, 0, 0) * Cylinder(r, 100)
    s += Pos(x0 + dx, 0, z0 + dz) * Rot(90, 0, 0) * Cylinder(r, 100)
    s += Pos(x0, 0, z0) * Box(2 * a if along == "x" else d, 100, d if along == "x" else 2 * a)
    return s


def bracket(cx):
    """Bracket centred at x = cx. Back face on the extrusion side face (y = -20)."""
    b = Pos(cx, EXTR_Y, PL_Z0) * Box(PL_L, PL_T, PL_Z1 - PL_Z0, align=(Align.CENTER, Align.MAX, Align.MIN))
    for dx in (-M6_DX, M6_DX):
        b -= _slot_y(cx + dx, SLOT_Z, M6_ADJ + M6_D, M6_D, "x")
    z_hole = body_bottom_z + SW_HOLE_Z
    for dx in (-SW_HOLE_PITCH / 2, SW_HOLE_PITCH / 2):
        b -= _slot_y(cx + dx, z_hole, M3_ADJ + M3_D, M3_D, "z")
    return b


def switch(cx):
    """Switch body on the plate's outer face, lever hinged at -X, roller at the +X end."""
    import math
    y0 = EXTR_Y - PL_T
    body = Pos(cx, y0, body_bottom_z) * Box(SW_L, SW_T, SW_H, align=(Align.CENTER, Align.MAX, Align.MIN))
    yc = y0 - SW_T / 2
    top = body_bottom_z + SW_H
    hx = cx - SW_L / 2 + 2.0
    rx = cx + SW_L / 2 - ROLLER_R
    rz = top + ROLLER_FREE_Z
    length = math.hypot(rx - hx, rz - top)
    ang = math.degrees(math.atan2(rz - top, rx - hx))
    lever = Pos(hx, yc, top) * Rot(0, -ang, 0) * Box(length, 3.0, 0.6, align=(Align.MIN, Align.CENTER, Align.CENTER))
    roller = Pos(rx, yc, rz) * Rot(90, 0, 0) * Cylinder(ROLLER_R, ROLLER_W)
    return body, lever + roller, rx


# Roller x positions chosen so the strike edge trips the switch where wanted.
# The strike edge meets the roller when its face is about sqrt(r^2-(r-i)^2) from the roller
# centre (i = INTERFERENCE): about 2.2 mm with the values above.
reach = (ROLLER_R ** 2 - (ROLLER_R - INTERFERENCE) ** 2) ** 0.5
NEAR_TRIP_X = 67.0            # clamp block -X face (70 at G0): trips 3 mm past minimum gap
FAR_TRIP_X = 317.2 + 320.0    # nut plate +X face after 320 mm of travel
near_rx = NEAR_TRIP_X - reach  # roller on the -X side of the approaching face
far_rx = FAR_TRIP_X + reach
near_cx = near_rx - (SW_L / 2 - ROLLER_R)
far_cx = far_rx - (SW_L / 2 - ROLLER_R)

parts = {}
for name, cx in (("lim_near", near_cx), ("lim_far", far_cx)):
    br = bracket(cx)
    body, lever, _ = switch(cx)
    parts[name] = (br, body, lever)
    export_step(br, OUT / f"{name}_bracket.step")
    export_stl(br, OUT / f"{name}_bracket.stl")
    export_step(Compound([body, lever]), OUT / f"{name}_switch_placeholder.step")

if __name__ == "__main__":
    print(f"body bottom z = {body_bottom_z:.2f}, top z = {body_bottom_z + SW_H:.2f}")
    print(f"LIM_NEAR bracket x {near_cx - PL_L/2:.1f}..{near_cx + PL_L/2:.1f}, roller x {near_rx:.1f}")
    print(f"LIM_FAR  bracket x {far_cx - PL_L/2:.1f}..{far_cx + PL_L/2:.1f}, roller x {far_rx:.1f}")
