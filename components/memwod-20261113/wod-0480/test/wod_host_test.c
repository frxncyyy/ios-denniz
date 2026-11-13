/*
 * 0480 host test: the wire-on-demand state machine (sweet_surface_wod.h) under EVERY interleaving of its users.
 *
 * Model (one surface): the descriptor (prepared count, volatile, purged), the hardware that can reach its pages (GPU page tables,
 * display IOMMU entry), and these actors, each step being either one call made under the WOD lock or one descriptor operation made
 * without it, exactly as sweet_surface_wod_kern.inc / sweet_fb_iommu_wod.inc / nord2_gpu_wod.inc do them:
 *   gpu[0..1]   import: blocking hold -> physical lookup + GPU map -> job (lookup again) -> GPU unmap -> unhold
 *   disp        swap: blocking hold -> present thread: never-blocking hold for the IOMMU entry + lookup -> swap released (unhold)
 *   sweep       the idle IOMMU entry: unmap -> unhold
 *   tick        N rounds of tick_begin -> complete() or setPurgeable(Volatile) -> tick_done
 *   client      SetPurgeable requests (Volatile, NonVolatile, ...): begin -> [setPurgeable(NonVolatile)] -> done -> answer
 *   probe       a present-thread lookup with NO swap hold (a layer whose swap could not wire it): never-blocking hold -> lookup -> unhold
 *   vm          purges the object whenever it is volatile and not prepared;  clock: uptime advances;  prepare() may fail once
 * A depth-first search with a visited set walks all reachable states; after every step the invariants I1..I5 of the header are
 * checked, plus: no step is ever taken on unwired pages, no deadlock, and from every final state two late ticks leave the surface
 * unwired with the VM object volatile exactly when the client wants it.  The same search must FAIL for each of the deliberately
 * broken variants (mutants) below, which proves the invariants can see the bugs they are there for.
 *
 * cc -std=c11 -O2 -Wall -Wextra -Werror -o wod_host_test wod_host_test.c && ./wod_host_test
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../b/iokit/Kernel/arm/sweet_surface_wod.h"

#define IDLE_S          2u
#define NGPU            2
#define TICKS           3
#define NREQ            4

static int mutant;      /* 0 = the real functions */

/* ---- the functions under test, or a broken copy ---- */
static int m_tick_begin(struct sweet_wod *w, uint32_t now, uint32_t idle_s, bool in_use, bool gpu_busy)
{
	if (mutant == 1) {      /* MUTANT 1: the tick forgets the holders */
		if (w->state == SWEET_WOD_BUSY || now - w->idle_at < idle_s) { return SWEET_WOD_A_NONE; }
		if (w->state == SWEET_WOD_WIRED) { w->state = SWEET_WOD_BUSY; return SWEET_WOD_A_UNWIRE; }
		return SWEET_WOD_A_NONE;
	}
	if (mutant == 5) {      /* MUTANT 5: volatile although the client took it back (stale want read) */
		if (w->state == SWEET_WOD_UNWIRED && w->holds == 0 && !w->md_volatile && now - w->idle_at >= idle_s) {
			w->state = SWEET_WOD_BUSY; return SWEET_WOD_A_VOLATILE;
		}
	}
	return sweet_wod_tick_begin(w, now, idle_s, in_use, gpu_busy);
}
static bool m_hold_try(struct sweet_wod *w)
{
	if (mutant == 2) { w->holds++; return true; }      /* MUTANT 2: the present thread's hold succeeds on an unwired surface */
	return sweet_wod_hold_try(w);
}
static int m_hold_begin(struct sweet_wod *w)
{
	if (mutant == 3 && w->state == SWEET_WOD_BUSY) {   /* MUTANT 3: a holder does not wait for a transition in progress */
		w->holds++; return SWEET_WOD_A_NONE;
	}
	return sweet_wod_hold_begin(w);
}
static int m_purgeable_begin(struct sweet_wod *w, uint32_t req)
{
	if (mutant == 4 && req == SWEET_WOD_P_NONVOLATILE) {    /* MUTANT 4: NonVolatile only recorded, the object stays volatile */
		if (w->state == SWEET_WOD_BUSY) { return SWEET_WOD_A_WAIT; }
		w->want_volatile = 0; return SWEET_WOD_A_NONE;
	}
	return sweet_wod_purgeable_begin(w, req);
}
#define MUTANTS 6       /* 6: the holder skips setPurgeable(NonVolatile) before prepare() (in holder_step) */

/* ---- model state ---- */
struct holder { uint8_t pc, unvol, was_empty, failed; };
struct st {
	struct sweet_wod w;
	uint8_t prepared, vol, purged;          /* the descriptor / its VM object */
	uint8_t gpu_mapped, iommu_mapped;       /* hardware that can reach the pages */
	uint8_t now;
	struct holder gpu[NGPU], disp;
	uint8_t sweep_pc, probe_pc;
	uint8_t tick_pc, tick_round, tick_act;
	uint8_t cl_pc, cl_i, cl_was_empty;
	uint8_t client_nv;                      /* the client was answered for NonVolatile and has not asked for Volatile since */
	uint8_t lost;                           /* the VM purged the contents and no NonVolatile answer has said Empty yet */
	uint8_t fail_budget, purge_budget, clock_budget;
};
static const uint32_t reqs[NREQ] = { SWEET_WOD_P_VOLATILE, SWEET_WOD_P_NONVOLATILE, SWEET_WOD_P_VOLATILE, SWEET_WOD_P_NONVOLATILE };

static const char *bad;         /* first violated rule */
#define CHECK(c, msg) do { if (!(c) && !bad) { bad = msg; } } while (0)

/* blocking hold: pc 0 begin, 1 [setPurgeable NonVolatile], 2 prepare, 3 done; returns the pc after the hold (4) through *pc */
static bool
holder_step(struct st *s, struct holder *h)
{
	switch (h->pc) {
	case 0: {
		int a = m_hold_begin(&s->w);
		if (a == SWEET_WOD_A_WAIT) { return false; }           /* asleep until the owner's wakeup */
		if (a == SWEET_WOD_A_NONE) { h->pc = 4; return true; }
		h->unvol = s->w.md_volatile; h->pc = h->unvol ? 1 : 2;
		return true;
	}
	case 1:
		if (mutant != 6) { h->was_empty = s->purged; s->vol = 0; s->purged = 0; }
		h->pc = 2;
		return true;
	case 2:
		if (s->fail_budget) { s->fail_budget--; h->failed = 1; h->pc = 3; return true; }       /* prepare() fails */
		CHECK(!s->vol, "I3: prepare() on a volatile object");
		CHECK(s->prepared == 0, "I2: prepared twice");
		s->prepared++;
		h->pc = 3;
		return true;
	case 3:
		(void)sweet_wod_hold_done(&s->w, !h->failed, h->unvol, h->was_empty);
		h->pc = h->failed ? 9 : 4;      /* 9 = finished without a hold */
		return true;
	default:
		return false;
	}
}

static bool
step(struct st *s, int t)
{
	if (t < NGPU) {
		struct holder *h = &s->gpu[t];
		if (h->pc < 4) { return holder_step(s, h); }
		switch (h->pc) {
		case 4: CHECK(s->prepared == 1, "physical lookup (GPU import) of unwired pages"); s->gpu_mapped++; h->pc = 5; return true;
		case 5: CHECK(s->prepared == 1, "GPU job on unwired pages"); h->pc = 6; return true;
		case 6: s->gpu_mapped--; h->pc = 7; return true;        /* core: unmap + flush, before free_owner */
		case 7: CHECK(sweet_wod_unhold(&s->w, s->now), "unhold without a hold"); h->pc = 9; return true;
		default: return false;
		}
	}
	if (t == NGPU) {        /* display */
		struct holder *h = &s->disp;
		if (h->pc < 4) { return holder_step(s, h); }
		switch (h->pc) {
		case 4:         /* present thread: never blocks; wired by the swap's hold, so it must succeed */
			if (m_hold_try(&s->w)) {
				CHECK(s->prepared == 1, "physical lookup (IOMMU map) of unwired pages");
				s->iommu_mapped = 1;
			} else {
				CHECK(0, "the present thread could not take its hold under the swap's hold");
			}
			h->pc = 5;
			return true;
		case 5: CHECK(sweet_wod_unhold(&s->w, s->now), "unhold without a hold"); h->pc = 9; return true;       /* swap released */
		default: return false;
		}
	}
	if (t == NGPU + 1) {    /* IOMMU idle sweep (after the swap went: the entry is not programmed any more) */
		if (s->disp.pc != 9 || !s->iommu_mapped) { return false; }
		if (s->sweep_pc == 0) { s->sweep_pc = 1; return true; }       /* decided under n2io_lock */
		s->iommu_mapped = 0;                                           /* unmap + TLB invalidate ... */
		CHECK(sweet_wod_unhold(&s->w, s->now), "unhold without a hold");       /* ... then the hold */
		s->sweep_pc = 0;
		return true;
	}
	if (t == NGPU + 2) {    /* tick */
		if (s->tick_round >= TICKS) { return false; }
		switch (s->tick_pc) {
		case 0:
			s->tick_act = (uint8_t)m_tick_begin(&s->w, s->now, IDLE_S, false, s->gpu_mapped != 0);
			if (s->tick_act == SWEET_WOD_A_NONE) { s->tick_round++; } else { s->tick_pc = 1; }
			return true;
		case 1:
			if (s->tick_act == SWEET_WOD_A_UNWIRE) {
				CHECK(s->w.holds == 0, "I4: complete() with holders");
				CHECK(s->gpu_mapped == 0 && !s->iommu_mapped, "I4: complete() while the hardware can reach the pages");
				CHECK(s->prepared == 1, "I2: complete() without prepare()");
				if (s->prepared) { s->prepared--; }
			} else {
				CHECK(s->prepared == 0, "I3: volatile while prepared");
				s->vol = 1;
			}
			s->tick_pc = 2;
			return true;
		default:
			sweet_wod_tick_done(&s->w, s->tick_act, true, s->now);
			s->tick_pc = 0; s->tick_round++;
			return true;
		}
	}
	if (t == NGPU + 3) {    /* client SetPurgeable */
		if (s->cl_i >= NREQ) { return false; }
		uint32_t req = reqs[s->cl_i];
		switch (s->cl_pc) {
		case 0: {
			int a = m_purgeable_begin(&s->w, req);
			if (a == SWEET_WOD_A_WAIT) { return false; }
			if (req == SWEET_WOD_P_VOLATILE) { s->client_nv = 0; }
			s->cl_pc = a == SWEET_WOD_A_NONVOLATILE ? 1 : 3;
			return true;
		}
		case 1: s->cl_was_empty = s->purged; s->vol = 0; s->purged = 0; s->cl_pc = 2; return true;
		case 2: sweet_wod_purgeable_done(&s->w, s->cl_was_empty); s->cl_pc = 3; return true;
		default: {
			uint32_t old = sweet_wod_purgeable_answer(&s->w, req, req == SWEET_WOD_P_NONVOLATILE ? SWEET_WOD_P_VOLATILE : SWEET_WOD_P_NONVOLATILE);
			if (req == SWEET_WOD_P_NONVOLATILE) {
				CHECK(!s->vol, "I5: NonVolatile answered while the object is volatile");
				CHECK(!s->lost || old == SWEET_WOD_P_EMPTY, "purged contents not reported as Empty");
				CHECK(s->lost || old != SWEET_WOD_P_EMPTY, "Empty reported for contents that were never purged");
				s->lost = 0; s->client_nv = 1;
			}
			s->cl_pc = 0; s->cl_i++;
			return true;
		}
		}
	}
	if (t == NGPU + 4) {    /* the VM purges a volatile, unwired object */
		if (!s->purge_budget || !s->vol || s->prepared || s->purged) { return false; }
		CHECK(!s->client_nv, "I5: the VM purged an object its client holds NonVolatile");
		s->purged = 1; s->lost = 1; s->purge_budget--;
		return true;
	}
	if (t == NGPU + 5) {    /* clock */
		if (!s->clock_budget) { return false; }
		s->clock_budget--; s->now++;
		return true;
	}
	if (t == NGPU + 6) {    /* probe: may run at any time, at most once */
		if (s->probe_pc == 0) {
			if (m_hold_try(&s->w)) { CHECK(s->prepared == 1, "physical lookup (probe) of unwired pages"); s->probe_pc = 1; } else { s->probe_pc = 2; }
			return true;
		}
		if (s->probe_pc == 1) { CHECK(sweet_wod_unhold(&s->w, s->now), "unhold without a hold"); s->probe_pc = 2; return true; }
		return false;
	}
	return false;
}
#define NACT (NGPU + 7)

static void
invariants(const struct st *s)
{
	CHECK(!(s->w.holds > 0) || (s->w.state == SWEET_WOD_WIRED && s->prepared == 1), "I1: a holder on a surface that is not wired");
	CHECK(s->prepared <= 1, "I2: prepared twice");
	CHECK(s->w.state != SWEET_WOD_WIRED || s->prepared == 1, "I2: WIRED without prepare()");
	CHECK(s->w.state != SWEET_WOD_UNWIRED || s->prepared == 0, "I2: UNWIRED but prepared");
	CHECK(!(s->vol && s->prepared), "I3: volatile and prepared");
	CHECK(!(s->gpu_mapped || s->iommu_mapped) || s->prepared == 1, "hardware mapping of unwired pages");
	CHECK(s->w.state == SWEET_WOD_BUSY || s->w.md_volatile == s->vol, "md_volatile out of step with the VM object");
	CHECK(!(s->client_nv && s->vol), "I5: volatile under a NonVolatile client");
}

/* ---- visited set (open addressing on the raw state bytes) ---- */
static struct st *tab;
static uint8_t *used;
static size_t cap, count;
static uint64_t
hash(const struct st *s)
{
	uint64_t h = 1469598103934665603ull;
	const uint8_t *p = (const uint8_t *)s;
	for (size_t i = 0; i < sizeof(*s); i++) { h = (h ^ p[i]) * 1099511628211ull; }
	return h;
}
static bool
visit(const struct st *s)       /* true = new */
{
	size_t i = hash(s) & (cap - 1);
	while (used[i]) {
		if (memcmp(&tab[i], s, sizeof(*s)) == 0) { return false; }
		i = (i + 1) & (cap - 1);
	}
	used[i] = 1; tab[i] = *s; count++;
	if (count * 2 > cap) { fprintf(stderr, "visited set full\n"); exit(2); }
	return true;
}

static uint64_t finals, steps;
static void
final_check(struct st s)
{
	/* quiescent: every actor done.  Late ticks (time passes) must leave it unwired, volatile iff wanted. */
	for (int k = 0; k < 3 && !bad; k++) {
		s.now = (uint8_t)(s.now + IDLE_S);
		s.tick_round = 0; s.tick_pc = 0;
		while (s.tick_round < 1 && !bad) { (void)step(&s, NGPU + 2); invariants(&s); }
	}
	CHECK(s.w.holds == 0, "final: holds left");
	CHECK(s.w.state == SWEET_WOD_UNWIRED && s.prepared == 0, "final: still wired after the late ticks");
	CHECK(s.vol == s.w.want_volatile, "final: VM object volatility differs from what the client asked");
	finals++;
}

static void
dfs(const struct st *s0)
{
	/* explicit stack */
	static struct st *stack;
	static size_t sp, scap;
	if (!stack) { scap = 1u << 16; stack = malloc(scap * sizeof(*stack)); }
	sp = 0; stack[sp++] = *s0; (void)visit(s0);
	while (sp && !bad) {
		struct st s = stack[--sp];
		bool any = false, all_done = true;
		for (int t = 0; t < NACT && !bad; t++) {
			struct st n = s;
			if (!step(&n, t)) { continue; }
			steps++;
			invariants(&n);
			if (t < NGPU + 4 || t == NGPU + 6) { any = true; }       /* environment steps (purge, clock) are not progress of an actor */
			if (visit(&n)) {
				if (sp == scap) { scap *= 2; stack = realloc(stack, scap * sizeof(*stack)); }
				stack[sp++] = n;
			}
		}
		for (int g = 0; g < NGPU; g++) { all_done = all_done && s.gpu[g].pc == 9; }
		all_done = all_done && s.disp.pc == 9 && !s.iommu_mapped && s.tick_round >= TICKS && s.cl_i >= NREQ && s.probe_pc == 2;
		if (!any && !all_done) { CHECK(0, "deadlock: an actor is blocked for ever"); }
		if (all_done) { final_check(s); }
	}
}

static const char *
run(int m, unsigned fail_budget, uint64_t *states)
{
	mutant = m; bad = NULL; finals = steps = 0;
	memset(used, 0, cap); count = 0;
	struct st s;
	memset(&s, 0, sizeof(s));
	s.w.on = 1; s.w.idle_at = 0;
	s.fail_budget = (uint8_t)fail_budget; s.purge_budget = 2; s.clock_budget = 2 * IDLE_S + 1;
	dfs(&s);
	*states = count;
	return bad;
}

static int failures;
#define EXPECT(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void
unit_tests(void)
{
	/* eligibility: mode, kernel-written surfaces, size */
	EXPECT(!sweet_wod_eligible(0, 1u << 20, false, false, 65536));
	EXPECT(sweet_wod_eligible(1, 1u << 20, false, false, 65536));
	EXPECT(!sweet_wod_eligible(1, 1u << 20, true, false, 65536));          /* 4:2:0 chroma fill */
	EXPECT(!sweet_wod_eligible(1, 1u << 20, false, true, 65536));          /* client memory snapshot */
	EXPECT(!sweet_wod_eligible(1, 65535, false, false, 65536));
	EXPECT(sweet_wod_eligible(1, 65536, false, false, 65536));
	EXPECT(!sweet_wod_eligible(2, 1u << 20, false, false, 65536));         /* unknown modes = off */
	/* a plain life: hold, second holder, release both, idle unwire after idle_s only, volatile after idle_s more */
	struct sweet_wod w = { .on = 1 };
	EXPECT(sweet_wod_hold_begin(&w) == SWEET_WOD_A_WIRE && w.state == SWEET_WOD_BUSY);
	EXPECT(!sweet_wod_hold_try(&w));                                       /* present thread during the wiring: refused */
	EXPECT(sweet_wod_hold_begin(&w) == SWEET_WOD_A_WAIT);
	EXPECT(sweet_wod_tick_begin(&w, 100, 2, false, false) == SWEET_WOD_A_NONE);
	EXPECT(sweet_wod_purgeable_begin(&w, SWEET_WOD_P_VOLATILE) == SWEET_WOD_A_WAIT);
	EXPECT(sweet_wod_hold_done(&w, true, false, false) && w.state == SWEET_WOD_WIRED && w.holds == 1);
	EXPECT(sweet_wod_hold_begin(&w) == SWEET_WOD_A_NONE && w.holds == 2);
	EXPECT(sweet_wod_hold_try(&w) && w.holds == 3);
	EXPECT(sweet_wod_tick_begin(&w, 100, 2, false, false) == SWEET_WOD_A_NONE);     /* held: never unwired */
	EXPECT(sweet_wod_unhold(&w, 10) && sweet_wod_unhold(&w, 11) && sweet_wod_unhold(&w, 12) && w.holds == 0 && w.idle_at == 12);
	EXPECT(!sweet_wod_unhold(&w, 13) && w.holds == 0);                      /* underflow refused, not wrapped */
	EXPECT(sweet_wod_tick_begin(&w, 13, 2, false, false) == SWEET_WOD_A_NONE);      /* idle 1 s < 2 */
	EXPECT(sweet_wod_tick_begin(&w, 14, 2, false, true) == SWEET_WOD_A_NONE);       /* GPU write pending */
	EXPECT(sweet_wod_tick_begin(&w, 14, 2, false, false) == SWEET_WOD_A_UNWIRE && w.state == SWEET_WOD_BUSY);
	sweet_wod_tick_done(&w, SWEET_WOD_A_UNWIRE, true, 14);
	EXPECT(w.state == SWEET_WOD_UNWIRED && !sweet_wod_hold_try(&w));
	EXPECT(sweet_wod_purgeable_begin(&w, SWEET_WOD_P_VOLATILE) == SWEET_WOD_A_NONE && w.want_volatile && !w.md_volatile);
	EXPECT(sweet_wod_tick_begin(&w, 15, 2, false, false) == SWEET_WOD_A_NONE);      /* 1 s after the unwire */
	EXPECT(sweet_wod_tick_begin(&w, 16, 2, true, false) == SWEET_WOD_A_NONE);       /* IOSurfaceIsInUse */
	EXPECT(sweet_wod_tick_begin(&w, 16, 2, false, false) == SWEET_WOD_A_VOLATILE);
	sweet_wod_tick_done(&w, SWEET_WOD_A_VOLATILE, true, 16);
	EXPECT(w.md_volatile && w.state == SWEET_WOD_UNWIRED);
	EXPECT(sweet_wod_tick_begin(&w, 99, 2, false, false) == SWEET_WOD_A_NONE);      /* nothing left to do */
	/* a holder arrives on a volatile, purged surface: non-volatile first, Empty reported once at the next NonVolatile */
	EXPECT(sweet_wod_hold_begin(&w) == SWEET_WOD_A_WIRE && w.md_volatile);
	EXPECT(sweet_wod_hold_done(&w, true, true, true) && !w.md_volatile && w.empty && w.want_volatile);
	EXPECT(sweet_wod_purgeable_answer(&w, SWEET_WOD_P_KEEP, SWEET_WOD_P_VOLATILE) == SWEET_WOD_P_EMPTY && w.empty);
	EXPECT(sweet_wod_purgeable_begin(&w, SWEET_WOD_P_NONVOLATILE) == SWEET_WOD_A_NONE && !w.want_volatile);       /* wired: nothing to clear */
	EXPECT(sweet_wod_purgeable_answer(&w, SWEET_WOD_P_NONVOLATILE, SWEET_WOD_P_VOLATILE) == SWEET_WOD_P_EMPTY && !w.empty);
	EXPECT(sweet_wod_purgeable_answer(&w, SWEET_WOD_P_NONVOLATILE, SWEET_WOD_P_NONVOLATILE) == SWEET_WOD_P_NONVOLATILE);
	/* prepare() failure: no hold, unwired, the next holder tries again */
	struct sweet_wod f = { .on = 1 };
	EXPECT(sweet_wod_hold_begin(&f) == SWEET_WOD_A_WIRE);
	EXPECT(!sweet_wod_hold_done(&f, false, false, false) && f.state == SWEET_WOD_UNWIRED && f.holds == 0);
	EXPECT(sweet_wod_hold_begin(&f) == SWEET_WOD_A_WIRE);
	/* a failed setPurgeable(Volatile) leaves it non-volatile and is tried again */
	struct sweet_wod v = { .on = 1, .want_volatile = 1 };
	EXPECT(sweet_wod_tick_begin(&v, 5, 2, false, false) == SWEET_WOD_A_VOLATILE);
	sweet_wod_tick_done(&v, SWEET_WOD_A_VOLATILE, false, 5);
	EXPECT(!v.md_volatile && v.state == SWEET_WOD_UNWIRED && sweet_wod_tick_begin(&v, 6, 2, false, false) == SWEET_WOD_A_VOLATILE);
	/* SetPurgeable(Empty): contents given up at once */
	struct sweet_wod e = { .on = 1 };
	EXPECT(sweet_wod_purgeable_begin(&e, SWEET_WOD_P_EMPTY) == SWEET_WOD_A_NONE && e.want_volatile && e.empty);
	/* uptime wrap of the idle test (u32 seconds): unsigned difference */
	struct sweet_wod u = { .on = 1, .state = SWEET_WOD_WIRED, .idle_at = 0xfffffffeu };
	EXPECT(sweet_wod_tick_begin(&u, 0xffffffffu, 2, false, false) == SWEET_WOD_A_NONE);
	EXPECT(sweet_wod_tick_begin(&u, 1, 2, false, false) == SWEET_WOD_A_UNWIRE);
	/* IOMMU entry idle rule */
	EXPECT(!sweet_wod_iommu_idle(7, 7, false, 10, 100, 5));         /* looked up in the generation being planned */
	EXPECT(!sweet_wod_iommu_idle(6, 7, true, 10, 100, 5));          /* programmed in an overlay slot */
	EXPECT(!sweet_wod_iommu_idle(6, 7, false, 96, 100, 5));         /* used 4 s ago */
	EXPECT(sweet_wod_iommu_idle(6, 7, false, 95, 100, 5));
}

int
main(void)
{
	cap = 1u << 23;
	tab = malloc(cap * sizeof(*tab)); used = malloc(cap);
	if (!tab || !used) { return 2; }
	unit_tests();
	printf("unit tests: %s\n", failures ? "FAILED" : "ok");
	uint64_t states = 0;
	for (unsigned fb = 0; fb <= 1; fb++) {
		const char *r = run(0, fb, &states);
		printf("model (prepare failures %u): %llu states, %llu steps, %llu final states -> %s\n", fb, (unsigned long long)states,
		    (unsigned long long)steps, (unsigned long long)finals, r ? r : "all invariants hold");
		if (r || finals == 0) { failures++; }
	}
	static const char *what[MUTANTS + 1] = { "", "tick ignores holders", "hold_try succeeds unwired", "holder does not wait for BUSY",
		"NonVolatile only recorded", "tick makes a NonVolatile surface volatile", "holder wires a volatile object" };
	for (int m = 1; m <= MUTANTS; m++) {
		const char *r = run(m, 0, &states);
		printf("mutant %d (%s): %s%s\n", m, what[m], r ? "caught: " : "NOT CAUGHT", r ? r : "");
		if (!r) { failures++; }
	}
	printf("%s\n", failures ? "WOD_HOST_TEST FAILED" : "WOD_HOST_TEST OK");
	return failures ? 1 : 0;
}
