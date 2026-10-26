#include "midi_parser.h"

#include <string.h>

/* Total length of a message with this status byte, 0 if it has no fixed length */
static size_t message_length(unsigned char status)
{
    switch (status & 0xF0) {
    case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0:
        return 3;
    case 0xC0: case 0xD0:
        return 2;
    }
    switch (status) {
    case 0xF1: case 0xF3:
        return 2;
    case 0xF2:
        return 3;
    case 0xF6:
        return 1;
    }
    return 0;   /* F0, F4, F5, F7 */
}

void midi_parser_init(midi_parser_t *p, midi_parser_emit_fn emit, void *ctx)
{
    memset(p, 0, sizeof(*p));
    p->emit = emit;
    p->ctx = ctx;
}

static void sysex_byte(midi_parser_t *p, unsigned char byte)
{
    /* keep one byte free for the closing F7 */
    if (p->sysex_len < MIDI_PARSER_SYSEX_MAX - 1)
        p->sysex[p->sysex_len++] = byte;
    else
        p->sysex_overflow = 1;
}

static void sysex_end(midi_parser_t *p)
{
    p->in_sysex = 0;
    if (p->sysex_overflow) {
        p->dropped_sysex++;
        return;
    }
    p->sysex[p->sysex_len++] = 0xF7;
    p->emit(p->ctx, p->sysex, p->sysex_len);
}

void midi_parser_feed(midi_parser_t *p, unsigned char byte)
{
    /* Real-time messages may come anywhere, also inside another message or a SysEx */
    if (byte >= 0xF8) {
        if (byte == 0xF9 || byte == 0xFD)
            p->dropped_bytes++;   /* undefined */
        else
            p->emit(p->ctx, &byte, 1);
        return;
    }

    if (p->in_sysex) {
        if (byte == 0xF7) {
            sysex_end(p);
            return;
        }
        if (!(byte & 0x80)) {
            sysex_byte(p, byte);
            return;
        }
        /* Any other status byte ends the SysEx without F7: drop it, then handle the byte */
        p->in_sysex = 0;
        p->dropped_sysex++;
    }

    if (byte == 0xF0) {
        p->in_sysex = 1;
        p->sysex_overflow = 0;
        p->sysex[0] = 0xF0;
        p->sysex_len = 1;
        p->running_status = 0;
        p->msg_len = 0;
        return;
    }

    if (byte & 0x80) {
        size_t len = message_length(byte);

        /* Only channel messages set running status, system common clears it */
        p->running_status = byte < 0xF0 ? byte : 0;
        p->msg_len = 0;
        if (len == 0) {
            p->dropped_bytes++;   /* F4, F5, stray F7 */
            return;
        }
        if (len == 1) {
            p->emit(p->ctx, &byte, 1);   /* F6 */
            return;
        }
        p->msg[0] = byte;
        p->msg_len = 1;
        p->msg_expected = len;
        return;
    }

    /* Data byte */
    if (p->msg_len == 0) {
        if (p->running_status == 0) {
            p->dropped_bytes++;
            return;
        }
        p->msg[0] = p->running_status;
        p->msg_len = 1;
        p->msg_expected = message_length(p->running_status);
    }
    p->msg[p->msg_len++] = byte;
    if (p->msg_len == p->msg_expected) {
        p->emit(p->ctx, p->msg, p->msg_len);
        p->msg_len = 0;
    }
}
