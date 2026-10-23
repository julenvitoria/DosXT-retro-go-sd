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
#ifdef GWDOS_HOST
#include <stdlib.h>
#endif
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
#define SC_COLON 0xF3                     /* ":" in one key: Shift + ";" */
typedef struct { const char* label; uint8_t sc; } OSK_KEY;
/* four rows: 0-1 in the band above the picture, 2-3 in the band below. A key is
   label x 8 + 4 pixels wide; every row fits in 320. */
static const OSK_KEY osk_r0[] = { {"Esc",0x01},{"1",0x02},{"2",0x03},{"3",0x04},{"4",0x05},{"5",0x06},{"6",0x07},{"7",0x08},{"8",0x09},{"9",0x0A},{"0",0x0B},{"-",0x0C},{"=",0x0D},{"\x1B-",0x0E},
                                  {"F1",0x3B},{"F2",0x3C},{"F3",0x3D},{"F4",0x3E},{"F5",0x3F},{"F6",0x40},{NULL,0} };
static const OSK_KEY osk_r1[] = { {"Tab",0x0F},{"Q",0x10},{"W",0x11},{"E",0x12},{"R",0x13},{"T",0x14},{"Y",0x15},{"U",0x16},{"I",0x17},{"O",0x18},{"P",0x19},{"[",0x1A},{"]",0x1B},{"Ent",0x1C},
                                  {"F7",0x41},{"F8",0x42},{"F9",0x43},{"F10",0x44},{"Del",0x53},{NULL,0} };
static const OSK_KEY osk_r2[] = { {"Ctl",0x1D},{"A",0x1E},{"S",0x1F},{"D",0x20},{"F",0x21},{"G",0x22},{"H",0x23},{"J",0x24},{"K",0x25},{"L",0x26},{";",0x27},{":",SC_COLON},{"'",0x28},{"\\",0x2B},{"`",0x29},
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
    /* The latches are STATE ONLY: no make code goes out when Shift/Ctrl/Alt is
       latched, the modifier is wrapped round the next on-screen key instead.
       Holding the make while the keyboard was closed turned the d-pad's arrows
       into 8/2/4/6 (Shift + keypad) at the DOS prompt. */
    if (sc == 0x2A) { latched_shift = (uint8_t)((latched_shift + 1) % 3); return; }  /* off -> next key -> locked -> off */
    if (sc == 0x1D) { latched_ctrl ^= 1; return; }
    if (sc == 0x38) { latched_alt ^= 1; return; }
    int shift = latched_shift || sc == SC_COLON;   /* ":" in one key: Shift + ";" */
    if (latched_ctrl) key_push(0x1D);
    if (latched_alt)  key_push(0x38);
    if (shift)        key_push(0x2A);
    key_tap(sc == SC_COLON ? 0x27 : sc);
    if (shift)        key_push(0x2A | 0x80);
    if (latched_alt)  key_push(0x38 | 0x80);
    if (latched_ctrl) key_push(0x1D | 0x80);
    if (latched_shift == 1) latched_shift = 0;      /* 2 = locked: stays on for the next keys */
    latched_ctrl = latched_alt = 0;
}

/* ---- disk menu ---- */
#define MAX_IMAGES 24
static char images[MAX_IMAGES][40];
static int n_images = 0, disk_sel = 0;
static char drive_a[48] = "", drive_b[48] = "", drive_c[48] = "", drive_d[48] = "";
static char boot_missing[40];             /* the cfg's "boot =" image that was not found */

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
#define EJECT_ENTRY "(sacar el disco de A:)"
static void open_disk_menu(void) {
    n_images = 0;
    if (drive_a[0] && drive_a[0] != '-') strcpy(images[n_images++], EJECT_ENTRY);   /* first entry */
    plat_list_dir(dos_dir, add_image, NULL);
    disk_sel = 0;
    ui_mode = 2;
}
static void insert_a(const char* name) {
    char path[320];
    if (!strcmp(name, EJECT_ENTRY)) {
        /* an empty drive, as when an installer says "remove the disk and press ENTER":
           the reboot then starts from C: (biosdisk_int19h, static_mem.py) */
        biosdisk_eject(&machine.CPU, 0);
        drive_a[0] = 0;
        return;
    }
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

static int run_state;                     /* "run =" autotyping (below): non-zero while it works */
static int dos_prompt_visible(void);

static void handle_input(uint32_t b) {
    uint32_t pressed = b & ~prev_buttons;
    /* While "run =" is still typing its commands, a button press would land in the middle
       of them: Esc cancels the DOS line (DOS prints "\" and drops to a new line with no
       prompt), the autotyper never sees a prompt again and the game never starts until
       Enter is pressed by hand. So the pad (not the on-screen keyboard's door) waits --
       from the moment a DOS prompt is on screen. Before that it still works, so Esc can
       skip the BIOS memory test. */
    if (ui_mode == 0 && run_state && (run_state != 1 || dos_prompt_visible())) {
        uint32_t door = 0;
        for (int i = 0; i < GC_BUTTONS; i++)
            if (padkeys[i][0] == GC_KEY_KEYBOARD) door |= 1u << i;
        b &= door;
        pressed &= door;
    }
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

#ifdef GWDOS_PROF
/* Profiling builds (EXTRA_DEFS=-DGWDOS_PROF): where each frame's cycles go, shown on the
   bottom line -- cpu = the 8088 interpreter (port I/O, video memory writes and the AdLib
   synthesis triggered by register writes included), opl = the AdLib synthesis on its own,
   snd = speaker / mixing / hand-off, vid = drawing the LCD, esp = waiting for the audio DMA;
   c/i = console cycles per emulated instruction. */
enum { P_CPU, P_SND, P_VID, P_WAIT, P_N };
static uint32_t prof_acc[P_N], prof_pct[P_N + 1], prof_cpi;
extern uint32_t adlib_prof_cyc;
#define PROF_T0() uint32_t prof_t0 = plat_cycles()
#define PROF_ADD(k) do { uint32_t t_ = plat_cycles(); prof_acc[k] += t_ - prof_t0; prof_t0 = t_; } while (0)
#else
#define PROF_T0() do { } while (0)
#define PROF_ADD(k) do { } while (0)
#endif

/* ---- the bands above and below the picture ---- */
#define PIC_Y 20                          /* the PC picture: lines 20..219 */
static uint32_t load_pct = 0;

/* The status lines show for INFO_MS after start-up and then get out of the way: the top
   band (cfg, drives) always, the bottom band (CPU, MHz, load) unless "Info" is ON in
   retro-go's PAUSE/SET menu (platform_core.c), saved as the setting "InfoBar". A video
   memory warning stays up regardless -- it says why the picture is wrong. */
#define INFO_MS 7000u
static uint32_t info_until;               /* plat_ms() when the start-up window closes */
static int info_on = 0;
int  dos_info_get(void) { return info_on; }
void dos_info_set(int on) { info_on = on ? 1 : 0; plat_setting_set("InfoBar", info_on); }

/* ---- speed governor ----
   The chosen speed is a CEILING. If the console cannot emulate that many instructions per
   frame in real time, frames run late, retro-go's audio buffer drains between them and the
   PC speaker crackles and stutters (Monkey Island at 16 MHz). So when fewer than ~58 frames
   a second get through, the emulated CPU is slowed down to what fits, and sped up again
   (never past the chosen speed) once there is headroom. Virtual time follows ips, so the
   PIT -- music tempo and note pitch -- stays right; only CPU-bound game logic runs slower,
   exactly as if a slower speed had been picked by hand. */
#define IPS_MIN 300000u
static void speed_governor(uint32_t frames, uint32_t ms, uint32_t busy, uint32_t misses) {
    static uint8_t hold;                   /* half seconds before it may speed up again */
    static int32_t deficit;                /* frames behind the wall clock, x1000 */
    static uint32_t cap;                   /* a speed that already lost frames: stay under it */
    static uint16_t cap_left;              /* half seconds the cap still applies */
    uint32_t ceiling = speeds[speed_idx].ips;
    if (!ms || ms > 1500) return;      /* a long stall is retro-go's menu or a disk load, not speed */
    /* THE SIGNAL THAT MATTERS IS FRAMES BEHIND THE CLOCK. Each frame is 1/60 s of music; the
       audio DMA plays 60 blocks a second whatever happens, so every frame that is not ready
       in time stretches the music by 16.7 ms. The first versions let up to 4 % through
       (fps >= 57.5) and judged the rest by load, which on the console reads 65-80 % while
       frames are still being lost now and then: Monkey Island's AdLib music came out ~8 %
       slow next to the same music rendered here, and "slowed down and sped up" as the
       governor crept back up into the limit every 10 s. Now the frames owed are counted
       against the clock across windows, and one whole frame owed is a cut. */
    /* Leaky: a lone late frame (a heavy screen update, say) is lost whatever the speed and
       must not walk the speed down; losses that keep coming are the CPU's. */
    int32_t owed = (int32_t)(ms * 60u) - (int32_t)(frames * 1000u);   /* frames behind the clock */
    int32_t missed = (int32_t)misses * 1000;                           /* blocks replayed */
    deficit = deficit * 9 / 10 + (missed > owed ? missed : owed);      /* ~5 s memory */
    if (deficit < -500) deficit = -500;
    if (cap_left && !--cap_left) cap = 0;
    uint64_t n = ips;
    /* ...and only with the console actually busy, or frames visibly short of 60 a second (an
       overrun frame takes two DMA slots, so a badly overloaded console can READ ~55 % busy):
       at 8 MHz with ~40 % idle the console
       still reported the odd missed block (the firmware's own hiccups -- an SD access, its
       frame pacing) and the governor cut 8 -> 7.4-7.6 MHz for nothing. */
    uint32_t fps10 = frames * 10000u / ms;                   /* frames/s x 10 */
    if (deficit >= 4000 && (busy >= 75 || fps10 < 575)) {  /* frames keep being lost: ~1/s or more */
        if (!cap || ips < cap) cap = ips;                     /* this speed is too much */
        cap_left = 120;                                       /* for a minute */
        n = n * 92u / 100u; hold = 20; deficit = 0;
    }
    else if (busy > 85) { n = n * 78u / busy; hold = 20; }    /* no slack left */
    else if (hold) hold--;
    else if (busy < 70 && ips < ceiling) n = n + n / 33;      /* +3 % */
    if (cap && n > (uint64_t)cap * 95u / 100u) n = (uint64_t)cap * 95u / 100u;
    ips = n < IPS_MIN ? IPS_MIN : (uint32_t)n;
    if (ips > ceiling) ips = ceiling;
}

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
                if (k->sc >= 0xF0) bg = 2;
                /* latched for the next key = green, Shift locked = red */
                if ((k->sc == 0x2A && latched_shift == 1) || (k->sc == 0x1D && latched_ctrl) || (k->sc == 0x38 && latched_alt)) bg = 2;
                if (k->sc == 0x2A && latched_shift == 2) bg = 4;
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
        int early = (int32_t)(info_until - plat_ms()) > 0;
        if (early) {
        if (cfg_name[0]) {
            snprintf(line, sizeof(line), gcfg.errors ? "cfg: %.24s (%d errores)" : "cfg: %.24s", cfg_name, gcfg.errors);
            ui_text(fb, 0, 2, line, gcfg.errors ? 12 : 10, 0);
        } else {
            ui_text(fb, 0, 2, "A=Intro B=Espacio GAME=Esc TIME=Teclado", 7, 0);
        }
        if (drive_d[0])
            snprintf(line, sizeof(line), "A:%.11s C:%.11s D:%.9s", drive_a[0] ? drive_a : "-", drive_c, drive_d);
        else if (drive_b[0])
            snprintf(line, sizeof(line), "A:%.11s B:%.11s C:%.9s", drive_a[0] ? drive_a : "-", drive_b, drive_c[0] ? drive_c : "-");
        else
            snprintf(line, sizeof(line), "A:%.14s C:%.14s", drive_a[0] ? drive_a : "-", drive_c[0] ? drive_c : "-");
        ui_text(fb, 0, 11, line, 3, 0);
        }
        if (ips < speeds[speed_idx].ips) {               /* the governor has slowed it down */
            unsigned m10 = (unsigned)((ips + 3300) / 6600);  /* ~66,000 instr/s per MHz */
            snprintf(line, sizeof(line), "8088 %s->%u.%u MHz  carga %u%%", speeds[speed_idx].mhz, m10 / 10, m10 % 10, (unsigned)load_pct);
        } else
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
        if (early || info_on) ui_text(fb, 0, 221, line, 8, 0);
#ifdef GWDOS_PROF
        snprintf(line, sizeof(line), "cpu%u opl%u snd%u vid%u esp%u %uc/i", (unsigned)prof_pct[P_CPU], (unsigned)prof_pct[P_N],
                 (unsigned)prof_pct[P_SND], (unsigned)prof_pct[P_VID], (unsigned)prof_pct[P_WAIT], (unsigned)prof_cpi);
        ui_text(fb, 0, 230, line, 14, 0);
#endif
        if (boot_missing[0]) {                           /* stays up: it is why nothing boots */
            snprintf(line, sizeof(line), "No encuentro %.26s", boot_missing);
            ui_fill(fb, 0, 0, 320, PIC_Y, 0);
            ui_text(fb, 0, 2, line, 12, 0);
            ui_text(fb, 0, 11, "Ponlo en /cores/dos o junto al juego", 12, 0);
        }
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
#ifdef XT_ADLIB_HLE
void adlib_hle_init(const uint64_t* exec, const uint32_t* ips);
void adlib_hle_frame_begin(unsigned n, unsigned rate, uint32_t frame_instr);
void adlib_hle_chunk(uint32_t done, uint32_t len);
void adlib_hle_frame_end_mix(int16_t* dst);
#endif
#ifdef XT_SB_HLE
void sb_hle_init(I8237_t* d, I8259_t* p, const uint64_t* exec, const uint32_t* ips);
void sb_hle_chunk(uint32_t done);
void sb_hle_tick(uint32_t done, uint32_t n);
void sb_hle_render_mix(int16_t* dst, unsigned n, uint32_t frame_instr);
#endif

static void run_frame(void) {
    /* exactly ips/60 instructions per frame on average: the remainder carries over and the
       last block is shortened. Rounding every frame up to whole blocks of 100 ran the
       guest's clock fast against the audio at any speed that is not a multiple of 6000
       instructions/s -- the speed governor's in-between speeds -- and every note came out
       up to 0.7 % sharp. */
    static uint32_t frame_rem = 0;
    const uint32_t total = ips + frame_rem;
    const uint32_t target = total / PLAT_FPS;
    frame_rem = total % PLAT_FPS;
    int si = 0;
#ifdef XT_SPK_HLE
    /* the speaker is rendered from the guest's port writes, timed to the instruction */
    spk_hle_frame_begin(&machine.CPU.totalexec, target, ips, audio, PLAT_AUDIO_SAMPLES);
#endif
#ifdef XT_ADLIB_HLE
    adlib_hle_frame_begin(PLAT_AUDIO_SAMPLES, PLAT_AUDIO_RATE, target);
#endif
    for (uint32_t done = 0; done < target; done += instructionsperloop) {
        const uint32_t chunk = target - done < instructionsperloop ? target - done : instructionsperloop;
        cpu_interruptCheck(&machine.CPU, &machine.i8259);
#ifdef XT_SPK_HLE
        spk_hle_chunk(done, chunk);
#endif
#ifdef XT_SB_HLE
        sb_hle_chunk(done);
#endif
#ifdef XT_ADLIB_HLE
        adlib_hle_chunk(done, chunk);
#endif
        {
            PROF_T0();
            cpu_exec(&machine.CPU, chunk);
            PROF_ADD(P_CPU);
        }
#ifdef XT_SB_HLE
        sb_hle_tick(done, chunk);              /* DMA samples due in this block, IRQ 7 */
#endif
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
    PROF_T0();
#ifdef XT_SPK_HLE
    spk_hle_frame_end();
    (void)si;
#ifdef XT_ADLIB_HLE
    adlib_hle_frame_end_mix(audio);                                     /* AdLib music on top */
#endif
#ifdef XT_SB_HLE
    sb_hle_render_mix(audio, PLAT_AUDIO_SAMPLES, target);               /* and digitised sound */
#endif
#else
    int16_t last = si ? audio[si - 1] : 0;
    while (si < PLAT_AUDIO_SAMPLES) audio[si++] = last;
#endif
    cga_doDraw = 0;
    PROF_ADD(P_SND);
}

/* ---- "run =": type commands at the DOS prompt through the BIOS keyboard buffer ----
   Characters go straight into the BIOS ring buffer (40:1E..40:3D), so what DOS reads is
   the ASCII code itself, independent of KEYB's keyboard layout. Each command is typed
   only when a DOS prompt ("X:\...>" with the cursor right after it) is on screen. */
static const char* run_ptr = NULL;
/* run_state: 0 idle, 1 wait prompt, 2 typing, 3 wait next prompt (declared above) */
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

/* DOS's answer to Esc at the prompt: "\" at the end of the prompt line and the cursor on
   an empty new line, with no prompt -- the autotyper would wait for one for ever. */
static int dos_line_cancelled(void) {
    if (bda8(0x49) > 3) return 0;
    int cols = bda16(0x4A); if (cols != 40 && cols != 80) return 0;
    uint16_t cur = bda16(0x50);
    int col = cur & 0xFF, row = cur >> 8;
    if (row < 1 || row > 24 || col >= cols) return 0;
    uint32_t line = 0xB8000 + (uint32_t)row * cols * 2, prev = line - (uint32_t)cols * 2;
    for (int c = 0; c < col; c++) if (cpu_read(&machine.CPU, line + c * 2) != ' ') return 0;
    int last = cols - 1;
    while (last > 0 && cpu_read(&machine.CPU, prev + last * 2) == ' ') last--;
    uint8_t p0 = cpu_read(&machine.CPU, prev), p1 = cpu_read(&machine.CPU, prev + 2);
    return cpu_read(&machine.CPU, prev + last * 2) == '\\' && (p1 == ':' || p0 == ' ' || p0 == '\\');
}

#ifdef GWDOS_HOST
/* Host harness only: GWDOS_FEED=<file> drives a whole session -- an installer, say -- one
   step per line, each step waiting for the one before:
     wait <text>     until <text> is on the 80x25 text screen
     frames <n>      let n frames run
     type <text>     type the text (ASCII)
     key <name>      enter esc tab bksp up down left right home end pgup pgdn f1..f10
     disk <path>     put that image in A: (as the disk menu does)
     eject           empty A: (the disk menu's first entry)
     screen          print the text screen
   Every step is logged with its frame number. */
static FILE* feed_f;
static char feed_line[256];
static int feed_have, feed_frames, feed_tpos;
static uint32_t feed_frame;

static void feed_screen(void) {
    printf("---- frame %u\n", (unsigned)feed_frame);
    for (int r = 0; r < 25; r++) {
        char l[81];
        for (int c = 0; c < 80; c++) { uint8_t ch = cpu_read(&machine.CPU, 0xB8000 + (r * 80 + c) * 2); l[c] = ch >= 32 && ch < 127 ? (char)ch : ' '; }
        int n = 80; while (n > 0 && l[n - 1] == ' ') n--; l[n] = 0;
        printf("|%s\n", l);
    }
    fflush(stdout);
}

static int feed_onscreen(const char* t) {
    int n = (int)strlen(t);
    for (int r = 0; r < 25; r++)
        for (int c = 0; c + n <= 80; c++) {
            int k = 0;
            while (k < n && cpu_read(&machine.CPU, 0xB8000 + (r * 80 + c + k) * 2) == (uint8_t)t[k]) k++;
            if (k == n) return 1;
        }
    return 0;
}

static uint16_t feed_ascii(char ch) {
    static const char* rows[4] = { "1234567890-=", "qwertyuiop[]", "asdfghjkl;'`", "\\zxcvbnm,./" };
    static const char* shifted[4] = { "!@#$%^&*()_+", "QWERTYUIOP{}", "ASDFGHJKL:\"~", "|ZXCVBNM<>?" };
    static const uint8_t base[4] = { 0x02, 0x10, 0x1E, 0x2B };
    if (ch == ' ') return 0x3920;
    for (int r = 0; r < 4; r++) {
        const char* p = strchr(rows[r], ch); if (p && ch) return (uint16_t)(((base[r] + (p - rows[r])) << 8) | (uint8_t)ch);
        p = strchr(shifted[r], ch); if (p && ch) return (uint16_t)(((base[r] + (p - shifted[r])) << 8) | (uint8_t)ch);
    }
    return (uint8_t)ch;
}

static void feed_frame_step(void) {
    static int init;
    feed_frame++;
    if (!init) { const char* e = getenv("GWDOS_FEED"); init = 1; if (e) feed_f = fopen(e, "r"); }
    if (!feed_f) return;
    for (;;) {
        if (!feed_have) {
            if (!fgets(feed_line, sizeof feed_line, feed_f)) { fclose(feed_f); feed_f = 0; printf("[feed] end at frame %u\n", (unsigned)feed_frame); return; }
            feed_line[strcspn(feed_line, "\r\n")] = 0;
            if (!feed_line[0] || feed_line[0] == '#') continue;
            feed_have = 1; feed_frames = -1; feed_tpos = 0;
        }
        char* arg = strchr(feed_line, ' '); arg = arg ? arg + 1 : feed_line + strlen(feed_line);
        if (!strncmp(feed_line, "wait ", 5)) {
            if (!feed_onscreen(arg)) return;
        } else if (!strncmp(feed_line, "frames ", 7)) {
            if (feed_frames < 0) feed_frames = atoi(arg);
            if (feed_frames-- > 0) return;
        } else if (!strncmp(feed_line, "type ", 5)) {
            if (bda16(0x1A) != bda16(0x1C)) return;          /* one key at a time */
            if (arg[feed_tpos]) { bios_kbd_put(feed_ascii(arg[feed_tpos++])); return; }
        } else if (!strncmp(feed_line, "key ", 4)) {
            static const struct { const char* n; uint16_t k; } keys[] = {
                {"enter",0x1C0D},{"esc",0x011B},{"tab",0x0F09},{"bksp",0x0E08},{"up",0x4800},{"down",0x5000},
                {"left",0x4B00},{"right",0x4D00},{"home",0x4700},{"end",0x4F00},{"pgup",0x4900},{"pgdn",0x5100},
                {"f1",0x3B00},{"f2",0x3C00},{"f3",0x3D00},{"f4",0x3E00},{"f5",0x3F00},{"f6",0x4000},{"f7",0x4100},
                {"f8",0x4200},{"f9",0x4300},{"f10",0x4400} };
            if (bda16(0x1A) != bda16(0x1C)) return;
            uint16_t k = 0;
            for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) if (!strcmp(keys[i].n, arg)) k = keys[i].k;
            if (!k) { printf("[feed] unknown key %s\n", arg); }
            else bios_kbd_put(k);
        } else if (!strncmp(feed_line, "disk ", 5)) {
            if (biosdisk_insert(&machine.CPU, 0, arg) == 0) snprintf(drive_a, sizeof(drive_a), "%.47s", strrchr(arg, '/') ? strrchr(arg, '/') + 1 : arg);
            else printf("[feed] cannot open %s\n", arg);
        } else if (!strncmp(feed_line, "osk ", 4)) {      /* press an on-screen key by its label */
            int hit = 0;
            for (int r = 0; r < OSK_ROWS && !hit; r++)
                for (int c = 0; osk_rows[r][c].label && !hit; c++)
                    if (!strcmp(osk_rows[r][c].label, arg)) { osk_press(osk_rows[r][c].sc); hit = 1; }
            if (!hit) printf("[feed] no OSK key %s\n", arg);
            else printf("[feed] shift latch %d\n", latched_shift);
        } else if (!strncmp(feed_line, "pad ", 4)) {      /* tap a pad button by index (0 up,1 down,2 left,3 right..) */
            int i = atoi(arg);
            if (i >= 0 && i < GC_BUTTONS) { pad_key(i, 1); pad_key(i, 0); }
            printf("[feed] kbd flags 0417=%02X\n", cpu_read(&machine.CPU, 0x417));
        } else if (!strcmp(feed_line, "eject")) {
            insert_a(EJECT_ENTRY);
        } else if (!strcmp(feed_line, "screen")) {
            feed_screen();
        } else printf("[feed] unknown step: %s\n", feed_line);
        printf("[feed] %6u  %s\n", (unsigned)feed_frame, feed_line); fflush(stdout);
        feed_have = 0;
        return;                                           /* one step per frame */
    }
}
#endif

static void autorun_frame(void) {
    if (!run_state) return;
    int prompt = dos_prompt_visible();
    if ((run_state == 1 || run_state == 3) && !prompt && dos_line_cancelled()) {
        /* a key that was waiting in the BIOS buffer from before the prompt (the pad is only
           held back once a prompt is on screen) cancelled the line: ask for a new prompt */
        if (++prompt_frames >= 20) { bios_kbd_put(0x1C0D); prompt_frames = 0; }
        return;
    }
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
        if (file_size(bpath) > (long)FLOPPY_MAX_BYTES) {
            /* "boot =" a hard disk (DOS installed on c.img): it is C: and boots; the game goes
               in A: if it is a floppy, else it is the second hard disk, D: */
            if (biosdisk_insert(&machine.CPU, 2, bpath) == 0) snprintf(drive_c, sizeof(drive_c), "%s", base_name(bpath));
            else snprintf(boot_missing, sizeof(boot_missing), "%.30s", gcfg.boot);
            bootdrive = 0x80;
            if (size > (long)FLOPPY_MAX_BYTES) {
                if (biosdisk_insert(&machine.CPU, 3, (char*)boot) == 0) snprintf(drive_d, sizeof(drive_d), "%s", base_name(boot));
            } else if (biosdisk_insert(&machine.CPU, 0, (char*)boot) == 0) {
                snprintf(drive_a, sizeof(drive_a), "%s", base_name(boot));
            } else snprintf(drive_a, sizeof(drive_a), "(error)");
            return;
        }
        if (biosdisk_insert(&machine.CPU, 0, bpath) == 0) snprintf(drive_a, sizeof(drive_a), "%s", base_name(bpath));
        else {
            /* without it the BIOS stops at "Booting OS" with nothing said: say it, and keep
               saying it (draw_bands) -- the start-up lines are gone after 7 s */
            snprintf(drive_a, sizeof(drive_a), "(falta)");
            snprintf(boot_missing, sizeof(boot_missing), "%.30s", gcfg.boot);
        }
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
    else { snprintf(drive_a, sizeof(drive_a), "(error)"); snprintf(boot_missing, sizeof(boot_missing), "%.30s", base_name(boot)); }
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
    /* clut keeps what was last pushed, so it is also the "unchanged?" reference (a
       separate copy cost 1 KB of RAM_EMU, i.e. a 4 KB chunk of DOS memory) */
    static uint32_t clut[256];
    static int last_n = 0;
    if (kind && !force && kind == clut_kind) {         /* games often rewrite an unchanged palette */
        int n = kind == 1 ? 16 : 256;
        uint32_t tmp[256];
        if (kind == 1) vga_hle_palette16(tmp); else vga_hle_palette256(tmp);
        if (memcmp(tmp, clut + (kind == 1 ? 16 : 0), n * sizeof(uint32_t)) == 0 && last_n == (kind == 1 ? 32 : 256)) return;
    }
    clut_kind = kind;
    for (int i = 0; i < 16; i++) ui_cmap[i] = (uint8_t)i;
    if (kind == 0) { plat_set_palette(cga_rgb, 16); last_n = 16; return; }
    if (kind == 1) {
        memcpy(clut, cga_rgb, sizeof(cga_rgb));
        vga_hle_palette16(clut + 16);
        plat_set_palette(clut, 32);
        last_n = 32;
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
    last_n = 256;
#else
    if (force || clut_kind != 0) { clut_kind = 0; plat_set_palette(cga_rgb, 16); }
#endif
}

static void draw_screen(uint8_t* fb) {
    /* The picture is drawn every frame on purpose. Skipping unchanged ones (tried: keys on
       video memory/registers, 2 frames per change for the two LCD buffers, every 7th frame
       for retro-go's HUD) made the frames uneven -- light ones, then heavy ones -- and on the
       console Monkey Island's music got more repeated audio blocks ("pof" between notes:
       4.4 % of 16.7 ms blocks against < 1 %) for a gain of a few % in still scenes only. */
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
    /* no speed pop-up at start-up: the bottom status line already shows it for 7 s.
       Pressing CPU on the on-screen keyboard still pops it up, over the keys. */
    info_on = plat_setting_get("InfoBar", 0) ? 1 : 0;
    info_until = plat_ms() + INFO_MS;
    cga_zoom80 = plat_setting_get("Zoom80", 1) ? 1 : 0;

    for (int i = 0; i < 4; i++) {                 /* RAM_EMU, LCD spare, DTCM, ITCM */
        uint8_t* base;
        size_t size = plat_mem_region(i, &base);
        if (size) xt_pool_add(base, size);
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
#ifdef XT_ADLIB_HLE
    if (ok) adlib_hle_init(&machine.CPU.totalexec, &ips);
#endif
#ifdef XT_SB_HLE
    if (ok) sb_hle_init(&machine.i8237, &machine.i8259, &machine.CPU.totalexec, &ips);
#endif

    if (ok) insert_boot_disks();

    uint32_t sec_t0 = plat_ms(), emu_ms = 0, frame_no = 0, win_frames = 0, win_late = 0, last_ms = plat_ms(), wait_ms = 0;
    /* frames that MISSED their audio block: plat_frame_end() came back without waiting, i.e.
       the DMA had already moved on and is replaying the previous 16.7 ms. The frame rate
       cannot see these -- the firmware catches up with back-to-back frames, so 60 frames
       still go by every second while the music stretches (a recording had ~10 % of its
       16.7 ms blocks repeated at a "60 fps", 70 % load setting). */
    uint32_t win_miss = 0, win_c0 = plat_cycles(), cyc_per_ms = 0;   /* 0 until calibrated: no misses counted */
    for (;;) {
        uint32_t tw = plat_ms();
#ifdef GWDOS_PROF
        uint32_t tb = plat_cycles();
        bool draw = plat_frame_begin();
        prof_acc[P_WAIT] += plat_cycles() - tb;
#else
        bool draw = plat_frame_begin();
#endif
        wait_ms += plat_ms() - tw;
        /* the LCD switches to 8-bit mode at a vsync and the CLUT is ignored until then:
           program the palette again during the first frames */
        ++frame_no;
        update_palette(frame_no == 2 || frame_no == 10 || frame_no == 60);
        handle_input(plat_buttons());
        if (ok) {
            uint32_t t0 = plat_ms();
            run_frame();
            autorun_frame();
#ifdef GWDOS_HOST
            feed_frame_step();
#endif
            mouse_frame();
            emu_ms += plat_ms() - t0;
        } else {
            memset(audio, 0, sizeof(audio));
        }
        uint32_t now = plat_ms();
#ifdef GWDOS_HOST
        {   /* host harness: GWDOS_IPS_AT=frame:ips changes the CPU speed mid-run, as the
               governor does on the console (the harness is not real time) */
            static long at = -2; static uint32_t to;
            if (at == -2) { const char* e = getenv("GWDOS_IPS_AT"); at = -1; if (e) { at = atol(e); const char* c = strchr(e, ':'); to = c ? (uint32_t)atol(c + 1) : 0; } }
            if (at >= 0 && frame_no == (uint32_t)at && to) ips = to;
        }
#endif
        win_frames++;
        if (now - last_ms >= 20 && now - last_ms < 200) win_late++;   /* a frame is 16.7 ms */
        (void)win_late;
        last_ms = now;
        if (now - sec_t0 >= 500) {                     /* every half second: load and speed */
            /* "carga" = the share of the time the console was busy, not waiting for audio */
            uint32_t win = now - sec_t0;
            load_pct = wait_ms >= win ? 0 : 100u - wait_ms * 100u / win;
#ifndef GWDOS_HOST                                    /* the harness is not real time */
            {
                extern volatile uint32_t biosdisk_ops;
                static uint32_t ops_seen;
                uint32_t ops = biosdisk_ops;
                /* a window with disk transfers waited for the SD card, not for the CPU */
                if (ok && ops == ops_seen) speed_governor(win_frames, win, load_pct, win_miss);
                ops_seen = ops;
            }
#endif
            {   uint32_t c = plat_cycles();                  /* recalibrate cycles per ms */
#ifdef GWDOS_PROF
                {   static uint64_t ex0; uint32_t tot = c - win_c0;
                    uint64_t ex = machine.CPU.totalexec;
                    if (tot) {
                        for (int k = 0; k < P_N; k++) prof_pct[k] = (uint32_t)((uint64_t)prof_acc[k] * 100u / tot);
                        prof_pct[P_N] = (uint32_t)((uint64_t)adlib_prof_cyc * 100u / tot);
                    }
                    prof_cpi = ex > ex0 ? (uint32_t)(prof_acc[P_CPU] / (ex - ex0)) : 0;
                    ex0 = ex; adlib_prof_cyc = 0;
                    for (int k = 0; k < P_N; k++) prof_acc[k] = 0; }
#endif
                if (win) cyc_per_ms = (c - win_c0) / win;
                win_c0 = c; }
            emu_ms = 0; win_frames = 0; win_late = 0; win_miss = 0; wait_ms = 0; sec_t0 = now;
        }
        PROF_T0();
        if (draw) {
            draw_screen(plat_framebuffer());
            plat_present();
        }
        PROF_ADD(P_VID);
        plat_audio(audio, PLAT_AUDIO_SAMPLES);
        PROF_ADD(P_SND);
        tw = plat_ms();
        uint32_t tc = plat_cycles();
        plat_frame_end();                              /* waits for the audio DMA: idle time */
        if (plat_cycles() - tc < cyc_per_ms / 20) win_miss++;   /* under 50 us: no wait, late */
        PROF_ADD(P_WAIT);
        wait_ms += plat_ms() - tw;
    }
    return 0;
}
