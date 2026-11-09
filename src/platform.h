/*
  gw-dos platform layer: everything the DOS frontend needs from the host.
  Two implementations: platform_core.c (Game & Watch external core, retro-go SD
  "external_cores" firmware, ABI v2) and host/platform_host.c (PC test harness). The frontend (dos_main.c,
  cga_render.c) and the XTulator core only talk to this interface and to stdio.
*/
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PLAT_LCD_W 320
#define PLAT_LCD_H 240
#define PLAT_AUDIO_RATE 48000
#define PLAT_FPS 60
#define PLAT_AUDIO_SAMPLES (PLAT_AUDIO_RATE / PLAT_FPS)   /* 800 per frame */

/* buttons (bitmask) */
enum {
    BTN_UP = 1u << 0, BTN_DOWN = 1u << 1, BTN_LEFT = 1u << 2, BTN_RIGHT = 1u << 3,
    BTN_A = 1u << 4, BTN_B = 1u << 5,
    BTN_GAME = 1u << 6,   /* GAME (Mario) / START */
    BTN_TIME = 1u << 7,   /* TIME (Mario) / SELECT */
    BTN_X = 1u << 8,      /* START on Zelda units */
    BTN_Y = 1u << 9,      /* SELECT on Zelda units */
};

/* Called once at startup. Switches the LCD to 8-bit indexed mode. */
void plat_init(bool start_paused);

/* Memory the frontend may hand to the XTulator pool, region by region
   (returns 0 when there are no more). Contents are undefined. */
size_t plat_mem_region(int index, uint8_t** base);

/* 16-entry 0x00RRGGBB palette for the indexed framebuffer */
void plat_set_palette(const uint32_t* rgb, int count);

/* One frame: begin (returns true if this frame should be drawn), read buttons
   (the platform also runs its own menu handling here), get the 320x240x8 back
   buffer, present it, push PLAT_AUDIO_SAMPLES mono samples, end (pacing). */
bool     plat_frame_begin(void);
uint32_t plat_buttons(void);
uint8_t* plat_framebuffer(void);
void     plat_present(void);
void     plat_audio(const int16_t* samples, int count);
void     plat_frame_end(void);

/* Directory listing for the disk menu: calls cb(name) for each regular file in dir. */
void plat_list_dir(const char* dir, void (*cb)(const char* name, void* user), void* user);

/* Disk image chosen in the launcher (full path), or NULL */
const char* plat_boot_image(void);

/* Small persistent integer settings (per-core .cfg on the console) */
int32_t plat_setting_get(const char* key, int32_t default_value);
void    plat_setting_set(const char* key, int32_t value);

/* Monotonic milliseconds (for the CPU-load meter only) */
uint32_t plat_ms(void);
/* Free-running CPU cycle counter (any rate; the governor calibrates it against plat_ms) */
uint32_t plat_cycles(void);

/* ---- savestates (platform_core.c drives them, the frontend owns the disks) ----
   A state is every byte of core RAM. What belongs to THIS session rather than
   the saved machine -- the open disk-image FILEs, the start-up info timer -- is
   taken before a load and put back after it; a different disk in A: is
   re-inserted by name. */
typedef struct {
    void*    disk[4];
    uint8_t  ins[4];
    char     a[48];
    uint32_t info_until;
} dos_ss_live_t;
void dos_ss_presave(void);
void dos_ss_live_get(dos_ss_live_t* l);
void dos_ss_live_put(const dos_ss_live_t* l);
