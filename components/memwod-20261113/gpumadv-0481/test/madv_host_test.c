/*
 * 0481 host test: the MADVISE decisions (nord2_gpu_madv.h) against a small model of the core's buffer table.
 *   unit tests of every rule, then 400000 random operations (ALLOC with the mode on or off, MADVISE, job submit / finish, client RELEASE,
 *   purge passes with random goals, mode switches) with these checks after each one:
 *     - a buffer a job references, an imported one, or one that is not marked is never purged;
 *     - a purged handle answers retained = 0 for ever, a live one retained = 1;
 *     - nothing is marked or purged while kern.n2gpu_madv is 0, and memory that was not allocated purgeable is never marked;
 *     - a pass stops at its goal (it overshoots by less than one buffer) and frees nothing without pressure;
 *     - the marked counter equals the number of marked live buffers.
 * cc -std=c11 -O2 -Wall -Wextra -Werror -o madv_host_test madv_host_test.c && ./madv_host_test
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "../b/iokit/Kernel/arm/nord2_gpu_madv.h"

static int failures;
#define EXPECT(c) do { if (!(c)) { if (failures < 20) printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

#define NBO 48
struct bo { bool live, handle_live, imported, md_purgeable, prepared; uint32_t refs, handle, size; uint8_t flags; };
static struct bo bos[NBO];
static uint32_t next_handle = 0x100, marked;
static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint32_t rnd(uint32_t n) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)((rng >> 20) % n); }

static struct bo *find(uint32_t h) { for (int i = 0; i < NBO; i++) { if (bos[i].live && bos[i].handle_live && bos[i].handle == h) return &bos[i]; } return NULL; }
static void drop(struct bo *b) { if (b->flags & N2MADV_F_DONTNEED) marked--; b->live = false; }
/* the core's release: handle dead at once, memory when no job holds it */
static void release(struct bo *b) { b->handle_live = false; if (--b->refs == 0) drop(b); }

static void
unit(void)
{
	uint8_t f = 0; uint32_t ret = 9; int mark = 9;
	EXPECT(n2madv_markable(true, true, true, false, true));
	EXPECT(!n2madv_markable(false, true, true, false, true) && !n2madv_markable(true, false, true, false, true));       /* heap / wired memory */
	EXPECT(!n2madv_markable(true, true, false, false, true));       /* allocated while the mode was off */
	EXPECT(!n2madv_markable(true, true, true, true, true) && !n2madv_markable(true, true, true, false, false));          /* import, quarantined */
	EXPECT(!n2madv_advise(1, true, true, &f, 2, &ret, &mark) && f == 0);                             /* bad advice */
	EXPECT(n2madv_advise(1, false, false, &f, N2MADV_WILLNEED, &ret, &mark) && ret == 0 && mark == 0);       /* purged handle */
	EXPECT(n2madv_advise(1, false, false, &f, N2MADV_DONTNEED, &ret, &mark) && ret == 0 && mark == 0);
	EXPECT(n2madv_advise(0, true, true, &f, N2MADV_DONTNEED, &ret, &mark) && ret == 1 && mark == 0 && f == 0);     /* mode off: inert */
	EXPECT(n2madv_advise(1, true, false, &f, N2MADV_DONTNEED, &ret, &mark) && ret == 1 && mark == 0 && f == 0);    /* not markable */
	EXPECT(n2madv_advise(1, true, true, &f, N2MADV_DONTNEED, &ret, &mark) && ret == 1 && mark == 1 && f == N2MADV_F_DONTNEED);
	EXPECT(n2madv_advise(1, true, true, &f, N2MADV_DONTNEED, &ret, &mark) && mark == 0 && f == N2MADV_F_DONTNEED);  /* twice: counted once */
	EXPECT(n2madv_purge_ok(f, true, 1, false, true));
	EXPECT(!n2madv_purge_ok(f, true, 2, false, true));               /* a job lists it */
	EXPECT(!n2madv_purge_ok(f, false, 1, false, true));              /* released, waiting for older jobs */
	EXPECT(!n2madv_purge_ok(f, true, 1, true, true) && !n2madv_purge_ok(f, true, 1, false, false));
	EXPECT(!n2madv_purge_ok((uint8_t)(f | N2MADV_F_PURGED), true, 1, false, true));  /* once */
	EXPECT(!n2madv_purge_ok(0, true, 1, false, true));
	EXPECT(n2madv_advise(0, true, true, &f, N2MADV_WILLNEED, &ret, &mark) && ret == 1 && mark == -1 && f == 0);     /* unmark works with the mode off too */
	EXPECT(n2madv_advise(1, true, true, &f, N2MADV_WILLNEED, &ret, &mark) && mark == 0);
	EXPECT(n2madv_goal(1000, 1000, 16384, 1 << 20) == 0 && n2madv_goal(5000, 1000, 16384, 1 << 20) == 0);
	EXPECT(n2madv_goal(900, 1000, 16384, 1 << 20) == 100ull * 16384 + (1 << 20));
	EXPECT(n2madv_default_thr(5088ull << 20, 16384) == 65126);      /* = the governor's low mark at 5088 MiB */
	EXPECT(n2madv_default_thr(3776ull << 20, 16384) == 48332 && n2madv_default_thr(1, 0) == 0);
}

int
main(void)
{
	unit();
	printf("unit tests: %s\n", failures ? "FAILED" : "ok");
	int mode = 1; uint64_t purged_n = 0, purged_bytes = 0, gone_answers = 0, passes = 0;
	static uint32_t dead[4096]; unsigned ndead = 0;      /* handles the kernel purged */
	for (unsigned it = 0; it < 400000; it++) {
		uint32_t op = rnd(100);
		struct bo *b = &bos[rnd(NBO)];
		if (op < 22) {                  /* ALLOC / import */
			if (!b->live) { *b = (struct bo){ .live = true, .handle_live = true, .refs = 1, .handle = next_handle++, .size = (1 + rnd(64)) * 65536u };
				b->imported = rnd(8) == 0; b->prepared = !b->imported && rnd(5) != 0; b->md_purgeable = b->prepared && mode == 1; }
		} else if (op < 50) {           /* MADVISE on a live or a stale handle */
			uint32_t h = rnd(6) == 0 && ndead ? dead[rnd(ndead)] : b->handle, advice = rnd(2), ret = 7; int mark = 0;
			struct bo *t = find(h); uint8_t none = 0;
			bool was = t && (t->flags & N2MADV_F_DONTNEED);
			EXPECT(n2madv_advise(mode, t != NULL, t && n2madv_markable(true, t->prepared, t->md_purgeable, t->imported, true), t ? &t->flags : &none, advice, &ret, &mark));
			marked = (uint32_t)((int)marked + mark);
			EXPECT(ret == (t ? 1u : 0u));
			if (!t) gone_answers++;
			if (t && mark > 0) EXPECT(mode == 1 && t->md_purgeable && !t->imported && !was);
			if (t && (t->flags & N2MADV_F_DONTNEED) && !was) EXPECT(mark == 1);
		} else if (op < 62) {           /* a job takes / drops a reference */
			if (b->live && b->handle_live && rnd(2)) b->refs++;
			else if (b->live && b->refs > (b->handle_live ? 1u : 0u)) { if (--b->refs == 0) drop(b); }
		} else if (op < 72) {           /* client RELEASE */
			if (b->live && b->handle_live) release(b);
		} else if (op < 75) {
			mode = (int)rnd(2);
		} else {                        /* purge pass */
			uint64_t goal = n2madv_goal(rnd(2000), 1000, 16384, 0), freed = 0, last = 0;
			uint32_t before = marked;
			passes++;
			for (int i = 0; i < NBO && freed < goal; i++) {
				struct bo *t = &bos[i];
				if (!t->live || !n2madv_purge_ok(t->flags, t->handle_live, t->refs, t->imported, true)) continue;
				EXPECT(t->refs == 1 && !t->imported && t->md_purgeable && (t->flags & N2MADV_F_DONTNEED));
				t->flags |= N2MADV_F_PURGED;
				if (ndead < 4096) dead[ndead++] = t->handle;
				last = t->size; freed += t->size; purged_n++; purged_bytes += t->size;
				release(t);
				EXPECT(!t->live);       /* refs was 1: gone at once */
			}
			if (goal == 0) EXPECT(freed == 0 && marked == before);
			if (freed > goal) EXPECT(freed - last < goal);
		}
		uint32_t m = 0;
		for (int i = 0; i < NBO; i++) { if (bos[i].live && (bos[i].flags & N2MADV_F_DONTNEED)) m++; }
		EXPECT(m == marked);
		for (int i = 0; i < NBO; i++) { if (bos[i].live && (bos[i].flags & N2MADV_F_DONTNEED)) EXPECT(bos[i].md_purgeable && !bos[i].imported); }
	}
	printf("model: 400000 operations, %llu passes, %llu buffers purged (%llu MiB), %llu retained=0 answers -> %s\n", (unsigned long long)passes,
	    (unsigned long long)purged_n, (unsigned long long)(purged_bytes >> 20), (unsigned long long)gone_answers, failures ? "FAILED" : "ok");
	EXPECT(purged_n > 1000 && gone_answers > 1000);
	printf("%s\n", failures ? "MADV_HOST_TEST FAILED" : "MADV_HOST_TEST OK");
	return failures ? 1 : 0;
}
