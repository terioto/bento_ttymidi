#!/usr/bin/env python3
"""
End-to-end test of bento_ttymidi without MIDI hardware. A pseudo terminal stands in for
the UART, the ALSA sequencer is the real one. Runs on Linux with ALSA, e.g. a Raspberry Pi:

    python3 -I tests/integration/pty_bridge.py [build/bento_ttymidi]

The bridge only ever opens the pty (--device), never a real UART. The checks address the
ports by number, ALSA tools do not take port names:
  port 0 "MIDI out"  UART RX -> ALSA
  port 1 "MIDI in"   ALSA -> UART TX

Checks:
  1. UART -> ALSA: bytes written to the pty arrive at "MIDI out" (aseqdump)
  2. no echo: nothing of it comes back out of the UART
  3. ALSA -> UART: aplaymidi into "MIDI in" comes out of the pty as MIDI bytes
  4. idle CPU: the bridge sleeps in poll()
  5. SIGTERM: exit code 0
  6. UART gone (pty closed): exit code 1, so systemd restarts the service

Needs aseqdump, aplaymidi (alsa-utils) and access to /dev/snd/seq.
"""

import os
import re
import select
import signal
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BRIDGE = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, "build", "bento_ttymidi")

failures = []


def check(cond, what):
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        failures.append(what)


def start_bridge(extra_args=()):
    master, slave = os.openpty()
    proc = subprocess.Popen([BRIDGE, "--device", os.ttyname(slave), "--debug", *extra_args],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
    deadline = time.time() + 5
    output = b""
    client = None
    while time.time() < deadline and client is None:
        ready, _, _ = select.select([proc.stdout], [], [], 0.2)
        if ready:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                break
            output += chunk
            m = re.search(rb"^bento_ttymidi .*ALSA client (\d+)$", output, re.MULTILINE)
            if m:
                client = int(m.group(1))
    lines = output.decode(errors="replace").splitlines()
    if client is None:
        proc.kill()
        sys.exit("bridge did not start:\n" + "\n".join(lines))
    return proc, master, slave, client, lines


def read_pty(fd, quiet=0.5, limit=5.0):
    """Read until the pty stays quiet for `quiet` seconds"""
    data = b""
    end = time.time() + limit
    while time.time() < end:
        ready, _, _ = select.select([fd], [], [], quiet)
        if not ready:
            break
        data += os.read(fd, 4096)
    return data


def cpu_seconds(pid):
    with open(f"/proc/{pid}/stat") as f:
        fields = f.read().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


def var_len(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)


def write_smf(path, events):
    """Format 0 MIDI file, events as (delta ticks, bytes in SMF notation)"""
    track = b"".join(var_len(delta) + data for delta, data in events) + b"\x00\xff\x2f\x00"
    with open(path, "wb") as f:
        f.write(b"MThd" + (6).to_bytes(4, "big") + (0).to_bytes(2, "big")
                + (1).to_bytes(2, "big") + (96).to_bytes(2, "big"))
        f.write(b"MTrk" + len(track).to_bytes(4, "big") + track)


def compact(text):
    return re.sub(r"\s+", " ", text)


def main():
    if not os.access(BRIDGE, os.X_OK):
        sys.exit(f"not found: {BRIDGE} (make first)")
    print(f"bridge: {BRIDGE}")

    proc, master, slave, client, lines = start_bridge()
    print("started: " + next(l for l in lines if l.startswith("bento_ttymidi ")))

    # 1. UART -> ALSA
    dump = subprocess.Popen(["aseqdump", "-p", f"{client}:0"], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    time.sleep(0.5)
    uart_in = bytes.fromhex(
        "90 3C 40"        # Note On
        " 3E 41"          # running status
        " 90 3C 00"       # Note On velocity 0 -> Note Off
        " B0 07 64"       # Control Change
        " C5 07"          # Program Change channel 5
        " E0 00 40"       # Pitch Bend center
        " F0 7D 01 02 F7" # SysEx
        " F0 7D F8 03 F7" # Clock inside a SysEx
        " F2 10 20"       # Song Position Pointer
        " F8 FA FC")      # Clock, Start, Stop
    os.write(master, uart_in)
    time.sleep(1.0)
    dump.terminate()
    dumped = compact(dump.communicate(timeout=5)[0])
    print("aseqdump: " + dumped)
    for expected in ["Note on 0, note 60, velocity 64",
                     "Note on 0, note 62, velocity 65",
                     "Note off 0, note 60, velocity 0",
                     "Control change 0, controller 7, value 100",
                     "Program change 5, program 7",
                     "Pitch bend 0, value 0",
                     "System exclusive F0 7D 01 02 F7",
                     "System exclusive F0 7D 03 F7",
                     "Song position pointer value 4112",
                     "Clock", "Start", "Stop"]:
        check(expected in dumped, f"UART -> ALSA: {expected}")

    # 2. no echo
    echoed = read_pty(master, quiet=0.3, limit=1.0)
    check(echoed == b"", f"no echo to the UART (got {echoed.hex(' ')})")

    # 3. ALSA -> UART
    with tempfile.TemporaryDirectory() as tmp:
        mid = os.path.join(tmp, "test.mid")
        write_smf(mid, [
            (0, bytes.fromhex("90 3C 40")),
            (0, bytes.fromhex("B0 07 64")),
            (0, bytes.fromhex("E0 00 40")),
            (0, bytes.fromhex("F0 04 7D 01 02 F7")),   # SMF SysEx: F0, length, data, F7
            (24, bytes.fromhex("80 3C 00")),
        ])
        subprocess.run(["aplaymidi", "-p", f"{client}:1", mid], check=True, timeout=10)
    uart_out = read_pty(master)
    expected_out = bytes.fromhex("90 3C 40 B0 07 64 E0 00 40 F0 7D 01 02 F7 80 3C 00")
    check(uart_out == expected_out,
          f"ALSA -> UART: {uart_out.hex(' ')} (expected {expected_out.hex(' ')})")

    # 4. idle CPU
    before = cpu_seconds(proc.pid)
    time.sleep(3)
    used = cpu_seconds(proc.pid) - before
    check(used < 0.15, f"idle CPU {used / 3 * 100:.1f} % over 3 s (limit 5 %)")

    # 5. SIGTERM
    proc.send_signal(signal.SIGTERM)
    try:
        code = proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        code = None
    check(code == 0, f"SIGTERM: exit code {code}")
    os.close(master)
    os.close(slave)

    # 6. UART gone
    proc, master, slave, client, _ = start_bridge()
    os.close(slave)
    os.close(master)
    try:
        code = proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        code = None
    check(code == 1, f"UART closed: exit code {code}")

    print()
    if failures:
        print(f"{len(failures)} check(s) failed")
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
