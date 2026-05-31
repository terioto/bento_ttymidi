#!/usr/bin/env python3
"""
Minimal UART MIDI test without bento_ttymidi (direct /dev/ttyAMA0).

Usage:
  ./raw_uart_test.py listen
  ./raw_uart_test.py send 90 3c 40
  ./raw_uart_test.py --overlay-baud send 90 3c 40   # Pi + midi-uart0-pi5 (default)
  ./raw_uart_test.py --exact-baud send 90 3c 40     # termios2/BOTHER @ 31250

Baud modes (mutually exclusive):
  --overlay-baud  B38400 in termios; midi-uart overlay → 31250 on wire (Pi default)
  --exact-baud    termios2 / BOTHER @ UART_BAUD
  --legacy-baud   B38400 + TIOCGSERIAL custom divisor (Pi 4, often unsupported on Pi 5)

Environment:
  UART_OVERLAY_BAUD=1|0   default 1
  UART_DEVICE / BENTO_UART_DEVICE   serial device (default /dev/ttyAMA0)
  UART_BAUD   / BENTO_UART_BAUD     wire baud for --exact-baud (default 31250)

Requires: Python 3, group dialout for the serial device.
"""

from __future__ import annotations

import argparse
import ctypes
import errno
import fcntl
import os
import select
import struct
import sys
import termios
import time

DEVICE = os.environ.get("UART_DEVICE") or os.environ.get(
    "BENTO_UART_DEVICE", "/dev/ttyAMA0"
)
BAUD = int(os.environ.get("UART_BAUD") or os.environ.get("BENTO_UART_BAUD", "31250"))
DEFAULT_OVERLAY_BAUD = os.environ.get("UART_OVERLAY_BAUD", "1") != "0"
DEFAULT_ROUNDTRIP_TIMEOUT = 2.0

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


def legacy_set_baud(fd: int, baud: int) -> bool:
    """B38400 placeholder + TIOCGSERIAL divisor. Returns False if unsupported."""
    tty = termios.tcgetattr(fd)
    make_raw_tty(tty)
    set_tty_speed_b38400(tty)
    tty[3] |= termios.CLOCAL | termios.CREAD
    if hasattr(termios, "CRTSCTS"):
        tty[3] &= ~termios.CRTSCTS
    termios.tcsetattr(fd, termios.TCSANOW, tty)

    ser = SerialStruct()
    try:
        fcntl.ioctl(fd, TIOCGSERIAL, ser)
    except OSError as exc:
        if exc.errno in (errno.ENOTTY, errno.EINVAL, errno.EPERM):
            return False
        raise

    ser.custom_divisor = ser.baud_base // baud
    if ser.custom_divisor == 0:
        raise OSError(f"invalid custom_divisor for baud_base={ser.baud_base}")

    ser.flags = (ser.flags & ~ASYNC_SPD_MASK) | ASYNC_SPD_CUST
    try:
        fcntl.ioctl(fd, TIOCSSERIAL, ser)
    except OSError as exc:
        if exc.errno in (errno.ENOTTY, errno.EINVAL, errno.EPERM):
            return False
        raise
    return True


def overlay_set_baud(fd: int) -> None:
    """B38400 8N1 raw — midi-uart0-pi5 overlay maps this to 31250 on the wire."""
    tty = termios.tcgetattr(fd)
    make_raw_tty(tty)
    set_tty_speed_b38400(tty)
    tty[3] |= termios.CLOCAL | termios.CREAD
    if hasattr(termios, "CRTSCTS"):
        tty[3] &= ~termios.CRTSCTS
    termios.tcsetattr(fd, termios.TCSANOW, tty)


def open_uart(
    *,
    prefer_legacy: bool,
    use_overlay: bool,
    exact_baud: bool,
) -> tuple[int, str]:
    fd = os.open(DEVICE, os.O_RDWR | os.O_NOCTTY)
    if prefer_legacy and legacy_set_baud(fd, BAUD):
        return fd, f"legacy B38400+TIOCGSERIAL @ {BAUD}"

    if use_overlay and not exact_baud and not prefer_legacy:
        overlay_set_baud(fd)
        return fd, f"overlay B38400 (~{BAUD} on wire via midi-uart)"

    if prefer_legacy:
        print(
            f"WARN: TIOCGSERIAL not supported on {DEVICE}; "
            f"falling back to termios2/BOTHER @ {BAUD}",
            file=sys.stderr,
        )

    termios2_set_baud(fd, BAUD)
    return fd, f"termios2/BOTHER @ {BAUD}"


class BaudOptions:
    __slots__ = ("legacy", "overlay", "exact")

    def __init__(self, legacy: bool, overlay: bool, exact: bool) -> None:
        self.legacy = legacy
        self.overlay = overlay
        self.exact = exact

    def open(self) -> tuple[int, str]:
        return open_uart(
            prefer_legacy=self.legacy,
            use_overlay=self.overlay,
            exact_baud=self.exact,
        )


def read_until_deadline(fd: int, deadline: float) -> bytes:
    rx = bytearray()
    while True:
        remaining = deadline - time.time()
        if remaining <= 0:
            break
        ready, _, _ = select.select([fd], [], [], remaining)
        if not ready:
            break
        chunk = os.read(fd, 64)
        if chunk:
            rx.extend(chunk)
    return bytes(rx)


def cmd_listen(baud: BaudOptions, timeout: float | None) -> int:
    fd, mode = baud.open()
    if timeout is None:
        print(f"Listening on {DEVICE} ({mode}), Ctrl+C to stop")
    else:
        print(f"Listening on {DEVICE} ({mode}) for {timeout:g}s")
    rx = bytearray()
    try:
        if timeout is None:
            while True:
                chunk = os.read(fd, 64)
                if chunk:
                    rx.extend(chunk)
                    print(" ".join(f"{b:02X}" for b in chunk), flush=True)
        else:
            data = read_until_deadline(fd, time.time() + timeout)
            rx.extend(data)
            if data:
                print(" ".join(f"{b:02X}" for b in data), flush=True)
    except KeyboardInterrupt:
        print()
    finally:
        os.close(fd)
    if timeout is not None and not rx:
        return 2
    return 0


def cmd_send(baud: BaudOptions, hex_args: list[str]) -> int:
    if not hex_args:
        print("Usage: send 90 3c 40 ...", file=sys.stderr)
        return 1
    data = bytes(int(tok, 16) & 0xFF for tok in hex_args)
    fd, mode = baud.open()
    os.write(fd, data)
    os.close(fd)
    print(f"Sent ({mode}): {' '.join(f'{b:02X}' for b in data)}")
    return 0


def cmd_roundtrip(baud: BaudOptions, hex_args: list[str], timeout: float) -> int:
    if not hex_args:
        print("Usage: roundtrip 90 3c 40 ...", file=sys.stderr)
        return 1
    data = bytes(int(tok, 16) & 0xFF for tok in hex_args)
    fd, mode = baud.open()
    os.write(fd, data)
    print(f"Sent ({mode}): {' '.join(f'{b:02X}' for b in data)}")
    print(f"Listening {timeout:g}s for RX (needs OUT→IN loopback)...")
    rx = read_until_deadline(fd, time.time() + timeout)
    os.close(fd)
    if rx:
        print(f"RX: {' '.join(f'{b:02X}' for b in rx)}")
    if rx == data:
        print("OK: RX matches TX")
        return 0
    if not rx:
        print("FAIL: no RX (wire MIDI OUT to MIDI IN?)", file=sys.stderr)
        return 1
    print(f"FAIL: expected {' '.join(f'{b:02X}' for b in data)}", file=sys.stderr)
    return 1


def main() -> int:
    global DEVICE

    parser = argparse.ArgumentParser(description="Raw UART MIDI test (no ALSA bridge)")
    parser.add_argument(
        "--device",
        default=None,
        help=f"Serial device (default: {DEVICE})",
    )
    parser.add_argument(
        "--overlay-baud",
        action=argparse.BooleanOptionalAction,
        default=DEFAULT_OVERLAY_BAUD,
        help="B38400 for midi-uart overlay (default; use --no-overlay-baud to disable)",
    )
    parser.add_argument(
        "--exact-baud",
        action="store_true",
        help="termios2/BOTHER @ UART_BAUD (disables overlay mode)",
    )
    parser.add_argument(
        "--legacy-baud",
        action="store_true",
        help="Try B38400 + TIOCGSERIAL custom divisor",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=None,
        metavar="SEC",
        help="listen: stop after SEC (exit 2 if idle); roundtrip: RX window",
    )
    parser.add_argument(
        "command",
        choices=("listen", "send", "roundtrip"),
        help="listen, send, or roundtrip",
    )
    parser.add_argument("hex_bytes", nargs="*", help="hex bytes for send/roundtrip")
    args = parser.parse_args()

    if args.device:
        DEVICE = args.device

    use_overlay = args.overlay_baud and not args.exact_baud and not args.legacy_baud
    baud = BaudOptions(
        legacy=args.legacy_baud,
        overlay=use_overlay,
        exact=args.exact_baud,
    )

    if args.command == "listen":
        return cmd_listen(baud, args.timeout)
    if args.command == "send":
        return cmd_send(baud, args.hex_bytes)
    timeout = args.timeout if args.timeout is not None else DEFAULT_ROUNDTRIP_TIMEOUT
    return cmd_roundtrip(baud, args.hex_bytes, timeout)


if __name__ == "__main__":
    raise SystemExit(main())
