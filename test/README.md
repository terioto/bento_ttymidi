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
| `run_tests.sh` | Runs OUT + IN tests via ALSA |
| `test_midi_out.sh` | Plays `fixtures/bento_test.mid` into `bento_ttymidi:MIDI in` |
| `test_midi_in.sh` | Captures events on `bento_ttymidi:MIDI out` |
| `monitor.sh` | Manual live monitor or recording |
| `raw_uart_test.py` | Optional: direct UART send/listen without ALSA bridge |

## Test MIDI file

`fixtures/bento_test.mid` — copyright-free C-major scale (8 notes) for the OUT test.

## MIDI OUT test

Verifies that `aplaymidi` can send the test file to the bridge without error.

## MIDI IN test

**Passive (default)** — listens for `${BENTO_CAPTURE_SEC:-5}` seconds on MIDI out.
Exits with code **2 (skip)** if nothing arrives (no keyboard connected).

**Loopback** — round-trip when TX is wired to RX on the HAT. On **TRS Type-A**, a straight OUT→IN patch cable usually fails (OUT uses Tip, IN uses Ring). Use an external partner (Mac) or a Tip→Ring adapter.

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

## Optional: raw UART (no bento_ttymidi)

Stop the bridge first: `sudo systemctl stop bento_ttymidi`

```bash
./raw_uart_test.py send 90 3c 40 80 3c 00
./raw_uart_test.py listen --timeout 5
./raw_uart_test.py roundtrip --timeout 2 90 3c 40
```

Uses overlay B38400 by default (Pi 5 + `midi-uart0-pi5`). See `raw_uart_test.py --help`.

## Manual monitoring

```bash
./monitor.sh              # aseqdump on MIDI out
./monitor.sh record out.mid
```
