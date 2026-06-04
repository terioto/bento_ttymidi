#!/bin/bash
# Run all bento_ttymidi MIDI tests.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PASS=0
FAIL=0
SKIP=0

run_one() {
    local name="$1"
    local script="$2"
    echo ""
    echo "########################################"
    echo "# $name"
    echo "########################################"
    if bash "$script"; then
        PASS=$((PASS + 1))
    else
        local rc=$?
        if [ "$rc" -eq 2 ]; then
            SKIP=$((SKIP + 1))
        else
            FAIL=$((FAIL + 1))
        fi
    fi
}

echo "bento_ttymidi automated test suite"
echo "Date: $(date -Iseconds 2>/dev/null || date)"

run_one "MIDI OUT (aplaymidi -> TTY MIDI in)" "$SCRIPT_DIR/test_midi_out.sh"
run_one "MIDI IN (TTY MIDI out capture)" "$SCRIPT_DIR/test_midi_in.sh"

echo ""
echo "========================================"
echo "Summary: $PASS passed, $FAIL failed, $SKIP skipped"
echo "========================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
