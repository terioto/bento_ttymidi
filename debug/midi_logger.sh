#!/bin/bash

# Kombinierter MIDI-Logger & Recorder
# Zeigt MIDI-Eingänge live (via aseqdump) und speichert sie in einer Datei (via arecordmidi)

PORT="128:0"
OUTFILE="midirecord_$(date +%Y%m%d_%H%M%S).mid"

echo "🎙 Starting MIDI recording from port $PORT..."
echo "💾 Output file: $OUTFILE"
echo "👀 Live MIDI messages below:"

# Starte arecordmidi im Hintergrund
arecordmidi -p "$PORT" "$OUTFILE" &
REC_PID=$!

# Starte aseqdump im Vordergrund
aseqdump -p "$PORT"

# Sobald aseqdump beendet wird (z. B. durch Ctrl+C), stoppen wir auch die Aufnahme
echo ""
echo "🛑 Stopping MIDI recording..."
kill $REC_PID
wait $REC_PID 2>/dev/null

echo "✅ Done. Saved to $OUTFILE"
