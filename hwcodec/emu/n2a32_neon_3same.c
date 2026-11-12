/* SPDX-License-Identifier: MIT */
/*
 * n2a32_neon_3same.c -- V18: Advanced SIMD (NEON) three registers of the same length.
 *
 * Input is the A32 form of the instruction (the core converts T32 111U 1111 ... to 1111 001U ...),
 * so bits 31-25 are 1111001; this class is exactly bit23 == 0:
 *
 *     1111 001U 0D size Vn Vd opc N Q M bit4 Vm      (ARM ARM A7.4.3, "three registers of the same length")
 *
 * U, size, opc (bits 11-8) and bit4 select the operation; for the float operations bits 21-20 are an
 * operation selector (00 = plain, 10 = the "negated/second" form) rather than an element size.  The
 * table below was checked against llvm-mc --disassemble for every (U, size, opc, bit4, Q) combination
 * of this space; see test_v18/gen_decode_oracle.py, which re-checks it on every test run.
 *
 * Implemented (integer, D and Q forms):
 *   VADD VSUB VHADD VHSUB VRHADD VQADD VQSUB          VCGT VCGE VCEQ VTST
 *   VSHL VQSHL VRSHL VQRSHL (register shifts, shift count = signed byte in Vn)
 *   VMAX VMIN VABD VABA                               VMUL (I8/I16/I32 and P8) VMLA VMLS
 *   VQDMULH VQRDMULH                                  VPMAX VPMIN VPADD (D form only, as encoded)
 *   VAND VBIC VORR VORN VEOR VBSL VBIT VBIF
 * Implemented (float32, standard FPSCR value: round-to-nearest-even, DN=1, FZ=1):
 *   VADD VSUB VMUL VMLA VMLS VFMA VFMS                VCEQ VCGE VCGT VACGE VACGT
 *   VMAX VMIN VPMAX VPMIN VPADD VABD                  VRECPS VRSQRTS VMAXNM VMINNM
 *
 * Deliberately N2A32_UNDEF (every one is outside the task; all were confirmed absent from the vendor
 * libraries' reachable code by the D01 census / the V18 re-check in test_v18):
 *   - the SHA1/SHA256 crypto instructions that share this space (opc 1100 with bit4 0, Q form):
 *     SHA1C SHA1P SHA1M SHA1SU0 SHA256H SHA256H2 SHA256SU1.  The vendor libraries do use NEON
 *     crypto, but only SHA1H/SHA1SU1/SHA256SU0/AESE/... which live in the two-registers-misc space
 *     (bit23 == 1, V20), not here;
 *   - every unallocated encoding of the space (e.g. VHADD with size 11, VQDMULH with size 00,
 *     VCEQ/VTST/VMLA/VMUL with size 11, VPMAX/VPMIN/VPADD with Q == 1, all float ops with size
 *     01/11, the ARMv8.2 dot-product and fp16 encodings, which the build's -mattr does not enable);
 *   - Q form with an odd D-register number (the operands are D-register pairs, so an odd number is
 *     architecturally UNPREDICTABLE; llvm-mc rejects those words too).  The D/N/M bits themselves are
 *     part of the register number: q8-q15 have them set and are executed normally.
 *
 * Contract (include/n2a32.h): the condition has been evaluated by the core; no branch, no memory
 * access, no printing, no abort.  QC is FPSCR bit 27, which is the same architectural bit as APSR.Q,
 * so both c->fpscr and c->q are updated (c->q is what the core's MRS reports).
 *
 * Expected values in the tests are literals derived from the ARM ARM pseudocode (the operations
 * below cite the pseudocode rules they implement); the float model uses double-precision
 * intermediates where the ARM ARM specifies a single rounding (FPMulAdd, FPRecipStepFused,
 * FPRSqrtStepFused), which is exact for binary32 because 53 >= 2*24+2.
 */
#include "n2a32.h"

/* The host IEEE rounding mode can be forced to round-to-nearest for the float operations below (the
 * standard FPSCR value fixes RMode = RNE).  The reduced freestanding SDK used for the iOS compile
 * check has no <fenv.h>; there the process default (RNE) is used, which is what the daemon runs
 * with anyway. */
#if defined(__has_include)
#  if __has_include(<fenv.h>)
#    include <fenv.h>
#    define N2_HAVE_FENV 1
#  endif
#endif
#ifndef N2_HAVE_FENV
#  define N2_HAVE_FENV 0
#endif

#define N2_QC (1u << 27)
#define N2_F32_DEFAULT_NAN 0x7fc00000u

/* ---------------------------------------------------------------------------------------------------- */
/* integer helpers                                                                                       */

static uint64_t mask_of(unsigned esize)
{
	return esize >= 64 ? ~UINT64_C(0) : ((UINT64_C(1) << esize) - 1);
}

/* Sign-extend the low esize bits of v to a 64-bit two's complement pattern. */
static uint64_t sx64(uint64_t v, unsigned esize)
{
	uint64_t m;

	if (esize >= 64)
		return v;
	m = UINT64_C(1) << (esize - 1);
	return ((v & mask_of(esize)) ^ m) - m;
}

/* Arithmetic shift right of a 64-bit pattern (does not rely on the implementation-defined >> of a
 * negative signed value).  s may be any value; the result is all sign bits for s >= 64. */
static uint64_t asr64(uint64_t v, unsigned s)
{
	if (s == 0)
		return v;
	if (s >= 64)
		return (v >> 63) != 0 ? ~UINT64_C(0) : 0;
	if ((v >> 63) == 0)
		return v >> s;
	return ~(~v >> s);
}

/* a < b, comparing the two 64-bit patterns as signed values. */
static int slt64(uint64_t a, uint64_t b)
{
	if ((a >> 63) != (b >> 63))
		return (int)(a >> 63);
	return a < b;
}

/* SignedSatQ() on a sign-extended 64-bit pattern (the exact value, at most the full 64-bit range:
 * the VQSHL product can be as large as 2^62 for 32-bit elements). */
static uint64_t clamp_s(uint64_t v, unsigned esize, int *sat)
{
	uint64_t hi = (UINT64_C(1) << (esize - 1)) - 1;
	uint64_t lo = UINT64_C(0) - (UINT64_C(1) << (esize - 1));

	if (slt64(v, lo)) {
		*sat = 1;
		return lo;
	}
	if (slt64(hi, v)) {
		*sat = 1;
		return hi;
	}
	return v & mask_of(esize);
}

/* UnsignedSatQ() on an exact (non-negative) value; esize <= 32. */
static uint64_t clamp_u(uint64_t v, unsigned esize, int *sat)
{
	uint64_t hi = mask_of(esize);

	if (v > hi) {
		*sat = 1;
		return hi;
	}
	return v;
}

/* Signed saturating 64-bit add/sub (ARM ARM: SignedSatQ(sum, 64)). */
static uint64_t qadd_s64(uint64_t a, uint64_t b, int *sat)
{
	uint64_t s = a + b;
	uint64_t sa = a >> 63, sb = b >> 63;

	if (sa == sb && (s >> 63) != sa) {
		*sat = 1;
		return sa != 0 ? (UINT64_C(1) << 63) : ~(UINT64_C(1) << 63);
	}
	return s;
}

static uint64_t qsub_s64(uint64_t a, uint64_t b, int *sat)
{
	uint64_t s = a - b;
	uint64_t sa = a >> 63, sb = b >> 63;

	if (sa != sb && (s >> 63) != sa) {
		*sat = 1;
		return sa != 0 ? (UINT64_C(1) << 63) : ~(UINT64_C(1) << 63);
	}
	return s;
}

static uint64_t qadd_u64(uint64_t a, uint64_t b, int *sat)
{
	uint64_t s = a + b;

	if (s < a) {
		*sat = 1;
		return ~UINT64_C(0);
	}
	return s;
}

static uint64_t qsub_u64(uint64_t a, uint64_t b, int *sat)
{
	if (a < b) {
		*sat = 1;
		return 0;
	}
	return a - b;
}

/* VMUL.P8: carry-less product of two 8-bit polynomials, low 8 bits. */
static uint64_t pmul8(uint64_t a, uint64_t b)
{
	uint64_t r = 0;
	unsigned i;

	for (i = 0; i < 8; i++)
		if ((b >> i) & 1u)
			r ^= (a & 0xffu) << i;
	return r & 0xffu;
}

/* Signed byte shift count used by VSHL/VQSHL/VRSHL/VQRSHL: Elem[Vn]<7:0>, sign-extended. */
static int shift_count(uint64_t n)
{
	return (int)(n & 0x7fu) - (int)(n & 0x80u);
}

static void set_qc(struct n2a32_cpu *c)
{
	c->fpscr |= N2_QC;
	c->q = 1;                        /* APSR.Q is FPSCR.QC (ARM ARM APSR.Q == FPSCR.QC) */
}

/* ---------------------------------------------------------------------------------------------------- */
/* integer lane operations                                                                               */

enum n2_iop {
	N2I_ADD, N2I_SUB, N2I_HADD, N2I_HSUB, N2I_RHADD, N2I_QADD, N2I_QSUB,
	N2I_CGT, N2I_CGE, N2I_CEQ, N2I_TST,
	N2I_SHL, N2I_QSHL, N2I_RSHL, N2I_QRSHL,
	N2I_MAX, N2I_MIN, N2I_ABD, N2I_ABA,
	N2I_MUL, N2I_MLA, N2I_MLS, N2I_QDMULH, N2I_QRDMULH, N2I_PMUL
};

/* One element of an integer three-same operation.  d/n/m are the old destination, Vn and Vm elements;
 * for the shift operations the shift count comes from Vn (Elem[Vn]<7:0>) and the shifted value from Vm,
 * exactly as the ARM ARM's VSHL/VQSHL/VRSHL/VQRSHL pseudocode specifies. */
static uint64_t ilane(struct n2a32_cpu *c, int op, unsigned esize, int sg,
		      uint64_t d, uint64_t n, uint64_t m)
{
	uint64_t mask = mask_of(esize);
	uint64_t a = n & mask, b = m & mask, acc = d & mask;
	uint64_t sa, sb, r;
	int sat = 0;

	switch (op) {
	case N2I_ADD:
		return (a + b) & mask;

	case N2I_SUB:
		return (a - b) & mask;

	case N2I_HADD:
	case N2I_HSUB:
		if (sg) {
			sa = sx64(a, esize);
			sb = sx64(b, esize);
			r = (op == N2I_HADD) ? sa + sb : sa - sb;
			return asr64(r, 1) & mask;
		}
		/* Unsigned: the intermediate is esize+1 bits wide; in the 64-bit pattern the low
		 * esize bits after a logical shift are the same as in that wider intermediate. */
		r = (op == N2I_HADD) ? a + b : a - b;
		return (r >> 1) & mask;

	case N2I_RHADD:
		if (sg) {
			sa = sx64(a, esize);
			sb = sx64(b, esize);
			return asr64(sa + sb + 1, 1) & mask;
		}
		return ((a + b + 1) >> 1) & mask;

	case N2I_QADD:
		if (sg) {
			if (esize == 64)
				r = qadd_s64(a, b, &sat);
			else
				r = clamp_s(sx64(a, esize) + sx64(b, esize), esize, &sat);
		} else {
			if (esize == 64)
				r = qadd_u64(a, b, &sat);
			else
				r = clamp_u(a + b, esize, &sat);
		}
		if (sat)
			set_qc(c);
		return r;

	case N2I_QSUB:
		if (sg) {
			if (esize == 64)
				r = qsub_s64(a, b, &sat);
			else
				r = clamp_s(sx64(a, esize) - sx64(b, esize), esize, &sat);
		} else {
			if (esize == 64)
				r = qsub_u64(a, b, &sat);
			else
				r = a >= b ? a - b : (sat = 1, UINT64_C(0));
		}
		if (sat)
			set_qc(c);
		return r;

	case N2I_CGT:
		if (sg)
			return slt64(sx64(b, esize), sx64(a, esize)) ? mask : 0;
		return a > b ? mask : 0;

	case N2I_CGE:
		if (sg)
			return slt64(sx64(a, esize), sx64(b, esize)) ? 0 : mask;
		return a >= b ? mask : 0;

	case N2I_CEQ:
		return a == b ? mask : 0;

	case N2I_TST:
		return ((a & b) != 0) ? mask : 0;

	case N2I_SHL: {
		int sh = shift_count(n);

		if (sh >= 0) {
			if ((unsigned)sh >= esize)
				return 0;
			return (b << (unsigned)sh) & mask;
		}
		if (sg)
			return asr64(sx64(b, esize), (unsigned)-sh) & mask;
		return (unsigned)-sh >= 64 ? 0 : b >> (unsigned)-sh;
	}

	case N2I_QSHL:
	case N2I_QRSHL: {
		int sh = shift_count(n);
		int round = (op == N2I_QRSHL);

		if (sh == 0)
			return b & mask;
		if (sh >= 0) {
			/* "element << shift", exact, then SignedSatQ/UnsignedSatQ to the element size. */
			if (sg) {
				uint64_t v = sx64(b, esize);

				if ((unsigned)sh >= esize) {
					if (v == 0)
						return 0;
					sat = 1;
					set_qc(c);
					return (v >> 63) != 0 ? (UINT64_C(1) << (esize - 1))
							      : (mask >> 1);
				}
				if (esize == 64) {
					unsigned s = (unsigned)sh;
					uint64_t shifted = v << s;

					/* The exact value v * 2^s is representable iff shifting back
					 * (arithmetically) reproduces v. */
					if (asr64(shifted, s) != v) {
						set_qc(c);
						return (v >> 63) != 0 ? (UINT64_C(1) << 63)
								      : ~(UINT64_C(1) << 63);
					}
					return shifted;
				}
				r = clamp_s(v * (UINT64_C(1) << (unsigned)sh), esize, &sat);
			} else {
				if ((unsigned)sh >= esize) {
					if (b == 0)
						return 0;
					set_qc(c);
					return mask;
				}
				if (esize == 64) {
					if ((b >> (64 - (unsigned)sh)) != 0) {
						set_qc(c);
						return mask;
					}
					return b << (unsigned)sh;
				}
				if ((b >> (esize - (unsigned)sh)) != 0) {
					set_qc(c);
					return mask;
				}
				return (b << (unsigned)sh) & mask;
			}
			if (sat)
				set_qc(c);
			return r;
		}

		/* Negative shift: right shift; with rounding for VQRSHL, then (for VQRSHL) SatQ. */
		if (round) {
			unsigned s = (unsigned)-sh;
			uint64_t rounded;

			if (s >= 64) {
				rounded = 0;
			} else if (sg) {
				uint64_t sv = sx64(b, esize);

				rounded = asr64(sv, s) + ((sv >> (s - 1)) & 1u);
			} else {
				rounded = (b >> s) + ((b >> (s - 1)) & 1u);
			}
			if (sg) {
				if (esize == 64)
					return rounded;
				r = clamp_s(rounded, esize, &sat);
				if (sat)
					set_qc(c);
				return r;
			}
			if (esize == 64)
				return rounded;
			r = clamp_u(rounded, esize, &sat);
			if (sat)
				set_qc(c);
			return r;
		}
		if (sg)
			return asr64(sx64(b, esize), (unsigned)-sh) & mask;
		return (unsigned)-sh >= 64 ? 0 : b >> (unsigned)-sh;
	}

	case N2I_RSHL: {
		int sh = shift_count(n);
		unsigned s;

		if (sh >= 0) {
			if ((unsigned)sh >= esize || (unsigned)sh >= 64)
				return 0;
			return (b << (unsigned)sh) & mask;
		}
		s = (unsigned)-sh;
		if (s >= 64)
			return 0;
		if (sg) {
			uint64_t sv = sx64(b, esize);

			return (asr64(sv, s) + ((sv >> (s - 1)) & 1u)) & mask;
		}
		return ((b >> s) + ((b >> (s - 1)) & 1u)) & mask;
	}

	case N2I_MAX:
		if (sg)
			return slt64(sx64(a, esize), sx64(b, esize)) ? b : a;
		return a < b ? b : a;

	case N2I_MIN:
		if (sg)
			return slt64(sx64(b, esize), sx64(a, esize)) ? b : a;
		return b < a ? b : a;

	case N2I_ABD:
	case N2I_ABA: {
		uint64_t diff;

		if (sg) {
			diff = sx64(a, esize) - sx64(b, esize);
			if ((diff >> 63) != 0)
				diff = UINT64_C(0) - diff;
		} else {
			diff = a >= b ? a - b : b - a;
		}
		if (op == N2I_ABA)
			return (acc + diff) & mask;
		return diff & mask;
	}

	case N2I_MUL:
		return (a * b) & mask;

	case N2I_PMUL:
		return pmul8(a, b);

	case N2I_MLA:
		return (acc + a * b) & mask;

	case N2I_MLS:
		return (acc - a * b) & mask;

	case N2I_QDMULH:
	case N2I_QRDMULH: {
		/* (2*op1*op2 (+ 2^(esize-1))) >> esize, computed as the equivalent
		 * (op1*op2 (+ 2^(esize-2))) >> (esize-1), then SignedSatQ. */
		sa = sx64(a, esize);
		sb = sx64(b, esize);
		r = sa * sb;
		if (op == N2I_QRDMULH)
			r += UINT64_C(1) << (esize - 2);
		r = asr64(r, esize - 1);
		r = clamp_s(r, esize, &sat);
		if (sat)
			set_qc(c);
		return r;
	}
	}
	return 0;
}

static uint64_t elem_get(const union n2a32_dreg *v, unsigned esize, unsigned i)
{
	switch (esize) {
	case 8:
		return v->u8[i];
	case 16:
		return v->u16[i];
	case 32:
		return v->u32[i];
	default:
		(void)i;
		return v->u64;
	}
}

static void elem_set(union n2a32_dreg *v, unsigned esize, unsigned i, uint64_t x)
{
	switch (esize) {
	case 8:
		v->u8[i] = (uint8_t)x;
		break;
	case 16:
		v->u16[i] = (uint16_t)x;
		break;
	case 32:
		v->u32[i] = (uint32_t)x;
		break;
	default:
		(void)i;
		v->u64 = x;
		break;
	}
}

static void int_op(struct n2a32_cpu *c, int op, unsigned esize, int sg,
		   unsigned d, unsigned n, unsigned m, int q)
{
	unsigned regs = q ? 2u : 1u, per = 64u / esize, r, e;

	for (r = 0; r < regs; r++) {
		for (e = 0; e < per; e++) {
			uint64_t acc = elem_get(&c->d[d + r], esize, e);
			uint64_t a = elem_get(&c->d[n + r], esize, e);
			uint64_t b = elem_get(&c->d[m + r], esize, e);

			elem_set(&c->d[d + r], esize, e, ilane(c, op, esize, sg, acc, a, b));
		}
	}
}

/* VPADD/VPMAX/VPMIN (integer): D form only; first half of the result from Vn pairs, second from Vm. */
static void pair_int(struct n2a32_cpu *c, int op, unsigned esize, int sg,
		     unsigned d, unsigned n, unsigned m)
{
	unsigned per = 64u / esize, h = per / 2u, e;

	for (e = 0; e < h; e++) {
		uint64_t x = elem_get(&c->d[n], esize, 2 * e);
		uint64_t y = elem_get(&c->d[n], esize, 2 * e + 1);

		elem_set(&c->d[d], esize, e, ilane(c, op, esize, sg, 0, x, y));
		x = elem_get(&c->d[m], esize, 2 * e);
		y = elem_get(&c->d[m], esize, 2 * e + 1);
		elem_set(&c->d[d], esize, e + h, ilane(c, op, esize, sg, 0, x, y));
	}
}

/* VADD/VSUB integer: exhaustive wrap-around, no flags. */
static void addsub_int(struct n2a32_cpu *c, int sub, unsigned esize,
		       unsigned d, unsigned n, unsigned m, int q)
{
	unsigned regs = q ? 2u : 1u, per = 64u / esize, r, e;
	uint64_t mask = mask_of(esize);

	for (r = 0; r < regs; r++) {
		for (e = 0; e < per; e++) {
			uint64_t a = elem_get(&c->d[n + r], esize, e);
			uint64_t b = elem_get(&c->d[m + r], esize, e);

			elem_set(&c->d[d + r], esize, e,
				 (sub ? a - b : a + b) & mask);
		}
	}
}

enum n2_lop { N2L_AND, N2L_BIC, N2L_ORR, N2L_ORN, N2L_EOR, N2L_BSL, N2L_BIT, N2L_BIF };

static void logic_op(struct n2a32_cpu *c, int op, unsigned d, unsigned n, unsigned m, int q)
{
	unsigned regs = q ? 2u : 1u, r;

	for (r = 0; r < regs; r++) {
		uint64_t a = c->d[n + r].u64, b = c->d[m + r].u64, acc = c->d[d + r].u64, v;

		switch (op) {
		case N2L_AND:
			v = a & b;
			break;
		case N2L_BIC:
			v = a & ~b;
			break;
		case N2L_ORR:
			v = a | b;
			break;
		case N2L_ORN:
			v = a | ~b;
			break;
		case N2L_EOR:
			v = a ^ b;
			break;
		case N2L_BSL:
			/* ARM ARM: Dd = (Dn AND Dd) | (Dm AND NOT Dd) */
			v = (a & acc) | (b & ~acc);
			break;
		case N2L_BIT:
			/* ARM ARM: Dd = (Dn AND Dm) | (Dd AND NOT Dm) */
			v = (a & b) | (acc & ~b);
			break;
		default:
			/* ARM ARM: Dd = (Dd AND Dm) | (Dn AND NOT Dm) */
			v = (acc & b) | (a & ~b);
			break;
		}
		c->d[d + r].u64 = v;
	}
}

/* ---------------------------------------------------------------------------------------------------- */
/* float32 helpers: the standard FPSCR value = round to nearest even, DN = 1, FZ = 1.                    */

static int f32_is_nan(uint32_t u)
{
	return (u & 0x7f800000u) == 0x7f800000u && (u & 0x007fffffu) != 0;
}

static int f32_is_qnan(uint32_t u)
{
	return f32_is_nan(u) && (u & 0x00400000u) != 0;
}

static int f32_is_inf(uint32_t u)
{
	return (u & 0x7fffffffu) == 0x7f800000u;
}

static int f32_is_zero(uint32_t u)
{
	return (u & 0x7fffffffu) == 0;
}

static float f32_of(uint32_t u)
{
	float f;

	memcpy(&f, &u, 4);
	return f;
}

static uint32_t f32_bits(float f)
{
	uint32_t u;

	memcpy(&u, &f, 4);
	return u;
}

/* FPUnpack() with FZ = 1: a denormal input is flushed to a signed zero. */
static uint32_t f32_flush_in(uint32_t u)
{
	if ((u & 0x7f800000u) == 0)
		return u & 0x80000000u;
	return u;
}

/* Result processing with DN = 1 and FZ = 1: any NaN becomes the default NaN, a denormal result a
 * signed zero. */
static uint32_t f32_result(float f)
{
	uint32_t u = f32_bits(f);

	if (f32_is_nan(u))
		return N2_F32_DEFAULT_NAN;
	if ((u & 0x7f800000u) == 0 && (u & 0x007fffffu) != 0)
		u &= 0x80000000u;
	return u;
}

static uint32_t f32_add(uint32_t a, uint32_t b)
{
	a = f32_flush_in(a);
	b = f32_flush_in(b);
	if (f32_is_nan(a) || f32_is_nan(b))
		return N2_F32_DEFAULT_NAN;
	return f32_result(f32_of(a) + f32_of(b));
}

static uint32_t f32_sub(uint32_t a, uint32_t b)
{
	a = f32_flush_in(a);
	b = f32_flush_in(b);
	if (f32_is_nan(a) || f32_is_nan(b))
		return N2_F32_DEFAULT_NAN;
	return f32_result(f32_of(a) - f32_of(b));
}

static uint32_t f32_mul(uint32_t a, uint32_t b)
{
	a = f32_flush_in(a);
	b = f32_flush_in(b);
	if (f32_is_nan(a) || f32_is_nan(b))
		return N2_F32_DEFAULT_NAN;
	return f32_result(f32_of(a) * f32_of(b));
}

/* VMLA/VMLS: FPMul then FPAdd, each rounded separately (not fused). */
static uint32_t f32_mla(uint32_t a, uint32_t b, uint32_t d, int sub)
{
	uint32_t p;

	a = f32_flush_in(a);
	b = f32_flush_in(b);
	d = f32_flush_in(d);
	if (f32_is_nan(a) || f32_is_nan(b) || f32_is_nan(d))
		return N2_F32_DEFAULT_NAN;
	p = f32_result(f32_of(a) * f32_of(b));
	if (sub)
		p ^= 0x80000000u;
	return f32_result(f32_of(d) + f32_of(p));
}

/* VFMA/VFMS: FPMulAdd with a single rounding (ARM ARM "does not round the result of the multiply
 * before the accumulation").  Double intermediates are exact for this: the product of two binary32
 * values has at most 48 significant bits, so the double result is the correctly rounded exact value
 * and the final conversion is a harmless double rounding (53 >= 2*24+2). */
static uint32_t f32_fma(uint32_t a, uint32_t b, uint32_t d, int sub)
{
	double va, vb, vd, r;

	a = f32_flush_in(a);
	b = f32_flush_in(b);
	d = f32_flush_in(d);
	if (f32_is_nan(a) || f32_is_nan(b) || f32_is_nan(d))
		return N2_F32_DEFAULT_NAN;
	va = (double)f32_of(a);
	vb = (double)f32_of(b);
	vd = (double)f32_of(d);
	if (sub)
		va = -va;
	r = va * vb + vd;
	return f32_result((float)r);
}

/* FPMax/FPMin with the standard FPSCR value: DN = 1, so any NaN operand yields the default NaN
 * (this is the difference from VMAXNM/VMINNM, see FPMaxNum/FPMinNum in the ARM ARM). */
static uint32_t f32_max(uint32_t a, uint32_t b)
{
	a = f32_flush_in(a);
	b = f32_flush_in(b);
	if (f32_is_nan(a) || f32_is_nan(b))
		return N2_F32_DEFAULT_NAN;
	if (f32_is_zero(a) && f32_is_zero(b))
		return (a & b) & 0x80000000u;            /* +0 unless both are -0 */
	return f32_of(a) > f32_of(b) ? a : b;
}

static uint32_t f32_min(uint32_t a, uint32_t b)
{
	a = f32_flush_in(a);
	b = f32_flush_in(b);
	if (f32_is_nan(a) || f32_is_nan(b))
		return N2_F32_DEFAULT_NAN;
	if (f32_is_zero(a) && f32_is_zero(b))
		return (a | b) & 0x80000000u;            /* -0 if either is -0 */
	return f32_of(a) < f32_of(b) ? a : b;
}

/* FPMaxNum/FPMinNum: a single quiet NaN is treated as -infinity/+infinity, then FPMax/FPMin.
 * A signaling NaN is not replaced, so it still produces the default NaN; both NaN likewise. */
static uint32_t f32_maxnm(uint32_t a, uint32_t b)
{
	a = f32_flush_in(a);
	b = f32_flush_in(b);
	if (f32_is_qnan(a) && !f32_is_qnan(b))
		a = 0xff800000u;
	else if (!f32_is_qnan(a) && f32_is_qnan(b))
		b = 0xff800000u;
	return f32_max(a, b);
}

static uint32_t f32_minnm(uint32_t a, uint32_t b)
{
	a = f32_flush_in(a);
	b = f32_flush_in(b);
	if (f32_is_qnan(a) && !f32_is_qnan(b))
		a = 0x7f800000u;
	else if (!f32_is_qnan(a) && f32_is_qnan(b))
		b = 0x7f800000u;
	return f32_min(a, b);
}

/* FPRecipStepFused(): 2.0 + (-op1)*op2 with a single rounding, 0*inf -> 2.0, NaN -> default NaN,
 * infinity -> infinity with the sign of the negated product. */
static uint32_t f32_recps(uint32_t a, uint32_t b)
{
	a = f32_flush_in(a);
	b = f32_flush_in(b);
	if (f32_is_nan(a) || f32_is_nan(b))
		return N2_F32_DEFAULT_NAN;
	if ((f32_is_inf(a) && f32_is_zero(b)) || (f32_is_zero(a) && f32_is_inf(b)))
		return f32_bits(2.0f);
	if (f32_is_inf(a) || f32_is_inf(b))
		return (((a ^ b) & 0x80000000u) ^ 0x80000000u) | 0x7f800000u;
	return f32_result((float)(2.0 - (double)f32_of(a) * (double)f32_of(b)));
}

/* FPRSqrtStepFused(): (3.0 + (-op1)*op2) / 2.0 with a single rounding; 0*inf -> 1.5. */
static uint32_t f32_rsqrts(uint32_t a, uint32_t b)
{
	a = f32_flush_in(a);
	b = f32_flush_in(b);
	if (f32_is_nan(a) || f32_is_nan(b))
		return N2_F32_DEFAULT_NAN;
	if ((f32_is_inf(a) && f32_is_zero(b)) || (f32_is_zero(a) && f32_is_inf(b)))
		return f32_bits(1.5f);
	if (f32_is_inf(a) || f32_is_inf(b))
		return (((a ^ b) & 0x80000000u) ^ 0x80000000u) | 0x7f800000u;
	return f32_result((float)((3.0 - (double)f32_of(a) * (double)f32_of(b)) * 0.5));
}

enum n2_fop {
	N2F_ADD, N2F_SUB, N2F_MUL, N2F_MLA, N2F_MLS, N2F_FMA, N2F_FMS,
	N2F_CEQ, N2F_CGE, N2F_CGT, N2F_ACGE, N2F_ACGT,
	N2F_MAX, N2F_MIN, N2F_MAXNM, N2F_MINNM, N2F_ABD, N2F_RECPS, N2F_RSQRTS
};

/* One float32 element.  Comparisons return all-ones / all-zeros; NaN always fails the comparison
 * (FPCompare*, ARM ARM). */
static uint32_t flane(int op, uint32_t a, uint32_t b, uint32_t d)
{
	uint32_t fa, fb, r;

	switch (op) {
	case N2F_ADD:
		return f32_add(a, b);
	case N2F_SUB:
		return f32_sub(a, b);
	case N2F_MUL:
		return f32_mul(a, b);
	case N2F_MLA:
		return f32_mla(a, b, d, 0);
	case N2F_MLS:
		return f32_mla(a, b, d, 1);
	case N2F_FMA:
		return f32_fma(a, b, d, 0);
	case N2F_FMS:
		return f32_fma(a, b, d, 1);
	case N2F_MAX:
		return f32_max(a, b);
	case N2F_MIN:
		return f32_min(a, b);
	case N2F_MAXNM:
		return f32_maxnm(a, b);
	case N2F_MINNM:
		return f32_minnm(a, b);
	case N2F_RECPS:
		return f32_recps(a, b);
	case N2F_RSQRTS:
		return f32_rsqrts(a, b);
	default:
		break;
	}

	/* Comparisons. */
	a = f32_flush_in(a);
	b = f32_flush_in(b);
	if (f32_is_nan(a) || f32_is_nan(b))
		return 0;
	if (op == N2F_ABD) {
		r = f32_result(f32_of(a) - f32_of(b));
		return r & 0x7fffffffu;                  /* FPAbs: clear the sign bit */
	}
	fa = a & 0x7fffffffu;                            /* FPAbs for the AC forms */
	fb = b & 0x7fffffffu;
	switch (op) {
	case N2F_CEQ:
		return f32_of(a) == f32_of(b) ? 0xffffffffu : 0;
	case N2F_CGE:
		return f32_of(a) >= f32_of(b) ? 0xffffffffu : 0;
	case N2F_CGT:
		return f32_of(a) > f32_of(b) ? 0xffffffffu : 0;
	case N2F_ACGE:
		return f32_of(fa) >= f32_of(fb) ? 0xffffffffu : 0;
	default:                                         /* N2F_ACGT */
		return f32_of(fa) > f32_of(fb) ? 0xffffffffu : 0;
	}
}

static void float_op(struct n2a32_cpu *c, int op, unsigned d, unsigned n, unsigned m, int q)
{
	unsigned regs = q ? 2u : 1u, r, e;

	for (r = 0; r < regs; r++) {
		for (e = 0; e < 2; e++) {
			uint32_t a = c->d[n + r].u32[e];
			uint32_t b = c->d[m + r].u32[e];
			uint32_t acc = c->d[d + r].u32[e];

			c->d[d + r].u32[e] = flane(op, a, b, acc);
		}
	}
}

/* VPADD.F32/VPMAX.F32/VPMIN.F32: D form only; [0] from the Vn pair, [1] from the Vm pair. */
static void pair_float(struct n2a32_cpu *c, int op, unsigned d, unsigned n, unsigned m)
{
	uint32_t n0 = c->d[n].u32[0], n1 = c->d[n].u32[1];
	uint32_t m0 = c->d[m].u32[0], m1 = c->d[m].u32[1];

	c->d[d].u32[0] = flane(op, n0, n1, 0);
	c->d[d].u32[1] = flane(op, m0, m1, 0);
}

/* The standard FPSCR value fixes round-to-nearest-even: make sure the host FPU uses it while the
 * float operations run, and restore the caller's mode afterwards. */
static void float_op_std(struct n2a32_cpu *c, int op, unsigned d, unsigned n, unsigned m, int q)
{
#if N2_HAVE_FENV
	int saved = fegetround();

	if (saved != FE_TONEAREST)
		(void)fesetround(FE_TONEAREST);
#endif
	float_op(c, op, d, n, m, q);
#if N2_HAVE_FENV
	if (saved != FE_TONEAREST)
		(void)fesetround(saved);
#endif
}

static void pair_float_std(struct n2a32_cpu *c, int op, unsigned d, unsigned n, unsigned m)
{
#if N2_HAVE_FENV
	int saved = fegetround();

	if (saved != FE_TONEAREST)
		(void)fesetround(FE_TONEAREST);
#endif
	pair_float(c, op, d, n, m);
#if N2_HAVE_FENV
	if (saved != FE_TONEAREST)
		(void)fesetround(saved);
#endif
}

/* ---------------------------------------------------------------------------------------------------- */
/* decode                                                                                                */

static unsigned vd_of(uint32_t insn)
{
	return ((insn >> 18) & 0x10u) | ((insn >> 12) & 0xfu);
}

static unsigned vn_of(uint32_t insn)
{
	return ((insn >> 3) & 0x10u) | ((insn >> 16) & 0xfu);
}

static unsigned vm_of(uint32_t insn)
{
	return ((insn >> 1) & 0x10u) | (insn & 0xfu);
}

int n2a32_neon_3same(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned u, size, opc, b4, q, d, n, m, es;
	int sg;

	/* 1111 001U 0--... : the three-registers-same-length space. */
	if ((insn & 0xfe800000u) != 0xf2000000u)
		return N2A32_UNDEF;

	u = (insn >> 24) & 1u;
	size = (insn >> 20) & 3u;
	opc = (insn >> 8) & 0xfu;
	b4 = (insn >> 4) & 1u;
	q = (insn >> 6) & 1u;

	d = vd_of(insn);
	n = vn_of(insn);
	m = vm_of(insn);

	/* Q form: the operands are D-register pairs (Qd = D(Vd), D(Vd)+1), so the 5-bit D-register
	 * numbers must be even.  (D/N/M themselves may be set: that is how Q8-Q15 are encoded.)
	 * An odd number is architecturally UNPREDICTABLE and llvm-mc rejects it. */
	if (q != 0 && ((d | n | m) & 1u) != 0)
		return N2A32_UNDEF;
	es = 8u << size;
	sg = (u == 0);

	switch (opc) {
	case 0x0:
		if (b4 == 0) {
			if (size == 3)
				return N2A32_UNDEF;      /* VHADD has no 64-bit form */
			int_op(c, N2I_HADD, es, sg, d, n, m, (int)q);
		} else {
			int_op(c, N2I_QADD, es, sg, d, n, m, (int)q);
		}
		return N2A32_OK;

	case 0x1:
		if (b4 == 0) {
			if (size == 3)
				return N2A32_UNDEF;      /* VRHADD has no 64-bit form */
			int_op(c, N2I_RHADD, es, sg, d, n, m, (int)q);
		} else {
			int lop;

			if (u == 0)
				lop = size == 0 ? N2L_AND : size == 1 ? N2L_BIC
				    : size == 2 ? N2L_ORR : N2L_ORN;
			else
				lop = size == 0 ? N2L_EOR : size == 1 ? N2L_BSL
				    : size == 2 ? N2L_BIT : N2L_BIF;
			logic_op(c, lop, d, n, m, (int)q);
		}
		return N2A32_OK;

	case 0x2:
		if (b4 == 0) {
			if (size == 3)
				return N2A32_UNDEF;      /* VHSUB has no 64-bit form */
			int_op(c, N2I_HSUB, es, sg, d, n, m, (int)q);
		} else {
			int_op(c, N2I_QSUB, es, sg, d, n, m, (int)q);
		}
		return N2A32_OK;

	case 0x3:
		if (size == 3)
			return N2A32_UNDEF;          /* VCGT/VCGE have no 64-bit form */
		int_op(c, b4 ? N2I_CGE : N2I_CGT, es, sg, d, n, m, (int)q);
		return N2A32_OK;

	case 0x4:
		int_op(c, b4 ? N2I_QSHL : N2I_SHL, es, sg, d, n, m, (int)q);
		return N2A32_OK;

	case 0x5:
		int_op(c, b4 ? N2I_QRSHL : N2I_RSHL, es, sg, d, n, m, (int)q);
		return N2A32_OK;

	case 0x6:
		if (size == 3)
			return N2A32_UNDEF;          /* VMAX/VMIN have no 64-bit form */
		int_op(c, b4 ? N2I_MIN : N2I_MAX, es, sg, d, n, m, (int)q);
		return N2A32_OK;

	case 0x7:
		if (size == 3)
			return N2A32_UNDEF;          /* VABD/VABA have no 64-bit form */
		int_op(c, b4 ? N2I_ABA : N2I_ABD, es, sg, d, n, m, (int)q);
		return N2A32_OK;

	case 0x8:
		if (b4 == 0)
			addsub_int(c, (int)u, es, d, n, m, (int)q);
		else {
			if (size == 3)
				return N2A32_UNDEF;  /* VTST/VCEQ have no 64-bit form */
			int_op(c, u != 0 ? N2I_CEQ : N2I_TST, es, 0, d, n, m, (int)q);
		}
		return N2A32_OK;

	case 0x9:
		if (b4 == 0) {
			if (size == 3)
				return N2A32_UNDEF;  /* VMLA/VMLS have no 64-bit form */
			int_op(c, u != 0 ? N2I_MLS : N2I_MLA, es, 0, d, n, m, (int)q);
		} else {
			if (u != 0) {
				if (size != 0)
					return N2A32_UNDEF;      /* VMUL.P8 only */
				int_op(c, N2I_PMUL, 8, 0, d, n, m, (int)q);
			} else {
				if (size == 3)
					return N2A32_UNDEF;
				int_op(c, N2I_MUL, es, 0, d, n, m, (int)q);
			}
		}
		return N2A32_OK;

	case 0xa:
		if (q != 0 || size == 3)
			return N2A32_UNDEF;          /* VPMAX/VPMIN are D form only */
		pair_int(c, b4 ? N2I_MIN : N2I_MAX, es, sg, d, n, m);
		return N2A32_OK;

	case 0xb:
		if (b4 == 0) {
			if (size == 0 || size == 3)
				return N2A32_UNDEF;  /* VQDMULH/VQRDMULH: 16 and 32 bit only */
			int_op(c, u != 0 ? N2I_QRDMULH : N2I_QDMULH, es, 1, d, n, m, (int)q);
		} else {
			if (u != 0 || q != 0 || size == 3)
				return N2A32_UNDEF;  /* VPADD is D form only */
			pair_int(c, N2I_ADD, es, 0, d, n, m);
		}
		return N2A32_OK;

	case 0xc:
		/* bit4 == 0 is the SHA1/SHA256 crypto space: deliberately not implemented.
		 * bit4 == 1: VFMA.F32 (size 00) / VFMS.F32 (size 10), U == 0. */
		if (b4 == 0 || u != 0)
			return N2A32_UNDEF;
		if (size == 0)
			float_op_std(c, N2F_FMA, d, n, m, (int)q);
		else if (size == 2)
			float_op_std(c, N2F_FMS, d, n, m, (int)q);
		else
			return N2A32_UNDEF;
		return N2A32_OK;

	case 0xd:
		if (b4 == 0) {
			if (u == 0) {
				if (size == 0)
					float_op_std(c, N2F_ADD, d, n, m, (int)q);
				else if (size == 2)
					float_op_std(c, N2F_SUB, d, n, m, (int)q);
				else
					return N2A32_UNDEF;
			} else {
				if (size == 0) {
					if (q != 0)
						return N2A32_UNDEF;      /* VPADD.F32: D only */
					pair_float_std(c, N2F_ADD, d, n, m);
				} else if (size == 2) {
					float_op_std(c, N2F_ABD, d, n, m, (int)q);
				} else {
					return N2A32_UNDEF;
				}
			}
		} else {
			if (u == 0) {
				if (size == 0)
					float_op_std(c, N2F_MLA, d, n, m, (int)q);
				else if (size == 2)
					float_op_std(c, N2F_MLS, d, n, m, (int)q);
				else
					return N2A32_UNDEF;
			} else {
				if (size != 0)
					return N2A32_UNDEF;
				float_op_std(c, N2F_MUL, d, n, m, (int)q);
			}
		}
		return N2A32_OK;

	case 0xe:
		if (b4 == 0) {
			if (u == 0) {
				if (size != 0)
					return N2A32_UNDEF;
				float_op_std(c, N2F_CEQ, d, n, m, (int)q);
			} else if (size == 0) {
				float_op_std(c, N2F_CGE, d, n, m, (int)q);
			} else if (size == 2) {
				float_op_std(c, N2F_CGT, d, n, m, (int)q);
			} else {
				return N2A32_UNDEF;
			}
		} else {
			if (u == 0)
				return N2A32_UNDEF;
			if (size == 0)
				float_op_std(c, N2F_ACGE, d, n, m, (int)q);
			else if (size == 2)
				float_op_std(c, N2F_ACGT, d, n, m, (int)q);
			else
				return N2A32_UNDEF;
		}
		return N2A32_OK;

	case 0xf:
		if (b4 == 0) {
			if (u == 0) {
				if (size == 0)
					float_op_std(c, N2F_MAX, d, n, m, (int)q);
				else if (size == 2)
					float_op_std(c, N2F_MIN, d, n, m, (int)q);
				else
					return N2A32_UNDEF;
			} else {
				if (q != 0)
					return N2A32_UNDEF;      /* VPMAX/VPMIN.F32: D only */
				if (size == 0)
					pair_float_std(c, N2F_MAX, d, n, m);
				else if (size == 2)
					pair_float_std(c, N2F_MIN, d, n, m);
				else
					return N2A32_UNDEF;
			}
		} else {
			if (u == 0) {
				if (size == 0)
					float_op_std(c, N2F_RECPS, d, n, m, (int)q);
				else if (size == 2)
					float_op_std(c, N2F_RSQRTS, d, n, m, (int)q);
				else
					return N2A32_UNDEF;
			} else {
				if (size == 0)
					float_op_std(c, N2F_MAXNM, d, n, m, (int)q);
				else if (size == 2)
					float_op_std(c, N2F_MINNM, d, n, m, (int)q);
				else
					return N2A32_UNDEF;
			}
		}
		return N2A32_OK;
	}
	return N2A32_UNDEF;
}
