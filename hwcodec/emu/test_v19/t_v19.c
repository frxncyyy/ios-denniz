/* SPDX-License-Identifier: MIT */
/*
 * V19 host test: n2a32_neon_shift.
 *
 * Three layers:
 *   1. t_vectors: generated literals (gen_vectors.py, ARM ARM pseudocode model) for every
 *      implemented encoding, 8 vectors each, checking all D0-D9 and FPSCR.QC;
 *   2. t_hand: hand-worked edge cases (rounding carries, saturation, insert, fixed-point,
 *      scalar lane selection, float NaN/denormal) with literals computed by hand;
 *   3. t_undef + t_sweep: encodings outside V19 return N2A32_UNDEF and leave the CPU state
 *      untouched; 200000 random words of the F2/F3 space must execute or return UNDEF.
 */
#include "n2a32.h"
#include "vec_v19.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct n2a32_cpu cpu;
static struct n2a32_mem mem;
static uint8_t *membase;
static unsigned checks, fails;

#define D(n) cpu.d[n].u64

static void reset(void)
{
	memset(&cpu, 0, sizeof(cpu));
	cpu.mem = &mem;
	memset(mem.mmio_idx, 0, sizeof(mem.mmio_idx));
}

static void ck(int cond, const char *what, const char *name, unsigned idx)
{
	checks++;
	if (!cond) {
		fails++;
		printf("FAIL [%s #%u] %s\n", name, idx, what);
	}
}

static int run(uint32_t insn)
{
	return n2a32_neon_shift(&cpu, insn);
}

static unsigned qc(void)
{
	return (cpu.fpscr >> 27) & 1u;
}

/* --------------------------------------------------------------------------------------------------------- */

static void t_vectors(void)
{
	unsigned v, r;

	for (v = 0; v < V19_VEC_COUNT; v++) {
		const struct v19_vec *t = &v19_vecs[v];

		reset();
		for (r = 0; r < 10; r++)
			cpu.d[r].u64 = t->in[r];
		cpu.fpscr = t->fpscr_in;
		if (run(t->insn) != N2A32_OK) {
			ck(0, "return != OK", t->name, v);
			continue;
		}
		for (r = 0; r < 10; r++)
			ck(cpu.d[r].u64 == t->out[r], "register value", t->name, v);
		ck(cpu.fpscr == t->fpscr_out, "FPSCR (QC)", t->name, v);
	}
}

/* --------------------------------------------------------------------------------------------------------- */

#define OK(insn) do { ck(run(insn) == N2A32_OK, "return != OK", #insn, 0); } while (0)
#define EQ(expr, val) ck((expr) == (val), #expr, __func__, __LINE__)
#define QCIS(v) ck(qc() == (v), "QC", __func__, __LINE__)

static void t_hand(void)
{
	/* VSHR.S8: -128 >> 1 = -64, 127 >> 1 = 63, -1 >> 1 = -1 */
	reset(); D(2) = 0x000000000000807FULL;
	OK(ENC_VSHR_8_U0_Q0_S1);              /* name generated as vshr_8_u0_q0_s1 */
	EQ(D(0), 0x000000000000C03FULL);

	/* VRSHR.U8 rounding carry: 0xFF/2 -> 0x80, 0x01/2 -> 0x01, 0x02/2 -> 0x01 */
	reset(); D(2) = 0x00000000000002FFULL;
	OK(ENC_VRSHR_8_U1_Q0_S1);
	EQ(D(0), 0x0000000000000180ULL);

	/* VRSHR.S8: -1/2 -> 0, -3/2 -> -1, 3/2 -> 2, -2/2 -> -1 */
	reset(); D(2) = 0x0000000000FD03FFULL;  /* bytes: FF(-1), 03(3), FD(-3), 00 */
	OK(ENC_VRSHR_8_U0_Q0_S1);
	EQ(D(0), 0x0000000000FF0200ULL);

	/* VSRI.8 #8 leaves the destination unchanged (mask is empty) */
	reset(); D(0) = 0x0123456789ABCDEFULL; D(2) = 0xFFFFFFFFFFFFFFFFULL;
	OK(ENC_VSRI_8_Q0_S8);
	EQ(D(0), 0x0123456789ABCDEFULL);

	/* VSLI.8 #0 keeps none of the destination: the result is the source */
	reset(); D(0) = 0x0123456789ABCDEFULL; D(2) = 0xFFFFFFFFFFFFFFFFULL;
	OK(ENC_VSLI_8_Q0_S0);
	EQ(D(0), 0xFFFFFFFFFFFFFFFFULL);

	/* R1 F10 repro: VSRI.32 q0,q1,#8 keeps the top 8 bits of Vd */
	reset(); D(0) = 0xFFFFFFFFULL; D(2) = 0x11223344ULL;
	OK(0xF3B80452u);
	EQ(D(0), 0x00000000FF112233ULL);
	EQ(D(1), 0);

	/* and with a visible destination half: 0xdeadbeef -> 0xDE112233 */
	reset(); D(0) = 0xDEADBEEFULL; D(2) = 0x11223344ULL;
	OK(0xF3B80452u);
	EQ(D(0), 0x00000000DE112233ULL);

	/* VSLI.32 q0,q1,#8 keeps the low 8 bits of Vd */
	reset(); D(0) = 0xFFFFFFFFULL; D(2) = 0x11223344ULL;
	OK(0xF3A80552u);
	EQ(D(0), 0x00000000223344FFULL);
	reset(); D(0) = 0xDEADBEEFULL; D(2) = 0x11223344ULL;
	OK(0xF3A80552u);
	EQ(D(0), 0x00000000223344EFULL);

	/* VQSHL.S16 saturates positive and negative, sets QC */
	reset(); D(2) = 0x0000000080007FFFULL;
	OK(ENC_VQSHL_16_U0_Q0_S0);             /* shift 0: no saturation */
	EQ(D(0), 0x0000000080007FFFULL);
	QCIS(0);
	reset(); D(2) = 0x0000000000014000ULL;  /* 0x4000 << 1 = 0x8000: signed overflow */
	OK(ENC_VQSHL_16_U0_Q0_S1);
	EQ(D(0), 0x0000000000027FFFULL);
	QCIS(1);

	/* VQSHLU.S8: -1 -> 0, 0x7F << 1 -> 0xFE (unsigned), no saturation */
	reset(); D(2) = 0x0000000000007F7FULL;
	OK(ENC_VQSHLU_8_Q0_S1);
	EQ(D(0), 0x000000000000FEFEULL);
	QCIS(0);
	reset(); D(2) = 0x00000000000000FFULL;
	OK(ENC_VQSHLU_8_Q0_S1);
	EQ(D(0), 0);
	QCIS(1);

	/* VQSHL.U8: 0x80 << 1 = 0x100 -> 0xFF, QC */
	reset(); D(2) = 0x0000000000000080ULL;
	OK(ENC_VQSHL_8_U1_Q0_S1);
	EQ(D(0), 0x00000000000000FFULL);
	QCIS(1);

	/* VSHRN.I16 logical: 0xFFFF >> 1 = 0x7FFF, narrowed to 0xFF */
	reset(); D(4) = 0x000000000000FFFFULL;
	OK(ENC_VSHRN_16_S1);
	EQ(D(0), 0x00000000000000FFULL);

	/* VRSHRN.I16 rounding: 0x0003 >> 1 = 1, 0xFFFF >> 1 = 0x8000 -> 0x00 */
	reset(); D(4) = 0x0000000000000003ULL;
	OK(ENC_VRSHRN_16_S1);
	EQ(D(0), 0x0000000000000002ULL);
	reset(); D(4) = 0x000000000000FFFFULL;
	OK(ENC_VRSHRN_16_S1);
	EQ(D(0), 0x0000000000000000ULL);

	/* VQSHRN.S16: 0x7FFF >> 1 = 0x3FFF -> 0xFF, QC; -1 -> -1 -> 0xFF, no QC */
	reset(); D(4) = 0x0000000000007FFFULL;
	OK(ENC_VQSHRN_16_U0_S1);
	EQ(D(0), 0x000000000000007FULL);
	QCIS(1);
	reset(); D(4) = 0x000000000000FFFFULL;
	OK(ENC_VQSHRN_16_U0_S1);
	EQ(D(0), 0x00000000000000FFULL);
	QCIS(0);

	/* VQSHRUN.S16: -1 -> 0, QC; 0x7FFF >> 1 = 0x3FFF -> 0xFF, QC */
	reset(); D(4) = 0x000000000000FFFFULL;
	OK(ENC_VQSHRUN_16_S1);
	EQ(D(0), 0);
	QCIS(1);
	reset(); D(4) = 0x0000000000007FFFULL;
	OK(ENC_VQSHRUN_16_S1);
	EQ(D(0), 0x00000000000000FFULL);
	QCIS(1);

	/* VSHLL.S8: sign extension then shift */
	reset(); D(2) = 0x00000000000080FFULL;
	OK(ENC_VSHLL_8_U0_S1);
	EQ(D(0), 0x00000000FF00FFFEULL);
	EQ(D(1), 0);

	/* VMOVL.S32 sign extension */
	reset(); D(2) = 0x800000007FFFFFFFULL;
	OK(ENC_VMOVL_32_U0);
	EQ(D(0), 0x000000007FFFFFFFULL);
	EQ(D(1), 0xFFFFFFFF80000000ULL);

	/* VCVT.S32.F32 #1 truncates toward zero: 1.75 -> 3, -1.75 -> -3 */
	reset();
	cpu.d[2].f32[0] = 1.75f;
	cpu.d[2].f32[1] = -1.75f;
	OK(ENC_VCVT_S32_F32_F1);
	EQ(D(0), 0xFFFFFFFD00000003ULL);
	QCIS(0);

	/* VCVT.S32.F32 saturation: 1e9 * 2^1 overflows int32 -> 0x7FFFFFFF, QC */
	reset();
	cpu.d[2].f32[0] = 2.0e9f;
	cpu.d[2].f32[1] = -2.0e9f;
	OK(ENC_VCVT_S32_F32_F1);
	EQ(D(0), 0x800000007FFFFFFFULL);
	QCIS(1);

	/* VCVT.U32.F32: -1 -> 0, QC; 1.0 -> 1 */
	reset();
	cpu.d[2].f32[0] = -1.0f;
	cpu.d[2].f32[1] = 1.0f;
	OK(ENC_VCVT_U32_F32_F1);
	EQ(D(0), 0x0000000200000000ULL);
	QCIS(1);

	/* VCVT.F32.S32 #16: 0x00018000 -> 1.5, 0xFFFDC000 -> -2.25 */
	reset(); D(2) = 0xFFFDC00000018000ULL;
	OK(ENC_VCVT_F32_S32_F16);
	/* the two float words are 0x3FC00000 (1.5) and 0xC0100000 (-2.25) */
	EQ(cpu.d[0].u32[0], 0x3FC00000u);
	EQ(cpu.d[0].u32[1], 0xC0100000u);

	/* VADDL.U8 carry across the widened element: 0xFF + 0xFF = 0x01FE */
	reset(); D(4) = 0x000000000000FFFFULL; D(6) = 0x0000000000000001ULL;
	OK(ENC_VADDL_8_U);
	EQ(D(0), 0x0000000000FF0100ULL);
	EQ(D(1), 0);

	/* VSUBL.S16: 0x8000 - 0x7FFF = 1 */
	reset(); D(4) = 0x0000000000008000ULL; D(6) = 0x0000000000007FFFULL;
	OK(ENC_VSUBL_16_S);
	EQ(D(0), 0x00000000FFFF0001ULL);

	/* VADDW.S16 wraps at the widened width: INT32_MAX + 1 = INT32_MIN */
	reset(); D(2) = 0x000000007FFFFFFFULL; D(3) = 0xFFFFFFFF80000000ULL;
	D(6) = 0x0000000000000001ULL;
	OK(ENC_VADDW_16_S);
	EQ(D(0), 0x0000000080000000ULL);
	EQ(D(1), 0xFFFFFFFF80000000ULL);

	/* VADDHN.I16 rounding: (0x00FF + 0x0001 + 0x0080) >> 8 = 0x01 */
	reset(); D(2) = 0x00000000000000FFULL; D(4) = 0x0000000000000001ULL;
	OK(ENC_VRADDHN_16);
	EQ(D(0), 0x0000000000000001ULL);

	/* VRSUBHN.I16: (0x0001 - 0x0002 + 0x0080) >> 8 = 0x00 */
	reset(); D(2) = 0x0000000000000001ULL; D(4) = 0x0000000000000002ULL;
	OK(ENC_VRSUBHN_16);
	EQ(D(0), 0x0000000000000000ULL);

	/* VABDL.S8: -128 - 127 = -255 -> 255 */
	reset(); D(4) = 0x000000000000807FULL; D(6) = 0x0000000000000000ULL;
	OK(ENC_VABDL_8_S);
	EQ(D(0), 0x000000000080007FULL);
	EQ(D(1), 0x0000000000000000ULL);

	/* VABAL.U8 accumulates without saturation */
	reset(); D(0) = 0x000000000000FFFFULL; D(4) = 0xFF; D(6) = 0x01;
	OK(ENC_VABAL_8_U);
	EQ(D(0), 0x00000000000000FDULL);

	/* VMLAL.S16 negative product: -2 * 3 = -6 */
	reset(); D(4) = 0x000000000000FFFEULL; D(6) = 0x0000000000000003ULL;
	OK(ENC_VMLAL_16_S);
	EQ(D(0), 0x00000000FFFFFFFAULL);

	/* VMULL.U32: 0xFFFFFFFF * 0xFFFFFFFF = 0xFFFFFFFE00000001 */
	reset(); D(4) = 0x00000000FFFFFFFFULL; D(6) = 0x00000000FFFFFFFFULL;
	OK(ENC_VMULL_32_U);
	EQ(D(0), 0xFFFFFFFE00000001ULL);

	/* VMULL.P8: 0x03 * 0x05 = 0x0F; 0xFF * 0xFF = 0x5555 */
	reset(); D(4) = 0x000000000000FF03ULL; D(6) = 0x000000000000FF05ULL;
	OK(ENC_VMULL_P8);
	EQ(D(0), 0x000000005555000FULL);

	/* VQDMULL.S16 saturates the doubled product: (-32768)^2*2 = 2^31 -> 0x7FFFFFFF */
	reset(); D(4) = 0x0000000000008000ULL; D(6) = 0x0000000000008000ULL;
	OK(ENC_VQDMULL_16);
	EQ(D(0), 0x000000007FFFFFFFULL);
	QCIS(1);

	/* VQDMLAL.S16 accumulation saturates: 0x7FFFFFFF + 2*(0x4000^2) */
	reset(); D(0) = 0x000000007FFFFFFFULL; D(4) = 0x0000000000004000ULL;
	D(6) = 0x0000000000004000ULL;
	OK(ENC_VQDMLAL_16);
	EQ(D(0), 0x000000007FFFFFFFULL);
	QCIS(1);

	/* VQDMLSL.S16: 0x80000000 - 2*(0x4000^2) saturates negative */
	reset(); D(0) = 0x0000000080000000ULL; D(4) = 0x0000000000004000ULL;
	D(6) = 0x0000000000004000ULL;
	OK(ENC_VQDMLSL_16);
	EQ(D(0), 0x0000000080000000ULL);
	QCIS(1);

	/* VMLA.I16 scalar lane 0 */
	reset(); D(0) = 0x0000000000000001ULL; D(4) = 0x0000000000000002ULL;
	D(6) = 0x0000000000000003ULL;
	OK(ENC_VMLA_16_Q0_L0);
	EQ(D(0), 0x0000000000000007ULL);

	/* VMLA.I16 Q form uses U=1 and Q4 = D8,D9 */
	reset(); D(8) = 0x0000000000000002ULL; D(6) = 0x0000000000000003ULL;
	OK(ENC_VMLA_16_Q1_L0);
	EQ(D(0), 0x0000000000000006ULL);
	EQ(D(1), 0);

	/* VQDMULH.S16: 0x4000*0x4000*2 >> 16 = 0x2000 */
	reset(); D(4) = 0x0000000000004000ULL; D(6) = 0x0000000000004000ULL;
	OK(ENC_VQDMULH_16_Q0_L0);
	EQ(D(0), 0x0000000000002000ULL);
	QCIS(0);

	/* VQDMULH.S16 saturation: INT16_MIN * INT16_MIN -> 0x7FFF, QC */
	reset(); D(4) = 0x0000000000008000ULL; D(6) = 0x0000000000008000ULL;
	OK(ENC_VQDMULH_16_Q0_L0);
	EQ(D(0), 0x0000000000007FFFULL);
	QCIS(1);

	/* VQRDMULH.S16 rounding: 0x4000*0x4000*2 + 0x8000 >> 16 = 0x2000 */
	reset(); D(4) = 0x0000000000004000ULL; D(6) = 0x0000000000004000ULL;
	OK(ENC_VQRDMULH_16_Q0_L0);
	EQ(D(0), 0x0000000000002000ULL);

	/* VQDMULH.S32 saturation: INT32_MIN * INT32_MIN */
	reset(); D(4) = 0x0000000080000000ULL; D(6) = 0x0000000080000000ULL;
	OK(ENC_VQDMULH_32_Q0_L0);
	EQ(D(0), 0x000000007FFFFFFFULL);
	QCIS(1);

	/* VMLA.F32: 1.0 + 2.0*3.0 = 7.0 */
	reset(); D(0) = 0x3F800000ULL; D(4) = 0x40000000ULL; D(6) = 0x40400000ULL;
	OK(ENC_VMLA_F32_Q0_L0);
	EQ(D(0), 0x0000000040E00000ULL);

	/* VMLA.F32 default NaN: 0*inf + 1 -> default NaN 0x7FC00000 */
	reset(); D(0) = 0x3F800000ULL; D(4) = 0x7F800000ULL; D(6) = 0x00000000ULL;
	OK(ENC_VMLA_F32_Q0_L0);
	EQ(D(0), 0x000000007FC00000ULL);

	/* VMLA.F32 flush-to-zero: smallest denormal * 2 + 0 -> +0 */
	reset(); D(0) = 0; D(4) = 0x00000001ULL; D(6) = 0x40000000ULL;
	OK(ENC_VMLA_F32_Q0_L0);
	EQ(D(0), 0);

	/* VMUL.F32 Q form, scalar lane 1 */
	reset(); D(8) = 0x400000003F800000ULL; D(9) = 0x4040000040400000ULL;
	D(6) = 0x4080000040000000ULL;           /* lane 1 = 4.0 */
	OK(ENC_VMUL_F32_Q1_L1);
	EQ(D(0), 0x4100000040800000ULL);
	EQ(D(1), 0x4140000041400000ULL);

	/* VSHR.S64 #64: arithmetic shift by the full width keeps the sign (-1) */
	reset(); D(2) = 0xFFFFFFFFFFFFFFFFULL;
	OK(ENC_VSHR_64_U0_Q0_S64);
	EQ(D(0), 0xFFFFFFFFFFFFFFFFULL);

	/* VRSHR.S64 #64: (v + 2^63) >> 64 is 0 for every v */
	reset(); D(2) = 0xFFFFFFFFFFFFFFFFULL;
	OK(ENC_VRSHR_64_U0_Q0_S64);
	EQ(D(0), 0);

	/* VCVT.U32.F32 NaN -> 0 (no QC), 0.5 * 2^1 = 1 */
	reset();
	cpu.d[2].f32[0] = (float)NAN;
	cpu.d[2].f32[1] = 0.5f;
	OK(ENC_VCVT_U32_F32_F1);
	EQ(D(0), 0x0000000100000000ULL);
	QCIS(0);

	/* VMLA.F32 with a quiet NaN operand -> default NaN, not the operand payload */
	reset(); D(0) = 0x3F800000ULL; D(4) = 0x7FC00001ULL; D(6) = 0x40000000ULL;
	OK(ENC_VMLA_F32_Q0_L0);
	EQ(D(0), 0x000000007FC00000ULL);

	/* VCVT.F32.U32 #32: 1 / 2^32 = 2^-32 = 0x2F800000 */
	reset(); D(2) = 0x0000000000000001ULL;
	OK(ENC_VCVT_F32_U32_F32);
	EQ(cpu.d[0].u32[0], 0x2F800000u);

	/* T32 vshrlt.u32 d19,d10,#6 inside an IT block (census case): the core converts the
	 * T32 word fffa301a to the A32 form f3fa301a and applies the IT condition. */
	reset(); D(10) = 0x8000000000000001ULL;
	OK(0xF3FA301Au);
	EQ(D(19), 0x0200000000000000ULL);

	/* register-preservation contract: no core register or PC state changes */
	reset();
	cpu.r[0] = 0x11111111; cpu.r[13] = 0x22222222; cpu.r[15] = 0x33333333;
	cpu.next_pc = 0x44444444; cpu.thumb = 1; cpu.in_it = 1;
	OK(ENC_VADDW_16_U);
	EQ(cpu.r[0], 0x11111111u);
	EQ(cpu.r[13], 0x22222222u);
	EQ(cpu.r[15], 0x33333333u);
	EQ(cpu.next_pc, 0x44444444u);
	EQ(cpu.thumb, 1);
	EQ(cpu.in_it, 1);
}

/* --------------------------------------------------------------------------------------------------------- */

static void t_undef_one(uint32_t insn, const char *name)
{
	uint32_t i;
	uint64_t save[32];
	unsigned fsave = cpu.fpscr;

	for (i = 0; i < 32; i++)
		save[i] = cpu.d[i].u64;
	if (run(insn) != N2A32_UNDEF) {
		ck(0, "expected UNDEF", name, 0);
		return;
	}
	for (i = 0; i < 32; i++)
		ck(cpu.d[i].u64 == save[i], "UNDEF changed a D register", name, 0);
	ck(cpu.fpscr == fsave, "UNDEF changed FPSCR", name, 0);
}

static void t_undef(void)
{
	unsigned i;

	reset();
	for (i = 0; i < 32; i++)
		cpu.d[i].u64 = 0x0123456789ABCDEFULL ^ i;
	t_undef_one(0xF2000802u, "three registers same length (VADD.I8, V18)");
	t_undef_one(0xF2800812u, "one register + modified immediate (VMOV.I16, V20)");
	t_undef_one(0xF3830652u, "one register + modified immediate (VMOV.I32, V20)");
	t_undef_one(0xF3B20202u, "two registers misc (VMOVN.I16, V20)");
	t_undef_one(0xF3B00042u, "two registers misc (VREV64.8, V20)");
	t_undef_one(0xF2B36087u, "VEXT (V20)");
	t_undef_one(0xF38F0053u, "VSHL Q form with odd source D register");
	t_undef_one(0xF2890A51u, "VSHLL with bit6=1");
	t_undef_one(0xF29F0F11u, "VCVT with fbits=33");
	t_undef_one(0xF2B00000u, "VADDL with size=3");
	t_undef_one(0xF2800900u, "VQDMLAL.S8 (size 0)");
	t_undef_one(0xF2800D00u, "VQDMULL.S8 (size 0)");
	t_undef_one(0xF2800C40u, "VQDMULH.S8 (scalar size 0)");
	t_undef_one(0xF2800411u, "VSRI with U=0");
	t_undef_one(0xF2800611u, "VQSHLU with U=0");
	t_undef_one(0xF28F0B11u, "shift opc=0xb");
	t_undef_one(0xF28F0C11u, "shift opc=0xc");
	t_undef_one(0xF28F0D11u, "shift opc=0xd");
	t_undef_one(0xF3900900u, "VQDMLAL with U=1");
	t_undef_one(0xF3900340u, "VQDMLAL scalar with U=1");
	t_undef_one(0xF2900140u, "VMLA.F32 with size=16");
	t_undef_one(0xF2800E11u, "VCVT with fbits=49");
}

/* --------------------------------------------------------------------------------------------------------- */

static uint64_t rng_state = 0x243F6A8885A308D3ULL;

static uint32_t rng(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 7;
	rng_state ^= rng_state << 17;
	return (uint32_t)(rng_state >> 16);
}

static void t_sweep(void)
{
	unsigned i, undef = 0, ok = 0;

	for (i = 0; i < 200000; i++) {
		uint32_t w = rng();
		uint32_t insn;
		int r;
		uint64_t save[32];
		unsigned fsave;
		unsigned k;

		/* Random word in the A32 Advanced SIMD data-processing space: 1111001 U 1 ... */
		insn = 0xF2000000u | (w & 0x01FFFFFFu);
		insn &= ~0x00800000u;              /* bit23 = 1 */
		insn |= 0x00800000u;
		/* keep the condition field at 0xF, as the core delivers it */
		insn |= 0xF0000000u;
		for (k = 0; k < 32; k++)
			save[k] = cpu.d[k].u64;
		fsave = cpu.fpscr;
		r = run(insn);
		if (r == N2A32_OK) {
			ok++;
		} else if (r == N2A32_UNDEF) {
			undef++;
			for (k = 0; k < 32; k++)
				if (cpu.d[k].u64 != save[k]) {
					ck(0, "UNDEF modified state during sweep", "sweep", i);
					break;
				}
			if (cpu.fpscr != fsave)
				ck(0, "UNDEF modified FPSCR during sweep", "sweep", i);
		} else {
			ck(0, "sweep returned neither OK nor UNDEF", "sweep", i);
		}
	}
	printf("sweep: %u OK, %u UNDEF, 200000 total\n", ok, undef);
	checks += 2;
	if (ok == 0 || undef == 0) {
		fails++;
		printf("FAIL sweep coverage (ok=%u undef=%u)\n", ok, undef);
	}
}

/* --------------------------------------------------------------------------------------------------------- */

int main(void)
{
	membase = calloc(1, 16u << 20);
	if (!membase) {
		printf("N2TEST V19 FAIL calloc\n");
		return 1;
	}
	memset(&mem, 0, sizeof(mem));
	mem.base = membase;
	mem.mmio[0].rd = NULL;
	mem.mmio[0].wr = NULL;
	mem.mmio[0].ctx = NULL;

	t_vectors();
	t_hand();
	t_undef();
	t_sweep();

	printf("TESTS %u/%u\n", checks - fails, checks);
	printf("N2TEST V19 %s %u/%u\n", fails ? "FAIL" : "PASS", checks - fails, checks);
	free(membase);
	return fails ? 1 : 0;
}
