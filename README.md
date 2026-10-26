
# BentoIO TTYMIDI

bento_ttymidi is a lightweight MIDI bridge that connects ALSA MIDI with UART MIDI hardware such as the BentoIO AMH2. It supports both sending (TX via GPIO14) and receiving (RX via GPIO15) standard 5-pin DIN MIDI messages. Designed for the Raspberry Pi 4 and Raspberry Pi 5.

It creates the ALSA client `bento_ttymidi` with two ports:

| Port | Name | Direction |
|---|---|---|
| `bento_ttymidi:0` | `MIDI out` | MIDI IN jack → ALSA, read from here |
| `bento_ttymidi:1` | `MIDI in` | ALSA → MIDI OUT jack, write to here |

---

## Features

- UART MIDI at 31250 baud in both directions
- All channel messages: Note On, Note Off, Polyphonic Pressure, Control Change, Program Change, Channel Pressure, Pitch Bend
- Running status on MIDI IN
- SysEx up to 4096 bytes from MIDI IN (one event from `F0` to `F7`), any length to MIDI OUT
- Real-time messages (Clock, Start, Continue, Stop, Active Sensing, Reset), also in the middle of another message
- System common messages (MTC Quarter Frame, Song Position, Song Select, Tune Request)
- 14-bit Control Change and NRPN from ALSA go out as Control Change messages
- No echo: what the bridge sends to ALSA never comes back out of MIDI OUT
- Optional debug output (`--debug`)
- Can run as a `systemd` service on boot

---

## Preparation

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

### 2. Install the build tools

```bash
sudo apt install build-essential libasound2-dev
```

### 3. Build the binary

```bash
make
```

The binary is `build/bento_ttymidi`.

### 4. Install the binary system-wide

```bash
sudo install -m 755 build/bento_ttymidi /usr/local/bin/
bento_ttymidi --version
```

### 5. Run the tests (optional)

Unit tests:

```bash
make test
```

End-to-end test without MIDI hardware. A pseudo terminal stands in for the UART, the real UART is not touched. It needs `alsa-utils` (`aseqdump`, `aplaymidi`):

```bash
sudo apt install alsa-utils
python3 -I tests/integration/pty_bridge.py
```

### Update from an older version

```bash
cd bento_ttymidi
git pull
make
sudo install -m 755 build/bento_ttymidi /usr/local/bin/
sudo systemctl restart bento_ttymidi.service    # if you use the service
```

Older versions were built with `gcc bento_ttymidi.c`. That file is gone, use `make`.

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

After the reboot `/dev/ttyAMA0` should exist (`ls -l /dev/ttyAMA0`). `bento_ttymidi` opens it by default.

> **Raspberry Pi 5:** `/dev/serial0` is the debug UART there (`ttyAMA10`), not GPIO14/15. Use `/dev/ttyAMA0`.

The overlay sets the UART clock so that 38400 baud in software are 31250 baud on the wire. Without the overlay MIDI does not work. Older versions of `bento_ttymidi` also set a custom divisor, which made the kernel log `bento_ttymidi sets custom speed on ttyAMA0. This is deprecated.` The current version does not do that anymore.

If a login console holds the port, disable it:

```bash
sudo systemctl disable --now serial-getty@ttyAMA0.service
```

Your user needs the group `dialout` for the serial port:

```bash
sudo usermod -aG dialout $USER
```

---

### Options

```
bento_ttymidi [OPTIONS]
```

| Option | Meaning |
|---|---|
| `-s`, `--device PATH` | Serial device (default: `/dev/ttyAMA0`) |
| `-v`, `--debug` | Print every MIDI message in hex |
| `--note-off-0x80` | Send Note On with velocity 0 as Note Off (`8n`) to MIDI OUT |
| `--exact-baud` | Set the baud rate with termios2 instead of the overlay. Only without the `midi-uart0` / `midi-uart0-pi5` overlay, with it the bytes come out garbled |
| `--baud RATE` | Baud rate for `--exact-baud` (default: 31250) |
| `--version` | Print the version |
| `-h`, `--help` | Show the help |

Unknown options are ignored with a warning, so older command lines keep working.

### Show the version:

```bash
bento_ttymidi --version
```

### Manual start with debug output:

```bash
bento_ttymidi --debug
```

### Manual start silently:

```bash
bento_ttymidi &
```

### Another serial device:

```bash
bento_ttymidi --device /dev/ttyAMA0
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

> `aconnect`, `aplaymidi` and `aseqdump` do not understand port names. They read `bento_ttymidi:MIDI in` as port 0, so `aplaymidi` fails with `Cannot connect to port 128:0 - Operation not permitted`. Use the port number with the client name or ID: `bento_ttymidi:0` (`MIDI out`, from the MIDI IN jack) and `bento_ttymidi:1` (`MIDI in`, to the MIDI OUT jack).

### Config the MIDI connection (Take look of the client IDs of your system):

```bash
aconnect 14:0 bento_ttymidi:1
aconnect bento_ttymidi:0 14:0
```

### Test MIDI out:

```bash
aplaymidi -p bento_ttymidi:1 your_test_midi_file.mid
```

### Test MIDI in (You should see incomming MIDI data):

```bash
aseqdump -p bento_ttymidi:0
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
After=sound.target dev-ttyAMA0.device
Requires=dev-ttyAMA0.device

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

## Changes

### 1.0.0 (2026-10-26)

- New core: the code is split into `src/` with a `Makefile`, unit tests and an end-to-end test on a pseudo terminal. Build with `make` instead of `gcc bento_ttymidi.c`.
- MIDI IN: running status works (the second note was lost), Program Change and Channel Pressure arrive, Active Sensing or Clock in the middle of a note no longer drops the note. Clock, Start, Stop and the other real-time messages, system common messages and SysEx up to 4096 bytes now reach ALSA.
- MIDI OUT: every message goes out with its status byte. Real-time and system common messages, 14-bit Control Change and NRPN go out too.
- No echo: events from the bridge itself are never written to MIDI OUT, also when `MIDI out` is connected to `MIDI in`.
- Fixed a crash (NULL pointer) when reading from ALSA failed.
- The bridge sleeps in `poll()`. A UART or ALSA error ends it with exit code 1, so systemd restarts it. SIGTERM and SIGINT end it with exit code 0.
- New options: `--note-off-0x80`, `--exact-baud`, `--baud`, `--version`.
- Unchanged: program, client and port names (`MIDI out` = port 0, `MIDI in` = port 1), `/dev/ttyAMA0` as default device without a custom divisor, `--device` / `-s`, `--debug` / `-v`, unknown options only give a warning.

---

## License

MIT, see [LICENSE](LICENSE).

The name follows ttymidi by Thiago Teixeira (GPL-3.0), a serial MIDI bridge for ALSA.
bento_ttymidi is a separate implementation and contains no code from it.

---

## Contact

Website: [https://www.terioto.com](https://www.terioto.com)  
Email: [info@terioto.com](mailto:info@terioto.com)
