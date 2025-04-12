#!/bin/bash

echo "📦 Installing bento_ttymidi..."

# 1. Build bento_ttymidi
echo "🔧 Compiling..."
gcc bento_ttymidi.c -o bento_ttymidi -lasound -lpthread

# 2. Install binary
echo "🚚 Installing binary to /usr/local/bin..."
sudo cp bento_ttymidi /usr/local/bin/
sudo chmod +x /usr/local/bin/bento_ttymidi

# 3. Create systemd service
echo "🛠 Creating systemd service..."
sudo tee /etc/systemd/system/bento_ttymidi.service > /dev/null <<EOL
[Unit]
Description=Bento UART MIDI bridge (bento_ttymidi)
After=sound.target dev-serial0.device
Requires=dev-serial0.device

[Service]
ExecStart=/usr/local/bin/bento_ttymidi
Restart=always
User=pi
Group=pi

[Install]
WantedBy=multi-user.target
EOL

# 4. Reload systemd and enable service
echo "🔄 Enabling service..."
sudo systemctl daemon-reexec
sudo systemctl daemon-reload
sudo systemctl enable bento_ttymidi.service
sudo systemctl start bento_ttymidi.service

# 5. Add udev rule for /dev/serial0 (if needed)
echo "🔗 Ensuring /dev/serial0 symlink exists..."
sudo tee /etc/udev/rules.d/99-serial0.rules > /dev/null <<EOL
KERNEL=="ttyAMA0", SYMLINK+="serial0"
EOL
sudo udevadm control --reload-rules
sudo udevadm trigger

# 6. UART config reminder
echo ""
echo "⚙️  Please ensure your /boot/config.txt includes the following:"
echo "--------------------------------------"
echo "enable_uart=1"
echo "dtoverlay=disable-bt"
echo "dtoverlay=midi-uart0"
echo "--------------------------------------"
echo "Edit with: sudo nano /boot/config.txt"
echo ""
echo "✅ Setup complete. Reboot to apply all changes."
