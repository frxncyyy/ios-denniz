/*
 * 0398 (usecount-20261112): the IOSurface use count, shared across processes.
 * SPDX-License-Identifier: MIT
 *
 * IOSurface.framework (iOS 16.7, disassembled from the shared cache, usecount-20261112/README):
 *   - every process keeps its own two counters per surface (IOSurfaceClient + 0x8: u32 category 0, u32 category 1, saturating);
 *     IOSurfaceGetUseCount() returns only their sum (the local count);
 *   - IOSurfaceIncrementUseCountForCategory(c) calls trap 0 (surface id, c) only on that process's 0 -> 1 step of category c,
 *     IOSurfaceDecrementUseCountForCategory(c) calls trap 1 (id, c) only on the 1 -> 0 step; c >= 2 raises before any trap;
 *     IOSurfaceIncrement/DecrementUseCount() are category 0;
 *   - IOSurfaceIsInUse() reads the u64 at shared state page + 0x18 (!= 0), IsInUseForCategory(c) the u32 at +0x18 (c == 0) or
 *     +0x1c (c == 1), IsInUseForAnyOtherCategory(c) the other one.  Nothing else of the use count goes through the kernel.
 * So the kernel keeps, per surface, the number of (client connection, category) pairs that hold it plus its own holders, in the
 * two state-page words.  The words are the truth (atomic read-modify-write on kernel memory that users map read-only).
 *
 * Holders:
 *   user      one bit per category per client mapping (SweetSurfaceRef): trap 0 sets it, trap 1 clears it, the mapping's release
 *             (trap 4/5, client close, process death) clears what is left ("dropped at release");
 *   display   SweetFramebuffer: every surface reference a swap holds (SwapEnd capture -> copy end / FRAME_DONE replacement);
 *   GPU       Nord2GPU: every IOSurface import listed by a job, from submit until the job left QUEUED/RUNNING/STOPPING.
 * Kernel holders count in category 0 (what IOSurfaceIsInUse and the pools read).
 *
 * Pure: no IOKit, no allocation; kernel and host test (test/usecount_host_test.c) share it.
 */
#ifndef SWEET_SURFACE_USE_H
#define SWEET_SURFACE_USE_H
#include <stdint.h>
#include <stdbool.h>

#define SWEET_USE_STATE_WORDS   0x18u           /* state page offset of the two u32 global counts */
#define SWEET_USE_CATEGORIES    2u
#define SWEET_USE_SATURATE      0x00ffffffu     /* a word never goes above this (a runaway holder is counted, not wrapped) */

enum { SWEET_USE_DISPLAY = 0, SWEET_USE_GPU = 1, SWEET_USE_KINDS = 2 };

/* kern.n2surfuse (boot-arg n2su): 0 = the old no-op traps (holds that exist still drain), 1 = user traps, 2 = + display/GPU */
enum { SWEET_USE_MODE_OFF = 0, SWEET_USE_MODE_USER = 1, SWEET_USE_MODE_ALL = 2 };

struct sweet_use_stats {
	uint64_t user_inc, user_dec;            /* traps that changed a word */
	uint64_t user_inc_dup, user_dec_unmatched;      /* trap 0 for a category this mapping already holds; trap 1 for one it does not */
	uint64_t dropped, dropped_maps;         /* holds cleared by a mapping's release (process gone or surface released while in use) */
	uint64_t kern_inc[SWEET_USE_KINDS], kern_dec[SWEET_USE_KINDS], kern_unmatched[SWEET_USE_KINDS];
	uint64_t saturated, refused_off;        /* increments refused: word at SWEET_USE_SATURATE; mode too low */
	uint64_t not_found;                     /* trap for an id this client has no mapping of */
	uint64_t free_nonzero;                  /* surfaces freed with a word or kernel hold left (accounting bug: must stay 0) */
	int64_t  live;                          /* holds now (all kinds) */
	int64_t  live_kind[SWEET_USE_KINDS];    /* ... of the display / the GPU */
	int64_t  max_live;
	uint32_t max_word;                      /* highest value one word reached */
};

static inline uint32_t sweet_use_load(const volatile uint32_t *w) { return __atomic_load_n(w, __ATOMIC_ACQUIRE); }

static inline void sweet_use_max64(int64_t *m, int64_t v)
{
	int64_t o = __atomic_load_n(m, __ATOMIC_RELAXED);
	while (v > o && !__atomic_compare_exchange_n(m, &o, v, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) { }
}

static inline void sweet_use_max32(uint32_t *m, uint32_t v)
{
	uint32_t o = __atomic_load_n(m, __ATOMIC_RELAXED);
	while (v > o && !__atomic_compare_exchange_n(m, &o, v, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) { }
}

/* +1 on a word unless saturated; returns the new value, 0 when refused. */
static inline uint32_t sweet_use_word_inc(volatile uint32_t *w)
{
	uint32_t o = __atomic_load_n(w, __ATOMIC_RELAXED);
	do {
		if (o >= SWEET_USE_SATURATE) return 0;
	} while (!__atomic_compare_exchange_n(w, &o, o + 1, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
	return o + 1;
}

/* -1 on a word unless it is 0; returns false when it was 0. */
static inline bool sweet_use_word_dec(volatile uint32_t *w)
{
	uint32_t o = __atomic_load_n(w, __ATOMIC_RELAXED);
	do {
		if (o == 0) return false;
	} while (!__atomic_compare_exchange_n(w, &o, o - 1, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
	return true;
}

static inline void sweet_use_live(struct sweet_use_stats *st, int kind, int d)
{
	int64_t v = __atomic_add_fetch(&st->live, d, __ATOMIC_RELAXED);
	if (kind >= 0) __atomic_add_fetch(&st->live_kind[kind], d, __ATOMIC_RELAXED);
	if (d > 0) sweet_use_max64(&st->max_live, v);
}

/*
 * Trap 0 / 1 of one client mapping.  *held = the mapping's bits (bit c = holds category c), changed under the client's lock;
 * words = the surface's two state-page counts.  Returns true when a word changed.
 */
static inline bool sweet_use_user(uint8_t *held, volatile uint32_t *words, uint32_t cat, bool inc, int mode, struct sweet_use_stats *st)
{
	if (cat >= SWEET_USE_CATEGORIES) return false;
	uint8_t bit = (uint8_t)(1u << cat);
	if (inc) {
		if (mode < SWEET_USE_MODE_USER) { __atomic_add_fetch(&st->refused_off, 1, __ATOMIC_RELAXED); return false; }
		if (*held & bit) { __atomic_add_fetch(&st->user_inc_dup, 1, __ATOMIC_RELAXED); return false; }
		uint32_t v = sweet_use_word_inc(&words[cat]);
		if (v == 0) { __atomic_add_fetch(&st->saturated, 1, __ATOMIC_RELAXED); return false; }
		*held |= bit;
		sweet_use_max32(&st->max_word, v);
		__atomic_add_fetch(&st->user_inc, 1, __ATOMIC_RELAXED);
		sweet_use_live(st, -1, 1);
		return true;
	}
	if (!(*held & bit)) {
		if (mode >= SWEET_USE_MODE_USER) __atomic_add_fetch(&st->user_dec_unmatched, 1, __ATOMIC_RELAXED);
		return false;
	}
	*held &= (uint8_t)~bit;
	if (!sweet_use_word_dec(&words[cat])) { __atomic_add_fetch(&st->user_dec_unmatched, 1, __ATOMIC_RELAXED); return false; }
	__atomic_add_fetch(&st->user_dec, 1, __ATOMIC_RELAXED);
	sweet_use_live(st, -1, -1);
	return true;
}

/* A mapping goes away: what it still holds is let go.  Returns the holds dropped. */
static inline unsigned sweet_use_user_drop(uint8_t *held, volatile uint32_t *words, struct sweet_use_stats *st)
{
	unsigned n = 0;
	for (uint32_t c = 0; c < SWEET_USE_CATEGORIES; c++) {
		if (!(*held & (1u << c))) continue;
		if (sweet_use_word_dec(&words[c])) { n++; sweet_use_live(st, -1, -1); }
		else __atomic_add_fetch(&st->user_dec_unmatched, 1, __ATOMIC_RELAXED);
	}
	*held = 0;
	if (n) { __atomic_add_fetch(&st->dropped, n, __ATOMIC_RELAXED); __atomic_add_fetch(&st->dropped_maps, 1, __ATOMIC_RELAXED); }
	return n;
}

/*
 * A kernel holder (display / GPU) takes (+1) or gives back (-1) one hold, in category 0.  kern[kind] = the surface's kernel holds
 * of that kind (a give-back with none is refused and counted: the caller's references and the holds are paired, a mode change in
 * between is the only legal source).  Returns true when the word changed.
 */
static inline bool sweet_use_kern(volatile uint32_t *kern, volatile uint32_t *words, int kind, int delta, int mode, struct sweet_use_stats *st)
{
	if (kind < 0 || kind >= SWEET_USE_KINDS) return false;
	if (delta > 0) {
		if (mode < SWEET_USE_MODE_ALL) { __atomic_add_fetch(&st->refused_off, 1, __ATOMIC_RELAXED); return false; }
		uint32_t v = sweet_use_word_inc(&words[0]);
		if (v == 0) { __atomic_add_fetch(&st->saturated, 1, __ATOMIC_RELAXED); return false; }
		__atomic_add_fetch(&kern[kind], 1, __ATOMIC_ACQ_REL);
		sweet_use_max32(&st->max_word, v);
		__atomic_add_fetch(&st->kern_inc[kind], 1, __ATOMIC_RELAXED);
		sweet_use_live(st, kind, 1);
		return true;
	}
	if (!sweet_use_word_dec(&kern[kind])) {
		/* below mode 2 the holder's give-backs find no hold by design (its takes were refused): not an imbalance */
		if (mode >= SWEET_USE_MODE_ALL) __atomic_add_fetch(&st->kern_unmatched[kind], 1, __ATOMIC_RELAXED);
		return false;
	}
	if (!sweet_use_word_dec(&words[0])) { __atomic_add_fetch(&st->kern_unmatched[kind], 1, __ATOMIC_RELAXED); return false; }
	__atomic_add_fetch(&st->kern_dec[kind], 1, __ATOMIC_RELAXED);
	sweet_use_live(st, kind, -1);
	return true;
}

/* At SweetSurface::free(): every mapping and kernel holder is gone, so both words and the kernel holds must be 0. */
static inline bool sweet_use_free_check(const volatile uint32_t *words, const volatile uint32_t *kern, struct sweet_use_stats *st)
{
	bool bad = sweet_use_load(&words[0]) || sweet_use_load(&words[1]) || sweet_use_load(&kern[0]) || sweet_use_load(&kern[1]);
	if (bad) __atomic_add_fetch(&st->free_nonzero, 1, __ATOMIC_RELAXED);
	return !bad;
}

/*
 * GPU holds by job (Nord2GPU): a fixed table of (job id, surface) recorded at submit and reaped when the job is no longer
 * unfinished.  The surface pointer is opaque here; the caller retains it while it is in the table.
 */
#define SWEET_USE_GPU_SLOTS 256u
struct sweet_use_gpu_slot { uint64_t job; void *surface; };
struct sweet_use_gpu_table {
	struct sweet_use_gpu_slot s[SWEET_USE_GPU_SLOTS];
	uint32_t used;
	uint64_t recorded, reaped, full, dup;
	uint32_t max_used;
};

/* Is surface already held for this job (a submit lists one import per handle; two handles of one surface hold once)? */
static inline bool sweet_use_gpu_has(const struct sweet_use_gpu_table *t, uint64_t job, const void *surface)
{
	for (unsigned i = 0; i < SWEET_USE_GPU_SLOTS; i++) if (t->s[i].surface == surface && t->s[i].job == job) return true;
	return false;
}

/* A free slot for (job, surface): its index, or -1 when full (the hold is then not taken; counted). */
static inline int sweet_use_gpu_put(struct sweet_use_gpu_table *t, uint64_t job, void *surface)
{
	if (job == 0 || surface == NULL) return -1;
	for (unsigned i = 0; i < SWEET_USE_GPU_SLOTS; i++) {
		if (t->s[i].surface != NULL) continue;
		t->s[i].job = job; t->s[i].surface = surface; t->used++; t->recorded++;
		if (t->used > t->max_used) t->max_used = t->used;
		return (int)i;
	}
	t->full++;
	return -1;
}

/*
 * Reap: every slot whose job is not in unfinished[0..n) is emptied and its surface handed to out[] (at most out_cap; what does not
 * fit stays for the next reap).  Returns the number handed out.
 */
static inline unsigned sweet_use_gpu_reap(struct sweet_use_gpu_table *t, const uint64_t *unfinished, unsigned n, void **out, unsigned out_cap)
{
	unsigned k = 0;
	for (unsigned i = 0; i < SWEET_USE_GPU_SLOTS && k < out_cap; i++) {
		if (t->s[i].surface == NULL) continue;
		bool live = false;
		for (unsigned j = 0; j < n && !live; j++) live = unfinished[j] == t->s[i].job;
		if (live) continue;
		out[k++] = t->s[i].surface;
		t->s[i].surface = NULL; t->s[i].job = 0; t->used--; t->reaped++;
	}
	return k;
}
/*
 * Display pool (0394) leak report and admission (0398).  Run fbhwlive (q10jfi, 8 rounds home/photos/cc/switch): the pool filled to
 * 23 surfaces / 247 MiB in round 1 and never freed one; only 7 of the 23 were ever named in a swap (backboardd 2/3/15/51, MobileSlideShow
 * 59/60, 74); 16 (ids 97..124, made in one burst while the app switcher ran) never were -- while SpringBoard alone kept 104 surfaces of
 * 1125x2436 BGRA (1114 MiB, oldest 367 s) alive and mapped by itself.  So the pool is taken by long-lived full-screen surfaces that are
 * never scanned out (switcher snapshots [I]), not by references the kernel forgot.
 *
 * Registry: every live pool surface (not retained; added at attach, removed in SweetSurface::free under the table lock).
 * Classification for the report: young (< age limit old), active (named in a swap within the limit), stale (named before, not since),
 * never (never named in a swap).  Leak candidates = stale + never.
 */
#define SWEET_POOL_REG_MAX 128u
struct sweet_pool_reg { void *s[SWEET_POOL_REG_MAX]; uint32_t n, overflow; };

static inline bool sweet_pool_reg_add(struct sweet_pool_reg *r, void *p)
{
	for (unsigned i = 0; i < SWEET_POOL_REG_MAX; i++) if (r->s[i] == NULL) { r->s[i] = p; r->n++; return true; }
	r->overflow++;
	return false;
}

static inline bool sweet_pool_reg_remove(struct sweet_pool_reg *r, const void *p)
{
	for (unsigned i = 0; i < SWEET_POOL_REG_MAX; i++) if (r->s[i] == p) { r->s[i] = NULL; r->n--; return true; }
	return false;
}

enum { SWEET_POOL_YOUNG = 0, SWEET_POOL_ACTIVE = 1, SWEET_POOL_STALE = 2, SWEET_POOL_NEVER = 3, SWEET_POOL_CLASSES = 4 };

static inline int sweet_pool_classify(uint32_t now, uint32_t born, uint32_t shown_at, uint32_t limit)
{
	if (now - born < limit) return SWEET_POOL_YOUNG;
	if (shown_at == 0) return SWEET_POOL_NEVER;
	return now - shown_at < limit ? SWEET_POOL_ACTIVE : SWEET_POOL_STALE;
}

/* kern.n2fbhw_pool_policy: 0 any creator (0394), 1 not SpringBoard (default: its full-screen surfaces are switcher snapshots that
 * stay alive and are never scanned out), 2 backboardd only (its own framebuffers). */
enum { SWEET_POOL_POLICY_ANY = 0, SWEET_POOL_POLICY_NO_SPRINGBOARD = 1, SWEET_POOL_POLICY_BACKBOARDD = 2 };

static inline bool sweet_pool_name_is(const char *a, const char *b)
{
	while (*a && *a == *b) { a++; b++; }
	return *a == 0 && *b == 0;
}

static inline bool sweet_pool_admit(int policy, const char *proc)
{
	if (policy == SWEET_POOL_POLICY_NO_SPRINGBOARD) return !sweet_pool_name_is(proc, "SpringBoard");
	if (policy == SWEET_POOL_POLICY_BACKBOARDD) return sweet_pool_name_is(proc, "backboardd");
	return true;
}
#endif /* SWEET_SURFACE_USE_H */
