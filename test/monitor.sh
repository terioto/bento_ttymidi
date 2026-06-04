#!/bin/bash
# Live MIDI monitor for bento_ttymidi (manual debugging).
# Replaces the former debug/midi_logger.sh with dynamic port detection.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"

MODE="${1:-dump}"
RECORD_FILE="${2:-bento_midi_capture.mid}"

require_cmd aconnect
require_bento_ttymidi

case "$MODE" in
    dump)
        echo "Monitoring $CLIENT:TTY MIDI out ($BENTO_MIDI_OUT_PORT)"
        echo "Press Ctrl+C to stop."
        exec aseqdump -p "$BENTO_MIDI_OUT_PORT"
        ;;
    record)
        require_cmd arecordmidi
        echo "Recording from $CLIENT:TTY MIDI out ($BENTO_MIDI_OUT_PORT) -> $RECORD_FILE"
        echo "Press Ctrl+C to stop."
        exec arecordmidi -p "$BENTO_MIDI_OUT_PORT" "$RECORD_FILE"
        ;;
    *)
        echo "Usage: $0 [dump|record] [output.mid]"
        echo ""
        echo "  dump              Live hex/event monitor (default)"
        echo "  record [file.mid] Record MIDI to file (default: bento_midi_capture.mid)"
        exit 1
        ;;
esac
