/*
 * 0406 host test: the pool eviction decision (sweet_fb_pool_reclaim.h), the victim pick against the real 0394 allocator, and the
 * grace rule for giving an evicted range back.  Rows modelled on night18 (q10jgb mode 3, autocycle/runs/night18/ui-usb/live10.log).
 * Build: cc -O2 -Wall -Wextra -I../b/iokit/Kernel/arm -I../../hwcomp-20261112/pool-0394/patched-sources-0394/iokit/Kernel/arm reclaim_host_test.c -o reclaim_host_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sweet_fb_pool_reclaim.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails++ < 40) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

#define SZ 11239424ull          /* 1125x2436 bpr 4608 */
#define NOW 3600u

static struct sweet_reclaim_cand
row(int32_t creator, uint32_t born, uint32_t shown, uint32_t purg, uint32_t w0, uint32_t w1, uint32_t kd, uint32_t kg, uint32_t imp)
{
	struct sweet_reclaim_cand c;
	memset(&c, 0, sizeof(c));
	c.creator = creator; c.born_at = born; c.shown_at = shown; c.purgeable = purg;
	c.w0 = w0; c.w1 = w1; c.kd = kd; c.kg = kg; c.imports = imp; c.pool = true; c.ngran = sweet_fb_pool_granules(SZ);
	return c;
}

static void
test_verdict(void)
{
	struct sweet_reclaim_cand c;
	/* night18 MobileSlideShow id 56: stale, mappings 2, use 0/0, no holds -- non-volatile: kept (its contents may be shown again) */
	c = row(270, NOW - 113, NOW - 99, SWEET_PURGEABLE_NONVOLATILE, 0, 0, 0, 0, 0);
	CHECK(sweet_reclaim_verdict(&c, NOW, 30, true, false) == SWEET_RECLAIM_NO_NONVOLATILE, "stale non-volatile must stay");
	c.purgeable = SWEET_PURGEABLE_VOLATILE;
	CHECK(sweet_reclaim_verdict(&c, NOW, 30, true, false) == SWEET_RECLAIM_OK, "stale volatile idle = evictable");
	c.purgeable = SWEET_PURGEABLE_EMPTY;
	CHECK(sweet_reclaim_verdict(&c, NOW, 30, true, false) == SWEET_RECLAIM_NO_NONVOLATILE, "Empty is not volatile");
	/* backboardd never-shown id 140, volatile */
	c = row(59, NOW - 95, 0, SWEET_PURGEABLE_VOLATILE, 0, 0, 0, 0, 0);
	CHECK(sweet_reclaim_verdict(&c, NOW, 30, true, false) == SWEET_RECLAIM_OK, "never shown volatile idle = evictable");
	/* SubwaySurf 161 while alive: use 1/0, gpu import 1 */
	c = row(295, NOW - 48, NOW - 44, SWEET_PURGEABLE_VOLATILE, 1, 0, 0, 0, 1);
	CHECK(sweet_reclaim_verdict(&c, NOW, 30, true, false) == SWEET_RECLAIM_NO_USE, "in use");
	c.w0 = 0;
	CHECK(sweet_reclaim_verdict(&c, NOW, 30, true, false) == SWEET_RECLAIM_NO_GPU_IMPORT, "gpu import");
	c.imports = 0; c.kg = 1;
	CHECK(sweet_reclaim_verdict(&c, NOW, 30, true, false) == SWEET_RECLAIM_NO_GPU_JOB, "gpu job");
	c.kg = 0; c.kd = 1;
	CHECK(sweet_reclaim_verdict(&c, NOW, 30, true, false) == SWEET_RECLAIM_NO_DISPLAY, "display hold");
	c.kd = 0; c.gpu_busy = true;
	CHECK(sweet_reclaim_verdict(&c, NOW, 30, true, false) == SWEET_RECLAIM_NO_GPU_BUSY, "gpu busy");
	c.gpu_busy = false;
	CHECK(sweet_reclaim_verdict(&c, NOW, 30, true, false) == SWEET_RECLAIM_OK, "all clear");
	CHECK(sweet_reclaim_verdict(&c, NOW, 30, false, false) == SWEET_RECLAIM_NO_MODE, "n2surfuse < 2: holds not counted");
	CHECK(sweet_reclaim_verdict(&c, NOW, 30, true, true) == SWEET_RECLAIM_NO_IOMMU, "iommu on");
	c.pending = true;
	CHECK(sweet_reclaim_verdict(&c, NOW, 30, true, false) == SWEET_RECLAIM_NO_PENDING, "pending");
	c.pending = false; c.pool = false;
	CHECK(sweet_reclaim_verdict(&c, NOW, 30, true, false) == SWEET_RECLAIM_NO_POOL, "not pool");
	/* young / active / kernel */
	c = row(270, NOW - 10, 0, SWEET_PURGEABLE_VOLATILE, 0, 0, 0, 0, 0);
	CHECK(sweet_reclaim_verdict(&c, NOW, 30, true, false) == SWEET_RECLAIM_NO_YOUNG, "young");
	c = row(270, NOW - 100, NOW - 5, SWEET_PURGEABLE_VOLATILE, 0, 0, 0, 0, 0);
	CHECK(sweet_reclaim_verdict(&c, NOW, 30, true, false) == SWEET_RECLAIM_NO_ACTIVE, "shown 5 s ago");
	CHECK(sweet_reclaim_verdict(&c, NOW, 0, true, false) == SWEET_RECLAIM_OK, "age limit 0 treated as 1 s");
	c = row(0, NOW - 1000, 0, SWEET_PURGEABLE_VOLATILE, 0, 0, 0, 0, 0);
	CHECK(sweet_reclaim_verdict(&c, NOW, 30, true, false) == SWEET_RECLAIM_NO_KERNEL, "kernel-made (test pattern)");
	/* exhaustive: any hold / non-volatile / young / active => never OK */
	unsigned ok_seen = 0;
	for (unsigned m = 0; m < (1u << 10); m++) {
		c = row((m & 1) ? 0 : 77, (m & 2) ? NOW - 5 : NOW - 500, (m & 4) ? NOW - 3 : 0, (m & 8) ? 0 : 1, (m >> 4) & 1, (m >> 5) & 1,
		    (m >> 6) & 1, (m >> 7) & 1, (m >> 8) & 1);
		c.gpu_busy = (m >> 9) & 1;
		bool ok = sweet_reclaim_verdict(&c, NOW, 30, true, false) == SWEET_RECLAIM_OK;
		ok_seen += ok;
		CHECK(ok == (m == 0), "exhaustive mask %#x -> %d", m, ok);
	}
	CHECK(ok_seen == 1, "exactly one clear combination");
}

/* night18 layout: 23 surfaces of SZ allocated first-fit -> 247 MiB used, an allocation of SZ fails */
static void
fill(struct sweet_fb_pool *p, uint64_t *off, int n)
{
	memset(p, 0, sizeof(*p));
	for (int i = 0; i < n; i++) {
		off[i] = sweet_fb_pool_alloc(p, SZ);
	}
}

static void
test_pick(void)
{
	struct sweet_fb_pool pool, sim;
	uint64_t off[23];
	struct sweet_reclaim_cand c[23];
	uint32_t v[23], out[8];
	bool fits;
	fill(&pool, off, 23);
	CHECK(off[22] != SWEET_FB_POOL_FAIL && (pool.used * 64) >> 10 == 247, "night18 fill: used %u MiB", (pool.used * 64) >> 10);
	sim = pool;
	CHECK(sweet_fb_pool_alloc(&sim, SZ) == SWEET_FB_POOL_FAIL, "24th must fail as on the phone");
	for (int i = 0; i < 23; i++) {
		c[i] = row(270, NOW - 500, NOW - 400 + (uint32_t)i, SWEET_PURGEABLE_NONVOLATILE, 0, 0, 0, 0, 0);
		c[i].g0 = (uint32_t)(off[i] / SWEET_FB_HW_POOL_GRANULE);
		v[i] = sweet_reclaim_verdict(&c[i], NOW, 30, true, false);
	}
	/* all non-volatile (what night18 would most likely be): no victim */
	sim = pool;
	CHECK(sweet_reclaim_pick(&sim, c, v, 23, SZ, out, 8, &fits) == 0 && !fits, "nothing volatile -> no eviction");
	/* three volatile: 5, 9, 17 with last use 17 < 5 < 9 -> only the LRU one (17) is taken, and the allocation then fits */
	c[5].purgeable = c[9].purgeable = c[17].purgeable = SWEET_PURGEABLE_VOLATILE;
	c[17].shown_at = NOW - 3000;
	for (int i = 0; i < 23; i++) v[i] = sweet_reclaim_verdict(&c[i], NOW, 30, true, false);
	sim = pool;
	uint32_t k = sweet_reclaim_pick(&sim, c, v, 23, SZ, out, 8, &fits);
	CHECK(k == 1 && out[0] == 17, "LRU single victim: k %u out0 %u", k, out[0]);
	/* model the kernel: give the victim's range back, then the real allocator must succeed exactly there */
	struct sweet_fb_pool after = pool;
	CHECK(sweet_fb_pool_free(&after, off[out[0]], SZ), "free victim");
	CHECK(sweet_fb_pool_alloc(&after, SZ) == off[17], "allocation lands in the evicted range");
	/* a held victim is never picked: give 17 a display hold */
	c[17].kd = 1;
	for (int i = 0; i < 23; i++) v[i] = sweet_reclaim_verdict(&c[i], NOW, 30, true, false);
	sim = pool;
	k = sweet_reclaim_pick(&sim, c, v, 23, SZ, out, 8, &fits);
	CHECK(k == 1 && out[0] == 5, "held LRU skipped -> next LRU (5): k %u out0 %u", k, out[0]);
	c[17].kd = 0;
	/* bigger request than one victim: two adjacent volatile neighbours needed, a lone one elsewhere does not help */
	for (int i = 0; i < 23; i++) { c[i].purgeable = SWEET_PURGEABLE_NONVOLATILE; }
	c[3].purgeable = c[10].purgeable = c[11].purgeable = SWEET_PURGEABLE_VOLATILE;
	c[3].shown_at = 1;              /* LRU but alone: taken first, then pruned */
	for (int i = 0; i < 23; i++) v[i] = sweet_reclaim_verdict(&c[i], NOW, 30, true, false);
	sim = pool;
	k = sweet_reclaim_pick(&sim, c, v, 23, 2 * SZ, out, 8, &fits);
	CHECK(k == 2 && out[0] == 10 && out[1] == 11, "2*SZ: lone LRU 3 pruned, 10 + 11 (contiguous) -> k %u", k);
	struct sweet_fb_pool chk = pool;
	for (uint32_t i = 0; i < k; i++) sweet_fb_pool_free(&chk, off[out[i]], SZ);
	CHECK(sweet_fb_pool_alloc(&chk, 2 * SZ) != SWEET_FB_POOL_FAIL, "2*SZ fits after the picked evictions");
	/* impossible: only lone volatile ones for 2*SZ -> evict nothing */
	c[11].purgeable = SWEET_PURGEABLE_NONVOLATILE;
	for (int i = 0; i < 23; i++) v[i] = sweet_reclaim_verdict(&c[i], NOW, 30, true, false);
	sim = pool;
	CHECK(sweet_reclaim_pick(&sim, c, v, 23, 2 * SZ, out, 8, &fits) == 0 && !fits, "no fit possible -> no victim");
	/* pending ranges already cleared in sim: fits now, nothing more evicted */
	sim = pool;
	sweet_reclaim_clear(&sim, c[7].g0, c[7].ngran);
	CHECK(sweet_reclaim_pick(&sim, c, v, 23, SZ, out, 8, &fits) == 0 && fits, "pending range suffices");
	/* randomized: picks are always OK-verdict, distinct, and the request fits after freeing exactly them */
	srand(406);
	for (int it = 0; it < 20000; it++) {
		int n = 1 + rand() % 23;
		fill(&pool, off, n);
		for (int i = 0; i < n; i++) {
			c[i] = row(rand() % 4 ? 300 : 0, NOW - (uint32_t)(rand() % 200), rand() % 3 ? NOW - (uint32_t)(rand() % 200) : 0,
			    (uint32_t)(rand() % 3), rand() % 5 == 0, rand() % 7 == 0, rand() % 6 == 0, rand() % 9 == 0, rand() % 5 == 0);
			c[i].g0 = (uint32_t)(off[i] / SWEET_FB_HW_POOL_GRANULE);
			v[i] = sweet_reclaim_verdict(&c[i], NOW, 30, true, false);
		}
		uint64_t need = SZ * (uint64_t)(1 + rand() % 3);
		sim = pool;
		k = sweet_reclaim_pick(&sim, c, v, (uint32_t)n, need, out, 8, &fits);
		struct sweet_fb_pool t = pool;
		for (uint32_t i = 0; i < k; i++) {
			CHECK(v[out[i]] == SWEET_RECLAIM_OK, "picked a kept surface (it %d)", it);
			for (uint32_t j = 0; j < i; j++) CHECK(out[i] != out[j], "duplicate victim");
			CHECK(sweet_fb_pool_free(&t, off[out[i]], SZ), "free picked");
		}
		uint64_t got = sweet_fb_pool_alloc(&t, need);
		if (k > 0 || fits) CHECK(got != SWEET_FB_POOL_FAIL, "pick says fit but alloc fails (it %d k %u)", it, k);
		/* minimality: dropping any one victim must not fit */
		for (uint32_t d = 0; d < k; d++) {
			struct sweet_fb_pool m = pool;
			for (uint32_t i = 0; i < k; i++) if (i != d) sweet_fb_pool_free(&m, off[out[i]], SZ);
			CHECK(sweet_fb_pool_alloc(&m, need) == SWEET_FB_POOL_FAIL, "victim %u was unnecessary (it %d)", d, it);
		}
	}
}

static void
test_grace(void)
{
	/* evicted at 100, grace 2, shown_at 50 at eviction */
	CHECK(sweet_reclaim_grace(101, 100, 2, 50, 50, 0, 0, 0, 1) == SWEET_RECLAIM_WAIT, "inside grace");
	CHECK(sweet_reclaim_grace(102, 100, 2, 50, 50, 0, 0, 0, 1) == SWEET_RECLAIM_GIVE, "grace over, nothing left");
	CHECK(sweet_reclaim_grace(500, 100, 2, 50, 50, 0, 0, 0, 2) == SWEET_RECLAIM_WAIT, "a mapping of the old range is left (remap failed)");
	CHECK(sweet_reclaim_grace(500, 100, 2, 50, 50, 1, 0, 0, 1) == SWEET_RECLAIM_WAIT, "display hold");
	CHECK(sweet_reclaim_grace(500, 100, 2, 50, 50, 0, 1, 0, 1) == SWEET_RECLAIM_WAIT, "gpu job hold");
	CHECK(sweet_reclaim_grace(500, 100, 2, 50, 50, 0, 0, 1, 1) == SWEET_RECLAIM_WAIT, "gpu import");
	CHECK(sweet_reclaim_grace(500, 100, 2, 50, 101, 0, 0, 0, 1) == SWEET_RECLAIM_REARM, "swapped after eviction: re-arm");
	CHECK(sweet_reclaim_grace(102, 101, 2, 101, 101, 0, 0, 0, 1) == SWEET_RECLAIM_WAIT, "re-armed: new grace");
	CHECK(sweet_reclaim_grace(103, 101, 2, 101, 101, 0, 0, 0, 1) == SWEET_RECLAIM_GIVE, "re-armed grace over");
	CHECK(sweet_reclaim_grace(100, 100, 0, 50, 50, 0, 0, 0, 1) == SWEET_RECLAIM_WAIT, "grace 0 is at least 1 s");
}

int
main(void)
{
	test_verdict();
	test_pick();
	test_grace();
	printf("%d checks, %s (%d failures)\n", checks, fails ? "FAILED" : "ALL OK", fails);
	return fails != 0;
}
