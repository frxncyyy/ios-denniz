#ifndef SWEET_FB_SWAPWAIT_H
#define SWEET_FB_SWAPWAIT_H
/*
 * SwapWait (IOMobileFramebuffer selector 6) and swap retirement, pure and host-testable (never touches IOKit).
 *
 * CoreAnimation (IOMFBDisplay::can_update_) asks SwapWait with mode 0 and the no-block bit before it draws into a buffer again and
 * skips the frame if the buffer's last swap has not retired.  Until now the selector always answered success, so CA could draw into a
 * surface the panel copy was still reading, and nothing ever pushed back on a CA that runs ahead of the GPU.
 * The scalar layout [swap id, options, timeout ms] and the three modes are from the iOS 20H392 IOMobileFramebufferLegacy
 * swap_wait_gated as documented by the reference project (docs/SCHED.md, patch 0123); ours was only logged so far, which is why the
 * behaviour is opt-in (boot-arg n2swait) and the first calls' raw scalars are logged.
 *
 *   options bit 31 : do not block      low bits: mode
 *   mode 0         : the swap is retired: neither waiting to be shown nor being shown (we copy out of the surfaces, so a surface is free
 *                    as soon as the copy ended, or when a newer swap replaced the swap unseen)
 *   mode 1         : the swap is no longer waiting (its copy started or finished)
 *   mode 2         : no swap is waiting
 *   unknown / old ids are ready at once; not ready: kIOReturnBusy with the no-block bit, else wait until ready or timeout
 *   (timeout 0 = the cap), then kIOReturnTimeout.
 * Level 2 (retire on GPU, reference patch 0235): a swap replaced unseen while a GPU job still writes its surfaces is NOT retired
 * until the GPU is done with them (or 200 ms, or the 4 late slots are full): CA then skips frames instead of queueing them without
 * bound behind a GPU that is slower than its CPU side.
 */
#include <stdbool.h>
#include <stdint.h>

#define SWEET_FB_RETIRE_LATE_SLOTS   4u
#define SWEET_FB_RETIRE_LATE_MAX_MS  200u
#define SWEET_FB_SWAPWAIT_MAX_MS     1000u
#define SWEET_FB_SWAPWAIT_NOBLOCK    0x80000000u

struct sweet_fb_retire {
	uint32_t pending;                               /* swap queued, not taken yet (0 none) */
	uint32_t presenting;                            /* swap being copied/transferred (0 none) */
	uint32_t preparing;                             /* 0393: swap taken by the double buffer's prepare slot, copy not finished (0 none) */
	uint64_t valve;                                 /* 0393: not-ready answers turned into ready after SWEET_FB_SWAPWAIT_VALVE_MS */
	uint32_t late_id[SWEET_FB_RETIRE_LATE_SLOTS];   /* replaced unseen, GPU may still write their surfaces (0 free) */
	uint64_t late_since_ms[SWEET_FB_RETIRE_LATE_SLOTS];
	uint64_t dropped, late, late_busy, late_forced;
	uint64_t calls, not_ready, blocked, timeouts;
};

enum { SWEET_FB_RETIRE_NONE = 0, SWEET_FB_RETIRE_RETIRED = 1, SWEET_FB_RETIRE_HELD = 2 };
enum { SWEET_FB_LATE_KEEP = 0, SWEET_FB_LATE_RETIRE = 1, SWEET_FB_LATE_FORCE = 2 };
enum { SWEET_FB_WAIT_OK = 0, SWEET_FB_WAIT_BUSY = 1, SWEET_FB_WAIT_TIMEOUT = 2, SWEET_FB_WAIT_SLEEP = 3 };

/* A new SwapEnd with id replaces the pending one.  With hold the replaced swap goes into a free late slot instead of retiring;
 * *slot tells which (the caller moves the replaced surface references there).  A full late list retires it (forced). */
static inline int
sweet_fb_retire_on_swap(struct sweet_fb_retire *r, uint32_t id, bool hold, uint64_t now_ms, unsigned *slot)
{
	uint32_t replaced = r->pending;
	r->pending = id;
	if (replaced == 0 || replaced == id) {
		return SWEET_FB_RETIRE_NONE;
	}
	r->dropped++;
	if (hold) {
		for (unsigned i = 0; i < SWEET_FB_RETIRE_LATE_SLOTS; i++) {
			if (r->late_id[i] == 0) {
				r->late_id[i] = replaced;
				r->late_since_ms[i] = now_ms;
				r->late++;
				*slot = i;
				return SWEET_FB_RETIRE_HELD;
			}
		}
		r->late_forced++;
	}
	return SWEET_FB_RETIRE_RETIRED;
}

/* The vsync handler takes the pending swap for presentation. */
static inline void
sweet_fb_retire_take(struct sweet_fb_retire *r)
{
	r->presenting = r->pending;
	r->pending = 0;
}

/*
 * 0393 (fbsync-20261112): the double buffer (0156) takes the pending swap in prepNext() and copies it while the previous frame is
 * transferred.  Before 0393 the retire state did not know: the taken swap stayed "pending", so the next SwapEnd counted it as
 * replaced unseen (retired) although its copy had not run yet, and handOver() then marked the NEWER pending swap as presenting.
 * A swap's surfaces are free as soon as its copy ended (the panel reads our own buffer), not only after the transfer.
 */
#define SWEET_FB_SWAPWAIT_VALVE_MS 250u
static inline void
sweet_fb_retire_prep(struct sweet_fb_retire *r)
{
	r->preparing = r->pending;
	r->pending = 0;
}

/* The prepared swap's copy ended, or the prepare slot hands an uncopied swap to the presenter (to_presenting). */
static inline void
sweet_fb_retire_prep_end(struct sweet_fb_retire *r, bool to_presenting)
{
	if (to_presenting) {
		r->presenting = r->preparing;
	}
	r->preparing = 0;
}

/* The prepare slot dropped its swap (id) without showing it: back to pending unless a newer swap is queued. */
static inline void
sweet_fb_retire_prep_drop(struct sweet_fb_retire *r, bool requeued, uint32_t id)
{
	if (requeued && r->pending == 0) {
		r->pending = id;
	}
	r->preparing = 0;
}

/* The presenting swap's copy ended (the transfer of our own buffer may still run). */
static inline void
sweet_fb_retire_copied(struct sweet_fb_retire *r)
{
	r->presenting = 0;
}

/* The present could not start: the swap goes back to pending unless a newer one replaced it meanwhile. */
static inline void
sweet_fb_retire_requeue(struct sweet_fb_retire *r, bool requeued)
{
	if (requeued && r->pending == 0) {
		r->pending = r->presenting;
	}
	r->presenting = 0;
}

/* Copy and transfer finished (or the present was abandoned for good). */
static inline void
sweet_fb_retire_done(struct sweet_fb_retire *r)
{
	r->presenting = 0;
}

/* One late slot, evaluated with the GPU's current answer.  RETIRE/FORCE free the slot (the caller releases its references). */
static inline int
sweet_fb_retire_late_check(struct sweet_fb_retire *r, unsigned slot, bool gpu_busy, uint64_t now_ms)
{
	if (slot >= SWEET_FB_RETIRE_LATE_SLOTS || r->late_id[slot] == 0) {
		return SWEET_FB_LATE_RETIRE;
	}
	if (gpu_busy && now_ms - r->late_since_ms[slot] < SWEET_FB_RETIRE_LATE_MAX_MS) {
		return SWEET_FB_LATE_KEEP;
	}
	r->late_id[slot] = 0;
	if (gpu_busy) {
		r->late_forced++;
		return SWEET_FB_LATE_FORCE;
	}
	return SWEET_FB_LATE_RETIRE;
}

static inline bool
sweet_fb_retire_is_late(const struct sweet_fb_retire *r, uint32_t id)
{
	for (unsigned i = 0; i < SWEET_FB_RETIRE_LATE_SLOTS; i++) {
		if (id != 0 && r->late_id[i] == id) {
			return true;
		}
	}
	return false;
}

static inline bool
sweet_fb_retire_any_late(const struct sweet_fb_retire *r)
{
	for (unsigned i = 0; i < SWEET_FB_RETIRE_LATE_SLOTS; i++) {
		if (r->late_id[i] != 0) {
			return true;
		}
	}
	return false;
}

/* Is SwapWait(id, mode) satisfied?  With the display off or the driver stopped nothing is read any more. */
static inline bool
sweet_fb_swap_ready(const struct sweet_fb_retire *r, uint32_t id, uint32_t mode, bool display_off, bool stopped)
{
	if (display_off || stopped) {
		return true;
	}
	switch (mode) {
	case 0:
		return id == 0 || (id != r->pending && id != r->presenting && id != r->preparing && !sweet_fb_retire_is_late(r, id));
	case 1:
		return id == 0 || (id != r->pending && !sweet_fb_retire_is_late(r, id));
	case 2:
		return r->pending == 0 && !sweet_fb_retire_any_late(r);
	default:
		return true;
	}
}

/* Decide one SwapWait iteration.  timeout_ms 0 = the cap; elapsed_ms is the time spent waiting so far. */
static inline int
sweet_fb_swapwait_step(bool ready, uint32_t options, uint32_t timeout_ms, uint64_t elapsed_ms)
{
	if (ready) {
		return SWEET_FB_WAIT_OK;
	}
	if (options & SWEET_FB_SWAPWAIT_NOBLOCK) {
		return SWEET_FB_WAIT_BUSY;
	}
	uint64_t limit = (timeout_ms == 0 || timeout_ms > SWEET_FB_SWAPWAIT_MAX_MS) ? SWEET_FB_SWAPWAIT_MAX_MS : timeout_ms;
	return elapsed_ms >= limit ? SWEET_FB_WAIT_TIMEOUT : SWEET_FB_WAIT_SLEEP;
}

/*
 * IOMFBDisplayRefresh.displayMinRefreshInterval: on this device CoreAnimation does not follow the vsync notifications (ChipID 0x8015
 * has CADeviceUseVBL false); a timer runs it at AppleCLCDDisplay::heartbeat_rate, which reads this property (seconds * 2^32) from
 * the framebuffer service and defaults to 1/60.  Without it CA renders every 16.7 ms although only every flush_ticks-th frame is
 * shown.  0 = do not publish (present every vsync).  At most 30 ticks (2 Hz) so the value fits 32 bits.
 */
static inline uint32_t
sweet_fb_refresh_interval(uint32_t flush_ticks)
{
	if (flush_ticks <= 1) {
		return 0;
	}
	if (flush_ticks > 30) {
		flush_ticks = 30;
	}
	return (uint32_t)((((uint64_t)flush_ticks << 32) + 30) / 60);
}

#endif
