#include "midi_convert.h"

int midi_convert_to_event(snd_midi_event_t *encoder, const unsigned char *msg, size_t len,
                          snd_seq_event_t *ev)
{
    snd_seq_ev_clear(ev);
    if (len == 0)
        return -1;

    if (msg[0] == 0xF0) {
        snd_seq_ev_set_sysex(ev, (unsigned int)len, (void *)msg);
        return 0;
    }

    snd_midi_event_reset_encode(encoder);
    if (snd_midi_event_encode(encoder, msg, (long)len, ev) != (long)len
        || ev->type == SND_SEQ_EVENT_NONE)
        return -1;

    /* bento_ttymidi has always sent Note Off here, keep it that way */
    if (ev->type == SND_SEQ_EVENT_NOTEON && ev->data.note.velocity == 0)
        ev->type = SND_SEQ_EVENT_NOTEOFF;
    return 0;
}

long midi_convert_from_event(snd_midi_event_t *decoder, const snd_seq_event_t *ev,
                             int note_off_0x80, unsigned char *buf, size_t size,
                             const unsigned char **out)
{
    long n;

    if (ev->type == SND_SEQ_EVENT_SYSEX) {
        if (ev->data.ext.len == 0)
            return -1;
        *out = ev->data.ext.ptr;
        return (long)ev->data.ext.len;
    }

    n = snd_midi_event_decode(decoder, buf, (long)size, ev);
    if (n <= 0)
        return -1;

    if (note_off_0x80 && n == 3 && (buf[0] & 0xF0) == 0x90 && buf[2] == 0)
        buf[0] = (unsigned char)(0x80 | (buf[0] & 0x0F));
    *out = buf;
    return n;
}
