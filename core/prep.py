# Prepare a headless copy of XTulator (arg1 = copy dir): add a "sergey_xt" machine, stub SDL, drop NE2000.
import sys, re, os
d = sys.argv[1]
def rd(p): return open(os.path.join(d, p)).read()
def wr(p, s): open(os.path.join(d, p), "w").write(s)

# machine definition + memory map (parallel arrays: append at the end of both)
m = rd("machine.c")
m = m.replace('\t{ NULL }\n};',
  '\t{ "sergey_xt", "Sergey 8088 BIOS (bios-xt.bin, 16 KB), CGA", machine_init_generic_xt, VIDEO_CARD_CGA, -1, MACHINE_HW_DISK_HLE },\n\t{ NULL }\n};', 1)
land = '"roms/machine/landmark/landmark.bin" },\n\t\t{ MACHINE_MEM_ENDLIST, 0, 0, 0, NULL }\n\t}'
assert land in m
m = m.replace(land, land + ',\n\n\t//Sergey XT BIOS\n\t{\n\t\t{ MACHINE_MEM_RAM, 0x00000, 0xA0000, MACHINE_ROM_ISNOTROM, NULL },\n\t\t{ MACHINE_MEM_ROM, 0xFC000, 0x04000, MACHINE_ROM_REQUIRED, "bios-xt.bin" },\n\t\t{ MACHINE_MEM_ENDLIST, 0, 0, 0, NULL }\n\t}', 1)
wr("machine.c", m)

# config: no NE2000 (needs pcap)
c = rd("config.h")
c = c.replace("#define USE_NE2000", "//#define USE_NE2000")
wr("config.h", c)

# SDL stubs (headers and sources replaced by headless versions)
wr("modules/video/sdlconsole.h", '''#ifndef _SDLCONSOLE_H_
#define _SDLCONSOLE_H_
#include <stdint.h>
#define SDLCONSOLE_EVENT_NONE 0
#define SDLCONSOLE_EVENT_KEY 1
#define SDLCONSOLE_EVENT_QUIT 2
#define SDLCONSOLE_EVENT_DEBUG_1 3
#define SDLCONSOLE_EVENT_DEBUG_2 4
int sdlconsole_init(char *title);
void sdlconsole_blit(uint32_t* pixels, int w, int h, int stride);
int sdlconsole_loop();
uint8_t sdlconsole_getScancode();
int sdlconsole_setWindow(int w, int h);
void sdlconsole_setTitle(char* title);
extern int sdlconsole_blits;
#endif
''')
wr("modules/video/sdlconsole.c", '''#include "sdlconsole.h"
int sdlconsole_blits = 0;
int sdlconsole_init(char *t) { (void)t; return 0; }
void sdlconsole_blit(uint32_t* p, int w, int h, int s) { (void)p; (void)w; (void)h; (void)s; sdlconsole_blits++; }
int sdlconsole_loop() { return 0; }
uint8_t sdlconsole_getScancode() { return 0; }
int sdlconsole_setWindow(int w, int h) { (void)w; (void)h; return 0; }
void sdlconsole_setTitle(char* t) { (void)t; }
''')
wr("modules/audio/sdlaudio.h", '''#ifndef _SDLAUDIO_H_
#define _SDLAUDIO_H_
#include "../../machine.h"
int sdlaudio_init(MACHINE_t* machine);
void sdlaudio_generateSample(void* dummy);
void sdlaudio_updateSampleTiming();
#endif
''')
wr("modules/audio/sdlaudio.c", '''#include "sdlaudio.h"
int sdlaudio_init(MACHINE_t* machine) { (void)machine; return 0; }
void sdlaudio_generateSample(void* dummy) { (void)dummy; }
void sdlaudio_updateSampleTiming() {}
''')
