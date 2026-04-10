#ifndef CONFIG_H
#define CONFIG_H

/* ── Version ─────────────────────────────────────────────────── */
#define VIML_VERSION    "0.1.0"

/* ── Editor behaviour ────────────────────────────────────────── */
#define VIML_TAB_SIZE       4
#define VIML_QUIT_TIMES     2  
#define STATUS_MSG_TIMEOUT  5  
/* ── Autocomplete ────────────────────────────────────────────── */
#define AC_MAX_SUGGESTIONS  5
#define AC_MAX_WORD_LEN     128
#define AC_MIN_PARTIAL_LEN  2  
#define AC_MAX_CANDIDATES   512 

#define C_RESET         "\x1b[0m"
#define C_INVERT        "\x1b[7m"

/* Status bar: NORMAL mode */
#define C_SB_NORMAL     "\x1b[48;5;136m\x1b[38;5;232m"
/* Status bar: INSERT mode */
#define C_SB_INSERT     "\x1b[48;5;64m\x1b[38;5;255m"
/* Status bar: COMMAND mode */
#define C_SB_COMMAND    "\x1b[48;5;24m\x1b[38;5;255m"

/* Autocomplete bar: unselected token */
#define C_AC_TOKEN      "\x1b[38;5;244m"
/* Autocomplete bar: selected/active token */
#define C_AC_SELECT     "\x1b[48;5;24m\x1b[38;5;255m"

/* Tilde gutter and welcome text */
#define C_GUTTER        "\x1b[38;5;240m"

/* ── Popup box ───────────────────────────────────────────────── */
/* Border lines */
#define C_POPUP_BORDER  "\x1b[38;5;240m"
/* Unselected row: dark bg, light fg */
#define C_POPUP_ITEM    "\x1b[48;5;236m\x1b[38;5;252m"
/* Selected row: blue bg, white fg */
#define C_POPUP_SEL     "\x1b[48;5;24m\x1b[38;5;255m"
/* Dim hint showing word origin (dict vs buffer) */
#define C_POPUP_HINT    "\x1b[38;5;239m"

/* Box-drawing characters (ASCII — safe on every terminal) */
#define BOX_TL  "+"
#define BOX_TR  "+"
#define BOX_BL  "+"
#define BOX_BR  "+"
#define BOX_H   "-"
#define BOX_V   "|"
#define BOX_SEL ">"   
#define BOX_NSL " "  

/* ── Language dict loader ────────────────────────────────────── */
#define DICT_MAX_WORDS    2048  
#define DICT_PATH_LOCAL   "dicts"         
#define DICT_PATH_HOME    ".config/viml"   
#endif /* CONFIG_H */
