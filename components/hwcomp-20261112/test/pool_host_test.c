/*
 * 0394 host test: the display pool allocator (pool-0394/b/iokit/Kernel/arm/sweet_fb_hw_pool_alloc.h, the exact kernel code) and
 * the contract header (../sweet_fb_hw_pool.h).
 *   cc -O2 -Wall -Wextra -Werror -I../pool-0394/b/iokit/Kernel/arm -o pool_host_test pool_host_test.c && ./pool_host_test
 * Checks: contract constants and contains(); eligibility table; first-fit placement, alignment, no overlap, every byte below 4 GiB,
 * counters == reference model over 200k random alloc/free steps (sizes: CA full screen, panel size, landscape, odd big ones);
 * exhaustion -> FAIL (the kernel then uses normal memory) and recovery after frees; bad frees change nothing; 24 x 1125x2436 fit;
 * canaries; a simulated create/release life cycle with the fallback path counted.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sweet_fb_hw_pool_alloc.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; if (fails < 20) printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint64_t rng = 0x123456789abcdefull;
static uint64_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

static uint64_t pitch64(uint32_t w) { return ((uint64_t)w * 4 + 63) & ~63ull; }
static uint64_t page16k(uint64_t b) { return (b + 16383) & ~16383ull; }

/* reference model: owner per granule */
static int owner[SWEET_FB_POOL_NGRAN];

static void
check_model(const struct sweet_fb_pool *p)
{
	uint32_t used = 0;
	for (uint32_t g = 0; g < SWEET_FB_POOL_NGRAN; g++) {
		CHECK(sweet_fb_pool_test(p, g) == (owner[g] != 0));
		used += owner[g] != 0;
	}
	CHECK(used == p->used);
}

int
main(void)
{
	/* contract */
	CHECK(SWEET_FB_HW_POOL_BASE == 0xC0000000ull && SWEET_FB_HW_POOL_SIZE == 0x10000000ull);
	CHECK(SWEET_FB_HW_POOL_BASE + SWEET_FB_HW_POOL_SIZE == 0xD0000000ull);
	CHECK(SWEET_FB_POOL_NGRAN == 4096);
	CHECK(sweet_fb_hw_pool_contains(0xC0000000ull, 1));
	CHECK(sweet_fb_hw_pool_contains(0xC0000000ull, 0x10000000ull));
	CHECK(!sweet_fb_hw_pool_contains(0xC0000000ull, 0x10000001ull));
	CHECK(sweet_fb_hw_pool_contains(0xCFFFFFFFull, 1));
	CHECK(!sweet_fb_hw_pool_contains(0xCFFFFFFFull, 2));
	CHECK(!sweet_fb_hw_pool_contains(0xBFFFFFFFull, 2));
	CHECK(!sweet_fb_hw_pool_contains(0xD0000000ull, 1));
	CHECK(!sweet_fb_hw_pool_contains(0xC0000000ull, 0));
	CHECK(!sweet_fb_hw_pool_contains(0x76e10000ull, 0x1df0000ull));      /* panel carve-out */
	CHECK(!sweet_fb_hw_pool_contains(0x1C0000000ull, 0x1000));            /* above 4 GiB, same low bits */
	CHECK(!sweet_fb_hw_pool_contains(~0ull - 10, 100));                   /* no wrap */

	/* eligibility */
	const uint32_t B = SWEET_FB_POOL_BGRA;
	uint64_t ca = pitch64(1125) * 2436;
	CHECK(pitch64(1125) == 4544 && ca == 11069184);
	CHECK(sweet_fb_pool_eligible(1, 1125, 2436, 4544, 4, 1, 1, B, 0, 0, page16k(ca)) == SWEET_FB_POOL_OK);
	CHECK(sweet_fb_pool_eligible(0, 1125, 2436, 4544, 4, 1, 1, B, 0, 0, page16k(ca)) == SWEET_FB_POOL_NO_OFF);
	CHECK(sweet_fb_pool_eligible(1, 1080, 2400, 4352, 4, 1, 1, B, 0, 0, page16k(4352ull * 2400)) == SWEET_FB_POOL_OK);
	CHECK(sweet_fb_pool_eligible(1, 2436, 1125, (uint32_t)pitch64(2436), 4, 1, 1, B, 0, 0, page16k(pitch64(2436) * 1125)) == SWEET_FB_POOL_OK);
	CHECK(sweet_fb_pool_eligible(1, 1000, 2000, 4000, 4, 1, 1, B, 0, 0, page16k(8000000)) == SWEET_FB_POOL_NO_STRIDE);   /* 4000 % 64 */
	CHECK(sweet_fb_pool_eligible(1, 1000, 2000, 4032, 4, 1, 1, B, 0, 0, page16k(4032ull * 2000)) == SWEET_FB_POOL_OK);
	CHECK(sweet_fb_pool_eligible(1, 999, 2000, 4032, 4, 1, 1, B, 0, 0, page16k(4032ull * 2000)) == SWEET_FB_POOL_NO_SMALL);
	CHECK(sweet_fb_pool_eligible(1, 1125, 1000, 4544, 4, 1, 1, B, 0, 0, page16k(4544ull * 1000)) == SWEET_FB_POOL_NO_SMALL);
	CHECK(sweet_fb_pool_eligible(1, 1125, 2436, 4544, 4, 1, 1, 0x34323076u, 0, 0, page16k(ca)) == SWEET_FB_POOL_NO_FORMAT);   /* 420v */
	CHECK(sweet_fb_pool_eligible(1, 1125, 2436, 4544, 4, 1, 1, 0x52474241u, 0, 0, page16k(ca)) == SWEET_FB_POOL_NO_FORMAT);   /* RGBA */
	CHECK(sweet_fb_pool_eligible(1, 1125, 2436, 4544, 8, 1, 1, B, 0, 0, page16k(ca)) == SWEET_FB_POOL_NO_FORMAT);
	CHECK(sweet_fb_pool_eligible(1, 1125, 2436, 4544, 4, 2, 1, B, 0, 0, page16k(ca)) == SWEET_FB_POOL_NO_FORMAT);
	CHECK(sweet_fb_pool_eligible(1, 1125, 2436, 4544, 4, 1, 1, B, 2, 0, page16k(ca)) == SWEET_FB_POOL_NO_PLANES);
	CHECK(sweet_fb_pool_eligible(1, 1125, 2436, 4480, 4, 1, 1, B, 0, 0, page16k(ca)) == SWEET_FB_POOL_NO_STRIDE);   /* < w*4 */
	CHECK(sweet_fb_pool_eligible(1, 1125, 2436, 1u << 20, 4, 1, 1, B, 0, 0, 0x10000000ull) == SWEET_FB_POOL_NO_STRIDE);
	CHECK(sweet_fb_pool_eligible(1, 1125, 2436, 4544, 4, 1, 1, B, 0, 4096, page16k(ca)) == SWEET_FB_POOL_NO_OFFSET);
	CHECK(sweet_fb_pool_eligible(1, 1125, 2436, 4544, 4, 1, 1, B, 0, 0, 0x10000000ull + 16384) == SWEET_FB_POOL_NO_SIZE);
	CHECK(sweet_fb_pool_eligible(1, 1125, 2436, 4544, 4, 1, 1, B, 0, 0, ca - 64) == SWEET_FB_POOL_NO_SIZE);

	/* 24 CA full-screen surfaces fit, the 25th fails, all disjoint/aligned/below 4 GiB */
	static struct sweet_fb_pool p;
	memset(&p, 0, sizeof(p));
	uint64_t off[32];
	CHECK(sweet_fb_pool_granules(page16k(ca)) == 169);
	for (int i = 0; i < 24; i++) {
		off[i] = sweet_fb_pool_alloc(&p, page16k(ca));
		CHECK(off[i] != SWEET_FB_POOL_FAIL);
		CHECK(off[i] == (uint64_t)i * 169 * SWEET_FB_HW_POOL_GRANULE);     /* first fit packs */
		CHECK(sweet_fb_hw_pool_contains(SWEET_FB_HW_POOL_BASE + off[i], page16k(ca)));
		CHECK(SWEET_FB_HW_POOL_BASE + off[i] + page16k(ca) <= 0x100000000ull);
	}
	CHECK(sweet_fb_pool_alloc(&p, page16k(ca)) == SWEET_FB_POOL_FAIL && p.full == 1);
	CHECK(p.live == 24 && p.live_peak == 24 && p.used == 24 * 169);
	/* free #5 -> the next one lands exactly there (first fit) */
	CHECK(sweet_fb_pool_free(&p, off[5], page16k(ca)));
	CHECK(sweet_fb_pool_alloc(&p, page16k(ca)) == off[5]);
	/* bad frees change nothing */
	struct sweet_fb_pool before = p;
	CHECK(!sweet_fb_pool_free(&p, off[5] + 16384, page16k(ca)));                   /* misaligned */
	CHECK(!sweet_fb_pool_free(&p, SWEET_FB_HW_POOL_SIZE, 65536));                   /* out of range */
	CHECK(!sweet_fb_pool_free(&p, (uint64_t)4095 * 65536, 2 * 65536));             /* runs past the end */
	CHECK(!sweet_fb_pool_free(&p, (uint64_t)24 * 169 * 65536, 65536));             /* never allocated (tail) */
	CHECK(!sweet_fb_pool_free(&p, off[3], 0));
	CHECK(p.bad_free == before.bad_free + 5 && memcmp(p.bits, before.bits, sizeof(p.bits)) == 0 && p.used == before.used);
	/* double free */
	CHECK(sweet_fb_pool_free(&p, off[7], page16k(ca)));
	CHECK(!sweet_fb_pool_free(&p, off[7], page16k(ca)));
	/* small allocation fills the tail hole; zero / oversize fail */
	CHECK(sweet_fb_pool_alloc(&p, 0) == SWEET_FB_POOL_FAIL);
	CHECK(sweet_fb_pool_alloc(&p, SWEET_FB_HW_POOL_SIZE + 1) == SWEET_FB_POOL_FAIL);
	uint64_t tail = sweet_fb_pool_alloc(&p, 16384);
	CHECK(tail == off[7]);          /* first fit: the hole left by #7 comes before the 40-granule tail */
	CHECK(sweet_fb_pool_largest_free(&p) == 168);     /* rest of #7's hole beats the 40-granule tail */

	/* whole pool in one piece, then nothing */
	memset(&p, 0, sizeof(p));
	CHECK(sweet_fb_pool_alloc(&p, SWEET_FB_HW_POOL_SIZE) == 0);
	CHECK(sweet_fb_pool_alloc(&p, 16384) == SWEET_FB_POOL_FAIL);
	CHECK(sweet_fb_pool_free(&p, 0, SWEET_FB_HW_POOL_SIZE) && p.used == 0 && p.live == 0);
	CHECK(sweet_fb_pool_largest_free(&p) == 4096);

	/* random against the reference model */
	memset(&p, 0, sizeof(p));
	memset(owner, 0, sizeof(owner));
	struct { uint64_t off, bytes; int tag; } live[64];
	int nlive = 0, tag = 1;
	uint64_t fallbacks = 0, steps = 200000, worst_frag_fail = 0;
	const uint64_t sizes[] = { page16k(ca), page16k(4352ull * 2400), page16k(pitch64(2436) * 1125), page16k(pitch64(1170) * 2532),
		                   page16k(pitch64(2048) * 2048), 16384, 65536, 65536 + 16384, page16k(pitch64(1000) * 2000) };
	for (uint64_t step = 0; step < steps; step++) {
		bool do_alloc = nlive == 0 || (nlive < 64 && (rnd() % 100) < 55);
		if (do_alloc) {
			uint64_t bytes = sizes[rnd() % (sizeof(sizes) / sizeof(sizes[0]))];
			uint32_t n = sweet_fb_pool_granules(bytes);
			/* does the model have a run of n free granules? */
			uint32_t run = 0, first = ~0u;
			for (uint32_t g = 0; g < SWEET_FB_POOL_NGRAN; g++) {
				run = owner[g] ? 0 : run + 1;
				if (run == n) { first = g + 1 - n; break; }
			}
			uint64_t o = sweet_fb_pool_alloc(&p, bytes);
			if (first == ~0u) {
				CHECK(o == SWEET_FB_POOL_FAIL);
				fallbacks++;
				if (p.used + n <= SWEET_FB_POOL_NGRAN) {
					worst_frag_fail++;      /* enough free bytes, no run: fragmentation (counted, legal) */
				}
				continue;
			}
			CHECK(o == (uint64_t)first * SWEET_FB_HW_POOL_GRANULE);
			CHECK(o % SWEET_FB_HW_POOL_GRANULE == 0 && sweet_fb_hw_pool_contains(SWEET_FB_HW_POOL_BASE + o, bytes));
			for (uint32_t g = first; g < first + n; g++) {
				CHECK(owner[g] == 0);
				owner[g] = tag;
			}
			live[nlive].off = o; live[nlive].bytes = bytes; live[nlive].tag = tag++; nlive++;
		} else {
			int k = (int)(rnd() % (uint64_t)nlive);
			CHECK(sweet_fb_pool_free(&p, live[k].off, live[k].bytes));
			for (uint32_t g = (uint32_t)(live[k].off / SWEET_FB_HW_POOL_GRANULE); g < (live[k].off + live[k].bytes + 65535) / 65536; g++) {
				CHECK(owner[g] == live[k].tag);
				owner[g] = 0;
			}
			live[k] = live[--nlive];
		}
		CHECK(p.live == (uint32_t)nlive);
		if ((step & 1023) == 0) {
			check_model(&p);
		}
	}
	check_model(&p);
	CHECK(p.full == fallbacks);
	CHECK(p.allocs - p.frees == (uint64_t)nlive);

	/* canaries */
	uint64_t head[2];
	for (uint32_t g = 0; g < SWEET_FB_POOL_NGRAN; g += 97) {
		sweet_fb_pool_canary_put(head, g);
		CHECK(sweet_fb_pool_canary_ok(head, g));
		CHECK(!sweet_fb_pool_canary_ok(head, g + 1));
		head[1] ^= 1;
		CHECK(!sweet_fb_pool_canary_ok(head, g));
	}
	memset(head, 0, sizeof(head));
	CHECK(!sweet_fb_pool_canary_ok(head, 0));       /* a zeroed (allocated, then leaked) granule never passes */

	/* life cycle as SweetSurface.cpp drives it: create (eligible -> pool, else / full -> normal), release -> free */
	memset(&p, 0, sizeof(p));
	uint32_t pooled = 0, normal = 0;
	uint64_t ids[40]; int nid = 0;
	for (int i = 0; i < 40; i++) {           /* backboardd + apps: 40 full-screen creations, nothing released */
		uint64_t bytes = page16k(ca);
		if (sweet_fb_pool_eligible(1, 1125, 2436, 4544, 4, 1, 1, B, 0, 0, bytes) == SWEET_FB_POOL_OK &&
		    (ids[nid] = sweet_fb_pool_alloc(&p, bytes)) != SWEET_FB_POOL_FAIL) {
			pooled++; nid++;
		} else {
			normal++;
		}
	}
	CHECK(pooled == 24 && normal == 16 && p.full == 16);
	while (nid > 0) {
		CHECK(sweet_fb_pool_free(&p, ids[--nid], page16k(ca)));
	}
	CHECK(p.used == 0 && p.live == 0 && p.live_peak == 24 && p.used_peak == 24 * 169);

	printf("pool: 24/24 full-screen fit, 16 of 40 fell back; random steps %llu, fallbacks %llu (fragmentation %llu), live at end %d\n",
	    (unsigned long long)steps, (unsigned long long)fallbacks, (unsigned long long)worst_frag_fail, nlive);
	printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
	return fails != 0;
}
