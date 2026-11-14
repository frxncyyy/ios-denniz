/* The camera flash's policy (rwcam_flash.h). */
#include <stddef.h>
#include "rwcam_flash.h"

static float
rate_of(const struct rwf_frame *fr)
{
	double e = (double)fr->exp_ns * 1e-9 * (fr->gain > 0 ? fr->gain : 1);

	return e > 0 ? (float)(fr->level / e) : 0;
}

static float
clampf(float v, float lo, float hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

void
rwf_flash_exposure(float amb_rate, float pre_rate, float target, uint64_t period_ns, float gain_max, uint64_t *exp_ns, float *gain)
{
	float flash_part = pre_rate - amb_rate;
	double rate, e, tmax;

	if (flash_part < 0) {
		flash_part = 0;                         /* (the pre-flash did not reach anything: the flash will not either) */
	}
	rate = (double)amb_rate + (double)RWF_RATIO * flash_part;
	if (rate <= 1e-6) {
		rate = 1e-6;
	}
	e = (double)target / rate;                      /* seconds x gain */
	tmax = (double)RWF_EXP_MAX_NS * 1e-9;
	if (period_ns != 0 && (double)period_ns * 1e-9 * 0.8 < tmax) {
		tmax = (double)period_ns * 1e-9 * 0.8;
	}
	if (gain_max < 1) {
		gain_max = 1;
	}
	if (e <= tmax) {
		*exp_ns = (uint64_t)(e * 1e9);
		*gain = 1;
	} else {
		*exp_ns = (uint64_t)(tmax * 1e9);
		*gain = clampf((float)(e / tmax), 1, gain_max);
	}
	if (*exp_ns < 100000) {
		*exp_ns = 100000;                       /* 0.1 ms: the sensor's shortest here */
	}
}

uint32_t
rwf_strobe_ms(uint64_t period_ns, uint64_t readout_ns, uint64_t exp_ns)
{
	/* one whole frame (readout + exposure) inside the strobe wherever the frames fall: two frame lengths + the window + LED latency */
	uint64_t ns = 2 * period_ns + readout_ns + exp_ns + RWF_LED_LATENCY_NS;
	uint32_t ms = (uint32_t)((ns + 999999) / 1000000);

	return ms < RWF_STROBE_MS_MIN ? RWF_STROBE_MS_MIN : ms > RWF_STROBE_MS_MAX ? RWF_STROBE_MS_MAX : ms;
}

float
rwf_lit_fraction(const struct rwf_frame *fr, uint64_t on_ns, uint64_t off_ns)
{
	uint64_t win = fr->readout_ns + fr->exp_ns;
	uint64_t a = fr->end_ns > win ? fr->end_ns - win : 0, b = fr->end_ns, lo, hi;

	if (win == 0 || off_ns <= on_ns) {
		return 0;
	}
	lo = a > on_ns ? a : on_ns;
	hi = b < off_ns ? b : off_ns;
	return hi > lo ? (float)((double)(hi - lo) / (double)win) : 0;
}

static void
act_clear(struct rwf_act *a)
{
	a->torch = -1;
	a->strobe = false;
	a->strobe_level = a->strobe_ms = 0;
	a->expose = false;
	a->exp_ns = 0;
	a->gain = 0;
	a->hold_ae = false;
	a->restore = false;
	a->pre_done = false;
	a->fired = false;
	a->done = false;
	a->read_led = false;
	a->kind = RWF_FRAME_NONE;
}

void
rwf_start(struct rwf *f, const struct rwf_frame *now, float target, bool want_ambient)
{
	struct rwf z = { 0 };

	*f = z;
	f->state = RWF_PRE;
	f->want_ambient = want_ambient;
	f->start_ns = now->end_ns;
	f->amb_rate = rate_of(now);
	f->amb_level = now->level;
	f->amb_exp_ns = now->exp_ns;
	f->amb_gain = now->gain;
	f->target = target > 0.02f ? target : 0.18f;
	f->pre_req_ns = 0;                              /* the torch request goes out with the first rwf_frame */
}

static void
finish(struct rwf *f, struct rwf_act *act)
{
	act->torch = 0;
	act->restore = true;
	act->done = true;
	f->state = RWF_DONE;
}

void
rwf_frame(struct rwf *f, const struct rwf_frame *fr, const struct rwf_led *led, struct rwf_act *act)
{
	act_clear(act);
	if (f->state == RWF_IDLE || f->state == RWF_DONE) {
		return;
	}
	f->frames++;
	if (fr->end_ns > f->start_ns + RWF_TIMEOUT_NS) {
		f->timed_out = true;
		finish(f, act);
		return;
	}
	switch (f->state) {
	case RWF_PRE:
		if (f->pre_req_ns == 0) {
			f->pre_req_ns = fr->end_ns;             /* (the request goes out now: this frame was before it) */
			act->torch = (int)RWF_PRE_LEVEL;
			act->read_led = true;
			break;
		}
		{
			/* the pre-flash's light from when the kernel says the LED went on (else the request + latency) */
			uint64_t on = led != NULL && led->valid && led->state == 1 && led->on_ns >= f->pre_req_ns - RWF_LED_LATENCY_NS ? led->on_ns :
			    f->pre_req_ns + RWF_LED_LATENCY_NS;

			act->read_led = true;
			if (rwf_lit_fraction(fr, on, UINT64_MAX) >= 0.999f) {
				f->pre_lit++;
			}
			/* settled: AE has had RWF_PRE_MIN_LIT lit frames and the level is near the target; or enough of them */
			if ((f->pre_lit >= RWF_PRE_MIN_LIT && fr->level > f->target * 0.7f && fr->level < f->target * 1.4f) || f->pre_lit >= RWF_PRE_MAX_LIT) {
				f->pre_rate = rate_of(fr);
				f->pre_level = fr->level;
				act->torch = 0;
				act->pre_done = true;
				rwf_flash_exposure(f->amb_rate, f->pre_rate, f->target, fr->period_ns, fr->gain_max, &f->f_exp_ns, &f->f_gain);
				f->strobe_ms = rwf_strobe_ms(fr->period_ns, fr->readout_ns, f->f_exp_ns);
				act->expose = true;
				act->exp_ns = f->f_exp_ns;
				act->gain = f->f_gain;
				act->hold_ae = true;
				f->state = RWF_SETTLE;
				f->settle = 0;
			}
		}
		break;
	case RWF_SETTLE:
		act->hold_ae = true;
		f->settle++;
		{
			double de = fr->exp_ns > f->f_exp_ns ? (double)(fr->exp_ns - f->f_exp_ns) : (double)(f->f_exp_ns - fr->exp_ns);
			bool took = de <= 0.05 * (double)f->f_exp_ns + 50000 && fr->gain > f->f_gain * 0.94f && fr->gain < f->f_gain * 1.06f;

			if (took && f->want_ambient && !f->ambient_given) {
				/* the same exposure, no flash: the photograph's ambient frame (the pre-flash is off since a frame or more) */
				act->kind = RWF_FRAME_AMBIENT;
				f->ambient_given = true;
				break;
			}
			if (took || f->settle >= RWF_SETTLE_FRAMES) {
				act->strobe = true;
				act->strobe_level = RWF_STROBE_LEVEL;
				act->strobe_ms = f->strobe_ms;
				act->fired = true;
				act->read_led = true;
				f->fire_req_ns = fr->end_ns;
				f->led_seq0 = led != NULL && led->valid ? led->lit_seq : 0;
				f->state = RWF_FIRED;
			}
		}
		break;
	case RWF_FIRED: {
		uint64_t on, off;

		act->hold_ae = true;
		act->read_led = true;
		if (led != NULL && led->valid && led->lit_seq != f->led_seq0 && led->state == 2 && led->on_ns >= f->fire_req_ns) {
			on = led->on_ns;
			off = led->off_ns != 0 ? led->off_ns : on + (uint64_t)f->strobe_ms * 1000000;
		} else if (led != NULL && led->valid && led->lit_seq != f->led_seq0 && led->state == 0 && led->off_ns > led->on_ns &&
		    led->on_ns >= f->fire_req_ns) {
			on = led->on_ns;                        /* (already over) */
			off = led->off_ns;
		} else {
			f->no_strobe_status = true;
			on = f->fire_req_ns + RWF_LED_LATENCY_NS;
			off = on + (uint64_t)f->strobe_ms * 1000000;
		}
		f->on_ns = on;
		f->off_ns = off;
		{
			/* a little margin at both ends (the LED's rise, the kernel's stamp taken after the enabling write) */
			float frac = rwf_lit_fraction(fr, on + 2000000, off > 2000000 ? off - 2000000 : off);

			if (frac >= RWF_LIT_ENOUGH && f->flash_frames < RWF_MAX_FLASH_FRAMES) {
				act->kind = RWF_FRAME_FLASH;
				f->flash_frames++;
				f->flash_level = fr->level;
			} else if (frac > f->best_frac) {
				f->best_frac = frac;
				f->best_seq = fr->seq;
			}
		}
		/* over: the strobe has ended and a frame after it was seen, or the flash frames are all given */
		if (f->flash_frames >= RWF_MAX_FLASH_FRAMES || (fr->end_ns > off + fr->period_ns)) {
			if (f->flash_frames == 0) {
				/* none (nearly) wholly inside the strobe -- the LED's times were wrong: this frame all the same, so that the photograph
				 * completes (darker than meant: logged as partial) */
				f->partial = true;
				act->kind = RWF_FRAME_FLASH;
				f->flash_frames++;
				f->flash_level = fr->level;
			}
			finish(f, act);
		}
		break;
	}
	default:
		break;
	}
}
