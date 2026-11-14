/* SPDX-License-Identifier: MIT */
/*
 * t_3same.c -- host tests for package V18 (NEON three registers of the same length,
 * gpu/hwcodec/emu/n2a32_neon_3same.c).
 *
 * The class function is driven exactly as the core drives it: c.r[15] holds the biased PC,
 * c.next_pc the following instruction, the condition has already been evaluated.  The instruction
 * words come from enc_v18.h, which gen_cases.py generates with llvm-mc from assembly text
 * (generated forms and the hand-written golden_encodings.txt); no encoding is typed by hand.
 *
 * Groups:
 *   1. CASES: generated cases (>= 6 per allocated encoding form, computed by ref_model.py from the
 *      ARM ARM pseudocode) plus golden_cases.inc (hand-derived expected values).
 *   2. ORACLE: every (U, size, opc, bit4, Q) combination of the instruction space, three register
 *      patterns each, with the expected OK/UNDEF taken from llvm-mc's own decoder, plus derived
 *      Q-form words with an odd register or with a D/N/M high bit set (must be UNDEF).
 *   3. SWEEP: 200000 random words of the encoding space under ASan/UBSan -- every word must execute
 *      or return N2A32_UNDEF, never crash, never write a core register or a flag other than the
 *      architectural ones (QC), and a UNDEF must leave the whole CPU state unchanged.  Plus 20000
 *      fully random words, which are outside the class and must all return N2A32_UNDEF.
 *   4. FPU MODE: one float case is run with the host rounding mode set to FE_UPWARD; NEON uses the
 *      standard FPSCR value (round to nearest even), so the result must still be the RNE one and the
 *      caller's rounding mode must be restored.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fenv.h>

#include "n2a32.h"
#include "enc_v18.h"

static struct n2a32_mem g_mem;
static int g_mem_touched;

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
	uint64_t d[2], n[2], m[2];
	uint64_t ed[2];
	uint8_t qc;
	uint8_t q;
};

#define CASE_D(name, insn, dv, nv, mv, edv, qcv) \
	{ name, insn, { (dv), 0 }, { (nv), 0 }, { (mv), 0 }, { (edv), 0 }, (qcv), 0 },
#define CASE_Q(name, insn, dv0, dv1, nv0, nv1, mv0, mv1, edv0, edv1, qcv) \
	{ name, insn, { (dv0), (dv1) }, { (nv0), (nv1) }, { (mv0), (mv1) }, \
	  { (edv0), (edv1) }, (qcv), 1 },

static const struct tc CASES[] = {
#include "cases_v18.inc"
#include "golden_cases.inc"
};

struct oracle_case {
	uint32_t insn;
	int expect_ok;
	const char *text;
};

static const struct oracle_case ORACLE[] = {
#include "decode_oracle.inc"
};

#define N_CASES (sizeof CASES / sizeof CASES[0])
#define N_ORACLE (sizeof ORACLE / sizeof ORACLE[0])

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

static unsigned vd_of(uint32_t insn) { return ((insn >> 18) & 0x10u) | ((insn >> 12) & 0xfu); }
static unsigned vn_of(uint32_t insn) { return ((insn >> 3) & 0x10u) | ((insn >> 16) & 0xfu); }
static unsigned vm_of(uint32_t insn) { return ((insn >> 1) & 0x10u) | (insn & 0xfu); }

/* A deterministic filler for every D register, so "unchanged" really is checked. */
static uint64_t filler(unsigned i)
{
	return UINT64_C(0x9e3779b97f4a7c15) * (uint64_t)(i + 1) ^ UINT64_C(0x5a5a5a5a5a5a5a5a);
}

static void fill_all(struct n2a32_cpu *c)
{
	unsigned i;

	for (i = 0; i < 32; i++)
		c->d[i].u64 = filler(i);
}

static void init_cpu(struct n2a32_cpu *c)
{
	unsigned i;

	memset(c, 0, sizeof *c);
	c->mem = &g_mem;
	fill_all(c);
	for (i = 0; i < 16; i++)
		c->r[i] = 0x10000000u + i * 0x01010101u;
	c->r[15] = TEST_PC;
	c->next_pc = TEST_NEXT;
	c->n = 1;
	c->z = 0;
	c->cf = 1;
	c->v = 0;
	c->q = 0;
	c->ge = 0xa;
	c->thumb = 0;
	c->itstate = 0x1c;
	c->in_it = 1;
	c->fpscr = 0;                    /* DN=0, FZ=0: NEON must use the standard value anyway */
}

static int set_input(struct n2a32_cpu *c, uint32_t *seen, unsigned reg,
		     uint64_t lo, uint64_t hi, int q)
{
	unsigned k;

	for (k = 0; k < (q ? 2u : 1u); k++) {
		unsigned r = reg + k;
		uint64_t v = k ? hi : lo;

		if (*seen & (1u << r)) {
			if (c->d[r].u64 != v)
				return 1;
		} else {
			c->d[r].u64 = v;
			*seen |= 1u << r;
		}
	}
	return 0;
}

static int setup_case(struct n2a32_cpu *c, const struct tc *t)
{
	uint32_t seen = 0;

	init_cpu(c);
	if (set_input(c, &seen, vd_of(t->insn), t->d[0], t->d[1], t->q) ||
	    set_input(c, &seen, vn_of(t->insn), t->n[0], t->n[1], t->q) ||
	    set_input(c, &seen, vm_of(t->insn), t->m[0], t->m[1], t->q))
		return 1;
	return 0;
}

static void check_reg(const char *name, const char *what, uint64_t got, uint64_t want)
{
	char buf[128];

	if (got == want)
		return;
	snprintf(buf, sizeof buf, "%s = 0x%016llx, want 0x%016llx", what,
		 (unsigned long long)got, (unsigned long long)want);
	fail(name, buf);
}

static void run_case(const struct tc *t)
{
	struct n2a32_cpu c;
	uint64_t before[32];
	unsigned vd = vd_of(t->insn), r;
	unsigned fails_before = g_failures;
	int rc;

	if (setup_case(&c, t)) {
		fail(t->name, "test case is inconsistent (aliased registers with different values)");
		return;
	}
	memcpy(before, (void *)&c.d[0], sizeof before);
	g_total++;

	rc = n2a32_neon_3same(&c, t->insn);
	if (rc != N2A32_OK) {
		char buf[64];

		snprintf(buf, sizeof buf, "encoding 0x%08x returned %d, want N2A32_OK",
			 t->insn, rc);
		fail(t->name, buf);
		return;
	}
	for (r = 0; r < 32; r++) {
		uint64_t want = before[r];

		if (r == vd)
			want = t->ed[0];
		if (t->q && r == vd + 1)
			want = t->ed[1];
		check_reg(t->name, "D register", c.d[r].u64, want);
	}
	if (c.q != t->qc) {
		char buf[64];

		snprintf(buf, sizeof buf, "QC = %u, want %u", c.q, t->qc);
		fail(t->name, buf);
	}
	if (((c.fpscr >> 27) & 1u) != t->qc)
		fail(t->name, "FPSCR.QC (bit 27) does not match APSR.Q");
	if ((c.fpscr & ~(1u << 27)) != 0)
		fail(t->name, "FPSCR bits other than QC were changed");

	/* Core state must be untouched. */
	for (r = 0; r < 16; r++) {
		uint32_t want = 0x10000000u + r * 0x01010101u;

		if (r == 15)
			want = TEST_PC;
		if (c.r[r] != want) {
			char buf[64];

			snprintf(buf, sizeof buf, "r%u changed to 0x%08x", r, c.r[r]);
			fail(t->name, buf);
			break;
		}
	}
	if (c.next_pc != TEST_NEXT)
		fail(t->name, "next_pc changed");
	if (c.thumb != 0 || c.in_it != 1 || c.itstate != 0x1c)
		fail(t->name, "thumb/in_it/itstate changed");
	if (c.n != 1 || c.z != 0 || c.cf != 1 || c.v != 0 || c.ge != 0xa)
		fail(t->name, "NZCV/GE changed");

	if (g_failures == fails_before)
		g_pass++;
}

/* UNDEF: the encoding must be refused and the whole CPU must be unchanged. */
static void run_undef(const char *name, uint32_t insn)
{
	struct n2a32_cpu c, before;
	unsigned fails_before = g_failures;
	int rc;

	init_cpu(&c);
	before = c;
	g_total++;

	rc = n2a32_neon_3same(&c, insn);
	if (rc != N2A32_UNDEF) {
		char buf[64];

		snprintf(buf, sizeof buf, "encoding 0x%08x returned %d, want N2A32_UNDEF",
			 insn, rc);
		fail(name, buf);
		return;
	}
	if (memcmp(&c, &before, sizeof c) != 0)
		fail(name, "state changed on N2A32_UNDEF");
	if (g_failures == fails_before)
		g_pass++;
}

static uint32_t g_rng = 0x13579bdfu;

static uint32_t rnd(void)
{
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 17;
	g_rng ^= g_rng << 5;
	return g_rng;
}

/* One sweep word: OK or UNDEF, never a crash, no core register, only the destination vector
 * register(s) and QC may change. */
static void sweep_word(uint32_t w, unsigned *ok, unsigned *undef, int expect)
{
	struct n2a32_cpu c, before;
	uint64_t vec_before[32];
	unsigned fails_before = g_failures;
	unsigned vd, q, r;
	int rc;

	init_cpu(&c);
	{
		uint32_t qc0 = rnd() & 1u;

		c.q = (uint8_t)qc0;
		c.fpscr |= qc0 << 27;
	}
	before = c;
	memcpy(vec_before, (void *)&c.d[0], sizeof vec_before);
	g_total++;

	rc = n2a32_neon_3same(&c, w);
	if (rc != N2A32_OK && rc != N2A32_UNDEF) {
		char buf[64];

		snprintf(buf, sizeof buf, "encoding 0x%08x returned %d", w, rc);
		fail("sweep", buf);
		return;
	}
	if (rc == N2A32_UNDEF) {
		if (memcmp(&c, &before, sizeof c) != 0) {
			char buf[64];

			snprintf(buf, sizeof buf, "encoding 0x%08x changed state on UNDEF", w);
			fail("sweep", buf);
			return;
		}
		if (expect == 1) {
			char buf[64];

			snprintf(buf, sizeof buf, "encoding 0x%08x unexpectedly UNDEF", w);
			fail("sweep", buf);
			return;
		}
		(*undef)++;
		if (g_failures == fails_before)
			g_pass++;
		return;
	}
	if (expect == 2) {
		char buf[64];

		snprintf(buf, sizeof buf, "encoding 0x%08x unexpectedly executed", w);
		fail("sweep", buf);
		return;
	}

	/* OK: check that nothing but the destination D register(s) and QC changed. */
	vd = vd_of(w);
	q = (w >> 6) & 1u;
	for (r = 0; r < 32; r++) {
		int dst = (r == vd) || (q != 0 && r == vd + 1);

		if (!dst && c.d[r].u64 != vec_before[r]) {
			char buf[64];

			snprintf(buf, sizeof buf, "encoding 0x%08x wrote D%u", w, r);
			fail("sweep", buf);
			return;
		}
	}
	for (r = 0; r < 16; r++)
		if (c.r[r] != before.r[r]) {
			char buf[64];

			snprintf(buf, sizeof buf, "encoding 0x%08x wrote r%u", w, r);
			fail("sweep", buf);
			return;
		}
	if (c.next_pc != before.next_pc || c.thumb != before.thumb ||
	    c.in_it != before.in_it || c.itstate != before.itstate) {
		char buf[64];

		snprintf(buf, sizeof buf, "encoding 0x%08x changed control state", w);
		fail("sweep", buf);
		return;
	}
	if (c.n != before.n || c.z != before.z || c.cf != before.cf || c.v != before.v ||
	    c.ge != before.ge) {
		char buf[64];

		snprintf(buf, sizeof buf, "encoding 0x%08x changed NZCV/GE", w);
		fail("sweep", buf);
		return;
	}
	if (c.q > 1 || ((c.fpscr >> 27) & 1u) != c.q) {
		char buf[64];

		snprintf(buf, sizeof buf, "encoding 0x%08x left QC inconsistent", w);
		fail("sweep", buf);
		return;
	}
	if ((c.fpscr & ~(1u << 27)) != (before.fpscr & ~(1u << 27))) {
		char buf[64];

		snprintf(buf, sizeof buf, "encoding 0x%08x changed FPSCR exception bits", w);
		fail("sweep", buf);
		return;
	}
	(*ok)++;
	if (g_failures == fails_before)
		g_pass++;
}

/* Float results must use the standard FPSCR value (round to nearest even) even if the host FPU was
 * left in another mode, and the host mode must be restored. */
static void test_rounding_mode(void)
{
	struct n2a32_cpu c;
	int saved = fegetround();

	if (fesetround(FE_UPWARD) != 0) {
		printf("(FE_UPWARD not supported, rounding-mode test skipped)\n");
		return;
	}
	init_cpu(&c);
	c.d[vd_of(E_G_VADD_F32)].u64 = UINT64_C(0x0000000000000000);
	c.d[vn_of(E_G_VADD_F32)].u64 = UINT64_C(0x3f8000003f800000);  /* 1.0 */
	c.d[vm_of(E_G_VADD_F32)].u64 = UINT64_C(0x3380000033800000);  /* 2^-24 */
	g_total++;
	if (n2a32_neon_3same(&c, E_G_VADD_F32) != N2A32_OK) {
		fail("rounding mode", "vadd.f32 returned UNDEF");
	} else if (c.d[vd_of(E_G_VADD_F32)].u64 != UINT64_C(0x3f8000003f800000)) {
		char buf[128];

		snprintf(buf, sizeof buf, "result 0x%016llx, want 0x3f8000003f800000 (RNE)",
			 (unsigned long long)c.d[vd_of(E_G_VADD_F32)].u64);
		fail("rounding mode", buf);
	} else if (fegetround() != FE_UPWARD) {
		fail("rounding mode", "the host rounding mode was not restored");
	} else {
		g_pass++;
	}
	(void)fesetround(saved);
}

int main(void)
{
	size_t i;
	unsigned ok = 0, undef = 0;

	g_mem.base = calloc(16u << 20, 1);
	if (!g_mem.base) {
		printf("N2TEST V18 FAIL calloc\n");
		return 1;
	}

	for (i = 0; i < N_CASES; i++)
		run_case(&CASES[i]);
	for (i = 0; i < N_ORACLE; i++) {
		if (ORACLE[i].expect_ok)
			sweep_word(ORACLE[i].insn, &ok, &undef, 1);
		else
			run_undef(ORACLE[i].text, ORACLE[i].insn);
	}

	test_rounding_mode();

	/* 200000 random class-space words: a mix of all fields, Q=1 mostly with even registers so the
	 * execute paths are exercised too. */
	for (i = 0; i < 200000; i++) {
		uint32_t w = 0xf2000000u | ((uint32_t)(rnd() & 1u) << 24) |
			     ((uint32_t)(rnd() & 3u) << 20) | ((uint32_t)(rnd() & 0xfu) << 8) |
			     ((uint32_t)(rnd() & 1u) << 4) | ((uint32_t)(rnd() & 3u) << 22) |
			     ((uint32_t)(rnd() & 0xfu) << 12) | ((uint32_t)(rnd() & 0xfu) << 16) |
			     ((uint32_t)(rnd() & 1u) << 7) | ((uint32_t)(rnd() & 1u) << 6) |
			     ((uint32_t)(rnd() & 1u) << 5) | (rnd() & 0xfu);

		if (rnd() & 1u) {
			/* Bias half of the sweep to well-formed Q forms with even register
			 * numbers and D/N/M clear, so the execute paths are exercised too.
			 * Unallocated encodings in that space are still accepted as UNDEF. */
			w &= ~0x00400280u;                    /* clear the D/N/M high bits */
			w |= 0x40u;                           /* Q = 1 */
			w &= ~0xf000u;
			w |= 0x2000u;                         /* Vd even */
			w &= ~0xf0000u;
			w |= 0x20000u;                        /* Vn even */
			w &= ~0xfu;
			w |= 0x2u;                            /* Vm even */
		}
		sweep_word(w, &ok, &undef, 0);
	}
	/* 20000 words outside the class: must all be UNDEF and change nothing. */
	for (i = 0; i < 20000; i++) {
		uint32_t w = rnd();

		if ((w & 0xfe800000u) == 0xf2000000u)
			w ^= 0x00800000u;      /* move it out of this instruction class */
		sweep_word(w, &ok, &undef, 2);
	}

	if (g_mem_touched)
		fail("memory", "the class function called the MMIO hook");

	printf("V18: %zu cases, %zu oracle words, sweep ok=%u undef=%u\n",
	       N_CASES, N_ORACLE, ok, undef);
	free(g_mem.base);
	if (g_failures) {
		printf("N2TEST V18 FAIL %u of %u checks (first: %s)\n",
		       g_failures, g_total, g_first_fail ? g_first_fail : "?");
		return 1;
	}
	printf("N2TEST V18 PASS %u/%u\n", g_pass, g_total);
	return 0;
}
