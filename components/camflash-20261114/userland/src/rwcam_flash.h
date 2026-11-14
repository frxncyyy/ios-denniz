/*
 * The camera flash (camflash-20261114, kernel 0560): a photograph with the flash, as an iPhone's ISP firmware does it for CMCapture --
 * pre-flash (the LED as a torch: the exposure settles on the lit scene, the eyes' pupils close a little: less red eye), PreFlashComplete,
 * the exposure for the flash worked out from the scene with and without the pre-flash, a STROBE of the MT6360 (/dev/redwood-torch 12-byte
 * request) long enough for a whole frame to be exposed inside it, FlashFired, the frame(s) whose whole exposure lies inside the strobe
 * (the kernel says when the LED went on and off: read() of /dev/redwood-torch) given as the photograph, then the exposure back to AE.
 *
 * This file is the policy only (no I/O, host-tested by test/t_flash.c); rwcam_core.c runs it on the reader thread, frame by frame.
 * Times in nanoseconds of the frames' clock (mach_absolute_time converted), levels = green above black over white (0..1) without digital
 * gain, exposure = seconds x analog gain ("ns x gain").
 */
#ifndef RWCAM_FLASH_H
#define RWCAM_FLASH_H
#include <stdint.h>
#include <stdbool.h>

#define RWF_PRE_LEVEL           1000u           /* /dev/redwood-torch torch level of the pre-flash (thousandths of kern.n2torch_maxsel: 100 mA x 2) */
#define RWF_STROBE_LEVEL        1000u           /* strobe level (thousandths of kern.n2torch_strobesel: 500 mA x 2) */
#define RWF_RATIO               4.0f            /* strobe light over pre-flash light (5x the current, LED efficacy falls with current) */
#define RWF_PRE_MIN_LIT         3u              /* frames wholly lit by the pre-flash before its level is taken ... */
#define RWF_PRE_MAX_LIT         10u             /* ... at most (AE settled or not) */
#define RWF_SETTLE_FRAMES       4u              /* frames for the sensor to take the flash exposure (else fire anyway) */
#define RWF_STROBE_MS_MIN       60u
#define RWF_STROBE_MS_MAX       250u            /* = kernel N2T_STROBE_MS_MAX */
#define RWF_LED_LATENCY_NS      15000000ull     /* when the kernel cannot say: the LED is on this long after the request (owner thread 10 ms) */
#define RWF_TIMEOUT_NS          3000000000ull   /* the whole sequence */
#define RWF_MAX_FLASH_FRAMES    2u
#define RWF_LIT_ENOUGH          0.92f           /* share of a frame's exposure window inside the strobe for it to be a flash frame */
#define RWF_EXP_MAX_NS          30000000ull     /* the flash frame's exposure time at most (the rest by gain): less blur */

enum rwf_state { RWF_IDLE = 0, RWF_PRE, RWF_SETTLE, RWF_FIRED, RWF_DONE };
enum rwf_kind { RWF_FRAME_NONE = 0, RWF_FRAME_AMBIENT = 1, RWF_FRAME_FLASH = 2 };

/* what the reader knows of a frame (its header) */
struct rwf_frame {
	uint64_t seq;
	uint64_t end_ns;                /* when it was done (its last line) */
	uint64_t exp_ns;                /* exposure time */
	uint64_t readout_ns;            /* first line to last line (frame length is an upper bound) */
	uint64_t period_ns;             /* frame length */
	float    gain;                  /* analog gain (x) */
	float    gain_max;
	float    level;                 /* green above black over white, no digital gain */
};

/* what the kernel says of the LED (struct n2t_status), converted to ns of the same clock; valid = the read worked */
struct rwf_led {
	bool     valid;
	uint32_t state;                 /* 0 dark, 1 torch mode, 2 strobe */
	uint64_t on_ns, off_ns;         /* off_ns 0: not since on */
	uint32_t lit_seq;
};

/* what the reader must do after a frame */
struct rwf_act {
	int      torch;                 /* -1 nothing, else a 4-byte torch request of this level (0 = off) */
	bool     strobe;                /* a 12-byte strobe request: strobe_level, strobe_ms */
	uint32_t strobe_level, strobe_ms;
	bool     expose;                /* give the sensor exp_ns x gain (and keep AE off) */
	uint64_t exp_ns;
	float    gain;
	bool     hold_ae;               /* AE must not change the exposure now */
	bool     restore;               /* the sequence is over: the exposure back to what it was (amb_exp_ns x amb_gain), AE on */
	bool     pre_done;              /* post PreFlashComplete */
	bool     fired;                 /* post FlashFired */
	bool     done;                  /* the sequence ended (ok or not): the photograph's request ends */
	bool     read_led;              /* read the LED's status before the next frame */
	enum rwf_kind kind;             /* this frame is the photograph's ambient frame / a flash frame */
};

struct rwf {
	enum rwf_state state;
	bool     want_ambient;
	uint64_t start_ns, pre_req_ns, fire_req_ns;
	uint32_t pre_lit, settle, flash_frames, frames;
	uint32_t led_seq0;              /* the LED's lit_seq before the strobe was asked for */
	bool     ambient_given;
	/* the scene: ambient (before the pre-flash) and with the pre-flash, as levels per (s x gain) */
	float    amb_rate, pre_rate, target;
	uint64_t amb_exp_ns;
	float    amb_gain;
	/* the flash exposure and the strobe */
	uint64_t f_exp_ns;
	float    f_gain;
	uint32_t strobe_ms;
	/* the best partly lit frame, if no frame is wholly inside the strobe */
	uint64_t best_seq;
	float    best_frac;
	/* results (for the log) */
	float    flash_level, pre_level, amb_level;
	bool     partial, timed_out, no_strobe_status;
	uint64_t on_ns, off_ns;
};

void rwf_start(struct rwf *f, const struct rwf_frame *now, float target, bool want_ambient);
void rwf_frame(struct rwf *f, const struct rwf_frame *fr, const struct rwf_led *led, struct rwf_act *act);
/* the exposure for the flash: e = target / (ambient + RWF_RATIO x pre-flash part), time <= min(RWF_EXP_MAX_NS, period - readout-ish) */
void rwf_flash_exposure(float amb_rate, float pre_rate, float target, uint64_t period_ns, float gain_max, uint64_t *exp_ns, float *gain);
uint32_t rwf_strobe_ms(uint64_t period_ns, uint64_t readout_ns, uint64_t exp_ns);
/* the share (0..1) of a frame's exposure window (first line's start .. last line's end) inside [on, off] */
float rwf_lit_fraction(const struct rwf_frame *fr, uint64_t on_ns, uint64_t off_ns);
#endif
