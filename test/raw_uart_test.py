#!/usr/bin/env python3
"""
Minimal UART MIDI test without bento_ttymidi (direct /dev/ttyAMA0).

Usage:
  ./raw_uart_test.py listen
  ./raw_uart_test.py send 90 3c 40
  ./raw_uart_test.py --legacy-baud listen
  ./raw_uart_test.py --legacy-baud send 90 3c 40 80 3c 00

Options:
  --legacy-baud   B38400 + TIOCGSERIAL custom divisor (original bento_ttymidi)
  (default)       termios2 / BOTHER @ 31250

Requires: Python 3, group dialout for /dev/ttyAMA0.
"""

from __future__ import annotations

import argparse
import ctypes
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
TIOCGSERIAL = 0x8020541E
TIOCSSERIAL = 0x4020541F
ASYNC_SPD_MASK = 0x1030
ASYNC_SPD_CUST = 0x0030
B38400 = getattr(termios, "B38400", 0x00001002)
CBAUD_MASK = getattr(termios, "CBAUD", 0o010017)

TERMIOS2_STRUCT = struct.Struct("IIII B 19s II")


class SerialStruct(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int),
        ("line", ctypes.c_int),
        ("port", ctypes.c_uint),
        ("irq", ctypes.c_int),
        ("flags", ctypes.c_int),
        ("xmit_fifo_size", ctypes.c_int),
        ("custom_divisor", ctypes.c_int),
        ("baud_base", ctypes.c_int),
        ("close_delay", ctypes.c_ushort),
        ("io_type", ctypes.c_byte),
        ("reserved_char", ctypes.c_byte),
        ("hub6", ctypes.c_int),
        ("closing_wait", ctypes.c_ushort),
        ("closing_wait2", ctypes.c_ushort),
        ("iomem_base", ctypes.c_void_p),
        ("iomem_reg_shift", ctypes.c_ushort),
        ("port_high", ctypes.c_uint),
        ("iomap_base", ctypes.c_ulong),
    ]


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


def make_raw_tty(tty: list) -> None:
    """Python termios has no cfmakeraw(); apply equivalent flags."""
    tty[0] &= ~(
        termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP
        | termios.INLCR | termios.IGNCR | termios.ICRNL | termios.IXON
    )
    tty[1] &= ~termios.OPOST
    tty[2] &= ~(
        termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN
    )
    tty[3] &= ~(termios.CSIZE | termios.PARENB)
    tty[3] |= termios.CS8
    if hasattr(termios, "CRTSCTS"):
        tty[3] &= ~termios.CRTSCTS


def set_tty_speed_b38400(tty: list) -> None:
    """Set B38400 without cfsetispeed/cfsetospeed (not exposed in Python termios)."""
    if len(tty) >= 6:
        tty[4] = B38400
        tty[5] = B38400
    tty[3] &= ~CBAUD_MASK
    tty[3] |= B38400


def legacy_set_baud(fd: int, baud: int) -> None:
    """Match original bento_ttymidi: B38400 placeholder + custom divisor."""
    tty = termios.tcgetattr(fd)
    make_raw_tty(tty)
    set_tty_speed_b38400(tty)
    tty[3] |= termios.CLOCAL | termios.CREAD
    if hasattr(termios, "CRTSCTS"):
        tty[3] &= ~termios.CRTSCTS
    termios.tcsetattr(fd, termios.TCSANOW, tty)

    ser = SerialStruct()
    fcntl.ioctl(fd, TIOCGSERIAL, ser)
    ser.custom_divisor = ser.baud_base // baud
    if ser.custom_divisor == 0:
        raise OSError(f"invalid custom_divisor for baud_base={ser.baud_base}")
    ser.flags = (ser.flags & ~ASYNC_SPD_MASK) | ASYNC_SPD_CUST
    fcntl.ioctl(fd, TIOCSSERIAL, ser)


def open_uart(legacy: bool) -> int:
    fd = os.open(DEVICE, os.O_RDWR | os.O_NOCTTY)
    if legacy:
        legacy_set_baud(fd, BAUD)
    else:
        termios2_set_baud(fd, BAUD)
    return fd


def baud_mode_label(legacy: bool) -> str:
    if legacy:
        return f"legacy B38400+TIOCGSERIAL @ {BAUD}"
    return f"termios2/BOTHER @ {BAUD}"


def cmd_listen(legacy: bool) -> int:
    fd = open_uart(legacy)
    print(f"Listening on {DEVICE} ({baud_mode_label(legacy)}), Ctrl+C to stop")
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


def cmd_send(legacy: bool, hex_args: list[str]) -> int:
    if not hex_args:
        print("Usage: send 90 3c 40 ...", file=sys.stderr)
        return 1
    data = bytes(int(tok, 16) & 0xFF for tok in hex_args)
    fd = open_uart(legacy)
    os.write(fd, data)
    os.close(fd)
    print(f"Sent ({baud_mode_label(legacy)}): {' '.join(f'{b:02X}' for b in data)}")
    return 0


def cmd_roundtrip(legacy: bool, hex_args: list[str]) -> int:
    if not hex_args:
        print("Usage: roundtrip 90 3c 40 ...", file=sys.stderr)
        return 1
    data = bytes(int(tok, 16) & 0xFF for tok in hex_args)
    fd = open_uart(legacy)
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
    parser = argparse.ArgumentParser(description="Raw UART MIDI test on /dev/ttyAMA0")
    parser.add_argument(
        "--legacy-baud",
        action="store_true",
        help="Use B38400 + TIOCGSERIAL (original bento_ttymidi method)",
    )
    parser.add_argument(
        "command",
        choices=("listen", "send", "roundtrip"),
        help="listen, send, or roundtrip",
    )
    parser.add_argument("hex_bytes", nargs="*", help="hex bytes for send/roundtrip")
    args = parser.parse_args()

    if args.command == "listen":
        return cmd_listen(args.legacy_baud)
    if args.command == "send":
        return cmd_send(args.legacy_baud, args.hex_bytes)
    return cmd_roundtrip(args.legacy_baud, args.hex_bytes)


if __name__ == "__main__":
    raise SystemExit(main())
