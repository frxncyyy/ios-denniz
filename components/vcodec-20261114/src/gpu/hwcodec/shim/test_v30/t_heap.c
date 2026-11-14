/* SPDX-License-Identifier: MIT */
/*
 * t_heap.c -- the guest heap: 100000 random malloc/calloc/memalign/realloc/free operations with pattern checks,
 * then four host threads doing the same under TSan.
 *
 * The pattern is a byte unique to each allocation; every block is verified before it is freed, so a block that
 * overlaps another one (a bug in the free lists or in the alignment path) is caught.  At the end the heap must
 * report zero live bytes and zero errors.
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>

#include "n2a32.h"
#include "n2elf.h"
#include "n2fmt.h"
#include "v30test.h"

uint64_t n2shim_heap_used(void);
uint64_t n2shim_heap_peak(void);
uint64_t n2shim_heap_allocs(void);
uint64_t n2shim_heap_frees(void);
uint32_t n2shim_heap_errors(void);
uint32_t n2shim_heap_check(struct n2a32_cpu *c);

#define MAXLIVE 2048

struct live {
	uint32_t p;
	uint32_t size;
	uint8_t tag;
};

struct heap_state {
	struct live v[MAXLIVE];
	int n;
	uint8_t next_tag;
	uint32_t ops;
	uint32_t rng;          /* per-state so the four threads never share a generator */
};

static uint32_t hrnd(struct heap_state *st)
{
	uint32_t x = st->rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	st->rng = x;
	return x;
}

static uint32_t hcall(struct n2a32_cpu *c, const char *name, const uint32_t *args, int nargs, uint32_t *r1)
{
	const struct n2shim_entry *e = t_find(n2shim_heap, name);
	int i;

	if (!e) {
		printf("FATAL: no heap shim %s\n", name);
		return 0;
	}
	memset(c->r, 0, sizeof c->r);
	for (i = 0; i < nargs; i++) {
		if (i < 4)
			c->r[i] = args[i];
		else
			n2a32_st32(c, c->r[13] + 4 * (uint32_t)(i - 4), args[i]);
	}
	e->fn(c);
	if (r1)
		*r1 = c->r[1];
	return c->r[0];
}

static uint8_t tag_of(uint32_t p, uint32_t size)
{
	return (uint8_t)(((p >> 4) ^ (size * 7u)) | 1u);
}

static void fill(uint32_t p, uint32_t size, uint8_t tag)
{
	if (size)
		memset(t_mem.base + p, tag, size);
}

static int verify(uint32_t p, uint32_t size, uint8_t tag)
{
	uint32_t i;
	const uint8_t *b = t_mem.base + p;

	if (size <= 8192) {
		for (i = 0; i < size; i++)
			if (b[i] != tag)
				return 0;
		return 1;
	}
	for (i = 0; i < 512; i++)
		if (b[i] != tag || b[size - 1 - i] != tag)
			return 0;
	for (i = 0; i < 64; i++) {
		uint32_t off = (uint32_t)((uint64_t)size * i / 64);
		if (b[off] != tag)
			return 0;
	}
	return 1;
}

static uint32_t gen_size(struct heap_state *st)
{
	uint32_t r = hrnd(st);

	if ((r & 31) == 0)
		return 1 + hrnd(st) % 65536;            /* a few large blocks */
	if ((r & 3) == 0)
		return 1 + hrnd(st) % 8192;
	return 1 + hrnd(st) % 2048;
}

/* One random operation.  Returns 0 on a detected failure (already recorded by the caller's checks). */
static void one_op(struct n2a32_cpu *c, struct heap_state *st)
{
	uint32_t r = hrnd(st);
	uint32_t args[4], p, size;
	uint8_t tag;

	if (st->n == 0 || (r & 3) == 0) {
		/* allocate */
		size = (r & 3) == 0 ? 0 : gen_size(st);
		if (size && (r & 7) == 0) {
			/* memalign */
			uint32_t align = 16u << (hrnd(st) % 8);   /* 16 .. 2048 */
			args[0] = align;
			args[1] = size;
			p = hcall(c, "memalign", args, 2, NULL);
			t_checkf(p != 0 && (p % align) == 0, "memalign(%u,%u) = 0x%x", align, size, p);
		} else if ((r & 7) == 1) {
			uint32_t nm = 1 + hrnd(st) % 8;
			uint32_t sz = 1 + hrnd(st) % 64;
			args[0] = nm;
			args[1] = sz;
			p = hcall(c, "calloc", args, 2, NULL);
			T_CHECK(p != 0);
			{
				uint32_t i, total = nm * sz;
				int zero = 1;
				for (i = 0; i < total; i++)
					if (t_mem.base[p + i] != 0) {
						zero = 0;
						break;
					}
				T_CHECK(zero);
			}
			size = nm * sz;
		} else {
			args[0] = size;
			p = hcall(c, "malloc", args, 1, NULL);
			T_CHECK(p != 0);
		}
		if (!p)
			return;
		if (size >= 16)
			T_CHECK((p % 16) == 0);
		tag = tag_of(p, size);
		fill(p, size, tag);
		if (st->n < MAXLIVE) {
			st->v[st->n].p = p;
			st->v[st->n].size = size;
			st->v[st->n].tag = tag;
			st->n++;
		} else {
			args[0] = p;
			hcall(c, "free", args, 1, NULL);
		}
	} else if ((r & 7) == 2) {
		/* realloc */
		int k = (int)(hrnd(st) % (uint32_t)st->n);
		struct live *l = &st->v[k];
		uint32_t newsize = gen_size(st);
		int ok = verify(l->p, l->size, l->tag);
		t_checkf(ok, "realloc source pattern damaged at 0x%x size %u", l->p, l->size);
		args[0] = l->p;
		args[1] = newsize;
		p = hcall(c, "realloc", args, 2, NULL);
		T_CHECK(p != 0);
		if (!p)
			return;
		{
			uint32_t copy = l->size < newsize ? l->size : newsize;
			uint32_t i;
			int same = 1;
			for (i = 0; i < copy; i++)
				if (t_mem.base[p + i] != l->tag) {
					same = 0;
					break;
				}
			t_checkf(same, "realloc lost contents (0x%x -> 0x%x, %u/%u)", l->p, p, l->size, newsize);
		}
		if (newsize >= 16)
			T_CHECK((p % 16) == 0);
		tag = tag_of(p, newsize);
		fill(p, newsize, tag);
		l->p = p;
		l->size = newsize;
		l->tag = tag;
	} else {
		/* free */
		int k = (int)(hrnd(st) % (uint32_t)st->n);
		struct live *l = &st->v[k];
		int ok = verify(l->p, l->size, l->tag);
		t_checkf(ok, "pattern damaged before free at 0x%x size %u", l->p, l->size);
		args[0] = l->p;
		hcall(c, "free", args, 1, NULL);
		st->v[k] = st->v[st->n - 1];
		st->n--;
	}
	st->ops++;
}

static void drain(struct n2a32_cpu *c, struct heap_state *st)
{
	while (st->n > 0) {
		uint32_t args[1];
		st->n--;
		args[0] = st->v[st->n].p;
		hcall(c, "free", args, 1, NULL);
	}
}

void test_heap(void)
{
	struct heap_state st;
	struct n2a32_cpu *c = t_cpu();
	uint32_t i;

	memset(&st, 0, sizeof st);
	{
		const char *seed = getenv("V30_HEAP_SEED");   /* extra seeds for manual stress runs */
		st.rng = seed ? (uint32_t)strtoul(seed, NULL, 0) : 0x2468ace1u;
	}
	for (i = 0; i < 100000; i++) {
		one_op(c, &st);
		if ((i % 500) == 0) {
			uint32_t bad = n2shim_heap_check(c);
			if (bad) {
				t_checkf(0, "free-list corruption after %u ops (%u bad links)", i, bad);
				break;
			}
		}
	}
	/* every live block still has its own pattern at the end */
	for (i = 0; i < (uint32_t)st.n; i++)
		t_checkf(verify(st.v[i].p, st.v[i].size, st.v[i].tag), "final pattern check 0x%x",
			 st.v[i].p);
	drain(c, &st);
	T_CHECK(n2shim_heap_used() == 0);
	T_CHECK(n2shim_heap_errors() == 0);
	t_checkf(n2shim_heap_allocs() >= 25000 && n2shim_heap_frees() >= 25000, "heap counters %llu/%llu",
		 (unsigned long long)n2shim_heap_allocs(), (unsigned long long)n2shim_heap_frees());
	t_checkf(n2shim_heap_peak() > 0, "peak %llu", (unsigned long long)n2shim_heap_peak());

	/* double free and a bogus pointer are detected, and the heap keeps working */
	{
		uint32_t args[2], p, q;
		uint32_t errors = n2shim_heap_errors();
		args[0] = 64;
		p = hcall(c, "malloc", args, 1, NULL);
		args[0] = p;
		hcall(c, "free", args, 1, NULL);
		hcall(c, "free", args, 1, NULL);
		t_checkf(n2shim_heap_errors() == errors + 1, "double free not detected (%u -> %u)", errors,
			 n2shim_heap_errors());
		args[0] = 0x1234;
		hcall(c, "free", args, 1, NULL);
		t_checkf(n2shim_heap_errors() == errors + 2, "bogus free not detected");
		args[0] = 32;
		q = hcall(c, "malloc", args, 1, NULL);
		T_CHECK(q != 0);
		args[0] = q;
		hcall(c, "free", args, 1, NULL);
		T_CHECK(n2shim_heap_used() == 0);
	}
	/* free(NULL) is a no-op */
	{
		uint32_t args[1] = { 0 };
		hcall(c, "free", args, 1, NULL);
		T_CHECK(n2shim_heap_errors() == 2);
	}
	/* memalign rejects a non-power-of-two alignment and accepts a smaller one (over-aligns) */
	{
		uint32_t args[2] = { 24, 64 };
		T_CHECK(hcall(c, "memalign", args, 2, NULL) == 0);
		args[0] = 8;
		args[1] = 64;
		{
			uint32_t q = hcall(c, "memalign", args, 2, NULL);
			T_CHECK(q != 0 && (q % 8) == 0);
			args[0] = q;
			hcall(c, "free", args, 1, NULL);
		}
	}
	/* posix_memalign writes the pointer and returns 0 */
	{
		uint32_t slot = t_alloc(4);
		uint32_t args[3] = { slot, 64, 100 };
		uint32_t p;
		T_CHECK(hcall(c, "posix_memalign", args, 3, NULL) == 0);
		p = n2a32_ld32(c, slot);
		T_CHECK(p != 0 && (p % 64) == 0);
		args[0] = p;
		hcall(c, "free", args, 1, NULL);
	}
	/* realloc(NULL, n) is malloc; realloc(p, 0) is free + NULL */
	{
		uint32_t args[2] = { 0, 48 }, p;
		p = hcall(c, "realloc", args, 2, NULL);
		T_CHECK(p != 0 && (p % 16) == 0);
		args[0] = p;
		args[1] = 0;
		T_CHECK(hcall(c, "realloc", args, 2, NULL) == 0);
		T_CHECK(n2shim_heap_used() == 0);
	}
	/* calloc overflow and zero sizes */
	{
		uint32_t args[2] = { 0xffffffffu, 2 };
		T_CHECK(hcall(c, "calloc", args, 2, NULL) == 0);
		args[0] = 0;
		args[1] = 5;
		{
			uint32_t p = hcall(c, "calloc", args, 2, NULL);
			T_CHECK(p != 0);
			args[0] = p;
			hcall(c, "free", args, 1, NULL);
		}
	}
	/* an oversized malloc is refused, not wrapped */
	{
		uint32_t args[1] = { 0xffffffffu };
		T_CHECK(hcall(c, "malloc", args, 1, NULL) == 0);
	}
	/* a pointer into the middle of a block is not a block */
	{
		uint32_t args[2] = { 64, 0 }, p;
		uint32_t e0 = n2shim_heap_errors();
		p = hcall(c, "malloc", args, 1, NULL);
		T_CHECK(p != 0);
		args[0] = p + 8;
		hcall(c, "free", args, 1, NULL);
		T_CHECK(n2shim_heap_errors() == e0 + 1);
		args[0] = p;
		hcall(c, "free", args, 1, NULL);
		T_CHECK(n2shim_heap_used() == 0);
	}
	/* opus-enc4k: a recycled large block comes back zeroed (bionic's large allocations are fresh mappings; the vendor's
	 * hevc_enc_init relies on it), a recycled small block is not required to */
	{
		uint32_t args[1] = { 0x5ab10u }, p, q;
		p = hcall(c, "malloc", args, 1, NULL);
		T_CHECK(p != 0);
		if (p) {
			memset(t_mem.base + p, 0xa5, 0x5ab10u);
			args[0] = p;
			hcall(c, "free", args, 1, NULL);
			args[0] = 0x5ab10u;
			q = hcall(c, "malloc", args, 1, NULL);
			T_CHECK(q == p);              /* same size class: the block is recycled */
			int zero = 1;
			for (uint32_t i = 0; q && i < 0x5ab10u; i += 61u) { if (t_mem.base[q + i] != 0) { zero = 0; break; } }
			T_CHECK(zero && t_mem.base[q + 0x58cd8u] == 0);
			args[0] = q;
			hcall(c, "free", args, 1, NULL);
		}
		T_CHECK(n2shim_heap_used() == 0);
	}
	/* the counters are still consistent */
	T_CHECK(n2shim_heap_used() == 0);
}

/* ---- four threads -------------------------------------------------------------------------------------------- */

struct thr_arg {
	unsigned zone;
	uint32_t ops;
	uint32_t failures;
};

static void *thread_body(void *vp)
{
	struct thr_arg *a = (struct thr_arg *)vp;
	struct n2a32_cpu *c = t_cpu_zone(a->zone, NULL);
	struct heap_state st;
	uint32_t i;

	memset(&st, 0, sizeof st);
	{
		const char *seed = getenv("V30_HEAP_SEED");
		uint32_t base = seed ? (uint32_t)strtoul(seed, NULL, 0) : 0x9e3779b9u;
		st.rng = base ^ (a->zone * 2654435761u) ^ 1u;
	}
	for (i = 0; i < a->ops; i++)
		one_op(c, &st);
	for (i = 0; i < (uint32_t)st.n; i++)
		if (!verify(st.v[i].p, st.v[i].size, st.v[i].tag))
			a->failures++;
	drain(c, &st);
	return NULL;
}

void test_heap_threads(void)
{
	pthread_t th[4];
	struct thr_arg a[4];
	uint32_t errors0 = n2shim_heap_errors();
	int i;

	for (i = 0; i < 4; i++) {
		a[i].zone = (unsigned)i + 1;
		a[i].ops = 20000;
		a[i].failures = 0;
		if (pthread_create(&th[i], NULL, thread_body, &a[i]) != 0) {
			t_checkf(0, "pthread_create failed");
			return;
		}
	}
	for (i = 0; i < 4; i++) {
		pthread_join(th[i], NULL);
		t_checkf(a[i].failures == 0, "thread %d saw %u damaged blocks", i, a[i].failures);
	}
	T_CHECK(n2shim_heap_used() == 0);
	t_checkf(n2shim_heap_errors() == errors0, "heap errors changed: %u -> %u", errors0,
		 n2shim_heap_errors());
}
