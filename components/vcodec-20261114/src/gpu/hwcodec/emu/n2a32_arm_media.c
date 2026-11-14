/* SPDX-License-Identifier: MIT */
/*
 * n2a32_arm_media.c -- A32 "media" instruction class of the n2a32 AArch32 interpreter.
 *
 * Package V11.  Implements n2a32_arm_media() exactly as specified by include/n2a32.h:
 * the core has already evaluated the condition, set c->r[15] (= instruction address + 8),
 * c->next_pc (= address of the following instruction) and c->in_it.  This file returns
 * N2A32_OK or N2A32_UNDEF, never branches, never touches guest memory, never prints and
 * never aborts.
 *
 * Instruction set covered (ARMv7-A A32, "media" encoding space A5.4 plus the four
 * media patterns of the 0001 data-processing space):
 *
 *   extend       SXTB SXTH UXTB UXTH [ROR #0/8/16/24]; SXTAB SXTAH UXTAB UXTAH;
 *                SXTB16 SXTAB16 UXTB16 UXTAB16
 *   bit field    UBFX SBFX BFI BFC
 *   bytes        REV REV16 REVSH RBIT
 *   saturate     SSAT USAT (LSL/ASR shift), SSAT16 USAT16
 *   pack/select  PKHBT PKHTB SEL
 *   parallel     SADD16 SASX SSAX SSUB16 SADD8 SSUB8
 *                QADD16 QASX QSAX QSUB16 QADD8 QSUB8
 *                SHADD16 SHASX SHSAX SHSUB16 SHADD8 SHSUB8
 *                UADD16 UASX USAX USUB16 UADD8 USUB8
 *                UQADD16 UQASX UQSAX UQSUB16 UQADD8 UQSUB8
 *                UHADD16 UHASX UHSAX UHSUB16 UHADD8 UHSUB8
 *   saturating   QADD QSUB QDADD QDSUB
 *   multiply     SMULBB/BT/TB/TT, SMLABB/BT/TB/TT, SMULWB/WT, SMLAW B/WT,
 *                SMLALBB/BT/TB/TT, SMUAD(X) SMUSD(X) SMLAD(X) SMLSD(X),
 *                SMLALD(X) SMLSLD(X), SMMUL(R) SMMLA(R) SMMLS(R)
 *                (the X forms exchange the second operand's halfwords: product1 =
 *                Rn<15:0> * Rm<31:16>, product2 = Rn<31:16> * Rm<15:0>, and the
 *                subtract forms compute product1 - product2, per the Arm ARM)
 *   sum of abs   USAD8 USADA8
 *   divide       SDIV UDIV (division by zero yields 0)
 *
 * Flag rules used (from the ARMv7-A ARM ARM A7.4/A8.8 and the Arm A32/T32 instruction
 * set reference):
 *   - The S and U parallel variants set GE[3:0]; for halfword variants GE[1:0] move
 *     together and GE[3:2] move together.  GE is 1 when the exact (untruncated) signed
 *     sum is >= 0 for the S variants, on carry out for U additions and on no-borrow for
 *     U subtractions.
 *   - The Q, SH, UQ and UH parallel variants change no flags at all (the ARM ARM:
 *     "The Q flag is not affected even if this operation saturates"); in particular they
 *     leave Q and GE alone.
 *   - SSAT/USAT/SSAT16/USAT16 and QADD/QSUB/QDADD/QDSUB set Q (sticky) on saturation.
 *   - SMUAD/SMUADX, SMLAD/SMLADX, SMLSD/SMLSDX, SMLAxy, SMLAWy set Q when the exact
 *     result does not fit in 32 bits.  SMUSD/SMUSDX do not set Q: they cannot overflow
 *     (the difference of the two products is in (-2^31, 2^31); the Arm A32/T32
 *     instruction reference lists the Q rule for SMUAD/SMUADX only).  SMLSD/SMLSDX
 *     can overflow because of the accumulator and do set Q.
 *   - SMULxy, SMULWy, SMLALxy, SMLALD/SMLSLD, SMMUL/SMMLA/SMMLS, USAD8/USADA8,
 *     SDIV/UDIV, the extends, REV/RBIT, UBFX/SBFX/BFI/BFC, PKH, SEL set no flags.
 *   - No instruction of this class touches N, Z, C or V.
 *
 * Decoding of every group was cross-checked against llvm-mc --show-encoding (see
 * test_v11/v11_enc.s and test_v11/run_tests.sh); the bit layouts below are the ARM ARM
 * encoding diagrams.  Encodings this file deliberately rejects with N2A32_UNDEF are
 * listed in test_v11/STATUS.md.
 */
#include "n2a32.h"

#define N2_S32_MIN (-2147483648LL)
#define N2_S32_MAX (2147483647LL)

/* ------------------------------------------------------------------------------------ */
/* small helpers                                                                        */
/* ------------------------------------------------------------------------------------ */

static inline uint32_t mask_bits(unsigned bits)
{
	return bits >= 32u ? 0xffffffffu : ((1u << bits) - 1u);
}

/* Sign-extend the low `bits` bits of v to a 64-bit signed value.  No implementation-
 * defined conversions: the value is built with int64 arithmetic. */
static inline int64_t sx_bits(uint32_t v, unsigned bits)
{
	uint32_t m = (bits >= 32u) ? 0x80000000u : (1u << (bits - 1u));
	uint32_t x = v & mask_bits(bits);

	return (int64_t)x - ((x & m) ? ((int64_t)1 << bits) : 0);
}

static inline uint32_t ror32(uint32_t v, unsigned n)
{
	n &= 31u;
	return n ? ((v >> n) | (v << (32u - n))) : v;
}

/* arithmetic shift right, n in 1..32 */
static inline uint32_t asr32(uint32_t v, unsigned n)
{
	if (n >= 32u)
		return 0u - (v >> 31);
	return (v >> n) | ((0u - (v >> 31)) << (32u - n));
}

static inline uint32_t rev32(uint32_t v)
{
	return (v >> 24) | ((v >> 8) & 0x0000ff00u) | ((v << 8) & 0x00ff0000u) | (v << 24);
}

/* swap the bytes inside both halfwords */
static inline uint32_t rev16_32(uint32_t v)
{
	return ((v & 0x00ff00ffu) << 8) | ((v & 0xff00ff00u) >> 8);
}

static inline uint32_t rbit32(uint32_t v)
{
	v = ((v & 0x55555555u) << 1) | ((v >> 1) & 0x55555555u);
	v = ((v & 0x33333333u) << 2) | ((v >> 2) & 0x33333333u);
	v = ((v & 0x0f0f0f0fu) << 4) | ((v >> 4) & 0x0f0f0f0fu);
	v = ((v & 0x00ff00ffu) << 8) | ((v >> 8) & 0x00ff00ffu);
	return (v << 16) | (v >> 16);
}

static inline int fits_s32(int64_t v)
{
	return v >= N2_S32_MIN && v <= N2_S32_MAX;
}

/* floor(v / 2) without relying on the implementation-defined shift of negative
 * signed values (this is the "sum<16:1>" of the ARM ARM halving variants) */
static inline int64_t half64(int64_t v)
{
	return v >= 0 ? (v >> 1) : ~((~v) >> 1);
}

/* SignedSat(value, bits): clamp to [-2^(bits-1), 2^(bits-1)-1]; *sat set on clamping. */
static inline uint32_t sat_s(int64_t v, unsigned bits, int *sat)
{
	int64_t lo = -((int64_t)1 << (bits - 1u));
	int64_t hi = ((int64_t)1 << (bits - 1u)) - 1;

	if (v < lo) { *sat = 1; return (uint32_t)(uint64_t)lo; }
	if (v > hi) { *sat = 1; return (uint32_t)(uint64_t)hi; }
	return (uint32_t)(uint64_t)v;
}

/* UnsignedSat(value, bits): clamp to [0, 2^bits-1]; *sat set on clamping. */
static inline uint32_t sat_u(int64_t v, unsigned bits, int *sat)
{
	int64_t hi = ((int64_t)1 << bits) - 1;

	if (v < 0) { *sat = 1; return 0u; }
	if (v > hi) { *sat = 1; return (uint32_t)hi; }
	return (uint32_t)v;
}

/* Same as sat_s but returning the signed 64-bit value (for QDADD/QDSUB). */
static inline int64_t sat_s64(int64_t v, unsigned bits, int *sat)
{
	int64_t lo = -((int64_t)1 << (bits - 1u));
	int64_t hi = ((int64_t)1 << (bits - 1u)) - 1;

	if (v < lo) { *sat = 1; return lo; }
	if (v > hi) { *sat = 1; return hi; }
	return v;
}

/* ------------------------------------------------------------------------------------ */
/* 0110 space: extend, reverse, saturate, pack, select, parallel add/sub                */
/* ------------------------------------------------------------------------------------ */

/* SXTB/SXTH/UXTB/UXTH and the SXTAB/SXTAH/UXTAB/UXTAH forms, plus the 16-bit
 * (SXTAB16/UXTAB16) forms.
 * op = [23:20] (8, A, B, C, E, F); add_reg = [19:16] (15 = plain extend). */
static int do_extend(struct n2a32_cpu *c, uint32_t insn, unsigned op, unsigned add_reg,
                     unsigned rd, unsigned rm)
{
	uint32_t v = ror32(c->r[rm], ((insn >> 10) & 3u) * 8u);
	uint32_t base = (add_reg != 15u) ? c->r[add_reg] : 0u;
	uint32_t lo = v & 0xffu, hi = (v >> 16) & 0xffu, res;

	switch (op) {
	case 0xau: /* SXTB, SXTAB */
		res = base + (uint32_t)(uint64_t)sx_bits(lo, 8);
		break;
	case 0xbu: /* SXTH, SXTAH */
		res = base + (uint32_t)(uint64_t)sx_bits(v & 0xffffu, 16);
		break;
	case 0xeu: /* UXTB, UXTAB */
		res = base + lo;
		break;
	case 0xfu: /* UXTH, UXTAH */
		res = base + (v & 0xffffu);
		break;
	case 0x8u: /* SXTB16, SXTAB16 */
		res = ((base & 0xffffu) + (uint32_t)(uint64_t)sx_bits(lo, 8)) & 0xffffu;
		res |= (((base >> 16) + (uint32_t)(uint64_t)sx_bits(hi, 8)) & 0xffffu) << 16;
		break;
	case 0xcu: /* UXTB16, UXTAB16 */
		res = ((base & 0xffffu) + lo) & 0xffffu;
		res |= (((base >> 16) + hi) & 0xffffu) << 16;
		break;
	default:
		return N2A32_UNDEF;
	}
	c->r[rd] = res;
	return N2A32_OK;
}

/* kind: 0 REV, 1 REV16, 2 REVSH, 3 RBIT */
static void do_rev(struct n2a32_cpu *c, unsigned kind, unsigned rd, unsigned rm)
{
	uint32_t v = c->r[rm];

	switch (kind) {
	case 0: c->r[rd] = rev32(v); break;
	case 1: c->r[rd] = rev16_32(v); break;
	case 2: c->r[rd] = (uint32_t)(uint64_t)sx_bits(rev16_32(v) & 0xffffu, 16); break;
	default: c->r[rd] = rbit32(v); break;
	}
}

/* SSAT16 (unsigned_op = 0, saturate_to = sat_imm + 1) / USAT16 (sat_imm). */
static void do_sat16(struct n2a32_cpu *c, unsigned unsigned_op, unsigned sat_imm,
                     unsigned rd, unsigned rm)
{
	uint32_t v = c->r[rm], res;
	int sat = 0;
	uint32_t lo, hi;

	if (unsigned_op) {
		lo = sat_u(sx_bits(v & 0xffffu, 16), sat_imm, &sat);
		hi = sat_u(sx_bits(v >> 16, 16), sat_imm, &sat);
	} else {
		lo = sat_s(sx_bits(v & 0xffffu, 16), sat_imm + 1u, &sat);
		hi = sat_s(sx_bits(v >> 16, 16), sat_imm + 1u, &sat);
	}
	res = (lo & 0xffffu) | ((hi & 0xffffu) << 16);
	c->r[rd] = res;
	if (sat)
		c->q = 1;
}

/* PKHBT (tb = 0) / PKHTB (tb = 1). */
static void do_pkh(struct n2a32_cpu *c, uint32_t insn, int tb, unsigned rd, unsigned rn,
                   unsigned rm)
{
	unsigned sh = (insn >> 7) & 31u;

	if (!tb)
		c->r[rd] = (c->r[rn] & 0xffffu) | ((c->r[rm] << sh) & 0xffff0000u);
	else
		c->r[rd] = (c->r[rn] & 0xffff0000u) | (asr32(c->r[rm], sh ? sh : 32u) & 0xffffu);
}

/* SSAT (op = A/B) / USAT (op = E/F). */
static void do_ssat(struct n2a32_cpu *c, uint32_t insn, unsigned op, unsigned rd, unsigned rn)
{
	unsigned sat_imm = (insn >> 16) & 31u;
	unsigned imm5 = (insn >> 7) & 31u;
	unsigned sh = (insn >> 6) & 1u;
	uint32_t v = c->r[rn];
	int sat = 0;

	if (sh)
		v = asr32(v, imm5 ? imm5 : 32u);
	else
		v <<= imm5;

	if (op & 4u)
		c->r[rd] = sat_u(sx_bits(v, 32), sat_imm, &sat);
	else
		c->r[rd] = sat_s(sx_bits(v, 32), sat_imm + 1u, &sat);
	if (sat)
		c->q = 1;
}

/* SEL */
static void do_sel(struct n2a32_cpu *c, unsigned rd, unsigned rn, unsigned rm)
{
	uint32_t a = c->r[rn], b = c->r[rm], res = 0;
	unsigned i;

	for (i = 0; i < 4; i++) {
		uint32_t m = 0xffu << (8u * i);
		res |= ((c->ge >> i) & 1u) ? (a & m) : (b & m);
	}
	c->r[rd] = res;
}

/*
 * Parallel addition and subtraction.
 *   op  [23:20]: 1 S, 2 Q, 3 SH, 5 U, 6 UQ, 7 UH
 *   op2 [7:4]  : 1 ADD16, 3 ASX, 5 SAX, 7 SUB16, 9 ADD8, F SUB8
 */
static void do_parallel(struct n2a32_cpu *c, unsigned op, unsigned op2, unsigned rd,
                        unsigned rn, unsigned rm)
{
	uint32_t a = c->r[rn], b = c->r[rm];
	unsigned unsigned_op = (op >= 5u);
	unsigned saturating = (op == 2u || op == 6u);
	unsigned halving = (op == 3u || op == 7u);
	unsigned lane_bits = (op2 >= 9u) ? 8u : 16u;
	unsigned nlane = (lane_bits == 8u) ? 4u : 2u;
	unsigned lane_add[4];
	uint32_t res = 0, ge = 0;
	unsigned i;

	if (op2 == 1u || op2 == 9u) {
		for (i = 0; i < 4; i++)
			lane_add[i] = 1u;
	} else if (op2 == 7u || op2 == 0xfu) {
		for (i = 0; i < 4; i++)
			lane_add[i] = 0u;
	} else if (op2 == 3u) {   /* ASX: lo = Rn.lo + Rm.hi, hi = Rn.hi - Rm.lo */
		lane_add[0] = 1u;
		lane_add[1] = 0u;
	} else {                  /* SAX: lo = Rn.lo - Rm.hi, hi = Rn.hi + Rm.lo */
		lane_add[0] = 0u;
		lane_add[1] = 1u;
	}

	for (i = 0; i < nlane; i++) {
		unsigned sh = lane_bits * i;
		uint32_t x = (a >> sh) & mask_bits(lane_bits);
		uint32_t y;
		int64_t xs, ys, s, u;
		uint32_t lane = 0;
		unsigned gbit = (lane_bits == 16u) ? 2u * i : i;

		if (lane_bits == 16u && (op2 == 3u || op2 == 5u))
			y = (b >> (16u * (1u - i))) & 0xffffu;   /* exchanged half */
		else
			y = (b >> sh) & mask_bits(lane_bits);

		xs = sx_bits(x, lane_bits);
		ys = sx_bits(y, lane_bits);
		s = lane_add[i] ? (xs + ys) : (xs - ys);
		u = lane_add[i] ? ((int64_t)x + (int64_t)y) : ((int64_t)x - (int64_t)y);

		if (!saturating && !halving && !unsigned_op) {          /* S */
			if (s >= 0) {
				ge |= 1u << gbit;
				if (lane_bits == 16u)
					ge |= 1u << (gbit + 1u);
			}
			lane = (uint32_t)(uint64_t)s & mask_bits(lane_bits);
		} else if (!saturating && !halving && unsigned_op) {    /* U */
			int g = lane_add[i] ? (u >= ((int64_t)1 << lane_bits)) : (u >= 0);
			if (g) {
				ge |= 1u << gbit;
				if (lane_bits == 16u)
					ge |= 1u << (gbit + 1u);
			}
			lane = (uint32_t)(uint64_t)u & mask_bits(lane_bits);
		} else if (halving) {
			lane = (uint32_t)((uint64_t)half64(unsigned_op ? u : s)) & mask_bits(lane_bits);
		} else if (unsigned_op) {                                /* UQ: no flag change */
			int dummy = 0;
			lane = sat_u(u, lane_bits, &dummy);
		} else {                                                 /* Q: no flag change */
			int dummy = 0;
			lane = sat_s(s, lane_bits, &dummy) & mask_bits(lane_bits);
		}
		res |= lane << sh;
	}

	c->r[rd] = res;
	if (!saturating && !halving)
		c->ge = (uint8_t)(ge & 0xfu);
}

/* ------------------------------------------------------------------------------------ */
/* 0001 space: QADD/QSUB/QDADD/QDSUB and the 16x16 multiplies                          */
/* ------------------------------------------------------------------------------------ */

static int do_qadd(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned rd = (insn >> 12) & 15u, rn = insn & 15u, rm = (insn >> 16) & 15u;
	unsigned op = (insn >> 21) & 7u;      /* 0 QADD, 1 QSUB, 2 QDADD, 3 QDSUB */
	int64_t a, b;
	int sat = 0;

	if (rd == 15u || rn == 15u || rm == 15u)
		return N2A32_UNDEF;
	a = sx_bits(c->r[rn], 32);
	b = sx_bits(c->r[rm], 32);

	if (op == 0u) {
		c->r[rd] = sat_s(a + b, 32, &sat);
	} else if (op == 1u) {
		c->r[rd] = sat_s(a - b, 32, &sat);
	} else if (op == 2u) {
		int sat2 = 0;
		int64_t d = sat_s64(2 * b, 32, &sat2);
		c->r[rd] = sat_s(a + d, 32, &sat);
		sat |= sat2;
	} else {
		int sat2 = 0;
		int64_t d = sat_s64(2 * b, 32, &sat2);
		c->r[rd] = sat_s(a - d, 32, &sat);
		sat |= sat2;
	}
	if (sat)
		c->q = 1;
	return N2A32_OK;
}

static int do_smla_xy(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned rd = (insn >> 16) & 15u, ra = (insn >> 12) & 15u;
	unsigned rm = (insn >> 8) & 15u, rn = insn & 15u;
	unsigned op = (insn >> 20) & 0xffu, op2 = (insn >> 4) & 15u;
	unsigned x = (op2 >> 1) & 1u, y = (op2 >> 2) & 1u;
	uint32_t a = c->r[rn], b = c->r[rm];
	int64_t p;

	if (rd == 15u || rn == 15u || rm == 15u)
		return N2A32_UNDEF;

	if (op == 0x12u) {                            /* SMULWy / SMLAWy */
		if (x && ra != 0u)
			return N2A32_UNDEF;           /* SMULWy has a fixed 0000 field at [15:12] */
		p = (int64_t)sx_bits(a, 32) * sx_bits(y ? (b >> 16) : b, 16);
		if (x) {                              /* bit5 = 1: no accumulate */
			c->r[rd] = (uint32_t)((uint64_t)p >> 16);
			return N2A32_OK;
		}
		if (ra == 15u)
			return N2A32_UNDEF;
		{
			int64_t top = (int64_t)(uint32_t)((uint64_t)p >> 16);
			int64_t s = sx_bits((uint32_t)top, 32) + sx_bits(c->r[ra], 32);

			c->r[rd] = (uint32_t)(uint64_t)s;
			if (!fits_s32(s))
				c->q = 1;
		}
		return N2A32_OK;
	}

	p = (int64_t)sx_bits(x ? (a >> 16) : a, 16) * sx_bits(y ? (b >> 16) : b, 16);
	if (op == 0x16u) {                            /* SMULxy: [15:12] is fixed 0000 */
		if (ra != 0u)
			return N2A32_UNDEF;
		c->r[rd] = (uint32_t)(uint64_t)p;
		return N2A32_OK;
	}
	if (op == 0x14u) {                            /* SMLALxy, RdHi:RdLo */
		uint64_t acc;

		if (ra == 15u || rd == ra)
			return N2A32_UNDEF;           /* PC operand / UNPREDICTABLE */
		acc = ((uint64_t)c->r[rd] << 32) | c->r[ra];
		acc += (uint64_t)p;
		c->r[ra] = (uint32_t)acc;
		c->r[rd] = (uint32_t)(acc >> 32);
		return N2A32_OK;
	}
	/* SMLAxy */
	if (ra == 15u)
		return N2A32_UNDEF;
	{
		int64_t s = p + sx_bits(c->r[ra], 32);

		c->r[rd] = (uint32_t)(uint64_t)s;
		if (!fits_s32(s))
			c->q = 1;
	}
	return N2A32_OK;
}

/* ------------------------------------------------------------------------------------ */
/* 0111 space: dual multiplies, divide, USAD8, bit field                               */
/* ------------------------------------------------------------------------------------ */

static int do_dual(struct n2a32_cpu *c, uint32_t insn, unsigned op2)
{
	unsigned rd = (insn >> 16) & 15u, ra = (insn >> 12) & 15u;
	unsigned rm = (insn >> 8) & 15u, rn = insn & 15u;
	uint32_t a = c->r[rn], b = c->r[rm];
	int64_t p1, p2, sum;
	unsigned accumulate = (ra != 15u);
	unsigned subtract = (op2 & 4u) != 0;

	if (rd == 15u || rn == 15u || rm == 15u)
		return N2A32_UNDEF;

	if (op2 & 2u) {        /* X (bit 1): the second operand's halves are exchanged.
	                        * ARM ARM: product1 = Rn<15:0> * Rm<31:16>,
	                        *          product2 = Rn<31:16> * Rm<15:0>,
	                        *          result   = product1 - product2 (SMUSDX/SMLSDX),
	                        * so the order matters for the subtract forms. */
		p1 = (int64_t)sx_bits(a & 0xffffu, 16) * sx_bits(b >> 16, 16);
		p2 = (int64_t)sx_bits(a >> 16, 16) * sx_bits(b & 0xffffu, 16);
	} else {
		p1 = (int64_t)sx_bits(a & 0xffffu, 16) * sx_bits(b & 0xffffu, 16);
		p2 = (int64_t)sx_bits(a >> 16, 16) * sx_bits(b >> 16, 16);
	}
	sum = subtract ? (p1 - p2) : (p1 + p2);
	if (accumulate)
		sum += sx_bits(c->r[ra], 32);

	c->r[rd] = (uint32_t)(uint64_t)sum;
	/* Q on overflow of the exact result.  SMUSD/SMUSDX (no accumulate, subtract)
	 * are the documented exception: they leave Q unchanged. */
	if ((accumulate || !subtract) && !fits_s32(sum))
		c->q = 1;
	return N2A32_OK;
}

static int do_smlal_dual(struct n2a32_cpu *c, uint32_t insn, unsigned op2)
{
	unsigned rdhi = (insn >> 16) & 15u, rdlo = (insn >> 12) & 15u;
	unsigned rm = (insn >> 8) & 15u, rn = insn & 15u;
	uint32_t a = c->r[rn], b = c->r[rm];
	int64_t p1, p2, sum;
	uint64_t acc;

	if (rdhi == 15u || rdlo == 15u || rn == 15u || rm == 15u)
		return N2A32_UNDEF;
	if (rdhi == rdlo)
		return N2A32_UNDEF;   /* UNPREDICTABLE */

	if (op2 & 2u) {        /* X: same exchange as do_dual (order matters for SMLSLDX) */
		p1 = (int64_t)sx_bits(a & 0xffffu, 16) * sx_bits(b >> 16, 16);
		p2 = (int64_t)sx_bits(a >> 16, 16) * sx_bits(b & 0xffffu, 16);
	} else {
		p1 = (int64_t)sx_bits(a & 0xffffu, 16) * sx_bits(b & 0xffffu, 16);
		p2 = (int64_t)sx_bits(a >> 16, 16) * sx_bits(b >> 16, 16);
	}
	sum = (op2 & 4u) ? (p1 - p2) : (p1 + p2);

	acc = ((uint64_t)c->r[rdhi] << 32) | c->r[rdlo];
	acc += (uint64_t)sum;
	c->r[rdlo] = (uint32_t)acc;
	c->r[rdhi] = (uint32_t)(acc >> 32);
	return N2A32_OK;
}

static int do_smml(struct n2a32_cpu *c, uint32_t insn, unsigned op2)
{
	unsigned rd = (insn >> 16) & 15u, ra = (insn >> 12) & 15u;
	unsigned rm = (insn >> 8) & 15u, rn = insn & 15u;
	int64_t p = (int64_t)sx_bits(c->r[rn], 32) * sx_bits(c->r[rm], 32);
	unsigned round = (op2 & 2u) != 0, mls = (op2 & 8u) != 0;
	uint64_t acc;

	if (rd == 15u || rn == 15u || rm == 15u)
		return N2A32_UNDEF;
	if (mls && ra == 15u)
		return N2A32_UNDEF;

	if (ra == 15u)
		acc = (uint64_t)p;                                  /* SMMUL / SMMULR */
	else if (mls)
		acc = ((uint64_t)c->r[ra] << 32) - (uint64_t)p;      /* SMMLS / SMMLSR */
	else
		acc = ((uint64_t)c->r[ra] << 32) + (uint64_t)p;      /* SMMLA / SMMLAR */
	if (round)
		acc += 0x80000000u;
	c->r[rd] = (uint32_t)(acc >> 32);
	return N2A32_OK;
}

static int do_usad(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned rd = (insn >> 16) & 15u, ra = (insn >> 12) & 15u;
	unsigned rm = (insn >> 8) & 15u, rn = insn & 15u;
	uint32_t a = c->r[rn], b = c->r[rm], sum = 0;
	unsigned i;

	if (rd == 15u || rn == 15u || rm == 15u)
		return N2A32_UNDEF;
	for (i = 0; i < 4; i++) {
		uint32_t x = (a >> (8u * i)) & 0xffu, y = (b >> (8u * i)) & 0xffu;

		sum += (x > y) ? (x - y) : (y - x);
	}
	if (ra != 15u)
		sum += c->r[ra];
	c->r[rd] = sum;
	return N2A32_OK;
}

static int do_div(struct n2a32_cpu *c, uint32_t insn, int is_signed)
{
	unsigned rd = (insn >> 16) & 15u, ra = (insn >> 12) & 15u;
	unsigned rm = (insn >> 8) & 15u, rn = insn & 15u;
	uint32_t n = c->r[rn], d = c->r[rm];

	if (rd == 15u || rn == 15u || rm == 15u || ra != 15u)
		return N2A32_UNDEF;
	if (d == 0u)
		c->r[rd] = 0u;                        /* divide by zero yields 0 */
	else if (!is_signed)
		c->r[rd] = n / d;
	else
		c->r[rd] = (uint32_t)(uint64_t)(sx_bits(n, 32) / sx_bits(d, 32));
	return N2A32_OK;
}

/* sel: 1 = SBFX, 2 = BFI/BFC, 3 = UBFX (bit 23 is known set here). */
static int do_bitfield(struct n2a32_cpu *c, uint32_t insn, unsigned sel)
{
	unsigned rd = (insn >> 12) & 15u, rn = insn & 15u;
	unsigned lsb = (insn >> 7) & 31u;
	unsigned wm = (insn >> 16) & 31u;
	unsigned lsb6_5 = (insn >> 5) & 3u;

	if (rd == 15u)
		return N2A32_UNDEF;

	if (sel == 2u) {                       /* insert: [6:5] must be 00 */
		uint32_t mask, inserted = 0;

		if (lsb6_5 != 0u)
			return N2A32_UNDEF;
		if (wm < lsb)
			return N2A32_UNDEF;    /* UNPREDICTABLE */
		mask = mask_bits(wm - lsb + 1u) << lsb;
		if (rn != 15u)
			inserted = (c->r[rn] << lsb) & mask;
		c->r[rd] = (c->r[rd] & ~mask) | inserted;
		return N2A32_OK;
	}

	/* extract: [6:5] must be 10 */
	if (lsb6_5 != 2u || rn == 15u)
		return N2A32_UNDEF;
	if (lsb + wm > 31u)
		return N2A32_UNDEF;            /* UNPREDICTABLE */
	{
		uint32_t field = (c->r[rn] >> lsb) & mask_bits(wm + 1u);

		if (sel == 3u)                 /* UBFX */
			c->r[rd] = field;
		else                           /* SBFX */
			c->r[rd] = (uint32_t)(uint64_t)sx_bits(field, wm + 1u);
	}
	return N2A32_OK;
}

/* [27:24] == 0111 */
static int media_0111(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned op = (insn >> 20) & 15u, op2 = (insn >> 4) & 15u;

	if ((insn & 0x00800000u) != 0u) {           /* bit 23 set */
		unsigned sel = (insn >> 21) & 3u;

		if (sel == 0u) {                    /* USAD8 / USADA8 */
			if (op != 8u || op2 != 1u)
				return N2A32_UNDEF;
			return do_usad(c, insn);
		}
		return do_bitfield(c, insn, sel);
	}

	switch (op) {
	case 0x0u:                                  /* SMUAD/SMUSD/SMLAD/SMLSD */
		if ((op2 & 9u) != 1u)
			return N2A32_UNDEF;
		return do_dual(c, insn, op2);
	case 0x1u:                                  /* SDIV */
		if (op2 != 1u || (insn & 0x0000f000u) != 0x0000f000u)
			return N2A32_UNDEF;
		return do_div(c, insn, 1);
	case 0x3u:                                  /* UDIV */
		if (op2 != 1u || (insn & 0x0000f000u) != 0x0000f000u)
			return N2A32_UNDEF;
		return do_div(c, insn, 0);
	case 0x4u:                                  /* SMLALD/SMLSLD */
		if ((op2 & 9u) != 1u)
			return N2A32_UNDEF;
		return do_smlal_dual(c, insn, op2);
	case 0x5u:                                  /* SMMUL/SMMLA/SMMLS */
		if (((op2 >> 2) & 1u) != ((op2 >> 3) & 1u) || (op2 & 1u) == 0u)
			return N2A32_UNDEF;
		return do_smml(c, insn, op2);
	default:
		return N2A32_UNDEF;
	}
}

/* [27:24] == 0110 */
static int media_0110(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned rd = (insn >> 12) & 15u, rm = insn & 15u;
	unsigned road = (insn >> 16) & 15u;         /* Rn, or a marker, or sat_imm */
	unsigned op = (insn >> 20) & 15u;
	unsigned sub = (insn >> 8) & 15u;
	unsigned op2 = (insn >> 4) & 15u;

	/* --- parallel addition and subtraction: [23:20] 1,2,3,5,6,7 ------------------ */
	if (op == 0x1u || op == 0x2u || op == 0x3u ||
	    op == 0x5u || op == 0x6u || op == 0x7u) {
		if (sub != 15u)
			return N2A32_UNDEF;
		if (op2 != 0x1u && op2 != 0x3u && op2 != 0x5u && op2 != 0x7u &&
		    op2 != 0x9u && op2 != 0xfu)
			return N2A32_UNDEF;
		if (rd == 15u || road == 15u || rm == 15u)
			return N2A32_UNDEF;
		do_parallel(c, op, op2, rd, road, rm);
		return N2A32_OK;
	}

	/* --- extend family: [7:4] == 0111 --------------------------------------------- */
	if (op2 == 0x7u) {
		if ((insn & 0x00000300u) != 0u)     /* bits [9:8] are fixed 00 */
			return N2A32_UNDEF;
		if (rd == 15u || rm == 15u)
			return N2A32_UNDEF;
		return do_extend(c, insn, op, road, rd, rm);
	}

	/* --- reverse / SSAT16 / USAT16 / SEL: [7:4] == 0011 or 1011 ------------------- */
	if (op2 == 0x3u || op2 == 0xbu) {
		if (sub != 15u || rd == 15u)
			return N2A32_UNDEF;
		if (op2 == 0x3u) {
			if (op == 0xbu) {           /* REV */
				if (road != 15u || rm == 15u)
					return N2A32_UNDEF;
				do_rev(c, 0, rd, rm);
				return N2A32_OK;
			}
			if (op == 0xfu) {           /* RBIT */
				if (road != 15u || rm == 15u)
					return N2A32_UNDEF;
				do_rev(c, 3, rd, rm);
				return N2A32_OK;
			}
			if (op == 0xau) {           /* SSAT16 */
				if (rm == 15u)
					return N2A32_UNDEF;
				do_sat16(c, 0, road, rd, rm);
				return N2A32_OK;
			}
			if (op == 0xeu) {           /* USAT16 */
				if (rm == 15u)
					return N2A32_UNDEF;
				do_sat16(c, 1, road, rd, rm);
				return N2A32_OK;
			}
			return N2A32_UNDEF;
		}
		/* op2 == 0xB */
		if (op == 0xbu) {                   /* REV16 */
			if (road != 15u || rm == 15u)
				return N2A32_UNDEF;
			do_rev(c, 1, rd, rm);
			return N2A32_OK;
		}
		if (op == 0xfu) {                   /* REVSH */
			if (road != 15u || rm == 15u)
				return N2A32_UNDEF;
			do_rev(c, 2, rd, rm);
			return N2A32_OK;
		}
		if (op == 0x8u) {                   /* SEL */
			if (road == 15u || rm == 15u)
				return N2A32_UNDEF;
			do_sel(c, rd, road, rm);
			return N2A32_OK;
		}
		return N2A32_UNDEF;
	}

	/* --- PKHBT/PKHTB and SSAT/USAT: [7:4] has bit5 == 0, bit4 == 1 ---------------- */
	if (op2 == 0x1u || op2 == 0x5u || op2 == 0x9u || op2 == 0xdu) {
		if (op == 0x8u) {
			if (rd == 15u || road == 15u || rm == 15u)
				return N2A32_UNDEF;
			/* bit 6 of [7:4] is the TB selector (1/5/9/D all reach here; bit 7 is imm5<4>) */
			do_pkh(c, insn, (op2 & 4u) != 0u, rd, road, rm);
			return N2A32_OK;
		}
		if (op == 0xau || op == 0xbu || op == 0xeu || op == 0xfu) {
			if (rd == 15u || rm == 15u)
				return N2A32_UNDEF;
			do_ssat(c, insn, op, rd, rm);
			return N2A32_OK;
		}
		return N2A32_UNDEF;
	}

	return N2A32_UNDEF;
}

/* ------------------------------------------------------------------------------------ */
/* entry point                                                                          */
/* ------------------------------------------------------------------------------------ */

int n2a32_arm_media(struct n2a32_cpu *c, uint32_t insn)
{
	/* A32 media space: bits[27:25] == 011 and bit[4] == 1 (0110 and 0111 groups). */
	if ((insn & 0x0e000010u) == 0x06000010u) {
		if ((insn & 0x0f000000u) == 0x07000000u)
			return media_0111(c, insn);
		return media_0110(c, insn);
	}
	/* QADD/QSUB/QDADD/QDSUB: [27:24]=0001, bit23=0, bit20=0, [11:4]=0x05. */
	if ((insn & 0x0f900ff0u) == 0x01000050u)
		return do_qadd(c, insn);
	/* SMULxy/SMLAxy/SMLALxy/SMULWy/SMLAWy: [27:24]=0001, bit23=0, bit20=0,
	 * [7:4] in {8, A, C, E}. */
	if ((insn & 0x0f900090u) == 0x01000080u)
		return do_smla_xy(c, insn);
	return N2A32_UNDEF;
}
