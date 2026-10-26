#include "alsa_seq.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <alsa/asoundlib.h>

#include "midi_convert.h"

static snd_seq_t *seq;
static int client_id = -1;
static int out_port = -1;
static int in_port = -1;
static int note_off_0x80;
static int debug;

static snd_midi_event_t *encoder;   /* UART thread only */
static snd_midi_event_t *decoder;   /* main thread only */
static unsigned char decoded[32];   /* longest decode: NRPN, 4 x 3 bytes */

static pthread_mutex_t seq_lock = PTHREAD_MUTEX_INITIALIZER;

int alsa_seq_open(int note_off_0x80_flag, int debug_flag)
{
    int err;

    note_off_0x80 = note_off_0x80_flag;
    debug = debug_flag;

    err = snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
    if (err < 0) {
        fprintf(stderr, "[ERROR] Cannot open ALSA sequencer: %s\n", snd_strerror(err));
        return -1;
    }
    snd_seq_set_client_name(seq, ALSA_SEQ_CLIENT_NAME);
    client_id = snd_seq_client_id(seq);

    out_port = snd_seq_create_simple_port(seq, ALSA_SEQ_OUT_PORT_NAME,
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    in_port = snd_seq_create_simple_port(seq, ALSA_SEQ_IN_PORT_NAME,
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (out_port < 0 || in_port < 0) {
        fprintf(stderr, "[ERROR] Cannot create ALSA ports\n");
        return -1;
    }

    if (snd_midi_event_new(16, &encoder) < 0 || snd_midi_event_new(sizeof(decoded), &decoder) < 0) {
        fprintf(stderr, "[ERROR] Cannot create ALSA MIDI coders\n");
        return -1;
    }
    /* Always send the status byte, no running status on the wire */
    snd_midi_event_no_status(decoder, 1);

    if (debug) {
        printf("[INFO] ALSA client %d, out port %d:%d, in port %d:%d\n",
               client_id, client_id, out_port, client_id, in_port);
    }
    return 0;
}

void alsa_seq_close(void)
{
    if (encoder) {
        snd_midi_event_free(encoder);
        encoder = NULL;
    }
    if (decoder) {
        snd_midi_event_free(decoder);
        decoder = NULL;
    }
    if (seq) {
        snd_seq_close(seq);
        seq = NULL;
    }
}

int alsa_seq_poll_descriptors(struct pollfd *pfds, int max)
{
    int count = snd_seq_poll_descriptors_count(seq, POLLIN);

    if (count <= 0 || count > max) {
        fprintf(stderr, "[ERROR] ALSA poll descriptors: %d (room for %d)\n", count, max);
        return -1;
    }
    return snd_seq_poll_descriptors(seq, pfds, (unsigned int)count, POLLIN);
}

int alsa_seq_send(const unsigned char *msg, size_t len)
{
    snd_seq_event_t ev;
    int err;

    if (midi_convert_to_event(encoder, msg, len, &ev) < 0) {
        if (debug)
            printf("[RX] no ALSA event for status %02X, dropped\n", msg[0]);
        return -1;
    }
    snd_seq_ev_set_source(&ev, out_port);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);

    pthread_mutex_lock(&seq_lock);
    err = snd_seq_event_output_direct(seq, &ev);
    pthread_mutex_unlock(&seq_lock);
    if (err < 0) {
        fprintf(stderr, "[WARN] ALSA output: %s\n", snd_strerror(err));
        return -1;
    }
    return 0;
}

int alsa_seq_read(const unsigned char **data, size_t *len)
{
    for (;;) {
        snd_seq_event_t *ev = NULL;
        long n;
        int err;

        pthread_mutex_lock(&seq_lock);
        err = snd_seq_event_input(seq, &ev);
        pthread_mutex_unlock(&seq_lock);

        if (err == -EAGAIN)
            return 0;
        if (err == -EINTR)
            continue;
        if (err == -ENOSPC) {
            fprintf(stderr, "[WARN] ALSA input overrun, events lost\n");
            continue;
        }
        if (err < 0) {
            fprintf(stderr, "[ERROR] snd_seq_event_input: %s\n", snd_strerror(err));
            return -1;
        }
        if (!ev)
            continue;

        /* Only what was sent to "MIDI in", and never our own output (no echo to the UART) */
        if (ev->source.client == client_id || ev->dest.client != client_id
            || ev->dest.port != in_port)
            continue;

        n = midi_convert_from_event(decoder, ev, note_off_0x80, decoded, sizeof(decoded), data);
        if (n < 0) {
            if (debug)
                printf("[TX] %d:%d -- no MIDI bytes for event type %d\n",
                       ev->source.client, ev->source.port, ev->type);
            continue;
        }
        *len = (size_t)n;
        return 1;
    }
}

int alsa_seq_client_id(void)
{
    return client_id;
}
