/*
  spk_hle: PC speaker for the gw-dos (XTulator) Game & Watch port. GPLv2 or later.

  XTulator's own speaker steps PIT channel 2 and a slew-limited "amplitude" from two 48 kHz
  timers. On this port the timers run in bursts (one burst per block of instructions, see
  run_frame), so both were effectively sampled at a few kHz: every tone came out aliased
  into a buzz with the wrong pitch, high notes were swallowed by the slew limit, and some
  were silent. Measured on a 440 Hz test tone: under half of the energy at its harmonics.

  This replaces it with a model computed from the state the guest programs, timed to the
  instruction:
    - port 61h bit 0 (timer 2 gate) and bit 1 (speaker data), as on the real board the
      speaker is (OUT2 AND data);
    - PIT channel 2 mode and count, loaded through port 42h/43h. Mode 3 is a square wave of
      period = count PIT clocks (1.193182 MHz), high for (count+1)/2; mode 0/1 are the
      one-shots that digitised-sound players use for PWM; with the gate low OUT2 is high.
  Every change is stamped with the instruction it happened at, and each 48 kHz output
  sample is the exact AVERAGE of the speaker level over its interval (a box filter), so a
  square wave is band-limited enough not to alias into other notes, and a hand-toggled
  bit 1 (direct drive) comes out at the right duty cycle.

  Frame protocol (dos_main.c run_frame): spk_hle_frame_begin() with the frame's length in
  instructions and the output buffer, spk_hle_chunk() before each cpu_exec() block, and
  spk_hle_frame_end() to finish the buffer. Guest port writes reach spk_hle_port61() and
  spk_hle_pit2*() from the patched i8255.c / i8253.c (static_mem.py).
*/
#include <stdint.h>
#include <math.h>

#define PIT_HZ 1193182.0
#define SPK_AMP 12000.0

static int16_t* out;
static int nout, iout;
static double frame_len;                 /* instructions in this frame */
static double pos;                       /* instructions of this frame rendered */
static double acc;                       /* level x instructions in the current sample */
static double kclk = 1.0;                /* PIT clocks per instruction */
static uint64_t frame_abs;               /* instructions before this frame */
static const uint64_t* exec_ctr;         /* the CPU's executed-instruction counter */
static uint64_t chunk_exec0;
static uint32_t chunk_done, chunk_len;

/* speaker state */
static uint8_t data_bit, gate_bit, mode = 3, loaded;
static double count = 65536.0;           /* PIT clocks */
static double t0;                        /* absolute instruction where counting started */

/* output filtering */
static double dc, lp;

static double now_rel(void) {
	if (!exec_ctr) return pos;
	uint64_t d = *exec_ctr - chunk_exec0;
	if (d > chunk_len) d = chunk_len;         /* HLT does not count instructions */
	return (double)chunk_done + (double)d;
}

/* time at which the square wave has been high, from its start to c PIT clocks */
static double high_upto(double c) {
	if (c <= 0) return 0;
	double hp = floor((count + 1.0) / 2.0);
	double n = floor(c / count);
	double r = c - n * count;
	return n * hp + (r < hp ? r : hp);
}

/* integral of the speaker level (0..1) over [a, b) in absolute instructions */
static double level_integral(double a, double b) {
	if (b <= a || !data_bit) return 0;
	if (!loaded) return (mode == 0 || mode == 1) ? 0 : (b - a);
	switch (mode) {
	case 3:
		if (!gate_bit || count < 2) return b - a;
		return (high_upto((b - t0) * kclk) - high_upto((a - t0) * kclk)) / kclk;
	case 0:
	case 1: {                                  /* low for `count` clocks, then high */
		double up = t0 + count / kclk;
		if (mode == 1 && !gate_bit) return b - a;
		if (b <= up) return 0;
		return b - (a > up ? a : up);
	}
	default:                                   /* rate generator: high but for one clock */
		return b - a;
	}
}

static void emit(double v) {
	double s = v * SPK_AMP;
	dc += (s - dc) * (1.0 / 512.0);            /* remove DC */
	s -= dc;
	lp += (s - lp) * 0.6;                      /* gentle low-pass, ~7 kHz */
	int32_t q = (int32_t)lp;
	if (q > 32767) q = 32767; else if (q < -32768) q = -32768;
	if (iout < nout) out[iout] = (int16_t)q;
	iout++;
}

static void advance_to(double t) {
	if (!out) return;
	if (t > frame_len) t = frame_len;
	while (pos < t && iout < nout) {
		double b = frame_len * (iout + 1) / nout;   /* end of the current sample */
		double e = t < b ? t : b;
		acc += level_integral(frame_abs + pos, frame_abs + e);
		pos = e;
		if (pos >= b) {
			double span = frame_len / nout;
			emit(acc / span);
			acc = 0;
		}
	}
}

void spk_hle_frame_begin(const uint64_t* exec, uint32_t instructions, uint32_t ips, int16_t* buf, int n) {
	exec_ctr = exec;
	frame_len = instructions;
	kclk = PIT_HZ / (double)(ips ? ips : 1);
	out = buf; nout = n; iout = 0; pos = 0; acc = 0;
	chunk_done = 0; chunk_len = 0; chunk_exec0 = exec ? *exec : 0;
}

void spk_hle_chunk(uint32_t done, uint32_t len) {
	chunk_done = done; chunk_len = len;
	chunk_exec0 = exec_ctr ? *exec_ctr : 0;
}

void spk_hle_frame_end(void) {
	advance_to(frame_len);
	while (iout < nout) emit(acc ? acc / (frame_len / nout) : 0);
	frame_abs += (uint64_t)frame_len;
	out = 0;
}

void spk_hle_port61(uint8_t v) {
	double t = now_rel();
	advance_to(t);
	uint8_t g = v & 1;
	if (g && !gate_bit) t0 = frame_abs + t;   /* gate rising: the counter restarts / triggers */
	gate_bit = g;
	data_bit = (v >> 1) & 1;
}

void spk_hle_pit2(uint8_t m, int32_t reload) {
	double t = now_rel();
	advance_to(t);
	mode = m;
	count = reload > 0 ? (double)reload : 65536.0;
	loaded = 1;
	t0 = frame_abs + t;
}

void spk_hle_pit2_mode(uint8_t m) {
	double t = now_rel();
	advance_to(t);
	mode = m;
	loaded = 0;
}
