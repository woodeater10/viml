#define _POSIX_C_SOURCE 200809L
#include "highlight.h"
#include "config.h"
#include <string.h>
#include <ctype.h>

int hl_get_type(const char *filename) {
    if (!filename) return HL_NONE;
    const char *dot = strrchr(filename, '.');
    if (!dot) return HL_NONE;
    dot++;
    if (!strcmp(dot,"c")||!strcmp(dot,"h")||!strcmp(dot,"cc")||
        !strcmp(dot,"cpp")||!strcmp(dot,"hpp")) return HL_C;
    if (!strcmp(dot,"py")) return HL_PY;
    if (!strcmp(dot,"js")||!strcmp(dot,"ts")) return HL_JS;
    if (!strcmp(dot,"sh")) return HL_SH;
    return HL_NONE;
}

static const char *c_kw[] = {
    "auto","break","case","char","const","continue","default","do","double",
    "else","enum","extern","float","for","goto","if","inline","int","long",
    "register","restrict","return","short","signed","sizeof","static","struct",
    "switch","typedef","union","unsigned","void","volatile","while",
    "NULL","true","false","define","include","ifndef","ifdef","endif","pragma",NULL
};
static const char *py_kw[] = {
    "and","as","assert","async","await","break","class","continue","def","del",
    "elif","else","except","finally","for","from","global","if","import","in",
    "is","lambda","not","or","pass","raise","return","try","while","with",
    "yield","True","False","None",NULL
};
static const char *js_kw[] = {
    "break","case","catch","class","const","continue","default","delete","do",
    "else","export","extends","false","finally","for","function","if","import",
    "in","instanceof","let","new","null","return","static","super","switch",
    "this","throw","true","try","typeof","undefined","var","void","while",
    "with","yield","async","await",NULL
};
static const char *sh_kw[] = {
    "if","then","else","elif","fi","for","while","do","done","case","esac",
    "function","in","return","export","local","echo","source","alias",NULL
};

static int is_kw(const char *s, int n, const char **kws) {
    for (int i = 0; kws[i]; i++) {
        int kn = (int)strlen(kws[i]);
        if (kn == n && !strncmp(s, kws[i], (size_t)n)) return 1;
    }
    return 0;
}

void hl_render_line(ABuf *ab, const char *s, int len, int hl_type) {
    if (len <= 0) return;
    if (hl_type == HL_NONE) { ab_append(ab, s, len); return; }

    const char **kws = hl_type==HL_C  ? c_kw  :
                       hl_type==HL_PY ? py_kw :
                       hl_type==HL_JS ? js_kw :
                       hl_type==HL_SH ? sh_kw : NULL;

    if (hl_type == HL_C) {
        int j = 0;
        while (j < len && s[j] == ' ') j++;
        if (j < len && s[j] == '#') {
            ab_appendz(ab, C_HL_PREPROC);
            ab_append(ab, s, len);
            ab_appendz(ab, C_RESET);
            return;
        }
    }
    if (hl_type == HL_SH) {
        int j = 0;
        while (j < len && s[j] == ' ') j++;
        if (j < len && s[j] == '#') {
            ab_appendz(ab, C_HL_COMMENT);
            ab_append(ab, s, len);
            ab_appendz(ab, C_RESET);
            return;
        }
    }

    const char *cur = NULL;
    int i = 0, in_str = 0, in_blk = 0;
    char str_ch = 0;

#define SET(c) do { if(cur!=(c)){ab_appendz(ab,(c)?(c):C_RESET);cur=(c);} } while(0)
#define RST()  do { if(cur){ab_appendz(ab,C_RESET);cur=NULL;} } while(0)
#define OUT(c) do { char _x=(c); ab_append(ab,&_x,1); } while(0)

    while (i < len) {
        char c = s[i];

        if (in_blk) {
            SET(C_HL_COMMENT);
            if (c=='*' && i+1<len && s[i+1]=='/') {
                OUT('*'); i++; OUT('/'); i++;
                in_blk=0; RST();
            } else { OUT(c); i++; }
            continue;
        }

        if (in_str) {
            SET(C_HL_STRING);
            OUT(c); i++;
            if (c=='\\' && i<len) { OUT(s[i]); i++; }
            else if (c==str_ch) { in_str=0; RST(); }
            continue;
        }

        if ((hl_type==HL_C||hl_type==HL_JS) && c=='/' && i+1<len && s[i+1]=='/') {
            SET(C_HL_COMMENT); ab_append(ab, s+i, len-i); i=len; RST(); continue;
        }
        if (hl_type==HL_PY && c=='#') {
            SET(C_HL_COMMENT); ab_append(ab, s+i, len-i); i=len; RST(); continue;
        }
        if ((hl_type==HL_C||hl_type==HL_JS) && c=='/' && i+1<len && s[i+1]=='*') {
            in_blk=1; SET(C_HL_COMMENT); OUT('/'); i++; OUT('*'); i++; continue;
        }
        if (c=='"' || (c=='\'' && hl_type!=HL_SH)) {
            in_str=1; str_ch=c; SET(C_HL_STRING); OUT(c); i++; continue;
        }
        if (isdigit((unsigned char)c) &&
            (i==0||(!isalnum((unsigned char)s[i-1])&&s[i-1]!='_'))) {
            SET(C_HL_NUMBER);
            while (i<len&&(isalnum((unsigned char)s[i])||s[i]=='.'||s[i]=='x'||s[i]=='X'))
                { OUT(s[i]); i++; }
            RST(); continue;
        }
        if (isalpha((unsigned char)c) || c=='_') {
            int st=i;
            while (i<len&&(isalnum((unsigned char)s[i])||s[i]=='_')) i++;
            if (kws && is_kw(s+st, i-st, kws)) SET(C_HL_KEYWORD);
            else RST();
            ab_append(ab, s+st, i-st);
            RST(); continue;
        }
        RST(); OUT(c); i++;
    }
    RST();
#undef SET
#undef RST
#undef OUT
}
