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
  tests/
```

Pure logic (`wire/`, `session.py`) is deliberately built before anything hardware- or
GUI-dependent, same order as firmware's own `lib/`-before-`src/` split. Serial transport
(`pyserial`), the command dispatcher, and the PyQt/PySide GUI are not started yet (see
`docs/decisions.md` for the full design: plotting, GUI layout, CSV/metadata schema,
robustness decisions -- all written down before any of that code exists).

## Running tests

```
pip install -e ".[dev]"
ruff check .
pytest -v
```
