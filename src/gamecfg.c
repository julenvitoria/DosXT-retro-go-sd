/*
  gw-dos per-game configuration file reader (see gamecfg.h). GPLv2 or later.
*/
#include <stdio.h>
#include <string.h>
#include "gamecfg.h"

typedef struct { const char* name; uint8_t sc; } KEYNAME;

/* PC/XT scancode set 1 make codes; English and Spanish names */
static const KEYNAME keynames[] = {
    {"esc",0x01},{"escape",0x01},
    {"1",0x02},{"2",0x03},{"3",0x04},{"4",0x05},{"5",0x06},{"6",0x07},{"7",0x08},{"8",0x09},{"9",0x0A},{"0",0x0B},
    {"minus",0x0C},{"-",0x0C},{"equals",0x0D},{"=",0x0D},
    {"backspace",0x0E},{"retroceso",0x0E},{"tab",0x0F},{"tabulador",0x0F},
    {"q",0x10},{"w",0x11},{"e",0x12},{"r",0x13},{"t",0x14},{"y",0x15},{"u",0x16},{"i",0x17},{"o",0x18},{"p",0x19},
    {"lbracket",0x1A},{"[",0x1A},{"rbracket",0x1B},{"]",0x1B},
    {"enter",0x1C},{"return",0x1C},{"intro",0x1C},
    {"ctrl",0x1D},{"control",0x1D},
    {"a",0x1E},{"s",0x1F},{"d",0x20},{"f",0x21},{"g",0x22},{"h",0x23},{"j",0x24},{"k",0x25},{"l",0x26},
    {"semicolon",0x27},{";",0x27},{"quote",0x28},{"'",0x28},{"backtick",0x29},{"`",0x29},
    {"shift",0x2A},{"lshift",0x2A},{"mayus",0x2A},{"backslash",0x2B},{"\\",0x2B},
    {"z",0x2C},{"x",0x2D},{"c",0x2E},{"v",0x2F},{"b",0x30},{"n",0x31},{"m",0x32},
    {"comma",0x33},{",",0x33},{"period",0x34},{".",0x34},{"slash",0x35},{"/",0x35},
    {"rshift",0x36},{"kpmul",0x37},{"*",0x37},{"alt",0x38},
    {"space",0x39},{"espacio",0x39},{"capslock",0x3A},
    {"f1",0x3B},{"f2",0x3C},{"f3",0x3D},{"f4",0x3E},{"f5",0x3F},{"f6",0x40},{"f7",0x41},{"f8",0x42},{"f9",0x43},{"f10",0x44},
    {"numlock",0x45},{"scrolllock",0x46},
    {"home",0x47},{"inicio",0x47},{"kp7",0x47},
    {"up",0x48},{"arriba",0x48},{"kp8",0x48},
    {"pgup",0x49},{"repag",0x49},{"kp9",0x49},{"kpminus",0x4A},
    {"left",0x4B},{"izquierda",0x4B},{"kp4",0x4B},{"kp5",0x4C},
    {"right",0x4D},{"derecha",0x4D},{"kp6",0x4D},{"kpplus",0x4E},
    {"end",0x4F},{"fin",0x4F},{"kp1",0x4F},
    {"down",0x50},{"abajo",0x50},{"kp2",0x50},
    {"pgdn",0x51},{"avpag",0x51},{"kp3",0x51},
    {"ins",0x52},{"insert",0x52},{"kp0",0x52},
    {"del",0x53},{"delete",0x53},{"supr",0x53},
    {"f11",0x57},{"f12",0x58},
    {"keyboard",GC_KEY_KEYBOARD},{"teclado",GC_KEY_KEYBOARD},
    {"none",GC_KEY_NONE},{"nada",GC_KEY_NONE},
    {"mup",GC_KEY_MUP},{"mdown",GC_KEY_MDOWN},{"mleft",GC_KEY_MLEFT},{"mright",GC_KEY_MRIGHT},
    {"click",GC_KEY_MCLICK},{"click1",GC_KEY_MCLICK},{"clic",GC_KEY_MCLICK},
    {"click2",GC_KEY_MCLICK2},{"clic2",GC_KEY_MCLICK2},
    {NULL,0}
};

/* button names, same order as BTN_* bits */
static const char* const buttons[GC_BUTTONS][3] = {
    {"up","arriba",NULL},{"down","abajo",NULL},{"left","izquierda",NULL},{"right","derecha",NULL},
    {"a",NULL,NULL},{"b",NULL,NULL},{"game",NULL,NULL},{"time",NULL,NULL},
    {"start","x",NULL},{"select","y",NULL},
};

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static void trim(char* s) {
    char* p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
}

static int lookup_key(const char* name) {
    char l[24];
    size_t i = 0;
    for (; name[i] && i < sizeof(l) - 1; i++) l[i] = lower(name[i]);
    l[i] = 0;
    for (const KEYNAME* k = keynames; k->name; k++)
        if (!strcmp(k->name, l)) return k->sc;
    return -1;
}

/* "ctrl+c" -> {0x1D, 0x2E, 0}; returns 0 on error */
static int parse_keys(const char* v, uint8_t out[3]) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%s", v);
    int n = 0;
    char* s = buf;
    memset(out, 0, 3);
    while (*s && n < 3) {
        char* plus = strchr(s + 1, '+');      /* s+1: a lone "+" key is not supported, "kpplus" is */
        if (plus) *plus = 0;
        trim(s);
        int sc = lookup_key(s);
        if (sc < 0) return 0;
        out[n++] = (uint8_t)sc;
        if (!plus) break;
        s = plus + 1;
    }
    return n > 0;
}

int gamecfg_load(const char* path, GAMECFG* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->zoom80 = -1;
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    char line[200];
    int c, n = 0;
    for (;;) {                                /* read lines without fgets (not in every libc bridge) */
        c = fgetc(f);
        if (c == EOF || c == '\n') {
            line[n] = 0;
            n = 0;
            char* hash = strchr(line, ';');
            char* eq = strchr(line, '=');
            /* ';' starts a comment, except inside the value of "run" (command separator) */
            if (hash && (!eq || hash < eq)) *hash = 0;
            char* h2 = strchr(line, '#');
            if (h2) *h2 = 0;
            eq = strchr(line, '=');
            if (eq) {
                *eq = 0;
                char* key = line;
                char* val = eq + 1;
                trim(key); trim(val);
                for (char* p = key; *p; p++) *p = lower(*p);
                if (!strcmp(key, "run")) {
                    /* a trailing "  ; comment" after the commands is not supported: keep it simple */
                    snprintf(cfg->run, sizeof(cfg->run), "%s", val);
                } else {
                    char* sc2 = strchr(val, ';');
                    if (sc2) { *sc2 = 0; trim(val); }
                    if (!strcmp(key, "boot")) snprintf(cfg->boot, sizeof(cfg->boot), "%s", val);
                    else if (!strcmp(key, "cpu")) {
                        int whole = 0, frac = 0, fd = 0;
                        const char* p = val;
                        while (*p >= '0' && *p <= '9') whole = whole * 10 + (*p++ - '0');
                        if (*p == '.' || *p == ',') { p++; if (*p >= '0' && *p <= '9') { frac = *p - '0'; fd = 1; } }
                        cfg->cpu_mhz10 = whole * 10 + (fd ? frac : 0);
                        if (!cfg->cpu_mhz10) cfg->errors++;
                    } else if (!strcmp(key, "mouse") || !strcmp(key, "raton")) {
                        char v0 = lower(val[0]);
                        cfg->mouse = (v0 == '1' || v0 == 's' || v0 == 'y' || !strcmp(val, "on")) ? 1 : 0;
                    } else if (!strcmp(key, "vram")) {
                        int kb = 0;
                        for (const char* q = val; *q >= '0' && *q <= '9'; q++) kb = kb * 10 + (*q - '0');
                        if (kb == 64 || kb == 128 || kb == 256) cfg->vram_kb = kb; else cfg->errors++;
                    } else if (!strcmp(key, "video")) {
                        char v0 = lower(val[0]);
                        if (v0 == 'c') cfg->video = 1;
                        else if (v0 == 'e' || v0 == 'v') cfg->video = 2;
                        else cfg->errors++;
                    } else if (!strcmp(key, "zoom80")) cfg->zoom80 = (val[0] == '1' || lower(val[0]) == 's' || lower(val[0]) == 'y') ? 1 : 0;
                    else {
                        int found = 0;
                        for (int b = 0; b < GC_BUTTONS && !found; b++)
                            for (int k = 0; k < 3 && buttons[b][k]; k++)
                                if (!strcmp(key, buttons[b][k])) {
                                    found = 1;
                                    if (parse_keys(val, cfg->keys[b])) cfg->set[b] = 1;
                                    else cfg->errors++;
                                    break;
                                }
                        if (!found && key[0]) cfg->errors++;
                    }
                }
            }
            if (c == EOF) break;
        } else if (n < (int)sizeof(line) - 1) {
            line[n++] = (char)c;
        }
    }
    fclose(f);
    cfg->loaded = 1;
    return 1;
}

uint16_t gamecfg_char_to_bioskey(char ch) {
    static const char row1[] = "1234567890-=";        /* 0x02.. */
    static const char row1s[] = "!@#$%^&*()_+";
    static const char row2[] = "qwertyuiop[]";        /* 0x10.. */
    static const char row3[] = "asdfghjkl;'`";        /* 0x1E.. */
    static const char row4[] = "zxcvbnm,./";          /* 0x2C.. */
    static const char row4s[] = "ZXCVBNM<>?";
    uint8_t a = (uint8_t)ch;
    const char* p;
    if (ch == ' ') return 0x3920;
    if (ch == '\\') return 0x2B5C;
    if (ch == '|') return 0x2B7C;
    if (ch == ':') return 0x273A;
    if (ch == '"') return 0x2822;
    if (ch == '{') return 0x1A7B;
    if (ch == '}') return 0x1B7D;
    if (ch == '~') return 0x297E;
    if ((p = strchr(row1, ch)) && *p) return (uint16_t)(((0x02 + (p - row1)) << 8) | a);
    if ((p = strchr(row1s, ch)) && *p) return (uint16_t)(((0x02 + (p - row1s)) << 8) | a);
    char l = lower(ch);
    if ((p = strchr(row2, l)) && *p) return (uint16_t)(((0x10 + (p - row2)) << 8) | a);
    if ((p = strchr(row3, l)) && *p) return (uint16_t)(((0x1E + (p - row3)) << 8) | a);
    if ((p = strchr(row4, l)) && *p) return (uint16_t)(((0x2C + (p - row4)) << 8) | a);
    if ((p = strchr(row4s, ch)) && *p) return (uint16_t)(((0x2C + (p - row4s)) << 8) | a);
    if (a >= 32 && a < 127) return a;                    /* ASCII only, scancode 0 */
    return 0;
}
