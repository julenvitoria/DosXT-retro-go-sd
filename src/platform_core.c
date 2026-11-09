/*
  gw-dos platform layer for the Game & Watch, as a standalone "external core" for the
  retro-go SD firmware (branch external_cores, firmware ABI v2).

  Firmware functions are called by their normal names (lcd_swap, fopen, ...); the core
  SDK's objcopy --redefine-syms step renames them to the core_* trampolines of
  Core/Src/porting/core_common/gw_core_bridge.c, which call through gw_firmware_abi_t.
  Data globals (ACTIVE_FILE, ram_start, common_emu_state) are macros from
  gw_core_bridge.h, which must be included after the firmware headers.
*/
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "odroid_system.h"
#include "odroid_overlay.h"
#include "odroid_settings.h"
#include "gw_lcd.h"
#include "gw_audio.h"
#include "gw_malloc.h"
#include "rom_manager.h"
#include "common.h"
#include "appid.h"
#include "ff.h"
#include "gw_core_bridge.h"
#include "platform.h"

/* from ld/gnw_ram_emu.ld (shared with the firmware) */
extern uint8_t __RAM_EMU_END__[];

extern uint8_t __CORE_DATA_START__[];   /* sdk/core_ram_emu.ld */

/* RAM_EMU kept free at the top for the firmware's own ram_malloc() users */
#define FIRMWARE_RESERVE (16 * 1024)

int dos_main(int start_paused);
void dos_repaint(void);

/* entry point (CORE_ENTRY in the Makefile; gw_core_entry.S branches here) */
/* ---- savestates ----
   Retro-Go owns the menu, the slots and the file names; a slot is every byte of
   core RAM: .data+.bss, and every region plat_mem_region() handed to the guest
   (RAM_EMU above the bss, the LCD bonus pool, the DTCM and ITCM pools). The
   XTulator core is malloc-free (core/static_mem.py), so that is the whole
   machine. A header with the build stamp and the region table refuses another
   build or another layout before anything is written. */
#define SS_NREG 5
typedef struct { uint32_t addr, len; } ss_region_t;
typedef struct {
    char        magic[8];
    uint32_t    version;
    char        build[24];
    ss_region_t reg[SS_NREG];
} ss_hdr_t;
static ss_region_t ss_reg[SS_NREG];        /* [1 + plat_mem_region index] */
static uint8_t ss_boot_pending;
static int8_t  ss_boot_slot;
static uint32_t ss_frames;

static void ss_fill(ss_hdr_t* h) {
    memset(h, 0, sizeof(*h));
    memcpy(h->magic, "DOSXTSS1", 8);
    h->version = 1;
    snprintf(h->build, sizeof(h->build), "%s %s", __DATE__, __TIME__);
    memcpy(h->reg, ss_reg, sizeof(ss_reg));
    h->reg[0].addr = (uint32_t)(uintptr_t)__CORE_DATA_START__;
    h->reg[0].len = (uint32_t)(uintptr_t)&__CORE_BSS_END__ - h->reg[0].addr;
}

/* 8 KB at a time, feeding the watchdog (~470 ms window) between pieces */
static bool ss_xfer(FILE* f, void* p, uint32_t n, int wr) {
    uint8_t* b = (uint8_t*)p;
    while (n) {
        uint32_t k = n < 8192u ? n : 8192u;
        wdog_refresh();
        if ((wr ? fwrite(b, 1, k, f) : fread(b, 1, k, f)) != k) return false;
        b += k;
        n -= k;
    }
    return true;
}

static bool ss_save(const char* path) {
    ss_hdr_t h;
    bool ok = false;
    if (!path) return false;
    dos_ss_presave();
    ss_fill(&h);
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    if (fwrite(&h, 1, sizeof(h), f) == sizeof(h)) {
        ok = true;
        for (int i = 0; i < SS_NREG && ok; i++)
            if (h.reg[i].len) ok = ss_xfer(f, (void*)(uintptr_t)h.reg[i].addr, h.reg[i].len, 1);
    }
    fclose(f);
    return ok;
}

static bool ss_load(const char* path) {
    ss_hdr_t h, cur;
    dos_ss_live_t live;
    bool ok = true;
    if (!path) return false;
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    ss_fill(&cur);
    if (fread(&h, 1, sizeof(h), f) != sizeof(h) || memcmp(&h, &cur, sizeof(h)) != 0) {
        fclose(f);                           /* another build or layout: untouched */
        return false;
    }
    dos_ss_live_get(&live);
    uint32_t frames = ss_frames;
    for (int i = 0; i < SS_NREG && ok; i++)
        if (h.reg[i].len) ok = ss_xfer(f, (void*)(uintptr_t)h.reg[i].addr, h.reg[i].len, 0);
    fclose(f);
    ss_frames = frames;
    ss_boot_pending = 0;
    dos_ss_live_put(&live);
    return ok;
}

/* entry point (CORE_ENTRY in the Makefile; gw_core_entry.S branches here) */
int app_main_dos(uint8_t load_state, uint8_t start_paused, int8_t save_slot) {
    ss_boot_pending = load_state ? 1 : 0;
    ss_boot_slot = save_slot;
    return dos_main(start_paused);
}

void plat_init(bool start_paused) {
    /* guest RAM = everything between our BSS and the reserve; the firmware's
       ram_malloc() bump starts at ram_start, i.e. inside the reserve */
    ram_start = (uint32_t)((uintptr_t)__RAM_EMU_END__ - FIRMWARE_RESERVE);

    common_emu_state.frame_time_10us = (int16_t)(100000 / PLAT_FPS);
    common_emu_state.pause_after_frames = start_paused ? 2 : 0;
    odroid_system_init(APPID_CORE, PLAT_AUDIO_RATE);
    odroid_system_emu_init(ss_load, ss_save, NULL, NULL, NULL, NULL, NULL);
    common_emu_state.pause_after_frames = start_paused ? 2 : 0;

    lcd_setup_framebuffers(LCD_MODE_LUT8);   /* 8-bit indexed: frees ~150 KB of LCD pool */
    audio_start_playing(PLAT_AUDIO_SAMPLES);
    *(volatile uint32_t*)0xE000EDFCu |= 1u << 24;          /* DEMCR.TRCENA */
    *(volatile uint32_t*)0xE0001000u |= 1u;                /* DWT_CTRL.CYCCNTENA */
}

static size_t plat_mem_region_(int index, uint8_t** base);
size_t plat_mem_region(int index, uint8_t** base) {
    size_t n = plat_mem_region_(index, base);
    if (index >= 0 && index < SS_NREG - 1) {
        ss_reg[1 + index].addr = n ? (uint32_t)(uintptr_t)*base : 0;
        ss_reg[1 + index].len = (uint32_t)n;
    }
    return n;
}
static size_t plat_mem_region_(int index, uint8_t** base) {
    if (index == 0) {
        uintptr_t start = ((uintptr_t)&__CORE_BSS_END__ + 7u) & ~(uintptr_t)7u;
        uintptr_t end = (uintptr_t)__RAM_EMU_END__ - FIRMWARE_RESERVE;
        *base = (uint8_t*)start;
        return end > start ? (size_t)(end - start) : 0;
    }
    if (index == 1) {
        uint8_t* p = NULL;
        size_t n = 0;
        lcd_get_bonus_pool(&p, &n);
        *base = p;
        return p ? n : 0;
    }
    /* The firmware's DTCM and ITCM bump pools are this core's too and were unused. Guest
       RAM goes in 4 KB chunks (XT_RAM_CHUNK in build.sh), so every whole chunk found here
       is 4 KB more DOS memory (the AdLib synthesiser's ~25 KB came out of RAM_EMU). A
       margin stays behind in each: DTCM's pool runs up to the stack's red zone. */
    if (index == 2 || index == 3) {
        const size_t margin = index == 2 ? 8 * 1024 : 4 * 1024;
        size_t free = index == 2 ? dtc_get_free_size() : itc_get_free_size();
        if (free < margin + 4 * 1024) return 0;
        size_t n = (free - margin) & ~(size_t)(4 * 1024 - 1);     /* whole chunks only */
        void* p = index == 2 ? dtc_malloc(n) : itc_malloc(n);
        *base = (uint8_t*)p;
        return p ? n : 0;
    }
    return 0;
}

void plat_set_palette(const uint32_t* rgb, int count) { lcd_set_clut(rgb, (uint16_t)count); }

/* Test hook for a debugger: 1 saves, 2 loads, 3 saves and loads 120 frames on */
volatile uint32_t dxt_ss_request, dxt_ss_saves, dxt_ss_loads, dxt_ss_fails;
static uint32_t ss_test_at;
static void ss_test(void) {
    static const char path[] = "/roms/dos/dosxt_ss_test.sav";
    uint32_t r = dxt_ss_request;
    if (r) {
        dxt_ss_request = 0;
        if (r == 1 || r == 3) { if (ss_save(path)) dxt_ss_saves++; else dxt_ss_fails++; }
        if (r == 2) { if (ss_load(path)) dxt_ss_loads++; else dxt_ss_fails++; }
        if (r == 3) ss_test_at = ss_frames + 120;
    }
    if (ss_test_at && ss_frames >= ss_test_at) {
        ss_test_at = 0;
        if (ss_load(path)) dxt_ss_loads++; else dxt_ss_fails++;
    }
}

bool plat_frame_begin(void) {
    wdog_refresh();
    ss_frames++;
    ss_test();
    /* "Resume" from the launcher: the slot loads once the machine is up */
    if (ss_boot_pending && ss_frames > 2) {
        ss_boot_pending = 0;
        odroid_system_emu_load_state(ss_boot_slot);
    }
    return common_emu_frame_loop();
}

/* "Info ON/OFF" in retro-go's PAUSE/SET menu: keeps the bottom status line on screen */
int  dos_info_get(void);
void dos_info_set(int on);
static char info_value[8];
static bool info_cb(odroid_dialog_choice_t* option, odroid_dialog_event_t event, uint32_t repeat) {
    (void)repeat;
    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT || event == ODROID_DIALOG_ENTER)
        dos_info_set(!dos_info_get());
    strcpy(option->value, dos_info_get() ? "ON" : "OFF");
    return false;                                      /* stay in the menu */
}

uint32_t plat_buttons(void) {
    static odroid_dialog_choice_t options[] = {
        { 100, "Info", info_value, 1, info_cb },      /* the CPU / MHz / load line */
        ODROID_DIALOG_CHOICE_LAST
    };
    odroid_gamepad_state_t js;
    odroid_input_read_gamepad(&js);
    common_emu_input_loop(&js, options, &dos_repaint);   /* PAUSE menu, power, volume... */
    uint32_t b = 0;
    if (js.values[ODROID_INPUT_UP]) b |= BTN_UP;
    if (js.values[ODROID_INPUT_DOWN]) b |= BTN_DOWN;
    if (js.values[ODROID_INPUT_LEFT]) b |= BTN_LEFT;
    if (js.values[ODROID_INPUT_RIGHT]) b |= BTN_RIGHT;
    if (js.values[ODROID_INPUT_A]) b |= BTN_A;
    if (js.values[ODROID_INPUT_B]) b |= BTN_B;
    if (js.values[ODROID_INPUT_START]) b |= BTN_GAME;
    if (js.values[ODROID_INPUT_SELECT]) b |= BTN_TIME;
    if (js.values[ODROID_INPUT_X]) b |= BTN_X;
    if (js.values[ODROID_INPUT_Y]) b |= BTN_Y;
    return b;
}

uint8_t* plat_framebuffer(void) { return (uint8_t*)lcd_get_active_buffer(); }

/* Retro-go's in-game HUD (volume / brightness bars from PAUSE/SET + a direction,
   low-battery blink) is drawn into the active buffer, so after our whole frame
   and right before the swap. common_emu_input_loop() already changes the level;
   without this call the change just had no picture. */
void plat_present(void) { common_ingame_overlay(); lcd_swap(); }

void plat_audio(const int16_t* s, int n) {
    if (common_emu_sound_loop_is_muted()) {
        audio_clear_active_buffer();
        return;
    }
    int16_t* out = audio_get_active_buffer();
    int len = audio_get_buffer_length();
    if (n > len) n = len;
    int32_t vol = common_emu_sound_get_volume();
    /* THE CONSOLE'S SPEAKER CANNOT PLAY BASS. A recording of AdLib music on the console had
       1 % of its energy below 300 Hz where the same music rendered here has 66 %: the bass
       line only drives the little speaker towards distortion and eats the headroom the
       melody needs. A 2nd-order high-pass at 200 Hz (Butterworth) takes it out before the
       volume, so what the speaker can play comes out cleaner. */
    static float x1, x2, y1, y2;
    const float b0 = 0.98166f, b1 = -1.96332f, b2 = 0.98166f, a1 = -1.96298f, a2 = 0.96365f;  /* fc 200 Hz @ 48 kHz */
    for (int i = 0; i < n; i++) {
        float x0 = (float)s[i];
        float y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x0; y2 = y1; y1 = y0;
        int32_t v = (int32_t)y0;
        if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
        out[i] = (int16_t)((v * vol) >> 8);
    }
}

void plat_frame_end(void) { common_emu_sound_sync(false); }

void plat_list_dir(const char* dir, void (*cb)(const char*, void*), void* user) {
    DIR d;
    FILINFO fi;
    if (f_opendir(&d, dir) != FR_OK) return;
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        if (!(fi.fattrib & AM_DIR)) cb(fi.fname, user);
    }
    f_closedir(&d);
}

const char* plat_boot_image(void) { return ACTIVE_FILE ? ACTIVE_FILE->path : NULL; }

int32_t plat_setting_get(const char* key, int32_t def) { return odroid_settings_app_int32_get(key, def); }
void plat_setting_set(const char* key, int32_t value) { odroid_settings_app_int32_set(key, value); }

uint32_t plat_ms(void) { return HAL_GetTick(); }
/* the Cortex-M7 cycle counter (DWT->CYCCNT), switched on in plat_init */
uint32_t plat_cycles(void) { return *(volatile uint32_t*)0xE0001004u; }
