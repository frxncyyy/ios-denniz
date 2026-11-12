#ifndef SWEET_FB_SCAN_H
#define SWEET_FB_SCAN_H
/*
 * NORD2 scan-address probe, pure and host-testable (never touches IOKit or registers).
 *
 * Why: the panel is DSI command mode.  The overlay engines (OVL 2L layer 0 of both pipes) read the picture from DRAM only while
 * a transfer runs (MUTEX_EN + DSI_START ... FRAME_DONE); between transfers nothing scans memory.  If the layer ADDR (and PITCH)
 * registers can simply be re-pointed before a transfer, then
 *   - a frame that is already in physically contiguous memory below 4 GiB (a GPU-rendered CoreAnimation surface) can be sent to the
 *     panel with NO CPU copy (the copy is ~2.6 ms of CPU and 10 MB of write-combined traffic per present), and
 *   - ownership of that surface ends at FRAME_DONE (no flip-completion tracking like a video-mode pipeline needs).
 * Nothing proves today that the ADDR/PITCH writes take effect (the only OVL writes ever made are CON/SRC/RDMA of the removed
 * warning layer).  The probe settles it on the device and costs nothing when the boot-arg is absent:
 *   n2scan=<ms>  (1 = default 1500 ms hold, clamped 200..5000) once, after the UI is up, show a test pattern from a second buffer
 *                in the framebuffer carve-out through ADDR+PITCH, then restore and continue normally.
 *
 * Layout facts (NOTES.md 2026-10-19 phase-1 dump; the driver re-checks them at run time before writing anything):
 *   panel buffer  0x76e10000, pitch 4352, 1080x2400 BGRA; pipe 0 reads x 0..539, pipe 1 x 540..1079 at +2160 bytes
 *   OVL0_2L 0x14001000 / OVL1_2L 0x14101000: CON +0x30 SIZE +0x38 PITCH +0x44 ADDR +0xf40 (layer 0)
 *   LK's warning layer lived at 0x77806000 and is disabled before the polled transfer path starts
 *   carve-out end 0x78c00000
 */
#include <stdbool.h>
#include <stdint.h>

#define SWEET_FB_SCAN_CARVE_END     0x78c00000ull
#define SWEET_FB_SCAN_ALIGN         0x1000ull
#define SWEET_FB_SCAN_PROBE_PITCH   4608u        /* != the LK pitch 4352: ADDR and PITCH are both exercised */
#define SWEET_FB_SCAN_PIPES         2u
#define SWEET_FB_SCAN_DEFAULT_HOLD_MS 1500u
#define SWEET_FB_SCAN_MIN_PRESENTS  20u          /* UI is animating */
#define SWEET_FB_SCAN_MIN_DELAY_MS  8000u        /* and has been for a while */

/* ---- address plan ---- */
struct sweet_fb_scan_plan {
	uint64_t slot_pa;                        /* second buffer */
	uint64_t bytes;                          /* probe_pitch * height */
	uint32_t pitch;
	uint64_t addr[SWEET_FB_SCAN_PIPES];      /* what each pipe's ADDR register must hold while the probe frame is scanned */
};

/* Slot directly behind the panel buffer (page aligned), inside the carve-out.  width must be even (two pipes read halves). */
static inline bool
sweet_fb_scan_plan_make(struct sweet_fb_scan_plan *p, uint64_t base_pa, uint32_t base_pitch, uint32_t width, uint32_t height,
    uint32_t probe_pitch)
{
	if (!p || !base_pa || !base_pitch || !width || (width & 1u) || !height || (base_pa & (SWEET_FB_SCAN_ALIGN - 1)) ||
	    probe_pitch < width * 4u || (probe_pitch & 63u) || base_pitch < width * 4u) {
		return false;
	}
	uint64_t base_end = base_pa + (uint64_t)base_pitch * height;
	uint64_t slot = (base_end + SWEET_FB_SCAN_ALIGN - 1) & ~(SWEET_FB_SCAN_ALIGN - 1);
	uint64_t bytes = (uint64_t)probe_pitch * height;
	if (slot < base_end || slot + bytes < slot || slot + bytes > SWEET_FB_SCAN_CARVE_END || slot + bytes > 0x100000000ull) {
		return false;
	}
	p->slot_pa = slot;
	p->bytes = bytes;
	p->pitch = probe_pitch;
	for (uint32_t i = 0; i < SWEET_FB_SCAN_PIPES; i++) {
		p->addr[i] = slot + (uint64_t)i * (width / SWEET_FB_SCAN_PIPES) * 4u;
	}
	return true;
}

/* What pipe i must show before the probe touches it (the layout the early console verified) and after restore. */
static inline uint32_t
sweet_fb_scan_base_addr(uint64_t base_pa, uint32_t width, uint32_t pipe)
{
	return (uint32_t)(base_pa + (uint64_t)pipe * (width / SWEET_FB_SCAN_PIPES) * 4u);
}

/* PITCH register: only the low 16 bits are the pitch, the rest is preserved. */
static inline uint32_t
sweet_fb_scan_pitch_reg(uint32_t old, uint32_t pitch)
{
	return (old & ~0xffffu) | (pitch & 0xffffu);
}

/* ---- test pattern (BGRA words 0xAARRGGBB as stored little endian) ---- */
#define SWEET_FB_SCAN_BORDER 8u
static inline uint32_t
sweet_fb_scan_pixel(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	static const uint32_t bars[8] = { 0xffffffffu, 0xffffff00u, 0xff00ffffu, 0xff00ff00u, 0xffff00ffu, 0xffff0000u, 0xff0000ffu, 0xff000000u };
	if (x >= w || y >= h) {
		return 0;
	}
	if (x < SWEET_FB_SCAN_BORDER || y < SWEET_FB_SCAN_BORDER || x >= w - SWEET_FB_SCAN_BORDER || y >= h - SWEET_FB_SCAN_BORDER) {
		return ((x + y) & 16u) ? 0xffffffffu : 0xffff8000u;           /* striped frame: shows cropping, shifts and pitch skew */
	}
	if (y >= h - h / 8u) {
		uint32_t v = (uint32_t)(((uint64_t)x * 255u) / (w - 1u));   /* grey ramp left to right */
		return 0xff000000u | (v << 16) | (v << 8) | v;
	}
	return bars[(uint32_t)(((uint64_t)x * 8u) / w)];
}

/* ---- probe state machine ---- */
enum sweet_fb_scan_state {
	SWEET_FB_SCAN_OFF = 0,
	SWEET_FB_SCAN_ARMED,      /* waiting for the UI */
	SWEET_FB_SCAN_FLIPPED,    /* registers written, probe frame transferring */
	SWEET_FB_SCAN_HOLD,       /* probe frame on the panel */
	SWEET_FB_SCAN_DONE,       /* restored, probe finished */
	SWEET_FB_SCAN_FAILED,     /* refused or failed, restored; never retried */
};
enum sweet_fb_scan_event {
	SWEET_FB_SCAN_EV_BEGIN_OK,
	SWEET_FB_SCAN_EV_BEGIN_FAIL,   /* layout mismatch, readback mismatch, link busy */
	SWEET_FB_SCAN_EV_XFER_DONE,
	SWEET_FB_SCAN_EV_XFER_TIMEOUT,
	SWEET_FB_SCAN_EV_HOLD_EXPIRED,
	SWEET_FB_SCAN_EV_STOP,
};

static inline uint32_t
sweet_fb_scan_hold_ms(uint32_t arg)
{
	if (arg == 0) {
		return 0;
	}
	if (arg == 1) {
		return SWEET_FB_SCAN_DEFAULT_HOLD_MS;
	}
	return arg < 200 ? 200 : (arg > 5000 ? 5000 : arg);
}

static inline bool
sweet_fb_scan_due(int state, bool link_ready, uint32_t presents, uint64_t now_ms, uint64_t first_present_ms)
{
	return state == SWEET_FB_SCAN_ARMED && link_ready && presents >= SWEET_FB_SCAN_MIN_PRESENTS &&
	    first_present_ms != 0 && now_ms >= first_present_ms && now_ms - first_present_ms >= SWEET_FB_SCAN_MIN_DELAY_MS;
}

/* Next state.  *restore is set when the base ADDR/PITCH must be written back (from FLIPPED or HOLD on any exit). */
static inline int
sweet_fb_scan_next(int state, int event, bool *restore)
{
	*restore = false;
	switch (state) {
	case SWEET_FB_SCAN_ARMED:
		if (event == SWEET_FB_SCAN_EV_BEGIN_OK) { return SWEET_FB_SCAN_FLIPPED; }
		if (event == SWEET_FB_SCAN_EV_BEGIN_FAIL) { *restore = true; return SWEET_FB_SCAN_FAILED; }   /* partial writes undone */
		if (event == SWEET_FB_SCAN_EV_STOP) { return SWEET_FB_SCAN_FAILED; }
		return state;
	case SWEET_FB_SCAN_FLIPPED:
		if (event == SWEET_FB_SCAN_EV_XFER_DONE) { return SWEET_FB_SCAN_HOLD; }
		if (event == SWEET_FB_SCAN_EV_XFER_TIMEOUT || event == SWEET_FB_SCAN_EV_STOP) { *restore = true; return SWEET_FB_SCAN_FAILED; }
		return state;
	case SWEET_FB_SCAN_HOLD:
		if (event == SWEET_FB_SCAN_EV_HOLD_EXPIRED) { *restore = true; return SWEET_FB_SCAN_DONE; }
		if (event == SWEET_FB_SCAN_EV_STOP) { *restore = true; return SWEET_FB_SCAN_FAILED; }
		return state;
	default:
		return state;       /* OFF, DONE, FAILED are terminal */
	}
}

/* While the probe owns the panel (FLIPPED/HOLD) the vsync handler must not hand over a frame. */
static inline bool
sweet_fb_scan_owns_panel(int state)
{
	return state == SWEET_FB_SCAN_FLIPPED || state == SWEET_FB_SCAN_HOLD;
}

#endif
