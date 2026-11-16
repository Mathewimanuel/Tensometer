"""Build the controller PCB (rough placement, unrouted) from the KiCad-exported netlist.

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
W, H = 125.0, 88.0          # board size, mm
OX, OY = 50.0, 50.0         # board origin on the page
SCH_FILE = f"{PROJECT}.kicad_sch"


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
  (descr "Teensy 4.1 in two 1x24 female headers, 0.1in pitch, rows 0.6in apart. Pad order: left column top->bottom 1-24, right column bottom->top 25-48 (USB end at the top).")
  (tags "Teensy 4.1 socket")
  (attr through_hole)
  (fp_text reference "REF**" (at 7.62 -4.5) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))
  (fp_text value "Teensy41_Socket" (at 7.62 61.5) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))
  (fp_text user "USB" (at 7.62 -1.0) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))
  (fp_rect (start {body[0]}) (end {body[1]}) (stroke (width 0.12) (type default)) (fill none) (layer "F.SilkS"))
  (fp_rect (start {body[0]}) (end {body[1]}) (stroke (width 0.1) (type default)) (fill none) (layer "F.Fab"))
  (fp_rect (start 5.0 -3.8) (end 10.24 0.5) (stroke (width 0.1) (type default)) (fill none) (layer "F.Fab"))
  (fp_rect (start -1.77 -4.0) (end 17.01 60.19) (stroke (width 0.05) (type default)) (fill none) (layer "F.CrtYd"))
{chr(10).join(pads)}
)
"""
    with open(os.path.join(pretty_dir, "Teensy41_Socket.kicad_mod"), "w") as f:
        f.write(text)


# ------------------------------------------------------------------ placement (mm, deg)
# Board: Teensy in the middle, USB at the top edge; machine wiring on the left and bottom
# edges, operator/expansion connectors on the right.
PLACE = {
    # power, top-left: 24 V in -> F1 -> D1 -> U1 buck -> +5 V -> D4 -> Teensy VIN
    "J1": (6.5, 12, 270), "F1": (17, 8, 0), "D1": (25.5, 8, 0), "D2": (17, 14.5, 0), "C1": (25.5, 14.5, 90),
    "U1": (33, 8, 0), "C2": (45.5, 8, 0), "D4": (45.5, 12.5, 180), "C3": (45.5, 17, 0),
    "R1": (33, 15, 0), "D3": (38, 15, 0),
    # Teensy (USB at the top edge)
    "U2": (55, 12, 0),
    # driver interface, left-middle
    "J2": (10.6, 28, 270), "U3": (31, 35, 0), "C4": (37.5, 27, 0),
    "R2": (44, 26, 90), "R3": (47.5, 26, 90), "R4": (44, 32.5, 90),
    # HX711, left-bottom
    "J3": (5.5, 66, 270), "U4": (17, 72, 0), "C5": (16, 77.5, 0), "C6": (21.5, 77.5, 0),
    "R5": (15, 64, 0), "R6": (15, 67.5, 0),
    # e-stop sense, bottom edge left
    "J6": (28, 81.5, 0), "R11": (27, 73.5, 0), "D5": (34, 73.5, 180), "U5": (38, 63, 0),
    "R12": (48.5, 66, 90), "C9": (48.5, 72, 90),
    # limit switches, bottom edge
    "J4": (53, 82, 0), "R7": (53, 76, 0), "R8": (58, 76, 0), "C7": (62.5, 76, 90),
    "J5": (68, 82, 0), "R9": (68, 76, 0), "R10": (73, 76, 0), "C8": (77.5, 76, 90),
    # panel pull-ups/filters, then the ribbon header at the right
    "R13": (80, 50, 90), "C10": (83.6, 50, 90), "R14": (87.2, 50, 90), "C11": (90.8, 50, 90),
    "R15": (94.4, 50, 90), "C12": (98, 50, 90), "R16": (80, 58, 90), "C13": (83.6, 58, 90),
    "R17": (87.2, 58, 90), "C14": (90.8, 58, 90), "R18": (94.4, 58, 90), "C15": (98, 58, 90),
    "R19": (92, 66, 0), "J7": (107, 66, 90),
    # expansion + OLED, right edge
    "J10": (110, 14, 0), "J8": (117, 40, 0), "R20": (106, 41, 90), "R21": (109.5, 41, 90),
    # sync LED, top-right
    "J9": (85, 6, 0), "Q1": (81, 15, 0), "R22": (76, 15, 90), "R23": (79, 21, 0), "R24": (87, 15, 90),
    # mounting holes
    "H1": (3.5, 3.5, 0), "H2": (W - 3.5, 3.5, 0), "H3": (3.5, H - 3.5, 0), "H4": (W - 3.5, H - 3.5, 0),
}

LABELS = [  # silkscreen text: (text, x, y, size)
    ("24V IN +/-", 1.5, 21.3, 0.8),
    ("TB6600: PUL+ PUL- DIR+ DIR- ENA+ ENA-", 12, 23.0, 0.8),
    ("HX711: 3V3A GND DOUT SCK", 2, 59, 0.8),
    ("E-STOP: 24V_SW / 0V", 37, 81, 0.8),
    ("LIM NEAR", 50, 86.8, 0.8), ("LIM FAR", 65.5, 86.8, 0.8),
    ("PANEL", 103, 74.5, 1.0), ("OLED: GND 3V3 SCL SDA", 101, 37, 0.8),
    ("EXPANSION (phase 2)", 99, 9.5, 0.8), ("SYNC LED", 91.5, 6, 0.8),
    ("TENSOMETER CONTROLLER v0.1 (rough)", 82, 86.0, 1.0),
    ("Cut Teensy VUSB-VIN pad", 58, 4.5, 0.8),
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


def main(out_dir, net_xml):
    pretty = os.path.join(out_dir, "tensometer.pretty")
    write_teensy_fp(pretty)
    nets = netlist(net_xml)

    board = pcbnew.BOARD()
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    tb = board.GetTitleBlock()
    tb.SetTitle("Tensometer controller board (rough)")
    tb.SetRevision("0.1")
    tb.SetDate("2026-11-16")
    tb.SetComment(0, "Generated by hardware/controller-board/tools/gen_pcb.py - placement only, unrouted")

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
        for pad in fp.Pads():
            name = nets.get(ref, {}).get(pad.GetNumber())
            if name:
                pad.SetNet(net_objs[name])
        board.Add(fp)

    add_rect(board, pcbnew.Edge_Cuts, 0, 0, W, H, 0.1)
    for text, x, y, size in LABELS:
        t = pcbnew.PCB_TEXT(board)
        t.SetText(text)
        t.SetPosition(pt(x, y))
        t.SetLayer(pcbnew.F_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
        t.SetTextThickness(mm(size * 0.15))
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
        board.Add(t)

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
