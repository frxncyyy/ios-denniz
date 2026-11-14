/* SPDX-License-Identifier: MIT */
/*
 * t_core.c -- V02 host tests: memory API, the routing tables (one llvm-mc encoding per mnemonic family),
 * IT blocks, host->guest calls (nested, interworking, stack arguments), host-call registry, faults, MMIO,
 * the exclusive monitor and the stop reasons.
 *
 * The instruction classes here are deliberately tiny: they implement only the handful of instructions the guest
 * programs and the execution tests need, and record which class the core routed to.  With exec_mode off they
 * return N2A32_UNDEF, which is how the routing test stops after exactly one instruction.
 */
#include "n2a32.h"
#include "n2enc.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct n2a32_mem *g_mem;
static int g_pass, g_fail;
struct hook_ctx;
static int g_exec;                 /* 1 = execute the tiny subset, 0 = record and return N2A32_UNDEF */
static const char *g_last_class;
static uint32_t g_last_insn;
static int g_calls, g_in_it_calls;

#define CHECK(cond, ...) do { \
	if (cond) { g_pass++; } \
	else { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static void note(const char *name, struct n2a32_cpu *c, uint32_t insn)
{
	g_last_class = name;
	g_last_insn = insn;
	g_calls++;
	if (c->in_it)
		g_in_it_calls++;
}

/* ------------------------------------------------------------------ tiny instruction subset (exec mode only) */

static uint32_t ror32(uint32_t v, unsigned n)
{
	n &= 31u;
	if (n == 0)
		return v;
	return (v >> n) | (v << (32u - n));
}

static void flags_add(struct n2a32_cpu *c, uint32_t x, uint32_t y, uint32_t r)
{
	uint8_t carry, ov;
	(void)n2a32_adc(x, y, 0, &carry, &ov);
	c->cf = carry;
	c->v = ov;
	n2a32_set_nz(c, r);
}

static void flags_sub(struct n2a32_cpu *c, uint32_t x, uint32_t y, uint32_t r)
{
	uint8_t carry, ov;
	(void)n2a32_adc(x, ~y, 1, &carry, &ov);
	c->cf = carry;
	c->v = ov;
	n2a32_set_nz(c, r);
}

static int exec_arm_dp(struct n2a32_cpu *c, uint32_t insn)
{
	uint32_t opcode = (insn >> 21) & 0xFu;
	uint32_t rn = (insn >> 16) & 0xFu, rd = (insn >> 12) & 0xFu;
	int s = (insn >> 20) & 1;
	uint32_t op2, r;

	if ((insn & 0x0FF00000u) == 0x03000000u || (insn & 0x0FF00000u) == 0x03400000u) {
		uint32_t imm16 = ((insn >> 4) & 0xF000u) | (insn & 0xFFFu);
		if (rd == 15)
			return N2A32_UNDEF;
		if ((insn & 0x0FF00000u) == 0x03000000u)
			c->r[rd] = imm16;
		else
			c->r[rd] = (c->r[rd] & 0xFFFFu) | (imm16 << 16);
		return N2A32_OK;
	}
	if ((insn & 0x02000000u) != 0) {
		op2 = ror32(insn & 0xFFu, ((insn >> 8) & 0xFu) * 2u);
	} else if ((insn & 0x00000010u) == 0) {
		uint32_t rm = insn & 0xFu;
		unsigned type = (insn >> 5) & 3u, sh = (insn >> 7) & 0x1Fu;
		uint32_t v = c->r[rm];
		switch (type) {
		case 0: v = sh ? (v << sh) : v; break;
		case 1: v = sh ? (v >> sh) : 0u; break;
		case 2: v = sh ? (uint32_t)((int32_t)v >> sh) : (uint32_t)((int32_t)v >> 31); break;
		default: v = sh ? ror32(v, sh) : ((uint32_t)c->cf << 31) | (v >> 1); break;
		}
		op2 = v;
	} else {
		return N2A32_UNDEF;
	}

	switch (opcode) {
	case 0x0: r = c->r[rn] & op2; if (s) n2a32_set_nz(c, r); if (rd == 15) n2a32_bx(c, r); else c->r[rd] = r; return N2A32_OK;
	case 0x1: r = c->r[rn] ^ op2; if (s) n2a32_set_nz(c, r); if (rd == 15) n2a32_bx(c, r); else c->r[rd] = r; return N2A32_OK;
	case 0x2: r = c->r[rn] - op2; if (s) flags_sub(c, c->r[rn], op2, r); if (rd == 15) n2a32_bx(c, r); else c->r[rd] = r; return N2A32_OK;
	case 0x3: r = op2 - c->r[rn]; if (s) flags_sub(c, op2, c->r[rn], r); if (rd == 15) n2a32_bx(c, r); else c->r[rd] = r; return N2A32_OK;
	case 0x4: r = c->r[rn] + op2; if (s) flags_add(c, c->r[rn], op2, r); if (rd == 15) n2a32_bx(c, r); else c->r[rd] = r; return N2A32_OK;
	case 0xA: r = c->r[rn] - op2; flags_sub(c, c->r[rn], op2, r); return N2A32_OK;
	case 0xC: r = c->r[rn] | op2; if (s) n2a32_set_nz(c, r); if (rd == 15) n2a32_bx(c, r); else c->r[rd] = r; return N2A32_OK;
	case 0xD: r = op2; if (s) n2a32_set_nz(c, r); if (rd == 15) n2a32_bx(c, r); else c->r[rd] = r; return N2A32_OK;
	case 0xE: r = c->r[rn] & ~op2; if (s) n2a32_set_nz(c, r); if (rd == 15) n2a32_bx(c, r); else c->r[rd] = r; return N2A32_OK;
	case 0xF: r = ~op2; if (s) n2a32_set_nz(c, r); if (rd == 15) n2a32_bx(c, r); else c->r[rd] = r; return N2A32_OK;
	default: return N2A32_UNDEF;
	}
}

static int exec_arm_mem(struct n2a32_cpu *c, uint32_t insn)
{
	uint32_t rn, rt, off, addr;
	int p, u, b, w, l;

	if ((insn & 0x0C000000u) != 0x04000000u)
		return N2A32_UNDEF;
	p = (insn >> 24) & 1;
	u = (insn >> 23) & 1;
	b = (insn >> 22) & 1;
	w = (insn >> 21) & 1;
	l = (insn >> 20) & 1;
	rn = (insn >> 16) & 0xFu;
	rt = (insn >> 12) & 0xFu;
	if ((insn & 0x02000000u) == 0) {
		off = insn & 0xFFFu;
	} else {
		unsigned sh = (insn >> 7) & 0x1Fu;
		if ((insn & 0x10u) != 0 || sh > 3)
			return N2A32_UNDEF;
		off = c->r[insn & 0xFu] << sh;
	}
	addr = c->r[rn] + (u ? off : -off);
	if (!p)
		addr = c->r[rn];
	if (l) {
		uint32_t v = b ? n2a32_ld8(c, addr) : n2a32_ld32(c, addr);
		if (rt == 15)
			n2a32_bx(c, v);
		else
			c->r[rt] = v;
	} else {
		if (b)
			n2a32_st8(c, addr, (uint8_t)c->r[rt]);
		else
			n2a32_st32(c, addr, c->r[rt]);
	}
	if (!p || w)
		c->r[rn] = c->r[rn] + (u ? off : -off);
	return N2A32_OK;
}

static int exec_arm_misc(struct n2a32_cpu *c, uint32_t insn)
{
	uint32_t pc = c->r[15] - 8u;

	if ((insn & 0x0E000000u) == 0x0A000000u) {          /* B / BL */
		uint32_t off = (uint32_t)((int32_t)(insn << 8) >> 6);
		if ((insn & 0x01000000u) != 0)
			c->r[14] = pc + 4u;
		n2a32_branch(c, pc + 8u + off);
		return N2A32_OK;
	}
	if ((insn & 0x0FFFFFF0u) == 0x012FFF10u) {          /* BX */
		n2a32_bx(c, c->r[insn & 0xFu]);
		return N2A32_OK;
	}
	if ((insn & 0x0FFFFFF0u) == 0x012FFF30u) {          /* BLX (register) */
		c->r[14] = pc + 4u;
		n2a32_bx(c, c->r[insn & 0xFu]);
		return N2A32_OK;
	}
	if ((insn & 0x0F000000u) == 0x0F000000u) {          /* SVC */
		c->stop = N2A32_STOP_SVC;
		return N2A32_UNDEF;
	}
	if ((insn & 0x0FF000F0u) == 0x01200070u) {          /* BKPT */
		c->stop = N2A32_STOP_BKPT;
		return N2A32_UNDEF;
	}
	if ((insn & 0x0FFF00FFu) == 0x03200000u)             /* hints */
		return N2A32_OK;
	if ((insn & 0xFFFFFFF0u) == 0xF57FF000u)             /* barriers / CLREX */
		return N2A32_OK;
	return N2A32_UNDEF;
}

static int exec_t16(struct n2a32_cpu *c, uint32_t insn)
{
	uint32_t rd, rm, imm, addr, r;

	/* MOV (immediate) */
	if ((insn & 0xF800u) == 0x2000u) {
		rd = (insn >> 8) & 7u;
		r = insn & 0xFFu;
		c->r[rd] = r;
		if (!c->in_it) n2a32_set_nz(c, r);
		return N2A32_OK;
	}
	/* CMP (immediate) */
	if ((insn & 0xF800u) == 0x2800u) {
		rd = (insn >> 8) & 7u;
		r = c->r[rd] - (insn & 0xFFu);
		flags_sub(c, c->r[rd], insn & 0xFFu, r);
		return N2A32_OK;
	}
	/* ADD/SUB (immediate) */
	if ((insn & 0xF800u) == 0x3000u || (insn & 0xF800u) == 0x3800u) {
		rd = (insn >> 8) & 7u;
		imm = insn & 0xFFu;
		if ((insn & 0x0800u) != 0) {
			r = c->r[rd] - imm;
			c->r[rd] = r;
			if (!c->in_it) flags_sub(c, c->r[rd] + imm, imm, r);
		} else {
			r = c->r[rd] + imm;
			c->r[rd] = r;
			if (!c->in_it) flags_add(c, c->r[rd] - imm, imm, r);
		}
		return N2A32_OK;
	}
	/* ADD/SUB (immediate) T2: 0001 110 imm3 Rn Rd / 0001 111 imm3 Rn Rd */
	if ((insn & 0xFE00u) == 0x1C00u || (insn & 0xFE00u) == 0x1E00u) {
		uint32_t rn = (insn >> 3) & 7u;
		imm = (insn >> 6) & 7u;
		rd = insn & 7u;
		if ((insn & 0x0200u) != 0) {
			r = c->r[rn] - imm;
			c->r[rd] = r;
			if (!c->in_it) flags_sub(c, c->r[rn], imm, r);
		} else {
			r = c->r[rn] + imm;
			c->r[rd] = r;
			if (!c->in_it) flags_add(c, c->r[rn], imm, r);
		}
		return N2A32_OK;
	}
	/* ADD (register) three-register */
	if ((insn & 0xFE00u) == 0x1800u) {
		rd = insn & 7u;
		r = c->r[(insn >> 3) & 7u] + c->r[(insn >> 6) & 7u];
		c->r[rd] = r;
		if (!c->in_it) flags_add(c, c->r[(insn >> 3) & 7u], c->r[(insn >> 6) & 7u], r);
		return N2A32_OK;
	}
	/* MOV (register), high registers */
	if ((insn & 0xFF00u) == 0x4600u) {
		rd = (insn & 7u) | ((insn >> 4) & 8u);
		rm = (insn >> 3) & 0xFu;
		c->r[rd] = c->r[rm];
		return N2A32_OK;
	}
	/* BX / BLX (register) */
	if ((insn & 0xFF87u) == 0x4700u) {
		n2a32_bx(c, c->r[(insn >> 3) & 0xFu]);
		return N2A32_OK;
	}
	if ((insn & 0xFF87u) == 0x4780u) {
		uint32_t pc = c->r[15] - 4u;
		c->r[14] = (pc + 2u) | 1u;
		n2a32_bx(c, c->r[(insn >> 3) & 0xFu]);
		return N2A32_OK;
	}
	/* LDR (literal) */
	if ((insn & 0xF800u) == 0x4800u) {
		rd = (insn >> 8) & 7u;
		addr = (c->r[15] & ~3u) + ((insn & 0xFFu) << 2);
		c->r[rd] = n2a32_ld32(c, addr);
		return N2A32_OK;
	}
	/* IT and hints */
	if ((insn & 0xFF00u) == 0xBF00u) {
		if ((insn & 0xFu) != 0)
			c->itstate = (uint8_t)((((insn >> 4) & 0xFu) << 4) | (insn & 0xFu));
		return N2A32_OK;
	}
	/* B unconditional */
	if ((insn & 0xF800u) == 0xE000u) {
		uint32_t off = (uint32_t)((int32_t)(insn << 21) >> 20);
		n2a32_branch(c, c->r[15] + off);
		return N2A32_OK;
	}
	/* B<cond> */
	if ((insn & 0xF000u) == 0xD000u) {
		unsigned cond = (insn >> 8) & 0xFu;
		if (n2a32_cond(c, cond)) {
			uint32_t off = (uint32_t)((int32_t)(insn << 24) >> 23);
			n2a32_branch(c, c->r[15] + off);
		}
		return N2A32_OK;
	}
	/* CBZ / CBNZ */
	if ((insn & 0xF500u) == 0xB100u) {
		unsigned nonzero = (insn >> 11) & 1u;
		uint32_t off = ((insn >> 3) & 0x1Fu) << 1;
		rm = insn & 7u;
		if ((c->r[rm] != 0) == (nonzero != 0))
			n2a32_branch(c, c->r[15] + off);
		return N2A32_OK;
	}
	if ((insn & 0xFF00u) == 0xDF00u) {                   /* SVC */
		c->stop = N2A32_STOP_SVC;
		return N2A32_UNDEF;
	}
	if ((insn & 0xFF00u) == 0xBE00u) {                   /* BKPT */
		c->stop = N2A32_STOP_BKPT;
		return N2A32_UNDEF;
	}
	if ((insn & 0xFF00u) == 0xDE00u)                     /* UDF */
		return N2A32_UNDEF;
	return N2A32_UNDEF;
}

/* ---- the class entry points the core calls ---- */
int n2a32_arm_dp(struct n2a32_cpu *c, uint32_t insn) { note("arm_dp", c, insn); return g_exec ? exec_arm_dp(c, insn) : N2A32_UNDEF; }
int n2a32_arm_media(struct n2a32_cpu *c, uint32_t insn) { note("arm_media", c, insn); return g_exec ? N2A32_UNDEF : N2A32_UNDEF; }
int n2a32_arm_mem(struct n2a32_cpu *c, uint32_t insn) { note("arm_mem", c, insn); return g_exec ? exec_arm_mem(c, insn) : N2A32_UNDEF; }
int n2a32_arm_misc(struct n2a32_cpu *c, uint32_t insn) { note("arm_misc", c, insn); return g_exec ? exec_arm_misc(c, insn) : N2A32_UNDEF; }
int n2a32_t16(struct n2a32_cpu *c, uint32_t insn) { note("t16", c, insn); return g_exec ? exec_t16(c, insn) : N2A32_UNDEF; }
int n2a32_t32_dp(struct n2a32_cpu *c, uint32_t insn) { note("t32_dp", c, insn); return N2A32_UNDEF; }
int n2a32_t32_mem(struct n2a32_cpu *c, uint32_t insn) { note("t32_mem", c, insn); return N2A32_UNDEF; }
int n2a32_t32_misc(struct n2a32_cpu *c, uint32_t insn) { note("t32_misc", c, insn); return N2A32_UNDEF; }
int n2a32_vfp(struct n2a32_cpu *c, uint32_t insn) { note("vfp", c, insn); return N2A32_UNDEF; }
int n2a32_neon_3same(struct n2a32_cpu *c, uint32_t insn) { note("neon_3same", c, insn); return N2A32_UNDEF; }
int n2a32_neon_shift(struct n2a32_cpu *c, uint32_t insn) { note("neon_shift", c, insn); return N2A32_UNDEF; }
int n2a32_neon_misc(struct n2a32_cpu *c, uint32_t insn) { note("neon_misc", c, insn); return N2A32_UNDEF; }
int n2a32_neon_ldst(struct n2a32_cpu *c, uint32_t insn) { note("neon_ldst", c, insn); return N2A32_UNDEF; }

/* ------------------------------------------------------------------ helpers */

static void put32(uint32_t addr, uint32_t v) { memcpy(g_mem->base + addr, &v, 4); }
static uint32_t get32(uint32_t addr) { uint32_t v; memcpy(&v, g_mem->base + addr, 4); return v; }
static void put16(uint32_t addr, uint16_t v) { memcpy(g_mem->base + addr, &v, 2); }

static void put_insn(uint32_t addr, uint32_t insn, int thumb, unsigned size)
{
	if (!thumb) {
		put32(addr, insn);
	} else if (size == 2) {
		put16(addr, (uint16_t)insn);
	} else {
		put16(addr, (uint16_t)(insn >> 16));
		put16(addr + 2, (uint16_t)(insn & 0xFFFFu));
	}
}

static enum n2a32_stop run_one(struct n2a32_cpu *c, uint32_t addr, uint32_t insn, int thumb, unsigned size)
{
	n2a32_cpu_init(c, g_mem);
	put_insn(addr, insn, thumb, size);
	c->thumb = (uint8_t)(thumb != 0);
	c->r[15] = addr;
	c->next_pc = addr;
	c->ilimit = 1;
	return n2a32_run(c);
}

/* ------------------------------------------------------------------ routing table */

struct route_case { const char *name; uint32_t insn; int thumb; const char *want; uint32_t want_insn; };

#define NOSEEN 0xFFFFFFFFu
static const struct route_case route_cases[] = {
	{ "A_ADD_REG", ENC_A_ADD_REG, 0, "arm_dp", NOSEEN },
	{ "A_MOV_IMM", ENC_A_MOV_IMM, 0, "arm_dp", NOSEEN },
	{ "A_MOVW", ENC_A_MOVW, 0, "arm_dp", NOSEEN },
	{ "A_MRS", ENC_A_MRS, 0, "arm_dp", NOSEEN },
	{ "A_CLZ", ENC_A_CLZ, 0, "arm_dp", NOSEEN },
	{ "A_MOV_FP_SP", ENC_A_MOV_FP_SP, 0, "arm_dp", NOSEEN },
	{ "A_MOV_SHIFT_REG", ENC_A_MOV_SHIFT_REG, 0, "arm_dp", NOSEEN },
	{ "A_LDRH_NEG", ENC_A_LDRH_NEG, 0, "arm_mem", NOSEEN },
	{ "A_YIELD", ENC_A_YIELD, 0, "arm_misc", NOSEEN },
	{ "A_MSR_IMM", ENC_A_MSR_IMM, 0, "arm_dp", NOSEEN },
	{ "A_MUL", ENC_A_MUL, 0, "arm_dp", NOSEEN },
	{ "A_NOP", ENC_A_NOP, 0, "arm_misc", NOSEEN },
	{ "A_SXTB", ENC_A_SXTB, 0, "arm_media", NOSEEN },
	{ "A_SDIV", ENC_A_SDIV, 0, "arm_media", NOSEEN },
	{ "A_QADD", ENC_A_QADD, 0, "arm_media", NOSEEN },
	{ "A_SMLABB", ENC_A_SMLABB, 0, "arm_media", NOSEEN },
	{ "A_SMULBB", ENC_A_SMULBB, 0, "arm_media", NOSEEN },
	{ "A_SMULWB", ENC_A_SMULWB, 0, "arm_media", NOSEEN },
	{ "A_LDR_IMM", ENC_A_LDR_IMM, 0, "arm_mem", NOSEEN },
	{ "A_LDR_REG", ENC_A_LDR_REG, 0, "arm_mem", NOSEEN },
	{ "A_LDRH", ENC_A_LDRH, 0, "arm_mem", NOSEEN },
	{ "A_LDREX", ENC_A_LDREX, 0, "arm_mem", NOSEEN },
	{ "A_LDM", ENC_A_LDM, 0, "arm_mem", NOSEEN },
	{ "A_PLD", ENC_A_PLD, 0, "arm_mem", NOSEEN },
	{ "A_PLI", ENC_A_PLI, 0, "arm_mem", NOSEEN },
	{ "A_PLD_REG", ENC_A_PLD_REG, 0, "arm_mem", NOSEEN },
	{ "A_PLI_NEG", ENC_A_PLI_NEG, 0, "arm_mem", NOSEEN },
	{ "A_PLD_NEG", ENC_A_PLD_NEG, 0, "arm_mem", NOSEEN },
	{ "A_PLDW", ENC_A_PLDW, 0, "arm_mem", NOSEEN },
	{ "A_PLDW_REG", ENC_A_PLDW_REG, 0, "arm_mem", NOSEEN },
	{ "A_PLI_REG", ENC_A_PLI_REG, 0, "arm_mem", NOSEEN },
	{ "A_B", ENC_A_B, 0, "arm_misc", NOSEEN },
	{ "A_BL", ENC_A_BL, 0, "arm_misc", NOSEEN },
	{ "A_BLX_REG", ENC_A_BLX_REG, 0, "arm_misc", NOSEEN },
	{ "A_BX", ENC_A_BX, 0, "arm_misc", NOSEEN },
	{ "A_SVC", ENC_A_SVC, 0, "arm_misc", NOSEEN },
	{ "A_BKPT", ENC_A_BKPT, 0, "arm_misc", NOSEEN },
	{ "A_UDF", ENC_A_UDF, 0, "arm_misc", NOSEEN },
	{ "A_DMB", ENC_A_DMB, 0, "arm_misc", NOSEEN },
	{ "A_MRC15", ENC_A_MRC15, 0, "arm_misc", NOSEEN },
	{ "A_VADD", ENC_A_VADD, 0, "vfp", NOSEEN },
	{ "A_VLDR", ENC_A_VLDR, 0, "vfp", NOSEEN },
	{ "A_VMAXNM", ENC_A_VMAXNM, 0, "vfp", NOSEEN },
	{ "A_NEON_ADD", ENC_A_NEON_ADD, 0, "neon_3same", NOSEEN },
	{ "A_NEON_SUB", ENC_A_NEON_SUB, 0, "neon_3same", NOSEEN },
	{ "A_NEON_SHL", ENC_A_NEON_SHL, 0, "neon_shift", NOSEEN },
	{ "A_NEON_ADDL", ENC_A_NEON_ADDL, 0, "neon_shift", NOSEEN },
	{ "A_NEON_MULL", ENC_A_NEON_MULL, 0, "neon_shift", NOSEEN },
	{ "A_NEON_MLAS", ENC_A_NEON_MLAS, 0, "neon_shift", NOSEEN },
	{ "A_NEON_MOVL", ENC_A_NEON_MOVL, 0, "neon_shift", NOSEEN },
	{ "A_NEON_EXT", ENC_A_NEON_EXT, 0, "neon_misc", NOSEEN },
	{ "A_NEON_TBL", ENC_A_NEON_TBL, 0, "neon_misc", NOSEEN },
	{ "A_NEON_DUP", ENC_A_NEON_DUP, 0, "neon_misc", NOSEEN },
	{ "A_NEON_IMM", ENC_A_NEON_IMM, 0, "neon_misc", NOSEEN },
	{ "A_NEON_REV", ENC_A_NEON_REV, 0, "neon_misc", NOSEEN },
	{ "A_NEON_CVT", ENC_A_NEON_CVT, 0, "neon_misc", NOSEEN },
	{ "A_NEON_VLD", ENC_A_NEON_VLD, 0, "neon_ldst", NOSEEN },
	{ "A_NEON_VST", ENC_A_NEON_VST, 0, "neon_ldst", NOSEEN },
	{ "A_NEON_VLDLANE", ENC_A_NEON_VLDLANE, 0, "neon_ldst", NOSEEN },
	{ "A_NEON_SHRN64", ENC_A_NEON_SHRN64, 0, "neon_shift", NOSEEN },
	{ "A_NEON_ADDL32", ENC_A_NEON_ADDL32, 0, "neon_shift", NOSEEN },
	{ "A_NEON_MLAS32", ENC_A_NEON_MLAS32, 0, "neon_shift", NOSEEN },
	{ "A_NEON_SHL64", ENC_A_NEON_SHL64, 0, "neon_shift", NOSEEN },
	{ "A_NEON_IM16", ENC_A_NEON_IM16, 0, "neon_misc", NOSEEN },
	{ "A_NEON_IM32D", ENC_A_NEON_IM32D, 0, "neon_misc", NOSEEN },
	{ "A_NEON_IMF32D", ENC_A_NEON_IMF32D, 0, "neon_misc", NOSEEN },
	{ "A_NEON_VORRD", ENC_A_NEON_VORRD, 0, "neon_misc", NOSEEN },

	{ "T_MOV_REG", ENC_T_MOV_REG, 1, "t16", NOSEEN },
	{ "T_NOP", ENC_T_NOP, 1, "t16", NOSEEN },
	{ "T_IT", ENC_T_IT, 1, "t16", NOSEEN },
	{ "T_PUSH", ENC_T_PUSH, 1, "t16", NOSEEN },
	{ "T_POP", ENC_T_POP, 1, "t16", NOSEEN },
	{ "T_BX", ENC_T_BX, 1, "t16", NOSEEN },
	{ "T_SVC", ENC_T_SVC, 1, "t16", NOSEEN },
	{ "T_BKPT", ENC_T_BKPT, 1, "t16", NOSEEN },
	{ "T_UDF", ENC_T_UDF, 1, "t16", NOSEEN },
	{ "T_LDRLIT", ENC_T_LDRLIT, 1, "t16", NOSEEN },
	{ "T_ADD_IMM", ENC_T_ADD_IMM, 1, "t16", NOSEEN },
	{ "T_B", ENC_T_B, 1, "t16", NOSEEN },
	{ "T_BEQ", ENC_T_BEQ, 1, "t16", NOSEEN },
	{ "T_CBZ", ENC_T_CBZ, 1, "t16", NOSEEN },

	{ "T32_ADD", ENC_T32_ADD, 1, "t32_dp", NOSEEN },
	{ "T32_AND", ENC_T32_AND, 1, "t32_dp", NOSEEN },
	{ "T32_MOV_IMM", ENC_T32_MOV_IMM, 1, "t32_dp", NOSEEN },
	{ "T32_MOVW", ENC_T32_MOVW, 1, "t32_dp", NOSEEN },
	{ "T32_ADDW", ENC_T32_ADDW, 1, "t32_dp", NOSEEN },
	{ "T32_SSAT", ENC_T32_SSAT, 1, "t32_dp", NOSEEN },
	{ "T32_UBFX", ENC_T32_UBFX, 1, "t32_dp", NOSEEN },
	{ "T32_MUL", ENC_T32_MUL, 1, "t32_dp", NOSEEN },
	{ "T32_UDIV", ENC_T32_UDIV, 1, "t32_dp", NOSEEN },
	{ "T32_CLZ", ENC_T32_CLZ, 1, "t32_dp", NOSEEN },
	{ "T32_REV", ENC_T32_REV, 1, "t32_dp", NOSEEN },
	{ "T32_QADD", ENC_T32_QADD, 1, "t32_dp", NOSEEN },
	{ "T32_SXTAB", ENC_T32_SXTAB, 1, "t32_dp", NOSEEN },
	{ "T32_PKHBT", ENC_T32_PKHBT, 1, "t32_dp", NOSEEN },
	{ "T32_LDR", ENC_T32_LDR, 1, "t32_mem", NOSEEN },
	{ "T32_STR", ENC_T32_STR, 1, "t32_mem", NOSEEN },
	{ "T32_LDRB", ENC_T32_LDRB, 1, "t32_mem", NOSEEN },
	{ "T32_LDRH", ENC_T32_LDRH, 1, "t32_mem", NOSEEN },
	{ "T32_LDRSB", ENC_T32_LDRSB, 1, "t32_mem", NOSEEN },
	{ "T32_LDR_REG", ENC_T32_LDR_REG, 1, "t32_mem", NOSEEN },
	{ "T32_LDRD", ENC_T32_LDRD, 1, "t32_mem", NOSEEN },
	{ "T32_LDREX", ENC_T32_LDREX, 1, "t32_mem", NOSEEN },
	{ "T32_TBB", ENC_T32_TBB, 1, "t32_mem", NOSEEN },
	{ "T32_LDM", ENC_T32_LDM, 1, "t32_mem", NOSEEN },
	{ "T32_STMDB", ENC_T32_STMDB, 1, "t32_mem", NOSEEN },
	{ "T32_PLD", ENC_T32_PLD, 1, "t32_mem", NOSEEN },
	{ "T32_PLI", ENC_T32_PLI, 1, "t32_mem", NOSEEN },
	{ "T32_B", ENC_T32_B, 1, "t32_misc", NOSEEN },
	{ "T32_BL", ENC_T32_BL, 1, "t32_misc", NOSEEN },
	{ "T32_BLX", ENC_T32_BLX, 1, "t32_misc", NOSEEN },
	{ "T32_BEQ", ENC_T32_BEQ, 1, "t32_misc", NOSEEN },
	{ "T32_MRS", ENC_T32_MRS, 1, "t32_misc", NOSEEN },
	{ "T32_MSR", ENC_T32_MSR, 1, "t32_misc", NOSEEN },
	{ "T32_DMB", ENC_T32_DMB, 1, "t32_misc", NOSEEN },
	{ "T32_UDF", ENC_T32_UDF, 1, "t32_misc", NOSEEN },
	{ "T32_VMOV", ENC_T32_VMOV, 1, "vfp", NOSEEN },
	{ "T32_VLDR", ENC_T32_VLDR, 1, "vfp", NOSEEN },
	{ "T32_VPUSH", ENC_T32_VPUSH, 1, "vfp", NOSEEN },
	{ "T32_VMAXNM", ENC_T32_VMAXNM, 1, "vfp", NOSEEN },
	{ "T32_NEON_ADD", ENC_T32_NEON_ADD, 1, "neon_3same", 0xF2020844u },
	{ "T32_NEON_SUB", ENC_T32_NEON_SUB, 1, "neon_3same", 0xF3110802u },
	{ "T32_NEON_SHL", ENC_T32_NEON_SHL, 1, "neon_shift", 0xF2890552u },
	{ "T32_NEON_IMM", ENC_T32_NEON_IMM, 1, "neon_misc", 0xF2800E50u },
	{ "T32_NEON_VLD", ENC_T32_NEON_VLD, 1, "neon_ldst", 0xF420070Fu },
	{ "T32_NEON_VST", ENC_T32_NEON_VST, 1, "neon_ldst", 0xF400028Du },
};

static void test_routing(void)
{
	struct n2a32_cpu cpu;
	unsigned i;

	g_exec = 0;
	for (i = 0; i < sizeof(route_cases) / sizeof(route_cases[0]); i++) {
		const struct route_case *rc = &route_cases[i];
		unsigned size = rc->thumb ? ((rc->insn >> 16) != 0 ? 4u : 2u) : 4u;
		enum n2a32_stop st;

		g_last_class = NULL;
		g_last_insn = 0;
		st = run_one(&cpu, 0x1000, rc->insn, rc->thumb, size);
		CHECK(st == N2A32_STOP_UNDEF, "%s: stop %d, expected UNDEF", rc->name, (int)st);
		CHECK(g_last_class != NULL && strcmp(g_last_class, rc->want) == 0,
		      "%s: routed to %s, expected %s", rc->name, g_last_class ? g_last_class : "(none)", rc->want);
		if (rc->want_insn != NOSEEN)
			CHECK(g_last_insn == rc->want_insn, "%s: class saw 0x%08x, expected 0x%08x",
			      rc->name, g_last_insn, rc->want_insn);
		if (rc->thumb && size == 2)
			CHECK((rc->insn & 0xFFFF0000u) == 0 || rc->insn < 0xE800u, "%s: not a 16-bit value", rc->name);
	}
	CHECK(cpu.mem == g_mem, "cpu_init did not record the memory object");
}

/* ------------------------------------------------------------------ memory API */

struct hook_ctx {
	int loads, stores, byte_calls;
	uint32_t last_addr, last_value;
	unsigned last_bytes;
};
static struct hook_ctx g_hc;
static uint32_t hook_rd(void *ctx, uint32_t addr, unsigned bytes);
static void hook_wr(void *ctx, uint32_t addr, unsigned bytes, uint32_t value);
static struct n2a32_mmio g_hook = { hook_rd, hook_wr, &g_hc };

static uint32_t hook_rd(void *ctx, uint32_t addr, unsigned bytes)
{
	struct hook_ctx *h = (struct hook_ctx *)ctx;
	h->loads++;
	h->last_addr = addr;
	h->last_bytes = bytes;
	if (bytes != 4)
		h->byte_calls++;
	return 0xA5000000u | (addr & 0xFFu);
}

static void hook_wr(void *ctx, uint32_t addr, unsigned bytes, uint32_t value)
{
	struct hook_ctx *h = (struct hook_ctx *)ctx;
	h->stores++;
	h->last_addr = addr;
	h->last_bytes = bytes;
	h->last_value = value;
	if (bytes != 4)
		h->byte_calls++;
}

static void test_memory(void)
{
	uint32_t a1, a2, a3;
	struct n2a32_mem *m2;

	CHECK(n2a32_mem_map(g_mem, 0x1001, 0x1000, 1) == -1, "unaligned map accepted");
	CHECK(n2a32_mem_map(g_mem, 0x0, 0x1000, 1) == -1, "mapping guest address 0 accepted");
	CHECK(n2a32_mem_map(g_mem, 0x1000, 0, 1) == -1, "zero-length map accepted");
	CHECK(n2a32_mem_map(g_mem, 0xFFFFF000u, 0x2000, 1) == -1, "map beyond 4 GiB accepted");
	CHECK(n2a32_mem_protect(g_mem, 0x80000000u, 0x1000, 0) == -1, "protect of unmapped memory accepted");

	a1 = n2a32_mem_alloc(g_mem, 0x1234, 0);
	CHECK(a1 != 0 && (a1 & 0xFFFu) == 0, "alloc returned %08x (not page aligned)", a1);
	CHECK(a1 >= 0x40000000u, "alloc below 0x40000000: %08x", a1);
	CHECK(g_mem->base[a1] == 0 && g_mem->base[a1 + 0x1233] == 0, "fresh allocation is not zeroed");
	memset(g_mem->base + a1, 0xAA, 0x2000);
	CHECK(n2a32_mem_map(g_mem, a1, 0x1000, 1) == 0, "remap of an allocated page failed");
	CHECK(g_mem->base[a1] == 0, "remap did not zero the page");

	{
		/* a partial-length remap must zero the whole rounded page range, not just len bytes */
		uint32_t ap = n2a32_mem_alloc(g_mem, 0x2000, 0);
		CHECK(ap != 0, "partial-map allocation failed");
		memset(g_mem->base + ap, 0xAA, 0x2000);
		CHECK(n2a32_mem_map(g_mem, ap, 0x100, 1) == 0, "partial map failed");
		CHECK(g_mem->base[ap + 0xFFFu] == 0, "partial map left a stale tail in its page");
		CHECK(g_mem->base[ap + 0x1000u] == 0xAA, "partial map zeroed a page outside its rounded length");
	}

	a2 = n2a32_mem_alloc(g_mem, 0x1000, 0x10000);
	CHECK(a2 != 0 && (a2 & 0xFFFFu) == 0, "aligned alloc returned %08x", a2);
	CHECK(a2 != a1, "alloc reused an address");
	a3 = n2a32_mem_alloc(g_mem, 0x1000, 0);
	CHECK(a3 != 0 && a3 != a1 && a3 != a2, "third alloc repeated an address");
	CHECK(n2a32_mem_alloc(g_mem, 0, 0) == 0, "alloc(0) accepted");
	CHECK(n2a32_mem_alloc(g_mem, 0x1000, 3) == 0, "non-power-of-two alignment accepted");

	memset(&g_hc, 0, sizeof(g_hc));
	CHECK(n2a32_mem_mmio(g_mem, 0x50000001u, 0x100000u, &g_hook) == -1, "unaligned mmio base accepted");
	CHECK(n2a32_mem_mmio(g_mem, 0u, 0x100000u, &g_hook) == -1, "mmio at guest address 0 accepted");
	CHECK(n2a32_mem_mmio(g_mem, 0x1000, 0x100000u, &g_hook) == -1, "mmio over mapped memory accepted");
	CHECK(n2a32_mem_mmio(g_mem, 0x50000000u, 0x200000u, &g_hook) == 0, "2 MiB mmio registration failed");
	CHECK(n2a32_is_mmio(g_mem, 0x50000000u) && n2a32_is_mmio(g_mem, 0x501FFFFFu), "mmio range not registered");
	CHECK(!n2a32_is_mmio(g_mem, 0x50200000u), "mmio range extends past its length");
	CHECK(n2a32_mem_map(g_mem, 0x50000000u, 0x1000, 1) == -1, "map over mmio accepted");

	/* a write-only hook (rd == NULL) must be stored in its slot, not mistaken for a free one */
	{
		struct n2a32_mmio wo;
		wo.rd = NULL;
		wo.wr = hook_wr;
		wo.ctx = &g_hc;
		CHECK(n2a32_mem_mmio(g_mem, 0x51000000u, 0x100000u, &wo) == 0, "write-only mmio registration failed");
		memset(&g_hc, 0, sizeof(g_hc));
		n2a32_mmio_store(g_mem, 0x51000100u, 4, 0x11223344u);
		CHECK(g_hc.stores == 1 && g_hc.last_addr == 0x51000100u && g_hc.last_value == 0x11223344u,
		      "write-only mmio hook not called: stores=%d addr=%08x value=%08x",
		      g_hc.stores, g_hc.last_addr, g_hc.last_value);
		n2a32_mmio_store(g_mem, 0x50000100u, 4, 0xAABBCCDDu);
		CHECK(g_hc.stores == 2 && g_hc.last_addr == 0x50000100u, "first mmio hook lost after the second registration");
	}

	m2 = n2a32_mem_create();
	CHECK(m2 != NULL, "second n2a32_mem_create failed");
	if (m2) {
		CHECK(n2a32_mem_map(m2, 0x2000, 0x1000, 0) == 0, "map read-only failed");
		CHECK(n2a32_mem_protect(m2, 0x2000, 0x1000, 1) == 0, "protect to writable failed");
		n2a32_mem_destroy(m2);
	}
}

/* ------------------------------------------------------------------ IT blocks */

static void run_guest_program(struct n2a32_cpu *c, uint32_t addr, const unsigned char *bytes, unsigned len,
			      int thumb, uint32_t stack, uint32_t lr)
{
	n2a32_cpu_init(c, g_mem);
	memcpy(g_mem->base + addr, bytes, len);
	c->thumb = (uint8_t)(thumb != 0);
	c->r[15] = addr;
	c->r[13] = stack;
	c->r[14] = lr;
}

static void test_it(void)
{
	struct n2a32_cpu cpu;
	enum n2a32_stop st;

	g_exec = 1;
	run_guest_program(&cpu, 0x30000, it_prog_bytes, IT_PROG_SIZE, 1, 0x8000, N2A32_RETURN_ADDR);
	cpu.z = 1;
	g_calls = 0;
	g_in_it_calls = 0;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_RETURN, "IT Z=1: stop %d, expected RETURN", (int)st);
	CHECK(cpu.r[0] == 3, "IT Z=1: r0=%u, expected 3", cpu.r[0]);
	CHECK(cpu.r[1] == 4, "IT Z=1: r1=%u, expected 4", cpu.r[1]);
	CHECK(g_in_it_calls == 2, "IT Z=1: %d in-block class calls, expected 2", g_in_it_calls);
	CHECK(cpu.itstate == 0, "IT Z=1: ITSTATE left 0x%02x", cpu.itstate);
	CHECK(g_calls >= 4, "IT Z=1: only %d class calls", g_calls);

	cpu.z = 0;
	g_calls = 0;
	g_in_it_calls = 0;
	run_guest_program(&cpu, 0x30000, it_prog_bytes, IT_PROG_SIZE, 1, 0x8000, N2A32_RETURN_ADDR);
	cpu.z = 0;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_RETURN, "IT Z=0: stop %d, expected RETURN", (int)st);
	CHECK(cpu.r[0] == 30, "IT Z=0: r0=%u, expected 30 (the E slot)", cpu.r[0]);
	CHECK(cpu.r[1] == 4, "IT Z=0: r1=%u, expected 4", cpu.r[1]);
	CHECK(g_in_it_calls == 1, "IT Z=0: %d in-block class calls, expected 1", g_in_it_calls);
	CHECK(cpu.itstate == 0, "IT Z=0: ITSTATE left 0x%02x", cpu.itstate);
}

/* ------------------------------------------------------------------ host calls and n2a32_call */

static uint32_t g_seen_arg[8];
static uint32_t g_argshim_sp;
static int g_argshim_calls;

static void shim_outer(struct n2a32_cpu *c)
{
	uint32_t r1 = 0;
	uint32_t v = n2a32_call(c, 0x20001u, NULL, 0, &r1);
	c->r[0] = v + 1u;
	c->r[1] = 0x55u;
}

static void shim_inner(struct n2a32_cpu *c)
{
	c->r[0] = 200u;
}

static void shim_seven(struct n2a32_cpu *c)
{
	c->r[0] = 7u;
}

static void shim_args(struct n2a32_cpu *c)
{
	uint32_t sum = 0;
	unsigned i;
	for (i = 0; i < 8; i++) {
		g_seen_arg[i] = (i < 4) ? c->r[i] : n2a32_ld32(c, c->r[13] + 4u * (i - 4u));
		sum += g_seen_arg[i];
	}
	g_argshim_calls++;
	g_argshim_sp = c->r[13];
	c->r[0] = sum;
	c->r[1] = 0x1234u;
}

static uint32_t g_udf_addr;

static void shim_nested_udf(struct n2a32_cpu *c)
{
	c->r[0] = n2a32_call(c, g_udf_addr, NULL, 0, NULL);
}

/* A host call whose nested n2a32_call stops (here: UDF) must stop the OUTER run too, with the inner reason,
 * address and encoding (hw-lead request; root cause of R1 F20).  Runs after the slot-order checks. */
static void test_nested_stop(void)
{
	struct n2a32_cpu cpu;
	enum n2a32_stop st;
	uint32_t slot = n2a32_hostcall_register(shim_nested_udf, "test.nested.udf");
	uint32_t w = ENC_A_UDF;

	CHECK(slot != 0, "nested-stop shim registration failed");
	g_udf_addr = 0x30000u;
	memcpy(g_mem->base + g_udf_addr, &w, 4);
	n2a32_cpu_init(&cpu, g_mem);
	cpu.r[13] = 0x8000u;
	cpu.r[14] = N2A32_RETURN_ADDR;
	cpu.r[15] = slot;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_UNDEF, "nested UDF: outer stop %d, expected UNDEF", (int)st);
	CHECK(cpu.stop_addr == g_udf_addr, "nested UDF: stop_addr %08x, expected %08x", cpu.stop_addr, g_udf_addr);
	CHECK(cpu.stop_insn == ENC_A_UDF, "nested UDF: stop_insn %08x", cpu.stop_insn);
}

static void test_hostcalls(void)
{
	struct n2a32_cpu cpu;
	enum n2a32_stop st;
	uint32_t outer, inner, alias, seven, args, dup;
	uint32_t r1 = 0;

	/* unresolved slot 0 first, before any registration */
	CHECK(n2a32_hostcall_name(N2A32_HOSTCALL_BASE) != NULL &&
	      strcmp(n2a32_hostcall_name(N2A32_HOSTCALL_BASE), "unresolved") == 0, "slot 0 is not named unresolved");
	n2a32_cpu_init(&cpu, g_mem);
	cpu.r[15] = N2A32_HOSTCALL_BASE;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_NOIMPORT, "unresolved slot: stop %d, expected NOIMPORT", (int)st);
	CHECK(cpu.stop_addr == N2A32_HOSTCALL_BASE, "unresolved slot: stop_addr 0x%08x", cpu.stop_addr);

	outer = n2a32_hostcall_register(shim_outer, "test.outer");
	inner = n2a32_hostcall_register(shim_inner, "test.inner");
	CHECK(outer == N2A32_HOSTCALL_BASE + 4u, "outer registered at 0x%08x", outer);
	CHECK(inner == N2A32_HOSTCALL_BASE + 8u, "inner registered at 0x%08x", inner);
	dup = n2a32_hostcall_register(shim_outer, "test.outer");
	CHECK(dup == outer, "duplicate registration changed the slot: 0x%08x != 0x%08x", dup, outer);
	alias = n2a32_hostcall_register(shim_outer, "test.outer.alias");
	CHECK(alias != outer && alias != 0, "same function with a different name reused the slot");
	seven = n2a32_hostcall_register(shim_seven, "test.seven");
	args = n2a32_hostcall_register(shim_args, "test.args");
	CHECK(seven != 0 && args != 0, "registration failed");
	CHECK(n2a32_hostcall_name(outer) && strcmp(n2a32_hostcall_name(outer), "test.outer") == 0, "name lookup failed");
	/* named-but-unimplemented slots: the loader's unresolved imports (V03) */
	{
		uint32_t u1 = n2a32_hostcall_register(NULL, "unresolved.symbol");
		uint32_t u2 = n2a32_hostcall_register(NULL, "unresolved.symbol");
		CHECK(u1 != 0 && u1 == u2, "NULL-fn registration: %08x / %08x", u1, u2);
		CHECK(n2a32_hostcall_name(u1) && strcmp(n2a32_hostcall_name(u1), "unresolved.symbol") == 0,
		      "NULL-fn slot name lost");
		n2a32_cpu_init(&cpu, g_mem);
		cpu.r[15] = u1;
		cpu.ilimit = 1;
		st = n2a32_run(&cpu);
		CHECK(st == N2A32_STOP_NOIMPORT, "NULL-fn slot: stop %d", (int)st);
		CHECK(cpu.stop_addr == u1, "NULL-fn slot: stop_addr %08x", cpu.stop_addr);
		CHECK(n2a32_hostcall_name(cpu.stop_addr) &&
		      strcmp(n2a32_hostcall_name(cpu.stop_addr), "unresolved.symbol") == 0,
		      "NULL-fn slot: name not recoverable from stop_addr");
	}
	CHECK(n2a32_hostcall_name(0xFFFFFFFFu) == NULL, "bad host-call address has a name");
	CHECK(n2a32_hostcall_name(N2A32_HOSTCALL_BASE + 2u) == NULL, "unaligned host-call address has a name");

	/* direct dispatch, return to ARM */
	n2a32_cpu_init(&cpu, g_mem);
	cpu.r[15] = seven;
	cpu.r[14] = N2A32_RETURN_ADDR;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_RETURN, "direct hostcall: stop %d", (int)st);
	CHECK(cpu.r[0] == 7, "direct hostcall: r0=%u", cpu.r[0]);
	CHECK(cpu.thumb == 0, "direct hostcall: thumb not 0");

	/* return with interworking to Thumb */
	n2a32_cpu_init(&cpu, g_mem);
	cpu.r[15] = seven;
	cpu.r[14] = N2A32_RETURN_ADDR | 1u;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_RETURN, "thumb hostcall: stop %d", (int)st);
	CHECK(cpu.thumb == 1, "thumb hostcall: interworking did not set Thumb");

	/* host -> ARM guest -> host shim -> Thumb guest -> host shim -> back */
	g_exec = 1;
	g_calls = 0;
	run_guest_program(&cpu, 0x10000, guest_arm_bytes, GUEST_ARM_SIZE, 0, 0x8000, 0);
	memcpy(g_mem->base + 0x20000, guest_thumb_bytes, GUEST_THUMB_SIZE);
	cpu.r[0] = 0xAAAAu;
	cpu.r[1] = 0xBBBBu;
	cpu.r[2] = 0xCCCCu;
	cpu.r[3] = 0xDDDDu;
	g_argshim_calls = 0;
	{
		uint32_t ret = n2a32_call(&cpu, 0x10000u, NULL, 0, &r1);
		CHECK(ret == 213u, "nested call returned %u, expected 213", ret);
		CHECK(r1 == 0x55u, "nested call r1=%08x, expected 0x55", r1);
		CHECK(cpu.stop == N2A32_STOP_RETURN, "nested call: cpu.stop %d", (int)cpu.stop);
		CHECK(cpu.r[2] == 0xCCCCu && cpu.r[3] == 0xDDDDu, "nested call did not restore caller-saved registers");
		CHECK(cpu.r[13] == 0x8000u, "nested call did not restore sp: %08x", cpu.r[13]);
	}

	/* stack arguments of n2a32_call */
	n2a32_cpu_init(&cpu, g_mem);
	cpu.r[13] = 0x8000u;
	{
		uint32_t av[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
		uint32_t ret;
		g_argshim_calls = 0;
		ret = n2a32_call(&cpu, args, av, 8, &r1);
		CHECK(ret == 36u, "stack-argument call returned %u, expected 36", ret);
		CHECK(r1 == 0x1234u, "stack-argument r1=%08x", r1);
		CHECK(g_argshim_calls == 1, "argument shim called %d times", g_argshim_calls);
		CHECK(g_seen_arg[4] == 5u && g_seen_arg[5] == 6u && g_seen_arg[6] == 7u && g_seen_arg[7] == 8u,
		      "stack args %u,%u,%u,%u", g_seen_arg[4], g_seen_arg[5], g_seen_arg[6], g_seen_arg[7]);
		CHECK((g_argshim_sp & 7u) == 0, "sp not 8-aligned inside the callee: %08x", g_argshim_sp);
		CHECK(cpu.r[13] == 0x8000u, "sp not restored after the call: %08x", cpu.r[13]);
	}
}

/* ------------------------------------------------------------------ faults */

static void test_faults(void)
{
	struct n2a32_cpu cpu;
	enum n2a32_stop st;

	/* instruction fetch from unmapped memory */
	n2a32_cpu_init(&cpu, g_mem);
	cpu.r[15] = 0x80000000u;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_FAULT, "fetch fault: stop %d, expected FAULT", (int)st);
	CHECK(cpu.stop_addr == 0x80000000u, "fetch fault: stop_addr 0x%08x", cpu.stop_addr);
	CHECK(cpu.r[15] == 0x80000000u, "fetch fault: r15 0x%08x", cpu.r[15]);

	/* the host guard page after 4 GiB: a T32 fetch whose second halfword would cross the end */
	CHECK(n2a32_mem_map(g_mem, 0xFFFFF000u, 0x1000u, 1) == 0, "mapping the last guest page failed");
	put16(0xFFFFFFFEu, 0xE800u);
	n2a32_cpu_init(&cpu, g_mem);
	cpu.thumb = 1;
	cpu.r[15] = 0xFFFFFFFEu;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_FAULT, "boundary fetch: stop %d, expected FAULT", (int)st);
	CHECK(cpu.stop_addr == 0xFFFFFFFEu, "boundary fetch: stop_addr %08x", cpu.stop_addr);
	CHECK(cpu.r[15] == 0xFFFFFFFEu, "boundary fetch: r15 %08x", cpu.r[15]);

	/* and a data access that starts inside the last page but crosses the end */
	g_exec = 1;
	n2a32_cpu_init(&cpu, g_mem);
	put32(0x1000, ENC_A_LDR0);
	cpu.r[15] = 0x1000u;
	cpu.r[1] = 0xFFFFFFFEu;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_FAULT, "boundary load: stop %d, expected FAULT", (int)st);
	CHECK(cpu.stop_addr == 0xFFFFFFFFu, "boundary load: stop_addr %08x", cpu.stop_addr);
	CHECK(cpu.r[15] == 0x1000u, "boundary load: r15 %08x", cpu.r[15]);

	/* a word load wholly inside the last page still works */
	n2a32_cpu_init(&cpu, g_mem);
	put32(0x1000, ENC_A_LDR0);
	cpu.r[15] = 0x1000u;
	cpu.r[1] = 0xFFFFFFFCu;
	cpu.ilimit = 1;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_LIMIT, "last-page load: stop %d", (int)st);

	/* data load from unmapped memory */
	g_exec = 1;
	n2a32_cpu_init(&cpu, g_mem);
	put32(0x1000, ENC_A_LDR0);
	cpu.r[15] = 0x1000u;
	cpu.r[1] = 0x90000000u;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_FAULT, "data fault: stop %d, expected FAULT", (int)st);
	CHECK(cpu.stop_addr == 0x90000000u, "data fault: stop_addr 0x%08x", cpu.stop_addr);
	CHECK(cpu.r[15] == 0x1000u, "data fault: r15 0x%08x, expected 0x1000", cpu.r[15]);
}

/* ------------------------------------------------------------------ condition skip and read-only memory */

static void test_cond_skip(void)
{
	struct n2a32_cpu cpu;
	enum n2a32_stop st;

	g_exec = 1;
	n2a32_cpu_init(&cpu, g_mem);
	put32(0x1000, ENC_A_MOVEQ_IMM);
	put32(0x1004, ENC_A_MOV2_IMM);
	cpu.r[15] = 0x1000u;
	cpu.ilimit = 2;
	cpu.r[0] = 0;
	g_calls = 0;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_LIMIT, "cond skip: stop %d", (int)st);
	CHECK(cpu.r[0] == 0u, "cond skip (Z=0): r0=%u, expected 0", cpu.r[0]);
	CHECK(cpu.r[1] == 2u, "cond skip (Z=0): r1=%u, expected 2", cpu.r[1]);
	CHECK(g_calls == 1, "cond skip (Z=0): %d class calls, expected 1", g_calls);

	n2a32_cpu_init(&cpu, g_mem);
	put32(0x1000, ENC_A_MOVEQ_IMM);
	put32(0x1004, ENC_A_MOV2_IMM);
	cpu.r[15] = 0x1000u;
	cpu.ilimit = 2;
	cpu.z = 1;
	cpu.r[0] = 0;
	g_calls = 0;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_LIMIT, "cond run: stop %d", (int)st);
	CHECK(cpu.r[0] == 1u, "cond run (Z=1): r0=%u, expected 1", cpu.r[0]);
	CHECK(cpu.r[1] == 2u, "cond run (Z=1): r1=%u, expected 2", cpu.r[1]);
	CHECK(g_calls == 2, "cond run (Z=1): %d class calls, expected 2", g_calls);

	/* a store into read-only guest memory is a fault, a load from it is not */
	CHECK(n2a32_mem_protect(g_mem, 0x3000, 0x1000, 0) == 0, "protect 0x3000 read-only failed");
	n2a32_cpu_init(&cpu, g_mem);
	put32(0x1000, ENC_A_LDR0);
	cpu.r[15] = 0x1000u;
	cpu.r[1] = 0x3000u;
	cpu.ilimit = 1;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_LIMIT, "RO read: stop %d", (int)st);

	n2a32_cpu_init(&cpu, g_mem);
	put32(0x1000, ENC_A_STR0);
	cpu.r[15] = 0x1000u;
	cpu.r[1] = 0x3000u;
	cpu.r[2] = 0x1234u;
	cpu.ilimit = 1;
	st = n2a32_run(&cpu);
	{
		/* Host protection is per host page (n2a32_mem.c): with a granule above 4 KiB, 0x3000 shares its host page
		 * with writable pages, so the store succeeds by design.  Check the fault only on a 4 KiB granule. */
		long g = sysconf(_SC_PAGESIZE);
		const char *ge = getenv("N2A32_MEM_GRANULE");
		if (ge && *ge && strtol(ge, NULL, 0) > g)
			g = strtol(ge, NULL, 0);
		if (g <= 4096) {
			CHECK(st == N2A32_STOP_FAULT, "RO write: stop %d, expected FAULT", (int)st);
			CHECK(cpu.stop_addr == 0x3000u, "RO write: stop_addr %08x", cpu.stop_addr);
		} else {
			CHECK(st == N2A32_STOP_LIMIT, "RO write (granule %ld): stop %d, expected LIMIT", g, (int)st);
		}
	}
	CHECK(n2a32_mem_protect(g_mem, 0x3000, 0x1000, 1) == 0, "protect 0x3000 writable failed");
}

/* ------------------------------------------------------------------ MMIO */

static void test_mmio(void)
{
	struct n2a32_cpu cpu;
	enum n2a32_stop st;
	uint32_t v;

	memset(&g_hc, 0, sizeof(g_hc));
	g_exec = 1;

	/* LDR from a register page */
	n2a32_cpu_init(&cpu, g_mem);
	put32(0x1000, ENC_A_LDR0);
	cpu.r[15] = 0x1000u;
	cpu.r[1] = 0x50000100u;
	cpu.ilimit = 1;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_LIMIT, "mmio load: stop %d", (int)st);
	CHECK(g_hc.loads == 1 && g_hc.last_addr == 0x50000100u && g_hc.last_bytes == 4,
	      "mmio load hook: loads=%d addr=%08x bytes=%u", g_hc.loads, g_hc.last_addr, g_hc.last_bytes);
	CHECK(cpu.r[0] == (0xA5000000u | 0x00u), "mmio load value %08x", cpu.r[0]);

	/* STR to a register page */
	n2a32_cpu_init(&cpu, g_mem);
	put32(0x1000, ENC_A_STR_IMM);
	cpu.r[15] = 0x1000u;
	cpu.r[1] = 0x50000100u;
	cpu.r[2] = 0xDEADBEEFu;
	cpu.ilimit = 1;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_LIMIT, "mmio store: stop %d", (int)st);
	CHECK(g_hc.stores == 1 && g_hc.last_addr == 0x50000104u && g_hc.last_bytes == 4 && g_hc.last_value == 0xDEADBEEFu,
	      "mmio store hook: stores=%d addr=%08x bytes=%u value=%08x", g_hc.stores, g_hc.last_addr, g_hc.last_bytes, g_hc.last_value);

	/* a 32-bit access that straddles a megabyte: bytes go through the hook in order */
	memset(&g_hc, 0, sizeof(g_hc));
	n2a32_cpu_init(&cpu, g_mem);
	put32(0x1000, ENC_A_LDR0);
	cpu.r[15] = 0x1000u;
	cpu.r[1] = 0x500FFFFEu;
	cpu.ilimit = 1;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_LIMIT, "mmio cross: stop %d", (int)st);
	CHECK(g_hc.byte_calls == 4 && g_hc.loads == 4, "mmio cross: %d calls (%d non-word)", g_hc.loads, g_hc.byte_calls);
	v = (0xA5000000u | 0xFEu) | ((0xA5000000u | 0xFFu) << 8) |
	    ((0xA5000000u | 0x00u) << 16) | ((0xA5000000u | 0x01u) << 24);
	CHECK(cpu.r[0] == v, "mmio cross value %08x, expected %08x", cpu.r[0], v);
}

/* ------------------------------------------------------------------ exclusive monitor */

static void test_excl(void)
{
	struct n2a32_cpu c1, c2;
	uint64_t w;

	n2a32_cpu_init(&c1, g_mem);
	n2a32_cpu_init(&c2, g_mem);

	put32(0x2000, 0x11223344u);
	n2a32_excl_mark(&c1, 0x2000, 4, 0x11223344u);
	CHECK(n2a32_excl_store(&c1, 0x2000, 4, 0xAABBCCDDu) == 0, "STREX of the current value failed");
	CHECK(get32(0x2000) == 0xAABBCCDDu, "STREX did not store: %08x", get32(0x2000));

	n2a32_excl_mark(&c1, 0x2000, 4, 0x11223344u);          /* stale expected value */
	CHECK(n2a32_excl_store(&c1, 0x2000, 4, 0x55667788u) == 1, "STREX succeeded with a stale value");
	CHECK(get32(0x2000) == 0xAABBCCDDu, "failed STREX changed memory");

	n2a32_excl_mark(&c1, 0x2000, 4, 0xAABBCCDDu);
	CHECK(n2a32_excl_store(&c1, 0x2000, 1, 0x11u) == 1, "STREX with the wrong width succeeded");
	CHECK(n2a32_excl_store(&c1, 0x2000, 4, 0x1u) == 1, "STREX after a failed width change succeeded");

	put32(0x2000, 0x12345678u);
	n2a32_excl_mark(&c1, 0x2000, 4, 0x12345678u);
	c1.excl_valid = 0;                                       /* CLREX */
	CHECK(n2a32_excl_store(&c1, 0x2000, 4, 0x9u) == 1, "STREX after CLREX succeeded");

	put32(0x2000, 0x01020304u);
	n2a32_excl_mark(&c1, 0x2001, 4, 0x01020304u);
	CHECK(n2a32_excl_store(&c1, 0x2001, 4, 0x05060708u) == 1, "unaligned STREX succeeded");

	/* 8/16/64-bit widths */
	g_mem->base[0x2008] = 0x5Au;
	n2a32_excl_mark(&c1, 0x2008, 1, 0x5Au);
	CHECK(n2a32_excl_store(&c1, 0x2008, 1, 0xA5u) == 0, "8-bit STREX failed");
	CHECK(g_mem->base[0x2008] == 0xA5, "8-bit STREX value %02x", g_mem->base[0x2008]);
	put16(0x200A, 0xCAFEu);
	n2a32_excl_mark(&c1, 0x200A, 2, 0xCAFEu);
	CHECK(n2a32_excl_store(&c1, 0x200A, 2, 0xBEEFu) == 0, "16-bit STREX failed");
	{
		uint16_t h;
		memcpy(&h, g_mem->base + 0x200A, 2);
		CHECK(h == 0xBEEFu, "16-bit STREX value %04x", h);
	}
	w = 0x0102030405060708ull;
	memcpy(g_mem->base + 0x2010, &w, 8);
	n2a32_excl_mark(&c1, 0x2010, 8, w);
	CHECK(n2a32_excl_store(&c1, 0x2010, 8, 0x1112131415161718ull) == 0, "64-bit STREX failed");
	memcpy(&w, g_mem->base + 0x2010, 8);
	CHECK(w == 0x1112131415161718ull, "64-bit STREX value %016llx", (unsigned long long)w);

	/* two CPUs race for the same word: exactly one compare-and-swap can win */
	put32(0x2020, 0x0u);
	n2a32_excl_mark(&c1, 0x2020, 4, 0u);
	n2a32_excl_mark(&c2, 0x2020, 4, 0u);
	{
		int s1 = n2a32_excl_store(&c1, 0x2020, 4, 1u);
		int s2 = n2a32_excl_store(&c2, 0x2020, 4, 1u);
		CHECK(s1 != s2, "two STREX claims: %d and %d (exactly one must win)", s1, s2);
		CHECK(get32(0x2020) == 1u, "winner did not store");
	}
}

/* ------------------------------------------------------------------ random-word robustness sweep */

static void test_sweep(void)
{
	struct n2a32_cpu cpu;
	uint32_t s = 0x9E3779B9u;
	unsigned i;

	g_exec = 0;                     /* every class records and returns N2A32_UNDEF */
	for (i = 0; i < 20000; i++) {
		enum n2a32_stop st;
		int thumb = (int)(i & 1u);
		s = s * 1664525u + 1013904223u;
		st = run_one(&cpu, 0x1000, s, thumb, 4);
		if (st != N2A32_STOP_UNDEF && st != N2A32_STOP_LIMIT) {
			CHECK(0, "sweep word %08x thumb=%d: stop %d", s, thumb, (int)st);
			break;
		}
	}
	CHECK(1, "sweep completed");
}

/* ------------------------------------------------------------------ stop reasons and the loop */

static void test_stops(void)
{
	struct n2a32_cpu cpu;
	enum n2a32_stop st;

	g_exec = 1;
	n2a32_cpu_init(&cpu, g_mem);
	cpu.stop_request = 1;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_REQUEST, "stop_request: stop %d", (int)st);
	CHECK(cpu.icount == 0, "stop_request executed %llu instructions", (unsigned long long)cpu.icount);

	st = run_one(&cpu, 0x1000, ENC_A_SVC, 0, 4);
	CHECK(st == N2A32_STOP_SVC, "SVC: stop %d", (int)st);
	CHECK(cpu.stop_insn == ENC_A_SVC, "SVC: stop_insn %08x", cpu.stop_insn);

	st = run_one(&cpu, 0x1000, ENC_T_BKPT, 1, 2);
	CHECK(st == N2A32_STOP_BKPT, "BKPT: stop %d", (int)st);
	CHECK(cpu.stop_insn == ENC_T_BKPT, "BKPT: stop_insn %08x", cpu.stop_insn);

	st = run_one(&cpu, 0x1000, ENC_A_UDF, 0, 4);
	CHECK(st == N2A32_STOP_UNDEF, "UDF: stop %d", (int)st);
	CHECK(cpu.stop_insn == ENC_A_UDF, "UDF: stop_insn %08x", cpu.stop_insn);

	st = run_one(&cpu, 0x1000, ENC_A_SXTB, 0, 4);
	CHECK(st == N2A32_STOP_UNDEF, "unimplemented class encoding: stop %d", (int)st);

	n2a32_cpu_init(&cpu, g_mem);
	cpu.r[15] = N2A32_RETURN_ADDR;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_RETURN, "RETURN_ADDR: stop %d", (int)st);

	/* guest loop: add r0,r0,#1; b back, bounded by ilimit */
	memcpy(g_mem->base + 0x40000, loop_prog_bytes, LOOP_PROG_SIZE);
	n2a32_cpu_init(&cpu, g_mem);
	cpu.r[15] = 0x40000u;
	cpu.ilimit = 1001u;
	st = n2a32_run(&cpu);
	CHECK(st == N2A32_STOP_LIMIT, "loop: stop %d", (int)st);
	CHECK(cpu.icount == 1001u, "loop: icount %llu", (unsigned long long)cpu.icount);
	CHECK(cpu.r[0] == 501u, "loop: r0=%u, expected 501", cpu.r[0]);
}

/* The core's predecoded fast paths (opus-perf, emu/n2a32_fast.h) execute common T16/T32 forms without calling
 * the class function.  With the real classes the result is identical (tests/perf/t_fast.c checks that); this
 * test replaces the classes with recorders, so it checks the routing with the fast paths switched off. */
extern volatile int n2a32_core_fastpath;

int main(void)
{
	n2a32_core_fastpath = 0;
	g_mem = n2a32_mem_create();
	if (!g_mem) {
		printf("FATAL: n2a32_mem_create failed\n");
		return 1;
	}
	if (n2a32_mem_map(g_mem, 0x1000, 0x40000, 1) != 0) {
		printf("FATAL: map of the test window failed\n");
		return 1;
	}

	test_hostcalls();      /* first: the registry slot order is part of the test */
	test_nested_stop();
	test_memory();
	test_routing();
	test_it();
	test_cond_skip();
	test_faults();
	test_mmio();
	test_excl();
	test_stops();
	test_sweep();

	n2a32_mem_destroy(g_mem);
	printf("TESTS %d/%d\n", g_pass, g_pass + g_fail);
	return g_fail == 0 ? 0 : 1;
}
