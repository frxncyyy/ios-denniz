/* SPDX-License-Identifier: MIT */
/*
 * t_fast.c -- differential test of the core's predecoded fast paths and decode cache (opus-perf).
 *
 * The reference is the instruction-class functions (each verified by its own package suite against llvm-mc
 * encodings and architecture literals).  Every case single-steps ONE instruction through n2a32_run() twice
 * from the same random state, in two separate address spaces: once with the fast paths enabled and once with
 * n2a32_core_fastpath = 0 (every instruction through its class function).  Everything architectural must
 * match: r0-r15, NZCVQ/GE, Thumb bit, ITSTATE, D registers, FPSCR, the stop reason/address/encoding, icount
 * and the 64 KiB data region (both copies evolve identically; they are re-synchronised only after a mismatch).
 *
 *   every T16 encoding 0x0000-0xE7FF x STATES16 random states (the last two may start inside an IT block)
 *   T32_RANDOM random 32-bit T32 encodings + T32_TARGET encodings drawn from the fast-path prefixes
 *   A32_RANDOM random A32 words (decode cache only)
 *   the same pc re-used with a different encoding / instruction set every case (cache re-validation)
 *
 * Prints "TESTS passed/total" and exits non-zero on the first few mismatches (each printed).
 */
#define _POSIX_C_SOURCE 200809L
#include "n2a32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern volatile int n2a32_core_fastpath;

typedef int (*class_fn)(struct n2a32_cpu *, uint32_t);
#include "../../emu/n2a32_fast.h"      /* only to report which fast paths the cases covered */
static unsigned long op_hits[256];

#define CODE 0x00010000u
#define DATA 0x00100000u
#define DSIZE 0x10000u

#ifndef STATES16
#define STATES16 4
#endif
#ifndef T32_RANDOM
#define T32_RANDOM 600000
#endif
#ifndef T32_TARGET
#define T32_TARGET 900000
#endif
#ifndef A32_RANDOM
#define A32_RANDOM 100000
#endif

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
	return (uint32_t)(rs >> 16);
}

static struct n2a32_mem *ma, *mb;
static uint8_t data0[DSIZE];
static unsigned long passed, total, failed;

static void random_state(struct n2a32_cpu *s, int thumb, int allow_it)
{
	memset(s, 0, sizeof(*s));
	for (int i = 0; i < 15; i++) {
		uint32_t r = rnd();
		switch (r & 3u) {
		case 0: s->r[i] = rnd(); break;
		case 1: s->r[i] = rnd() & 0xFFu; break;                                   /* small values: offsets, shifts */
		default: s->r[i] = DATA + 0x4000u + (rnd() % (DSIZE - 0x8000u)); break;   /* pointers into the data region */
		}
	}
	s->r[13] = (DATA + 0x4000u + (rnd() % (DSIZE - 0x8000u))) & ~3u;
	s->n = rnd() & 1u; s->z = rnd() & 1u; s->cf = rnd() & 1u; s->v = rnd() & 1u; s->q = rnd() & 1u;
	s->ge = rnd() & 0xFu;
	s->thumb = (uint8_t)thumb;
	if (thumb && allow_it && (rnd() & 3u) == 0) {
		uint8_t it = (uint8_t)rnd();
		if ((it & 0xFu) == 0)
			it |= 8u;
		s->itstate = it;
	}
	for (int i = 0; i < 32; i++)
		s->d[i].u64 = (uint64_t)rnd() << 32 | rnd();
	s->fpscr = rnd() & 0x03C00000u;
}

static void put_insn(struct n2a32_mem *m, uint32_t pc, int thumb, uint32_t raw)
{
	if (!thumb) {
		memcpy(m->base + pc, &raw, 4);
	} else if (raw >= 0xE8000000u) {
		uint16_t h1 = (uint16_t)(raw >> 16), h2 = (uint16_t)raw;
		memcpy(m->base + pc, &h1, 2);
		memcpy(m->base + pc + 2, &h2, 2);
	} else {
		uint16_t h = (uint16_t)raw;
		memcpy(m->base + pc, &h, 2);
	}
}

static void step(struct n2a32_mem *m, struct n2a32_cpu *c, const struct n2a32_cpu *s, uint32_t pc, int fast)
{
	*c = *s;
	c->mem = m;
	c->r[15] = pc;
	c->ilimit = 1;
	n2a32_core_fastpath = fast;
	(void)n2a32_run(c);
}

static int same(const struct n2a32_cpu *a, const struct n2a32_cpu *b)
{
	if (memcmp(a->r, b->r, sizeof(a->r)) || a->n != b->n || a->z != b->z || a->cf != b->cf || a->v != b->v ||
	    a->q != b->q || a->ge != b->ge || a->thumb != b->thumb || a->itstate != b->itstate ||
	    memcmp(a->d, b->d, sizeof(a->d)) || a->fpscr != b->fpscr || a->stop != b->stop ||
	    a->stop_addr != b->stop_addr || a->stop_insn != b->stop_insn || a->icount != b->icount)
		return 0;
	return 1;
}

static void one(uint32_t raw, int thumb, int allow_it)
{
	struct n2a32_cpu s, a, b;
	uint32_t pc = CODE + ((rnd() & 0x3Fu) << (thumb ? 1 : 2));

	random_state(&s, thumb, allow_it);
	if (thumb && !s.itstate) {
		struct dent e;
		memset(&e, 0, sizeof(e));
		if (raw >= 0xE8000000u)
			fast_t32(&e, raw);
		else
			fast_t16(&e, raw);
		op_hits[e.op]++;
	}
	put_insn(ma, pc, thumb, raw);
	put_insn(mb, pc, thumb, raw);
	step(ma, &a, &s, pc, 1);
	step(mb, &b, &s, pc, 0);
	total++;
	if (same(&a, &b) && memcmp(ma->base + DATA, mb->base + DATA, DSIZE) == 0) {
		passed++;
		return;
	}
	failed++;
	if (failed <= 12) {
		printf("MISMATCH %s %08x it=%02x stop %d/%d addr %08x/%08x insn %08x/%08x\n", thumb ? "T" : "A", raw,
		       s.itstate, a.stop, b.stop, a.stop_addr, b.stop_addr, a.stop_insn, b.stop_insn);
		for (int i = 0; i < 16; i++)
			if (a.r[i] != b.r[i])
				printf("   r%-2d in %08x fast %08x class %08x\n", i, s.r[i], a.r[i], b.r[i]);
		printf("   nzcvq fast %u%u%u%u%u class %u%u%u%u%u thumb %u/%u it %02x/%02x mem %s\n", a.n, a.z, a.cf, a.v,
		       a.q, b.n, b.z, b.cf, b.v, b.q, a.thumb, b.thumb, a.itstate, b.itstate,
		       memcmp(ma->base + DATA, mb->base + DATA, DSIZE) ? "DIFFERS" : "same");
	}
	memcpy(ma->base + DATA, data0, DSIZE);
	memcpy(mb->base + DATA, data0, DSIZE);
}

/* fast-path prefixes (hw1 masks) the T32 decoder recognises; the generator also mutates hw2 freely */
static uint32_t t32_target(void)
{
	static const uint16_t pre[] = { 0xF000, 0xF400, 0xF100, 0xF1A0, 0xF1B0, 0xF240, 0xF200, 0xF2A0, 0xF04F,
					0xF06F, 0xF010, 0xF020, 0xF080, 0xF0C0, 0xF1C0, 0xF110,
					0xF8D0, 0xF8C0, 0xF890, 0xF880, 0xF8B0, 0xF8A0, 0xF850, 0xF840, 0xF830,
					0xF820, 0xF810, 0xF800, 0xF990, 0xF9B0, 0xF910, 0xF930, 0xF85F, 0xF8DF,
					0xE9C0, 0xE9D0, 0xE940, 0xE950, 0xE8C0, 0xE8D0, 0xE92D, 0xE8BD, 0xE96D };
	uint32_t hw1 = pre[rnd() % (sizeof(pre) / sizeof(pre[0]))];
	uint32_t hw2 = rnd() & 0xFFFFu;

	if ((hw1 & 0xF800u) == 0xF000u) {
		hw1 |= rnd() & 0x040Fu;                 /* i / S bit area and Rn */
		if ((rnd() & 3u) == 0)
			hw1 |= rnd() & 0x07FFu;         /* branches: all offset bits */
		if ((rnd() & 1u) == 0)
			hw2 |= 0x8000u;                 /* branch / misc control space */
	} else if (hw1 != 0xE92Du && hw1 != 0xE8BDu) {
		hw1 |= rnd() & 0x000Fu;                 /* Rn */
		if ((rnd() & 3u) == 0)
			hw1 |= 0x000Fu;                 /* pc as base: literal forms */
	}
	return hw1 << 16 | hw2;
}

int main(void)
{
	ma = n2a32_mem_create();
	mb = n2a32_mem_create();
	if (!ma || !mb || n2a32_mem_map(ma, CODE, 0x1000u, 1) || n2a32_mem_map(mb, CODE, 0x1000u, 1) ||
	    n2a32_mem_map(ma, DATA, DSIZE, 1) || n2a32_mem_map(mb, DATA, DSIZE, 1)) {
		printf("setup failed\nTESTS 0/1\n");
		return 1;
	}
	for (unsigned i = 0; i < DSIZE; i++)
		data0[i] = (uint8_t)rnd();
	memcpy(ma->base + DATA, data0, DSIZE);
	memcpy(mb->base + DATA, data0, DSIZE);

	for (uint32_t w = 0; w < 0xE800u; w++)
		for (int k = 0; k < STATES16; k++)
			one(w, 1, k >= STATES16 - 2);
	printf("T16 exhaustive: %lu/%lu\n", passed, total);
	for (long k = 0; k < T32_TARGET; k++)
		one(t32_target(), 1, (k & 7) == 0);
	for (long k = 0; k < T32_RANDOM; k++) {
		uint32_t hw1 = 0xE800u + rnd() % 0x1800u;
		one(hw1 << 16 | (rnd() & 0xFFFFu), 1, (k & 7) == 0);
	}
	printf("T32: %lu/%lu cumulative\n", passed, total);
	for (long k = 0; k < A32_RANDOM; k++)
		one(rnd(), 0, 0);
	printf("A32: %lu/%lu cumulative\n", passed, total);

	{
		unsigned uncovered = 0;
		unsigned long on_fast = 0;
		for (int op = 1; op <= F_RSBW_I; op++) {
			on_fast += op_hits[op];
			if (op_hits[op] < 10) {
				printf("fast op %d covered by only %lu cases\n", op, op_hits[op]);
				uncovered++;
			}
		}
		printf("fast-path coverage: %d ops, %u with fewer than 10 cases; %lu thumb cases on a fast path\n",
		       (int)F_RSBW_I, uncovered, on_fast);
		total++;
		if (uncovered == 0)
			passed++;
		else
			failed++;
	}
	n2a32_core_fastpath = 1;
	n2a32_mem_destroy(ma);
	n2a32_mem_destroy(mb);
	printf("TESTS %lu/%lu\n", passed, total);
	return failed ? 1 : 0;
}
