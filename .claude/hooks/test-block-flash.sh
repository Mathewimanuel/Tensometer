#!/usr/bin/env bash
# Self-test for block-flash.sh. Run: bash .claude/hooks/test-block-flash.sh
# Also run in CI so the guard cannot silently stop working.

set -u
HOOK="$(dirname "$0")/block-flash.sh"
fail=0

# CRLF line endings break the hook in Git Bash (and it fails closed, blocking everything).
if grep -q $'\r' "$HOOK"; then
    echo "FAIL: $HOOK has CRLF line endings. Fix: rm .claude/hooks/*.sh && git checkout -- .claude/hooks"
    exit 1
fi

check() {  # check <expected exit> <command>
    local expect="$1" cmd="$2" json got
    # JSON-escape backslashes and quotes in pure bash (no python3 needed on Windows).
    local esc="${cmd//\\/\\\\}"
    esc="${esc//\"/\\\"}"
    json=$(printf '{"tool_name":"Bash","tool_input":{"command":"%s"}}' "$esc")
    printf '%s' "$json" | bash "$HOOK" 2>/dev/null
    got=$?
    if [[ "$got" != "$expect" ]]; then
        echo "FAIL (expected $expect, got $got): $cmd"
        fail=1
    fi
}

# Must be blocked (exit 2)
check 2 'pio run -t upload'
check 2 'cd firmware && pio run -e teensy41 -t upload'
check 2 'platformio run --target upload -e teensy41'
check 2 'pio run --target=upload'
check 2 'pio run -e teensy41 -t uploadfs'
check 2 'PIO RUN -T UPLOAD'
check 2 'teensy_loader_cli --mcu=TEENSY41 -w firmware.hex'
check 2 '~/.platformio/packages/tool-teensy/teensy_reboot'
check 2 'tycmd upload firmware.hex'
check 2 'pio test'
check 2 'pio test -e teensy41'
check 2 'cd firmware; pio test -e teensy41_sim'
check 2 'echo HOME > /dev/ttyACM0'
check 2 'python send.py COM3'
# Windows forms (Git Bash and PowerShell)
check 2 'pio.exe run -e teensy41 -t upload'
check 2 'C:\Users\mathew\.platformio\penv\Scripts\pio.exe run --target upload'
check 2 'C:\Users\mathew\.platformio\packages\tool-teensy\teensy_loader_cli.exe -w fw.hex'
check 2 'cd firmware; pio test'
check 2 'python -m platformio run -t upload'
check 2 'mode COM4 BAUD=115200'

# Must be allowed (exit 0)
check 0 'pio test -e native'
check 0 'cd firmware && pio test -e native -v'
check 0 'pio test --environment native'
check 0 'pio run -e teensy41'
check 0 'pio run -e teensy41_sim'
check 0 'pio pkg list'
check 0 'git commit -m "docs: explain upload procedure"'
check 0 'grep -rn upload_protocol firmware/platformio.ini'
check 0 'pio.exe test -e native'
check 0 'C:\Users\mathew\.platformio\penv\Scripts\pio.exe run -e teensy41'
check 0 'git status'

if [[ $fail == 0 ]]; then echo "block-flash hook: all checks passed"; fi
exit $fail
