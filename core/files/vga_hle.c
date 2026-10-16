/*
  vga_hle: EGA/VGA graphics for the XTulator Game & Watch port (see vga_hle.h).
  GPLv2 or later (same as XTulator).
*/
#include <stdint.h>
#include <string.h>
#include "vga_hle.h"
#include "memory.h"
#include "ports.h"
#include "xt_pool.h"
#include "modules/video/cga.h"

#define BDA 0x400

int vga_active = 0;
uint8_t vga_mode = 3;
volatile uint8_t vga_pal_dirty = 1;
uint32_t vga_plane_size = 0;

static uint8_t* vram = NULL;
static uint8_t seq[5], seqi, gc[9], gci, attr[0x15], attri, attrff, misc = 0x63;
static uint8_t dac[256][3], dac_wi, dac_ri, dac_comp, dac_state, dac_mask = 0xFF;
static uint32_t latch;
uint32_t vga_dbg_drop, vga_dbg_max;   /* accesses beyond video memory (debug) */
/* Anything that can change the picture bumps vga_hle_gen (vga_hle_frame_key): the frontend
   skips redrawing a picture that has not changed. With chain-4 mapped directly the writes
   bypass us, so that mode always redraws. */
static uint32_t vga_hle_gen;
static uint8_t vga_hle_direct;

/* lane p of the result = 0xFF if bit p of n is set */
static const uint32_t nib2lanes[16] = {
	0x00000000, 0x000000FF, 0x0000FF00, 0x0000FFFF, 0x00FF0000, 0x00FF00FF, 0x00FFFF00, 0x00FFFFFF,
	0xFF000000, 0xFF0000FF, 0xFF00FF00, 0xFF00FFFF, 0xFFFF0000, 0xFFFF00FF, 0xFFFFFF00, 0xFFFFFFFF };

static inline uint32_t rep8(uint8_t v) { return (uint32_t)v * 0x01010101u; }
static inline uint8_t ror8(uint8_t v, uint8_t n) { n &= 7; return n ? (uint8_t)((v >> n) | (v << (8 - n))) : v; }
static inline uint32_t* cell(uint32_t a) { return (uint32_t*)(vram + a * 4); }

static inline uint32_t alu(uint32_t d) {
	switch ((gc[3] >> 3) & 3) {
	case 1: return d & latch;
	case 2: return d | latch;
	case 3: return d ^ latch;
	}
	return d;
}

/* ---- memory at A0000h ---- */
static int mem_window_ok(void) { return ((gc[6] >> 2) & 3) < 2; }   /* A0000 128K or 64K map */

static void update_mapping(void) {
	/* chain-4 with all planes enabled is linear memory: map it directly (fast path) */
	if (vga_active && (seq[4] & 0x08) && (seq[2] & 0x0F) == 0x0F && mem_window_ok() && vga_plane_size * 4 >= 0x10000)
		{ memory_mapRegister(0xA0000, 0x10000, vram, vram); vga_hle_direct = 1; }
	else
		{ memory_mapRegister(0xA0000, 0x10000, NULL, NULL); vga_hle_direct = 0; }
	vga_hle_gen++;
}

static void vga_writemem(void* u, uint32_t addr, uint8_t val) {
	(void)u;
	vga_hle_gen++;
	if (!mem_window_ok()) return;
	uint32_t o = addr - 0xA0000;
	if (seq[4] & 0x08) {                               /* chain-4: plane = A0..A1 */
		if (o < vga_plane_size * 4 && (seq[2] & (1u << (o & 3)))) vram[o] = val;
		return;
	}
	if (!(seq[4] & 0x04)) {                            /* odd/even */
		uint32_t a = o & ~1u, p = o & 1;
		if (a < vga_plane_size && (seq[2] & (1u << p))) vram[a * 4 + p] = val;
		return;
	}
	if (o > vga_dbg_max) vga_dbg_max = o;
	if (o >= vga_plane_size) { vga_dbg_drop++; return; }
	uint32_t res, mask = rep8(gc[8]);
	switch (gc[5] & 3) {
	case 0: {
		uint32_t d = rep8(ror8(val, gc[3]));
		uint32_t en = nib2lanes[gc[1] & 0x0F];
		d = (d & ~en) | (nib2lanes[gc[0] & 0x0F] & en);
		res = (alu(d) & mask) | (latch & ~mask);
		break;
	}
	case 1:
		res = latch;
		break;
	case 2:
		res = (alu(nib2lanes[val & 0x0F]) & mask) | (latch & ~mask);
		break;
	default: {
		uint32_t m = rep8(ror8(val, gc[3]) & gc[8]);
		res = (alu(nib2lanes[gc[0] & 0x0F]) & m) | (latch & ~m);
		break;
	}
	}
	uint32_t pm = nib2lanes[seq[2] & 0x0F];
	uint32_t* c = cell(o);
	*c = (*c & ~pm) | (res & pm);
}

static uint8_t vga_readmem(void* u, uint32_t addr) {
	(void)u;
	if (!mem_window_ok()) return 0xFF;
	uint32_t o = addr - 0xA0000;
	if (seq[4] & 0x08) {
		if (o >= vga_plane_size * 4) return 0xFF;
		latch = *cell(o >> 2);
		return vram[o];
	}
	if (!(seq[4] & 0x04)) {
		uint32_t a = o & ~1u;
		if (a >= vga_plane_size) return 0xFF;
		latch = *cell(a);
		return vram[a * 4 + (o & 1)];
	}
	if (o >= vga_plane_size) return 0xFF;
	latch = *cell(o);
	if (!(gc[5] & 0x08)) return (uint8_t)(latch >> ((gc[4] & 3) * 8));
	uint32_t x = (latch ^ nib2lanes[gc[2] & 0x0F]) & nib2lanes[gc[7] & 0x0F];   /* colour compare */
	return (uint8_t)~(x | (x >> 8) | (x >> 16) | (x >> 24));
}

/* ---- ports 3C0h..3CFh ---- */
void vga_hle_status_read(void) { attrff = 0; }

static void vga_writeport(void* u, uint32_t port, uint8_t v) {
	(void)u;
	vga_hle_gen++;
	switch (port) {
	case 0x3C0:
		if (!attrff) attri = v & 0x3F;
		else if ((attri & 0x1F) < 0x15) { attr[attri & 0x1F] = v; vga_pal_dirty = 1; }
		attrff ^= 1;
		break;
	case 0x3C2: misc = v; break;
	case 0x3C4: seqi = v & 7; break;
	case 0x3C5: if (seqi < 5) { seq[seqi] = v; if (seqi == 2 || seqi == 4) update_mapping(); } break;
	case 0x3C6: dac_mask = v; vga_pal_dirty = 1; break;
	case 0x3C7: dac_ri = v; dac_comp = 0; dac_state = 3; break;
	case 0x3C8: dac_wi = v; dac_comp = 0; dac_state = 0; break;
	case 0x3C9:
		dac[dac_wi][dac_comp] = v & 0x3F;
		if (++dac_comp == 3) { dac_comp = 0; dac_wi++; }
		vga_pal_dirty = 1;
		break;
	case 0x3CE: gci = v & 0x0F; break;
	case 0x3CF: if (gci < 9) { gc[gci] = v; if (gci == 6) update_mapping(); } break;
	}
}

static uint8_t vga_readport(void* u, uint32_t port) {
	(void)u;
	switch (port) {
	case 0x3C0: return attri;
	case 0x3C1: return (attri & 0x1F) < 0x15 ? attr[attri & 0x1F] : 0;
	case 0x3C2: return 0x10;                           /* input status 0: switch sense */
	case 0x3C3: return 0x01;
	case 0x3C4: return seqi;
	case 0x3C5: return seqi < 5 ? seq[seqi] : 0xFF;
	case 0x3C6: return dac_mask;
	case 0x3C7: return dac_state;
	case 0x3C8: return dac_wi;
	case 0x3C9: {
		uint8_t r = dac[dac_ri][dac_comp];
		if (++dac_comp == 3) { dac_comp = 0; dac_ri++; }
		return r;
	}
	case 0x3CA: return 0x00;
	case 0x3CC: return misc;
	case 0x3CE: return gci;
	case 0x3CF: return gci < 9 ? gc[gci] : 0xFF;
	}
	return 0xFF;
}

/* 0 = must redraw every frame; otherwise equal keys mean an identical picture */
uint32_t vga_hle_frame_key(void) {
	if (vga_hle_direct || !vram) return 0;
	const uint8_t* c = cga_datareg;
	uint32_t k = vga_hle_gen * 2654435761u;
	k ^= ((uint32_t)c[0x0C] << 24) | ((uint32_t)c[0x0D] << 16) | ((uint32_t)c[0x12] << 8) | c[0x13];
	k ^= (((uint32_t)c[0x07] << 8) | c[0x09]) * 40503u;
	return k ? k : 1;
}

int vga_hle_init(uint32_t plane_size) {
	vram = (uint8_t*)xt_pool_alloc(plane_size * 4);
	if (!vram) return -1;
	vga_plane_size = plane_size;
	memset(vram, 0, plane_size * 4);
	static const uint8_t seq_text[5] = { 0x03, 0x00, 0x03, 0x00, 0x02 };
	static const uint8_t gc_text[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF };
	memcpy(seq, seq_text, 5);
	memcpy(gc, gc_text, 9);
	for (int i = 0; i < 16; i++) attr[i] = (uint8_t)i;
	attr[0x10] = 0x0C; attr[0x12] = 0x0F;
	ports_cbRegister(0x3C0, 16, vga_readport, NULL, vga_writeport, NULL, NULL);
	memory_mapCallbackRegister(0xA0000, 0x10000, vga_readmem, vga_writemem, NULL);
	vga_active = 0;
	return 0;
}

/* ---- default palettes ---- */
static void dac_set(int i, int r, int g, int b) { dac[i][0] = (uint8_t)r; dac[i][1] = (uint8_t)g; dac[i][2] = (uint8_t)b; }

static void dac_load_ega64(int lines200) {
	for (int i = 0; i < 64; i++) {
		int r, g, b;
		if (lines200) {                                /* 200-line modes: bit 4 = intensity */
			int in = (i & 0x10) ? 21 : 0;
			r = ((i & 4) ? 42 : 0) + in; g = ((i & 2) ? 42 : 0) + in; b = ((i & 1) ? 42 : 0) + in;
			if ((i & 0x17) == 6) g = 21;               /* brown */
		} else {                                       /* rgbRGB */
			r = ((i & 4) ? 42 : 0) + ((i & 0x20) ? 21 : 0);
			g = ((i & 2) ? 42 : 0) + ((i & 0x10) ? 21 : 0);
			b = ((i & 1) ? 42 : 0) + ((i & 0x08) ? 21 : 0);
		}
		dac_set(i, r, g, b);
	}
}

static void dac_load_vga256(void) {
	static const uint8_t cga16[16][3] = {
		{0,0,0},{0,0,42},{0,42,0},{0,42,42},{42,0,0},{42,0,42},{42,21,0},{42,42,42},
		{21,21,21},{21,21,63},{21,63,21},{21,63,63},{63,21,21},{63,21,63},{63,63,21},{63,63,63} };
	static const uint8_t grey[16] = { 0,5,8,11,14,17,20,24,28,32,36,40,45,50,56,63 };
	static const uint8_t lv[9][5] = {
		{0,16,31,47,63},{31,39,47,55,63},{45,49,54,58,63},
		{0,7,14,21,28},{14,17,21,24,28},{20,22,24,26,28},
		{0,4,8,12,16},{8,10,12,14,16},{11,12,13,15,16} };
	static const uint8_t hue[24][3] = {
		{0,0,4},{1,0,4},{2,0,4},{3,0,4},{4,0,4},{4,0,3},{4,0,2},{4,0,1},
		{4,0,0},{4,1,0},{4,2,0},{4,3,0},{4,4,0},{3,4,0},{2,4,0},{1,4,0},
		{0,4,0},{0,4,1},{0,4,2},{0,4,3},{0,4,4},{0,3,4},{0,2,4},{0,1,4} };
	for (int i = 0; i < 16; i++) dac_set(i, cga16[i][0], cga16[i][1], cga16[i][2]);
	for (int i = 0; i < 16; i++) dac_set(16 + i, grey[i], grey[i], grey[i]);
	int n = 32;
	for (int s = 0; s < 9; s++)
		for (int h = 0; h < 24; h++, n++) dac_set(n, lv[s][hue[h][0]], lv[s][hue[h][1]], lv[s][hue[h][2]]);
	for (; n < 256; n++) dac_set(n, 0, 0, 0);
}

/* ---- mode set ---- */
typedef struct {
	uint8_t mode, misc, seq[5], crtc[25], gc[9], attr10[5];
	uint8_t cols, rows, chh, units;       /* text geometry; 4-byte cells per pixel line */
	uint16_t pagesize;                     /* bytes per page (per plane) */
	uint32_t need;                         /* bytes per plane */
} VGAMODE;

static const VGAMODE modes[] = {
	{ 0x0D, 0x63, {0x03,0x09,0x0F,0x00,0x06},
	  {0x2D,0x27,0x28,0x90,0x2B,0x80,0xBF,0x1F,0x00,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x14,0x00,0x96,0xB9,0xE3,0xFF},
	  {0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x0F,0xFF}, {0x01,0x00,0x0F,0x00,0x00}, 40, 25, 8, 40, 0x2000, 8000 },
	{ 0x0E, 0x63, {0x03,0x01,0x0F,0x00,0x06},
	  {0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,0x00,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x28,0x00,0x96,0xB9,0xE3,0xFF},
	  {0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x0F,0xFF}, {0x01,0x00,0x0F,0x00,0x00}, 80, 25, 8, 80, 0x4000, 16000 },
	{ 0x10, 0xA3, {0x03,0x01,0x0F,0x00,0x06},
	  {0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x83,0x85,0x5D,0x28,0x0F,0x63,0xBA,0xE3,0xFF},
	  {0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x0F,0xFF}, {0x01,0x00,0x0F,0x00,0x00}, 80, 25, 14, 80, 0x8000, 28000 },
	{ 0x12, 0xE3, {0x03,0x01,0x0F,0x00,0x06},
	  {0x5F,0x4F,0x50,0x82,0x54,0x80,0x0B,0x3E,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0xEA,0x8C,0xDF,0x28,0x00,0xE7,0x04,0xE3,0xFF},
	  {0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x0F,0xFF}, {0x01,0x00,0x0F,0x00,0x00}, 80, 30, 16, 80, 0xA000, 38400 },
	{ 0x13, 0x63, {0x03,0x01,0x0F,0x00,0x0E},
	  {0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,0x00,0x41,0x00,0x00,0x00,0x00,0x00,0x00,0x9C,0x8E,0x8F,0x28,0x40,0x96,0xB9,0xA3,0xFF},
	  {0x00,0x00,0x00,0x00,0x00,0x40,0x05,0x0F,0xFF}, {0x41,0x00,0x0F,0x00,0x00}, 40, 25, 8, 80, 0x0000, 16000 },
};
#define NMODES (sizeof(modes) / sizeof(modes[0]))

static const VGAMODE* cur = NULL;

static uint8_t rd8(CPU_t* c, uint32_t a) { return cpu_read(c, a); }
static uint16_t rd16(CPU_t* c, uint32_t a) { return (uint16_t)(cpu_read(c, a) | (cpu_read(c, a + 1) << 8)); }
static void wr8(CPU_t* c, uint32_t a, uint8_t v) { cpu_write(c, a, v); }
static void wr16(CPU_t* c, uint32_t a, uint16_t v) { cpu_write(c, a, (uint8_t)v); cpu_write(c, a + 1, (uint8_t)(v >> 8)); }

static void set_start(uint32_t s) { cga_datareg[0x0C] = (uint8_t)(s >> 8); cga_datareg[0x0D] = (uint8_t)s; }

static int setmode(CPU_t* cpu, uint8_t al) {
	uint8_t m = al & 0x7F;
	const VGAMODE* md = NULL;
	for (unsigned i = 0; i < NMODES; i++) if (modes[i].mode == m) md = &modes[i];
	if (!md) return 0;
	if (md->need > vga_plane_size) return 1;           /* not enough video memory: refuse */
	cur = md;
	misc = md->misc;
	memcpy(seq, md->seq, 5);
	memcpy(gc, md->gc, 9);
	for (int i = 0; i < 25; i++) cga_datareg[i] = md->crtc[i];
	memcpy(&attr[0x10], md->attr10, 5);
	if (m == 0x13) {
		for (int i = 0; i < 16; i++) attr[i] = (uint8_t)i;
		dac_load_vga256();
	} else if (m == 0x10 || m == 0x12) {
		static const uint8_t egapal[16] = { 0,1,2,3,4,5,0x14,7,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F };
		memcpy(attr, egapal, 16);
		dac_load_ega64(0);
	} else {
		for (int i = 0; i < 8; i++) { attr[i] = (uint8_t)i; attr[8 + i] = (uint8_t)(0x10 + i); }
		dac_load_ega64(1);
	}
	attr[0x11] = 0;
	dac_mask = 0xFF;
	if (!(al & 0x80)) memset(vram, 0, vga_plane_size * 4);
	vga_mode = m;
	vga_active = 1;
	vga_pal_dirty = 1;
	update_mapping();

	wr8(cpu, BDA + 0x49, m);
	wr16(cpu, BDA + 0x4A, md->cols);
	wr16(cpu, BDA + 0x4C, md->pagesize);
	wr16(cpu, BDA + 0x4E, 0);
	for (int i = 0; i < 8; i++) wr16(cpu, BDA + 0x50 + i * 2, 0);
	wr8(cpu, BDA + 0x62, 0);
	wr16(cpu, BDA + 0x63, 0x3D4);
	wr8(cpu, BDA + 0x84, (uint8_t)(md->rows - 1));
	wr16(cpu, BDA + 0x85, md->chh);
	uint8_t memcode = (uint8_t)(vga_plane_size >= 65536 ? 3 : (vga_plane_size >= 32768 ? 1 : 0));
	wr8(cpu, BDA + 0x87, (uint8_t)((memcode << 5) | (al & 0x80)));
	wr8(cpu, BDA + 0x88, 0xF9);
	wr8(cpu, BDA + 0x89, 0x51);
	wr16(cpu, 0x43 * 4, 0xFA6E);                       /* INT 43h: 8x8 graphics font */
	wr16(cpu, 0x43 * 4 + 2, 0xF000);
	return 1;
}

static void leave_vga(void) {
	if (!vga_active) return;
	vga_active = 0;
	cur = NULL;
	seq[4] = 0x02;
	gc[5] = 0x10; gc[6] = 0x0E;                        /* window at B8000: CGA owns it */
	update_mapping();
	vga_pal_dirty = 1;
}

/* ---- drawing helpers for the BIOS services (cells = 4-byte vram units) ---- */
static uint32_t page_base(CPU_t* cpu) {
	return cur->pagesize ? (uint32_t)rd8(cpu, BDA + 0x62) * cur->pagesize : 0;
}
static int is256(void) { return (attr[0x10] & 0x40) != 0; }
static uint32_t fill_cell(uint8_t color) { return is256() ? rep8(color) : nib2lanes[color & 0x0F]; }
static int cells_per_col(void) { return is256() ? 2 : 1; }

static uint8_t glyph(CPU_t* cpu, uint8_t ch, int row) {
	if (ch < 0x80) return rd8(cpu, 0xFFA6E + ch * 8 + row);
	uint16_t off = rd16(cpu, 0x1F * 4), seg = rd16(cpu, 0x1F * 4 + 2);
	if (!off && !seg) return 0;
	return rd8(cpu, (((uint32_t)seg << 4) + off + (ch - 0x80) * 8 + row) & 0xFFFFF);
}

static void draw_char(CPU_t* cpu, int col, int row, uint8_t ch, uint8_t color) {
	uint32_t base = page_base(cpu);
	int units = cur->units, chh = cur->chh;
	for (int r = 0; r < chh; r++) {
		uint8_t g = r < 8 ? glyph(cpu, ch, r) : 0;
		uint32_t y = (uint32_t)(row * chh + r);
		if (is256()) {
			uint32_t idx = (y * units + col * 2) * 4;
			if (idx + 8 > vga_plane_size * 4) return;
			for (int k = 0; k < 8; k++) vram[idx + k] = (g & (0x80 >> k)) ? color : 0;
		} else {
			uint32_t a = base + y * units + col;
			if (a >= vga_plane_size) return;
			uint32_t v = rep8(g) & nib2lanes[color & 0x0F];
			uint32_t* c = cell(a);
			if (color & 0x80) *c ^= v; else *c = v;
		}
	}
}

/* scroll a window of character cells up (n > 0) or down (n < 0); n = 0 clears it */
static void scroll(CPU_t* cpu, int r0, int c0, int r1, int c1, int n, uint8_t color, int up) {
	int rows = rd8(cpu, BDA + 0x84) + 1, cols = rd16(cpu, BDA + 0x4A);
	if (r1 >= rows) r1 = rows - 1;
	if (c1 >= cols) c1 = cols - 1;
	if (r0 > r1 || c0 > c1) return;
	int h = r1 - r0 + 1;
	if (n <= 0 || n > h) n = h;
	uint32_t base = page_base(cpu), units = cur->units, chh = cur->chh;
	int cpc = cells_per_col(), w = (c1 - c0 + 1) * cpc;
	uint32_t f = fill_cell(color);
	for (int i = 0; i < h; i++) {
		int dst = up ? r0 + i : r1 - i;
		int src = up ? dst + n : dst - n;
		for (uint32_t y = 0; y < chh; y++) {
			uint32_t d = base + (dst * chh + y) * units + c0 * cpc;
			uint32_t s = base + (src * chh + y) * units + c0 * cpc;
			for (int x = 0; x < w; x++) {
				if (d + x >= vga_plane_size) break;
				int inside = up ? (src <= r1) : (src >= r0);
				*cell(d + x) = (inside && s + x < vga_plane_size) ? *cell(s + x) : f;
			}
		}
	}
}

static void put_pixel(CPU_t* cpu, uint32_t x, uint32_t y, uint8_t color) {
	if (is256()) {
		uint32_t i = y * 320 + x;
		if (i < vga_plane_size * 4) vram[i] = color;
		return;
	}
	uint32_t a = page_base(cpu) + y * cur->units + (x >> 3);
	if (a >= vga_plane_size) return;
	uint32_t bit = rep8((uint8_t)(0x80 >> (x & 7))), c = nib2lanes[color & 0x0F];
	uint32_t* p = cell(a);
	if (color & 0x80) *p ^= bit & c;
	else *p = (*p & ~bit) | (bit & c);
}

static uint8_t get_pixel(CPU_t* cpu, uint32_t x, uint32_t y) {
	if (is256()) {
		uint32_t i = y * 320 + x;
		return i < vga_plane_size * 4 ? vram[i] : 0;
	}
	uint32_t a = page_base(cpu) + y * cur->units + (x >> 3);
	if (a >= vga_plane_size) return 0;
	uint32_t v = *cell(a), sh = 7 - (x & 7);
	uint8_t r = 0;
	for (int p = 0; p < 4; p++) r |= (uint8_t)(((v >> (p * 8 + sh)) & 1) << p);
	return r;
}

static void teletype(CPU_t* cpu, uint8_t ch, uint8_t color) {
	uint8_t page = rd8(cpu, BDA + 0x62);
	uint16_t pos = rd16(cpu, BDA + 0x50 + page * 2);
	int row = pos >> 8, col = pos & 0xFF;
	int rows = rd8(cpu, BDA + 0x84) + 1, cols = rd16(cpu, BDA + 0x4A);
	switch (ch) {
	case 0x07: return;
	case 0x08: if (col > 0) col--; break;
	case 0x0A: row++; break;
	case 0x0D: col = 0; break;
	default:
		draw_char(cpu, col, row, ch, color);
		if (++col >= cols) { col = 0; row++; }
	}
	if (row >= rows) { scroll(cpu, 0, 0, rows - 1, cols - 1, 1, 0, 1); row = rows - 1; }
	wr16(cpu, BDA + 0x50 + page * 2, (uint16_t)((row << 8) | col));
}

/* ---- INT 10h ---- */
#define AH cpu->regs.byteregs[regah]
#define AL cpu->regs.byteregs[regal]
#define BH cpu->regs.byteregs[regbh]
#define BL cpu->regs.byteregs[regbl]
#define CH cpu->regs.byteregs[regch]
#define CL cpu->regs.byteregs[regcl]
#define DH cpu->regs.byteregs[regdh]
#define DL cpu->regs.byteregs[regdl]
#define BX cpu->regs.wordregs[regbx]
#define CX cpu->regs.wordregs[regcx]
#define DX cpu->regs.wordregs[regdx]

static uint32_t es_ptr(CPU_t* cpu, uint16_t off) { return (((uint32_t)cpu->segregs[reges] << 4) + off) & 0xFFFFF; }

static int palette_services(CPU_t* cpu) {
	uint32_t p;
	switch (AL) {
	case 0x00: attr[BL & 0x0F] = BH; break;
	case 0x01: attr[0x11] = BH; break;
	case 0x02:
		p = es_ptr(cpu, DX);
		for (int i = 0; i < 16; i++) attr[i] = rd8(cpu, p + i);
		attr[0x11] = rd8(cpu, p + 16);
		break;
	case 0x03: {
		if (BL) attr[0x10] |= 0x08; else attr[0x10] &= ~0x08;
		uint8_t m = rd8(cpu, BDA + 0x65);
		m = BL ? (m | 0x20) : (m & ~0x20);
		wr8(cpu, BDA + 0x65, m);
		if (!vga_active) port_write(cpu, 0x3D8, m);
		break;
	}
	case 0x07: BH = attr[BL & 0x1F]; return 1;
	case 0x08: BH = attr[0x11]; return 1;
	case 0x09:
		p = es_ptr(cpu, DX);
		for (int i = 0; i < 16; i++) wr8(cpu, p + i, attr[i]);
		wr8(cpu, p + 16, attr[0x11]);
		return 1;
	case 0x10: dac_set(BL, DH & 63, CH & 63, CL & 63); break;
	case 0x12:
		p = es_ptr(cpu, DX);
		for (uint32_t i = 0; i < CX; i++) {
			uint8_t n = (uint8_t)(BX + i);
			dac_set(n, rd8(cpu, p) & 63, rd8(cpu, p + 1) & 63, rd8(cpu, p + 2) & 63);
			p += 3;
		}
		break;
	case 0x13:
		if (BL == 0) { if (BH & 1) attr[0x10] |= 0x80; else attr[0x10] &= ~0x80; }
		else attr[0x14] = (attr[0x10] & 0x80) ? (BH & 0x0F) : ((BH & 3) << 2);
		break;
	case 0x15: DH = dac[BL][0]; CH = dac[BL][1]; CL = dac[BL][2]; return 1;
	case 0x17:
		p = es_ptr(cpu, DX);
		for (uint32_t i = 0; i < CX; i++) {
			uint8_t n = (uint8_t)(BX + i);
			wr8(cpu, p, dac[n][0]); wr8(cpu, p + 1, dac[n][1]); wr8(cpu, p + 2, dac[n][2]);
			p += 3;
		}
		return 1;
	case 0x18: dac_mask = BL; break;
	case 0x19: BL = dac_mask; return 1;
	case 0x1A:
		BL = (attr[0x10] >> 7) & 1;
		BH = (attr[0x10] & 0x80) ? (attr[0x14] & 0x0F) : ((attr[0x14] >> 2) & 3);
		return 1;
	case 0x1B:
		for (uint32_t i = 0; i < CX; i++) {
			uint8_t n = (uint8_t)(BX + i);
			uint8_t g = (uint8_t)((dac[n][0] * 30 + dac[n][1] * 59 + dac[n][2] * 11 + 50) / 100);
			dac_set(n, g, g, g);
		}
		break;
	default: return 1;
	}
	vga_pal_dirty = 1;
	return 1;
}

int vga_hle_int10(CPU_t* cpu) {
	vga_hle_gen++;
	if (!vram) return 0;
	switch (AH) {
	case 0x00:
		if (setmode(cpu, AL)) return 1;
		leave_vga();                                   /* text / CGA mode: the BIOS does it */
		return 0;
	case 0x10: return palette_services(cpu);
	case 0x11:
		if (AL == 0x30) {
			uint16_t seg = 0xF000, off = 0xFA6E;
			if (BH == 0) { off = rd16(cpu, 0x1F * 4); seg = rd16(cpu, 0x1F * 4 + 2); }
			else if (BH == 1) { off = rd16(cpu, 0x43 * 4); seg = rd16(cpu, 0x43 * 4 + 2); }
			else if (BH == 4) { off = rd16(cpu, 0x1F * 4); seg = rd16(cpu, 0x1F * 4 + 2); }
			cpu->segregs[reges] = seg;
			cpu->regs.wordregs[regbp] = off;
			CX = rd16(cpu, BDA + 0x85) ? rd16(cpu, BDA + 0x85) : 8;
			DL = rd8(cpu, BDA + 0x84) ? rd8(cpu, BDA + 0x84) : 24;
		}
		return 1;                                      /* font loading: ignored */
	case 0x12:
		if (BL == 0x10) {                              /* EGA info: colour, memory 64/128/192/256 KB */
			BH = 0; BL = (uint8_t)(vga_plane_size >= 65536 ? 3 : (vga_plane_size >= 32768 ? 1 : 0)); CH = 0; CL = 9;
		}
		else if (BL >= 0x30 && BL <= 0x36) AL = 0x12;
		return 1;
	case 0x1A:
		if (AL == 0x00) { AL = 0x1A; BL = 0x08; BH = 0x00; }
		else if (AL == 0x01) AL = 0x1A;
		return 1;
	}
	if (!vga_active || !cur) return 0;

	uint8_t page;
	switch (AH) {
	case 0x01: wr16(cpu, BDA + 0x60, CX); return 1;
	case 0x02: wr16(cpu, BDA + 0x50 + (BH & 7) * 2, DX); return 1;
	case 0x03: DX = rd16(cpu, BDA + 0x50 + (BH & 7) * 2); CX = rd16(cpu, BDA + 0x60); return 1;
	case 0x04: AH = 0; return 1;
	case 0x05:
		page = AL & 7;
		wr8(cpu, BDA + 0x62, page);
		wr16(cpu, BDA + 0x4E, (uint16_t)(page * cur->pagesize));
		set_start((uint32_t)page * cur->pagesize);
		return 1;
	case 0x06:
	case 0x07:
		scroll(cpu, CH, CL, DH, DL, AL, BH, AH == 0x06);
		return 1;
	case 0x08: cpu->regs.wordregs[regax] = 0; return 1;
	case 0x09:
	case 0x0A: {
		page = rd8(cpu, BDA + 0x62);
		uint16_t pos = rd16(cpu, BDA + 0x50 + page * 2);
		int col = pos & 0xFF, row = pos >> 8, cols = rd16(cpu, BDA + 0x4A);
		for (uint16_t i = 0; i < CX && col < cols; i++, col++) draw_char(cpu, col, row, AL, BL);
		return 1;
	}
	case 0x0B:
		if (BH == 0 && !is256()) {
			uint8_t c = (uint8_t)((BL & 7) | ((BL & 0x08) ? 0x10 : 0));
			attr[0] = c; attr[0x11] = c; vga_pal_dirty = 1;
		}
		return 1;
	case 0x0C: put_pixel(cpu, CX, DX, AL); return 1;
	case 0x0D: AL = get_pixel(cpu, CX, DX); return 1;
	case 0x0E: teletype(cpu, AL, BL); return 1;
	case 0x0F:
		AL = (uint8_t)(vga_mode | (rd8(cpu, BDA + 0x87) & 0x80));
		AH = (uint8_t)rd16(cpu, BDA + 0x4A);
		BH = rd8(cpu, BDA + 0x62);
		return 1;
	case 0x13: {
		uint32_t src = es_ptr(cpu, cpu->regs.wordregs[regbp]);
		page = BH & 7;
		uint16_t save = rd16(cpu, BDA + 0x50 + page * 2);
		wr16(cpu, BDA + 0x50 + page * 2, DX);
		for (uint16_t i = 0; i < CX; i++) {
			uint8_t ch = rd8(cpu, src++), a = BL;
			if (AL & 2) a = rd8(cpu, src++);
			teletype(cpu, ch, a);
		}
		if (!(AL & 1)) wr16(cpu, BDA + 0x50 + page * 2, save);
		return 1;
	}
	}
	return 0;
}

/* ---- renderer ---- */
int vga_hle_is256(void) { return is256(); }

static uint32_t rgb6(const uint8_t* c) {
	uint32_t r = (c[0] << 2) | (c[0] >> 4), g = (c[1] << 2) | (c[1] >> 4), b = (c[2] << 2) | (c[2] >> 4);
	return (r << 16) | (g << 8) | b;
}

void vga_hle_palette16(uint32_t* out) {
	for (int n = 0; n < 16; n++) {
		uint8_t a = attr[n], idx;
		if (attr[0x10] & 0x80) idx = (uint8_t)(((attr[0x14] & 0x0F) << 4) | (a & 0x0F));
		else idx = (uint8_t)(((attr[0x14] & 0x0C) << 4) | (a & 0x3F));
		out[n] = rgb6(dac[idx & dac_mask]);
	}
	vga_pal_dirty = 0;
}

void vga_hle_palette256(uint32_t* out) {
	for (int n = 0; n < 256; n++) out[n] = rgb6(dac[n & dac_mask]);
	vga_pal_dirty = 0;
}

/* byte k (leftmost pixel first) = bit (7-k) of the index */
static uint64_t expand_bits(uint8_t b) {
	uint64_t v = 0;
	for (int k = 0; k < 8; k++) if (b & (0x80 >> k)) v |= (uint64_t)1 << (k * 8);
	return v;
}

void vga_hle_render(uint8_t* fb, uint8_t base16) {
	static uint64_t ex[256];
	static int ex_ok = 0;
	if (!ex_ok) { for (int i = 0; i < 256; i++) ex[i] = expand_bits((uint8_t)i); ex_ok = 1; }

	const uint8_t* c = cga_datareg;
	uint32_t vde = (c[0x12] | ((c[0x07] & 0x02) << 7) | ((c[0x07] & 0x40) << 3)) + 1;
	uint32_t msl = (c[0x09] & 0x1F) + 1;
	if (c[0x09] & 0x80) msl *= 2;
	uint32_t rows = vde / msl;
	if (rows < 1 || rows > 600) rows = 200;
	uint32_t start = ((uint32_t)c[0x0C] << 8) | c[0x0D];
	uint32_t pitch = (uint32_t)c[0x13] * 2;
	uint32_t ps = vga_plane_size;
	int out_h = rows < 200 ? (int)rows : 200;
	int y0 = (200 - out_h) / 2;
	if (y0) memset(fb, 0, 320 * 200);

	if (attr[0x10] & 0x40) {                           /* 256 colours: 4 pixels per cell */
		for (int y = 0; y < out_h; y++) {
			uint32_t srow = rows > 200 ? (uint32_t)y * rows / 200 : (uint32_t)y;
			uint32_t a = (start + srow * pitch) % ps;
			uint8_t* d = fb + (y0 + y) * 320;
			if (a + 80 <= ps) memcpy(d, vram + a * 4, 320);
			else for (int x = 0; x < 320; x++) d[x] = vram[((a + (x >> 2)) % ps) * 4 + (x & 3)];
		}
		return;
	}
	/* 16 colours, planar: 8 pixels per cell; 640-wide modes keep every second pixel */
	int wide = !(seq[1] & 0x08);
	uint32_t pen = attr[0x12] & 0x0F;
	uint64_t add = 0x0101010101010101ull * base16;
	for (int y = 0; y < out_h; y++) {
		uint32_t srow = rows > 200 ? (uint32_t)y * rows / 200 : (uint32_t)y;
		uint32_t a = (start + srow * pitch) % ps;
		uint8_t* d = fb + (y0 + y) * 320;
		int cells = wide ? 80 : 40;
		for (int u = 0; u < cells; u++) {
			uint32_t v = *cell((a + u) % ps);
			uint64_t px = 0;
			if (pen & 1) px |= ex[v & 0xFF];
			if (pen & 2) px |= ex[(v >> 8) & 0xFF] << 1;
			if (pen & 4) px |= ex[(v >> 16) & 0xFF] << 2;
			if (pen & 8) px |= ex[v >> 24] << 3;
			px += add;
			if (!wide) { memcpy(d, &px, 8); d += 8; }
			else { d[0] = (uint8_t)px; d[1] = (uint8_t)(px >> 16); d[2] = (uint8_t)(px >> 32); d[3] = (uint8_t)(px >> 48); d += 4; }
		}
	}
}
