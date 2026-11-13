/*
 * 0417 host test: purge verdict, pressure goal, LRU pick, combined (surfaces + GPU private) budget incl. the GPU side's soft rule,
 * per-process budget table.  Numbers modelled on autocycle/runs/lag1 (q10jgn): surfaces ~1.8 GiB, GPU private 1388 MiB, avail 8-11k
 * pages (16 KiB), Photos 936 surfaces / 1062 MiB of which 521 volatile / 312 MiB.
 * 0418: test_policy (RAM-scaled limits, no refusal while memory is available: r5b Photos case).
 * Build: cc -O2 -Wall -Wextra -I../b/iokit/Kernel/arm purge_host_test.c -o purge_host_test
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "sweet_surface_purge.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails++ < 40) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)
#define MiB (1ull << 20)
#define NOW 300u
#define SINK (16u << 20)

static struct sweet_purge_cand
idle_vol(uint32_t size, uint32_t born, uint32_t shown)
{
	struct sweet_purge_cand c;
	memset(&c, 0, sizeof(c));
	c.size = size; c.born_at = born; c.shown_at = shown; c.purgeable = SWEET_PURGE_VOLATILE; c.creator = 310; c.buffer = true;
	return c;
}

static void
test_verdict(void)
{
	struct sweet_purge_cand c = idle_vol(600 << 10, 100, 0);
	CHECK(sweet_purge_verdict(&c, NOW, 3, true, SINK) == SWEET_PURGE_OK, "idle volatile never shown");
	c.shown_at = 290; CHECK(sweet_purge_verdict(&c, NOW, 3, true, SINK) == SWEET_PURGE_OK, "shown 10 s ago");
	c.shown_at = 298; CHECK(sweet_purge_verdict(&c, NOW, 3, true, SINK) == SWEET_PURGE_NO_ACTIVE, "shown 2 s ago");
	c = idle_vol(600 << 10, 299, 0); CHECK(sweet_purge_verdict(&c, NOW, 3, true, SINK) == SWEET_PURGE_NO_YOUNG, "young");
	c = idle_vol(600 << 10, 100, 0); c.purgeable = 0; CHECK(sweet_purge_verdict(&c, NOW, 3, true, SINK) == SWEET_PURGE_NO_NONVOLATILE, "nonvol");
	c.purgeable = 2; CHECK(sweet_purge_verdict(&c, NOW, 3, true, SINK) == SWEET_PURGE_NO_NONVOLATILE, "empty (0406-evicted) is not volatile");
	c = idle_vol(600 << 10, 100, 0); c.w0 = 1; CHECK(sweet_purge_verdict(&c, NOW, 3, true, SINK) == SWEET_PURGE_NO_USE, "w0");
	c = idle_vol(600 << 10, 100, 0); c.w1 = 1; CHECK(sweet_purge_verdict(&c, NOW, 3, true, SINK) == SWEET_PURGE_NO_USE, "w1");
	c = idle_vol(600 << 10, 100, 0); c.kd = 1; CHECK(sweet_purge_verdict(&c, NOW, 3, true, SINK) == SWEET_PURGE_NO_DISPLAY, "display");
	c = idle_vol(600 << 10, 100, 0); c.kg = 1; CHECK(sweet_purge_verdict(&c, NOW, 3, true, SINK) == SWEET_PURGE_NO_GPU_JOB, "gpu job");
	c = idle_vol(600 << 10, 100, 0); c.imports = 1; CHECK(sweet_purge_verdict(&c, NOW, 3, true, SINK) == SWEET_PURGE_NO_GPU_IMPORT, "import");
	c = idle_vol(600 << 10, 100, 0); c.gpu_busy = true; CHECK(sweet_purge_verdict(&c, NOW, 3, true, SINK) == SWEET_PURGE_NO_GPU_BUSY, "busy");
	c = idle_vol(600 << 10, 100, 0); c.iommu_prog = true; CHECK(sweet_purge_verdict(&c, NOW, 3, true, SINK) == SWEET_PURGE_NO_IOMMU, "iommu");
	c = idle_vol(600 << 10, 100, 0); c.creator = 0; CHECK(sweet_purge_verdict(&c, NOW, 3, true, SINK) == SWEET_PURGE_NO_KERNEL, "kernel");
	c = idle_vol(600 << 10, 100, 0); c.buffer = false; CHECK(sweet_purge_verdict(&c, NOW, 3, true, SINK) == SWEET_PURGE_NO_BUFFER, "pool/purged");
	c = idle_vol(600 << 10, 100, 0); CHECK(sweet_purge_verdict(&c, NOW, 3, false, SINK) == SWEET_PURGE_NO_MODE, "n2surfuse < 2");
	c = idle_vol(SINK + 16384, 100, 0); CHECK(sweet_purge_verdict(&c, NOW, 3, true, SINK) == SWEET_PURGE_NO_SIZE, "bigger than sink");
	c = idle_vol(SINK, 100, 0); CHECK(sweet_purge_verdict(&c, NOW, 3, true, SINK) == SWEET_PURGE_OK, "exactly sink size");
	c = idle_vol(600 << 10, 100, 0); CHECK(sweet_purge_verdict(&c, NOW, 0, true, SINK) == SWEET_PURGE_OK, "idle 0 -> 1");
}

static void
test_goal(void)
{
	uint32_t pg = 16384;
	/* boot: 300 MiB surfaces, 250 GPU, avail 211k: no pressure */
	CHECK(sweet_purge_goal(300 * MiB, 250 * MiB, 211874, pg, 1228 * MiB, 40000, 2240 * MiB, 64 * MiB, 0) == 0, "no pressure at boot");
	CHECK(sweet_purge_goal(300 * MiB, 250 * MiB, 211874, pg, 1228 * MiB, 40000, 2240 * MiB, 64 * MiB, 11 * MiB) == 0, "create 11 MiB, no pressure");
	/* lag1: surfaces 1800, GPU 1388, avail 8227 */
	uint64_t g = sweet_purge_goal(1800 * MiB, 1388 * MiB, 8227, pg, 1228 * MiB, 40000, 2240 * MiB, 64 * MiB, 0);
	uint64_t want_thr = (1800 - 1228 + 64) * MiB, want_av = (uint64_t)(40000 - 8227) * pg + 64 * MiB, want_c = (1800 + 1388 - 2240 + 64) * MiB;
	uint64_t want = want_thr > want_av ? want_thr : want_av; if (want_c > want) want = want_c;
	CHECK(g == want, "lag1 goal %llu want %llu", (unsigned long long)g, (unsigned long long)want);
	CHECK(g >= 900 * MiB, "lag1 goal is the combined overflow (~1 GiB) %llu", (unsigned long long)(g >> 20));
	/* surfaces only over thr */
	g = sweet_purge_goal(1300 * MiB, 100 * MiB, 100000, pg, 1228 * MiB, 40000, 2240 * MiB, 64 * MiB, 0);
	CHECK(g == (1300 - 1228 + 64) * MiB, "thr only %llu", (unsigned long long)(g >> 20));
	/* need bigger than the overflow */
	g = sweet_purge_goal(1229 * MiB, 0, 100000, pg, 1228 * MiB, 40000, 0, 0, 200 * MiB);
	CHECK(g == 201 * MiB, "need dominates %llu", (unsigned long long)(g >> 20));
	/* all rules off */
	CHECK(sweet_purge_goal(5000 * MiB, 5000 * MiB, 0, pg, 0, 0, 0, 64 * MiB, 100 * MiB) == 0, "rules off");
	/* hysteresis larger than thr does not underflow */
	CHECK(sweet_purge_goal(10 * MiB, 0, 100000, pg, 8 * MiB, 0, 0, 64 * MiB, 0) == 10 * MiB, "thr < hyst");
	/* avail exactly at min: no pressure */
	CHECK(sweet_purge_goal(0, 0, 40000, pg, 0, 40000, 0, 64 * MiB, 0) == 0, "avail == min");
}

static void
test_budget(void)
{
	uint64_t L = 2240 * MiB;
	CHECK(sweet_mem_combined_ok(800 * MiB, 1388 * MiB, 50 * MiB, L), "2238 <= 2240");
	CHECK(!sweet_mem_combined_ok(800 * MiB, 1388 * MiB, 53 * MiB, L), "2241 > 2240");
	CHECK(sweet_mem_combined_ok(5000 * MiB, 5000 * MiB, 1, 0), "off");
	CHECK(!sweet_mem_combined_ok(3000 * MiB, 0, 0, L), "surfaces alone over");
	CHECK(!sweet_mem_combined_ok(0, 3000 * MiB, 0, L), "gpu alone over");
	CHECK(!sweet_mem_combined_ok(1000 * MiB, 1000 * MiB, ~0ull, L), "huge request no overflow");
	CHECK(sweet_mem_combined_ok(L, 0, 0, L), "exactly full, zero request");
	/* GPU side, lag1: surfaces 1800 + GPU 1388 = 3188 > 2240; idle volatile 312 MiB does not save it -> refuse */
	CHECK(sweet_mem_gpu_decide(1800 * MiB, 1388 * MiB, 16 * MiB, L, 312 * MiB) == 0, "lag1 gpu refused");
	/* surfaces 900, GPU 1300, request 64: 2264 > 2240, but 312 MiB idle volatile -> 1952: go (kick) */
	CHECK(sweet_mem_gpu_decide(900 * MiB, 1300 * MiB, 64 * MiB, L, 312 * MiB) == 2, "fits after purge");
	CHECK(sweet_mem_gpu_decide(800 * MiB, 1300 * MiB, 64 * MiB, L, 0) == 1, "fits now");
	CHECK(sweet_mem_gpu_decide(100 * MiB, 1300 * MiB, 64 * MiB, L, 500 * MiB) == 1, "idle > wired");
	CHECK(sweet_mem_gpu_decide(5000 * MiB, 5000 * MiB, 64 * MiB, 0, 0) == 1, "off");
	/* per process */
	uint64_t P = 896 * MiB;
	CHECK(sweet_mem_proc_ok(800 * MiB, 96 * MiB, P), "896 fits");
	CHECK(!sweet_mem_proc_ok(800 * MiB, 97 * MiB, P), "897 not");
	CHECK(!sweet_mem_proc_ok(1062 * MiB, 1, P), "Photos at 1062 refused");
	CHECK(sweet_mem_proc_ok(1062 * MiB, 1, 0), "off");
}

static void
test_proc_table(void)
{
	struct sweet_mem_proc t[SWEET_MEM_PROCS];
	memset(t, 0, sizeof(t));
	sweet_mem_proc_add(t, 310, 100 * MiB);
	sweet_mem_proc_add(t, 52, 30 * MiB);
	sweet_mem_proc_add(t, 310, 5 * MiB);
	int i = sweet_mem_proc_find(t, 310, false);
	CHECK(i >= 0 && t[i].bytes == 105 * MiB, "310 = 105");
	sweet_mem_proc_add(t, 310, -(int64_t)(200 * MiB));
	CHECK(t[i].bytes == 0, "clamped at 0");
	CHECK(sweet_mem_proc_find(t, 999, false) < 0, "unknown pid");
	sweet_mem_proc_add(t, 999, -(int64_t)MiB);
	CHECK(sweet_mem_proc_find(t, 999, false) < 0, "a decrement never makes a row");
	sweet_mem_proc_add(t, 0, MiB); sweet_mem_proc_add(t, -1, MiB);
	CHECK(sweet_mem_proc_find(t, 0, false) < 0 && sweet_mem_proc_find(t, -1, true) < 0, "kernel pids untracked");
	/* the emptied row of 310 is reused, the table fills, then a new pid is untracked */
	for (int p = 1000; p < 1000 + SWEET_MEM_PROCS; p++) sweet_mem_proc_add(t, p, MiB);
	int full = 0; for (int k = 0; k < SWEET_MEM_PROCS; k++) full += t[k].bytes != 0;
	CHECK(full == SWEET_MEM_PROCS - 1 + 0 || full == SWEET_MEM_PROCS, "table filled (%d)", full);
	sweet_mem_proc_add(t, 5000, MiB); sweet_mem_proc_add(t, 5001, MiB);
	CHECK(sweet_mem_proc_find(t, 5001, false) < 0, "full table: untracked");
	/* 52 kept its bytes throughout */
	i = sweet_mem_proc_find(t, 52, false);
	CHECK(i >= 0 && t[i].bytes == 30 * MiB, "52 kept");
}

static void
test_pick(void)
{
	/* Photos-like: 521 volatile ~600 KiB, half of them idle; 415 non-volatile 1.8 MiB */
	static struct sweet_purge_cand c[936];
	static uint32_t v[936], out[256];
	uint64_t idle = 0;
	for (int i = 0; i < 936; i++) {
		if (i < 521) c[i] = idle_vol(614400, 10 + (uint32_t)(i * 7 % 200), (i % 3) ? 250 + (uint32_t)(i % 40) : 0);
		else { c[i] = idle_vol(1900544, 10, 0); c[i].purgeable = 0; }
		if (i % 5 == 0 && i < 521) c[i].w0 = 1;     /* some in use */
		v[i] = sweet_purge_verdict(&c[i], NOW, 3, true, SINK);
		if (v[i] == SWEET_PURGE_OK) idle += c[i].size;
	}
	uint64_t freed = 0;
	uint32_t k = sweet_purge_pick(c, v, 936, 100 * MiB, out, 256, &freed);
	CHECK(freed >= 100 * MiB && freed < 100 * MiB + 614400, "goal reached with minimal overshoot: %llu", (unsigned long long)freed);
	uint32_t prev = 0; bool lru = true, okv = true, uniq = true;
	for (uint32_t i = 0; i < k; i++) {
		uint32_t lu = sweet_purge_last_use(&c[out[i]]);
		if (i && lu < prev) lru = false;
		prev = lu;
		if (v[out[i]] != SWEET_PURGE_OK) okv = false;
		for (uint32_t j = 0; j < i; j++) if (out[j] == out[i]) uniq = false;
	}
	CHECK(lru, "oldest first"); CHECK(okv, "only OK verdicts"); CHECK(uniq, "no duplicates");
	/* goal beyond everything: all idle ones (cap 256 permitting) */
	k = sweet_purge_pick(c, v, 936, 10000 * MiB, out, 256, &freed);
	uint32_t nok = 0; for (int i = 0; i < 936; i++) nok += v[i] == SWEET_PURGE_OK;
	CHECK(k == (nok < 256 ? nok : 256), "all idle up to the cap: %u of %u", k, nok);
	CHECK(sweet_purge_pick(c, v, 936, 0, out, 256, &freed) == 0 && freed == 0, "goal 0");
	/* equal last-use ties: every index once */
	struct sweet_purge_cand t[5]; uint32_t tv[5];
	for (int i = 0; i < 5; i++) { t[i] = idle_vol(MiB, 100, 0); tv[i] = SWEET_PURGE_OK; }
	k = sweet_purge_pick(t, tv, 5, 100 * MiB, out, 256, &freed);
	CHECK(k == 5 && out[0] == 0 && out[4] == 4 && freed == 5 * MiB, "ties: %u", k);
	printf("photos model: %llu MiB idle volatile of %u surfaces\n", (unsigned long long)(idle >> 20), 936);
}

static void
test_policy(void)
{
	struct sweet_mem_policy p5, p3;
	sweet_mem_policy_for(5088 * MiB, 16384, &p5);
	sweet_mem_policy_for(3776 * MiB, 16384, &p3);
	printf("5088: surf %llu gpu %llu proc %llu thr %llu comb %llu MiB high %u floor %u\n", (unsigned long long)(p5.surf_cap >> 20),
	    (unsigned long long)(p5.gpu_cap >> 20), (unsigned long long)(p5.proc_cap >> 20), (unsigned long long)(p5.purge_thr >> 20),
	    (unsigned long long)(p5.combined_purge >> 20), p5.avail_high, p5.avail_floor);
	printf("3776: surf %llu gpu %llu proc %llu thr %llu comb %llu MiB high %u floor %u\n", (unsigned long long)(p3.surf_cap >> 20),
	    (unsigned long long)(p3.gpu_cap >> 20), (unsigned long long)(p3.proc_cap >> 20), (unsigned long long)(p3.purge_thr >> 20),
	    (unsigned long long)(p3.combined_purge >> 20), p3.avail_high, p3.avail_floor);
	CHECK(p5.avail_high > 118000 && p5.avail_high < 122000, "5088 high ~120k: %u", p5.avail_high);
	CHECK(p5.avail_floor > 24000 && p5.avail_floor < 26000, "5088 floor ~25k: %u", p5.avail_floor);
	CHECK(p3.surf_cap >= 2048 * MiB && p3.surf_cap < 2100 * MiB, "3776 surf cap ~ old 2048");
	CHECK(p3.purge_thr > 1200 * MiB && p3.purge_thr < 1240 * MiB, "3776 purge thr ~ old 1228");
	CHECK(p3.combined_purge > 2200 * MiB && p3.combined_purge < 2250 * MiB, "3776 combined mark ~ old 2240");
	CHECK(p5.surf_cap > p3.surf_cap && p5.gpu_cap > p3.gpu_cap && p5.proc_cap > p3.proc_cap, "scales with RAM");
	/* r5b: GPU all-private 926 MiB, request 768 KiB, Nord2GPU limit 1536, avail 156,673 -> must go */
	CHECK(sweet_mem_gpu_rule(&p5, 947936ull << 10, 768 << 10, 1536 * MiB, 156673, false) == 1, "r5b gpu allowed");
	CHECK(sweet_mem_gpu_rule(&p5, 947936ull << 10, 768 << 10, 1536 * MiB, 100000, true) == 2, "pressure: allowed + kick");
	CHECK(sweet_mem_gpu_rule(&p5, 947936ull << 10, 768 << 10, 1536 * MiB, 20000, true) == 0, "under floor: refused");
	CHECK(sweet_mem_gpu_rule(&p5, 2030 * MiB, 16 * MiB, 1536 * MiB, 156673, false) == 0, "5088 GPU cap 2035");
	CHECK(sweet_mem_gpu_rule(&p3, 1520 * MiB, 8 * MiB, 1536 * MiB, 156673, false) == 1, "3776: Nord2GPU's 1536 stays the floor of the cap");
	CHECK(sweet_mem_gpu_rule(&p3, 1530 * MiB, 8 * MiB, 1536 * MiB, 156673, false) == 0, "3776: over 1536");
	/* r5b: Photos ~1000 MiB of surfaces, 11 MiB more, avail 156,673 -> must go (0417 refused at 896) */
	CHECK(sweet_mem_surface_rule(&p5, 1000 * MiB, 11239424, 156673), "r5b photos allowed");
	CHECK(sweet_mem_surface_rule(&p3, 1000 * MiB, 11239424, 30000), "3776 above floor allowed");
	CHECK(!sweet_mem_surface_rule(&p3, 1000 * MiB, 11239424, 18000), "3776 under floor refused");
	CHECK(!sweet_mem_surface_rule(&p5, 3050 * MiB, 11239424, 156673), "runaway guard 60 pct");
	/* purge goal with the new marks: r5b has no pressure */
	CHECK(sweet_purge_goal(1306 * MiB, 926 * MiB, 156673, 16384, p5.purge_thr, p5.avail_high, p5.combined_purge, 64 * MiB, 0) == 0, "r5b no purge pressure");
	CHECK(sweet_purge_goal(1306 * MiB, 926 * MiB, 100000, 16384, p5.purge_thr, p5.avail_high, p5.combined_purge, 64 * MiB, 0) > 0, "avail < high: purge");
	struct sweet_mem_policy tiny;
	sweet_mem_policy_for(64 * MiB, 16384, &tiny);
	CHECK(tiny.avail_floor == 8192 && tiny.avail_high >= tiny.avail_floor, "floor minimum");
}

int
main(void)
{
	test_policy();
	test_verdict();
	test_goal();
	test_budget();
	test_proc_table();
	test_pick();
	printf("%d checks, %d failures\n", checks, fails);
	return fails != 0;
}
