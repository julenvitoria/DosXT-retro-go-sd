/*
  adlib_hle: AdLib (YM3812 / OPL2) for the gw-dos (XTulator) Game & Watch port. GPLv2 or later.

  The synthesis is emu8950 (emu8950/, MIT, Mitsutaka Okazaki and Graham Sanderson), the
  same build that plays OPL music on this console in the owner's G&W Doom port and in the
  TinyEMU core (x86_opl.c there; this adapter follows it). XTulator's own Nuked-OPL3 is not
  used: emu8950 is 2.4x faster per sample (measured on the PC, same patch) and is an OPL2,
  which is what an AdLib is.

  Ports 388h/389h (index / data, status on read). The two OPL timers are kept HERE, not in
  emu8950 (built EMU8950_NO_TIMER), because they are what every AdLib detection routine
  checks: start timer 1, wait ~80 us, expect status C0h. Time is the emulated CPU's,
  counted in instructions, plus 1 us for every access to the card -- an ISA cycle is about
  a microsecond whatever the CPU speed, and detection loops time themselves by reading the
  status port; charged at the instruction rate alone a fast CPU would never see the timer
  run out (the TinyEMU core measured exactly that).

  Percussion mode (register BDh bit 5) is not synthesised (EMU8950_NO_PERCUSSION_MODE, the
  Doom port's build); adlib_hle_rhythm counts the games that ask for it.

  The chip is rendered up to each register write as it happens (see "Rendering" below) and
  the frame is mixed into the audio at its end. Nothing is synthesised while every operator
  is silent, so a game without AdLib music costs nothing.
*/
#include <stdint.h>
#include <string.h>
#include "emu8950/emu8950.h"

#define OPL_CLOCK_HZ 49716u                /* 14.31818 MHz / 288 */
#define OPL_EG_MAX 0x1FF                   /* envelope fully attenuated: the slot is silent */

void ports_cbRegister(uint32_t start, uint32_t count, uint8_t (*readb)(void*, uint32_t), uint16_t (*readw)(void*, uint32_t),
                      void (*writeb)(void*, uint32_t, uint8_t), void (*writew)(void*, uint32_t, uint16_t), void* udata);

uint32_t adlib_hle_writes, adlib_hle_keyons, adlib_hle_rhythm;

static OPL* opl;
static uint8_t index_latch, rhythm_on;

/* ---- the timers, on the emulated CPU's clock ---- */
static const uint64_t* exec_ctr;
static const uint32_t* ips_ptr;
static uint64_t last_exec;
static double now_us_acc;                  /* microseconds of emulated time, monotonic */
static uint8_t t_preset[2], t_ctrl, t_flags;
static double t_expire[2];

static double now_us(void) {
	if (exec_ctr && ips_ptr && *ips_ptr) {
		uint64_t e = *exec_ctr;
		now_us_acc += (double)(e - last_exec) * 1e6 / (double)*ips_ptr;
		last_exec = e;
	}
	return now_us_acc;
}

static void timers_update(void) {
	static const double unit[2] = { 80.0, 320.0 };
	double t = now_us();
	for (int i = 0; i < 2; i++) {
		if (!(t_ctrl & (1 << i))) continue;            /* not running */
		double period = (256 - t_preset[i]) * unit[i];
		while (t >= t_expire[i]) {
			if (!(t_ctrl & (0x40 >> i))) t_flags |= 0x40 >> i;   /* unless masked: T1 -> bit 6, T2 -> bit 5 */
			t_expire[i] += period;
		}
	}
}

static uint8_t opl_status(void) {
	timers_update();
	uint8_t s = t_flags & 0x60;
	if (s) s |= 0x80;
	return (uint8_t)(s | 0x06);                         /* low bits as a YM3812 reports them */
}

static void opl_reset_chip(void) {
	if (!opl) opl = OPL_new(OPL_CLOCK_HZ * 72u, OPL_CLOCK_HZ);
	if (opl) OPL_reset(opl);
	rhythm_on = 0;
	t_ctrl = t_flags = 0;
}

static void render_to_now(void);

static void opl_write_reg(uint8_t reg, uint8_t val) {
	adlib_hle_writes++;
	switch (reg) {
	case 0x02: t_preset[0] = val; return;
	case 0x03: t_preset[1] = val; return;
	case 0x04:
		if (val & 0x80) { t_flags = 0; return; }        /* reset the flags; the rest ignored */
		timers_update();
		for (int i = 0; i < 2; i++)
			if ((val & (1 << i)) && !(t_ctrl & (1 << i)))  /* starting: count from now */
				t_expire[i] = now_us_acc + (256 - t_preset[i]) * (i ? 320.0 : 80.0);
		t_ctrl = val;
		return;
	}
	if (!opl) return;
	render_to_now();          /* the chip plays its old state up to this instruction */
	if (reg == 0xBD) {
		uint8_t want = (val >> 5) & 1;
		if (want && !rhythm_on) {
			adlib_hle_rhythm++;
			for (int c = 6; c < 9; c++)                     /* the drum channels go quiet */
				OPL_writeReg(opl, 0xB0 + c, opl->reg[0xB0 + c] & 0xDF);
		}
		rhythm_on = want;
		/* emu8950 is built with EMU8950_NO_PERCUSSION_MODE and that build ASSERTS bit 5
		   is clear: Ironman with AdLib died on "FATAL EXCEPTION: Assert". Only the
		   AM/vibrato depth bits go through. */
		val &= 0xC0;
	}
	if (rhythm_on && reg >= 0xB6 && reg <= 0xB8)         /* drums: never key them on as notes */
		val &= 0xDF;
	if ((reg & 0xF0) == 0xB0 && (reg & 0x0F) < 9) {
		uint8_t was = (opl->reg[reg] >> 5) & 1;
		if (((val >> 5) & 1) && !was) adlib_hle_keyons++;
	}
	OPL_writeReg(opl, reg, val);
}

static uint8_t adlib_read(void* u, uint32_t port) {
	(void)u;
	now_us_acc += 1.0;                                  /* one ISA bus cycle */
	if ((port & 1) == 0) return opl_status();
	return 0xFF;                                        /* the data port is write-only */
}

static void adlib_write(void* u, uint32_t port, uint8_t val) {
	(void)u;
	now_us_acc += 1.0;
	if ((port & 1) == 0) index_latch = val;
	else opl_write_reg(index_latch, val);
}

/* the Sound Blaster answers the same chip at 228h/229h (sb_hle.c) */
uint8_t adlib_hle_port_read(uint32_t port) { return adlib_read(NULL, port); }
void adlib_hle_port_write(uint32_t port, uint8_t val) { adlib_write(NULL, port, val); }

void adlib_hle_init(const uint64_t* exec, const uint32_t* ips) {
	exec_ctr = exec; ips_ptr = ips;
	last_exec = exec ? *exec : 0;
	opl_reset_chip();
	ports_cbRegister(0x388, 2, adlib_read, NULL, adlib_write, NULL, NULL);
}

/* ---- rendering ---- */
#define OPL_CHUNK 256u
static int32_t chunk[OPL_CHUNK];
static unsigned chunk_len, chunk_pos;
static uint32_t frac;
static int16_t s0, s1;

static int opl_active(void) {
	if (!opl) return 0;
	if (opl->slot_key_status) return 1;
	for (unsigned i = 0; i < 18; i++)
		if (opl->slot[i].eg_out < OPL_EG_MAX) return 1;
	return 0;
}

/*
  Rendering is INCREMENTAL, like the speaker's: before every register write the chip is
  rendered up to the instruction the write happens at, so a note starts where the game
  started it and not at the next frame boundary. Rendering once per frame instead batched
  a music driver's ~70-140 Hz ticks onto the 60 Hz frame -- notes and volume steps landed
  up to 16 ms late and unevenly, which showed as a 60 Hz ripple on the music's envelope.
  The frame's samples go to opl_buf and are mixed in at the end of the frame.
*/
#define FRAME_MAX 1024
static int16_t opl_buf[FRAME_MAX];
static unsigned f_n, f_done, f_rate;       /* output samples in this frame, rendered so far */
static uint32_t f_len;                     /* instructions in this frame */
static uint32_t blk_done, blk_len;        /* the block of instructions being run */
static uint64_t blk_exec0;

#ifdef GWDOS_PROF
uint32_t adlib_prof_cyc;                   /* cycles spent synthesising (profiling builds) */
uint32_t plat_cycles(void);
#endif
static void render_samples_(unsigned upto);
static void render_samples(unsigned upto) {
#ifdef GWDOS_PROF
	uint32_t c0 = plat_cycles();
	render_samples_(upto);
	adlib_prof_cyc += plat_cycles() - c0;
#else
	render_samples_(upto);
#endif
}
static void render_samples_(unsigned upto) {
	if (upto > f_n) upto = f_n;
	if (upto <= f_done) return;
	if (!opl || !opl_active()) {
		memset(opl_buf + f_done, 0, (upto - f_done) * sizeof(int16_t));
		s0 = s1 = 0; f_done = upto; return;
	}
	uint32_t step = (uint32_t)(((uint64_t)OPL_CLOCK_HZ << 16) / f_rate);
	for (unsigned i = f_done; i < upto; i++) {
		frac += step;
		while (frac >= 0x10000u) {
			frac -= 0x10000u;
			s0 = s1;
			if (chunk_pos >= chunk_len) {
				OPL_calc_buffer_linear(opl, chunk, OPL_CHUNK);
				chunk_len = OPL_CHUNK; chunk_pos = 0;
			}
			int32_t s = chunk[chunk_pos++];
			if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
			s1 = (int16_t)s;
		}
		int32_t v = s0 + (((int32_t)(s1 - s0) * (int32_t)(frac >> 8)) >> 8);   /* linear, not a hold */
		v = v * 3 / 2;                                  /* music gain, with headroom left */
		if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
		opl_buf[i] = (int16_t)v;
	}
	f_done = upto;
}

static void render_to_now(void) {
	if (!f_n || !f_len) return;
	uint64_t d = exec_ctr ? *exec_ctr - blk_exec0 : 0;
	if (d > blk_len) d = blk_len;                  /* HLT does not count instructions */
	uint64_t pos = (uint64_t)blk_done + d;
	render_samples((unsigned)(pos * f_n / f_len));
}

void adlib_hle_frame_begin(unsigned n, unsigned rate, uint32_t frame_instr) {
	f_n = n > FRAME_MAX ? FRAME_MAX : n; f_rate = rate; f_len = frame_instr; f_done = 0;
	blk_done = blk_len = 0;
	blk_exec0 = exec_ctr ? *exec_ctr : 0;
}

void adlib_hle_chunk(uint32_t done, uint32_t len) {
	blk_done = done; blk_len = len;
	blk_exec0 = exec_ctr ? *exec_ctr : 0;
}

/* finish the frame and add it into dst (clamped) */
void adlib_hle_frame_end_mix(int16_t* dst) {
	render_samples(f_n);
	for (unsigned i = 0; i < f_n; i++) {
		int32_t o = (int32_t)dst[i] + opl_buf[i];
		if (o > 32767) o = 32767; else if (o < -32768) o = -32768;
		dst[i] = (int16_t)o;
	}
}
