/* SPDX-License-Identifier: MIT */
/*
 * n2a32_fast.h -- predecoded fast paths for the hottest T16/T32 forms (opus-perf).  Included only by
 * n2a32_core.c; not an interface.
 *
 * The core keeps a per-thread direct-mapped cache of decoded instructions keyed by (pc, thumb).  Every
 * entry also stores the raw encoding, and the core re-fetches and compares it on every hit, so the cache
 * is a pure memo of decode(raw, thumb): self-modifying code, remapped libraries and several address spaces
 * need no invalidation (a changed word is simply a miss).
 *
 * fast_decode() recognises a small set of very common, side-effect-simple forms and records their operands.
 * Everything else (and every form inside an IT block) still goes through the instruction-class functions,
 * which remain the reference semantics.  Excluded on purpose: any form that writes r15 except the plain
 * branches/BX/POP {pc} listed here, UNPREDICTABLE register combinations, flag-setting forms whose carry
 * comes from a register-specified shift.  tests/perf/t_fast.c checks every T16 encoding and millions of
 * random T32 encodings, single-stepped through n2a32_run() with and without fast paths, for identical
 * architectural state, memory and stop reason.
 */
#ifndef N2A32_FAST_H
#define N2A32_FAST_H

enum fast_op {
	F_NONE = 0,
	/* T16 shifts / moves / arithmetic, flag setting (outside IT only) */
	F_LSL_I, F_LSR_I, F_ASR_I, F_MOVS_R, F_ADDS_R, F_SUBS_R, F_ADDS_I, F_SUBS_I, F_CMP_I, F_MOVS_I,
	F_AND, F_EOR, F_ADC, F_SBC, F_TST, F_RSB0, F_CMP_R, F_CMN, F_ORR, F_MUL, F_BIC, F_MVN,
	/* no flags */
	F_ADD_R_NF, F_MOV_R_NF, F_ADD_I_NF, F_ADR, F_MOVW, F_SXTH, F_SXTB, F_UXTH, F_UXTB,
	F_CMP_HI,
	/* loads / stores: ra = rt, rb = rn, rc = rm; imm = offset (two's complement) */
	F_LDR_LIT, F_LDR_RR, F_STR_RR, F_LDRB_RR, F_STRB_RR, F_LDRH_RR, F_STRH_RR, F_LDRSB_RR, F_LDRSH_RR,
	F_LDR_RI, F_STR_RI, F_LDRB_RI, F_STRB_RI, F_LDRH_RI, F_STRH_RI, F_LDRSB_RI, F_LDRSH_RI,
	F_LDRD_RI, F_STRD_RI,                 /* ra = rt, rc = rt2 */
	F_PUSH, F_POP,                        /* imm = 16-bit register mask (bit 14 lr, bit 15 pc) */
	/* branches: imm = offset from c->r[15] */
	F_B, F_BCOND, F_CBZ, F_CBNZ, F_BL, F_BLX_I, F_BX, F_BLX_R,
	/* T32 modified-immediate data processing: imm = expanded constant; fl bit0 = S, bit1 = carry := imm>>31 */
	F_ADDW_I, F_SUBW_I, F_CMPW_I, F_CMNW_I, F_MOVW_I, F_MVNW_I, F_ANDW_I, F_ORRW_I, F_EORW_I, F_BICW_I, F_TSTW_I,
	F_RSBW_I
};

struct dent {
	uint32_t key;        /* pc | thumb; DC_EMPTY = unused */
	uint32_t raw;        /* the encoding fetched at pc (T32: hw1 << 16 | hw2) */
	uint32_t routed;     /* the word the class function receives */
	uint8_t size;        /* 2 or 4 */
	uint8_t op;          /* enum fast_op */
	uint8_t ra, rb, rc, fl;
	uint16_t pad;
	uint32_t imm;
	class_fn fn;
};

static inline uint32_t fast_sext(uint32_t v, unsigned bits) { uint32_t m = 1u << (bits - 1); return (v ^ m) - m; }
static inline unsigned fast_popcount16(uint32_t x) { return (unsigned)__builtin_popcount(x & 0xFFFFu); }

/* ---- predecode ---------------------------------------------------------------------------------------- */

static void fast_t16(struct dent *e, uint32_t w)
{
	unsigned rd = w & 7u, rn = (w >> 3) & 7u, rm = (w >> 6) & 7u;
	unsigned imm5 = (w >> 6) & 0x1Fu;

	e->ra = (uint8_t)rd; e->rb = (uint8_t)rn; e->rc = (uint8_t)rm;
	switch (w >> 11) {
	case 0x00:                                              /* LSL imm / MOVS reg */
		if (imm5 == 0) { e->op = F_MOVS_R; return; }
		e->op = F_LSL_I; e->imm = imm5; return;
	case 0x01: e->op = F_LSR_I; e->imm = imm5 ? imm5 : 32u; return;
	case 0x02: e->op = F_ASR_I; e->imm = imm5 ? imm5 : 32u; return;
	case 0x03:
		switch ((w >> 9) & 3u) {
		case 0: e->op = F_ADDS_R; return;
		case 1: e->op = F_SUBS_R; return;
		case 2: e->op = F_ADDS_I; e->imm = rm; return;
		default: e->op = F_SUBS_I; e->imm = rm; return;
		}
	case 0x04: e->op = F_MOVS_I; e->ra = (uint8_t)((w >> 8) & 7u); e->imm = w & 0xFFu; return;
	case 0x05: e->op = F_CMP_I;  e->rb = (uint8_t)((w >> 8) & 7u); e->imm = w & 0xFFu; return;
	case 0x06: e->op = F_ADDS_I; e->ra = e->rb = (uint8_t)((w >> 8) & 7u); e->imm = w & 0xFFu; return;
	case 0x07: e->op = F_SUBS_I; e->ra = e->rb = (uint8_t)((w >> 8) & 7u); e->imm = w & 0xFFu; return;
	case 0x08:
		if ((w & 0xFC00u) == 0x4000u) {                    /* data processing register: ra = rdn, rb = rm */
			static const uint8_t ops[16] = { F_AND, F_EOR, 0, 0, 0, F_ADC, F_SBC, 0,
							 F_TST, F_RSB0, F_CMP_R, F_CMN, F_ORR, F_MUL, F_BIC, F_MVN };
			e->op = ops[(w >> 6) & 0xFu];
			return;
		}
		/* 0100 01xx: special data processing and BX/BLX; D:Rdn and Rm are 4-bit */
		rd = (w & 7u) | ((w >> 4) & 8u);
		rm = (w >> 3) & 0xFu;
		e->ra = (uint8_t)rd; e->rb = (uint8_t)rm;
		switch ((w >> 8) & 3u) {
		case 0:                                         /* ADD rdn, rm (no flags) */
			if (rd == 15u || (rd == 13u && rm == 13u))
				return;
			e->op = F_ADD_R_NF; e->rb = (uint8_t)rd; e->rc = (uint8_t)rm; return;
		case 1:                                         /* CMP (high registers) */
			if ((rd < 8u && rm < 8u) || rd == 15u || rm == 15u)
				return;
			e->op = F_CMP_HI; e->rb = (uint8_t)rd; e->rc = (uint8_t)rm; return;
		case 2:                                         /* MOV rd, rm (no flags) */
			if (rd == 15u || rm == 15u)
				return;
			e->op = F_MOV_R_NF; e->rb = (uint8_t)rm; return;
		default:                                        /* BX / BLX register */
			if ((w & 7u) != 0 || rm == 15u)
				return;
			e->op = (w & 0x80u) ? F_BLX_R : F_BX; e->rb = (uint8_t)rm; return;
		}
	case 0x09: e->op = F_LDR_LIT; e->ra = (uint8_t)((w >> 8) & 7u); e->imm = (w & 0xFFu) << 2; return;
	case 0x0A: case 0x0B: {                                 /* load/store register offset: ra = rt, rb = rn, rc = rm */
		static const uint8_t ops[8] = { F_STR_RR, F_STRH_RR, F_STRB_RR, F_LDRSB_RR,
						F_LDR_RR, F_LDRH_RR, F_LDRB_RR, F_LDRSH_RR };
		e->op = ops[(w >> 9) & 7u];
		return;
	}
	case 0x0C: e->op = F_STR_RI;  e->imm = imm5 << 2; return;
	case 0x0D: e->op = F_LDR_RI;  e->imm = imm5 << 2; return;
	case 0x0E: e->op = F_STRB_RI; e->imm = imm5; return;
	case 0x0F: e->op = F_LDRB_RI; e->imm = imm5; return;
	case 0x10: e->op = F_STRH_RI; e->imm = imm5 << 1; return;
	case 0x11: e->op = F_LDRH_RI; e->imm = imm5 << 1; return;
	case 0x12: case 0x13:                                   /* STR/LDR rt, [sp, #imm8 * 4] */
		e->op = (w & 0x0800u) ? F_LDR_RI : F_STR_RI;
		e->ra = (uint8_t)((w >> 8) & 7u); e->rb = 13; e->imm = (w & 0xFFu) << 2; return;
	case 0x14: e->op = F_ADR; e->ra = (uint8_t)((w >> 8) & 7u); e->imm = (w & 0xFFu) << 2; return;
	case 0x15: e->op = F_ADD_I_NF; e->ra = (uint8_t)((w >> 8) & 7u); e->rb = 13; e->imm = (w & 0xFFu) << 2; return;
	case 0x16: case 0x17:                                   /* miscellaneous 16-bit */
		if ((w & 0xFF00u) == 0xB000u) {                 /* ADD/SUB SP, SP, #imm7 * 4 */
			uint32_t imm = (w & 0x7Fu) << 2;
			e->op = F_ADD_I_NF; e->ra = e->rb = 13; e->imm = (w & 0x80u) ? (0u - imm) : imm; return;
		}
		if ((w & 0xF500u) == 0xB100u) {                 /* CBZ / CBNZ */
			e->op = (w & 0x0800u) ? F_CBNZ : F_CBZ; e->rb = (uint8_t)(w & 7u);
			e->imm = (((w >> 3) & 0x1Fu) << 1) | (((w >> 9) & 1u) << 6); return;
		}
		if ((w & 0xFF00u) == 0xB200u) {                 /* SXTH/SXTB/UXTH/UXTB rd, rm */
			static const uint8_t ops[4] = { F_SXTH, F_SXTB, F_UXTH, F_UXTB };
			e->op = ops[(w >> 6) & 3u]; return;
		}
		if ((w & 0xFE00u) == 0xB400u && (w & 0x1FFu) != 0) { /* PUSH {r0-r7[,lr]} */
			e->op = F_PUSH; e->imm = (w & 0xFFu) | ((w & 0x100u) << 6); return;
		}
		if ((w & 0xFE00u) == 0xBC00u && (w & 0x1FFu) != 0) { /* POP {r0-r7[,pc]} */
			e->op = F_POP; e->imm = (w & 0xFFu) | ((w & 0x100u) << 7); return;
		}
		return;
	case 0x1A: case 0x1B: {                                 /* B<c> (not UDF / SVC) */
		unsigned cond = (w >> 8) & 0xFu;
		if (cond >= 0xEu)
			return;
		e->op = F_BCOND; e->ra = (uint8_t)cond; e->imm = fast_sext((w & 0xFFu) << 1, 9); return;
	}
	case 0x1C: e->op = F_B; e->imm = fast_sext((w & 0x7FFu) << 1, 12); return;
	default:
		return;
	}
}

/* ThumbExpandImm_C: returns the constant; *rot = 1 when the carry out is bit 31 of the result. */
static int fast_t32_expand(uint32_t i12, uint32_t *out, int *rot)
{
	uint32_t b = i12 & 0xFFu;

	*rot = 0;
	if ((i12 >> 10) == 0) {
		switch ((i12 >> 8) & 3u) {
		case 0: *out = b; return 0;
		case 1: if (!b) return -1; *out = b | b << 16; return 0;
		case 2: if (!b) return -1; *out = b << 8 | b << 24; return 0;
		default: if (!b) return -1; *out = b | b << 8 | b << 16 | b << 24; return 0;
		}
	}
	{
		uint32_t v = 0x80u | (i12 & 0x7Fu);
		unsigned r = (i12 >> 7) & 0x1Fu;
		*out = (v >> r) | (v << (32u - r));   /* r >= 8 here */
		*rot = 1;
		return 0;
	}
}

static void fast_t32(struct dent *e, uint32_t w)
{
	uint32_t hw1 = w >> 16, hw2 = w & 0xFFFFu;
	unsigned rn = hw1 & 0xFu, rt = (hw2 >> 12) & 0xFu;

	if ((hw1 & 0xF800u) == 0xF000u && (hw2 & 0x8000u)) {   /* branches and miscellaneous control */
		uint32_t s = (hw1 >> 10) & 1u, j1 = (hw2 >> 13) & 1u, j2 = (hw2 >> 11) & 1u;
		if ((hw2 & 0x5000u) == 0x5000u || (hw2 & 0x5000u) == 0x4000u) {   /* BL / BLX imm */
			uint32_t i1 = !(j1 ^ s), i2 = !(j2 ^ s);
			uint32_t off = (s << 24) | (i1 << 23) | (i2 << 22) | ((hw1 & 0x3FFu) << 12) | ((hw2 & 0x7FFu) << 1);
			if ((hw2 & 0x1000u) == 0) {
				if (hw2 & 1u)
					return;                         /* BLX imm with H = 1: UNDEFINED */
				e->op = F_BLX_I;
			} else {
				e->op = F_BL;
			}
			e->imm = fast_sext(off, 25);
			return;
		}
		if ((hw2 & 0x5000u) == 0x1000u) {               /* B.W (T4) */
			uint32_t i1 = !(j1 ^ s), i2 = !(j2 ^ s);
			uint32_t off = (s << 24) | (i1 << 23) | (i2 << 22) | ((hw1 & 0x3FFu) << 12) | ((hw2 & 0x7FFu) << 1);
			e->op = F_B; e->imm = fast_sext(off, 25);
			return;
		}
		if ((hw2 & 0x5000u) == 0x0000u && ((hw1 >> 6) & 0xFu) < 0xEu) {   /* B<c>.W (T3) */
			uint32_t off = (s << 20) | (j2 << 19) | (j1 << 18) | ((hw1 & 0x3Fu) << 12) | ((hw2 & 0x7FFu) << 1);
			e->op = F_BCOND; e->ra = (uint8_t)((hw1 >> 6) & 0xFu); e->imm = fast_sext(off, 21);
			return;
		}
		return;
	}
	if ((hw1 & 0xFA00u) == 0xF000u && !(hw2 & 0x8000u)) {  /* data processing, modified immediate */
		unsigned op = (hw1 >> 5) & 0xFu, sbit = (hw1 >> 4) & 1u, rd = (hw2 >> 8) & 0xFu;
		uint32_t i12 = ((hw1 >> 10) & 1u) << 11 | ((hw2 >> 12) & 7u) << 8 | (hw2 & 0xFFu);
		uint32_t imm;
		int rot;
		if (fast_t32_expand(i12, &imm, &rot) != 0)
			return;
		e->ra = (uint8_t)rd; e->rb = (uint8_t)rn; e->imm = imm;
		e->fl = (uint8_t)(sbit | (rot ? 2u : 0u));
		if (rd == 15u) {                                /* only the compare forms may name pc as Rd */
			if (!sbit)
				return;
			switch (op) {
			case 0x0: if (rn != 15u && rn != 13u) e->op = F_TSTW_I; return;
			case 0x8: if (rn != 15u) e->op = F_CMNW_I; return;
			case 0xD: if (rn != 15u) e->op = F_CMPW_I; return;
			default: return;
			}
		}
		if (rd == 13u && op != 0x8u && op != 0xDu)
			return;                                 /* SP as Rd: only ADD/SUB SP, SP, #imm */
		switch (op) {
		case 0x0: if (rn != 15u && rn != 13u) e->op = F_ANDW_I; return;
		case 0x1: if (rn != 15u && rn != 13u) e->op = F_BICW_I; return;
		case 0x2: if (rn == 15u) e->op = F_MOVW_I; else if (rn != 13u) e->op = F_ORRW_I; return;
		case 0x3: if (rn == 15u) e->op = F_MVNW_I; return;   /* ORN: class path */
		case 0x4: if (rn != 15u && rn != 13u) e->op = F_EORW_I; return;
		case 0x8: if (rn != 15u && (rd != 13u || rn == 13u)) e->op = F_ADDW_I; return;
		case 0xD: if (rn != 15u && (rd != 13u || rn == 13u)) e->op = F_SUBW_I; return;
		case 0xE: if (rn != 15u && rn != 13u) e->op = F_RSBW_I; return;
		default: return;
		}
	}
	if ((hw1 & 0xFB00u) == 0xF200u && !(hw2 & 0x8000u)) {  /* plain binary immediate */
		unsigned rd = (hw2 >> 8) & 0xFu;
		uint32_t i12 = ((hw1 >> 10) & 1u) << 11 | ((hw2 >> 12) & 7u) << 8 | (hw2 & 0xFFu);
		if (rd == 15u || (rd == 13u && rn != 13u))
			return;
		e->ra = (uint8_t)rd; e->rb = (uint8_t)rn;
		switch ((hw1 >> 4) & 0x1Fu) {
		case 0x00:                                      /* ADDW / ADR (add) */
			if (rn == 15u) { e->op = F_ADR; e->imm = i12; } else { e->op = F_ADD_I_NF; e->imm = i12; }
			return;
		case 0x0A:                                      /* SUBW / ADR (sub) */
			if (rn == 15u) { e->op = F_ADR; e->imm = 0u - i12; } else { e->op = F_ADD_I_NF; e->imm = 0u - i12; }
			return;
		case 0x04:                                      /* MOVW */
			if (rd == 13u)
				return;
			e->op = F_MOVW; e->imm = (rn << 12) | i12; return;
		default: return;
		}
	}
	if ((hw1 & 0xFF00u) == 0xF800u || (hw1 & 0xFF50u) == 0xF910u) {   /* load/store single */
		static const uint8_t ld12[8] = { F_STRB_RI, F_LDRB_RI, F_STRH_RI, F_LDRH_RI, F_STR_RI, F_LDR_RI, 0, 0 };
		unsigned sz_l = (hw1 >> 4) & 7u;                /* size<1:0> : L, bits 6-4 */
		int sign = (hw1 >> 8) & 1u;
		int is_load = sz_l & 1u;
		if (rt == 15u)
			return;                                 /* PLD/PLI or LDR pc: class path */
		e->ra = (uint8_t)rt; e->rb = (uint8_t)rn;
		if (rn == 15u) {                                /* literal: only plain LDR here */
			if (!sign && (hw1 & 0xFF7Fu) == 0xF85Fu) {
				e->op = F_LDR_LIT;
				e->imm = (hw1 & 0x80u) ? (hw2 & 0xFFFu) : 0u - (hw2 & 0xFFFu);
			}
			return;
		}
		if (hw1 & 0x80u) {                              /* imm12, offset form */
			if (sign) {
				if (sz_l == 1u) e->op = F_LDRSB_RI; else if (sz_l == 3u) e->op = F_LDRSH_RI; else return;
			} else {
				e->op = ld12[sz_l];
			}
			if (!e->op)
				return;
			if (!is_load && rt == 15u)
				return;
			e->imm = hw2 & 0xFFFu;
			return;
		}
		/* imm8: only the offset forms P=1 U=x W=0 (hw2 1 1 U 0 / 1 1 0 0 = negative offset) */
		if ((hw2 & 0x0F00u) == 0x0C00u && !sign) {
			e->op = ld12[sz_l];
			if (!e->op)
				return;
			e->imm = 0u - (hw2 & 0xFFu);
			return;
		}
		return;
	}
	if ((hw1 & 0xFE40u) == 0xE840u && (hw1 & 0x0120u) == 0x0100u) {   /* LDRD/STRD offset form P=1 W=0 */
		unsigned rt2 = (hw2 >> 8) & 0xFu;
		uint32_t off = (hw2 & 0xFFu) << 2;
		int load = (hw1 >> 4) & 1u;
		if (rn == 15u || rt == 15u || rt2 == 15u || rt == 13u || rt2 == 13u)
			return;
		if (load && rt == rt2)
			return;
		e->op = load ? F_LDRD_RI : F_STRD_RI;
		e->ra = (uint8_t)rt; e->rc = (uint8_t)rt2; e->rb = (uint8_t)rn;
		e->imm = (hw1 & 0x80u) ? off : 0u - off;
		return;
	}
	if (hw1 == 0xE92Du) {                                   /* PUSH.W = STMDB sp!, {list} */
		if ((hw2 & 0xA000u) || fast_popcount16(hw2) < 2u)
			return;
		e->op = F_PUSH; e->imm = hw2; return;
	}
	if (hw1 == 0xE8BDu) {                                   /* POP.W = LDMIA sp!, {list} */
		if ((hw2 & 0x2000u) || (hw2 & 0xC000u) == 0xC000u || fast_popcount16(hw2) < 2u)
			return;
		e->op = F_POP; e->imm = hw2; return;
	}
}

/* ---- execute (called with c->r[15] / c->next_pc already set by the core, never inside an IT block) ---- */

/* n2a32_cond() as straight-line code (the compiler does not inline the header's switch) */
static inline int fast_cond(const struct n2a32_cpu *c, unsigned cond)
{
	int r;
	switch (cond >> 1) {
	case 0: r = c->z; break;
	case 1: r = c->cf; break;
	case 2: r = c->n; break;
	case 3: r = c->v; break;
	case 4: r = c->cf & !c->z; break;
	case 5: r = c->n == c->v; break;
	case 6: r = (c->n == c->v) & !c->z; break;
	default: return 1;
	}
	return r ^ (int)(cond & 1u);
}

#define FR(x) c->r[(x)]
static inline void fast_flags_add(struct n2a32_cpu *c, uint32_t r) { n2a32_set_nz(c, r); }

static inline __attribute__((always_inline)) int fast_exec(struct n2a32_cpu *c, const struct dent *e)
{
	uint32_t a, b, r;
	uint8_t cf, v;

	switch (e->op) {
	case F_LSL_I:
		a = FR(e->rb); r = a << e->imm; c->cf = (uint8_t)((a >> (32u - e->imm)) & 1u);
		FR(e->ra) = r; n2a32_set_nz(c, r); return 1;
	case F_LSR_I:
		a = FR(e->rb); c->cf = (uint8_t)((a >> (e->imm - 1u)) & 1u); r = e->imm == 32u ? 0u : a >> e->imm;
		FR(e->ra) = r; n2a32_set_nz(c, r); return 1;
	case F_ASR_I:
		a = FR(e->rb); c->cf = (uint8_t)(((int32_t)a >> (e->imm - 1u)) & 1);
		r = (uint32_t)((int32_t)a >> (e->imm == 32u ? 31u : e->imm));
		FR(e->ra) = r; n2a32_set_nz(c, r); return 1;
	case F_MOVS_R: r = FR(e->rb); FR(e->ra) = r; n2a32_set_nz(c, r); return 1;
	case F_ADDS_R: r = n2a32_adc(FR(e->rb), FR(e->rc), 0, &cf, &v); goto setall;
	case F_SUBS_R: r = n2a32_adc(FR(e->rb), ~FR(e->rc), 1, &cf, &v); goto setall;
	case F_ADDS_I: r = n2a32_adc(FR(e->rb), e->imm, 0, &cf, &v); goto setall;
	case F_SUBS_I: r = n2a32_adc(FR(e->rb), ~e->imm, 1, &cf, &v); goto setall;
	case F_CMP_I:  r = n2a32_adc(FR(e->rb), ~e->imm, 1, &cf, &v); goto setflags;
	case F_MOVS_I: FR(e->ra) = e->imm; n2a32_set_nz(c, e->imm); return 1;
	case F_AND: r = FR(e->ra) & FR(e->rb); FR(e->ra) = r; n2a32_set_nz(c, r); return 1;
	case F_EOR: r = FR(e->ra) ^ FR(e->rb); FR(e->ra) = r; n2a32_set_nz(c, r); return 1;
	case F_ORR: r = FR(e->ra) | FR(e->rb); FR(e->ra) = r; n2a32_set_nz(c, r); return 1;
	case F_BIC: r = FR(e->ra) & ~FR(e->rb); FR(e->ra) = r; n2a32_set_nz(c, r); return 1;
	case F_MVN: r = ~FR(e->rb); FR(e->ra) = r; n2a32_set_nz(c, r); return 1;
	case F_MUL: r = FR(e->ra) * FR(e->rb); FR(e->ra) = r; n2a32_set_nz(c, r); return 1;
	case F_TST: n2a32_set_nz(c, FR(e->ra) & FR(e->rb)); return 1;
	case F_ADC: r = n2a32_adc(FR(e->ra), FR(e->rb), c->cf, &cf, &v); FR(e->ra) = r; goto setflags_nostore;
	case F_SBC: r = n2a32_adc(FR(e->ra), ~FR(e->rb), c->cf, &cf, &v); FR(e->ra) = r; goto setflags_nostore;
	case F_RSB0: r = n2a32_adc(~FR(e->rb), 0, 1, &cf, &v); FR(e->ra) = r; goto setflags_nostore;
	case F_CMP_R: r = n2a32_adc(FR(e->ra), ~FR(e->rb), 1, &cf, &v); goto setflags;
	case F_CMN: r = n2a32_adc(FR(e->ra), FR(e->rb), 0, &cf, &v); goto setflags;
	case F_CMP_HI: r = n2a32_adc(FR(e->rb), ~FR(e->rc), 1, &cf, &v); goto setflags;

	case F_ADD_R_NF: FR(e->ra) = FR(e->rb) + FR(e->rc); return 1;
	case F_MOV_R_NF: FR(e->ra) = FR(e->rb); return 1;
	case F_ADD_I_NF: FR(e->ra) = FR(e->rb) + e->imm; return 1;
	case F_ADR: FR(e->ra) = (c->r[15] & ~3u) + e->imm; return 1;
	case F_MOVW: FR(e->ra) = e->imm; return 1;
	case F_SXTH: FR(e->ra) = fast_sext(FR(e->rb) & 0xFFFFu, 16); return 1;
	case F_SXTB: FR(e->ra) = fast_sext(FR(e->rb) & 0xFFu, 8); return 1;
	case F_UXTH: FR(e->ra) = FR(e->rb) & 0xFFFFu; return 1;
	case F_UXTB: FR(e->ra) = FR(e->rb) & 0xFFu; return 1;

	case F_LDR_LIT: FR(e->ra) = n2a32_ld32(c, (c->r[15] & ~3u) + e->imm); return 1;
	case F_LDR_RR:  FR(e->ra) = n2a32_ld32(c, FR(e->rb) + FR(e->rc)); return 1;
	case F_LDRB_RR: FR(e->ra) = n2a32_ld8(c, FR(e->rb) + FR(e->rc)); return 1;
	case F_LDRH_RR: FR(e->ra) = n2a32_ld16(c, FR(e->rb) + FR(e->rc)); return 1;
	case F_LDRSB_RR: FR(e->ra) = fast_sext(n2a32_ld8(c, FR(e->rb) + FR(e->rc)), 8); return 1;
	case F_LDRSH_RR: FR(e->ra) = fast_sext(n2a32_ld16(c, FR(e->rb) + FR(e->rc)), 16); return 1;
	case F_STR_RR:  n2a32_st32(c, FR(e->rb) + FR(e->rc), FR(e->ra)); return 1;
	case F_STRB_RR: n2a32_st8(c, FR(e->rb) + FR(e->rc), (uint8_t)FR(e->ra)); return 1;
	case F_STRH_RR: n2a32_st16(c, FR(e->rb) + FR(e->rc), (uint16_t)FR(e->ra)); return 1;
	case F_LDR_RI:  FR(e->ra) = n2a32_ld32(c, FR(e->rb) + e->imm); return 1;
	case F_LDRB_RI: FR(e->ra) = n2a32_ld8(c, FR(e->rb) + e->imm); return 1;
	case F_LDRH_RI: FR(e->ra) = n2a32_ld16(c, FR(e->rb) + e->imm); return 1;
	case F_LDRSB_RI: FR(e->ra) = fast_sext(n2a32_ld8(c, FR(e->rb) + e->imm), 8); return 1;
	case F_LDRSH_RI: FR(e->ra) = fast_sext(n2a32_ld16(c, FR(e->rb) + e->imm), 16); return 1;
	case F_STR_RI:  n2a32_st32(c, FR(e->rb) + e->imm, FR(e->ra)); return 1;
	case F_STRB_RI: n2a32_st8(c, FR(e->rb) + e->imm, (uint8_t)FR(e->ra)); return 1;
	case F_STRH_RI: n2a32_st16(c, FR(e->rb) + e->imm, (uint16_t)FR(e->ra)); return 1;
	case F_LDRD_RI: {
		uint32_t addr = FR(e->rb) + e->imm;
		a = n2a32_ld32(c, addr); b = n2a32_ld32(c, addr + 4u);
		FR(e->ra) = a; FR(e->rc) = b; return 1;
	}
	case F_STRD_RI: {
		uint32_t addr = FR(e->rb) + e->imm;
		n2a32_st32(c, addr, FR(e->ra)); n2a32_st32(c, addr + 4u, FR(e->rc)); return 1;
	}
	case F_PUSH: {
		uint32_t mask = e->imm, addr = c->r[13] - 4u * fast_popcount16(mask), p = addr;
		while (mask) {
			unsigned i = (unsigned)__builtin_ctz(mask);
			n2a32_st32(c, p, c->r[i]);
			p += 4u;
			mask &= mask - 1u;
		}
		c->r[13] = addr;
		return 1;
	}
	case F_POP: {
		uint32_t mask = e->imm, p = c->r[13], tgt = 0;
		while (mask) {
			unsigned i = (unsigned)__builtin_ctz(mask);
			uint32_t val = n2a32_ld32(c, p);
			p += 4u;
			if (i == 15u) tgt = val; else c->r[i] = val;
			mask &= mask - 1u;
		}
		c->r[13] = p;
		if (e->imm & 0x8000u)
			n2a32_bx(c, tgt);
		return 1;
	}

	case F_B: c->next_pc = (c->r[15] + e->imm) & ~1u; return 1;
	case F_BCOND: if (fast_cond(c, e->ra)) c->next_pc = (c->r[15] + e->imm) & ~1u; return 1;
	case F_CBZ: if (FR(e->rb) == 0) c->next_pc = (c->r[15] + e->imm) & ~1u; return 1;
	case F_CBNZ: if (FR(e->rb) != 0) c->next_pc = (c->r[15] + e->imm) & ~1u; return 1;
	case F_BL: c->r[14] = c->next_pc | 1u; c->next_pc = (c->r[15] + e->imm) & ~1u; return 1;
	case F_BLX_I: c->r[14] = c->next_pc | 1u; c->thumb = 0; c->next_pc = ((c->r[15] & ~3u) + e->imm) & ~3u; return 1;
	case F_BX: n2a32_bx(c, FR(e->rb)); return 1;
	case F_BLX_R: a = FR(e->rb); c->r[14] = c->next_pc | 1u; n2a32_bx(c, a); return 1;

	case F_ADDW_I:
		if (e->fl & 1u) { r = n2a32_adc(FR(e->rb), e->imm, 0, &cf, &v); goto setall; }
		FR(e->ra) = FR(e->rb) + e->imm; return 1;
	case F_SUBW_I:
		if (e->fl & 1u) { r = n2a32_adc(FR(e->rb), ~e->imm, 1, &cf, &v); goto setall; }
		FR(e->ra) = FR(e->rb) - e->imm; return 1;
	case F_RSBW_I:
		if (e->fl & 1u) { r = n2a32_adc(~FR(e->rb), e->imm, 1, &cf, &v); goto setall; }
		FR(e->ra) = e->imm - FR(e->rb); return 1;
	case F_CMPW_I: r = n2a32_adc(FR(e->rb), ~e->imm, 1, &cf, &v); goto setflags;
	case F_CMNW_I: r = n2a32_adc(FR(e->rb), e->imm, 0, &cf, &v); goto setflags;
	case F_MOVW_I: r = e->imm; goto logic;
	case F_MVNW_I: r = ~e->imm; goto logic;
	case F_ANDW_I: r = FR(e->rb) & e->imm; goto logic;
	case F_ORRW_I: r = FR(e->rb) | e->imm; goto logic;
	case F_EORW_I: r = FR(e->rb) ^ e->imm; goto logic;
	case F_BICW_I: r = FR(e->rb) & ~e->imm; goto logic;
	case F_TSTW_I:
		r = FR(e->rb) & e->imm;
		n2a32_set_nz(c, r);
		if (e->fl & 2u) c->cf = (uint8_t)(e->imm >> 31);
		return 1;
	default:
		return 0;
	}
logic:
	FR(e->ra) = r;
	if (e->fl & 1u) {
		n2a32_set_nz(c, r);
		if (e->fl & 2u) c->cf = (uint8_t)(e->imm >> 31);
	}
	return 1;
setall:
	FR(e->ra) = r;
setflags_nostore:
setflags:
	fast_flags_add(c, r); c->cf = cf; c->v = v;
	return 1;
}
#undef FR

#endif
