#!/bin/bash
# MIDI IN test: capture events on bento_ttymidi:TTY MIDI out.
#
# Without hardware loopback this test only verifies that the ALSA port is
# readable. With BENTO_MIDI_LOOPBACK=1 (TX shorted to RX on the HAT) it runs
# a full round-trip: aplaymidi -> TTY MIDI in -> UART -> TTY MIDI out -> aseqdump.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"

MID_FILE="${BENTO_TEST_MID:-$SCRIPT_DIR/fixtures/bento_test.mid}"
CAPTURE_SEC="${BENTO_CAPTURE_SEC:-5}"
TIMEOUT="${BENTO_TEST_TIMEOUT:-20}"
LOOPBACK="${BENTO_MIDI_LOOPBACK:-0}"
DUMP_FILE="$(mktemp /tmp/bento_midi_in.XXXXXX)"

cleanup() {
    rm -f "$DUMP_FILE"
}
trap cleanup EXIT

require_cmd aconnect
require_cmd aseqdump
require_bento_ttymidi

echo "=== bento_ttymidi MIDI IN test ==="
echo "Source port : $BENTO_MIDI_OUT_PORT ($CLIENT:TTY MIDI out)"
echo "Capture     : ${CAPTURE_SEC}s"
echo ""

if [ "$LOOPBACK" = "1" ]; then
    require_cmd aplaymidi
    if [ ! -f "$MID_FILE" ]; then
        echo "[ERROR] MIDI file not found: $MID_FILE"
        exit 1
    fi

    echo "Loopback mode: playing $MID_FILE while capturing on TTY MIDI out."
    echo "(Requires TX connected to RX on the MIDI UART.)"
    echo ""

    timeout "$TIMEOUT" bash -c "
        aseqdump -p '$BENTO_MIDI_OUT_PORT' > '$DUMP_FILE' &
        dump_pid=\$!
        sleep 0.5
        aplaymidi -p '$BENTO_MIDI_IN_PORT' '$MID_FILE'
        sleep '$CAPTURE_SEC'
        kill \$dump_pid 2>/dev/null || true
        wait \$dump_pid 2>/dev/null || true
    "

    if grep -qE 'note on|Note on|NoteOn' "$DUMP_FILE" 2>/dev/null; then
        echo ""
        echo "[PASS] Note events received on TTY MIDI out (loopback round-trip)."
        grep -E 'note on|Note on|NoteOn' "$DUMP_FILE" | head -5
        exit 0
    fi

    echo ""
    echo "[FAIL] No note events on TTY MIDI out."
    echo "       Check TX/RX loopback wiring and that bento_ttymidi is running."
    echo "--- aseqdump output (last 20 lines) ---"
    tail -20 "$DUMP_FILE" 2>/dev/null || true
    exit 1
fi

echo "Passive capture (connect a MIDI source to the HAT RX, or set BENTO_MIDI_LOOPBACK=1)."
echo "Listening on TTY MIDI out for ${CAPTURE_SEC}s ..."
echo ""

timeout "$((CAPTURE_SEC + 2))" aseqdump -p "$BENTO_MIDI_OUT_PORT" >"$DUMP_FILE" &
dump_pid=$!
sleep "$CAPTURE_SEC"
kill "$dump_pid" 2>/dev/null || true
wait "$dump_pid" 2>/dev/null || true

if grep -qE 'note on|Note on|NoteOn|control change|Control change' "$DUMP_FILE" 2>/dev/null; then
    echo "[PASS] MIDI events received on TTY MIDI out."
    grep -E 'note on|Note on|NoteOn|control change|Control change' "$DUMP_FILE" | head -5
    exit 0
fi

echo "[SKIP] No MIDI events captured in ${CAPTURE_SEC}s."
echo "       This is expected without an external controller or TX/RX loopback."
echo ""
echo "       For automated round-trip: BENTO_MIDI_LOOPBACK=1 $0"
echo "       Or connect a keyboard/controller to the HAT and re-run."
exit 2
