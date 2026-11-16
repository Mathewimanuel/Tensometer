"""Build the controller PCB placement (unrouted) from the KiCad-exported netlist.

Footprints come from the installed KiCad libraries plus the project footprint library
(Teensy socket). Each footprint is linked to its schematic symbol by UUID path, so
KiCad's "Update PCB from Schematic" recognises every part.

Usage: python3 gen_pcb.py <out_dir> <netlist.xml>
"""
import os
import sys
import xml.etree.ElementTree as ET

import pcbnew

from design import LCSC, PARTS
from gen_sch import PROJECT, uid

FPLIB = "/usr/share/kicad/footprints"
W, H = 57.5, 72.0           # board size, mm (rev 0.3 compact layout)
OX, OY = 50.0, 50.0         # board origin on the page
SCH_FILE = f"{PROJECT}.kicad_sch"


# Dedicated GND vias, placed before routing. Every GND pad of a two-pad SMD part gets its
# own via just past the pad's outer end (standard decoupling practice: shortest return
# path, and the router cannot leave the pad walled in by signal tracks). Explicit taps
# cover the rest. Offsets are from the pad centre, in board mm.
GND_TAPS = {("J10", "10"): (-1.8, 0.0), ("J10", "15"): (0.0, -1.8), ("J10", "16"): (0.0, -1.8)}
TAP_GAP = 0.75   # pad end to via centre: 0.3 via radius + 0.45 copper gap


def mm(v):
    return pcbnew.FromMM(v)


def pt(x, y):
    return pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))


# ------------------------------------------------------------------ Teensy footprint
def write_teensy_fp(pretty_dir):
    os.makedirs(pretty_dir, exist_ok=True)
    pads = []
    for i in range(24):  # left column, top -> bottom
        shape = "rect" if i == 0 else "circle"
        pads.append(f'  (pad "{i+1}" thru_hole {shape} (at 0 {i*2.54:.2f}) (size 1.7 1.7) (drill 1.0) (layers "*.Cu" "*.Mask"))')
    for j in range(24):  # right column, bottom -> top (pads 25..48)
        pads.append(f'  (pad "{25+j}" thru_hole circle (at 15.24 {(23-j)*2.54:.2f}) (size 1.7 1.7) (drill 1.0) (layers "*.Cu" "*.Mask"))')
    body = ("-1.27 -2.54", "16.51 59.69")   # Teensy 4.1 is 17.78 x 61 mm; USB end overhangs at the top
    text = f"""(footprint "Teensy41_Socket" (version 20221018) (generator tensometer_gen) (layer "F.Cu")
  (descr "Teensy 4.1 in two 1x24 female headers (8.5 mm), 0.1in pitch, rows 0.6in apart. Courtyard covers the headers only: the module stands ~11 mm above the board, so low SMD parts may sit between the rows. Pad order: left column top->bottom 1-24, right column bottom->top 25-48 (USB end at the top).")
  (tags "Teensy 4.1 socket")
  (attr through_hole)
  (fp_text reference "REF**" (at 7.62 -2.9) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))
  (fp_text value "Teensy41_Socket" (at 7.62 61.5) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))
  (fp_text user "USB" (at 7.62 -1.0) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))
  (fp_rect (start -1.4 -1.4) (end 1.4 59.82) (stroke (width 0.12) (type default)) (fill none) (layer "F.SilkS"))
  (fp_rect (start 13.84 -1.4) (end 16.64 59.82) (stroke (width 0.12) (type default)) (fill none) (layer "F.SilkS"))
  (fp_line (start -1.9 -1.9) (end -0.4 -1.9) (stroke (width 0.12) (type default)) (layer "F.SilkS"))
  (fp_rect (start {body[0]}) (end {body[1]}) (stroke (width 0.1) (type default)) (fill none) (layer "F.Fab"))
  (fp_rect (start 5.0 -3.8) (end 10.24 0.5) (stroke (width 0.1) (type default)) (fill none) (layer "F.Fab"))
  (fp_rect (start -1.77 -1.77) (end 1.77 60.19) (stroke (width 0.05) (type default)) (fill none) (layer "F.CrtYd"))
  (fp_rect (start 13.47 -1.77) (end 17.01 60.19) (stroke (width 0.05) (type default)) (fill none) (layer "F.CrtYd"))
{chr(10).join(pads)}
)
"""
    with open(os.path.join(pretty_dir, "Teensy41_Socket.kicad_mod"), "w") as f:
        f.write(text)


# ------------------------------------------------------------------ placement (mm, deg)
# Rev 0.3 compact layout, 57.5 x 72 mm (was 125 x 88). Teensy vertical, USB at the top
# edge. Screw/pluggable terminals on the left edge (wire entry outward), JST-XH along the
# bottom, operator/expansion connectors on the right. Low SMD passives sit under the
# socketed Teensy, close to the pins they serve.
TX, TY = 29.0, 4.1          # Teensy pad 1
PLACE = {
    # left edge: wire entry faces the board edge
    "J1": (5.15, 10.1, 270), "J2": (10.55, 22.4, 270), "J6": (5.15, 55.0, 270),
    # power: 24 V -> F1 -> D1 -> U1 buck -> +5 V -> D4 -> Teensy VIN
    "U1": (14.6, 7.0, 0), "F1": (14.1, 12.3, 0), "D1": (21.0, 12.3, 0),
    "D2": (17.6, 17.0, 0), "C1": (23.6, 17.2, 90), "C2": (25.2, 3.5, 90),
    "R1": (17.0, 47.0, 0), "D3": (21.5, 47.0, 0),          # power LED, visible beside the Teensy
    # Teensy
    "U2": (TX, TY, 0),
    # TB6600 buffer + reset-safe pulls
    "U3": (19.5, 25.0, 0), "C4": (25.0, 25.5, 90),
    "R2": (17.0, 31.0, 0), "R3": (21.0, 31.0, 0), "R4": (25.0, 31.0, 0),
    # HX711 supply
    "U4": (17.0, 38.0, 0), "C5": (21.8, 37.5, 90), "C6": (25.2, 37.5, 90),
    # e-stop sense
    "U5": (20.0, 57.5, 0), "R11": (12.5, 63.5, 90), "D5": (20.0, 62.5, 0),
    # bottom edge: JST-XH
    "J3": (16.7, 67.45, 0), "J4": (30.6, 67.45, 0), "J5": (39.5, 67.45, 0),
    # right edge
    "J10": (50.84, 26.68, 180), "J8": (55.5, 9.5, 0), "J7": (50.9, 34.2, 0), "J9": (50.0, 53.5, 0),
    "Q1": (49.0, 60.0, 0), "R24": (54.0, 60.0, 90),
    # under the Teensy (between the header rows)
    "D4": (36.6, 5.0, 0), "C3": (41.3, 8.5, 90),
    "R5": (31.8, 19.5, 90), "R6": (34.4, 19.5, 90), "R20": (38.2, 22.5, 90), "R21": (41.0, 22.5, 90),
    "R12": (31.8, 29.5, 90), "C9": (34.4, 29.5, 90),
    "R7": (31.8, 34.0, 90), "R8": (34.2, 34.0, 90), "C7": (36.6, 34.0, 90), "R9": (39.0, 34.0, 90), "R10": (41.4, 34.0, 90),
    "C8": (31.8, 40.0, 90), "R22": (34.2, 40.0, 90), "R23": (36.6, 40.0, 90),
    "R13": (31.8, 46.0, 90), "C10": (34.2, 46.0, 90), "R14": (36.6, 46.0, 90), "C11": (39.0, 46.0, 90), "R15": (41.4, 46.0, 90),
    "C12": (31.8, 52.0, 90), "R16": (34.2, 52.0, 90), "C13": (36.6, 52.0, 90), "R17": (39.0, 52.0, 90), "C14": (41.4, 52.0, 90),
    "R18": (31.8, 58.0, 270), "C15": (34.2, 58.0, 90), "R19": (36.6, 58.0, 90),
    # mounting holes and fiducials
    "H1": (3.5, 3.5, 0), "H2": (W - 3.5, 3.5, 0), "H3": (3.5, H - 3.5, 0), "H4": (W - 3.5, H - 3.5, 0),
    "FID1": (8.8, 1.8, 0), "FID2": (56.0, 63.0, 0), "FID3": (8.5, 66.5, 0),
}
# Parts hidden under the Teensy: their reference designators go to F.Fab only.
UNDER_TEENSY = {r for r, (x, y, _) in PLACE.items() if TX + 1.5 < x < TX + 13.7 and r != "U2"}

LABELS = [  # (text, x, y, size, angle, layer). Bottom silk carries the full pinout legend.
    ("24V IN", 9.0, 12.6, 0.8, 90, "F.SilkS"),
    ("E-STOP", 9.0, 57.5, 0.8, 90, "F.SilkS"),
    ("EXPANSION", 55.6, 23.7, 0.8, 90, "F.SilkS"),
    ("OLED", 53.2, 11.4, 0.8, 90, "F.SilkS"),
    ("J1 24V IN: +24V (before contactor) / 0V", 14.2, 16.0, 0.9, 90, "B.SilkS"),
    ("J2 TB6600: PUL+ PUL- DIR+ DIR- ENA+ ENA-", 15.6, 16.0, 0.9, 90, "B.SilkS"),
    ("J3 HX711: 3V3A GND DOUT SCK", 17.0, 16.0, 0.9, 90, "B.SilkS"),
    ("J4/J5 LIMIT NEAR/FAR: SIG GND (NC sw)", 18.4, 16.0, 0.9, 90, "B.SilkS"),
    ("J6 E-STOP SENSE: 24V after contactor / 0V", 19.8, 16.0, 0.9, 90, "B.SilkS"),
    ("J7 PANEL: START STOP JOG+ JOG- HOME TARE", 21.2, 16.0, 0.9, 90, "B.SilkS"),
    ("J8 OLED: GND 3V3 SCL SDA   J9 SYNC: +5V LED-", 22.6, 16.0, 0.9, 90, "B.SilkS"),
    ("J10 EXPANSION (phase 2)   Cut Teensy VUSB-VIN", 24.0, 16.0, 0.9, 90, "B.SilkS"),
    ("TENSOMETER CONTROLLER rev 0.3", 25.6, 16.0, 0.9, 90, "B.SilkS"),
]


def load_fp(lib_fp, pretty_local):
    lib, name = lib_fp.split(":")
    path = pretty_local if lib == "tensometer" else os.path.join(FPLIB, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise SystemExit(f"footprint not found: {lib_fp}")
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp


def netlist(path):
    """ref -> {pin: net name exactly as the schematic exports it}."""
    root = ET.parse(path).getroot()
    out = {}
    for net in root.iter("net"):
        for node in net.iter("node"):
            out.setdefault(node.get("ref"), {})[node.get("pin")] = net.get("name")
    return out


def add_rect(board, layer, x1, y1, x2, y2, width=0.1):
    for (a, b, c, d) in ((x1, y1, x2, y1), (x2, y1, x2, y2), (x2, y2, x1, y2), (x1, y2, x1, y1)):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pt(a, b))
        s.SetEnd(pt(c, d))
        s.SetLayer(layer)
        s.SetWidth(mm(width))
        board.Add(s)


def add_zone(board, layer, net):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(net)
    z.SetLocalClearance(mm(0.3))
    z.SetMinThickness(mm(0.25))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    ol = z.Outline()
    ol.NewOutline()
    for x, y in ((0.5, 0.5), (W - 0.5, 0.5), (W - 0.5, H - 0.5), (0.5, H - 0.5)):
        ol.Append(mm(OX + x), mm(OY + y))
    board.Add(z)


def place_references(board):
    """Put each silkscreen reference where it overlaps no pad, no other silk and stays
    clear of the board edge; parts with no free spot get their reference on F.Fab only
    (the assembly drawing). Tries above, below, left and right of the courtyard."""
    def rect(bb, grow=0.0):
        g = mm(grow)
        return (bb.GetLeft() - g, bb.GetTop() - g, bb.GetRight() + g, bb.GetBottom() + g)

    def hit(a, b):
        return not (a[2] <= b[0] or b[2] <= a[0] or a[3] <= b[1] or b[3] <= a[1])

    edge = (mm(OX + 0.6), mm(OY + 0.6), mm(OX + W - 0.6), mm(OY + H - 0.6))
    blocked = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.IsOnLayer(pcbnew.F_Cu) or pad.IsOnLayer(pcbnew.F_Mask):
                blocked.append((fp.GetReference(), rect(pad.GetBoundingBox(), 0.25)))
        for it in fp.GraphicalItems():
            if it.GetLayer() == pcbnew.F_SilkS and "TEXT" not in it.GetClass().upper():
                bb = it.GetBoundingBox()
                if bb.GetWidth() > mm(4) and bb.GetHeight() > mm(4) and it.GetClass() in ("FP_SHAPE", "PCB_SHAPE"):
                    # a rectangle outline: block only its four sides, not the inside
                    l, t, r, b = rect(bb, 0.15)
                    w = mm(0.3)
                    blocked += [(fp.GetReference(), q) for q in ((l, t, r, t + w), (l, b - w, r, b), (l, t, l + w, b), (r - w, t, r, b))]
                else:
                    blocked.append((fp.GetReference(), rect(bb, 0.15)))
    for d in board.GetDrawings():
        if d.GetLayer() == pcbnew.F_SilkS:
            blocked.append((None, rect(d.GetBoundingBox(), 0.15)))

    for fp in sorted(board.GetFootprints(), key=lambda f: f.GetReference()):
        ref = fp.Reference()
        if ref.GetLayer() != pcbnew.F_SilkS:
            continue
        ref.SetTextSize(pcbnew.VECTOR2I(mm(1.0), mm(1.0)))
        ref.SetTextThickness(mm(0.15))
        xs, ys = [], []
        for it in fp.GraphicalItems():
            if it.GetLayer() == pcbnew.F_CrtYd:
                l, t, r, b = rect(it.GetBoundingBox())
                xs += [l, r]
                ys += [t, b]
        if not xs:
            l, t, r, b = rect(fp.GetBoundingBox(False, False))
            xs, ys = [l, r], [t, b]
        left, top, right, bottom = min(xs), min(ys), max(xs), max(ys)
        own_pads = [rect(p.GetBoundingBox(), 0.25) for p in fp.Pads()]
        cx, cyy = (left + right) // 2, (top + bottom) // 2
        options = [((cx, top - mm(1.0)), 0), ((cx, bottom + mm(1.0)), 0),
                   ((left - mm(1.0), cyy), 90), ((right + mm(1.0), cyy), 90),
                   ((cx, cyy), 0)]
        placed = False
        for size in (1.0, 0.8):
            ref.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
            ref.SetTextThickness(mm(0.15 if size >= 1.0 else 0.12))
            for (x, y), ang in options:
                ref.SetTextAngleDegrees(ang)
                ref.SetPosition(pcbnew.VECTOR2I(int(x), int(y)))
                r = rect(ref.GetBoundingBox())
                if r[0] < edge[0] or r[1] < edge[1] or r[2] > edge[2] or r[3] > edge[3]:
                    continue
                # own silk/pads only matter if the text would land on the pads themselves
                if any(hit(r, q) for o, q in blocked if o != fp.GetReference() or q in own_pads):
                    continue
                blocked.append((None, rect(ref.GetBoundingBox(), 0.1)))
                placed = True
                break
            if placed:
                break
        if not placed:
            ref.SetTextSize(pcbnew.VECTOR2I(mm(1.0), mm(1.0)))
            ref.SetTextThickness(mm(0.15))
            ref.SetLayer(pcbnew.F_Fab)


def main(out_dir, net_xml):
    pretty = os.path.join(out_dir, "tensometer.pretty")
    write_teensy_fp(pretty)
    nets = netlist(net_xml)

    board = pcbnew.BOARD()
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    tb = board.GetTitleBlock()
    tb.SetTitle("Tensometer controller board")
    tb.SetRevision("0.3")
    tb.SetDate("2026-11-16")
    tb.SetComment(0, "Generated by hardware/controller-board/tools (gen_pcb.py placement, route_board.py routing)")

    net_objs = {}
    for name in sorted({n for pins in nets.values() for n in pins.values()}):
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        net_objs[name] = ni

    for ref, _lib, value, fp_id, _pins, _grp in PARTS:
        fp = load_fp(fp_id, pretty)
        fp.SetReference(ref)
        fp.SetValue(value)
        fp.SetPath(pcbnew.KIID_PATH("/" + uid("sym", ref)))
        fp.SetProperty("Sheetname", "")
        fp.SetProperty("Sheetfile", SCH_FILE)
        if ref in LCSC:
            fp.SetProperty("LCSC", LCSC[ref])
        x, y, rot = PLACE[ref]
        fp.SetPosition(pt(x, y))
        fp.SetOrientationDegrees(rot)
        if ref in UNDER_TEENSY or ref.startswith(("H", "FID")):
            fp.Reference().SetLayer(pcbnew.F_Fab)   # covered by the module / not useful on silk
        for pad in fp.Pads():
            name = nets.get(ref, {}).get(pad.GetNumber())
            if name:
                pad.SetNet(net_objs[name])
        board.Add(fp)

    add_rect(board, pcbnew.Edge_Cuts, 0, 0, W, H, 0.1)
    for text, x, y, size, angle, layer in LABELS:
        t = pcbnew.PCB_TEXT(board)
        t.SetText(text)
        t.SetPosition(pt(x, y))
        t.SetLayer({"F.SilkS": pcbnew.F_SilkS, "B.SilkS": pcbnew.B_SilkS}[layer])
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
        t.SetTextThickness(mm(0.15))
        t.SetTextAngleDegrees(angle)
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT if layer == "B.SilkS" else pcbnew.GR_TEXT_H_ALIGN_CENTER)
        if layer == "B.SilkS":
            t.SetMirrored(True)
        board.Add(t)

    # Pre-routed GND taps for pads the router walls in: it judges plane connectivity on
    # zone outlines, so a GND pad boxed in by signal tracks on both layers ships
    # unconnected. (pad, via offset from pad in mm); 0.4 mm track, 0.6/0.3 via.
    taps = [(board.FindFootprintByReference(r).FindPadByNumber(n), d) for (r, n), d in GND_TAPS.items()]
    for fp in board.GetFootprints():
        pads = list(fp.Pads())
        if len(pads) != 2 or any(p.HasHole() for p in pads):
            continue
        for pad in pads:
            if pad.GetNetname() != "GND":
                continue
            c, p = fp.GetPosition(), pad.GetPosition()
            vx, vy = p.x - c.x, p.y - c.y
            n = (vx * vx + vy * vy) ** 0.5
            ux, uy = vx / n, vy / n
            bb = pad.GetBoundingBox()
            half = abs(ux) * bb.GetWidth() / 2 + abs(uy) * bb.GetHeight() / 2
            d = pcbnew.ToMM(half) + TAP_GAP
            taps.append((pad, (ux * d, uy * d)))
    for pad, (dx, dy) in taps:
        p0 = pad.GetPosition()
        p1 = pcbnew.VECTOR2I(p0.x + mm(dx), p0.y + mm(dy))
        tr = pcbnew.PCB_TRACK(board)
        tr.SetStart(p0); tr.SetEnd(p1); tr.SetWidth(mm(0.4)); tr.SetLayer(pcbnew.F_Cu)
        tr.SetNet(net_objs["GND"]); board.Add(tr)
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(p1); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(net_objs["GND"])
        board.Add(v)

    place_references(board)
    gnd = net_objs["GND"]
    add_zone(board, pcbnew.B_Cu, gnd)
    add_zone(board, pcbnew.F_Cu, gnd)
    out = os.path.join(out_dir, f"{PROJECT}.kicad_pcb")
    board.Save(out)
    # Filling zones on a board built in memory crashes KiCad 7's filler (no project or
    # connectivity yet), so reload the saved file and fill that.
    board = pcbnew.LoadBoard(out)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.Save(out)
    return out


if __name__ == "__main__":
    print(main(sys.argv[1], sys.argv[2]))
