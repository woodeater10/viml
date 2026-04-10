

/* POSIX.1-2008 for sigaction, sigemptyset, SIGWINCH */
#define _POSIX_C_SOURCE 200809L

#include "terminal.h"
#include "editor.h"
#include "config.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global editor state — one instance for the lifetime of the process */
static EditorConfig E;

/* Set by SIGWINCH; polled at the top of the event loop */
static volatile sig_atomic_t g_resize_pending = 0;

static void handle_sigwinch(int sig) {
    (void)sig;
    g_resize_pending = 1;
}

int main(int argc, char *argv[]) {
    term_raw_enable();
    editor_init(&E);

    /* Install resize handler *after* editor_init so screenrows/cols are set */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigwinch;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, NULL);

    /* Ignore SIGPIPE (e.g. if stdout is redirected to a closed pipe) */
    signal(SIGPIPE, SIG_IGN);

    if (argc >= 2) {
        if (editor_open(&E, argv[1]) != 0)
            editor_set_status(&E, "New file: %s  (will be created on :w)", argv[1]);
        else
            editor_set_status(&E, "\"%s\"  %d lines", argv[1], E.numrows);
    } else {
        editor_set_status(&E,
            "viml " VIML_VERSION "  —  i=insert  :w=save  :q=quit  Tab=autocomplete");
    }

    /* ── Event loop ─────────────────────────────────────────── */
    for (;;) {
        /* Handle terminal resize */
        if (g_resize_pending) {
            g_resize_pending = 0;
            if (term_get_size(&E.screenrows, &E.screencols) == 0)
                E.screenrows -= 2; /* status + cmd bar */
        }

        editor_render(&E);

        int key = term_read_key();
        if (key != KEY_NULL)
            editor_process_key(&E, key);
    }

    /* Unreachable — exit happens inside editor_process_key on :q */
    return 0;
}
