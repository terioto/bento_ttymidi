#!/bin/bash
# RX path diagnosis: HAT loopback first, then Mac→IN hints.
#
# Usage:
#   ./diagnose_uart_rx.sh              # checks only
#   ./diagnose_uart_rx.sh loopback     # wire HAT OUT → HAT IN first

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAW="$SCRIPT_DIR/raw_uart_test.py"
DEVICE="${UART_DEVICE:-/dev/ttyAMA0}"

echo "=== UART RX diagnosis ==="
echo "Device: $DEVICE"
echo ""

fail() {
    echo "[FAIL] $*"
    exit 1
}

warn() {
    echo "[WARN] $*"
}

ok() {
    echo "[OK]   $*"
}

[ -e "$DEVICE" ] || fail "Device missing: $DEVICE"
[ -r "$DEVICE" ] && [ -w "$DEVICE" ] || fail "No read/write on $DEVICE (group dialout?)"

if pgrep -x bento_ttymidi >/dev/null 2>&1; then
    warn "bento_ttymidi is running — run ./stop_bento_midi.sh first"
else
    ok "bento_ttymidi not running"
fi

if sudo lsof "$DEVICE" 2>/dev/null | grep -v '^COMMAND' | grep -q .; then
    warn "Another process has $DEVICE open:"
    sudo lsof "$DEVICE" 2>/dev/null | grep -v '^COMMAND' || true
else
    ok "$DEVICE is not open"
fi

if grep -q 'midi-uart0' /boot/firmware/config.txt 2>/dev/null; then
    ok "midi-uart overlay in config.txt"
else
    warn "No midi-uart overlay in /boot/firmware/config.txt"
fi

if pinctrl funcs 15 2>/dev/null | grep -q RXD0; then
    ok "GPIO15 = UART0 RX (RXD0)"
else
    warn "GPIO15 is not RXD0 — check overlay / reboot"
fi

if pinctrl funcs 14 2>/dev/null | grep -q TXD0; then
    ok "GPIO14 = UART0 TX (TXD0)"
else
    warn "GPIO14 is not TXD0"
fi

echo ""
echo "--- Interpretation ---"
echo "Pi → Mac (HAT OUT) already works  →  TX + baud + overlay are OK."
echo "Mac → Pi (HAT IN) shows nothing   →  signal never reaches GPIO15 RX."
echo ""
echo "Most common cause: cable on HAT OUT only, not on HAT IN."
echo "  Mac adapter OUT  ──►  Pi HAT IN  (separate jack from OUT)"
echo ""

if [ "${1:-}" != "loopback" ]; then
    echo "Next: connect a short cable HAT OUT → HAT IN, then run:"
    echo "  $0 loopback"
    exit 0
fi

echo "=== Loopback test (HAT OUT → HAT IN must be wired) ==="
echo ""

set +e
out=$(python3 "$RAW" --device "$DEVICE" --overlay-baud roundtrip --timeout 3 90 18 40 80 18 00 2>&1)
rc=$?
set -e
echo "$out"
echo ""

if [ "$rc" -eq 0 ]; then
    ok "Loopback PASS — Pi UART RX + MMX IN path work."
    echo ""
    echo "Software on the Pi is fine. Fix Mac side:"
    echo "  1. Mac adapter OUT → Pi HAT IN (Type-A)"
    echo "  2. While running: ./raw_uart_test.py listen --timeout 30"
    echo "  3. Send one note from Mac to adapter OUT port"
    exit 0
fi

echo "[FAIL] Loopback FAIL — no echo on RX."
echo ""
echo "Hardware checks:"
echo "  • Firm cable HAT OUT → HAT IN (same stack, MMX jacks)"
echo "  • MMX seated on AMX / J10 UART link"
echo "  • Schematics: pcb/SCH_MMX1.pdf"
echo ""
echo "If loopback fails, Mac→Pi will never work until HAT IN is fixed."
exit 1
