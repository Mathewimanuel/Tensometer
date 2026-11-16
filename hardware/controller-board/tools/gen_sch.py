"""Generate the controller board schematic (KiCad 7 format; KiCad 8/9/10 open and upgrade it).

Every used pin gets a short wire stub ending in a net label (or a power symbol for power
nets); unused pins get a no-connect flag. Symbols are copied from the installed KiCad
libraries (derived symbols flattened) plus the project library for the Teensy.

Usage: python3 gen_sch.py <out_dir>
"""
import copy
import os
import sys
import uuid as _uuid

from design import GROUP_TITLES, PARTS, POWER_NETS, PWR_FLAG_NETS, TEENSY_LEFT, TEENSY_RIGHT
from sexp import Q, dump, find, first, parse

SYMLIB = "/usr/share/kicad/symbols"
PROJECT = "tensometer-controller"
ROOT_UUID = "6a1f2c4e-0b7d-4e8a-9c31-5d2e7f40a001"
NS = _uuid.UUID("2f0c7b5e-3d1a-4c8e-9f6b-1a2b3c4d5e6f")
STUB = 2.54


def uid(*key):
    return str(_uuid.uuid5(NS, "/".join(map(str, key))))


# ------------------------------------------------------------------ library symbols
_lib_cache = {}


def load_lib(nick):
    if nick not in _lib_cache:
        path = os.path.join(SYMLIB, nick + ".kicad_sym") if nick != "tensometer" else TEENSY_LIB_PATH
        tree = parse(open(path, encoding="utf-8").read())[0]
        _lib_cache[nick] = {s[1]: s for s in find(tree, "symbol")}
    return _lib_cache[nick]


def flattened(lib_id):
    nick, name = lib_id.split(":")
    lib = load_lib(nick)
    sym = copy.deepcopy(lib[name])
    ext = first(sym, "extends")
    if ext:
        parent = flattened(f"{nick}:{ext[1]}")
        parent_name = parent[1].split(":")[1]
        child_props = {p[1]: p for p in find(sym, "property")}
        out = [sym[0], Q(name)]
        for c in parent[2:]:
            if isinstance(c, list) and c[0] == "property" and c[1] in child_props:
                out.append(child_props.pop(c[1]))
            elif isinstance(c, list) and c[0] == "symbol":
                c = copy.deepcopy(c)
                c[1] = Q(c[1].replace(parent_name + "_", name + "_", 1))
                out.append(c)
            else:
                out.append(c)
        # Child-only properties go before the unit sub-symbols.
        idx = next(i for i, c in enumerate(out) if isinstance(c, list) and c[0] == "symbol")
        for p in child_props.values():
            out.insert(idx, p)
        sym = out
    sym[1] = Q(f"{nick}:{name}")
    return sym


def pins_of(sym):
    """pin number -> (x, y, angle, length, type) in symbol coordinates (Y up)."""
    pins = {}
    for unit in find(sym, "symbol"):
        for p in find(unit, "pin"):
            at = first(p, "at")
            length = float(first(p, "length")[1])
            num = first(p, "number")[1]
            pins[str(num)] = (float(at[1]), float(at[2]), float(at[3]), length, p[1])
    return pins


# ------------------------------------------------------------------ Teensy symbol
TEENSY_LIB_PATH = None


def write_teensy_lib(path):
    global TEENSY_LIB_PATH
    TEENSY_LIB_PATH = path
    pin_sx = []
    top = 29.21
    seen_3v3 = False

    def ptype(name):
        nonlocal seen_3v3
        if name in ("GND", "VIN"):
            return "power_in"
        if name == "3V3":
            if not seen_3v3:
                seen_3v3 = True
                return "power_out"
            return "passive"
        return "bidirectional"

    def pin(num, name, x, y, ang):
        label = {"3V3": "3.3V"}.get(name, name if not name.isdigit() else name)
        return ["pin", ptype(name), "line", ["at", x, y, ang], ["length", 3.81],
                ["name", Q(label), ["effects", ["font", ["size", 1.27, 1.27]]]],
                ["number", Q(str(num)), ["effects", ["font", ["size", 1.27, 1.27]]]]]

    for i, name in enumerate(TEENSY_LEFT):
        pin_sx.append(pin(i + 1, name, -16.51, round(top - i * 2.54, 2), 0))
    for j, name in enumerate(TEENSY_RIGHT):  # pads 25..48, bottom -> top
        pin_sx.append(pin(25 + j, name, 16.51, round(top - (23 - j) * 2.54, 2), 180))

    def prop(k, v, x, y, hide=False):
        eff = ["effects", ["font", ["size", 1.27, 1.27]]]
        if hide:
            eff.append("hide")
        return ["property", Q(k), Q(v), ["at", x, y, 0], eff]

    sym = ["symbol", Q("Teensy4.1"), ["pin_names", ["offset", 1.016]], ["in_bom", "yes"], ["on_board", "yes"],
           prop("Reference", "U", 0, 31.75), prop("Value", "Teensy 4.1", 0, -31.75),
           prop("Footprint", "tensometer:Teensy41_Socket", 0, -34.29, True),
           prop("Datasheet", "https://www.pjrc.com/store/teensy41.html", 0, -36.83, True),
           ["symbol", Q("Teensy4.1_0_1"),
            ["rectangle", ["start", -12.7, 30.48], ["end", 12.7, -30.48],
             ["stroke", ["width", 0.254], ["type", "default"]], ["fill", ["type", "background"]]],
            ["text", Q("USB"), ["at", 0, 27.94, 0], ["effects", ["font", ["size", 1.27, 1.27]]]]],
           ["symbol", Q("Teensy4.1_1_1")] + pin_sx]
    lib = ["kicad_symbol_lib", ["version", 20220914], ["generator", "tensometer_gen"], sym]
    with open(path, "w", encoding="utf-8") as f:
        f.write(dump(lib) + "\n")


# ------------------------------------------------------------------ schematic builder
class Sheet:
    def __init__(self):
        self.lib_symbols = {}
        self.items = []
        self.pwr_count = 0

    def lib(self, lib_id):
        if lib_id not in self.lib_symbols:
            self.lib_symbols[lib_id] = flattened(lib_id)
        return self.lib_symbols[lib_id]

    def symbol(self, lib_id, ref, value, footprint, x, y, hide_ref=False):
        sym = self.lib(lib_id)
        u = uid("sym", ref)
        eff = lambda hide=False: ["effects", ["font", ["size", 1.27, 1.27]]] + (["hide"] if hide else [])
        node = ["symbol", ["lib_id", Q(lib_id)], ["at", x, y, 0], ["unit", 1],
                ["in_bom", "no" if ref.startswith("#") else "yes"], ["on_board", "no" if ref.startswith("#") else "yes"],
                ["dnp", "no"], ["uuid", Q(u)]]
        # Field positions and visibility come from the library symbol (designed to not
        # overlap its graphics), translated to the placement point.
        libprops = {p[1]: p for p in find(sym, "property")}
        for key, val, force_hide in (("Reference", ref, hide_ref), ("Value", value, False),
                                     ("Footprint", footprint, True), ("Datasheet", "~", True)):
            lp = libprops.get(key)
            if lp is not None:
                at = first(lp, "at")
                fx, fy, fa = round(x + float(at[1]), 2), round(y - float(at[2]), 2), at[3] if len(at) > 3 else 0
                e = copy.deepcopy(first(lp, "effects")) or eff()
                if force_hide and "hide" not in e:
                    e.append("hide")
                node.append(["property", Q(key), Q(val), ["at", fx, fy, fa], e])
            else:
                node.append(["property", Q(key), Q(val), ["at", x, y, 0], eff(force_hide)])
        for num in pins_of(sym):
            node.append(["pin", Q(num), ["uuid", Q(uid("pin", ref, num))]])
        node.append(["instances", ["project", Q(PROJECT),
                                   ["path", Q("/" + ROOT_UUID), ["reference", Q(ref)], ["unit", 1]]]])
        self.items.append(node)
        return u

    def wire(self, x1, y1, x2, y2):
        self.items.append(["wire", ["pts", ["xy", x1, y1], ["xy", x2, y2]],
                           ["stroke", ["width", 0], ["type", "default"]], ["uuid", Q(uid("w", x1, y1, x2, y2))]])

    def label(self, net, x, y, ang):
        just = {0: "left", 180: "right", 90: "left", 270: "right"}[ang]
        self.items.append(["label", Q(net), ["at", x, y, ang], ["fields_autoplaced"],
                           ["effects", ["font", ["size", 1.27, 1.27]], ["justify", just, "bottom"]],
                           ["uuid", Q(uid("lbl", net, x, y))]])

    def no_connect(self, x, y):
        self.items.append(["no_connect", ["at", x, y], ["uuid", Q(uid("nc", x, y))]])

    def power(self, net, x, y, ang):
        self.pwr_count += 1
        lib_id = POWER_NETS[net] if net in POWER_NETS else "power:PWR_FLAG"
        ref = f"#PWR{self.pwr_count:03d}" if net in POWER_NETS else f"#FLG{self.pwr_count:03d}"
        value = net if net in POWER_NETS else "PWR_FLAG"
        # GND points down; supply symbols point up. Rotate so the symbol sits beyond the stub.
        self.symbol(lib_id, ref, value, "", x, y, hide_ref=True)
        node = self.items[-1]
        node[2] = ["at", x, y, ang]
        if ang in (90, 270):
            # Sideways symbol: put the value text horizontally, just beyond the symbol body.
            # Field angles are relative to the symbol, so 90 here reads horizontal.
            away = -1 if ang == 90 else 1          # GND at 270 / supply at 90 point the other way
            is_gnd = net == "GND"
            sign = away if not is_gnd else -away
            for p in find(node, "property"):
                if p[1] == "Value":
                    p[3] = ["at", round(x + sign * 6.35, 2), y, 90]

    def text(self, s, x, y, size=2.0):
        self.items.append(["text", Q(s), ["at", x, y, 0], ["effects", ["font", ["size", size, size]], ["justify", "left", "bottom"]],
                           ["uuid", Q(uid("txt", s))]])

    def save(self, path):
        root = ["kicad_sch", ["version", 20230121], ["generator", "eeschema"], ["uuid", Q(ROOT_UUID)], ["paper", "A2"],
                ["title_block", ["title", Q("Tensometer controller board (rough)")],
                 ["date", Q("2026-11-16")], ["rev", Q("0.1")],
                 ["comment", 1, Q("Generated by hardware/controller-board/tools/gen_sch.py")],
                 ["comment", 2, Q("Phase 1: Teensy 4.1, TB6600 via 74AHCT244, HX711, limits, e-stop sense, panel")]],
                ["lib_symbols"] + list(self.lib_symbols.values())]
        root += self.items
        root.append(["sheet_instances", ["path", Q("/"), ["page", Q("1")]]])
        with open(path, "w", encoding="utf-8") as f:
            f.write(dump(root) + "\n")


def place_part(sh, part, x, y):
    ref, lib_id, value, fp, pin_nets, _grp = part
    sh.symbol(lib_id, ref, value, fp, x, y)
    for num, (px, py, ang, _len, ptype) in pins_of(sh.lib(lib_id)).items():
        ax, ay = round(x + px, 2), round(y - py, 2)
        net = pin_nets.get(num)
        if net is None:
            sh.no_connect(ax, ay)
            continue
        out = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}[int(ang) % 360]  # outward, schematic Y-down
        ex, ey = round(ax + out[0] * STUB, 2), round(ay + out[1] * STUB, 2)
        sh.wire(ax, ay, ex, ey)
        if net in POWER_NETS:
            down = net == "GND"
            # Rotate the power symbol so its body points away from the part.
            dir_ang = {(-1, 0): 90, (1, 0): 270, (0, 1): 180, (0, -1): 0}[out]
            if down:
                dir_ang = (dir_ang + 180) % 360
            sh.power(net, ex, ey, dir_ang)
        else:
            sh.label(net, ex, ey, {(-1, 0): 180, (1, 0): 0, (0, 1): 270, (0, -1): 90}[out])


# Layout: groups in columns on an A2 sheet (594 x 420 mm).
LAYOUT = {
    "power": (30, 40), "driver": (30, 175), "mcu": (215, 70), "hx711": (30, 300),
    "limits": (330, 40), "estop": (330, 175), "panel": (480, 40), "oled": (480, 230),
    "sync": (330, 300), "expansion": (480, 300), "mech": (215, 330),
}
GROUP_COLS = {"power": 4, "driver": 4, "hx711": 4, "limits": 3, "estop": 3, "panel": 2,
              "oled": 3, "sync": 3, "expansion": 1, "mech": 4, "mcu": 1}


def main(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    write_teensy_lib(os.path.join(out_dir, "tensometer.kicad_sym"))
    sh = Sheet()
    groups = {}
    for part in PARTS:
        groups.setdefault(part[5], []).append(part)
    for grp, parts in groups.items():
        gx, gy = LAYOUT[grp]
        sh.text(GROUP_TITLES[grp], gx, gy - 10, 2.0)
        cols = GROUP_COLS[grp]
        dx, dy = (45.72, 33.02) if grp not in ("panel",) else (40.64, 22.86)
        for i, part in enumerate(parts):
            if grp == "mcu":
                x, y = gx + 40.64, gy + 35.56
            elif grp in ("driver",) and part[0] == "U3":
                x, y = gx + 27.94, gy + 25.4
            else:
                col, row = i % cols, i // cols
                x = gx + 12.7 + col * dx
                y = gy + 12.7 + row * dy
                if grp == "driver":  # leave room for U3 at the top-left
                    x, y = gx + 12.7 + (i % 3 + 1) * 40.64 + 30.48, gy + 12.7 + (i // 3) * 30.48
            place_part(sh, part, round(x / 1.27) * 1.27, round(y / 1.27) * 1.27)
    # PWR_FLAGs on their own labelled stubs below the title area.
    fx, fy = 215.9, 30.48
    for k, net in enumerate(PWR_FLAG_NETS):
        x = fx + k * 25.4
        sh.power("PWR_FLAG", x, fy, 0)
        sh.wire(x, fy, x, fy + STUB)
        if net in POWER_NETS:
            sh.power(net, x, fy + STUB, 180 if net != "GND" else 0)
        else:
            sh.label(net, x, fy + STUB, 270)
    sh.text("PWR_FLAGs", fx, fy - 10, 2.0)
    sh.save(os.path.join(out_dir, "tensometer-controller.kicad_sch"))


if __name__ == "__main__":
    main(sys.argv[1])
