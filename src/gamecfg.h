/*
  gw-dos per-game configuration (/cores/dos/<image name>.cfg; also /roms/dos/cfg/ or next to the image).

    ; comment                      (also #)
    boot   = MS-DOS 6.22.img       ; boot DOS from this image (A:); the game image becomes
                                   ; B: (floppy) or C: (hard disk). Relative to /roms/dos.
    run    = b:;prince             ; typed at the DOS prompt, ';' = Enter between commands
    cpu    = 10                    ; MHz: 4.77, 8, 10, 12, 16, 20 (nearest preset)
    zoom80 = 0                     ; 80-column text: 1 = 40-column window, 0 = whole line
    video  = vga                   ; cga: CGA only, all 640 KB for DOS; vga (or ega): EGA/VGA too
    vram   = 128                   ; EGA/VGA memory in KB: 64, 128 (default), 256 (taken from DOS RAM)
    mouse  = on                    ; mouse driver; d-pad moves the pointer, A/B = buttons
                                   ; (or per button: mup mdown mleft mright click click2)
    a      = shift                 ; buttons: up down left right a b game time start select
    b      = ctrl+a                ; values: key names, combos with '+', "keyboard", "none"
*/
#pragma once
#include <stdint.h>

#define GC_BUTTONS 10            /* same order as the BTN_* bits in platform.h */
#define GC_KEY_KEYBOARD 0xFE     /* open the on-screen keyboard */
#define GC_KEY_NONE     0xFD     /* button does nothing */
#define GC_KEY_MUP      0xF0     /* mouse: move pointer up / down / left / right */
#define GC_KEY_MDOWN    0xF1
#define GC_KEY_MLEFT    0xF2
#define GC_KEY_MRIGHT   0xF3
#define GC_KEY_MCLICK   0xF4     /* mouse: left / right button */
#define GC_KEY_MCLICK2  0xF5

typedef struct {
    int loaded;
    char boot[96];
    char run[160];
    int cpu_mhz10;                /* MHz x10, 0 = not set */
    int zoom80;                   /* -1 = not set */
    int video;                    /* 0 = not set, 1 = cga, 2 = ega/vga */
    int mouse;                    /* 1: d-pad = pointer, A/B = mouse buttons (unless set) */
    int vram_kb;                  /* EGA/VGA video memory: 64, 128 or 256 (0 = not set) */
    uint8_t set[GC_BUTTONS];      /* 1 if the button was given in the file */
    uint8_t keys[GC_BUTTONS][3];  /* scancodes (make codes), 0-terminated; or GC_KEY_* */
    int errors;                   /* unknown keys / values (shown on screen) */
} GAMECFG;

/* returns 1 if the file was found and read */
int gamecfg_load(const char* path, GAMECFG* cfg);

/* scancode (set 1) and ASCII for typing a character into the BIOS buffer; 0 if unknown */
uint16_t gamecfg_char_to_bioskey(char ch);
