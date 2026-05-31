#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "Installing bento_ttymidi for Pi 5 / CM5 (Trixie)..."

if ! command -v make >/dev/null 2>&1; then
    echo "[ERROR] make not found. Install build-essential."
    exit 1
fi

if ! pkg-config --exists alsa 2>/dev/null; then
    echo "[WARN] libasound2-dev may be missing (pkg-config alsa not found)"
fi

echo "Building..."
make clean 2>/dev/null || true
make

echo "Installing binary to /usr/local/bin..."
sudo install -m 755 bento_ttymidi /usr/local/bin/bento_ttymidi

echo "Creating systemd service..."
sudo tee /etc/systemd/system/bento_ttymidi.service > /dev/null <<'EOL'
[Unit]
Description=Bento UART MIDI bridge (bento_ttymidi)
After=sound.target dev-ttyAMA0.device
Requires=dev-ttyAMA0.device

[Service]
Type=simple
ExecStart=/usr/local/bin/bento_ttymidi --device /dev/ttyAMA0
Restart=on-failure
RestartSec=2
KillSignal=SIGTERM
TimeoutStopSec=5
User=pi
Group=pi
SupplementaryGroups=audio dialout

[Install]
WantedBy=multi-user.target
EOL

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
