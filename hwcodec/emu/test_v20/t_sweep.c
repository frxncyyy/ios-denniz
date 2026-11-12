/* SPDX-License-Identifier: MIT */
/*
 * t_sweep.c -- random-encoding sweep for package V20 (n2a32_neon_misc and n2a32_neon_ldst).
 *
 * Feeds N pseudo-random words of the two encoding spaces through the class functions.  Requirements:
 * every call returns N2A32_OK or N2A32_UNDEF, never anything else and never crashes; an UNDEF leaves the
 * whole CPU state unchanged; the ldst space only ever sees guest addresses inside the 16 MiB test buffer.
 */
#include "n2a32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMSZ (16u << 20)
#define BASE  0x40000u

/* The MMIO hooks live in the core (V02); this test never marks a megabyte as MMIO, so these are never
 * called, but the link needs them because the load/store helpers reference them. */
uint32_t n2a32_mmio_load(struct n2a32_mem *m, uint32_t a, unsigned bytes)
{
	(void)m; (void)a; (void)bytes;
	return 0;
}

void n2a32_mmio_store(struct n2a32_mem *m, uint32_t a, unsigned bytes, uint32_t v)
{
	(void)m; (void)a; (void)bytes; (void)v;
}

static struct n2a32_cpu cpu;
static struct n2a32_mem m;
static uint8_t *mem;
static uint64_t rng = 0x9E3779B97F4A7C15ull;
static int checks, fails;

static uint32_t rnd(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return (uint32_t)(rng >> 16);
}

int main(int argc, char **argv)
{
	unsigned n = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 0) : 200000;
	unsigned i;

	mem = calloc(1, MEMSZ);
	if (!mem) {
		printf("FAIL calloc\n");
		return 1;
	}
	memset(&m, 0, sizeof m);
	m.base = mem;

	for (i = 0; i < n; i++) {
		struct n2a32_cpu before;
		uint32_t w;
		int r, is_ldst = (int)(i & 1u);

		memset(&cpu, 0, sizeof cpu);
		cpu.mem = &m;
		/* keep every load/store address well inside the test buffer */
		for (unsigned k = 0; k < 16; k++)
			cpu.r[k] = BASE + (rnd() & 0xFFFu);
		if (is_ldst) {
			w = 0xF4000000u | (rnd() & 0x00FFFFFFu);
		} else {
			w = 0xF2000000u | (rnd() & 0x01FFFFFFu);
		}
		if (i % 4u == 0 && !is_ldst)
			w = 0xF2800000u | (rnd() & 0x007FF01Fu);   /* hit the immediate group often */
		before = cpu;
		r = is_ldst ? n2a32_neon_ldst(&cpu, w) : n2a32_neon_misc(&cpu, w);
		checks++;
		if (r != N2A32_OK && r != N2A32_UNDEF) {
			fails++;
			printf("FAIL sweep %08x: result %d\n", w, r);
		} else if (r == N2A32_UNDEF && memcmp(&before, &cpu, sizeof cpu) != 0) {
			fails++;
			printf("FAIL sweep %08x: UNDEF changed the cpu state\n", w);
		}
		if (fails > 20)
			break;
	}
	free(mem);
	printf("t_sweep: %d checks, %d failures\n", checks, fails);
	return fails == 0 ? 0 : 1;
}
