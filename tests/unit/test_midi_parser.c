/* Unit tests for src/midi_parser.c, run with: make test (Mac or Pi) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "midi_parser.h"

static midi_parser_t parser;
static char out[65536];      /* emitted messages as hex, separated by " | " */
static int emitted;
static size_t last_len;

static void emit(void *ctx, const unsigned char *msg, size_t len)
{
    size_t pos = strlen(out);

    (void)ctx;
    emitted++;
    last_len = len;
    if (pos)
        pos += (size_t)snprintf(out + pos, sizeof(out) - pos, " | ");
    for (size_t i = 0; i < len && pos < sizeof(out); i++)
        pos += (size_t)snprintf(out + pos, sizeof(out) - pos, i ? " %02X" : "%02X", msg[i]);
}

static void reset(void)
{
    out[0] = '\0';
    emitted = 0;
    last_len = 0;
    midi_parser_init(&parser, emit, NULL);
}

/* Feed hex bytes ("90 3C 40") into a fresh parser, return what came out */
static const char *run(const char *hex)
{
    const char *s = hex;
    char *end;

    reset();
    for (;;) {
        unsigned long byte = strtoul(s, &end, 16);
        if (end == s)
            break;
        midi_parser_feed(&parser, (unsigned char)byte);
        s = end;
    }
    return out;
}

#define EXPECT(input, expected) do { \
        const char *got_ = run(input); \
        check_count++; \
        if (strcmp(got_, expected) != 0) { \
            check_failures++; \
            fprintf(stderr, "FAIL %s:%d: [%s] -> [%s], expected [%s]\n", \
                    __FILE__, __LINE__, input, got_, expected); \
        } \
    } while (0)

static void test_channel_messages(void)
{
    EXPECT("90 3C 40", "90 3C 40");
    EXPECT("80 3C 00", "80 3C 00");
    EXPECT("90 3C 00", "90 3C 00");             /* the parser does not turn it into Note Off */
    EXPECT("A0 3C 20", "A0 3C 20");
    EXPECT("B0 07 64", "B0 07 64");
    EXPECT("C0 05", "C0 05");
    EXPECT("D0 30", "D0 30");
    EXPECT("E0 00 40", "E0 00 40");
    EXPECT("9F 7F 7F", "9F 7F 7F");
}

static void test_running_status(void)
{
    EXPECT("90 3C 40 3E 41", "90 3C 40 | 90 3E 41");
    EXPECT("B0 07 64 0A 40", "B0 07 64 | B0 0A 40");
    EXPECT("C0 05 06", "C0 05 | C0 06");
    /* a new status replaces an incomplete message */
    EXPECT("90 3C 80 3C 00", "80 3C 00");
    /* data without any status is dropped */
    EXPECT("3C 40 90 3C 40", "90 3C 40");
    CHECK(parser.dropped_bytes == 2);
}

static void test_realtime(void)
{
    EXPECT("F8 FA FB FC FE FF", "F8 | FA | FB | FC | FE | FF");
    /* real-time inside a message does not break it */
    EXPECT("90 F8 3C F8 40", "F8 | F8 | 90 3C 40");
    EXPECT("90 3C 40 F8 3E 40", "90 3C 40 | F8 | 90 3E 40");
    /* undefined real-time bytes are dropped */
    EXPECT("F9 FD", "");
    CHECK(parser.dropped_bytes == 2);
}

static void test_system_common(void)
{
    EXPECT("F1 23", "F1 23");
    EXPECT("F2 10 20", "F2 10 20");
    EXPECT("F3 05", "F3 05");
    EXPECT("F6", "F6");
    /* system common clears running status */
    EXPECT("90 3C 40 F2 01 02 3E 40", "90 3C 40 | F2 01 02");
    CHECK(parser.dropped_bytes == 2);
    EXPECT("90 3C 40 F6 3E 40", "90 3C 40 | F6");
    CHECK(parser.dropped_bytes == 2);
    /* undefined F4, F5 and a stray F7 are dropped and clear running status */
    EXPECT("F7 F4 F5", "");
    CHECK(parser.dropped_bytes == 3);
    EXPECT("90 3C F4 3E 40", "");
    CHECK(parser.dropped_bytes == 3);
}

static void test_sysex(void)
{
    EXPECT("F0 7D 01 02 F7", "F0 7D 01 02 F7");
    EXPECT("F0 F7", "F0 F7");
    /* real-time inside SysEx comes out first, the SysEx stays whole */
    EXPECT("F0 7D F8 01 F7", "F8 | F0 7D 01 F7");
    /* another status ends a SysEx without F7: dropped, the status counts */
    EXPECT("F0 7D 01 90 3C 40", "90 3C 40");
    CHECK(parser.dropped_sysex == 1);
    EXPECT("F0 7D 01 F0 7D 02 F7", "F0 7D 02 F7");
    CHECK(parser.dropped_sysex == 1);
    /* SysEx clears running status */
    EXPECT("90 3C 40 F0 7D F7 3E 40", "90 3C 40 | F0 7D F7");
    CHECK(parser.dropped_bytes == 2);
    /* back to back */
    EXPECT("F0 01 F7 F0 02 F7", "F0 01 F7 | F0 02 F7");
}

static void feed_sysex(size_t data_bytes)
{
    reset();
    midi_parser_feed(&parser, 0xF0);
    for (size_t i = 0; i < data_bytes; i++)
        midi_parser_feed(&parser, 0x01);
    midi_parser_feed(&parser, 0xF7);
}

static void test_sysex_limit(void)
{
    /* F0 + data + F7 = MIDI_PARSER_SYSEX_MAX: still fits */
    feed_sysex(MIDI_PARSER_SYSEX_MAX - 2);
    CHECK(emitted == 1);
    CHECK(last_len == MIDI_PARSER_SYSEX_MAX);
    CHECK(parser.sysex[0] == 0xF0 && parser.sysex[MIDI_PARSER_SYSEX_MAX - 1] == 0xF7);

    /* one byte more: dropped as a whole */
    feed_sysex(MIDI_PARSER_SYSEX_MAX - 1);
    CHECK(emitted == 0);
    CHECK(parser.dropped_sysex == 1);

    /* and the parser goes on normally */
    midi_parser_feed(&parser, 0x90);
    midi_parser_feed(&parser, 0x3C);
    midi_parser_feed(&parser, 0x40);
    CHECK(emitted == 1 && last_len == 3);
}

int main(void)
{
    test_channel_messages();
    test_running_status();
    test_realtime();
    test_system_common();
    test_sysex();
    test_sysex_limit();
    return check_report("test_midi_parser");
}
