/* Unit tests for src/midi_convert.c, run with: make test (needs libasound, so Pi or Linux) */
#include <string.h>
#include <alsa/asoundlib.h>

#include "check.h"
#include "midi_convert.h"

static snd_midi_event_t *encoder;
static snd_midi_event_t *decoder;

static int encode(const unsigned char *msg, size_t len, snd_seq_event_t *ev)
{
    return midi_convert_to_event(encoder, msg, len, ev);
}

#define ENCODE(ev, ...) do { \
        static const unsigned char msg_[] = { __VA_ARGS__ }; \
        CHECK(encode(msg_, sizeof(msg_), (ev)) == 0); \
    } while (0)

/* Decode ev and compare with the expected bytes */
static int decodes_to(const snd_seq_event_t *ev, int note_off_0x80,
                      const unsigned char *expected, size_t len)
{
    unsigned char buf[32];
    const unsigned char *out = NULL;
    long n = midi_convert_from_event(decoder, ev, note_off_0x80, buf, sizeof(buf), &out);

    return n == (long)len && out && memcmp(out, expected, len) == 0;
}

#define DECODES_TO(ev, note_off, ...) do { \
        static const unsigned char exp_[] = { __VA_ARGS__ }; \
        CHECK(decodes_to((ev), (note_off), exp_, sizeof(exp_))); \
    } while (0)

static void test_encode_channel(void)
{
    snd_seq_event_t ev;

    ENCODE(&ev, 0x90, 0x3C, 0x40);
    CHECK(ev.type == SND_SEQ_EVENT_NOTEON && ev.data.note.channel == 0
          && ev.data.note.note == 0x3C && ev.data.note.velocity == 0x40);

    /* Note On with velocity 0 -> Note Off */
    ENCODE(&ev, 0x93, 0x3C, 0x00);
    CHECK(ev.type == SND_SEQ_EVENT_NOTEOFF && ev.data.note.channel == 3
          && ev.data.note.velocity == 0);

    ENCODE(&ev, 0x80, 0x3C, 0x10);
    CHECK(ev.type == SND_SEQ_EVENT_NOTEOFF && ev.data.note.velocity == 0x10);

    ENCODE(&ev, 0xA0, 0x3C, 0x20);
    CHECK(ev.type == SND_SEQ_EVENT_KEYPRESS && ev.data.note.velocity == 0x20);

    ENCODE(&ev, 0xB1, 0x07, 0x64);
    CHECK(ev.type == SND_SEQ_EVENT_CONTROLLER && ev.data.control.channel == 1
          && ev.data.control.param == 7 && ev.data.control.value == 100);

    ENCODE(&ev, 0xC2, 0x05);
    CHECK(ev.type == SND_SEQ_EVENT_PGMCHANGE && ev.data.control.channel == 2
          && ev.data.control.value == 5);

    ENCODE(&ev, 0xD0, 0x30);
    CHECK(ev.type == SND_SEQ_EVENT_CHANPRESS && ev.data.control.value == 0x30);

    ENCODE(&ev, 0xE0, 0x00, 0x40);
    CHECK(ev.type == SND_SEQ_EVENT_PITCHBEND && ev.data.control.value == 0);
    ENCODE(&ev, 0xE0, 0x00, 0x00);
    CHECK(ev.type == SND_SEQ_EVENT_PITCHBEND && ev.data.control.value == -8192);
    ENCODE(&ev, 0xE0, 0x7F, 0x7F);
    CHECK(ev.type == SND_SEQ_EVENT_PITCHBEND && ev.data.control.value == 8191);
}

static void test_encode_system(void)
{
    snd_seq_event_t ev;

    ENCODE(&ev, 0xF8);
    CHECK(ev.type == SND_SEQ_EVENT_CLOCK);
    ENCODE(&ev, 0xFA);
    CHECK(ev.type == SND_SEQ_EVENT_START);
    ENCODE(&ev, 0xFB);
    CHECK(ev.type == SND_SEQ_EVENT_CONTINUE);
    ENCODE(&ev, 0xFC);
    CHECK(ev.type == SND_SEQ_EVENT_STOP);
    ENCODE(&ev, 0xFE);
    CHECK(ev.type == SND_SEQ_EVENT_SENSING);
    ENCODE(&ev, 0xFF);
    CHECK(ev.type == SND_SEQ_EVENT_RESET);

    ENCODE(&ev, 0xF1, 0x23);
    CHECK(ev.type == SND_SEQ_EVENT_QFRAME && ev.data.control.value == 0x23);
    ENCODE(&ev, 0xF2, 0x10, 0x20);
    CHECK(ev.type == SND_SEQ_EVENT_SONGPOS && ev.data.control.value == (0x10 | (0x20 << 7)));
    ENCODE(&ev, 0xF3, 0x05);
    CHECK(ev.type == SND_SEQ_EVENT_SONGSEL && ev.data.control.value == 5);
    ENCODE(&ev, 0xF6);
    CHECK(ev.type == SND_SEQ_EVENT_TUNE_REQUEST);

    {
        static const unsigned char sysex[] = { 0xF0, 0x7D, 0x01, 0x02, 0xF7 };
        CHECK(encode(sysex, sizeof(sysex), &ev) == 0);
        CHECK(ev.type == SND_SEQ_EVENT_SYSEX && ev.data.ext.len == sizeof(sysex)
              && ev.data.ext.ptr == (const void *)sysex);
    }
}

static void test_decode(void)
{
    snd_seq_event_t ev;
    static unsigned char sysex[] = { 0xF0, 0x7D, 0x01, 0xF7 };

    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_noteon(&ev, 0, 0x3C, 0x40);
    DECODES_TO(&ev, 0, 0x90, 0x3C, 0x40);
    /* the same again: still with status byte, no running status */
    DECODES_TO(&ev, 0, 0x90, 0x3C, 0x40);

    snd_seq_ev_set_noteon(&ev, 1, 0x3C, 0x00);
    DECODES_TO(&ev, 0, 0x91, 0x3C, 0x00);
    DECODES_TO(&ev, 1, 0x81, 0x3C, 0x00);

    snd_seq_ev_set_noteoff(&ev, 2, 0x3C, 0x05);
    DECODES_TO(&ev, 0, 0x82, 0x3C, 0x05);

    snd_seq_ev_set_controller(&ev, 0, 7, 100);
    DECODES_TO(&ev, 0, 0xB0, 0x07, 0x64);

    snd_seq_ev_set_pgmchange(&ev, 0, 5);
    DECODES_TO(&ev, 0, 0xC0, 0x05);

    snd_seq_ev_set_pitchbend(&ev, 0, 0);
    DECODES_TO(&ev, 0, 0xE0, 0x00, 0x40);
    snd_seq_ev_set_pitchbend(&ev, 0, -8192);
    DECODES_TO(&ev, 0, 0xE0, 0x00, 0x00);

    snd_seq_ev_clear(&ev);
    ev.type = SND_SEQ_EVENT_CLOCK;
    DECODES_TO(&ev, 0, 0xF8);

    snd_seq_ev_clear(&ev);
    ev.type = SND_SEQ_EVENT_SONGPOS;
    ev.data.control.value = 0x10 | (0x20 << 7);
    DECODES_TO(&ev, 0, 0xF2, 0x10, 0x20);

    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_sysex(&ev, sizeof(sysex), sysex);
    DECODES_TO(&ev, 0, 0xF0, 0x7D, 0x01, 0xF7);

    /* 14-bit controller: MSB on the controller, LSB on controller + 32 */
    snd_seq_ev_clear(&ev);
    ev.type = SND_SEQ_EVENT_CONTROL14;
    ev.data.control.channel = 1;
    ev.data.control.param = 7;
    ev.data.control.value = (0x24 << 7) | 0x34;
    DECODES_TO(&ev, 0, 0xB1, 0x07, 0x24, 0xB1, 0x27, 0x34);

    /* NRPN: four controllers 99, 98, 6, 38 */
    snd_seq_ev_clear(&ev);
    ev.type = SND_SEQ_EVENT_NONREGPARAM;
    ev.data.control.channel = 0;
    ev.data.control.param = (0x01 << 7) | 0x02;
    ev.data.control.value = (0x03 << 7) | 0x04;
    DECODES_TO(&ev, 0, 0xB0, 0x63, 0x01, 0xB0, 0x62, 0x02, 0xB0, 0x06, 0x03, 0xB0, 0x26, 0x04);

    /* events without MIDI bytes */
    {
        unsigned char buf[32];
        const unsigned char *out;

        snd_seq_ev_clear(&ev);
        ev.type = SND_SEQ_EVENT_PORT_SUBSCRIBED;
        CHECK(midi_convert_from_event(decoder, &ev, 0, buf, sizeof(buf), &out) < 0);
    }
}

/* UART bytes -> event -> UART bytes gives the same bytes back */
static void test_round_trip(void)
{
    static const unsigned char messages[][3] = {
        { 0x90, 0x3C, 0x40 }, { 0x85, 0x3C, 0x10 }, { 0xA0, 0x3C, 0x20 }, { 0xBF, 0x7B, 0x00 },
        { 0xC0, 0x7F, 0 },    { 0xD0, 0x30, 0 },    { 0xE0, 0x12, 0x34 }, { 0xF2, 0x7F, 0x7F },
        { 0xF3, 0x01, 0 },    { 0xF1, 0x70, 0 },    { 0xF6, 0, 0 },       { 0xF8, 0, 0 },
    };
    static const size_t lengths[] = { 3, 3, 3, 3, 2, 2, 3, 3, 2, 2, 1, 1 };

    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        snd_seq_event_t ev;

        CHECK(encode(messages[i], lengths[i], &ev) == 0);
        CHECK(decodes_to(&ev, 0, messages[i], lengths[i]));
    }
}

int main(void)
{
    if (snd_midi_event_new(16, &encoder) < 0 || snd_midi_event_new(32, &decoder) < 0) {
        fprintf(stderr, "snd_midi_event_new failed\n");
        return 1;
    }
    snd_midi_event_no_status(decoder, 1);

    test_encode_channel();
    test_encode_system();
    test_decode();
    test_round_trip();

    snd_midi_event_free(encoder);
    snd_midi_event_free(decoder);
    return check_report("test_midi_convert");
}
