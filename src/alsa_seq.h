/*
 * The ALSA sequencer client "bento_ttymidi" with its two ports:
 *   "MIDI out"  UART RX -> ALSA (readable, from the MIDI IN jack), port 0
 *   "MIDI in"   ALSA -> UART TX (writable, to the MIDI OUT jack), port 1
 * ALSA tools only take port numbers (aplaymidi -p bento_ttymidi:1), and setups, docs
 * and tests rely on them. The numbers 0 and 1 come from the order in which
 * alsa_seq_open() creates the ports: do not change it.
 *
 * Threads: alsa_seq_send() belongs to the UART thread, alsa_seq_read() to the main
 * thread. Both lock the sequencer handle, alsa-lib does not promise thread safety.
 */
#ifndef BENTO_TTYMIDI_ALSA_SEQ_H
#define BENTO_TTYMIDI_ALSA_SEQ_H

#include <poll.h>
#include <stddef.h>

#define ALSA_SEQ_CLIENT_NAME   "bento_ttymidi"
#define ALSA_SEQ_OUT_PORT_NAME "MIDI out"
#define ALSA_SEQ_IN_PORT_NAME  "MIDI in"

int alsa_seq_open(int note_off_0x80, int debug);
void alsa_seq_close(void);

/* Poll descriptors for incoming events; fills at most max, returns the count or -1 */
int alsa_seq_poll_descriptors(struct pollfd *pfds, int max);

/* Complete message from midi_parser -> subscribers of "MIDI out". 0 or -1 */
int alsa_seq_send(const unsigned char *msg, size_t len);

/*
 * Next event for the UART, as MIDI bytes. *data stays valid until the next call.
 * Returns 1 for a message, 0 when nothing is queued, -1 on a fatal error.
 */
int alsa_seq_read(const unsigned char **data, size_t *len);

/* Client and port numbers for the log */
int alsa_seq_client_id(void);

#endif
