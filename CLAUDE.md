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
- When Mathew reports a measurement or a decision: record it in `docs/decisions.md` (dated), update the skill and `config.h`/`safety_limits.h` where it applies, close the issue with a comment linking the commit.
- Reference issues in commits (`Closes #3`) so the history explains itself.
- **The firmware skill has two copies:** this repo (master) and Mathew's claude.ai skills. If you change `.claude/skills/tensometer-firmware/SKILL.md`, say so clearly in your final message so Mathew can update the claude.ai copy.

## Current status (2026-10-26)

- Done: repo skeleton, config/safety_limits/protocol/module headers, safe-boot `main.cpp`, `crc16`, `cobs`, `frame`, `motion_profile`, `break_detect`, `messages`, `state_machine`, `homing` and `motion_guard` libraries with native tests (111 total across the suite), CI with static analysis and memory budget, flash-blocking hook, reviewer subagent. Motion rate-update method decided (#1, closed). Panel/LED/guard pins finalized and guard interlock confirmed not fitted (#8, closed). Protocol bumped to 0.2 with core message bodies (HELLO/ACK/NACK/HEARTBEAT/TELEMETRY/EVENT) defined (#12, closed). Break detection classifier implemented per skill section 7 (#13, closed); `break_detect::EventType` is deliberately not wire-compatible with `proto::EventType` and needs an explicit mapping function (not a cast) whenever it's wired into comms/SD. **State machine, motion planner and step ISR (#9, closed):** `lib/state_machine` implements skill section 5's full transition table (lock owner, ARM/L0 via an internal `arming()` flag with its own `ARM_CANCEL`, SEGMENT_DONE landing in a real HOLD, BREAK_HOLD's own RESET exit); `src/core/stepper.*` is the step-pulse generator/position tracker (IntervalTimer, highest-priority ISR); `lib/homing` sequences fast-approach/back-off/slow-reapproach/done; `src/core/motion_controller.*` is the glue that owns `position_valid` and drives homing through the planner into the stepper, taking the `|force|` homing guard as a caller-supplied bool (no HX711 yet, #10). A CRITICAL bug (silent direction-reversal rejection corrupting `position_valid`) was found and fixed during review; the fix itself is now in natively-tested `lib/motion_guard`, not left inline in untested `src/` code. Full history and every open sub-question: `docs/decisions.md` 2026-10-26.
- Confirmed safety gap, not yet fixed: no buffer chip and no reset-safe pull resistors on ENA/STEP/DIR (#18). Treat "motor disabled on reset" as unverified until a physical pull-down is added and bench bring-up step 2 passes.
- Two decisions made for #11 to implement when it's built (docs/decisions.md 2026-10-26): the core-safety loop must suspend its global limit-switch-trip-FAULT rule entirely while any homing phase is active (homing's own sequencer is the sole authority until `HOMING_DONE` or an explicit abort) -- note the LIM_NEAR-during-homing edge case flagged there, worth re-confirming when #11 is actually written; and inhibit-mask enforcement for every motion-starting event (`HOME`/`JOG_START`/`TARE_START`/`PRELOAD_START`/`ARM`/`START`) is #11's job, not `lib/state_machine`'s (only `RESUME` checks it there, deliberately).
- Next: see open GitHub issues. `motion_controller`/`homing`/`stepper`/`state_machine` are not yet wired into `main.cpp` -- that needs #11 (safety module, now unblocked) to exist first, since the limit-switch carve-out above has to be implemented somewhere before homing can safely run under the core-safety loop. Real bench bring-up of any of it must not be trusted until #18 (reset-safe pulls) is physically fixed and bring-up step 2 passes. HX711 driver (#10) still waits on decision #2 and bench numbers from #5.
