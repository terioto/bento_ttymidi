# bento_ttymidi Tests

Automated and manual MIDI tests for the ALSA ↔ UART bridge on Raspberry Pi.

## Prerequisites

On the Pi (Debian Trixie):

```bash
sudo apt install alsa-utils
sudo systemctl start bento_ttymidi
```

Required commands: `aconnect`, `aplaymidi`, `aseqdump`.

## Quick run

**ALSA bridge** (requires `bento_ttymidi` running):

```bash
cd test
./run_tests.sh
```

**UART hardware only** (no `bento_ttymidi`, no ALSA):

```bash
cd test
./uart_midi.sh
```

| Script | Purpose |
|--------|---------|
| `run_tests.sh` | Runs OUT + IN tests via ALSA |
| `uart_midi.sh` | UART OUT / IN / loopback via `raw_uart_test.py` |
| `raw_uart_test.py` | Low-level send / listen / roundtrip on serial |
| `stop_bento_midi.sh` | Stop bridge before isolated UART tests |
| `test_midi_out.sh` | Plays `fixtures/bento_test.mid` into `bento_ttymidi:MIDI in` |
| `test_midi_in.sh` | Captures events on `bento_ttymidi:MIDI out` |
| `monitor.sh` | Manual live monitor or recording (former `debug/midi_logger.sh`) |

## Test MIDI file

`fixtures/bento_test.mid` is an original, copyright-free C-major scale (8 notes).
Regenerate with:

```bash
python3 fixtures/generate_bento_test_mid.py
```

## MIDI OUT test

Verifies that `aplaymidi` can send the test file to the bridge without error.
UART bytes appear on TX; confirm with a scope or logic analyzer if needed.

## MIDI IN test

Two modes:

**Passive (default)** — listens for `${BENTO_CAPTURE_SEC:-5}` seconds on MIDI out.
Exits with code **2 (skip)** if nothing arrives (no keyboard / no loopback).

**Loopback** — full round-trip when TX is wired to RX on the HAT:

```bash
BENTO_MIDI_LOOPBACK=1 ./test_midi_in.sh
```

## Environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `BENTO_MIDI_CLIENT` | `bento_ttymidi` | ALSA client name |
| `BENTO_TEST_MID` | `fixtures/bento_test.mid` | MIDI file for playback |
| `BENTO_MIDI_LOOPBACK` | `0` | `1` = round-trip IN test |
| `BENTO_CAPTURE_SEC` | `5` | Passive capture duration |
| `BENTO_TEST_TIMEOUT` | `15` / `20` | Command timeouts |

## UART test suite (`uart_midi.sh`)

Stops `bento_ttymidi` by default, then runs three checks on `/dev/ttyAMA0`:

| Step | Auto PASS | Needs hardware |
|------|-----------|----------------|
| OUT | bytes written | optional: MIDI monitor on HAT OUT |
| IN | bytes in capture window | MIDI source on HAT IN (or skip) |
| Loopback | RX == TX | `UART_LOOPBACK=1` + OUT wired to IN |

```bash
./uart_midi.sh
UART_LOOPBACK=1 ./uart_midi.sh
UART_CAPTURE_SEC=10 ./uart_midi.sh
UART_STOP_BRIDGE=0 ./uart_midi.sh   # do not stop bento_ttymidi
```

| Variable | Default | Description |
|----------|---------|-------------|
| `UART_DEVICE` | `/dev/ttyAMA0` | Serial device |
| `UART_BAUD` | `31250` | Baud rate |
| `UART_CAPTURE_SEC` | `5` | Passive IN listen duration |
| `UART_LOOPBACK` | `0` | `1` = run roundtrip test |
| `UART_ROUNDTRIP_TIMEOUT` | `2` | Loopback RX window (seconds) |
| `UART_STOP_BRIDGE` | `1` | Stop `bento_ttymidi` before test |
| `UART_OVERLAY_BAUD` | `1` | B38400 via midi-uart overlay (Pi 5 default) |
| `UART_LEGACY_BAUD` | `0` | `1` = try TIOCGSERIAL first |

Manual UART tools:

```bash
./raw_uart_test.py send 90 3c 40 80 3c 00
./raw_uart_test.py listen --timeout 5
./raw_uart_test.py roundtrip --timeout 2 90 3c 40
```

## Manual monitoring

```bash
./monitor.sh              # aseqdump on MIDI out
./monitor.sh record out.mid
```
