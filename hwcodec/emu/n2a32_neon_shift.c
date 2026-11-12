/* SPDX-License-Identifier: MIT */
/*
 * n2a32_neon_shift.c -- V19: Advanced SIMD (NEON) two registers + shift amount, three registers of
 * different lengths, and two registers + scalar.
 *
 * Input is the A32 form of the instruction (the core converts T32 111U 1111 ... to 1111 001U ...),
 * so the top seven bits are always 1111001.  The function implements the classes below and returns
 * N2A32_UNDEF for every other encoding (including the V20 spaces it must not touch).  No branches,
 * no memory access, no printing; QC is FPSCR bit 27 (n2a32.h; same convention as V18).
 *
 * Encoding summary for the 1111001 U 1 D ... space (ARM ARM A7.4.4/A7.4.5):
 *   bit23 == 1 always here.
 *   bit4 == 1: two registers and a shift amount.  bits21-19==0 && bit7==0 is the V20 modified
 *              immediate space; every other encoding is V19 (opc 0..7 shifts, 8/9 narrow,
 *              a widen, e/f VCVT fixed-point).
 *   bit4 == 0, bits21-20 == 3: V20 two-registers-miscellaneous; V19 never uses size 3.
 *   bit4 == 0 otherwise: bit6==0 three registers of different lengths, bit6==1 two registers and
 *              a scalar.
 *
 * The shift encoding is imm7 = (bit7 << 6) | bits21-16.  With esize the element size:
 *   right shifts: imm7 = 2*esize - shift (shift 1..esize)
 *   left  shifts: imm7 = esize + shift   (shift 0..esize-1)
 *   narrow:       imm7 = src_esize - shift (src 16/32/64, shift 1..src/2)
 *   widen:        imm7 = src_esize + shift (src 8/16/32, shift 0..src-1)
 *   VCVT fixed:   imm7 = 64 - fbits (fbits 1..32)
 * The ranges [8,16), [16,32), [32,64), [64,128) identify the element size unambiguously because of
 * the architecturally allowed shift ranges (llvm-mc rejects everything outside them).
 */
#include "n2a32.h"
#include <fenv.h>

#define N2_QC (1u << 27)
/* Vd may overlap Vn/Vm, so every source read uses a snapshot taken on entry. */

/* --------------------------------------------------------------------------------------------------------- */
/* small helpers                                                                                              */

static void set_qc(struct n2a32_cpu *c)
{
	c->fpscr |= N2_QC;
}

static uint64_t mask_bits(unsigned bits)
{
	return bits >= 64 ? ~UINT64_C(0) : ((UINT64_C(1) << bits) - 1);
}

static int64_t sext_bits(uint64_t v, unsigned bits)
{
	uint64_t m;

	if (bits >= 64)
		return (int64_t)v;
	m = UINT64_C(1) << (bits - 1);
	return (int64_t)((v ^ m) - m);
}

/* arithmetic shift right without relying on the implementation-defined signed >> */
static int64_t asr64(int64_t v, unsigned s)
{
	if (s == 0)
		return v;
	if (s >= 64)
		return v < 0 ? -1 : 0;
	if (v >= 0)
		return (int64_t)((uint64_t)v >> s);
	return (int64_t)~(~(uint64_t)v >> s);
}

static int64_t sat_signed(int64_t v, unsigned bits)
{
	int64_t hi, lo;

	if (bits >= 64)
		return v;
	hi = (INT64_C(1) << (bits - 1)) - 1;
	lo = -(INT64_C(1) << (bits - 1));
	if (v > hi)
		return hi;
	if (v < lo)
		return lo;
	return v;
}

static uint64_t sat_unsigned(uint64_t v, unsigned bits)
{
	uint64_t hi;

	if (bits >= 64)
		return v;
	hi = (UINT64_C(1) << bits) - 1;
	return v > hi ? hi : v;
}

static uint64_t elem_get(const union n2a32_dreg *d, unsigned esize, unsigned i)
{
	switch (esize) {
	case 8:
		return d->u8[i];
	case 16:
		return d->u16[i];
	case 32:
		return d->u32[i];
	default:
		(void)i;
		return d->u64;
	}
}

static void elem_set(union n2a32_dreg *d, unsigned esize, unsigned i, uint64_t v)
{
	switch (esize) {
	case 8:
		d->u8[i] = (uint8_t)v;
		break;
	case 16:
		d->u16[i] = (uint16_t)v;
		break;
	case 32:
		d->u32[i] = (uint32_t)v;
		break;
	default:
		(void)i;
		d->u64 = v;
		break;
	}
}

/* dreg is the first D register of the vector: one D for a 64-bit vector, two for a Q (dreg even). */
static uint64_t vec_get(const union n2a32_dreg *src, unsigned dreg, unsigned esize, unsigned i)
{
	unsigned per = 64u / esize;

	return elem_get(&src[dreg + i / per], esize, i % per);
}

static void vec_set(struct n2a32_cpu *c, unsigned dreg, unsigned esize, unsigned i, uint64_t v)
{
	unsigned per = 64u / esize;

	elem_set(&c->d[dreg + i / per], esize, i % per, v);
}

/* right shifts, logical and arithmetic, with and without the rounding constant 2^(shift-1) */
static uint64_t rshift_u(uint64_t v, unsigned shift)
{
	if (shift == 0)
		return v;
	if (shift >= 64)
		return 0;
	return v >> shift;
}

static uint64_t rshift_round_u(uint64_t v, unsigned shift)
{
	if (shift == 0)
		return v;
	if (shift >= 64)
		return (v >> 63) & 1;      /* carry out of the 64-bit addend */
	return (v >> shift) + ((v >> (shift - 1)) & 1);
}

static int64_t rshift_round_s(int64_t v, unsigned shift)
{
	if (shift == 0)
		return v;
	if (shift >= 64)
		return 0;                  /* (v + 2^63) >> 64 is 0 for every 64-bit v */
	return asr64(v, shift) + (int64_t)(((uint64_t)v >> (shift - 1)) & 1);
}

/* VSRI: shift the source right, insert it into the low bits of the destination; the most
 * significant `shift' bits of the destination are unchanged. */
static uint64_t vsri(uint64_t dv, uint64_t sv, unsigned esize, unsigned shift)
{
	uint64_t mask;

	if (shift == 0)
		return sv;                 /* not encodable (right shift range is 1..esize) */
	if (shift >= esize)
		return dv;
	mask = mask_bits(esize - shift);
	return (dv & ~mask) | ((sv >> shift) & mask);
}

/* VSLI: shift the source left, insert it into the high bits of the destination; the least
 * significant `shift' bits of the destination are unchanged. */
static uint64_t vsli(uint64_t dv, uint64_t sv, unsigned esize, unsigned shift)
{
	uint64_t mask;

	if (shift == 0)
		return sv;                 /* nothing of the destination is kept */
	mask = mask_bits(esize - shift) << shift;
	return (dv & ~mask) | ((sv << shift) & mask);
}

/* saturating left shift, signed source/result */
static int64_t qshl_s(int64_t v, unsigned shift, unsigned bits, int *sat)
{
	int64_t hi = (bits >= 64) ? INT64_MAX : ((INT64_C(1) << (bits - 1)) - 1);
	int64_t lo = (bits >= 64) ? INT64_MIN : -(INT64_C(1) << (bits - 1));

	if (v == 0)
		return 0;
	if (shift >= bits) {
		*sat = 1;
		return v > 0 ? hi : lo;
	}
	if (v > 0) {
		if (v > asr64(hi, shift)) {
			*sat = 1;
			return hi;
		}
	} else if (v < asr64(lo, shift)) {
		*sat = 1;
		return lo;
	}
	return (int64_t)((uint64_t)v << shift);
}

/* saturating left shift, unsigned source/result */
static uint64_t qshl_u(uint64_t v, unsigned shift, unsigned bits, int *sat)
{
	uint64_t hi = mask_bits(bits);

	v &= hi;
	if (v == 0)
		return 0;
	if (shift >= bits || v > (hi >> shift)) {
		*sat = 1;
		return hi;
	}
	return (v << shift) & hi;
}

/* VQSHLU: signed source, unsigned result */
static uint64_t qshlu_s(int64_t v, unsigned shift, unsigned bits, int *sat)
{
	if (v < 0) {
		*sat = 1;
		return 0;
	}
	return qshl_u((uint64_t)v, shift, bits, sat);
}

static int64_t narrow_s(int64_t v, unsigned shift, unsigned dbits, int round, int *sat)
{
	int64_t r = round ? rshift_round_s(v, shift) : asr64(v, shift);
	int64_t s = sat_signed(r, dbits);

	if (s != r)
		*sat = 1;
	return s;
}

static uint64_t narrow_u(uint64_t v, unsigned shift, unsigned dbits, int round, int *sat)
{
	uint64_t r = round ? rshift_round_u(v, shift) : rshift_u(v, shift);
	uint64_t s = sat_unsigned(r, dbits);

	if (s != r)
		*sat = 1;
	return s;
}

static uint64_t narrow_su(int64_t v, unsigned shift, unsigned dbits, int round, int *sat)
{
	int64_t r = round ? rshift_round_s(v, shift) : asr64(v, shift);
	uint64_t s;

	if (r < 0) {
		*sat = 1;
		return 0;
	}
	s = sat_unsigned((uint64_t)r, dbits);
	if (s != (uint64_t)r)
		*sat = 1;
	return s;
}

/* 2*product saturated to `bits' (VQDMULL/VQDMLAL/VQDMLSL) */
static int64_t dbl_sat(int64_t p, unsigned bits, int *sat)
{
	int64_t d, s;

	if (bits >= 64) {
		if (p > INT64_MAX / 2) {
			*sat = 1;
			return INT64_MAX;
		}
		return p * 2;              /* p >= -2^62, so no negative overflow */
	}
	d = p * 2;
	s = sat_signed(d, bits);
	if (s != d)
		*sat = 1;
	return s;
}

static int64_t add_sat(int64_t a, int64_t b, unsigned bits, int *sat)
{
	int64_t sum, s;

	if (bits >= 64) {
		if (b > 0 && a > INT64_MAX - b) {
			*sat = 1;
			return INT64_MAX;
		}
		if (b < 0 && a < INT64_MIN - b) {
			*sat = 1;
			return INT64_MIN;
		}
		return a + b;
	}
	sum = a + b;
	s = sat_signed(sum, bits);
	if (s != sum)
		*sat = 1;
	return s;
}

static int64_t sub_sat(int64_t a, int64_t b, unsigned bits, int *sat)
{
	int64_t diff, s;

	if (bits >= 64) {
		if (b < 0 && a > INT64_MAX + b) {
			*sat = 1;
			return INT64_MAX;
		}
		if (b > 0 && a < INT64_MIN + b) {
			*sat = 1;
			return INT64_MIN;
		}
		return a - b;
	}
	diff = a - b;
	s = sat_signed(diff, bits);
	if (s != diff)
		*sat = 1;
	return s;
}

/* VQDMULH/VQRDMULH: (2*p + (round ? 2^(bits-1) : 0)) >> bits, saturated to `bits'. */
static int64_t qdmulh(int64_t p, unsigned bits, int round, int *sat)
{
	int64_t r;

	if (bits == 32) {
		if (p == (INT64_C(1) << 62)) {          /* only INT32_MIN * INT32_MIN */
			*sat = 1;
			return INT32_MAX;
		}
		if (round)
			r = asr64(p, 31) + (int64_t)(((uint64_t)p >> 30) & 1);
		else
			r = asr64(p * 2, 32);
		if (r > INT32_MAX) {
			*sat = 1;
			return INT32_MAX;
		}
		if (r < INT32_MIN) {
			*sat = 1;
			return INT32_MIN;
		}
		return r;
	} else {
		int64_t d = p * 2;
		int64_t shifted;

		if (round)
			d += INT64_C(1) << (bits - 1);
		shifted = asr64(d, bits);
		r = sat_signed(shifted, bits);
		if (r != shifted)
			*sat = 1;
		return r;
	}
}

/* --------------------------------------------------------------------------------------------------------- */
/* NEON float32: the standard FPSCR value (round to nearest even, default NaN, flush to zero).                */

static float f32_from_bits(uint32_t u)
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

static float f32_flush(float f)
{
	uint32_t u = f32_bits(f);

	if ((u & 0x7f800000u) == 0) {           /* denormal (or zero): flush to signed zero */
		u &= 0x80000000u;
		f = f32_from_bits(u);
	}
	return f;
}

static float f32_std(float f)
{
	if (f != f)                             /* any NaN becomes the default NaN (DN=1) */
		f = f32_from_bits(0x7fc00000u);
	return f32_flush(f);
}

/* op: 0 = a*b, 1 = d + a*b, 2 = d - a*b; every operation is rounded separately (no FMA). */
static uint32_t f32_scalar_op(uint32_t a, uint32_t b, uint32_t d, int op)
{
	int saved = fegetround();
	float fa, fb, fd, p, r;
	volatile float prod, sum;

	if (saved != FE_TONEAREST)
		(void)fesetround(FE_TONEAREST);
	fa = f32_flush(f32_from_bits(a));
	fb = f32_flush(f32_from_bits(b));
	prod = fa * fb;
	p = f32_std(prod);
	if (op == 0) {
		r = p;
	} else {
		fd = f32_flush(f32_from_bits(d));
		if (op == 1)
			sum = fd + p;
		else
			sum = fd - p;
		r = f32_std(sum);
	}
	if (saved != FE_TONEAREST)
		(void)fesetround(saved);
	return f32_bits(r);
}

/* VCVT.<Td>.F32: fp * 2^fbits, round toward zero, saturate to 32 bits. */
static uint32_t fp_to_fixed(uint32_t bits, unsigned fbits, int is_unsigned, int *sat)
{
	float f = f32_from_bits(bits);
	double scaled;

	if (f != f)
		return 0;                        /* NaN converts to zero */
	scaled = (double)f * (double)(UINT64_C(1) << fbits);
	if (is_unsigned) {
		if (scaled < 0.0) {
			*sat = 1;
			return 0;
		}
		if (scaled >= 4294967296.0) {
			*sat = 1;
			return UINT32_MAX;
		}
		return (uint32_t)scaled;         /* C cast truncates toward zero */
	}
	if (scaled >= 2147483648.0) {
		*sat = 1;
		return (uint32_t)INT32_MAX;
	}
	if (scaled < -2147483648.0) {
		*sat = 1;
		return (uint32_t)INT32_MIN;
	}
	return (uint32_t)(int32_t)scaled;
}

/* VCVT.F32.<Td>: int / 2^fbits, rounded to nearest even (host FE_TONEAREST). */
static uint32_t fixed_to_fp(uint32_t bits, unsigned fbits, int is_unsigned)
{
	int saved = fegetround();
	double scale = (double)(UINT64_C(1) << fbits);
	double v;
	float f;

	if (saved != FE_TONEAREST)
		(void)fesetround(FE_TONEAREST);
	if (is_unsigned)
		v = (double)(uint32_t)bits / scale;
	else
		v = (double)(int32_t)bits / scale;
	f = (float)v;
	f = f32_std(f);
	if (saved != FE_TONEAREST)
		(void)fesetround(saved);
	return f32_bits(f);
}

/* --------------------------------------------------------------------------------------------------------- */
/* two registers and a shift amount                                                                           */

static int do_shift(struct n2a32_cpu *c, const union n2a32_dreg *src, uint32_t insn)
{
	unsigned U = (insn >> 24) & 1u, D = (insn >> 22) & 1u;
	unsigned Vd = (insn >> 12) & 0xfu, opc = (insn >> 8) & 0xfu;
	unsigned N = (insn >> 7) & 1u, Q = (insn >> 6) & 1u, M = (insn >> 5) & 1u;
	unsigned Vm = insn & 0xfu;
	unsigned dreg = (D << 4) | Vd, mreg = (M << 4) | Vm;
	unsigned imm7, esize, shift, nd, total, i;
	uint64_t emask;

	/* one register and a modified immediate belongs to V20 */
	if (((insn >> 19) & 7u) == 0 && N == 0)
		return N2A32_UNDEF;
	imm7 = (N << 6) | ((insn >> 16) & 0x3fu);

	if (opc == 8 || opc == 9) {
		unsigned src_esize, dbits;

		if (imm7 < 8)
			return N2A32_UNDEF;
		if (imm7 < 16) {
			src_esize = 16;
			shift = 16 - imm7;
		} else if (imm7 < 32) {
			src_esize = 32;
			shift = 32 - imm7;
		} else if (imm7 < 64) {
			src_esize = 64;
			shift = 64 - imm7;
		} else {
			return N2A32_UNDEF;
		}
		if (mreg & 1u)                   /* source is a Q register */
			return N2A32_UNDEF;
		dbits = src_esize / 2;
		total = 128u / src_esize;
		for (i = 0; i < total; i++) {
			uint64_t sv = vec_get(src, mreg, src_esize, i);
			int64_t ssv = sext_bits(sv, src_esize);
			int sat = 0;
			uint64_t r;

			if (opc == 8) {
				if (U == 0)
					r = Q ? rshift_round_u(sv, shift) : rshift_u(sv, shift);
				else
					r = narrow_su(ssv, shift, dbits, (int)Q, &sat);
			} else {
				if (U == 0)
					r = narrow_s(ssv, shift, dbits, (int)Q, &sat);
				else
					r = narrow_u(sv, shift, dbits, (int)Q, &sat);
			}
			if (sat)
				set_qc(c);
			vec_set(c, dreg, dbits, i, r & mask_bits(dbits));
		}
		return N2A32_OK;
	}

	if (opc == 0xau) {                       /* VSHLL / VMOVL */
		unsigned src_esize, ew;

		if (imm7 < 8)
			return N2A32_UNDEF;
		if (imm7 < 16) {
			src_esize = 8;
			shift = imm7 - 8;
		} else if (imm7 < 32) {
			src_esize = 16;
			shift = imm7 - 16;
		} else if (imm7 < 64) {
			src_esize = 32;
			shift = imm7 - 32;
		} else {
			return N2A32_UNDEF;
		}
		if (Q || (dreg & 1u))            /* destination is Q, bit6 must be 0 */
			return N2A32_UNDEF;
		ew = 2 * src_esize;
		total = 64u / src_esize;
		for (i = 0; i < total; i++) {
			uint64_t sv = elem_get(&src[mreg], src_esize, i);
			uint64_t w = U ? sv : (uint64_t)sext_bits(sv, src_esize);

			vec_set(c, dreg, ew, i, (w << shift) & mask_bits(ew));
		}
		return N2A32_OK;
	}

	if (opc == 0xeu || opc == 0xfu) {        /* VCVT fixed <-> float */
		unsigned fbits;

		if (imm7 < 32 || imm7 > 63)
			return N2A32_UNDEF;
		fbits = 64 - imm7;
		if (Q && ((dreg | mreg) & 1u))
			return N2A32_UNDEF;
		nd = Q ? 2 : 1;
		total = nd * 2;
		for (i = 0; i < total; i++) {
			uint32_t sv = (uint32_t)vec_get(src, mreg, 32, i);
			int sat = 0;
			uint32_t r;

			if (opc == 0xfu)
				r = fp_to_fixed(sv, fbits, (int)U, &sat);
			else
				r = fixed_to_fp(sv, fbits, (int)U);
			if (sat)
				set_qc(c);
			vec_set(c, dreg, 32, i, r);
		}
		return N2A32_OK;
	}

	if (opc == 0xbu || opc == 0xcu || opc == 0xdu)
		return N2A32_UNDEF;      /* unallocated in the two-registers-and-shift space */
	if (opc == 4 || opc == 6) {
		if (!U)                          /* VSRI and VQSHLU exist only with U=1 */
			return N2A32_UNDEF;
	}
	if (imm7 < 8)
		return N2A32_UNDEF;
	if (imm7 < 16)
		esize = 8;
	else if (imm7 < 32)
		esize = 16;
	else if (imm7 < 64)
		esize = 32;
	else
		esize = 64;
	if (Q && ((dreg | mreg) & 1u))
		return N2A32_UNDEF;
	shift = (opc <= 4) ? (2 * esize - imm7) : (imm7 - esize);
	nd = Q ? 2 : 1;
	total = nd * (64u / esize);
	emask = mask_bits(esize);
	for (i = 0; i < total; i++) {
		uint64_t sv = vec_get(src, mreg, esize, i);
		uint64_t dv = vec_get(src, dreg, esize, i);
		int64_t ssv = sext_bits(sv, esize);
		uint64_t r;

		switch (opc) {
		case 0:                          /* VSHR */
			r = U ? rshift_u(sv, shift) : (uint64_t)asr64(ssv, shift);
			break;
		case 1:                          /* VSRA */
			r = dv + (U ? rshift_u(sv, shift) : (uint64_t)asr64(ssv, shift));
			break;
		case 2:                          /* VRSHR */
			r = U ? rshift_round_u(sv, shift) : (uint64_t)rshift_round_s(ssv, shift);
			break;
		case 3:                          /* VRSRA */
			r = dv + (U ? rshift_round_u(sv, shift) : (uint64_t)rshift_round_s(ssv, shift));
			break;
		case 4:                          /* VSRI */
			r = vsri(dv, sv, esize, shift);
			break;
		case 5:                          /* VSHL (immediate) / VSLI */
			r = U ? vsli(dv, sv, esize, shift) : (sv << shift);
			break;
		case 6: {                        /* VQSHLU */
			int sat = 0;

			r = qshlu_s(ssv, shift, esize, &sat);
			if (sat)
				set_qc(c);
			break;
		}
		default: {                       /* opc 7: VQSHL signed/unsigned */
			int sat = 0;

			if (U)
				r = qshl_u(sv, shift, esize, &sat);
			else
				r = (uint64_t)qshl_s(ssv, shift, esize, &sat);
			if (sat)
				set_qc(c);
			break;
		}
		}
		vec_set(c, dreg, esize, i, r & emask);
	}
	return N2A32_OK;
}

/* --------------------------------------------------------------------------------------------------------- */
/* three registers of different lengths                                                                       */

static int do_diff(struct n2a32_cpu *c, const union n2a32_dreg *src, uint32_t insn)
{
	unsigned U = (insn >> 24) & 1u, D = (insn >> 22) & 1u;
	unsigned size = (insn >> 20) & 3u;
	unsigned Vn = (insn >> 16) & 0xfu, Vd = (insn >> 12) & 0xfu;
	unsigned opc = (insn >> 8) & 0xfu, N = (insn >> 7) & 1u, M = (insn >> 5) & 1u;
	unsigned Vm = insn & 0xfu;
	unsigned esize, ew, total, i;
	unsigned dreg = (D << 4) | Vd, nreg = (N << 4) | Vn, mreg = (M << 4) | Vm;

	if (size == 3)
		return N2A32_UNDEF;

	/* long forms: Qd (even), Dn, Dm */
	if (opc == 0 || opc == 2 || opc == 5 || opc == 7 || opc == 8 ||
	    opc == 0xa || opc == 0xc || opc == 0xe) {
		if (opc == 0xe) {                /* VMULL.P8 */
			if (U || size != 0)
				return N2A32_UNDEF;
		}
		if (dreg & 1u)
			return N2A32_UNDEF;
		esize = 8u << size;
		ew = 2 * esize;
		total = 64u / esize;
		for (i = 0; i < total; i++) {
			uint64_t a = elem_get(&src[nreg], esize, i);
			uint64_t b = elem_get(&src[mreg], esize, i);
			uint64_t r;
			int sat = 0;

			switch (opc) {
			case 0:                  /* VADDL */
				r = U ? (a + b) : (uint64_t)(sext_bits(a, esize) + sext_bits(b, esize));
				break;
			case 2:                  /* VSUBL */
				r = U ? (a - b) : (uint64_t)(sext_bits(a, esize) - sext_bits(b, esize));
				break;
			case 5:                  /* VABAL */
				if (U) {
					uint64_t d = a >= b ? a - b : b - a;

					r = vec_get(src, dreg, ew, i) + d;
				} else {
					int64_t d = sext_bits(a, esize) - sext_bits(b, esize);

					if (d < 0)
						d = -d;
					r = vec_get(src, dreg, ew, i) + (uint64_t)d;
				}
				break;
			case 7:                  /* VABDL */
				if (U) {
					r = a >= b ? a - b : b - a;
				} else {
					int64_t d = sext_bits(a, esize) - sext_bits(b, esize);

					r = (uint64_t)(d < 0 ? -d : d);
				}
				break;
			case 8:                  /* VMLAL */
			case 0xa: {              /* VMLSL */
				uint64_t p;

				if (U)
					p = a * b;
				else
					p = (uint64_t)(sext_bits(a, esize) * sext_bits(b, esize));
				if (opc == 8)
					r = vec_get(src, dreg, ew, i) + p;
				else
					r = vec_get(src, dreg, ew, i) - p;
				break;
			}
			case 0xc:                /* VMULL */
				if (U)
					r = a * b;
				else
					r = (uint64_t)(sext_bits(a, esize) * sext_bits(b, esize));
				break;
			default: {               /* VMULL.P8 */
				uint64_t p = 0;
				unsigned k;

				for (k = 0; k < 8; k++) {
					if ((a >> k) & 1u)
						p ^= b << k;
				}
				r = p;
				break;
			}
			}
			if (sat)
				set_qc(c);
			vec_set(c, dreg, ew, i, r & mask_bits(ew));
		}
		return N2A32_OK;
	}

	/* VADDW/VSUBW: Qd, Qn, Dm */
	if (opc == 1 || opc == 3) {
		if ((dreg | nreg) & 1u)
			return N2A32_UNDEF;
		esize = 8u << size;
		ew = 2 * esize;
		total = 64u / esize;
		for (i = 0; i < total; i++) {
			uint64_t a = vec_get(src, nreg, ew, i);
			uint64_t b = elem_get(&src[mreg], esize, i);
			uint64_t r;

			if (opc == 1)
				r = U ? (a + b) : (a + (uint64_t)sext_bits(b, esize));
			else
				r = U ? (a - b) : (a - (uint64_t)sext_bits(b, esize));
			vec_set(c, dreg, ew, i, r & mask_bits(ew));
		}
		return N2A32_OK;
	}

	/* VADDHN/VRADDHN/VSUBHN/VRSUBHN: Dd, Qn, Qm */
	if (opc == 4 || opc == 6) {
		unsigned round = U;

		if ((nreg | mreg) & 1u)
			return N2A32_UNDEF;
		esize = 8u << size;             /* destination element size */
		ew = 2 * esize;                 /* source element size */
		total = 64u / esize;
		for (i = 0; i < total; i++) {
			uint64_t a = vec_get(src, nreg, ew, i);
			uint64_t b = vec_get(src, mreg, ew, i);
			uint64_t r;

			if (opc == 4)
				r = a + b;
			else
				r = a - b;
			if (round)
				r += UINT64_C(1) << (esize - 1);
			vec_set(c, dreg, esize, i, (r >> esize) & mask_bits(esize));
		}
		return N2A32_OK;
	}

	/* VQDMLAL/VQDMLSL/VQDMULL: signed only, 16/32-bit */
	if (opc == 9 || opc == 0xb || opc == 0xd) {
		if (U || size == 0 || (dreg & 1u))
			return N2A32_UNDEF;
		esize = 8u << size;
		ew = 2 * esize;
		total = 64u / esize;
		for (i = 0; i < total; i++) {
			int64_t a = sext_bits(elem_get(&src[nreg], esize, i), esize);
			int64_t b = sext_bits(elem_get(&src[mreg], esize, i), esize);
			int sat = 0;
			int64_t p = a * b;
			int64_t d = dbl_sat(p, ew, &sat);
			int64_t r;

			if (opc == 9)
				r = add_sat(sext_bits(vec_get(src, dreg, ew, i), ew), d, ew, &sat);
			else if (opc == 0xb)
				r = sub_sat(sext_bits(vec_get(src, dreg, ew, i), ew), d, ew, &sat);
			else
				r = d;
			if (sat)
				set_qc(c);
			vec_set(c, dreg, ew, i, (uint64_t)r & mask_bits(ew));
		}
		return N2A32_OK;
	}

	return N2A32_UNDEF;
}

/* --------------------------------------------------------------------------------------------------------- */
/* two registers and a scalar                                                                                 */

static int do_scalar(struct n2a32_cpu *c, const union n2a32_dreg *src, uint32_t insn)
{
	unsigned U = (insn >> 24) & 1u, D = (insn >> 22) & 1u;
	unsigned size = (insn >> 20) & 3u;
	unsigned Vn = (insn >> 16) & 0xfu, Vd = (insn >> 12) & 0xfu;
	unsigned opc = (insn >> 8) & 0xfu, N = (insn >> 7) & 1u, M = (insn >> 5) & 1u;
	unsigned Vm = insn & 0xfu;
	unsigned esize, dreg = (D << 4) | Vd, nreg = (N << 4) | Vn;
	unsigned sreg, lane, total, i, nd;
	uint64_t scalar;

	if (size != 1 && size != 2)
		return N2A32_UNDEF;
	esize = 8u << size;                      /* 16 or 32 */
	if (size == 1) {
		sreg = Vm & 7u;
		lane = (M << 1) | ((Vm >> 3) & 1u);
	} else {
		sreg = Vm;
		lane = M;
	}
	scalar = elem_get(&src[sreg], esize, lane);

	/* float VMLA/VMLS/VMUL by scalar: 32-bit only, U selects D (0) or Q (1) */
	if (opc == 1 || opc == 5 || opc == 9) {
		if (size != 2)
			return N2A32_UNDEF;
		if (U) {
			if ((dreg | nreg) & 1u)
				return N2A32_UNDEF;
			nd = 2;
		} else {
			nd = 1;
		}
		total = nd * 2;
		for (i = 0; i < total; i++) {
			uint32_t a = (uint32_t)vec_get(src, nreg, 32, i);
			uint32_t d = (uint32_t)vec_get(src, dreg, 32, i);
			uint32_t r;

			if (opc == 1)
				r = f32_scalar_op(a, (uint32_t)scalar, d, 1);
			else if (opc == 5)
				r = f32_scalar_op(a, (uint32_t)scalar, d, 2);
			else
				r = f32_scalar_op(a, (uint32_t)scalar, 0, 0);
			vec_set(c, dreg, 32, i, r);
		}
		return N2A32_OK;
	}

	/* integer VMLA/VMLS/VMUL by scalar: U selects D (0) or Q (1) */
	if (opc == 0 || opc == 4 || opc == 8) {
		uint64_t emask = mask_bits(esize);

		if (U) {
			if ((dreg | nreg) & 1u)
				return N2A32_UNDEF;
			nd = 2;
		} else {
			nd = 1;
		}
		total = nd * (64u / esize);
		for (i = 0; i < total; i++) {
			uint64_t a = vec_get(src, nreg, esize, i);
			uint64_t d = vec_get(src, dreg, esize, i);
			uint64_t p = a * scalar;
			uint64_t r;

			if (opc == 0)
				r = d + p;
			else if (opc == 4)
				r = d - p;
			else
				r = p;
			vec_set(c, dreg, esize, i, r & emask);
		}
		return N2A32_OK;
	}

	/* long forms: Qd, Dn, scalar; U = signed/unsigned */
	if (opc == 2 || opc == 6 || opc == 0xa) {
		unsigned ew = 2 * esize;

		if (dreg & 1u)
			return N2A32_UNDEF;
		total = 64u / esize;
		for (i = 0; i < total; i++) {
			uint64_t a = elem_get(&src[nreg], esize, i);
			uint64_t p;
			uint64_t r;

			if (U)
				p = a * scalar;
			else
				p = (uint64_t)(sext_bits(a, esize) * sext_bits(scalar, esize));
			if (opc == 2)
				r = vec_get(src, dreg, ew, i) + p;
			else if (opc == 6)
				r = vec_get(src, dreg, ew, i) - p;
			else
				r = p;
			vec_set(c, dreg, ew, i, r & mask_bits(ew));
		}
		return N2A32_OK;
	}

	/* VQDMLAL/VQDMLSL/VQDMULL by scalar: signed only */
	if (opc == 3 || opc == 7 || opc == 0xb) {
		unsigned ew = 2 * esize;

		if (U || (dreg & 1u))
			return N2A32_UNDEF;
		total = 64u / esize;
		for (i = 0; i < total; i++) {
			int64_t a = sext_bits(elem_get(&src[nreg], esize, i), esize);
			int64_t b = sext_bits(scalar, esize);
			int sat = 0;
			int64_t d = dbl_sat(a * b, ew, &sat);
			int64_t r;

			if (opc == 3)
				r = add_sat(sext_bits(vec_get(src, dreg, ew, i), ew), d, ew, &sat);
			else if (opc == 7)
				r = sub_sat(sext_bits(vec_get(src, dreg, ew, i), ew), d, ew, &sat);
			else
				r = d;
			if (sat)
				set_qc(c);
			vec_set(c, dreg, ew, i, (uint64_t)r & mask_bits(ew));
		}
		return N2A32_OK;
	}

	/* VQDMULH/VQRDMULH by scalar: signed, U selects D (0) or Q (1) */
	if (opc == 0xc || opc == 0xd) {
		if (U) {
			if ((dreg | nreg) & 1u)
				return N2A32_UNDEF;
			nd = 2;
		} else {
			nd = 1;
		}
		total = nd * (64u / esize);
		for (i = 0; i < total; i++) {
			int64_t a = sext_bits(vec_get(src, nreg, esize, i), esize);
			int64_t b = sext_bits(scalar, esize);
			int sat = 0;
			int64_t r = qdmulh(a * b, esize, opc == 0xd, &sat);

			if (sat)
				set_qc(c);
			vec_set(c, dreg, esize, i, (uint64_t)r & mask_bits(esize));
		}
		return N2A32_OK;
	}

	return N2A32_UNDEF;
}

/* --------------------------------------------------------------------------------------------------------- */
/* entry point                                                                                                */

int n2a32_neon_shift(struct n2a32_cpu *c, uint32_t insn)
{
	/* Vd may overlap Vn/Vm: all source reads use the entry snapshot (stack local). */
	union n2a32_dreg src[32];
	unsigned Q;

	/* A32 form of Advanced SIMD data processing: 1111001 U 1 ... */
	if ((insn & 0xfe000000u) != 0xf2000000u)
		return N2A32_UNDEF;
	if ((insn & 0x00800000u) == 0)             /* three registers of the same length: V18 */
		return N2A32_UNDEF;
	memcpy(src, c->d, sizeof src);

	if (insn & 0x10u) {                        /* two registers and a shift amount / V20 immediate */
		return do_shift(c, src, insn);
	}
	if (((insn >> 20) & 3u) == 3u)             /* two registers, miscellaneous: V20 */
		return N2A32_UNDEF;
	Q = (insn >> 6) & 1u;
	if (Q)
		return do_scalar(c, src, insn);
	return do_diff(c, src, insn);
}
