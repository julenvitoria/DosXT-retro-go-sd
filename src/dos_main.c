/*
  gw-dos frontend: XTulator (8088/V20-class PC + CGA) on the Game & Watch.
  Shared by the retro-go external core (platform_core.c) and the PC test harness (host/).
  GPLv2 or later (XTulator is GPLv2+); the embedded BIOS is GPLv3 (Sergey Kiselev).

  Screen (320x240, 8-bit indexed): the PC picture centred on lines 20..219; the 20-line
  bands above and below carry the status lines, or the on-screen keyboard (two rows of
  keys in each band, so the game never moves or shrinks). The disk menu opens over the
  picture.

  Buttons, normal mode:  D-pad = arrow keys, A = Enter, B = Space, GAME = Esc,
                         X (START) = Tab, Y (SELECT) = Backspace, TIME = on-screen keyboard.
  On-screen keyboard:    D-pad moves, A types (Shift/Ctrl/Alt latch until the next key),
                         B or TIME closes. Special keys: "A:" disk menu, "CPU" speed,
                         "80c" 80-column text: 40-column window following the cursor
                         (default, legible) or the whole line squeezed to 4-pixel glyphs.
  Disk images:           the image picked in the launcher (an .img in /roms/dos) boots: up to
                         2.88 MB it is drive A:, bigger images are a hard disk (drive C:).
                         When booting a floppy, c.img in the same folder is attached as C:.
                         Any .img of that folder can be put in A: from the disk menu.
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "platform.h"
#ifndef GWDOS_CFG_DIR
#define GWDOS_CFG_DIR "/cores/dos"     /* per-game .cfg files */
#endif
#include "cga_render.h"
#include "gamecfg.h"
#include "config.h"
#include "machine.h"
#include "memory.h"
#include "ports.h"
#include "timing.h"
#include "xt_pool.h"
#include "cpu/cpu.h"
#include "chipset/i8259.h"
#include "modules/disk/biosdisk.h"
#include "modules/video/cga.h"
#ifdef XT_VGA_HLE
#include "vga_hle.h"
#endif

#define FLOPPY_MAX_BYTES 2949120u            /* 2.88 MB: bigger images are hard disks */
static char dos_dir[256] = ".";

/* ---- globals the XTulator core expects from its frontend ---- */
char* usemachine = "sergey_xt";
uint32_t baudrate = 115200, ramsize = 640, instructionsperloop = 100;
uint8_t videocard = 0xFF;
volatile double speed = 0;
volatile uint8_t running = 1;
double speedarg = 0;
MACHINE_t machine;
extern uint64_t timing_virtual_us;
extern int timing_virtual_enable;

/* ---- BIOS (Sergey Kiselev 8088_bios, GPLv3), compiled in, read in place ---- */
extern const uint8_t bios_xt_rom[16384];
const uint8_t* xt_rom_lookup(const char* filename, uint32_t size) {
    if (size == 16384 && strcmp(filename, "bios-xt.bin") == 0) return bios_xt_rom;
    return NULL;
}

/* ---- emulation speed ---- */
/* Speed presets as "equivalent 8088 MHz": an IBM PC/XT (8088 @ 4.77 MHz) averages about
   0.315 million instructions/s (~15 clock cycles per instruction), so 1 MHz ~ 66,000 instr/s.
   This is an approximation: real timing depends on the instruction mix. */
static const struct { const char* mhz; uint32_t ips; const char* pc; } speeds[] = {
    { "4.77", 315000,  "IBM PC/XT" },
    { "8",    528000,  "Turbo XT" },
    { "10",   660000,  "Turbo XT" },
    { "12",   792000,  "" },
    { "16",   1056000, "" },
    { "20",   1320000, "" },
};
#define NSPEEDS (sizeof(speeds) / sizeof(speeds[0]))
static int speed_idx = 1;                 /* 8 MHz (saved setting "CpuMHz") */
static uint32_t ips = 528000;
static uint32_t osd_frames = 0;           /* frames left to show the on-screen speed message */

/* ---- keyboard queue (scancode set 1) ---- */
#define KQ_SIZE 64
static uint8_t kq[KQ_SIZE];
static int kq_head = 0, kq_tail = 0;
static uint32_t kq_gap = 0;               /* instructions since the last scancode */
#define KQ_SPACING 4000                   /* give the BIOS ISR time to read port 60h */

static void key_push(uint8_t sc) {
    int n = (kq_head + 1) % KQ_SIZE;
    if (n != kq_tail) { kq[kq_head] = sc; kq_head = n; }
}
static void key_pump(uint32_t executed) {
    kq_gap += executed;
    if (kq_tail != kq_head && kq_gap >= KQ_SPACING) {
        machine.KeyState.scancode = kq[kq_tail];
        machine.KeyState.isNew = 1;
        i8259_doirq(&machine.i8259, 1);
        kq_tail = (kq_tail + 1) % KQ_SIZE;
        kq_gap = 0;
    }
}
static void key_tap(uint8_t sc) { key_push(sc); key_push(sc | 0x80); }

/* ---- on-screen keyboard ---- */
#define SC_DISK 0xF0
#define SC_CPU  0xF1
#define SC_ZOOM 0xF2
typedef struct { const char* label; uint8_t sc; } OSK_KEY;
/* four rows: 0-1 in the band above the picture, 2-3 in the band below. A key is
   label x 8 + 4 pixels wide; every row fits in 320. */
static const OSK_KEY osk_r0[] = { {"Esc",0x01},{"1",0x02},{"2",0x03},{"3",0x04},{"4",0x05},{"5",0x06},{"6",0x07},{"7",0x08},{"8",0x09},{"9",0x0A},{"0",0x0B},{"-",0x0C},{"=",0x0D},{"\x1B-",0x0E},
                                  {"F1",0x3B},{"F2",0x3C},{"F3",0x3D},{"F4",0x3E},{"F5",0x3F},{"F6",0x40},{NULL,0} };
static const OSK_KEY osk_r1[] = { {"Tab",0x0F},{"Q",0x10},{"W",0x11},{"E",0x12},{"R",0x13},{"T",0x14},{"Y",0x15},{"U",0x16},{"I",0x17},{"O",0x18},{"P",0x19},{"[",0x1A},{"]",0x1B},{"Ent",0x1C},
                                  {"F7",0x41},{"F8",0x42},{"F9",0x43},{"F10",0x44},{"Del",0x53},{NULL,0} };
static const OSK_KEY osk_r2[] = { {"Ctl",0x1D},{"A",0x1E},{"S",0x1F},{"D",0x20},{"F",0x21},{"G",0x22},{"H",0x23},{"J",0x24},{"K",0x25},{"L",0x26},{";",0x27},{"'",0x28},{"\\",0x2B},{"`",0x29},
                                  {"Ins",0x52},{"80c",SC_ZOOM},{"CPU",SC_CPU},{"A:",SC_DISK},{NULL,0} };
static const OSK_KEY osk_r3[] = { {"Shf",0x2A},{"Z",0x2C},{"X",0x2D},{"C",0x2E},{"V",0x2F},{"B",0x30},{"N",0x31},{"M",0x32},{",",0x33},{".",0x34},{"/",0x35},{"Alt",0x38},{"Spc",0x39},
                                  {"\x18",0x48},{"\x19",0x50},{"\x1B",0x4B},{"\x1A",0x4D},{"Hom",0x47},{"End",0x4F},{NULL,0} };
#define OSK_ROWS 4
static const OSK_KEY* const osk_rows[OSK_ROWS] = { osk_r0, osk_r1, osk_r2, osk_r3 };
static const int osk_row_y[OSK_ROWS] = { 2, 11, 221, 230 };

static int ui_mode = 0;                   /* 0 status bar, 1 keyboard, 2 disk menu */
static int osk_row = 1, osk_col = 1;
static uint8_t latched_shift = 0, latched_ctrl = 0, latched_alt = 0;

static int row_len(int r) { int n = 0; while (osk_rows[r][n].label) n++; return n; }
static int key_w(const OSK_KEY* k) { return (int)strlen(k->label) * 8 + 4; }
static int key_x(int r, int c) { int x = 0; for (int i = 0; i < c; i++) x += key_w(&osk_rows[r][i]); return x; }

static void osk_move_row(int dr) {
    int cx = key_x(osk_row, osk_col) + key_w(&osk_rows[osk_row][osk_col]) / 2;
    osk_row = (osk_row + dr + OSK_ROWS) % OSK_ROWS;
    int best = 0, bestd = 1 << 30;
    for (int c = 0; c < row_len(osk_row); c++) {
        int m = key_x(osk_row, c) + key_w(&osk_rows[osk_row][c]) / 2;
        int d = m > cx ? m - cx : cx - m;
        if (d < bestd) { bestd = d; best = c; }
    }
    osk_col = best;
}

static void open_disk_menu(void);

static void osk_press(uint8_t sc) {
    if (sc == SC_DISK) { open_disk_menu(); return; }
    if (sc == SC_ZOOM) { cga_zoom80 ^= 1; plat_setting_set("Zoom80", cga_zoom80); return; }
    if (sc == SC_CPU) {
        speed_idx = (speed_idx + 1) % NSPEEDS; ips = speeds[speed_idx].ips;
        plat_setting_set("CpuMHz", speed_idx);
        osd_frames = 2 * PLAT_FPS;
        return;
    }
    uint8_t* latch = sc == 0x2A ? &latched_shift : sc == 0x1D ? &latched_ctrl : sc == 0x38 ? &latched_alt : NULL;
    if (latch) {
        *latch ^= 1;
        key_push(*latch ? sc : (sc | 0x80));
        return;
    }
    key_tap(sc);
    if (latched_shift) { key_push(0x2A | 0x80); latched_shift = 0; }
    if (latched_ctrl)  { key_push(0x1D | 0x80); latched_ctrl = 0; }
    if (latched_alt)   { key_push(0x38 | 0x80); latched_alt = 0; }
}

/* ---- disk menu ---- */
#define MAX_IMAGES 24
static char images[MAX_IMAGES][40];
static int n_images = 0, disk_sel = 0;
static char drive_a[48] = "", drive_b[48] = "", drive_c[48] = "";

static int ends_with_img(const char* s) {
    size_t n = strlen(s);
    if (n < 4) return 0;
    const char* e = s + n - 4;
    return e[0] == '.' && (e[1] | 0x20) == 'i' && (e[2] | 0x20) == 'm' && (e[3] | 0x20) == 'g';
}
static void add_image(const char* name, void* user) {
    (void)user;
    if (n_images < MAX_IMAGES && ends_with_img(name) && strlen(name) < sizeof(images[0])) {
        strcpy(images[n_images++], name);
    }
}
static void open_disk_menu(void) {
    n_images = 0;
    plat_list_dir(dos_dir, add_image, NULL);
    disk_sel = 0;
    ui_mode = 2;
}
static void insert_a(const char* name) {
    char path[320];
    snprintf(path, sizeof(path), "%s/%s", dos_dir, name);
    if (biosdisk_insert(&machine.CPU, 0, path) == 0) {
        snprintf(drive_a, sizeof(drive_a), "%s", name);
    } else {
        snprintf(drive_a, sizeof(drive_a), "(error)");
    }
}

/* ---- per-game configuration ---- */
static GAMECFG gcfg;
static char cfg_name[40] = "";

/* ---- input ---- */
static uint32_t prev_buttons = 0;
/* button i (BTN_* bit i) -> up to 3 make codes (pressed in order, released in reverse) */
static uint8_t padkeys[GC_BUTTONS][3] = {
    {0x48}, {0x50}, {0x4B}, {0x4D},              /* up down left right: arrows */
    {0x1C}, {0x39}, {0x01},                      /* A Enter, B Space, GAME Esc */
    {GC_KEY_KEYBOARD},                           /* TIME: on-screen keyboard */
    {0x0F}, {0x0E},                              /* START Tab, SELECT Backspace */
};

#ifdef XT_MOUSE_HLE
extern int mouse_hle_enabled;
void mouse_hle_move(int dx, int dy);
void mouse_hle_buttons(int b);
int mouse_hle_cursor(CPU_t* cpu, int* x, int* y);
void mouse_hle_install(CPU_t* cpu);
#endif
static uint8_t mouse_dirs, mouse_btns;            /* held mouse directions (bit = code-GC_KEY_MUP), buttons */
static uint16_t mouse_hold;

static int is_mouse_code(uint8_t k) { return k >= GC_KEY_MUP && k <= GC_KEY_MCLICK2; }

static void pad_key(int i, int down) {
    const uint8_t* k = padkeys[i];
    if (is_mouse_code(k[0])) {
        uint8_t bit = (uint8_t)(1u << (k[0] - GC_KEY_MUP));
        if (k[0] >= GC_KEY_MCLICK) { bit = (uint8_t)(1u << (k[0] - GC_KEY_MCLICK)); mouse_btns = down ? (mouse_btns | bit) : (mouse_btns & ~bit); }
        else mouse_dirs = down ? (mouse_dirs | bit) : (mouse_dirs & ~bit);
        return;
    }
    if (k[0] == GC_KEY_KEYBOARD || k[0] == GC_KEY_NONE || !k[0]) return;
    int n = 0;
    while (n < 3 && k[n]) n++;
    if (down) for (int j = 0; j < n; j++) key_push(k[j]);
    else for (int j = n - 1; j >= 0; j--) key_push(k[j] | 0x80);
}

/* typematic repeat like a real keyboard: a held button re-sends its last (non-modifier) make
   code after 0.5 s, 20 times a second. Games that move a cursor one step per key need it */
static uint16_t hold_frames[GC_BUTTONS];

static void pad_repeat(int i) {
    const uint8_t* k = padkeys[i];
    if (is_mouse_code(k[0])) return;
    if (k[0] == GC_KEY_KEYBOARD || k[0] == GC_KEY_NONE || !k[0]) return;
    int n = 0;
    while (n < 3 && k[n]) n++;
    uint8_t sc = k[n - 1];
    if (sc == 0x2A || sc == 0x36 || sc == 0x1D || sc == 0x38) return;   /* shift ctrl alt */
    if (++hold_frames[i] >= 30 && ((hold_frames[i] - 30) % 3) == 0) key_push(sc);
    if (hold_frames[i] > 60000) hold_frames[i] = 30;
}

static void release_pad_keys(void) {
    for (int i = 0; i < GC_BUTTONS; i++)
        if (prev_buttons & (1u << i)) pad_key(i, 0);
}

/* menus (on-screen keyboard, disk list): a held direction keeps moving the
   cursor, after 0.25 s, about 15 steps per second */
#define NAV_DIRS (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)
#define NAV_DELAY 15                   /* frames before the first repeat */
#define NAV_RATE  4                    /* frames between repeats */
static uint16_t nav_hold;

static uint32_t nav_repeat(uint32_t b, uint32_t pressed) {
    uint32_t held = b & NAV_DIRS;
    if (!held || (pressed & NAV_DIRS)) { nav_hold = 0; return pressed; }
    if (++nav_hold >= NAV_DELAY && (nav_hold - NAV_DELAY) % NAV_RATE == 0) {
        if (nav_hold > 1000) nav_hold = NAV_DELAY;
        return pressed | held;
    }
    return pressed;
}

static void handle_input(uint32_t b) {
    uint32_t pressed = b & ~prev_buttons;
    if (ui_mode != 0) pressed = nav_repeat(b, pressed);
    if (ui_mode == 0) {
        int osk = -1;
        for (int i = 0; i < GC_BUTTONS; i++)
            if ((pressed & (1u << i)) && padkeys[i][0] == GC_KEY_KEYBOARD) osk = i;
        if (osk >= 0) {
            release_pad_keys();
            ui_mode = 1;
        } else {
            for (int i = 0; i < GC_BUTTONS; i++) {
                uint32_t m = 1u << i;
                if ((b & m) && !(prev_buttons & m)) { pad_key(i, 1); hold_frames[i] = 0; }
                else if (b & m) pad_repeat(i);
                if (!(b & m) && (prev_buttons & m)) pad_key(i, 0);
            }
        }
    } else if (ui_mode == 1) {
        if (pressed & (BTN_B | BTN_TIME)) ui_mode = 0;
        else if (pressed & BTN_UP) osk_move_row(-1);
        else if (pressed & BTN_DOWN) osk_move_row(1);
        else if (pressed & BTN_LEFT) osk_col = (osk_col + row_len(osk_row) - 1) % row_len(osk_row);
        else if (pressed & BTN_RIGHT) osk_col = (osk_col + 1) % row_len(osk_row);
        else if (pressed & BTN_A) osk_press(osk_rows[osk_row][osk_col].sc);
    } else {
        if (pressed & (BTN_B | BTN_TIME)) ui_mode = 1;
        else if ((pressed & BTN_UP) && n_images) disk_sel = (disk_sel + n_images - 1) % n_images;
        else if ((pressed & BTN_DOWN) && n_images) disk_sel = (disk_sel + 1) % n_images;
        else if ((pressed & BTN_A) && n_images) { insert_a(images[disk_sel]); ui_mode = 0; }
    }
    prev_buttons = b;
}

/* ---- the bands above and below the picture ---- */
#define PIC_Y 20                          /* the PC picture: lines 20..219 */
static uint32_t load_pct = 0;

static void draw_bands(uint8_t* fb) {
    char line[48];
    ui_fill(fb, 0, 0, 320, PIC_Y, 0);
    ui_fill(fb, 0, PIC_Y + 200, 320, 240 - PIC_Y - 200, 0);
    if (ui_mode == 1) {
        for (int r = 0; r < OSK_ROWS; r++) {
            int x = 0, y = osk_row_y[r];
            for (int c = 0; osk_rows[r][c].label; c++) {
                const OSK_KEY* k = &osk_rows[r][c];
                int w = key_w(k);
                uint8_t bg = 1, fg = 15;
                if ((k->sc == 0x2A && latched_shift) || (k->sc == 0x1D && latched_ctrl) || (k->sc == 0x38 && latched_alt)) bg = 4;
                if (k->sc >= 0xF0) bg = 2;
                if (r == osk_row && c == osk_col) { bg = 14; fg = 0; }
                ui_fill(fb, x, y, w - 1, 8, bg);
                ui_text(fb, x + 2, y, k->label, fg, bg);
                x += w;
            }
        }
    } else if (ui_mode == 2) {
        /* disk menu: a list over the picture, the title in the band above */
        ui_text(fb, 0, 6, "Disco para A:  (A=insertar  B=volver)", 14, 0);
        ui_fill(fb, 0, PIC_Y, 320, 200, 0);
        if (!n_images) {
            char msg[48];
            snprintf(msg, sizeof(msg), "No hay .img en %.24s", dos_dir);
            ui_text(fb, 8, PIC_Y + 8, msg, 12, 0);
        }
        const int vis = 19;                         /* entries that fit, 10 lines apart */
        int first = disk_sel - vis / 2;
        if (first > n_images - vis) first = n_images - vis;
        if (first < 0) first = 0;
        for (int i = 0; i < vis && first + i < n_images; i++) {
            int sel = first + i == disk_sel, y = PIC_Y + 6 + i * 10;
            ui_fill(fb, 0, y - 1, 320, 10, sel ? 1 : 0);
            ui_text(fb, 8, y, images[first + i], sel ? 15 : 7, sel ? 1 : 0);
        }
    } else {
        if (cfg_name[0]) {
            snprintf(line, sizeof(line), gcfg.errors ? "cfg: %.24s (%d errores)" : "cfg: %.24s", cfg_name, gcfg.errors);
            ui_text(fb, 0, 2, line, gcfg.errors ? 12 : 10, 0);
        } else {
            ui_text(fb, 0, 2, "A=Intro B=Espacio GAME=Esc TIME=Teclado", 7, 0);
        }
        if (drive_b[0])
            snprintf(line, sizeof(line), "A:%.11s B:%.11s C:%.9s", drive_a[0] ? drive_a : "-", drive_b, drive_c[0] ? drive_c : "-");
        else
            snprintf(line, sizeof(line), "A:%.14s C:%.14s", drive_a[0] ? drive_a : "-", drive_c[0] ? drive_c : "-");
        ui_text(fb, 0, 11, line, 3, 0);
        snprintf(line, sizeof(line), "8088 %s MHz  carga %u%%", speeds[speed_idx].mhz, (unsigned)load_pct);
#ifdef XT_VGA_HLE
        extern uint32_t vga_dbg_drop;
        if (vga_active) {                                /* video mode and memory */
            size_t l = strlen(line);
            snprintf(line + l, sizeof(line) - l, "  %s %uK", vga_hle_is256() ? "VGA" : "EGA", (unsigned)(vga_plane_size / 256));
        }
        if (vga_dbg_drop > 64)                           /* the game used more video memory than there is */
            ui_text(fb, 0, 230, vga_plane_size < 65536 ? "Falta memoria de video: vram = 256 en cfg" : "Falta memoria de video", 12, 0);
#endif
        ui_text(fb, 0, 221, line, 8, 0);
    }
}

/* ---- emulation ---- */
static int16_t audio[PLAT_AUDIO_SAMPLES];
static uint32_t vt_rem = 0, au_rem = 0;
static int32_t dc = 0;

static int16_t speaker_sample(void) {
    int32_t a = pcspeaker_getSample(&machine.pcspeaker);   /* 0..15000 */
    dc += (a - dc) >> 9;                                    /* remove DC */
    int32_t s = a - dc;
    if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
    return (int16_t)s;
}

#ifdef XT_SPK_HLE
void spk_hle_frame_begin(const uint64_t* exec, uint32_t instructions, uint32_t ips, int16_t* buf, int n);
void spk_hle_chunk(uint32_t done, uint32_t len);
void spk_hle_frame_end(void);
#endif

static void run_frame(void) {
    const uint32_t chunk = instructionsperloop;
    const uint32_t target = ips / PLAT_FPS;
    int si = 0;
#ifdef XT_SPK_HLE
    /* the speaker is rendered from the guest's port writes, timed to the instruction */
    spk_hle_frame_begin(&machine.CPU.totalexec, (target + chunk - 1) / chunk * chunk, ips, audio, PLAT_AUDIO_SAMPLES);
#endif
    for (uint32_t done = 0; done < target; done += chunk) {
        cpu_interruptCheck(&machine.CPU, &machine.i8259);
#ifdef XT_SPK_HLE
        spk_hle_chunk(done, chunk);
#endif
        cpu_exec(&machine.CPU, chunk);
        uint32_t t = chunk * 1000000u + vt_rem;            /* advance virtual time */
        timing_virtual_us += t / ips;
        vt_rem = t % ips;
        timing_loop();
#ifndef XT_SPK_HLE
        au_rem += chunk * PLAT_AUDIO_RATE;                 /* audio at 48 kHz of virtual time */
        while (au_rem >= ips) {
            au_rem -= ips;
            if (si < PLAT_AUDIO_SAMPLES) audio[si++] = speaker_sample();
        }
#endif
        key_pump(chunk);
    }
#ifdef XT_SPK_HLE
    spk_hle_frame_end();
    (void)si;
#else
    int16_t last = si ? audio[si - 1] : 0;
    while (si < PLAT_AUDIO_SAMPLES) audio[si++] = last;
#endif
    cga_doDraw = 0;
}

/* ---- "run =": type commands at the DOS prompt through the BIOS keyboard buffer ----
   Characters go straight into the BIOS ring buffer (40:1E..40:3D), so what DOS reads is
   the ASCII code itself, independent of KEYB's keyboard layout. Each command is typed
   only when a DOS prompt ("X:\...>" with the cursor right after it) is on screen. */
static const char* run_ptr = NULL;
static int run_state = 0;                 /* 0 idle, 1 wait prompt, 2 typing, 3 wait next prompt */
static int prompt_frames = 0;
static uint16_t run_cursor = 0;

static uint8_t bda8(uint32_t o) { return cpu_read(&machine.CPU, 0x400 + o); }
static uint16_t bda16(uint32_t o) { return (uint16_t)(bda8(o) | (bda8(o + 1) << 8)); }

static int dos_prompt_visible(void) {
    uint8_t mode = bda8(0x49);
    if (mode > 3) return 0;
    int cols = bda16(0x4A); if (cols != 40 && cols != 80) return 0;
    uint16_t cur = bda16(0x50);
    int col = cur & 0xFF, row = cur >> 8;
    if (col < 3 || row > 24) return 0;
    uint32_t line = 0xB8000 + (uint32_t)row * cols * 2;
    uint8_t c0 = cpu_read(&machine.CPU, line), c1 = cpu_read(&machine.CPU, line + 2);
    uint8_t last = cpu_read(&machine.CPU, line + (col - 1) * 2);
    return last == '>' && c1 == ':' && ((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z'));
}

static int bios_kbd_put(uint16_t key) {
    uint16_t head = bda16(0x1A), tail = bda16(0x1C);
    uint16_t next = (uint16_t)(tail + 2);
    if (next >= 0x3E) next = 0x1E;
    if (next == head) return 0;                       /* buffer full */
    cpu_write(&machine.CPU, 0x400 + tail, (uint8_t)key);
    cpu_write(&machine.CPU, 0x400 + tail + 1, (uint8_t)(key >> 8));
    cpu_write(&machine.CPU, 0x41C, (uint8_t)next);
    cpu_write(&machine.CPU, 0x41D, (uint8_t)(next >> 8));
    return 1;
}

static void autorun_frame(void) {
    if (!run_state) return;
    int prompt = dos_prompt_visible();
    if (run_state == 1 || run_state == 3) {
        uint16_t cur = bda16(0x50);
        if (prompt && (run_state == 1 || cur != run_cursor)) prompt_frames++;
        else prompt_frames = 0;
        if (prompt_frames >= 20) { run_state = 2; prompt_frames = 0; }
        return;
    }
    for (int i = 0; i < 4; i++) {                     /* typing: a few characters per frame */
        char ch = *run_ptr;
        if (ch == 0 || ch == ';') {
            if (!bios_kbd_put(0x1C0D)) return;        /* Enter */
            run_cursor = bda16(0x50);
            if (ch == 0) { run_state = 0; return; }
            run_ptr++;
            while (*run_ptr == ' ') run_ptr++;
            run_state = *run_ptr ? 3 : 0;
            return;
        }
        uint16_t k = gamecfg_char_to_bioskey(ch);
        if (k && !bios_kbd_put(k)) return;
        run_ptr++;
    }
}

/* pointer speed: 1 pixel per frame at first, up to 5 when held */
static void mouse_frame(void) {
#ifdef XT_MOUSE_HLE
    if (!mouse_hle_enabled) return;
    mouse_hle_install(&machine.CPU);
    if (mouse_dirs) {
        int sp = 1 + mouse_hold / 10;
        if (sp > 5) sp = 5;
        if (mouse_hold < 1000) mouse_hold++;
        int dx = 0, dy = 0;
        if (mouse_dirs & 1) dy -= sp;
        if (mouse_dirs & 2) dy += sp;
        if (mouse_dirs & 4) dx -= sp;
        if (mouse_dirs & 8) dx += sp;
        mouse_hle_move(dx, dy);
    } else mouse_hold = 0;
    mouse_hle_buttons(mouse_btns);
#endif
}

static void apply_config(void) {
    if (gcfg.mouse) {                                   /* d-pad = pointer, A/B = buttons */
        static const uint8_t mk[6] = { GC_KEY_MUP, GC_KEY_MDOWN, GC_KEY_MLEFT, GC_KEY_MRIGHT, GC_KEY_MCLICK, GC_KEY_MCLICK2 };
        for (int i = 0; i < 6; i++) { padkeys[i][0] = mk[i]; padkeys[i][1] = 0; }
    }
    for (int i = 0; i < GC_BUTTONS; i++)
        if (gcfg.set[i]) memcpy(padkeys[i], gcfg.keys[i], 3);
#ifdef XT_MOUSE_HLE
    for (int i = 0; i < GC_BUTTONS; i++) if (is_mouse_code(padkeys[i][0])) mouse_hle_enabled = 1;
#endif
    int has_osk = 0;                                   /* keep a way to open the keyboard */
    for (int i = 0; i < GC_BUTTONS; i++) if (padkeys[i][0] == GC_KEY_KEYBOARD) has_osk = 1;
    if (!has_osk) { padkeys[7][0] = GC_KEY_KEYBOARD; padkeys[7][1] = 0; }
    if (gcfg.cpu_mhz10) {
        int best = 0, bestd = 1 << 30;
        for (int i = 0; i < (int)NSPEEDS; i++) {
            int m = 0; const char* p = speeds[i].mhz;
            while (*p >= '0' && *p <= '9') m = m * 10 + (*p++ - '0');
            m *= 10; if (*p == '.') m += p[1] - '0';
            int d = m > gcfg.cpu_mhz10 ? m - gcfg.cpu_mhz10 : gcfg.cpu_mhz10 - m;
            if (d < bestd) { bestd = d; best = i; }
        }
        speed_idx = best; ips = speeds[best].ips;
    }
    if (gcfg.zoom80 >= 0) cga_zoom80 = gcfg.zoom80;
    if (gcfg.run[0]) { run_ptr = gcfg.run; while (*run_ptr == ' ') run_ptr++; run_state = *run_ptr ? 1 : 0; }
}

/* file size, or -1 if it cannot be opened */
static long file_size(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    fclose(f);
    return n;
}

static const char* base_name(const char* path) {
    const char* b = strrchr(path, '/');
    return b ? b + 1 : path;
}

/* find and apply the per-game .cfg (before the machine is built: it can choose the video card) */
static void load_config(void) {
    const char* boot = plat_boot_image();
    if (!boot || !boot[0]) return;
    snprintf(dos_dir, sizeof(dos_dir), "%s", boot);
    char* slash = strrchr(dos_dir, '/');
    if (slash) *slash = 0; else strcpy(dos_dir, ".");
    if (!dos_dir[0]) strcpy(dos_dir, "/");

    /* per-game configuration: /cores/dos/<stem>.cfg (next to the core, hidden from the rom list),
       else <dir>/cfg/<stem>.cfg, else <dir>/<stem>.cfg */
    {
        char stem[64], cpath[320];
        snprintf(stem, sizeof(stem), "%s", base_name(boot));
        char* dot = strrchr(stem, '.'); if (dot) *dot = 0;
        snprintf(cpath, sizeof(cpath), "%s/%s.cfg", GWDOS_CFG_DIR, stem);
        if (!gamecfg_load(cpath, &gcfg)) {
            snprintf(cpath, sizeof(cpath), "%s/cfg/%s.cfg", dos_dir, stem);
            if (!gamecfg_load(cpath, &gcfg)) {
                snprintf(cpath, sizeof(cpath), "%s/%s.cfg", dos_dir, stem);
                gamecfg_load(cpath, &gcfg);
            }
        }
        if (gcfg.loaded) { snprintf(cfg_name, sizeof(cfg_name), "%s.cfg", stem); apply_config(); }
    }
}

/* insert the launcher's image (floppy -> A:, hard disk -> C:) and pick the boot drive */
static void insert_boot_disks(void) {
    const char* boot = plat_boot_image();
    if (!boot || !boot[0]) { drive_a[0] = 0; bootdrive = 0x00; return; }

    long size = file_size(boot);
    if (gcfg.loaded && gcfg.boot[0]) {               /* boot DOS from another image, game as B:/C: */
        char bpath[320];
        if (gcfg.boot[0] == '/') snprintf(bpath, sizeof(bpath), "%s", gcfg.boot);
        else {                                       /* /cores/dos/<boot>, else next to the game */
            snprintf(bpath, sizeof(bpath), "%s/%s", GWDOS_CFG_DIR, gcfg.boot);
            if (file_size(bpath) <= 0) snprintf(bpath, sizeof(bpath), "%s/%s", dos_dir, gcfg.boot);
        }
        if (biosdisk_insert(&machine.CPU, 0, bpath) == 0) snprintf(drive_a, sizeof(drive_a), "%s", base_name(bpath));
        else snprintf(drive_a, sizeof(drive_a), "(error)");
        bootdrive = 0x00;
        if (size > (long)FLOPPY_MAX_BYTES) {
            if (biosdisk_insert(&machine.CPU, 2, (char*)boot) == 0) snprintf(drive_c, sizeof(drive_c), "%s", base_name(boot));
        } else if (biosdisk_insert(&machine.CPU, 1, (char*)boot) == 0) {
            snprintf(drive_b, sizeof(drive_b), "%s", base_name(boot));
        }
        if (!drive_c[0]) {
            char cimg[320];
            snprintf(cimg, sizeof(cimg), "%s/c.img", dos_dir);
            if (file_size(cimg) > (long)FLOPPY_MAX_BYTES && biosdisk_insert(&machine.CPU, 2, cimg) == 0)
                snprintf(drive_c, sizeof(drive_c), "c.img");
        }
        return;
    }
    if (size > (long)FLOPPY_MAX_BYTES) {
        if (biosdisk_insert(&machine.CPU, 2, (char*)boot) == 0) snprintf(drive_c, sizeof(drive_c), "%s", base_name(boot));
        bootdrive = 0x80;
        return;
    }
    if (biosdisk_insert(&machine.CPU, 0, (char*)boot) == 0) snprintf(drive_a, sizeof(drive_a), "%s", base_name(boot));
    else snprintf(drive_a, sizeof(drive_a), "(error)");
    bootdrive = 0x00;

    char cpath[320];
    snprintf(cpath, sizeof(cpath), "%s/c.img", dos_dir);
    if (strcmp(base_name(boot), "c.img") != 0 && file_size(cpath) > (long)FLOPPY_MAX_BYTES &&
        biosdisk_insert(&machine.CPU, 2, cpath) == 0)
        snprintf(drive_c, sizeof(drive_c), "c.img");
}

static int machine_ok = 0;

/* LCD palette: 0..15 CGA colours (also the UI), 16..31 the EGA/VGA 16-colour palette,
   or all 256 entries from the VGA DAC in mode 13h (the UI then uses the nearest colours) */
static int clut_kind = -1;

static void update_palette(int force) {
#ifdef XT_VGA_HLE
    int kind = !vga_active ? 0 : (vga_hle_is256() ? 2 : 1);
    if (!force && kind == clut_kind && !(kind && vga_pal_dirty)) return;
    static uint32_t clut[256], last[256];
    static int last_n = 0;
    if (kind && !force && kind == clut_kind) {         /* games often rewrite an unchanged palette */
        int n = kind == 1 ? 16 : 256;
        uint32_t tmp[256];
        if (kind == 1) vga_hle_palette16(tmp); else vga_hle_palette256(tmp);
        if (memcmp(tmp, last + (kind == 1 ? 16 : 0), n * sizeof(uint32_t)) == 0 && last_n == (kind == 1 ? 32 : 256)) return;
    }
    clut_kind = kind;
    for (int i = 0; i < 16; i++) ui_cmap[i] = (uint8_t)i;
    if (kind == 0) { plat_set_palette(cga_rgb, 16); last_n = 16; return; }
    if (kind == 1) {
        memcpy(clut, cga_rgb, sizeof(cga_rgb));
        vga_hle_palette16(clut + 16);
        plat_set_palette(clut, 32);
        memcpy(last, clut, 32 * sizeof(uint32_t)); last_n = 32;
        return;
    }
    vga_hle_palette256(clut);
    for (int i = 0; i < 16; i++) {                       /* nearest game colour for each UI colour */
        int r = (cga_rgb[i] >> 16) & 255, g = (cga_rgb[i] >> 8) & 255, b = cga_rgb[i] & 255;
        int best = 0, bd = 1 << 30;
        for (int n = 0; n < 256; n++) {
            int dr = r - (int)((clut[n] >> 16) & 255), dg = g - (int)((clut[n] >> 8) & 255), db = b - (int)(clut[n] & 255);
            int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
            if (d < bd) { bd = d; best = n; }
        }
        ui_cmap[i] = (uint8_t)best;
    }
    plat_set_palette(clut, 256);
    memcpy(last, clut, sizeof(clut)); last_n = 256;
#else
    if (force || clut_kind != 0) { clut_kind = 0; plat_set_palette(cga_rgb, 16); }
#endif
}

static void draw_screen(uint8_t* fb) {
#ifdef XT_VGA_HLE
    if (machine_ok && vga_active) vga_hle_render(fb + PIC_Y * 320, 16);
    else
#endif
    if (machine_ok) cga_render(fb, PIC_Y);
    else {
        ui_fill(fb, 0, PIC_Y, 320, 200, 0);
        ui_text(fb, 8, PIC_Y + 80, "gw-dos: sin memoria para el PC", 12, 0);
        ui_text(fb, 8, PIC_Y + 96, "(no se pudo iniciar la maquina)", 7, 0);
    }
#ifdef XT_MOUSE_HLE
    int cx, cy;
    if (machine_ok && mouse_hle_cursor(&machine.CPU, &cx, &cy)) {   /* driver cursor: an arrow */
        static const uint16_t arrow[11] = { 0x8000, 0xC000, 0xA000, 0x9000, 0x8800, 0x8400, 0x8E00, 0xB000, 0xD000, 0x8800, 0x0800 };
        static const uint16_t fill[11]  = { 0x0000, 0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7000, 0x4000, 0x0000, 0x0000, 0x0000 };
        for (int j = 0; j < 11; j++) for (int i = 0; i < 8; i++) {
            int x = cx + i, y = cy + j;
            if (x < 0 || x >= 320 || y < 0 || y >= 200) continue;
            y += PIC_Y;
            if (arrow[j] & (0x8000 >> i)) fb[y * 320 + x] = ui_cmap[0];
            else if (fill[j] & (0x8000 >> i)) fb[y * 320 + x] = ui_cmap[15];
        }
    }
#endif
    draw_bands(fb);
    if (osd_frames) {                               /* CPU speed change: over the band above */
        char msg[48];
        osd_frames--;
        snprintf(msg, sizeof(msg), " CPU: 8088 a %s MHz%s%s ", speeds[speed_idx].mhz,
                 speeds[speed_idx].pc[0] ? " - " : "", speeds[speed_idx].pc);
        int w = (int)strlen(msg) * 8;
        if (w > 320) w = 320;
        ui_fill(fb, (320 - w) / 2 - 2, 4, w + 4, 12, 15);
        ui_text(fb, (320 - w) / 2, 6, msg, 0, 15);
    }
}

#ifdef GWDOS_HOST
#include <stdlib.h>
/* host test harness only: one-line CPU / video state for logs */
void dos_debug_state(char* buf, size_t n) {
    I8253_t* t = &machine.i8253;
    if (getenv("GWDOS_CPU")) {
        CPU_t* c = &machine.CPU;
        uint32_t lin = ((uint32_t)c->segregs[regcs] << 4) + c->ip;
        snprintf(buf, n, "CS:IP=%04X:%04X SS:SP=%04X:%04X op=%02X rep=%d segovr=%d AX=%04X BX=%04X CX=%04X DX=%04X DS=%04X ES=%04X SI=%04X DI=%04X fl=%04X bytes=%02X %02X %02X %02X tf=%d",
                 c->segregs[regcs], c->ip, c->segregs[regss], c->regs.wordregs[regsp], c->opcode, c->reptype, c->segoverride,
                 c->regs.wordregs[regax], c->regs.wordregs[regbx], c->regs.wordregs[regcx], c->regs.wordregs[regdx],
                 c->segregs[regds], c->segregs[reges], c->regs.wordregs[regsi], c->regs.wordregs[regdi], makeflagsword(c),
                 cpu_read(c, lin), cpu_read(c, lin + 1), cpu_read(c, lin + 2), cpu_read(c, lin + 3), c->tf);
        return;
    }
#ifdef XT_VGA_HLE
    if (getenv("GWDOS_VGA")) {
        extern uint32_t vga_dbg_drop, vga_dbg_max;
        snprintf(buf, n, "vga active=%d mode=%02X drops=%u maxaddr=%05X start=%02X%02X off=%02X crtc9=%02X 3C4[4]=?", vga_active, vga_mode,
                 (unsigned)vga_dbg_drop, (unsigned)vga_dbg_max, cga_datareg[0x0C], cga_datareg[0x0D], cga_datareg[0x13], cga_datareg[9]);
        return;
    }
#endif
    if (getenv("GWDOS_VID")) {
        int nz = 0, nz2 = 0;
        for (int i = 0; i < 16384; i++) { if (cga_RAM[i]) nz++; }
        for (uint32_t a = 0xA0000; a < 0xB8000; a += 4) if (cpu_read(&machine.CPU, a)) nz2++;
        snprintf(buf, n, "CS:IP=%04X:%04X mode=%02X csel=%02X start=%04X vram_nz=%d A000-B7FF_nz=%d int10_vec=%04X:%04X bda_mode=%02X",
                 machine.CPU.segregs[regcs], machine.CPU.ip, cga_regs[8], cga_regs[9],
                 (cga_datareg[0x0C] << 8) | cga_datareg[0x0D], nz, nz2,
                 cpu_read(&machine.CPU, 0x42) | (cpu_read(&machine.CPU, 0x43) << 8), cpu_read(&machine.CPU, 0x40) | (cpu_read(&machine.CPU, 0x41) << 8),
                 cpu_read(&machine.CPU, 0x449));
        return;
    }
    snprintf(buf, n, "CS:IP=%04X:%04X IF=%d instr=%llu PIC imr=%02X irr=%02X isr=%02X | PIT0 m%d act%d cnt%ld rel%ld rl%d | PIT2 m%d cnt%ld rel%ld | tick=%u",
             machine.CPU.segregs[regcs], machine.CPU.ip, machine.CPU.ifl, (unsigned long long)machine.CPU.totalexec,
             machine.i8259.imr, machine.i8259.irr, machine.i8259.isr,
             t->mode[0], t->active[0], (long)t->counter[0], (long)t->reload[0], t->rlmode[0],
             t->mode[2], (long)t->counter[2], (long)t->reload[2],
             (unsigned)(cpu_read(&machine.CPU, 0x46C) | (cpu_read(&machine.CPU, 0x46D) << 8)));
}
#endif

/* used by the platform when its own menu needs the game picture redrawn */
void dos_repaint(void) { draw_screen(plat_framebuffer()); }

int dos_main(int start_paused) {
    plat_init(start_paused != 0);
    update_palette(1);
    speed_idx = (int)plat_setting_get("CpuMHz", 1);
    if (speed_idx < 0 || speed_idx >= (int)NSPEEDS) speed_idx = 1;
    ips = speeds[speed_idx].ips;
    osd_frames = 3 * PLAT_FPS;                /* show the speed for a moment at start-up */
    cga_zoom80 = plat_setting_get("Zoom80", 1) ? 1 : 0;

    for (int i = 0; ; i++) {
        uint8_t* base;
        size_t size = plat_mem_region(i, &base);
        if (!size) break;
        xt_pool_add(base, size);
    }

    timing_virtual_enable = 1;
    ports_init();
    timing_init();
    memory_init();
    machine.pcap_if = -1;
    load_config();
#ifdef XT_VGA_HLE
    /* EGA/VGA: 128 KB of video memory by default (64/256 with "vram =" in the cfg), taken
       before the guest RAM, which gets the rest */
    if (gcfg.video != 1 && plat_setting_get("Video", 1)) {
        uint32_t kb = gcfg.vram_kb ? (uint32_t)gcfg.vram_kb : 128;
        while (kb >= 64 && vga_hle_init(kb * 256) != 0) kb /= 2;
    }
#endif
    machine_ok = machine_init(&machine, usemachine) >= 0;
    int ok = machine_ok;
#ifdef XT_CGA_STATUS_ICOUNT
    cga_status_icount = &machine.CPU.totalexec;    /* 3DAh retrace bits follow the emulated CPU */
    cga_status_ips = &ips;
#endif

    if (ok) insert_boot_disks();

    uint32_t sec_t0 = plat_ms(), emu_ms = 0, frame_no = 0;
    for (;;) {
        bool draw = plat_frame_begin();
        /* the LCD switches to 8-bit mode at a vsync and the CLUT is ignored until then:
           program the palette again during the first frames */
        ++frame_no;
        update_palette(frame_no == 2 || frame_no == 10 || frame_no == 60);
        handle_input(plat_buttons());
        if (ok) {
            uint32_t t0 = plat_ms();
            run_frame();
            autorun_frame();
            mouse_frame();
            emu_ms += plat_ms() - t0;
        } else {
            memset(audio, 0, sizeof(audio));
        }
        uint32_t now = plat_ms();
        if (now - sec_t0 >= 1000) { load_pct = emu_ms * 100u / (now - sec_t0); emu_ms = 0; sec_t0 = now; }
        if (draw) {
            draw_screen(plat_framebuffer());
            plat_present();
        }
        plat_audio(audio, PLAT_AUDIO_SAMPLES);
        plat_frame_end();
    }
    return 0;
}
