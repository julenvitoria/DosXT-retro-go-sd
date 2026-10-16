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
#ifdef SPK_LOG                           /* host debugging only: every speaker event to spk.log */
#include <stdio.h>
static FILE* spk_log;
#define LOG(...) do { if (!spk_log) spk_log = fopen("spk.log", "w"); fprintf(spk_log, __VA_ARGS__); } while (0)
#else
#define LOG(...) do { } while (0)
#endif

#define PIT_HZ 1193182.0
#define SPK_AMP 12000.0
/* no libm in the core: floor() compiles to one VRINTM on the M7, fmod() would not link */
static double wrap(double c, double p) { return c - floor(c / p) * p; }

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
/* A count written while mode 3 is running takes effect at the end of the current half
   cycle, as on the 8253 -- it does NOT restart the wave. Monkey Island rewrites the same
   count on every timer tick (~400 times a second); restarting there chopped every note
   longer than ~2.5 ms into a buzz at the tick rate. */
static double pend_count, pend_t;
static uint8_t pend;

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

/* switch to the pending count at its half-cycle boundary, keeping the polarity */
static void apply_pending(void) {
	/* which edge is it: the end of a high half (r ~ hp) or of a low half (r ~ 0 / count)? */
	double c = (pend_t - t0) * kclk;
	double hp_old = floor((count + 1.0) / 2.0);
	double r = wrap(c, count);
	double d_hl = (r > hp_old ? r - hp_old : hp_old - r), d_lh = r < count - r ? r : count - r;
	count = pend_count;
	if (d_hl < d_lh) t0 = pend_t - floor((count + 1.0) / 2.0) / kclk;  /* continue low */
	else t0 = pend_t;                                                  /* continue high */
	pend = 0;
}

static void advance_to(double t) {
	if (!out) return;
	if (t > frame_len) t = frame_len;
	while (pos < t && iout < nout) {
		double b = frame_len * (iout + 1) / nout;   /* end of the current sample */
		double e = t < b ? t : b;
		double pr = pend ? pend_t - (double)frame_abs : 1e300;
		if (pend && pr <= pos) { apply_pending(); continue; }
		if (pr < e) e = pr;
		acc += level_integral(frame_abs + pos, frame_abs + e);
		pos = e;
		if (pend && pos >= pr) apply_pending();
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
	/* SILENT FAST PATH: with the speaker data bit off the level is 0 whatever the PIT does,
	   and once the DC and low-pass filters have settled every sample is 0. Monkey Island
	   (AdLib music) spent ~18 % of a host profile here averaging and filtering 800 zeros a
	   frame in double precision. */
	if (!data_bit && pos == 0 && dc > -0.5 && dc < 0.5 && lp > -0.5 && lp < 0.5) {
		for (int i = 0; i < nout; i++) out[i] = 0;
		dc = lp = 0; acc = 0;
		if (pend && pend_t <= frame_abs + frame_len) apply_pending();
		frame_abs += (uint64_t)frame_len;
		out = 0;
		return;
	}
	advance_to(frame_len);
	while (iout < nout) emit(acc ? acc / (frame_len / nout) : 0);
	frame_abs += (uint64_t)frame_len;
	out = 0;
}

void spk_hle_port61(uint8_t v) {
	double t = now_rel();
	LOG("%.0f P61 %02x\n", frame_abs + t, v);
	advance_to(t);
	uint8_t g = v & 1;
	if (g && !gate_bit) {                      /* gate rising: the counter restarts / triggers */
		t0 = frame_abs + t;
		if (pend) { count = pend_count; pend = 0; }
	}
	gate_bit = g;
	data_bit = (v >> 1) & 1;
}

void spk_hle_pit2(uint8_t m, int32_t reload) {
	double t = now_rel();
	LOG("%.0f LOAD m%u %d\n", frame_abs + t, m, (int)reload);
	advance_to(t);
	double c = reload > 0 ? (double)reload : 65536.0;
	if (loaded && m == 3 && mode == 3 && gate_bit && count >= 2 && c >= 2) {
		/* running square wave: the new count starts at the next half-cycle boundary */
		if (c == count && !pend) return;
		double now = frame_abs + t;
		double ph = (now - t0) * kclk;
		double hp = floor((count + 1.0) / 2.0);
		double r = wrap(ph, count), nb;
		nb = (r < hp) ? ph - r + hp : ph - r + count;   /* clocks to the next edge */
		pend_count = c; pend_t = t0 + nb / kclk; pend = 1;
		return;
	}
	mode = m;
	count = c;
	loaded = 1;
	pend = 0;
	t0 = frame_abs + t;
}

void spk_hle_pit2_mode(uint8_t m) {
	double t = now_rel();
	LOG("%.0f MODE m%u\n", frame_abs + t, m);
	advance_to(t);
	mode = m;
	loaded = 0;
	pend = 0;
}
