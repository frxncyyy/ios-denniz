/* SPDX-License-Identifier: MIT */
/*
 * t_dp.c -- host tests for package V15 (n2a32_t32_dp.c).
 *
 * The class function is called directly with a struct n2a32_cpu whose memory is
 * a 16 MiB calloc, as the package's test oracle prescribes.  Instruction words
 * come from vectors.s through llvm-mc (enc_gen.h); every expected register,
 * flag and Q/GE value below is a literal worked out from the ARM ARM pseudocode.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n2a32.h"
#include "enc_gen.h"

#define KEEP (-1)

static struct n2a32_mem g_mem;
static struct n2a32_cpu g_cpu;
static const char *g_name;
static uint32_t g_insn;
static int g_checks, g_fails;

#define PC_IN 0x1004u		/* r[15] as the core presents it: address + 4 */
#define NPC_IN 0x1000u

static void check(int cond, const char *what, unsigned long got, unsigned long want)
{
	g_checks++;
	if (!cond) {
		g_fails++;
		if (g_fails <= 30)
			fprintf(stderr, "FAIL %s: %s = 0x%lx, expected 0x%lx\n",
				g_name, what, got, want);
	}
}

static void begin(const char *name, uint32_t insn)
{
	memset(g_cpu.r, 0, sizeof(g_cpu.r));
	g_cpu.r[15] = PC_IN;
	g_cpu.next_pc = NPC_IN;
	g_cpu.n = g_cpu.z = g_cpu.cf = g_cpu.v = g_cpu.q = g_cpu.ge = 0;
	g_cpu.thumb = 1;
	g_cpu.itstate = 0;
	g_cpu.in_it = 0;
	g_name = name;
	g_insn = insn;
}

static void flags_in(int n, int z, int c, int v)
{
	g_cpu.n = (uint8_t)n;
	g_cpu.z = (uint8_t)z;
	g_cpu.cf = (uint8_t)c;
	g_cpu.v = (uint8_t)v;
}

static void ge_in(int ge)
{
	g_cpu.ge = (uint8_t)ge;
}

static void reg(unsigned r, uint32_t v)
{
	g_cpu.r[r] = v;
}

static void expect_ok(void)
{
	int rc = n2a32_t32_dp(&g_cpu, g_insn);

	check(rc == N2A32_OK, "return (OK)", (unsigned long)rc, N2A32_OK);
	/* The class never branches: the core's PC state must be untouched. */
	check(g_cpu.r[15] == PC_IN, "r15 unchanged", g_cpu.r[15], PC_IN);
	check(g_cpu.next_pc == NPC_IN, "next_pc unchanged", g_cpu.next_pc, NPC_IN);
}

static void expect_undef(void)
{
	int rc = n2a32_t32_dp(&g_cpu, g_insn);

	check(rc == N2A32_UNDEF, "return (UNDEF)", (unsigned long)rc, N2A32_UNDEF);
	check(g_cpu.r[15] == PC_IN, "r15 unchanged", g_cpu.r[15], PC_IN);
	check(g_cpu.next_pc == NPC_IN, "next_pc unchanged", g_cpu.next_pc, NPC_IN);
}

static void expect_r(unsigned r, uint32_t v)
{
	check(g_cpu.r[r] == v, "register", g_cpu.r[r], v);
}

static void expect_nzcv(int n, int z, int c, int v)
{
	if (n != KEEP)
		check(g_cpu.n == (unsigned)n, "N", g_cpu.n, (unsigned)n);
	if (z != KEEP)
		check(g_cpu.z == (unsigned)z, "Z", g_cpu.z, (unsigned)z);
	if (c != KEEP)
		check(g_cpu.cf == (unsigned)c, "C", g_cpu.cf, (unsigned)c);
	if (v != KEEP)
		check(g_cpu.v == (unsigned)v, "V", g_cpu.v, (unsigned)v);
}

static void expect_q(int q)
{
	check(g_cpu.q == (unsigned)q, "Q", g_cpu.q, (unsigned)q);
}

static void expect_ge(int ge)
{
	check(g_cpu.ge == (unsigned)ge, "GE", g_cpu.ge, (unsigned)ge);
}

/* ------------------------------------------------------------------ region A */
/* Data processing (shifted register), 0xEA00-0xEBFF. */
static void t_region_a(void)
{
	/* AND */
	begin("and.w r1, r2, r3", ENC_and_01);
	reg(2, 0xF0F0F0F0); reg(3, 0x0FF00FF0);
	expect_ok(); expect_r(1, 0x00F000F0);
	expect_nzcv(0, 0, KEEP, KEEP);

	begin("and.w r1, r2, r3, lsl #4", ENC_and_02);
	reg(2, 0x12345678); reg(3, 0x0000000F);
	expect_ok(); expect_r(1, 0x00000070);

	begin("ands.w r1, r2, r3, lsr #31", ENC_and_03);
	reg(2, 0x80000000); reg(3, 0x80000000);
	expect_ok(); expect_r(1, 0);
	expect_nzcv(0, 1, 0, KEEP);

	begin("and.w r1, r2, r3, asr #32", ENC_and_04);
	reg(2, 0xFFFFFFFF); reg(3, 0x80000000);
	expect_ok(); expect_r(1, 0xFFFFFFFF);
	expect_nzcv(0, 0, KEEP, KEEP);	/* S == 0 */

	begin("ands.w r1, r2, r3, ror #7", ENC_and_05);
	reg(2, 0x000000FF); reg(3, 0x00000001);
	expect_ok(); expect_r(1, 0);
	expect_nzcv(0, 1, 0, KEEP);

	begin("ands.w r1, r2, r3, rrx", ENC_and_06);
	flags_in(0, 0, 1, 0);
	reg(2, 0xFFFFFFFF); reg(3, 0x00000002);
	expect_ok(); expect_r(1, 0x80000001);
	expect_nzcv(1, 0, 0, KEEP);

	begin("and.w r0, r1, r2, lsl #0", ENC_and_07);
	reg(1, 0xDEADBEEF); reg(2, 0x0F0F0F0F);
	expect_ok(); expect_r(0, 0x0E0D0E0F);

	begin("tst.w r0, r1", ENC_and_08);
	flags_in(0, 0, 1, 1);
	reg(0, 0x10); reg(1, 0x10);
	expect_ok(); expect_r(0, 0x10);
	expect_nzcv(0, 0, 1, 1);	/* LSL #0 leaves C, V untouched */

	begin("tst.w r0, r1, lsl #2", ENC_and_09);
	flags_in(0, 0, 1, 0);
	reg(0, 0x100); reg(1, 0x40);
	expect_ok(); expect_r(0, 0x100);
	expect_nzcv(0, 0, 0, KEEP);

	/* BIC */
	begin("bic.w r4, r5, r6", ENC_bic_01);
	reg(5, 0xFFFFFFFF); reg(6, 0x0F0F0F0F);
	expect_ok(); expect_r(4, 0xF0F0F0F0);

	begin("bics.w r4, r5, r6, lsl #8", ENC_bic_02);
	reg(5, 0xFFFFFFFF); reg(6, 0x000000FF);
	expect_ok(); expect_r(4, 0xFFFF00FF);
	expect_nzcv(1, 0, 0, KEEP);

	begin("bic.w r4, r5, r6, ror #1", ENC_bic_03);
	reg(5, 0x12345678); reg(6, 0x00000001);
	expect_ok(); expect_r(4, 0x12345678);

	/* ORR / MOV */
	begin("orr.w r7, r8, r9", ENC_orr_01);
	reg(8, 0x00FF0000); reg(9, 0x000000FF);
	expect_ok(); expect_r(7, 0x00FF00FF);

	begin("orrs.w r7, r8, r9, ror #13", ENC_orr_02);
	reg(8, 0); reg(9, 0x00002000);
	expect_ok(); expect_r(7, 1);
	expect_nzcv(0, 0, 0, KEEP);

	begin("mov.w r10, r11", ENC_orr_03);
	flags_in(1, 1, 1, 1);
	reg(11, 0xCAFEBABE);
	expect_ok(); expect_r(10, 0xCAFEBABE);
	expect_nzcv(1, 1, 1, 1);	/* S == 0: nothing changes */

	begin("lsl.w r10, r11, #3", ENC_orr_04);
	reg(11, 0x11111111);
	expect_ok(); expect_r(10, 0x88888888);

	begin("lsls.w r0, r1, #31", ENC_orr_05);
	reg(1, 0x00000003);
	expect_ok(); expect_r(0, 0x80000000);
	expect_nzcv(1, 0, 1, KEEP);

	begin("lsr.w r0, r1, #1", ENC_orr_06);
	reg(1, 0x80000001);
	expect_ok(); expect_r(0, 0x40000000);

	begin("asr.w r0, r1, #32", ENC_orr_07);
	reg(1, 0x80000000);
	expect_ok(); expect_r(0, 0xFFFFFFFF);

	begin("ror.w r0, r1, #1", ENC_orr_08);
	reg(1, 0x00000001);
	expect_ok(); expect_r(0, 0x80000000);

	begin("rrx r0, r1", ENC_orr_09);
	flags_in(0, 0, 1, 0);
	reg(1, 0x80000000);
	expect_ok(); expect_r(0, 0xC0000000);

	begin("lsrs.w r0, r1, #32", ENC_orr_10);
	reg(1, 0xFFFFFFFF);
	expect_ok(); expect_r(0, 0);
	expect_nzcv(0, 1, 1, KEEP);

	/* ORN / MVN */
	begin("orn r0, r1, r2", ENC_orn_01);
	reg(1, 0x0F0F0F0F); reg(2, 0xF0F0F0F0);
	expect_ok(); expect_r(0, 0x0F0F0F0F);

	begin("orns r0, r1, r2, lsl #5", ENC_orn_02);
	reg(1, 0); reg(2, 0x00000001);
	expect_ok(); expect_r(0, 0xFFFFFFDF);
	expect_nzcv(1, 0, 0, KEEP);

	begin("mvn.w r0, r1", ENC_orn_03);
	reg(1, 0x12345678);
	expect_ok(); expect_r(0, 0xEDCBA987);

	begin("mvn.w r0, r1, asr #4", ENC_orn_04);
	reg(1, 0x80000000);
	expect_ok(); expect_r(0, 0x07FFFFFF);

	begin("mvns.w r0, r1", ENC_orn_05);
	flags_in(0, 0, 1, 1);
	reg(1, 0);
	expect_ok(); expect_r(0, 0xFFFFFFFF);
	expect_nzcv(1, 0, 1, 1);	/* carry from LSL #0 is the old C */

	/* EOR / TEQ */
	begin("eor.w r0, r1, r2", ENC_eor_01);
	reg(1, 0xAAAAAAAA); reg(2, 0x55555555);
	expect_ok(); expect_r(0, 0xFFFFFFFF);

	begin("eors.w r0, r1, r2, ror #31", ENC_eor_02);
	reg(1, 0); reg(2, 0x80000000);
	expect_ok(); expect_r(0, 1);
	expect_nzcv(0, 0, 0, KEEP);

	begin("teq.w r0, r1", ENC_eor_03);
	flags_in(0, 0, 1, 1);
	reg(0, 0xF0F0F0F0); reg(1, 0xF0F0F0F0);
	expect_ok();
	expect_nzcv(0, 1, 1, 1);

	begin("teq.w r0, r1, lsl #16", ENC_eor_04);
	reg(0, 0xFFFF0000); reg(1, 0x0000FFFF);
	expect_ok();
	expect_nzcv(0, 1, 0, KEEP);

	/* PKH: PKHBT packs the bottom of Rn with the top of shifted Rm,
	 * PKHTB the top of Rn with the bottom of shifted Rm (T01 finding F1,
	 * confirmed against clang's `(x & 0xffff0000) | (y & 0xffff)` ->
	 * pkhbt and `(x & 0xffff0000) | ((y >> 8) & 0xffff)` -> pkhtb). */
	begin("pkhbt r0, r1, r2", ENC_pkhbt_01);
	reg(1, 0x11112222); reg(2, 0x33334444);
	expect_ok(); expect_r(0, 0x33332222);

	begin("pkhbt r0, r1, r2, lsl #8", ENC_pkhbt_02);
	reg(1, 0x11112222); reg(2, 0x0000AA00);
	expect_ok(); expect_r(0, 0x00AA2222);

	begin("pkhtb r0, r1, r2, asr #1", ENC_pkhtb_01);
	reg(1, 0x11112222); reg(2, 0x80000000);
	expect_ok(); expect_r(0, 0x11110000);

	begin("pkhtb r0, r1, r2, asr #1 (low bit)", ENC_pkhtb_01);
	reg(1, 0x11112222); reg(2, 0x00000002);
	expect_ok(); expect_r(0, 0x11110001);

	begin("pkhtb r0, r1, r2, asr #16", ENC_pkhtb_02);
	reg(1, 0x11112222); reg(2, 0x40000000);
	expect_ok(); expect_r(0, 0x11114000);

	/* ADD */
	begin("add.w r0, r1, r2", ENC_add_01);
	flags_in(0, 0, 1, 1);
	reg(1, 1); reg(2, 2);
	expect_ok(); expect_r(0, 3);
	expect_nzcv(0, 0, 1, 1);

	begin("adds.w r0, r1, r2", ENC_add_02);
	reg(1, 0x7FFFFFFF); reg(2, 1);
	expect_ok(); expect_r(0, 0x80000000);
	expect_nzcv(1, 0, 0, 1);

	begin("adds.w r0, r1, r2, lsl #2", ENC_add_03);
	reg(1, 0xFFFFFFFE); reg(2, 1);
	expect_ok(); expect_r(0, 2);
	expect_nzcv(0, 0, 1, 0);

	begin("add.w r0, r1, r2, ror #3", ENC_add_04);
	reg(1, 0x100); reg(2, 0x00000008);
	expect_ok(); expect_r(0, 0x101);

	begin("cmn.w r0, r1", ENC_add_05);
	reg(0, 0xFFFFFFFF); reg(1, 1);
	expect_ok(); expect_r(0, 0xFFFFFFFF);
	expect_nzcv(0, 1, 1, 0);

	begin("cmn.w r0, r1, lsl #1", ENC_add_06);
	reg(0, 0x80000000); reg(1, 0x40000000);
	expect_ok();
	expect_nzcv(0, 1, 1, 1);

	/* ADC / SBC */
	begin("adc.w r0, r1, r2", ENC_adc_01);
	flags_in(0, 0, 1, 0);
	reg(1, 1); reg(2, 2);
	expect_ok(); expect_r(0, 4);

	begin("adcs.w r0, r1, r2", ENC_adc_02);
	flags_in(0, 0, 1, 0);
	reg(1, 0xFFFFFFFF); reg(2, 0);
	expect_ok(); expect_r(0, 0);
	expect_nzcv(0, 1, 1, 0);

	begin("adcs.w r0, r1, r2, lsl #4", ENC_adc_03);
	flags_in(0, 0, 1, 0);
	reg(1, 0x7FFFFFF0); reg(2, 1);
	expect_ok(); expect_r(0, 0x80000001);
	expect_nzcv(1, 0, 0, 1);

	begin("sbc.w r0, r1, r2", ENC_sbc_01);
	flags_in(0, 0, 1, 0);
	reg(1, 5); reg(2, 3);
	expect_ok(); expect_r(0, 2);

	begin("sbcs.w r0, r1, r2, lsl #1", ENC_sbc_02);
	flags_in(0, 0, 0, 0);
	reg(1, 0); reg(2, 0x80000000);
	expect_ok(); expect_r(0, 0xFFFFFFFF);
	expect_nzcv(1, 0, 0, 0);

	/* SUB / CMP */
	begin("sub.w r0, r1, r2", ENC_sub_01);
	reg(1, 10); reg(2, 3);
	expect_ok(); expect_r(0, 7);

	begin("subs.w r0, r1, r2", ENC_sub_02);
	reg(1, 0); reg(2, 1);
	expect_ok(); expect_r(0, 0xFFFFFFFF);
	expect_nzcv(1, 0, 0, 0);

	begin("cmp.w r0, r1", ENC_sub_03);
	flags_in(0, 0, 0, 1);
	reg(0, 5); reg(1, 5);
	expect_ok();
	expect_nzcv(0, 1, 1, 0);

	begin("cmp.w r0, r1, asr #1", ENC_sub_04);
	reg(0, 0xFFFFFFFF); reg(1, 0x00000002);
	expect_ok();
	expect_nzcv(1, 0, 1, 0);

	begin("subs.w r0, r1, r2, lsr #4", ENC_sub_05);
	reg(1, 0x10); reg(2, 0x00000100);
	expect_ok(); expect_r(0, 0);
	expect_nzcv(0, 1, 1, 0);

	/* RSB */
	begin("rsb r0, r1, r2", ENC_rsb_01);
	reg(1, 3); reg(2, 10);
	expect_ok(); expect_r(0, 7);

	begin("rsbs r0, r1, r2, lsl #2", ENC_rsb_02);
	reg(1, 1); reg(2, 0x40000000);
	expect_ok(); expect_r(0, 0xFFFFFFFF);
	expect_nzcv(1, 0, 0, 0);

	begin("rsbs r0, r1, r2, ror #16", ENC_rsb_03);
	reg(1, 0x80000000); reg(2, 0x80000000);
	expect_ok(); expect_r(0, 0x80008000);
	expect_nzcv(1, 0, 0, 1);
}

/* ------------------------------------------------------------------ region B */
static void t_region_b(void)
{
	begin("and.w r0, r1, #0xff", ENC_bi_and_01);
	reg(1, 0x12345678);
	expect_ok(); expect_r(0, 0x78);

	begin("ands.w r0, r1, #0x80000000", ENC_bi_and_02);
	reg(1, 0x80000000);
	expect_ok(); expect_r(0, 0x80000000);
	expect_nzcv(1, 0, 1, KEEP);	/* rotated immediate: C = imm<31> */

	begin("tst.w r0, #0x00ff00ff", ENC_bi_and_03);
	flags_in(0, 0, 1, 0);
	reg(0, 0x00FF0000);
	expect_ok();
	expect_nzcv(0, 0, 1, KEEP);	/* replicated immediate: C unchanged */

	begin("tst.w r0, #0xff000000", ENC_bi_and_04);
	reg(0, 0x00FFFFFF);
	expect_ok();
	expect_nzcv(0, 1, 1, KEEP);

	begin("bic.w r0, r1, #0xff", ENC_bi_bic_01);
	reg(1, 0x12345678);
	expect_ok(); expect_r(0, 0x12345600);

	begin("bics.w r0, r1, #0x00ff00ff", ENC_bi_bic_02);
	reg(1, 0xFFFFFFFF);
	expect_ok(); expect_r(0, 0xFF00FF00);
	expect_nzcv(1, 0, 0, KEEP);

	begin("eor.w r0, r1, #0xaaaaaaaa", ENC_bi_eor_01);
	reg(1, 0x55555555);
	expect_ok(); expect_r(0, 0xFFFFFFFF);

	begin("teq.w r0, #0xf0000000", ENC_bi_eor_02);
	reg(0, 0x0FFFFFFF);
	expect_ok();
	expect_nzcv(1, 0, 1, KEEP);

	begin("eors.w r0, r1, #0xff00ff00", ENC_bi_eor_03);
	reg(1, 0xFF00FF00);
	expect_ok(); expect_r(0, 0);
	expect_nzcv(0, 1, 0, KEEP);

	begin("orr.w r0, r1, #0x100", ENC_bi_orr_01);
	reg(1, 0x12345678);
	expect_ok(); expect_r(0, 0x12345778);

	begin("orrs.w r0, r1, #0x80000000", ENC_bi_orr_02);
	reg(1, 0);
	expect_ok(); expect_r(0, 0x80000000);
	expect_nzcv(1, 0, 1, KEEP);

	begin("orr.w r0, r1, #0xab00ab", ENC_bi_orr_03);
	reg(1, 0);
	expect_ok(); expect_r(0, 0x00AB00AB);

	begin("orr.w r0, r1, #0x00ff00ff", ENC_bi_orr_04);
	reg(1, 0);
	expect_ok(); expect_r(0, 0x00FF00FF);

	begin("mov.w r0, #0x01010101", ENC_bi_orr_05);
	reg(0, 0xFFFFFFFF);
	expect_ok(); expect_r(0, 0x01010101);

	begin("mov.w r0, #0xff000000", ENC_bi_orr_06);
	expect_ok(); expect_r(0, 0xFF000000);

	begin("movs.w r0, #0", ENC_bi_orr_07);
	flags_in(0, 0, 1, 1);
	reg(0, 0xFFFFFFFF);
	expect_ok(); expect_r(0, 0);
	expect_nzcv(0, 1, 1, 1);	/* imm12 = 0: C unchanged */

	begin("orn.w r0, r1, #0x7", ENC_bi_orn_01);
	reg(1, 0xFFFFFFF8);
	expect_ok(); expect_r(0, 0xFFFFFFF8);

	begin("mvn.w r0, #0xff", ENC_bi_orn_02);
	expect_ok(); expect_r(0, 0xFFFFFF00);

	begin("add.w r0, r1, #0x100", ENC_bi_add_01);
	reg(1, 0x100);
	expect_ok(); expect_r(0, 0x200);

	begin("adds.w r0, r1, #0xff", ENC_bi_add_02);
	reg(1, 0xFFFFFF01);
	expect_ok(); expect_r(0, 0);
	expect_nzcv(0, 1, 1, 0);

	begin("add.w r0, r1, #0xab00ab", ENC_bi_add_03);
	reg(1, 0x00AB0000);
	expect_ok(); expect_r(0, 0x015600AB);

	begin("cmn.w r0, #0x1000", ENC_bi_add_04);
	reg(0, 0xFFFFF000);
	expect_ok();
	expect_nzcv(0, 1, 1, 0);

	begin("sub.w r0, r1, #0x100", ENC_bi_sub_01);
	reg(1, 0x200);
	expect_ok(); expect_r(0, 0x100);

	begin("subs.w r0, r1, #1", ENC_bi_sub_02);
	reg(1, 0);
	expect_ok(); expect_r(0, 0xFFFFFFFF);
	expect_nzcv(1, 0, 0, 0);

	begin("cmp.w r0, #0x80", ENC_bi_sub_03);
	reg(0, 0x80);
	expect_ok();
	expect_nzcv(0, 1, 1, 0);

	begin("subs.w r0, r1, #0xFF00FF00", ENC_bi_sub_04);
	reg(1, 0xFF00FF01);
	expect_ok(); expect_r(0, 1);
	expect_nzcv(0, 0, 1, 0);

	begin("rsb.w r0, r1, #0x1000", ENC_bi_rsb_01);
	reg(1, 0x1001);
	expect_ok(); expect_r(0, 0xFFFFFFFF);

	begin("rsbs.w r0, r1, #0x80000000", ENC_bi_rsb_02);
	reg(1, 0x80000000);
	expect_ok(); expect_r(0, 0);
	expect_nzcv(0, 1, 1, 0);
}

/* ------------------------------------------------------------------ region C */
static void t_region_c(void)
{
	begin("addw r0, r1, #0x123", ENC_addw_01);
	reg(1, 0x1000);
	expect_ok(); expect_r(0, 0x1123);

	begin("addw r0, r1, #0xf00", ENC_addw_02);
	reg(1, 0x1000);
	expect_ok(); expect_r(0, 0x1F00);

	begin("addw r0, sp, #0x7f0", ENC_addw_03);
	reg(13, 0x80000000);
	expect_ok(); expect_r(0, 0x800007F0);

	begin("addw sp, sp, #0x40", ENC_addw_04);
	reg(13, 0x1000);
	expect_ok(); expect_r(13, 0x1040);

	begin("subw r0, r1, #0x123", ENC_subw_01);
	reg(1, 0x1000);
	expect_ok(); expect_r(0, 0x0EDD);

	begin("subw r9, sp, #0x10", ENC_subw_02);
	reg(13, 0x1000);
	expect_ok(); expect_r(9, 0x0FF0);

	/* ADR forward: the assembler placed the target 4 bytes after Align(PC,4),
	 * and the test presents PC as 0x1004, so the result is 0x1008. */
	begin("adr.w r1, +4", ENC_adr_fwd);
	expect_ok(); expect_r(1, 0x1008);

	/* ADR backward: target 8 bytes before the instruction: 0x1004 - 8. */
	begin("adr.w r1, -8", ENC_adr_back);
	expect_ok(); expect_r(1, 0x0FFC);

	begin("movw r0, #0x1234", ENC_movw_01);
	expect_ok(); expect_r(0, 0x1234);

	begin("movw r7, #0xffff", ENC_movw_02);
	expect_ok(); expect_r(7, 0xFFFF);

	begin("movw r12, #0x0001", ENC_movw_03);
	expect_ok(); expect_r(12, 1);

	begin("movt r0, #0xabcd", ENC_movt_01);
	reg(0, 0x12345678);
	expect_ok(); expect_r(0, 0xABCD5678);

	begin("movt r12, #0x8000", ENC_movt_02);
	expect_ok(); expect_r(12, 0x80000000);

	begin("ssat r0, #8, r1", ENC_ssat_01);
	reg(1, 0x1234);
	expect_ok(); expect_r(0, 0x7F); expect_q(1);

	begin("ssat r0, #8, r1 (negative)", ENC_ssat_01);
	reg(1, 0xFFFFFF80);
	expect_ok(); expect_r(0, 0xFFFFFF80); expect_q(0);

	begin("ssat r0, #1, r1, lsl #3", ENC_ssat_02);
	reg(1, 0x00000020);
	expect_ok(); expect_r(0, 0); expect_q(1);

	begin("ssat r0, #32, r1", ENC_ssat_03);
	reg(1, 0x80000000);
	expect_ok(); expect_r(0, 0x80000000); expect_q(0);

	begin("ssat r0, #16, r1, asr #20", ENC_ssat_04);
	reg(1, 0x80000000);
	expect_ok(); expect_r(0, 0xFFFFF800); expect_q(0);

	begin("ssat r5, #12, r9, asr #1", ENC_ssat_05);
	reg(9, 0x7FFFFFFF);
	expect_ok(); expect_r(5, 0x7FF); expect_q(1);

	begin("ssat16 r0, #8, r1", ENC_ssat16_01);
	reg(1, 0x12345678);
	expect_ok(); expect_r(0, 0x007F007F); expect_q(1);

	begin("ssat16 r0, #16, r1", ENC_ssat16_02);
	reg(1, 0x80007FFF);
	expect_ok(); expect_r(0, 0x80007FFF); expect_q(0);

	begin("ssat16 r3, #1, r4", ENC_ssat16_03);
	reg(4, 0x0001FFFF);
	expect_ok(); expect_r(3, 0x0000FFFF); expect_q(1);

	begin("usat r0, #8, r1", ENC_usat_01);
	reg(1, 0x1234);
	expect_ok(); expect_r(0, 0xFF); expect_q(1);

	begin("usat r0, #0, r1", ENC_usat_02);
	reg(1, 0);
	expect_ok(); expect_r(0, 0); expect_q(0);

	begin("usat r0, #31, r1, asr #1", ENC_usat_03);
	reg(1, 0xFFFFFFFF);
	expect_ok(); expect_r(0, 0); expect_q(1);

	begin("usat r5, #16, r9, lsl #4", ENC_usat_04);
	reg(9, 0x00010000);
	expect_ok(); expect_r(5, 0xFFFF); expect_q(1);

	begin("usat16 r0, #8, r1", ENC_usat16_01);
	reg(1, 0x00001234);
	expect_ok(); expect_r(0, 0x000000FF); expect_q(1);

	begin("usat16 r0, #15, r1", ENC_usat16_02);
	reg(1, 0xFFFF0000);
	expect_ok(); expect_r(0, 0x7FFF0000); expect_q(1);

	begin("sbfx r0, r1, #4, #8", ENC_sbfx_01);
	reg(1, 0x00000F80);
	expect_ok(); expect_r(0, 0xFFFFFFF8);

	begin("sbfx r0, r1, #0, #32", ENC_sbfx_02);
	reg(1, 0x80000001);
	expect_ok(); expect_r(0, 0x80000001);

	begin("sbfx r0, r1, #31, #1", ENC_sbfx_03);
	reg(1, 0x80000000);
	expect_ok(); expect_r(0, 0xFFFFFFFF);

	begin("sbfx r5, r9, #3, #17", ENC_sbfx_04);
	reg(9, 0x000F0000);
	expect_ok(); expect_r(5, 0xFFFFE000);

	begin("ubfx r0, r1, #4, #8", ENC_ubfx_01);
	reg(1, 0x00000F80);
	expect_ok(); expect_r(0, 0xF8);

	begin("ubfx r0, r1, #0, #1", ENC_ubfx_02);
	reg(1, 0xFFFFFFFF);
	expect_ok(); expect_r(0, 1);

	begin("ubfx r0, r1, #31, #1", ENC_ubfx_03);
	reg(1, 0x80000000);
	expect_ok(); expect_r(0, 1);

	begin("ubfx r5, r9, #3, #17", ENC_ubfx_04);
	reg(9, 0xFFFFFFFF);
	expect_ok(); expect_r(5, 0x1FFFF);

	begin("bfi r0, r1, #8, #8", ENC_bfi_01);
	reg(0, 0xFFFF0000); reg(1, 0x12345678);
	expect_ok(); expect_r(0, 0xFFFF7800);

	begin("bfi r0, r1, #0, #32", ENC_bfi_02);
	reg(0, 0); reg(1, 0x12345678);
	expect_ok(); expect_r(0, 0x12345678);

	begin("bfi r5, r9, #3, #17", ENC_bfi_03);
	reg(5, 0xFFFFFFFF); reg(9, 0);
	expect_ok(); expect_r(5, 0xFFF00007);

	begin("bfc r0, #4, #12", ENC_bfc_01);
	reg(0, 0xFFFFFFFF);
	expect_ok(); expect_r(0, 0xFFFF000F);

	begin("bfc r0, #0, #32", ENC_bfc_02);
	reg(0, 0x12345678);
	expect_ok(); expect_r(0, 0);

	begin("bfc r5, #3, #17", ENC_bfc_03);
	reg(5, 0xFFFFFFFF);
	expect_ok(); expect_r(5, 0xFFF00007);
}

/* ------------------------------------------------------------------ region D */
static void t_region_d_shift(void)
{
	begin("lsl.w r0, r1, r2", ENC_lslr_01);
	reg(1, 1); reg(2, 31);
	expect_ok(); expect_r(0, 0x80000000);

	begin("lsl.w r0, r1, r2 (32)", ENC_lslr_01);
	reg(1, 3); reg(2, 32);
	expect_ok(); expect_r(0, 0);

	begin("lsls.w r0, r1, r2", ENC_lslr_02);
	reg(1, 0x80000001); reg(2, 1);
	expect_ok(); expect_r(0, 2);
	expect_nzcv(0, 0, 1, KEEP);

	begin("lsr.w r0, r1, r2", ENC_lsrr_01);
	reg(1, 0x80000000); reg(2, 31);
	expect_ok(); expect_r(0, 1);

	begin("lsrs.w r0, r1, r2 (32)", ENC_lsrr_02);
	reg(1, 0xFFFFFFFF); reg(2, 32);
	expect_ok(); expect_r(0, 0);
	expect_nzcv(0, 1, 1, KEEP);

	begin("lsrs.w r0, r1, r2 (33)", ENC_lsrr_02);
	flags_in(1, 1, 0, 1);
	reg(1, 0xFFFFFFFF); reg(2, 33);
	expect_ok(); expect_r(0, 0);
	expect_nzcv(0, 1, 1, 1);

	begin("asr.w r0, r1, r2", ENC_asrr_01);
	reg(1, 0x80000000); reg(2, 4);
	expect_ok(); expect_r(0, 0xF8000000);

	begin("asrs.w r0, r1, r2 (33)", ENC_asrr_02);
	reg(1, 0x80000000); reg(2, 33);
	expect_ok(); expect_r(0, 0xFFFFFFFF);
	expect_nzcv(1, 0, 1, KEEP);

	begin("ror.w r0, r1, r2", ENC_rorr_01);
	reg(1, 1); reg(2, 1);
	expect_ok(); expect_r(0, 0x80000000);

	begin("rors.w r0, r1, r2 (0)", ENC_rorr_02);
	flags_in(0, 0, 1, 1);
	reg(1, 0x12345678); reg(2, 0);
	expect_ok(); expect_r(0, 0x12345678);
	expect_nzcv(0, 0, 1, 1);	/* ROR by 0 leaves C alone */

	begin("ror.w r0, r1, r2 (32)", ENC_rorr_01);
	reg(1, 0x12345678); reg(2, 32);
	expect_ok(); expect_r(0, 0x12345678);

	begin("rors.w r0, r1, r2 (32)", ENC_rorr_02);
	flags_in(0, 0, 0, 0);
	reg(1, 0x12345678); reg(2, 32);
	expect_ok(); expect_r(0, 0x12345678);
	expect_nzcv(0, 0, 0, 0);
}

static void t_region_d_extend(void)
{
	begin("sxtb.w r0, r1", ENC_sxtb_01);
	reg(1, 0x00000080);
	expect_ok(); expect_r(0, 0xFFFFFF80);

	begin("sxtb.w r0, r1, ror #8", ENC_sxtb_02);
	reg(1, 0x00008000);
	expect_ok(); expect_r(0, 0xFFFFFF80);

	begin("sxtb.w r0, r1, ror #16", ENC_sxtb_03);
	reg(1, 0x00800000);
	expect_ok(); expect_r(0, 0xFFFFFF80);

	begin("sxtb.w r0, r1, ror #24", ENC_sxtb_04);
	reg(1, 0x80000000);
	expect_ok(); expect_r(0, 0xFFFFFF80);

	begin("sxth.w r0, r1", ENC_sxth_01);
	reg(1, 0x00008000);
	expect_ok(); expect_r(0, 0xFFFF8000);

	begin("sxth.w r0, r1, ror #16", ENC_sxth_02);
	reg(1, 0x80000000);
	expect_ok(); expect_r(0, 0xFFFF8000);

	begin("uxtb.w r0, r1", ENC_uxtb_01);
	reg(1, 0xFFFFFF80);
	expect_ok(); expect_r(0, 0x80);

	begin("uxtb.w r0, r1, ror #8", ENC_uxtb_02);
	reg(1, 0x00008000);
	expect_ok(); expect_r(0, 0x80);

	begin("uxth.w r0, r1", ENC_uxth_01);
	reg(1, 0xFFFF8000);
	expect_ok(); expect_r(0, 0x8000);

	begin("uxth.w r0, r1, ror #24", ENC_uxth_02);
	reg(1, 0x12345678);
	expect_ok(); expect_r(0, 0x7812);

	begin("sxtb16 r0, r1", ENC_sxtb16_01);
	reg(1, 0x0000807F);
	expect_ok(); expect_r(0, 0x0000007F);

	begin("sxtb16 r0, r1, ror #8", ENC_sxtb16_02);
	reg(1, 0x0000807F);
	expect_ok(); expect_r(0, 0x0000FF80);

	begin("uxtb16 r0, r1", ENC_uxtb16_01);
	reg(1, 0x12345678);
	expect_ok(); expect_r(0, 0x00340078);

	begin("uxtb16 r0, r1, ror #16", ENC_uxtb16_02);
	reg(1, 0x12345678);
	expect_ok(); expect_r(0, 0x00780034);

	begin("sxtab r0, r1, r2", ENC_sxtab_01);
	reg(1, 0x00001000); reg(2, 0xFFFFFF80);
	expect_ok(); expect_r(0, 0x00000F80);

	begin("sxtab r0, r1, r2, ror #24", ENC_sxtab_02);
	reg(1, 1); reg(2, 0x80000000);
	expect_ok(); expect_r(0, 0xFFFFFF81);

	begin("sxtah r0, r1, r2, ror #16", ENC_sxtah_01);
	reg(1, 0x00010000); reg(2, 0x80000000);
	expect_ok(); expect_r(0, 0x00008000);

	begin("uxtab r0, r1, r2", ENC_uxtab_01);
	reg(1, 0x00001000); reg(2, 0xFFFFFF80);
	expect_ok(); expect_r(0, 0x00001080);

	begin("uxtah r0, r1, r2, ror #24", ENC_uxtah_01);
	reg(1, 0); reg(2, 0x00001234);
	expect_ok(); expect_r(0, 0x00003400);

	begin("sxtab16 r0, r1, r2", ENC_sxtab16_01);
	reg(1, 0x00010002); reg(2, 0x00000080);
	expect_ok(); expect_r(0, 0x0001FF82);

	begin("uxtab16 r0, r1, r2, ror #8", ENC_uxtab16_01);
	reg(1, 0x00010002); reg(2, 0x00008000);
	expect_ok(); expect_r(0, 0x00010082);
}

static void t_region_d_misc(void)
{
	begin("rev.w r0, r1", ENC_rev_01);
	reg(1, 0x12345678);
	expect_ok(); expect_r(0, 0x78563412);

	begin("rev16.w r0, r1", ENC_rev16_01);
	reg(1, 0x12345678);
	expect_ok(); expect_r(0, 0x34127856);

	begin("revsh.w r0, r1", ENC_revsh_01);
	reg(1, 0x00001280);
	expect_ok(); expect_r(0, 0xFFFF8012);

	begin("rbit r0, r1", ENC_rbit_01);
	reg(1, 0x00000001);
	expect_ok(); expect_r(0, 0x80000000);

	begin("rbit r0, r1 (pattern)", ENC_rbit_01);
	reg(1, 0x12345678);
	expect_ok(); expect_r(0, 0x1E6A2C48);

	begin("clz r0, r1", ENC_clz_01);
	reg(1, 0x00000001);
	expect_ok(); expect_r(0, 31);

	begin("clz r0, r1 (zero)", ENC_clz_01);
	reg(1, 0);
	expect_ok(); expect_r(0, 32);

	begin("sel r0, r1, r2", ENC_sel_01);
	ge_in(0x5);
	reg(1, 0xAABBCCDD); reg(2, 0x11223344);
	expect_ok(); expect_r(0, 0x11BB33DD);
}

/* Parallel add/subtract: two operand sets, one for the 8-bit and one for the
 * 16-bit forms.  a = 0x7F8001FF, b = 0x01010201 for 8-bit; lanes (low to high)
 * are a: FF 01 80 7F, b: 01 02 01 01.  16-bit: a = 0x80017FFF, b = 0x00020001,
 * lanes a: 7FFF 8001, b: 0001 0002. */
static void par8(const char *name, uint32_t insn, uint32_t a, uint32_t b,
		 uint32_t want, int ge, int q)
{
	begin(name, insn);
	reg(1, a); reg(2, b);
	expect_ok(); expect_r(0, want); expect_ge(ge); expect_q(q);
}

static void par16(const char *name, uint32_t insn, uint32_t a, uint32_t b,
		  uint32_t want, int ge, int q)
{
	par8(name, insn, a, b, want, ge, q);
}

static void t_region_d_parallel(void)
{
	const uint32_t a8 = 0x7F8001FFu, b8 = 0x01010201u;
	const uint32_t a16 = 0x80017FFFu, b16 = 0x00020001u;

	par8("sadd8", ENC_parallel_00, a8, b8, 0x80810300u, 0xB, 0);
	par8("sadd8 #2", ENC_parallel_00, 0x01020304u, 0x01020304u, 0x02040608u,
	     0xF, 0);
	par16("sadd16", ENC_parallel_01, a16, b16, 0x80038000u, 0x3, 0);
	par16("sadd16 #2", ENC_parallel_01, 0x00010002u, 0x00030004u,
	      0x00040006u, 0xF, 0);
	/* hw-r1's F3 repro: a halfword lane drives two GE bits (GE<1:0> low,
	 * GE<3:2> high), both set when the exact lane sum is non-negative. */
	par16("sadd16 F3 repro", ENC_parallel_01, 0x0001FFFFu, 0x00010001u,
	      0x00020000u, 0xF, 0);
	par16("uadd16 both carry", ENC_parallel_19, 0xFFFFFFFFu, 0x00010001u,
	      0x00000000u, 0xF, 0);
	par16("sasx", ENC_parallel_02, a16, b16, 0x80008001u, 0x3, 0);
	par8("ssub8", ENC_parallel_03, a8, b8, 0x7E7FFFFEu, 0x8, 0);
	par8("ssub8 #2", ENC_parallel_03, 0x00000000u, 0x01010101u,
	     0xFFFFFFFFu, 0x0, 0);
	par16("ssub16", ENC_parallel_04, a16, b16, 0x7FFF7FFEu, 0x3, 0);
	par16("ssax", ENC_parallel_05, a16, b16, 0x80027FFDu, 0x3, 0);
	par8("qadd8", ENC_parallel_06, a8, b8, 0x7F810300u, 0xB, 1);
	par16("qadd16", ENC_parallel_07, a16, b16, 0x80037FFFu, 0x3, 1);
	par16("qasx", ENC_parallel_08, a16, b16, 0x80007FFFu, 0x3, 1);
	par8("qsub8", ENC_parallel_09, a8, b8, 0x7E80FFFEu, 0x8, 1);
	par16("qsub16", ENC_parallel_10, a16, b16, 0x80007FFEu, 0x3, 1);
	par16("qsax", ENC_parallel_11, a16, b16, 0x80027FFDu, 0x3, 0);
	par8("shadd8", ENC_parallel_12, a8, b8, 0x40C00100u, 0xB, 0);
	par16("shadd16", ENC_parallel_13, a16, b16, 0xC0014000u, 0x3, 0);
	par16("shasx", ENC_parallel_14, a16, b16, 0xC0004000u, 0x3, 0);
	par8("shsub8", ENC_parallel_15, a8, b8, 0x3FBFFFFFu, 0x8, 0);
	par16("shsub16", ENC_parallel_16, a16, b16, 0xBFFF3FFFu, 0x3, 0);
	par16("shsax", ENC_parallel_17, a16, b16, 0xC0013FFEu, 0x3, 0);
	par8("uadd8", ENC_parallel_18, a8, b8, 0x80810300u, 0x1, 0);
	par8("uadd8 #2", ENC_parallel_18, 0xFFFFFFFFu, 0x01010101u,
	     0x00000000u, 0xF, 0);
	par16("uadd16", ENC_parallel_19, a16, b16, 0x80038000u, 0x0, 0);
	par16("uasx", ENC_parallel_20, a16, b16, 0x80008001u, 0xC, 0);
	par8("usub8", ENC_parallel_21, a8, b8, 0x7E7FFFFEu, 0xD, 0);
	par16("usub16", ENC_parallel_22, a16, b16, 0x7FFF7FFEu, 0xF, 0);
	par16("usax", ENC_parallel_23, a16, b16, 0x80027FFDu, 0x3, 0);
	par8("uqadd8", ENC_parallel_24, a8, b8, 0x808103FFu, 0x1, 1);
	par16("uqadd16", ENC_parallel_25, a16, b16, 0x80038000u, 0x0, 0);
	par16("uqasx", ENC_parallel_26, a16, b16, 0x80008001u, 0xC, 0);
	par8("uqsub8", ENC_parallel_27, a8, b8, 0x7E7F00FEu, 0xD, 1);
	par16("uqsub16", ENC_parallel_28, a16, b16, 0x7FFF7FFEu, 0xF, 0);
	par16("uqsax", ENC_parallel_29, a16, b16, 0x80027FFDu, 0x3, 0);
	par8("uhadd8", ENC_parallel_30, a8, b8, 0x40400180u, 0x1, 0);
	par16("uhadd16", ENC_parallel_31, a16, b16, 0x40014000u, 0x0, 0);
	par16("uhasx", ENC_parallel_32, a16, b16, 0x40004000u, 0xC, 0);
	par8("uhsub8", ENC_parallel_33, a8, b8, 0x3F3FFF7Fu, 0xD, 0);
	par16("uhsub16", ENC_parallel_34, a16, b16, 0x3FFF3FFFu, 0xF, 0);
	par16("uhsax", ENC_parallel_35, a16, b16, 0x40013FFEu, 0x3, 0);
}

static void t_region_d_qadd(void)
{
	begin("qadd r0, r1, r2", ENC_qadd_01);
	reg(1, 0x30000000); reg(2, 0x30000000);
	expect_ok(); expect_r(0, 0x60000000); expect_q(0);

	begin("qadd saturating", ENC_qadd_01);
	reg(1, 0x7FFFFFFF); reg(2, 0x7FFFFFFF);
	expect_ok(); expect_r(0, 0x7FFFFFFF); expect_q(1);

	begin("qsub r0, r1, r2", ENC_qsub_01);
	reg(1, 5); reg(2, 3);
	expect_ok(); expect_r(0, 2); expect_q(0);

	begin("qsub saturating", ENC_qsub_01);
	reg(1, 0x80000000); reg(2, 1);
	expect_ok(); expect_r(0, 0x80000000); expect_q(1);

	begin("qdadd r0, r1, r2", ENC_qdadd_01);
	reg(1, 1); reg(2, 2);
	expect_ok(); expect_r(0, 5); expect_q(0);

	begin("qdadd saturating", ENC_qdadd_01);
	reg(1, 0); reg(2, 0x40000000);
	expect_ok(); expect_r(0, 0x7FFFFFFF); expect_q(1);

	begin("qdsub r0, r1, r2", ENC_qdsub_01);
	reg(1, 10); reg(2, 3);
	expect_ok(); expect_r(0, 4); expect_q(0);

	begin("qdsub saturating", ENC_qdsub_01);
	reg(1, 0x80000000); reg(2, 0x40000000);
	expect_ok(); expect_r(0, 0x80000000); expect_q(1);
}

static void t_region_d_mul(void)
{
	begin("mul r0, r1, r2", ENC_mul_01);
	reg(1, 6); reg(2, 7);
	expect_ok(); expect_r(0, 42);

	begin("mul wrap", ENC_mul_01);
	reg(1, 0x10000); reg(2, 0x10000);
	expect_ok(); expect_r(0, 0);

	begin("mla r0, r1, r2, r3", ENC_mla_01);
	reg(1, 6); reg(2, 7); reg(3, 0x100);
	expect_ok(); expect_r(0, 0x12A);

	begin("mls r0, r1, r2, r3", ENC_mls_01);
	reg(1, 6); reg(2, 7); reg(3, 0x100);
	expect_ok(); expect_r(0, 0x0D6);

	begin("smull r0, r1, r2, r3", ENC_smull_01);
	reg(2, 0x00010000); reg(3, 0x00020000);
	expect_ok(); expect_r(0, 0); expect_r(1, 2);

	begin("smull negative", ENC_smull_01);
	reg(2, 0xFFFFFFFF); reg(3, 2);
	expect_ok(); expect_r(0, 0xFFFFFFFE); expect_r(1, 0xFFFFFFFF);

	begin("umull r0, r1, r2, r3", ENC_umull_01);
	reg(2, 0xFFFFFFFF); reg(3, 0xFFFFFFFF);
	expect_ok(); expect_r(0, 1); expect_r(1, 0xFFFFFFFE);

	begin("smlal r0, r1, r2, r3", ENC_smlal_01);
	reg(0, 0xFFFFFFFF); reg(1, 0); reg(2, 1); reg(3, 1);
	expect_ok(); expect_r(0, 0); expect_r(1, 1);

	begin("umlal r0, r1, r2, r3", ENC_umlal_01);
	reg(0, 0); reg(1, 1); reg(2, 0xFFFFFFFF); reg(3, 2);
	expect_ok(); expect_r(0, 0xFFFFFFFE); expect_r(1, 2);

	begin("umaal r0, r1, r2, r3", ENC_umaal_01);
	reg(0, 1); reg(1, 1); reg(2, 2); reg(3, 3);
	expect_ok(); expect_r(0, 8); expect_r(1, 0);

	begin("umaal carries", ENC_umaal_01);
	reg(0, 0xFFFFFFFF); reg(1, 0xFFFFFFFF);
	reg(2, 0xFFFFFFFF); reg(3, 0xFFFFFFFF);
	expect_ok(); expect_r(0, 0xFFFFFFFF); expect_r(1, 0xFFFFFFFF);

	begin("sdiv r0, r1, r2", ENC_sdiv_01);
	reg(1, 100); reg(2, 7);
	expect_ok(); expect_r(0, 14);

	begin("sdiv negative", ENC_sdiv_01);
	reg(1, 0xFFFFFF9C); reg(2, 7);
	expect_ok(); expect_r(0, 0xFFFFFFF2);

	begin("sdiv by zero", ENC_sdiv_01);
	reg(1, 5); reg(2, 0);
	expect_ok(); expect_r(0, 0);

	begin("sdiv INT_MIN / -1", ENC_sdiv_01);
	reg(1, 0x80000000); reg(2, 0xFFFFFFFF);
	expect_ok(); expect_r(0, 0x80000000);

	begin("udiv r0, r1, r2", ENC_udiv_01);
	reg(1, 100); reg(2, 7);
	expect_ok(); expect_r(0, 14);

	begin("udiv by zero", ENC_udiv_01);
	reg(1, 5); reg(2, 0);
	expect_ok(); expect_r(0, 0);

	begin("udiv max", ENC_udiv_01);
	reg(1, 0xFFFFFFFF); reg(2, 2);
	expect_ok(); expect_r(0, 0x7FFFFFFF);

	/* halfword multiplies */
	begin("smulbb", ENC_smulbb_01);
	reg(1, 0x00030004); reg(2, 0x00050006);
	expect_ok(); expect_r(0, 24);

	begin("smulbt", ENC_smulbt_01);
	reg(1, 0x00030004); reg(2, 0x00050006);
	expect_ok(); expect_r(0, 20);

	begin("smultb", ENC_smultb_01);
	reg(1, 0x00030004); reg(2, 0x00050006);
	expect_ok(); expect_r(0, 18);

	begin("smultt", ENC_smultt_01);
	reg(1, 0x00030004); reg(2, 0x00050006);
	expect_ok(); expect_r(0, 15);

	begin("smulbb negative", ENC_smulbb_01);
	reg(1, 0xFFFFFFFF); reg(2, 0x00000002);
	expect_ok(); expect_r(0, 0xFFFFFFFE);

	begin("smlabb", ENC_smlabb_01);
	reg(1, 0x00000004); reg(2, 0x00000006); reg(3, 10);
	expect_ok(); expect_r(0, 34);

	begin("smlabt", ENC_smlabt_01);
	reg(1, 0x00000004); reg(2, 0x00050006); reg(3, 100);
	expect_ok(); expect_r(0, 120);

	begin("smlatb", ENC_smlatb_01);
	reg(1, 0x00030004); reg(2, 0x00000006); reg(3, 100);
	expect_ok(); expect_r(0, 118);

	begin("smlatt", ENC_smlatt_01);
	reg(1, 0x00030004); reg(2, 0x00050006); reg(3, 100);
	expect_ok(); expect_r(0, 115);

	begin("smulwb", ENC_smulwb_01);
	reg(1, 0x00010000); reg(2, 0x00000003);
	expect_ok(); expect_r(0, 3);

	begin("smulwt", ENC_smulwt_01);
	reg(1, 0x00010000); reg(2, 0x00030000);
	expect_ok(); expect_r(0, 3);

	begin("smulwb negative", ENC_smulwb_01);
	reg(1, 0xFFFFFFFF); reg(2, 3);
	expect_ok(); expect_r(0, 0xFFFFFFFF);

	begin("smlawb", ENC_smlawb_01);
	reg(1, 0x00010000); reg(2, 3); reg(3, 0x100);
	expect_ok(); expect_r(0, 0x103);

	begin("smlawt", ENC_smlawt_01);
	reg(1, 0x00010000); reg(2, 0x00030000); reg(3, 0x100);
	expect_ok(); expect_r(0, 0x103);

	begin("smlalbb", ENC_smlalbb_01);
	reg(0, 0); reg(1, 0); reg(2, 0x00030004); reg(3, 0x00050006);
	expect_ok(); expect_r(0, 24); expect_r(1, 0);

	begin("smlalbt", ENC_smlalbt_01);
	reg(0, 0); reg(1, 0); reg(2, 0x00030004); reg(3, 0x00050006);
	expect_ok(); expect_r(0, 20); expect_r(1, 0);

	begin("smlaltb", ENC_smlaltb_01);
	reg(0, 0); reg(1, 0); reg(2, 0x00030004); reg(3, 0x00050006);
	expect_ok(); expect_r(0, 18); expect_r(1, 0);

	begin("smlaltt", ENC_smlaltt_01);
	reg(0, 0); reg(1, 0); reg(2, 0x00030004); reg(3, 0x00050006);
	expect_ok(); expect_r(0, 15); expect_r(1, 0);

	begin("smuad", ENC_smuad_01);
	reg(1, 0x00030004); reg(2, 0x00050006);
	expect_ok(); expect_r(0, 39);

	begin("smuadx", ENC_smuadx_01);
	reg(1, 0x00030004); reg(2, 0x00050006);
	expect_ok(); expect_r(0, 38);

	begin("smusd", ENC_smusd_01);
	reg(1, 0x00030004); reg(2, 0x00050006);
	expect_ok(); expect_r(0, 9);

	begin("smusdx", ENC_smusdx_01);
	reg(1, 0x00030004); reg(2, 0x00050006);
	expect_ok(); expect_r(0, 2);

	begin("smlad", ENC_smlad_01);
	reg(1, 0x00030004); reg(2, 0x00050006); reg(3, 100);
	expect_ok(); expect_r(0, 139);

	begin("smladx", ENC_smladx_01);
	reg(1, 0x00030004); reg(2, 0x00050006); reg(3, 100);
	expect_ok(); expect_r(0, 138);

	begin("smlsd", ENC_smlsd_01);
	reg(1, 0x00030004); reg(2, 0x00050006); reg(3, 100);
	expect_ok(); expect_r(0, 109);

	begin("smlsdx", ENC_smlsdx_01);
	reg(1, 0x00030004); reg(2, 0x00050006); reg(3, 100);
	expect_ok(); expect_r(0, 102);

	begin("smlald", ENC_smlald_01);
	reg(0, 0); reg(1, 0); reg(2, 0x00030004); reg(3, 0x00050006);
	expect_ok(); expect_r(0, 39); expect_r(1, 0);

	begin("smlaldx", ENC_smlaldx_01);
	reg(0, 0); reg(1, 0); reg(2, 0x00030004); reg(3, 0x00050006);
	expect_ok(); expect_r(0, 38); expect_r(1, 0);

	begin("smlsld", ENC_smlsld_01);
	reg(0, 0); reg(1, 0); reg(2, 0x00030004); reg(3, 0x00050006);
	expect_ok(); expect_r(0, 9); expect_r(1, 0);

	begin("smlsldx", ENC_smlsldx_01);
	reg(0, 0); reg(1, 0); reg(2, 0x00030004); reg(3, 0x00050006);
	expect_ok(); expect_r(0, 2); expect_r(1, 0);

	begin("smmul", ENC_smmul_01);
	reg(1, 0x00010000); reg(2, 0x00010000);
	expect_ok(); expect_r(0, 1);

	begin("smmul negative", ENC_smmul_01);
	reg(1, 0xFFFFFFFF); reg(2, 0x00010000);
	expect_ok(); expect_r(0, 0xFFFFFFFF);

	begin("smmulr", ENC_smmulr_01);
	reg(1, 0x0000C000); reg(2, 0x00010000);
	expect_ok(); expect_r(0, 1);

	begin("smmla", ENC_smmla_01);
	reg(1, 0x00010000); reg(2, 0x00010000); reg(3, 10);
	expect_ok(); expect_r(0, 11);

	begin("smmlar", ENC_smmlar_01);
	reg(1, 0x0000C000); reg(2, 0x00010000); reg(3, 10);
	expect_ok(); expect_r(0, 11);

	begin("smmls", ENC_smmls_01);
	reg(1, 0x00010000); reg(2, 0x00010000); reg(3, 10);
	expect_ok(); expect_r(0, 9);

	begin("smmlsr", ENC_smmlsr_01);
	reg(1, 0x0000C000); reg(2, 0x00010000); reg(3, 10);
	expect_ok(); expect_r(0, 9);

	begin("usad8", ENC_usad8_01);
	reg(1, 0x01020304); reg(2, 0x04030201);
	expect_ok(); expect_r(0, 8);

	begin("usada8", ENC_usada8_01);
	reg(1, 0x01020304); reg(2, 0x04030201); reg(3, 100);
	expect_ok(); expect_r(0, 108);
}


/* --------------------------------------------------- extra flag / operand edges */
static void t_edges(void)
{
	/* SP as an operand of the register forms is legal for ADD/SUB. */
	begin("add.w sp, sp, r1", ENC_add_07);
	reg(13, 0x1000); reg(1, 0x234);
	expect_ok(); expect_r(13, 0x1234);

	begin("add.w r0, sp, r2", ENC_add_08);
	reg(13, 0x80000000); reg(2, 0x10);
	expect_ok(); expect_r(0, 0x80000010);

	begin("sub.w sp, sp, r3", ENC_sub_06);
	reg(13, 0x1000); reg(3, 1);
	expect_ok(); expect_r(13, 0x0FFF);

	begin("sub.w r0, sp, #0", ENC_bi_sub_05);
	reg(13, 0x1234);
	expect_ok(); expect_r(0, 0x1234);

	/* ADC/SBC take the C flag as carry-in (the shift only changes the
	 * operand); this is the `adcs; adc.w rHi, rHi, Ry, asr #31` idiom that
	 * compilers emit for a 64-bit add of a sign-extended value. */
	begin("adcs.w r0, r1, r2, lsl #1 (C=0)", ENC_adc_04);
	flags_in(0, 0, 0, 0);
	reg(1, 0); reg(2, 0x80000000);
	expect_ok(); expect_r(0, 0);
	expect_nzcv(0, 1, 0, 0);

	begin("adcs.w r0, r1, r2, lsl #1 (C=1)", ENC_adc_04);
	flags_in(0, 0, 1, 0);
	reg(1, 0xFFFFFFFF); reg(2, 0x80000000);
	expect_ok(); expect_r(0, 0);
	expect_nzcv(0, 1, 1, 0);

	begin("adcs.w r0, r1, r2, lsl #1 (carry out)", ENC_adc_04);
	flags_in(0, 0, 0, 0);
	reg(1, 0xFFFFFFFF); reg(2, 0x40000000);
	expect_ok(); expect_r(0, 0x7FFFFFFF);
	expect_nzcv(0, 0, 1, 1);

	begin("sbcs.w r0, r1, r2, lsl #1 (C=0)", ENC_sbc_03);
	flags_in(0, 0, 0, 0);
	reg(1, 0); reg(2, 0x80000000);
	expect_ok(); expect_r(0, 0xFFFFFFFF);
	expect_nzcv(1, 0, 0, 0);

	begin("sbcs.w r0, r1, r2, lsl #1 (C=1)", ENC_sbc_03);
	flags_in(0, 0, 1, 0);
	reg(1, 5); reg(2, 0x80000000);
	expect_ok(); expect_r(0, 5);
	expect_nzcv(0, 0, 1, 0);

	begin("sbcs.w r0, r1, r2, lsl #1 (borrow)", ENC_sbc_03);
	flags_in(0, 0, 0, 0);
	reg(1, 5); reg(2, 0);
	expect_ok(); expect_r(0, 4);
	expect_nzcv(0, 0, 1, 0);

	/* RRX uses the incoming C and reports the shifted-out bit in C. */
	begin("bics.w r0, r1, r2, rrx", ENC_bic_04);
	flags_in(0, 0, 1, 0);
	reg(1, 0xFFFFFFFF); reg(2, 0x00000001);
	expect_ok(); expect_r(0, 0x7FFFFFFF);
	expect_nzcv(0, 0, 1, KEEP);

	begin("eors.w r0, r1, r2, rrx", ENC_eor_05);
	flags_in(0, 0, 0, 0);
	reg(1, 0xFFFFFFFF); reg(2, 0x00000001);
	expect_ok(); expect_r(0, 0xFFFFFFFF);
	expect_nzcv(1, 0, 1, KEEP);

	begin("teq.w r0, r1, rrx", ENC_eor_06);
	flags_in(0, 0, 1, 0);
	reg(0, 0); reg(1, 0x80000000);
	expect_ok();
	expect_nzcv(1, 0, 0, KEEP);

	begin("rrxs r0, r1", ENC_orr_11);
	flags_in(0, 0, 1, 0);
	reg(1, 0x00000003);
	expect_ok(); expect_r(0, 0x80000001);
	expect_nzcv(1, 0, 1, KEEP);

	begin("orns r0, r1, r2, asr #5", ENC_orn_06);
	reg(1, 0); reg(2, 0x80000000);
	expect_ok(); expect_r(0, 0x03FFFFFF);
	expect_nzcv(0, 0, 0, KEEP);

	begin("tst.w r0, r1, asr #1", ENC_and_10);
	reg(0, 0x80000000); reg(1, 0x00000001);
	expect_ok();
	expect_nzcv(0, 1, 1, KEEP);

	begin("cmn.w r0, r1, lsr #1", ENC_add_09);
	reg(0, 0x80000000); reg(1, 0x80000000);
	expect_ok();
	expect_nzcv(1, 0, 0, 0);

	begin("rsbs r0, r1, r2, asr #3", ENC_rsb_04);
	reg(1, 16); reg(2, 0x00000080);
	expect_ok(); expect_r(0, 0);
	expect_nzcv(0, 1, 1, 0);

	begin("subs.w r0, r1, r2, rrx", ENC_sub_07);
	flags_in(0, 0, 1, 0);
	reg(1, 0x10); reg(2, 0x00000001);
	expect_ok(); expect_r(0, 0x80000010);
	expect_nzcv(1, 0, 0, 1);

	/* shift amounts 32 and 31 in the logical forms */
	begin("and.w r0, r1, r2, lsr #32", ENC_and_11);
	reg(1, 0xFFFFFFFF); reg(2, 0x80000000);
	expect_ok(); expect_r(0, 0);

	begin("bic.w r0, r1, r2, asr #32", ENC_bic_05);
	reg(1, 0xFFFFFFFF); reg(2, 0x80000000);
	expect_ok(); expect_r(0, 0);

	/* PKHTB with a zero shift field: the assembler canonicalises
	 * `pkhtb r0,r1,r2` to `pkhbt r0,r2,r1`, so the encoding is derived from
	 * the assembled PKHBT by setting the shift type to ASR with imm5 = 0.
	 * Semantics: top of Rn, bottom of unshifted Rm. */
	begin("pkhtb r0, r1, r2 (no shift)", (ENC_pkhbt_01 & ~0x30u) | 0x20u);
	reg(1, 0x11112222); reg(2, 0x33334444);
	expect_ok(); expect_r(0, 0x11114444);

	begin("eor.w r0, r1, r2, lsl #31", ENC_eor_07);
	reg(1, 0); reg(2, 0x00000001);
	expect_ok(); expect_r(0, 0x80000000);

	/* Q is sticky: a non-saturating SSAT leaves it set. */
	begin("ssat r0, #8, r1 (Q sticky)", ENC_ssat_01);
	reg(1, 5);
	g_cpu.q = 1;
	expect_ok(); expect_r(0, 5); expect_q(1);

	begin("usat16 r0, #0, r1 (zero)", ENC_usat16_03);
	reg(1, 0);
	expect_ok(); expect_r(0, 0); expect_q(0);

	begin("usat16 r0, #0, r1 (saturates)", ENC_usat16_03);
	reg(1, 0x00010000);
	expect_ok(); expect_r(0, 0); expect_q(1);

	/* more data for the byte-reverse family */
	begin("rev16.w r0, r1 (2)", ENC_rev16_01);
	reg(1, 0xDEADBEEF);
	expect_ok(); expect_r(0, 0xADDEEFBE);

	begin("revsh.w r0, r1 (2)", ENC_revsh_01);
	reg(1, 0x00008001);
	expect_ok(); expect_r(0, 0x00000180);

	begin("revsh.w r0, r1 (negative)", ENC_revsh_01);
	reg(1, 0x000000FF);
	expect_ok(); expect_r(0, 0xFFFFFF00);

	begin("rbit r0, r1 (2)", ENC_rbit_01);
	reg(1, 0x80000000);
	expect_ok(); expect_r(0, 1);

	begin("clz r0, r1 (2)", ENC_clz_01);
	reg(1, 0x00F00000);
	expect_ok(); expect_r(0, 8);
}


/* --------------------------------------------------- breadth: extra operands
 * per form so that every mnemonic has several independent cases. */
static void t_fill(void)
{
	begin("bic.w r4, r5, r6 (all ones)", ENC_bic_01);
	reg(5, 0); reg(6, 0xFFFFFFFF);
	expect_ok(); expect_r(4, 0);

	begin("bic.w r4, r5, r6 (mask)", ENC_bic_01);
	reg(5, 0x0F0F0F0F); reg(6, 0x00FF00FF);
	expect_ok(); expect_r(4, 0x0F000F00);

	begin("rsb r0, r1, r2 (equal)", ENC_rsb_01);
	reg(1, 0x80000000); reg(2, 0x80000000);
	expect_ok(); expect_r(0, 0);

	begin("rsb r0, r1, r2 (wrap)", ENC_rsb_01);
	reg(1, 0xFFFFFFFF); reg(2, 0);
	expect_ok(); expect_r(0, 1);

	begin("tst.w r0, r1 (negative)", ENC_and_08);
	flags_in(0, 0, 1, 1);
	reg(0, 0xFFFFFFFF); reg(1, 0x80000000);
	expect_ok();
	expect_nzcv(1, 0, 1, 1);

	begin("tst.w r0, r1, lsl #2 (carry out)", ENC_and_09);
	reg(0, 0x80000000); reg(1, 0x20000000);
	expect_ok();
	expect_nzcv(1, 0, 0, KEEP);

	begin("teq.w r0, r1 (negative)", ENC_eor_03);
	reg(0, 0xFFFFFFFF); reg(1, 0);
	expect_ok();
	expect_nzcv(1, 0, KEEP, KEEP);

	begin("cmn.w r0, r1 (overflow)", ENC_add_05);
	reg(0, 0x7FFFFFFF); reg(1, 1);
	expect_ok();
	expect_nzcv(1, 0, 0, 1);

	begin("mvn.w r0, r1 (all ones)", ENC_orn_03);
	reg(1, 0xFFFFFFFF);
	expect_ok(); expect_r(0, 0);

	begin("mvn.w r0, r1 (sign)", ENC_orn_03);
	reg(1, 0x80000000);
	expect_ok(); expect_r(0, 0x7FFFFFFF);

	begin("ssat16 r0, #8, r1 (no saturation)", ENC_ssat16_01);
	reg(1, 0xFFFF0000);
	expect_ok(); expect_r(0, 0xFFFF0000); expect_q(0);

	begin("usat16 r0, #8, r1 (zero)", ENC_usat16_01);
	reg(1, 0);
	expect_ok(); expect_r(0, 0); expect_q(0);

	begin("usat16 r0, #8, r1 (one lane)", ENC_usat16_01);
	reg(1, 0x00000100);
	expect_ok(); expect_r(0, 0x000000FF); expect_q(1);

	begin("sbfx r0, r1, #4, #8 (positive)", ENC_sbfx_01);
	reg(1, 0x00000780);
	expect_ok(); expect_r(0, 0x78);

	begin("ubfx r0, r1, #4, #8 (all ones)", ENC_ubfx_01);
	reg(1, 0xFFFFFFFF);
	expect_ok(); expect_r(0, 0xFF);

	begin("bfi r0, r1, #8, #8 (zero base)", ENC_bfi_01);
	reg(0, 0); reg(1, 0x12345678);
	expect_ok(); expect_r(0, 0x7800);

	begin("bfc r0, #4, #12 (pattern)", ENC_bfc_01);
	reg(0, 0x12345678);
	expect_ok(); expect_r(0, 0x12340008);

	begin("subw r0, r1, #0x123 (borrow)", ENC_subw_01);
	reg(1, 0x100);
	expect_ok(); expect_r(0, 0xFFFFFFDD);

	begin("movt r0, #0xabcd (keep low)", ENC_movt_01);
	reg(0, 0xFFFFFFFF);
	expect_ok(); expect_r(0, 0xABCDFFFF);

	begin("rev.w r0, r1 (alternating)", ENC_rev_01);
	reg(1, 0xFF00FF00);
	expect_ok(); expect_r(0, 0x00FF00FF);

	begin("rev16.w r0, r1 (all ones)", ENC_rev16_01);
	reg(1, 0xFFFFFFFF);
	expect_ok(); expect_r(0, 0xFFFFFFFF);

	begin("revsh.w r0, r1 (sign)", ENC_revsh_01);
	reg(1, 0x00000080);
	expect_ok(); expect_r(0, 0xFFFF8000);

	begin("rbit r0, r1 (all ones)", ENC_rbit_01);
	reg(1, 0xFFFFFFFF);
	expect_ok(); expect_r(0, 0xFFFFFFFF);

	begin("clz r0, r1 (all ones)", ENC_clz_01);
	reg(1, 0xFFFFFFFF);
	expect_ok(); expect_r(0, 0);

	begin("sel r0, r1, r2 (none)", ENC_sel_01);
	ge_in(0x0);
	reg(1, 0xAABBCCDD); reg(2, 0x11223344);
	expect_ok(); expect_r(0, 0x11223344);

	begin("sel r0, r1, r2 (all)", ENC_sel_01);
	ge_in(0xF);
	reg(1, 0xAABBCCDD); reg(2, 0x11223344);
	expect_ok(); expect_r(0, 0xAABBCCDD);

	begin("umlal r0, r1, r2, r3 (small)", ENC_umlal_01);
	reg(0, 1); reg(1, 0); reg(2, 1); reg(3, 1);
	expect_ok(); expect_r(0, 2); expect_r(1, 0);

	begin("smlal r0, r1, r2, r3 (negative)", ENC_smlal_01);
	reg(0, 0); reg(1, 0); reg(2, 0xFFFFFFFF); reg(3, 1);
	expect_ok(); expect_r(0, 0xFFFFFFFF); expect_r(1, 0xFFFFFFFF);

	begin("smull r0, r1, r2, r3 (min*min)", ENC_smull_01);
	reg(2, 0x80000000); reg(3, 0x80000000);
	expect_ok(); expect_r(0, 0); expect_r(1, 0x40000000);

	begin("umaal r0, r1, r2, r3 (carry into RdHi)", ENC_umaal_01);
	reg(0, 0x80000000); reg(1, 0x80000000); reg(2, 0); reg(3, 0);
	expect_ok(); expect_r(0, 0); expect_r(1, 1);

	begin("mls r0, r1, r2, r3 (wrapped product)", ENC_mls_01);
	reg(1, 0x10000); reg(2, 0x10000); reg(3, 1);
	expect_ok(); expect_r(0, 1);

	begin("sdiv r0, r1, r2 (exact)", ENC_sdiv_01);
	reg(1, 0xFFFFFFF9); reg(2, 2);
	expect_ok(); expect_r(0, 0xFFFFFFFD);

	begin("udiv r0, r1, r2 (zero dividend)", ENC_udiv_01);
	reg(1, 0); reg(2, 3);
	expect_ok(); expect_r(0, 0);
}

/* ------------------------------------------------------------------ UNDEF */

/* Derive words from assembled encodings so no hand-typed hexadecimal instruction
 * appears here; each mutation selects an encoding the class does not implement. */
static void t_undef(void)
{
	/* Field offsets in the 32-bit word: first-halfword bits 8:5 (region A op)
	 * are word bits 24:21; the S bit is word bit 20; Rn / op1 are word bits
	 * 19:16 / 23:20; the second halfword's fixed 1111 is word bits 15:12. */

	/* region A: op field 0101 is not assigned */
	begin("and op=0101 (unassigned)", (ENC_and_01 & ~0x01E00000u) | (5u << 21));
	expect_undef();

	/* region A: op 6 (PKH) with S = 1 is UNPREDICTABLE */
	begin("pkh with S", ENC_pkhbt_01 | 0x00100000u);
	expect_undef();

	/* region A: PKH with type 01 */
	begin("pkh type 01", (ENC_pkhbt_01 & ~0x30u) | 0x10u);
	expect_undef();

	/* region A: ADD with Rn = 1111 is UNPREDICTABLE */
	begin("add.w r0, pc, r1", (ENC_add_01 & ~0x000F0000u) | 0x000F0000u);
	expect_undef();

	/* region A: MOVS with Rd = 1111 is UNPREDICTABLE (no ALU write to PC) */
	begin("movs.w pc, r1", (ENC_orr_03 & ~0xF00u) | 0xF00u);
	expect_undef();

	/* region A: CMP with S = 0 is UNPREDICTABLE */
	begin("cmp without S", ENC_sub_03 & ~0x00100000u);
	expect_undef();

	/* region B: op 0101 (unassigned) */
	begin("imm op=0101 (unassigned)", (ENC_bi_and_01 & ~0x01E00000u) | (5u << 21));
	expect_undef();

	/* region B: second halfword bit 15 set -> a branch, not data processing */
	begin("imm with second bit15", ENC_bi_and_01 | 0x8000u);
	expect_undef();

	/* region C: op5 = 1 is not assigned */
	begin("addw op5=1", (ENC_addw_01 & ~0x01F00000u) | (1u << 20));
	expect_undef();

	/* region C: BFI with msb < lsb */
	begin("bfi msb<lsb", (ENC_bfi_01 & ~0x3Fu) | 0x3u);
	expect_undef();

	/* region C: SBFX with lsb+width > 32 */
	begin("sbfx too wide", (ENC_sbfx_04 & ~0x3Fu) | 0x1Fu);
	expect_undef();

	/* region C: SSAT16 with sat > 16 */
	begin("ssat16 sat 17", ENC_ssat16_02 | 0x10u);
	expect_undef();

	/* region D: FA with second<15:12> != 1111 */
	begin("fa without 1111 field", ENC_lslr_01 & ~0x0000F000u);
	expect_undef();

	/* region D: parallel shape 1011 (unassigned) */
	begin("parallel shape 1011", (ENC_parallel_00 & ~0x00F00000u) | (0xBu << 20));
	expect_undef();

	/* region D: FB op1 = 1111 (unassigned) */
	begin("fb op1 1111", (ENC_mul_01 & ~0x00F00000u) | (0xFu << 20));
	expect_undef();

	/* not a 32-bit data-processing word at all: the branch space */
	begin("b.w-like word", 0xF0009000u);
	expect_undef();

	begin("zero word", 0x00000000u);
	expect_undef();

	begin("0xffffffff", 0xFFFFFFFFu);
	expect_undef();
}

/* ------------------------------------------------------------------ sweep */

static uint32_t xs_state = 0x12345678u;

static uint32_t xs(void)
{
	xs_state ^= xs_state << 13;
	xs_state ^= xs_state >> 17;
	xs_state ^= xs_state << 5;
	return xs_state;
}

/* 200000 random words: half drawn from the class's encoding regions, half fully
 * random.  The class must always return OK or UNDEF, never crash, and must never
 * touch the core's PC state. */
static void t_sweep(void)
{
	unsigned i;

	for (i = 0; i < 200000u; i++) {
		uint32_t insn;
		unsigned kind = xs() % 8;
		int rc;

		switch (kind) {
		case 0:	/* 0xEA/0xEB shifted register */
			insn = (0xEA000000u | (xs() & 0x01FFFFFFu));
			break;
		case 1:	/* 0xEA/0xEB again, register form */
			insn = (0xEB000000u | (xs() & 0x01FFFFFFu));
			break;
		case 2:	/* modified immediate */
			insn = (xs() & 1) ? (0xF0000000u | (xs() & 0x01FFFFFFu))
					  : (0xF4000000u | (xs() & 0x01FFFFFFu));
			insn &= ~0x00008000u;	/* keep second<15> = 0 */
			break;
		case 3:	/* plain binary immediate */
			insn = (xs() & 1) ? (0xF2000000u | (xs() & 0x01FFFFFFu))
					  : (0xF6000000u | (xs() & 0x01FFFFFFu));
			insn &= ~0x00008000u;
			break;
		case 4:	/* 0xFA00 register */
			insn = 0xFA000000u | (xs() & 0x01FFFFFFu);
			break;
		case 5:	/* 0xFB00 multiply */
			insn = 0xFB000000u | (xs() & 0x01FFFFFFu);
			break;
		default:/* anything at all */
			insn = xs();
			break;
		}

		/* random architectural state */
		for (unsigned r = 0; r < 15; r++)
			g_cpu.r[r] = xs();
		g_cpu.r[15] = PC_IN;
		g_cpu.next_pc = NPC_IN;
		g_cpu.n = xs() & 1;
		g_cpu.z = xs() & 1;
		g_cpu.cf = xs() & 1;
		g_cpu.v = xs() & 1;
		g_cpu.q = xs() & 1;
		g_cpu.ge = xs() & 0xF;

		rc = n2a32_t32_dp(&g_cpu, insn);
		if (rc != N2A32_OK && rc != N2A32_UNDEF) {
			g_fails++;
			fprintf(stderr, "FAIL sweep: rc=%d for 0x%08x\n", rc, insn);
			return;
		}
		if (g_cpu.r[15] != PC_IN || g_cpu.next_pc != NPC_IN) {
			g_fails++;
			fprintf(stderr, "FAIL sweep: PC modified for 0x%08x\n", insn);
			return;
		}
	}
	g_checks++;
}

/* ------------------------------------------------------------------ main */

int main(void)
{
	g_mem.base = calloc(1, 16u << 20);	/* guest memory below 16 MiB */
	if (!g_mem.base) {
		fprintf(stderr, "calloc failed\n");
		return 2;
	}
	g_cpu.mem = &g_mem;
	g_cpu.excl_valid = 0;
	g_cpu.icount = 0;
	g_cpu.ilimit = 0;
	g_cpu.stop_request = 0;

	t_region_a();
	t_region_b();
	t_region_c();
	t_region_d_shift();
	t_region_d_extend();
	t_region_d_misc();
	t_region_d_parallel();
	t_region_d_qadd();
	t_region_d_mul();
	t_edges();
	t_fill();
	t_undef();
	t_sweep();

	free(g_mem.base);
	if (g_fails) {
		printf("N2TEST V15 FAIL %d checks failed (%d run)\n", g_fails, g_checks);
		return 1;
	}
	printf("N2TEST V15 PASS %d/%d\n", g_checks, g_checks);
	return 0;
}
