/* SPDX-License-Identifier: MIT */
/*
 * n2a32_arm_mem.c -- V12: A32 memory-access instructions of the n2a32 interpreter.
 *
 * The core has already evaluated the condition and set c->r[15] (instruction address + 8),
 * c->next_pc (address of the following instruction) and c->in_it.  This file returns
 * N2A32_OK or N2A32_UNDEF only; it never aborts, prints or guesses.
 *
 * Implemented (ARM ARM: A5.3, A5.5, A8.8.32, A8.8.55-58, A8.8.66-77, A8.8.93-96,
 * A8.8.120-125, A8.8.147-148):
 *   LDR/STR/LDRB/STRB   immediate (incl. literal through pc), shifted register, pre/post
 *                       index, writeback, unprivileged LDRT/STRT/LDRBT/STRBT;
 *                       LDR with Rt=15 branches through n2a32_bx().
 *   LDRH/STRH/LDRSB/LDRSH/LDRD/STRD  immediate and register offsets.
 *   LDM/STM             IA/IB/DA/DB, writeback, POP/PUSH; a PC in the list branches
 *                       through n2a32_bx().
 *   LDREX/STREX, LDREXB/H/D, STREXB/H/D through n2a32_excl_mark()/n2a32_excl_store();
 *                       CLREX opens the local monitor.
 *   PLD/PLI/PLDW        address-generation only, no memory access, no effect.
 *
 * No instruction in this file writes the condition flags.
 *
 * Deliberately N2A32_UNDEF (architecturally UNPREDICTABLE/UNALLOCATED forms, listed in
 * test_v12/STATUS.md): byte/halfword/signed/doubleword transfer with Rt=15, LDRD/STRD
 * with an odd Rt or Rt=14, LDM/STM with the S (user-register) bit set, LDREX/STREX with
 * any register 15, and every word in the spaces above that is not an encoding below.
 */
#include "n2a32.h"

/* ------------------------------------------------------------------ helpers */

static unsigned bitcount16(unsigned v)
{
	unsigned n = 0;
	while (v) { n += v & 1u; v >>= 1; }
	return n;
}

/* LSR/ASR with an amount of 0 meaning 32.  No C undefined behaviour. */
static uint32_t lsr32(uint32_t v, unsigned n)
{
	return n >= 32 ? 0u : v >> n;
}

static uint32_t asr32(uint32_t v, unsigned n)
{
	uint32_t sign = v & 0x80000000u;
	if (n >= 32) return sign ? 0xFFFFFFFFu : 0u;
	return (v >> n) | (sign ? (0xFFFFFFFFu << (32 - n)) : 0u);
}

static uint32_t ror32(uint32_t v, unsigned n)
{
	return n ? ((v >> n) | (v << (32 - n))) : v;
}

/* Shifted register operand of the load/store register form (A5.3.2): imm5, type, Rm.
 * type 0 LSL, 1 LSR, 2 ASR, 3 ROR; amount 0 means LSL #0, LSR #32, ASR #32, RRX. */
static uint32_t shifted_rm(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned rm = insn & 0xFu;
	unsigned type = (insn >> 5) & 3u;
	unsigned amount = (insn >> 7) & 0x1Fu;
	uint32_t v = c->r[rm];

	switch (type) {
	case 0: return v << amount;                       /* LSL #0 is identity */
	case 1: return lsr32(v, amount ? amount : 32u);
	case 2: return asr32(v, amount ? amount : 32u);
	default:
		if (amount == 0) return (v >> 1) | ((uint32_t)c->cf << 31); /* RRX */
		return ror32(v, amount);
	}
}

/* ------------------------------------------------------------------ load/store single */

static int do_ldr_str(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned p = (insn >> 24) & 1u, u = (insn >> 23) & 1u;
	unsigned b = (insn >> 22) & 1u, w = (insn >> 21) & 1u, l = (insn >> 20) & 1u;
	unsigned rn = (insn >> 16) & 0xFu, rt = (insn >> 12) & 0xFu;
	unsigned base = c->r[rn], offset, addr, newbase;
	int wback, apply_off, unpriv, reg_form;

	reg_form = (insn >> 25) & 1u;
	if (reg_form) {
		if (insn & 0x10u) return N2A32_UNDEF;    /* bit 4 set: media space, not us */
		offset = shifted_rm(c, insn);
	} else {
		offset = insn & 0xFFFu;
	}
	/* The immediate form with P=0,W=1 is LDRT/STRT/LDRBT/STRBT: the offset is applied to
	 * the address and nothing is written back (A8.8.95, A8.8.148).  There is no unprivileged
	 * register form, so P=0,W=1 there is an ordinary post-indexed writeback.  P=0,W=0 is
	 * ordinary post-indexing: access at Rn, then Rn += offset.  P=1,W=1: pre-index. */
	unpriv = !p && w && !reg_form;
	apply_off = p || unpriv;
	wback = p ? w : (unpriv ? 0 : 1);
	addr = apply_off ? (u ? base + offset : base - offset) : base;
	newbase = u ? base + offset : base - offset;

	if (l) {
		uint32_t v;
		if (b) {
			if (rt == 15) return N2A32_UNDEF;    /* byte load to pc: UNPREDICTABLE */
			v = n2a32_ld8(c, addr);
			c->r[rt] = v;
		} else {
			v = n2a32_ld32(c, addr);
			if (rt == 15) {
				if (wback && rn != 15) c->r[rn] = newbase;
				n2a32_bx(c, v);                  /* LDR pc: interworking branch */
				return N2A32_OK;
			}
			c->r[rt] = v;
		}
	} else {
		if (b) {
			if (rt == 15) return N2A32_UNDEF;    /* byte store of pc: UNPREDICTABLE */
			n2a32_st8(c, addr, (uint8_t)c->r[rt]);
		} else {
			n2a32_st32(c, addr, c->r[rt]);       /* STR pc stores address+8 (r[15]) */
		}
	}
	if (wback && rn != 15) c->r[rn] = newbase;
	return N2A32_OK;
}

/* ------------------------------------------------------------------ PLD / PLI / PLDW */

/* The preload hints exist only in the unconditional (cond=0xF) load/store space with
 * Rt=15 (A8.8.99, A8.8.100, A8.8.101).  They generate an address and have no other effect. */
static int do_preload(uint32_t insn)
{
	unsigned p = (insn >> 24) & 1u, b = (insn >> 22) & 1u, w = (insn >> 21) & 1u;
	unsigned l = (insn >> 20) & 1u, rt = (insn >> 12) & 0xFu;
	unsigned reg = (insn >> 25) & 1u;

	if (rt != 15 || l != 1 || w != 0) return N2A32_UNDEF;
	if (reg && (insn & 0x10u)) return N2A32_UNDEF;
	/* (P,B): 11 PLD, 01 PLI, 10 PLDW.  (0,0) is not allocated. */
	if (p && b) return N2A32_OK;                     /* PLD  */
	if (!p && b) return N2A32_OK;                    /* PLI  */
	if (p && !b) return N2A32_OK;                    /* PLDW */
	return N2A32_UNDEF;
}

/* ------------------------------------------------------------------ exclusives */

static int is_exclusive(uint32_t insn)
{
	switch ((insn >> 20) & 0xFFu) {
	case 0x19: return (insn & 0xFFFu) == 0xF9Fu;     /* LDREX  */
	case 0x1D: return (insn & 0xFFFu) == 0xF9Fu;     /* LDREXB */
	case 0x1F: return (insn & 0xFFFu) == 0xF9Fu;     /* LDREXH */
	case 0x1B: return (insn & 0xFFFu) == 0xF9Fu;     /* LDREXD */
	case 0x18: return (insn & 0xFF0u) == 0xF90u;     /* STREX  */
	case 0x1C: return (insn & 0xFF0u) == 0xF90u;     /* STREXB */
	case 0x1E: return (insn & 0xFF0u) == 0xF90u;     /* STREXH */
	case 0x1A: return (insn & 0xFF0u) == 0xF90u;     /* STREXD */
	default: return 0;
	}
}

static int do_exclusive(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned op = (insn >> 20) & 0xFFu;
	unsigned rn = (insn >> 16) & 0xFu;
	unsigned rd = (insn >> 12) & 0xFu;   /* Rt for loads, Rd (status) for stores */
	unsigned st_rt = insn & 0xFu;        /* stored register for STREX */
	uint32_t addr = c->r[rn];

	if (rd == 15) return N2A32_UNDEF;
	if ((op == 0x18u || op == 0x1Cu || op == 0x1Eu || op == 0x1Au) && st_rt == 15)
		return N2A32_UNDEF;

	switch (op) {
	case 0x19: {                                     /* LDREX */
		uint32_t v = n2a32_ld32(c, addr);
		n2a32_excl_mark(c, addr, 4, v);
		c->r[rd] = v;
		return N2A32_OK;
	}
	case 0x1D: {                                     /* LDREXB */
		uint32_t v = n2a32_ld8(c, addr);
		n2a32_excl_mark(c, addr, 1, v);
		c->r[rd] = v;
		return N2A32_OK;
	}
	case 0x1F: {                                     /* LDREXH */
		uint32_t v = n2a32_ld16(c, addr);
		n2a32_excl_mark(c, addr, 2, v);
		c->r[rd] = v;
		return N2A32_OK;
	}
	case 0x1B: {                                     /* LDREXD */
		uint64_t v;
		if (rd & 1u || rd > 12u) return N2A32_UNDEF;  /* Rt even, Rt+1 <= 14 */
		v = n2a32_ld64(c, addr);
		n2a32_excl_mark(c, addr, 8, v);
		c->r[rd] = (uint32_t)v;
		c->r[rd + 1] = (uint32_t)(v >> 32);
		return N2A32_OK;
	}
	case 0x18: {                                     /* STREX */
		c->r[rd] = (uint32_t)n2a32_excl_store(c, addr, 4, c->r[st_rt]);
		return N2A32_OK;
	}
	case 0x1C: {                                     /* STREXB */
		c->r[rd] = (uint32_t)n2a32_excl_store(c, addr, 1, c->r[st_rt]);
		return N2A32_OK;
	}
	case 0x1E: {                                     /* STREXH */
		c->r[rd] = (uint32_t)n2a32_excl_store(c, addr, 2, c->r[st_rt]);
		return N2A32_OK;
	}
	case 0x1A: {                                     /* STREXD */
		uint64_t v;
		if (st_rt & 1u || st_rt > 12u) return N2A32_UNDEF;
		v = (uint64_t)c->r[st_rt] | ((uint64_t)c->r[st_rt + 1] << 32);
		c->r[rd] = (uint32_t)n2a32_excl_store(c, addr, 8, v);
		return N2A32_OK;
	}
	default:
		return N2A32_UNDEF;
	}
}

/* CLREX: open the local monitor (A8.8.32).  The monitor's state lives in c->excl_valid;
 * there is no n2a32_excl_clear() in the frozen interface. */
static int do_clrex(struct n2a32_cpu *c)
{
	c->excl_valid = 0;
	return N2A32_OK;
}

/* ------------------------------------------------------------------ extra load/store */

static int do_extra(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned p = (insn >> 24) & 1u, u = (insn >> 23) & 1u;
	unsigned i = (insn >> 22) & 1u, w = (insn >> 21) & 1u;
	unsigned l = (insn >> 20) & 1u;
	unsigned rn = (insn >> 16) & 0xFu, rt = (insn >> 12) & 0xFu;
	unsigned s = (insn >> 6) & 1u, h = (insn >> 5) & 1u;
	unsigned shape = (s << 1) | h;
	uint32_t base = c->r[rn], offset, addr, newbase;
	int wback = (!p || w);

	if (i) {
		offset = (uint32_t)(((insn >> 8) & 0xFu) << 4) | (insn & 0xFu);
	} else {
		if (insn & 0xF00u) return N2A32_UNDEF;   /* register form requires 0000 in 11-8 */
		offset = c->r[insn & 0xFu];
	}
	addr = p ? (u ? base + offset : base - offset) : base;
	newbase = u ? base + offset : base - offset;

	switch (shape) {
	case 1:                                          /* S=0,H=1: LDRH/STRH */
		if (rt == 15) return N2A32_UNDEF;
		if (l) c->r[rt] = n2a32_ld16(c, addr);
		else n2a32_st16(c, addr, (uint16_t)c->r[rt]);
		break;
	case 2:                                          /* S=1,H=0: LDRSB (L=1) / LDRD (L=0) */
		if (l) {
			if (rt == 15) return N2A32_UNDEF;
			c->r[rt] = (uint32_t)(int32_t)(int8_t)n2a32_ld8(c, addr);
		} else {
			uint64_t v;
			if (rt & 1u || rt > 12u) return N2A32_UNDEF;
			v = n2a32_ld64(c, addr);
			c->r[rt] = (uint32_t)v;
			c->r[rt + 1] = (uint32_t)(v >> 32);
		}
		break;
	case 3:                                          /* S=1,H=1: LDRSH (L=1) / STRD (L=0) */
		if (l) {
			if (rt == 15) return N2A32_UNDEF;
			c->r[rt] = (uint32_t)(int32_t)(int16_t)n2a32_ld16(c, addr);
		} else {
			if (rt & 1u || rt > 12u) return N2A32_UNDEF;
			n2a32_st64(c, addr, (uint64_t)c->r[rt] | ((uint64_t)c->r[rt + 1] << 32));
		}
		break;
	default:                                         /* S=0,H=0: unallocated (exclusive space) */
		return N2A32_UNDEF;
	}
	if (wback && rn != 15) c->r[rn] = newbase;
	return N2A32_OK;
}

/* ------------------------------------------------------------------ LDM / STM */

static int do_ldm_stm(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned p = (insn >> 24) & 1u, u = (insn >> 23) & 1u;
	unsigned s = (insn >> 22) & 1u, w = (insn >> 21) & 1u, l = (insn >> 20) & 1u;
	unsigned rn = (insn >> 16) & 0xFu;
	unsigned list = insn & 0xFFFFu;
	unsigned n = bitcount16(list);
	uint32_t base = c->r[rn], addr;
	uint32_t newbase;

	if (s) return N2A32_UNDEF;                       /* user-register form: not modelled */
	if (u) addr = p ? base + 4u : base;              /* IB / IA */
	else addr = p ? base - 4u * n : base - 4u * n + 4u;  /* DB / DA */
	newbase = u ? base + 4u * n : base - 4u * n;

	if (l) {
		uint32_t pcv = 0;
		unsigned i;
		for (i = 0; i < 15; i++) {
			if (list & (1u << i)) {
				c->r[i] = n2a32_ld32(c, addr);
				addr += 4;
			}
		}
		if (list & 0x8000u) pcv = n2a32_ld32(c, addr);
		/* ARMv7: the base is not written back when it is itself in the list. */
		if (w && !(list & (1u << rn))) c->r[rn] = newbase;
		if (list & 0x8000u) {
			n2a32_bx(c, pcv);                    /* LDM with pc: interworking branch */
		}
	} else {
		unsigned i;
		for (i = 0; i < 16; i++) {
			if (list & (1u << i)) {
				n2a32_st32(c, addr, c->r[i]);
				addr += 4;
			}
		}
		if (w) c->r[rn] = newbase;
	}
	return N2A32_OK;
}

/* ------------------------------------------------------------------ entry point */

int n2a32_arm_mem(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned cond = insn >> 28;

	/* CLREX in the unconditional barrier space.  The core normally routes it to arm_misc;
	 * both entry points accept it.  It has to be recognised before the bits 27-26 == 01
	 * test below, which it would otherwise enter as a malformed preload. */
	if ((insn & 0xFFFFFF00u) == 0xF57FF000u && ((insn >> 4) & 0xFu) == 1u)
		return do_clrex(c);
	/* LDM/STM: bits 27-25 == 100 (cond=1111 is the Advanced SIMD space). */
	if ((insn & 0x0E000000u) == 0x08000000u) {
		if (cond == 15) return N2A32_UNDEF;
		return do_ldm_stm(c, insn);
	}
	/* Load/store word or unsigned byte: bits 27-26 == 01.  With bit25=1 and bit4=1 this
	 * word belongs to the media class (V11), which is why the core must not route it here;
	 * the check below keeps a stray call harmless. */
	if ((insn & 0x0C000000u) == 0x04000000u) {
		if (cond == 15) return do_preload(insn);
		if (((insn >> 25) & 1u) && (insn & 0x10u)) return N2A32_UNDEF;
		return do_ldr_str(c, insn);
	}
	/* Extra load/store, exclusives and (defensively) CLREX: bits 27-25 == 000, bits 7 and 4 set. */
	if ((insn & 0x0E000090u) == 0x00000090u) {
		if (is_exclusive(insn)) return do_exclusive(c, insn);
		if (cond == 15) return N2A32_UNDEF;
		return do_extra(c, insn);
	}
	return N2A32_UNDEF;
}
