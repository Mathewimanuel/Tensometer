#!/usr/bin/env python3
"""Fail the build if Teensy 4.1 memory headroom drops below the budget.

Reads a PlatformIO build log and parses the teensy_size report, e.g.
  teensy_size:   FLASH: code:..., data:..., headers:...   free for files:8114176
  teensy_size:    RAM1: variables:..., code:..., padding:...   free for local variables:503392
  teensy_size:    RAM2: variables:12416  free for malloc/new:511872

Usage: python3 tools/check_size.py build.log
"""
import re
import sys

# Budgets (bytes). Why these numbers:
# RAM1 free = stack for the main loop and ISRs; keep a generous margin.
# RAM2 (DMAMEM) must hold the 64 kB SD buffer and continuity ADC DMA buffers later,
# with room to spare.
MIN_FREE = {
    "FLASH": 4 * 1024 * 1024,
    "RAM1": 64 * 1024,
    "RAM2": 256 * 1024,
}

PATTERNS = {
    "FLASH": re.compile(r"FLASH:.*free for files:\s*(\d+)"),
    "RAM1": re.compile(r"RAM1:.*free for local variables:\s*(\d+)"),
    "RAM2": re.compile(r"RAM2:.*free for malloc/new:\s*(\d+)"),
}


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    text = open(sys.argv[1], encoding="utf-8", errors="replace").read()

    ok = True
    for region, pattern in PATTERNS.items():
        m = pattern.search(text)
        if not m:
            print(f"::error::check_size: no {region} line found in the build log "
                  "(teensy_size output format changed?)")
            ok = False
            continue
        free = int(m.group(1))
        need = MIN_FREE[region]
        status = "OK" if free >= need else "OVER BUDGET"
        print(f"{region}: {free / 1024:.0f} kB free (budget >= {need / 1024:.0f} kB) {status}")
        print(f"::notice title=memory {region}::{free / 1024:.0f} kB free, budget {need / 1024:.0f} kB")
        if free < need:
            print(f"::error::{region} headroom {free} B is below the {need} B budget")
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
