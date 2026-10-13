# Phase 3: make the XTulator core malloc-free and MCU-sized (arg1 = XTulator copy already
# prepared by phase-2 prep.py + virtual_time.py).
#  - memory.c/ports.c: page / index tables instead of per-byte pointer tables (20 MB -> ~5 KB)
#  - xt_pool: static bump allocator for guest RAM/ROM (+ optional ROM-in-flash hook)
#  - timing.c: fixed timer array instead of realloc
#  - cga.c: static 16 KB VRAM mapped directly, no render thread; host renderer optional
#           (XT_NO_HOST_RENDER drops the 1 MB 640x400x32 framebuffer and the font load)
import sys, os, shutil
d = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
def rd(p): return open(os.path.join(d, p)).read()
def wr(p, s): open(os.path.join(d, p), "w").write(s)
def sub(s, old, new, count=1):
    assert s.count(old) >= 1, "pattern not found: " + old[:60]
    return s.replace(old, new, count)

for f in ["memory.c", "memory.h", "ports.c", "ports.h", "xt_pool.c", "xt_pool.h"]:
    shutil.copy(os.path.join(here, "files", f), os.path.join(d, f))

# ---- timing: static timer table
t = rd("timing.h")
t = sub(t, "#define TIMING_RINGSIZE\t1024", "#define TIMING_RINGSIZE\t1024\n\n#ifndef TIMING_MAX_TIMERS\n#define TIMING_MAX_TIMERS 24\n#endif")
wr("timing.h", t)
t = rd("timing.c")
t = sub(t, "TIMER* timers = NULL;", "static TIMER timers[TIMING_MAX_TIMERS];")
t = sub(t, """	temp = (TIMER*)realloc(timers, (size_t)sizeof(TIMER) * (timers_count + 1));
	if (temp == NULL) {
		//TODO: error handling
		return TIMING_ERROR; //NULL;
	}
	timers = temp;
""", """	(void)temp;
	if (timers_count >= TIMING_MAX_TIMERS) {
		debug_log(DEBUG_ERROR, "[TIMING] too many timers (max %d)\\r\\n", TIMING_MAX_TIMERS);
		return TIMING_ERROR;
	}
""")
# XT_TIMING_CATCHUP: run every timer as many times as it is due (bounded by the existing
# 100-interval lag reset) instead of at most once per timing_loop() call. Without it a
# 48 kHz timer polled every ~250 us of virtual time only ticks ~4 kHz (PIT/IRQ0 too slow).
t = sub(t, "\t\tif (timing_cur >= (timers[i].previous + timers[i].interval)) {",
           "\t\tTIMING_IF ((timers[i].enabled != TIMING_DISABLED) && (timing_cur >= (timers[i].previous + timers[i].interval))) {")
t = sub(t, "void timing_loop() {", "#ifdef XT_TIMING_CATCHUP\n#define TIMING_IF while\n#else\n#define TIMING_IF if\n#endif\nvoid timing_loop() {")
wr("timing.c", t)

# ---- machine: guest RAM/ROM from xt_pool, ROM optionally read in place
m = rd("machine.c")
m = sub(m, '#include "machine.h"', '#include "machine.h"\n#include "xt_pool.h"')
m = sub(m, """		temp = (uint8_t*)malloc((size_t)machine_mem[num][i].size);
		if ((temp == NULL) &&""", """		temp = NULL;
		if (machine_mem[num][i].memtype == MACHINE_MEM_ROM) {
			const uint8_t* rom = xt_rom_lookup(machine_mem[num][i].filename, machine_mem[num][i].size);
			if (rom != NULL) { //read-only image provided by the platform (e.g. flash, XIP)
				memory_mapRegister(machine_mem[num][i].start, machine_mem[num][i].size, (uint8_t*)rom, NULL);
				i++;
				continue;
			}
		}
		if (machine_mem[num][i].memtype == MACHINE_MEM_RAM) { //RAM in chunks: may span several pool regions
			uint32_t off;
			for (off = 0; off < machine_mem[num][i].size; off += XT_RAM_CHUNK) {
				uint8_t* chunk = (uint8_t*)xt_pool_alloc(XT_RAM_CHUNK);
				if (chunk == NULL) {
					debug_log(DEBUG_ERROR, "[MACHINE] ERROR: out of pool memory for guest RAM at %05lX\\r\\n", (unsigned long)(machine_mem[num][i].start + off));
					return -1;
				}
				memory_mapRegister(machine_mem[num][i].start + off, XT_RAM_CHUNK, chunk, chunk);
			}
			i++;
			continue;
		}
		temp = (uint8_t*)xt_pool_alloc((size_t)machine_mem[num][i].size);
		if ((temp == NULL) &&""")
m = sub(m, 'debug_log(DEBUG_ERROR, "[MACHINE] ERROR: Unable to allocate %lu bytes of memory\\r\\n", machine_mem[num][i].size);',
           'debug_log(DEBUG_ERROR, "[MACHINE] ERROR: Unable to allocate %lu bytes of memory\\r\\n", (unsigned long)machine_mem[num][i].size);')
# guest RAM size of the sergey_xt machine is configurable (default 640 KB); BIOS reports it via the RAM test
m = sub(m, '\t\t{ MACHINE_MEM_RAM, 0x00000, 0xA0000, MACHINE_ROM_ISNOTROM, NULL },\n\t\t{ MACHINE_MEM_ROM, 0xFC000, 0x04000, MACHINE_ROM_REQUIRED, "bios-xt.bin" },',
           '\t\t{ MACHINE_MEM_RAM, 0x00000, XT_GUEST_RAM_BYTES, MACHINE_ROM_ISNOTROM, NULL },\n\t\t{ MACHINE_MEM_ROM, 0xFC000, 0x04000, MACHINE_ROM_REQUIRED, "bios-xt.bin" },')
m = sub(m, '#include "xt_pool.h"', '#include "xt_pool.h"\n#ifndef XT_GUEST_RAM_BYTES\n#define XT_GUEST_RAM_BYTES 0xA0000\n#endif\n#ifndef XT_RAM_CHUNK\n#define XT_RAM_CHUNK 0x4000\n#endif')

# XT_MINIMAL: only the devices the G&W core ships (PIC, PIT, DMA, PPI, PC speaker,
# HLE disk, CGA). SB/OPL, UARTs/mouse, RTC, NE2000 and VGA are compiled out.
m = sub(m, "\t//check machine HW flags and init devices accordingly", "#ifndef XT_MINIMAL\n\t//check machine HW flags and init devices accordingly")
m = sub(m, "\n\tcpu_reset(&machine->CPU);", "\n#endif //XT_MINIMAL\n\tcpu_reset(&machine->CPU);")
m = sub(m, "\tcase VIDEO_CARD_VGA:\n\t\tif (vga_init()) return -1;\n\t\tbreak;",
           "#ifndef XT_MINIMAL\n\tcase VIDEO_CARD_VGA:\n\t\tif (vga_init()) return -1;\n\t\tbreak;\n#endif")
wr("machine.c", m)

# MACHINE_t: drop the state of devices XT_MINIMAL does not build (OPL3/SB/UART/FDC ~24 KB)
mh = rd("machine.h")
mh = sub(mh, "\tUART_t UART[2];", "#ifndef XT_MINIMAL\n\tUART_t UART[2];")
mh = sub(mh, "\tuint8_t mixBlaster;\n", "\tuint8_t mixBlaster;\n#endif\n")
mh = sub(mh, "\tFDC_t fdc;\n", "#ifndef XT_MINIMAL\n\tFDC_t fdc;\n#endif\n")
wr("machine.h", mh)

# ---- CGA: static VRAM, direct mapping, no thread, optional host renderer
c = rd("modules/video/cga.c")
c = sub(c, """#ifdef _WIN32
#include <process.h>
#else
#include <pthread.h>
pthread_t cga_renderThreadID;
#endif
""", "")
c = sub(c, "uint32_t cga_framebuffer[400][640];", "#ifndef XT_NO_HOST_RENDER\nuint32_t cga_framebuffer[400][640];\n#endif")
c = sub(c, "uint8_t *cga_RAM = NULL;", "static uint8_t cga_VRAM[16384] __attribute__((aligned(4)));\nuint8_t *cga_RAM = cga_VRAM;")
c = sub(c, """	if (utility_loadFile(cga_font, 4096, "roms/video/cgachar.bin")) {
		debug_log(DEBUG_ERROR, "[CGA] Failed to load character generator ROM\\r\\n");
		return -1;
	}

	for (y = 0; y < 400; y++) {
		for (x = 0; x < 640; x++) {
			cga_framebuffer[y][x] = cga_color(CGA_BLACK);
		}
	}
	sdlconsole_blit((uint32_t *)cga_framebuffer, 640, 400, 640 * sizeof(uint32_t));
""", """#ifndef XT_NO_HOST_RENDER
	if (utility_loadFile(cga_font, 4096, "roms/video/cgachar.bin")) {
		debug_log(DEBUG_ERROR, "[CGA] Failed to load character generator ROM\\r\\n");
		return -1;
	}

	for (y = 0; y < 400; y++) {
		for (x = 0; x < 640; x++) {
			cga_framebuffer[y][x] = cga_color(CGA_BLACK);
		}
	}
	sdlconsole_blit((uint32_t *)cga_framebuffer, 640, 400, 640 * sizeof(uint32_t));
#else
	(void)x; (void)y;
#endif
""")
c = sub(c, """	cga_RAM = (uint8_t*)malloc(16384);
	if (cga_RAM == NULL) {
		debug_log(DEBUG_ERROR, "[CGA] Failed to allocate video memory\\r\\n");
		return -1;
	}

	//TODO: error checking below
#ifdef _WIN32
	_beginthread(cga_renderThread, 0, NULL);
#else
	pthread_create(&cga_renderThreadID, NULL, cga_renderThread, NULL);
#endif
""", """	//G&W port: no render thread. The frontend polls cga_doDraw (set at 60 Hz by
	//cga_drawCallback) and either calls cga_update() (host) or draws from cga_RAM itself.
""")
c = sub(c, """	memory_mapCallbackRegister(0xB8000, 0x4000, (void*)cga_readmemory, (void*)cga_writememory, NULL);""",
           """	memory_mapRegister(0xB8000, 0x4000, cga_RAM, cga_RAM); //direct page mapping (faster than callbacks)""")
# host renderer + thread body only when host rendering is wanted
c = sub(c, "void cga_update(uint32_t start_x, uint32_t start_y, uint32_t end_x, uint32_t end_y) {",
           "#ifndef XT_NO_HOST_RENDER\nvoid cga_update(uint32_t start_x, uint32_t start_y, uint32_t end_x, uint32_t end_y) {")
c = sub(c, """void cga_renderThread(void* dummy) {
	while (running) {
		if (cga_doDraw == 1) {
			cga_update(0, 0, 639, 399);
			cga_doDraw = 0;
		}
		else {
			utility_sleep(1);
		}
	}
#ifdef _WIN32
	_endthread();
#else
	pthread_exit(NULL);
#endif
}
""", "#endif //XT_NO_HOST_RENDER\n")
# 3DAh status from the CPU instruction counter (XT_CGA_STATUS_ICOUNT). The original sets it from a
# 62.8 kHz timer callback, but timers only run between 100-instruction chunks, so the status was
# sampled once per chunk and aliased (a "horizontal retrace" could last thousands of instructions).
# Also bit 0 ("display inactive") now stays set during the whole vertical blank, as on a real CGA.
# Games that time scanlines or look for the vertical blank this way (Ironman Super Off Road) hung.
c = sub(c, "cga_regs[0xA] |= (hpart == 3) ? 1 : 0;", "cga_regs[0xA] |= ((hpart == 3) || (scanline >= 200)) ? 1 : 0;")
c = sub(c, "uint8_t cga_readport(void* dummy, uint16_t port) {", """#ifdef XT_CGA_STATUS_ICOUNT
const uint64_t* cga_status_icount = NULL;   /* set by the frontend: CPU instruction counter */
const uint32_t* cga_status_ips = NULL;      /* and instructions per second of virtual time */
static uint8_t cga_status(void) {
	/* 15700 lines/s, 256 lines per frame (vsync 224..255), each line in 4 parts, last = retrace */
	uint64_t q = (*cga_status_icount * 62800ull) / *cga_status_ips;
	uint32_t hpart = (uint32_t)q & 3, scanline = (uint32_t)(q >> 2) & 255;
	return 6 | (((hpart == 3) || (scanline >= 200)) ? 1 : 0) | ((scanline >= 224) ? 8 : 0);
}
#endif
uint8_t cga_readport(void* dummy, uint16_t port) {""")
c = sub(c, "		return cga_regs[0xA]; //rand() & 0xF;", """#ifdef XT_CGA_STATUS_ICOUNT
		if (cga_status_icount && cga_status_ips && *cga_status_ips) return cga_status();
#endif
		return cga_regs[0xA]; //rand() & 0xF;""")
c = sub(c, "	timing_addTimer(cga_scanlineCallback, NULL, 62800, TIMING_ENABLED);", """#ifndef XT_CGA_STATUS_ICOUNT
	timing_addTimer(cga_scanlineCallback, NULL, 62800, TIMING_ENABLED);
#endif""")
wr("modules/video/cga.c", c)

h = rd("modules/video/cga.h")
h = sub(h, "void cga_renderThread(void* cpu);\n", "")
h = sub(h, "extern const uint8_t cga_palette[16][3];",
        "extern const uint8_t cga_palette[16][3];\n/* state a frontend needs to draw the screen itself */\nextern uint8_t* cga_RAM;\nextern uint8_t cga_regs[16];\nextern uint8_t cga_datareg[256];\nextern uint8_t cga_cursor_blink_state;\nextern volatile uint8_t cga_doDraw;\n#ifdef XT_CGA_STATUS_ICOUNT\nextern const uint64_t* cga_status_icount;\nextern const uint32_t* cga_status_ips;\n#endif")
wr("modules/video/cga.h", h)

# i8237.c uses memset without <string.h> (error on GCC >= 14)
i = rd("chipset/i8237.c")
if "#include <string.h>" not in i:
    i = sub(i, "#include <stdint.h>", "#include <stdint.h>\n#include <string.h>")
wr("chipset/i8237.c", i)

# CGA font is only used by the host renderer
c = rd("modules/video/cga.c")
c = sub(c, "uint8_t cga_font[4096];", "#ifndef XT_NO_HOST_RENDER\nuint8_t cga_font[4096];\n#endif")
wr("modules/video/cga.c", c)

# utility.c: loadFile must not free() a buffer it did not allocate (pool / static memory);
# sleep is not needed by the core (no threads) and nanosleep does not exist on newlib.
u = rd("utility.c")
u = sub(u, "\tif (file == NULL) {\n\t\tfree(dst);\n\t\treturn -1;", "\tif (file == NULL) {\n\t\treturn -1;")
u = sub(u, "\tif (fread(dst, 1, len, file) < len) {\n\t\tfree(dst);\n", "\tif (fread(dst, 1, len, file) < len) {\n")
u = sub(u, "void utility_sleep(uint32_t ms) {\n#ifdef _WIN32", "void utility_sleep(uint32_t ms) {\n#if defined(XT_MINIMAL)\n\t(void)ms;\n#elif defined(_WIN32)")
wr("utility.c", u)

# debuglog.c: XT_NO_STDIO turns logging into a platform hook (weak no-op by default),
# so the MCU build does not drag in vfprintf.
g = rd("debuglog.c")
g = sub(g, "void debug_log(uint8_t level, char* format, ...) {\n\tva_list argptr;",
        "#ifdef XT_NO_STDIO\n__attribute__((weak)) void xt_log_hook(uint8_t level, const char* format, va_list ap) { (void)level; (void)format; (void)ap; }\n#endif\n\nvoid debug_log(uint8_t level, char* format, ...) {\n\tva_list argptr;")
g = sub(g, "\tvfprintf(stderr, format, argptr);\n\tfflush(stderr);",
        "#ifdef XT_NO_STDIO\n\txt_log_hook(level, format, argptr);\n#else\n\tvfprintf(stderr, format, argptr);\n\tfflush(stderr);\n#endif")
wr("debuglog.c", g)

# biosdisk: INT 13h AH=15h (drive type) and AH=16h (disk change line), so DOS notices a
# floppy swap instead of reusing the cached FAT of the previous disk.
b = rd("modules/disk/biosdisk.c")
b = sub(b, "DISK_t biosdisk[4];", "DISK_t biosdisk[4];\nstatic uint8_t biosdisk_changed[4];")
b = sub(b, "\tbiosdisk[drivenum].inserted = 1;\n\tbiosdisk[drivenum].diskfile = fopen(filename, \"r+b\");",
           "\tbiosdisk[drivenum].inserted = 1;\n\tbiosdisk_changed[drivenum] = 1;\n\tbiosdisk[drivenum].diskfile = fopen(filename, \"r+b\");")
b = sub(b, """	default:
		cpu->cf = 1;
	}
	lastah = cpu->regs.byteregs[regah];""", """	case 0x15: //get disk type
		if (!biosdisk[curdisk].inserted && curdisk >= 2) {
			cpu->regs.byteregs[regah] = 0; //no such drive
		} else if (curdisk < 2) {
			cpu->regs.byteregs[regah] = 2; //floppy with change-line support
		} else {
			uint32_t sectors = (uint32_t)biosdisk[curdisk].cyls * biosdisk[curdisk].heads * biosdisk[curdisk].sects;
			cpu->regs.byteregs[regah] = 3; //fixed disk
			cpu->regs.wordregs[regcx] = (uint16_t)(sectors >> 16);
			cpu->regs.wordregs[regdx] = (uint16_t)sectors;
		}
		cpu->cf = 0;
		break;
	case 0x16: //detect disk change
		if (biosdisk_changed[curdisk] || !biosdisk[curdisk].inserted) {
			biosdisk_changed[curdisk] = 0;
			cpu->regs.byteregs[regah] = 6; //change line active
			cpu->cf = 1;
		} else {
			cpu->regs.byteregs[regah] = 0;
			cpu->cf = 0;
		}
		break;
	default:
		cpu->cf = 1;
	}
	lastah = cpu->regs.byteregs[regah];""")
wr("modules/disk/biosdisk.c", b)

# cpu.c: int_callback assignment has a mismatched pointer type (error on GCC >= 14)
p = rd("cpu/cpu.c")
p = sub(p, "cpu->int_callback[interrupt] = cb;", "cpu->int_callback[interrupt] = (void (*)(void*, uint8_t))cb;")
# XT_IRQ_EVERY_INSTR: accept a pending, unmasked IRQ after every instruction (as a real 8088
# does) instead of only when the frontend calls cpu_interruptCheck() between instruction
# batches. With batch-only checks, a guest loop whose length divides the batch size can sit
# with IF=0 at every check forever (Alley Cat: INT 1Ah polling loop waiting for the BIOS tick,
# IRQ0 never accepted -> game frozen).
p = sub(p, '#include "../debuglog.h"', '#include "../debuglog.h"\n\nI8259_t* cpu_pic = NULL; /* set by machine init; used by XT_IRQ_EVERY_INSTR */')
p = sub(p, """	for (loopcount = 0; loopcount < execloops; loopcount++) {

		if (cpu->trap_toggle) {""", """	for (loopcount = 0; loopcount < execloops; loopcount++) {

#ifdef XT_IRQ_EVERY_INSTR
		if (cpu->ifl && cpu_pic && (cpu_pic->irr & ~cpu_pic->imr)) {
			cpu_interruptCheck(cpu, cpu_pic);
		}
#endif

		if (cpu->trap_toggle) {""")
# XT_INT10_EXT: INT 10h services missing from the XT BIOS (int10ext.c, e.g. AH=13h write
# string) are handled before the call reaches the guest's INT 10h vector.
p = sub(p, "FUNC_INLINE void cpu_intcall(CPU_t* cpu, uint8_t intnum) {\n\tif (cpu->int_callback[intnum] != NULL) {",
           "#ifdef XT_INT10_EXT\nint int10ext_hook(CPU_t* cpu);\n#endif\nFUNC_INLINE void cpu_intcall(CPU_t* cpu, uint8_t intnum) {\n#ifdef XT_INT10_EXT\n\tif (intnum == 0x10 && int10ext_hook(cpu)) return;\n#endif\n\tif (cpu->int_callback[intnum] != NULL) {")
wr("cpu/cpu.c", p)
shutil.copy(os.path.join(here, "files", "int10ext.c"), os.path.join(d, "int10ext.c"))
# XT_VGA_HLE: EGA/VGA graphics on top of the CGA card (vga_hle.c): its INT 10h services run
# first, a read of 3DAh resets the attribute controller flip-flop, and guest RAM that does not
# fit after the video memory is simply left out (the BIOS then reports less than 640 KB).
p = rd("cpu/cpu.c")
p = sub(p, "#ifdef XT_INT10_EXT\n\tif (intnum == 0x10 && int10ext_hook(cpu)) return;\n#endif",
           "#ifdef XT_VGA_HLE\n\tif (intnum == 0x10 && vga_hle_int10(cpu)) return;\n#endif\n#ifdef XT_INT10_EXT\n\tif (intnum == 0x10 && int10ext_hook(cpu)) return;\n#endif")
p = sub(p, "#ifdef XT_INT10_EXT\nint int10ext_hook(CPU_t* cpu);\n#endif", "#ifdef XT_INT10_EXT\nint int10ext_hook(CPU_t* cpu);\n#endif\n#ifdef XT_VGA_HLE\nint vga_hle_int10(CPU_t* cpu);\n#endif")
wr("cpu/cpu.c", p)
# XT_MOUSE_HLE: INT 33h mouse driver inside the emulator (mouse_hle.c)
p = rd("cpu/cpu.c")
p = sub(p, "#ifdef XT_VGA_HLE\n\tif (intnum == 0x10 && vga_hle_int10(cpu)) return;\n#endif",
           "#ifdef XT_VGA_HLE\n\tif (intnum == 0x10 && vga_hle_int10(cpu)) return;\n#endif\n#ifdef XT_MOUSE_HLE\n\tif (intnum == 0x33 && mouse_hle_int33(cpu)) return;\n#endif")
p = sub(p, "#ifdef XT_VGA_HLE\nint vga_hle_int10(CPU_t* cpu);\n#endif", "#ifdef XT_VGA_HLE\nint vga_hle_int10(CPU_t* cpu);\n#endif\n#ifdef XT_MOUSE_HLE\nint mouse_hle_int33(CPU_t* cpu);\n#endif")
wr("cpu/cpu.c", p)
shutil.copy(os.path.join(here, "files", "mouse_hle.c"), os.path.join(d, "mouse_hle.c"))
for f in ("vga_hle.c", "vga_hle.h"):
    shutil.copy(os.path.join(here, "files", f), os.path.join(d, f))
c = rd("modules/video/cga.c")
c = sub(c, "#ifdef XT_CGA_STATUS_ICOUNT\n\t\tif (cga_status_icount", "#ifdef XT_VGA_HLE\n\t\t{ void vga_hle_status_read(void); vga_hle_status_read(); }\n#endif\n#ifdef XT_CGA_STATUS_ICOUNT\n\t\tif (cga_status_icount")
wr("modules/video/cga.c", c)
m = rd("machine.c")
m = sub(m, """					debug_log(DEBUG_ERROR, "[MACHINE] ERROR: out of pool memory for guest RAM at %05lX\\r\\n", (unsigned long)(machine_mem[num][i].start + off));
					return -1;""", """					debug_log(DEBUG_ERROR, "[MACHINE] ERROR: out of pool memory for guest RAM at %05lX\\r\\n", (unsigned long)(machine_mem[num][i].start + off));
#ifdef XT_VGA_HLE
					if (off >= 0x40000) break;   /* keep what fits (at least 256 KB) */
#endif
					return -1;""")
wr("machine.c", m)
h = rd("cpu/cpu.h")
h = sub(h, "void cpu_interruptCheck(CPU_t* cpu, I8259_t* i8259);", "void cpu_interruptCheck(CPU_t* cpu, I8259_t* i8259);\nextern I8259_t* cpu_pic;")
wr("cpu/cpu.h", h)
m = rd("machine.c")
m = sub(m, "\ti8259_init(&machine->i8259);", "\ti8259_init(&machine->i8259);\n\tcpu_pic = &machine->i8259;")
wr("machine.c", m)
print("phase 3 static-memory patches applied")
