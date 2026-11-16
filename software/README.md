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
  tests/
```

`wire/` is deliberately the only thing built so far. Serial transport, the command
dispatcher, and the PyQt/PySide GUI are not started (see `docs/decisions.md` for the full
design: plotting, GUI layout, CSV/metadata schema, robustness decisions -- all written
down before any of that code exists).

## Running tests

```
pip install -e ".[dev]"
ruff check .
pytest -v
```
