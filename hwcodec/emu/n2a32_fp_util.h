/* SPDX-License-Identifier: MIT */
/*
 * n2a32_fp_util.h -- shared scalar floating-point helpers for the AArch32 interpreter.
 *
 * Owner: V17 (n2a32_vfp.c).  V18/V19/V20 may include this file read-only; do not edit it outside V17.
 *
 * The helpers are deliberately header-only and independent of n2a32_vfp.c so that the NEON classes
 * can use the same rounding and saturating-conversion rules.  Everything here is pure C11 and does
 * not depend on the host FPU's conversion instructions: the float->integer path is written with
 * integer arithmetic on the IEEE-754 bit pattern, so x86 and arm64 produce the same result (x86's
 * cvttsd2si returns 0x80000000 on overflow, which is *not* the ARM result).
 *
 * FPSCR layout used everywhere (ARMv8-A AArch32): N 31, Z 30, C 29, V 28, QC 27, DN 25, FZ 24,
 * RMode 23-22 (0 RN, 1 RP, 2 RM, 3 RZ), IDC 7, IXC 4, UFC 3, OFC 2, DZC 1, IOC 0.
 */
#ifndef N2A32_FP_UTIL_H
#define N2A32_FP_UTIL_H

#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "n2a32.h"

#define N2AFP_IOC 0x00000001u
#define N2AFP_DZC 0x00000002u
#define N2AFP_OFC 0x00000004u
#define N2AFP_UFC 0x00000008u
#define N2AFP_IXC 0x00000010u
#define N2AFP_IDC 0x00000080u
#define N2AFP_QC  0x08000000u
#define N2AFP_DN  0x02000000u
#define N2AFP_FZ  0x01000000u
#define N2AFP_RMODE_MASK 0x00C00000u

#define N2AFP_F32_DEFAULT_NAN 0x7FC00000u
#define N2AFP_F64_DEFAULT_NAN UINT64_C(0x7FF8000000000000)
#define N2AFP_F16_DEFAULT_NAN 0x7E00u

/* Rounding selectors for the saturating converters.  The first four match FPSCR.RMode. */
enum {
	N2AFP_RN = 0,	/* nearest, ties to even */
	N2AFP_RP = 1,	/* toward +infinity */
	N2AFP_RM = 2,	/* toward -infinity */
	N2AFP_RZ = 3,	/* toward zero */
	N2AFP_RA = 4	/* nearest, ties away from zero (ARMv8 VCVTA/VRINTA) */
};

/* ------------------------------------------------------------------ bit access */

static inline uint32_t n2afp_f32_bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline float n2afp_f32_from_bits(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static inline uint64_t n2afp_f64_bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static inline double n2afp_f64_from_bits(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }

static inline int n2afp_f32_is_nan(float f) { uint32_t b = n2afp_f32_bits(f); return (b & 0x7F800000u) == 0x7F800000u && (b & 0x007FFFFFu) != 0; }
static inline int n2afp_f32_is_snan(float f) { uint32_t b = n2afp_f32_bits(f); return (b & 0x7F800000u) == 0x7F800000u && (b & 0x007FFFFFu) != 0 && (b & 0x00400000u) == 0; }
static inline int n2afp_f32_is_inf(float f) { uint32_t b = n2afp_f32_bits(f); return (b & 0x7FFFFFFFu) == 0x7F800000u; }
static inline int n2afp_f32_is_subnormal(float f) { uint32_t b = n2afp_f32_bits(f); return (b & 0x7F800000u) == 0 && (b & 0x007FFFFFu) != 0; }

static inline int n2afp_f64_is_nan(double d) { uint64_t b = n2afp_f64_bits(d); return (b & UINT64_C(0x7FF0000000000000)) == UINT64_C(0x7FF0000000000000) && (b & UINT64_C(0x000FFFFFFFFFFFFF)) != 0; }
static inline int n2afp_f64_is_snan(double d) { uint64_t b = n2afp_f64_bits(d); return (b & UINT64_C(0x7FF0000000000000)) == UINT64_C(0x7FF0000000000000) && (b & UINT64_C(0x000FFFFFFFFFFFFF)) != 0 && (b & UINT64_C(0x0008000000000000)) == 0; }
static inline int n2afp_f64_is_inf(double d) { uint64_t b = n2afp_f64_bits(d); return (b & UINT64_C(0x7FFFFFFFFFFFFFFF)) == UINT64_C(0x7FF0000000000000); }
static inline int n2afp_f64_is_subnormal(double d) { uint64_t b = n2afp_f64_bits(d); return (b & UINT64_C(0x7FF0000000000000)) == 0 && (b & UINT64_C(0x000FFFFFFFFFFFFF)) != 0; }

static inline float n2afp_f32_quiet(float f) { return n2afp_f32_from_bits(n2afp_f32_bits(f) | 0x00400000u); }
static inline double n2afp_f64_quiet(double d) { return n2afp_f64_from_bits(n2afp_f64_bits(d) | UINT64_C(0x0008000000000000)); }

/* ------------------------------------------------------------------ rounding mode */

/* FPSCR.RMode -> enum above (identical numbering). */
static inline int n2afp_fpscr_rmode(uint32_t fpscr) { return (int)((fpscr & N2AFP_RMODE_MASK) >> 22); }

/* enum above -> C fenv mode */
static inline int n2afp_fe_mode(int rmode)
{
	switch (rmode) {
	case N2AFP_RP: return FE_UPWARD;
	case N2AFP_RM: return FE_DOWNWARD;
	case N2AFP_RZ: return FE_TOWARDZERO;
	default: return FE_TONEAREST;
	}
}

/* Set the host rounding mode; returns the previous one (for restoration). */
static inline int n2afp_set_host_rounding(int rmode) { int old = fegetround(); (void)fesetround(n2afp_fe_mode(rmode)); return old; }
static inline void n2afp_restore_host_rounding(int old) { (void)fesetround(old); }

/* ------------------------------------------------------------------ exact float -> integer magnitude */

/*
 * Round |v| to an integer according to `rmode`, writing the magnitude to *out.
 * Returns 0 = ok, 1 = magnitude overflows uint64, -1 = v is NaN or infinity (caller handles).
 * *inexact is set when the integer differs from the exact value.
 *
 * Written from the IEEE-754 bit pattern; no host conversion instruction is used.
 */
static inline int n2afp_f64_round_mag(double v, int rmode, uint64_t *out, int *inexact)
{
	uint64_t bits = n2afp_f64_bits(v);
	unsigned sign = (unsigned)(bits >> 63);
	unsigned exp = (unsigned)((bits >> 52) & 0x7FFu);
	uint64_t frac = bits & UINT64_C(0x000FFFFFFFFFFFFF);
	uint64_t m, q, rem;
	int e, shift;

	*inexact = 0;
	if (exp == 0x7FFu) { *out = UINT64_MAX; return -1; }
	if (exp == 0 && frac == 0) { *out = 0; return 0; }
	if (exp == 0) {
		m = frac;
		e = -1074;
	} else {
		m = (UINT64_C(1) << 52) | frac;
		e = (int)exp - 1023 - 52;
	}

	if (e >= 0) {
		if (e >= 64 || m > (UINT64_MAX >> (unsigned)e)) { *out = UINT64_MAX; return 1; }
		*out = m << (unsigned)e;
		return 0;
	}

	shift = -e;
	if (shift >= 64) {
		q = 0;
		rem = m;	/* m != 0 here; value < 2^53 / 2^64 < 1/2 */
	} else {
		q = m >> (unsigned)shift;
		rem = m & ((UINT64_C(1) << (unsigned)shift) - 1u);
	}

	if (rem != 0) {
		*inexact = 1;
		switch (rmode) {
		case N2AFP_RN: {
			if (shift < 64) {
				uint64_t half = UINT64_C(1) << (unsigned)(shift - 1);
				if (rem > half || (rem == half && (q & 1u) != 0))
					q++;
			}
			break;
		}
		case N2AFP_RA: {
			if (shift < 64) {
				uint64_t half = UINT64_C(1) << (unsigned)(shift - 1);
				if (rem >= half)
					q++;
			}
			break;
		}
		case N2AFP_RP:
			if (!sign)
				q++;
			break;
		case N2AFP_RM:
			if (sign)
				q++;
			break;
		case N2AFP_RZ:
		default:
			break;
		}
	}
	*out = q;
	return 0;
}

/* Saturating float -> signed/unsigned 32-bit and 64-bit conversions, ARM behaviour.
 * On NaN/inf/out-of-range *invalid is set and the ARM saturation value is returned
 * (0 for NaN, INT_MIN/INT_MAX, 0/UINT_MAX, etc.).  Callers that must distinguish NaN
 * check it themselves first. */
static inline int32_t n2afp_f64_to_i32(double v, int rmode, int *inexact, int *invalid)
{
	uint64_t mag;
	unsigned sign = (unsigned)(n2afp_f64_bits(v) >> 63);
	int r;

	*invalid = 0;
	r = n2afp_f64_round_mag(v, rmode, &mag, inexact);
	if (r < 0) { *invalid = 1; return 0; }
	if (r > 0) { *invalid = 1; return sign ? INT32_MIN : INT32_MAX; }
	if (sign) {
		if (mag == 0) return 0;
		if (mag <= UINT64_C(2147483648)) return (int32_t)(0u - (uint32_t)mag);
		*invalid = 1;
		return INT32_MIN;
	}
	if (mag > UINT64_C(2147483647)) { *invalid = 1; return INT32_MAX; }
	return (int32_t)mag;
}

static inline uint32_t n2afp_f64_to_u32(double v, int rmode, int *inexact, int *invalid)
{
	uint64_t mag;
	unsigned sign = (unsigned)(n2afp_f64_bits(v) >> 63);
	int r;

	*invalid = 0;
	r = n2afp_f64_round_mag(v, rmode, &mag, inexact);
	if (r < 0) { *invalid = 1; return 0; }
	if (r > 0) { *invalid = 1; return UINT32_MAX; }
	if (sign) {
		if (mag == 0) return 0;
		*invalid = 1;
		return 0;
	}
	if (mag > UINT64_C(0xFFFFFFFF)) { *invalid = 1; return UINT32_MAX; }
	return (uint32_t)mag;
}

static inline int64_t n2afp_f64_to_i64(double v, int rmode, int *inexact, int *invalid)
{
	uint64_t mag;
	unsigned sign = (unsigned)(n2afp_f64_bits(v) >> 63);
	int r;

	*invalid = 0;
	r = n2afp_f64_round_mag(v, rmode, &mag, inexact);
	if (r < 0) { *invalid = 1; return 0; }
	if (r > 0) { *invalid = 1; return sign ? INT64_MIN : INT64_MAX; }
	if (sign) {
		if (mag == 0) return 0;
		if (mag <= UINT64_C(0x7FFFFFFFFFFFFFFF)) return -(int64_t)mag;
		if (mag == UINT64_C(0x8000000000000000)) return INT64_MIN;
		*invalid = 1;
		return INT64_MIN;
	}
	if (mag > UINT64_C(0x7FFFFFFFFFFFFFFF)) { *invalid = 1; return INT64_MAX; }
	return (int64_t)mag;
}

static inline uint64_t n2afp_f64_to_u64(double v, int rmode, int *inexact, int *invalid)
{
	uint64_t mag;
	unsigned sign = (unsigned)(n2afp_f64_bits(v) >> 63);
	int r;

	*invalid = 0;
	r = n2afp_f64_round_mag(v, rmode, &mag, inexact);
	if (r < 0) { *invalid = 1; return 0; }
	if (r > 0) { *invalid = 1; return UINT64_MAX; }
	if (sign) {
		if (mag == 0) return 0;
		*invalid = 1;
		return 0;
	}
	return mag;
}

/* ------------------------------------------------------------------ host exception bridging */

/* Host fenv bits -> FPSCR cumulative flags.  FE_INEXACT etc. are the standard ones. */
static inline uint32_t n2afp_host_exc_to_fpscr(int host_exc)
{
	uint32_t f = 0;
	if (host_exc & FE_INVALID) f |= N2AFP_IOC;
	if (host_exc & FE_DIVBYZERO) f |= N2AFP_DZC;
	if (host_exc & FE_OVERFLOW) f |= N2AFP_OFC;
	if (host_exc & FE_UNDERFLOW) f |= N2AFP_UFC;
	if (host_exc & FE_INEXACT) f |= N2AFP_IXC;
	return f;
}

#endif /* N2A32_FP_UTIL_H */
