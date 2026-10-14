#!/usr/bin/env bash
# PreToolUse hook: refuse any command that could flash a Teensy or talk to the machine.
# Flashing is Mathew's job, with the e-stop in reach (see CLAUDE.md).
#
# Reads the hook's JSON input on stdin and scans the raw text (no jq needed), so it
# also matches text in the tool description. False positives are acceptable: the
# hook is meant to fail closed. Exit 2 = block; stderr is shown to Claude.
# Tests: .claude/hooks/test-block-flash.sh

set -u
input="$(cat)"

block() {
    echo "BLOCKED by .claude/hooks/block-flash.sh: $1" >&2
    echo "Agents must never flash firmware, run on-target tests or write to the machine's serial port. Ask Mathew." >&2
    exit 2
}

shopt -s nocasematch

# Regexes live in variables so bash passes them to the regex engine unmodified.
SEP='[^;&|]*'                                   # stay within one shell command
RE_UPLOAD="(pio|platformio)${SEP}(-t|--target)[[:space:]=]*(upload|uploadfs|program)"
RE_LOADER='(teensy_loader_cli|teensy_post_compile|teensy_reboot|teensy_restart|tycmd|tyuploader)'
RE_TEST='(pio|platformio)[[:space:]]+test'
RE_TEST_NATIVE="(pio|platformio)[[:space:]]+test${SEP}(-e|--environment)[[:space:]=]*native"
RE_SERIAL='(>[[:space:]]*/dev/(tty(ACM|USB)|cu\.usbmodem)|[[:space:]]COM[0-9]+)'

[[ "$input" =~ $RE_UPLOAD ]] && block "PlatformIO upload target"
[[ "$input" =~ $RE_LOADER ]] && block "Teensy loader / reboot tool"
if [[ "$input" =~ $RE_TEST ]] && ! [[ "$input" =~ $RE_TEST_NATIVE ]]; then
    block "pio test without '-e native' uploads test firmware to the Teensy"
fi
[[ "$input" =~ $RE_SERIAL ]] && block "direct access to a serial port"

exit 0
