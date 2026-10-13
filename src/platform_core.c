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

/* RAM_EMU kept free at the top for the firmware's own ram_malloc() users */
#define FIRMWARE_RESERVE (16 * 1024)

int dos_main(int start_paused);
void dos_repaint(void);

/* entry point (CORE_ENTRY in the Makefile; gw_core_entry.S branches here) */
int app_main_dos(uint8_t load_state, uint8_t start_paused, int8_t save_slot) {
    (void)load_state; (void)save_slot;
    return dos_main(start_paused);
}

void plat_init(bool start_paused) {
    /* guest RAM = everything between our BSS and the reserve; the firmware's
       ram_malloc() bump starts at ram_start, i.e. inside the reserve */
    ram_start = (uint32_t)((uintptr_t)__RAM_EMU_END__ - FIRMWARE_RESERVE);

    common_emu_state.frame_time_10us = (int16_t)(100000 / PLAT_FPS);
    common_emu_state.pause_after_frames = start_paused ? 2 : 0;
    odroid_system_init(APPID_CORE, PLAT_AUDIO_RATE);
    odroid_system_emu_init(NULL, NULL, NULL, NULL, NULL, NULL, NULL);   /* no savestates yet */
    common_emu_state.pause_after_frames = start_paused ? 2 : 0;

    lcd_setup_framebuffers(LCD_MODE_LUT8);   /* 8-bit indexed: frees ~150 KB of LCD pool */
    audio_start_playing(PLAT_AUDIO_SAMPLES);
}

size_t plat_mem_region(int index, uint8_t** base) {
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
    return 0;
}

void plat_set_palette(const uint32_t* rgb, int count) { lcd_set_clut(rgb, (uint16_t)count); }

bool plat_frame_begin(void) {
    wdog_refresh();
    return common_emu_frame_loop();
}

uint32_t plat_buttons(void) {
    static odroid_dialog_choice_t options[] = { ODROID_DIALOG_CHOICE_LAST };
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

void plat_present(void) { lcd_swap(); }

void plat_audio(const int16_t* s, int n) {
    if (common_emu_sound_loop_is_muted()) {
        audio_clear_active_buffer();
        return;
    }
    int16_t* out = audio_get_active_buffer();
    int len = audio_get_buffer_length();
    if (n > len) n = len;
    int32_t vol = common_emu_sound_get_volume();
    for (int i = 0; i < n; i++) out[i] = (int16_t)((s[i] * vol) >> 8);
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
