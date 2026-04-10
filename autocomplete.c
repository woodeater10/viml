/* POSIX.1-2008 */
#define _POSIX_C_SOURCE 200809L



#include "editor.h"       /* ABuf, EditorConfig, ERow */
#include "autocomplete.h"
#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Open-addressing hash table ──────────────────────────────── */
#define HT_SIZE  1024
#define HT_MASK  (HT_SIZE - 1)

typedef struct { char word[AC_MAX_WORD_LEN]; int freq; } HTSlot;
static HTSlot s_ht[HT_SIZE];

static unsigned int djb2(const char *s, int n) {
    unsigned int h = 5381;
    for (int i = 0; i < n; i++) h = ((h << 5) + h) ^ (unsigned char)s[i];
    return h;
}

static int ht_insert(const char *w, int wl) {
    unsigned int base = djb2(w, wl) & HT_MASK;
    for (int p = 0; p < HT_SIZE; p++) {
        HTSlot *sl = &s_ht[(base + (unsigned)p) & HT_MASK];
        if (sl->freq == 0) {
            memcpy(sl->word, w, (size_t)wl); sl->word[wl] = '\0';
            sl->freq = 1; return 1;
        }
        if (sl->word[wl] == '\0' && memcmp(sl->word, w, (size_t)wl) == 0) {
            sl->freq++; return 1;
        }
    }
    return 0;
}

/* ── Language dict ───────────────────────────────────────────── */
static char s_dict[DICT_MAX_WORDS][AC_MAX_WORD_LEN];
static int  s_dict_count = 0;

static int is_word_char(unsigned char c) { return isalnum(c) || c == '_'; }

/* Try to open a dict file; return FILE* or NULL */
static FILE *dict_fopen(const char *ext) {
    char path[512];

    /* 1. ./dicts/<ext>.dict */
    snprintf(path, sizeof(path), "%s/%s.dict", DICT_PATH_LOCAL, ext);
    FILE *f = fopen(path, "r");
    if (f) return f;

    /* 2. $HOME/.config/viml/<ext>.dict */
    const char *home = getenv("HOME");
    if (home) {
        snprintf(path, sizeof(path), "%s/%s/%s.dict",
                 home, DICT_PATH_HOME, ext);
        f = fopen(path, "r");
        if (f) return f;
    }
    return NULL;
}

void ac_load_dict(const char *filepath) {
    s_dict_count = 0;
    if (!filepath) return;

    /* Extract extension */
    const char *dot = strrchr(filepath, '.');
    if (!dot || dot == filepath || *(dot + 1) == '\0') return;
    const char *ext = dot + 1;

    FILE *f = dict_fopen(ext);
    if (!f) return;

    char line[AC_MAX_WORD_LEN + 4];
    while (s_dict_count < DICT_MAX_WORDS && fgets(line, sizeof(line), f)) {
        /* Strip trailing whitespace */
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'
                           || line[len-1] == ' ' || line[len-1] == '\t'))
            line[--len] = '\0';
        /* Skip blank lines and comments */
        if (len == 0 || line[0] == '#') continue;
        /* Validate: must be a pure word token */
        int valid = 1;
        for (int i = 0; i < len; i++)
            if (!is_word_char((unsigned char)line[i])) { valid = 0; break; }
        if (!valid || len >= AC_MAX_WORD_LEN) continue;

        memcpy(s_dict[s_dict_count++], line, (size_t)(len + 1));
    }
    fclose(f);
}

/* ── Public: lifecycle ───────────────────────────────────────── */

void ac_init(AutoComplete *ac) {
    memset(ac, 0, sizeof(*ac));
    ac->selected = -1;
}

void ac_reset(AutoComplete *ac) {
    ac->count       = 0;
    ac->selected    = -1;
    ac->active      = 0;
    ac->partial_len = 0;
    ac->partial[0]  = '\0';
    ac->anchor_row  = 0;
    ac->anchor_col  = 0;
}

int ac_get_partial(struct EditorConfig *e, char *out, int outlen) {
    if (e->cy >= e->numrows || outlen <= 1) return 0;
    ERow *row   = &e->rows[e->cy];
    int   start = e->cx;
    while (start > 0 && is_word_char((unsigned char)row->chars[start - 1]))
        start--;
    int len = e->cx - start;
    if (len <= 0 || len >= outlen) return 0;
    memcpy(out, row->chars + start, (size_t)len);
    out[len] = '\0';
    return len;
}

/* ── Build ───────────────────────────────────────────────────── */

static int cmp_slot_ptr(const void *a, const void *b) {
    const HTSlot *sa = *(const HTSlot *const *)a;
    const HTSlot *sb = *(const HTSlot *const *)b;
    if (sb->freq != sa->freq) return sb->freq - sa->freq;
    return strcmp(sa->word, sb->word);
}

void ac_build(struct EditorConfig *e, AutoComplete *ac) {
    ac_reset(ac);

    char partial[AC_MAX_WORD_LEN];
    int  plen = ac_get_partial(e, partial, sizeof(partial));
    if (plen < AC_MIN_PARTIAL_LEN) return;

    memcpy(ac->partial, partial, (size_t)(plen + 1));
    ac->partial_len = plen;
    ac->anchor_row  = e->cy;
    ac->anchor_col  = e->cx - plen;

    /* ── Tier 1: buffer scan into hash table ─────────────────── */
    memset(s_ht, 0, sizeof(s_ht));

    for (int ri = 0; ri < e->numrows; ri++) {
        ERow *row = &e->rows[ri]; int j = 0;
        while (j < row->size) {
            if (!is_word_char((unsigned char)row->chars[j])) { j++; continue; }
            int ws = j;
            while (j < row->size && is_word_char((unsigned char)row->chars[j])) j++;
            int wl = j - ws;
            if (wl <= plen || wl >= AC_MAX_WORD_LEN) continue;
            if (memcmp(row->chars + ws, partial, (size_t)plen) != 0) continue;
            if (ri == e->cy && ws == ac->anchor_col) continue;
            ht_insert(row->chars + ws, wl);
        }
    }

    /* Collect & sort HT slots */
    static const HTSlot *matches[HT_SIZE];
    int mc = 0;
    for (int i = 0; i < HT_SIZE; i++)
        if (s_ht[i].freq > 0) matches[mc++] = &s_ht[i];
    qsort(matches, (size_t)mc, sizeof(matches[0]), cmp_slot_ptr);

    int n = mc < AC_MAX_SUGGESTIONS ? mc : AC_MAX_SUGGESTIONS;
    for (int i = 0; i < n; i++) {
        int wl = (int)strlen(matches[i]->word);
        if (wl >= AC_MAX_WORD_LEN) wl = AC_MAX_WORD_LEN - 1;
        memcpy(ac->sugg[i].word, matches[i]->word, (size_t)(wl + 1));
        ac->sugg[i].src = AC_SRC_BUFFER;
    }
    ac->count = n;

    /* ── Tier 2: pad from dict if buffer gave fewer than max ─── */
    if (ac->count < AC_MAX_SUGGESTIONS && s_dict_count > 0) {
        for (int di = 0; di < s_dict_count && ac->count < AC_MAX_SUGGESTIONS; di++) {
            const char *dw  = s_dict[di];
            int          dwl = (int)strlen(dw);
            if (dwl <= plen) continue;
            if (memcmp(dw, partial, (size_t)plen) != 0) continue;
            /* Check not already in sugg[] */
            int dup = 0;
            for (int k = 0; k < ac->count; k++)
                if (strcmp(ac->sugg[k].word, dw) == 0) { dup = 1; break; }
            if (dup) continue;
            memcpy(ac->sugg[ac->count].word, dw, (size_t)(dwl + 1));
            ac->sugg[ac->count].src = AC_SRC_DICT;
            ac->count++;
        }
    }
}

/* ── Apply ───────────────────────────────────────────────────── */

void ac_apply(struct EditorConfig *e, AutoComplete *ac, int idx) {
    if (idx < 0 || idx >= ac->count) return;
    if (e->cy != ac->anchor_row)     return;
    while (e->cx > ac->anchor_col) editor_delete_char(e);
    const char *w = ac->sugg[idx].word;
    int wl = (int)strlen(w);
    for (int i = 0; i < wl; i++) editor_insert_char(e, (unsigned char)w[i]);
}

/* ── Popup renderer ──────────────────────────────────────────── */

static void popup_hline(ABuf *ab, const char *l, const char *r,
                         const char *fill, int inner_w) {
    ab_appendz(ab, C_POPUP_BORDER);
    ab_appendz(ab, l);
    for (int i = 0; i < inner_w; i++) ab_appendz(ab, fill);
    ab_appendz(ab, r);
    ab_appendz(ab, C_RESET);
}


static void popup_goto(ABuf *ab, int row, int col) {
    char esc[24];
    int  n = snprintf(esc, sizeof(esc), "\x1b[%d;%dH", row, col);
    ab_append(ab, esc, n);
}

void ac_draw_popup(const AutoComplete *ac, struct ABuf *ab,
                   int scr_row, int popup_col,
                   int max_rows, int max_cols) {
    if (ac->count == 0) return;

    /* ── Measure inner width ──────────────────────────────────── */
   
    int word_col = 0;
    for (int i = 0; i < ac->count; i++) {
        int wl = (int)strlen(ac->sugg[i].word);
        if (wl > word_col) word_col = wl;
    }
    /* Cap word column so popup never exceeds screencols */
    int max_word_col = max_cols - popup_col - 9;
    if (max_word_col < 4) max_word_col = 4;
    if (word_col > max_word_col) word_col = max_word_col;

    int inner_w = word_col + 7;  /* sel(1)+sp(1)+word+sp(1)+hint(3)+sp(1) */
    int box_w   = inner_w + 2;   /* two BOX_V chars                         */
    int box_h   = ac->count + 2; /* top border + rows + bottom border       */

    /* ── Clamp horizontal ────────────────────────────────────── */
    if (popup_col + box_w > max_cols + 1)
        popup_col = max_cols - box_w + 1;
    if (popup_col < 1) popup_col = 1;

    /* ── Clamp vertical: prefer below, fallback above ────────── */
    int popup_row = scr_row + 1;
    if (popup_row + box_h > max_rows + 1)
        popup_row = scr_row - box_h;
    if (popup_row < 1) popup_row = 1;

    /* ── Draw top border ─────────────────────────────────────── */
    popup_goto(ab, popup_row, popup_col);
    popup_hline(ab, BOX_TL, BOX_TR, BOX_H, inner_w);

    /* ── Draw each suggestion row ────────────────────────────── */
    for (int i = 0; i < ac->count; i++) {
        popup_goto(ab, popup_row + 1 + i, popup_col);

        int is_sel = (ac->active && i == ac->selected);

        /* Left border */
        ab_appendz(ab, C_POPUP_BORDER);
        ab_appendz(ab, BOX_V);
        ab_appendz(ab, C_RESET);

        /* Row color */
        ab_appendz(ab, is_sel ? C_POPUP_SEL : C_POPUP_ITEM);

        /* Selector glyph */
        ab_appendz(ab, is_sel ? BOX_SEL : BOX_NSL);
        ab_appendz(ab, " ");

        /* Word (truncated + padded to word_col) */
        const char *w    = ac->sugg[i].word;
        int         wl   = (int)strlen(w);
        int         show = wl < word_col ? wl : word_col;
        ab_append(ab, w, show);
        for (int s = show; s < word_col; s++) ab_append(ab, " ", 1);

        /* Hint: " buf" or " dic" */
        ab_append(ab, " ", 1);
        ab_appendz(ab, C_POPUP_HINT);
        ab_appendz(ab, ac->sugg[i].src == AC_SRC_DICT ? "dic" : "buf");

        /* Trailing space + right border */
        ab_appendz(ab, is_sel ? C_POPUP_SEL : C_POPUP_ITEM);
        ab_append(ab, " ", 1);
        ab_appendz(ab, C_RESET);

        ab_appendz(ab, C_POPUP_BORDER);
        ab_appendz(ab, BOX_V);
        ab_appendz(ab, C_RESET);
    }

    /* ── Draw bottom border ──────────────────────────────────── */
    popup_goto(ab, popup_row + 1 + ac->count, popup_col);
    popup_hline(ab, BOX_BL, BOX_BR, BOX_H, inner_w);
}
