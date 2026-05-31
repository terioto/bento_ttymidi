#!/bin/bash
# Stop bento_ttymidi and related MIDI listeners for isolated tests.

set -euo pipefail

echo "Stopping bento_ttymidi..."
sudo systemctl stop bento_ttymidi 2>/dev/null || true
pkill -x bento_ttymidi 2>/dev/null || true
pkill -x aseqdump 2>/dev/null || true

echo ""
echo "Remaining MIDI-related processes:"
pgrep -a bento_ttymidi || echo "  (no bento_ttymidi)"
pgrep -a aseqdump || echo "  (no aseqdump)"

echo ""
echo "ALSA clients:"
aconnect -l 2>/dev/null | grep -E "client|bento|BentoIO|Midi Through" || true

echo ""
echo "Raw MIDI / sequencer ports:"
aplaymidi -l 2>/dev/null || true

echo ""
echo "Done. UART (/dev/ttyAMA0) is NOT an ALSA port without a bridge."
echo "Use test/raw_uart_test.py for hardware tests without bento_ttymidi."
