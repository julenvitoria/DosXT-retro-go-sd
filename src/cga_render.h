#pragma once
#include <stdint.h>

/* CGA palette (index 0..15) as 0x00RRGGBB */
extern const uint32_t cga_rgb[16];

/* Draw the current CGA screen (text 40/80x25, graphics 320/640x200) into rows
   y0..y0+199 of a 320-pixel-wide 8-bit indexed framebuffer. */
void cga_render(uint8_t* fb, int y0);

/* 80x25 text: 1 = show a 40-column window at full size that follows the cursor,
   0 = squeeze all 80 columns into 4-pixel-wide glyphs */
extern int cga_zoom80;

/* Text helper for the frontend's own UI (8x8 CP437 font) */
void ui_text(uint8_t* fb, int x, int y, const char* s, uint8_t fg, uint8_t bg);
void ui_fill(uint8_t* fb, int x, int y, int w, int h, uint8_t color);

/* UI colour (CGA index 0..15) -> LCD palette index. Identity, except in 256-colour VGA modes
   where the frontend points it at the nearest colours of the game's palette */
extern uint8_t ui_cmap[16];
