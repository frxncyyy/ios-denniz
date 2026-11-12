/* SPDX-License-Identifier: MIT */
/* t_qsort.c -- qsort with a guest comparator.  V02's interpreter is not linked here (the task allows this stage):
 * n2a32_call() is the test double, and it verifies that the two pointers handed to the comparator are elements of
 * the array.  The comparator function address is a tag the double recognises. */
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "n2a32.h"
#include "n2elf.h"
#include "n2fmt.h"
#include "v30test.h"

#define CMP_TAG 0x51510001u

extern uint32_t (*t_cmp_hook)(struct n2a32_cpu *c, uint32_t a, uint32_t b);

static uint32_t g_base, g_n, g_size;
static uint32_t g_calls, g_bad;
static int g_mode;   /* 0 = u32, 1 = int32 in the first field of an 8-byte element */

static uint32_t cmp_impl(struct n2a32_cpu *c, uint32_t a, uint32_t b)
{
	g_calls++;
	if (a < g_base || b < g_base || (a - g_base) % g_size != 0 || (b - g_base) % g_size != 0 ||
	    (a - g_base) / g_size >= g_n || (b - g_base) / g_size >= g_n) {
		g_bad++;
		return 0;
	}
	if (g_mode == 0) {
		uint32_t x = n2a32_ld32(c, a), y = n2a32_ld32(c, b);
		return x < y ? 0xffffffffu : x > y ? 1u : 0u;
	} else {
		int32_t x = (int32_t)n2a32_ld32(c, a), y = (int32_t)n2a32_ld32(c, b);
		return x < y ? 0xffffffffu : x > y ? 1u : 0u;
	}
}

static uint32_t qsort_guest(uint32_t base, uint32_t n, uint32_t size)
{
	uint32_t args[4];

	args[0] = base;
	args[1] = n;
	args[2] = size;
	args[3] = CMP_TAG;
	return t_call(n2shim_string, "qsort", args, 4, NULL);
}

static uint32_t rng = 0x55aa55aau;

static uint32_t rnd(void)
{
	uint32_t x = rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	rng = x;
	return x;
}

void test_qsort(void)
{
	t_cmp_hook = cmp_impl;
	/* 1000 distinct 32-bit keys, shuffled, sorted by the guest comparator */
	{
		uint32_t base = t_alloc(1000 * 4), i, a;
		struct n2a32_cpu *c = t_cpu();

		for (i = 0; i < 1000; i++)
			n2a32_st32(c, base + 4 * i, i);
		rng = 0x55aa55aau;
		for (i = 999; i > 0; i--) {
			uint32_t j = rnd() % (i + 1);
			uint32_t x = n2a32_ld32(c, base + 4 * i);
			uint32_t y = n2a32_ld32(c, base + 4 * j);
			n2a32_st32(c, base + 4 * i, y);
			n2a32_st32(c, base + 4 * j, x);
		}
		g_base = base;
		g_n = 1000;
		g_size = 4;
		g_mode = 0;
		g_calls = 0;
		g_bad = 0;
		a = qsort_guest(base, 1000, 4);
		T_CHECK(a == base);
		T_CHECK(g_bad == 0);
		T_CHECK(g_calls > 5000 && g_calls < 50000);
		{
			int ok = 1;
			for (i = 0; i < 1000; i++)
				if (n2a32_ld32(c, base + 4 * i) != i) {
					ok = 0;
					break;
				}
			t_checkf(ok, "qsort u32 not sorted at %u", i);
		}
	}
	/* 8-byte elements compared on their first int32 field */
	{
		uint32_t base = t_alloc(300 * 8), i;
		struct n2a32_cpu *c = t_cpu();
		int ok = 1;

		for (i = 0; i < 300; i++) {
			n2a32_st32(c, base + 8 * i, i);
			n2a32_st32(c, base + 8 * i + 4, 0xaaaaaaaa);
		}
		rng = 0x1234abcd;
		for (i = 299; i > 0; i--) {
			uint32_t j = rnd() % (i + 1), k;
			for (k = 0; k < 8; k += 4) {
				uint32_t x = n2a32_ld32(c, base + 8 * i + k);
				uint32_t y = n2a32_ld32(c, base + 8 * j + k);
				n2a32_st32(c, base + 8 * i + k, y);
				n2a32_st32(c, base + 8 * j + k, x);
			}
		}
		g_base = base;
		g_n = 300;
		g_size = 8;
		g_mode = 1;
		g_calls = 0;
		g_bad = 0;
		qsort_guest(base, 300, 8);
		T_CHECK(g_bad == 0);
		for (i = 0; i < 300; i++) {
			if ((int32_t)n2a32_ld32(c, base + 8 * i) != (int32_t)i)
				ok = 0;
			if (n2a32_ld32(c, base + 8 * i + 4) != 0xaaaaaaaau)
				ok = 0;
		}
		T_CHECK(ok);
	}
	/* degenerate sizes: no comparator call, no crash */
	{
		uint32_t base = t_alloc(32), args[4];

		g_calls = 0;
		args[0] = base;
		args[1] = 0;
		args[2] = 4;
		args[3] = CMP_TAG;
		t_call(n2shim_string, "qsort", args, 4, NULL);
		args[1] = 1;
		t_call(n2shim_string, "qsort", args, 4, NULL);
		args[1] = 4;
		args[2] = 0;
		t_call(n2shim_string, "qsort", args, 4, NULL);
		T_CHECK(g_calls == 0);
	}
}
