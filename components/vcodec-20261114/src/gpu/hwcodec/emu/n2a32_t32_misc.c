/* SPDX-License-Identifier: MIT */
/*
 * n2a32_t32_misc.c -- V16: 32-bit Thumb (T32) branches, system instructions and cp15 reads.
 *
 * Owner: hw-v16 (package V16).  Implements n2a32_t32_misc() of include/n2a32.h.  The core has
 * already fetched the two halfwords and set c->r[15] (instruction address + 4), c->next_pc
 * (address of the following instruction) and c->in_it.  Inside an IT block the core has
 * already evaluated (and, if false, skipped) the instruction, so the B<c>.W handled here is the
 * one that carries its own condition in its encoding.  This file returns N2A32_OK or
 * N2A32_UNDEF only; it never aborts or prints.  Branches go through n2a32_branch() (B.W, BL,
 * B<c>.W) or n2a32_bx() (BLX immediate, which always switches to ARM state).
 *
 * Encoding regions (ARM ARM A5.3.2, A5.3.3, A8.8.20/24/42/50/51/70/161):
 *
 *   B.W / B<c>.W / BL / BLX, all with hw1 bits 15-11 == 11110:
 *     S = hw1 bit 10, J1 = hw2 bit 13, J2 = hw2 bit 11, hw2 bit 12 selects the form:
 *       hw2 bits 15-14 == 10, bit 12 == 0, hw1 bits 9-6 == cond (< 1110):
 *           B<c>.W (T3): 21-bit offset, cond evaluated here with n2a32_cond().
 *           cond == 1110 and cond == 1111 are not allocated in the T3 space
 *           (B.W is the 25-bit T4 form and UDF owns part of the 1111 space).
 *       hw2 bits 15-14 == 10, bit 12 == 1:
 *           B (T4): 25-bit offset, always taken.
 *       hw2 bits 15-14 == 11, bit 12 == 1:
 *           BL (T1): 25-bit offset, LR = PC | 1, stays in Thumb.
 *       hw2 bits 15-14 == 11, bit 12 == 0:
 *           BLX (T2): 25-bit offset with bit 1 taken from hw2 bit 0, target =
 *           Align(PC,4) + offset, LR = PC | 1, switches to ARM state.
 *     The 25-bit offset is SignExtend(S : NOT(J1 EOR S) : NOT(J2 EOR S) : hw1 bits 9-0 :
 *     hw2 bits 10-0 : '0'); the 21-bit one is SignExtend(S : J2 : J1 : hw1 bits 5-0 :
 *     hw2 bits 10-0 : '0') -- i.e. in the conditional form the J bits are the top two
 *     offset bits below S.  Both were derived from the ARM ARM encodings and checked
 *     against llvm-mc and lld on the whole immediate range (test_v16).
 *
 *   MRS / MSR / hints / barriers / UDF:
 *     0xF3EF 1000 Rd 0000 0000   MRS Rd, APSR          -> Rd = n2a32_apsr(c)
 *     0xF38n 1000 00m0 0000 0000 MSR APSR_<mask>, Rn   (m = 2 nzcvq, 1 g, 3 both)
 *     0xF3AF 1000 0000 hint      NOP/YIELD/WFE/WFI/SEV/SEVL/DBG and the unallocated hints:
 *                                no effect
 *     0xF3BF 1000 1111 opc opt   CLREX (opc 2), DSB (4), DMB (5), ISB (6): barriers are
 *                                no-ops in this execution model; CLREX opens the monitor
 *     0xF7Fn 1010 imm12          UDF.W #imm16: c->stop = N2A32_STOP_UNDEF, return UNDEF
 *
 *   Coprocessor transfers (hw2 bits 11-8 = coprocessor number; the core routes cp 10/11 to
 *   the VFP class): exactly one is implemented, MRC p15, 0, Rt, c13, c0, 3 -> Rt =
 *   c->tpidruro.  Every other MRC/MCR/MRRC/MCRR/LDC/STC/CDP word, including MCR of the same
 *   cp15 register and every other cp15 opc1/CRn/CRm/opc2, returns N2A32_UNDEF (listed in
 *   test_v16/STATUS.md).
 *
 * Deliberately N2A32_UNDEF: B<c>.W with cond == 111x (the T3 space allocates only
 * conditions 0000-1101; cond 1110 is not the 25-bit B.W, whose second halfword has bit
 * 12 set, and cond 1111 is the UDF/unallocated space), a branch word with hw1 bits
 * 15-11 != 11110, MRS with SYSm != 0 or Rd == 1111, MSR with the reserved bits set,
 * MCR/MRRC/MCRR and the cp15 forms above, and everything else in the space the core
 * routes here (e.g. LDC/STC/CDP for cp 12-14).  32-bit SVC does not exist (SVC is 16-bit
 * only, V14); a 32-bit word that would look like one is a UDF or unallocated and returns
 * UNDEF.
 */
#include "n2a32.h"

/* --- branch helpers ---------------------------------------------------------------- */

static uint32_t sign_extend32(uint32_t v, unsigned bits)
{
	if (bits < 32u && (v & (1u << (bits - 1u))) != 0u)
		v |= ~((1u << bits) - 1u);
	return v;
}

/* 25-bit immediate of B (T4), BL (T1) and BLX (T2):
 * SignExtend(S : I1 : I2 : imm10 : imm11 : '0') with I1 = NOT(J1 EOR S), I2 = NOT(J2 EOR S).
 * For BLX, imm10 is 10 bits wide and its lowest bit is the H bit (hw2 bit 0). */
static uint32_t branch_imm25(unsigned hw1, unsigned hw2, unsigned blx)
{
	unsigned s = (hw1 >> 10) & 1u;
	unsigned j1 = (hw2 >> 13) & 1u;
	unsigned j2 = (hw2 >> 11) & 1u;
	uint32_t imm;

	if (blx)
		imm = ((uint32_t)(hw1 & 0x3FFu) << 12) |
		      ((uint32_t)((hw2 >> 1) & 0x3FFu) << 2) |
		      ((uint32_t)(hw2 & 1u) << 1);
	else
		imm = ((uint32_t)(hw1 & 0x3FFu) << 12) |
		      ((uint32_t)(hw2 & 0x7FFu) << 1);
	imm |= (uint32_t)(j2 ^ s ^ 1u) << 22;
	imm |= (uint32_t)(j1 ^ s ^ 1u) << 23;
	imm |= (uint32_t)s << 24;
	return sign_extend32(imm, 25);
}

/* 21-bit immediate of B<c>.W (T3): SignExtend(S : J2 : J1 : imm6 : imm11 : '0'). */
static uint32_t branch_imm21(unsigned hw1, unsigned hw2)
{
	unsigned s = (hw1 >> 10) & 1u;
	uint32_t imm = ((uint32_t)(hw1 & 0x3Fu) << 12) |
		       ((uint32_t)(hw2 & 0x7FFu) << 1) |
		       ((uint32_t)((hw2 >> 13) & 1u) << 18) |
		       ((uint32_t)((hw2 >> 11) & 1u) << 19) |
		       ((uint32_t)s << 20);

	return sign_extend32(imm, 21);
}

/* --- system instructions ----------------------------------------------------------- */

static int hint_or_barrier(struct n2a32_cpu *c, unsigned hw1, unsigned hw2)
{
	if (hw1 == 0xF3AFu) {
		/* Hints: bits 11-8 must be zero, the low byte is the hint number.  NOP, YIELD,
		 * WFE, WFI, SEV, SEVL, DBG and the unallocated hint numbers all execute as a
		 * no-op in this execution model (there is no event or debug hardware). */
		if ((hw2 & 0xFF00u) != 0x8000u)
			return N2A32_UNDEF;
		return N2A32_OK;
	}
	if (hw1 == 0xF3BFu) {
		if ((hw2 & 0xFF00u) != 0x8F00u)
			return N2A32_UNDEF;
		switch ((hw2 >> 4) & 0xFu) {
		case 2:                          /* CLREX */
			c->excl_valid = 0;
			return N2A32_OK;
		case 4:                          /* DSB */
		case 5:                          /* DMB */
		case 6:                          /* ISB */
			return N2A32_OK;
		default:
			return N2A32_UNDEF;
		}
	}
	if (hw1 == 0xF3EFu) {
		/* MRS Rd, APSR: SYSm (hw2 bits 7-0) must be 0. */
		unsigned rd;

		if ((hw2 & 0xF0FFu) != 0x8000u)
			return N2A32_UNDEF;
		rd = (hw2 >> 8) & 0xFu;
		if (rd == 15u)
			return N2A32_UNDEF;
		c->r[rd] = n2a32_apsr(c);
		return N2A32_OK;
	}
	if ((hw1 & 0xFFF0u) == 0xF380u) {
		/* MSR APSR_<mask>, Rn: bits 11-10 = mask, 9-8 and 7-0 must be zero. */
		unsigned rn = hw1 & 0xFu;
		unsigned mask;

		if ((hw2 & 0xF3FFu) != 0x8000u)
			return N2A32_UNDEF;
		if (rn == 15u)
			return N2A32_UNDEF;
		mask = (hw2 >> 10) & 3u;
		if (mask & 2u) {                 /* APSR_nzcvq */
			uint32_t v = c->r[rn];

			c->n = (uint8_t)(v >> 31);
			c->z = (uint8_t)((v >> 30) & 1u);
			c->cf = (uint8_t)((v >> 29) & 1u);
			c->v = (uint8_t)((v >> 28) & 1u);
			c->q = (uint8_t)((v >> 27) & 1u);
		}
		if (mask & 1u)                   /* APSR_g */
			c->ge = (uint8_t)((c->r[rn] >> 16) & 0xFu);
		return N2A32_OK;
	}
	return N2A32_UNDEF;
}

/* --- entry point ------------------------------------------------------------------- */

int n2a32_t32_misc(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned hw1 = (insn >> 16) & 0xFFFFu;
	unsigned hw2 = insn & 0xFFFFu;

	if ((hw1 & 0xF800u) == 0xF000u) {
		/* The system encodings share their first halfword with B.W / BL / BLX (e.g.
		 * hw1 0xF3AF is both a hint and a legal BLX first halfword, 0xF380 is both MSR
		 * and a legal B.W).  They are only recognised when the second halfword has the
		 * 1000 pattern every one of them requires; otherwise control falls through to
		 * the branch decode, which is what the hardware does. */
		if ((hw2 & 0xF000u) == 0x8000u) {
			if (hw1 == 0xF3AFu)
				return hint_or_barrier(c, hw1, hw2);
			if (hw1 == 0xF3BFu && (hw2 & 0xFF00u) == 0x8F00u)
				return hint_or_barrier(c, hw1, hw2);
			if (hw1 == 0xF3EFu && (hw2 & 0xF0FFu) == 0x8000u)
				return hint_or_barrier(c, hw1, hw2);
			if ((hw1 & 0xFFF0u) == 0xF380u && (hw2 & 0xF3FFu) == 0x8000u)
				return hint_or_barrier(c, hw1, hw2);
		}
		if ((hw1 & 0xFFF0u) == 0xF7F0u && (hw2 & 0xF000u) == 0xA000u) {
			/* UDF.W #imm16: 1111 0111 1111 imm4 | 1010 imm12.  hw2 nibble
			 * 1010 has bit 12 clear, which keeps this word out of the
			 * B.W/BL/BLX space (their second halfwords have bit 12 set);
			 * the same first halfword with bit 12 set falls through to the
			 * branch decode as a normal B.W/BL/BLX. */
			c->stop = N2A32_STOP_UNDEF;
			return N2A32_UNDEF;
		}

		if ((hw2 & 0xC000u) == 0x8000u) {
			if (hw2 & 0x1000u) {
				/* B (T4): 25-bit unconditional branch. */
				n2a32_branch(c, c->r[15] + branch_imm25(hw1, hw2, 0));
				return N2A32_OK;
			}
			{
				unsigned cond = (hw1 >> 6) & 0xFu;

				/* The T3 space allocates conditions 0000-1101 only.
				 * cond 1110 is unallocated (B.W uses the 25-bit T4 form,
				 * whose hw2 bit 12 is 1) and cond 1111 is the UDF space
				 * (handled above when hw2 selects UDF.W). */
				if (cond >= 0xEu)
					return N2A32_UNDEF;
				if (n2a32_cond(c, cond))
					n2a32_branch(c, c->r[15] + branch_imm21(hw1, hw2));
				return N2A32_OK;
			}
		}
		if ((hw2 & 0xC000u) == 0xC000u) {
			if (hw2 & 0x1000u) {
				/* BL (T1): link, stay in Thumb. */
				c->r[14] = c->r[15] | 1u;
				n2a32_branch(c, c->r[15] + branch_imm25(hw1, hw2, 0));
				return N2A32_OK;
			}
			/* BLX (T2): link, switch to ARM state; target is Align(PC,4)-relative. */
			c->r[14] = c->r[15] | 1u;
			n2a32_bx(c, (c->r[15] & ~3u) + branch_imm25(hw1, hw2, 1));
			return N2A32_OK;
		}
		/* hw2 bit 15 is set for every encoding the core routes here, so this is
		 * unreachable for the branch space; fall through to the coprocessor test. */
	}

	/* Coprocessor transfers: hw2 bits 11-8 hold the coprocessor number.  Exactly one
	 * register read is implemented: MRC p15, 0, Rt, c13, c0, 3 (the user-mode TLS
	 * pointer). */
	if ((hw2 & 0x0F00u) == 0x0F00u) {
		if ((insn & 0x0FFF0FFFu) == 0x0E1D0F70u) {
			unsigned rt = (hw2 >> 12) & 0xFu;

			if (rt == 15u)
				return N2A32_UNDEF;
			c->r[rt] = c->tpidruro;
			return N2A32_OK;
		}
	}
	return N2A32_UNDEF;
}
