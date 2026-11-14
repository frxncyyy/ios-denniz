/* SPDX-License-Identifier: MIT */
/*
 * n2a32_neon_ldst.c -- package V20: A32 Advanced SIMD element and structure load/store.
 *
 *   1111 0100 0D1L Rn Vd type size align Rm   multiple structures (VLD1-4, VST1-4)
 *   1111 0100 1D1L Rn Vd type size align Rm   single structure to/from one lane, and VLD1-4 to all lanes
 *
 * type (bits 11-8) for the multiple forms:
 *   0000 VLD4/VST4 {d0-d3}   0010 VLD1/VST1 four registers   0011 VLD2/VST2 four registers
 *   0100 VLD3/VST3 {d0-d2}   0110 VLD1/VST1 three registers   0111 VLD1/VST1 one register
 *   1000 VLD2/VST2 {d0,d1}   1010 VLD1/VST1 two registers
 * type for the single-structure forms:
 *   0..11: (number of structures - 1) + 4 * size (size 0 = 8 bits, 1 = 16, 2 = 32)
 *   1100/1101/1110/1111: VLD1/VLD2/VLD3/VLD4 to all lanes (size in bits 7-6)
 *
 * size (bits 7-6) for the multiple forms and for the to-all-lanes forms: 0 = 8, 1 = 16, 2 = 32, 3 = 64 bits.
 * The lane index of a single-lane access is in bits 7-5 (.8), 7-6 (.16) or 7 (.32).
 * Rm: 1111 = no writeback, 1101 = writeback by the number of bytes transferred, otherwise writeback by Rm.
 * Rn = 15 is rejected (UNDEFINED).
 *
 * The align field (bits 5-4) only tells the core which alignment it may assume; it does not change the
 * bytes transferred, and this interpreter has no alignment-fault model, so it is accepted and ignored
 * (documented in test_v20/STATUS.md). Every access goes through the n2a32_ld and n2a32_st helpers so MMIO windows and the
 * core's fault handling work as for every other load/store.
 */
#include "n2a32.h"

/*
 * Which values of bits 7-4 (element size and alignment for the multiple forms, the lane index
 * and alignment for the single-lane forms) are allocated.  The bit mask is indexed by
 * [single][load][type]; word bit (8 + n) of the mask corresponds to bits 7-4 == n.  The table
 * was derived from llvm-mc's disassembler over the whole F4 encoding space (every word with
 * Rn = 1, Vd = 0, Rm = 15; the answer does not depend on D, Vd, Rn or Rm).  Register lists
 * that wrap past d31 are rejected separately.
 */
static const uint16_t ldst_valid_nib[2][2][16] = {
	{	/* multiple structures; the load bit does not change the allocation */
		{ 0x0FFF, 0x0FFF, 0xFFFF, 0x0FFF, 0x0333, 0x0333, 0x3333, 0x3333,
		  0x0777, 0x0777, 0x7777, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
		{ 0x0FFF, 0x0FFF, 0xFFFF, 0x0FFF, 0x0333, 0x0333, 0x3333, 0x3333,
		  0x0777, 0x0777, 0x7777, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 }
	}, {	/* single structure: types 12-15 are the to-all-lanes forms and exist only as loads */
		{ 0x5555, 0xFFFF, 0x5555, 0xFFFF, 0x3333, 0xFFFF, 0x5555, 0xFFFF,
		  0x0909, 0x3333, 0x1111, 0x7777, 0x0000, 0x0000, 0x0000, 0x0000 },
		{ 0x5555, 0xFFFF, 0x5555, 0xFFFF, 0x3333, 0xFFFF, 0x5555, 0xFFFF,
		  0x0909, 0x3333, 0x1111, 0x7777, 0x0FF5, 0x0FFF, 0x0555, 0xAFFF }
	}
};

static uint64_t ld_elem(struct n2a32_cpu *c, uint32_t a, unsigned esize)
{
	switch (esize) {
	case 1: return n2a32_ld8(c, a);
	case 2: return n2a32_ld16(c, a);
	case 4: return n2a32_ld32(c, a);
	default: return n2a32_ld64(c, a);
	}
}

static void st_elem(struct n2a32_cpu *c, uint32_t a, unsigned esize, uint64_t v)
{
	switch (esize) {
	case 1: n2a32_st8(c, a, (uint8_t)v); break;
	case 2: n2a32_st16(c, a, (uint16_t)v); break;
	case 4: n2a32_st32(c, a, (uint32_t)v); break;
	default: n2a32_st64(c, a, v); break;
	}
}

/* copy one element between a guest address and lane `lane` of register `reg` */
static void move_elem(struct n2a32_cpu *c, int load, unsigned reg, unsigned lane,
		      unsigned esize, uint32_t addr)
{
	unsigned sh = lane * esize * 8u;
	uint64_t mask = esize >= 8 ? ~(uint64_t)0 : (((uint64_t)1 << (esize * 8u)) - 1);

	if (load) {
		uint64_t v = ld_elem(c, addr, esize);

		c->d[reg & 31u].u64 = (c->d[reg & 31u].u64 & ~(mask << sh)) | ((v & mask) << sh);
	} else {
		uint64_t v = (c->d[reg & 31u].u64 >> sh) & mask;

		st_elem(c, addr, esize, v);
	}
}

int n2a32_neon_ldst(struct n2a32_cpu *c, uint32_t insn)
{
	unsigned single = (insn >> 23) & 1u;
	unsigned d = (insn >> 22) & 1u;
	unsigned load = (insn >> 21) & 1u;
	unsigned rn = (insn >> 16) & 0xFu;
	unsigned vd = (insn >> 12) & 0xFu;
	unsigned type = (insn >> 8) & 0xFu;
	unsigned sz = (insn >> 6) & 3u;
	unsigned rm = insn & 0xFu;
	unsigned base = (d << 4) | vd;
	uint32_t addr = c->r[rn];
	uint32_t wb_addr = 0;
	unsigned nregs = 0, nstruct = 1, esize = 1, lane = 0, all_lanes = 0, stride = 1;
	unsigned transfer = 0, writeback = 0;
	unsigned i, j, b;

	if ((insn & 0xFF000000u) != 0xF4000000u)
		return N2A32_UNDEF;
	if (insn & 0x00100000u)                 /* bit 20 must be 0 in this group */
		return N2A32_UNDEF;
	if (rn == 15u)                          /* PC as base is UNDEFINED */
		return N2A32_UNDEF;
	if (((ldst_valid_nib[single][load][type] >> ((insn >> 4) & 0xFu)) & 1u) == 0u)
		return N2A32_UNDEF;             /* unallocated size / alignment / lane field */

	if (!single) {
		/* multiple structures */
		switch (type) {
		case 0x0: nregs = 4; nstruct = 4; break;        /* VLD4/VST4 */
		case 0x1: nregs = 4; nstruct = 4; stride = 2; break;  /* VLD4/VST4, register stride 2 (ARMv8) */
		case 0x2: nregs = 4; nstruct = 1; break;        /* VLD1/VST1 four registers */
		case 0x3: nregs = 4; nstruct = 2; break;        /* VLD2/VST2 four registers */
		case 0x4: nregs = 3; nstruct = 3; break;        /* VLD3/VST3 */
		case 0x5: nregs = 3; nstruct = 3; stride = 2; break;  /* VLD3/VST3, register stride 2 */
		case 0x6: nregs = 3; nstruct = 1; break;        /* VLD1/VST1 three registers */
		case 0x7: nregs = 1; nstruct = 1; break;        /* VLD1/VST1 one register */
		case 0x8: nregs = 2; nstruct = 2; break;        /* VLD2/VST2 */
		case 0x9: nregs = 2; nstruct = 2; stride = 2; break;  /* VLD2/VST2, register stride 2 */
		case 0xA: nregs = 2; nstruct = 1; break;        /* VLD1/VST1 two registers */
		default: return N2A32_UNDEF;
		}
		esize = 1u << sz;
		transfer = nregs * 8u;
	} else if (type >= 12u) {
		/* to all lanes; bit 5 is the register stride (VLD2-4) or a second register (VLD1), and
		 * bits 7-6 == 11 with bit 4 set is the 128-bit-alignment encoding of the .32 form */
		unsigned biased = (insn >> 5) & 1u;

		if (!load)
			return N2A32_UNDEF;     /* no store-to-all-lanes form exists */
		if (sz == 3u) {
			if ((insn & 0x10u) == 0 || (type != 0xDu && type != 0xFu))
				return N2A32_UNDEF;
			sz = 2;
		}
		esize = 1u << sz;
		if (type == 0xCu) {
			nstruct = 1u + biased;  /* VLD1: one or two consecutive registers */
		} else {
			nstruct = type - 11u;
			stride = 1u + biased;
		}
		nregs = nstruct;
		all_lanes = 1;
		transfer = nstruct * esize;
	} else {
		/* one structure to one lane */
		nstruct = (type & 3u) + 1u;
		nregs = nstruct;
		if ((type >> 2) == 3u)
			return N2A32_UNDEF;
		esize = 1u << (type >> 2);
		/* For .8 the lane index is bits 7-5 and bit 4 is the alignment hint.  For .16 and .32
		 * ARMv8 adds a register-stride-2 bit just above the lane index: .16 lane bits 7-6,
		 * stride bit 5, alignment bit 4; .32 lane bit 7, stride bit 6, alignment bits 5-4. */
		switch (esize) {
		case 1: lane = (insn >> 5) & 7u; stride = 1; break;
		case 2: lane = (insn >> 6) & 3u; stride = 1u + ((insn >> 5) & 1u); break;
		default: lane = (insn >> 7) & 1u; stride = 1u + ((insn >> 6) & 1u); break;
		}
		transfer = nstruct * esize;
	}

	if (base + (nregs - 1u) * stride + 1u > 32u)
		return N2A32_UNDEF;             /* the register list would wrap past d31 */

	if (rm == 15u) {
		writeback = 0;
	} else if (rm == 13u) {
		writeback = 1;
		wb_addr = addr + transfer;
	} else {
		writeback = 1;
		wb_addr = addr + c->r[rm];
	}

	if (!single) {
		unsigned per = 8u / esize;      /* elements per register */

		if (nstruct == 1u) {
			for (i = 0; i < nregs * per; i++)
				move_elem(c, (int)load, base + i / per, i % per, esize,
					  addr + i * esize);
		} else {
			unsigned blocks = nregs / nstruct;

			for (b = 0; b < blocks; b++)
				for (i = 0; i < per; i++)
					for (j = 0; j < nstruct; j++)
						move_elem(c, (int)load, base + (b * nstruct + j) * stride, i,
							  esize,
							  addr + (b * per * nstruct + i * nstruct + j) * esize);
		}
	} else if (all_lanes) {
		for (j = 0; j < nstruct; j++) {
			uint64_t v = ld_elem(c, addr + j * esize, esize);
			uint64_t rep = 0;
			unsigned per = 8u / esize;

			for (i = 0; i < per; i++)
				rep |= (v & (esize >= 8 ? ~(uint64_t)0 : (((uint64_t)1 << (esize * 8u)) - 1)))
				       << (i * esize * 8u);
			c->d[(base + j * stride) & 31u].u64 = rep;
		}
	} else {
		for (j = 0; j < nstruct; j++)
			move_elem(c, (int)load, base + j * stride, lane, esize,
				  addr + j * esize);
	}

	if (writeback)
		c->r[rn] = wb_addr;
	return N2A32_OK;
}
