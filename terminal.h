#ifndef TERMINAL_H
#define TERMINAL_H

#include <termios.h>

/* Saved state for atexit restoration */
extern struct termios g_orig_termios;

void term_raw_enable(void);
void term_raw_disable(void);
int  term_get_size(int *rows, int *cols); /* returns 0 on success, -1 on fail */
int  term_read_key(void);                /* blocks up to 100 ms; KEY_NULL = timeout */

/* ── Key code constants ──────────────────────────────────────── */
enum {
    KEY_NULL       = 0,
    KEY_CTRL_H     = 8,
    KEY_TAB        = 9,
    KEY_ENTER      = 13,
    KEY_ESC        = 27,
    KEY_BACKSPACE  = 127,

    /* Extended keys live above 127 to avoid collision with printable ASCII */
    KEY_ARROW_LEFT  = 1000,
    KEY_ARROW_RIGHT,
    KEY_ARROW_UP,
    KEY_ARROW_DOWN,
    KEY_DEL,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
};

#endif /* TERMINAL_H */
