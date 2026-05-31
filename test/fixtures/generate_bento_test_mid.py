#!/usr/bin/env python3
"""Generate a minimal copyright-free test MIDI file (C major scale)."""

from __future__ import annotations

import struct
from pathlib import Path


def write_vlq(value: int) -> bytes:
    buffer = [value & 0x7F]
    value >>= 7
    while value:
        buffer.insert(0, (value & 0x7F) | 0x80)
        value >>= 7
    return bytes(buffer)


def main() -> None:
    events = bytearray()
    notes = (60, 62, 64, 65, 67, 69, 71, 72)

    for note in notes:
        events += write_vlq(0)
        events += bytes((0x90, note, 0x50))
        events += write_vlq(24)
        events += bytes((0x80, note, 0x00))

    events += write_vlq(0)
    events += b"\xff\x2f\x00"

    track = b"MTrk" + struct.pack(">I", len(events)) + bytes(events)
    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, 96)

    out = Path(__file__).with_name("bento_test.mid")
    out.write_bytes(header + track)
    print(f"Wrote {out} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
