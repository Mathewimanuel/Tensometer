"""Route the placed board with KiCad Routing Tools (github.com/drandyhaas/KiCadRoutingTools).

Steps: strip the placement-stage pours -> GND plane on B.Cu (route_planes.py) -> route
every net at 0.25 mm / 0.2 mm clearance, power nets wider, vias 0.6/0.3 never shrunk
(route.py --escalation board) -> convert the router's KiCad 9 tokens to the KiCad 7 file
format used here -> GND pour on F.Cu too, 0.5 mm thermal spokes -> fill.

Usage: python3 route_board.py <placement.kicad_pcb> <project.kicad_pro> <out.kicad_pcb> <krt_dir>
"""
import os
import shutil
import subprocess
import sys
import tempfile

import pcbnew

from gen_pcb import add_zone

RULES = ["--track-width", "0.25", "--clearance", "0.2", "--via-size", "0.6", "--via-drill", "0.3"]
POWER = {"+24V": 0.6, "/24V_F": 0.6, "/VIN_24": 0.6, "+5V": 0.5, "/VIN_T": 0.5,
         "+3V3": 0.4, "+3.3VA": 0.4, "GND": 0.5}


def run(cmd, log):
    with open(log, "w") as f:
        r = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        raise SystemExit(f"failed ({r.returncode}): {' '.join(cmd)}  -- see {log}")


def pour(out):
    """GND pour on F.Cu next to the router's B.Cu plane, both with usable thermals."""
    b = pcbnew.LoadBoard(out)
    layers = [pcbnew.BOARD.GetStandardLayerName(z.GetLayerSet().Seq()[0]) for z in b.Zones()]
    if "F.Cu" not in layers:
        add_zone(b, pcbnew.F_Cu, b.FindNet("GND"))
    for z in b.Zones():
        z.SetLocalClearance(pcbnew.FromMM(0.3))
        z.SetMinThickness(pcbnew.FromMM(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(pcbnew.FromMM(0.4))
        z.SetThermalReliefSpokeWidth(pcbnew.FromMM(0.5))   # must exceed min thickness or spokes vanish
    b.Save(out)


def fill(out):
    b = pcbnew.LoadBoard(out)
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    b.Save(out)


def main(placement, pro, out, krt):
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
         "--escalation", "board", "--power-nets", *POWER.keys(),
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
    print(f"routed board: {out} (logs in {work})")


if __name__ == "__main__":
    # pcbnew 7 can segfault while Python prints a traceback (GC of SWIG objects), which
    # hides the real error; report it ourselves and exit hard.
    try:
        if sys.argv[1] == "--pour":
            pour(sys.argv[2])
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
