/*
 * y2s97 (backdrop capture rename): the MATCHER, pure C over an abstract list of a command buffer's steps, so that it can be
 * tested on the build machine (rw_backdrop_host_test.c, rw-backdrop-host-test.sh). redwoodmetal.m (rw_bdr_commit) turns the
 * command buffer's RedwoodMetalWork list into this list, asks rwb_find, and rewrites (or, by default, only counts).
 *
 * Pattern (inferred from CoreAnimation's backdrop comment at air_metal.c rwa_texture_copy, NOT yet seen in a trace):
 *   f: the first step of the command buffer that touches S is a pass into S (attachment 0, level 0, slice 0) whose load is Clear;
 *   k: a later texture copy S -> B of the WHOLE surface (level 0, slice 0, origin 0, full size; same format and size, one sample,
 *      2D, one level, one slice; B private: no IOSurface, no buffer, not a view, not a YCbCr plane);
 *   l: the next step after k that touches S is a pass into S (attachment 0, level 0, slice 0) whose load is Load.
 * Conditions, every one of them required:
 *   in [f, k): S only ever as attachment 0 at level 0 / slice 0 (never sampled, never another attachment / depth / copied);
 *              B not touched at all; no opaque step (a blit whose textures are not known); no argument-buffer pipeline;
 *   in (k, l): S not touched (by the definition of l); B never WRITTEN (attachment, depth, copy destination, compute texture);
 *              no opaque step; no argument-buffer pipeline;
 *   at l:      S only as attachment 0; B not an attachment of it; no argument-buffer pipeline.
 * Rewrite (redwoodmetal.m): attachment 0 of the S passes in [f, k) becomes B, the copy k is dropped, one whole copy B -> S is
 * inserted right before l (rwa_texture_copy_whole: S not preloaded). What S and B hold after l is what they held before.
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#ifndef RW_BACKDROP_PURE_H
#define RW_BACKDROP_PURE_H
#include <stdint.h>

enum { RWB_NONE = 0, RWB_DRAW, RWB_CLEAR, RWB_COPY, RWB_OPAQUE, RWB_COMPUTE };
enum { RWB_LOAD_DONTCARE = 0, RWB_LOAD_LOAD = 1, RWB_LOAD_CLEAR = 2 };

/* rejection reasons, in the order they are checked for a candidate copy */
enum {
	RWB_R_PARTIAL = 0,  /* the copy is not of the whole level-0 / slice-0 surface (left untouched, by design) */
	RWB_R_SHAPE,        /* S and B differ in format / size / samples / type / levels / slices, or S is not plain */
	RWB_R_BSHARED,      /* B has an IOSurface / buffer / is a view / a YCbCr plane, or B == S */
	RWB_R_FIRST,        /* the first step touching S is not a Clear pass into S's level 0 / slice 0 */
	RWB_R_SUSED,        /* S touched in [f, k) other than as attachment 0 at level 0 / slice 0 */
	RWB_R_BUSED,        /* B touched in [f, k) */
	RWB_R_OPAQUE,       /* a blit with unknown textures in [f, l) */
	RWB_R_LATER,        /* the next step touching S after k is not a Load pass into S (or B is an attachment of it) */
	RWB_R_NOLATER,      /* nothing touches S after k */
	RWB_R_BWRITTEN,     /* B written in (k, l) */
	RWB_R_ARGBUF,       /* an argument-buffer pipeline in [f, l] (its textures are not visible here) */
	RWB_R_TABLE,        /* (redwoodmetal.m) too many steps / textures in the command buffer */
	RWB_R_LIB,          /* (redwoodmetal.m, rename on) the library has no rwa_texture_copy_whole */
	RWB_R_N
};

#define RWB_TEX 32
struct rwb_step {
	uint8_t kind;        /* RWB_* */
	uint8_t load0;       /* draw / clear: attachment 0's load action */
	uint8_t c0_base;     /* draw / clear: attachment 0 is at level 0, slice 0 */
	uint8_t argbuf;      /* draw / compute: the pipeline declares argument buffers */
	uint8_t copy_full;   /* copy: whole level-0 / slice-0 surface, origins 0 */
	uint8_t copy_shape;  /* copy: same format/size, 1 sample, 2D, 1 level, 1 slice, and the source is plain */
	uint8_t copy_bok;    /* copy: the destination is private (no IOSurface / buffer / parent / YCbCr) */
	uint8_t ncolor, ntex;
	int color[8];        /* texture identities (-1: none) */
	int depth;
	int tex[RWB_TEX];
	int src, dst;        /* copy */
};

struct rwb_match {
	unsigned f, k, l;
	int s, b;
};

#define RWB_REF_C0    1u   /* attachment 0 */
#define RWB_REF_OTHER 2u   /* anything else: another attachment, depth, sampled / compute texture, copy source or destination */

static inline unsigned
rwb_refs(const struct rwb_step *st, int id)
{
	unsigned r = 0;

	if (id < 0) {
		return 0;
	}
	if (st->kind == RWB_DRAW || st->kind == RWB_CLEAR) {
		for (unsigned i = 0; i < st->ncolor && i < 8; i++) {
			if (st->color[i] == id) {
				r |= i == 0 ? RWB_REF_C0 : RWB_REF_OTHER;
			}
		}
		if (st->depth == id) {
			r |= RWB_REF_OTHER;
		}
	}
	if (st->kind == RWB_DRAW || st->kind == RWB_COMPUTE) {
		for (unsigned i = 0; i < st->ntex && i < RWB_TEX; i++) {
			if (st->tex[i] == id) {
				r |= RWB_REF_OTHER;
			}
		}
	}
	if (st->kind == RWB_COPY && (st->src == id || st->dst == id)) {
		r |= RWB_REF_OTHER;
	}
	return r;
}

/* does this (known) step write `id`? */
static inline int
rwb_writes(const struct rwb_step *st, int id)
{
	if (id < 0) {
		return 0;
	}
	if (st->kind == RWB_DRAW || st->kind == RWB_CLEAR) {
		for (unsigned i = 0; i < st->ncolor && i < 8; i++) {
			if (st->color[i] == id) {
				return 1;
			}
		}
		return st->depth == id;
	}
	if (st->kind == RWB_COMPUTE) {   /* a compute texture may be written: no way to tell from here */
		for (unsigned i = 0; i < st->ntex && i < RWB_TEX; i++) {
			if (st->tex[i] == id) {
				return 1;
			}
		}
		return 0;
	}
	return st->kind == RWB_COPY && st->dst == id;
}

/* the candidate copy k checked; 0 = it matches (m filled), else 1 + the reason */
static inline int
rwb_check(const struct rwb_step *st, unsigned n, unsigned k, struct rwb_match *m)
{
	const struct rwb_step *c = &st[k];
	int s = c->src, b = c->dst;
	unsigned f, l, j;

	if (!c->copy_full) {
		return 1 + RWB_R_PARTIAL;
	}
	if (!c->copy_shape) {
		return 1 + RWB_R_SHAPE;
	}
	if (!c->copy_bok || s == b || s < 0 || b < 0) {
		return 1 + RWB_R_BSHARED;
	}
	for (f = 0; f < k && rwb_refs(&st[f], s) == 0; f++) {
	}
	if (f == k || (st[f].kind != RWB_DRAW && st[f].kind != RWB_CLEAR) || rwb_refs(&st[f], s) != RWB_REF_C0 ||
	    !st[f].c0_base || st[f].load0 != RWB_LOAD_CLEAR) {
		return 1 + RWB_R_FIRST;
	}
	for (j = f; j < k; j++) {
		unsigned r = rwb_refs(&st[j], s);

		if (st[j].kind == RWB_OPAQUE) {
			return 1 + RWB_R_OPAQUE;
		}
		if (st[j].argbuf) {
			return 1 + RWB_R_ARGBUF;
		}
		if ((r & RWB_REF_OTHER) || ((r & RWB_REF_C0) && !st[j].c0_base)) {
			return 1 + RWB_R_SUSED;
		}
		if (rwb_refs(&st[j], b) != 0) {
			return 1 + RWB_R_BUSED;
		}
	}
	for (l = k + 1; l < n && rwb_refs(&st[l], s) == 0 && st[l].kind != RWB_OPAQUE; l++) {
		if (st[l].argbuf) {
			return 1 + RWB_R_ARGBUF;
		}
		if (rwb_writes(&st[l], b)) {
			return 1 + RWB_R_BWRITTEN;
		}
	}
	if (l == n) {
		return 1 + RWB_R_NOLATER;
	}
	if (st[l].kind == RWB_OPAQUE) {
		return 1 + RWB_R_OPAQUE;
	}
	if (st[l].argbuf) {
		return 1 + RWB_R_ARGBUF;
	}
	if ((st[l].kind != RWB_DRAW && st[l].kind != RWB_CLEAR) || rwb_refs(&st[l], s) != RWB_REF_C0 || !st[l].c0_base ||
	    st[l].load0 != RWB_LOAD_LOAD) {
		return 1 + RWB_R_LATER;
	}
	for (unsigned i = 0; i < st[l].ncolor && i < 8; i++) {
		if (st[l].color[i] == b) {
			return 1 + RWB_R_LATER;
		}
	}
	if (st[l].depth == b) {
		return 1 + RWB_R_LATER;
	}
	m->f = f;
	m->k = k;
	m->l = l;
	m->s = s;
	m->b = b;
	return 0;
}

/*
 * The first candidate (a copy whose source was drawn into earlier in the buffer) that matches: 1 and *m; 0 if none does.
 * Every candidate before it that does not match is counted in rejects[reason]; *candidates counts all that were looked at.
 */
static inline int
rwb_find(const struct rwb_step *st, unsigned n, struct rwb_match *m, unsigned rejects[RWB_R_N], unsigned *candidates)
{
	for (unsigned k = 0; k < n; k++) {
		if (st[k].kind != RWB_COPY || st[k].src < 0) {
			continue;
		}
		unsigned j;

		for (j = 0; j < k; j++) {
			if ((st[j].kind == RWB_DRAW || st[j].kind == RWB_CLEAR) && st[j].ncolor > 0 && st[j].color[0] == st[k].src) {
				break;
			}
		}
		if (j == k) {
			continue;   /* not a capture of something this buffer drew */
		}
		(*candidates)++;
		int r = rwb_check(st, n, k, m);

		if (r == 0) {
			return 1;
		}
		rejects[r - 1]++;
	}
	return 0;
}

#endif
