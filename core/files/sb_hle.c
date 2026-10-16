/*
  sb_hle: Sound Blaster 2.0 (DSP 2.01) for the gw-dos (XTulator) Game & Watch port. GPLv2 or later.

  Base 220h, IRQ 7, DMA 1 -- what BLASTER=A220 I7 D1 T3 says and what most games assume
  without one. Its FM part is the AdLib (adlib_hle.c), also answered at 228h/229h.

  What a game sees:
    reset (226h: 1 then 0)          -> AAh in the read buffer
    E1h version                     -> 02h 01h (DSP 2.01: auto-init and high-speed DMA)
    40h time constant, 48h block size, 14h / 91h single-cycle and 1Ch / 90h auto-init 8-bit
    DMA, 10h direct DAC, 80h silence, D0h/D4h pause/continue, DAh end auto-init,
    D1h/D3h/D8h speaker, E0h/E4h/E8h test, F2h IRQ, 20h ADC (answers silence).
    22Eh read: data-available flag AND the 8-bit IRQ acknowledge.
  The 2-bit/4-bit ADPCM commands are run as 8-bit PCM so a game that uses them at least
  gets its IRQs (and noise) rather than hanging.

  Timing is the emulated CPU's, to the instruction: before each block of instructions the
  frame loop calls sb_hle_chunk(), after it sb_hle_tick(), which takes from DMA channel 1
  every sample that came due inside the block, stamped with its position in the frame, and
  raises IRQ 7 at the end of each DMA block. Direct-DAC writes are stamped the same way.
  sb_hle_render_mix() turns the frame's stamped samples into the 48 kHz output (each sample
  held until the next, so any playback rate comes out at its own speed) and adds them in.
*/
#include <stdint.h>
#include <string.h>
#include "chipset/i8237.h"
#include "chipset/i8259.h"
#ifdef SB_LOG                              /* host debugging only: DSP traffic to sb.log */
#include <stdio.h>
static FILE* sb_log;
#define LOG(...) do { if (!sb_log) sb_log = fopen("sb.log", "w"); fprintf(sb_log, __VA_ARGS__); fflush(sb_log); } while (0)
#else
#define LOG(...) do { } while (0)
#endif

#define SB_BASE 0x220
#define SB_IRQ 7
#define SB_DMA 1
#define SB_GAIN 48                         /* 8-bit sample -> about +-6,100: under the music */

void ports_cbRegister(uint32_t start, uint32_t count, uint8_t (*readb)(void*, uint32_t), uint16_t (*readw)(void*, uint32_t),
                      void (*writeb)(void*, uint32_t, uint8_t), void (*writew)(void*, uint32_t, uint16_t), void* udata);
uint8_t adlib_hle_port_read(uint32_t port);
void adlib_hle_port_write(uint32_t port, uint8_t val);

uint32_t sb_hle_resets, sb_hle_dma_bytes, sb_hle_irqs;

static I8237_t* dma;
static I8259_t* pic;
static const uint64_t* exec_ctr;
static const uint32_t* ips_ptr;

/* DSP state */
static uint8_t rbuf[16], rlen;             /* bytes the game will read from 22Ah */
static uint8_t cmd, need, got, args[2];    /* command being collected */
static uint8_t speaker, testreg, mixer_idx, mixer[256], reset_latch, irq_pending;
static uint32_t rate = 11025;              /* samples per second, from the time constant */
static uint32_t blocksize = 0x800;
static uint8_t dma_on, dma_auto, dma_paused, dma_stop_after;
static uint32_t dma_left;                  /* samples left in the current block */
static uint32_t silence_left;              /* 80h: samples of silence before the IRQ */
static uint64_t phase;                     /* instructions x rate, against ips */

/* this frame's samples: position (instructions into the frame) and value */
#define EV_MAX 1024                        /* 44.1 kHz / 60 = 735 a frame */
static struct { uint16_t pos; int8_t val; } ev[EV_MAX];
static unsigned nev;
static int8_t cur_level;
static uint32_t chunk_done;
static uint64_t chunk_exec0;

static void push_read(uint8_t b) { if (rlen < sizeof rbuf) rbuf[rlen++] = b; }

static void put_sample(uint32_t pos, int8_t v) {
	if (nev < EV_MAX) { ev[nev].pos = (uint16_t)(pos > 0xFFFF ? 0xFFFF : pos); ev[nev].val = v; nev++; }
	else cur_level = v;
}

static uint32_t now_pos(void) {
	uint64_t d = exec_ctr ? *exec_ctr - chunk_exec0 : 0;
	return chunk_done + (uint32_t)d;
}

static void raise_irq(void) { irq_pending = 1; sb_hle_irqs++; i8259_doirq(pic, SB_IRQ); }

static void dsp_reset(void) {
	rlen = 0; cmd = need = got = 0;
	dma_on = dma_auto = dma_paused = dma_stop_after = 0; silence_left = 0;
	speaker = 0; irq_pending = 0; cur_level = 0;
	push_read(0xAA);
	sb_hle_resets++;
}

static void start_dma(uint32_t len, int autoinit) {
	dma_left = len; dma_on = 1; dma_auto = (uint8_t)autoinit; dma_paused = 0; dma_stop_after = 0;
}

static void run_command(void) {
	uint32_t len = (uint32_t)args[0] | ((uint32_t)args[1] << 8);
	switch (cmd) {
	case 0x10: put_sample(now_pos(), (int8_t)(args[0] - 128)); break;     /* direct DAC */
	case 0x14: case 0x16: case 0x17: case 0x74: case 0x75: case 0x76: case 0x77:
		start_dma(len + 1, 0); break;                                      /* single-cycle */
	case 0x1C: case 0x1F: case 0x7D: case 0x7F: case 0x90:
		start_dma(blocksize, 1); break;                                    /* auto-init */
	case 0x91: start_dma(blocksize, 0); break;                             /* high-speed single */
	case 0x40: rate = 1000000u / (uint32_t)(256 - args[0]); if (!rate) rate = 4000; break;
	case 0x41: case 0x42: rate = ((uint32_t)args[0] << 8) | args[1]; if (!rate) rate = 4000; break;
	case 0x48: blocksize = len + 1; break;
	case 0x80: silence_left = len + 1; break;
	case 0xD0: dma_paused = 1; break;
	case 0xD4: dma_paused = 0; break;
	case 0xDA: dma_stop_after = 1; break;
	case 0xD1: speaker = 1; break;
	case 0xD3: speaker = 0; break;
	case 0xD8: push_read(speaker ? 0xFF : 0x00); break;
	case 0xE0: push_read((uint8_t)~args[0]); break;
	case 0xE1: push_read(0x02); push_read(0x01); break;
	case 0xE4: testreg = args[0]; break;
	case 0xE8: push_read(testreg); break;
	case 0xF2: raise_irq(); break;
	case 0x20: push_read(0x80); break;
	default: break;
	}
}

static uint8_t nargs(uint8_t c) {
	switch (c) {
	case 0x10: case 0x40: case 0xE0: case 0xE4: return 1;
	case 0x14: case 0x16: case 0x17: case 0x74: case 0x75: case 0x76: case 0x77:
	case 0x41: case 0x42: case 0x48: case 0x80: return 2;
	default: return 0;
	}
}

static uint8_t sb_read(void* u, uint32_t port) {
	(void)u;
	LOG("R %03X\n", (unsigned)port);
	switch (port - SB_BASE) {
	case 0x4: return mixer_idx;
	case 0x5: return mixer[mixer_idx];
	case 0x8: case 0x9: return adlib_hle_port_read(port - SB_BASE - 8);
	case 0xA: {                                              /* read data */
		uint8_t b = rlen ? rbuf[0] : 0xFF;
		if (rlen) { memmove(rbuf, rbuf + 1, sizeof rbuf - 1); rlen--; }
		return b;
	}
	case 0xC: return 0x7F;                                   /* write buffer: always ready */
	case 0xE:                                                /* read status + IRQ acknowledge */
		irq_pending = 0;
		return rlen ? 0xFF : 0x7F;
	default: return 0xFF;
	}
}

static void sb_write(void* u, uint32_t port, uint8_t val) {
	(void)u;
	LOG("W %03X %02X\n", (unsigned)port, val);
	switch (port - SB_BASE) {
	case 0x4: mixer_idx = val; break;
	case 0x5: mixer[mixer_idx] = val; break;
	case 0x6:
		if (val & 1) reset_latch = 1;
		else if (reset_latch) { reset_latch = 0; dsp_reset(); }
		break;
	case 0x8: case 0x9: adlib_hle_port_write(port - SB_BASE - 8, val); break;
	case 0xC:
		if (need) {                                          /* an argument */
			args[got++] = val;
			if (got >= need) { need = 0; run_command(); }
		} else {
			cmd = val; got = 0; need = nargs(val);
			if (!need) run_command();
		}
		break;
	default: break;
	}
}

void sb_hle_init(I8237_t* d, I8259_t* p, const uint64_t* exec, const uint32_t* ips) {
	dma = d; pic = p; exec_ctr = exec; ips_ptr = ips;
	dsp_reset(); sb_hle_resets = 0;
	ports_cbRegister(SB_BASE, 16, sb_read, NULL, sb_write, NULL, NULL);
}

void sb_hle_chunk(uint32_t done) {
	chunk_done = done;
	chunk_exec0 = exec_ctr ? *exec_ctr : 0;
}

/* after a block of n instructions that started `done` instructions into the frame */
void sb_hle_tick(uint32_t done, uint32_t n) {
	uint32_t ips = ips_ptr && *ips_ptr ? *ips_ptr : 1;
	int busy = (dma_on && !dma_paused) || silence_left;
	if (!busy) { phase = 0; return; }
	uint64_t before = phase;
	phase += (uint64_t)n * rate;
	uint32_t k = 0;
	while (phase >= ips) {
		phase -= ips;
		k++;
		uint32_t at = done + (uint32_t)(((uint64_t)k * ips - before + rate - 1) / rate);   /* when it came due */
		if (at > done + n) at = done + n;
		if (silence_left) {
			put_sample(at, 0);
			if (--silence_left == 0) raise_irq();
			continue;
		}
		if (!dma_on || dma_paused) break;
		uint8_t b = 0x80;
		if (!dma->chan[SB_DMA].masked) { b = i8237_read(dma, SB_DMA); sb_hle_dma_bytes++; }
		put_sample(at, speaker ? (int8_t)(b - 128) : 0);
		if (--dma_left == 0) {
			raise_irq();
			if (dma_auto && !dma_stop_after) dma_left = blocksize;
			else dma_on = 0;
		}
	}
}

/* the frame's samples, held between their stamps, into n output samples */
void sb_hle_render_mix(int16_t* dst, unsigned n, uint32_t frame_instr) {
	if (!nev && !cur_level) return;
	unsigned e = 0;
	int32_t prev = cur_level;
	for (unsigned i = 0; i < n; i++) {
		uint32_t t = (uint32_t)(((uint64_t)(2 * i + 1) * frame_instr) / (2u * n));
		while (e < nev && ev[e].pos <= t) cur_level = ev[e++].val;
		int32_t v = (prev + cur_level) * SB_GAIN / 2;          /* two-tap smoothing */
		prev = cur_level;
		int32_t o = dst[i] + v;
		if (o > 32767) o = 32767; else if (o < -32768) o = -32768;
		dst[i] = (int16_t)o;
	}
	while (e < nev) cur_level = ev[e++].val;
	nev = 0;
	if (!dma_on && !silence_left) cur_level = 0;              /* idle: no DC left behind */
}
