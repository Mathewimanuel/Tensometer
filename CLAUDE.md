# Tensometer: instructions for Claude Code

Stepper-driven ball-screw tensile tester (Teensy 4.1 firmware, later a Python PC app) for knit fabrics with embedded conductors, plus ABS and neoprene dog-bones. Owner: Mathew (sole engineer). Phase 1 = stress-strain only; impedance modules come later.

## Where the truth lives

1. `.claude/skills/tensometer-firmware/SKILL.md` — hardware facts, architecture, safety rules, checklist. **Load it for any firmware work.** If code and skill disagree, stop and flag it.
2. `docs/decisions.md` — what was decided, when, and what is still only PROPOSED.
3. `docs/handover.md` — mechanical and project context. The drive is a **DTGN 5:1 gearbox + TB6600 on 24 V**. Anything mentioning MG10, DM542 or 36 V is outdated.
4. `docs/datasheets/` — manufacturer datasheets. `TEENSY41-L_DATASHEET-EN.pdf` is the source for stock Teensy 4.1 board specs (pin counts, voltages, memory, peripherals). Project-specific pin assignments and hardware decisions still live in the skill (section 2.6), not here.

## How to work with Mathew

- Casual and direct. Wants verified answers, not guesses.
- Act as a critical senior reviewer: check margins, name failure modes, disagree when warranted, correct your own mistakes openly.
- Ask before deciding anything PROVISIONAL, OPEN or PROPOSED, or anything touching hardware, pins, safety behaviour or the protocol.
- Never invent hardware values. Use `// TODO(verify):` instead.

## Commands (run from `firmware/`)

```
pio test -e native          # unit tests for pure logic in lib/ (must pass before any hand-off)
pio run -e teensy41         # phase 1 firmware build
pio run -e teensy41_sim     # phase 1 + simulated continuity module
```

CI (`.github/workflows/firmware.yml`) runs on every push: the hook self-test, cppcheck (any finding fails), native tests, both Teensy builds with `-Werror` on our sources, and a memory budget check (`firmware/tools/check_size.py`). Run cppcheck locally with the same command as CI before pushing.

## Rules for agents

- **Never flash firmware to the real machine, and never run anything that could move the motor.** Uploading is Mathew's job, with the e-stop in reach. This is enforced, not just asked: `.claude/settings.json` denies upload commands and runs `.claude/hooks/block-flash.sh` before every shell command (uploads, Teensy loader tools, `pio test` on non-native envs, raw serial writes). If it blocks you, stop and ask; never work around it. A future hardware-in-the-loop rig may be flashed by agents once it exists and Mathew changes the hook.
- **Independent review:** after changing firmware, especially safety, ISRs, motion, HX711, break detection or the protocol, run the `firmware-reviewer` subagent on the diff before handing off. Fix or report every CRITICAL and MAJOR finding.
- Work loop: change → `pio test -e native` → `pio run` for every env → fix → repeat. Do not hand off with failing tests or builds; if something was not built or run, say so.
- Pure logic goes in `firmware/lib/` with no Arduino includes, and gets native tests. Hardware access stays in thin driver files under `firmware/src/`.
- Keep module isolation: core code never calls module code directly (skill section 1.2).
- Run the skill's pre-delivery checklist (section 14) and report the result.
- Record new decisions in `docs/decisions.md` (dated, with status).
- Commits: small and descriptive. Do not rewrite history on `main`.

## Work queue and memory between sessions

Sessions don't remember each other. The repo is the only memory: if it isn't written here, it didn't happen.

- **GitHub issues are the work queue.** At the start of a session with no specific task, list open issues (`gh api repos/Mathewimanuel/tensometer/issues`; GraphQL is unavailable in some sessions, so prefer the REST API). Labels: `decision` (needs Mathew, never decide it yourself), `hardware` (needs a measurement on the real parts), `firmware`, `tooling`, `phase-2`, `blocked`.
- Never start an issue labelled `blocked` or `decision`. When a blocking issue closes, remove `blocked` from the issues it unblocks.
- When Mathew reports a measurement or a decision: record it in `docs/decisions.md` (dated), update the skill and `config.h`/`limits.h` where it applies, close the issue with a comment linking the commit.
- Reference issues in commits (`Closes #3`) so the history explains itself.
- **The firmware skill has two copies:** this repo (master) and Mathew's claude.ai skills. If you change `.claude/skills/tensometer-firmware/SKILL.md`, say so clearly in your final message so Mathew can update the claude.ai copy.

## Current status (2026-10-14)

- Done: repo skeleton, config/limits/protocol/module headers, safe-boot `main.cpp`, `crc16`, `cobs` and `frame` libraries with native tests, CI with static analysis and memory budget, flash-blocking hook, reviewer subagent.
- Next: see open GitHub issues. Unblocked firmware work: #13 (break detection), #12 (protocol bodies). Motion (#9) waits on decision #1; HX711 driver (#10) waits on decision #2.
