/* SPDX-License-Identifier: MIT */
/*
 * n2a32_vfp.c -- package V17: AArch32 VFP (cp10/cp11) scalar floating-point instructions.
 *
 * Contract (include/n2a32.h): the core has already evaluated the condition, set c->r[15] to the
 * instruction address + 8 (A32) / +4 (T32), c->next_pc and c->in_it.  This file only executes or
 * returns N2A32_UNDEF.  It never branches, never touches c->next_pc/c->r[15] and reaches guest
 * memory only through n2a32_ld32/ld64/n2a32_st32/st64.
 *
 * The core routes to this class (a) every A32 word with bits 27-25 == 110 and cp (bits 11-8) 10/11,
 * (b) every A32 word with bits 27-25 == 111 and cp 10/11, and (c) T32 words 0xEC00-0xEEFF with the
 * same cp field.  T32 VFP words are the A32 words with the condition nibble replaced by 1110/1101/
 * 1100, so the decoder masks bits 31-28 out and treats the word as an A32 encoding; words with the
 * top nibble 1111 are the ARMv8 unconditional VFP space (VSEL/VMAXNM/VRINT/VCVTA).
 *
 * Implemented (see test_v17/STATUS.md for the tested list):
 *   load/store   VLDR, VSTR, VLDM/VSTM (IA/DB, writeback), VPUSH, VPOP, FLDMX/FSSTMX odd-imm8 forms
 *   transfers    VMOV Rt,Sn / Sn,Rt; VMOV Dm,Rt,Rt2 / Rt,Rt2,Dm; VMOV Sm,Sm1,Rt,Rt2 and reverse;
 *                VMOV Rt,Dn[x] / Dn[x],Rt (.8/.s8/.u8/.16/.s16/.u16/.32); VDUP.8/16/32 Dd/Qd,Rt
 *   system       VMRS APSR_nzcv,FPSCR; VMRS Rt,FPSCR; VMSR FPSCR,Rt
 *   arithmetic   VADD VSUB VMUL VNMUL VDIV VMLA VMLS VNMLA VNMLS VFMA VFMS VFNMA VFNMS
 *   unary        VABS VNEG VSQRT VMOV(immediate; the VFP imm8 format cannot encode zero) VMOV(register)
 *   compare      VCMP, VCMPE (register and #0)
 *   convert      VCVT.F64.F32/F32.F64, VCVT.{S,U}32.F32/F64, VCVTR (FPSCR rounding),
 *                VCVT.F32/F64.{S,U}32, VCVT fixed-point 16/32-bit both directions,
 *                VCVTB/VCVTT half precision
 *   ARMv8        VSEL (EQ/VS/GE/GT), VMAXNM, VMINNM, VRINTA/N/P/M/X/Z/R, VCVTA/N/P/M
 *
 * Deliberately N2A32_UNDEF (listed in test_v17/STATUS.md): MRC/MCR cp10/cp11 other than the FPSCR
 * VMRS/VMSR above (FPSID/FPEXC/FPINST/FPINST2/MVFR access), every encoding in the VFP space that
 * the ARM ARM leaves undefined or that this file does not decode, VCVT fixed with an fbits value
 * outside the architectural range, VDUP/VMOV lane with Rt==15, VLDM register lists that run past
 * register 31, VLDM/VSTM with Rn==15, VSTR literal, and the FLDMX/FSSTMX "extra word" is written
 * to/read from FPSCR (an intentional approximation of the removed VFPv2 mixed format).
 *
 * Floating-point results: the host float/double unit with fesetround() supplies + - * / sqrt fma
 * and the f64->f32 conversion.  Integer conversions, saturation, NaN selection/payload handling,
 * half conversion and FPSCR exception accumulation are written out in integer code, so their
 * results do not depend on the host CPU's conversion instructions.
 */
#pragma STDC FENV_ACCESS ON
#include "n2a32.h"
#include "n2a32_fp_util.h"

#include <fenv.h>
#include <math.h>
#include <string.h>

#define FPSCR_NZCV 0xF0000000u
#define FPSCR_WRITABLE 0xFBC0009Fu

/* ------------------------------------------------------------------ register access */

static unsigned vfp_sreg(unsigned f4, unsigned ext) { return (f4 << 1) | (ext & 1u); }
static unsigned vfp_dreg(unsigned f4, unsigned ext) { return ((ext & 1u) << 4) | (f4 & 0xFu); }
static unsigned vfp_reg(unsigned sz, unsigned f4, unsigned ext) { return sz ? vfp_dreg(f4, ext) : vfp_sreg(f4, ext); }

static float vfp_get_s(const struct n2a32_cpu *c, unsigned s) { return c->d[(s >> 1) & 31u].f32[s & 1u]; }
static void vfp_set_s(struct n2a32_cpu *c, unsigned s, float v) { c->d[(s >> 1) & 31u].f32[s & 1u] = v; }
static uint32_t vfp_get_sb(const struct n2a32_cpu *c, unsigned s) { return c->d[(s >> 1) & 31u].u32[s & 1u]; }
static void vfp_set_sb(struct n2a32_cpu *c, unsigned s, uint32_t v) { c->d[(s >> 1) & 31u].u32[s & 1u] = v; }
static double vfp_get_d(const struct n2a32_cpu *c, unsigned d) { return c->d[d & 31u].f64; }
static void vfp_set_d(struct n2a32_cpu *c, unsigned d, double v) { c->d[d & 31u].f64 = v; }
static uint64_t vfp_get_db(const struct n2a32_cpu *c, unsigned d) { return c->d[d & 31u].u64; }
static void vfp_set_db(struct n2a32_cpu *c, unsigned d, uint64_t v) { c->d[d & 31u].u64 = v; }

/* ------------------------------------------------------------------ NaN / FZ helpers */

static float fp_nan1_f32(struct n2a32_cpu *c, float a)
{
	if (n2afp_f32_is_snan(a)) {
		c->fpscr |= N2AFP_IOC;
		if (c->fpscr & N2AFP_DN)
			return n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN);
		return n2afp_f32_quiet(a);
	}
	if (c->fpscr & N2AFP_DN)
		return n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN);
	return a;
}

static double fp_nan1_f64(struct n2a32_cpu *c, double a)
{
	if (n2afp_f64_is_snan(a)) {
		c->fpscr |= N2AFP_IOC;
		if (c->fpscr & N2AFP_DN)
			return n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN);
		return n2afp_f64_quiet(a);
	}
	if (c->fpscr & N2AFP_DN)
		return n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN);
	return a;
}

static float fp_nan2_f32(struct n2a32_cpu *c, float a, float b)
{
	int dn = (c->fpscr & N2AFP_DN) != 0;

	if (n2afp_f32_is_snan(a)) {
		c->fpscr |= N2AFP_IOC;
		return dn ? n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN) : n2afp_f32_quiet(a);
	}
	if (n2afp_f32_is_snan(b)) {
		c->fpscr |= N2AFP_IOC;
		return dn ? n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN) : n2afp_f32_quiet(b);
	}
	if (dn) return n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN);
	if (n2afp_f32_is_nan(a)) return a;
	return b;
}

static double fp_nan2_f64(struct n2a32_cpu *c, double a, double b)
{
	int dn = (c->fpscr & N2AFP_DN) != 0;

	if (n2afp_f64_is_snan(a)) {
		c->fpscr |= N2AFP_IOC;
		return dn ? n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN) : n2afp_f64_quiet(a);
	}
	if (n2afp_f64_is_snan(b)) {
		c->fpscr |= N2AFP_IOC;
		return dn ? n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN) : n2afp_f64_quiet(b);
	}
	if (dn) return n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN);
	if (n2afp_f64_is_nan(a)) return a;
	return b;
}

static float fp_nan3_f32(struct n2a32_cpu *c, float a, float b, float d)
{
	int dn = (c->fpscr & N2AFP_DN) != 0;

	if (n2afp_f32_is_snan(a)) {
		c->fpscr |= N2AFP_IOC;
		return dn ? n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN) : n2afp_f32_quiet(a);
	}
	if (n2afp_f32_is_snan(b)) {
		c->fpscr |= N2AFP_IOC;
		return dn ? n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN) : n2afp_f32_quiet(b);
	}
	if (n2afp_f32_is_snan(d)) {
		c->fpscr |= N2AFP_IOC;
		return dn ? n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN) : n2afp_f32_quiet(d);
	}
	if (dn) return n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN);
	if (n2afp_f32_is_nan(a)) return a;
	if (n2afp_f32_is_nan(b)) return b;
	return d;
}

static double fp_nan3_f64(struct n2a32_cpu *c, double a, double b, double d)
{
	int dn = (c->fpscr & N2AFP_DN) != 0;

	if (n2afp_f64_is_snan(a)) {
		c->fpscr |= N2AFP_IOC;
		return dn ? n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN) : n2afp_f64_quiet(a);
	}
	if (n2afp_f64_is_snan(b)) {
		c->fpscr |= N2AFP_IOC;
		return dn ? n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN) : n2afp_f64_quiet(b);
	}
	if (n2afp_f64_is_snan(d)) {
		c->fpscr |= N2AFP_IOC;
		return dn ? n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN) : n2afp_f64_quiet(d);
	}
	if (dn) return n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN);
	if (n2afp_f64_is_nan(a)) return a;
	if (n2afp_f64_is_nan(b)) return b;
	return d;
}

/* FPSCR.FZ: subnormal inputs read as signed zero (and set IDC). */
static float fp_fz_in_f32(struct n2a32_cpu *c, float x)
{
	if ((c->fpscr & N2AFP_FZ) && n2afp_f32_is_subnormal(x)) {
		c->fpscr |= N2AFP_IDC;
		return n2afp_f32_from_bits(n2afp_f32_bits(x) & 0x80000000u);
	}
	return x;
}

static double fp_fz_in_f64(struct n2a32_cpu *c, double x)
{
	if ((c->fpscr & N2AFP_FZ) && n2afp_f64_is_subnormal(x)) {
		c->fpscr |= N2AFP_IDC;
		return n2afp_f64_from_bits(n2afp_f64_bits(x) & UINT64_C(0x8000000000000000));
	}
	return x;
}

/* FPSCR.FZ: a subnormal result becomes signed zero, UFC and IXC are set. */
static float fp_fz_out_f32(struct n2a32_cpu *c, float x)
{
	if ((c->fpscr & N2AFP_FZ) && n2afp_f32_is_subnormal(x)) {
		c->fpscr |= N2AFP_UFC | N2AFP_IXC;
		return n2afp_f32_from_bits(n2afp_f32_bits(x) & 0x80000000u);
	}
	return x;
}

static double fp_fz_out_f64(struct n2a32_cpu *c, double x)
{
	if ((c->fpscr & N2AFP_FZ) && n2afp_f64_is_subnormal(x)) {
		c->fpscr |= N2AFP_UFC | N2AFP_IXC;
		return n2afp_f64_from_bits(n2afp_f64_bits(x) & UINT64_C(0x8000000000000000));
	}
	return x;
}

/* Host fenv -> FPSCR cumulative bits.  `keep_inexact` = 0 drops FE_INEXACT (VRINTZ/VRINTR). */
static void fp_host_flags(struct n2a32_cpu *c, int keep_inexact)
{
	int e = fetestexcept(FE_ALL_EXCEPT);
	uint32_t f = n2afp_host_exc_to_fpscr(e);
	if (!keep_inexact)
		f &= ~N2AFP_IXC;
	c->fpscr |= f;
}

static int fp_round_start(struct n2a32_cpu *c)
{
	int old = n2afp_set_host_rounding(n2afp_fpscr_rmode(c->fpscr));
	feclearexcept(FE_ALL_EXCEPT);
	return old;
}

static void fp_round_end(int old)
{
	feclearexcept(FE_ALL_EXCEPT);
	n2afp_restore_host_rounding(old);
}

/* ------------------------------------------------------------------ scalar operations */

static float fp_add_f32(struct n2a32_cpu *c, float a, float b)
{
	float r; int old;
	if (n2afp_f32_is_nan(a) || n2afp_f32_is_nan(b)) return fp_nan2_f32(c, a, b);
	a = fp_fz_in_f32(c, a); b = fp_fz_in_f32(c, b);
	old = fp_round_start(c);
	r = a + b;
	fp_host_flags(c, 1);
	fp_round_end(old);
	if (n2afp_f32_is_nan(r)) r = n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN);
	return fp_fz_out_f32(c, r);
}

static float fp_sub_f32(struct n2a32_cpu *c, float a, float b)
{
	float r; int old;
	if (n2afp_f32_is_nan(a) || n2afp_f32_is_nan(b)) return fp_nan2_f32(c, a, b);
	a = fp_fz_in_f32(c, a); b = fp_fz_in_f32(c, b);
	old = fp_round_start(c);
	r = a - b;
	fp_host_flags(c, 1);
	fp_round_end(old);
	if (n2afp_f32_is_nan(r)) r = n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN);
	return fp_fz_out_f32(c, r);
}

static float fp_mul_f32(struct n2a32_cpu *c, float a, float b)
{
	float r; int old;
	if (n2afp_f32_is_nan(a) || n2afp_f32_is_nan(b)) return fp_nan2_f32(c, a, b);
	a = fp_fz_in_f32(c, a); b = fp_fz_in_f32(c, b);
	old = fp_round_start(c);
	r = a * b;
	fp_host_flags(c, 1);
	fp_round_end(old);
	if (n2afp_f32_is_nan(r)) r = n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN);
	return fp_fz_out_f32(c, r);
}

static float fp_div_f32(struct n2a32_cpu *c, float a, float b)
{
	float r; int old;
	if (n2afp_f32_is_nan(a) || n2afp_f32_is_nan(b)) return fp_nan2_f32(c, a, b);
	a = fp_fz_in_f32(c, a); b = fp_fz_in_f32(c, b);
	old = fp_round_start(c);
	r = a / b;
	fp_host_flags(c, 1);
	fp_round_end(old);
	if (n2afp_f32_is_nan(r)) r = n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN);
	return fp_fz_out_f32(c, r);
}

static float fp_neg_f32(struct n2a32_cpu *c, float a)
{
	if (n2afp_f32_is_nan(a)) return fp_nan1_f32(c, a);
	return n2afp_f32_from_bits(n2afp_f32_bits(a) ^ 0x80000000u);
}

static float fp_abs_f32(struct n2a32_cpu *c, float a)
{
	if (n2afp_f32_is_nan(a)) return fp_nan1_f32(c, a);
	return n2afp_f32_from_bits(n2afp_f32_bits(a) & 0x7FFFFFFFu);
}

static float fp_sqrt_f32(struct n2a32_cpu *c, float a)
{
	float r; int old;
	if (n2afp_f32_is_nan(a)) return fp_nan1_f32(c, a);
	a = fp_fz_in_f32(c, a);
	if (a == 0.0f) return a;	/* +0 and -0 */
	if (n2afp_f32_bits(a) & 0x80000000u) {
		c->fpscr |= N2AFP_IOC;
		return n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN);
	}
	old = fp_round_start(c);
	r = sqrtf(a);
	fp_host_flags(c, 1);
	fp_round_end(old);
	return fp_fz_out_f32(c, r);
}

/* Fused multiply-add family.  neg_prod negates Vn*Vm before the add; neg_res negates the result.
 * NaN order is (Vn, Vm, Vd) as FPProcessNaNs3 in the ARM ARM. */
static float fp_fma_f32(struct n2a32_cpu *c, float a, float b, float acc, int neg_prod, int neg_res)
{
	float r; int old;
	if (n2afp_f32_is_nan(a) || n2afp_f32_is_nan(b) || n2afp_f32_is_nan(acc))
		return fp_nan3_f32(c, a, b, acc);
	a = fp_fz_in_f32(c, a); b = fp_fz_in_f32(c, b); acc = fp_fz_in_f32(c, acc);
	if (neg_prod) a = n2afp_f32_from_bits(n2afp_f32_bits(a) ^ 0x80000000u);
	old = fp_round_start(c);
	r = fmaf(a, b, acc);
	fp_host_flags(c, 1);
	fp_round_end(old);
	if (n2afp_f32_is_nan(r)) r = n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN);
	if (neg_res) r = fp_neg_f32(c, r);
	return fp_fz_out_f32(c, r);
}

static double fp_add_f64(struct n2a32_cpu *c, double a, double b)
{
	double r; int old;
	if (n2afp_f64_is_nan(a) || n2afp_f64_is_nan(b)) return fp_nan2_f64(c, a, b);
	a = fp_fz_in_f64(c, a); b = fp_fz_in_f64(c, b);
	old = fp_round_start(c);
	r = a + b;
	fp_host_flags(c, 1);
	fp_round_end(old);
	if (n2afp_f64_is_nan(r)) r = n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN);
	return fp_fz_out_f64(c, r);
}

static double fp_sub_f64(struct n2a32_cpu *c, double a, double b)
{
	double r; int old;
	if (n2afp_f64_is_nan(a) || n2afp_f64_is_nan(b)) return fp_nan2_f64(c, a, b);
	a = fp_fz_in_f64(c, a); b = fp_fz_in_f64(c, b);
	old = fp_round_start(c);
	r = a - b;
	fp_host_flags(c, 1);
	fp_round_end(old);
	if (n2afp_f64_is_nan(r)) r = n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN);
	return fp_fz_out_f64(c, r);
}

static double fp_mul_f64(struct n2a32_cpu *c, double a, double b)
{
	double r; int old;
	if (n2afp_f64_is_nan(a) || n2afp_f64_is_nan(b)) return fp_nan2_f64(c, a, b);
	a = fp_fz_in_f64(c, a); b = fp_fz_in_f64(c, b);
	old = fp_round_start(c);
	r = a * b;
	fp_host_flags(c, 1);
	fp_round_end(old);
	if (n2afp_f64_is_nan(r)) r = n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN);
	return fp_fz_out_f64(c, r);
}

static double fp_div_f64(struct n2a32_cpu *c, double a, double b)
{
	double r; int old;
	if (n2afp_f64_is_nan(a) || n2afp_f64_is_nan(b)) return fp_nan2_f64(c, a, b);
	a = fp_fz_in_f64(c, a); b = fp_fz_in_f64(c, b);
	old = fp_round_start(c);
	r = a / b;
	fp_host_flags(c, 1);
	fp_round_end(old);
	if (n2afp_f64_is_nan(r)) r = n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN);
	return fp_fz_out_f64(c, r);
}

static double fp_neg_f64(struct n2a32_cpu *c, double a)
{
	if (n2afp_f64_is_nan(a)) return fp_nan1_f64(c, a);
	return n2afp_f64_from_bits(n2afp_f64_bits(a) ^ UINT64_C(0x8000000000000000));
}

static double fp_abs_f64(struct n2a32_cpu *c, double a)
{
	if (n2afp_f64_is_nan(a)) return fp_nan1_f64(c, a);
	return n2afp_f64_from_bits(n2afp_f64_bits(a) & UINT64_C(0x7FFFFFFFFFFFFFFF));
}

static double fp_sqrt_f64(struct n2a32_cpu *c, double a)
{
	double r; int old;
	if (n2afp_f64_is_nan(a)) return fp_nan1_f64(c, a);
	a = fp_fz_in_f64(c, a);
	if (a == 0.0) return a;
	if (n2afp_f64_bits(a) & UINT64_C(0x8000000000000000)) {
		c->fpscr |= N2AFP_IOC;
		return n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN);
	}
	old = fp_round_start(c);
	r = sqrt(a);
	fp_host_flags(c, 1);
	fp_round_end(old);
	return fp_fz_out_f64(c, r);
}

static double fp_fma_f64(struct n2a32_cpu *c, double a, double b, double acc, int neg_prod, int neg_res)
{
	double r; int old;
	if (n2afp_f64_is_nan(a) || n2afp_f64_is_nan(b) || n2afp_f64_is_nan(acc))
		return fp_nan3_f64(c, a, b, acc);
	a = fp_fz_in_f64(c, a); b = fp_fz_in_f64(c, b); acc = fp_fz_in_f64(c, acc);
	if (neg_prod) a = n2afp_f64_from_bits(n2afp_f64_bits(a) ^ UINT64_C(0x8000000000000000));
	old = fp_round_start(c);
	r = fma(a, b, acc);
	fp_host_flags(c, 1);
	fp_round_end(old);
	if (n2afp_f64_is_nan(r)) r = n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN);
	if (neg_res) r = fp_neg_f64(c, r);
	return fp_fz_out_f64(c, r);
}

/* ------------------------------------------------------------------ compare */

static void fp_cmp_f32(struct n2a32_cpu *c, float a, float b, int signal_all_nan)
{
	uint32_t nzcv;
	if (n2afp_f32_is_nan(a) || n2afp_f32_is_nan(b)) {
		if (signal_all_nan || n2afp_f32_is_snan(a) || n2afp_f32_is_snan(b))
			c->fpscr |= N2AFP_IOC;
		nzcv = 0x30000000u;	/* N=0 Z=0 C=1 V=1 */
	} else if (a < b) {
		nzcv = 0x80000000u;	/* N=1 Z=0 C=0 V=0 */
	} else if (a == b) {
		nzcv = 0x60000000u;	/* N=0 Z=1 C=1 V=0 */
	} else {
		nzcv = 0x20000000u;	/* N=0 Z=0 C=1 V=0 */
	}
	c->fpscr = (c->fpscr & ~FPSCR_NZCV) | nzcv;
}

static void fp_cmp_f64(struct n2a32_cpu *c, double a, double b, int signal_all_nan)
{
	uint32_t nzcv;
	if (n2afp_f64_is_nan(a) || n2afp_f64_is_nan(b)) {
		if (signal_all_nan || n2afp_f64_is_snan(a) || n2afp_f64_is_snan(b))
			c->fpscr |= N2AFP_IOC;
		nzcv = 0x30000000u;
	} else if (a < b) {
		nzcv = 0x80000000u;
	} else if (a == b) {
		nzcv = 0x60000000u;
	} else {
		nzcv = 0x20000000u;
	}
	c->fpscr = (c->fpscr & ~FPSCR_NZCV) | nzcv;
}

/* ------------------------------------------------------------------ conversions */

static float fp_cvt_f64_to_f32(struct n2a32_cpu *c, double a)
{
	float r; int old;
	if (n2afp_f64_is_nan(a)) {
		uint64_t b = n2afp_f64_bits(a);
		uint32_t sign = (uint32_t)(b >> 32) & 0x80000000u;
		uint32_t frac;
		if (n2afp_f64_is_snan(a)) {
			c->fpscr |= N2AFP_IOC;
			if (c->fpscr & N2AFP_DN)
				return n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN);
			b |= UINT64_C(0x0008000000000000);
		} else if (c->fpscr & N2AFP_DN) {
			return n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN);
		}
		frac = (uint32_t)((b >> 29) & 0x007FFFFFu);
		return n2afp_f32_from_bits(sign | 0x7F800000u | frac);
	}
	a = fp_fz_in_f64(c, a);
	old = fp_round_start(c);
	r = (float)a;
	fp_host_flags(c, 1);
	fp_round_end(old);
	if (n2afp_f32_is_nan(r)) r = n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN);
	return fp_fz_out_f32(c, r);
}

static double fp_cvt_f32_to_f64(struct n2a32_cpu *c, float a)
{
	uint32_t b;
	if (n2afp_f32_is_nan(a)) {
		b = n2afp_f32_bits(a);
		if (n2afp_f32_is_snan(a)) {
			c->fpscr |= N2AFP_IOC;
			if (c->fpscr & N2AFP_DN)
				return n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN);
			b |= 0x00400000u;
		} else if (c->fpscr & N2AFP_DN) {
			return n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN);
		}
		return n2afp_f64_from_bits(((uint64_t)(b & 0x80000000u) << 32) |
					   UINT64_C(0x7FF0000000000000) |
					   ((uint64_t)(b & 0x007FFFFFu) << 29));
	}
	return (double)a;
}

/* integer -> FP, with the destination precision given by sz */
static float fp_cvt_i32_to_f32(struct n2a32_cpu *c, uint32_t bits, int is_signed)
{
	double dv = is_signed ? (double)(int32_t)bits : (double)bits;
	float r; int old;
	old = fp_round_start(c);
	r = (float)dv;
	fp_host_flags(c, 1);
	fp_round_end(old);
	return r;
}

static double fp_cvt_i32_to_f64(struct n2a32_cpu *c, uint32_t bits, int is_signed)
{
	(void)c;
	return is_signed ? (double)(int32_t)bits : (double)bits;
}

/* FP -> integer with saturation (ARM FPToFixed): NaN/out-of-range -> IOC and the ARM
 * saturation value (0 for NaN), inexact -> IXC. */
static uint32_t fp_cvt_f32_to_i32(struct n2a32_cpu *c, float a, int is_signed, int rmode)
{
	double dv;
	int inexact = 0, invalid = 0;
	uint32_t r;

	if (n2afp_f32_is_nan(a)) { c->fpscr |= N2AFP_IOC; return 0; }
	a = fp_fz_in_f32(c, a);
	dv = (double)a;
	r = is_signed ? (uint32_t)n2afp_f64_to_i32(dv, rmode, &inexact, &invalid)
		      : n2afp_f64_to_u32(dv, rmode, &inexact, &invalid);
	if (invalid) c->fpscr |= N2AFP_IOC;
	if (inexact) c->fpscr |= N2AFP_IXC;
	return r;
}

static uint32_t fp_cvt_f64_to_i32(struct n2a32_cpu *c, double a, int is_signed, int rmode)
{
	int inexact = 0, invalid = 0;
	uint32_t r;

	if (n2afp_f64_is_nan(a)) { c->fpscr |= N2AFP_IOC; return 0; }
	a = fp_fz_in_f64(c, a);
	r = is_signed ? (uint32_t)n2afp_f64_to_i32(a, rmode, &inexact, &invalid)
		      : n2afp_f64_to_u32(a, rmode, &inexact, &invalid);
	if (invalid) c->fpscr |= N2AFP_IOC;
	if (inexact) c->fpscr |= N2AFP_IXC;
	return r;
}

/* fixed-point conversions.  The integer side is 16 or 32 bits, signed or unsigned, with `fbits`
 * fractional bits.  The result is stored in the low 32 bits of the same S/D register. */
static uint32_t fp_get_fixed_int(const struct n2a32_cpu *c, unsigned sz, unsigned reg, int bits32)
{
	uint32_t raw = sz ? c->d[reg & 31u].u32[0] : vfp_get_sb(c, reg);

	if (bits32)
		return raw;
	return raw & 0xFFFFu;
}

/* raw is the zero-extended 16/32-bit integer; is_signed selects sign extension. */
static double fp_fixed_int_to_double(uint32_t raw, int bits32, int is_signed)
{
	if (bits32) {
		if (is_signed) return (double)(int32_t)raw;
		return (double)raw;
	}
	if (is_signed) return (double)(int16_t)(uint16_t)raw;
	return (double)(uint16_t)raw;
}

/* exact scaling by 2^-fbits; |raw| < 2^32 and fbits <= 32, so the result stays a normal double */
static double fp_fixed_scale_down(double d, int fbits)
{
	uint64_t b;
	int e;

	if (d == 0.0 || fbits <= 0)
		return d;
	b = n2afp_f64_bits(d);
	e = (int)((b >> 52) & 0x7FFu) - 1023 - fbits;
	b = (b & UINT64_C(0x800FFFFFFFFFFFFF)) | ((uint64_t)(e + 1023) << 52);
	return n2afp_f64_from_bits(b);
}

static float fp_cvt_fixed_to_f32(struct n2a32_cpu *c, uint32_t raw, int fbits, int bits32, int is_signed)
{
	double d = fp_fixed_scale_down(fp_fixed_int_to_double(raw, bits32, is_signed), fbits);
	float r; int old;

	old = fp_round_start(c);
	r = (float)d;
	fp_host_flags(c, 1);
	fp_round_end(old);
	return r;
}

static double fp_cvt_fixed_to_f64(struct n2a32_cpu *c, uint32_t raw, int fbits, int bits32, int is_signed)
{
	(void)c;
	return fp_fixed_scale_down(fp_fixed_int_to_double(raw, bits32, is_signed), fbits);
}

/* exact scaling by 2^fbits for the FP->fixed direction; an out-of-range value becomes infinity,
 * which the saturating converter turns into the saturation value + IOC. */
static double fp_fixed_scale_up(double d, int fbits)
{
	uint64_t b;
	unsigned ex;
	int e;

	if (d == 0.0 || fbits <= 0)
		return d;
	b = n2afp_f64_bits(d);
	ex = (unsigned)((b >> 52) & 0x7FFu);
	if (ex == 0 || ex == 0x7FFu)
		return d;		/* subnormal stays tiny; inf/nan unchanged */
	e = (int)ex - 1023 + fbits;
	if (e >= 0x7FF - 1)
		return n2afp_f64_from_bits((b & UINT64_C(0x8000000000000000)) |
					   UINT64_C(0x7FF0000000000000));
	b = (b & UINT64_C(0x800FFFFFFFFFFFFF)) | ((uint64_t)(e + 1023) << 52);
	return n2afp_f64_from_bits(b);
}

static uint32_t fp_cvt_f32_to_fixed(struct n2a32_cpu *c, float a, int fbits, int bits32, int is_signed)
{
	double scaled;
	int inexact = 0, invalid = 0;
	uint32_t r;
	int rmode = n2afp_fpscr_rmode(c->fpscr);

	if (n2afp_f32_is_nan(a)) { c->fpscr |= N2AFP_IOC; return 0; }
	a = fp_fz_in_f32(c, a);
	scaled = fp_fixed_scale_up((double)a, fbits);
	r = is_signed ? (uint32_t)n2afp_f64_to_i32(scaled, rmode, &inexact, &invalid)
		      : n2afp_f64_to_u32(scaled, rmode, &inexact, &invalid);
	if (!bits32) {
		if (is_signed) {
			int32_t sv = (int32_t)r;
			if (sv < -32768) { invalid = 1; sv = -32768; }
			else if (sv > 32767) { invalid = 1; sv = 32767; }
			r = (uint32_t)sv;
		} else {
			if (r > 0xFFFFu) { invalid = 1; r = 0xFFFFu; }
		}
	}
	if (invalid) c->fpscr |= N2AFP_IOC;
	if (inexact) c->fpscr |= N2AFP_IXC;
	return r;
}

static uint32_t fp_cvt_f64_to_fixed(struct n2a32_cpu *c, double a, int fbits, int bits32, int is_signed)
{
	double scaled;
	int inexact = 0, invalid = 0;
	uint32_t r;
	int rmode = n2afp_fpscr_rmode(c->fpscr);

	if (n2afp_f64_is_nan(a)) { c->fpscr |= N2AFP_IOC; return 0; }
	a = fp_fz_in_f64(c, a);
	scaled = fp_fixed_scale_up(a, fbits);
	r = is_signed ? (uint32_t)n2afp_f64_to_i32(scaled, rmode, &inexact, &invalid)
		      : n2afp_f64_to_u32(scaled, rmode, &inexact, &invalid);
	if (!bits32) {
		if (is_signed) {
			int32_t sv = (int32_t)r;
			if (sv < -32768) { invalid = 1; sv = -32768; }
			else if (sv > 32767) { invalid = 1; sv = 32767; }
			r = (uint32_t)sv;
		} else {
			if (r > 0xFFFFu) { invalid = 1; r = 0xFFFFu; }
		}
	}
	if (invalid) c->fpscr |= N2AFP_IOC;
	if (inexact) c->fpscr |= N2AFP_IXC;
	return r;
}

/* ------------------------------------------------------------------ half precision */

static float fp_f16_to_f32(struct n2a32_cpu *c, uint16_t h)
{
	uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
	unsigned exp = (h >> 10) & 0x1Fu;
	unsigned frac = h & 0x3FFu;
	uint32_t b;

	if (exp == 0) {
		if (frac == 0) return n2afp_f32_from_bits(sign);
		if (c->fpscr & N2AFP_FZ) {
			c->fpscr |= N2AFP_IDC;
			return n2afp_f32_from_bits(sign);
		}
		{
			int p = 9;
			while ((frac & (1u << p)) == 0) p--;
			b = sign | ((uint32_t)(127 + p - 24) << 23) |
			    ((uint32_t)(frac & ((1u << p) - 1u)) << (23 - p));
			return n2afp_f32_from_bits(b);
		}
	}
	if (exp == 0x1F) {
		if (frac == 0) return n2afp_f32_from_bits(sign | 0x7F800000u);
		if ((frac & 0x200u) == 0) {
			c->fpscr |= N2AFP_IOC;
			if (c->fpscr & N2AFP_DN)
				return n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN);
			frac |= 0x200u;
		} else if (c->fpscr & N2AFP_DN) {
			return n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN);
		}
		return n2afp_f32_from_bits(sign | 0x7F800000u | ((uint32_t)frac << 13));
	}
	return n2afp_f32_from_bits(sign | ((uint32_t)(exp + 127 - 15) << 23) | ((uint32_t)frac << 13));
}

/* Round a 24-bit significand to a 10-bit half fraction; returns the half fraction bits and sets
 * *inexact when any discarded bit was nonzero.  `sticky`/`guard` come from the discarded bits. */
static uint16_t fp_f32_to_f16(struct n2a32_cpu *c, float a)
{
	uint32_t b = n2afp_f32_bits(a);
	uint32_t sign = (b >> 16) & 0x8000u;
	unsigned exp = (b >> 23) & 0xFFu;
	uint32_t frac = b & 0x007FFFFFu;
	int rmode = n2afp_fpscr_rmode(c->fpscr);
	uint32_t sig, q, rem;
	int shift, e;
	uint16_t out;

	if (exp == 0xFFu) {
		if (frac == 0) return (uint16_t)(sign | 0x7C00u);
		if ((frac & 0x00400000u) == 0) {
			c->fpscr |= N2AFP_IOC;
			if (c->fpscr & N2AFP_DN)
				return (uint16_t)N2AFP_F16_DEFAULT_NAN;
			frac |= 0x00400000u;
		} else if (c->fpscr & N2AFP_DN) {
			return (uint16_t)N2AFP_F16_DEFAULT_NAN;
		}
		return (uint16_t)(sign | 0x7C00u | ((frac >> 13) & 0x3FFu));
	}
	if (exp == 0) {		/* f32 subnormal or zero: far below half's normal range */
		if (frac == 0) return (uint16_t)sign;
		if (c->fpscr & N2AFP_FZ) {
			c->fpscr |= N2AFP_IDC;	/* input flushed to zero: exact zero result */
			return (uint16_t)sign;
		}
		c->fpscr |= N2AFP_UFC | N2AFP_IXC;
		return (uint16_t)sign;
	}

	e = (int)exp - 127;
	sig = 0x00800000u | frac;	/* 24-bit significand */

	if (e > 15) {		/* overflow: RN/RP(+)/RM(-) give infinity, the others max finite */
		int to_inf;
		c->fpscr |= N2AFP_OFC | N2AFP_IXC;
		if (rmode == N2AFP_RN) to_inf = 1;
		else if (rmode == N2AFP_RZ) to_inf = 0;
		else if (rmode == N2AFP_RP) to_inf = (sign == 0);
		else to_inf = (sign != 0);
		out = to_inf ? 0x7C00u : 0x7BFFu;
		return (uint16_t)(sign | out);
	}

	/* target value: sig * 2^(e-23); half fraction quantum 2^(e-10) for normals,
	 * 2^-24 for subnormals. */
	if (e >= -14) {
		uint32_t qfrac = frac >> 13;
		uint32_t dropped = frac & 0x1FFFu;
		uint16_t bexp = (uint16_t)((e + 15) & 0x1Fu);
		int inc = 0;
		int inexact = dropped != 0;

		if (inexact) {
			switch (rmode) {
			case N2AFP_RN: {
				uint32_t guard = (frac >> 12) & 1u;
				uint32_t sticky = frac & 0xFFFu;
				inc = guard && (sticky != 0 || (qfrac & 1u) != 0);
				break;
			}
			case N2AFP_RA: {
				uint32_t guard = (frac >> 12) & 1u;
				inc = guard;
				break;
			}
			case N2AFP_RP: inc = (sign == 0); break;
			case N2AFP_RM: inc = (sign != 0); break;
			default: inc = 0; break;
			}
		}
		if (inc) {
			qfrac++;
			if (qfrac == 0x400u) { qfrac = 0; bexp++; }
		}
		if (inexact) c->fpscr |= N2AFP_IXC;
		if (bexp >= 0x1Fu) { c->fpscr |= N2AFP_OFC | N2AFP_IXC; bexp = 0x1Fu; qfrac = 0; }
		out = (uint16_t)((bexp << 10) | qfrac);
		return (uint16_t)(sign | out);
	}

	/* subnormal half: value = sig * 2^(e-23); half subnormal quantum 2^-24, so the integer
	 * number of quanta is sig >> (23 - e - 24)?  shift = (23 - e) - 24 + 24 = 23 - e?  Use
	 * q = sig * 2^(e+1) = sig >> -(e+1). */
	shift = -(e + 1);
	if (shift >= 32) {
		q = 0;
		rem = sig;
	} else {
		q = sig >> (unsigned)shift;
		rem = sig & ((1u << (unsigned)shift) - 1u);
	}
	if (rem != 0) {
		c->fpscr |= N2AFP_IXC | N2AFP_UFC;
		switch (rmode) {
		case N2AFP_RN: {
			uint32_t h = (shift >= 32) ? 0xFFFFFFFFu : (1u << (unsigned)(shift - 1));
			if (shift < 32 && (rem > h || (rem == h && (q & 1u) != 0))) q++;
			break;
		}
		case N2AFP_RA: {
			uint32_t h = (shift >= 32) ? 0xFFFFFFFFu : (1u << (unsigned)(shift - 1));
			if (shift < 32 && rem >= h) q++;
			break;
		}
		case N2AFP_RP: if (sign == 0) q++; break;
		case N2AFP_RM: if (sign != 0) q++; break;
		default: break;
		}
	}
	if (c->fpscr & N2AFP_FZ) {
		c->fpscr |= N2AFP_UFC | N2AFP_IXC;
		q = 0;
	}
	if (q >= 0x400u) return (uint16_t)(sign | 0x0400u);	/* rounded up to min normal */
	return (uint16_t)(sign | q);
}

/* ------------------------------------------------------------------ VRINT */

/* kind: 0=A, 1=N, 2=P, 3=M, 4=X, 5=Z, 6=R */
static float fp_rint_f32(struct n2a32_cpu *c, float a, int kind)
{
	float r; int old, keep_inexact = 0;

	if (n2afp_f32_is_nan(a)) return fp_nan1_f32(c, a);
	a = fp_fz_in_f32(c, a);
	if (n2afp_f32_is_inf(a)) return a;

	if (kind == 1) {
		old = n2afp_set_host_rounding(N2AFP_RN);
	} else if (kind == 6) {
		old = fp_round_start(c);
	} else {
		old = n2afp_set_host_rounding(n2afp_fpscr_rmode(c->fpscr));
	}
	feclearexcept(FE_ALL_EXCEPT);
	switch (kind) {
	case 0: r = roundf(a); break;
	case 1: r = nearbyintf(a); break;
	case 2: r = ceilf(a); break;
	case 3: r = floorf(a); break;
	case 4: r = rintf(a); keep_inexact = 1; break;
	case 5: r = truncf(a); break;
	default: r = nearbyintf(a); break;
	}
	fp_host_flags(c, keep_inexact);
	feclearexcept(FE_ALL_EXCEPT);
	n2afp_restore_host_rounding(old);
	return r;
}

static double fp_rint_f64(struct n2a32_cpu *c, double a, int kind)
{
	double r; int old, keep_inexact = 0;

	if (n2afp_f64_is_nan(a)) return fp_nan1_f64(c, a);
	a = fp_fz_in_f64(c, a);
	if (n2afp_f64_is_inf(a)) return a;

	if (kind == 1) {
		old = n2afp_set_host_rounding(N2AFP_RN);
	} else if (kind == 6) {
		old = fp_round_start(c);
	} else {
		old = n2afp_set_host_rounding(n2afp_fpscr_rmode(c->fpscr));
	}
	feclearexcept(FE_ALL_EXCEPT);
	switch (kind) {
	case 0: r = round(a); break;
	case 1: r = nearbyint(a); break;
	case 2: r = ceil(a); break;
	case 3: r = floor(a); break;
	case 4: r = rint(a); keep_inexact = 1; break;
	case 5: r = trunc(a); break;
	default: r = nearbyint(a); break;
	}
	fp_host_flags(c, keep_inexact);
	feclearexcept(FE_ALL_EXCEPT);
	n2afp_restore_host_rounding(old);
	return r;
}

/* ------------------------------------------------------------------ vmaxnm / vminnm */

static float fp_maxnm_f32(struct n2a32_cpu *c, float a, float b, int is_max)
{
	int an = n2afp_f32_is_nan(a), bn = n2afp_f32_is_nan(b);
	if (n2afp_f32_is_snan(a) || n2afp_f32_is_snan(b)) c->fpscr |= N2AFP_IOC;
	if (an && bn) {
		if (c->fpscr & N2AFP_DN) return n2afp_f32_from_bits(N2AFP_F32_DEFAULT_NAN);
		if (n2afp_f32_is_snan(a)) return n2afp_f32_quiet(a);
		if (n2afp_f32_is_snan(b)) return n2afp_f32_quiet(b);
		return a;
	}
	if (an) return b;
	if (bn) return a;
	a = fp_fz_in_f32(c, a); b = fp_fz_in_f32(c, b);
	return is_max ? fmaxf(a, b) : fminf(a, b);
}

static double fp_maxnm_f64(struct n2a32_cpu *c, double a, double b, int is_max)
{
	int an = n2afp_f64_is_nan(a), bn = n2afp_f64_is_nan(b);
	if (n2afp_f64_is_snan(a) || n2afp_f64_is_snan(b)) c->fpscr |= N2AFP_IOC;
	if (an && bn) {
		if (c->fpscr & N2AFP_DN) return n2afp_f64_from_bits(N2AFP_F64_DEFAULT_NAN);
		if (n2afp_f64_is_snan(a)) return n2afp_f64_quiet(a);
		if (n2afp_f64_is_snan(b)) return n2afp_f64_quiet(b);
		return a;
	}
	if (an) return b;
	if (bn) return a;
	a = fp_fz_in_f64(c, a); b = fp_fz_in_f64(c, b);
	return is_max ? fmax(a, b) : fmin(a, b);
}

/* ------------------------------------------------------------------ shared field extraction */

struct vfp_fields {
	unsigned sz, d, n, m;		/* D/N/M extension bits */
	unsigned vd, vn, vm;		/* 4-bit register fields */
	unsigned rd, rn, rm;		/* 5-bit register numbers (single or double per sz) */
};

static void vfp_fields_get(const uint32_t insn, struct vfp_fields *f)
{
	f->d = (insn >> 22) & 1u;
	f->sz = (insn >> 8) & 1u;
	f->vd = (insn >> 12) & 0xFu;
	f->vn = (insn >> 16) & 0xFu;
	f->vm = insn & 0xFu;
	f->n = (insn >> 7) & 1u;
	f->m = (insn >> 5) & 1u;
	f->rd = vfp_reg(f->sz, f->vd, f->d);
	f->rn = vfp_reg(f->sz, f->vn, f->n);
	f->rm = vfp_reg(f->sz, f->vm, f->m);
}

/* ------------------------------------------------------------------ data processing */

enum {
	FPOP_ADD, FPOP_SUB, FPOP_MUL, FPOP_NMUL, FPOP_DIV,
	FPOP_MLA, FPOP_MLS, FPOP_NMLA, FPOP_NMLS,
	FPOP_FMA, FPOP_FMS, FPOP_FNMA, FPOP_FNMS
};

static void fp_binop_f32(struct n2a32_cpu *c, unsigned op, unsigned rd, unsigned rn, unsigned rm)
{
	float n = vfp_get_s(c, rn), m = vfp_get_s(c, rm), d0 = vfp_get_s(c, rd), r, p;

	switch (op) {
	case FPOP_ADD: r = fp_add_f32(c, n, m); break;
	case FPOP_SUB: r = fp_sub_f32(c, n, m); break;
	case FPOP_MUL: r = fp_mul_f32(c, n, m); break;
	case FPOP_NMUL: r = fp_neg_f32(c, fp_mul_f32(c, n, m)); break;
	case FPOP_DIV: r = fp_div_f32(c, n, m); break;
	case FPOP_MLA: p = fp_mul_f32(c, n, m); r = fp_add_f32(c, p, d0); break;
	case FPOP_MLS: p = fp_mul_f32(c, n, m); r = fp_sub_f32(c, d0, p); break;
	case FPOP_NMLA: p = fp_mul_f32(c, n, m); r = fp_neg_f32(c, fp_add_f32(c, d0, p)); break;
	case FPOP_NMLS: p = fp_mul_f32(c, n, m); r = fp_neg_f32(c, fp_sub_f32(c, d0, p)); break;
	case FPOP_FMA: r = fp_fma_f32(c, n, m, d0, 0, 0); break;
	case FPOP_FMS: r = fp_fma_f32(c, n, m, d0, 1, 0); break;
	case FPOP_FNMA: r = fp_fma_f32(c, n, m, d0, 0, 1); break;
	default: r = fp_fma_f32(c, n, m, d0, 1, 1); break;
	}
	vfp_set_s(c, rd, r);
}

static void fp_binop_f64(struct n2a32_cpu *c, unsigned op, unsigned rd, unsigned rn, unsigned rm)
{
	double n = vfp_get_d(c, rn), m = vfp_get_d(c, rm), d0 = vfp_get_d(c, rd), r, p;

	switch (op) {
	case FPOP_ADD: r = fp_add_f64(c, n, m); break;
	case FPOP_SUB: r = fp_sub_f64(c, n, m); break;
	case FPOP_MUL: r = fp_mul_f64(c, n, m); break;
	case FPOP_NMUL: r = fp_neg_f64(c, fp_mul_f64(c, n, m)); break;
	case FPOP_DIV: r = fp_div_f64(c, n, m); break;
	case FPOP_MLA: p = fp_mul_f64(c, n, m); r = fp_add_f64(c, p, d0); break;
	case FPOP_MLS: p = fp_mul_f64(c, n, m); r = fp_sub_f64(c, d0, p); break;
	case FPOP_NMLA: p = fp_mul_f64(c, n, m); r = fp_neg_f64(c, fp_add_f64(c, d0, p)); break;
	case FPOP_NMLS: p = fp_mul_f64(c, n, m); r = fp_neg_f64(c, fp_sub_f64(c, d0, p)); break;
	case FPOP_FMA: r = fp_fma_f64(c, n, m, d0, 0, 0); break;
	case FPOP_FMS: r = fp_fma_f64(c, n, m, d0, 1, 0); break;
	case FPOP_FNMA: r = fp_fma_f64(c, n, m, d0, 0, 1); break;
	default: r = fp_fma_f64(c, n, m, d0, 1, 1); break;
	}
	vfp_set_d(c, rd, r);
}

static int vfp_vmov_imm(struct n2a32_cpu *c, uint32_t insn, const struct vfp_fields *f)
{
	unsigned imm8 = (((insn >> 16) & 0xFu) << 4) | (insn & 0xFu);
	uint32_t sign = (uint32_t)(imm8 >> 7) << 31;
	unsigned b = (imm8 >> 6) & 1u;
	unsigned e = f->sz ? 11u : 8u;
	uint32_t exponent = ((b ? 0u : 1u) << (e - 1)) |
			    (b ? (((1u << (e - 3)) - 1u) << 2) : 0u) |
			    ((imm8 >> 4) & 3u);

	if (f->sz) {
		uint64_t frac = (uint64_t)(imm8 & 0xFu) << (52 - 4);
		vfp_set_db(c, f->rd, ((uint64_t)sign << 32) | ((uint64_t)exponent << 52) | frac);
	} else {
		uint32_t frac = (imm8 & 0xFu) << (23 - 4);
		vfp_set_sb(c, f->rd, sign | (exponent << 23) | frac);
	}
	return N2A32_OK;
}

static int vfp_vmov_reg(struct n2a32_cpu *c, const struct vfp_fields *f)
{
	if (f->sz) vfp_set_db(c, f->rd, vfp_get_db(c, f->rm));
	else vfp_set_sb(c, f->rd, vfp_get_sb(c, f->rm));
	return N2A32_OK;
}

static int vfp_unary_absneg(struct n2a32_cpu *c, const struct vfp_fields *f, int is_abs)
{
	if (f->sz) vfp_set_d(c, f->rd, is_abs ? fp_abs_f64(c, vfp_get_d(c, f->rm))
					      : fp_neg_f64(c, vfp_get_d(c, f->rm)));
	else vfp_set_s(c, f->rd, is_abs ? fp_abs_f32(c, vfp_get_s(c, f->rm))
					: fp_neg_f32(c, vfp_get_s(c, f->rm)));
	return N2A32_OK;
}

static int vfp_sqrt(struct n2a32_cpu *c, const struct vfp_fields *f)
{
	if (f->sz) vfp_set_d(c, f->rd, fp_sqrt_f64(c, vfp_get_d(c, f->rm)));
	else vfp_set_s(c, f->rd, fp_sqrt_f32(c, vfp_get_s(c, f->rm)));
	return N2A32_OK;
}

static int vfp_vcmp(struct n2a32_cpu *c, uint32_t insn, const struct vfp_fields *f, int with_zero)
{
	int signal_all = (int)((insn >> 7) & 1u);

	if (with_zero) {
		if ((insn & 0x7Fu) != 0x40u)
			return N2A32_UNDEF;
		if (f->sz) fp_cmp_f64(c, vfp_get_d(c, f->rd), 0.0, signal_all);
		else fp_cmp_f32(c, vfp_get_s(c, f->rd), 0.0f, signal_all);
		return N2A32_OK;
	}
	if (f->sz) fp_cmp_f64(c, vfp_get_d(c, f->rd), vfp_get_d(c, f->rm), signal_all);
	else fp_cmp_f32(c, vfp_get_s(c, f->rd), vfp_get_s(c, f->rm), signal_all);
	return N2A32_OK;
}

static int vfp_cvt_sd(struct n2a32_cpu *c, const struct vfp_fields *f)
{
	if (f->sz == 0) {	/* F64.F32: Dd <- Sm */
		vfp_set_d(c, vfp_dreg(f->vd, f->d), fp_cvt_f32_to_f64(c, vfp_get_s(c, vfp_sreg(f->vm, f->m))));
	} else {		/* F32.F64: Sd <- Dm */
		vfp_set_s(c, vfp_sreg(f->vd, f->d), fp_cvt_f64_to_f32(c, vfp_get_d(c, vfp_dreg(f->vm, f->m))));
	}
	return N2A32_OK;
}

static int vfp_cvt_int_to_fp(struct n2a32_cpu *c, uint32_t insn, const struct vfp_fields *f)
{
	int is_signed = (int)((insn >> 7) & 1u);
	uint32_t raw = vfp_get_sb(c, vfp_sreg(f->vm, f->m));
	unsigned rd = vfp_reg(f->sz, f->vd, f->d);

	if (f->sz) vfp_set_d(c, rd, fp_cvt_i32_to_f64(c, raw, is_signed));
	else vfp_set_s(c, rd, fp_cvt_i32_to_f32(c, raw, is_signed));
	return N2A32_OK;
}

static int vfp_cvt_fp_to_int(struct n2a32_cpu *c, uint32_t insn, const struct vfp_fields *f)
{
	int is_signed = (int)((insn >> 16) & 1u);
	int rmode;
	uint32_t r;
	unsigned rd = vfp_sreg(f->vd, f->d);

	if ((insn >> 7) & 1u)
		rmode = N2AFP_RZ;
	else
		rmode = n2afp_fpscr_rmode(c->fpscr);
	if (f->sz) r = fp_cvt_f64_to_i32(c, vfp_get_d(c, vfp_dreg(f->vm, f->m)), is_signed, rmode);
	else r = fp_cvt_f32_to_i32(c, vfp_get_s(c, vfp_sreg(f->vm, f->m)), is_signed, rmode);
	vfp_set_sb(c, rd, r);
	return N2A32_OK;
}

static int vfp_cvt_fixed(struct n2a32_cpu *c, uint32_t insn, const struct vfp_fields *f)
{
	unsigned opc = (insn >> 16) & 0xFu;
	int to_fp = (opc & 4u) == 0;	/* A/B: fixed -> FP; E/F: FP -> fixed */
	int is_signed = ((opc & 1u) == 0);
	int bits32 = (int)((insn >> 7) & 1u);
	unsigned i = (insn >> 5) & 1u;
	unsigned imm4 = insn & 0xFu;
	int fbits = (bits32 ? 32 : 16) - (int)(imm4 * 2u + i);
	unsigned rd = vfp_reg(f->sz, f->vd, f->d);

	if (bits32 ? (fbits < 1 || fbits > 32) : (fbits < 0 || fbits > 16))
		return N2A32_UNDEF;

	if (to_fp) {
		uint32_t raw = fp_get_fixed_int(c, f->sz, rd, bits32);
		if (f->sz) vfp_set_d(c, rd, fp_cvt_fixed_to_f64(c, raw, fbits, bits32, is_signed));
		else vfp_set_s(c, rd, fp_cvt_fixed_to_f32(c, raw, fbits, bits32, is_signed));
	} else {
		uint32_t raw;
		if (f->sz) raw = fp_cvt_f64_to_fixed(c, vfp_get_d(c, rd), fbits, bits32, is_signed);
		else raw = fp_cvt_f32_to_fixed(c, vfp_get_s(c, rd), fbits, bits32, is_signed);
		if (f->sz) c->d[rd & 31u].u32[0] = raw;
		else vfp_set_sb(c, rd, raw);
	}
	return N2A32_OK;
}

static int vfp_vcvt_half(struct n2a32_cpu *c, uint32_t insn, const struct vfp_fields *f)
{
	unsigned opc = (insn >> 16) & 0xFu;
	int to_single = (opc == 2);	/* VCVT.F32.F16 else VCVT.F16.F32 */
	int top = (int)((insn >> 7) & 1u);
	unsigned rd = vfp_sreg(f->vd, f->d);
	unsigned rm = vfp_sreg(f->vm, f->m);

	if (f->sz != 0)
		return N2A32_UNDEF;
	if (to_single) {
		uint32_t raw = vfp_get_sb(c, rm);
		uint16_t h = (uint16_t)(top ? (raw >> 16) : raw);
		vfp_set_s(c, rd, fp_f16_to_f32(c, h));
	} else {
		/* VCVT.F16.F32 writes one half of Sd and leaves the other half of *Sd* unchanged. */
		uint32_t dst = vfp_get_sb(c, rd);
		uint16_t h = fp_f32_to_f16(c, vfp_get_s(c, rm));
		uint32_t out = top ? ((dst & 0x0000FFFFu) | ((uint32_t)h << 16))
				   : ((dst & 0xFFFF0000u) | h);
		vfp_set_sb(c, rd, out);
	}
	return N2A32_OK;
}

static int vfp_vrint_cond(struct n2a32_cpu *c, uint32_t insn, const struct vfp_fields *f, int kind)
{
	(void)insn;
	if (f->sz) vfp_set_d(c, f->rd, fp_rint_f64(c, vfp_get_d(c, f->rm), kind));
	else vfp_set_s(c, f->rd, fp_rint_f32(c, vfp_get_s(c, f->rm), kind));
	return N2A32_OK;
}

static int vfp_dp(struct n2a32_cpu *c, uint32_t insn)
{
	struct vfp_fields f;
	unsigned op1, op2, opc3, bit6, bit4;

	if ((insn & 0x0F000000u) != 0x0E000000u)
		return N2A32_UNDEF;
	bit4 = (insn >> 4) & 1u;

	/* VMRS/VMSR: 1110 1110/1111 0001 Rt 1010 0001 0000.  All other MRC/MCR cp10/cp11 -> UNDEF. */
	if (bit4) {
		uint32_t m = insn & 0x0FFFFFFFu;
		unsigned rt = (insn >> 12) & 0xFu;
		if ((m & 0x0FFF0FFFu) == 0x0EF10A10u) {		/* VMRS Rt/APSR_nzcv, FPSCR */
			if ((m & 0x000F0000u) != 0x00010000u)
				return N2A32_UNDEF;
			if (rt == 15) {
				c->n = (uint8_t)((c->fpscr >> 31) & 1u);
				c->z = (uint8_t)((c->fpscr >> 30) & 1u);
				c->cf = (uint8_t)((c->fpscr >> 29) & 1u);
				c->v = (uint8_t)((c->fpscr >> 28) & 1u);
			} else {
				c->r[rt] = c->fpscr;
			}
			return N2A32_OK;
		}
		if ((m & 0x0FFF0FFFu) == 0x0EE10A10u) {		/* VMSR FPSCR, Rt */
			if ((m & 0x000F0000u) != 0x00010000u || rt == 15)
				return N2A32_UNDEF;
			c->fpscr = c->r[rt] & FPSCR_WRITABLE;
			return N2A32_OK;
		}
		return N2A32_UNDEF;
	}

	if ((insn & 0x00000E00u) != 0x00000A00u)	/* bits 11-9 must be 101 */
		return N2A32_UNDEF;

	vfp_fields_get(insn, &f);
	op1 = (insn >> 23) & 1u;
	op2 = (insn >> 20) & 3u;
	opc3 = (insn >> 16) & 0xFu;
	bit6 = (insn >> 6) & 1u;

	if (op1 == 0) {
		unsigned op;
		switch (op2) {
		case 0: op = bit6 ? FPOP_MLS : FPOP_MLA; break;
		case 1: op = bit6 ? FPOP_NMLA : FPOP_NMLS; break;
		case 2: op = bit6 ? FPOP_NMUL : FPOP_MUL; break;
		default: op = bit6 ? FPOP_SUB : FPOP_ADD; break;
		}
		if (f.sz) fp_binop_f64(c, op, f.rd, f.rn, f.rm);
		else fp_binop_f32(c, op, f.rd, f.rn, f.rm);
		return N2A32_OK;
	}

	switch (op2) {
	case 0:
		if (bit6) return N2A32_UNDEF;
		if (f.sz) vfp_set_d(c, f.rd, fp_div_f64(c, vfp_get_d(c, f.rn), vfp_get_d(c, f.rm)));
		else vfp_set_s(c, f.rd, fp_div_f32(c, vfp_get_s(c, f.rn), vfp_get_s(c, f.rm)));
		return N2A32_OK;
	case 1:
		if (f.sz) fp_binop_f64(c, bit6 ? FPOP_FNMA : FPOP_FNMS, f.rd, f.rn, f.rm);
		else fp_binop_f32(c, bit6 ? FPOP_FNMA : FPOP_FNMS, f.rd, f.rn, f.rm);
		return N2A32_OK;
	case 2:
		if (f.sz) fp_binop_f64(c, bit6 ? FPOP_FMS : FPOP_FMA, f.rd, f.rn, f.rm);
		else fp_binop_f32(c, bit6 ? FPOP_FMS : FPOP_FMA, f.rd, f.rn, f.rm);
		return N2A32_OK;
	default:
		break;
	}

	/* op1=1, op2=3: unary/CVT/compare group.  VMOV immediate is bits 7-6 == 00. */
	if (bit6 == 0 && f.n == 0)
		return vfp_vmov_imm(c, insn, &f);
	if (bit6 == 0)
		return N2A32_UNDEF;

	switch (opc3) {
	case 0x0:
		if (f.n) return vfp_unary_absneg(c, &f, 1);
		return vfp_vmov_reg(c, &f);
	case 0x1:
		if (f.n) return vfp_sqrt(c, &f);
		return vfp_unary_absneg(c, &f, 0);
	case 0x2:
		return vfp_vcvt_half(c, insn, &f);
	case 0x3:
		return vfp_vcvt_half(c, insn, &f);
	case 0x4:
		return vfp_vcmp(c, insn, &f, 0);
	case 0x5:
		return vfp_vcmp(c, insn, &f, 1);
	case 0x6:
		return vfp_vrint_cond(c, insn, &f, f.n ? 5 : 6);	/* VRINTZ / VRINTR */
	case 0x7:
		if (f.n) return vfp_cvt_sd(c, &f);
		return vfp_vrint_cond(c, insn, &f, 4);		/* VRINTX */
	case 0x8:
		return vfp_cvt_int_to_fp(c, insn, &f);
	case 0xC:
	case 0xD:
		return vfp_cvt_fp_to_int(c, insn, &f);
	case 0xA:
	case 0xB:
	case 0xE:
	case 0xF:
		return vfp_cvt_fixed(c, insn, &f);
	default:
		return N2A32_UNDEF;
	}
}

/* ------------------------------------------------------------------ unconditional (ARMv8) space */

static int vfp_vsel(struct n2a32_cpu *c, uint32_t insn, const struct vfp_fields *f, unsigned cc)
{
	int take_n;

	(void)insn;
	/* VSEL selects on the APSR condition flags (standard ConditionPassed); compilers put a
	 * VMRS APSR_nzcv,FPSCR between a VFP compare and VSEL precisely because VSEL does not read
	 * FPSCR.NZCV directly. */
	switch (cc) {
	case 0: take_n = c->z != 0; break;		/* EQ */
	case 1: take_n = c->v != 0; break;		/* VS */
	case 2: take_n = c->n == c->v; break;		/* GE */
	default: take_n = c->z == 0 && c->n == c->v; break;	/* GT */
	}
	if (f->sz) vfp_set_db(c, f->rd, take_n ? vfp_get_db(c, f->rn) : vfp_get_db(c, f->rm));
	else vfp_set_sb(c, f->rd, take_n ? vfp_get_sb(c, f->rn) : vfp_get_sb(c, f->rm));
	return N2A32_OK;
}

static int vfp_vmaxnm(struct n2a32_cpu *c, uint32_t insn, const struct vfp_fields *f, int is_max)
{
	(void)insn;
	if (f->sz) vfp_set_d(c, f->rd, fp_maxnm_f64(c, vfp_get_d(c, f->rn), vfp_get_d(c, f->rm), is_max));
	else vfp_set_s(c, f->rd, fp_maxnm_f32(c, vfp_get_s(c, f->rn), vfp_get_s(c, f->rm), is_max));
	return N2A32_OK;
}

static int vfp_uncond(struct n2a32_cpu *c, uint32_t insn)
{
	struct vfp_fields f;
	unsigned op;

	if ((insn & 0x0F000000u) != 0x0E000000u)
		return N2A32_UNDEF;
	if ((insn & 0x00000E00u) != 0x00000A00u)
		return N2A32_UNDEF;
	if (insn & 0x10u)		/* every unconditional VFP data-processing op has bit 4 = 0 */
		return N2A32_UNDEF;
	vfp_fields_get(insn, &f);
	op = (insn >> 20) & 0xFu;

	if (((insn >> 23) & 1u) == 0) {
		/* VSEL: bits 23=0, 22=D, 21-20 = {EQ,VS,GE,GT} */
		unsigned cc = op & 3u;
		if ((insn >> 6) & 1u) return N2A32_UNDEF;
		return vfp_vsel(c, insn, &f, cc);
	}
	if ((op & 3u) == 0u) {
		/* VMAXNM/VMINNM: bits 23=1, 22=D, 21-20=00, bit 6 selects max/min */
		unsigned is_min = (insn >> 6) & 1u;
		return vfp_vmaxnm(c, insn, &f, !is_min);
	}
	if ((op & 3u) == 3u) {
		unsigned opc = (insn >> 16) & 0xFu;
		if (opc >= 8 && opc <= 0xB)
			return vfp_vrint_cond(c, insn, &f, (int)(opc - 8));	/* A,N,P,M */
		if (opc >= 0xC) {
			/* VCVT{A,N,P,M}.{S32,U32}.FP: opc 12..15, bit 6=1, bit 7 = signed */
			unsigned kind = opc - 0xCu;
			int is_signed = (int)((insn >> 7) & 1u);
			int rmode = (kind == 0) ? N2AFP_RA : (kind == 1) ? N2AFP_RN : (kind == 2) ? N2AFP_RP : N2AFP_RM;
			uint32_t r;
			unsigned rd = vfp_sreg(f.vd, f.d);

			if (((insn >> 6) & 1u) == 0) return N2A32_UNDEF;
			if (f.sz) r = fp_cvt_f64_to_i32(c, vfp_get_d(c, vfp_dreg(f.vm, f.m)), is_signed, rmode);
			else r = fp_cvt_f32_to_i32(c, vfp_get_s(c, vfp_sreg(f.vm, f.m)), is_signed, rmode);
			vfp_set_sb(c, rd, r);
			return N2A32_OK;
		}
	}
	return N2A32_UNDEF;
}

/* ------------------------------------------------------------------ load/store and transfers */

static int vfp_vldr_vstr(struct n2a32_cpu *c, uint32_t insn, unsigned P, unsigned U, unsigned D,
			 unsigned W, unsigned L, unsigned Rn, unsigned Vd, unsigned sz, unsigned imm8)
{
	uint32_t base, addr;
	unsigned rd = sz ? vfp_dreg(Vd, D) : vfp_sreg(Vd, D);

	(void)insn; (void)P; (void)W;
	if (Rn == 15 && !L)
		return N2A32_UNDEF;
	base = (Rn == 15) ? (c->r[15] & ~3u) : c->r[Rn];
	addr = U ? base + imm8 * 4u : base - imm8 * 4u;
	if (L) {
		if (sz) vfp_set_db(c, rd, n2a32_ld64(c, addr));
		else vfp_set_sb(c, rd, n2a32_ld32(c, addr));
	} else {
		if (sz) n2a32_st64(c, addr, vfp_get_db(c, rd));
		else n2a32_st32(c, addr, vfp_get_sb(c, rd));
	}
	return N2A32_OK;
}

static int vfp_vldm_vstm(struct n2a32_cpu *c, uint32_t insn, unsigned P, unsigned U, unsigned D,
			 unsigned W, unsigned L, unsigned Rn, unsigned Vd, unsigned sz, unsigned imm8)
{
	uint32_t total, start, addr;
	unsigned first = sz ? vfp_dreg(Vd, D) : vfp_sreg(Vd, D);
	unsigned n, i;
	int db, odd_extra = 0;

	(void)insn;
	if (Rn == 15 || imm8 == 0)
		return N2A32_UNDEF;
	total = imm8 * 4u;
	if (sz) {
		n = imm8 / 2u;
		odd_extra = (int)(imm8 & 1u);
		if (n == 0 && !odd_extra)
			return N2A32_UNDEF;
	} else {
		n = imm8;
		if (n > 32u)
			return N2A32_UNDEF;
	}
	if (P && !U && W) db = 1;			/* VLDMDB */
	else if (!P && U) db = 0;			/* IA */
	else if (!P && !U && W && !L) db = 1;		/* VSTMDB / VPUSH */
	else return N2A32_UNDEF;

	if (first + n > 32u)
		return N2A32_UNDEF;			/* register list wraps: UNPREDICTABLE */
	start = db ? c->r[Rn] - total : c->r[Rn];

	for (i = 0; i < n; i++) {
		unsigned reg = first + i;
		addr = start + i * (sz ? 8u : 4u);
		if (L) {
			if (sz) vfp_set_db(c, reg, n2a32_ld64(c, addr));
			else vfp_set_sb(c, reg, n2a32_ld32(c, addr));
		} else {
			if (sz) n2a32_st64(c, addr, vfp_get_db(c, reg));
			else n2a32_st32(c, addr, vfp_get_sb(c, reg));
		}
	}
	if (odd_extra) {			/* FLDMX/FSSTMX extra word: FPSCR (approximation) */
		addr = start + n * 8u;
		if (L) c->fpscr = n2a32_ld32(c, addr) & FPSCR_WRITABLE;
		else n2a32_st32(c, addr, c->fpscr);
	}
	if (W)
		c->r[Rn] = db ? start : start + total;
	return N2A32_OK;
}

static int vfp_vmov_core2(struct n2a32_cpu *c, uint32_t insn, unsigned op, unsigned Rt2,
			  unsigned Rt, unsigned cp, unsigned Vm, unsigned M)
{
	(void)insn;
	if (Rt == 15 || Rt2 == 15)
		return N2A32_UNDEF;
	if (cp == 0xBu) {			/* one double */
		unsigned rd = vfp_dreg(Vm, M);
		if (op == 0)
			vfp_set_db(c, rd, ((uint64_t)c->r[Rt2] << 32) | c->r[Rt]);
		else {
			uint64_t v = vfp_get_db(c, rd);
			c->r[Rt] = (uint32_t)v;
			c->r[Rt2] = (uint32_t)(v >> 32);
		}
		return N2A32_OK;
	}
	if (cp == 0xAu) {			/* two singles */
		unsigned rd = vfp_sreg(Vm, M);
		if (rd & 1u)
			return N2A32_UNDEF;	/* pair must be even */
		if (op == 0) {
			vfp_set_sb(c, rd, c->r[Rt]);
			vfp_set_sb(c, rd + 1u, c->r[Rt2]);
		} else {
			c->r[Rt] = vfp_get_sb(c, rd);
			c->r[Rt2] = vfp_get_sb(c, rd + 1u);
		}
		return N2A32_OK;
	}
	return N2A32_UNDEF;
}

static int vfp_vmov_core1(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned dir = (insn >> 20) & 1u;
	unsigned Vn = (insn >> 16) & 0xFu, N = (insn >> 7) & 1u;
	unsigned Rt = (insn >> 12) & 0xFu;
	unsigned s = vfp_sreg(Vn, N);

	if (Rt == 15)
		return N2A32_UNDEF;
	if (dir) c->r[Rt] = vfp_get_sb(c, s);
	else vfp_set_sb(c, s, c->r[Rt]);
	return N2A32_OK;
}

static int vfp_vmov_lane(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned dir = (insn >> 20) & 1u;
	unsigned uns = (insn >> 23) & 1u;
	unsigned is8 = (insn >> 22) & 1u;
	unsigned hi = (insn >> 21) & 1u;
	unsigned Vn = (insn >> 16) & 0xFu, N = (insn >> 7) & 1u;
	unsigned Rt = (insn >> 12) & 0xFu;
	unsigned b6 = (insn >> 6) & 1u, b5 = (insn >> 5) & 1u;
	unsigned reg = (N << 4) | Vn;
	unsigned lane;
	uint32_t raw;

	if (Rt == 15)
		return N2A32_UNDEF;
	if (is8) {
		lane = (hi << 2) | (b6 << 1) | b5;
	} else if (b5) {			/* 16-bit */
		lane = (hi << 1) | b6;
	} else {				/* 32-bit */
		if (b6) return N2A32_UNDEF;
		lane = hi;
		if (lane > 1u) return N2A32_UNDEF;
	}
	if (lane > 7u) return N2A32_UNDEF;

	if (dir) {
		if (is8) {
			raw = c->d[reg & 31u].u8[lane];
			if (uns) c->r[Rt] = raw;
			else c->r[Rt] = (uint32_t)(int32_t)(int8_t)raw;
		} else if (b5) {
			raw = c->d[reg & 31u].u16[lane & 3u];
			if (uns) c->r[Rt] = raw;
			else c->r[Rt] = (uint32_t)(int32_t)(int16_t)raw;
		} else {
			c->r[Rt] = c->d[reg & 31u].u32[lane & 1u];
		}
	} else {
		if (is8) c->d[reg & 31u].u8[lane] = (uint8_t)c->r[Rt];
		else if (b5) c->d[reg & 31u].u16[lane & 3u] = (uint16_t)c->r[Rt];
		else c->d[reg & 31u].u32[lane & 1u] = c->r[Rt];
	}
	return N2A32_OK;
}

static int vfp_vdup(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned q = (insn >> 21) & 1u;
	unsigned is8 = (insn >> 22) & 1u;
	unsigned is16 = (insn >> 5) & 1u;
	unsigned Vn = (insn >> 16) & 0xFu, N = (insn >> 7) & 1u;
	unsigned Rt = (insn >> 12) & 0xFu;
	unsigned reg = (N << 4) | Vn;
	uint32_t w;

	if (Rt == 15 || (is8 && is16) || ((insn >> 6) & 1u))
		return N2A32_UNDEF;
	if (q && (reg & 1u))
		return N2A32_UNDEF;
	if (is8) {
		uint8_t b = (uint8_t)c->r[Rt];
		w = (uint32_t)b * 0x01010101u;
	} else if (is16) {
		uint16_t h = (uint16_t)c->r[Rt];
		w = (uint32_t)h * 0x00010001u;
	} else {
		w = c->r[Rt];
	}
	c->d[reg & 31u].u32[0] = w;
	c->d[reg & 31u].u32[1] = w;
	if (q) {
		c->d[(reg + 1u) & 31u].u32[0] = w;
		c->d[(reg + 1u) & 31u].u32[1] = w;
	}
	return N2A32_OK;
}

static int vfp_ldst(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned P = (insn >> 24) & 1u, U = (insn >> 23) & 1u, D = (insn >> 22) & 1u;
	unsigned W = (insn >> 21) & 1u, L = (insn >> 20) & 1u;
	unsigned Rn = (insn >> 16) & 0xFu, Vd = (insn >> 12) & 0xFu;
	unsigned cp = (insn >> 8) & 0xFu, imm8 = insn & 0xFFu;
	unsigned sz;

	if (cp != 0xAu && cp != 0xBu)
		return N2A32_UNDEF;
	sz = cp & 1u;

	if (P && !W)
		return vfp_vldr_vstr(c, insn, P, U, D, W, L, Rn, Vd, sz, imm8);

	/* 64-bit transfers between two core registers and one/two FP registers. */
	if (!P && !U && D && !W)
		return vfp_vmov_core2(c, insn, L, Rn, Vd, cp, insn & 0xFu, (insn >> 5) & 1u);

	return vfp_vldm_vstm(c, insn, P, U, D, W, L, Rn, Vd, sz, imm8);
}

/* ------------------------------------------------------------------ entry point */

int n2a32_vfp(struct n2a32_cpu *c, uint32_t insn)
{
	fexcept_t saved_exc;
	int saved_round = fegetround();
	int rc;

	if (c == NULL || c->mem == NULL)
		return N2A32_UNDEF;
	fegetexceptflag(&saved_exc, FE_ALL_EXCEPT);

	if ((insn >> 28) == 0xFu) {
		rc = vfp_uncond(c, insn);
	} else if (((insn >> 25) & 7u) == 6u) {
		rc = vfp_ldst(c, insn);
	} else if (((insn >> 25) & 7u) == 7u) {
		/* transfer / data processing.  VMOV core<->single, VMOV lane and VDUP are
		 * MCR/MRC-shaped (bit 4 set) and must be recognised before the generic
		 * coprocessor-register path in vfp_dp(). */
		uint32_t m = insn & 0x0FFFFFFFu;
		if ((m & 0x0FE00F7Fu) == 0x0E000A10u) {
			rc = vfp_vmov_core1(c, insn);
		} else if ((m & 0x0F000F1Fu) == 0x0E000B10u) {
			if (((insn >> 23) & 1u) && (((insn >> 20) & 1u) == 0))
				rc = vfp_vdup(c, insn);
			else
				rc = vfp_vmov_lane(c, insn);
		} else {
			rc = vfp_dp(c, insn);
		}
	} else {
		rc = N2A32_UNDEF;
	}

	fesetexceptflag(&saved_exc, FE_ALL_EXCEPT);
	(void)fesetround(saved_round);
	return rc;
}
