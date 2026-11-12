/* SPDX-License-Identifier: MIT */
/*
 * n2a32_t16.c -- T32 16-bit instruction class for the AArch32 interpreter.
 *
 * Contract (include/n2a32.h): the core has already fetched the 16-bit unit,
 * evaluated the IT condition (so this file must NOT skip the instruction), set
 * c->r[15] to the instruction address + 4, c->next_pc to the address of the
 * following instruction and c->in_it to InITBlock().  We return N2A32_OK, or
 * N2A32_UNDEF for an encoding we do not implement.  Branches go through
 * n2a32_branch() (simple branch) or n2a32_bx() (BXWritePC, interworking);
 * memory goes through the n2a32_ld8/16/32 and n2a32_st8/16/32 helpers.
 *
 * Decode follows the T32 instruction-set encoding tree of the Arm ARM
 * (DDI 0487, "T32 instruction set encoding"); every 16-bit encoding is owned
 * here.  Reference semantics are the Arm pseudocode for each instruction.
 *
 * Deliberately N2A32_UNDEF (see test_v14/STATUS.md for the full list):
 *   - SETEND and CPS (0xB650-0xB651, 0xB660-0xB66F): privileged execution
 *     control / data endianness, not part of an EL0 little-endian user mode.
 *   - HLT (0xBA80-0xBABF): debug/semihosting instruction.
 *   - reserved holes 0xB300-0xB3FF, 0xB600-0xB6FF (except SETEND/CPS above),
 *     0xB700-0xB8FF, 0xBB00-0xBBFF, and the REV-group hole 0xBA80-0xBABF.
 *   - IT with firstcond 1111, and IT with firstcond 1110 whose mask does not
 *     have exactly one bit set: both are architecturally UNPREDICTABLE and
 *     UNDEFINED is one of the permitted behaviours.
 *   - 32-bit prefixes (top five bits 11101/11110/11111) and values wider than
 *     16 bits: defensive only; the core routes those elsewhere.
 *   - UDF, BKPT and SVC stop the run: they set c->stop (STOP_UNDEF /
 *     STOP_BKPT / STOP_SVC), c->stop_insn and c->stop_addr and return
 *     N2A32_UNDEF, matching the convention used by V12 (the core must check
 *     c->stop before translating the UNDEF return into STOP_UNDEF).
 *
 * Architecturally UNPREDICTABLE encodings are executed deterministically with
 * their natural semantics rather than rejected (CMP with PC, BX/BLX with PC,
 * ADD/MOV with PC in a non-last IT slot, IT inside IT, LSL #0 inside IT);
 * they cannot crash and the vendor libraries are compiled code that does not
 * use them.  They are listed in STATUS.md.
 *
 * Flags: 16-bit instructions that would set flags use setflags = !InITBlock(),
 * except CMP, CMN and TST, which always update the flags (Arm ARM: "16-bit
 * instructions in the IT block, other than CMP, CMN and TST, do not set the
 * condition flags").  The 16-bit high-register ADD and MOV never set flags
 * (their decode has setflags = FALSE), nor do the SP/PC adds.
 */

#include "n2a32.h"

/* Sign-extend the low `bits` bits of v to 32 bits (modulo-2^32 arithmetic). */
static uint32_t t16_sx(uint32_t v, unsigned bits)
{
	uint32_t m = 1u << (bits - 1u);

	return (v ^ m) - m;
}

static uint32_t t16_sx8(uint32_t v) { return t16_sx(v & 0xFFu, 8); }
static uint32_t t16_sx16(uint32_t v) { return t16_sx(v & 0xFFFFu, 16); }

static unsigned t16_popcount8(unsigned x)
{
	unsigned n = 0;

	while (x) {
		n += x & 1u;
		x >>= 1;
	}
	return n;
}

static unsigned t16_popcount4(unsigned x)
{
	return t16_popcount8(x & 0xFu);
}

/*
 * AddWithCarry with optional flag commit.  Used for ADD, SUB, ADC, SBC, RSB,
 * CMP and CMN.  With setflags == 0 the arithmetic result is still produced but
 * no architectural state is touched.
 */
static uint32_t t16_add(struct n2a32_cpu *c, uint32_t x, uint32_t y, uint32_t cin,
			int setflags)
{
	uint8_t carry, overflow;
	uint32_t r = n2a32_adc(x, y, cin, &carry, &overflow);

	if (setflags) {
		c->cf = carry;
		c->v = overflow;
		n2a32_set_nz(c, r);
	}
	return r;
}

/*
 * Shift and rotate, with the Arm ARM's special cases for an amount of zero:
 * LSL #0 and ROR #0 leave C unchanged; LSR/ASR #0 mean a shift of 32.
 * `type`: 0 LSL, 1 LSR, 2 ASR, 3 ROR.  `n` may be 0..255 for register shifts.
 * V is never changed by a shift; N, Z and (when defined) C are committed only
 * when setflags is true.
 */
static uint32_t t16_shift(struct n2a32_cpu *c, uint32_t x, unsigned n,
			  unsigned type, int setflags, int immediate)
{
	uint32_t r;
	uint8_t carry = c->cf;	/* LSL #0 / ROR #0: C unaffected */

	switch (type) {
	case 0:			/* LSL */
		if (n == 0) {
			r = x;
		} else if (n < 32) {
			carry = (uint8_t)((x >> (32u - n)) & 1u);
			r = x << n;
		} else if (n == 32) {
			carry = (uint8_t)(x & 1u);
			r = 0;
		} else {
			carry = 0;
			r = 0;
		}
		break;
	case 1:			/* LSR */
		if (n == 0) {
			if (!immediate) {	/* register form: no shift */
				r = x;
				break;
			}
			n = 32;
		}
		if (n < 32) {
			carry = (uint8_t)((x >> (n - 1u)) & 1u);
			r = x >> n;
		} else if (n == 32) {
			carry = (uint8_t)(x >> 31);
			r = 0;
		} else {
			carry = 0;
			r = 0;
		}
		break;
	case 2:			/* ASR */
		if (n == 0) {
			if (!immediate) {	/* register form: no shift */
				r = x;
				break;
			}
			n = 32;
		}
		if (n < 32) {
			carry = (uint8_t)((x >> (n - 1u)) & 1u);
			r = x >> n;
			if (x & 0x80000000u)
				r |= ~(0xFFFFFFFFu >> n);
		} else {
			carry = (uint8_t)(x >> 31);
			r = (x & 0x80000000u) ? 0xFFFFFFFFu : 0u;
		}
		break;
	default:		/* ROR */
		if (n == 0) {
			r = x;
		} else {
			unsigned e = n & 31u;

			if (e == 0) {
				r = x;
				carry = (uint8_t)(x >> 31);
			} else {
				r = (x >> e) | (x << (32u - e));
				carry = (uint8_t)(r >> 31);
			}
		}
		break;
	}
	if (setflags) {
		c->cf = carry;
		n2a32_set_nz(c, r);
	}
	return r;
}

/* Store the low halfword/byte of a register for STRH/STRB. */
static void t16_st16(struct n2a32_cpu *c, uint32_t addr, uint32_t v)
{
	n2a32_st16(c, addr, (uint16_t)v);
}

static void t16_st8(struct n2a32_cpu *c, uint32_t addr, uint32_t v)
{
	n2a32_st8(c, addr, (uint8_t)v);
}

/* 0xB000-0xB0FF: ADD/SUB SP plus immediate. */
static int t16_misc_sp_adjust(struct n2a32_cpu *c, uint32_t w)
{
	uint32_t imm = (uint32_t)(w & 0x7Fu) << 2;

	if ((w & 0x80u) == 0)
		c->r[13] += imm;
	else
		c->r[13] -= imm;
	return N2A32_OK;
}

/* 0xB100/0xB900: CBZ/CBNZ. */
static int t16_misc_cbz(struct n2a32_cpu *c, uint32_t w)
{
	unsigned nonzero = (w >> 11) & 1u;
	unsigned rn = w & 7u;
	uint32_t target = c->r[15] + (((uint32_t)((w >> 3) & 0x1Fu) << 1) |
				      (((w >> 9) & 1u) << 6));

	if (nonzero ? (c->r[rn] != 0) : (c->r[rn] == 0))
		n2a32_branch(c, target);
	return N2A32_OK;
}

/* 0xB200-0xB2FF: SXTH/SXTB/UXTH/UXTB. */
static int t16_misc_extend(struct n2a32_cpu *c, uint32_t w)
{
	unsigned rm = (w >> 3) & 7u, rd = w & 7u;
	uint32_t v = c->r[rm];

	switch ((w >> 6) & 3u) {
	case 0:
		c->r[rd] = t16_sx16(v);
		break;
	case 1:
		c->r[rd] = t16_sx8(v);
		break;
	case 2:
		c->r[rd] = v & 0xFFFFu;
		break;
	default:
		c->r[rd] = v & 0xFFu;
		break;
	}
	return N2A32_OK;
}

/* 0xB400-0xB5FF: PUSH {r0-r7[,lr]}. */
static int t16_misc_push(struct n2a32_cpu *c, uint32_t w)
{
	unsigned list = w & 0xFFu, m = (w >> 8) & 1u;
	unsigned count = t16_popcount8(list) + m;
	uint32_t addr = c->r[13] - 4u * count;
	unsigned i;

	for (i = 0; i < 8; i++) {
		if (list & (1u << i)) {
			n2a32_st32(c, addr, c->r[i]);
			addr += 4;
		}
	}
	if (m)
		n2a32_st32(c, addr, c->r[14]);
	c->r[13] -= 4u * count;
	return N2A32_OK;
}

/* 0xBC00-0xBDFF: POP {r0-r7[,pc]}; the PC value branches through BXWritePC. */
static int t16_misc_pop(struct n2a32_cpu *c, uint32_t w)
{
	unsigned list = w & 0xFFu, p = (w >> 8) & 1u;
	unsigned count = t16_popcount8(list) + p;
	uint32_t addr = c->r[13];
	uint32_t pc = 0;
	unsigned i;

	for (i = 0; i < 8; i++) {
		if (list & (1u << i)) {
			c->r[i] = n2a32_ld32(c, addr);
			addr += 4;
		}
	}
	if (p)
		pc = n2a32_ld32(c, addr);
	c->r[13] += 4u * count;
	if (p)
		n2a32_bx(c, pc);
	return N2A32_OK;
}

/* 0xBA00-0xBAFF minus HLT: REV, REV16, REVSH. */
static int t16_misc_rev(struct n2a32_cpu *c, uint32_t w)
{
	unsigned rm = (w >> 3) & 7u, rd = w & 7u;
	uint32_t x = c->r[rm];

	switch ((w >> 6) & 3u) {
	case 0:			/* REV */
		c->r[rd] = (x >> 24) | ((x >> 8) & 0x0000FF00u) |
			   ((x << 8) & 0x00FF0000u) | (x << 24);
		break;
	case 1:			/* REV16 */
		c->r[rd] = ((x & 0x00FF00FFu) << 8) |
			   ((x & 0xFF00FF00u) >> 8);
		break;
	case 2:			/* HLT: 0xBA80-0xBABF */
		return N2A32_UNDEF;
	default:		/* REVSH */
		c->r[rd] = t16_sx16(((x & 0xFFu) << 8) | ((x >> 8) & 0xFFu));
		break;
	}
	return N2A32_OK;
}

/* 0xBF00-0xBFFF: hints (mask == 0) and IT (mask != 0). */
static int t16_misc_it_hint(struct n2a32_cpu *c, uint32_t w)
{
	unsigned firstcond, mask;

	if ((w & 0xFu) == 0)	/* NOP, YIELD, WFE, WFI, SEV, reserved hints */
		return N2A32_OK;

	firstcond = (w >> 4) & 0xFu;
	mask = w & 0xFu;
	/* Both cases are UNPREDICTABLE; UNDEFINED is a permitted behaviour. */
	if (firstcond == 0xFu)
		return N2A32_UNDEF;
	if (firstcond == 0xEu && t16_popcount4(mask) != 1)
		return N2A32_UNDEF;

	/* ITSTATE<7:0> = firstcond:mask; the core advances it after each
	 * following instruction (ShouldAdvanceIT is FALSE for IT itself). */
	c->itstate = (uint8_t)w;
	return N2A32_OK;
}

/* 0xB000-0xBFFF dispatch. */
static int t16_misc(struct n2a32_cpu *c, uint32_t w)
{
	if ((w & 0xFF80u) == 0xB000u || (w & 0xFF80u) == 0xB080u)
		return t16_misc_sp_adjust(c, w);
	if ((w & 0xF500u) == 0xB100u)	/* CBZ 0xB1xx, CBNZ 0xB9xx (bit 9 = i) */
		return t16_misc_cbz(c, w);
	if ((w & 0xFF00u) == 0xB200u)
		return t16_misc_extend(c, w);
	if ((w & 0xFE00u) == 0xB400u)
		return t16_misc_push(c, w);
	if ((w & 0xFE00u) == 0xBC00u)
		return t16_misc_pop(c, w);
	if ((w & 0xFF00u) == 0xBA00u)
		return t16_misc_rev(c, w);
	if ((w & 0xFF00u) == 0xBE00u) {	/* BKPT */
		c->stop = N2A32_STOP_BKPT;
		c->stop_insn = w;
		c->stop_addr = c->r[15] - 4u;
		return N2A32_UNDEF;
	}
	if ((w & 0xFF00u) == 0xBF00u)
		return t16_misc_it_hint(c, w);
	/* SETEND/CPS and the reserved holes. */
	return N2A32_UNDEF;
}

/* 0x4000-0x43FF: data-processing (two low registers), 16 opcodes. */
static int t16_data_processing(struct n2a32_cpu *c, uint32_t w)
{
	unsigned op = (w >> 6) & 0xFu;
	unsigned rm = (w >> 3) & 7u;
	unsigned rd = w & 7u;
	uint32_t x = c->r[rd], y = c->r[rm], r;
	int sf = !c->in_it;

	switch (op) {
	case 0x0:		/* AND(S) */
		r = x & y;
		if (sf)
			n2a32_set_nz(c, r);
		c->r[rd] = r;
		break;
	case 0x1:		/* EOR(S) */
		r = x ^ y;
		if (sf)
			n2a32_set_nz(c, r);
		c->r[rd] = r;
		break;
	case 0x2:		/* LSL(S) (register) */
		c->r[rd] = t16_shift(c, x, y & 0xFFu, 0, sf, 0);
		break;
	case 0x3:		/* LSR(S) (register) */
		c->r[rd] = t16_shift(c, x, y & 0xFFu, 1, sf, 0);
		break;
	case 0x4:		/* ASR(S) (register) */
		c->r[rd] = t16_shift(c, x, y & 0xFFu, 2, sf, 0);
		break;
	case 0x5:		/* ADC(S) */
		c->r[rd] = t16_add(c, x, y, c->cf, sf);
		break;
	case 0x6:		/* SBC(S) */
		c->r[rd] = t16_add(c, x, ~y, c->cf, sf);
		break;
	case 0x7:		/* ROR(S) (register) */
		c->r[rd] = t16_shift(c, x, y & 0xFFu, 3, sf, 0);
		break;
	case 0x8:		/* TST: always sets N,Z */
		r = x & y;
		n2a32_set_nz(c, r);
		break;
	case 0x9:		/* RSB(S) Rdn, Rm, #0 */
		c->r[rd] = t16_add(c, ~y, 0, 1, sf);
		break;
	case 0xA:		/* CMP: always sets N,Z,C,V */
		(void)t16_add(c, x, ~y, 1, 1);
		break;
	case 0xB:		/* CMN: always sets N,Z,C,V */
		(void)t16_add(c, x, y, 0, 1);
		break;
	case 0xC:		/* ORR(S) */
		r = x | y;
		if (sf)
			n2a32_set_nz(c, r);
		c->r[rd] = r;
		break;
	case 0xD:		/* MUL(S): N,Z only */
		r = x * y;
		if (sf)
			n2a32_set_nz(c, r);
		c->r[rd] = r;
		break;
	case 0xE:		/* BIC(S) */
		r = x & ~y;
		if (sf)
			n2a32_set_nz(c, r);
		c->r[rd] = r;
		break;
	default:		/* MVN(S) */
		r = ~y;
		if (sf)
			n2a32_set_nz(c, r);
		c->r[rd] = r;
		break;
	}
	return N2A32_OK;
}

/* 0x4400-0x47FF: two-high-register ADD/CMP/MOV and BX/BLX. */
static int t16_special(struct n2a32_cpu *c, uint32_t w)
{
	unsigned op = (w >> 8) & 3u;
	unsigned rdn = (((w >> 7) & 1u) << 3) | (w & 7u);
	unsigned rs = (w >> 3) & 0xFu;

	switch (op) {
	case 0x0: {		/* ADD, or ADD (SP plus register) T1/T2 */
		uint32_t r;

		if (rs == 13u) {	/* ADD Rdm, SP, Rdm: setflags FALSE */
			r = c->r[13] + c->r[rdn];
			if (rdn == 15u)
				n2a32_branch(c, r);
			else
				c->r[rdn] = r;
		} else if (rdn == 13u) {	/* ADD SP, SP, Rm */
			c->r[13] += c->r[rs];
		} else {		/* ADD (register) T2: setflags FALSE */
			r = c->r[rdn] + c->r[rs];
			if (rdn == 15u)
				n2a32_branch(c, r);
			else
				c->r[rdn] = r;
		}
		return N2A32_OK;
	}
	case 0x1:		/* CMP (register) T2: always sets N,Z,C,V */
		(void)t16_add(c, c->r[rdn], ~c->r[rs], 1, 1);
		return N2A32_OK;
	case 0x2:		/* MOV (register) T1: setflags FALSE, PC branches */
		if (rdn == 15u)
			n2a32_bx(c, c->r[rs]);
		else
			c->r[rdn] = c->r[rs];
		return N2A32_OK;
	default:		/* BX / BLX (register) */
		if ((w & 7u) != 0)
			return N2A32_UNDEF;	/* bits 2:0 are fixed 000 */
		{
			uint32_t target = c->r[rs];	/* read before LR is written: BLX lr (opus-perf fix) */
			if ((w >> 7) & 1u)		/* BLX: LR = next insn | 1 */
				c->r[14] = ((c->r[15] - 2u) & ~1u) | 1u;
			n2a32_bx(c, target);
		}
		return N2A32_OK;
	}
}

/* 0x5000-0x5FFF: load/store with register offset, all sizes. */
static int t16_ldst_reg(struct n2a32_cpu *c, uint32_t w)
{
	unsigned rt = w & 7u;
	unsigned rn = (w >> 3) & 7u;
	unsigned rm = (w >> 6) & 7u;
	uint32_t addr = c->r[rn] + c->r[rm];

	switch ((w >> 9) & 7u) {
	case 0:			/* STR (register) */
		n2a32_st32(c, addr, c->r[rt]);
		break;
	case 1:			/* STRH (register) */
		t16_st16(c, addr, c->r[rt]);
		break;
	case 2:			/* STRB (register) */
		t16_st8(c, addr, c->r[rt]);
		break;
	case 3:			/* LDRSB (register) */
		c->r[rt] = t16_sx8(n2a32_ld8(c, addr));
		break;
	case 4:			/* LDR (register) */
		c->r[rt] = n2a32_ld32(c, addr);
		break;
	case 5:			/* LDRH (register) */
		c->r[rt] = n2a32_ld16(c, addr);
		break;
	case 6:			/* LDRB (register) */
		c->r[rt] = n2a32_ld8(c, addr);
		break;
	default:		/* LDRSH (register) */
		c->r[rt] = t16_sx16(n2a32_ld16(c, addr));
		break;
	}
	return N2A32_OK;
}

/* 0x6000-0x8FFF: load/store immediate offset, word/byte/halfword. */
static int t16_ldst_imm(struct n2a32_cpu *c, uint32_t w)
{
	unsigned group = w >> 12;
	unsigned rt = w & 7u;
	unsigned rn = (w >> 3) & 7u;
	unsigned imm = (w >> 6) & 0x1Fu;
	unsigned load = (w >> 11) & 1u;
	uint32_t addr = c->r[rn];

	if (group == 0x6u) {		/* STR/LDR (immediate, word) */
		addr += imm << 2;
		if (load)
			c->r[rt] = n2a32_ld32(c, addr);
		else
			n2a32_st32(c, addr, c->r[rt]);
	} else if (group == 0x7u) {	/* STRB/LDRB (immediate) */
		addr += imm;
		if (load)
			c->r[rt] = n2a32_ld8(c, addr);
		else
			t16_st8(c, addr, c->r[rt]);
	} else {			/* STRH/LDRH (immediate) */
		addr += imm << 1;
		if (load)
			c->r[rt] = n2a32_ld16(c, addr);
		else
			t16_st16(c, addr, c->r[rt]);
	}
	return N2A32_OK;
}

/* 0x9000-0x9FFF: load/store SP-relative. */
static int t16_ldst_sp(struct n2a32_cpu *c, uint32_t w)
{
	unsigned rt = (w >> 8) & 7u;
	uint32_t addr = c->r[13] + ((uint32_t)(w & 0xFFu) << 2);

	if ((w >> 11) & 1u)
		c->r[rt] = n2a32_ld32(c, addr);
	else
		n2a32_st32(c, addr, c->r[rt]);
	return N2A32_OK;
}

/* 0xA000-0xAFFF: ADR and ADD (SP plus immediate). */
static int t16_adr_addsp(struct n2a32_cpu *c, uint32_t w)
{
	unsigned rd = (w >> 8) & 7u;
	uint32_t imm = (uint32_t)(w & 0xFFu) << 2;

	if ((w >> 11) & 1u)
		c->r[rd] = c->r[13] + imm;
	else
		c->r[rd] = (c->r[15] & ~3u) + imm;
	return N2A32_OK;
}

/* 0xC000-0xCFFF: STMIA/LDMIA of r0-r7. */
static int t16_ldm_stm(struct n2a32_cpu *c, uint32_t w)
{
	unsigned rn = (w >> 8) & 7u;
	unsigned list = w & 0xFFu;
	unsigned count = t16_popcount8(list);
	uint32_t addr = c->r[rn];
	unsigned i;

	if ((w >> 11) & 1u) {		/* LDMIA */
		for (i = 0; i < 8; i++) {
			if (list & (1u << i)) {
				c->r[i] = n2a32_ld32(c, addr);
				addr += 4;
			}
		}
	} else {			/* STMIA */
		for (i = 0; i < 8; i++) {
			if (list & (1u << i)) {
				n2a32_st32(c, addr, c->r[i]);
				addr += 4;
			}
		}
	}
	/* Base in the list is architecturally UNPREDICTABLE.  Clang emits
	 * `ldm r7,{r2,r3,r7}` and relies on the loaded value surviving, so keep
	 * the load and suppress the writeback in that case. */
	if (!(list & (1u << rn)))
		c->r[rn] += 4u * count;
	return N2A32_OK;
}

/* 0xD000-0xDFFF: B<cond>, UDF, SVC. */
static int t16_bcond_except(struct n2a32_cpu *c, uint32_t w)
{
	unsigned cond = (w >> 8) & 0xFu;

	if (cond < 0xEu) {
		uint32_t off = (uint32_t)t16_sx(w & 0xFFu, 8) << 1;

		if (n2a32_cond(c, cond))
			n2a32_branch(c, c->r[15] + off);
		return N2A32_OK;
	}
	if (cond == 0xEu) {		/* UDF */
		c->stop = N2A32_STOP_UNDEF;
		c->stop_insn = w;
		c->stop_addr = c->r[15] - 4u;
		return N2A32_UNDEF;
	}
	/* SVC */
	c->stop = N2A32_STOP_SVC;
	c->stop_insn = w;
	c->stop_addr = c->r[15] - 4u;
	return N2A32_UNDEF;
}

int n2a32_t16(struct n2a32_cpu *c, uint32_t insn)
{
	uint32_t w;
	unsigned rn, rd, rm, rt;

	if (insn > 0xFFFFu)
		return N2A32_UNDEF;	/* the core passes a 16-bit unit */
	w = insn;
	if ((w & 0xF800u) >= 0xE800u)	/* 11101/11110/11111: 32-bit */
		return N2A32_UNDEF;

	switch (w >> 12) {
	case 0x0:
	case 0x1:
		switch (w >> 11) {
		case 0x0:		/* LSL (immediate) */
		case 0x1:		/* LSR (immediate) */
		case 0x2:		/* ASR (immediate) */
			rd = w & 7u;
			rm = (w >> 3) & 7u;
			c->r[rd] = t16_shift(c, c->r[rm], (w >> 6) & 0x1Fu,
					     w >> 11, !c->in_it, 1);
			return N2A32_OK;
		default: {		/* ADD/SUB (register or imm3) */
			unsigned op = (w >> 9) & 3u;
			uint32_t x = (w >> 6) & 7u;
			uint32_t y;

			rn = (w >> 3) & 7u;
			rd = w & 7u;
			y = (op & 2u) ? x : c->r[x];
			c->r[rd] = t16_add(c, c->r[rn], (op & 1u) ? ~y : y,
					   op & 1u, !c->in_it);
			return N2A32_OK;
		}
		}
	case 0x2:
	case 0x3: {
		uint32_t imm = w & 0xFFu;

		rd = (w >> 8) & 7u;	/* MOV/CMP/ADD/SUB imm8: Rd is 10:8 */
		switch (w >> 11) {
		case 0x4:		/* MOV (immediate) */
			c->r[rd] = imm;
			if (!c->in_it)
				n2a32_set_nz(c, imm);
			return N2A32_OK;
		case 0x5:		/* CMP (immediate): always flags */
			(void)t16_add(c, c->r[rd], ~imm, 1, 1);
			return N2A32_OK;
		case 0x6:		/* ADD (immediate) */
			c->r[rd] = t16_add(c, c->r[rd], imm, 0, !c->in_it);
			return N2A32_OK;
		default:		/* SUB (immediate) */
			c->r[rd] = t16_add(c, c->r[rd], ~imm, 1, !c->in_it);
			return N2A32_OK;
		}
	}
	case 0x4:
		switch ((w >> 10) & 3u) {
		case 0x0:
			return t16_data_processing(c, w);
		case 0x1:
			return t16_special(c, w);
		default:		/* LDR (literal) */
			rt = (w >> 8) & 7u;
			c->r[rt] = n2a32_ld32(c, (c->r[15] & ~3u) +
					      ((uint32_t)(w & 0xFFu) << 2));
			return N2A32_OK;
		}
	case 0x5:
		return t16_ldst_reg(c, w);
	case 0x6:
	case 0x7:
	case 0x8:
		return t16_ldst_imm(c, w);
	case 0x9:
		return t16_ldst_sp(c, w);
	case 0xA:
		return t16_adr_addsp(c, w);
	case 0xB:
		return t16_misc(c, w);
	case 0xC:
		return t16_ldm_stm(c, w);
	case 0xD:
		return t16_bcond_except(c, w);
	default:		/* 0xE: B (unconditional); 0xF is a 32-bit prefix */
		if ((w & 0xF800u) != 0xE000u)
			return N2A32_UNDEF;
		n2a32_branch(c, c->r[15] +
			     ((uint32_t)t16_sx(w & 0x7FFu, 11) << 1));
		return N2A32_OK;
	}
}
