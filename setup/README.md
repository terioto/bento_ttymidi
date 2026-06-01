# Setup scripts

Scripts for building, packaging, and installing `bento_ttymidi` on **Raspberry Pi 5 / CM5** with **Debian Trixie**.

For project overview and UART config see the main [README.md](../README.md).

| Script | Purpose |
|--------|---------|
| [`pack_bento_ttymidi.sh`](pack_bento_ttymidi.sh) | Build and stage binary + unit into [`dist/`](../dist/) |
| [`setup_bento_ttymidi.sh`](setup_bento_ttymidi.sh) | Install from `dist/` on a running Pi |

---

## Pack for integration (`dist/`)

Build installable artifacts (binary + systemd unit) for use by an external image or OS build — not for direct install on a running system:

```bash
chmod +x setup/pack_bento_ttymidi.sh
./setup/pack_bento_ttymidi.sh
# or: make dist
```

Output:

```text
dist/
├── bento_ttymidi
├── bento_ttymidi.service
├── README.md
└── VERSION              # git short hash, if available
```

Copy `dist/` into your integration file area (e.g. image overlay assets):

```bash
cp -a dist/* /path/to/your-build/files/bento_ttymidi/
```

See [`packaging/dist.README.md`](../packaging/dist.README.md) for rootfs install paths and image prerequisites (overlay, getty, groups).

**Cross-build (optional):** `CC=aarch64-linux-gnu-gcc make` before pack, or run pack on the Pi.

---

## Prerequisites (install on Pi)

```bash
sudo apt update
sudo apt install -y build-essential libasound2-dev alsa-utils
```

**User groups:** the service runs as user `pi` and needs serial + ALSA access:

```bash
sudo usermod -aG dialout,audio pi
# log out and back in (or reboot) after changing groups
```

**Getty:** disable a login shell on the MIDI UART (blocks `/dev/ttyAMA0`):

```bash
sudo systemctl disable --now serial-getty@ttyAMA0.service
```

Ensure UART0 is enabled in `/boot/firmware/config.txt` (see [README.md](../README.md#uart-boot-configuration)):

```ini
enable_uart=1
dtoverlay=disable-bt
dtoverlay=midi-uart0-pi5
```

---

## Install on a running Pi

Run from the repository root:

```bash
chmod +x setup/setup_bento_ttymidi.sh
./setup/setup_bento_ttymidi.sh
```

The script will:

1. Run [`pack_bento_ttymidi.sh`](pack_bento_ttymidi.sh) if `dist/` is missing
2. Install `dist/bento_ttymidi` and `dist/bento_ttymidi.service`
3. Enable and start the systemd unit (user `pi`, groups `audio` + `dialout`)
4. Warn if `/dev/ttyAMA0` is missing or serial-getty is active

**Custom install user:** edit [`packaging/bento_ttymidi.service`](../packaging/bento_ttymidi.service), then re-run pack and setup.

---

## Files Created (on Pi)

| Path | Source in repo |
|------|----------------|
| `/usr/local/bin/bento_ttymidi` | `dist/bento_ttymidi` |
| `/etc/systemd/system/bento_ttymidi.service` | `dist/bento_ttymidi.service` |

---

## Verify

```bash
ls -l /dev/ttyAMA0
systemctl status bento_ttymidi
aconnect -l | grep bento_ttymidi
```

---

## Uninstall

```bash
sudo systemctl stop bento_ttymidi
sudo systemctl disable bento_ttymidi
sudo rm /etc/systemd/system/bento_ttymidi.service
sudo rm /usr/local/bin/bento_ttymidi
sudo systemctl daemon-reload
```

Reboot optional.

---

## systemd unit (source of truth)

[`packaging/bento_ttymidi.service`](../packaging/bento_ttymidi.service)

Add `--debug` to `ExecStart` for verbose logging, then re-pack and re-install.
