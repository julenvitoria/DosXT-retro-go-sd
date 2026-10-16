/*
  PC test harness for gw-dos: implements platform.h on Linux so the exact frontend +
  core that go into the G&W homebrew can be run and checked headlessly.

  usage: gwdos_host <frames> [script] [shot_prefix]
    script: comma-separated "frame=MASK" entries; from that frame on, plat_buttons()
            returns MASK (hex, platform.h BTN_* bits) until the next entry.
    shots:  "<shot_prefix>NNNNN.ppm" written every 600 frames and at the end.
  Memory is offered as two regions (551 KB + 150 KB) like RAM_EMU + the LUT8 bonus pool.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include "platform.h"

extern uint8_t* cga_RAM;   /* XTulator CGA video memory */

static void dump_hex(void) {
    for (int r = 0; r < 25; r += 3) {
        printf("row%02d:", r);
        for (int x = 0; x < 24; x++) printf(" %02X%02X", cga_RAM[(r * 80 + x) * 2], cga_RAM[(r * 80 + x) * 2 + 1]);
        printf("\n");
    }
}

extern uint8_t* memory_pageRead[];
static void dump_mem(const char* fn) {      /* GWDOS_MEM=file: conventional RAM + video, 1 MB raw */
    FILE* f = fopen(fn, "wb");
    if (!f) return;
    for (uint32_t a = 0; a < 0x100000; a++) {
        uint8_t* pg = memory_pageRead[a >> 11];
        fputc(pg ? pg[a & 2047] : 0xFF, f);
    }
    fclose(f);
}

static void dump_text(void) {   /* 80x25 text page, printable ASCII only */
    for (int y = 0; y < 25; y++) {
        char line[81];
        for (int x = 0; x < 80; x++) {
            uint8_t c = cga_RAM[(y * 80 + x) * 2];
            line[x] = (c >= 32 && c < 127) ? (char)c : ' ';
        }
        int n = 80;
        while (n > 0 && line[n - 1] == ' ') n--;
        line[n] = 0;
        if (n) printf("|%s\n", line);
    }
}

int dos_main(int start_paused);
void dos_debug_state(char* buf, size_t n);

static uint8_t region0[514 * 1024], region1[150 * 1024], region2[88 * 1024];   /* about as on the console: RAM_EMU after the core, LCD spare, DTCM+ITCM */
static uint8_t fb[PLAT_LCD_W * PLAT_LCD_H];
static uint32_t pal[256];
static long frame = 0, frames_total = 0;
static const char* script = "";
static const char* shot_prefix = "shot_";
static uint32_t cur_buttons = 0;
static double audio_energy = 0;
static long audio_samples = 0;

size_t plat_mem_region(int index, uint8_t** base) {
    if (index == 0) { *base = region0; return sizeof(region0); }
    if (index == 1) { *base = region1; return sizeof(region1); }
    if (index == 2) { *base = region2; return sizeof(region2); }
    return 0;
}
void plat_init(bool start_paused) { (void)start_paused; }
void plat_set_palette(const uint32_t* rgb, int count) { memcpy(pal, rgb, count * sizeof(uint32_t)); }
bool plat_frame_begin(void) { return true; }

uint32_t plat_buttons(void) {
    const char* p = script;
    while (p && *p) {
        long f = strtol(p, (char**)&p, 10);
        if (*p != '=') break;
        uint32_t m = (uint32_t)strtoul(p + 1, (char**)&p, 16);
        if (f <= frame) cur_buttons = m;
        if (*p == ',') p++;
    }
    return cur_buttons;
}

uint8_t* plat_framebuffer(void) { return fb; }

static void shot(void) {
    char name[256], st[160];
    dos_debug_state(st, sizeof(st));
    if (getenv("GWDOS_TRACE")) fprintf(stderr, "frame %6ld %s\n", frame, st);
    snprintf(name, sizeof(name), "%s%05ld.ppm", shot_prefix, frame);
    FILE* f = fopen(name, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", PLAT_LCD_W, PLAT_LCD_H);
    for (int i = 0; i < PLAT_LCD_W * PLAT_LCD_H; i++) {
        uint32_t c = pal[fb[i]];
        fputc((c >> 16) & 255, f); fputc((c >> 8) & 255, f); fputc(c & 255, f);
    }
    fclose(f);
}

void plat_present(void) {}

/* GWDOS_WAV=file.raw: also dump the audio as raw 16-bit mono at PLAT_AUDIO_RATE */
void plat_audio(const int16_t* s, int n) {
    static FILE* wav = NULL;
    static int wav_tried = 0;
    if (!wav_tried) { const char* w = getenv("GWDOS_WAV"); wav_tried = 1; if (w) wav = fopen(w, "wb"); }
    if (wav) fwrite(s, sizeof(int16_t), (size_t)n, wav);
    for (int i = 0; i < n; i++) audio_energy += (double)s[i] * s[i];
    audio_samples += n;
}

static clock_t t_start;
void plat_frame_end(void) {
    frame++;
    static long every = 0;
    if (!every) { const char* e = getenv("GWDOS_SHOT_EVERY"); every = e ? atol(e) : 600; if (every <= 0) every = 600; }
    if (frame % every == 0) shot();
    if (frame >= frames_total) {
        shot();
        dump_text();
        if (getenv("GWDOS_HEX")) dump_hex();
        if (getenv("GWDOS_MEM")) dump_mem(getenv("GWDOS_MEM"));
        double secs = (double)(clock() - t_start) / CLOCKS_PER_SEC;
        printf("frames=%ld  host_cpu=%.2fs (%.1fx real time)  audio_rms=%.0f\n", frame, secs,
               (frame / 60.0) / (secs > 0 ? secs : 1), audio_samples ? __builtin_sqrt(audio_energy / audio_samples) : 0.0);
        exit(0);
    }
}

void plat_list_dir(const char* dir, void (*cb)(const char*, void*), void* user) {
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) {
        char path[512];
        struct stat st;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) cb(e->d_name, user);
    }
    closedir(d);
}

#ifndef DOS_DIR
#define DOS_DIR "."
#endif
const char* plat_boot_image(void) {
    const char* e = getenv("GWDOS_BOOT");          /* override for tests */
    return e ? e : DOS_DIR "/a.img";
}

static struct { char key[16]; int32_t v; } settings[16];
int32_t plat_setting_get(const char* key, int32_t def) {
    for (int i = 0; i < 16; i++) if (!strcmp(settings[i].key, key)) return settings[i].v;
    return def;
}
void plat_setting_set(const char* key, int32_t v) {
    for (int i = 0; i < 16; i++)
        if (!settings[i].key[0] || !strcmp(settings[i].key, key)) {
            snprintf(settings[i].key, sizeof(settings[i].key), "%s", key); settings[i].v = v; return;
        }
}

uint32_t plat_ms(void) { return (uint32_t)(clock() * 1000.0 / CLOCKS_PER_SEC); }

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <frames> [script] [shot_prefix]\n", argv[0]); return 2; }
    frames_total = atol(argv[1]);
    if (argc > 2) script = argv[2];
    if (argc > 3) shot_prefix = argv[3];
    t_start = clock();
    return dos_main(0);
}
