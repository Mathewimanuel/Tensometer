"""Run KiCad DRC on the board and summarise it by violation type.

Usage: python3 drc.py <board.kicad_pcb> <report.rpt> [--routed]
Unrouted connections are expected while the board is placement-only; everything else
(courtyard overlaps, clearance, items off the board) should be zero. With --routed,
unconnected items and starved thermals count too and a non-zero total fails the run.
"""
import collections
import re
import sys

import pcbnew


def main(pcb, rpt, routed=False):
    board = pcbnew.LoadBoard(pcb)
    pcbnew.WriteDRCReport(board, rpt, pcbnew.EDA_UNITS_MILLIMETRES, True)
    text = open(rpt).read()
    kinds = collections.Counter(re.findall(r"^\[([a-z_]+)\]", text, re.M))
    for k, n in kinds.most_common():
        print(f"{n:4d}  {k}")
    # Expected while rough: unrouted nets; lib warnings when the stock libs are not in this
    # machine's global library table; cosmetic silkscreen hits.
    expected = ("lib_footprint_issues", "silk_overlap", "silk_over_copper", "silk_edge_clearance")
    if not routed:
        expected += ("unconnected_items", "starved_thermal")
    real = {k: n for k, n in kinds.items() if k not in expected}
    print("PLACEMENT/ELECTRICAL ISSUES:", sum(real.values()), real or "")
    return 1 if routed and real else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2], "--routed" in sys.argv[3:]))
