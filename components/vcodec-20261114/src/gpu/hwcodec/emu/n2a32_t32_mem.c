/* SPDX-License-Identifier: MIT */
/*
 * n2a32_t32_mem.c -- V16: 32-bit Thumb (T32) memory-access instructions of the n2a32 interpreter.
 *
 * Owner: hw-v16 (package V16).  Implements n2a32_t32_mem() of include/n2a32.h.  The core has
 * already fetched the two halfwords, evaluated the condition (IT state) and set c->r[15]
 * (instruction address + 4), c->next_pc (address of the following instruction) and c->in_it.
 * This file returns N2A32_OK or N2A32_UNDEF only; it never aborts, prints or guesses.  Every
 * guest memory access goes through n2a32_ld8/16/32/64 and n2a32_st8/16/32/64, every branch
 * through n2a32_branch() (TBB/TBH) or n2a32_bx() (LDR pc, LDM with pc).  No instruction here
 * writes the condition flags.
 *
 * The 32-bit guest word is (first halfword << 16) | second halfword, as the core builds it.
 * Three encoding regions belong to this class (A5.3.4, A5.3.5, A5.3.7 of the ARM ARM):
 *
 *   A  (hw1 & 0xFE00) == 0xE800      0xE800-0xE9FF: LDM/STM, LDRD/STRD, LDREX/STREX, TBB/TBH.
 *        hw1 bit 6 == 0: LDM/STM.  (bit 8,bit 7) = (0,1): IA, (1,0): DB.
 *        hw1 bit 5 = W (writeback), bit 4 = L (load).
 *        hw1 bit 6 == 1: load/store dual, exclusive and table branch, with P = bit 8,
 *        U = bit 7, W = bit 5, L = bit 4:
 *          (P,U,W) = (0,0,0)  STREX (L=0) / LDREX (L=1, second halfword bits 11-8 == 1111)
 *          (P,U,W) = (0,1,0)  second halfword bits 15-12: 4 LDREXB/STREXB, 5 LDREXH/STREXH,
 *                             7 LDREXD/STREXD, 0xF TBB/TBH; L selects load (1) / store (0)
 *          P=1 (W selects pre-indexed writeback) or P=0,W=1 (post-indexed): LDRD/STRD
 *
 *   B  (hw1 & 0x0E00) == 0x0800      0xF800-0xF9FF: load/store single data item.
 *        op = hw1 bits 7-4: 1 STRB, 2 STRH, 3 LDRH, 4 STR, 5 LDR (immediate-8/register forms);
 *        8 STRB, 9 LDRB, A STRH, B LDRH, C STR, D LDR (immediate-12 forms).
 *        hw1 bit 8 (0xF9 instead of 0xF8) makes a byte/halfword load sign-extending (LDRSB
 *        with op 1/9, LDRSH with op 3/B); for a word load it is not allocated (0xF9C/0xF9D).
 *        Rt == 1111 in a byte/halfword load slot is PLD/PLDW/PLI (a hint: address generation
 *        only, no memory access).
 *        Immediate-12 forms (op 8-13): offset = hw2 bits 11-0, always added, no writeback;
 *        hw1 bit 8 selects the 0xF9 (sign-extending byte/halfword load) group.
 *        Rn == 1111 makes it the literal form: base = Align(PC,4), offset = hw2 bits 11-0;
 *        added for the immediate-12 forms (op 8-13) and subtracted for the op 1/3/5
 *        literal variants (which the assembler uses for negative offsets).  Only loads
 *        and the preloads exist.
 *        Immediate-8 forms (hw2 bit 11 == 1): P = bit 10, U = bit 9, W = bit 8, offset = hw2
 *        bits 7-0.  (P,U,W) = (1,1,0) is the unprivileged LDRT/STRT family (offset applied,
 *        no writeback); (0,*,0) is not allocated.  LDR pc branches.
 *        Register forms (hw2 bit 11 == 0, hw2 bits 10-6 == 0): offset = Rm << imm2, always
 *        added, no writeback.
 *
 * Deliberately N2A32_UNDEF (unallocated or architecturally UNPREDICTABLE; listed in
 * test_v16/STATUS.md):
 *   - LDM/STM with Rn == 1111, an empty register list, or SP in the list;
 *   - LDRD/STRD with Rt or Rt2 == 1111, or a literal form with writeback (the P=0,W=0
 *     pattern belongs to the word LDREX/STREX encoding, not to a dual transfer);
 *   - LDREX/STREX (any width) with a register 15 in any field, and LDREXD/STREXD with an
 *     odd Rt, Rt > 12, or an encoded Rt2 != Rt + 1;
 *   - TBB/TBH with Rm == 1111;
 *   - byte/halfword stores with Rt == 1111;
 *   - the word-size 0xF9C/0xF9D encodings (no sign-extending word load exists), op 0/6/7/E/F,
 *     the register form with Rm == 1111 or hw2 bits 10-6 != 0, and the unallocated
 *     (P,U,W) = (0,0,0)/(0,1,0) combinations of the load/store-single immediate-8 form;
 *   - anything outside regions A and B.
 *
 * LDRD/STRD execute the encoded register pair even when Rt is odd or Rt2 != Rt + 1.  The
 * ARMv7 wording calls that UNPREDICTABLE, but ARMv8-A hardware and every assembler accept
 * it and the phone's libraries use it heavily (e.g. `strd r3, r1, [sp, #8]`); Rt == Rt2
 * loads Rt2 last / stores the same value twice.
 *
 * Not modelled: address alignment checks (the guest memory layer is byte-addressed and the
 * real SCTLR.A setting is not known) and the "translate" bit of the unprivileged forms (there
 * is no MMU).  Rn == Rt with writeback is executed as access-then-writeback, one of the
 * architecturally UNPREDICTABLE outcomes.
 */
#include "n2a32.h"

/* ------------------------------------------------------------------ helpers */

static unsigned bitcount16(unsigned v)
{
	unsigned n = 0;

	while (v) {
		n += v & 1u;
		v >>= 1;
	}
	return n;
}

static uint32_t apply_u(uint32_t base, uint32_t off, unsigned u)
{
	return u ? base + off : base - off;
}

/* ------------------------------------------------------------------ load/store single */

/* Region B. */
static int single_transfer(struct n2a32_cpu *c, unsigned hw1, unsigned hw2)
{
	unsigned rn = hw1 & 0xFu;
	unsigned op = (hw1 >> 4) & 0xFu;
	unsigned size = (op >> 1) & 3u;         /* 0 byte, 1 halfword, 2 word */
	unsigned load = op & 1u;
	unsigned imm12_form = (op >> 3) & 1u;
	unsigned sign = (hw1 >> 8) & 1u;        /* 0xF9 instead of 0xF8 */
	unsigned rt = (hw2 >> 12) & 0xFu;
	uint32_t base, addr, newbase = 0;
	unsigned wback = 0;

	/* Only op 0-5 (immediate-8 / register) and 8-13 (immediate-12) are allocated; 6, 7,
	 * 14 and 15 are the size-11 holes and are rejected below as well. */
	if ((op >= 6u && op <= 7u) || op >= 14u)
		return N2A32_UNDEF;
	if (size == 3u)
		return N2A32_UNDEF;
	if (sign && !load)
		return N2A32_UNDEF;             /* no sign-extending store */
	if (sign && size == 2u)
		return N2A32_UNDEF;             /* no sign-extending word load */

	/* Preload hints: byte/halfword load slots with Rt == 1111 are PLD/PLDW/PLI.  They only
	 * generate an address and have no architectural effect; accept every addressing form
	 * the assembler produces (immediate 12, immediate 8 including its offset variant, and
	 * register) so that a hint can never stop the guest. */
	if (load && size != 2u && rt == 15u)
		return N2A32_OK;

	if (rn == 15u) {
		/* Literal form: only loads exist; base = Align(PC,4).  The assembler encodes a
		 * negative offset in the immediate-8 op slot (U = 0) and a positive one in the
		 * immediate-12 slot (U = 1), so U is op bit 7 here, not hw1 bit 8. */
		if (!load)
			return N2A32_UNDEF;
		base = c->r[15] & ~3u;
		addr = apply_u(base, hw2 & 0xFFFu, imm12_form);
	} else if (imm12_form) {
		/* Immediate-12 forms always add (there is no U bit in this encoding). */
		addr = c->r[rn] + (hw2 & 0xFFFu);
	} else if (hw2 & 0x0800u) {
		/* Immediate 8: P = bit 10, U = bit 9, W = bit 8. */
		unsigned p = (hw2 >> 10) & 1u, uu = (hw2 >> 9) & 1u, w = (hw2 >> 8) & 1u;
		uint32_t off = hw2 & 0xFFu;
		unsigned unpriv;

		if (!p && !w)
			return N2A32_UNDEF;     /* (0,*,0): not allocated */
		unpriv = (p && !w && uu);       /* (1,1,0): LDRT/STRT/LDRBT/STRBT/... */
		base = c->r[rn];
		addr = (p || unpriv) ? apply_u(base, off, uu) : base;
		newbase = apply_u(base, off, uu);
		wback = (w && !unpriv) ? 1u : 0u;
	} else {
		/* Register form: hw2 bits 10-6 zero, Rm << imm2, always added. */
		unsigned rm, shift;

		if (hw2 & 0x07C0u)
			return N2A32_UNDEF;
		rm = hw2 & 0xFu;
		shift = (hw2 >> 4) & 3u;
		if (rm == 15u)
			return N2A32_UNDEF;     /* UNPREDICTABLE */
		addr = c->r[rn] + (c->r[rm] << shift);
	}

	if (load) {
		uint32_t v;

		switch (size) {
		case 0:
			v = n2a32_ld8(c, addr);
			if (sign)
				v = (uint32_t)(int32_t)(int8_t)v;
			break;
		case 1:
			v = n2a32_ld16(c, addr);
			if (sign)
				v = (uint32_t)(int32_t)(int16_t)v;
			break;
		default:
			v = n2a32_ld32(c, addr);
			break;
		}
		if (rt == 15u) {                /* only the word load reaches here */
			if (wback)
				c->r[rn] = newbase;
			n2a32_bx(c, v);         /* LDR pc: interworking branch */
			return N2A32_OK;
		}
		c->r[rt] = v;
	} else {
		if (rt == 15u && size != 2u)
			return N2A32_UNDEF;     /* byte/halfword store of PC: UNPREDICTABLE */
		switch (size) {
		case 0:
			n2a32_st8(c, addr, (uint8_t)c->r[rt]);
			break;
		case 1:
			n2a32_st16(c, addr, (uint16_t)c->r[rt]);
			break;
		default:
			n2a32_st32(c, addr, c->r[rt]);   /* STR pc stores c->r[15] */
			break;
		}
	}
	if (wback)
		c->r[rn] = newbase;
	return N2A32_OK;
}

/* ------------------------------------------------------------------ LDM / STM */

/* Region A with hw1 bit 6 == 0.  IA when hw1 bit 7 == 1 (and bit 8 == 0), DB when bit 8 == 1
 * (and bit 7 == 0).  hw1 bit 5 = W, bit 4 = L.  ARM ARM A8.8.53/54: the writeback is applied
 * after the transfers and uses the (possibly loaded) base register; for a base that is itself
 * in the list that is the architecturally UNPREDICTABLE case, and the pseudocode behaviour is
 * followed here. */
static int ldm_stm(struct n2a32_cpu *c, unsigned hw1, unsigned hw2)
{
	unsigned w = (hw1 >> 5) & 1u, l = (hw1 >> 4) & 1u;
	unsigned rn = hw1 & 0xFu;
	unsigned list = hw2 & 0xFFFFu;
	unsigned n = bitcount16(list);
	unsigned db = ((hw1 & 0x0180u) == 0x0100u) ? 1u : 0u;
	uint32_t base = c->r[rn], addr, newbase;

	if (rn == 15u)
		return N2A32_UNDEF;             /* UNPREDICTABLE */
	if (list == 0u)
		return N2A32_UNDEF;             /* at least one register */
	if (list & (1u << 13))
		return N2A32_UNDEF;             /* SP in the list: UNPREDICTABLE */

	newbase = db ? base - 4u * n : base + 4u * n;
	addr = db ? newbase : base;

	if (l) {
		uint32_t pcv = 0;
		unsigned i;

		for (i = 0; i < 15u; i++) {
			if (list & (1u << i)) {
				c->r[i] = n2a32_ld32(c, addr);
				addr += 4u;
			}
		}
		if (list & 0x8000u)
			pcv = n2a32_ld32(c, addr);
		if (w)
			c->r[rn] = db ? c->r[rn] - 4u * n : c->r[rn] + 4u * n;
		if (list & 0x8000u) {
			n2a32_bx(c, pcv);       /* LDM with pc: interworking branch */
			return N2A32_OK;
		}
	} else {
		unsigned i;

		for (i = 0; i < 16u; i++) {
			if (list & (1u << i)) {
				n2a32_st32(c, addr, c->r[i]);
				addr += 4u;
			}
		}
		if (w)
			c->r[rn] = newbase;
	}
	return N2A32_OK;
}

/* ------------------------------------------------------------------ exclusive / dual / table */

/* Region A with hw1 bit 6 == 1.
 *   P = bit 8, U = bit 7, W = bit 5, L = bit 4, Rn = bits 3-0.
 *   (0,0,0): STREX (L=0) / LDREX (L=1)
 *   (0,1,0): LDREXB/H/D, STREXB/H/D (hw2 bits 15-12 = 4/5/7) and TBB/TBH (15-12 = 0xF)
 *   P=1 (offset or pre-indexed with W) or P=0,W=1 (post-indexed): LDRD / STRD.
 */
static int dual_excl(struct n2a32_cpu *c, unsigned hw1, unsigned hw2)
{
	unsigned rn = hw1 & 0xFu;
	unsigned p = (hw1 >> 8) & 1u, u = (hw1 >> 7) & 1u;
	unsigned w = (hw1 >> 5) & 1u, l = (hw1 >> 4) & 1u;
	unsigned hi4 = (hw2 >> 12) & 0xFu;      /* STREX/LDREX word Rt, LDRD Rt */
	unsigned f8 = (hw2 >> 8) & 0xFu;        /* LDREX 1111 / STREX status / LDRD Rt2 */
	unsigned imm8 = hw2 & 0xFFu;
	uint32_t base, addr, newbase;

	if (!p && !u && !w) {
		/* STREX / LDREX (word): imm8 is scaled by 4. */
		uint32_t off = (imm8 & 0xFFu) << 2;

		if (rn == 15u)
			return N2A32_UNDEF;
		addr = c->r[rn] + off;
		if (!l) {
			/* STREX Rd, Rt, [Rn, #imm8*4]: bits 15-12 = Rt (value), 11-8 = Rd. */
			if (hi4 == 15u || f8 == 15u)
				return N2A32_UNDEF;
			c->r[f8] = (uint32_t)n2a32_excl_store(c, addr, 4, c->r[hi4]);
			return N2A32_OK;
		}
		/* LDREX Rt, [Rn, #imm8*4]: bits 15-12 = Rt, 11-8 = 1111. */
		if (f8 != 15u || hi4 == 15u)
			return N2A32_UNDEF;
		{
			uint32_t v = n2a32_ld32(c, addr);

			n2a32_excl_mark(c, addr, 4, v);
			c->r[hi4] = v;
		}
		return N2A32_OK;
	}

	if (!p && u && !w) {
		/* Exclusives (byte/halfword/doubleword) and TBB/TBH.  The second halfword is
		 *   Rt | Rt2-or-1111 | op(4) | Rd-or-1111      (op 4 LDREXB/STREXB, 5 ...H,
		 *                                                7 LDREXD/STREXD)
		 *   1111 | 0000 | 000H | Rm                    (TBB with H=0, TBH with H=1) */
		unsigned rt = (hw2 >> 12) & 0xFu;       /* data register / 1111 for TBB */
		unsigned rt2 = (hw2 >> 8) & 0xFu;       /* Rt2 (doubleword) or 1111 */
		unsigned op = (hw2 >> 4) & 0xFu;        /* 4, 5, 7, or 0/1 for TBB/TBH */
		unsigned rd = hw2 & 0xFu;               /* status register or 1111 */

		if (l) {
			switch (op) {
			case 4:                 /* LDREXB Rt, [Rn] */
			case 5: {               /* LDREXH Rt, [Rn] */
				unsigned bytes = (op == 4u) ? 1u : 2u;
				uint32_t v;

				if (rn == 15u || rt2 != 15u || rd != 15u || rt == 15u)
					return N2A32_UNDEF;
				addr = c->r[rn];
				v = (bytes == 1u) ? n2a32_ld8(c, addr) : n2a32_ld16(c, addr);
				n2a32_excl_mark(c, addr, bytes, v);
				c->r[rt] = v;
				return N2A32_OK;
			}
			case 7: {               /* LDREXD Rt, Rt2, [Rn] */
				uint64_t v;

				if (rn == 15u || rt == 15u || (rt & 1u) || rt > 12u)
					return N2A32_UNDEF;
				if (rt2 != rt + 1u || rd != 15u)
					return N2A32_UNDEF;
				addr = c->r[rn];
				v = n2a32_ld64(c, addr);
				n2a32_excl_mark(c, addr, 8, v);
				c->r[rt] = (uint32_t)v;
				c->r[rt + 1u] = (uint32_t)(v >> 32);
				return N2A32_OK;
			}
			case 0u:                /* TBB [Rn, Rm] */
			case 1u: {              /* TBH [Rn, Rm, lsl #1] */
				unsigned rm = hw2 & 0xFu;
				uint32_t idx;

				if (rt != 15u || rt2 != 0u || (hw2 & 0x00E0u) != 0u || rm == 15u)
					return N2A32_UNDEF;
				/* Rn == 1111 reads the PC as it is: for TBB/TBH the table base
				 * is the branch instruction's PC (address + 4), with no
				 * Align(PC,4) (T01's k_switch_dense differential and review
				 * finding F13; a 32-bit T32 instruction can be 2 mod 4 in
				 * ARMv8-A, which makes the difference observable). */
				base = (rn == 15u) ? c->r[15] : c->r[rn];
				addr = base + ((op == 1u) ? 2u * c->r[rm] : c->r[rm]);
				idx = (op == 1u) ? (uint32_t)n2a32_ld16(c, addr)
						 : (uint32_t)n2a32_ld8(c, addr);
				n2a32_branch(c, c->r[15] + 2u * idx);
				return N2A32_OK;
			}
			default:
				return N2A32_UNDEF;
			}
		}
		switch (op) {
		case 4:                         /* STREXB Rd, Rt, [Rn] */
		case 5: {                       /* STREXH */
			unsigned bytes = (op == 4u) ? 1u : 2u;

			if (rn == 15u || rt2 != 15u || rd == 15u || rt == 15u)
				return N2A32_UNDEF;
			addr = c->r[rn];
			c->r[rd] = (uint32_t)n2a32_excl_store(c, addr, bytes, c->r[rt]);
			return N2A32_OK;
		}
		case 7: {                       /* STREXD Rd, Rt, Rt2, [Rn] */
			uint64_t v;

			if (rn == 15u || rt == 15u || (rt & 1u) || rt > 12u)
				return N2A32_UNDEF;
			if (rt2 != rt + 1u || rd == 15u)
				return N2A32_UNDEF;
			addr = c->r[rn];
			v = (uint64_t)c->r[rt] | ((uint64_t)c->r[rt + 1u] << 32);
			c->r[rd] = (uint32_t)n2a32_excl_store(c, addr, 8, v);
			return N2A32_OK;
		}
		default:
			return N2A32_UNDEF;
		}
	}

	/* LDRD / STRD (immediate, and literal with Rn == 1111).  Rt and Rt2 are used exactly
	 * as encoded: the vendor libraries store/load arbitrary register pairs (e.g.
	 * `strd r3, r1, [sp, #8]`), which the ARM ARM lists as UNPREDICTABLE in ARMv7 but
	 * which real ARMv8-A hardware and every assembler accept.  Rt == Rt2 is executed as
	 * two consecutive transfers of the same register. */
	{
		uint32_t off = (imm8 & 0xFFu) << 2;
		unsigned wback;

		if (hi4 == 15u || f8 == 15u)
			return N2A32_UNDEF;             /* untransferable register */
		if (rn == 15u) {
			if (w)
				return N2A32_UNDEF;     /* literal with writeback: UNPREDICTABLE */
			base = c->r[15] & ~3u;
			addr = apply_u(base, off, u);
			wback = 0;
		} else {
			base = c->r[rn];
			addr = p ? apply_u(base, off, u) : base;
			newbase = apply_u(base, off, u);
			wback = (p && w) || !p;
		}
		if (l) {
			uint32_t lo = n2a32_ld32(c, addr), hi = n2a32_ld32(c, addr + 4u);

			c->r[hi4] = lo;
			c->r[f8] = hi;
		} else {
			n2a32_st32(c, addr, c->r[hi4]);
			n2a32_st32(c, addr + 4u, c->r[f8]);
		}
		if (wback && rn != 15u)
			c->r[rn] = newbase;
		return N2A32_OK;
	}
}

/* ------------------------------------------------------------------ entry point */

int n2a32_t32_mem(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned hw1 = (insn >> 16) & 0xFFFFu;
	unsigned hw2 = insn & 0xFFFFu;

	if ((hw1 & 0xFE00u) == 0xE800u) {
		if (!(hw1 & 0x0040u)) {
			/* LDM/STM: bits 8 and 7 must be complementary (IA or DB). */
			if ((hw1 & 0x0180u) != 0x0080u && (hw1 & 0x0180u) != 0x0100u)
				return N2A32_UNDEF;
			return ldm_stm(c, hw1, hw2);
		}
		return dual_excl(c, hw1, hw2);
	}
	if ((hw1 & 0x0E00u) == 0x0800u)
		return single_transfer(c, hw1, hw2);
	return N2A32_UNDEF;
}
