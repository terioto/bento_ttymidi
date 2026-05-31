#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <termios.h>
#include <pthread.h>
#include <signal.h>
#include <poll.h>
#include <linux/serial.h>
#include <sys/ioctl.h>
#include <alsa/asoundlib.h>

#ifdef __linux__
/*
 * Kernel termios2 layout (uapi asm-generic/termbits.h). Do not use glibc
 * speed_t here — size may differ and TCGETS2/TCSETS2 would fail silently.
 */
#define BENTO_NCCS 19

struct bento_termios2 {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t c_line;
    uint8_t c_cc[BENTO_NCCS];
    uint32_t c_ispeed;
    uint32_t c_ospeed;
};

/* Avoid system TCGETS2 — it uses incomplete struct termios2 from ioctl.h. */
#define BENTO_TCGETS2 _IOR('T', 0x2A, struct bento_termios2)
#define BENTO_TCSETS2 _IOW('T', 0x2B, struct bento_termios2)
#ifndef BOTHER
#define BOTHER 0010000
#endif
#endif

#define DEFAULT_DEVICE "/dev/ttyAMA0"
#define DEFAULT_BAUD   31250
#define SYSEX_MAX      4096

static int serial_fd = -1;
static snd_seq_t *seq;
static int out_port = -1;
static int in_port = -1;
static int client_id = -1;

static char *device_path = DEFAULT_DEVICE;
static int baud_rate = DEFAULT_BAUD;
static int debug = 0;
static int note_off_0x80 = 0;
static int overlay_baud = 1;

static volatile sig_atomic_t keep_running = 1;

typedef struct {
    unsigned char running_status;
    unsigned char data[2];
    int count;
    int expected;
    unsigned char sysex[SYSEX_MAX];
    int sysex_len;
    int in_sysex;
} midi_parser_t;

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "ALSA UART MIDI bridge for Raspberry Pi 5 / CM5 (Bento AMX -> MMX).\n"
        "\n"
        "Options:\n"
        "  --device PATH   Serial device (default: %s)\n"
        "  --baud RATE     Wire baud when using --exact-baud (default: %d)\n"
        "  --exact-baud    Set termios2/BOTHER @ --baud (skip overlay B38400)\n"
        "  --debug         Verbose hex logging\n"
        "  --note-off-0x80 Send Note Off as 0x80 instead of Note On vel=0\n"
        "  --help          Show this help\n"
        "\n"
        "Default serial mode: B38400 termios with midi-uart overlay (31250 on wire).\n"
        "On Pi 5 use /dev/ttyAMA0, not /dev/serial0 (debug UART).\n",
        prog, DEFAULT_DEVICE, DEFAULT_BAUD);
}

static int parse_args(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 1;
        }
        if (strcmp(argv[i], "--debug") == 0) {
            debug = 1;
        } else if (strcmp(argv[i], "--note-off-0x80") == 0) {
            note_off_0x80 = 1;
        } else if (strcmp(argv[i], "--device") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[ERROR] --device requires an argument\n");
                return -1;
            }
            device_path = argv[++i];
        } else if (strcmp(argv[i], "--baud") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[ERROR] --baud requires an argument\n");
                return -1;
            }
            baud_rate = atoi(argv[++i]);
            if (baud_rate <= 0) {
                fprintf(stderr, "[ERROR] Invalid baud rate\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--exact-baud") == 0) {
            overlay_baud = 0;
        } else {
            fprintf(stderr, "[ERROR] Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return -1;
        }
    }
    return 0;
}

static void handle_signal(int sig) {
    (void)sig;
    keep_running = 0;
}

#ifdef __linux__
static int configure_serial_termios2(int fd, int speed) {
    struct bento_termios2 tio;

    if (ioctl(fd, BENTO_TCGETS2, &tio) < 0) {
        if (debug)
            perror("[WARN] TCGETS2 failed");
        return -1;
    }

    tio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tio.c_oflag &= ~OPOST;
    tio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tio.c_cflag &= ~(CSIZE | PARENB | CRTSCTS);
    tio.c_cflag |= CS8 | CLOCAL | CREAD;
    tio.c_cflag &= ~CBAUD;
    tio.c_cflag |= BOTHER;
    tio.c_ispeed = (uint32_t)speed;
    tio.c_ospeed = (uint32_t)speed;

    if (ioctl(fd, BENTO_TCSETS2, &tio) < 0) {
        if (debug)
            perror("[WARN] TCSETS2 failed");
        return -1;
    }

    if (debug)
        printf("[INFO] Serial 8N1 @ %d baud via termios2/BOTHER\n", speed);
    return 0;
}
#endif

static int set_baudrate_legacy(int fd, int speed) {
    struct serial_struct ser;

    if (ioctl(fd, TIOCGSERIAL, &ser) < 0)
        return -1;

    ser.custom_divisor = ser.baud_base / speed;
    if (ser.custom_divisor == 0)
        return -1;

    ser.flags &= ~ASYNC_SPD_MASK;
    ser.flags |= ASYNC_SPD_CUST;

    if (ioctl(fd, TIOCSSERIAL, &ser) < 0)
        return -1;

    if (debug)
        printf("[INFO] Baudrate %d set via legacy TIOCGSERIAL (baud_base %u)\n",
               speed, ser.baud_base);
    return 0;
}

static void open_serial(void) {
    struct termios tty;

    serial_fd = open(device_path, O_RDWR | O_NOCTTY);
    if (serial_fd < 0) {
        perror("[ERROR] open serial device");
        exit(1);
    }
    if (debug)
        printf("[INFO] Opened serial device: %s\n", device_path);

    if (tcgetattr(serial_fd, &tty) != 0) {
        perror("[ERROR] tcgetattr");
        exit(1);
    }

    cfmakeraw(&tty);
    cfsetispeed(&tty, B38400);
    cfsetospeed(&tty, B38400);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(serial_fd, TCSANOW, &tty) != 0) {
        perror("[ERROR] tcsetattr");
        exit(1);
    }

    if (set_baudrate_legacy(serial_fd, baud_rate) == 0)
        return;

    /*
     * midi-uart0 / midi-uart0-pi5 overlay: request B38400 in termios; the DT
     * overlay maps that to 31250 on the wire. Do not override with BOTHER.
     */
    if (overlay_baud) {
        if (debug)
            printf("[INFO] Serial 8N1 @ B38400 (overlay maps to %d on wire)\n",
                   baud_rate);
        return;
    }

#ifdef __linux__
    if (configure_serial_termios2(serial_fd, baud_rate) == 0)
        return;
#endif

    fprintf(stderr, "[ERROR] Failed to configure serial for %d baud\n", baud_rate);
    exit(1);
}

static void open_alsa_ports(void) {
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

    if (out_port < 0 || in_port < 0) {
        fprintf(stderr, "[ERROR] Cannot create ALSA ports\n");
        exit(1);
    }

    snd_seq_nonblock(seq, 1);

    if (debug) {
        printf("[INFO] ALSA client ID: %d\n", client_id);
        printf("[INFO] ALSA out port:  %d:%d\n", client_id, out_port);
        printf("[INFO] ALSA in port:   %d:%d\n", client_id, in_port);
    }
}

static int write_all(const unsigned char *data, int len) {
    int written = 0;

    while (written < len) {
        ssize_t n = write(serial_fd, data + written, (size_t)(len - written));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("[ERROR] write serial");
            return -1;
        }
        written += (int)n;
    }
    return 0;
}

static void send_bytes(const unsigned char *data, int len, int src_client, int src_port) {
    if (write_all(data, len) != 0)
        return;

    if (debug) {
        printf("[TX] %d:%d -- ", src_client, src_port);
        for (int i = 0; i < len; ++i)
            printf("%02X ", data[i]);
        printf("\n");
    }
}

static int midi_expected_data_bytes(unsigned char status) {
    unsigned char cmd;

    if (status >= 0xF8)
        return 0;
    if (status == 0xF0)
        return -1;
    if (status == 0xF7)
        return 0;
    if (status >= 0xF1 && status <= 0xF6) {
        if (status == 0xF1 || status == 0xF3)
            return 1;
        if (status == 0xF2)
            return 2;
        return 0;
    }

    cmd = status & 0xF0;
    switch (cmd) {
    case 0xC0:
    case 0xD0:
        return 1;
    case 0x80:
    case 0x90:
    case 0xA0:
    case 0xB0:
    case 0xE0:
        return 2;
    default:
        return 0;
    }
}

static void emit_channel_message(midi_parser_t *p, unsigned char status,
                                 unsigned char d1, unsigned char d2) {
    (void)p;
    snd_seq_event_t ev;
    unsigned char cmd = status & 0xF0;
    unsigned char channel = status & 0x0F;

    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, out_port);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);

    if (cmd == 0x90 && d2 > 0) {
        ev.type = SND_SEQ_EVENT_NOTEON;
        ev.data.note.channel = channel;
        ev.data.note.note = d1;
        ev.data.note.velocity = d2;
    } else if (cmd == 0x80 || (cmd == 0x90 && d2 == 0)) {
        ev.type = SND_SEQ_EVENT_NOTEOFF;
        ev.data.note.channel = channel;
        ev.data.note.note = d1;
        ev.data.note.velocity = (cmd == 0x80) ? d2 : 0;
    } else if (cmd == 0xA0) {
        ev.type = SND_SEQ_EVENT_KEYPRESS;
        ev.data.note.channel = channel;
        ev.data.note.note = d1;
        ev.data.note.velocity = d2;
    } else if (cmd == 0xB0) {
        ev.type = SND_SEQ_EVENT_CONTROLLER;
        ev.data.control.channel = channel;
        ev.data.control.param = d1;
        ev.data.control.value = d2;
    } else if (cmd == 0xC0) {
        ev.type = SND_SEQ_EVENT_PGMCHANGE;
        ev.data.control.channel = channel;
        ev.data.control.value = d1;
    } else if (cmd == 0xD0) {
        ev.type = SND_SEQ_EVENT_CHANPRESS;
        ev.data.control.channel = channel;
        ev.data.control.value = d1;
    } else if (cmd == 0xE0) {
        int value = d1 | (d2 << 7);
        ev.type = SND_SEQ_EVENT_PITCHBEND;
        ev.data.control.channel = channel;
        ev.data.control.value = value - 8192;
    } else {
        return;
    }

    snd_seq_event_output_direct(seq, &ev);
}

static void emit_realtime(unsigned char byte) {
    snd_seq_event_t ev;

    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, out_port);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);

    switch (byte) {
    case 0xF8:
        ev.type = SND_SEQ_EVENT_CLOCK;
        break;
    case 0xFA:
        ev.type = SND_SEQ_EVENT_START;
        break;
    case 0xFB:
        ev.type = SND_SEQ_EVENT_CONTINUE;
        break;
    case 0xFC:
        ev.type = SND_SEQ_EVENT_STOP;
        break;
    case 0xFE:
        ev.type = SND_SEQ_EVENT_SENSING;
        break;
    case 0xFF:
        ev.type = SND_SEQ_EVENT_RESET;
        break;
    default:
        return;
    }

    snd_seq_event_output_direct(seq, &ev);
}

static void emit_sysex(midi_parser_t *p) {
    snd_seq_event_t ev;

    if (p->sysex_len <= 0)
        return;

    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, out_port);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    ev.type = SND_SEQ_EVENT_SYSEX;
    snd_seq_ev_set_sysex(&ev, p->sysex_len, p->sysex);
    snd_seq_event_output_direct(seq, &ev);
}

static void parser_reset(midi_parser_t *p) {
    p->count = 0;
    p->expected = 0;
    p->in_sysex = 0;
    p->sysex_len = 0;
}

static void parser_reset_channel(midi_parser_t *p) {
    p->count = 0;
    p->expected = 0;
}

static void process_midi_byte(midi_parser_t *p, unsigned char byte) {
    if (debug)
        printf("[RX] %02X\n", byte);

    if (byte >= 0xF8) {
        emit_realtime(byte);
        return;
    }

    if (p->in_sysex) {
        if (byte == 0xF7) {
            emit_sysex(p);
            parser_reset(p);
        } else {
            if (p->sysex_len < SYSEX_MAX)
                p->sysex[p->sysex_len++] = byte;
            else if (debug)
                fprintf(stderr, "[WARN] SysEx overflow, resetting\n");
        }
        return;
    }

    if (byte == 0xF0) {
        parser_reset(p);
        p->in_sysex = 1;
        p->sysex[0] = 0xF0;
        p->sysex_len = 1;
        return;
    }

    if (byte == 0xF7) {
        parser_reset(p);
        return;
    }

    if (byte & 0x80) {
        p->running_status = byte;
        p->expected = midi_expected_data_bytes(byte);
        p->count = 0;
        if (p->expected == 0 && byte >= 0xF1 && byte <= 0xF6)
            p->expected = midi_expected_data_bytes(byte);
        return;
    }

    if (p->expected <= 0) {
        if (p->running_status)
            p->expected = midi_expected_data_bytes(p->running_status);
        else
            return;
    }

    if (p->count < 2)
        p->data[p->count++] = byte;

    if (p->count >= p->expected) {
        unsigned char status = p->running_status;
        emit_channel_message(p, status, p->data[0],
                             p->expected >= 2 ? p->data[1] : 0);
        parser_reset_channel(p);
    }
}

static void *serial_to_midi(void *arg) {
    midi_parser_t parser = {0};
    struct pollfd pfd;

    (void)arg;

    pfd.fd = serial_fd;
    pfd.events = POLLIN;

    while (keep_running) {
        int r = poll(&pfd, 1, 500);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            perror("[ERROR] poll serial");
            break;
        }
        if (r == 0)
            continue;

        unsigned char byte;
        ssize_t n = read(serial_fd, &byte, 1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("[ERROR] read serial");
            break;
        }
        if (n == 0)
            continue;

        process_midi_byte(&parser, byte);
    }

    return NULL;
}

static int should_forward_to_uart(const snd_seq_event_t *ev) {
    if (ev->source.client == client_id)
        return 0;
    if (ev->dest.client != client_id)
        return 0;
    if (ev->dest.port != in_port)
        return 0;
    return 1;
}

static void forward_realtime_to_uart(const snd_seq_event_t *ev) {
    unsigned char byte = 0;

    switch (ev->type) {
    case SND_SEQ_EVENT_CLOCK:
        byte = 0xF8;
        break;
    case SND_SEQ_EVENT_START:
        byte = 0xFA;
        break;
    case SND_SEQ_EVENT_CONTINUE:
        byte = 0xFB;
        break;
    case SND_SEQ_EVENT_STOP:
        byte = 0xFC;
        break;
    case SND_SEQ_EVENT_SENSING:
        byte = 0xFE;
        break;
    case SND_SEQ_EVENT_RESET:
        byte = 0xFF;
        break;
    default:
        return;
    }

    send_bytes(&byte, 1, ev->source.client, ev->source.port);
}

static void forward_event_to_uart(const snd_seq_event_t *ev) {
    int client = ev->source.client;
    int port = ev->source.port;

    switch (ev->type) {
    case SND_SEQ_EVENT_NOTEON: {
        if (note_off_0x80 && ev->data.note.velocity == 0) {
            unsigned char data[3] = {
                (unsigned char)(0x80 | ev->data.note.channel),
                ev->data.note.note,
                0
            };
            send_bytes(data, 3, client, port);
        } else {
            unsigned char data[3] = {
                (unsigned char)(0x90 | ev->data.note.channel),
                ev->data.note.note,
                ev->data.note.velocity
            };
            send_bytes(data, 3, client, port);
        }
        break;
    }
    case SND_SEQ_EVENT_NOTEOFF: {
        unsigned char data[3] = {
            (unsigned char)(0x80 | ev->data.note.channel),
            ev->data.note.note,
            ev->data.note.velocity
        };
        send_bytes(data, 3, client, port);
        break;
    }
    case SND_SEQ_EVENT_CONTROLLER: {
        unsigned char data[3] = {
            (unsigned char)(0xB0 | ev->data.control.channel),
            ev->data.control.param,
            ev->data.control.value
        };
        send_bytes(data, 3, client, port);
        break;
    }
    case SND_SEQ_EVENT_PGMCHANGE: {
        unsigned char data[2] = {
            (unsigned char)(0xC0 | ev->data.control.channel),
            ev->data.control.value
        };
        send_bytes(data, 2, client, port);
        break;
    }
    case SND_SEQ_EVENT_CHANPRESS: {
        unsigned char data[2] = {
            (unsigned char)(0xD0 | ev->data.control.channel),
            ev->data.control.value
        };
        send_bytes(data, 2, client, port);
        break;
    }
    case SND_SEQ_EVENT_KEYPRESS: {
        unsigned char data[3] = {
            (unsigned char)(0xA0 | ev->data.note.channel),
            ev->data.note.note,
            ev->data.note.velocity
        };
        send_bytes(data, 3, client, port);
        break;
    }
    case SND_SEQ_EVENT_PITCHBEND: {
        int value = ev->data.control.value + 8192;
        unsigned char data[3] = {
            (unsigned char)(0xE0 | ev->data.control.channel),
            (unsigned char)(value & 0x7F),
            (unsigned char)((value >> 7) & 0x7F)
        };
        send_bytes(data, 3, client, port);
        break;
    }
    case SND_SEQ_EVENT_SYSEX:
        send_bytes((const unsigned char *)ev->data.ext.ptr, ev->data.ext.len,
                   client, port);
        break;
    case SND_SEQ_EVENT_CLOCK:
    case SND_SEQ_EVENT_START:
    case SND_SEQ_EVENT_CONTINUE:
    case SND_SEQ_EVENT_STOP:
    case SND_SEQ_EVENT_SENSING:
    case SND_SEQ_EVENT_RESET:
        forward_realtime_to_uart(ev);
        break;
    default:
        if (debug)
            printf("[TX] %d:%d -- Unsupported MIDI event type: %d\n",
                   client, port, ev->type);
        break;
    }
}

static void cleanup(void) {
    if (serial_fd >= 0) {
        close(serial_fd);
        serial_fd = -1;
    }
    if (seq) {
        snd_seq_close(seq);
        seq = NULL;
    }
}

int main(int argc, char *argv[]) {
    pthread_t rx_thread;
    int parse_result;

    parse_result = parse_args(argc, argv);
    if (parse_result != 0)
        return parse_result > 0 ? 0 : 1;

    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    open_serial();
    open_alsa_ports();

    if (pthread_create(&rx_thread, NULL, serial_to_midi, NULL) != 0) {
        fprintf(stderr, "[ERROR] pthread_create failed\n");
        cleanup();
        return 1;
    }

    if (debug)
        printf("[INFO] bento_ttymidi running on %s @ %d baud\n",
               device_path, baud_rate);

    while (keep_running) {
        snd_seq_event_t *ev = NULL;
        int err = snd_seq_event_input(seq, &ev);

        if (err == -EAGAIN || err == -EINTR)
            continue;
        if (err < 0) {
            if (!keep_running)
                break;
            fprintf(stderr, "[ERROR] snd_seq_event_input: %s\n", snd_strerror(err));
            continue;
        }
        if (!ev)
            continue;

        if (should_forward_to_uart(ev))
            forward_event_to_uart(ev);
    }

    keep_running = 0;
    pthread_join(rx_thread, NULL);
    cleanup();
    return 0;
}
