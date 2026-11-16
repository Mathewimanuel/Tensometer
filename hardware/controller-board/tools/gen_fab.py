"""JLCPCB fabrication + assembly outputs: Gerbers/drill zip, BOM and CPL.

BOM columns: Comment, Designator, Footprint, LCSC Part #   (JLC's template)
CPL columns: Designator, Mid X, Mid Y, Layer, Rotation      (JLC's template)
Rotation corrections follow the community database used by the JLCPCB KiCad plugin
(matthewlai/JLCKicadTools cpl_rotations_db.csv), see ROT_FIX.
Always check part orientation in JLC's placement preview before ordering.

Usage: python3 gen_fab.py <board.kicad_pcb> <out_dir>
"""
import csv
import os
import re
import subprocess
import sys
import tempfile
import zipfile

from design import LCSC, PARTS

LAYERS = "F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts"
# KiCad footprints follow IPC-7351 zero orientation (pin 1 top-left, leads left/right);
# JLC's EasyEDA footprints put pin 1 bottom-left with leads top/bottom, i.e. a 90 degree
# difference for SOT/SOIC/TSSOP-style packages. Diodes, LEDs and passives are 0 in both.
# SMDIP-4 (LTV-817S) is not in the community table; it follows the same convention as
# SOIC here. Check every polarised part in JLC's preview anyway (README checklist).
ROT_FIX = [(r"^SOT-23", -90), (r"^TSSOP-", 270), (r"^SOIC-", 270), (r"^SMDIP-", 270),
           (r"^R_Array_Convex_", 90), (r"^CP_Elec_", 180)]


def main(pcb, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="fab_")
    subprocess.run(["kicad-cli", "pcb", "export", "gerbers", "-l", LAYERS, "-o", tmp + "/", pcb],
                   check=True, capture_output=True)
    subprocess.run(["kicad-cli", "pcb", "export", "drill", "--format", "excellon", "-u", "mm", "--excellon-separate-th",
                    "--generate-map", "--map-format", "gerberx2", "-o", tmp + "/", pcb],
                   check=True, capture_output=True)
    gz = os.path.join(out_dir, "gerbers.zip")
    with zipfile.ZipFile(gz, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(tmp)):
            z.write(os.path.join(tmp, f), f)

    # Positions straight from KiCad (Y already in Gerber orientation).
    pos = os.path.join(tmp, "pos.csv")
    subprocess.run(["kicad-cli", "pcb", "export", "pos", "--format", "csv", "--units", "mm",
                    "--side", "both", "-o", pos, pcb], check=True, capture_output=True)
    rows = list(csv.DictReader(open(pos)))

    parts = {p[0]: p for p in PARTS}
    with open(os.path.join(out_dir, "CPL.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for r in rows:
            ref = r["Ref"]
            if ref not in LCSC:
                continue
            fp = parts[ref][3].split(":")[1]
            rot = float(r["Rot"])
            for pat, fix in ROT_FIX:
                if re.match(pat, fp):
                    rot = (rot + fix) % 360
                    break
            w.writerow([ref, f"{float(r['PosX']):.3f}mm", f"{float(r['PosY']):.3f}mm",
                        "Top" if r["Side"] == "top" else "Bottom", f"{rot:g}"])

    groups = {}
    for ref, _lib, value, fp, _pins, _g in PARTS:
        if ref in LCSC:
            groups.setdefault((value, fp.split(":")[1], LCSC[ref]), []).append(ref)
    nat = lambda r: (re.sub(r"\d", "", r), int(re.sub(r"\D", "", r) or 0))
    with open(os.path.join(out_dir, "BOM.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for (value, fp, lcsc), refs in sorted(groups.items(), key=lambda kv: nat(sorted(kv[1], key=nat)[0])):
            w.writerow([value, ",".join(sorted(refs, key=nat)), fp, lcsc])
    print(f"wrote {gz}, BOM.csv ({len(groups)} lines), CPL.csv")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
