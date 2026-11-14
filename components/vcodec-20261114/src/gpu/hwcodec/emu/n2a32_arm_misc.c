/* SPDX-License-Identifier: MIT */
/*
 * n2a32_arm_misc.c -- V12: A32 branches, exception/system instructions and cp15 reads.
 *
 * The core has already evaluated the condition (A32 cond bits 31-28) and set c->r[15]
 * (instruction address + 8) and c->next_pc (address of the following instruction).
 * Class functions never test the condition; this file returns N2A32_OK or N2A32_UNDEF
 * and, for SVC/BKPT/UDF, sets c->stop to the reason before returning N2A32_UNDEF so the
 * core can report it (the core must preserve a non-NONE c->stop).
 *
 * Implemented (ARM ARM A5.5, A8.8.18-20, A8.8.24, A8.8.27, A8.8.32, A8.8.34, A8.8.42,
 * A8.8.50, A8.8.70, A8.8.144, A8.8.161):
 *   B, BL, BLX (immediate, always switches to Thumb), BX, BLX (register);
 *   SVC -> N2A32_STOP_SVC, BKPT -> N2A32_STOP_BKPT, UDF -> N2A32_STOP_UNDEF;
 *   NOP, YIELD, WFE, WFI, SEV, the other HINT encodings and DBG: no effect;
 *   DMB, DSB, ISB: no effect (single-threaded guest execution model);
 *   CLREX: opens the local exclusive monitor;
 *   MRC p15, 0, Rt, c13, c0, 3 -> Rt = c->tpidruro.
 * Every other MRC/MCR (any coprocessor, and every other cp15 transfer, including MCR of
 * the same c13/c0/3 word) and every other encoding in these spaces returns N2A32_UNDEF.
 */
#include "n2a32.h"

/* Sign-extend a 26-bit byte offset (imm24:'00') and add the bias-corrected PC. */
static uint32_t branch_target(const struct n2a32_cpu *c, uint32_t off26)
{
	if (off26 & 0x02000000u) off26 |= 0xFC000000u;
	return c->r[15] + off26;
}

int n2a32_arm_misc(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned cond = insn >> 28;

	/* B / BL / BLX (immediate): bits 27-25 == 101.  cond=1111 selects BLX, which is the
	 * only branch in the unconditional space and always changes to Thumb (A8.8.24). */
	if ((insn & 0x0E000000u) == 0x0A000000u) {
		uint32_t off;
		if (cond == 15) {
			off = ((insn & 0x00FFFFFFu) << 2) | ((insn >> 23) & 2u);
			c->r[14] = c->next_pc;
			c->thumb = 1;
			n2a32_branch(c, branch_target(c, off));
			return N2A32_OK;
		}
		off = (insn & 0x00FFFFFFu) << 2;
		if (insn & 0x01000000u) c->r[14] = c->next_pc;   /* BL */
		n2a32_branch(c, branch_target(c, off));
		return N2A32_OK;
	}

	/* SVC (previously SWI): cond 1111 imm24 (A8.8.144).  No return to the guest. */
	if ((insn & 0x0F000000u) == 0x0F000000u) {
		c->stop = N2A32_STOP_SVC;
		return N2A32_UNDEF;
	}

	/* BKPT: cond 0001 0010 imm12 0111 imm4 (A8.8.27). */
	if ((insn & 0x0FF000F0u) == 0x01200070u) {
		c->stop = N2A32_STOP_BKPT;
		return N2A32_UNDEF;
	}

	/* UDF: cond 0111 1111 imm12 1111 imm4 (A8.8.161). */
	if ((insn & 0x0FF000F0u) == 0x07F000F0u) {
		c->stop = N2A32_STOP_UNDEF;
		return N2A32_UNDEF;
	}

	/* BX / BLX (register): cond 0001 0010 1111 1111 1111 0001/0011 Rm (A8.8.34, A8.8.20).
	 * BXJ (bits 7-0 = 0x2F) is deprecated and not implemented. */
	if ((insn & 0x0FFFFFF0u) == 0x012FFF10u) {
		n2a32_bx(c, c->r[insn & 0xFu]);
		return N2A32_OK;
	}
	if ((insn & 0x0FFFFFF0u) == 0x012FFF30u) {
		c->r[14] = c->next_pc;
		n2a32_bx(c, c->r[insn & 0xFu]);
		return N2A32_OK;
	}

	/* HINT #imm4 and DBG #imm4: NOP/YIELD/WFE/WFI/SEV and the reserved hints execute as
	 * NOP; DBG is a no-op when debug is not enabled (A8.8.42, A8.8.50).  cond=1111 with
	 * bits 27-25 == 001 is the Advanced SIMD space, not this one. */
	if (((insn & 0x0FFFFFF0u) == 0x0320F000u || (insn & 0x0FFFFFF0u) == 0x0320F0F0u) &&
	    cond != 15)
		return N2A32_OK;

	/* CLREX / DMB / DSB / ISB: cond 1111 0101 0111 1111 1111 0000 xxxx yyyy.
	 * Barriers are no-ops in this execution model; CLREX opens the local monitor. */
	if ((insn & 0xFFFFFF00u) == 0xF57FF000u) {
		switch ((insn >> 4) & 0xFu) {
		case 1: c->excl_valid = 0; return N2A32_OK;   /* CLREX */
		case 4:                                       /* DSB */
		case 5:                                       /* DMB */
		case 6: return N2A32_OK;                      /* ISB */
		default: return N2A32_UNDEF;
		}
	}

	/* MRC/MCR and the other coprocessor transfers: bits 27-25 == 110 or 111.  This package
	 * implements exactly one register read, the user-mode TLS pointer. */
	if ((insn & 0x0E000000u) >= 0x0C000000u) {
		if ((insn & 0x0FFF0FFFu) == 0x0E1D0F70u && cond != 15) {
			unsigned rt = (insn >> 12) & 0xFu;
			if (rt == 15) return N2A32_UNDEF;
			c->r[rt] = c->tpidruro;
			return N2A32_OK;
		}
		return N2A32_UNDEF;
	}

	return N2A32_UNDEF;
}
