/* SPDX-License-Identifier: MIT */
/*
 * n2a32_neon_misc.c -- package V20: A32 Advanced SIMD instructions that are neither three-registers-same
 * (V18), nor two-registers-with-shift / three-different-lengths / two-registers-with-scalar (V19), nor
 * element/structure load/store (V20, n2a32_neon_ldst.c).
 *
 * Implemented here, from the ARM ARM (ARMv8-A, AArch32 Advanced SIMD data-processing):
 *   two registers, miscellaneous (bits 23-20 = 1D11 with U = 1, i.e. 0xF3B/0xF3F):
 *     VREV64/32/16, VPADDL, VPADAL, VCLS, VCLZ, VCNT, VMVN, VQABS, VQNEG,
 *     VCGT/VCGE/VCEQ/VCLE/VCLT #0 (integer and .F32), VABS, VNEG (integer and .F32),
 *     VSWP, VTRN, VUZP, VZIP, VMOVN, VQMOVN.S/VQMOVN.U, VQMOVUN, VSHLL (maximum shift),
 *     VCVT{.F32.S32,.F32.U32,.S32.F32,.U32.F32}, VRECPE, VRSQRTE (integer and .F32),
 *     VRINT{N,X,A,Z,M,P}.F32, VCVT{A,N,P,M}.{S32,U32}.F32 (ARMv8),
 *     VTBL/VTBX (1-4 table registers), VDUP (scalar lane).
 *   VEXT (bits 23-20 = 1D11 with U = 0, i.e. 0xF2B/0xF2F).
 *   one register, modified immediate (bits 23-19 = 10000, bit 4 = 1): VMOV, VMVN, VORR, VBIC.
 *
 * Deliberately N2A32_UNDEF (listed in test_v20/STATUS.md):
 *   - VCVT.F16.F32 / VCVT.F32.F16 (half precision; unused by the vendor libraries);
 *   - the encodings the ARM ARM leaves UNDEFINED: VREV32.32, VCNT/VMVN with a size other than .8,
 *     the .F32 forms with a size other than 32, VUZP.32/VZIP.32 with D registers (UNDEFINED in
 *     ARMv8), VTBL with a register list that runs past d31, bits 5-4 set in the VTBL encoding;
 *   - the one-register modified immediate with cmode = 1111, op = 1 (no such instruction);
 *   - everything outside the two encoding groups above.
 *
 * Floating point: conversions and roundings are done with explicit mantissa arithmetic, never with the
 * host's cast rounding.  Advanced SIMD uses StandardFPSCRValue(): DN = 1 (any NaN result is the
 * default NaN), FZ = 1 (denormal inputs are flushed to a zero of the same sign) and RMode = RNE,
 * regardless of the live FPSCR bits.  VABS/VNEG are pure sign operations (no NaN processing).  The
 * two conversions have instruction-fixed rounding, not FPSCR.RMode: VCVT.<int>.F32 rounds towards
 * zero, VCVT.F32.<int> rounds to nearest with ties to even.  Saturation of a saturating integer
 * instruction sets FPSCR.QC.  The reciprocal and inverse-square-root estimates return the correctly
 * rounded result (relative error <= 2^-24, inside the architecture's 2^-8 bound) rather than a
 * particular core's implementation-defined table value.
 */
#include "n2a32.h"

/* -------------------------------------------------------------------------------------------------- helpers */

#define QC_BIT    0x08000000u           /* FPSCR.QC, the only status bit NEON here sets */
#define F32_SIGN  0x80000000u
#define F32_EXP   0x7F800000u
#define F32_FRAC  0x007FFFFFu
#define F32_DEFNAN 0x7FC00000u

static uint64_t d_rd(const struct n2a32_cpu *c, unsigned n) { return c->d[n & 31u].u64; }
static void d_wr(struct n2a32_cpu *c, unsigned n, uint64_t v) { c->d[n & 31u].u64 = v; }

static uint32_t f32_bits(float f) { uint32_t u; memcpy(&u, &f, sizeof u); return u; }
static float f32_make(uint32_t u) { float f; memcpy(&f, &u, sizeof f); return f; }

static uint64_t width_mask(unsigned bytes)
{
	return bytes >= 8 ? ~(uint64_t)0 : (((uint64_t)1 << (bytes * 8)) - 1);
}

static uint64_t lane_get(uint64_t v, unsigned bytes, unsigned i)
{
	if (bytes >= 8)
		return v;
	return (v >> (i * bytes * 8)) & width_mask(bytes);
}

static void lane_set(uint64_t *v, unsigned bytes, unsigned i, uint64_t x)
{
	unsigned sh;
	uint64_t m;

	if (bytes >= 8) {
		*v = x;
		return;
	}
	sh = i * bytes * 8;
	m = width_mask(bytes) << sh;
	*v = (*v & ~m) | ((x & width_mask(bytes)) << sh);
}

static int64_t sext(uint64_t v, unsigned bytes)
{
	unsigned bits = bytes * 8;
	uint64_t m;

	if (bits >= 64)
		return (int64_t)v;
	m = (uint64_t)1 << (bits - 1);
	return (int64_t)((v ^ m) - m);
}

static int64_t sat_signed(int64_t v, unsigned bytes)
{
	unsigned bits = bytes * 8;
	int64_t max, min;

	if (bits >= 64)
		return v;
	max = (int64_t)(((uint64_t)1 << (bits - 1)) - 1);
	min = -(int64_t)((uint64_t)1 << (bits - 1));
	if (v > max)
		return max;
	if (v < min)
		return min;
	return v;
}

static uint64_t sat_unsigned(int64_t v, unsigned bytes)
{
	unsigned bits = bytes * 8;
	uint64_t max;

	if (v < 0)
		return 0;
	if (bits >= 64)
		return (uint64_t)v;
	max = ((uint64_t)1 << bits) - 1;
	return (uint64_t)v > max ? max : (uint64_t)v;
}

static unsigned clz32(uint32_t x)
{
	unsigned n = 0;

	if (x == 0)
		return 32;
	while (!(x & 0x80000000u)) { n++; x <<= 1; }
	return n;
}

/* count leading zeros of `bytes`-wide value, result 0..bytes*8 */
static unsigned clz_n(uint64_t v, unsigned bytes)
{
	uint64_t x = v & width_mask(bytes);

	if (bytes > 4) {
		uint32_t hi = (uint32_t)(x >> 32);
		if (hi)
			return clz32(hi);
		return 32u + clz32((uint32_t)x);
	}
	return clz32((uint32_t)x) - (32u - bytes * 8u);
}

static unsigned popcount8(uint8_t b)
{
	unsigned n = 0;

	while (b) { n += b & 1u; b >>= 1; }
	return n;
}

static void sat_qc(struct n2a32_cpu *c) { c->fpscr |= QC_BIT; }

/* ------------------------------------------------------------------------------------------ float helpers */

enum { RND_RN = 0, RND_RP = 1, RND_RM = 2, RND_RZ = 3, RND_RA = 4 };

static int f32_is_nan(uint32_t b) { return (b & F32_EXP) == F32_EXP && (b & F32_FRAC) != 0; }
static int f32_is_zero(uint32_t b) { return (b & 0x7FFFFFFFu) == 0; }
static int f32_is_denormal(uint32_t b) { return (b & F32_EXP) == 0 && (b & F32_FRAC) != 0; }

/* Advanced SIMD uses StandardFPSCRValue(): FPSCR.DN is not consulted, every NaN result that is
 * produced by an arithmetic operation is the default NaN (FABS/FNEG are pure sign operations). */
static uint32_t f32_nan(void) { return F32_DEFNAN; }

/* float -> signed 64-bit, explicit rounding; NaN -> 0; *sat when out of range */
static int64_t f2i(uint32_t b, int mode, int *sat)
{
	unsigned sign = b >> 31;
	unsigned exp = (b >> 23) & 0xFFu;
	uint32_t frac = b & F32_FRAC;
	uint64_t m, ip;
	int e;

	*sat = 0;
	if (exp == 0xFFu) {
		if (frac)
			return 0;
		*sat = 1;
		return sign ? INT64_MIN : INT64_MAX;
	}
	if (exp == 0) {
		if (frac == 0)
			return 0;
		m = frac;
		e = -149;
	} else {
		m = (uint64_t)frac | 0x800000u;
		e = (int)exp - 150;
	}
	if (e >= 0) {
		if (e >= 40) {
			*sat = 1;
			return sign ? INT64_MIN : INT64_MAX;
		}
		ip = m << (unsigned)e;
	} else {
		unsigned sh = (unsigned)(-e);
		if (sh >= 64) {
			ip = 0;
			if (mode == RND_RP && !sign)
				ip = 1;
			else if (mode == RND_RM && sign)
				ip = 1;
		} else {
			uint64_t rem = m & (((uint64_t)1 << sh) - 1);
			uint64_t half = (uint64_t)1 << (sh - 1);

			ip = m >> sh;
			switch (mode) {
			case RND_RN:
				if (rem > half || (rem == half && (ip & 1)))
					ip++;
				break;
			case RND_RA:
				if (rem >= half)
					ip++;
				break;
			case RND_RP:
				if (rem && !sign)
					ip++;
				break;
			case RND_RM:
				if (rem && sign)
					ip++;
				break;
			default:
				break;
			}
		}
	}
	if (sign) {
		if (ip > (uint64_t)INT64_MAX + 1u) {
			*sat = 1;
			return INT64_MIN;
		}
		return -(int64_t)ip;
	}
	if (ip > (uint64_t)INT64_MAX) {
		*sat = 1;
		return INT64_MAX;
	}
	return (int64_t)ip;
}

/* float -> 32-bit integer, saturating; *sat set when saturation happened */
static uint32_t f2i32(uint32_t b, int mode, int is_signed, int *sat)
{
	int s = 0;
	int64_t v = f2i(b, mode, &s);

	*sat = 0;
	if (s) {
		*sat = 1;
		if (is_signed)
			return (b >> 31) ? 0x80000000u : 0x7FFFFFFFu;
		return (b >> 31) ? 0u : 0xFFFFFFFFu;
	}
	if (is_signed) {
		if (v > INT32_MAX) { *sat = 1; return 0x7FFFFFFFu; }
		if (v < INT32_MIN) { *sat = 1; return 0x80000000u; }
		return (uint32_t)(int32_t)v;
	}
	if (v < 0) { *sat = 1; return 0u; }
	if ((uint64_t)v > 0xFFFFFFFFu) { *sat = 1; return 0xFFFFFFFFu; }
	return (uint32_t)v;
}

/* integer -> f32 bits, explicit rounding */
static uint32_t i2f32_bits(int64_t v, int mode)
{
	unsigned sign = 0, msb = 0, sh;
	uint64_t mag, keep, rem;

	if (v == 0)
		return 0;
	if (v < 0) {
		sign = 1;
		mag = (uint64_t)(-(v + 1)) + 1u;
	} else {
		mag = (uint64_t)v;
	}
	while ((mag >> (msb + 1)) != 0)
		msb++;
	if (msb <= 23) {
		keep = mag << (23 - msb);
	} else {
		sh = msb - 23;
		keep = mag >> sh;
		rem = mag & (((uint64_t)1 << sh) - 1);
		if (rem) {
			int rm = mode;

			if (sign) {
				if (rm == RND_RP)
					rm = RND_RM;
				else if (rm == RND_RM)
					rm = RND_RP;
			}
			if (rm == RND_RN) {
				uint64_t half = (uint64_t)1 << (sh - 1);

				if (rem > half || (rem == half && (keep & 1)))
					keep++;
			} else if (rm == RND_RP) {
				keep++;
			}
			if (keep == ((uint64_t)1 << 24)) {
				keep >>= 1;
				msb++;
			}
		}
	}
	return ((uint32_t)sign << 31) | (((msb + 127u) & 0xFFu) << 23) | ((uint32_t)keep & F32_FRAC);
}

/* round an f32 to an integral f32 value */
static uint32_t vrint_bits(uint32_t b, int mode)
{
	unsigned exp = (b >> 23) & 0xFFu;
	int sat = 0;
	int64_t v;

	if (f32_is_nan(b))
		return f32_nan();
	if (exp == 0xFFu)
		return b;
	if (f32_is_denormal(b))                 /* standard FPSCR FZ = 1 */
		return b & F32_SIGN;
	if (exp >= 0x7Fu + 23u)                 /* |x| >= 2^23 is already integral */
		return b;
	if (exp == 0)                           /* denormal: |x| < 1 */
		return b & F32_SIGN;
	v = f2i(b, mode, &sat);
	if (sat)
		return b;
	return i2f32_bits(v, RND_RN);
}

static uint32_t frecpe_bits(uint32_t b)
{
	unsigned sign = b >> 31;
	unsigned exp;

	if (f32_is_nan(b))
		return f32_nan();
	if (f32_is_denormal(b))                 /* standard FPSCR FZ = 1: +-0 in, +-inf out */
		b = sign << 31;
	exp = (b >> 23) & 0xFFu;
	if (exp == 0xFFu)                       /* +-inf -> +-0 */
		return sign << 31;
	if (f32_is_zero(b))                     /* +-0 -> +-inf */
		return (sign << 31) | F32_EXP;
	if (exp >= 253u)                        /* |x| >= 2^126: result underflows, FZ -> +-0 */
		return sign << 31;
	return f32_bits(1.0f / f32_make(b));
}

/* double square root without libm: bit-hack seed, Newton iterations */
static double dsqrt(double x)
{
	union { double d; uint64_t u; } v;
	int i;

	if (!(x > 0.0))
		return 0.0;
	v.d = x;
	v.u = (v.u >> 1) + 0x1FF8000000000000ull;
	for (i = 0; i < 7; i++)
		v.d = 0.5 * (v.d + x / v.d);
	return v.d;
}

static uint32_t frsqrt_bits(uint32_t b)
{
	unsigned sign = b >> 31;

	if (f32_is_nan(b))
		return f32_nan();
	if (f32_is_denormal(b))                 /* standard FPSCR FZ = 1: +-0 in */
		b = sign << 31;
	if (((b >> 23) & 0xFFu) == 0xFFu) {
		if (sign)                       /* -inf is an invalid operand */
			return F32_DEFNAN;
		return 0;                       /* +inf -> +0 */
	}
	if (f32_is_zero(b))                     /* +-0 -> +-inf */
		return (sign << 31) | F32_EXP;
	if (sign)
		return F32_DEFNAN;              /* negative normal: default NaN */
	return f32_bits((float)(1.0 / dsqrt((double)f32_make(b))));
}

static int f32_gt0(uint32_t b) { return !(b >> 31) && (b & 0x7FFFFFFFu) != 0 && !f32_is_nan(b); }
static int f32_ge0(uint32_t b) { return !f32_is_nan(b) && (!(b >> 31) || (b & 0x7FFFFFFFu) == 0); }
static int f32_eq0(uint32_t b) { return (b & 0x7FFFFFFFu) == 0; }
static int f32_le0(uint32_t b) { return (b >> 31) ? !f32_is_nan(b) : f32_eq0(b); }
static int f32_lt0(uint32_t b) { return (b >> 31) && (b & 0x7FFFFFFFu) != 0 && !f32_is_nan(b); }

/* ---------------------------------------------------------------------------------- two registers, misc */

/* VPADDL / VPADAL: B = 2 / 6 */
static int do_paddl(struct n2a32_cpu *c, unsigned dbase, unsigned mbase, unsigned nwords,
		    unsigned bytes, int is_unsigned, int acc)
{
	unsigned w, i, n = 8u / bytes, dbytes = bytes * 2u;

	for (w = 0; w < nwords; w++) {
		uint64_t src = d_rd(c, mbase + w);
		uint64_t out = acc ? d_rd(c, dbase + w) : 0;

		for (i = 0; i < n / 2u; i++) {
			int64_t sum;
			uint64_t old = lane_get(out, dbytes, i);

			if (is_unsigned)
				sum = (int64_t)lane_get(src, bytes, 2u * i) +
				      (int64_t)lane_get(src, bytes, 2u * i + 1u);
			else
				sum = sext(lane_get(src, bytes, 2u * i), bytes) +
				      sext(lane_get(src, bytes, 2u * i + 1u), bytes);
			if (acc) {
				if (is_unsigned)
					sum += (int64_t)old;
				else
					sum += sext(old, dbytes);
			}
			lane_set(&out, dbytes, i, (uint64_t)sum);
		}
		d_wr(c, dbase + w, out);
	}
	return N2A32_OK;
}

/* VABS / VNEG / VQABS / VQNEG (integer) */
static int vabs_neg(struct n2a32_cpu *c, unsigned dbase, unsigned mbase, unsigned nwords,
		    unsigned bytes, int neg, int saturating)
{
	unsigned w, i, n = 8u / bytes;

	for (w = 0; w < nwords; w++) {
		uint64_t src = d_rd(c, mbase + w), out = 0;

		for (i = 0; i < n; i++) {
			int64_t v = sext(lane_get(src, bytes, i), bytes);
			int64_t r;

			if (!neg) {
				if (v < 0) {
					if (saturating && v == -(int64_t)((uint64_t)1 << (bytes * 8u - 1u))) {
						r = (int64_t)(((uint64_t)1 << (bytes * 8u - 1u)) - 1u);
						sat_qc(c);
					} else {
						r = -v;
					}
				} else {
					r = v;
				}
			} else {
				if (saturating && v == -(int64_t)((uint64_t)1 << (bytes * 8u - 1u))) {
					r = (int64_t)(((uint64_t)1 << (bytes * 8u - 1u)) - 1u);
					sat_qc(c);
				} else {
					r = -v;
				}
			}
			lane_set(&out, bytes, i, (uint64_t)r);
		}
		d_wr(c, dbase + w, out);
	}
	return N2A32_OK;
}

/* VREV: reverse elements within `group` bytes */
static int vrev(struct n2a32_cpu *c, unsigned dbase, unsigned mbase, unsigned nwords,
		unsigned bytes, unsigned group)
{
	unsigned w, i, n = 8u / bytes, per = group / bytes;

	for (w = 0; w < nwords; w++) {
		uint64_t src = d_rd(c, mbase + w), out = 0;

		for (i = 0; i < n; i++) {
			unsigned g = i / per, k = i % per;

			lane_set(&out, bytes, i, lane_get(src, bytes, g * per + (per - 1u - k)));
		}
		d_wr(c, dbase + w, out);
	}
	return N2A32_OK;
}

static int vcls_vclz(struct n2a32_cpu *c, unsigned dbase, unsigned mbase, unsigned nwords,
		     unsigned bytes, int cls)
{
	unsigned w, i, n = 8u / bytes, bits = bytes * 8u;

	for (w = 0; w < nwords; w++) {
		uint64_t src = d_rd(c, mbase + w), out = 0;

		for (i = 0; i < n; i++) {
			uint64_t v = lane_get(src, bytes, i);
			unsigned r;

			if (cls) {
				uint64_t m = (v & ((uint64_t)1 << (bits - 1u))) ? ~v : v;

				r = clz_n(m, bytes) - 1u;
			} else {
				r = clz_n(v, bytes);
			}
			lane_set(&out, bytes, i, r);
		}
		d_wr(c, dbase + w, out);
	}
	return N2A32_OK;
}

static int vcnt(struct n2a32_cpu *c, unsigned dbase, unsigned mbase, unsigned nwords)
{
	unsigned w, i;

	for (w = 0; w < nwords; w++) {
		uint64_t src = d_rd(c, mbase + w), out = 0;

		for (i = 0; i < 8; i++)
			lane_set(&out, 1, i, popcount8((uint8_t)lane_get(src, 1, i)));
		d_wr(c, dbase + w, out);
	}
	return N2A32_OK;
}

/* compare each lane with zero: sel 0 >0, 1 >=0, 2 ==0, 3 <=0, 4 <0 */
static int vcmp0(struct n2a32_cpu *c, unsigned dbase, unsigned mbase, unsigned nwords,
		 unsigned bytes, unsigned sel, int is_float)
{
	unsigned w, i, n = 8u / bytes;

	for (w = 0; w < nwords; w++) {
		uint64_t src = d_rd(c, mbase + w), out = 0;

		for (i = 0; i < n; i++) {
			uint64_t v = lane_get(src, bytes, i);
			int r;

			if (is_float) {
				uint32_t f = (uint32_t)v;

				switch (sel) {
				case 0: r = f32_gt0(f); break;
				case 1: r = f32_ge0(f); break;
				case 2: r = f32_eq0(f); break;
				case 3: r = f32_le0(f); break;
				default: r = f32_lt0(f); break;
				}
			} else {
				int64_t s = sext(v, bytes);

				switch (sel) {
				case 0: r = s > 0; break;
				case 1: r = s >= 0; break;
				case 2: r = s == 0; break;
				case 3: r = s <= 0; break;
				default: r = s < 0; break;
				}
			}
			lane_set(&out, bytes, i, r ? width_mask(bytes) : 0);
		}
		d_wr(c, dbase + w, out);
	}
	return N2A32_OK;
}

/* VTRN / VUZP / VZIP; op 0 = TRN, 1 = UZP, 2 = ZIP */
static int vtrn_uzp_zip(struct n2a32_cpu *c, unsigned dfull, unsigned mfull, unsigned bytes, unsigned op, int q)
{
	uint64_t a[16], b[16], oa[16], ob[16];
	unsigned n = (q ? 16u : 8u) / bytes;    /* elements per register */
	unsigned base = q ? (dfull & ~1u) : dfull;
	unsigned mbase = q ? (mfull & ~1u) : mfull;
	unsigned w, i;
	uint64_t aw[2], bw[2], ow[2];

	aw[0] = d_rd(c, base);
	aw[1] = q ? d_rd(c, base + 1u) : 0;
	bw[0] = d_rd(c, mbase);
	bw[1] = q ? d_rd(c, mbase + 1u) : 0;
	for (i = 0; i < n; i++) {
		a[i] = lane_get(aw[i / (8u / bytes)], bytes, i % (8u / bytes));
		b[i] = lane_get(bw[i / (8u / bytes)], bytes, i % (8u / bytes));
	}
	switch (op) {
	case 0:                                 /* VTRN */
		for (i = 0; i < n; i++) {
			if ((i & 1u) == 0) {
				oa[i] = a[i];
				ob[i] = a[i + 1];
			} else {
				oa[i] = b[i - 1u];
				ob[i] = b[i];
			}
		}
		break;
	case 1:                                 /* VUZP: even elements, then odd elements */
		for (i = 0; i < n / 2u; i++) {
			oa[i] = a[2u * i];
			ob[i] = a[2u * i + 1u];
			oa[n / 2u + i] = b[2u * i];
			ob[n / 2u + i] = b[2u * i + 1u];
		}
		break;
	default:                                /* VZIP */
		for (i = 0; i < n; i += 2u) {
			oa[i] = a[i / 2u];
			oa[i + 1u] = b[i / 2u];
			ob[i] = a[n / 2u + i / 2u];
			ob[i + 1u] = b[n / 2u + i / 2u];
		}
		break;
	}
	for (w = 0; w < (q ? 2u : 1u); w++) {
		unsigned per = 8u / bytes;

		ow[0] = 0;
		for (i = 0; i < n; i++)
			if (i / per == w)
				lane_set(&ow[0], bytes, i % per, oa[i]);
		d_wr(c, base + w, ow[0]);
		ow[0] = 0;
		for (i = 0; i < n; i++)
			if (i / per == w)
				lane_set(&ow[0], bytes, i % per, ob[i]);
		d_wr(c, mbase + w, ow[0]);
	}
	return N2A32_OK;
}

/* VCVT float <-> integer; dir 0 = F32.S32, 1 = F32.U32, 2 = S32.F32, 3 = U32.F32 */
static int vcvt_fi(struct n2a32_cpu *c, unsigned dbase, unsigned mbase, unsigned nwords,
		   unsigned dir, int mode)
{
	unsigned w, i;

	for (w = 0; w < nwords; w++) {
		uint64_t src = d_rd(c, mbase + w), out = 0;

		for (i = 0; i < 2; i++) {
			uint32_t v = (uint32_t)lane_get(src, 4, i);
			uint32_t r;

			switch (dir) {
			case 0:
				r = i2f32_bits((int64_t)(int32_t)v, mode);
				break;
			case 1:
				r = i2f32_bits((int64_t)v, mode);
				break;
			case 2: {
				int sat = 0;

				r = f2i32(v, mode, 1, &sat);
				if (sat)
					sat_qc(c);
				break;
			}
			default: {
				int sat = 0;

				r = f2i32(v, mode, 0, &sat);
				if (sat)
					sat_qc(c);
				break;
			}
			}
			lane_set(&out, 4, i, r);
		}
		d_wr(c, dbase + w, out);
	}
	return N2A32_OK;
}

/* VRECPE / VRSQRTE, integer (U32) or float */
static int vrecpe(struct n2a32_cpu *c, unsigned dbase, unsigned mbase, unsigned nwords,
		  int is_float, int is_rsqrt)
{
	unsigned w, i;

	for (w = 0; w < nwords; w++) {
		uint64_t src = d_rd(c, mbase + w), out = 0;

		for (i = 0; i < 2; i++) {
			uint32_t v = (uint32_t)lane_get(src, 4, i);
			uint32_t r;

			if (is_float) {
				r = is_rsqrt ? frsqrt_bits(v) : frecpe_bits(v);
			} else if (!is_rsqrt) {
				/* VRECPE.U32: fixed-point estimate of 2^32/a */
				if (v == 0) {
					r = 0xFFFFFFFFu;
				} else {
					uint64_t q = 0x100000000ull / v;

					r = q > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)q;
				}
			} else if (v == 0) {
				r = 0xFFFFFFFFu;
			} else {
				/* VRSQRTE.U32: fixed-point estimate of 2^32/sqrt(a) */
				double q = 4294967296.0 / dsqrt((double)v);

				r = q >= 4294967295.0 ? 0xFFFFFFFFu : (uint32_t)q;
			}
			lane_set(&out, 4, i, r);
		}
		d_wr(c, dbase + w, out);
	}
	return N2A32_OK;
}

/* ------------------------------------------------------------------------------------------ the decoder */

static int two_reg_misc(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned D = (insn >> 22) & 1u;
	unsigned A = (insn >> 16) & 0xFu;
	unsigned Vd = (insn >> 12) & 0xFu;
	unsigned B = (insn >> 8) & 0xFu;
	unsigned C = (insn >> 7) & 1u;
	unsigned Q = (insn >> 6) & 1u;
	unsigned M = (insn >> 5) & 1u;
	unsigned Vm = insn & 0xFu;
	unsigned dfull = (D << 4) | Vd;
	unsigned mfull = (M << 4) | Vm;
	unsigned qd = dfull >> 1;
	unsigned qm = mfull >> 1;
	/* register bases for the plain two-register ops: two words for a Q form, one for a D form */
	unsigned dbase = Q ? (dfull & ~1u) : dfull;
	unsigned mbase = Q ? (mfull & ~1u) : mfull;
	unsigned nwords = Q ? 2u : 1u;
	unsigned bytes, size;
	unsigned w, i, n;

	if (insn & 0x10u)                       /* bit 4 is fixed 0 in this group */
		return N2A32_UNDEF;

	/* A Q form must name even D registers; odd numbers are UNPREDICTABLE and llvm rejects
	 * those words.  Bit 6 is not a Q bit in VMOVN/VQMOVN (B = 2, the narrowing op), VSHLL
	 * (B = 3, fixed 0) or VTBL/VTBX (B = 8..11, TBX/table bits); VDUP's source is a D register. */
	if (B >= 8u && B <= 11u) {
		/* VTBL/VTBX: D registers only */
	} else if (B == 12u) {
		if (Q && (dfull & 1u))
			return N2A32_UNDEF;
	} else if ((A & 3u) == 2u && B == 2u) {
		if (mfull & 1u)                 /* VMOVN/VQMOVN: Qm must be even */
			return N2A32_UNDEF;
	} else if ((A & 3u) == 2u && B == 3u) {
		if (dfull & 1u)                 /* VSHLL: Qd must be even */
			return N2A32_UNDEF;
	} else if (Q && ((dfull | mfull) & 1u)) {
		return N2A32_UNDEF;
	}

	/* VTBL / VTBX: bits 11-10 = 10, bits 9-8 = length-1, bit 7 = Vn<4>, bit 6 = TBX */
	if (B >= 8u && B <= 11u) {
		unsigned len = (B & 3u) + 1u;
		unsigned base = (C << 4) | A;
		uint64_t idx, out = 0;
		unsigned tbx = Q;

		if (base + len > 32u)
			return N2A32_UNDEF;
		idx = d_rd(c, mfull);
		for (i = 0; i < 8; i++) {
			unsigned k = (unsigned)(idx >> (8 * i)) & 0xFFu;
			uint64_t v;

			if (k < 8u * len)
				v = (d_rd(c, base + k / 8u) >> (8 * (k % 8u))) & 0xFFu;
			else if (tbx)
				v = (d_rd(c, dfull) >> (8 * i)) & 0xFFu;
			else
				v = 0;
			out |= v << (8 * i);
		}
		d_wr(c, dfull, out);
		return N2A32_OK;
	}

	/* VDUP (scalar lane): bits 11-8 = 1100, lane and size in bits 19-16; bit 7 is fixed 0,
	 * bit 6 = Q, bit 5 = M. */
	if (B == 12u) {
		unsigned idx;
		uint32_t val;
		uint64_t rep = 0;

		if (C)
			return N2A32_UNDEF;
		if (A & 1u) { size = 0; idx = A >> 1; }
		else if (A & 2u) { size = 1; idx = A >> 2; }
		else if (A & 4u) { size = 2; idx = A >> 3; }
		else
			return N2A32_UNDEF;
		bytes = 1u << size;
		val = (uint32_t)lane_get(d_rd(c, mfull), bytes, idx);
		n = 8u / bytes;
		for (i = 0; i < n; i++)
			lane_set(&rep, bytes, i, val);
		if (Q) {
			d_wr(c, 2u * qd, rep);
			d_wr(c, 2u * qd + 1u, rep);
		} else {
			d_wr(c, dfull, rep);
		}
		return N2A32_OK;
	}

	switch (A & 3u) {
	case 0:                                 /* A = 0, 4, 8: size = A>>2 */
		size = A >> 2;
		if (size == 3u)                 /* A = 0xC: no .64 form in this group */
			return N2A32_UNDEF;
		bytes = 1u << size;
		switch (B) {
		case 0:
			if (C)
				return size < 2u ? vrev(c, dbase, mbase, nwords, bytes, 4u) : (int)N2A32_UNDEF;
			return vrev(c, dbase, mbase, nwords, bytes, 8u);
		case 1:
			if (C || size != 0)
				return N2A32_UNDEF;
			return vrev(c, dbase, mbase, nwords, 1, 2u);
		case 2:
			return do_paddl(c, dbase, mbase, nwords, bytes, (int)C, 0);
		case 4:
			return vcls_vclz(c, dbase, mbase, nwords, bytes, C ? 0 : 1);
		case 5:
			if (size != 0)
				return N2A32_UNDEF;
			if (C) {
				for (w = 0; w < nwords; w++)
					d_wr(c, dbase + w, ~d_rd(c, mbase + w));
				return N2A32_OK;
			}
			return vcnt(c, dbase, mbase, nwords);
		case 6:
			return do_paddl(c, dbase, mbase, nwords, bytes, (int)C, 1);
		case 7:
			return vabs_neg(c, dbase, mbase, nwords, bytes, (int)C, 1);
		default:
			return N2A32_UNDEF;
		}
	case 1:                                 /* A = 1, 5, 9: VCMP #0 / VABS / VNEG */
		size = A >> 2;
		bytes = 1u << size;
		if (size == 3u)
			return N2A32_UNDEF;
		switch (B) {
		case 0:                         /* VCGT #0 / VCGE #0, integer for every size */
			return vcmp0(c, dbase, mbase, nwords, bytes, C ? 1u : 0u, 0);
		case 1:                         /* VCEQ #0 / VCLE #0 */
			return vcmp0(c, dbase, mbase, nwords, bytes, C ? 3u : 2u, 0);
		case 2:                         /* VCLT #0 (C = 1 at A = 9 is SHA1H, not implemented) */
			if (C)
				return N2A32_UNDEF;
			return vcmp0(c, dbase, mbase, nwords, bytes, 4, 0);
		case 3:
			if (size == 2u) {
				/* VABS.S32 / VNEG.S32: integer, wrapping */
				for (w = 0; w < nwords; w++) {
					uint64_t src = d_rd(c, mbase + w), out = 0;

					for (i = 0; i < 2; i++) {
						uint32_t v = (uint32_t)lane_get(src, 4, i);

						lane_set(&out, 4, i, C ? (uint32_t)(-(uint32_t)v)
								       : (v & 0x80000000u ? (uint32_t)(-(uint32_t)v) : v));
					}
					d_wr(c, dbase + w, out);
				}
				return N2A32_OK;
			}
			return vabs_neg(c, dbase, mbase, nwords, bytes, (int)C, 0);
		default:                        /* B = 4..7: .F32 only */
			if (size != 2u)
				return N2A32_UNDEF;
			switch (B) {
			case 4:
				return vcmp0(c, dbase, mbase, nwords, 4, C ? 1u : 0u, 1);
			case 5:
				return vcmp0(c, dbase, mbase, nwords, 4, C ? 3u : 2u, 1);
			case 6:
				return C ? (int)N2A32_UNDEF : vcmp0(c, dbase, mbase, nwords, 4, 4, 1);
			case 7: {                       /* C = 0 VABS.F32, C = 1 VNEG.F32 */
				for (w = 0; w < nwords; w++) {
					uint64_t src = d_rd(c, mbase + w), out = 0;

					for (i = 0; i < 2; i++) {
						uint32_t v = (uint32_t)lane_get(src, 4, i);

						/* FPAbs/FPNeg are pure sign operations: NaNs are
						 * copied unchanged apart from the sign bit, and
						 * FPSCR.DN does not replace them. */
						if (C)
							v ^= F32_SIGN;
						else
							v &= ~F32_SIGN;
						lane_set(&out, 4, i, v);
					}
					d_wr(c, dbase + w, out);
				}
				return N2A32_OK;
			}
			default:                /* B = 8..15: unallocated */
				return N2A32_UNDEF;
			}
		}
	case 2:                                 /* A = 2, 6, A */
		size = A >> 2;
		if (size == 3u)                 /* A = 0xE: no instruction in this group */
			return N2A32_UNDEF;
		bytes = 1u << size;
		switch (B) {
		case 0:
			if (!C) {               /* VSWP has no size field: only A = 0x2 is valid */
				uint64_t t;

				if (size != 0u)
					return N2A32_UNDEF;
				t = d_rd(c, dfull);
				d_wr(c, dfull, d_rd(c, mfull));
				d_wr(c, mfull, t);
				if (Q) {
					t = d_rd(c, (dfull & ~1u) + 1u);
					d_wr(c, (dfull & ~1u) + 1u, d_rd(c, (mfull & ~1u) + 1u));
					d_wr(c, (mfull & ~1u) + 1u, t);
				}
				return N2A32_OK;
			}
			return vtrn_uzp_zip(c, dfull, mfull, bytes, 0, (int)Q);
		case 1:
			if (size == 2u && !Q)
				return N2A32_UNDEF;     /* VUZP.32/VZIP.32 D: UNDEFINED in ARMv8 */
			return vtrn_uzp_zip(c, dfull, mfull, bytes, C ? 2u : 1u, (int)Q);
		case 2: {                       /* VMOVN / VQMOVUN / VQMOVN.S / VQMOVN.U */
			unsigned mode = (insn >> 6) & 3u;
			/* here A = 2|size<<2 selects a *source* element of 16, 32 or 64 bits */
			unsigned sbytes = bytes * 2u;
			unsigned sper = 8u / sbytes;
			uint64_t out = 0;

			for (i = 0; i < 16u / sbytes; i++) {
				uint64_t sv = lane_get(d_rd(c, 2u * qm + i / sper), sbytes, i % sper);
				uint64_t r;

				if (mode == 0) {                /* VMOVN */
					r = sv & width_mask(bytes);
				} else if (mode == 1) {         /* VQMOVUN: signed -> unsigned */
					int64_t s2 = sext(sv, sbytes);

					r = sat_unsigned(s2, bytes);
					if ((int64_t)r != s2)
						sat_qc(c);
				} else if (mode == 2) {         /* VQMOVN.S */
					int64_t s2 = sext(sv, sbytes);
					int64_t t = sat_signed(s2, bytes);

					if (t != s2)
						sat_qc(c);
					r = (uint64_t)t & width_mask(bytes);
				} else {                        /* VQMOVN.U */
					uint64_t u = sat_unsigned((int64_t)sv, bytes);

					if (u != (sv & width_mask(sbytes)))
						sat_qc(c);
					r = u;
				}
				lane_set(&out, bytes, i, r);
			}
			d_wr(c, dfull, out);
			return N2A32_OK;
		}
		case 3: {                       /* VSHLL, maximum shift (C = 1 is the SHA1SU1/SHA256SU0 space) */
			uint64_t out[2] = { 0, 0 };

			if (Q || C)
				return N2A32_UNDEF;
			for (i = 0; i < 8u / bytes; i++) {
				uint64_t v = lane_get(d_rd(c, mfull), bytes, i) << (bytes * 8u);
				unsigned per = 4u / bytes;

				lane_set(&out[i / per], bytes * 2u, i % per, v);
			}
			d_wr(c, dfull & ~1u, out[0]);
			d_wr(c, (dfull & ~1u) + 1u, out[1]);
			return N2A32_OK;
		}
		default:
			if (A == 0xAu && B >= 4u && B <= 7u) {  /* VRINT */
				int mode;

				if (!C && B == 4) mode = RND_RN;        /* VRINTN */
				else if (!C && B == 5) mode = RND_RA;   /* VRINTA */
				else if (C && B == 4) mode = RND_RN;    /* VRINTX */
				else if (C && B == 5) mode = RND_RZ;    /* VRINTZ */
				else if (C && B == 6) mode = RND_RM;    /* VRINTM */
				else if (C && B == 7) mode = RND_RP;    /* VRINTP */
				else
					return N2A32_UNDEF;
				for (w = 0; w < nwords; w++) {
					uint64_t src = d_rd(c, mbase + w), out = 0;

					for (i = 0; i < 2; i++)
						lane_set(&out, 4, i,
							 vrint_bits((uint32_t)lane_get(src, 4, i), mode));
					d_wr(c, dbase + w, out);
				}
				return N2A32_OK;
			}
			return N2A32_UNDEF;             /* includes VCVT.F16.F32 / VCVT.F32.F16 */
		}
	default:                                /* A & 3 == 3: only A = 0xB is defined */
		if (A != 0xBu)
			return N2A32_UNDEF;
		switch (B) {
		case 0: case 1: case 2: case 3: {       /* VCVT{A,N,P,M}.{S32,U32}.F32 */
			static const int modes[4] = { RND_RA, RND_RN, RND_RP, RND_RM };

			for (w = 0; w < nwords; w++) {
				uint64_t src = d_rd(c, mbase + w), out = 0;

				for (i = 0; i < 2; i++) {
					int sat = 0;
					uint32_t r = f2i32((uint32_t)lane_get(src, 4, i),
							   modes[B], C ? 0 : 1, &sat);

					if (sat)
						sat_qc(c);
					lane_set(&out, 4, i, r);
				}
				d_wr(c, dbase + w, out);
			}
			return N2A32_OK;
		}
		case 4:
			return vrecpe(c, dbase, mbase, nwords, 0, (int)C);
		case 5:
			return vrecpe(c, dbase, mbase, nwords, 1, (int)C);
		case 6:                         /* VCVT.F32.S32/U32: round to nearest, ties to even */
			return vcvt_fi(c, dbase, mbase, nwords, C ? 1u : 0u, RND_RN);
		case 7:                         /* VCVT.S32/U32.F32: round towards zero */
			return vcvt_fi(c, dbase, mbase, nwords, C ? 3u : 2u, RND_RZ);
		default:
			return N2A32_UNDEF;
		}
	}
}

/* --------------------------------------------------------------------------------------------- VEXT */

/* VEXT Dd, Dn, Dm, #imm: the source is Dn:Dm (Dn in the low bytes), so result byte k is
 * (Dn:Dm) byte (imm + k): Dn bytes imm..7 then Dm bytes 0..imm-1. */
static int vext(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned D = (insn >> 22) & 1u;
	unsigned Vn = (insn >> 16) & 0xFu;
	unsigned Vd = (insn >> 12) & 0xFu;
	unsigned imm = (insn >> 8) & 0xFu;
	unsigned N = (insn >> 7) & 1u;
	unsigned Q = (insn >> 6) & 1u;
	unsigned M = (insn >> 5) & 1u;
	unsigned Vm = insn & 0xFu;
	unsigned dfull = (D << 4) | Vd;
	unsigned mfull = (M << 4) | Vm;
	unsigned nfull = (N << 4) | Vn;
	unsigned i;

	if (insn & 0x10u)
		return N2A32_UNDEF;
	if (Q && ((dfull | nfull | mfull) & 1u))
		return N2A32_UNDEF;             /* Q form: all three registers must be even */
	if (Q) {
		uint8_t src[32], out[16];
		uint64_t r0, r1;

		if (imm > 15u)
			return N2A32_UNDEF;
		memcpy(src, &c->d[nfull & ~1u].u64, 8);
		memcpy(src + 8, &c->d[(nfull & ~1u) + 1u].u64, 8);
		memcpy(src + 16, &c->d[mfull & ~1u].u64, 8);
		memcpy(src + 24, &c->d[(mfull & ~1u) + 1u].u64, 8);
		for (i = 0; i < 16; i++)
			out[i] = src[i + imm];
		memcpy(&r0, out, 8);
		memcpy(&r1, out + 8, 8);
		d_wr(c, dfull & ~1u, r0);
		d_wr(c, (dfull & ~1u) + 1u, r1);
	} else {
		uint64_t lo = d_rd(c, nfull), hi = d_rd(c, mfull), out = 0;

		if (imm > 7u)
			return N2A32_UNDEF;
		for (i = 0; i < 8; i++) {
			unsigned k = i + imm;
			uint64_t byte = k < 8u ? (lo >> (8 * k)) : (hi >> (8 * (k - 8u)));

			out |= (byte & 0xFFu) << (8 * i);
		}
		d_wr(c, dfull, out);
	}
	return N2A32_OK;
}

/* ------------------------------------------------------------------------------ one register, immediate */

/* AdvSIMDExpandImm: op = bit 5, cmode = bits 11-8, imm8 = i:imm3:imm4 */
static int expand_imm(unsigned op, unsigned cmode, uint32_t imm8, uint64_t *out)
{
	uint32_t imm32;

	switch (cmode >> 1) {
	case 0: imm32 = imm8; break;
	case 1: imm32 = imm8 << 8; break;
	case 2: imm32 = imm8 << 16; break;
	case 3: imm32 = imm8 << 24; break;
	case 4: imm32 = imm8 | (imm8 << 16); break;
	case 5: imm32 = (imm8 << 8) | (imm8 << 24); break;
	case 6:
		imm32 = ((cmode & 1u) == 0) ? ((imm8 << 8) | 0xFFu) : ((imm8 << 16) | 0xFFFFu);
		break;
	default:
		if (cmode == 0xEu) {
			uint64_t v = 0;
			unsigned i;

			for (i = 0; i < 8; i++) {
				if (op == 0)
					v |= (uint64_t)imm8 << (8 * i);
				else if (imm8 & (1u << i))
					v |= (uint64_t)0xFFu << (8 * i);
			}
			*out = v;
			return 0;
		}
		/* cmode == 15: VMOV.F32 for op = 0; op = 1 has no encoding */
		if (op)
			return -1;
		{
			uint32_t v = ((uint32_t)(imm8 >> 7) << 31) |
				     ((uint32_t)(((imm8 >> 6) & 1u) ^ 1u) << 30) |
				     ((uint32_t)((imm8 >> 6) & 1u) * 0x1Fu << 25) |
				     ((uint32_t)(imm8 & 0x3Fu) << 19);

			*out = (uint64_t)v | ((uint64_t)v << 32);
			return 0;
		}
	}
	*out = (uint64_t)imm32 | ((uint64_t)imm32 << 32);
	return 0;
}

static int modified_imm(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned i = (insn >> 24) & 1u;
	unsigned imm3 = (insn >> 16) & 7u;
	unsigned D = (insn >> 22) & 1u;
	unsigned Vd = (insn >> 12) & 0xFu;
	unsigned cmode = (insn >> 8) & 0xFu;
	unsigned op = (insn >> 5) & 1u;
	unsigned Q = (insn >> 6) & 1u;
	uint32_t imm8 = (i << 7) | (imm3 << 4) | (insn & 0xFu);
	unsigned dfull = (D << 4) | Vd;
	unsigned nwords = Q ? 2u : 1u;
	unsigned base = Q ? (dfull & ~1u) : dfull;
	uint64_t imm64, v;
	unsigned w;
	int ctl = (int)(cmode >> 1);

	if (expand_imm(op, cmode, imm8, &imm64) != 0)
		return N2A32_UNDEF;

	if (ctl <= 6 && (cmode & 1u) && ctl != 6) {
		/* VORR (op = 0) / VBIC (op = 1) */
		for (w = 0; w < nwords; w++) {
			uint64_t keep = d_rd(c, base + w);

			d_wr(c, base + w, op ? (keep & ~imm64) : (keep | imm64));
		}
		return N2A32_OK;
	}
	/* cmode 14/15 are VMOV.I8 / VMOV.I64 / VMOV.F32: the op bit selects the shape, it is not a NOT */
	v = (ctl == 7) ? imm64 : (op ? ~imm64 : imm64);
	for (w = 0; w < nwords; w++)
		d_wr(c, base + w, v);
	return N2A32_OK;
}

/* ------------------------------------------------------------------------------------------------ entry */

int n2a32_neon_misc(struct n2a32_cpu *c, uint32_t insn)
{
	/* one register, modified immediate: bits 23-19 = 10000, bit 7 = 0, bit 4 = 1
	 * (bit 7 = 1 is the VSHL/VSHR immediate group, V19, not ours) */
	if ((insn & 0xFEB80090u) == 0xF2800010u)
		return modified_imm(c, insn);

	/* two registers, miscellaneous (U = 1) and VEXT (U = 0): bits 23-20 = 1D11 */
	if ((insn & 0xFFB00000u) == 0xF3B00000u)
		return two_reg_misc(c, insn);
	if ((insn & 0xFFB00000u) == 0xF2B00000u)
		return vext(c, insn);
	return N2A32_UNDEF;
}
