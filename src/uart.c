#include "uart.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

/*
 * Kernel termios2 layout (uapi asm-generic/termbits.h). glibc's struct termios and
 * speed_t differ from it, TCGETS2/TCSETS2 with them would fail.
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

/* Not the system TCGETS2: it uses the incomplete struct termios2 from ioctl.h */
#define BENTO_TCGETS2 _IOR('T', 0x2A, struct bento_termios2)
#define BENTO_TCSETS2 _IOW('T', 0x2B, struct bento_termios2)
#ifndef BOTHER
#define BOTHER 0010000
#endif
#ifndef IBSHIFT
#define IBSHIFT 16
#endif

static int set_exact_baud(int fd, int baud)
{
    struct bento_termios2 tio;

    if (ioctl(fd, BENTO_TCGETS2, &tio) < 0)
        return -1;
    /* output and input speed both from c_ospeed/c_ispeed */
    tio.c_cflag &= ~(uint32_t)(CBAUD | (CBAUD << IBSHIFT));
    tio.c_cflag |= BOTHER | (BOTHER << IBSHIFT);
    tio.c_ispeed = (uint32_t)baud;
    tio.c_ospeed = (uint32_t)baud;
    return ioctl(fd, BENTO_TCSETS2, &tio);
}

int uart_open(const char *path, int exact_baud, int baud)
{
    struct termios tty;
    int fd;

    fd = open(path, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "[ERROR] open %s: %s\n", path, strerror(errno));
        return -1;
    }

    if (tcgetattr(fd, &tty) != 0) {
        fprintf(stderr, "[ERROR] tcgetattr %s: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }
    cfmakeraw(&tty);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    cfsetispeed(&tty, B38400);
    cfsetospeed(&tty, B38400);
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "[ERROR] tcsetattr %s: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }

    if (exact_baud && set_exact_baud(fd, baud) != 0) {
        fprintf(stderr, "[ERROR] termios2 %d baud on %s: %s\n", baud, path, strerror(errno));
        close(fd);
        return -1;
    }

    /* Bytes from before the start belong to no message we could still parse */
    tcflush(fd, TCIOFLUSH);
    return fd;
}

int uart_write_all(int fd, const unsigned char *data, size_t len)
{
    size_t written = 0;

    while (written < len) {
        ssize_t n = write(fd, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "[ERROR] write UART: %s\n", strerror(errno));
            return -1;
        }
        written += (size_t)n;
    }
    return 0;
}
