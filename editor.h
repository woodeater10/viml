#ifndef EDITOR_H
#define EDITOR_H

#include "config.h"
#include "autocomplete.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


typedef struct ABuf { char *b; int len; } ABuf;
#define ABUF_INIT {NULL, 0}

static inline void ab_append(ABuf *ab, const char *s, int len) {
    if (len <= 0) return;
    char *p = realloc(ab->b, (size_t)(ab->len + len));
    if (!p) { perror("realloc"); exit(1); }
    memcpy(p + ab->len, s, (size_t)len);
    ab->b    = p;
    ab->len += len;
}
static inline void ab_appendz(ABuf *ab, const char *s) {
    ab_append(ab, s, (int)strlen(s));
}
static inline void ab_free(ABuf *ab) {
    free(ab->b); ab->b = NULL; ab->len = 0;
}

/* ── Vim mode state machine ──────────────────────────────────── */
typedef enum {
    MODE_NORMAL  = 0,
    MODE_INSERT  = 1,
    MODE_COMMAND = 2,
} EditorMode;

/* ── Buffer row ──────────────────────────────────────────────── */
typedef struct {
    int   size;    /* raw chars length                   */
    int   rsize;   /* rendered length (tabs expanded)    */
    char *chars;   /* raw data                           */
    char *render;  /* display data (heap, tabs→spaces)   */
} ERow;

/* ── Central editor state ────────────────────────────────────── */
#define STATUS_MSG_LEN 256

typedef struct EditorConfig {
    int cx, cy;             /* cursor in file coordinates              */
    int rx;                 /* cursor rendered column                  */
    int rowoff, coloff;     /* viewport offsets                        */
    int screenrows;         /* usable rows (total - 2 for status bars) */
    int screencols;
    int   numrows;
    ERow *rows;
    EditorMode mode;
    int   dirty;
    char *filename;
    char   statusmsg[STATUS_MSG_LEN];
    time_t statusmsg_time;
    char cmdbuf[STATUS_MSG_LEN];
    int  cmdlen;
    char pending_op;
    int  quit_times;
    AutoComplete ac;
} EditorConfig;

/* ── Lifecycle ───────────────────────────────────────────────── */
void editor_init(EditorConfig *e);
void editor_free(EditorConfig *e);
int  editor_open(EditorConfig *e, const char *filename);
int  editor_save(EditorConfig *e);

/* ── Buffer operations ───────────────────────────────────────── */
void editor_insert_row      (EditorConfig *e, int at, const char *s, int len);
void editor_delete_row      (EditorConfig *e, int at);
void editor_row_insert_char (EditorConfig *e, ERow *row, int at, int c);
void editor_row_delete_char (EditorConfig *e, ERow *row, int at);
void editor_row_append_str  (EditorConfig *e, ERow *row, const char *s, int len);
void editor_update_row      (ERow *row);

/* ── High-level editing ──────────────────────────────────────── */
void editor_insert_char   (EditorConfig *e, int c);
void editor_insert_newline(EditorConfig *e);
void editor_delete_char   (EditorConfig *e);
void editor_delete_line   (EditorConfig *e);

/* ── Rendering ───────────────────────────────────────────────── */
void editor_render    (EditorConfig *e);
void editor_set_status(EditorConfig *e, const char *fmt, ...);

/* ── Keystroke dispatcher ────────────────────────────────────── */
void editor_process_key(EditorConfig *e, int key);

/* ── Cursor / scroll ─────────────────────────────────────────── */
int  editor_row_cx_to_rx(ERow *row, int cx);
int  editor_row_rx_to_cx(ERow *row, int rx);
void editor_scroll(EditorConfig *e);

#endif /* EDITOR_H */
