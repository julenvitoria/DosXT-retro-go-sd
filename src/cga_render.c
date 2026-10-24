/*
  CGA -> 320x200 8-bit indexed renderer for the Game & Watch (320x240 LCD).
    40x25 text, 320x200x4 : 1:1
    80x25 text            : 8x8 glyphs squeezed to 4x8 (pixel pairs OR-ed)
    640x200x2             : pixel pairs OR-ed
  Reads the XTulator CGA state (cga_RAM, cga_regs, cga_datareg). GPLv2 or later.
*/
#include <string.h>
#include "cga_render.h"
#include "font_cp437.h"
#include "modules/video/cga.h"

const uint32_t cga_rgb[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

/* 8-bit glyph row -> 4-bit (each output pixel = OR of an input pair) */
static uint8_t squeeze[256];
static int squeeze_ready = 0;

static void init_squeeze(void) {
    for (int b = 0; b < 256; b++) {
        uint8_t o = 0;
        for (int i = 0; i < 4; i++)
            if (b & (0xC0 >> (i * 2))) o |= 0x8 >> i;
        squeeze[b] = o;
    }
    squeeze_ready = 1;
}

int cga_zoom80 = 1;
static int win80;   /* first visible column of the 40-column zoom window */

static void render_text(uint8_t* fb, int y0, int cols) {
    const uint8_t mode = cga_regs[0x8];
    const int blink_enable = (mode & 0x20) != 0;
    const uint32_t start = ((((uint32_t)cga_datareg[0x0C] & 0x3F) << 8) | cga_datareg[0x0D]) * 2u;
    const uint32_t cursor = (((uint32_t)cga_datareg[0x0E] & 0x3F) << 8) | cga_datareg[0x0F];
    const uint8_t cstart = cga_datareg[0x0A] & 0x1F, cend = cga_datareg[0x0B] & 0x1F;
    const int cursor_on = ((cga_datareg[0x0A] & 0x60) != 0x20) && cga_cursor_blink_state;
    int cw = (cols == 80) ? 4 : 8, c0 = 0, c1 = cols;
    extern uint8_t biosdisk_booted;          /* INT 19h: the BIOS POST is over */
    /* 40 columns at full width, following the cursor -- but not on the BIOS POST screen:
       its lines are wider than 40 and nobody is typing there, so it is shown whole. */
    if (cols == 80 && cga_zoom80 && biosdisk_booted) {
        int cc = (int)(cursor % 80);
        if (cc < win80) win80 = cc - 4;
        if (cc >= win80 + 40) win80 = cc - 35;
        if (win80 < 0) win80 = 0;
        if (win80 > 40) win80 = 40;
        cw = 8; c0 = win80; c1 = win80 + 40;
    }

    for (int row = 0; row < 25; row++) {
        for (int col = c0; col < c1; col++) {
            uint32_t addr = (start + (uint32_t)(row * cols + col) * 2u) & 0x3FFF;
            uint8_t ch = cga_RAM[addr], attr = cga_RAM[(addr + 1) & 0x3FFF];
            uint8_t fg = attr & 0x0F, bg = attr >> 4;
            int hide = 0;
            if (blink_enable) {
                bg &= 7;
                if ((attr & 0x80) && !cga_cursor_blink_state) hide = 1;
            }
            const uint8_t* g = font_cp437[ch];
            int is_cursor = cursor_on && (uint32_t)(row * cols + col) == cursor;
            uint8_t* p = fb + (y0 + row * 8) * 320 + (col - c0) * cw;
            for (int y = 0; y < 8; y++, p += 320) {
                uint8_t bits = hide ? 0 : g[y];
                if (is_cursor && y >= cstart && y <= (cend > 7 ? 7 : cend)) bits = 0xFF;
                if (cw == 8) {
                    for (int x = 0; x < 8; x++) p[x] = (bits & (0x80 >> x)) ? fg : bg;
                } else {
                    uint8_t s = squeeze[bits];
                    p[0] = (s & 8) ? fg : bg; p[1] = (s & 4) ? fg : bg;
                    p[2] = (s & 2) ? fg : bg; p[3] = (s & 1) ? fg : bg;
                }
            }
        }
    }
}

static void render_gfx_lo(uint8_t* fb, int y0) {
    const uint8_t cs = cga_regs[0x9], mode = cga_regs[0x8];
    uint8_t pal[4];
    pal[0] = cs & 0x0F;                                  /* background */
    const int intense = (cs & 0x10) ? 8 : 0;
    if (mode & 0x04) { pal[1] = 3 + intense; pal[2] = 4 + intense; pal[3] = 7 + intense; }   /* "mode 5" */
    else if (cs & 0x20) { pal[1] = 3 + intense; pal[2] = 5 + intense; pal[3] = 7 + intense; }
    else { pal[1] = 2 + intense; pal[2] = 4 + intense; pal[3] = 6 + intense; }
    for (int y = 0; y < 200; y++) {
        const uint8_t* src = cga_RAM + ((y & 1) ? 0x2000 : 0) + (y >> 1) * 80;
        uint8_t* p = fb + (y0 + y) * 320;
        for (int x = 0; x < 80; x++) {
            uint8_t b = src[x];
            p[0] = pal[b >> 6]; p[1] = pal[(b >> 4) & 3]; p[2] = pal[(b >> 2) & 3]; p[3] = pal[b & 3];
            p += 4;
        }
    }
}

static void render_gfx_hi(uint8_t* fb, int y0) {
    const uint8_t fg = cga_regs[0x9] & 0x0F;
    for (int y = 0; y < 200; y++) {
        const uint8_t* src = cga_RAM + ((y & 1) ? 0x2000 : 0) + (y >> 1) * 80;
        uint8_t* p = fb + (y0 + y) * 320;
        for (int x = 0; x < 80; x++) {
            uint8_t s = squeeze[src[x]];
            p[0] = (s & 8) ? fg : 0; p[1] = (s & 4) ? fg : 0; p[2] = (s & 2) ? fg : 0; p[3] = (s & 1) ? fg : 0;
            p += 4;
        }
    }
}

void cga_render(uint8_t* fb, int y0) {
    if (!squeeze_ready) init_squeeze();
    const uint8_t mode = cga_regs[0x8];
    if (!(mode & 0x08)) {                       /* video disabled */
        memset(fb + y0 * 320, 0, 320 * 200);
        return;
    }
    if (mode & 0x02) {
        if (mode & 0x10) render_gfx_hi(fb, y0); else render_gfx_lo(fb, y0);
    } else {
        render_text(fb, y0, (mode & 0x01) ? 80 : 40);
    }
}

uint8_t ui_cmap[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

void ui_fill(uint8_t* fb, int x, int y, int w, int h, uint8_t color) {
    color = ui_cmap[color & 15];
    for (int j = 0; j < h; j++) {
        if (y + j < 0 || y + j >= 240) continue;
        uint8_t* p = fb + (y + j) * 320;
        for (int i = 0; i < w; i++) if (x + i >= 0 && x + i < 320) p[x + i] = color;
    }
}

void ui_text(uint8_t* fb, int x, int y, const char* s, uint8_t fg, uint8_t bg) {
    fg = ui_cmap[fg & 15];
    bg = ui_cmap[bg & 15];
    for (; *s; s++, x += 8) {
        if (x > 312) break;
        const uint8_t* g = font_cp437[(uint8_t)*s];
        for (int j = 0; j < 8; j++) {
            if (y + j < 0 || y + j >= 240) continue;
            uint8_t* p = fb + (y + j) * 320 + x;
            for (int i = 0; i < 8; i++) p[i] = (g[j] & (0x80 >> i)) ? fg : bg;
        }
    }
}
