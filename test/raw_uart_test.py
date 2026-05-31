#!/usr/bin/env python3
"""
Minimal UART MIDI test without bento_ttymidi (not ALSA — direct /dev/ttyAMA0).

Usage:
  ./raw_uart_test.py listen              # hex dump RX (Ctrl+C to stop)
  ./raw_uart_test.py send 90 3c 40       # send hex bytes to TX
  ./raw_uart_test.py roundtrip 90 3c 40  # send then listen 2s

Requires: Python 3, group dialout for /dev/ttyAMA0.
"""

from __future__ import annotations

import fcntl
import os
import struct
import sys
import termios
import time

DEVICE = os.environ.get("BENTO_UART_DEVICE", "/dev/ttyAMA0")
BAUD = int(os.environ.get("BENTO_UART_BAUD", "31250"))

BOTHER = 0o010000
NCCS = 19
TCGETS2 = 0x802C542A
TCSETS2 = 0x402C542B
CBAUD = 0o0010017
CS8 = 0o0000060
CLOCAL = 0o0004000
CREAD = 0o0000200
CRTSCTS = 0o80000000


class Termios2(struct.Struct):
    _fields_ = [
        ("c_iflag", "I"),
        ("c_oflag", "I"),
        ("c_cflag", "I"),
        ("c_lflag", "I"),
        ("c_line", "B"),
        ("c_cc", f"{NCCS}s"),
        ("c_ispeed", "I"),
        ("c_ospeed", "I"),
    ]


def termios2_set_baud(fd: int, baud: int) -> None:
    buf = bytearray(Termios2.size)
    fcntl.ioctl(fd, TCGETS2, buf)
    tio = Termios2.from_buffer_copy(buf)

    tio.c_iflag &= ~(
        termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP
        | termios.INLCR | termios.IGNCR | termios.ICRNL | termios.IXON
    )
    tio.c_oflag &= ~termios.OPOST
    tio.c_lflag &= ~(
        termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN
    )
    tio.c_cflag &= ~(termios.CSIZE | termios.PARENB | CRTSCTS)
    tio.c_cflag |= CS8 | CLOCAL | CREAD
    tio.c_cflag &= ~CBAUD
    tio.c_cflag |= BOTHER
    tio.c_ispeed = baud
    tio.c_ospeed = baud

    fcntl.ioctl(fd, TCSETS2, bytes(tio))


def open_uart() -> int:
    fd = os.open(DEVICE, os.O_RDWR | os.O_NOCTTY)
    termios2_set_baud(fd, BAUD)
    return fd


def parse_hex(argv: list[str]) -> bytes:
    return bytes(int(tok, 16) & 0xFF for tok in argv)


def cmd_listen(_args: list[str]) -> int:
    fd = open_uart()
    print(f"Listening on {DEVICE} @ {BAUD} baud (Ctrl+C to stop)")
    try:
        while True:
            chunk = os.read(fd, 64)
            if chunk:
                print(" ".join(f"{b:02X}" for b in chunk), flush=True)
    except KeyboardInterrupt:
        print()
    finally:
        os.close(fd)
    return 0


def cmd_send(args: list[str]) -> int:
    if not args:
        print("Usage: send 90 3c 40 ...", file=sys.stderr)
        return 1
    data = parse_hex(args)
    fd = open_uart()
    os.write(fd, data)
    os.close(fd)
    print(f"Sent {len(data)} bytes: {' '.join(f'{b:02X}' for b in data)}")
    return 0


def cmd_roundtrip(args: list[str]) -> int:
    if not args:
        print("Usage: roundtrip 90 3c 40 ...", file=sys.stderr)
        return 1
    data = parse_hex(args)
    fd = open_uart()
    os.write(fd, data)
    print(f"Sent: {' '.join(f'{b:02X}' for b in data)}")
    print("Listening 2s for RX (needs OUT→IN loopback)...")
    deadline = time.time() + 2.0
    while time.time() < deadline:
        chunk = os.read(fd, 64)
        if chunk:
            print("RX:", " ".join(f"{b:02X}" for b in chunk))
    os.close(fd)
    return 0


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    cmd = sys.argv[1]
    args = sys.argv[2:]
    if cmd == "listen":
        return cmd_listen(args)
    if cmd == "send":
        return cmd_send(args)
    if cmd == "roundtrip":
        return cmd_roundtrip(args)
    print(f"Unknown command: {cmd}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
