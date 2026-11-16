"""Compare the netlist KiCad exports from the schematic with design.py's intent.

Usage: python3 check_netlist.py <netlist.xml>   (kicad-cli sch export netlist --format kicadxml)
Exit 0 only if every intended net has exactly the intended pins and nothing extra exists.
"""
import sys
import xml.etree.ElementTree as ET

from design import PARTS, intended_nets


def kicad_nets(path):
    root = ET.parse(path).getroot()
    nets = {}
    for net in root.iter("net"):
        name = net.get("name").lstrip("/")
        nodes = sorted(f"{n.get('ref')}.{n.get('pin')}" for n in net.iter("node")
                       if not n.get("ref").startswith("#"))
        if not nodes:
            continue
        nets.setdefault(name, [])
        nets[name] = sorted(nets[name] + nodes)
    comps = {c.get("ref"): c.findtext("footprint") for c in root.iter("comp")}
    return nets, comps


def main(path):
    got, comps = kicad_nets(path)
    want = intended_nets()
    ok = True
    # Single-pin nets in KiCad export are named unconnected-(...); ignore those.
    got = {k: v for k, v in got.items() if not k.startswith("unconnected-") and not k.startswith("Net-(")} | \
          {k: v for k, v in got.items() if k.startswith("Net-(") and len(v) > 1}
    for net, pins in want.items():
        if got.get(net) != pins:
            ok = False
            print(f"MISMATCH {net}: want {pins} got {got.get(net)}")
    for net in got:
        if net not in want:
            ok = False
            print(f"EXTRA net in schematic: {net} {got[net]}")
    for ref, _lib, _val, fp, _p, _g in PARTS:
        if comps.get(ref) != fp:
            ok = False
            print(f"FOOTPRINT {ref}: want {fp} got {comps.get(ref)}")
    print(f"{len(want)} nets, {len(PARTS)} parts:", "MATCH" if ok else "DIFFERENCES")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
