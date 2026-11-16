/* 0407 (fpsmeter-20261114): what the panel really shows when the overlay composes (mode 2/3), measurement only.
 * SPDX-License-Identifier: MIT
 *
 * The overlay path never copies pixels, so the CPU-copy hash of FB SEEN (noteDistinct) does not run there.  Instead every HW frame
 * is classified by a content clock: Nord2GPU job ids are one device-wide, strictly increasing counter, and
 * nord2_gpu_surface_write_seq(id) is the id of the newest job that wrote the surface.  Per planner slot i (layer order of the plan)
 * the pair (surface id, write seq) of this frame is compared with the previous HW frame's pair in the same slot:
 *   back  some slot shows OLDER GPU content than the slot showed before (seq lower): the picture goes back in time;
 *   new   otherwise, some slot shows a newer seq (a GPU job wrote the shown pixels since the last frame);
 *   unk   otherwise, some slot changed surface but no GPU write is known for it (seq 0: CPU-drawn surface): cannot tell;
 *   same  nothing changed in any slot: the same pixels are scanned out again.
 * A different slot count (layout change) counts as new.  "new" means "re-rendered", not "visibly different": an app that draws the
 * same picture again still counts as new (the per-client figure below says WHO re-rendered).
 * Per client (creator pid of the shown surface): frames on which that client's layer was shown and on which it was fresh (its slot
 * changed id or seq) -- the game's own visible frame rate next to backboardd's.  Pure logic, no locks: present thread only. */
#ifndef SWEET_FB_FPSMETER_H
#define SWEET_FB_FPSMETER_H
#ifndef KERNEL
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#endif

#define SWEET_FB_FPS_SLOTS      6u          /* = SWEET_FB_HW_MAX_IN */
#define SWEET_FB_FPS_CLIENTS    4u          /* distinct creator pids kept per window; more go to "other" */

enum { SWEET_FB_FPS_SAME = 0, SWEET_FB_FPS_NEW = 1, SWEET_FB_FPS_UNK = 2, SWEET_FB_FPS_BACK = 3 };

struct sweet_fb_fps_client { int32_t pid; uint32_t shown, fresh; };

struct sweet_fb_fps {
	/* previous HW frame (kept across windows) */
	uint32_t last_n, last_id[SWEET_FB_FPS_SLOTS];
	uint64_t last_seq[SWEET_FB_FPS_SLOTS];
	/* this window */
	uint32_t n_new, n_same, n_back, n_unk;
	struct sweet_fb_fps_client cl[SWEET_FB_FPS_CLIENTS];
	uint32_t other_shown, other_fresh;
};

static inline void
sweet_fb_fps_client_note(struct sweet_fb_fps *f, int32_t pid, int fresh)
{
	for (unsigned k = 0; k < SWEET_FB_FPS_CLIENTS; k++) {
		struct sweet_fb_fps_client *c = &f->cl[k];
		if (c->shown == 0) { c->pid = pid; }
		if (c->pid == pid) { c->shown++; c->fresh += fresh ? 1u : 0u; return; }
	}
	f->other_shown++; f->other_fresh += fresh ? 1u : 0u;
}

/* One HW frame: n slots, surface ids, write seqs, creator pids (pid may be NULL). Returns the class. */
static inline int
sweet_fb_fps_note(struct sweet_fb_fps *f, unsigned n, const uint32_t *id, const uint64_t *seq, const int32_t *pid)
{
	if (n > SWEET_FB_FPS_SLOTS) { n = SWEET_FB_FPS_SLOTS; }
	bool layout = (n != f->last_n), back = false, newer = false, unk = false;
	for (unsigned i = 0; i < n; i++) {
		bool fresh = layout || id[i] != f->last_id[i] || seq[i] != f->last_seq[i];
		if (!layout) {
			if (seq[i] != 0 && f->last_seq[i] != 0 && seq[i] < f->last_seq[i]) { back = true; }
			else if (seq[i] != 0 && seq[i] > f->last_seq[i]) { newer = true; }
			else if (id[i] != f->last_id[i]) { if (seq[i] == 0) { unk = true; } else { newer = true; } }
		}
		if (pid != NULL) { sweet_fb_fps_client_note(f, pid[i], fresh); }
		f->last_id[i] = id[i]; f->last_seq[i] = seq[i];
	}
	f->last_n = n;
	int r = back ? SWEET_FB_FPS_BACK : (layout || newer) ? SWEET_FB_FPS_NEW : unk ? SWEET_FB_FPS_UNK : SWEET_FB_FPS_SAME;
	if (r == SWEET_FB_FPS_BACK) { f->n_back++; } else if (r == SWEET_FB_FPS_NEW) { f->n_new++; }
	else if (r == SWEET_FB_FPS_UNK) { f->n_unk++; } else { f->n_same++; }
	return r;
}

/* A CPU-path frame between HW frames: the next HW frame is compared against nothing (counts as new, not as back). */
static inline void
sweet_fb_fps_break(struct sweet_fb_fps *f)
{
	f->last_n = 0;
}

static inline void
sweet_fb_fps_window_reset(struct sweet_fb_fps *f)
{
	f->n_new = f->n_same = f->n_back = f->n_unk = 0;
	for (unsigned k = 0; k < SWEET_FB_FPS_CLIENTS; k++) { f->cl[k].pid = 0; f->cl[k].shown = f->cl[k].fresh = 0; }
	f->other_shown = f->other_fresh = 0;
}

/* " hw_new=.. hw_same=.. hw_back=.. hw_unk=.." appended to FB SEEN. */
static inline int
sweet_fb_fps_format_seen(const struct sweet_fb_fps *f, char *buf, size_t len)
{
	return snprintf(buf, len, "hw_new=%u hw_same=%u hw_back=%u hw_unk=%u", f->n_new, f->n_same, f->n_back, f->n_unk);
}

/* "pid 59 shown=430 fresh=86.0/s | ..." -- names are added by the caller (proc_name is a kernel call). fresh/s over win_us. */
static inline int
sweet_fb_fps_format_client(const struct sweet_fb_fps_client *c, const char *name, uint64_t win_us, char *buf, size_t len)
{
	uint64_t w = win_us ? win_us : 1;
	uint64_t r10 = ((uint64_t)c->fresh * 10000000ULL + w / 2) / w;      /* tenths of a frame a second */
	return snprintf(buf, len, "%s[%d] shown=%u fresh=%u (%llu.%llu/s)", name, (int)c->pid, c->shown, c->fresh,
	           (unsigned long long)(r10 / 10), (unsigned long long)(r10 % 10));
}
#endif
