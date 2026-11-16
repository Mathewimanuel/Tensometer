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
W, H = 65.0, 72.0           # board size, mm (rev 0.4: room for keyed headers, SD access)
OX, OY = 50.0, 50.0         # board origin on the page
SCH_FILE = f"{PROJECT}.kicad_sch"


# Dedicated GND vias, placed before routing. Every GND pad of a two-pad SMD part gets its
# own via just past the pad's outer end (standard decoupling practice: shortest return
# path, and the router cannot leave the pad walled in by signal tracks). Explicit taps
# cover the rest. Offsets are from the pad centre, in board mm.
GND_TAPS = {("J10", "10"): (1.8, 0.0), ("J10", "15"): (0.0, 1.8), ("J10", "16"): (0.0, 1.8)}
TAP_GAP = 0.75   # pad end to via centre: 0.3 via radius + 0.45 copper gap


def _clear_of(board, x, y, r, skip_pad, vias):
    """True if a via of radius r at (x, y) (internal units) clears every other pad by
    0.25 mm (same-net SMD pads too, so no via lands in a pad), every hole and existing via
    by 0.5 mm, and the board edge by 1.0 mm."""
    gap, hole_gap = mm(0.25), mm(0.5)
    if not (mm(OX + 1.0) + r < x < mm(OX + W - 1.0) - r and mm(OY + 1.0) + r < y < mm(OY + H - 1.0) - r):
        return False
    p = pcbnew.VECTOR2I(int(x), int(y))
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad is skip_pad:
                continue
            # every other pad is an obstacle, GND ones included: no via-in-pad
            if pad.HitTest(p, r + (hole_gap if pad.HasHole() else gap)):
                return False
            if pad.HasHole() and pad.HitTest(p, r + hole_gap):
                return False
    for vx, vy in vias:
        if (vx - x) ** 2 + (vy - y) ** 2 < (2 * r + hole_gap) ** 2:
            return False
    return True


def add_gnd_taps(board, gnd):
    """Dedicated GND via per two-pad SMD GND pad (see GND_TAPS above). Tries just past the
    pad's outer end first, then to either side; a nearby tap via is shared instead."""
    r = mm(0.3)
    vias = []
    jobs = [(board.FindFootprintByReference(ref).FindPadByNumber(n), [d]) for (ref, n), d in GND_TAPS.items()]
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
            along = pcbnew.ToMM(abs(ux) * bb.GetWidth() / 2 + abs(uy) * bb.GetHeight() / 2) + TAP_GAP
            across = pcbnew.ToMM(abs(uy) * bb.GetWidth() / 2 + abs(ux) * bb.GetHeight() / 2) + TAP_GAP
            jobs.append((pad, [(ux * along, uy * along), (-uy * across, ux * across), (uy * across, -ux * across),
                               (ux * along - uy * across, uy * along + ux * across),
                               (ux * along + uy * across, uy * along - ux * across)]))
    for pad, cands in jobs:
        p0 = pad.GetPosition()
        target = None
        for vx, vy in vias:   # share a tap via that is already right next to this pad
            if pad.HitTest(pcbnew.VECTOR2I(int(vx), int(vy)), mm(1.2)):
                target = pcbnew.VECTOR2I(int(vx), int(vy))
                break
        if target is None:
            for dx, dy in cands:
                x, y = p0.x + mm(dx), p0.y + mm(dy)
                if _clear_of(board, x, y, r, pad, vias):
                    target = pcbnew.VECTOR2I(int(x), int(y))
                    v = pcbnew.PCB_VIA(board)
                    v.SetPosition(target); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(gnd)
                    board.Add(v)
                    vias.append((x, y))
                    break
        if target is None:
            print(f"no room for a GND tap at {pad.GetParentFootprint().GetReference()}.{pad.GetNumber()}")
            continue
        tr = pcbnew.PCB_TRACK(board)
        tr.SetStart(p0); tr.SetEnd(target); tr.SetWidth(mm(0.4)); tr.SetLayer(pcbnew.F_Cu)
        tr.SetNet(gnd); board.Add(tr)


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
# Rev 0.4, 64 x 72 mm. Teensy vertical, USB at the top edge; the microSD end (bottom)
# has nothing tall in front of it. Left edge: screw/pluggable terminals, wire entry
# outward. Left interior: 24 V protection and the 5 V converter, then the TB6600 buffer,
# HX711 LDO and e-stop opto. Right: keyed expansion and panel box headers, then a column
# of cable connectors (OLED, sync LED, limits). Low SMD passives sit under the socketed
# Teensy (8.5 mm sockets: ~11 mm clearance), close to the pins they serve.
TX, TY = 29.0, 4.1          # Teensy pad 1
PLACE = {
    # left edge: wire entry faces the board edge
    "J1": (5.15, 10.1, 270), "J2": (10.55, 22.4, 270), "J6": (5.15, 55.0, 270),
    # power: 24 V -> F1 -> D1 -> VIN_24 (D2 TVS, C16 bulk, C1) -> U1 -> +5 V -> D4 -> VIN
    "U1": (14.6, 7.0, 0), "C1": (15.9, 11.2, 0), "C2": (20.4, 11.2, 180),
    "F1": (14.6, 15.2, 0), "D1": (21.6, 15.2, 180), "D2": (16.0, 21.0, 90),
    "C16": (22.6, 22.6, 90),
    # TB6600 buffer (C4 at VCC, pin 20) + reset-safe pulls
    "U3": (17.5, 31.5, 0), "C4": (22.6, 29.6, 270),
    "R2": (15.5, 37.6, 0), "R3": (19.0, 37.6, 0), "R4": (22.5, 37.6, 0),
    # HX711 supply
    "U4": (16.5, 42.6, 0), "C5": (21.2, 42.4, 90), "C6": (24.4, 42.4, 90),
    # power LED, visible beside the Teensy
    "R1": (16.5, 48.6, 0), "D3": (21.5, 48.6, 0),
    # e-stop sense
    "U5": (20.0, 57.5, 0), "R11": (12.5, 63.5, 90), "D5": (20.0, 62.5, 0),
    # bottom-left: HX711 cable (clear of the microSD end)
    "J3": (16.7, 67.45, 0),
    # Teensy
    "U2": (TX, TY, 0),
    # right column A: keyed box headers
    "J10": (51.0, 8.0, 0), "J7": (51.0, 37.6, 0),
    "RN1": (50.0, 57.2, 0), "RN2": (54.0, 57.2, 0),        # panel series 1k, next to J7
    # right column B: cable connectors
    "J8": (61.3, 17.6, 90), "J9": (61.0, 27.5, 90),
    "Q1": (60.5, 32.7, 0), "R24": (63.6, 32.7, 90),
    "J4": (61.0, 41.8, 90), "J5": (61.0, 52.0, 90),
    # under the Teensy (between the header rows)
    "D4": (35.0, 5.0, 0), "C3": (40.6, 5.5, 90),
    "RN6": (40.8, 11.0, 0), "RN5": (40.8, 17.5, 0),        # expansion ADC series 1k
    "R5": (31.8, 19.5, 90), "R6": (34.4, 19.5, 90), "R20": (38.0, 22.8, 90), "R21": (41.0, 22.8, 90),
    "R12": (31.8, 29.5, 90), "C9": (34.4, 29.5, 90),
    "R7": (31.8, 34.0, 90), "R8": (34.2, 34.0, 90), "C7": (36.6, 34.0, 90), "R9": (39.0, 34.0, 90), "R10": (41.4, 34.0, 90),
    "C8": (31.8, 40.0, 90), "R22": (34.2, 40.0, 90), "R23": (36.6, 40.0, 90),
    "R25": (31.8, 46.5, 90), "R19": (36.6, 46.5, 90),
    "RN3": (33.0, 52.6, 0), "C15": (36.6, 52.6, 90), "RN4": (40.8, 52.6, 0),
    "C10": (31.8, 58.4, 90), "C11": (34.2, 58.4, 90), "C12": (36.6, 58.4, 90), "C13": (39.0, 58.4, 90), "C14": (41.4, 58.4, 90),
    # test points: TP1 by the input, the rest along the bottom (flat pads, under the SD end)
    "TP1": (25.6, 4.0, 0),
    "TP2": (30.0, 69.5, 0), "TP3": (33.0, 69.5, 0), "TP4": (36.0, 69.5, 0),
    "TP5": (39.0, 69.5, 0), "TP6": (42.0, 69.5, 0), "TP7": (45.0, 69.5, 0),
    # mounting holes and fiducials
    "H1": (3.5, 3.5, 0), "H2": (W - 3.5, 3.5, 0), "H3": (3.5, H - 3.5, 0), "H4": (W - 3.5, H - 3.5, 0),
    "FID1": (8.8, 1.8, 0), "FID2": (62.0, 60.5, 0), "FID3": (8.5, 66.5, 0),
}
# Parts hidden under the Teensy: their reference designators go to F.Fab only.
UNDER_TEENSY = {r for r, (x, y, _) in PLACE.items()
                if TX + 1.5 < x < TX + 13.7 and y < TY + 60 and r != "U2"}

LABELS = [  # (text, x, y, size, angle, layer). Bottom silk carries the full pinout legend.
    ("24V IN", 9.0, 12.6, 0.8, 90, "F.SilkS"),
    ("E-STOP", 9.0, 57.5, 0.8, 90, "F.SilkS"),
    ("J10 EXPANSION", 52.3, 1.45, 0.8, 0, "F.SilkS"),
    ("J7 PANEL", 52.3, 54.3, 0.8, 0, "F.SilkS"),
    ("OLED", 61.3, 8.2, 0.8, 0, "F.SilkS"),
    ("SYNC", 61.0, 21.1, 0.8, 0, "F.SilkS"),
    ("LIM NEAR", 61.0, 35.4, 0.8, 0, "F.SilkS"),
    ("LIM FAR", 61.0, 45.6, 0.8, 0, "F.SilkS"),
    ("5V", 30.0, 67.6, 0.8, 0, "F.SilkS"), ("3V3", 33.0, 67.6, 0.8, 0, "F.SilkS"),
    ("3VA", 36.0, 67.6, 0.8, 0, "F.SilkS"), ("ENA", 39.0, 67.6, 0.8, 0, "F.SilkS"),
    ("GND", 42.0, 67.6, 0.8, 0, "F.SilkS"), ("GND", 45.0, 67.6, 0.8, 0, "F.SilkS"),
    ("J1 24V IN: +24V (before contactor) / 0V", 14.2, 16.0, 0.9, 90, "B.SilkS"),
    ("J2 TB6600: PUL+ PUL- DIR+ DIR- ENA+ ENA-", 15.6, 16.0, 0.9, 90, "B.SilkS"),
    ("J3 HX711: 3V3A GND DOUT SCK", 17.0, 16.0, 0.9, 90, "B.SilkS"),
    ("J4/J5 LIMIT NEAR/FAR: SIG GND (NC sw)", 18.4, 16.0, 0.9, 90, "B.SilkS"),
    ("J6 E-STOP: 24V after contactor / its 0V (isolated)", 19.8, 16.0, 0.9, 90, "B.SilkS"),
    ("J7 PANEL: START STOP JOG+ JOG- HOME TARE", 21.2, 16.0, 0.9, 90, "B.SilkS"),
    ("J8 OLED (PH): GND 3V3 SCL SDA   J9 SYNC: +5V LED-", 22.6, 16.0, 0.9, 90, "B.SilkS"),
    ("J10 EXPANSION (phase 2)   Cut Teensy VUSB-VIN", 24.0, 16.0, 0.9, 90, "B.SilkS"),
    ("TENSOMETER CONTROLLER rev 0.4", 25.4, 16.0, 0.9, 90, "B.SilkS"),
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
    tb.SetRevision("0.4")
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
        if ref in UNDER_TEENSY or ref.startswith(("H", "FID")) or ref in ("J7", "J10"):  # J7/J10: labelled by text
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
    add_gnd_taps(board, net_objs["GND"])

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
