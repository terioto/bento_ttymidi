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

```bash
cd test
./run_tests.sh
```

| Script | Purpose |
|--------|---------|
| `run_tests.sh` | Runs OUT + IN tests |
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

## Manual monitoring

```bash
./monitor.sh              # aseqdump on MIDI out
./monitor.sh record out.mid
```
