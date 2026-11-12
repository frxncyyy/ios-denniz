/* SPDX-License-Identifier: MIT */
/*
 * t_misc.c -- host unit tests for n2a32_neon_misc (package V20).
 *
 * Encodings come from misc.s through llvm-mc (mkenc.py); no hex is typed here. Expected register and
 * flag values are literals derived from the ARM ARM pseudocode, written out in the comments.
 */
#include "n2a32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "enc_v20.h"

/* The MMIO hooks live in the core (V02); no test marks a megabyte as MMIO, so these are never called,
 * but the link needs them because the load/store helpers reference them. */
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
static int checks, fails;

static void rst(void)
{
	memset(&cpu, 0, sizeof cpu);
}

static uint32_t enc_word(const char *name)
{
	unsigned i;

	for (i = 0; i < ENC_COUNT; i++)
		if (strcmp(enc_tab[i].name, name) == 0)
			return enc_tab[i].word;
	printf("FATAL: encoding %s not in table\n", name);
	exit(1);
}

/* run one encoding; returns 1 on N2A32_OK */
static int exec(const char *name)
{
	int r = n2a32_neon_misc(&cpu, enc_word(name));

	checks++;
	if (r != N2A32_OK) {
		printf("FAIL exec %s -> %d\n", name, r);
		fails++;
		return 0;
	}
	return 1;
}

static void ck64(const char *name, unsigned reg, uint64_t want)
{
	checks++;
	if (cpu.d[reg].u64 != want) {
		fails++;
		printf("FAIL %s d%u = %016llx want %016llx\n", name, reg,
		       (unsigned long long)cpu.d[reg].u64, (unsigned long long)want);
	}
}

static void ck_flags(const char *name, uint32_t mask, uint32_t want)
{
	checks++;
	if ((cpu.fpscr & mask) != want) {
		fails++;
		printf("FAIL %s fpscr & %08x = %08x want %08x\n", name, mask,
		       cpu.fpscr & mask, want);
	}
}

static void ck_undef(const char *name)
{
	struct n2a32_cpu before;
	uint32_t w = enc_word(name);
	int r;

	before = cpu;
	r = n2a32_neon_misc(&cpu, w);
	checks++;
	if (r != N2A32_UNDEF) {
		fails++;
		printf("FAIL %s: expected UNDEF, got %d\n", name, r);
	} else if (memcmp(&before, &cpu, sizeof cpu) != 0) {
		fails++;
		printf("FAIL %s: UNDEF changed the cpu state\n", name);
	}
}

/* one source register d1 -> d0 */
static void t_d1(const char *name, uint64_t in, uint64_t want)
{
	rst();
	cpu.d[1].u64 = in;
	if (exec(name))
		ck64(name, 0, want);
}

/* one source q1 -> q0 (two words) */
static void t_q1(const char *name, uint64_t w0, uint64_t w1, uint64_t want0, uint64_t want1)
{
	rst();
	cpu.d[2].u64 = w0;
	cpu.d[3].u64 = w1;
	if (exec(name)) {
		ck64(name, 0, want0);
		ck64(name, 1, want1);
	}
}

int main(void)
{
	/* ---------------- VREV64/32/16 ---------------- */
	/* reverse the 8 bytes of 00 01 02 03 04 05 06 07 -> 07 06 05 04 03 02 01 00 */
	t_d1("rev64_8_d", 0x0706050403020100ull, 0x0001020304050607ull);
	/* reverse the 4 halfwords 1111 2222 3333 4444 -> 4444 3333 2222 1111 */
	t_d1("rev64_16_d", 0x4444333322221111ull, 0x1111222233334444ull);
	/* swap the two words */
	t_d1("rev64_32_d", 0x8888888877777777ull, 0x7777777788888888ull);
	t_q1("rev64_8_q", 0x0706050403020100ull, 0x0F0E0D0C0B0A0908ull,
	     0x0001020304050607ull, 0x08090A0B0C0D0E0Full);
	t_q1("rev64_16_q", 0x4444333322221111ull, 0x8888777766665555ull,
	     0x1111222233334444ull, 0x5555666677778888ull);
	t_q1("rev64_32_q", 0x8888888877777777ull, 0xCCCCCCCCBBBBBBBBull,
	     0x7777777788888888ull, 0xBBBBBBBBCCCCCCCCull);
	/* VREV32.8: reverse the bytes inside each word: 22221111 -> 11112222 */
	t_d1("rev32_8_d", 0x4444333322221111ull, 0x3333444411112222ull);
	t_d1("rev32_16_d", 0x4444333322221111ull, 0x3333444411112222ull);
	t_q1("rev32_8_q", 0x4444333322221111ull, 0x8888777766665555ull,
	     0x3333444411112222ull, 0x7777888855556666ull);
	t_q1("rev32_16_q", 0x4444333322221111ull, 0x8888777766665555ull,
	     0x3333444411112222ull, 0x7777888855556666ull);
	/* VREV16.8: swap the two bytes of every halfword */
	t_d1("rev16_8_d", 0x8877665544332211ull, 0x7788556633441122ull);
	t_q1("rev16_8_q", 0x8877665544332211ull, 0xFFEEDDCCBBAA9988ull,
	     0x7788556633441122ull, 0xEEFFCCDDAABB8899ull);
	/* high registers: D bit and M bit */
	rst();
	cpu.d[17].u64 = 0x1122334455667788ull;
	if (exec("rev64_32_hi"))
		ck64("rev64_32_hi", 16, 0x5566778811223344ull);

	/* ---------------- VPADDL ---------------- */
	/* .S8: (08+07)=0x0F (06+05)=0x0B (04+03)=0x07 (02+01)=0x03 */
	t_d1("paddl_s8_d", 0x0102030405060708ull, 0x00030007000B000Full);
	/* .S16: four lanes of 0x8000 (-32768) -> 0xFFFF0000 each */
	t_d1("paddl_s16_d", 0x8000800080008000ull, 0xFFFF0000FFFF0000ull);
	/* .S32: 0x80000000 + 0x00000001 = 0x80000001 */
	t_d1("paddl_s32_d", 0x0000000180000000ull, 0xFFFFFFFF80000001ull);
	/* .U8: 0xFF + 0xFF = 0x1FE in every halfword */
	t_d1("paddl_u8_d", 0xFFFFFFFFFFFFFFFFull, 0x01FE01FE01FE01FEull);
	t_q1("paddl_u16_q", 0xFFFFFFFFFFFFFFFFull, 0x0001000200030004ull,
	     0x0001FFFE0001FFFEull, 0x0000000300000007ull);
	t_q1("paddl_u32_q", 0x0000000200000001ull, 0xFFFFFFFF00000000ull,
	     0x0000000000000003ull, 0x00000000FFFFFFFFull);

	/* ---------------- VPADAL ---------------- */
	/* .S8 with a destination of 1 in every halfword: 0x0F+1, 0x0B+1, 0x07+1, 0x03+1 */
	rst();
	cpu.d[0].u64 = 0x0001000100010001ull;
	cpu.d[1].u64 = 0x0102030405060708ull;
	if (exec("padal_s8_d"))
		ck64("padal_s8_d", 0, 0x00040008000C0010ull);
	/* .U16: 0xFFFF + (0x0004+0x0003) = 0x100000006 -> 0x00000006 (wraps, no saturation) */
	rst();
	cpu.d[0].u64 = 0xFFFFFFFFFFFFFFFFull;
	cpu.d[1].u64 = 0x0001000200030004ull;
	if (exec("padal_u16_d"))
		ck64("padal_u16_d", 0, 0x0000000200000006ull);
	/* q0 = (1, 2); q1 = (0x0000000200000001, 0x0000000400000003)
	 * word0: 1 + (1+2) = 4; word1: 2 + (3+4) = 9 */
	rst();
	cpu.d[0].u64 = 0x0000000000000001ull;
	cpu.d[1].u64 = 0x0000000000000002ull;
	cpu.d[2].u64 = 0x0000000200000001ull;
	cpu.d[3].u64 = 0x0000000400000003ull;
	if (exec("padal_s32_q")) {
		ck64("padal_s32_q", 0, 4);
		ck64("padal_s32_q", 1, 9);
	}
	rst();
	cpu.d[2].u64 = 0x0102030405060708ull;   /* q1 = d2,d3 */
	cpu.d[3].u64 = 0x1112131415161718ull;
	if (exec("padal_u8_q")) {
		ck64("padal_u8_q", 0, 0x00030007000B000Full);
		ck64("padal_u8_q", 1, 0x00230027002B002Full);
	}

	/* ---------------- VCLS / VCLZ ---------------- */
	/* bytes 00 01 7F 80 FF FE 40 C0 -> cls 7 6 0 0 7 6 0 1 */
	t_d1("cls_s8_d", 0xC040FEFF807F0100ull, 0x0100060700000607ull);
	/* halfwords 0001 0000 7FFF 8000 -> 14 15 0 0 */
	t_d1("cls_s16_d", 0x80007FFF00000001ull, 0x00000000000F000Eull);
	/* words 00000001 80000000 -> 30 0 */
	t_d1("cls_s32_d", 0x8000000000000001ull, 0x000000000000001Eull);
	t_q1("cls_s8_q", 0x00FF00FF00FF00FFull, 0xFFFFFFFF00000000ull,
	     0x0707070707070707ull, 0x0707070707070707ull);
	/* bytes 00 01 02 80 FF 0F 10 00 -> clz 8 7 6 0 0 4 3 8 */
	t_d1("clz_i8_d", 0x00100FFF80020100ull, 0x0803040000060708ull);
	/* halfwords 8000 0000 0000 0001 -> 0 16 16 15 ; 0000 0000 0001 0000 -> 16 16 15 16 */
	t_q1("clz_i16_q", 0x0001000000008000ull, 0x0000000100000000ull,
	     0x000F001000100000ull, 0x0010000F00100010ull);
	t_q1("clz_i32_q", 0x0000000000000001ull, 0x8000000000000000ull,
	     0x000000200000001Full, 0x0000000000000020ull);

	/* ---------------- VCNT / VMVN ---------------- */
	/* FF 00 0F F0 80 01 7F 55 -> 8 0 4 4 1 1 7 4 */
	t_d1("cnt_8_d", 0x557F0180F00F00FFull, 0x0407010104040008ull);
	t_q1("cnt_8_q", 0x557F0180F00F00FFull, 0x0000000000000000ull,
	     0x0407010104040008ull, 0);
	t_d1("mvn_d", 0x0123456789ABCDEFull, 0xFEDCBA9876543210ull);
	t_q1("mvn_q", 0x0123456789ABCDEFull, 0xFEDCBA9876543210ull,
	     0xFEDCBA9876543210ull, 0x0123456789ABCDEFull);

	/* ---------------- VQABS / VQNEG ---------------- */
	/* bytes 80 FF 01 7F 00 FE 80 10 -> 7F 01 01 7F 00 02 7F 10 ; 0x80 saturates -> QC */
	rst();
	cpu.d[1].u64 = 0x1080FE007F01FF80ull;
	if (exec("qabs_s8_d")) {
		ck64("qabs_s8_d", 0, 0x107F02007F01017Full);
		ck_flags("qabs_s8_d", 0x08000000u, 0x08000000u);
	}
	/* halfwords 0000 FFFF 7FFF 8000 -> 0000 0001 7FFF 7FFF */
	rst();
	cpu.d[1].u64 = 0x80007FFFFFFF0000ull;
	if (exec("qabs_s16_d"))
		ck64("qabs_s16_d", 0, 0x7FFF7FFF00010000ull);
	/* words of q1: 80000001 80000000 | 00000000 FFFFFFFF -> 7FFFFFFF 7FFFFFFF | 1 0 */
	rst();
	cpu.d[2].u64 = 0x8000000080000001ull;
	cpu.d[3].u64 = 0x00000000FFFFFFFFull;
	if (exec("qabs_s32_q")) {
		ck64("qabs_s32_q", 0, 0x7FFFFFFF7FFFFFFFull);
		ck64("qabs_s32_q", 1, 0x0000000000000001ull);
	}
	/* VQNEG of the same bytes: 80->7F(sat) FF->01 01->FF 7F->81 00->00 FE->02 80->7F 10->F0 */
	t_d1("qneg_s8_d", 0x1080FE007F01FF80ull, 0xF07F020081FF017Full);
	/* halfwords 0000 FFFF 7FFF 8000 -> 0000 0001 8001 7FFF */
	t_q1("qneg_s16_q", 0x80007FFFFFFF0000ull, 0,
	     0x7FFF800100010000ull, 0);
	/* words 80000000 00000001 -> 7FFFFFFF (saturating) FFFFFFFF */
	t_d1("qneg_s32_d", 0x0000000180000000ull, 0xFFFFFFFF7FFFFFFFull);

	/* ---------------- VCMP #0, integer ---------------- */
	/* bytes 01 FF 00 80 7F 81 02 FE -> >0: FF 00 00 00 FF 00 FF 00 */
	t_d1("cgt_s8", 0xFE02817F8000FF01ull, 0x00FF00FF000000FFull);
	/* halfwords 0001 FFFF 0000 8000 -> >=0: FFFF 0000 FFFF 0000 */
	t_d1("cge_s16", 0x80000000FFFF0001ull, 0x0000FFFF0000FFFFull);
	/* words 00000000 80000000 -> ==0: FFFFFFFF 00000000 */
	t_d1("ceq_s32", 0x8000000000000000ull, 0x00000000FFFFFFFFull);
	/* VCLT.S16: halfwords 0001 FFFF 0000 8000 -> <0: 0000 FFFF 0000 FFFF */
	rst();
	cpu.d[2].u64 = 0x80000000FFFF0001ull;
	cpu.d[3].u64 = 0;
	if (exec("clt_s16_q")) {
		ck64("clt_s16_q", 0, 0xFFFF0000FFFF0000ull);
		ck64("clt_s16_q", 1, 0);
	}
	/* VCLE.S8: bytes 01 FF 00 80 7F 81 02 FE -> <=0: 00 FF FF FF 00 FF 00 FF */
	rst();
	cpu.d[2].u64 = 0xFE02817F8000FF01ull;
	cpu.d[3].u64 = 0;
	if (exec("cle_s8_q")) {
		ck64("cle_s8_q", 0, 0xFF00FF00FFFFFF00ull);
		ck64("cle_s8_q", 1, 0xFFFFFFFFFFFFFFFFull);   /* the second source word is zero: 0 <= 0 */
	}
	/* VCGT.S32: 1 0 -> FFFFFFFF 00000000 */
	t_d1("cgt_s32_d", 0x0000000000000001ull, 0x00000000FFFFFFFFull);

	/* ---------------- VCMP #0, float ---------------- */
	/* lanes +1.0 -0.0 : > 0 -> FFFFFFFF 00000000 */
	t_d1("cgt_f32", 0x800000003F800000ull, 0x00000000FFFFFFFFull);
	/* lanes -0.0 +0.0 : >= 0 -> both true (IEEE -0 == 0) */
	t_q1("cge_f32_q", 0x0000000080000000ull, 0x0000000000000000ull,
	     0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull);
	/* lanes +0.0 -0.0 : == 0 -> both true */
	t_d1("ceq_f32", 0x8000000000000000ull, 0xFFFFFFFFFFFFFFFFull);
	/* lanes -1.0 +2.0 : <= 0 -> FFFFFFFF 00000000 */
	rst();
	cpu.d[2].u64 = 0x40000000BF800000ull;
	cpu.d[3].u64 = 0;
	if (exec("cle_f32_q")) {
		ck64("cle_f32_q", 0, 0x00000000FFFFFFFFull);
		ck64("cle_f32_q", 1, 0xFFFFFFFFFFFFFFFFull);
	}
	/* lanes -1.0 +1.0 : < 0 -> FFFFFFFF 00000000 */
	t_d1("clt_f32", 0x3F800000BF800000ull, 0x00000000FFFFFFFFull);

	/* ---------------- VABS / VNEG ---------------- */
	/* bytes 80 01 FF 7F 00 10 FE 00 -> 80 01 01 7F 00 10 02 00 (INT8_MIN stays, no saturation) */
	t_d1("abs_s8", 0x00FE10007FFF0180ull, 0x000210007F010180ull);
	/* halfwords 8000 0001 FFFF 0002 -> 8000 0001 0001 0002 */
	t_d1("abs_s16", 0x0002FFFF00018000ull, 0x0002000100018000ull);
	/* words 80000000 FFFFFFFE -> 80000000 00000002 */
	t_d1("abs_s32", 0xFFFFFFFE80000000ull, 0x0000000280000000ull);
	/* VNEG.S8 of q1 = (0102030405060708, 80...) */
	t_q1("neg_s8_q", 0x0102030405060708ull, 0x00FF00008001807Full,
	     0xFFFEFDFCFBFAF9F8ull, 0x0001000080FF8081ull);
	/* VNEG.S16: halfwords 0001 FFFF 7FFF 8000 -> FFFF 0001 8001 8000 */
	t_q1("neg_s16_q", 0x80007FFFFFFF0001ull, 0,
	     0x800080010001FFFFull, 0);
	/* VNEG.S32: 00000001 80000000 -> FFFFFFFF 80000000 */
	t_d1("neg_s32", 0x8000000000000001ull, 0x80000000FFFFFFFFull);
	/* VABS.F32: -2.0 -> 2.0 ; 1.0 -> 1.0 */
	t_d1("abs_f32", 0x3F800000C0000000ull, 0x3F80000040000000ull);
	/* VNEG.F32 q1 = (1.0, -2.0) -> (-1.0, 2.0) */
	rst();
	cpu.d[2].u64 = 0xC00000003F800000ull;
	cpu.d[3].u64 = 0;
	if (exec("neg_f32_q")) {
		ck64("neg_f32_q", 0, 0x40000000BF800000ull);
		ck64("neg_f32_q", 1, 0x8000000080000000ull);   /* VNEG of +0.0 is -0.0 */
	}
	/* VABS.F32 q: (-0.0, -inf) -> (+0.0, +inf) */
	rst();
	cpu.d[2].u64 = 0xFF80000080000000ull;
	cpu.d[3].u64 = 0;
	if (exec("abs_f32_q")) {
		ck64("abs_f32_q", 0, 0x7F80000000000000ull);
		ck64("abs_f32_q", 1, 0);
	}

	/* NaN handling of VABS/VNEG: FPAbs/FPNeg are pure sign operations (the VABS description:
	 * "The floating-point version only clears the sign bit"), so the NaN is copied with its sign
	 * cleared/flipped and is never replaced by the default NaN, even with FPSCR.DN = 1. */
	rst();
	cpu.d[1].u64 = 0xFF8000017F800001ull;   /* lanes: sNaN 0x7F800001, sNaN 0xFF800001 */
	if (exec("abs_f32"))
		ck64("abs_f32_nan", 0, 0x7F8000017F800001ull);
	rst();
	cpu.d[2].u64 = 0xFF8000017F800001ull;
	cpu.d[3].u64 = 0;
	if (exec("neg_f32_q")) {
		ck64("neg_f32_nan", 0, 0x7F800001FF800001ull);
		ck64("neg_f32_q", 1, 0x8000000080000000ull);   /* VNEG of +0.0 in the second word */
	}
	rst();
	cpu.fpscr = 0x02000000u;               /* DN = 1 must not change VABS/VNEG */
	cpu.d[1].u64 = 0x7F800001FF800001ull;
	if (exec("abs_f32"))
		ck64("abs_f32_dn", 0, 0x7F8000017F800001ull);

	/* ---------------- VSWP ---------------- */
	rst();
	cpu.d[0].u64 = 0x1111111111111111ull;
	cpu.d[1].u64 = 0x2222222222222222ull;
	if (exec("swp_d")) {
		ck64("swp_d", 0, 0x2222222222222222ull);
		ck64("swp_d", 1, 0x1111111111111111ull);
	}
	rst();
	cpu.d[0].u64 = 0x1111111111111111ull;
	cpu.d[1].u64 = 0x2222222222222222ull;
	cpu.d[2].u64 = 0x3333333333333333ull;
	cpu.d[3].u64 = 0x4444444444444444ull;
	if (exec("swp_q")) {
		ck64("swp_q", 0, 0x3333333333333333ull);
		ck64("swp_q", 1, 0x4444444444444444ull);
		ck64("swp_q", 2, 0x1111111111111111ull);
		ck64("swp_q", 3, 0x2222222222222222ull);
	}
	rst();
	cpu.d[16].u64 = 0xAAAAAAAAAAAAAAAAull;
	cpu.d[17].u64 = 0x5555555555555555ull;
	if (exec("swp_hi")) {
		ck64("swp_hi", 16, 0x5555555555555555ull);
		ck64("swp_hi", 17, 0xAAAAAAAAAAAAAAAAull);
	}

	/* ---------------- VTRN / VUZP / VZIP ---------------- */
	/* VTRN.8 d0,d1 with d0 = 00 01 .. 07, d1 = 10 11 .. 17:
	 * result d0 = 00 10 02 12 04 14 06 16 ; d1 = 01 11 03 13 05 15 07 17 */
	rst();
	cpu.d[0].u64 = 0x0706050403020100ull;
	cpu.d[1].u64 = 0x1716151413121110ull;
	if (exec("trn_8_d")) {
		ck64("trn_8_d", 0, 0x1606140412021000ull);
		ck64("trn_8_d", 1, 0x1707150513031101ull);
	}
	/* VTRN.16: halfwords d0 = 0000 0001 0002 0003, d1 = 0010 0011 0012 0013
	 * -> d0 = 0000 0010 0002 0012 ; d1 = 0001 0011 0003 0013 */
	rst();
	cpu.d[0].u64 = 0x0003000200010000ull;
	cpu.d[1].u64 = 0x0013001200110010ull;
	if (exec("trn_16_d")) {
		ck64("trn_16_d", 0, 0x0012000200100000ull);
		ck64("trn_16_d", 1, 0x0013000300110001ull);
	}
	/* VTRN.32: words d0 = 00000000 11111111, d1 = 22222222 33333333
	 * -> d0 = 00000000 22222222 ; d1 = 11111111 33333333 */
	rst();
	cpu.d[0].u64 = 0x1111111100000000ull;
	cpu.d[1].u64 = 0x3333333322222222ull;
	if (exec("trn_32_d")) {
		ck64("trn_32_d", 0, 0x2222222200000000ull);
		ck64("trn_32_d", 1, 0x3333333311111111ull);
	}
	/* VUZP.8 d0,d1: concatenation d0:d1 = 00..07,10..17
	 * -> d0 = even bytes 00 02 04 06 10 12 14 16 ; d1 = odd 01 03 05 07 11 13 15 17 */
	rst();
	cpu.d[0].u64 = 0x0706050403020100ull;
	cpu.d[1].u64 = 0x1716151413121110ull;
	if (exec("uzp_8_d")) {
		ck64("uzp_8_d", 0, 0x1614121006040200ull);
		ck64("uzp_8_d", 1, 0x1715131107050301ull);
	}
	/* VZIP.8 d0,d1 -> d0 = 00 10 01 11 02 12 03 13 ; d1 = 04 14 05 15 06 16 07 17 */
	rst();
	cpu.d[0].u64 = 0x0706050403020100ull;
	cpu.d[1].u64 = 0x1716151413121110ull;
	if (exec("zip_8_d")) {
		ck64("zip_8_d", 0, 0x1303120211011000ull);
		ck64("zip_8_d", 1, 0x1707160615051404ull);
	}
	/* VUZP.16 d0,d1: halfwords 0000 0001 0002 0003 | 0010 0011 0012 0013
	 * -> d0 = 0000 0002 0010 0012 ; d1 = 0001 0003 0011 0013 */
	rst();
	cpu.d[0].u64 = 0x0003000200010000ull;
	cpu.d[1].u64 = 0x0013001200110010ull;
	if (exec("uzp_16_d")) {
		ck64("uzp_16_d", 0, 0x0012001000020000ull);
		ck64("uzp_16_d", 1, 0x0013001100030001ull);
	}
	/* VTRN.8 q0,q1 over 16 bytes */
	rst();
	cpu.d[0].u64 = 0x0706050403020100ull;
	cpu.d[1].u64 = 0x0F0E0D0C0B0A0908ull;
	cpu.d[2].u64 = 0x1716151413121110ull;
	cpu.d[3].u64 = 0x1F1E1D1C1B1A1918ull;
	if (exec("trn_8_q")) {
		ck64("trn_8_q", 0, 0x1606140412021000ull);
		ck64("trn_8_q", 1, 0x1E0E1C0C1A0A1808ull);
		ck64("trn_8_q", 2, 0x1707150513031101ull);
		ck64("trn_8_q", 3, 0x1F0F1D0D1B0B1909ull);
	}
	/* VUZP.32 q0,q1: words 00000000..00000003 | 00000010..00000013
	 * -> q0 = 00000000 00000002 00000010 00000012 ; q1 = 00000001 00000003 00000011 00000013 */
	rst();
	cpu.d[0].u64 = 0x0000000100000000ull;
	cpu.d[1].u64 = 0x0000000300000002ull;
	cpu.d[2].u64 = 0x0000001100000010ull;
	cpu.d[3].u64 = 0x0000001300000012ull;
	if (exec("uzp_32_q")) {
		ck64("uzp_32_q", 0, 0x0000000200000000ull);
		ck64("uzp_32_q", 1, 0x0000001200000010ull);
		ck64("uzp_32_q", 2, 0x0000000300000001ull);
		ck64("uzp_32_q", 3, 0x0000001300000011ull);
	}
	/* VZIP.32 q0,q1 -> q0 = 00000000 00000010 00000001 00000011 ; q1 = 00000002 00000012 00000003 00000013 */
	rst();
	cpu.d[0].u64 = 0x0000000100000000ull;
	cpu.d[1].u64 = 0x0000000300000002ull;
	cpu.d[2].u64 = 0x0000001100000010ull;
	cpu.d[3].u64 = 0x0000001300000012ull;
	if (exec("zip_32_q")) {
		ck64("zip_32_q", 0, 0x0000001000000000ull);
		ck64("zip_32_q", 1, 0x0000001100000001ull);
		ck64("zip_32_q", 2, 0x0000001200000002ull);
		ck64("zip_32_q", 3, 0x0000001300000003ull);
	}

	/* ---------------- VMOVN / VQMOVN / VQMOVUN ---------------- */
	/* VMOVN.I16 of halfwords 1111 2222 3333 4444 -> bytes 11 22 33 44 */
	rst();
	cpu.d[0].u64 = 0x4444333322221111ull;
	cpu.d[1].u64 = 0;
	if (exec("movn_16"))
		ck64("movn_16", 0, 0x44332211ull);
	/* VMOVN.I32 of words 11111111 22222222 -> halfwords 1111 2222 */
	rst();
	cpu.d[0].u64 = 0x2222222211111111ull;
	cpu.d[1].u64 = 0;
	if (exec("movn_32"))
		ck64("movn_32", 0, 0x22221111ull);
	/* VMOVN.I64 of words (q0) -> 32-bit halves: d0 gets the low words */
	rst();
	cpu.d[0].u64 = 0x1111111122222222ull;
	cpu.d[1].u64 = 0x3333333344444444ull;
	if (exec("movn_64"))
		ck64("movn_64", 0, 0x4444444422222222ull);
	/* VQMOVN.S16 of halfwords 0080 FF80 7FFF 8000 -> 7F 80 7F 80 */
	rst();
	cpu.d[0].u64 = 0x80007FFFFF800080ull;
	cpu.d[1].u64 = 0;
	if (exec("qmovn_s16"))
		ck64("qmovn_s16", 0, 0x00000000807F807Full);
	/* VQMOVN.U16 of halfwords 0080 FF80 7FFF 0000 -> 80 FF FF 00 */
	rst();
	cpu.d[0].u64 = 0x00007FFFFF800080ull;
	cpu.d[1].u64 = 0;
	if (exec("qmovn_u16"))
		ck64("qmovn_u16", 0, 0x0000000000FFFF80ull);
	/* VQMOVUN.S16 of 0080 FF80 7FFF 8000 -> 80 00 FF 00 */
	rst();
	cpu.d[0].u64 = 0x80007FFFFF800080ull;
	cpu.d[1].u64 = 0;
	if (exec("qmovun_s16"))
		ck64("qmovun_s16", 0, 0x0000000000FF0080ull);
	/* VQMOVN.S32 of words 00000080 FFFFFF80 -> 7F 80 */
	rst();
	cpu.d[0].u64 = 0xFFFFFF8000000080ull;
	cpu.d[1].u64 = 0;
	if (exec("qmovn_s32"))
		ck64("qmovn_s32", 0, 0x00000000FF800080ull);
	/* VQMOVN.U32 of words 00000080 FFFFFFFF -> 80 FF */
	rst();
	cpu.d[0].u64 = 0xFFFFFFFF00000080ull;
	cpu.d[1].u64 = 0;
	if (exec("qmovn_u32"))
		ck64("qmovn_u32", 0, 0x00000000FFFF0080ull);
	/* VQMOVUN.S32 of words FFFFFFFF 0000007F -> 00 7F */
	rst();
	cpu.d[0].u64 = 0x0000007FFFFFFFFFull;
	cpu.d[1].u64 = 0;
	if (exec("qmovun_s32"))
		ck64("qmovun_s32", 0, 0x00000000007F0000ull);
	/* high registers: vmovn.i32 d16, q9 */
	rst();
	cpu.d[18].u64 = 0x2222222211111111ull;
	cpu.d[19].u64 = 0x4444444433333333ull;
	if (exec("movn_32_hi"))
		ck64("movn_32_hi", 16, 0x4444333322221111ull);

	/* ---------------- VSHLL ---------------- */
	/* .I8: bytes 11 22 33 44 55 66 77 88 -> halfwords 1100 2200 .. 8800 */
	rst();
	cpu.d[1].u64 = 0x8877665544332211ull;
	if (exec("shll_8")) {
		ck64("shll_8", 0, 0x4400330022001100ull);
		ck64("shll_8", 1, 0x8800770066005500ull);
	}
	/* .I16: halfwords 2222 0001 FFFF 8001 -> words 22220000 00010000 FFFF0000 80010000 */
	rst();
	cpu.d[1].u64 = 0x8001FFFF00012222ull;
	if (exec("shll_16")) {
		ck64("shll_16", 0, 0x0001000022220000ull);
		ck64("shll_16", 1, 0x80010000FFFF0000ull);
	}
	/* .I32: words 00000001 80000002 -> 0000000100000000 8000000200000000 */
	rst();
	cpu.d[1].u64 = 0x8000000200000001ull;
	if (exec("shll_32")) {
		ck64("shll_32", 0, 0x0000000100000000ull);
		ck64("shll_32", 1, 0x8000000200000000ull);
	}

	/* ---------------- VCVT float <-> integer ---------------- */
	/* Advanced SIMD VCVT has instruction-fixed rounding (ARM ARM F6.1.60): the FP -> integer
	 * operation uses Round towards Zero, the integer -> FP operation Round to Nearest (ties to
	 * even).  FPSCR.RMode is not consulted (StandardFPSCRValue() has RMode = RN). */
	/* VCVT.F32.S32 of 1, -1 -> 1.0, -1.0 */
	t_d1("cvt_f32_s32", 0xFFFFFFFF00000001ull, 0xBF8000003F800000ull);
	/* VCVT.F32.U32 of 1, 0xFFFFFFFF -> 1.0, 4294967296.0 (RNE) */
	t_d1("cvt_f32_u32", 0xFFFFFFFF00000001ull, 0x4F8000003F800000ull);
	/* VCVT.F32.S32 of INT32_MAX, 1 -> 2147483648.0 (RNE), 1.0, even with FPSCR.RMode = RZ
	 * (RZ would give 2147483520.0 = 0x4EFFFFFF). */
	rst();
	cpu.fpscr = 3u << 22;
	cpu.d[1].u64 = 0x000000017FFFFFFFull;
	if (exec("cvt_f32_s32"))
		ck64("cvt_f32_s32_rne", 0, 0x3F8000004F000000ull);
	/* VCVT.S32.F32 of 1.5, -1.5 -> 1, -1 (round towards zero) */
	t_d1("cvt_s32_f32", 0xBFC000003FC00000ull, 0xFFFFFFFF00000001ull);
	/* VCVT.U32.F32 of -1.0, 3.5 -> 0 (saturated, QC set), 3 (truncated) */
	rst();
	cpu.d[1].u64 = 0x40600000BF800000ull;
	if (exec("cvt_u32_f32")) {
		ck64("cvt_u32_f32", 0, 0x0000000300000000ull);
		ck_flags("cvt_u32_f32", 0x08000000u, 0x08000000u);
	}
	/* FPSCR.RMode = RZ, RM and RP: VCVT.S32.F32 still truncates (1, -1) */
	rst();
	cpu.fpscr = 3u << 22;
	cpu.d[1].u64 = 0xBFC000003FC00000ull;
	if (exec("cvt_s32_f32"))
		ck64("cvt_s32_f32_rz", 0, 0xFFFFFFFF00000001ull);
	rst();
	cpu.fpscr = 2u << 22;
	cpu.d[1].u64 = 0xBFC000003FC00000ull;
	if (exec("cvt_s32_f32"))
		ck64("cvt_s32_f32_rm", 0, 0xFFFFFFFF00000001ull);
	rst();
	cpu.fpscr = 1u << 22;
	cpu.d[1].u64 = 0xBFC000003FC00000ull;
	if (exec("cvt_s32_f32"))
		ck64("cvt_s32_f32_rp", 0, 0xFFFFFFFF00000001ull);
	/* q forms */
	rst();
	cpu.d[2].u64 = 0x0000000400000003ull;
	cpu.d[3].u64 = 0;
	if (exec("cvt_f32_s32_q")) {
		ck64("cvt_f32_s32_q", 0, 0x4080000040400000ull);
		ck64("cvt_f32_s32_q", 1, 0);
	}
	rst();
	cpu.d[2].u64 = 0x7F800000FF800000ull;
	cpu.d[3].u64 = 0;
	if (exec("cvt_s32_f32_q")) {
		ck64("cvt_s32_f32_q", 0, 0x7FFFFFFF80000000ull);   /* -inf -> INT_MIN, +inf -> INT_MAX */
		ck64("cvt_s32_f32_q", 1, 0);
	}

	/* ---------------- VRECPE / VRSQRTE ---------------- */
	/* 1/4.0 = 0.25 exactly */
	t_d1("recpe_f32", 0x4080000040800000ull, 0x3E8000003E800000ull);
	/* 1/0 = +inf, 1/inf = +0 */
	t_d1("recpe_f32", 0x7F80000000000000ull, 0x000000007F800000ull);
	/* 1/sqrt(4.0) = 0.5 ; 1/sqrt(0.25) = 2.0 */
	t_d1("rsqrte_f32", 0x3E80000040800000ull, 0x400000003F000000ull);
	/* VRECPE.U32: 2^32 / 0x80000000 = 2 ; 0 -> 0xFFFFFFFF */
	t_d1("recpe_u32", 0x8000000000000000ull, 0x00000002FFFFFFFFull);
	t_d1("recpe_u32", 0x0000000080000000ull, 0xFFFFFFFF00000002ull);
	/* VRSQRTE.U32: 2^32 / sqrt(4) = 0x80000000 */
	t_d1("rsqrte_u32", 0x0000000400000004ull, 0x8000000080000000ull);
	rst();
	cpu.d[2].u64 = 0x4080000040800000ull;
	cpu.d[3].u64 = 0x4080000040800000ull;
	if (exec("recpe_f32_q")) {
		ck64("recpe_f32_q", 0, 0x3E8000003E800000ull);
		ck64("recpe_f32_q", 1, 0x3E8000003E800000ull);
	}
	/* property: |1/sqrt(2) - result| <= 2^-8 * 1/sqrt(2) (architectural bound) */
	rst();
	cpu.d[1].u64 = 0x0000000040000000ull;
	if (exec("rsqrte_f32")) {
		float r;
		memcpy(&r, &cpu.d[0].u32[0], 4);
		checks++;
		if (!(r > 0.7f && r < 0.72f)) {
			fails++;
			printf("FAIL rsqrte_f32 1/sqrt(2) = %.9g\n", (double)r);
		}
	}

	/* clamping: 2^32/1 and 2^32/sqrt(1) are both 2^32, which saturates to 0xFFFFFFFF */
	rst();
	cpu.d[1].u64 = 0x0000000100000001ull;
	if (exec("recpe_u32_small"))
		ck64("recpe_u32_small", 0, 0xFFFFFFFFFFFFFFFFull);
	rst();
	cpu.d[1].u64 = 0x0000000100000001ull;
	if (exec("rsqrte_u32_small"))
		ck64("rsqrte_u32_small", 0, 0xFFFFFFFFFFFFFFFFull);
	/* 2^32/2 = 0x80000000 */
	rst();
	cpu.d[1].u64 = 0x0000000200000002ull;
	if (exec("recpe_u32_small"))
		ck64("recpe_u32_small", 0, 0x8000000080000000ull);
	/* Out-of-range FP inputs (armasm VRECPE/VRSQRTE table): VRECPE maps +-0 and denormals
	 * (flushed) to +-inf, +-inf to +-0 and |x| >= 2^126 to +-0; VRSQRTE maps +-0 to +-inf,
	 * +inf to +0, and NaN, negative finite values and -inf to the default NaN. */
	rst();
	cpu.d[1].u64 = 0x7F00000080000000ull;   /* lanes -0.0, 2^127 */
	if (exec("recpe_f32"))
		ck64("recpe_f32_special", 0, 0x00000000FF800000ull);
	rst();
	cpu.d[1].u64 = 0xFF80000000000000ull;   /* lanes +0.0, -inf */
	if (exec("recpe_f32"))
		ck64("recpe_f32_special2", 0, 0x800000007F800000ull);
	rst();
	cpu.d[1].u64 = 0x7F80000080000000ull;   /* lanes -0.0, +inf */
	if (exec("rsqrte_f32"))
		ck64("rsqrte_f32_special", 0, 0x00000000FF800000ull);
	rst();
	cpu.d[1].u64 = 0xC0800000FF800000ull;   /* lanes -inf, -4.0 */
	if (exec("rsqrte_f32"))
		ck64("rsqrte_f32_special2", 0, 0x7FC000007FC00000ull);
	/* NaN inputs use StandardFPSCRValue() DN = 1: default NaN, even with FPSCR.DN = 0 */
	rst();
	cpu.d[1].u64 = 0xFFC000007FC00000ull;
	if (exec("recpe_f32"))
		ck64("recpe_f32_nan", 0, 0x7FC000007FC00000ull);

	/* ---------------- VRINT ---------------- */
	/* 2.5 -> N 2.0, A 3.0, X 2.0, Z 2.0 ; P 3.0, M 2.0 */
	rst();
	cpu.d[1].u64 = 0xC020000040200000ull;   /* lanes 2.5 and -2.5 */
	if (exec("rintn"))
		ck64("rintn", 0, 0xC000000040000000ull);
	rst();
	cpu.d[1].u64 = 0xC020000040200000ull;
	if (exec("rinta"))
		ck64("rinta", 0, 0xC040000040400000ull);
	rst();
	cpu.d[1].u64 = 0xC020000040200000ull;
	if (exec("rintz"))
		ck64("rintz", 0, 0xC000000040000000ull);
	rst();
	cpu.d[1].u64 = 0xC020000040200000ull;
	if (exec("rintp"))
		ck64("rintp", 0, 0xC000000040400000ull);
	rst();
	cpu.d[1].u64 = 0xC020000040200000ull;
	if (exec("rintm"))
		ck64("rintm", 0, 0xC040000040000000ull);
	rst();
	cpu.d[1].u64 = 0xC020000040200000ull;
	if (exec("rintx"))
		ck64("rintx", 0, 0xC000000040000000ull);
	/* already integral: 3.0 -3.0 unchanged */
	rst();
	cpu.d[1].u64 = 0xC040000040400000ull;
	if (exec("rintn"))
		ck64("rintn", 0, 0xC040000040400000ull);
	/* NaN inputs: default NaN through StandardFPSCRValue() (DN = 1), sign dropped */
	rst();
	cpu.d[1].u64 = 0xFFC000007FC00000ull;
	if (exec("rintn"))
		ck64("rintn_nan", 0, 0x7FC000007FC00000ull);
	/* q form */
	rst();
	cpu.d[2].u64 = 0x4020000040200000ull;
	cpu.d[3].u64 = 0;
	if (exec("rintn_q")) {
		ck64("rintn_q", 0, 0x4000000040000000ull);
		ck64("rintn_q", 1, 0);
	}

	/* ---------------- VCVT{A,N,P,M} ---------------- */
	/* lanes 2.5 and -2.5 */
	rst();
	cpu.d[1].u64 = 0xC020000040200000ull;
	if (exec("cvta_s32"))
		ck64("cvta_s32", 0, 0xFFFFFFFD00000003ull);
	rst();
	cpu.d[1].u64 = 0xC020000040200000ull;
	if (exec("cvtn_s32"))
		ck64("cvtn_s32", 0, 0xFFFFFFFE00000002ull);
	rst();
	cpu.d[1].u64 = 0xC020000040200000ull;
	if (exec("cvtp_s32"))
		ck64("cvtp_s32", 0, 0xFFFFFFFE00000003ull);
	rst();
	cpu.d[1].u64 = 0xC020000040200000ull;
	if (exec("cvtm_s32"))
		ck64("cvtm_s32", 0, 0xFFFFFFFD00000002ull);
	/* unsigned: -1.0 -> 0 (saturated), 2.5 -> A 3 */
	rst();
	cpu.d[1].u64 = 0x40200000BF800000ull;
	if (exec("cvta_u32")) {
		ck64("cvta_u32", 0, 0x0000000300000000ull);
		ck_flags("cvta_u32", 0x08000000u, 0x08000000u);
	}
	rst();
	cpu.d[1].u64 = 0x40200000BF800000ull;
	if (exec("cvtn_u32"))
		ck64("cvtn_u32", 0, 0x0000000200000000ull);
	/* q forms: 0.5 and 1.5 */
	rst();
	cpu.d[2].u64 = 0x3FC000003F000000ull;
	cpu.d[3].u64 = 0;
	if (exec("cvtp_u32_q")) {
		ck64("cvtp_u32_q", 0, 0x0000000200000001ull);
		ck64("cvtp_u32_q", 1, 0);
	}
	rst();
	cpu.d[2].u64 = 0x3FC000003F000000ull;
	cpu.d[3].u64 = 0;
	if (exec("cvtm_u32_q")) {
		ck64("cvtm_u32_q", 0, 0x0000000100000000ull);
		ck64("cvtm_u32_q", 1, 0);
	}

	/* ---------------- VEXT ---------------- */
	/* VEXT Dd,Dn,Dm,#imm takes (Dn:Dm), Dn in the low bytes: result byte k = (Dn:Dm) byte
	 * (imm+k).  d1 = bytes 00..07, d2 = bytes 08..0F.
	 * #0 -> d1 (00..07); #3 -> d1 03..07 then d2 08..0A; #4 -> d1 04..07 then d2 08..0B;
	 * #7 -> d1 07 then d2 08..0E. */
	rst();
	cpu.d[1].u64 = 0x0706050403020100ull;
	cpu.d[2].u64 = 0x0F0E0D0C0B0A0908ull;
	if (exec("ext_d_0"))
		ck64("ext_d_0", 0, 0x0706050403020100ull);
	rst();
	cpu.d[1].u64 = 0x0706050403020100ull;
	cpu.d[2].u64 = 0x0F0E0D0C0B0A0908ull;
	if (exec("ext_d_3"))
		ck64("ext_d_3", 0, 0x0A09080706050403ull);
	rst();
	cpu.d[1].u64 = 0x0706050403020100ull;
	cpu.d[2].u64 = 0x1716151413121110ull;
	if (exec("ext_d_4"))                       /* R1's F16 repro: 1312111007060504 */
		ck64("ext_d_4", 0, 0x1312111007060504ull);
	rst();
	cpu.d[1].u64 = 0x0706050403020100ull;
	cpu.d[2].u64 = 0x0F0E0D0C0B0A0908ull;
	if (exec("ext_d_7"))
		ck64("ext_d_7", 0, 0x0E0D0C0B0A090807ull);
	/* .16 form: imm is in halfwords, encoded as 2*imm bytes */
	rst();
	cpu.d[1].u64 = 0x0706050403020100ull;
	cpu.d[2].u64 = 0x0F0E0D0C0B0A0908ull;
	if (exec("ext_d_16_1"))
		ck64("ext_d_16_1", 0, 0x0908070605040302ull);
	/* Q: source q1:q2 (q1 = bytes 00..0F, q2 = 10..1F), #imm bytes. */
	rst();
	cpu.d[2].u64 = 0x0706050403020100ull;
	cpu.d[3].u64 = 0x0F0E0D0C0B0A0908ull;
	cpu.d[4].u64 = 0x1716151413121110ull;
	cpu.d[5].u64 = 0x1F1E1D1C1B1A1918ull;
	if (exec("ext_q_0")) {
		ck64("ext_q_0", 0, 0x0706050403020100ull);
		ck64("ext_q_0", 1, 0x0F0E0D0C0B0A0908ull);
	}
	rst();
	cpu.d[2].u64 = 0x0706050403020100ull;
	cpu.d[3].u64 = 0x0F0E0D0C0B0A0908ull;
	cpu.d[4].u64 = 0x1716151413121110ull;
	cpu.d[5].u64 = 0x1F1E1D1C1B1A1918ull;
	if (exec("ext_q_4")) {                     /* R1's F16 repro: 0b0a090807060504 / 131211100f0e0d0c */
		ck64("ext_q_4", 0, 0x0B0A090807060504ull);
		ck64("ext_q_4", 1, 0x131211100F0E0D0Cull);
	}
	rst();
	cpu.d[2].u64 = 0x0706050403020100ull;
	cpu.d[3].u64 = 0x0F0E0D0C0B0A0908ull;
	cpu.d[4].u64 = 0x1716151413121110ull;
	cpu.d[5].u64 = 0x1F1E1D1C1B1A1918ull;
	if (exec("ext_q_8")) {
		ck64("ext_q_8", 0, 0x0F0E0D0C0B0A0908ull);
		ck64("ext_q_8", 1, 0x1716151413121110ull);
	}
	rst();
	cpu.d[2].u64 = 0x0706050403020100ull;
	cpu.d[3].u64 = 0x0F0E0D0C0B0A0908ull;
	cpu.d[4].u64 = 0x1716151413121110ull;
	cpu.d[5].u64 = 0x1F1E1D1C1B1A1918ull;
	if (exec("ext_q_15")) {                     /* source[15..30]: 0F then q2 bytes 10..1E */
		ck64("ext_q_15", 0, 0x161514131211100Full);
		ck64("ext_q_15", 1, 0x1E1D1C1B1A191817ull);
	}
	/* .32 form: imm 3 = 12 bytes */
	rst();
	cpu.d[2].u64 = 0x0706050403020100ull;
	cpu.d[3].u64 = 0x0F0E0D0C0B0A0908ull;
	cpu.d[4].u64 = 0x1716151413121110ull;
	cpu.d[5].u64 = 0x1F1E1D1C1B1A1918ull;
	if (exec("ext_q_32_3")) {
		ck64("ext_q_32_3", 0, 0x131211100F0E0D0Cull);
		ck64("ext_q_32_3", 1, 0x1B1A191817161514ull);
	}
	/* .64 form, q3 <- q0:q1 #1 = 8 bytes (element 0 is d0): result = d1,d2 */
	rst();
	cpu.d[0].u64 = 0x0706050403020100ull;
	cpu.d[1].u64 = 0x0F0E0D0C0B0A0908ull;
	cpu.d[2].u64 = 0x1716151413121110ull;
	cpu.d[3].u64 = 0x1F1E1D1C1B1A1918ull;
	if (exec("ext_q_64_1")) {
		ck64("ext_q_64_1", 6, 0x0F0E0D0C0B0A0908ull);
		ck64("ext_q_64_1", 7, 0x1716151413121110ull);
	}
	/* high registers: vext.8 d4, d20, d18, #5 -> d20 bytes 05..07 then d18 bytes 08..0C */
	rst();
	cpu.d[20].u64 = 0x0706050403020100ull;
	cpu.d[18].u64 = 0x0F0E0D0C0B0A0908ull;
	if (exec("ext_hi"))
		ck64("ext_hi", 4, 0x0C0B0A0908070605ull);

	/* ---------------- VTBL / VTBX ---------------- */
	/* table {d1} = 00..07, indices 00 01 02 07 08 FF 03 04 -> 00 01 02 07 00 00 03 04 */
	rst();
	cpu.d[1].u64 = 0x0706050403020100ull;
	cpu.d[2].u64 = 0x0403FF0807020100ull;
	if (exec("tbl_1"))
		ck64("tbl_1", 0, 0x0403000007020100ull);
	/* VTBX leaves out-of-range lanes unchanged: d0 = AA.. */
	rst();
	cpu.d[0].u64 = 0xAAAAAAAAAAAAAAAAull;
	cpu.d[1].u64 = 0x0706050403020100ull;
	cpu.d[2].u64 = 0x0403FF0807020100ull;
	if (exec("tbx_1"))
		ck64("tbx_1", 0, 0x0403AAAA07020100ull);
	/* table {d1,d2} = 00..0F, index 0x0A -> 0x0A */
	rst();
	cpu.d[0].u64 = 0;
	cpu.d[1].u64 = 0x0706050403020100ull;
	cpu.d[2].u64 = 0x0F0E0D0C0B0A0908ull;
	cpu.d[3].u64 = 0x000000000000000Aull;
	if (exec("tbl_2"))
		ck64("tbl_2", 0, 0x0Aull);
	/* table of 4 registers d1..d4 = 00..1F, index 0x1F -> 0x1F ; index 0x20 -> 0 */
	rst();
	cpu.d[1].u64 = 0x0706050403020100ull;
	cpu.d[2].u64 = 0x0F0E0D0C0B0A0908ull;
	cpu.d[3].u64 = 0x1716151413121110ull;
	cpu.d[4].u64 = 0x1F1E1D1C1B1A1918ull;
	cpu.d[5].u64 = 0x000000000000201Full;
	if (exec("tbl_4"))
		ck64("tbl_4", 0, 0x00001Full);
	/* VTBX with 4 tables leaves 0x20 and 0x30 unchanged */
	rst();
	cpu.d[0].u64 = 0xAAAAAAAAAAAAAAAAull;
	cpu.d[1].u64 = 0x0706050403020100ull;
	cpu.d[2].u64 = 0x0F0E0D0C0B0A0908ull;
	cpu.d[3].u64 = 0x1716151413121110ull;
	cpu.d[4].u64 = 0x1F1E1D1C1B1A1918ull;
	cpu.d[5].u64 = 0x000000000000201Full;
	if (exec("tbx_4"))
		ck64("tbx_4", 0, 0x000000000000AA1Full);
	/* tbl_3: table 00..17, index 0x10 -> 0x10 */
	rst();
	cpu.d[1].u64 = 0x0706050403020100ull;
	cpu.d[2].u64 = 0x0F0E0D0C0B0A0908ull;
	cpu.d[3].u64 = 0x1716151413121110ull;
	cpu.d[4].u64 = 0x0000000000000010ull;
	if (exec("tbl_3"))
		ck64("tbl_3", 0, 0x10ull);
	/* high registers: vtbl.8 d16, {d17,d18}, d19 ; index 0x0B -> d18 byte 3 = 0x13 */
	rst();
	cpu.d[17].u64 = 0x0706050403020100ull;
	cpu.d[18].u64 = 0x1716151413121110ull;
	cpu.d[19].u64 = 0x000000000000000Bull;
	if (exec("tbl_hi"))
		ck64("tbl_hi", 16, 0x13ull);

	/* ---------------- VDUP (scalar lane) ---------------- */
	/* byte 3 of 0x...2211 (d1 = 88 77 66 55 44 33 22 11) is 0x44 */
	t_d1("dup_8", 0x8877665544332211ull, 0x4444444444444444ull);
	t_d1("dup_16", 0x8888777766665555ull, 0x7777777777777777ull);
	t_d1("dup_32", 0x1122334455667788ull, 0x1122334411223344ull);
	rst();
	cpu.d[1].u64 = 0xAABBCCDDEEFF0011ull;
	if (exec("dup_8_q")) {
		ck64("dup_8_q", 0, 0xAAAAAAAAAAAAAAAAull);
		ck64("dup_8_q", 1, 0xAAAAAAAAAAAAAAAAull);
	}
	rst();
	cpu.d[1].u64 = 0xDEADBEEF22223333ull;
	if (exec("dup_16_q")) {
		ck64("dup_16_q", 0, 0x2222222222222222ull);
		ck64("dup_16_q", 1, 0x2222222222222222ull);
	}
	rst();
	cpu.d[1].u64 = 0xDEADBEEF12345678ull;
	if (exec("dup_32_q")) {
		ck64("dup_32_q", 0, 0x1234567812345678ull);
		ck64("dup_32_q", 1, 0x1234567812345678ull);
	}
	rst();
	cpu.d[18].u64 = 0xFFEEDDCCBBAA9988ull;
	if (exec("dup_hi"))
		ck64("dup_hi", 16, 0xEEEEEEEEEEEEEEEEull);

	/* ---------------- one register, modified immediate ---------------- */
	rst();
	if (exec("mov_i8_11"))
		ck64("mov_i8_11", 0, 0x1111111111111111ull);
	rst();
	if (exec("mov_i16_ff00"))
		ck64("mov_i16_ff00", 0, 0xFF00FF00FF00FF00ull);
	rst();
	if (exec("mov_i32_ff"))
		ck64("mov_i32_ff", 0, 0x000000FF000000FFull);
	rst();
	if (exec("mov_i32_ff00"))
		ck64("mov_i32_ff00", 0, 0x0000FF000000FF00ull);
	rst();
	if (exec("mov_i32_ff0000"))
		ck64("mov_i32_ff0000", 0, 0x00FF000000FF0000ull);
	rst();
	if (exec("mov_i32_ff000000"))
		ck64("mov_i32_ff000000", 0, 0xFF000000FF000000ull);
	rst();
	if (exec("mov_i32_q")) {
		ck64("mov_i32_q", 0, 0x000000FF000000FFull);
		ck64("mov_i32_q", 1, 0x000000FF000000FFull);
	}
	rst();
	if (exec("mov_i64_ff"))
		ck64("mov_i64_ff", 0, 0x00000000000000FFull);
	rst();
	if (exec("mov_i64_hi"))
		ck64("mov_i64_hi", 0, 0xFF00000000000000ull);
	rst();
	if (exec("mov_i64_mix"))
		ck64("mov_i64_mix", 0, 0xFF00FF00FF00FF00ull);
	rst();
	if (exec("mov_i8_q")) {
		ck64("mov_i8_q", 0, 0x1111111111111111ull);
		ck64("mov_i8_q", 1, 0x1111111111111111ull);
	}
	/* VMVN.I16 #0xff00 = ~0xFF00FF00FF00FF00 */
	rst();
	if (exec("mvn_i16"))
		ck64("mvn_i16", 0, 0x00FF00FF00FF00FFull);
	rst();
	if (exec("mvn_i32"))
		ck64("mvn_i32", 0, 0xFFFFFF00FFFFFF00ull);
	rst();
	if (exec("mvn_i32_q")) {
		ck64("mvn_i32_q", 0, 0xFFFFFF00FFFFFF00ull);
		ck64("mvn_i32_q", 1, 0xFFFFFF00FFFFFF00ull);
	}
	/* VORR / VBIC */
	rst();
	cpu.d[0].u64 = 0x1111111111111111ull;
	cpu.d[1].u64 = 0x1111111111111111ull;
	if (exec("orr_i32")) {
		ck64("orr_i32", 0, 0x111111FF111111FFull);
		ck64("orr_i32", 1, 0x111111FF111111FFull);
	}
	rst();
	cpu.d[0].u64 = 0xFFFFFFFFFFFFFFFFull;
	cpu.d[1].u64 = 0xFFFFFFFFFFFFFFFFull;
	if (exec("bic_i32")) {
		ck64("bic_i32", 0, 0xFFFFFF00FFFFFF00ull);
		ck64("bic_i32", 1, 0xFFFFFF00FFFFFF00ull);
	}
	rst();
	if (exec("orr_i16")) {
		ck64("orr_i16", 0, 0xFF00FF00FF00FF00ull);
		ck64("orr_i16", 1, 0xFF00FF00FF00FF00ull);
	}
	rst();
	cpu.d[0].u64 = 0xFFFFFFFFFFFFFFFFull;
	cpu.d[1].u64 = 0xFFFFFFFFFFFFFFFFull;
	if (exec("bic_i16")) {
		ck64("bic_i16", 0, 0x00FF00FF00FF00FFull);
		ck64("bic_i16", 1, 0x00FF00FF00FF00FFull);
	}
	rst();
	cpu.d[0].u64 = 0x0102030405060708ull;
	if (exec("orr_i32_d"))
		ck64("orr_i32_d", 0, 0x010203FF050607FFull);
	rst();
	cpu.d[0].u64 = 0xFFFFFFFFFFFFFFFFull;
	if (exec("bic_i32_d"))
		ck64("bic_i32_d", 0, 0xFFFFFF00FFFFFF00ull);
	/* VMOV.F32 */
	rst();
	if (exec("mov_f32_1"))
		ck64("mov_f32_1", 0, 0x3F8000003F800000ull);
	rst();
	if (exec("mov_f32_2"))
		ck64("mov_f32_2", 0, 0x4000000040000000ull);
	rst();
	if (exec("mov_f32_neg2"))
		ck64("mov_f32_neg2", 0, 0xC0000000C0000000ull);
	rst();
	if (exec("mov_f32_0p5"))
		ck64("mov_f32_0p5", 0, 0x3F0000003F000000ull);
	rst();
	if (exec("mov_f32_q")) {
		ck64("mov_f32_q", 0, 0x3F8000003F800000ull);
		ck64("mov_f32_q", 1, 0x3F8000003F800000ull);
	}

	/* ---------------- must be UNDEF ---------------- */
	rst();
	ck_undef("undef_f16_n");
	ck_undef("undef_f16_w");
	ck_undef("undef_3same");
	ck_undef("undef_shift");
	ck_undef("undef_vmovl");
	ck_undef("undef_vld1");

	printf("t_misc: %d checks, %d failures\n", checks, fails);
	return fails == 0 ? 0 : 1;
}
