/*
 * The MIDI UART: /dev/ttyAMA0 on GPIO14/15 of the Pi 4 and Pi 5, raw 8N1.
 * On the Pi 4 it is only there with dtoverlay=disable-bt, on the Pi 5 /dev/serial0
 * is the debug UART (ttyAMA10) and not this one.
 *
 * Default: termios B38400. The overlays midi-uart0 (Pi 4) and midi-uart0-pi5 (Pi 5)
 * set the UART clock so that B38400 gives 31250 baud on the wire. exact_baud sets
 * the given rate with termios2/BOTHER instead, only for a UART without such an overlay.
 */
#ifndef BENTO_TTYMIDI_UART_H
#define BENTO_TTYMIDI_UART_H

#include <stddef.h>

/* Returns the file descriptor, or -1 after printing the reason */
int uart_open(const char *path, int exact_baud, int baud);

/* Writes everything, returns 0 or -1 */
int uart_write_all(int fd, const unsigned char *data, size_t len);

#endif
