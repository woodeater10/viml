#ifndef AUTOCOMPLETE_H
#define AUTOCOMPLETE_H

#include "config.h"

/* ── Candidate source tag ────────────────────────────────────── */
typedef enum {
    AC_SRC_BUFFER = 0,  /* word found in the open file          */
    AC_SRC_DICT   = 1,  /* word loaded from a .dict file        */
} ACSrc;

/* ── Per-suggestion metadata ─────────────────────────────────── */
typedef struct {
    char  word[AC_MAX_WORD_LEN];
    ACSrc src;
} ACSuggestion;

/* ── AutoComplete state ──────────────────────────────────────── */
typedef struct {
    ACSuggestion sugg[AC_MAX_SUGGESTIONS]; 
    int  count;     
    int  selected;  
    int  active;     

    char partial[AC_MAX_WORD_LEN]; 
    int  partial_len;

    
    int  anchor_row;
    int  anchor_col;  
} AutoComplete;

/* Forward declarations */
struct EditorConfig;
struct ABuf;           

/* ── Lifecycle ───────────────────────────────────────────────── */
void ac_init (AutoComplete *ac);
void ac_reset(AutoComplete *ac);


void ac_load_dict(const char *filepath);


int ac_get_partial(struct EditorConfig *e, char *out, int outlen);


void ac_build(struct EditorConfig *e, AutoComplete *ac);


void ac_apply(struct EditorConfig *e, AutoComplete *ac, int idx);


void ac_draw_popup(const AutoComplete *ac, struct ABuf *ab,
                   int scr_row, int popup_col,
                   int max_rows, int max_cols);

#endif /* AUTOCOMPLETE_H */
