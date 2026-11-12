/* SPDX-License-Identifier: MIT */
/*
 * t_dp.c -- host tests for package V10: A32 data processing, MOVW/MOVT, multiplies,
 * MRS/MSR (APSR) and CLZ (gpu/hwcodec/emu/n2a32_arm_dp.c).
 *
 * The test drives the class function exactly as the core does: c.r[15] holds the biased
 * PC, c.next_pc the following instruction, the condition has already been evaluated.
 * The instruction words come from enc_v10.h, which mkenc.py generates with llvm-mc from
 * encodings.txt (assembly text in this directory); no encoding is hand-typed.
 *
 * The expected register/flag/next-PC values in cases_v10.inc are literals worked out from
 * the ARM ARM pseudocode (see gen_cases.py, the throwaway calculator used to derive them).
 *
 * Three test groups:
 *   1. CASES: every instruction form, >= 6 cases each, including carry-out, overflow,
 *      zero and negative flag edges, r15 (ADR) and sp operands, every shift variant,
 *      register-shifted variants, and ALU writes to r15 through n2a32_bx.
 *   2. UNDEFS: encodings that belong to other packages (BX/BLX, hints, MSR immediate,
 *      media, memory, coprocessor, unconditional space, ...) must return N2A32_UNDEF
 *      and must not change the CPU state.
 *   3. SWEEP: 200000 random words of this class's encoding space (bits 27-26 == 00,
 *      random condition/register/shift fields) under ASan/UBSan: each word must either
 *      execute or return N2A32_UNDEF, must never write r15 directly, and must leave the
 *      flag fields in range.  Plus 20000 fully random words for the same "no crash" check.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "n2a32.h"
#include "enc_v10.h"

static struct n2a32_mem g_mem;
static int g_mem_touched;

/* The class function must not touch guest memory at all.  These two are the only
 * memory entry points the header's inline helpers can reach; if the implementation ever
 * loads/stores through them the test reports it. */
uint32_t n2a32_mmio_load(struct n2a32_mem *m, uint32_t a, unsigned bytes)
{
	(void)m; (void)a; (void)bytes;
	g_mem_touched = 1;
	return 0;
}

void n2a32_mmio_store(struct n2a32_mem *m, uint32_t a, unsigned bytes, uint32_t v)
{
	(void)m; (void)a; (void)bytes; (void)v;
	g_mem_touched = 1;
}

struct tc {
	const char *name;
	uint32_t insn;
	int8_t fn, fz, fc, fv, fq, fge, fthumb;
	uint8_t nset;
	uint8_t sr[3];
	uint32_t sv[3];
	uint8_t ndw;
	uint8_t dw[2];
	uint32_t dv[2];
	int8_t on, oz, oc, ov, oq, oge, othumb;
	int32_t onext;
};

static const struct tc CASES[] = {
#include "cases_v10.inc"
};

struct uc {
	const char *name;
	uint32_t insn;
};

static const struct uc UNDEFS[] = {
#include "undef_v10.inc"
};

#define N_CASES (sizeof CASES / sizeof CASES[0])
#define N_UNDEF (sizeof UNDEFS / sizeof UNDEFS[0])

/* Register bank every case starts from unless it overrides one. */
static const uint32_t STD[15] = {
	0x11111111u, 0x12345678u, 0x0000000fu, 0x00000005u, 0xffffffffu,
	0x80000000u, 0x00000000u, 0x7fffffffu, 0x00000020u, 0x00000021u,
	0x000000ffu, 0x00000001u, 0x0000001fu, 0x00001000u, 0x00002000u
};
#define TEST_PC   0x00001008u
#define TEST_NEXT 0x00001004u

static unsigned g_pass, g_total, g_failures;
static const char *g_first_fail;

static void fail(const char *name, const char *what)
{
	g_failures++;
	if (!g_first_fail)
		g_first_fail = name;
	printf("FAIL %s: %s\n", name, what);
}

static void setup(struct n2a32_cpu *c, const struct tc *t, uint32_t *pre)
{
	int i;

	memset(c, 0, sizeof *c);
	c->mem = &g_mem;
	for (i = 0; i < 15; i++)
		c->r[i] = STD[i];
	for (i = 0; i < t->nset; i++)
		c->r[t->sr[i]] = t->sv[i];
	c->r[15] = TEST_PC;
	c->next_pc = TEST_NEXT;
	c->n = (uint8_t)t->fn;
	c->z = (uint8_t)t->fz;
	c->cf = (uint8_t)t->fc;
	c->v = (uint8_t)t->fv;
	c->q = (uint8_t)t->fq;
	c->ge = (uint8_t)t->fge;
	c->thumb = (uint8_t)t->fthumb;
	memcpy(pre, c->r, 16 * sizeof(uint32_t));
}

static void check_flag(const char *name, const char *what, int got, int want)
{
	char buf[80];

	if (want < 0 || got == want)
		return;
	snprintf(buf, sizeof buf, "%s = %d, want %d", what, got, want);
	fail(name, buf);
}

static void check_reg(const char *name, const char *what, uint32_t got, uint32_t want)
{
	char buf[96];

	if (got == want)
		return;
	snprintf(buf, sizeof buf, "%s = 0x%08x, want 0x%08x", what, got, want);
	fail(name, buf);
}

static void run_case(const struct tc *t)
{
	struct n2a32_cpu c;
	uint32_t pre[16], want;
	unsigned fails_before = g_failures;
	int i, k, rc;

	setup(&c, t, pre);
	g_total++;

	rc = n2a32_arm_dp(&c, t->insn);
	if (rc != N2A32_OK) {
		char buf[64];
		snprintf(buf, sizeof buf, "returned %d, want N2A32_OK", rc);
		fail(t->name, buf);
		return;
	}
	check_reg(t->name, "r15 (must not be written directly)", c.r[15], TEST_PC);

	for (i = 0; i < 15; i++) {
		want = pre[i];
		for (k = 0; k < t->ndw; k++)
			if (t->dw[k] == i)
				want = t->dv[k];
		check_reg(t->name, "register", c.r[i], want);
	}
	check_flag(t->name, "n", c.n, t->on);
	check_flag(t->name, "z", c.z, t->oz);
	check_flag(t->name, "c", c.cf, t->oc);
	check_flag(t->name, "v", c.v, t->ov);
	check_flag(t->name, "q", c.q, t->oq);
	check_flag(t->name, "ge", c.ge, t->oge);
	check_flag(t->name, "thumb", c.thumb, t->othumb);
	check_reg(t->name, "next_pc", c.next_pc,
		  t->onext < 0 ? (uint32_t)TEST_NEXT : (uint32_t)t->onext);

	if (g_failures == fails_before)
		g_pass++;
}

static void run_undef(const struct uc *u)
{
	struct n2a32_cpu c, before;
	struct tc t;
	int rc;

	memset(&t, 0, sizeof t);
	t.name = u->name;
	t.insn = u->insn;
	setup(&c, &t, before.r);
	before = c;
	g_total++;

	rc = n2a32_arm_dp(&c, u->insn);
	if (rc != N2A32_UNDEF) {
		char buf[64];
		snprintf(buf, sizeof buf, "encoding 0x%08x returned %d, want N2A32_UNDEF",
			 u->insn, rc);
		fail(u->name, buf);
		return;
	}
	if (memcmp(&c, &before, sizeof c) != 0)
		fail(u->name, "state changed on N2A32_UNDEF");
	else
		g_pass++;
}

static uint32_t g_rng = 0x12345678u;

static uint32_t rnd(void)
{
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 17;
	g_rng ^= g_rng << 5;
	return g_rng;
}

static void sweep_word(uint32_t w, unsigned *ok, unsigned *undef)
{
	struct n2a32_cpu c, before;
	int i, rc;

	memset(&c, 0, sizeof c);
	c.mem = &g_mem;
	for (i = 0; i < 15; i++)
		c.r[i] = rnd();
	c.r[15] = TEST_PC;
	c.next_pc = TEST_NEXT;
	c.n = (uint8_t)(rnd() & 1);
	c.z = (uint8_t)(rnd() & 1);
	c.cf = (uint8_t)(rnd() & 1);
	c.v = (uint8_t)(rnd() & 1);
	c.q = (uint8_t)(rnd() & 1);
	c.ge = (uint8_t)(rnd() & 15);
	c.thumb = 0;
	before = c;
	g_total++;

	rc = n2a32_arm_dp(&c, w);
	if (rc != N2A32_OK && rc != N2A32_UNDEF) {
		char buf[80];
		snprintf(buf, sizeof buf, "encoding 0x%08x returned %d", w, rc);
		fail("sweep", buf);
		return;
	}
	if (c.r[15] != TEST_PC) {
		char buf[80];
		snprintf(buf, sizeof buf, "encoding 0x%08x wrote r15 directly", w);
		fail("sweep", buf);
		return;
	}
	if (c.n > 1 || c.z > 1 || c.cf > 1 || c.v > 1 || c.q > 1 || c.ge > 15 ||
	    c.thumb > 1) {
		char buf[80];
		snprintf(buf, sizeof buf, "encoding 0x%08x left flag fields out of range", w);
		fail("sweep", buf);
		return;
	}
	if (rc == N2A32_OK) {
		(*ok)++;
	} else {
		/* UNDEF must not modify the state. */
		if (memcmp(&c, &before, sizeof c) != 0) {
			char buf[80];
			snprintf(buf, sizeof buf, "encoding 0x%08x changed state on UNDEF", w);
			fail("sweep", buf);
			return;
		}
		(*undef)++;
	}
	g_pass++;
}

int main(void)
{
	size_t i;
	unsigned ok = 0, undef = 0;

	g_mem.base = calloc(16u << 20, 1);	/* 16 MiB of guest memory, below 16 MiB */
	if (!g_mem.base) {
		printf("N2TEST V10 FAIL calloc\n");
		return 1;
	}

	for (i = 0; i < N_CASES; i++)
		run_case(&CASES[i]);
	for (i = 0; i < N_UNDEF; i++)
		run_undef(&UNDEFS[i]);

	/* 200000 words of this class's encoding space: bits 27-26 == 00, everything else
	 * random (condition, bit 25, opcode, S, registers, shift fields, cond == 0xF). */
	for (i = 0; i < 200000; i++) {
		uint32_t w = rnd() & 0x03ffffffu;
		w |= (uint32_t)(rnd() & 0xfu) << 28;
		sweep_word(w, &ok, &undef);
	}
	/* And 20000 entirely random words: no crash, no r15 write, valid flag values. */
	for (i = 0; i < 20000; i++)
		sweep_word(rnd(), &ok, &undef);

	if (g_mem_touched)
		fail("memory", "the class function called the MMIO hook");

	printf("V10: %zu cases, %zu undef encodings, sweep ok=%u undef=%u\n",
	       N_CASES, N_UNDEF, ok, undef);
	free(g_mem.base);
	if (g_failures) {
		printf("N2TEST V10 FAIL %u of %u checks (first: %s)\n",
		       g_failures, g_total, g_first_fail ? g_first_fail : "?");
		return 1;
	}
	printf("N2TEST V10 PASS %u/%u\n", g_pass, g_total);
	return 0;
}
