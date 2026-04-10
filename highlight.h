#ifndef HIGHLIGHT_H
#define HIGHLIGHT_H
#include "editor.h"

#define HL_NONE 0
#define HL_C    1
#define HL_PY   2
#define HL_JS   3
#define HL_SH   4

int  hl_get_type(const char *filename);
void hl_render_line(ABuf *ab, const char *s, int len, int hl_type);
#endif
