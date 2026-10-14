# Tensometer

Stepper-driven ball-screw tensile tester for knit fabrics with embedded conductors, plus ABS and neoprene dog-bones. Teensy 4.1 firmware now; a Python PC app later.

- `CLAUDE.md`: how Claude Code works on this repo.
- `.claude/skills/tensometer-firmware/SKILL.md`: hardware facts, architecture, safety rules, review checklist.
- `docs/decisions.md`: dated decision log. `docs/handover.md`: project context.
- `firmware/`: PlatformIO project.

## Quick start

```
pip install platformio
cd firmware
pio test -e native     # unit tests (no hardware)
pio run -e teensy41    # build phase 1 firmware
```

Flashing the real machine is a manual step with the e-stop in reach. The stage 1 firmware is a safe-boot skeleton and cannot move the motor.
