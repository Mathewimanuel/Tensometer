# Tensometer PC app

Python PC app for the tensometer (CLAUDE.md, `docs/decisions.md` 2026-11-13/16). Not a
separate repo: lives here specifically so protocol changes and their PC-side mirror land
in one commit (see `docs/decisions.md` 2026-11-13).

## Layout

```
software/
  pyproject.toml
  src/tensometer/
    wire/          # pure logic, mirrors firmware/lib/{crc16,cobs,frame,messages} and
                    # firmware/include/protocol.h -- no serial/GUI dependency, fully
                    # unit tested. Hand-mirrored by design (docs/decisions.md 2026-11-16),
                    # not generated: any firmware protocol.h change must be mirrored here
                    # by hand, and PROTOCOL_VERSION (wire.protocol.VERSION_MAJOR/MINOR)
                    # must match.
    session.py     # pure logic: HELLO handshake, one-command-at-a-time seq correlation
                    # (ACK/NACK/timeout), heartbeat health. No serial I/O, no Qt; testable
                    # with injected frames and a fake clock.
    transport.py   # thin driver: wires Session to a real pyserial port on a background
                    # thread. Hardware-adjacent, tested structurally against pyserial's
                    # loop:// virtual port (no real Teensy needed -- there's no command
                    # dispatcher on the firmware side yet anyway).
    cli.py         # minimal runnable entry point (`tensometer-cli <port>`): connects and
                    # prints every decoded event. A debug console, not the real PC app.
    units.py       # mm/N/percent/mm-per-min <-> the wire's um/mN/permille/steps.
    controller.py  # human-units app API (Controller) on top of Session/Transport -- the
                    # one place that turns an action into wire bytes; GUI calls only this.
    datalog.py     # per-test CSV/metadata logging and reload (TestLogger, load_*).
    gui/           # PySide6 + pyqtgraph. live_plot.py/cycle_plot.py (plotting),
                    # control_panel.py (state-gated buttons), dialogs.py (ARM config,
                    # specimen metadata -- two separate dialogs, not one), review_tab.py
                    # (load-and-replot), main_window.py (wires it all together),
                    # app.py (`tensometer-gui` entry point). Headless-tested via
                    # QT_QPA_PLATFORM=offscreen (tests/conftest.py), same as everything
                    # else -- not left untested the way a GUI normally would be.
  tests/
```

Pure logic (`wire/`, `session.py`, `units.py`) was built before anything hardware- or
GUI-dependent, same order as firmware's own `lib/`-before-`src/` split. See
`docs/decisions.md` for the full design history (plotting, GUI layout, CSV/metadata
schema, robustness decisions) and what was caught along the way.

## Running tests

```
pip install -e ".[dev]"
ruff check .
pytest -v
```

## Running the debug CLI

```
tensometer-cli --list      # list available serial ports
tensometer-cli COM5        # connect and print decoded events as they arrive
```

## Running the GUI

```
tensometer-gui
```

Against real hardware, either will show `Connected` the moment the Teensy sends its
`HELLO`, but nothing beyond that yet -- `main.cpp` has no command dispatcher, so any
`CMD_*` sent from here won't get an ACK/NACK back until that's built.
