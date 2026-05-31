#!/bin/bash
# Generic UART MIDI test suite — no bento_ttymidi, no ALSA.
#
# Usage:
#   ./uart_midi.sh
#   UART_LOOPBACK=1 ./uart_midi.sh          # OUT→IN wired on the HAT
#   UART_DEVICE=/dev/ttyAMA0 ./uart_midi.sh
#
# Exit: 0 if no failures (skips are OK), 1 if any test failed.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAW="$SCRIPT_DIR/raw_uart_test.py"

DEVICE="${UART_DEVICE:-${BENTO_UART_DEVICE:-/dev/ttyAMA0}}"
BAUD="${UART_BAUD:-${BENTO_UART_BAUD:-31250}}"
CAPTURE_SEC="${UART_CAPTURE_SEC:-5}"
LOOPBACK="${UART_LOOPBACK:-0}"
STOP_BRIDGE="${UART_STOP_BRIDGE:-1}"
ROUNDTRIP_TIMEOUT="${UART_ROUNDTRIP_TIMEOUT:-2}"
LEGACY="${UART_LEGACY_BAUD:-0}"
OVERLAY_BAUD="${UART_OVERLAY_BAUD:-1}"

# Note on / Note off C4
TEST_PATTERN=(90 3c 40 80 3c 00)

PASS=0
FAIL=0
SKIP=0

raw_uart() {
    local extra=()
    if [ "$LEGACY" = "1" ]; then
        extra+=(--legacy-baud)
    elif [ "$OVERLAY_BAUD" = "1" ]; then
        extra+=(--overlay-baud)
    else
        extra+=(--no-overlay-baud --exact-baud)
    fi
    python3 "$RAW" --device "$DEVICE" "${extra[@]}" "$@"
}

has_valid_midi_bytes() {
    # Channel voice status bytes are 0x80-0xEF
    grep -qE '(^| )[89ABCDEF][0-9A-F]( |$)' <<<"$1"
}

has_any_hex_bytes() {
    grep -qE '[0-9A-F]{2} [0-9A-F]{2}' <<<"$1"
}

record() {
    local rc=$1
    case "$rc" in
        0) PASS=$((PASS + 1)) ;;
        2) SKIP=$((SKIP + 1)) ;;
        *) FAIL=$((FAIL + 1)) ;;
    esac
}

echo "UART MIDI test suite (no ALSA / no bento_ttymidi)"
echo "Date  : $(date -Iseconds 2>/dev/null || date)"
echo "Device: $DEVICE"
if [ "$OVERLAY_BAUD" = "1" ] && [ "$LEGACY" != "1" ]; then
    echo "Baud  : overlay B38400 (~${BAUD} on wire via midi-uart0-pi5)"
else
    echo "Baud  : ${BAUD} (exact/legacy mode)"
fi
echo ""

if [ ! -e "$DEVICE" ]; then
    echo "[FAIL] Serial device not found: $DEVICE"
    exit 1
fi

if [ ! -r "$DEVICE" ] || [ ! -w "$DEVICE" ]; then
    echo "[FAIL] No read/write access to $DEVICE (add user to dialout?)"
    exit 1
fi

if [ "$STOP_BRIDGE" = "1" ]; then
    if pgrep -x bento_ttymidi >/dev/null 2>&1; then
        echo "Stopping bento_ttymidi so UART is free..."
        sudo systemctl stop bento_ttymidi 2>/dev/null || true
        pkill -x bento_ttymidi 2>/dev/null || true
        sleep 0.3
    fi
fi

echo "########################################"
echo "# UART MIDI OUT (TX)"
echo "########################################"
set +e
if raw_uart send "${TEST_PATTERN[@]}"; then
    echo "[PASS] OUT: ${#TEST_PATTERN[@]} bytes written to $DEVICE"
    echo "       Confirm on external MIDI monitor if OUT is wired."
    record 0
else
    echo "[FAIL] OUT: write to $DEVICE failed"
    record 1
fi
set -e

echo ""
echo "########################################"
echo "# UART MIDI IN (RX)"
echo "########################################"
if [ "$LOOPBACK" = "1" ]; then
    echo "[SKIP] IN: passive capture skipped (UART_LOOPBACK=1 uses roundtrip)"
    record 2
else
    echo "Listening ${CAPTURE_SEC}s — send MIDI into HAT IN from a controller/Mac."
    set +e
    in_output="$(raw_uart listen --timeout "$CAPTURE_SEC" 2>&1)"
    in_rc=$?
    set -e
    echo "$in_output"
    if [ "$in_rc" -eq 2 ]; then
        echo "[SKIP] IN: no bytes in ${CAPTURE_SEC}s (no external source?)"
        record 2
    elif [ "$in_rc" -ne 0 ]; then
        echo "[FAIL] IN: listen failed (rc=$in_rc)"
        record 1
    elif has_valid_midi_bytes "$in_output"; then
        echo "[PASS] IN: valid MIDI status bytes received"
        record 0
    elif has_any_hex_bytes "$in_output"; then
        echo "[FAIL] IN: bytes received but not valid MIDI (likely baud mismatch)"
        echo "       Expected e.g. 90 2F 40 — got garbage like 48 05 FF?"
        echo "       Use overlay mode (default) or check TRS Type-A / IN vs OUT."
        record 1
    else
        echo "[SKIP] IN: no payload bytes captured"
        record 2
    fi
fi

echo ""
echo "########################################"
echo "# UART loopback (OUT → IN)"
echo "########################################"
if [ "$LOOPBACK" != "1" ]; then
    echo "[SKIP] LOOPBACK: set UART_LOOPBACK=1 and wire HAT OUT to IN"
    record 2
else
    set +e
    raw_uart roundtrip --timeout "$ROUNDTRIP_TIMEOUT" "${TEST_PATTERN[@]}"
    lb_rc=$?
    set -e
    if [ "$lb_rc" -eq 0 ]; then
        echo "[PASS] LOOPBACK: RX matches TX"
        record 0
    else
        echo "[FAIL] LOOPBACK: mismatch or no echo (check OUT→IN wiring)"
        record 1
    fi
fi

echo ""
echo "========================================"
echo "Summary: $PASS passed, $FAIL failed, $SKIP skipped"
echo "========================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
