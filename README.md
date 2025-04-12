
# Bento TTYMIDI

`bento_ttymidi` is a lightweight MIDI bridge that forwards ALSA MIDI messages to UART (e.g. for standard 5-pin DIN MIDI hardware). It is designed for Raspberry Pi systems using GPIO14 as TX (MIDI Out).

---

## ✅ Features

- ALSA MIDI input to UART MIDI output (31250 baud)
- Supports Note On, Note Off, Control Change, Program Change, Pitch Bend, SysEx
- Optional debug output (`--debug`)
- Can run as a `systemd` service on boot

---

## 🚀 Installation

### 1. Clone the repository

```bash
git clone https://github.com/yourname/bento_ttymidi.git
cd bento_ttymidi
```

### 2. Build the binary

```bash
gcc bento_ttymidi.c -o bento_ttymidi -lasound -lpthread
```

### 3. Install the binary system-wide

```bash
sudo cp bento_ttymidi /usr/local/bin/
sudo chmod +x /usr/local/bin/bento_ttymidi
```

---

## 🧪 Usage

### Manual start with debug output:

```bash
bento_ttymidi --debug
```

### Manual start silently:

```bash
bento_ttymidi
```

---

## 🔁 Autostart on Boot (Systemd)

To run `bento_ttymidi` as a background service at boot:

### 1. Create a systemd service file

```bash
sudo nano /etc/systemd/system/bento_ttymidi.service
```

Paste the following:

```ini
[Unit]
Description=Bento UART MIDI bridge (bento_ttymidi)
After=sound.target

[Service]
ExecStart=/usr/local/bin/bento_ttymidi
Restart=always
User=pi
Group=pi

[Install]
WantedBy=multi-user.target
```

> 💡 If you want to enable debug mode, add `--debug` to `ExecStart`

### 2. Enable and start the service

```bash
sudo systemctl daemon-reexec
sudo systemctl daemon-reload
sudo systemctl enable bento_ttymidi.service
sudo systemctl start bento_ttymidi.service
```

### 3. Check service status

```bash
systemctl status bento_ttymidi.service
```

---

## 📄 License

MIT (or define your own)

---

## ✉️ Contact

Created by [Your Name] – for use with Raspberry Pi MIDI hardware systems.


---

## ⚙️ Raspberry Pi UART Configuration (`config.txt`)

To enable UART MIDI on Raspberry Pi GPIO14 (TX), edit the config.txt:

```bash
sudo nano /boot/config.txt
```

### Add or ensure the following lines are present:

```ini
enable_uart=1
dtoverlay=disable-bt
dtoverlay=midi-uart0
```

> These settings enable UART0 on GPIO14 (TX) and GPIO15 (RX),
> disable Bluetooth (which uses UART by default),
> and apply the `midi-uart0` overlay for compatibility.

After editing, reboot your system:

```bash
sudo reboot
```

You should now see `/dev/serial0` → usually linked to `/dev/ttyAMA0`

---

## 🧰 Troubleshooting

- Check UART is accessible: `ls -l /dev/serial0`
- Confirm MIDI messages: `aconnect -l`, `aplaymidi`, or `aseqdump`
- Watch logs: `journalctl -u bento_ttymidi -f`
