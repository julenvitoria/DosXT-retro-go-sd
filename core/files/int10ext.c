/*
  int10ext: INT 10h services that AT/EGA-class BIOSes have but the 8088 XT BIOS lacks,
  handled by the emulator before the call reaches the guest's INT 10h vector.
  Part of the XTulator Game & Watch port. GPLv2 or later (same as XTulator).

  Currently: AH=13h "write string" in the colour text modes (0-3). Everything else (and
  graphics modes) falls through to the normal INT 10h vector. Used by e.g. the Rick
  Dangerous II options menu, which was invisible on an XT BIOS.
*/
#include <stdint.h>
#include "cpu/cpu.h"

#define BDA 0x400

static uint8_t rd8(CPU_t* c, uint32_t a) { return cpu_read(c, a); }
static uint16_t rd16(CPU_t* c, uint32_t a) { return (uint16_t)(cpu_read(c, a) | (cpu_read(c, a + 1) << 8)); }
static void wr16(CPU_t* c, uint32_t a, uint16_t v) { cpu_write(c, a, (uint8_t)v); cpu_write(c, a + 1, (uint8_t)(v >> 8)); }

static void scroll_up(CPU_t* c, uint32_t base, int cols, int rows) {
	for (int i = 0; i < cols * (rows - 1); i++) wr16(c, base + i * 2, rd16(c, base + (i + cols) * 2));
	for (int i = 0; i < cols; i++) wr16(c, base + (cols * (rows - 1) + i) * 2, 0x0720);
}

/* returns 1 if the call was handled here, 0 to continue with the INT 10h vector */
int int10ext_hook(CPU_t* cpu) {
	if (cpu->regs.byteregs[regah] != 0x13) return 0;

	uint8_t mode = rd8(cpu, BDA + 0x49);
	if (mode > 3) return 0;                               /* only colour text modes */

	uint8_t al = cpu->regs.byteregs[regal];
	uint8_t page = cpu->regs.byteregs[regbh] & 7;
	uint8_t attr = cpu->regs.byteregs[regbl];
	uint16_t count = cpu->regs.wordregs[regcx];
	int row = cpu->regs.byteregs[regdh], col = cpu->regs.byteregs[regdl];
	uint32_t src = ((uint32_t)cpu->segregs[reges] << 4) + cpu->regs.wordregs[regbp];
	int cols = rd16(cpu, BDA + 0x4A);
	if (cols != 40 && cols != 80) cols = (mode < 2) ? 40 : 80;
	const int rows = 25;
	uint16_t pagesize = rd16(cpu, BDA + 0x4C);
	if (!pagesize) pagesize = (uint16_t)(cols * rows * 2);
	uint32_t base = 0xB8000 + (uint32_t)page * pagesize;

	for (uint16_t i = 0; i < count; i++) {
		uint8_t ch = cpu_read(cpu, (src++) & 0xFFFFF);
		uint8_t a = attr;
		if (al & 2) a = cpu_read(cpu, (src++) & 0xFFFFF);
		switch (ch) {
		case 0x0D: col = 0; break;
		case 0x0A: row++; break;
		case 0x08: if (col > 0) col--; break;
		case 0x07: break;
		default:
			wr16(cpu, base + (uint32_t)(row * cols + col) * 2, (uint16_t)(ch | (a << 8)));
			if (++col >= cols) { col = 0; row++; }
		}
		if (row >= rows) { scroll_up(cpu, base, cols, rows); row = rows - 1; }
	}

	if (al & 1) {                                         /* update the cursor */
		wr16(cpu, BDA + 0x50 + page * 2, (uint16_t)((row << 8) | col));
		if (page == rd8(cpu, BDA + 0x62)) {
			uint16_t off = (uint16_t)(page * pagesize / 2 + row * cols + col);
			port_write(cpu, 0x3D4, 0x0E); port_write(cpu, 0x3D5, (uint8_t)(off >> 8));
			port_write(cpu, 0x3D4, 0x0F); port_write(cpu, 0x3D5, (uint8_t)off);
		}
	}
	return 1;
}
