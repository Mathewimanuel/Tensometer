#!/usr/bin/env bash
# Self-test for block-flash.sh. Run: bash .claude/hooks/test-block-flash.sh
# Also run in CI so the guard cannot silently stop working.

set -u
HOOK="$(dirname "$0")/block-flash.sh"
fail=0

check() {  # check <expected exit> <command>
    local expect="$1" cmd="$2" json got
    json=$(printf '{"tool_name":"Bash","tool_input":{"command":%s}}' "$(printf '%s' "$cmd" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))')")
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

# Must be allowed (exit 0)
check 0 'pio test -e native'
check 0 'cd firmware && pio test -e native -v'
check 0 'pio test --environment native'
check 0 'pio run -e teensy41'
check 0 'pio run -e teensy41_sim'
check 0 'pio pkg list'
check 0 'git commit -m "docs: explain upload procedure"'
check 0 'grep -rn upload_protocol firmware/platformio.ini'

if [[ $fail == 0 ]]; then echo "block-flash hook: all checks passed"; fi
exit $fail
