/*
 * t_flash.c (camflash-20261114): rwcam_flash.c against a simulated rolling-shutter sensor (exposure taking effect 2 frames after it is
 * asked, a simple AE while the sequence lets it) and a simulated MT6360 LED behind kernel 0560 (torch / strobe on 5..15 ms after the
 * request -- the owner thread's 10 ms loop -- off up to 10 ms late, the on / off times read back, or not at all: an old kernel).
 * Checks, over 4000 random scenes (dim to bright, near and far subjects, 30 and 60 frames a second, LED brighter or dimmer than the model):
 *   the sequence ends (< 2 s), in order pre-flash -> PreFlashComplete -> exposure -> strobe -> FlashFired -> flash frame(s) -> restore;
 *   every frame named FLASH is >= RWF_LIT_ENOUGH inside the real strobe when the kernel gives the times; the ambient frame (when asked)
 *   comes before the strobe with the flash exposure and no LED; the strobe is 60..250 ms; the torch is off at the end; AE is held from
 *   PreFlashComplete to the end; the flash frame's level is within 0.5x..2x of the target when the LED model is right and the flash
 *   dominates (and within the sensor's range otherwise).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rwcam_flash.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 40) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static double
frand(double a, double b)
{
	return a + (b - a) * (double)rand() / RAND_MAX;
}

struct led_sim {
	int state;                      /* 0 dark, 1 torch, 2 strobe (truth, now) */
	uint64_t on, off;               /* truth: last on / off */
	uint32_t lit_seq;
	bool pending_torch, pending_strobe, pending_off;
	uint64_t at;                    /* when the pending change happens */
	uint32_t ms;
	uint64_t off_late;              /* the strobe's end: ms + the owner tick */
	struct { uint64_t a, b; int kind; } iv[16];     /* history of lit intervals (b 0: still lit) */
	int n;
};

static void
iv_close(struct led_sim *l, uint64_t t)
{
	if (l->n > 0 && l->iv[l->n - 1].b == 0) { l->iv[l->n - 1].b = t; }
}

static void
led_advance(struct led_sim *l, uint64_t now)
{
	if ((l->pending_torch || l->pending_strobe || l->pending_off) && now >= l->at) {
		iv_close(l, l->at);
		if (l->pending_off) {
			if (l->state != 0) { l->off = l->at; }
			l->state = 0;
		} else {
			l->state = l->pending_strobe ? 2 : 1;
			l->on = l->at;
			l->off = 0;
			l->lit_seq++;
			if (l->state == 2) { l->off_late = l->at + (uint64_t)l->ms * 1000000 + (uint64_t)(rand() % 10) * 1000000; }
			if (l->n < 16) { l->iv[l->n].a = l->at; l->iv[l->n].b = 0; l->iv[l->n].kind = l->state; l->n++; }
		}
		l->pending_torch = l->pending_strobe = l->pending_off = false;
	}
	if (l->state == 2 && now >= l->off_late) {
		l->state = 0;
		l->off = l->off_late;
		iv_close(l, l->off_late);
	}
}

/* light (level per s x gain) reaching the sensor from the LED at time t */
static double
led_light(const struct led_sim *l, uint64_t t, double pre, double strobe)
{
	for (int i = 0; i < l->n; i++) {
		if (t >= l->iv[i].a && (l->iv[i].b == 0 || t < l->iv[i].b)) { return l->iv[i].kind == 2 ? strobe : pre; }
	}
	return 0;
}

static int
run(unsigned seed, bool verbose)
{
	srand(seed);
	double period = rand() % 3 == 0 ? 16.7e6 : 33.3e6;
	double readout = period * frand(0.6, 0.95);
	double amb = pow(10, frand(-0.5, 2.5));                 /* ambient: 0.3 (dark) .. 300 (daylight) level per s x gain */
	double pre = pow(10, frand(-1, 1.5));                   /* the pre-flash's light on the subject (far .. near) */
	double model_err = rand() % 2 ? 1.0 : frand(0.6, 1.6);  /* the LED's real strobe / pre-flash ratio over RWF_RATIO */
	double strobe = pre * RWF_RATIO * model_err;
	bool status_ok = rand() % 5 != 0;
	bool want_amb = rand() % 3 == 0;
	double gain_max = 16, target = 0.15;
	/* sensor state: exposure asked / in effect (2 frames later) */
	double exp_s = fmin(0.033, target / amb), gain = 1;
	if (exp_s * amb < target) { gain = fmin(gain_max, target / (exp_s * amb)); }
	double q_exp[3] = { exp_s, exp_s, exp_s }, q_gain[3] = { gain, gain, gain };
	struct led_sim led = { 0 };
	struct rwf f;
	struct rwf_act act;
	uint64_t t = 1000000000ull;
	bool started = false, pre_done = false, fired = false, done = false, held = false;
	uint64_t fire_t = 0;
	unsigned flash_frames = 0, amb_frames = 0, frames = 0;
	int torch_now = 0;
	double flash_level = 0;
	bool light_dominant = strobe > 2 * amb;

	for (uint64_t seq = 1; seq < 400 && !done; seq++) {
		t += (uint64_t)period;
		led_advance(&led, t);
		/* this frame: exposure in effect = asked 2 frames ago */
		double e = q_exp[0], g = q_gain[0];
		struct rwf_frame fr = { seq, t, (uint64_t)(e * 1e9), (uint64_t)readout, (uint64_t)period, (float)g, (float)gain_max, 0 };
		/* level: ambient + LED light integrated over the frame's window (sampled), clipped */
		double sum = 0;
		uint64_t win0 = t - (uint64_t)readout - (uint64_t)(e * 1e9);
		for (int k = 0; k < 64; k++) {
			uint64_t line_end = win0 + (uint64_t)(e * 1e9) + (uint64_t)(readout * k / 63.0);
			double ll = 0;
			for (int j = 0; j < 8; j++) { ll += led_light(&led, line_end - (uint64_t)(e * 1e9 * j / 7.0), pre, strobe); }
			sum += (amb + ll / 8) * e * g;
		}
		fr.level = (float)fmin(1.0, sum / 64);
		if (!started && seq == 10) {
			rwf_start(&f, &fr, (float)target, want_amb);
			started = true;
			continue;
		}
		if (!started) { goto ae; }
		{
			struct rwf_led ls = { status_ok, (uint32_t)led.state, led.on, led.state == 0 ? led.off : 0, led.lit_seq };
			rwf_frame(&f, &fr, &ls, &act);
		}
		frames++;
		if (act.kind == RWF_FRAME_FLASH) {
			flash_frames++;
			flash_level = fr.level;
			CHECK(fired, "seed %u: a flash frame before the strobe", seed);
			if (status_ok && !f.partial) {
				/* truth: the frame's window inside the real strobe */
				uint64_t off = led.off != 0 && led.off > led.on ? led.off : led.on + (uint64_t)led.ms * 1000000;
				float frac = rwf_lit_fraction(&fr, led.on, off);
				CHECK(frac >= RWF_LIT_ENOUGH - 0.001f, "seed %u: flash frame %llu only %.2f inside the strobe", seed, (unsigned long long)seq, frac);
			}
		}
		if (act.kind == RWF_FRAME_AMBIENT) {
			amb_frames++;
			CHECK(pre_done && !fired && led.state == 0, "seed %u: ambient frame out of order / LED lit (%d)", seed, led.state);
			CHECK(fabs(e - (double)f.f_exp_ns / 1e9) < 0.06 * e + 1e-4, "seed %u: ambient frame not at the flash exposure", seed);
		}
		if (act.pre_done) { CHECK(!pre_done && torch_now > 0, "seed %u: pre_done twice / without pre-flash", seed); pre_done = true; }
		if (pre_done && !act.done) { CHECK(act.hold_ae, "seed %u: AE not held after pre-flash", seed); held = true; }
		if (act.torch >= 0) {
			torch_now = act.torch;
			if (act.torch > 0) { led.pending_torch = true; } else if (led.state == 1 || led.pending_torch) { led.pending_off = true; led.pending_torch = false; }
			led.at = t + (uint64_t)(5 + rand() % 11) * 1000000;
		}
		if (act.expose) {
			CHECK(pre_done, "seed %u: exposure before the pre-flash ended", seed);
			q_exp[2] = (double)act.exp_ns / 1e9; q_gain[2] = act.gain;
			CHECK(act.exp_ns <= RWF_EXP_MAX_NS && act.gain >= 1 && act.gain <= gain_max, "seed %u: flash exposure %llu x %.2f out of range", seed,
			    (unsigned long long)act.exp_ns, (double)act.gain);
		}
		if (act.strobe) {
			CHECK(pre_done && !fired && act.strobe_ms >= RWF_STROBE_MS_MIN && act.strobe_ms <= RWF_STROBE_MS_MAX, "seed %u: strobe %u ms", seed, act.strobe_ms);
			led.pending_strobe = true; led.pending_torch = false; led.pending_off = false;
			led.ms = act.strobe_ms;
			led.at = t + (uint64_t)(5 + rand() % 11) * 1000000;
			fire_t = t;
		}
		if (act.fired) { CHECK(act.strobe, "seed %u: fired without strobe", seed); fired = true; }
		if (act.done) {
			done = true;
			CHECK(act.torch == 0 && act.restore, "seed %u: not restored / torch left", seed);
			CHECK(f.timed_out || (pre_done && fired && flash_frames >= 1), "seed %u: done without a flash frame (pre %d fired %d frames %u)", seed,
			    pre_done, fired, flash_frames);
			CHECK(!want_amb || amb_frames == 1 || f.timed_out, "seed %u: ambient frames %u", seed, amb_frames);
			CHECK(!f.timed_out, "seed %u: timed out", seed);
			CHECK((t - f.start_ns) < 2000000000ull, "seed %u: took %.0f ms", seed, (double)(t - f.start_ns) / 1e6);
			if (status_ok && model_err == 1.0 && light_dominant && !f.partial && act.gain < gain_max - 0.01f) {
				CHECK(flash_level > target * 0.5 && flash_level < target * 2.0, "seed %u: flash level %.3f (target %.2f, amb %.2f pre %.2f)", seed,
				    flash_level, target, amb, pre);
			}
			CHECK(!status_ok || !f.partial, "seed %u: partial with the kernel's times", seed);
		}
ae:
		/* the sensor: the queue moves on; AE (unless held) asks for the exposure that puts this frame's level on target */
		q_exp[0] = q_exp[1]; q_gain[0] = q_gain[1]; q_exp[1] = q_exp[2]; q_gain[1] = q_gain[2];
		if (!(started && (held || (pre_done && !done))) && fr.level > 0.001) {
			double rate = fr.level / (e * g), want = target / rate;
			double ne = fmin(0.033, want), ng = fmax(1, fmin(gain_max, want / ne));
			if (fr.level >= 0.999) { ne = e * 0.4; ng = 1; }
			q_exp[2] = ne; q_gain[2] = ng;
		}
		(void)fire_t;
	}
	CHECK(done, "seed %u: never done (pre %d fired %d)", seed, pre_done, fired);
	if (verbose) {
		printf("seed %u: %s fps, amb %.2f pre %.2f err %.2f status %d: pre_lit %u, flash exp %.1f ms x %.2f strobe %u ms, flash level %.3f, frames %u%s\n",
		    seed, period < 20e6 ? "60" : "30", amb, pre, model_err, status_ok, f.pre_lit, (double)f.f_exp_ns / 1e6, (double)f.f_gain, f.strobe_ms,
		    flash_level, frames, f.partial ? " PARTIAL" : "");
	}
	return 0;
}

int
main(int argc, char **argv)
{
	unsigned n = argc > 1 ? (unsigned)atoi(argv[1]) : 4000;

	/* pure functions */
	{
		uint64_t e; float g;
		rwf_flash_exposure(10, 10, 0.15f, 33300000, 16, &e, &g);            /* no flash light: ambient only: 15 ms x 1 */
		CHECK(e > 14000000 && e < 16000000 && g == 1.0f, "ambient only %llu %.2f", (unsigned long long)e, (double)g);
		rwf_flash_exposure(0.5f, 10.5f, 0.15f, 33300000, 16, &e, &g);       /* flash 4 x 10 + 0.5: 3.7 ms */
		CHECK(e > 3500000 && e < 3800000 && g == 1.0f, "flash %llu", (unsigned long long)e);
		rwf_flash_exposure(0.01f, 0.02f, 0.15f, 33300000, 16, &e, &g);      /* far and dark: time capped, gain capped */
		CHECK(e <= RWF_EXP_MAX_NS && g == 16.0f, "dark %llu %.2f", (unsigned long long)e, (double)g);
		CHECK(rwf_strobe_ms(33300000, 30000000, 10000000) == 122, "strobe ms %u", rwf_strobe_ms(33300000, 30000000, 10000000));
		CHECK(rwf_strobe_ms(16700000, 15000000, 5000000) == 69, "strobe ms 60 %u", rwf_strobe_ms(16700000, 15000000, 5000000));
		struct rwf_frame fr = { 1, 200000000, 10000000, 30000000, 33300000, 1, 16, 0.1f };
		CHECK(rwf_lit_fraction(&fr, 150000000, 250000000) > 0.999f, "lit");
		CHECK(rwf_lit_fraction(&fr, 170000000, 250000000) < 0.8f, "half");
	}
	for (unsigned i = 1; i <= n; i++) {
		run(i, i <= 6);
	}
	if (fails) {
		printf("T_FLASH: %d FAILED\n", fails);
		return 1;
	}
	printf("T_FLASH OK (%u scenes)\n", n);
	return 0;
}
