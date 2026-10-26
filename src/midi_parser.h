/*
 * MIDI byte stream from the UART -> complete MIDI messages.
 *
 * Plain C without ALSA, so the unit tests also run on the Mac (make test).
 * The parser hands every complete message to the emit callback:
 *   - channel messages (2 or 3 bytes), running status expanded
 *   - system common F1, F2, F3 (with data) and F6
 *   - real-time F8, FA, FB, FC, FE, FF (1 byte), also in the middle of another message
 *   - SysEx as one message from F0 to F7, at most MIDI_PARSER_SYSEX_MAX bytes
 * It drops: data bytes without a status, F4, F5, F9, FD, a stray F7, a SysEx that another
 * status byte interrupts and a SysEx longer than MIDI_PARSER_SYSEX_MAX.
 */
#ifndef BENTO_TTYMIDI_MIDI_PARSER_H
#define BENTO_TTYMIDI_MIDI_PARSER_H

#include <stddef.h>

#define MIDI_PARSER_SYSEX_MAX 4096   /* F0 and F7 included */

typedef void (*midi_parser_emit_fn)(void *ctx, const unsigned char *msg, size_t len);

typedef struct {
    midi_parser_emit_fn emit;
    void *ctx;
    unsigned char running_status;   /* 0: none */
    unsigned char msg[3];
    size_t msg_len;                 /* bytes collected in msg, 0: no message open */
    size_t msg_expected;            /* length of the open message */
    unsigned char sysex[MIDI_PARSER_SYSEX_MAX];
    size_t sysex_len;
    int in_sysex;
    int sysex_overflow;
    unsigned long dropped_bytes;    /* for the log */
    unsigned long dropped_sysex;
} midi_parser_t;

void midi_parser_init(midi_parser_t *p, midi_parser_emit_fn emit, void *ctx);
void midi_parser_feed(midi_parser_t *p, unsigned char byte);

#endif
