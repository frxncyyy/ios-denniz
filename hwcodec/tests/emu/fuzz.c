/* SPDX-License-Identifier: MIT */
/*
 * fuzz.c -- feed random A32 words and T32 units through the interpreter.
 *
 * One mapped scratch megabyte holds random instruction bytes.  Each trial starts at a random
 * even/word address in it with random registers and flags, and runs with a small instruction
 * budget so a branch into a loop cannot hang the process.  The interesting outcomes are:
 *   undef  the class rejected the encoding (N2A32_STOP_UNDEF) -- expected for random data;
 *   fault  a memory access hit an unmapped guest page (N2A32_STOP_FAULT);
 *   ok     the core stopped deliberately: RETURN, LIMIT, SVC or BKPT.
 * Anything else (NONE, REQUEST, NOIMPORT, or an icount bigger than the budget) is a failure.
 * ASan/UBSan turn any host memory error or undefined operation into a failed exit.
 */
#include "n2a32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCRATCH_BASE 0x20000000u
#define SCRATCH_LEN  0x00100000u
#define BUDGET       24u

static uint64_t rng_state = 0x243f6a8885a308d3ULL;

static uint64_t rnd64(void)
{
	uint64_t x = rng_state;

	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	rng_state = x;
	return x;
}

static const char *stop_name(enum n2a32_stop s)
{
	switch (s) {
	case N2A32_STOP_NONE: return "NONE";
	case N2A32_STOP_RETURN: return "RETURN";
	case N2A32_STOP_UNDEF: return "UNDEF";
	case N2A32_STOP_FAULT: return "FAULT";
	case N2A32_STOP_SVC: return "SVC";
	case N2A32_STOP_BKPT: return "BKPT";
	case N2A32_STOP_LIMIT: return "LIMIT";
	case N2A32_STOP_REQUEST: return "REQUEST";
	case N2A32_STOP_NOIMPORT: return "NOIMPORT";
	}
	return "?";
}

static int one(struct n2a32_cpu *c, int thumb, unsigned long long n)
{
	uint64_t r = rnd64();
	uint32_t pc;
	int i;

	if (thumb)
		pc = SCRATCH_BASE + ((uint32_t)(r % (SCRATCH_LEN - 4u)) & ~1u);
	else
		pc = SCRATCH_BASE + ((uint32_t)(r % (SCRATCH_LEN - 4u)) & ~3u);

	n2a32_cpu_init(c, c->mem);
	for (i = 0; i < 15; i++)
		c->r[i] = (uint32_t)rnd64();
	c->r[13] = SCRATCH_BASE + SCRATCH_LEN - 64u - (uint32_t)(rnd64() & 0xffu);
	c->r[15] = pc;
	c->next_pc = pc;
	c->thumb = (uint8_t)thumb;
	c->n = (uint8_t)(rnd64() & 1u);
	c->z = (uint8_t)(rnd64() & 1u);
	c->cf = (uint8_t)(rnd64() & 1u);
	c->v = (uint8_t)(rnd64() & 1u);
	c->q = (uint8_t)(rnd64() & 1u);
	c->ge = (uint8_t)(rnd64() & 0xfu);
	c->ilimit = BUDGET;
	c->icount = 0;

	(void)n2a32_run(c);
	if (c->stop == N2A32_STOP_UNDEF)
		return 1;
	if (c->stop == N2A32_STOP_FAULT)
		return 2;
	if (c->stop == N2A32_STOP_RETURN || c->stop == N2A32_STOP_LIMIT ||
	    c->stop == N2A32_STOP_SVC || c->stop == N2A32_STOP_BKPT)
		return 0;
	printf("fuzz: trial %llu bad stop %s (%d) pc=%08x insn=%08x addr=%08x icount=%llu\n",
		n, stop_name(c->stop), (int)c->stop, c->next_pc, c->stop_insn,
		c->stop_addr, (unsigned long long)c->icount);
	return 3;
}

int main(int argc, char **argv)
{
	unsigned long long na = 1000000, nt = 1000000, i;
	unsigned long long ok = 0, undef = 0, fault = 0;
	struct n2a32_mem *mem;
	struct n2a32_cpu cpu;
	int k;

	for (k = 1; k < argc; k++) {
		if (strcmp(argv[k], "--a32") == 0 && k + 1 < argc)
			na = strtoull(argv[++k], NULL, 0);
		else if (strcmp(argv[k], "--t32") == 0 && k + 1 < argc)
			nt = strtoull(argv[++k], NULL, 0);
		else if (strcmp(argv[k], "--seed") == 0 && k + 1 < argc)
			rng_state = strtoull(argv[++k], NULL, 0);
		else {
			fprintf(stderr, "usage: %s [--a32 N] [--t32 N] [--seed S]\n", argv[0]);
			return 2;
		}
	}

	if (rng_state == 0)
		rng_state = 0x9e3779b97f4a7c15ULL;
	mem = n2a32_mem_create();
	if (!mem) {
		fprintf(stderr, "fuzz: cannot reserve guest memory\n");
		return 2;
	}
	if (n2a32_mem_map(mem, SCRATCH_BASE, SCRATCH_LEN, 1) != 0) {
		fprintf(stderr, "fuzz: cannot map the scratch megabyte\n");
		n2a32_mem_destroy(mem);
		return 2;
	}
	{
		uint8_t *p = mem->base + SCRATCH_BASE;
		for (i = 0; i < SCRATCH_LEN; i += 8) {
			uint64_t v = rnd64();
			memcpy(p + i, &v, 8);
		}
	}

	memset(&cpu, 0, sizeof(cpu));
	cpu.mem = mem;
	for (i = 0; i < na; i++) {
		int r = one(&cpu, 0, i);
		if (r == 0) ok++;
		else if (r == 1) undef++;
		else if (r == 2) fault++;
		else { n2a32_mem_destroy(mem); return 1; }
	}
	for (i = 0; i < nt; i++) {
		int r = one(&cpu, 1, na + i);
		if (r == 0) ok++;
		else if (r == 1) undef++;
		else if (r == 2) fault++;
		else { n2a32_mem_destroy(mem); return 1; }
	}

	printf("fuzz: a32=%llu t32=%llu ok=%llu undef=%llu fault=%llu budget=%u\n",
		na, nt, ok, undef, fault, BUDGET);
	n2a32_mem_destroy(mem);
	if (ok < 100 || undef < 100) {
		printf("fuzz: too few valid/undefined results (ok=%llu undef=%llu); classes missing?\n",
			ok, undef);
		return 1;
	}
	return 0;
}
