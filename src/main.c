/*
 * bento_ttymidi: UART MIDI bridge for the Raspberry Pi 4 and 5 (BentoIO AMH2).
 *
 *   UART RX -> midi_parser -> ALSA port "MIDI out"   (UART thread)
 *   ALSA port "MIDI in" -> MIDI bytes -> UART TX      (main thread)
 *
 * Both threads wait in poll() and wake up at least every POLL_TIMEOUT_MS to see whether
 * they should stop. A fatal UART or ALSA error ends the process with exit code 1, so
 * systemd restarts it (Restart=always or Restart=on-failure).
 */
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include "alsa_seq.h"
#include "midi_parser.h"
#include "uart.h"
#include "version.h"

#define DEFAULT_DEVICE  "/dev/ttyAMA0"
#define DEFAULT_BAUD    31250
#define POLL_TIMEOUT_MS 500
#define MAX_POLL_FDS    8

static const char *device_path = DEFAULT_DEVICE;
static int baud_rate = DEFAULT_BAUD;
static int baud_given;
static int exact_baud;
static int note_off_0x80;
static int debug;

static int serial_fd = -1;
static midi_parser_t parser;

static atomic_int running = 1;
static atomic_int exit_code = 0;

static void stop(int code)
{
    if (code != 0)
        atomic_store(&exit_code, code);
    atomic_store(&running, 0);
}

static void handle_signal(int sig)
{
    (void)sig;
    atomic_store(&running, 0);
}

static void usage(const char *prog)
{
    printf(
        "Usage: %s [OPTIONS]\n"
        "\n"
        "UART MIDI bridge for the Raspberry Pi 4 and 5 (BentoIO AMH2).\n"
        "ALSA client \"%s\": port 0 \"%s\" (from MIDI IN), port 1 \"%s\" (to MIDI OUT).\n"
        "\n"
        "Options:\n"
        "  -s, --device PATH    Serial device (default: %s, not /dev/serial0 on the Pi 5)\n"
        "  -v, --debug          Log every message in hex\n"
        "      --exact-baud     Set --baud with termios2/BOTHER. Only without the midi-uart0\n"
        "                       or midi-uart0-pi5 overlay, with it the bytes come out garbled\n"
        "      --baud RATE      Baud rate for --exact-baud (default: %d)\n"
        "      --note-off-0x80  Send Note On with velocity 0 as Note Off (8n) to the UART\n"
        "      --version        Print the version\n"
        "  -h, --help           Show this help\n"
        "\n"
        "Default: termios B38400, the overlay midi-uart0 (Pi 4) or midi-uart0-pi5 (Pi 5)\n"
        "makes it 31250 baud on the wire. Unknown options are ignored with a warning.\n",
        prog, ALSA_SEQ_CLIENT_NAME, ALSA_SEQ_OUT_PORT_NAME, ALSA_SEQ_IN_PORT_NAME,
        DEFAULT_DEVICE, DEFAULT_BAUD);
}

/* Returns 0 to run, 1 to exit with 0 (help, version), -1 to exit with 1 */
static int parse_args(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 1;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("bento_ttymidi %s (%s)\n", BENTO_TTYMIDI_VERSION, BENTO_TTYMIDI_COMMIT);
            return 1;
        } else if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-v") == 0) {
            debug = 1;
        } else if (strcmp(argv[i], "--note-off-0x80") == 0) {
            note_off_0x80 = 1;
        } else if (strcmp(argv[i], "--exact-baud") == 0) {
            exact_baud = 1;
        } else if (strcmp(argv[i], "--device") == 0 || strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[ERROR] %s requires an argument\n", argv[i]);
                return -1;
            }
            device_path = argv[++i];
        } else if (strcmp(argv[i], "--baud") == 0) {
            char *end;
            long value;

            if (i + 1 >= argc) {
                fprintf(stderr, "[ERROR] --baud requires an argument\n");
                return -1;
            }
            value = strtol(argv[++i], &end, 10);
            if (*end != '\0' || value <= 0 || value > 4000000) {
                fprintf(stderr, "[ERROR] Invalid baud rate: %s\n", argv[i]);
                return -1;
            }
            baud_rate = (int)value;
            baud_given = 1;
        } else {
            /* Versions before 1.0.0 ignored unknown options, so do not stop a running setup */
            fprintf(stderr, "[WARN] Ignoring unknown option: %s\n", argv[i]);
        }
    }
    if (baud_given && !exact_baud)
        fprintf(stderr, "[WARN] --baud has no effect without --exact-baud\n");
    return 0;
}

static void log_bytes(const char *tag, const unsigned char *data, size_t len)
{
    printf("%s", tag);
    for (size_t i = 0; i < len; i++)
        printf(" %02X", data[i]);
    printf("\n");
}

/* midi_parser callback: one complete message from the UART */
static void forward_to_alsa(void *ctx, const unsigned char *msg, size_t len)
{
    (void)ctx;
    if (debug)
        log_bytes("[RX]", msg, len);
    alsa_seq_send(msg, len);
}

static void *uart_rx_thread(void *arg)
{
    struct pollfd pfd = { .fd = serial_fd, .events = POLLIN };
    unsigned char buf[256];

    (void)arg;
    while (atomic_load(&running)) {
        int r = poll(&pfd, 1, POLL_TIMEOUT_MS);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "[ERROR] poll UART: %s\n", strerror(errno));
            stop(1);
            break;
        }
        if (r == 0)
            continue;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(stderr, "[ERROR] UART %s: poll revents 0x%x\n", device_path, pfd.revents);
            stop(1);
            break;
        }

        ssize_t n = read(serial_fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            fprintf(stderr, "[ERROR] read UART: %s\n", strerror(errno));
            stop(1);
            break;
        }
        if (n == 0) {
            fprintf(stderr, "[ERROR] read UART: end of file\n");
            stop(1);
            break;
        }
        for (ssize_t i = 0; i < n; i++)
            midi_parser_feed(&parser, buf[i]);
    }
    return NULL;
}

/* ALSA -> UART until stopped */
static void alsa_to_uart_loop(void)
{
    struct pollfd pfds[MAX_POLL_FDS];
    int nfds = alsa_seq_poll_descriptors(pfds, MAX_POLL_FDS);

    if (nfds < 0) {
        stop(1);
        return;
    }

    while (atomic_load(&running)) {
        int r = poll(pfds, (nfds_t)nfds, POLL_TIMEOUT_MS);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "[ERROR] poll ALSA: %s\n", strerror(errno));
            stop(1);
            return;
        }
        if (r == 0)
            continue;

        for (;;) {
            const unsigned char *data;
            size_t len;
            int got = alsa_seq_read(&data, &len);

            if (got == 0)
                break;
            if (got < 0) {
                stop(1);
                return;
            }
            if (uart_write_all(serial_fd, data, len) < 0) {
                stop(1);
                return;
            }
            if (debug)
                log_bytes("[TX]", data, len);
        }
    }
}

int main(int argc, char *argv[])
{
    struct sigaction sa;
    pthread_t rx_thread;
    int result;

    /* systemd journal: one line at a time, not when the buffer is full */
    setvbuf(stdout, NULL, _IOLBF, 0);

    result = parse_args(argc, argv);
    if (result != 0)
        return result > 0 ? 0 : 1;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    serial_fd = uart_open(device_path, exact_baud, baud_rate);
    if (serial_fd < 0)
        return 1;
    if (alsa_seq_open(note_off_0x80, debug) < 0) {
        alsa_seq_close();
        close(serial_fd);
        return 1;
    }

    if (exact_baud)
        printf("bento_ttymidi %s (%s): %s, %d baud via termios2, ALSA client %d\n",
               BENTO_TTYMIDI_VERSION, BENTO_TTYMIDI_COMMIT, device_path, baud_rate,
               alsa_seq_client_id());
    else
        printf("bento_ttymidi %s (%s): %s, B38400 (31250 baud with midi-uart0 on the Pi 4 "
               "or midi-uart0-pi5 on the Pi 5), ALSA client %d\n",
               BENTO_TTYMIDI_VERSION, BENTO_TTYMIDI_COMMIT, device_path, alsa_seq_client_id());

    midi_parser_init(&parser, forward_to_alsa, NULL);
    if (pthread_create(&rx_thread, NULL, uart_rx_thread, NULL) != 0) {
        fprintf(stderr, "[ERROR] pthread_create failed\n");
        alsa_seq_close();
        close(serial_fd);
        return 1;
    }

    alsa_to_uart_loop();

    stop(0);
    pthread_join(rx_thread, NULL);

    if (parser.dropped_bytes || parser.dropped_sysex)
        printf("[INFO] Dropped from the UART: %lu bytes, %lu SysEx\n",
               parser.dropped_bytes, parser.dropped_sysex);

    alsa_seq_close();
    close(serial_fd);
    return atomic_load(&exit_code);
}
