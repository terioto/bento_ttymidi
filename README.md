
# BentoIO TTYMIDI

bento_ttymidi is a lightweight MIDI bridge that connects ALSA MIDI with UART MIDI hardware. It supports both sending (TX via GPIO14) and receiving (RX via GPIO15) standard 5-pin DIN MIDI messages. Designed for Raspberry Pi systems.

---

## Features

- ALSA MIDI input to UART MIDI output (31250 baud)
- Supports Note On, Note Off, Control Change, Program Change, Pitch Bend, SysEx
- Optional debug output (`--debug`)
- Can run as a `systemd` service on boot

---

## Preperation

### 1. Update the system os

```bash
sudo apt update
sudo apt full-upgrade

sudo reboot
```

### 2. Update eeprom

```bash
sudo rpi-eeprom-update
sudo rpi-eeprom-update -a -d

sudo reboot
```


## Installation

### 1. Clone the repository

```bash
git clone https://github.com/terioto/bento_ttymidi.git
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

## Usage

## Raspberry Pi UART Configuration (`config.txt`)

To enable UART MIDI on Raspberry Pi GPIO14 (TX) / GPIO15 (RX), edit the config.txt:

```bash
sudo nano /boot/firmware/config.txt
```

### Add or ensure the following lines are present:

Raspberry Pi 4

```ini
enable_uart=1
dtoverlay=disable-bt
dtoverlay=midi-uart0
```

Raspberry Pi 5

```ini
enable_uart=1
dtoverlay=disable-bt
dtoverlay=midi-uart0-pi5
```

After editing, reboot your system:

```bash
sudo reboot
```

You should now see `/dev/serial0` → usually linked to `/dev/ttyAMA0`

---

### Manual start with debug output:

```bash
bento_ttymidi --debug
```

### Manual start silently:

```bash
bento_ttymidi -s /dev/ttyAMA0 -v &
```

### Check ALSA configuration (client IDs can be different):

```bash
aconnect -l

client 0: 'System' [type=kernel]
    0 'Timer           '
    1 'Announce        '
client 14: 'Midi Through' [type=kernel]
    0 'Midi Through Port-0'
client 128: 'bento_ttymidi' [type=user,pid=2041]
    0 'MIDI out        '
    1 'MIDI in         '
```

### Config the MIDI connection (Take look of the client IDs of your system):

```bash
aconnect 14:0 128:1
aconnect 128:0 14:0
```

### Test MIDI out:

```bash
aplaymidi -p 128:1 your_test_midi_file.mid
```

### Test MIDI in (You should see incomming MIDI data):

```bash
aseqdump -p 128:0
```


---

## Autostart on Boot (Systemd)

To run `bento_ttymidi` as a background service at boot:

### 1. Create a systemd service file

```bash
sudo nano /etc/systemd/system/bento_ttymidi.service
```

Paste the following:

```ini
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
```

> If you want to enable debug mode, add `--debug` to `ExecStart`

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



## License

Based on ttymidi by Jonathan Lissajoux (https://github.com/lathoub/ttymidi), licensed under the MIT License.
The original repository is no longer available, but various forks and archives remain accessible.

---

## Contact

Website: [https://www.terioto.com](https://www.terioto.com)  
Email: [info@terioto.com](mailto:info@terioto.com)
