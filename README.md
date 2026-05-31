# Bento TTYMIDI

`bento_ttymidi` is an ALSA Sequencer ↔ UART MIDI bridge for **Raspberry Pi 5 / CM5** running **Debian Trixie**.

It connects software MIDI (ALSA) to a UART MIDI interface at **31250 baud** (default: `/dev/ttyAMA0` on GPIO14/15).

---

## Features

- Bidirectional MIDI: ALSA ↔ UART (`/dev/ttyAMA0`)
- Channel messages, SysEx, Running Status, MIDI Real-Time (Clock, Transport)
- ALSA ports: `bento_ttymidi:MIDI in` (to hardware) and `bento_ttymidi:MIDI out` (from hardware)
- Loopback protection: hardware RX is not echoed back to UART TX
- Optional `--debug` hex logging
- systemd service for boot (see [setup/README.md](setup/README.md))

---

## Platform Requirements

| Item | Value |
|------|--------|
| Hardware | Raspberry Pi 5 or CM5 |
| OS | Debian Trixie (Pi image) |
| Serial device | **`/dev/ttyAMA0`** (UART0 on GPIO14/15) |
| Boot config | `/boot/firmware/config.txt` |

**Important:** On Pi 5, `/dev/serial0` points to the **debug UART** (`ttyAMA10`), not Pin 8/10. Do not use it for MIDI.

### UART boot configuration

Add to `/boot/firmware/config.txt`:

```ini
enable_uart=1
dtoverlay=disable-bt
dtoverlay=midi-uart0-pi5
```

The `midi-uart0-pi5` overlay maps UART0 to GPIO14/15 **and** sets the PL011 clock for MIDI (31250 baud).

Ensure `/boot/firmware/cmdline.txt` does **not** attach a console to `ttyAMA0` (Pi 5 console on `serial0` / debug UART is usually fine).

If MIDI still fails, check that no getty uses the port:

```bash
systemctl status serial-getty@ttyAMA0.service
sudo systemctl disable --now serial-getty@ttyAMA0.service
```

Reboot, then verify:

```bash
ls -l /dev/ttyAMA0
pinctrl funcs 14-15
```

Expected: GPIO14 = UART0 TXD, GPIO15 = UART0 RXD.

---

## Build

Dependencies (on the Pi):

```bash
sudo apt update
sudo apt install -y build-essential libasound2-dev
```

Build and install:

```bash
make
sudo make install
```

Or use the automated installer: [setup/README.md](setup/README.md)

---

## Usage

```bash
# Default: /dev/ttyAMA0 @ 31250 baud
bento_ttymidi

# Custom device / debug
bento_ttymidi --device /dev/ttyAMA0 --debug

# Send Note Off as 0x80 instead of Note On velocity 0
bento_ttymidi --note-off-0x80

bento_ttymidi --help
```

---

## ALSA Port Contract

| Direction | ALSA client | Port name | Purpose |
|-----------|-------------|-----------|---------|
| Hardware → software | `bento_ttymidi` | `MIDI out` | Subscribe/read (UART RX) |
| Software → hardware | `bento_ttymidi` | `MIDI in` | Connect/write (UART TX) |

List ports:

```bash
aconnect -l
```

Example connections (replace `CLIENT:PORT` with your app):

```bash
aconnect 'CLIENT:PORT' 'bento_ttymidi:MIDI in'
aconnect 'bento_ttymidi:MIDI out' 'CLIENT:PORT'
```

Monitor hardware input:

```bash
aseqdump -p 'bento_ttymidi:MIDI out'
```

Automated tests (Pi, `alsa-utils` installed, service running):

```bash
cd test && ./run_tests.sh
```

See [test/README.md](test/README.md) for MIDI IN/OUT scripts, loopback mode, and the copyright-free fixture `fixtures/bento_test.mid`.

---

## Manual Test Matrix

| Test | Command / action | Expected |
|------|------------------|----------|
| UART active | `pinctrl funcs 14-15` | UART0 on GPIO14/15 |
| Service | `systemctl status bento_ttymidi` | active (running) |
| TX | `aconnect` → `MIDI in`, send notes | Output on UART TX |
| RX | Source on UART RX, `aseqdump -p 'bento_ttymidi:MIDI out'` | Note/CC events |
| Program Change | Send PC from controller | No stream desync |
| SysEx | Short SysEx dump | Visible in `aseqdump` or round-trip |
| MIDI Clock | Sequencer clock → `MIDI in` | 0xF8 on wire (with `--debug`) |
| Restart | `systemctl restart bento_ttymidi` | Clean restart |

---

## Troubleshooting

| Problem | Check |
|---------|--------|
| `open serial device` fails | `ls -l /dev/ttyAMA0`, overlays in config.txt, reboot |
| Wrong UART / no MIDI | Use **`/dev/ttyAMA0`**, not `/dev/serial0` on Pi 5 |
| No RX/TX at all | `dtoverlay=midi-uart0-pi5`, disable `serial-getty@ttyAMA0` |
| dmesg: custom speed deprecated | Expected on old builds; current code uses termios2/BOTHER |
| No ALSA output from source | `aconnect` `MIDI out` to your app |
| No output on UART TX | `aconnect` your app to `bento_ttymidi:MIDI in` |
| Manual run: permission denied | `sudo usermod -aG dialout pi` (then re-login) |
| Baud rate / framing errors | Kernel ≥ 6.12.32 recommended; verify with `--debug` |
| Permission denied (service) | Service runs as `pi`; user in `audio` group |

---

## License

MIT
