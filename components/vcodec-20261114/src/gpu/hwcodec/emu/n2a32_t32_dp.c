/* SPDX-License-Identifier: MIT */
/*
 * n2a32_t32_dp.c -- 32-bit Thumb (T32) data-processing instructions.
 *
 * Owner: hw-v15 (package V15).  Implements n2a32_t32_dp() of include/n2a32.h.
 *
 * The 32-bit guest instruction word is  (first halfword << 16) | second halfword,
 * exactly as the core's fetcher builds it.  Four encoding regions belong to this
 * class; everything else returns N2A32_UNDEF.
 *
 *   A  (insn & 0xFE000000) == 0xEA000000
 *        Data-processing (shifted register) and its register form, 0xEA00-0xEBFF.
 *        first = 0b1110101 op(4) S Rn   (op is bits 8:5, so 0xEB carries op bit 3)
 *        second= imm3(3) Rd(4) imm2(2) type(2) Rm(4)
 *        op 0 AND, 1 BIC, 2 ORR/MOV, 3 ORN/MVN, 4 EOR, 6 PKH, 8 ADD,
 *        10 ADC, 11 SBC, 13 SUB, 14 RSB.  (5, 7, 9, 12, 15 are not assigned.)
 *
 *   B  (insn & 0xFA008000) == 0xF0000000
 *        Data-processing (modified immediate), 0xF000-0xF01F (i=0) and
 *        0xF400-0xF41F (i=1).  Bit 15 of the second halfword is 0 here; it is 1
 *        for every B<c>.W / B.W / BL / BLX encoding that shares the first
 *        halfword's bit pattern, which is what separates the two spaces.
 *        Same op numbers as A except that PKH does not exist (6 -> UNDEF).
 *
 *   C  (insn & 0xFA008000) == 0xF2000000
 *        Data-processing (plain binary immediate), 0xF200-0xF3FF (i=0) and
 *        0xF600-0xF7FF (i=1); second halfword bit 15 = 0 as in B.
 *        op5 = first<8:4>: 0 ADDW/ADR, 4 MOVW, 10 SUBW/ADR, 12 MOVT,
 *        16 SSAT, 18 SSAT16, 20 SBFX, 22 BFI/BFC, 24 USAT, 26 USAT16, 28 UBFX.
 *
 *   D  (insn & 0xFE000000) == 0xFA000000
 *        Data-processing (register), 0xFA00-0xFBFF.
 *        op1 = first<7:4>, op2 = second<7:4>, Rd = second<11:8>,
 *        Rm = second<3:0>, Rn = first<3:0>, second<15:12> = 0b1111.
 *
 * Conventions taken from the ARM ARM (ARMv7-A / ARMv8-A AArch32 pseudocode):
 *   - Shift_C(LSL/LSR/ASR) clamps the amount to 32; ROR reduces the amount
 *     modulo 32 after the "amount == 0 -> value, carry unchanged" case; RRX is
 *     the immediate ROR #0 encoding.
 *   - ThumbExpandImm_C: imm12<11:10> == 00 selects the replicated-byte forms,
 *     otherwise the value is ROR(0x80 | imm12<6:0>, imm12<11:7>).
 *   - Parallel add/subtract GE flags: for signed forms GE is the sign of the
 *     (unhalved, unsaturated) lane result; for unsigned forms GE is the carry
 *     out of an addition and the absence of a borrow for a subtraction.  The
 *     16-bit forms clear GE<3:2>.
 *   - Saturating instructions set the sticky Q flag when a lane saturates.
 *
 * UNPREDICTABLE encodings return N2A32_UNDEF (listed in STATUS.md).
 * No branch is taken: T32 data processing has no ALU write to PC, so r15 as Rd
 * is only ever the TST/TEQ/CMN/CMP alias and stays a compare.  No memory access.
 */
#include "n2a32.h"

/* ------------------------------------------------------------------ helpers */

static uint32_t ror32(uint32_t v, unsigned n)	/* 1 <= n <= 31 */
{
	return (v >> n) | (v << (32 - n));
}

/* Arithmetic right shift with a defined result (no shift of a negative value). */
static int64_t asr64(int64_t v, unsigned n)
{
	uint64_t u = (uint64_t)v;

	if (v < 0)
		return (int64_t)~(~u >> n);
	return (int64_t)(u >> n);
}

/* Sign-extend the low `bits` bits of v (1..32) without implementation-defined
 * conversions. */
static int64_t sext64(uint32_t v, unsigned bits)
{
	uint64_t u = (uint64_t)(v & ((bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1)));

	if (bits < 32 && (u & ((uint64_t)1 << (bits - 1))))
		u -= (uint64_t)1 << bits;
	return (int64_t)u;
}

/* Shift_C of the ARM ARM.  type: 0 LSL, 1 LSR, 2 ASR, 3 ROR.  The incoming
 * carry (used by RRX and returned unchanged for a zero amount) comes from and
 * goes to *carry.  Register shift amounts are 0..255; immediate ones 0..31 with
 * LSR #0/ASR #0 meaning #32, handled by shift_imm_c(). */
static uint32_t shift_c(uint32_t v, unsigned type, unsigned n, unsigned *carry)
{
	switch (type) {
	case 0:						/* LSL */
		if (n == 0)
			return v;
		if (n >= 32) {
			*carry = v & 1;
			return 0;
		}
		*carry = (v >> (32 - n)) & 1;
		return v << n;
	case 1:						/* LSR */
		if (n == 0)
			return v;
		if (n >= 32) {
			*carry = (v >> 31) & 1;
			return 0;
		}
		*carry = (v >> (n - 1)) & 1;
		return v >> n;
	case 2:						/* ASR */
		if (n == 0)
			return v;
		if (n >= 32) {
			*carry = (v >> 31) & 1;
			return (v >> 31) ? 0xFFFFFFFFu : 0u;
		}
		*carry = (v >> (n - 1)) & 1;
		return (v >> n) | ((v >> 31) ? (0xFFFFFFFFu << (32 - n)) : 0u);
	default:					/* ROR */
		if (n == 0)				/* no shift, carry kept */
			return v;
		{
			unsigned m = n & 31;
			uint32_t r = m ? ror32(v, m) : v;

			*carry = r >> 31;
			return r;
		}
	}
}

/* Immediate shift: imm5 == 0 means LSL #0, LSR #32, ASR #32, RRX. */
static uint32_t shift_imm_c(uint32_t v, unsigned type, unsigned imm5, unsigned *carry)
{
	if ((type == 1 || type == 2) && imm5 == 0)
		imm5 = 32;
	if (type == 3 && imm5 == 0) {			/* RRX */
		uint32_t r = (v >> 1) | ((*carry ? 1u : 0u) << 31);

		*carry = v & 1;
		return r;
	}
	return shift_c(v, type, imm5, carry);
}

/* ThumbExpandImm_C of the ARM ARM. */
static uint32_t expand_imm_c(uint32_t imm12, unsigned *carry)
{
	uint32_t b = imm12 & 0xFF;

	if ((imm12 & 0xC00) == 0) {
		switch ((imm12 >> 8) & 3) {
		case 0:
			return b;
		case 1:
			return b | (b << 16);
		case 2:
			return (b << 8) | (b << 24);
		default:
			return b * 0x01010101u;
		}
	}
	{
		uint32_t v = ror32(0x80u | (imm12 & 0x7Fu), (imm12 >> 7) & 0x1F);

		*carry = v >> 31;
		return v;
	}
}

static uint32_t sext(uint32_t v, unsigned bits)
{
	if (bits >= 32)
		return v;
	if (v & (1u << (bits - 1)))
		return v | ~((1u << bits) - 1);
	return v & ((1u << bits) - 1);
}

static uint32_t zext(uint32_t v, unsigned bits)
{
	if (bits >= 32)
		return v;
	return v & ((1u << bits) - 1);
}

/* Signed saturation to `bits` (1..32); returns the sign-extended 32-bit value. */
static uint32_t ssat_bits(int64_t v, unsigned bits, unsigned *sat)
{
	int64_t max = ((int64_t)1 << (bits - 1)) - 1;
	int64_t min = -((int64_t)1 << (bits - 1));

	if (v > max) {
		*sat = 1;
		return (uint32_t)max;
	}
	if (v < min) {
		*sat = 1;
		return (uint32_t)min;
	}
	return (uint32_t)(int32_t)v;
}

/* Unsigned saturation to `bits` (0..31). */
static uint32_t usat_bits(int64_t v, unsigned bits, unsigned *sat)
{
	int64_t max = ((int64_t)1 << bits) - 1;

	if (v > max) {
		*sat = 1;
		return (uint32_t)max;
	}
	if (v < 0) {
		*sat = 1;
		return 0;
	}
	return (uint32_t)v;
}

/* 32-bit signed saturation used by QADD/QSUB/QDADD/QDSUB. */
static uint32_t ssat32_value(int64_t v, unsigned *sat)
{
	if (v > 0x7FFFFFFF) {
		*sat = 1;
		return 0x7FFFFFFFu;
	}
	if (v < -0x80000000LL) {
		*sat = 1;
		return 0x80000000u;
	}
	return (uint32_t)(int32_t)v;
}

/* ------------------------------------------------------------------ region A */

/* op: 0 AND 1 BIC 2 ORR 3 ORN 4 EOR 6 PKH 8 ADD 10 ADC 11 SBC 13 SUB 14 RSB */
static int dp_shifted(struct n2a32_cpu *c, uint32_t first, uint32_t second)
{
	unsigned op = (first >> 5) & 0xF;
	unsigned s = (first >> 4) & 1;
	unsigned rn = first & 0xF;
	unsigned rd = (second >> 8) & 0xF;
	unsigned rm = second & 0xF;
	unsigned type = (second >> 4) & 3;
	unsigned imm5 = (((second >> 12) & 7) << 2) | ((second >> 6) & 3);
	unsigned carry = c->cf;
	unsigned cin = c->cf;		/* ADC/SBC carry-in: the C flag as it was */
	uint8_t co = 0, ov = 0;
	uint32_t a, b, res;

	if (rm == 15)
		return N2A32_UNDEF;

	if (op == 6) {					/* PKH */
		if (s || rn == 15 || rd == 15)
			return N2A32_UNDEF;
		if (type == 0) {			/* PKHBT: bottom of Rn,
							 * top of Rm << shift */
			b = shift_c(c->r[rm], 0, imm5, &carry);
			c->r[rd] = (c->r[rn] & 0xFFFFu) |
				   (b & 0xFFFF0000u);
			return N2A32_OK;
		}
		if (type == 2) {			/* PKHTB: top of Rn,
							 * bottom of Rm ASR shift
							 * (shift 0 = Rm) */
			b = imm5 ? shift_c(c->r[rm], 2, imm5, &carry)
				 : c->r[rm];
			c->r[rd] = (c->r[rn] & 0xFFFF0000u) |
				   (b & 0xFFFFu);
			return N2A32_OK;
		}
		return N2A32_UNDEF;			/* type 01/11 not assigned */
	}
	if (op != 0 && op != 1 && op != 2 && op != 3 && op != 4 &&
	    op != 8 && op != 10 && op != 11 && op != 13 && op != 14)
		return N2A32_UNDEF;

	/* MOV/MVN aliases are the only forms that allow Rn == 1111. */
	if (rn == 15 && op != 2 && op != 3)
		return N2A32_UNDEF;
	if (rd == 15) {
		/* Only the compare aliases may name r15 as Rd, and only with S. */
		if (!s)
			return N2A32_UNDEF;
		if (op != 0 && op != 4 && op != 8 && op != 13)
			return N2A32_UNDEF;
	}

	b = shift_imm_c(c->r[rm], type, imm5, &carry);
	a = (rn == 15) ? 0 : c->r[rn];		/* MOV/MVN have no first operand */

	switch (op) {
	case 0: res = a & b; break;
	case 1: res = a & ~b; break;
	case 2: res = (rn == 15) ? b : (a | b); break;
	case 3: res = (rn == 15) ? ~b : (a | ~b); break;
	case 4: res = a ^ b; break;
	case 8: res = n2a32_adc(a, b, 0, &co, &ov);
		goto arith;
	case 10: res = n2a32_adc(a, b, cin, &co, &ov);
		goto arith;
	case 11: res = n2a32_adc(a, ~b, cin, &co, &ov);
		goto arith;
	case 13: res = n2a32_adc(a, ~b, 1, &co, &ov);
		goto arith;
	default: res = n2a32_adc(b, ~a, 1, &co, &ov);
		goto arith;
	}

	if (rd != 15)
		c->r[rd] = res;
	if (s) {
		c->n = res >> 31;
		c->z = res == 0;
		c->cf = (uint8_t)carry;
	}
	return N2A32_OK;

arith:
	if (rd != 15)
		c->r[rd] = res;
	if (s) {
		c->n = res >> 31;
		c->z = res == 0;
		c->cf = co;
		c->v = ov;
	}
	return N2A32_OK;
}

/* ------------------------------------------------------------------ region B */

static int dp_modified_imm(struct n2a32_cpu *c, uint32_t first, uint32_t second)
{
	unsigned op = (first >> 5) & 0xF;
	unsigned s = (first >> 4) & 1;
	unsigned rn = first & 0xF;
	unsigned rd = (second >> 8) & 0xF;
	unsigned carry = c->cf;
	unsigned cin = c->cf;		/* ADC/SBC carry-in: the C flag as it was */
	uint32_t imm = expand_imm_c(((first >> 10) & 1) << 11 |
				    (((second >> 12) & 7) << 8) |
				    (second & 0xFF), &carry);
	uint32_t a, res;
	uint8_t co = 0, ov = 0;

	if (op != 0 && op != 1 && op != 2 && op != 3 && op != 4 &&
	    op != 8 && op != 10 && op != 11 && op != 13 && op != 14)
		return N2A32_UNDEF;
	if (rn == 15 && op != 2 && op != 3)
		return N2A32_UNDEF;
	if (rd == 15) {
		if (!s)
			return N2A32_UNDEF;
		if (op != 0 && op != 4 && op != 8 && op != 13)
			return N2A32_UNDEF;
	}

	a = (rn == 15) ? 0 : c->r[rn];
	switch (op) {
	case 0: res = a & imm; break;
	case 1: res = a & ~imm; break;
	case 2: res = (rn == 15) ? imm : (a | imm); break;
	case 3: res = (rn == 15) ? ~imm : (a | ~imm); break;
	case 4: res = a ^ imm; break;
	case 8: res = n2a32_adc(a, imm, 0, &co, &ov);
		goto arith;
	case 10: res = n2a32_adc(a, imm, cin, &co, &ov);
		goto arith;
	case 11: res = n2a32_adc(a, ~imm, cin, &co, &ov);
		goto arith;
	case 13: res = n2a32_adc(a, ~imm, 1, &co, &ov);
		goto arith;
	default: res = n2a32_adc(imm, ~a, 1, &co, &ov);
		goto arith;
	}

	if (rd != 15)
		c->r[rd] = res;
	if (s) {
		c->n = res >> 31;
		c->z = res == 0;
		c->cf = (uint8_t)carry;
	}
	return N2A32_OK;

arith:
	if (rd != 15)
		c->r[rd] = res;
	if (s) {
		c->n = res >> 31;
		c->z = res == 0;
		c->cf = co;
		c->v = ov;
	}
	return N2A32_OK;
}

/* ------------------------------------------------------------------ region C */

static int dp_plain_imm(struct n2a32_cpu *c, uint32_t first, uint32_t second)
{
	unsigned op5 = (first >> 4) & 0x1F;
	unsigned i = (first >> 10) & 1;
	unsigned rn = first & 0xF;
	unsigned rd = (second >> 8) & 0xF;
	unsigned imm3 = (second >> 12) & 7;
	unsigned imm8 = second & 0xFF;
	uint32_t imm12 = (i << 11) | (imm3 << 8) | imm8;
	uint32_t imm16 = ((first & 0xF) << 12) | (i << 11) | (imm3 << 8) | imm8;
	unsigned lsb = (imm3 << 2) | ((second >> 6) & 3);

	if (rd == 15)
		return N2A32_UNDEF;

	switch (op5) {
	case 0:						/* ADDW / ADR T3 */
		if (rn == 15)
			c->r[rd] = (c->r[15] & ~3u) + imm12;
		else
			c->r[rd] = c->r[rn] + imm12;
		return N2A32_OK;
	case 10:					/* SUBW / ADR T2 */
		if (rn == 15)
			c->r[rd] = (c->r[15] & ~3u) - imm12;
		else
			c->r[rd] = c->r[rn] - imm12;
		return N2A32_OK;
	case 4:						/* MOVW (Rn field = imm4) */
		c->r[rd] = imm16;
		return N2A32_OK;
	case 12:					/* MOVT (Rn field = imm4) */
		c->r[rd] = (c->r[rd] & 0xFFFFu) | (imm16 << 16);
		return N2A32_OK;
	case 16:					/* SSAT with LSL */
	case 18:					/* SSAT with ASR / SSAT16 */
	case 24:					/* USAT with LSL */
	case 26: {					/* USAT with ASR / USAT16 */
		unsigned unsignedp = op5 >= 24;
		unsigned sh2 = (first >> 4) & 3;	/* 00 LSL, 10 ASR */
		unsigned satv = second & 0x1F;
		unsigned sat = 0;
		unsigned carry = c->cf;

		if (rn == 15)
			return N2A32_UNDEF;
		/* bit 5 of the first halfword with a zero shift field selects
		 * the 16-bit (per-halfword) variants. */
		if (sh2 == 2 && lsb == 0) {
			uint32_t r = 0;
			unsigned lane;

			if (satv > 15)
				return N2A32_UNDEF;
			for (lane = 0; lane < 2; lane++) {
				uint32_t v = (c->r[rn] >> (16 * lane)) & 0xFFFFu;
				/* USAT16 saturates the unsigned halfword value,
				 * SSAT16 the signed one. */
				uint32_t o = unsignedp
					? usat_bits((int32_t)v, satv, &sat)
					: ssat_bits((int32_t)(int16_t)v, satv + 1, &sat);

				r |= (o & 0xFFFFu) << (16 * lane);
			}
			c->r[rd] = r;
		} else {
			uint32_t shifted;

			if (sh2 == 0)
				shifted = shift_c(c->r[rn], 0, lsb, &carry);
			else if (sh2 == 2)
				shifted = shift_c(c->r[rn], 2, lsb, &carry);
			else
				return N2A32_UNDEF;
			if (unsignedp)
				c->r[rd] = usat_bits((int32_t)shifted, satv, &sat);
			else
				c->r[rd] = ssat_bits((int32_t)shifted, satv + 1, &sat);
		}
		if (sat)
			c->q = 1;
		return N2A32_OK;
	}
	case 20:					/* SBFX */
	case 28: {					/* UBFX */
		unsigned width = (second & 0x1F) + 1;
		uint32_t v;

		if (rn == 15 || lsb + width > 32)
			return N2A32_UNDEF;
		v = (c->r[rn] >> lsb);
		if (width < 32)
			v &= (1u << width) - 1;
		c->r[rd] = (op5 == 20) ? sext(v, width) : v;
		return N2A32_OK;
	}
	case 22: {					/* BFI / BFC */
		unsigned msb = second & 0x1F;
		unsigned width;
		uint32_t mask, src, v;

		if (msb < lsb)
			return N2A32_UNDEF;
		width = msb - lsb + 1;
		mask = (width >= 32) ? 0xFFFFFFFFu
				     : (((1u << width) - 1) << lsb);
		src = (rn == 15) ? 0 : (c->r[rn] << lsb);
		v = c->r[rd];
		c->r[rd] = (v & ~mask) | (src & mask);
		return N2A32_OK;
	}
	default:
		return N2A32_UNDEF;
	}
}

/* ------------------------------------------------------------------ region D */

/* Extend (SXTB/SXTH/UXTB/UXTH/SXTB16/UXTB16 and the accumulating forms).
 * op1: 0 SXTH, 1 UXTH, 2 SXTB16, 3 UXTB16, 4 SXTB, 5 UXTB. */
static int extend_op(struct n2a32_cpu *c, unsigned op1, unsigned rn, unsigned rd,
		     unsigned rm, unsigned rot)
{
	uint32_t v = c->r[rm];
	uint32_t o;

	if (rot)
		v = ror32(v, rot * 8);
	switch (op1) {
	case 0: o = sext(v, 16); break;
	case 1: o = zext(v, 16); break;
	case 2: o = (sext(v, 8) & 0xFFFFu) | (sext(v >> 16, 8) << 16); break;
	case 3: o = (v & 0xFFu) | ((v >> 16) & 0xFFu) << 16; break;
	case 4: o = sext(v, 8); break;
	default: o = zext(v, 8); break;
	}
	if (rn == 15) {
		c->r[rd] = o;
		return N2A32_OK;
	}
	if (op1 == 2 || op1 == 3) {			/* per-halfword add */
		uint32_t lo = (c->r[rn] & 0xFFFFu) + (o & 0xFFFFu);
		uint32_t hi = (c->r[rn] >> 16) + (o >> 16);

		c->r[rd] = (lo & 0xFFFFu) | (hi << 16);
	} else {
		c->r[rd] = c->r[rn] + o;
	}
	return N2A32_OK;
}

/* Parallel add/subtract.  shape: 8 ADD8, 9 ADD16, 10 ASX, 12 SUB8, 13 SUB16,
 * 14 SAX.  variant: 0 S, 1 Q, 2 SH, 4 U, 5 UQ, 6 UH. */
static int parallel_op(struct n2a32_cpu *c, unsigned shape, unsigned variant,
		       unsigned rn, unsigned rd, unsigned rm)
{
	uint32_t a = c->r[rn], b = c->r[rm], out = 0;
	const unsigned unsignedp = (variant >= 4);
	const unsigned do_sat = (variant == 1 || variant == 5);
	const unsigned do_half = (variant == 2 || variant == 6);
	const unsigned lanes = (shape == 8 || shape == 12) ? 4 : 2;
	const unsigned width = lanes == 4 ? 8 : 16;
	const int64_t lane_max = (int64_t)((1u << width) - 1);
	unsigned ge = 0, lane, sat = 0;

	for (lane = 0; lane < lanes; lane++) {
		unsigned ash = lane * width, bsh = lane * width;
		int sub;
		int64_t av, bv, r;

		if (lanes == 2 && (shape == 10 || shape == 14)) {
			ash = lane ? 16 : 0;
			bsh = lane ? 0 : 16;
			sub = (shape == 10) ? (lane ? 1 : 0) : (lane ? 0 : 1);
		} else {
			sub = (shape == 12 || shape == 13);
		}
		if (unsignedp) {
			av = (int64_t)((a >> ash) & (uint32_t)lane_max);
			bv = (int64_t)((b >> bsh) & (uint32_t)lane_max);
		} else {
			av = sext64(a >> ash, width);
			bv = sext64(b >> bsh, width);
		}
		r = sub ? av - bv : av + bv;

		/* Byte lanes take one GE bit each; a halfword lane drives a
		 * two-bit GE field (GE<1:0> for the low, GE<3:2> for the high
		 * halfword), both bits equal. */
		if (lanes == 4)
			ge |= (unsigned)(unsignedp
					 ? (sub ? (r >= 0) : (r > lane_max))
					 : (r >= 0)) << lane;
		else
			ge |= (unsigned)((unsignedp
					  ? (sub ? (r >= 0) : (r > lane_max))
					  : (r >= 0)) ? 3u : 0u) << (2 * lane);

		if (do_sat) {
			if (unsignedp) {
				if (r > lane_max) {
					r = lane_max;
					sat = 1;
				} else if (r < 0) {
					r = 0;
					sat = 1;
				}
			} else {
				int64_t max = (((int64_t)1) << (width - 1)) - 1;
				int64_t min = -(((int64_t)1) << (width - 1));

				if (r > max) {
					r = max;
					sat = 1;
				} else if (r < min) {
					r = min;
					sat = 1;
				}
			}
		} else if (do_half) {
			r = asr64(r, 1);
		}
		out |= ((uint32_t)r & (uint32_t)lane_max) << (lane * width);
	}

	c->r[rd] = out;
	c->ge = (uint8_t)(ge & 0xF);
	if (sat)
		c->q = 1;
	return N2A32_OK;
}

/* QADD/QSUB/QDADD/QDSUB.  The encoding puts Rn in first<3:0> and Rm in
 * second<3:0>; the assembly syntax is QADD <Rd>, <Rm>, <Rn>. */
static int qadd_op(struct n2a32_cpu *c, unsigned op, unsigned rn, unsigned rd, unsigned rm)
{
	int64_t x = (int32_t)c->r[rm];			/* Rm */
	int64_t y = (int32_t)c->r[rn];			/* Rn */
	unsigned sat = 0;

	switch (op) {
	case 8:						/* QADD: Rm + Rn */
		c->r[rd] = ssat32_value(x + y, &sat);
		break;
	case 10:					/* QSUB: Rm - Rn */
		c->r[rd] = ssat32_value(x - y, &sat);
		break;
	case 9: {					/* QDADD: Rm + Sat(2*Rn) */
		unsigned s2 = 0;
		uint32_t d = ssat32_value(2 * y, &s2);

		c->r[rd] = ssat32_value(x + (int32_t)d, &sat);
		sat |= s2;
		break;
	}
	default: {					/* QDSUB: Rm - Sat(2*Rn) */
		unsigned s2 = 0;
		uint32_t d = ssat32_value(2 * y, &s2);

		c->r[rd] = ssat32_value(x - (int32_t)d, &sat);
		sat |= s2;
		break;
	}
	}
	if (sat)
		c->q = 1;
	return N2A32_OK;
}

/* Absolute difference of the four bytes, for USAD8/USADA8. */
static uint32_t usad8(uint32_t a, uint32_t b)
{
	uint32_t sum = 0;
	unsigned k;

	for (k = 0; k < 4; k++) {
		int32_t x = (int32_t)((a >> (8 * k)) & 0xFF);
		int32_t y = (int32_t)((b >> (8 * k)) & 0xFF);

		sum += (uint32_t)(x > y ? x - y : y - x);
	}
	return sum;
}

/* Multiply / multiply-accumulate / divide / USAD8, 0xFB00 space. */
static int mul_op(struct n2a32_cpu *c, uint32_t first, uint32_t second)
{
	unsigned op1 = (first >> 4) & 0xF;
	unsigned rn = first & 0xF;
	unsigned ra = (second >> 12) & 0xF;
	unsigned rd = (second >> 8) & 0xF;
	unsigned op2 = (second >> 4) & 0xF;
	unsigned rm = second & 0xF;
	uint32_t a = c->r[rn], b = c->r[rm];

	if (rn == 15 || rm == 15)
		return N2A32_UNDEF;

	switch (op1) {
	case 0:						/* MUL / MLA / MLS */
		if (rd == 15)
			return N2A32_UNDEF;
		if (op2 == 0) {
			uint32_t p = a * b;

			if (ra == 15)
				c->r[rd] = p;		/* MUL */
			else
				c->r[rd] = c->r[ra] + p;	/* MLA */
			return N2A32_OK;
		}
		if (op2 == 1) {				/* MLS */
			if (ra == 15)
				return N2A32_UNDEF;
			c->r[rd] = c->r[ra] - a * b;
			return N2A32_OK;
		}
		return N2A32_UNDEF;
	case 1:						/* SMULxy / SMLAxy */
		if (rd == 15 || op2 > 3)
			return N2A32_UNDEF;
		{
			int32_t x = (int32_t)(int16_t)((op2 & 2) ? (a >> 16) : a);
			int32_t y = (int32_t)(int16_t)((op2 & 1) ? (b >> 16) : b);
			uint32_t p = (uint32_t)(x * y);

			if (ra == 15)
				c->r[rd] = p;
			else
				c->r[rd] = c->r[ra] + p;
		}
		return N2A32_OK;
	case 2:						/* SMUAD(X) / SMLAD(X) */
	case 4: {					/* SMUSD(X) / SMLSD(X) */
		int64_t p0, p1, sum;
		int64_t res;
		uint32_t base;

		if (rd == 15 || op2 > 1)
			return N2A32_UNDEF;
		p0 = (int64_t)(int32_t)(int16_t)a *
		     (int32_t)(int16_t)(op2 ? (b >> 16) : b);
		p1 = (int64_t)(int32_t)(int16_t)(a >> 16) *
		     (int32_t)(int16_t)(op2 ? b : (b >> 16));
		sum = (op1 == 2) ? p0 + p1 : p0 - p1;
		base = (ra == 15) ? 0 : c->r[ra];
		res = (int64_t)(int32_t)base + sum;
		c->r[rd] = (uint32_t)res;
		if (res != (int64_t)(int32_t)(uint32_t)res)
			c->q = 1;
		return N2A32_OK;
	}
	case 3: {					/* SMULW<y> / SMLAW<y> */
		int32_t y;
		uint32_t p;

		if (rd == 15 || op2 > 1)
			return N2A32_UNDEF;
		y = (int32_t)(int16_t)(op2 ? (b >> 16) : b);
		p = (uint32_t)asr64((int64_t)(int32_t)a * y, 16);
		if (ra == 15)
			c->r[rd] = p;
		else
			c->r[rd] = c->r[ra] + p;
		return N2A32_OK;
	}
	case 5:						/* SMMUL(R) / SMMLA(R) */
	case 6: {					/* SMMLS(R) */
		int64_t p = (int64_t)(int32_t)a * (int32_t)b;
		uint32_t hi;
		int64_t res;

		if (rd == 15 || op2 > 1)
			return N2A32_UNDEF;
		if (op2 == 1)
			p += 0x80000000LL;
		hi = (uint32_t)((uint64_t)p >> 32);
		if (op1 == 5 && ra == 15) {		/* SMMUL / SMMULR */
			c->r[rd] = hi;
			return N2A32_OK;
		}
		if (ra == 15)
			return N2A32_UNDEF;
		res = (int64_t)(int32_t)c->r[ra] +
		      (op1 == 5 ? (int64_t)(int32_t)hi : -(int64_t)(int32_t)hi);
		c->r[rd] = (uint32_t)res;
		if (res != (int64_t)(int32_t)(uint32_t)res)
			c->q = 1;
		return N2A32_OK;
	}
	case 7:						/* USAD8 / USADA8 */
		if (rd == 15 || op2 != 0)
			return N2A32_UNDEF;
		if (ra == 15)
			c->r[rd] = usad8(a, b);
		else
			c->r[rd] = c->r[ra] + usad8(a, b);
		return N2A32_OK;
	case 8:						/* SMULL */
	case 10:					/* UMULL */
		if (op2 != 0 || rd == 15 || ra == 15 || rd == ra)
			return N2A32_UNDEF;
		{
			uint64_t p = (op1 == 8)
				? (uint64_t)((int64_t)(int32_t)a * (int32_t)b)
				: (uint64_t)a * b;

			c->r[ra] = (uint32_t)p;		/* RdLo */
			c->r[rd] = (uint32_t)(p >> 32);	/* RdHi */
		}
		return N2A32_OK;
	case 9:						/* SDIV */
	case 11:					/* UDIV */
		if (ra != 15 || op2 != 0xF || rd == 15)
			return N2A32_UNDEF;
		if (op1 == 9) {
			if (b == 0)
				c->r[rd] = 0;
			else if (a == 0x80000000u && b == 0xFFFFFFFFu)
				c->r[rd] = 0x80000000u;
			else
				c->r[rd] = (uint32_t)((int32_t)a / (int32_t)b);
		} else {
			c->r[rd] = b ? a / b : 0;
		}
		return N2A32_OK;
	case 12:					/* SMLAL / SMLALxy / SMLALD(X) */
	case 13:					/* SMLSLD(X) */
		if (rd == 15 || ra == 15 || rd == ra)
			return N2A32_UNDEF;
		if (op2 == 0 && op1 == 12) {		/* SMLAL */
			uint64_t acc = ((uint64_t)c->r[rd] << 32) | c->r[ra];

			acc += (uint64_t)((int64_t)(int32_t)a * (int32_t)b);
			c->r[ra] = (uint32_t)acc;
			c->r[rd] = (uint32_t)(acc >> 32);
			return N2A32_OK;
		}
		if (op1 == 12 && op2 >= 8 && op2 <= 11) {	/* SMLALxy */
			int32_t x = (int32_t)(int16_t)((op2 & 2) ? (a >> 16) : a);
			int32_t y = (int32_t)(int16_t)((op2 & 1) ? (b >> 16) : b);
			uint64_t acc = ((uint64_t)c->r[rd] << 32) | c->r[ra];

			acc += (uint64_t)((int64_t)x * y);
			c->r[ra] = (uint32_t)acc;
			c->r[rd] = (uint32_t)(acc >> 32);
			return N2A32_OK;
		}
		if (op2 == 12 || op2 == 13) {		/* SMLALD(X) / SMLSLD(X) */
			int64_t p0 = (int64_t)(int32_t)(int16_t)a *
				     (int32_t)(int16_t)(op2 == 13 ? (b >> 16) : b);
			int64_t p1 = (int64_t)(int32_t)(int16_t)(a >> 16) *
				     (int32_t)(int16_t)(op2 == 13 ? b : (b >> 16));
			uint64_t acc = ((uint64_t)c->r[rd] << 32) | c->r[ra];

			acc += (uint64_t)((op1 == 12) ? p0 + p1 : p0 - p1);
			c->r[ra] = (uint32_t)acc;
			c->r[rd] = (uint32_t)(acc >> 32);
			return N2A32_OK;
		}
		return N2A32_UNDEF;
	case 14:					/* UMLAL / UMAAL */
		if (rd == 15 || ra == 15 || rd == ra)
			return N2A32_UNDEF;
		if (op2 == 0) {				/* UMLAL */
			uint64_t acc = ((uint64_t)c->r[rd] << 32) | c->r[ra];

			acc += (uint64_t)a * b;
			c->r[ra] = (uint32_t)acc;
			c->r[rd] = (uint32_t)(acc >> 32);
			return N2A32_OK;
		}
		if (op2 == 6) {				/* UMAAL */
			uint64_t acc = (uint64_t)a * b + c->r[ra] + c->r[rd];

			c->r[ra] = (uint32_t)acc;
			c->r[rd] = (uint32_t)(acc >> 32);
			return N2A32_OK;
		}
		return N2A32_UNDEF;
	default:
		return N2A32_UNDEF;
	}
}

/* 0xFA00 space (data processing register). */
static int dp_register(struct n2a32_cpu *c, uint32_t first, uint32_t second)
{
	unsigned op1 = (first >> 4) & 0xF;
	unsigned rn = first & 0xF;
	unsigned rd = (second >> 8) & 0xF;
	unsigned op2 = (second >> 4) & 0xF;
	unsigned rm = second & 0xF;

	if ((second >> 12) != 0xF)			/* fixed 1111 field */
		return N2A32_UNDEF;
	if (rd == 15)
		return N2A32_UNDEF;

	if (op1 <= 7 && op2 == 0) {			/* shift by register */
		unsigned type = op1 >> 1;
		unsigned s = op1 & 1;
		unsigned carry = c->cf;
		uint32_t res;

		if (rn == 15 || rm == 15)
			return N2A32_UNDEF;
		res = shift_c(c->r[rn], type, c->r[rm] & 0xFF, &carry);
		c->r[rd] = res;
		if (s) {
			c->n = res >> 31;
			c->z = res == 0;
			c->cf = (uint8_t)carry;
		}
		return N2A32_OK;
	}
	if (op1 <= 5 && op2 >= 8 && op2 <= 11) {	/* extend */
		if (rm == 15)
			return N2A32_UNDEF;
		return extend_op(c, op1, rn, rd, rm, op2 & 3);
	}
	if ((op1 == 8 || op1 == 9 || op1 == 10 ||
	     op1 == 12 || op1 == 13 || op1 == 14) &&
	    (op2 == 0 || op2 == 1 || op2 == 2 || op2 == 4 || op2 == 5 || op2 == 6)) {
		if (rn == 15 || rm == 15)
			return N2A32_UNDEF;
		return parallel_op(c, op1, op2, rn, rd, rm);
	}
	if (op1 == 8 && op2 >= 8 && op2 <= 11) {	/* QADD family */
		if (rn == 15 || rm == 15)
			return N2A32_UNDEF;
		return qadd_op(c, op2, rn, rd, rm);
	}
	if (op1 == 9 && op2 >= 8 && op2 <= 11) {	/* REV / REV16 / RBIT / REVSH */
		/* These single-source encodings put Rm in both register fields.
		 * Toolchains agree on the low nibble; if an encoder wrote 1111
		 * there but a register in first<3:0>, use that instead. */
		unsigned src = (rm != 15) ? rm : rn;
		uint32_t v;

		if (src == 15)
			return N2A32_UNDEF;
		v = c->r[src];
		switch (op2) {
		case 8:					/* REV */
			c->r[rd] = (v >> 24) | ((v >> 8) & 0xFF00u) |
				   ((v << 8) & 0xFF0000u) | (v << 24);
			break;
		case 9:					/* REV16 */
			c->r[rd] = ((v >> 8) & 0x00FF00FFu) |
				   ((v << 8) & 0xFF00FF00u);
			break;
		case 10: {				/* RBIT */
			uint32_t r = 0;
			unsigned k;

			for (k = 0; k < 32; k++)
				r |= ((v >> k) & 1) << (31 - k);
			c->r[rd] = r;
			break;
		}
		default: {				/* REVSH */
			uint32_t lo = ((v >> 8) & 0xFFu) | ((v & 0xFFu) << 8);

			c->r[rd] = sext(lo, 16);
			break;
		}
		}
		return N2A32_OK;
	}
	if (op1 == 10 && op2 == 8) {			/* SEL */
		uint32_t a = c->r[rn], b = c->r[rm], r = 0;
		unsigned k;

		if (rn == 15 || rm == 15)
			return N2A32_UNDEF;
		for (k = 0; k < 4; k++) {
			uint32_t src = ((c->ge >> k) & 1) ? a : b;

			r |= ((src >> (8 * k)) & 0xFFu) << (8 * k);
		}
		c->r[rd] = r;
		return N2A32_OK;
	}
	if (op1 == 11 && op2 == 8) {			/* CLZ */
		unsigned src = (rm != 15) ? rm : rn;
		uint32_t v;

		if (src == 15)
			return N2A32_UNDEF;
		v = c->r[src];
		{
			unsigned n = 0;

			while (n < 32 && ((v >> (31 - n)) & 1) == 0)
				n++;
			c->r[rd] = n;
		}
		return N2A32_OK;
	}
	return N2A32_UNDEF;
}

/* --------------------------------------------------------------------- entry */

int n2a32_t32_dp(struct n2a32_cpu *c, uint32_t insn)
{
	uint32_t first = insn >> 16;
	uint32_t second = insn & 0xFFFFu;

	if ((insn & 0xFE000000u) == 0xEA000000u)
		return dp_shifted(c, first, second);
	if ((insn & 0xFA008000u) == 0xF0000000u)
		return dp_modified_imm(c, first, second);
	if ((insn & 0xFA008000u) == 0xF2000000u)
		return dp_plain_imm(c, first, second);
	if ((insn & 0xFE000000u) == 0xFA000000u) {
		if (first & 0x100)			/* 0xFB00: multiply/divide */
			return mul_op(c, first, second);
		return dp_register(c, first, second);
	}
	return N2A32_UNDEF;
}
