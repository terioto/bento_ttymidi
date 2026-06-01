#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "Installing bento_ttymidi for Pi 5 / CM5 (Trixie)..."

if [ ! -f dist/bento_ttymidi ] || [ ! -f dist/bento_ttymidi.service ]; then
    echo "dist/ not found — running pack first..."
    "$ROOT/setup/pack_bento_ttymidi.sh"
fi

echo "Installing from dist/..."
sudo install -m 755 dist/bento_ttymidi /usr/local/bin/bento_ttymidi
sudo install -m 644 dist/bento_ttymidi.service /etc/systemd/system/bento_ttymidi.service

echo "Enabling service..."
sudo systemctl daemon-reload
sudo systemctl enable bento_ttymidi.service

if [ -e /dev/ttyAMA0 ]; then
    if id pi >/dev/null 2>&1; then
        sudo usermod -aG dialout,audio pi 2>/dev/null || true
    fi
    sudo systemctl restart bento_ttymidi.service
    echo "Service started."
else
    echo "[WARN] /dev/ttyAMA0 not found — configure UART and reboot first."
    echo "       See README.md: midi-uart0-pi5 in /boot/firmware/config.txt"
fi

if systemctl is-active --quiet serial-getty@ttyAMA0.service 2>/dev/null; then
    echo "[WARN] serial-getty@ttyAMA0 is active — it blocks MIDI on ttyAMA0."
    echo "       sudo systemctl disable --now serial-getty@ttyAMA0.service"
fi

echo ""
echo "Setup complete."
echo "  systemctl status bento_ttymidi"
echo "  aconnect -l | grep bento_ttymidi"
