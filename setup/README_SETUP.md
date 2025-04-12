
# setup_bento_ttymidi.sh

This script automates the installation and configuration of `bento_ttymidi`, a UART MIDI bridge for Raspberry Pi. It compiles the binary, installs it, creates a systemd service, sets up `/dev/serial0` if needed, and reminds you to configure UART in `/boot/config.txt`.

---

## 📦 What It Does

- Compiles `bento_ttymidi.c` using gcc
- Installs the binary to `/usr/local/bin/`
- Creates a `systemd` service to auto-start at boot
- Adds a persistent udev rule for `/dev/serial0`
- Reminds you to configure UART in `config.txt`

---

## 🚀 Usage

### 1. Place this script and `bento_ttymidi.c` in the same folder.

### 2. Run it:

```bash
chmod +x setup_bento_ttymidi.sh
./setup_bento_ttymidi.sh
```

---

## ⚙️ Manual Config Required

Ensure the following lines are present in your `/boot/config.txt`:

```ini
enable_uart=1
dtoverlay=disable-bt
dtoverlay=midi-uart0
```

Edit with:

```bash
sudo nano /boot/config.txt
```

Then reboot:

```bash
sudo reboot
```

---

## 📂 Files Created

- `/usr/local/bin/bento_ttymidi`
- `/etc/systemd/system/bento_ttymidi.service`
- `/etc/udev/rules.d/99-serial0.rules`

---

## ✅ After Reboot

You can check everything using:

```bash
ls -l /dev/serial0
aconnect -l
systemctl status bento_ttymidi
```

---

## 🛠 Uninstall

```bash
sudo systemctl stop bento_ttymidi
sudo systemctl disable bento_ttymidi
sudo rm /etc/systemd/system/bento_ttymidi.service
sudo rm /usr/local/bin/bento_ttymidi
sudo rm /etc/udev/rules.d/99-serial0.rules
```

Then reboot to finish cleanup.

---

