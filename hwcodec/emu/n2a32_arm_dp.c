/* SPDX-License-Identifier: MIT */
/*
 * n2a32_arm_dp.c -- A32 data processing, moves and multiplies.  Package V10 of gpu/hwcodec track V.
 *
 * Implements the single class entry point declared in include/n2a32.h:
 *
 *     int n2a32_arm_dp(struct n2a32_cpu *c, uint32_t insn);
 *
 * Scope (all A32, condition already evaluated by the core):
 *   - the 16 data-processing opcodes with all three operand forms:
 *       immediate (ARMExpandImm_C), register with immediate shift (LSL/LSR/ASR/ROR/RRX)
 *       and register with register-specified shift; S variants included;
 *       TST/TEQ/CMP/CMN (always set the flags); writes to r15 go through n2a32_bx().
 *   - MOVW / MOVT
 *   - MUL, MLA, MLS, UMULL, UMLAL, SMULL, SMLAL, UMAAL
 *   - MRS {CPSR} / MSR {APSR_nzcvq, APSR_g} (register form; user mode)
 *   - CLZ
 *   - ADR is the assembler spelling of ADD/SUB (immediate) with Rn = r15 and needs no
 *     special code: r15 is read as the core supplied it.
 *
 * Everything else returns N2A32_UNDEF.  In particular this file deliberately does NOT
 * claim (they belong to other packages of the interpreter):
 *   - BX, BLX (register), all hints (NOP/YIELD/WFE/WFI/SEV/DBG), MSR (immediate):
 *     V12 (n2a32_arm_misc);
 *   - LDRH/STRH/LDRSB/LDRSH/LDRD/STRD and every other memory access: V12 (n2a32_arm_mem);
 *   - QADD/QSUB/QDADD/QDSUB, SDIV/UDIV, extend/bit-field/saturate/parallel ops: V11
 *     (n2a32_arm_media);
 *   - coprocessor, SVC, BKPT, UDF and the unconditional (cond == 0xF) space: V12.
 *
 * Notes on choices for encodings the ARM ARM calls UNPREDICTABLE or unallocated, all
 * listed in emu/test_v10/STATUS.md:
 *   - reading r15 as an ALU operand returns c->r[15] unchanged (that is what makes ADR work);
 *   - TST/TEQ/CMP/CMN ignore their Rd field, MOV/MVN ignore their Rn field (the
 *     architecture marks those fields (0), hardware ignores them);
 *   - MRS/MSR/CLZ with r15 as destination or source return N2A32_UNDEF, as do
 *     MOVW/MOVT with Rd = r15 and data processing with S = 1 and Rd = r15
 *     (ALUExceptionReturn, privileged; this interpreter is user-mode only);
 *   - MSR (register) is accepted only for the user-mode-writable APSR fields
 *     (mask bits 3 and 2, i.e. APSR_nzcvq and APSR_g); any other mask, and the
 *     SPSR form, return N2A32_UNDEF;
 *   - multiply operands naming r15 return N2A32_UNDEF.
 *
 * No memory is accessed by this class; it never calls the load/store helpers.
 */

#include "n2a32.h"

enum { SH_LSL = 0, SH_LSR = 1, SH_ASR = 2, SH_ROR = 3 };

/* Data-processing opcodes, bits 24-21. */
enum {
	OP_AND = 0, OP_EOR = 1, OP_SUB = 2, OP_RSB = 3, OP_ADD = 4, OP_ADC = 5,
	OP_SBC = 6, OP_RSC = 7, OP_TST = 8, OP_TEQ = 9, OP_CMP = 10, OP_CMN = 11,
	OP_ORR = 12, OP_MOV = 13, OP_BIC = 14, OP_MVN = 15
};

static uint32_t ror32(uint32_t v, unsigned n)
{
	n &= 31u;
	return n ? (v >> n) | (v << (32u - n)) : v;
}

/* Arithmetic shift right, 1 <= n <= 31, without relying on implementation-defined
 * signed right shifts. */
static uint32_t asr32(uint32_t v, unsigned n)
{
	uint32_t fill = (v & 0x80000000u) ? (uint32_t)(~0u << (32u - n)) : 0u;
	return (v >> n) | fill;
}

/* ARMExpandImm_C: rotation of an 8-bit immediate by 2 * rot, carry out as the ARM ARM
 * defines it (rot == 0: the carry is the incoming carry; otherwise bit 31 of the result). */
static uint32_t expand_imm(uint32_t imm12, unsigned cin, unsigned *cout)
{
	unsigned rot = (imm12 >> 8) & 15u;
	uint32_t v = imm12 & 0xffu;

	if (rot == 0) {
		*cout = cin;
		return v;
	}
	v = ror32(v, 2u * rot);
	*cout = v >> 31;
	return v;
}

/* Shift_C with an immediate (5-bit) amount; amount == 0 selects the special encodings
 * LSR #32, ASR #32, RRX (for LSL #0 it means "no shift, carry unchanged"). */
static uint32_t shift_imm(uint32_t v, unsigned type, unsigned amount, unsigned cin, unsigned *cout)
{
	switch (type) {
	case SH_LSL:
		if (amount == 0) {
			*cout = cin;
			return v;
		}
		*cout = (v >> (32u - amount)) & 1u;
		return v << amount;
	case SH_LSR:
		if (amount == 0) {			/* encoded as LSR #32 */
			*cout = v >> 31;
			return 0;
		}
		*cout = (v >> (amount - 1u)) & 1u;
		return v >> amount;
	case SH_ASR:
		if (amount == 0) {			/* encoded as ASR #32 */
			*cout = v >> 31;
			return (v & 0x80000000u) ? 0xffffffffu : 0u;
		}
		*cout = (v >> (amount - 1u)) & 1u;
		return asr32(v, amount);
	default: /* SH_ROR */
		if (amount == 0) {			/* encoded as RRX */
			uint32_t r = (cin << 31) | (v >> 1);
			*cout = v & 1u;
			return r;
		}
		*cout = (v >> (amount - 1u)) & 1u;
		return ror32(v, amount);
	}
}

/* Shift_C with a register-specified amount (Rs<7:0>). */
static uint32_t shift_reg(uint32_t v, unsigned type, unsigned amount, unsigned cin, unsigned *cout)
{
	if (amount == 0) {				/* no shift, carry unchanged */
		*cout = cin;
		return v;
	}
	switch (type) {
	case SH_LSL:
		if (amount < 32) {
			*cout = (v >> (32u - amount)) & 1u;
			return v << amount;
		}
		*cout = (amount == 32) ? (v & 1u) : 0u;
		return 0;
	case SH_LSR:
		if (amount < 32) {
			*cout = (v >> (amount - 1u)) & 1u;
			return v >> amount;
		}
		*cout = (amount == 32) ? (v >> 31) : 0u;
		return 0;
	case SH_ASR:
		if (amount < 32) {
			*cout = (v >> (amount - 1u)) & 1u;
			return asr32(v, amount);
		}
		*cout = v >> 31;
		return (v & 0x80000000u) ? 0xffffffffu : 0u;
	default: { /* SH_ROR */
		unsigned n = amount & 31u;
		if (n == 0) {				/* amount is a multiple of 32 and nonzero */
			*cout = v >> 31;
			return v;
		}
		*cout = (v >> (n - 1u)) & 1u;
		return ror32(v, n);
	}
	}
}

/* ---- MRS / MSR / CLZ ---------------------------------------------------------------- */

/*
 * Value returned by MRS <Rd>, CPSR.  The interpreter is user-mode only, so the mode field
 * is User (0b10000).  A, I and F read as 1 in User mode (ARMv8-A AArch32 CPSR, "in User
 * mode these bits read as 1"); E is 0 (little endian); T follows c->thumb; the IT bits are
 * taken from c->itstate for consistency with the T32 MRS.  hw-v16 (T32) uses the same
 * formula -- posted on the board.
 */
static uint32_t read_cpsr(const struct n2a32_cpu *c)
{
	uint32_t v = n2a32_apsr(c) | 0x000001d0u;	/* A | I | F | User mode */
	if (c->thumb)
		v |= 0x20u;				/* T */
	v |= (uint32_t)(c->itstate >> 2) << 10;		/* IT[7:2] -> CPSR[15:10] */
	v |= (uint32_t)(c->itstate & 3u) << 25;		/* IT[1:0] -> CPSR[26:25] */
	return v;
}

static int do_mrs(struct n2a32_cpu *c, uint32_t insn, int spsr)
{
	unsigned rd = (insn >> 12) & 0xfu;

	if (rd == 15 || spsr)				/* SPSR does not exist in User mode */
		return N2A32_UNDEF;
	c->r[rd] = read_cpsr(c);
	return N2A32_OK;
}

static int do_msr(struct n2a32_cpu *c, uint32_t insn, int spsr)
{
	unsigned mask = (insn >> 16) & 0xfu;		/* bits 19-16 */
	unsigned rm = insn & 0xfu;
	uint32_t v;

	if (spsr || rm == 15)
		return N2A32_UNDEF;
	/*
	 * mask bit 3 = f (CPSR[31:24]: N,Z,C,V,Q and the reserved 26:24),
	 * mask bit 2 = s (CPSR[23:16]: GE at 19:16, reserved 23:20).
	 * mask bits 1 (x, CPSR[15:8]) and 0 (c, CPSR[7:0]) are not writable in User mode.
	 */
	if (mask & ~0xcu)
		return N2A32_UNDEF;
	v = c->r[rm];
	if (mask & 8u) {
		c->n = (v >> 31) & 1u;
		c->z = (v >> 30) & 1u;
		c->cf = (v >> 29) & 1u;
		c->v = (v >> 28) & 1u;
		c->q = (v >> 27) & 1u;
	}
	if (mask & 4u)
		c->ge = (uint8_t)((v >> 16) & 0xfu);
	return N2A32_OK;
}

static int do_clz(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned rd = (insn >> 12) & 0xfu;
	unsigned rm = insn & 0xfu;
	uint32_t v;
	unsigned n = 0;

	if (rd == 15 || rm == 15)			/* UNPREDICTABLE */
		return N2A32_UNDEF;
	v = c->r[rm];
	if (v == 0) {
		n = 32;
	} else {
		while (!(v & 0x80000000u)) {
			v <<= 1;
			n++;
		}
	}
	c->r[rd] = n;
	return N2A32_OK;
}

/* ---- MOVW / MOVT -------------------------------------------------------------------- */

static int do_mov16(struct n2a32_cpu *c, uint32_t insn, int top)
{
	unsigned rd = (insn >> 12) & 0xfu;
	uint32_t imm = ((insn >> 16) & 0xfu) << 12 | (insn & 0xfffu);

	if (rd == 15)					/* UNPREDICTABLE */
		return N2A32_UNDEF;
	if (top)
		c->r[rd] = (c->r[rd] & 0x0000ffffu) | (imm << 16);
	else
		c->r[rd] = imm;
	return N2A32_OK;
}

/* ---- multiplies --------------------------------------------------------------------- */

/*
 * Multiply group: bits 27-23 == 00000 (MUL/MLA/MLS/UMAAL) or 00001 (UMULL/UMLAL/SMULL/
 * SMLAL) together with bits 7-4 == 1001.  The extra load/store encodings that share bits
 * 27-25 == 000 and bits 7,4 == 1,1 have bits 6-5 != 00 and never match 1001.
 */
static int do_mul(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned hi = (insn >> 16) & 0xfu;		/* Rd / RdHi */
	unsigned lo = (insn >> 12) & 0xfu;		/* Ra / RdLo */
	unsigned rs = (insn >> 8) & 0xfu;
	unsigned rm = insn & 0xfu;
	unsigned s = (insn >> 20) & 1u;
	uint32_t a = c->r[rs], b = c->r[rm];

	if (hi == 15 || lo == 15 || rs == 15 || rm == 15)
		return N2A32_UNDEF;

	if (((insn >> 23) & 0x1fu) == 0x1u) {		/* UMULL/UMLAL/SMULL/SMLAL */
		uint64_t acc = 0, prod;
		unsigned is_signed = (insn >> 22) & 1u;
		unsigned accumulate = (insn >> 21) & 1u;

		if (is_signed)
			prod = (uint64_t)(int64_t)(int32_t)a * (uint64_t)(int64_t)(int32_t)b;
		else
			prod = (uint64_t)a * (uint64_t)b;
		if (accumulate)
			acc = ((uint64_t)c->r[hi] << 32) | (uint64_t)c->r[lo];
		prod += acc;				/* 64-bit wraparound, defined for uint64_t */
		c->r[lo] = (uint32_t)prod;
		c->r[hi] = (uint32_t)(prod >> 32);
		if (s) {
			c->n = (uint32_t)(prod >> 63);
			c->z = (prod == 0);
		}
		return N2A32_OK;
	}

	switch ((insn >> 21) & 3u) {
	case 0:						/* MUL: Rd = Rm * Rs */
	{
		uint32_t res = (uint32_t)((uint64_t)a * (uint64_t)b);
		c->r[hi] = res;
		if (s) {
			c->n = res >> 31;
			c->z = (res == 0);
		}
		return N2A32_OK;
	}
	case 1:						/* MLA: Rd = Ra + Rm * Rs */
	{
		uint32_t res = (uint32_t)((uint64_t)a * (uint64_t)b + (uint64_t)c->r[lo]);
		c->r[hi] = res;
		if (s) {
			c->n = res >> 31;
			c->z = (res == 0);
		}
		return N2A32_OK;
	}
	case 2:						/* UMAAL: RdHi:RdLo += Rm * Rs */
	{
		uint64_t sum = (uint64_t)a * (uint64_t)b + (uint64_t)c->r[lo] + (uint64_t)c->r[hi];
		if (s)					/* no S variant exists */
			return N2A32_UNDEF;
		c->r[lo] = (uint32_t)sum;
		c->r[hi] = (uint32_t)(sum >> 32);
		return N2A32_OK;
	}
	default: /* case 3: MLS: Rd = Ra - Rm * Rs */
	{
		uint32_t res;
		if (s)					/* no S variant exists */
			return N2A32_UNDEF;
		res = c->r[lo] - (uint32_t)((uint64_t)a * (uint64_t)b);
		c->r[hi] = res;
		return N2A32_OK;
	}
	}
}

/* ---- data-processing register / immediate ------------------------------------------- */

static int do_dp(struct n2a32_cpu *c, uint32_t insn, unsigned op, unsigned s, unsigned rd,
		 uint32_t op2, unsigned carry)
{
	uint32_t a = c->r[(insn >> 16) & 0xfu];
	uint32_t res = 0;
	uint8_t add_carry = (uint8_t)carry, add_overflow = 0;
	int arithmetic = 0;
	int has_dest = !(op >= OP_TST && op <= OP_CMN);

	if (has_dest && rd == 15 && s)
		return N2A32_UNDEF;		/* ALUExceptionReturn: privileged */

	switch (op) {
	case OP_AND: res = a & op2; break;
	case OP_EOR: res = a ^ op2; break;
	case OP_SUB: res = n2a32_adc(a, ~op2, 1u, &add_carry, &add_overflow); arithmetic = 1; break;
	case OP_RSB: res = n2a32_adc(op2, ~a, 1u, &add_carry, &add_overflow); arithmetic = 1; break;
	case OP_ADD: res = n2a32_adc(a, op2, 0u, &add_carry, &add_overflow); arithmetic = 1; break;
	/* ADC/SBC/RSC add the *current* C flag, not the shifter carry-out. */
	case OP_ADC: res = n2a32_adc(a, op2, c->cf, &add_carry, &add_overflow); arithmetic = 1; break;
	case OP_SBC: res = n2a32_adc(a, ~op2, c->cf, &add_carry, &add_overflow); arithmetic = 1; break;
	case OP_RSC: res = n2a32_adc(op2, ~a, c->cf, &add_carry, &add_overflow); arithmetic = 1; break;
	case OP_TST: res = a & op2; break;
	case OP_TEQ: res = a ^ op2; break;
	case OP_CMP: res = n2a32_adc(a, ~op2, 1u, &add_carry, &add_overflow); arithmetic = 1; break;
	case OP_CMN: res = n2a32_adc(a, op2, 0u, &add_carry, &add_overflow); arithmetic = 1; break;
	case OP_ORR: res = a | op2; break;
	case OP_MOV: res = op2; break;
	case OP_BIC: res = a & ~op2; break;
	default:     /* OP_MVN */
		res = ~op2; break;
	}

	if (s) {
		c->n = res >> 31;
		c->z = (res == 0);
		c->cf = (uint8_t)(add_carry & 1u);
		if (arithmetic)
			c->v = (uint8_t)(add_overflow & 1u);
	}

	if (op >= OP_TST && op <= OP_CMN)
		return N2A32_OK;			/* no destination */

	if (rd == 15) {
		n2a32_bx(c, res);
		return N2A32_OK;
	}
	c->r[rd] = res;
	return N2A32_OK;
}

int n2a32_arm_dp(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned op = (insn >> 21) & 0xfu;
	unsigned s = (insn >> 20) & 1u;
	unsigned rn = (insn >> 16) & 0xfu;
	unsigned rd = (insn >> 12) & 0xfu;

	/* cond == 0xF is the A32 unconditional space: not this package. */
	if ((insn >> 28) == 0xfu)
		return N2A32_UNDEF;

	/* Only bits 27-25 == 000 (register forms) and 001 (immediate form) are data
	 * processing / multiply / move-immediate space; everything else is another class. */
	if ((insn & 0x0e000000u) != 0u && (insn & 0x0e000000u) != 0x02000000u)
		return N2A32_UNDEF;

	/* Multiply group. */
	if ((((insn >> 23) & 0x1fu) == 0u || ((insn >> 23) & 0x1fu) == 1u) &&
	    ((insn >> 4) & 0xfu) == 9u)
		return do_mul(c, insn);

	if (insn & 0x02000000u) {			/* bit 25: immediate operand */
		if (op >= OP_TST && op <= OP_CMN && !s) {
			if (op == OP_TST)		/* bits 24-21 = 1000: MOVW */
				return do_mov16(c, insn, 0);
			if (op == OP_CMP)		/* bits 24-21 = 1010: MOVT */
				return do_mov16(c, insn, 1);
			/* TEQ/CMN with S == 0: MSR (immediate) and the hint space. V12. */
			return N2A32_UNDEF;
		}
		{
			unsigned carry;
			uint32_t op2 = expand_imm(insn & 0xfffu, c->cf, &carry);
			return do_dp(c, insn, op, s, rd, op2, carry);
		}
	}

	if (!(insn & 0x10u)) {				/* register, shift by immediate */
		if (op >= OP_TST && op <= OP_CMN && !s) {
			if ((insn & 0xfffu) == 0 && rn == 15) {
				if (op == OP_TST)	/* 0001 0 0 00 1111 Rd 0000 0000 0000 */
					return do_mrs(c, insn, 0);
				if (op == OP_CMP)	/* 0001 0 1 00 1111 Rd 0000 0000 0000 */
					return do_mrs(c, insn, 1);
			}
			if (rd == 15 && (insn & 0xff0u) == 0) {
				if (op == OP_TEQ)	/* 0001 0 0 10 mask 1111 0000 0000 Rm */
					return do_msr(c, insn, 0);
				if (op == OP_CMN)	/* 0001 0 1 10 mask 1111 0000 0000 Rm */
					return do_msr(c, insn, 1);
			}
			return N2A32_UNDEF;		/* BX/BLX and the unallocated rest are V12 */
		}
		{
			unsigned type = (insn >> 5) & 3u;
			unsigned amount = (insn >> 7) & 0x1fu;
			unsigned carry;
			uint32_t op2 = shift_imm(c->r[insn & 0xfu], type, amount, c->cf, &carry);
			return do_dp(c, insn, op, s, rd, op2, carry);
		}
	}

	if (!(insn & 0x80u)) {				/* register, shift by register */
		if (op >= OP_TST && op <= OP_CMN && !s) {
			if (op == OP_CMN && rn == 15 && (insn & 0xff0u) == 0xf10u)
				return do_clz(c, insn);	/* 0001 0110 1111 Rd 1111 0001 Rm */
			return N2A32_UNDEF;		/* BLX (register), QADD/QSUB, ... */
		}
		{
			unsigned type = (insn >> 5) & 3u;
			unsigned rs = (insn >> 8) & 0xfu;
			unsigned amount = c->r[rs] & 0xffu;
			unsigned carry;
			uint32_t op2 = shift_reg(c->r[insn & 0xfu], type, amount, c->cf, &carry);
			return do_dp(c, insn, op, s, rd, op2, carry);
		}
	}

	/* bits 27-25 = 000, bit 7 = 1, bit 4 = 1: multiply (handled above), extra load/store,
	 * BX/BLX, barriers: not this package. */
	return N2A32_UNDEF;
}
