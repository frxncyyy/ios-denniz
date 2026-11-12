/* SPDX-License-Identifier: MIT */
/*
 * n2elf_reloc.c -- the three relocation encodings found in the phone's 32-bit ARM vendor set, and the four ARM
 * relocation types the loader implements (gpu/hwcodec track V, package V03).
 *
 * Encodings:
 *   - plain DT_REL / DT_JMPREL: an array of Elf32_Rel {uint32 r_offset; uint32 r_info;} (no explicit addend;
 *     the addend is the word already stored at r_offset, ARM ELF ABI "REL" form);
 *   - Android packed ("APS2", DT_ANDROID_REL 0x6000000f / DT_ANDROID_RELSZ 0x60000010): a byte stream of SLEB128
 *     groups as used by bionic's linker (design notes: "Packed Relocations"); the stream is applied directly, no
 *     expanded Elf32_Rel array is materialised;
 *   - RELR bitmaps (DT_RELR 36 / DT_RELRSZ 35 / DT_RELRENT 37, and the older DT_ANDROID_RELR 0x6fffe000 /
 *     0x6fffe001 / 0x6fffe003): 32-bit words; an even word is a target address followed by 31-address bitmaps.
 *
 * Types (anything else is an error naming the type):
 *   R_ARM_RELATIVE 23  *where = bias + A
 *   R_ARM_GLOB_DAT 21  *where = S
 *   R_ARM_JUMP_SLOT 22 *where = S
 *   R_ARM_ABS32 2      *where = S + A
 *
 * The resolve callback (owned by n2elf.c) turns a symbol index into a guest address (object, shim, or the
 * unresolved host-call slot). This file never reads the symbol table itself.
 *
 * This file is deliberately free of ELF header dependencies: it takes host pointers to already-validated tables.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

/* Optional tracing: set by a host test (or a debugger) before n2elf_link. Not part of the frozen interface. */
void (*n2elf_reloc_trace)(uint32_t where, uint32_t type, uint32_t value);

struct n2elf_rctx {
	uint8_t *g0;					/* host address of guest address 0 */
	uint32_t bias;					/* load bias of this object */
	uint32_t vaddr_lo;				/* first link-time address that is mapped */
	uint32_t vaddr_hi;				/* one past the last mapped link-time address */
	int (*resolve)(void *opaque, uint32_t symidx, uint32_t *value, char err[160]);
	void *opaque;
	unsigned applied;
};

#define R_ARM_NONE		0u
#define R_ARM_ABS32		2u
#define R_ARM_GLOB_DAT		21u
#define R_ARM_JUMP_SLOT		22u
#define R_ARM_RELATIVE		23u

/* Android packed relocation group flags (bionic: RelocationGroupFlags). */
#define N2E_G_INFO		0x1u
#define N2E_G_OFFSET		0x2u
#define N2E_G_ADDEND		0x4u
#define N2E_G_HAS_ADDEND	0x8u
#define N2E_G_KNOWN		(N2E_G_INFO | N2E_G_OFFSET | N2E_G_ADDEND | N2E_G_HAS_ADDEND)

static void rerr(char err[160], const char *msg)
{
	if (err)
		snprintf(err, 160, "%s", msg);
}

static const char *rtype_name(uint32_t type)
{
	switch (type) {
	case 0: return "R_ARM_NONE";
	case 1: return "R_ARM_PC24";
	case 2: return "R_ARM_ABS32";
	case 3: return "R_ARM_REL32";
	case 4: return "R_ARM_LDR_PC_G0";
	case 5: return "R_ARM_ABS16";
	case 6: return "R_ARM_ABS12";
	case 7: return "R_ARM_THM_ABS5";
	case 8: return "R_ARM_ABS8";
	case 9: return "R_ARM_SBREL32";
	case 10: return "R_ARM_THM_CALL";
	case 11: return "R_ARM_THM_PC8";
	case 12: return "R_ARM_BREL_ADJ";
	case 13: return "R_ARM_TLS_DESC";
	case 14: return "R_ARM_THM_SWI8";
	case 15: return "R_ARM_XPC25";
	case 16: return "R_ARM_THM_XPC22";
	case 17: return "R_ARM_TLS_DTPMOD32";
	case 18: return "R_ARM_TLS_DTPOFF32";
	case 19: return "R_ARM_TLS_TPOFF32";
	case 20: return "R_ARM_COPY";
	case 21: return "R_ARM_GLOB_DAT";
	case 22: return "R_ARM_JUMP_SLOT";
	case 23: return "R_ARM_RELATIVE";
	case 24: return "R_ARM_GOTOFF32";
	case 25: return "R_ARM_BASE_PREL";
	case 26: return "R_ARM_GOT_BREL";
	case 27: return "R_ARM_PLT32";
	case 28: return "R_ARM_CALL";
	case 29: return "R_ARM_JUMP24";
	case 30: return "R_ARM_THM_JUMP24";
	case 31: return "R_ARM_BASE_ABS";
	case 32: return "R_ARM_ALU_PCREL_7_0";
	case 33: return "R_ARM_ALU_PCREL_15_8";
	case 34: return "R_ARM_ALU_PCREL_23_15";
	case 35: return "R_ARM_LDR_SBREL_11_0_NC";
	case 36: return "R_ARM_ALU_SBREL_19_12_NC";
	case 37: return "R_ARM_ALU_SBREL_27_20_CK";
	case 38: return "R_ARM_TARGET1";
	case 39: return "R_ARM_SBREL31";
	case 40: return "R_ARM_V4BX";
	case 41: return "R_ARM_TARGET2";
	case 42: return "R_ARM_PREL31";
	case 43: return "R_ARM_MOVW_ABS_NC";
	case 44: return "R_ARM_MOVT_ABS";
	case 45: return "R_ARM_MOVW_PREL_NC";
	case 46: return "R_ARM_MOVT_PREL";
	case 47: return "R_ARM_THM_MOVW_ABS_NC";
	case 48: return "R_ARM_THM_MOVT_ABS";
	case 49: return "R_ARM_THM_MOVW_PREL_NC";
	case 50: return "R_ARM_THM_MOVT_PREL";
	case 51: return "R_ARM_THM_JUMP19";
	case 52: return "R_ARM_THM_JUMP6";
	case 53: return "R_ARM_THM_ALU_PREL_11_0";
	case 54: return "R_ARM_THM_PC12";
	case 55: return "R_ARM_ABS32_NOI";
	case 56: return "R_ARM_REL32_NOI";
	case 57: return "R_ARM_ALU_PC_G0_NC";
	case 58: return "R_ARM_ALU_PC_G0";
	case 59: return "R_ARM_ALU_PC_G1_NC";
	case 60: return "R_ARM_ALU_PC_G1";
	case 61: return "R_ARM_ALU_PC_G2";
	case 62: return "R_ARM_LDR_PC_G1";
	case 63: return "R_ARM_LDR_PC_G2";
	case 64: return "R_ARM_LDRS_PC_G0";
	case 65: return "R_ARM_LDRS_PC_G1";
	case 66: return "R_ARM_LDRS_PC_G2";
	case 67: return "R_ARM_LDC_PC_G0";
	case 68: return "R_ARM_LDC_PC_G1";
	case 69: return "R_ARM_LDC_PC_G2";
	case 70: return "R_ARM_ALU_SB_G0_NC";
	case 71: return "R_ARM_ALU_SB_G0";
	case 72: return "R_ARM_ALU_SB_G1_NC";
	case 73: return "R_ARM_ALU_SB_G1";
	case 74: return "R_ARM_ALU_SB_G2";
	case 75: return "R_ARM_LDR_SB_G0";
	case 76: return "R_ARM_LDR_SB_G1";
	case 77: return "R_ARM_LDR_SB_G2";
	case 78: return "R_ARM_LDRS_SB_G0";
	case 79: return "R_ARM_LDRS_SB_G1";
	case 80: return "R_ARM_LDRS_SB_G2";
	case 81: return "R_ARM_LDC_SB_G0";
	case 82: return "R_ARM_LDC_SB_G1";
	case 83: return "R_ARM_LDC_SB_G2";
	case 84: return "R_ARM_MOVW_BREL_NC";
	case 85: return "R_ARM_MOVT_BREL";
	case 86: return "R_ARM_MOVW_BREL";
	case 87: return "R_ARM_THM_MOVW_BREL_NC";
	case 88: return "R_ARM_THM_MOVT_BREL";
	case 89: return "R_ARM_THM_MOVW_BREL";
	case 90: return "R_ARM_TLS_GOTDESC";
	case 91: return "R_ARM_TLS_CALL";
	case 92: return "R_ARM_TLS_DESCSEQ";
	case 93: return "R_ARM_THM_TLS_CALL";
	case 94: return "R_ARM_PLT32_ABS";
	case 95: return "R_ARM_GOT_ABS";
	case 96: return "R_ARM_GOT_PREL";
	case 97: return "R_ARM_GOT_BREL12";
	case 98: return "R_ARM_GOTOFF12";
	case 99: return "R_ARM_GOTRELAX";
	case 100: return "R_ARM_GNU_VTENTRY";
	case 101: return "R_ARM_GNU_VTINHERIT";
	case 102: return "R_ARM_THM_JUMP11";
	case 103: return "R_ARM_THM_JUMP8";
	case 104: return "R_ARM_TLS_GD32";
	case 105: return "R_ARM_TLS_LDM32";
	case 106: return "R_ARM_TLS_LDO32";
	case 107: return "R_ARM_TLS_IE32";
	case 108: return "R_ARM_TLS_LE32";
	case 109: return "R_ARM_TLS_LDO12";
	case 110: return "R_ARM_TLS_LE12";
	case 111: return "R_ARM_TLS_IE12GP";
	default: return NULL;
	}
}

static void type_err(char err[160], uint32_t type, uint32_t where)
{
	const char *n = rtype_name(type);

	if (n)
		snprintf(err, 160, "unsupported relocation %s (%" PRIu32 ") at 0x%" PRIx32, n, type, where);
	else
		snprintf(err, 160, "unsupported relocation type %" PRIu32 " at 0x%" PRIx32, type, where);
}

struct n2elf_rctx *n2elf_rctx_create(uint8_t *g0, uint32_t bias, uint32_t vaddr_lo, uint32_t vaddr_hi,
	int (*resolve)(void *, uint32_t, uint32_t *, char[160]), void *opaque)
{
	struct n2elf_rctx *ctx = (struct n2elf_rctx *)calloc(1, sizeof(*ctx));

	if (!ctx)
		return NULL;
	ctx->g0 = g0;
	ctx->bias = bias;
	ctx->vaddr_lo = vaddr_lo;
	ctx->vaddr_hi = vaddr_hi;
	ctx->resolve = resolve;
	ctx->opaque = opaque;
	return ctx;
}

void n2elf_rctx_destroy(struct n2elf_rctx *ctx)
{
	free(ctx);
}

unsigned n2elf_rctx_applied(const struct n2elf_rctx *ctx)
{
	return ctx->applied;
}

/* Apply one relocation. `have_explicit` and `explicit_addend` exist because the packed format can carry addends;
 * on AArch32 (Elf32_Rel) bionic ignores them, and so do we. */
static int apply_one(struct n2elf_rctx *ctx, uint32_t r_offset, uint32_t r_info,
	int have_explicit, int64_t explicit_addend, char err[160])
{
	uint32_t type = r_info & 0xffu;
	uint32_t symidx = r_info >> 8;
	uint32_t addend, value, sym = 0;
	uint8_t *where;

	(void)have_explicit;
	(void)explicit_addend;

	if (ctx->vaddr_hi < ctx->vaddr_lo || ctx->vaddr_hi - ctx->vaddr_lo < 4u ||
	    r_offset < ctx->vaddr_lo || r_offset > ctx->vaddr_hi - 4u) {
		snprintf(err, 160, "relocation target 0x%" PRIx32 " outside loaded range [0x%" PRIx32
			",0x%" PRIx32 ")", r_offset, ctx->vaddr_lo, ctx->vaddr_hi);
		return -1;
	}
	if (r_offset > 0xffffffffu - ctx->bias) {
		snprintf(err, 160, "relocation target 0x%" PRIx32 " overflows guest address space", r_offset);
		return -1;
	}
	where = ctx->g0 + ctx->bias + r_offset;
	memcpy(&addend, where, 4);

	switch (type) {
	case R_ARM_RELATIVE:
		value = ctx->bias + addend;
		break;
	case R_ARM_GLOB_DAT:
	case R_ARM_JUMP_SLOT:
		if (!ctx->resolve || ctx->resolve(ctx->opaque, symidx, &sym, err) != 0)
			return -1;
		value = sym;
		break;
	case R_ARM_ABS32:
		if (!ctx->resolve || ctx->resolve(ctx->opaque, symidx, &sym, err) != 0)
			return -1;
		value = sym + addend;
		break;
	default:
		type_err(err, type, ctx->bias + r_offset);
		return -1;
	}

	memcpy(where, &value, 4);
	ctx->applied++;
	if (n2elf_reloc_trace)
		n2elf_reloc_trace(ctx->bias + r_offset, type, value);
	return 0;
}

int n2elf_apply_plain_rel(struct n2elf_rctx *ctx, const uint8_t *table, uint32_t size, char err[160])
{
	uint32_t i;

	if ((size & 7u) != 0) {
		snprintf(err, 160, "DT_REL size %" PRIu32 " is not a multiple of 8", size);
		return -1;
	}
	for (i = 0; i + 8u <= size; i += 8u) {
		uint32_t r_offset, r_info;

		memcpy(&r_offset, table + i, 4);
		memcpy(&r_info, table + i + 4, 4);
		if (apply_one(ctx, r_offset, r_info, 0, 0, err) != 0)
			return -1;
	}
	return 0;
}

/* Bounded SLEB128. Returns 0 on success. */
static int sleb128(const uint8_t *p, uint32_t size, uint32_t *pos, int64_t *out, char err[160])
{
	uint64_t v = 0;
	unsigned shift = 0;
	uint8_t b;

	for (;;) {
		if (*pos >= size) {
			rerr(err, "Android packed relocations: truncated SLEB128");
			return -1;
		}
		if (shift >= 64) {
			rerr(err, "Android packed relocations: SLEB128 overflow");
			return -1;
		}
		b = p[(*pos)++];
		v |= (uint64_t)(b & 0x7fu) << shift;
		shift += 7;
		if ((b & 0x80u) == 0) {
			if (shift < 64 && (b & 0x40u))
				v |= ~(uint64_t)0 << shift;
			break;
		}
	}
	*out = (int64_t)v;
	return 0;
}

int n2elf_apply_android_rel(struct n2elf_rctx *ctx, const uint8_t *table, uint32_t size, char err[160])
{
	uint32_t pos = 4;
	int64_t count64 = 0, start64 = 0;
	uint64_t index = 0;
	int64_t r_offset = 0, r_info = 0, r_addend = 0;
	int64_t group_delta = 0;

	if (size < 4u || memcmp(table, "APS2", 4) != 0) {
		rerr(err, "DT_ANDROID_REL: missing APS2 magic");
		return -1;
	}
	if (sleb128(table, size, &pos, &count64, err) != 0 ||
	    sleb128(table, size, &pos, &start64, err) != 0)
		return -1;
	if (count64 < 0 || count64 > 0x7fffffffLL) {
		snprintf(err, 160, "DT_ANDROID_REL: bad relocation count %" PRId64, count64);
		return -1;
	}
	if (start64 < 0 || (uint64_t)start64 > ctx->vaddr_hi) {
		snprintf(err, 160, "DT_ANDROID_REL: relocation array offset 0x%" PRIx64 " outside image",
			(uint64_t)start64);
		return -1;
	}

	while (index < (uint64_t)count64) {
		uint32_t group_start = pos;
		int64_t group_size = 0, group_flags = 0;

		if (sleb128(table, size, &pos, &group_size, err) != 0 ||
		    sleb128(table, size, &pos, &group_flags, err) != 0)
			return -1;
		if (group_size < 0 || (uint64_t)group_size > (uint64_t)count64 - index) {
			snprintf(err, 160, "DT_ANDROID_REL: group size %" PRId64 " overruns count %" PRId64,
				group_size, count64);
			return -1;
		}
		if ((group_flags & ~(int64_t)N2E_G_KNOWN) != 0) {
			snprintf(err, 160, "DT_ANDROID_REL: unknown group flags 0x%" PRIx64,
				(uint64_t)group_flags);
			return -1;
		}
		if (group_flags & N2E_G_INFO) {
			if (sleb128(table, size, &pos, &r_info, err) != 0)
				return -1;
		}
		if (group_flags & N2E_G_OFFSET) {
			if (sleb128(table, size, &pos, &group_delta, err) != 0)
				return -1;
		}
		if ((group_flags & N2E_G_HAS_ADDEND) && (group_flags & N2E_G_ADDEND)) {
			if (sleb128(table, size, &pos, &r_addend, err) != 0)
				return -1;
		}
		for (int64_t k = 0; k < group_size; k++) {
			/* Per bionic: offset delta first, then r_info, then (only for RELA formats) the addend. */
			if (!(group_flags & N2E_G_OFFSET)) {
				if (sleb128(table, size, &pos, &group_delta, err) != 0)
					return -1;
			}
			r_offset += group_delta;
			if (!(group_flags & N2E_G_INFO)) {
				if (sleb128(table, size, &pos, &r_info, err) != 0)
					return -1;
			}
			if (r_info < 0 || (uint64_t)r_info > 0xffffffffu) {
				snprintf(err, 160, "DT_ANDROID_REL: r_info 0x%" PRIx64 " out of range",
					(uint64_t)r_info);
				return -1;
			}
			if ((group_flags & N2E_G_HAS_ADDEND) && !(group_flags & N2E_G_ADDEND)) {
				int64_t d;

				if (sleb128(table, size, &pos, &d, err) != 0)
					return -1;
				r_addend += d;
			}
			if (r_offset < 0 || (uint64_t)r_offset > 0xffffffffu) {
				snprintf(err, 160, "DT_ANDROID_REL: target offset 0x%" PRIx64 " out of range",
					(uint64_t)r_offset);
				return -1;
			}
			if (apply_one(ctx, (uint32_t)r_offset, (uint32_t)r_info,
				      (group_flags & N2E_G_HAS_ADDEND) != 0, r_addend, err) != 0)
				return -1;
			index++;
		}
		if (pos == group_start) {
			rerr(err, "DT_ANDROID_REL: group made no progress");
			return -1;
		}
	}
	return 0;
}

int n2elf_apply_relr(struct n2elf_rctx *ctx, const uint8_t *table, uint32_t size, uint32_t ent,
	char err[160])
{
	uint32_t i, next = 0;

	if (ent == 0)
		ent = 4;
	if (ent != 4) {
		snprintf(err, 160, "RELR entry size %" PRIu32 " unsupported for AArch32", ent);
		return -1;
	}
	if ((size % ent) != 0) {
		snprintf(err, 160, "RELR size %" PRIu32 " is not a multiple of %" PRIu32, size, ent);
		return -1;
	}
	for (i = 0; i < size; i += ent) {
		uint32_t w;

		memcpy(&w, table + i, 4);
		if ((w & 1u) == 0) {
			if (apply_one(ctx, w, R_ARM_RELATIVE, 0, 0, err) != 0)
				return -1;
			next = w + 4u;
		} else {
			uint32_t where = next;

			if (where > 0xffffffffu - 31u * 4u) {
				rerr(err, "RELR bitmap overflows the guest address space");
				return -1;
			}
			for (unsigned bit = 1; bit < 32; bit++) {
				if (w & (1u << bit)) {
					if (apply_one(ctx, where + (bit - 1u) * 4u, R_ARM_RELATIVE,
						      0, 0, err) != 0)
						return -1;
				}
			}
			next += 31u * 4u;
		}
	}
	return 0;
}
