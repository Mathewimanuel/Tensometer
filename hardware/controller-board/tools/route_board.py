"""Route the placed board with KiCad Routing Tools (github.com/drandyhaas/KiCadRoutingTools).

Steps: strip the placement-stage pours -> GND plane on B.Cu (route_planes.py) -> route
every net at 0.25 mm / 0.2 mm clearance, power nets wider, vias 0.6/0.3 never shrunk
(route.py --escalation board) -> convert the router's KiCad 9 tokens to the KiCad 7 file
format used here -> GND pour on F.Cu too, 0.5 mm thermal spokes -> fill -> GND stitching vias -> refill with
island removal -> drop stitching vias left in removed islands -> refill.

Usage: python3 route_board.py <placement.kicad_pcb> <project.kicad_pro> <out.kicad_pcb> <krt_dir>
"""
import math
import os
import shutil
import subprocess
import sys
import tempfile

import pcbnew

from gen_pcb import add_zone

# IPC-2221 sizing, 1 oz outer copper, 10 C rise: 0.25 mm carries ~0.8 A, 0.4 mm ~1.2 A,
# 0.5 mm ~1.5 A. Board currents: 5 V rail <= 0.5 A (buck rated 1 A), 24 V input < 0.2 A,
# everything else is logic. +3V3 here only feeds pull-ups, the OLED and the panel LED
# (the Teensy's own regulator is limited to ~250 mA), so 0.25 mm is ample and keeps the
# pull-up fan-out under the Teensy routable. All vias 0.6/0.3 mm (JLC standard, no
# surcharge; a 0.3 mm barrel carries well over 1 A).
# No via-in-pad: JLC standard vias are not filled/capped, solder would wick into them.
RULES = ["--track-width", "0.25", "--clearance", "0.2", "--same-net-pad-clearance", "0.2"]
POWER = {"+24V": 0.5, "/24V_F": 0.5, "/VIN_24": 0.5, "+5V": 0.5, "/VIN_T": 0.5,
         "+3V3": 0.25, "+3.3VA": 0.4, "GND": 0.5}


def run(cmd, log):
    with open(log, "w") as f:
        r = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        raise SystemExit(f"failed ({r.returncode}): {' '.join(cmd)}  -- see {log}")


def pour(out):
    """GND pour on F.Cu next to the router's B.Cu plane, both with usable thermals."""
    b = pcbnew.LoadBoard(out)
    layers = [pcbnew.BOARD.GetStandardLayerName(z.GetLayerSet().Seq()[0]) for z in b.Zones()]
    for name, lay in (("F.Cu", pcbnew.F_Cu), ("B.Cu", pcbnew.B_Cu)):
        if name not in layers:
            add_zone(b, lay, b.FindNet("GND"))
    for z in b.Zones():
        z.SetLocalClearance(pcbnew.FromMM(0.3))
        z.SetMinThickness(pcbnew.FromMM(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(pcbnew.FromMM(0.4))
        z.SetThermalReliefSpokeWidth(pcbnew.FromMM(0.5))   # must exceed min thickness or spokes vanish
        # Keep every fragment for now so stitch() can see (and join) the ones walled in
        # by tracks; islands() turns removal back on for the final fill.
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_NEVER)
    b.Save(out)


def islands(out):
    """Final fill: drop any pour fragment that still touches nothing of GND."""
    b = pcbnew.LoadBoard(out)
    for z in b.Zones():
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    b.Save(out)


def fill(out):
    b = pcbnew.LoadBoard(out)
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    b.Save(out)


# B.Cu is the plane layer, so tracks there cost more and the plane stays mostly whole.
# On a board this dense the router's result swings with these settings (a GND pad can
# end up walled in), so main() tries them in turn and keeps the first board that KiCad's
# own DRC finds fully connected.
ATTEMPTS = [(c, o) for c in ("8.0", "5.0", "12.0", "3.0") for o in ("original", "inside_out", "mps")]
STITCH_PITCH = 5.0      # mm, GND stitching grid (well under lambda/20 for anything on this board)
STITCH_RING = 1.6       # mm in from the board edge for the perimeter row
STITCH_VIA = (0.6, 0.3)  # mm, same as signal vias; tented like every via here


def stitch(out):
    """Drop GND vias where both pours are solid, so the F.Cu and B.Cu planes act as one
    and no pour island is left hanging. Run after a fill; refill afterwards."""
    b = pcbnew.LoadBoard(out)
    gnd = b.FindNet("GND")
    fills = {}
    for z in b.Zones():
        lay = z.GetLayerSet().Seq()[0]
        fills[lay] = z.GetFilledPolysList(lay)
    holes = []   # existing drills and via barrels: keep stitching vias >= 1.0 mm away
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            if pad.GetDrillSizeX() > 0 or fp.GetReference().startswith("H"):
                p = pad.GetPosition()
                holes.append((pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), pcbnew.ToMM(max(pad.GetSizeX(), pad.GetSizeY())) / 2))
    gnd_vias = []
    for t in b.GetTracks():
        if t.GetClass() == "PCB_VIA":
            p = t.GetPosition()
            if t.GetNetname() == "GND":
                gnd_vias.append((pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)))
            holes.append((pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), pcbnew.ToMM(t.GetWidth()) / 2))
    bb = b.GetBoardEdgesBoundingBox()
    x0, y0 = pcbnew.ToMM(bb.GetX()), pcbnew.ToMM(bb.GetY())
    x1, y1 = x0 + pcbnew.ToMM(bb.GetWidth()), y0 + pcbnew.ToMM(bb.GetHeight())

    cands = []
    nx, ny = int((x1 - x0) // STITCH_PITCH), int((y1 - y0) // STITCH_PITCH)
    for i in range(nx + 1):
        for j in range(ny + 1):
            cands.append((x0 + (x1 - x0 - nx * STITCH_PITCH) / 2 + i * STITCH_PITCH,
                          y0 + (y1 - y0 - ny * STITCH_PITCH) / 2 + j * STITCH_PITCH))
    r = STITCH_RING
    for k in range(int((x1 - x0 - 2 * r) // 2.5) + 1):
        cands += [(x0 + r + k * 2.5, y0 + r), (x0 + r + k * 2.5, y1 - r)]
    for k in range(int((y1 - y0 - 2 * r) // 2.5) + 1):
        cands += [(x0 + r, y0 + r + k * 2.5), (x1 - r, y0 + r + k * 2.5)]

    ring = [(0.0, 0.0)] + [(0.5 * math.cos(a * math.pi / 4), 0.5 * math.sin(a * math.pi / 4)) for a in range(8)]
    placed = []
    for cx, cy in cands:
        if any(math.hypot(cx - hx, cy - hy) < hr + STITCH_VIA[0] / 2 + 0.7 for hx, hy, hr in holes):
            continue
        ok = all(fills[lay].Contains(pcbnew.VECTOR2I(pcbnew.FromMM(cx + dx), pcbnew.FromMM(cy + dy)))
                 for lay in (pcbnew.F_Cu, pcbnew.B_Cu) for dx, dy in ring)
        if not ok:
            continue
        v = pcbnew.PCB_VIA(b)
        v.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(cx), pcbnew.FromMM(cy)))
        v.SetWidth(pcbnew.FromMM(STITCH_VIA[0]))
        v.SetDrill(pcbnew.FromMM(STITCH_VIA[1]))
        v.SetNet(gnd)
        b.Add(v)
        holes.append((cx, cy, STITCH_VIA[0] / 2))
        placed.append((cx, cy))
    # Second pass: any pour fragment that is not the main plane gets a via into the
    # other layer's main plane, if a spot exists (finer 0.5 mm search).
    def main_idx(f):
        return max(range(f.OutlineCount()), key=lambda i: f.Outline(i).Area())
    for lay, other in ((pcbnew.B_Cu, pcbnew.F_Cu), (pcbnew.F_Cu, pcbnew.B_Cu)):
        f, g = fills[lay], fills[other]
        mi, gmi = main_idx(f), main_idx(g)
        targets = [g.Outline(gmi)] + [g.Outline(k) for k in range(g.OutlineCount())
                                      if k != gmi and g.Outline(k).Area() > 2e12]
        for i in range(f.OutlineCount()):
            if i == mi:
                continue
            ol = f.Outline(i)
            bb = ol.BBox()
            ax, ay = pcbnew.ToMM(bb.GetX()), pcbnew.ToMM(bb.GetY())
            w, h = pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())
            if any(ax <= px <= ax + w and ay <= py <= ay + h and
                   ol.PointInside(pcbnew.VECTOR2I(pcbnew.FromMM(px), pcbnew.FromMM(py))) for px, py in placed + gnd_vias):
                continue
            done = False
            for gx in [ax + 0.25 * k for k in range(int(w / 0.25) + 1)]:
                for gy in [ay + 0.25 * k for k in range(int(h / 0.25) + 1)]:
                    if any(math.hypot(gx - hx, gy - hy) < hr + STITCH_VIA[0] / 2 + 0.3 for hx, hy, hr in holes):
                        continue
                    pts = [pcbnew.VECTOR2I(pcbnew.FromMM(gx + dx), pcbnew.FromMM(gy + dy)) for dx, dy in ring]
                    if all(ol.PointInside(q) for q in pts) and any(all(gm.PointInside(q) for q in pts) for gm in targets):
                        v = pcbnew.PCB_VIA(b)
                        v.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(gx), pcbnew.FromMM(gy)))
                        v.SetWidth(pcbnew.FromMM(STITCH_VIA[0]))
                        v.SetDrill(pcbnew.FromMM(STITCH_VIA[1]))
                        v.SetNet(gnd)
                        b.Add(v)
                        holes.append((gx, gy, STITCH_VIA[0] / 2))
                        placed.append((gx, gy))
                        done = True
                        break
                if done:
                    break
    b.Save(out)
    print(f"stitching vias: {len(placed)}")


def prune(out):
    """After the final fill: remove GND vias with no track that no longer land in pour on
    both layers (their fragment pair was walled in and removed as an island)."""
    b = pcbnew.LoadBoard(out)
    fills = {}
    for z in b.Zones():
        lay = z.GetLayerSet().Seq()[0]
        fills[lay] = z.GetFilledPolysList(lay)
    ends = set()
    for t in b.GetTracks():
        if t.GetClass() != "PCB_VIA":
            ends.add((t.GetStart().x, t.GetStart().y))
            ends.add((t.GetEnd().x, t.GetEnd().y))
    gone = []
    for t in list(b.GetTracks()):
        if t.GetClass() != "PCB_VIA" or t.GetNetname() != "GND":
            continue
        p = t.GetPosition()
        if (p.x, p.y) in ends:
            continue
        if not all(fills[lay].Collide(p, t.GetWidth() // 2) for lay in (pcbnew.F_Cu, pcbnew.B_Cu)):
            gone.append(t)
    for t in gone:
        b.Remove(t)
    b.Save(out)
    print(f"pruned stitching vias: {len(gone)}")


def unconnected(out):
    """Unconnected items KiCad's DRC reports (run in a fresh process, see below)."""
    rpt = out + ".drc.rpt"
    subprocess.run([sys.executable, __file__, "--drc", out, rpt], check=True)
    for ln in open(rpt):
        if ln.startswith("** Found") and "unconnected" in ln:
            return int(ln.split()[2])
    raise SystemExit("could not read the DRC report")


def drc(out, rpt):
    pcbnew.WriteDRCReport(pcbnew.LoadBoard(out), rpt, pcbnew.EDA_UNITS_MILLIMETRES, True)


def main(placement, pro, out, krt):
    for b_cost, ordering in ATTEMPTS:
        work = route_once(placement, pro, out, krt, b_cost, ordering)
        n = unconnected(out)
        print(f"B.Cu cost {b_cost}, ordering {ordering}: {n} unconnected (logs in {work})")
        if n == 0:
            os.remove(out + ".drc.rpt")
            print(f"routed board: {out}")
            return
    raise SystemExit("no router setting gave a fully connected board")


def route_once(placement, pro, out, krt, b_cost, ordering):
    work = tempfile.mkdtemp(prefix="route_")
    src = os.path.join(work, "in.kicad_pcb")
    b = pcbnew.LoadBoard(placement)
    for z in list(b.Zones()):
        b.Remove(z)
    b.Save(src)
    for name in ("in", "planes"):
        shutil.copy(pro, os.path.join(work, f"{name}.kicad_pro"))

    planes = os.path.join(work, "planes.kicad_pcb")
    routed = os.path.join(work, "routed.kicad_pcb")
    py = sys.executable
    run([py, os.path.join(krt, "py_router/route_planes.py"), src, planes, "--nets", "GND",
         "--plane-layers", "B.Cu", "--board-edge-clearance", "0.5"] + RULES, os.path.join(work, "planes.log"))
    run([py, os.path.join(krt, "py_router/route.py"), planes, routed, "--layers", "F.Cu", "B.Cu",
         "--escalation", "board", "--layer-costs", "1.0", b_cost, "--ordering", ordering, "--power-nets", *POWER.keys(),
         "--power-nets-widths", *[str(w) for w in POWER.values()]] + RULES, os.path.join(work, "route.log"))

    # The router writes zones/segments/vias in KiCad 9+ syntax; KiCad 7 wants tstamp.
    lines = open(routed).read().split("\n")
    lines = [ln.replace("(uuid ", "(tstamp ") if ln.startswith("\t") else ln for ln in lines]
    open(out, "w").write("\n".join(lines))
    dst = os.path.splitext(out)[0] + ".kicad_pro"   # our rules, not the router's relaxed copy
    if os.path.abspath(pro) != os.path.abspath(dst):
        shutil.copy(pro, dst)

    # Fresh processes: reusing pcbnew objects across these steps in one process hands
    # back untyped SWIG objects in KiCad 7.
    subprocess.run([sys.executable, __file__, "--pour", out], check=True)
    subprocess.run([sys.executable, __file__, "--fill", out], check=True)
    subprocess.run([sys.executable, __file__, "--stitch", out], check=True)
    subprocess.run([sys.executable, __file__, "--islands", out], check=True)
    subprocess.run([sys.executable, __file__, "--fill", out], check=True)
    subprocess.run([sys.executable, __file__, "--prune", out], check=True)
    subprocess.run([sys.executable, __file__, "--fill", out], check=True)
    return work


if __name__ == "__main__":
    # pcbnew 7 can segfault while Python prints a traceback (GC of SWIG objects), which
    # hides the real error; report it ourselves and exit hard.
    try:
        if sys.argv[1] == "--pour":
            pour(sys.argv[2])
        elif sys.argv[1] == "--drc":
            drc(sys.argv[2], sys.argv[3])
        elif sys.argv[1] == "--prune":
            prune(sys.argv[2])
        elif sys.argv[1] == "--islands":
            islands(sys.argv[2])
        elif sys.argv[1] == "--stitch":
            stitch(sys.argv[2])
        elif sys.argv[1] == "--fill":
            fill(sys.argv[2])
        else:
            main(*sys.argv[1:5])
    except BaseException as exc:  # noqa: BLE001
        tb, where = exc.__traceback__, []
        while tb:
            where.append(f"{os.path.basename(tb.tb_frame.f_code.co_filename)}:{tb.tb_lineno}")
            tb = tb.tb_next
        sys.stderr.write(f"route_board failed: {exc!r}\n  at {' > '.join(where)}\n")
        sys.stderr.flush()
        os._exit(1)
    sys.stdout.flush()
    os._exit(0)
