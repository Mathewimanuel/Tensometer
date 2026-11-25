# Tensometer

Stepper-driven ball-screw tensile tester for knit fabrics with embedded conductors, plus ABS and neoprene dog-bones. Teensy 4.1 firmware (`firmware/`) and a Python PC app (`software/`), protocol 0.7 shared by hand between the two.

- `CLAUDE.md`: how Claude Code works on this repo, current status, rules for agents.
- `.claude/skills/tensometer-firmware/SKILL.md`: hardware facts, architecture, safety rules, review checklist.
- `docs/decisions.md`: dated decision log (newest first). `docs/handover.md`: project context. `docs/datasheets/`: manufacturer datasheets.
- `firmware/`: PlatformIO project. Pure logic in `lib/` with native tests, hardware drivers in `src/`.
- `software/`: PySide6 PC app, CLI, and a protocol-level fake firmware (`software/README.md`).
- GitHub issues are the work queue; labels `decision` and `hardware` need Mathew.

## Quick start

Firmware (no hardware needed):

```
pip install platformio
cd firmware
pio test -e native         # unit tests
pio run -e teensy41        # phase 1 firmware
pio run -e teensy41_sim    # phase 1 + simulated continuity module
```

PC app, against the fake firmware:

```
cd software
pip install -e ".[dev]"
pytest -v
tensometer-sim                          # serves a fake Teensy on a TCP socket
python -m tensometer.gui.app            # then connect to socket://localhost:<port>
```

CI runs both: `.github/workflows/firmware.yml` (hook self-test, cppcheck, native tests, both Teensy builds with `-Werror`, memory budget) and `.github/workflows/software.yml` (ruff, pytest).

## Status

Firmware: state machine, motion planner, step ISR, homing sequencer, break-detection classifier, core safety rules, command dispatcher and comms link are implemented and natively tested. Force is fail-closed until the HX711 driver lands (#10), so this build cannot move the motor. Bench bring-up must not be trusted until reset-safe pulls are fitted (#18).

PC app: connect/HELLO with exact protocol-version check, HOME/JOG/ARM/START/STOP, live and cycle plots, per-test CSV logging, review tab.

Flashing the real machine is a manual step with the e-stop in reach; agents are blocked from uploading by `.claude/hooks/block-flash.sh`.
