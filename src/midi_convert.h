/*
 * MIDI bytes <-> ALSA sequencer events, with the coders of alsa-lib (snd_midi_event).
 * Needs no open sequencer, so the tests can check it on their own (Linux only).
 */
#ifndef BENTO_TTYMIDI_MIDI_CONVERT_H
#define BENTO_TTYMIDI_MIDI_CONVERT_H

#include <stddef.h>
#include <alsa/asoundlib.h>

/*
 * One complete message from midi_parser -> event (source, destination and queue not set).
 * Note On with velocity 0 becomes Note Off. A SysEx event points to msg, keep msg alive
 * until the event is sent. Returns 0, or -1 if alsa-lib has no event for the message.
 */
int midi_convert_to_event(snd_midi_event_t *encoder, const unsigned char *msg, size_t len,
                          snd_seq_event_t *ev);

/*
 * Event -> bytes for the UART, always with status byte (decoder needs snd_midi_event_no_status).
 * A SysEx comes back as it is (*out points into the event), everything else is decoded
 * into buf. With note_off_0x80, Note On with velocity 0 goes out as 8n.
 * Returns the number of bytes in *out, or -1 if the event has no MIDI bytes.
 */
long midi_convert_from_event(snd_midi_event_t *decoder, const snd_seq_event_t *ev,
                             int note_off_0x80, unsigned char *buf, size_t size,
                             const unsigned char **out);

#endif
