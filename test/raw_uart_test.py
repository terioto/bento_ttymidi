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

BOTHER = 0x1000
TCGETS2 = 0x802C542A
TCSETS2 = 0x402C542B
CBAUD = 0x100F
CS8 = 0x30
CLOCAL = 0x800
CREAD = 0x200
CRTSCTS = 0x80000000


TERMIOS2_STRUCT = struct.Struct("IIII B 19s II")


def termios2_set_baud(fd: int, baud: int) -> None:
    buf = bytearray(TERMIOS2_STRUCT.size)
    fcntl.ioctl(fd, TCGETS2, buf)
    iflag, oflag, cflag, lflag, c_line, c_cc, ispeed, ospeed = TERMIOS2_STRUCT.unpack(buf)

    iflag &= ~(
        termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP
        | termios.INLCR | termios.IGNCR | termios.ICRNL | termios.IXON
    )
    oflag &= ~termios.OPOST
    lflag &= ~(
        termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN
    )
    cflag &= ~(termios.CSIZE | termios.PARENB | CRTSCTS)
    cflag |= CS8 | CLOCAL | CREAD
    cflag &= ~CBAUD
    cflag |= BOTHER
    ispeed = baud
    ospeed = baud

    TERMIOS2_STRUCT.pack_into(
        buf, 0, iflag, oflag, cflag, lflag, c_line, c_cc, ispeed, ospeed
    )
    fcntl.ioctl(fd, TCSETS2, buf)


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
