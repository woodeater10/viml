/* POSIX.1-2008: strdup, getline */
#define _POSIX_C_SOURCE 200809L



#include "editor.h"
#include "terminal.h"
#include "autocomplete.h"
#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static inline void xwrite(int fd, const char *s, size_t n) {
    ssize_t r = write(fd, s, n); (void)r;
}


void editor_init(EditorConfig *e) {
    memset(e, 0, sizeof(*e));
    e->mode       = MODE_NORMAL;
    e->quit_times = VIML_QUIT_TIMES;
    ac_init(&e->ac);
    if (term_get_size(&e->screenrows, &e->screencols) == -1) {
        perror("term_get_size"); exit(1);
    }
    e->screenrows -= 2;
}

void editor_free(EditorConfig *e) {
    for (int i = 0; i < e->numrows; i++) {
        free(e->rows[i].chars);
        free(e->rows[i].render);
    }
    free(e->rows);
    free(e->filename);
    e->rows = NULL; e->filename = NULL; e->numrows = 0;
}


void editor_update_row(ERow *row) {
    int tabs = 0;
    for (int j = 0; j < row->size; j++) if (row->chars[j] == '\t') tabs++;
    free(row->render);
    row->render = malloc((size_t)(row->size + tabs * (VIML_TAB_SIZE - 1) + 1));
    if (!row->render) { perror("malloc"); exit(1); }
    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % VIML_TAB_SIZE) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

int editor_row_cx_to_rx(ERow *row, int cx) {
    int rx = 0;
    for (int j = 0; j < cx && j < row->size; j++) {
        if (row->chars[j] == '\t') rx += (VIML_TAB_SIZE - 1) - (rx % VIML_TAB_SIZE);
        rx++;
    }
    return rx;
}

int editor_row_rx_to_cx(ERow *row, int rx) {
    int cur = 0, cx;
    for (cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t') cur += (VIML_TAB_SIZE - 1) - (cur % VIML_TAB_SIZE);
        cur++;
        if (cur > rx) return cx;
    }
    return cx;
}

void editor_insert_row(EditorConfig *e, int at, const char *s, int len) {
    if (at < 0 || at > e->numrows) return;
    e->rows = realloc(e->rows, sizeof(ERow) * (size_t)(e->numrows + 1));
    if (!e->rows) { perror("realloc"); exit(1); }
    memmove(&e->rows[at + 1], &e->rows[at], sizeof(ERow) * (size_t)(e->numrows - at));
    e->rows[at].size  = len;
    e->rows[at].chars = malloc((size_t)(len + 1));
    if (!e->rows[at].chars) { perror("malloc"); exit(1); }
    memcpy(e->rows[at].chars, s, (size_t)len);
    e->rows[at].chars[len] = '\0';
    e->rows[at].render = NULL;
    e->rows[at].rsize  = 0;
    editor_update_row(&e->rows[at]);
    e->numrows++;
    e->dirty++;
}

void editor_delete_row(EditorConfig *e, int at) {
    if (at < 0 || at >= e->numrows) return;
    free(e->rows[at].chars);
    free(e->rows[at].render);
    memmove(&e->rows[at], &e->rows[at + 1],
            sizeof(ERow) * (size_t)(e->numrows - at - 1));
    e->numrows--;
    e->dirty++;
}

void editor_row_insert_char(EditorConfig *e, ERow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    row->chars = realloc(row->chars, (size_t)(row->size + 2));
    if (!row->chars) { perror("realloc"); exit(1); }
    memmove(&row->chars[at + 1], &row->chars[at], (size_t)(row->size - at + 1));
    row->chars[at] = (char)c;
    row->size++;
    editor_update_row(row);
    e->dirty++;
}

void editor_row_delete_char(EditorConfig *e, ERow *row, int at) {
    if (at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], (size_t)(row->size - at));
    row->size--;
    editor_update_row(row);
    e->dirty++;
}

void editor_row_append_str(EditorConfig *e, ERow *row, const char *s, int len) {
    row->chars = realloc(row->chars, (size_t)(row->size + len + 1));
    if (!row->chars) { perror("realloc"); exit(1); }
    memcpy(&row->chars[row->size], s, (size_t)len);
    row->size += len;
    row->chars[row->size] = '\0';
    editor_update_row(row);
    e->dirty++;
}



int editor_open(EditorConfig *e, const char *filename) {
    free(e->filename);
    e->filename = strdup(filename);

    /* Load language dict for this file extension */
    ac_load_dict(filename);

    FILE *fp = fopen(filename, "r");
    if (!fp) return -1;

    char  *line    = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 &&
               (line[linelen-1] == '\n' || line[linelen-1] == '\r'))
            linelen--;
        editor_insert_row(e, e->numrows, line, (int)linelen);
    }
    free(line);
    fclose(fp);
    e->dirty = 0;
    return 0;
}

int editor_save(EditorConfig *e) {
    if (!e->filename) return -1;
    int total = 0;
    for (int j = 0; j < e->numrows; j++) total += e->rows[j].size + 1;
    char *buf = malloc((size_t)total);
    if (!buf) return -1;
    char *p = buf;
    for (int j = 0; j < e->numrows; j++) {
        memcpy(p, e->rows[j].chars, (size_t)e->rows[j].size);
        p += e->rows[j].size;
        *p++ = '\n';
    }
    int fd = open(e->filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) { free(buf); return -1; }
    ssize_t written = write(fd, buf, (size_t)total);
    close(fd); free(buf);
    if (written != (ssize_t)total) return -1;
    e->dirty = 0;
    return total;
}



void editor_insert_char(EditorConfig *e, int c) {
    if (e->cy == e->numrows) editor_insert_row(e, e->numrows, "", 0);
    editor_row_insert_char(e, &e->rows[e->cy], e->cx, c);
    e->cx++;
}

void editor_insert_newline(EditorConfig *e) {
    if (e->cx == 0) {
        editor_insert_row(e, e->cy, "", 0);
    } else {
        ERow *row = &e->rows[e->cy];
        editor_insert_row(e, e->cy + 1, &row->chars[e->cx], row->size - e->cx);
        row = &e->rows[e->cy];
        row->size = e->cx; row->chars[row->size] = '\0';
        editor_update_row(row);
    }
    e->cy++; e->cx = 0;
}

void editor_delete_char(EditorConfig *e) {
    if (e->cy == e->numrows) return;
    if (e->cx == 0 && e->cy == 0) return;
    if (e->cx > 0) {
        editor_row_delete_char(e, &e->rows[e->cy], e->cx - 1);
        e->cx--;
    } else {
        e->cx = e->rows[e->cy - 1].size;
        editor_row_append_str(e, &e->rows[e->cy - 1],
                              e->rows[e->cy].chars, e->rows[e->cy].size);
        editor_delete_row(e, e->cy);
        e->cy--;
    }
}

void editor_delete_line(EditorConfig *e) {
    if (e->numrows == 0) return;
    if (e->numrows == 1) {
        ERow *row = &e->rows[0];
        free(row->chars); row->chars = strdup(""); row->size = 0;
        editor_update_row(row); e->cx = 0; e->dirty++;
        return;
    }
    editor_delete_row(e, e->cy);
    if (e->cy >= e->numrows) e->cy = e->numrows - 1;
    e->cx = 0;
}


void editor_scroll(EditorConfig *e) {
    e->rx = 0;
    if (e->cy < e->numrows)
        e->rx = editor_row_cx_to_rx(&e->rows[e->cy], e->cx);
    if (e->cy < e->rowoff) e->rowoff = e->cy;
    if (e->cy >= e->rowoff + e->screenrows) e->rowoff = e->cy - e->screenrows + 1;
    if (e->rx < e->coloff) e->coloff = e->rx;
    if (e->rx >= e->coloff + e->screencols) e->coloff = e->rx - e->screencols + 1;
}



void editor_set_status(EditorConfig *e, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(e->statusmsg, STATUS_MSG_LEN, fmt, ap);
    va_end(ap);
    e->statusmsg_time = time(NULL);
}



static void draw_rows(EditorConfig *e, ABuf *ab) {
    for (int y = 0; y < e->screenrows; y++) {
        int filerow = y + e->rowoff;
        if (filerow >= e->numrows) {
            if (e->numrows == 0 && y == e->screenrows / 3) {
                char msg[80];
                int mlen = snprintf(msg, sizeof(msg),
                    "viml " VIML_VERSION "  —  :w save  :q quit  i insert  Tab autocomplete");
                if (mlen > e->screencols) mlen = e->screencols;
                int pad = (e->screencols - mlen) / 2;
                ab_appendz(ab, C_GUTTER);
                if (pad > 0) { ab_append(ab, "~", 1); pad--; }
                while (pad-- > 0) ab_append(ab, " ", 1);
                ab_appendz(ab, C_RESET);
                ab_append(ab, msg, mlen);
            } else {
                ab_appendz(ab, C_GUTTER);
                ab_append(ab, "~", 1);
                ab_appendz(ab, C_RESET);
            }
        } else {
            ERow *row = &e->rows[filerow];
            int   len = row->rsize - e->coloff;
            if (len < 0) len = 0;
            if (len > e->screencols) len = e->screencols;
            ab_append(ab, row->render + e->coloff, len);
        }
        ab_append(ab, "\x1b[K\r\n", 5);
    }
}

static void draw_status_bar(EditorConfig *e, ABuf *ab) {
    switch (e->mode) {
        case MODE_INSERT:  ab_appendz(ab, C_SB_INSERT);  break;
        case MODE_COMMAND: ab_appendz(ab, C_SB_COMMAND); break;
        default:           ab_appendz(ab, C_SB_NORMAL);  break;
    }
    const char *ml;
    switch (e->mode) {
        case MODE_INSERT:  ml = " INSERT "; break;
        case MODE_COMMAND: ml = " COMMAND"; break;
        default:           ml = " NORMAL "; break;
    }
    char left[128], right[64];
    int llen = snprintf(left,  sizeof(left),  "%s │ %.30s%s",
                        ml, e->filename ? e->filename : "[No Name]",
                        e->dirty ? " [+]" : "");
    int rlen = snprintf(right, sizeof(right), "%d:%d  %d%%",
                        e->cy + 1, e->cx + 1,
                        e->numrows > 0 ? (e->cy + 1) * 100 / e->numrows : 100);
    if (llen > e->screencols) llen = e->screencols;
    ab_append(ab, left, llen);
    int pad = e->screencols - llen - rlen;
    while (pad-- > 0) ab_append(ab, " ", 1);
    if (llen + rlen <= e->screencols) ab_append(ab, right, rlen);
    ab_appendz(ab, C_RESET "\r\n");
}

static void draw_cmd_bar(EditorConfig *e, ABuf *ab) {
    ab_append(ab, "\x1b[K", 3);
    if (e->mode == MODE_COMMAND) {
        ab_append(ab, ":", 1);
        int clen = e->cmdlen < e->screencols - 1 ? e->cmdlen : e->screencols - 1;
        if (clen > 0) ab_append(ab, e->cmdbuf, clen);
        return;
    }
    if (e->statusmsg[0] &&
        (time(NULL) - e->statusmsg_time) < STATUS_MSG_TIMEOUT) {
        int len = (int)strlen(e->statusmsg);
        if (len > e->screencols) len = e->screencols;
        ab_append(ab, e->statusmsg, len);
    }
}

void editor_render(EditorConfig *e) {
    editor_scroll(e);

    ABuf ab = ABUF_INIT;
    ab_append(&ab, "\x1b[?25l", 6); /* hide cursor   */
    ab_append(&ab, "\x1b[H",    3); /* cursor → home */

    draw_rows      (e, &ab);
    draw_status_bar(e, &ab);
    draw_cmd_bar   (e, &ab);

   
    if (e->mode == MODE_INSERT && e->ac.count > 0) {
        int scr_row = (e->cy - e->rowoff) + 1;

       
        int popup_col = (e->rx - e->ac.partial_len) - e->coloff + 1;
        if (popup_col < 1) popup_col = 1;

        ac_draw_popup(&e->ac, &ab,
                      scr_row, popup_col,
                      e->screenrows, e->screencols);
    }

    /* Restore hardware cursor to edit position */
    char pos[32]; int pr, pc;
    if (e->mode == MODE_COMMAND) {
        pr = e->screenrows + 2; pc = e->cmdlen + 2;
    } else {
        pr = (e->cy - e->rowoff) + 1;
        pc = (e->rx - e->coloff) + 1;
    }
    int plen = snprintf(pos, sizeof(pos), "\x1b[%d;%dH", pr, pc);
    ab_append(&ab, pos, plen);
    ab_append(&ab, "\x1b[?25h", 6); /* show cursor   */

    xwrite(STDOUT_FILENO, ab.b, (size_t)ab.len);
    ab_free(&ab);
}



static void execute_command(EditorConfig *e) {
    const char *cmd = e->cmdbuf;
    if (strcmp(cmd, "w") == 0 || strcmp(cmd, "write") == 0) {
        int n = editor_save(e);
        if (n < 0) editor_set_status(e, "Save failed: %s", strerror(errno));
        else        editor_set_status(e, "Wrote %d bytes to \"%s\"", n, e->filename);
    } else if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
        if (e->dirty && e->quit_times > 0) {
            editor_set_status(e,
                "Unsaved changes — :q! to force, :wq to save+quit  (%d left)",
                e->quit_times--);
            e->mode = MODE_NORMAL; e->cmdlen = 0; return;
        }
        goto do_quit;
    } else if (strcmp(cmd, "q!") == 0) {
        goto do_quit;
    } else if (strcmp(cmd, "wq") == 0 || strcmp(cmd, "x") == 0) {
        editor_save(e); goto do_quit;
    } else {
        editor_set_status(e, "Unknown command: :%s", cmd);
    }
    e->mode = MODE_NORMAL; e->cmdlen = 0; e->cmdbuf[0] = '\0';
    return;
do_quit:
    xwrite(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
    editor_free(e); exit(0);
}



static void move_cursor(EditorConfig *e, int key) {
    ERow *row = (e->cy < e->numrows) ? &e->rows[e->cy] : NULL;
    switch (key) {
        case 'h': case KEY_ARROW_LEFT:
            if (e->cx > 0) e->cx--;
            else if (e->cy > 0) { e->cy--; e->cx = e->rows[e->cy].size; }
            break;
        case 'l': case KEY_ARROW_RIGHT:
            if (row) {
                int max = (e->mode == MODE_NORMAL && row->size > 0)
                          ? row->size - 1 : row->size;
                if (e->cx < max) e->cx++;
            }
            break;
        case 'k': case KEY_ARROW_UP:    if (e->cy > 0) e->cy--;  break;
        case 'j': case KEY_ARROW_DOWN:  if (e->cy < e->numrows - 1) e->cy++; break;
        case KEY_HOME: e->cx = 0; break;
        case KEY_END:
            if (row && row->size > 0)
                e->cx = (e->mode == MODE_NORMAL) ? row->size - 1 : row->size;
            break;
        case KEY_PAGE_UP: {
            e->cy = e->rowoff;
            int t = e->screenrows; while (t-- > 0) move_cursor(e, 'k'); break;
        }
        case KEY_PAGE_DOWN: {
            e->cy = e->rowoff + e->screenrows - 1;
            if (e->cy >= e->numrows) e->cy = e->numrows - 1;
            int t = e->screenrows; while (t-- > 0) move_cursor(e, 'j'); break;
        }
    }
    row = (e->cy < e->numrows) ? &e->rows[e->cy] : NULL;
    int rowlen = row ? row->size : 0;
    int maxcx  = (e->mode == MODE_NORMAL && rowlen > 0) ? rowlen - 1 : rowlen;
    if (e->cx > maxcx) e->cx = maxcx;
    if (e->cx < 0)     e->cx = 0;
}


void editor_process_key(EditorConfig *e, int key) {

    /* ── NORMAL ───────────────────────────────────────────────── */
    if (e->mode == MODE_NORMAL) {
        e->quit_times = VIML_QUIT_TIMES;
        switch (key) {
            case 'h': case 'j': case 'k': case 'l':
            case KEY_ARROW_LEFT: case KEY_ARROW_RIGHT:
            case KEY_ARROW_UP:   case KEY_ARROW_DOWN:
            case KEY_HOME: case KEY_END:
            case KEY_PAGE_UP: case KEY_PAGE_DOWN:
                move_cursor(e, key); e->pending_op = 0; break;
            case '0': case '^': e->cx = 0; e->pending_op = 0; break;
            case '$':
                if (e->cy < e->numrows && e->rows[e->cy].size > 0)
                    e->cx = e->rows[e->cy].size - 1;
                e->pending_op = 0; break;
            case 'g':
                if (e->pending_op == 'g') { e->cy = 0; e->cx = 0; e->pending_op = 0; }
                else e->pending_op = 'g';
                break;
            case 'G':
                if (e->numrows > 0) { e->cy = e->numrows - 1; e->cx = 0; }
                e->pending_op = 0; break;
            case 'i': e->mode = MODE_INSERT; e->pending_op = 0; break;
            case 'I': e->cx = 0; e->mode = MODE_INSERT; e->pending_op = 0; break;
            case 'a':
                if (e->cy < e->numrows && e->rows[e->cy].size > 0) e->cx++;
                e->mode = MODE_INSERT; e->pending_op = 0; break;
            case 'A':
                if (e->cy < e->numrows) e->cx = e->rows[e->cy].size;
                e->mode = MODE_INSERT; e->pending_op = 0; break;
            case 'o':
                if (e->cy < e->numrows) e->cx = e->rows[e->cy].size;
                editor_insert_newline(e);
                e->mode = MODE_INSERT; e->pending_op = 0; break;
            case 'O':
                e->cx = 0;
                editor_insert_row(e, e->cy, "", 0);
                e->mode = MODE_INSERT; e->pending_op = 0; break;
            case ':':
                e->mode = MODE_COMMAND;
                e->cmdlen = 0; e->cmdbuf[0] = '\0';
                e->pending_op = 0; break;
            case 'x':
                if (e->cy < e->numrows && e->cx < e->rows[e->cy].size) {
                    editor_row_delete_char(e, &e->rows[e->cy], e->cx);
                    ERow *r = &e->rows[e->cy];
                    if (r->size > 0 && e->cx >= r->size) e->cx = r->size - 1;
                }
                e->pending_op = 0; break;
            case 'd':
                if (e->pending_op == 'd') { editor_delete_line(e); e->pending_op = 0; }
                else e->pending_op = 'd';
                break;
            default: e->pending_op = 0; break;
        }
        return;
    }

    /* ── INSERT ───────────────────────────────────────────────── */
    if (e->mode == MODE_INSERT) {
        switch (key) {
            case KEY_ESC:
                ac_reset(&e->ac);
                e->mode = MODE_NORMAL;
                if (e->cx > 0) e->cx--;
                break;

            case KEY_ENTER:
                ac_reset(&e->ac);
                editor_insert_newline(e);
                break;

            case KEY_BACKSPACE: case KEY_CTRL_H:
                ac_reset(&e->ac);
                editor_delete_char(e);
                break;

            case KEY_DEL:
                ac_reset(&e->ac);
                if (e->cy < e->numrows && e->cx < e->rows[e->cy].size)
                    editor_row_delete_char(e, &e->rows[e->cy], e->cx);
                break;

            /* ── Tab-cycling state machine ─────────────────────── */
            case KEY_TAB:
                if (!e->ac.active) {
                    /* First Tab: rebuild to capture latest typing, then apply [0] */
                    ac_build(e, &e->ac);
                    if (e->ac.count > 0) {
                        e->ac.active   = 1;
                        e->ac.selected = 0;
                        ac_apply(e, &e->ac, 0);
                    } else {
                        editor_insert_char(e, '\t'); /* fallback: literal tab */
                    }
                } else {
                    /* Subsequent Tab: cycle to next candidate */
                    int next = (e->ac.selected + 1) % e->ac.count;
                    e->ac.selected = next;
                    ac_apply(e, &e->ac, next);
                }
                break;

            case KEY_ARROW_LEFT: case KEY_ARROW_RIGHT:
            case KEY_ARROW_UP:   case KEY_ARROW_DOWN:
            case KEY_HOME: case KEY_END:
                ac_reset(&e->ac);
                move_cursor(e, key);
                break;

            default:
                if (key >= 32 && key < 127) {
                    if (e->ac.active) ac_reset(&e->ac); /* confirm silently */
                    editor_insert_char(e, key);
                    /* Auto-rebuild: show popup as user types */
                    {
                        char partial[AC_MAX_WORD_LEN];
                        int  plen = ac_get_partial(e, partial, sizeof(partial));
                        if (plen >= AC_MIN_PARTIAL_LEN) ac_build(e, &e->ac);
                        else                            ac_reset(&e->ac);
                    }
                }
                break;
        }
        return;
    }

    /* ── COMMAND ──────────────────────────────────────────────── */
    if (e->mode == MODE_COMMAND) {
        switch (key) {
            case KEY_ESC:
                e->mode = MODE_NORMAL; e->cmdlen = 0; e->cmdbuf[0] = '\0'; break;
            case KEY_ENTER:
                execute_command(e); break;
            case KEY_BACKSPACE: case KEY_CTRL_H:
                if (e->cmdlen > 0) e->cmdbuf[--e->cmdlen] = '\0';
                else               e->mode = MODE_NORMAL;
                break;
            default:
                if (key >= 32 && key < 127 &&
                    e->cmdlen < (int)sizeof(e->cmdbuf) - 1) {
                    e->cmdbuf[e->cmdlen++] = (char)key;
                    e->cmdbuf[e->cmdlen]   = '\0';
                }
                break;
        }
    }
}
