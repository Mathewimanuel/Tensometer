#!/usr/bin/env bash
# Rebuild the whole board from tools/design.py and verify it.
#   schematic -> netlist check -> placement -> route -> DRC -> JLCPCB outputs
# Needs KiCad 7+ (kicad-cli + pcbnew Python), the stock KiCad libraries, and for routing a
# clone of https://github.com/drandyhaas/KiCadRoutingTools with its Rust router built
# (`python3 build_router.py`). Point KRT_DIR at it.
# Usage (from hardware/controller-board):  KRT_DIR=/path/to/KiCadRoutingTools bash tools/build.sh
set -euo pipefail
cd "$(dirname "$0")/.."
: "${KRT_DIR:?set KRT_DIR to a KiCadRoutingTools clone}"
mkdir -p build
NET=build/net.xml

python3 tools/gen_sch.py .
kicad-cli sch export netlist --format kicadxml -o "$NET" tensometer-controller.kicad_sch >/dev/null
(cd tools && python3 check_netlist.py "../$NET")
kicad-cli sch export pdf -o tensometer-controller-schematic.pdf tensometer-controller.kicad_sch >/dev/null

python3 tools/gen_pcb.py . "$NET" >/dev/null
mv tensometer-controller.kicad_pcb build/placement.kicad_pcb
(cd tools && python3 route_board.py ../build/placement.kicad_pcb project_template.kicad_pro \
    ../tensometer-controller.kicad_pcb "$KRT_DIR")
python3 tools/drc.py tensometer-controller.kicad_pcb build/drc.rpt
(cd tools && python3 gen_fab.py ../tensometer-controller.kicad_pcb ../jlcpcb)
rm -f tensometer-controller.kicad_prl
