#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <pthread.h>
#include <alsa/asoundlib.h>

/*
 * UART0 on GPIO14/15. On the Pi 5, /dev/serial0 is the debug UART (ttyAMA10),
 * so open ttyAMA0 directly. On the Pi 4 with dtoverlay=disable-bt it is the same port.
 */
#define DEFAULT_DEVICE "/dev/ttyAMA0"

int serial_fd;
snd_seq_t *seq;
int out_port, in_port;
int debug = 0;
const char *device = DEFAULT_DEVICE;

void open_serial() {
    struct termios tty;

    serial_fd = open(device, O_RDWR | O_NOCTTY);
    if (serial_fd < 0) {
        perror("[ERROR] open serial device");
        exit(1);
    }
    if (debug) printf("[INFO] Opened serial device: %s\n", device);

    memset(&tty, 0, sizeof tty);
    if (tcgetattr(serial_fd, &tty) != 0) {
        perror("[ERROR] tcgetattr");
        exit(1);
    }

    /*
     * 38400 here is 31250 on the wire: the midi-uart0 (Pi 4) and midi-uart0-pi5 (Pi 5)
     * overlays set the UART clock that way. A custom divisor (TIOCSSERIAL) is not needed,
     * the PL011 driver ignores it and the kernel only logs "sets custom speed ... deprecated".
     */
    cfmakeraw(&tty);
    cfsetspeed(&tty, B38400);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(serial_fd, TCSANOW, &tty) != 0) {
        perror("[ERROR] tcsetattr");
        exit(1);
    }
    if (debug) printf("[INFO] Serial 8N1 at B38400 (31250 baud with the midi-uart overlay)\n");
}

void open_alsa_ports() {
    int client_id;

    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
        fprintf(stderr, "[ERROR] Cannot open ALSA sequencer\n");
        exit(1);
    }

    snd_seq_set_client_name(seq, "bento_ttymidi");
    client_id = snd_seq_client_id(seq);

    out_port = snd_seq_create_simple_port(seq, "MIDI out",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);

    in_port = snd_seq_create_simple_port(seq, "MIDI in",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);

    if (debug) {
        printf("[INFO] ALSA client ID: %d\n", client_id);
        printf("[INFO] ALSA out port:  %d:%d\n", client_id, out_port);
        printf("[INFO] ALSA in port:   %d:%d\n", client_id, in_port);
        printf("[INFO] bento_ttymidi now listening for ALSA → UART ...\n");
    }
}

void* serial_to_midi(void* arg) {
    unsigned char buffer[3];
    int state = 0;
    snd_seq_event_t ev;

    while (1) {
        unsigned char byte;
        if (read(serial_fd, &byte, 1) > 0) {
            if (debug) printf("[RX] %02X\n", byte);

            if ((byte & 0x80) != 0) {
                buffer[0] = byte;
                state = 1;
            } else if (state == 1) {
                buffer[1] = byte;
                state = 2;
            } else if (state == 2) {
                buffer[2] = byte;
                state = 0;

                unsigned char status = buffer[0] & 0xF0;
                unsigned char channel = buffer[0] & 0x0F;

                snd_seq_ev_clear(&ev);
                snd_seq_ev_set_source(&ev, out_port);
                snd_seq_ev_set_subs(&ev);
                snd_seq_ev_set_direct(&ev);

                if (status == 0x90 && buffer[2] > 0) {
                    ev.type = SND_SEQ_EVENT_NOTEON;
                    ev.data.note.channel = channel;
                    ev.data.note.note = buffer[1];
                    ev.data.note.velocity = buffer[2];
                } else if (status == 0x80 || (status == 0x90 && buffer[2] == 0)) {
                    ev.type = SND_SEQ_EVENT_NOTEOFF;
                    ev.data.note.channel = channel;
                    ev.data.note.note = buffer[1];
                    ev.data.note.velocity = buffer[2];
                } else if (status == 0xA0) {
                    ev.type = SND_SEQ_EVENT_KEYPRESS;
                    ev.data.note.channel = channel;
                    ev.data.note.note = buffer[1];
                    ev.data.note.velocity = buffer[2];
                } else if (status == 0xB0) {
                    ev.type = SND_SEQ_EVENT_CONTROLLER;
                    ev.data.control.channel = channel;
                    ev.data.control.param = buffer[1];
                    ev.data.control.value = buffer[2];
                } else if (status == 0xC0) {
                    ev.type = SND_SEQ_EVENT_PGMCHANGE;
                    ev.data.control.channel = channel;
                    ev.data.control.value = buffer[1];
                    state = 0; // only 1 data byte
                } else if (status == 0xD0) {
                    ev.type = SND_SEQ_EVENT_CHANPRESS;
                    ev.data.control.channel = channel;
                    ev.data.control.value = buffer[1];
                    state = 0;
                } else if (status == 0xE0) {
                    int value = buffer[1] | (buffer[2] << 7);
                    ev.type = SND_SEQ_EVENT_PITCHBEND;
                    ev.data.control.channel = channel;
                    ev.data.control.value = value - 8192;
                } else {
                    continue;
                }

                snd_seq_event_output_direct(seq, &ev);
            }
        }
    }

    return NULL;
}

void send_bytes(const unsigned char *data, int len, int client, int port) {
    write(serial_fd, data, len);
    if (debug) {
        printf("[TX] %d:%d -- ", client, port);
        for (int i = 0; i < len; ++i) {
            printf("%02X ", data[i]);
        }
        printf("\n");
    }
}

int main(int argc, char *argv[]) {
    /* -s and -v: older docs suggested "bento_ttymidi -s /dev/ttyAMA0 -v", keep it working */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-v") == 0) {
            debug = 1;
        } else if ((strcmp(argv[i], "--device") == 0 || strcmp(argv[i], "-s") == 0) && i + 1 < argc) {
            device = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--device PATH] [--debug]\n"
                   "  --device PATH  serial device (default: %s)\n"
                   "  --debug        print every byte\n",
                   argv[0], DEFAULT_DEVICE);
            return 0;
        } else {
            /* older versions ignored unknown options, so do not stop a running setup */
            fprintf(stderr, "[WARN] Ignoring unknown option: %s\n", argv[i]);
        }
    }

    pthread_t thread;

    open_serial();
    open_alsa_ports();

    pthread_create(&thread, NULL, serial_to_midi, NULL);

    snd_seq_event_t* ev;

    while (1) {
        snd_seq_event_input(seq, &ev);
        int client = ev->source.client;
        int port = ev->source.port;

        switch (ev->type) {
            case SND_SEQ_EVENT_NOTEON: {
                unsigned char data[3] = {
                    0x90 | ev->data.note.channel,
                    ev->data.note.note,
                    ev->data.note.velocity
                };
                send_bytes(data, 3, client, port);
                break;
            }
            case SND_SEQ_EVENT_NOTEOFF: {
                unsigned char data[3] = {
                    0x80 | ev->data.note.channel,
                    ev->data.note.note,
                    ev->data.note.velocity
                };
                send_bytes(data, 3, client, port);
                break;
            }
            case SND_SEQ_EVENT_CONTROLLER: {
                unsigned char data[3] = {
                    0xB0 | ev->data.control.channel,
                    ev->data.control.param,
                    ev->data.control.value
                };
                send_bytes(data, 3, client, port);
                break;
            }
            case SND_SEQ_EVENT_PGMCHANGE: {
                unsigned char data[2] = {
                    0xC0 | ev->data.control.channel,
                    ev->data.control.value
                };
                send_bytes(data, 2, client, port);
                break;
            }
            case SND_SEQ_EVENT_CHANPRESS: {
                unsigned char data[2] = {
                    0xD0 | ev->data.control.channel,
                    ev->data.control.value
                };
                send_bytes(data, 2, client, port);
                break;
            }
            case SND_SEQ_EVENT_KEYPRESS: {
                unsigned char data[3] = {
                    0xA0 | ev->data.note.channel,
                    ev->data.note.note,
                    ev->data.note.velocity
                };
                send_bytes(data, 3, client, port);
                break;
            }
            case SND_SEQ_EVENT_PITCHBEND: {
                int value = ev->data.control.value + 8192;
                unsigned char data[3] = {
                    0xE0 | ev->data.control.channel,
                    value & 0x7F,
                    (value >> 7) & 0x7F
                };
                send_bytes(data, 3, client, port);
                break;
            }
            case SND_SEQ_EVENT_SYSEX:
                send_bytes((unsigned char*)ev->data.ext.ptr, ev->data.ext.len, client, port);
                break;
            default:
                if (debug)
                    printf("[TX] %d:%d -- Unsupported MIDI event type: %d\n", client, port, ev->type);
                break;
        }
    }

    return 0;
}
