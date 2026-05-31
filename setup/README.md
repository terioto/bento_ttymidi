# setup_bento_ttymidi.sh

Automated install of `bento_ttymidi` on **Raspberry Pi 5 / CM5** with **Debian Trixie**.

For project overview and UART config see the main [README.md](../README.md).

---

## Prerequisites

```bash
sudo apt update
sudo apt install -y build-essential libasound2-dev
```

Ensure UART0 is enabled in `/boot/firmware/config.txt` (see [README.md](../README.md#uart-boot-configuration)):

```ini
enable_uart=1
dtoverlay=disable-bt
dtoverlay=midi-uart0-pi5
```

---

## Install

Run from the repository root:

```bash
chmod +x setup/setup_bento_ttymidi.sh
./setup/setup_bento_ttymidi.sh
```

The script will:

1. Build `bento_ttymidi` via `make`
2. Install to `/usr/local/bin/bento_ttymidi`
3. Create and enable `bento_ttymidi.service` (user `pi`)
4. Print a reminder if `/dev/ttyAMA0` is missing (reboot after config.txt changes)

---

## Files Created

| Path | Purpose |
|------|---------|
| `/usr/local/bin/bento_ttymidi` | Binary |
| `/etc/systemd/system/bento_ttymidi.service` | systemd unit |

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

## systemd Unit (reference)

The installer writes:

```ini
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
SupplementaryGroups=audio

[Install]
WantedBy=multi-user.target
```

Add `--debug` to `ExecStart` for verbose logging.
