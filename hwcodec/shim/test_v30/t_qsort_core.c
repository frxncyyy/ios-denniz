/* SPDX-License-Identifier: MIT */
/*
 * t_qsort_core.c -- qsort with a REAL guest comparator, executed by V02's interpreter.
 *
 * This is the "needs V02" half of the qsort oracle: the comparator is a Thumb function assembled by llvm-mc
 * (see run_tests_core.sh for the exact command) and executed by emu/n2a32_core.c + emu/n2a32_t16.c.  It is a
 * separate binary and a separate script on purpose: V02 is being edited by another worker, and the acceptance
 * test (run_tests.sh) must not become hostage to that file.  See STATUS.md.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "n2a32.h"
#include "n2elf.h"
#include "n2fmt.h"

/* cmp_u32: ldr r2,[r0]; ldr r3,[r1]; cmp r2,r3; blt -1; bgt 1; 0; bx lr   (llvm-mc, 24 bytes) */
static const uint8_t CMP_U32[] = {
	0x02, 0x68, 0x0b, 0x68, 0x9a, 0x42, 0x02, 0xdb, 0x04, 0xdc, 0x00, 0x20,
	0x70, 0x47, 0x01, 0x20, 0x40, 0x42, 0x70, 0x47, 0x01, 0x20, 0x70, 0x47,
};

static int passed, total;

static void ck(int ok, const char *what)
{
	total++;
	if (ok)
		passed++;
	else
		printf("FAIL: %s\n", what);
}

static const struct n2shim_entry *find(const char *name)
{
	const struct n2shim_entry *e;

	for (e = n2shim_string; e->name; e++)
		if (strcmp(e->name, name) == 0)
			return e;
	return NULL;
}

int main(void)
{
	struct n2a32_mem *m = n2a32_mem_create();
	struct n2a32_cpu c;
	uint32_t code, stack, arr, cmp;
	const struct n2shim_entry *q;
	uint32_t args[4], i;
	uint32_t rng = 0x55aa55aau;

	if (!m) {
		printf("N2TEST V30-CORE FAIL n2a32_mem_create\n");
		return 1;
	}
	code = n2a32_mem_alloc(m, 4096, 4096);
	stack = n2a32_mem_alloc(m, 65536, 4096) + 32768;
	arr = n2a32_mem_alloc(m, 500 * 4, 16);
	memcpy(m->base + code, CMP_U32, sizeof CMP_U32);
	cmp = code | 1;                              /* Thumb entry */

	for (i = 0; i < 500; i++)
		*(uint32_t *)(m->base + arr + 4 * i) = i;
	for (i = 499; i > 0; i--) {
		uint32_t j, x, y;
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		j = rng % (i + 1);
		x = *(uint32_t *)(m->base + arr + 4 * i);
		y = *(uint32_t *)(m->base + arr + 4 * j);
		*(uint32_t *)(m->base + arr + 4 * i) = y;
		*(uint32_t *)(m->base + arr + 4 * j) = x;
	}

	q = find("qsort");
	ck(q != NULL, "qsort is in n2shim_string[]");
	if (!q) {
		printf("N2TEST V30-CORE FAIL no qsort shim\n");
		return 1;
	}
	n2a32_cpu_init(&c, m);
	c.r[13] = stack;
	args[0] = arr;
	args[1] = 500;
	args[2] = 4;
	args[3] = cmp;
	for (i = 0; i < 4; i++)
		c.r[i] = args[i];
	{
		uint64_t before = c.icount;
		q->fn(&c);
		ck(c.stop == N2A32_STOP_RETURN, "the guest call returned normally (N2A32_STOP_RETURN)");
		ck(c.icount > before + 1000, "the interpreter executed the comparator many times");
	}
	{
		int ok = 1;
		for (i = 0; i < 500; i++)
			if (*(uint32_t *)(m->base + arr + 4 * i) != i) {
				ok = 0;
				break;
			}
		ck(ok, "array sorted by the guest comparator");
	}
	/* a second array of signed values, to see the negative result path */
	{
		static const int32_t vals[8] = { 5, -3, 0, 9, -100, 7, 2, -1 };
		for (i = 0; i < 8; i++)
			*(int32_t *)(m->base + arr + 4 * i) = vals[i];
		c.r[0] = arr;
		c.r[1] = 8;
		c.r[2] = 4;
		c.r[3] = cmp;
		q->fn(&c);
		ck(*(int32_t *)(m->base + arr) == -100 && *(int32_t *)(m->base + arr + 28) == 9,
		   "signed values sorted through the guest comparator");
	}

	/* a comparator that cannot run must stop the sort, not make the shim spin or crash */
	{
		for (i = 0; i < 8; i++)
			*(uint32_t *)(m->base + arr + 4 * i) = 8 - i;
		c.r[0] = arr;
		c.r[1] = 8;
		c.r[2] = 4;
		c.r[3] = 0x0000ffffu;            /* unmapped guest address: the core reports a fault */
		q->fn(&c);
		ck(c.stop == N2A32_STOP_FAULT || c.stop == N2A32_STOP_UNDEF || c.stop == N2A32_STOP_NOIMPORT,
		   "a failing comparator stops the sort");
	}
	printf("V30CORE %d/%d\n", passed, total);
	printf("N2TEST V30-CORE %s %d/%d\n", passed == total ? "PASS" : "FAIL", passed, total);
	return passed == total ? 0 : 1;
}
