#include "terminal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct termios g_orig_termios;

/* ── Raw mode ────────────────────────────────────────────────── */

void term_raw_disable(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
}

void term_raw_enable(void) {
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1) {
        perror("tcgetattr"); exit(1);
    }
    atexit(term_raw_disable);

    struct termios raw = g_orig_termios;
    /* Input: disable Ctrl-S/Q flow control, CR→NL translation, break signals */
    raw.c_iflag &= ~(unsigned)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* Output: disable NL→CRNL post-processing */
    raw.c_oflag &= ~(unsigned)(OPOST);
    /* Control: 8-bit chars */
    raw.c_cflag |=  (unsigned)(CS8);
    /* Local: disable echo, canonical, Ctrl-C/Z signals, extended processing */
    raw.c_lflag &= ~(unsigned)(ECHO | ICANON | IEXTEN | ISIG);
    /* Non-blocking read with 100 ms timeout */
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr"); exit(1);
    }
}

/* ── Screen size ─────────────────────────────────────────────── */

int term_get_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1 && ws.ws_col > 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
    /* Fallback: move cursor to extreme bottom-right and query position */
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B\x1b[6n", 16) != 16)
        return -1;
    char buf[32];
    unsigned int i = 0;
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i++] == 'R') break;
    }
    buf[i] = '\0';
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)   return -1;
    return 0;
}

/* ── Key reader ──────────────────────────────────────────────── */

int term_read_key(void) {
    int nread;
    char c;

    while ((nread = (int)read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) { perror("read"); exit(1); }
        return KEY_NULL; /* timeout — let the caller yield */
    }

    /* Escape sequence dispatcher */
    if (c == '\x1b') {
        char seq[4];
        /* If the next bytes don't arrive quickly it's a bare ESC */
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return KEY_ESC;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return KEY_ESC;

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return KEY_ESC;
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return KEY_HOME;
                        case '3': return KEY_DEL;
                        case '4': return KEY_END;
                        case '5': return KEY_PAGE_UP;
                        case '6': return KEY_PAGE_DOWN;
                        case '7': return KEY_HOME;
                        case '8': return KEY_END;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return KEY_ARROW_UP;
                    case 'B': return KEY_ARROW_DOWN;
                    case 'C': return KEY_ARROW_RIGHT;
                    case 'D': return KEY_ARROW_LEFT;
                    case 'H': return KEY_HOME;
                    case 'F': return KEY_END;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
            }
        }
        return KEY_ESC;
    }

    return (unsigned char)c;
}
