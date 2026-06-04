#!/bin/bash
# MIDI OUT test: play bento_test.mid into bento_ttymidi:TTY MIDI in via aplaymidi.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"

MID_FILE="${BENTO_TEST_MID:-$SCRIPT_DIR/fixtures/bento_test.mid}"
TIMEOUT="${BENTO_TEST_TIMEOUT:-15}"

require_cmd aconnect
require_cmd aplaymidi
require_bento_ttymidi

if [ ! -f "$MID_FILE" ]; then
    echo "[ERROR] MIDI file not found: $MID_FILE"
    exit 1
fi

echo "=== bento_ttymidi MIDI OUT test ==="
echo "Target port : $BENTO_MIDI_IN_PORT ($CLIENT:TTY MIDI in)"
echo "MIDI file   : $MID_FILE"
echo ""

if timeout "$TIMEOUT" aplaymidi -p "$BENTO_MIDI_IN_PORT" "$MID_FILE"; then
    echo ""
    echo "[PASS] aplaymidi completed successfully."
    echo "       UART TX was driven by bento_ttymidi (verify with scope or TX/RX loopback)."
    exit 0
fi

status=$?
echo ""
if [ "$status" -eq 124 ]; then
    echo "[FAIL] aplaymidi timed out after ${TIMEOUT}s."
else
    echo "[FAIL] aplaymidi exited with status $status."
fi
exit 1
