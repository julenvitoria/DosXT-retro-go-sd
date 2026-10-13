/*
  mouse_hle: a Microsoft-compatible mouse driver (INT 33h) inside the emulator, for the
  XTulator Game & Watch port. GPLv2 or later (same as XTulator).

  MS-DOS 6.22 ships no mouse driver, so games would never see one. When the frontend
  enables it, INT 33h is answered here before the guest's vector and the vector itself is
  pointed at an IRET so that games which check it find a driver. The frontend moves the
  pointer and presses the buttons (from the console's d-pad / buttons) and can draw the
  driver's cursor itself on top of the picture (the cursor never touches video memory).

  Event handlers (functions 0Ch / 14h) are stored but not called.
*/
#include <stdint.h>
#include "cpu/cpu.h"

int mouse_hle_enabled = 0;

static int mx, my, xmin, xmax, ymin, ymax, shown = -1, buttons;
static int mick_x, mick_y;
static uint16_t press_count[3], release_count[3];
static int press_x[3], press_y[3], release_x[3], release_y[3];

#define BDA 0x400

static int vheight(CPU_t* cpu) {
	uint8_t mode = cpu_read(cpu, BDA + 0x49);
	if (mode == 0x0F || mode == 0x10) return 350;
	if (mode == 0x11 || mode == 0x12) return 480;
	return 200;
}

static void defaults(CPU_t* cpu) {
	xmin = 0; xmax = 639; ymin = 0; ymax = vheight(cpu) - 1;
	mx = 320; my = (ymax + 1) / 2;
	shown = -1; mick_x = mick_y = 0;
	for (int i = 0; i < 3; i++) press_count[i] = release_count[i] = 0;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void mouse_hle_move(int dx, int dy) {
	mick_x += dx * 2;                                 /* default 8 mickeys per 8 pixels (x 640 virtual) */
	mick_y += dy * 2;
	mx = clampi(mx + dx * 2, xmin, xmax);              /* frontend gives 320x200 screen pixels */
	my = clampi(my + dy, ymin, ymax);
}

void mouse_hle_buttons(int b) {
	for (int i = 0; i < 3; i++) {
		int m = 1 << i;
		if ((b & m) && !(buttons & m)) { press_count[i]++; press_x[i] = mx; press_y[i] = my; }
		if (!(b & m) && (buttons & m)) { release_count[i]++; release_x[i] = mx; release_y[i] = my; }
	}
	buttons = b;
}

/* cursor position on the 320x200 picture; returns 1 if the driver cursor is shown */
int mouse_hle_cursor(CPU_t* cpu, int* x, int* y) {
	*x = mx / 2;
	*y = my * 200 / vheight(cpu);
	return mouse_hle_enabled && shown >= 0;
}

/* make INT 33h look installed: point the vector at an IRET in the BIOS ROM if it is empty */
void mouse_hle_install(CPU_t* cpu) {
	if (!mouse_hle_enabled) return;
	uint16_t off = (uint16_t)(cpu_read(cpu, 0xCC) | (cpu_read(cpu, 0xCD) << 8));
	uint16_t seg = (uint16_t)(cpu_read(cpu, 0xCE) | (cpu_read(cpu, 0xCF) << 8));
	if (off || seg) return;
	for (uint32_t a = 0xFFFF0; a > 0xFE000; a--) {
		if (cpu_read(cpu, a) == 0xCF) {
			uint16_t o = (uint16_t)(a - 0xF0000);
			cpu_write(cpu, 0xCC, (uint8_t)o); cpu_write(cpu, 0xCD, (uint8_t)(o >> 8));
			cpu_write(cpu, 0xCE, 0x00); cpu_write(cpu, 0xCF, 0xF0);
			static int init = 0;
			if (!init) { defaults(cpu); init = 1; }
			return;
		}
	}
}

#define AX cpu->regs.wordregs[regax]
#define BX cpu->regs.wordregs[regbx]
#define CX cpu->regs.wordregs[regcx]
#define DX cpu->regs.wordregs[regdx]

int mouse_hle_int33(CPU_t* cpu) {
	if (!mouse_hle_enabled) return 0;
	int b;
	switch (AX) {
	case 0x00:
	case 0x21:
		defaults(cpu);
		AX = 0xFFFF; BX = 2;
		break;
	case 0x01: if (shown < 0) shown++; break;
	case 0x02: shown--; break;
	case 0x03: BX = (uint16_t)buttons; CX = (uint16_t)mx; DX = (uint16_t)my; break;
	case 0x04: mx = clampi((int16_t)CX, xmin, xmax); my = clampi((int16_t)DX, ymin, ymax); break;
	case 0x05:
		b = BX < 3 ? BX : 0;
		AX = (uint16_t)buttons; BX = press_count[b]; CX = (uint16_t)press_x[b]; DX = (uint16_t)press_y[b];
		press_count[b] = 0;
		break;
	case 0x06:
		b = BX < 3 ? BX : 0;
		AX = (uint16_t)buttons; BX = release_count[b]; CX = (uint16_t)release_x[b]; DX = (uint16_t)release_y[b];
		release_count[b] = 0;
		break;
	case 0x07: {
		int a = (int16_t)CX, c = (int16_t)DX;
		xmin = a < c ? a : c; xmax = a < c ? c : a;
		mx = clampi(mx, xmin, xmax);
		break;
	}
	case 0x08: {
		int a = (int16_t)CX, c = (int16_t)DX;
		ymin = a < c ? a : c; ymax = a < c ? c : a;
		my = clampi(my, ymin, ymax);
		break;
	}
	case 0x0B:
		CX = (uint16_t)mick_x; DX = (uint16_t)mick_y;
		mick_x = mick_y = 0;
		break;
	case 0x14: CX = 0; DX = 0; cpu->segregs[reges] = 0; break;
	case 0x15: BX = 16; break;
	case 0x1B: BX = 50; CX = 50; DX = 50; break;
	case 0x24: BX = 0x0626; CX = 0x0400; break;            /* driver 6.26, PS/2 mouse */
	default: break;                                         /* shapes, handlers, ratios: accepted */
	}
	return 1;
}
