#!/usr/bin/env bash
# Regenerate schematic + PCB and verify them. Needs KiCad 7+ (kicad-cli and the pcbnew
# Python module) and the stock KiCad symbol/footprint libraries.
# Usage: bash tools/build.sh   (from hardware/controller-board)
set -euo pipefail
cd "$(dirname "$0")/.."
NET="$(mktemp -d)/net.xml"
python3 tools/gen_sch.py .
kicad-cli sch export netlist --format kicadxml -o "$NET" tensometer-controller.kicad_sch >/dev/null
(cd tools && python3 check_netlist.py "$NET")
python3 tools/gen_pcb.py . "$NET"
python3 tools/drc.py tensometer-controller.kicad_pcb "$(dirname "$NET")/drc.rpt"
kicad-cli sch export pdf -o tensometer-controller-schematic.pdf tensometer-controller.kicad_sch >/dev/null
