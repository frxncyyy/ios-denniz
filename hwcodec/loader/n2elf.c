/* SPDX-License-Identifier: MIT */
/*
 * n2elf.c -- ELF32/ARM shared-object loader for the AArch32 interpreter (gpu/hwcodec track V, package V03).
 *
 * Implements the loader half of include/n2elf.h:
 *   - ET_DYN only, PT_LOAD segments copied into a page-aligned guest range obtained with n2a32_mem_alloc(),
 *     bss zeroed, page protections applied only after relocation;
 *   - PT_DYNAMIC parsed for NEEDED, SONAME, SYMTAB/STRTAB/SYMENT, GNU_HASH (and DT_HASH as a symbol-count
 *     source), REL/RELSZ, JMPREL/PLTRELSZ, ANDROID_REL/ANDROID_RELSZ, DT_RELR+DT_RELRSZ+DT_RELRENT and the
 *     Android DT_ANDROID_RELR family, INIT, INIT_ARRAY, PREINIT_ARRAY, FINI_ARRAY (stored; no fini API);
 *   - symbol lookup by GNU hash over the loaded objects in load order, then the shim tables (functions through
 *     n2a32_hostcall_register(), data through a guest allocation that is initialised once);
 *   - undefined functions bind to a per-name unresolved host-call slot and increment the unresolved count;
 *     undefined data imports are an error (they would silently read zero otherwise);
 *   - DT_PREINIT_ARRAY/DT_INIT/DT_INIT_ARRAY run in dependency order through n2a32_call().
 *
 * Not implemented (listed in dsh-state/workers/V03/STATUS.md and test_v03/STATUS.md): symbol versions
 * (VERSYM/VERNEED are ignored), RELA, COPY relocations, TLS, GNU IFUNC, GNU_RELRO protection, FINI execution
 * (no interface), object unloading.
 *
 * The private bridge to n2elf_reloc.c is declared near the top of this file; it is not part of the frozen
 * interface.
 */
#include "n2elf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------------------------
 * ELF32 definitions. Kept local because <elf.h> does not exist on iOS and only a small subset is needed.
 * --------------------------------------------------------------------------------------------------------- */
struct n2e_ehdr {
	uint8_t e_ident[16];
	uint16_t e_type, e_machine;
	uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
	uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
};
struct n2e_phdr {
	uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
};
struct n2e_sym {
	uint32_t st_name, st_value, st_size;
	uint8_t st_info, st_other;
	uint16_t st_shndx;
};
struct n2e_rel {
	uint32_t r_offset, r_info;
};
struct n2e_dyn {
	int32_t d_tag;
	uint32_t d_val;
};
_Static_assert(sizeof(struct n2e_ehdr) == 52, "Elf32_Ehdr layout");
_Static_assert(sizeof(struct n2e_phdr) == 32, "Elf32_Phdr layout");
_Static_assert(sizeof(struct n2e_sym) == 16, "Elf32_Sym layout");
_Static_assert(sizeof(struct n2e_rel) == 8, "Elf32_Rel layout");
_Static_assert(sizeof(struct n2e_dyn) == 8, "Elf32_Dyn layout");

#define N2E_ET_DYN		3u
#define N2E_EM_ARM		40u
#define N2E_PT_LOAD		1u
#define N2E_PT_DYNAMIC		2u
#define N2E_PF_X		1u
#define N2E_PF_W		2u
#define N2E_PF_R		4u
#define N2E_PAGE		4096u

#define N2E_SHN_UNDEF		0u
#define N2E_SHN_ABS		0xfff1u
#define N2E_SHN_COMMON		0xfff2u

#define N2E_STB_LOCAL		0u
#define N2E_STB_GLOBAL		1u
#define N2E_STB_WEAK		2u
#define N2E_STB_GNU_UNIQUE	10u

#define N2E_STT_NOTYPE		0u
#define N2E_STT_OBJECT		1u
#define N2E_STT_FUNC		2u
#define N2E_STT_SECTION		3u
#define N2E_STT_FILE		4u
#define N2E_STT_COMMON		5u
#define N2E_STT_TLS		6u
#define N2E_STT_GNU_IFUNC	10u

#define N2E_STV_DEFAULT		0u
#define N2E_STV_INTERNAL	1u
#define N2E_STV_HIDDEN		2u
#define N2E_STV_PROTECTED	3u

#define N2E_DT_NULL		0
#define N2E_DT_NEEDED		1
#define N2E_DT_PLTRELSZ		2
#define N2E_DT_HASH		4
#define N2E_DT_GNU_HASH		0x6ffffef5
#define N2E_DT_STRTAB		5
#define N2E_DT_SYMTAB		6
#define N2E_DT_RELA		7
#define N2E_DT_RELASZ		8
#define N2E_DT_STRSZ		10
#define N2E_DT_SYMENT		11
#define N2E_DT_INIT		12
#define N2E_DT_FINI		13
#define N2E_DT_SONAME		14
#define N2E_DT_REL		17
#define N2E_DT_RELSZ		18
#define N2E_DT_RELENT		19
#define N2E_DT_PLTREL		20
#define N2E_DT_JMPREL		23
#define N2E_DT_INIT_ARRAY	25
#define N2E_DT_FINI_ARRAY	26
#define N2E_DT_INIT_ARRAYSZ	27
#define N2E_DT_FINI_ARRAYSZ	28
#define N2E_DT_PREINIT_ARRAY	32
#define N2E_DT_PREINIT_ARRAYSZ	33
#define N2E_DT_RELRSZ		35
#define N2E_DT_RELR		36
#define N2E_DT_RELRENT		37
#define N2E_DT_ANDROID_REL	0x6000000f
#define N2E_DT_ANDROID_RELSZ	0x60000010
#define N2E_DT_ANDROID_RELR	0x6fffe000
#define N2E_DT_ANDROID_RELRSZ	0x6fffe001
#define N2E_DT_ANDROID_RELRENT	0x6fffe003

#define N2ELF_MAX_IMAGES 32
#define N2ELF_MAX_SHIM_TABLES 16
#define N2ELF_MAX_DATA_SYMS 128
#define N2ELF_MAX_UNRES 512
#define N2ELF_MAX_NEEDED 16
#define N2ELF_MAX_SEGS 16
#define N2ELF_NAME_MAX 128
#define N2ELF_PATH_MAX 256
#define N2ELF_SONAME_MAX 64
#define N2ELF_MAX_DYN_ENTRIES 1024
#define N2ELF_MAX_FILE (256u * 1024u * 1024u)

struct n2elf_seg {
	uint32_t vaddr, memsz, filesz, off, flags;
};

struct n2elf_image {
	struct n2elf_set *set;
	char soname[N2ELF_SONAME_MAX];
	char path[N2ELF_PATH_MAX];
	uint32_t base;			/* load bias: guest address of link-time address 0 */
	uint32_t map_lo;		/* first mapped link-time address (page aligned) */
	uint32_t span;			/* mapped length from map_lo */
	uint32_t addr_hi;		/* map_lo + span, exclusive */
	uint32_t entry;
	struct n2elf_seg seg[N2ELF_MAX_SEGS];
	int nseg;

	uint32_t symtab, strtab, strsz, syment, nsyms;
	int have_symtab;
	uint32_t gnu_hash, sysv_hash;
	int have_gnu_hash;

	uint32_t rel, relsz;
	uint32_t jmprel, pltrelsz;
	uint32_t android_rel, android_relsz;
	uint32_t relr, relrsz, relrent;

	uint32_t init, fini;
	uint32_t preinit_array, preinit_sz;
	uint32_t init_array, init_sz;
	uint32_t fini_array, fini_sz;

	char needed[N2ELF_MAX_NEEDED][N2ELF_SONAME_MAX];
	int nneeded;
	uint8_t linked, inited;
	uint8_t failed;		/* a link attempt failed after writing some relocations: no retry */
};

struct n2elf_dsym {
	char name[N2ELF_NAME_MAX];
	uint32_t addr, size;
};

struct n2elf_usym {
	char name[N2ELF_NAME_MAX];
	uint32_t addr;
};

struct n2elf_set {
	struct n2a32_mem *mem;
	struct n2elf_image *img[N2ELF_MAX_IMAGES];
	int nimg;
	const struct n2shim_entry *ftab[N2ELF_MAX_SHIM_TABLES];
	int nftab;
	const struct n2shim_data *dtab[N2ELF_MAX_SHIM_TABLES];
	int ndtab;
	struct n2elf_dsym dsym[N2ELF_MAX_DATA_SYMS];
	int ndsym;
	struct n2elf_usym unres[N2ELF_MAX_UNRES];
	int nunres;
};

/* Defined in n2elf_reloc.c; private bridge, not part of include/n2elf.h. */
struct n2elf_rctx;
struct n2elf_rctx *n2elf_rctx_create(uint8_t *g0, uint32_t bias, uint32_t vaddr_lo, uint32_t vaddr_hi,
	int (*resolve)(void *, uint32_t, uint32_t *, char *), void *opaque);
void n2elf_rctx_destroy(struct n2elf_rctx *ctx);
unsigned n2elf_rctx_applied(const struct n2elf_rctx *ctx);
int n2elf_apply_plain_rel(struct n2elf_rctx *ctx, const uint8_t *table, uint32_t size, char err[160]);
int n2elf_apply_android_rel(struct n2elf_rctx *ctx, const uint8_t *table, uint32_t size, char err[160]);
int n2elf_apply_relr(struct n2elf_rctx *ctx, const uint8_t *table, uint32_t size, uint32_t ent,
	char err[160]);

static void set_err(char err[160], const char *fmt, ...)
{
	va_list ap;

	if (!err)
		return;
	va_start(ap, fmt);
	vsnprintf(err, 160, fmt, ap);
	va_end(ap);
}

static uint32_t rd32(const uint8_t *p)
{
	uint32_t v;

	memcpy(&v, p, sizeof(v));
	return v;
}

/* strnlen() is not in C11 and is hidden by strict -std=c11 on some libcs; keep a local copy. */
static size_t n2e_strnlen(const char *s, size_t max)
{
	size_t n = 0;

	while (n < max && s[n] != '\0')
		n++;
	return n;
}

/* ------------------------------------------------------------------------------------------------------------
 * Guest-memory helpers.
 * --------------------------------------------------------------------------------------------------------- */

/* Host pointer for a link-time address range, validated against the object's PT_LOAD segments. */
static uint8_t *im_ptr(struct n2elf_image *im, uint32_t vaddr, uint32_t len, int need_write,
	const char *what, char err[160])
{
	int i;

	if (len == 0) {
		set_err(err, "%s: zero-length range", what);
		return NULL;
	}
	if ((uint64_t)vaddr + len > im->addr_hi || vaddr < im->map_lo) {
		set_err(err, "%s: range 0x%x+0x%x outside image (mapped [0x%x,0x%x))",
			what, vaddr, len, im->map_lo, im->addr_hi);
		return NULL;
	}
	for (i = 0; i < im->nseg; i++) {
		const struct n2elf_seg *g = &im->seg[i];
		uint64_t end = (uint64_t)g->vaddr + g->memsz;

		if ((uint64_t)vaddr >= g->vaddr && (uint64_t)vaddr + len <= end) {
			if (need_write && !(g->flags & N2E_PF_W)) {
				set_err(err, "%s: range 0x%x+0x%x is in a read-only segment", what, vaddr, len);
				return NULL;
			}
			return im->set->mem->base + im->base + vaddr;
		}
	}
	set_err(err, "%s: range 0x%x+0x%x is not inside a PT_LOAD segment", what, vaddr, len);
	return NULL;
}

static int sym_read(struct n2elf_image *im, uint32_t idx, struct n2e_sym *out, char err[160])
{
	uint64_t off;
	uint8_t *p;

	if (!im->have_symtab) {
		set_err(err, "%s: symbol index %u but the object has no symbol table", im->soname, idx);
		return -1;
	}
	if (idx >= im->nsyms) {
		set_err(err, "%s: symbol index %u beyond symbol table (%u)", im->soname, idx, im->nsyms);
		return -1;
	}
	off = (uint64_t)im->symtab + (uint64_t)idx * im->syment;
	if (off > 0xffffffffu) {
		set_err(err, "%s: symbol index %u overflows the address space", im->soname, idx);
		return -1;
	}
	p = im_ptr(im, (uint32_t)off, (uint32_t)sizeof(*out), 0, "symbol table entry", err);
	if (!p)
		return -1;
	memcpy(out, p, sizeof(*out));
	return 0;
}

/* Copy a symbol's name. Returns 0 on success. */
static int sym_name(struct n2elf_image *im, const struct n2e_sym *sym, char name[N2ELF_NAME_MAX],
	char err[160])
{
	uint32_t avail;
	uint8_t *p;
	size_t n;

	name[0] = '\0';
	if (sym->st_name == 0)
		return 0;
	if (!im->have_symtab || im->strsz == 0 || sym->st_name >= im->strsz) {
		set_err(err, "%s: symbol name offset 0x%x outside .dynstr (0x%x)", im->soname,
			sym->st_name, im->strsz);
		return -1;
	}
	avail = im->strsz - sym->st_name;
	p = im_ptr(im, im->strtab + sym->st_name, avail, 0, "symbol name", err);
	if (!p)
		return -1;
	n = n2e_strnlen((const char *)p, avail);
	if (n == avail) {
		set_err(err, "%s: symbol name at 0x%x is not NUL terminated", im->soname,
			im->strtab + sym->st_name);
		return -1;
	}
	if (n >= N2ELF_NAME_MAX) {
		set_err(err, "%s: symbol name longer than %d bytes", im->soname, N2ELF_NAME_MAX - 1);
		return -1;
	}
	memcpy(name, p, n + 1);
	return 0;
}

static int sym_is_export(const struct n2e_sym *sym)
{
	unsigned bind = sym->st_info >> 4;
	unsigned type = sym->st_info & 0xf;
	unsigned vis = sym->st_other & 3;

	if (sym->st_shndx == N2E_SHN_UNDEF)
		return 0;
	if (bind != N2E_STB_GLOBAL && bind != N2E_STB_WEAK && bind != N2E_STB_GNU_UNIQUE)
		return 0;
	if (vis == N2E_STV_HIDDEN || vis == N2E_STV_INTERNAL)
		return 0;
	if (type == N2E_STT_SECTION || type == N2E_STT_FILE)
		return 0;
	if (type == N2E_STT_TLS)
		return 0;
	return 1;
}

/* Guest address of a defined symbol. */
static int sym_addr(struct n2elf_image *im, const struct n2e_sym *sym, uint32_t *addr, char err[160])
{
	unsigned type = sym->st_info & 0xf;
	char name[N2ELF_NAME_MAX];

	if (sym->st_shndx == N2E_SHN_UNDEF) {
		set_err(err, "%s: symbol is undefined", im->soname);
		return -1;
	}
	if (sym->st_shndx == N2E_SHN_COMMON) {
		set_err(err, "%s: SHN_COMMON symbol is not supported", im->soname);
		return -1;
	}
	if (type == N2E_STT_TLS) {
		set_err(err, "%s: TLS symbol is not supported", im->soname);
		return -1;
	}
	if (type == N2E_STT_GNU_IFUNC) {
		set_err(err, "%s: GNU IFUNC symbol is not supported (resolver would have to run at link time)",
			im->soname);
		return -1;
	}
	if (sym->st_shndx == N2E_SHN_ABS) {
		*addr = sym->st_value;
		return 0;
	}
	if ((uint64_t)sym->st_value >= im->addr_hi || sym->st_value < im->map_lo) {
		if (sym_name(im, sym, name, err) != 0)
			return -1;
		set_err(err, "%s: symbol %s value 0x%x outside image (mapped [0x%x,0x%x))",
			im->soname, name, sym->st_value, im->map_lo, im->addr_hi);
		return -1;
	}
	*addr = im->base + sym->st_value;
	return 0;
}

static uint32_t gnu_hash_str(const char *name)
{
	uint32_t h = 5381;

	for (const unsigned char *p = (const unsigned char *)name; *p; p++)
		h = (h << 5) + h + *p;
	return h;
}

/* Read one validated 32-bit word from the image. */
static int im_u32(struct n2elf_image *im, uint32_t vaddr, uint32_t *v, const char *what, char err[160])
{
	uint8_t *p = im_ptr(im, vaddr, 4, 0, what, err);

	if (!p)
		return -1;
	*v = rd32(p);
	return 0;
}

/* Number of symbols: GNU hash chains cover symoffset..nsyms-1, so the largest chain index + 1 is nsyms. */
static int gnu_nsyms(struct n2elf_image *im, uint32_t *nsyms, char err[160])
{
	uint8_t *hdr, *buckets;
	uint32_t nbuckets, symoffset, bloom_size;
	uint32_t max = 0;
	uint64_t bloom_bytes, head_bytes, chains_off;

	hdr = im_ptr(im, im->gnu_hash, 16, 0, "GNU hash header", err);
	if (!hdr)
		return -1;
	nbuckets = rd32(hdr);
	symoffset = rd32(hdr + 4);
	bloom_size = rd32(hdr + 8);
	if (nbuckets == 0 || bloom_size == 0) {
		set_err(err, "%s: GNU hash with zero buckets or bloom words", im->soname);
		return -1;
	}
	bloom_bytes = (uint64_t)bloom_size * 4u;
	head_bytes = 16u + bloom_bytes + (uint64_t)nbuckets * 4u;
	if (head_bytes > 0xffffffffu) {
		set_err(err, "%s: GNU hash table too large", im->soname);
		return -1;
	}
	if (im_ptr(im, im->gnu_hash, (uint32_t)head_bytes, 0, "GNU hash table", err) == NULL)
		return -1;
	buckets = im_ptr(im, im->gnu_hash, (uint32_t)head_bytes, 0, "GNU hash buckets", err) +
		16 + bloom_bytes;
	chains_off = (uint64_t)im->gnu_hash + head_bytes;
	for (uint32_t b = 0; b < nbuckets; b++) {
		uint32_t idx = rd32(buckets + 4u * b);

		if (idx < symoffset)
			continue;
		for (;;) {
			uint64_t coff = chains_off + (uint64_t)(idx - symoffset) * 4u;
			uint32_t chain;

			if (coff + 4u > im->addr_hi || coff > 0xffffffffu) {
				set_err(err, "%s: GNU hash chain runs outside the image", im->soname);
				return -1;
			}
			if (im_u32(im, (uint32_t)coff, &chain, "GNU hash chain", err) != 0)
				return -1;
			if (idx + 1u > max)
				max = idx + 1u;
			if (chain & 1u)
				break;
			idx++;
			if (idx < symoffset) {
				set_err(err, "%s: GNU hash chain index wrapped", im->soname);
				return -1;
			}
		}
	}
	*nsyms = max > symoffset ? max : symoffset;
	return 0;
}

/* 1 = found, 0 = not defined here, -1 = malformed table. */
static int gnu_lookup(struct n2elf_image *im, const char *name, uint32_t *addr, char err[160])
{
	uint8_t *hdr, *bloom, *buckets;
	uint32_t nbuckets, symoffset, bloom_size, bloom_shift;
	uint64_t bloom_bytes, head_bytes, chains_off;
	uint32_t hash, word, mask, idx;

	if (!im->have_gnu_hash)
		return 0;
	hdr = im_ptr(im, im->gnu_hash, 16, 0, "GNU hash header", err);
	if (!hdr)
		return -1;
	nbuckets = rd32(hdr);
	symoffset = rd32(hdr + 4);
	bloom_size = rd32(hdr + 8);
	bloom_shift = rd32(hdr + 12);
	if (nbuckets == 0 || bloom_size == 0) {
		set_err(err, "%s: GNU hash with zero buckets or bloom words", im->soname);
		return -1;
	}
	if (bloom_shift >= 32) {
		set_err(err, "%s: GNU hash bloom shift %u is invalid", im->soname, bloom_shift);
		return -1;
	}
	bloom_bytes = (uint64_t)bloom_size * 4u;
	head_bytes = 16u + bloom_bytes + (uint64_t)nbuckets * 4u;
	if (head_bytes > 0xffffffffu) {
		set_err(err, "%s: GNU hash table too large", im->soname);
		return -1;
	}
	if (im_ptr(im, im->gnu_hash, (uint32_t)head_bytes, 0, "GNU hash table", err) == NULL)
		return -1;
	bloom = im_ptr(im, im->gnu_hash, (uint32_t)head_bytes, 0, "GNU hash bloom", err) + 16;
	buckets = bloom + bloom_bytes;
	chains_off = (uint64_t)im->gnu_hash + head_bytes;

	hash = gnu_hash_str(name);
	word = rd32(bloom + 4u * ((hash / 32u) % bloom_size));
	mask = (1u << (hash % 32u)) | (1u << ((hash >> bloom_shift) % 32u));
	if ((word & mask) != mask)
		return 0;
	idx = rd32(buckets + 4u * (hash % nbuckets));
	if (idx < symoffset)
		return 0;
	for (;;) {
		uint64_t coff = chains_off + (uint64_t)(idx - symoffset) * 4u;
		uint8_t *cp;
		uint32_t chain;
		struct n2e_sym sym;
		char sname[N2ELF_NAME_MAX];

		if (idx >= im->nsyms) {
			set_err(err, "%s: GNU hash chain index %u beyond symbol table (%u)",
				im->soname, idx, im->nsyms);
			return -1;
		}
		if (coff > 0xffffffffu) {
			set_err(err, "%s: GNU hash chain overflows", im->soname);
			return -1;
		}
		cp = im_ptr(im, (uint32_t)coff, 4, 0, "GNU hash chain", err);
		if (!cp)
			return -1;
		chain = rd32(cp);
		if ((chain | 1u) == (hash | 1u)) {
			if (sym_read(im, idx, &sym, err) != 0)
				return -1;
			if (sym_name(im, &sym, sname, err) != 0)
				return -1;
			if (strcmp(sname, name) == 0 && sym_is_export(&sym)) {
				if (sym_addr(im, &sym, addr, err) != 0)
					return -1;
				return 1;
			}
		}
		if (chain & 1u)
			break;
		idx++;
	}
	return 0;
}

static int linear_lookup(struct n2elf_image *im, const char *name, uint32_t *addr, char err[160])
{
	uint32_t i;

	if (!im->have_symtab)
		return 0;
	for (i = 0; i < im->nsyms; i++) {
		struct n2e_sym sym;
		char sname[N2ELF_NAME_MAX];

		if (sym_read(im, i, &sym, err) != 0)
			return -1;
		if (!sym_is_export(&sym))
			continue;
		if (sym_name(im, &sym, sname, err) != 0)
			return -1;
		if (strcmp(sname, name) == 0) {
			if (sym_addr(im, &sym, addr, err) != 0)
				return -1;
			return 1;
		}
	}
	return 0;
}

static int image_lookup(struct n2elf_image *im, const char *name, uint32_t *addr, char err[160])
{
	if (im->have_gnu_hash)
		return gnu_lookup(im, name, addr, err);
	return linear_lookup(im, name, addr, err);
}

/* ------------------------------------------------------------------------------------------------------------
 * Shim tables.
 * --------------------------------------------------------------------------------------------------------- */
static int find_func_shim(struct n2elf_set *s, const char *name, uint32_t *addr)
{
	int t, i;

	for (t = 0; t < s->nftab; t++) {
		for (i = 0; s->ftab[t][i].name != NULL; i++) {
			if (strcmp(s->ftab[t][i].name, name) == 0) {
				uint32_t a;

				if (s->ftab[t][i].fn == NULL)
					return -1;	/* malformed table: a named entry with no implementation */
				a = n2a32_hostcall_register(s->ftab[t][i].fn, name);

				if (a == 0)
					return -1;
				*addr = a;
				return 1;
			}
		}
	}
	return 0;
}

static int find_data_shim(struct n2elf_set *s, const char *name, uint32_t *addr, char err[160])
{
	int t, i, d;

	for (d = 0; d < s->ndsym; d++) {
		if (strcmp(s->dsym[d].name, name) == 0) {
			*addr = s->dsym[d].addr;
			return 1;
		}
	}
	for (t = 0; t < s->ndtab; t++) {
		for (i = 0; s->dtab[t][i].name != NULL; i++) {
			uint32_t a;

			if (strcmp(s->dtab[t][i].name, name) != 0)
				continue;
			if (strlen(name) >= N2ELF_NAME_MAX) {
				set_err(err, "data import name longer than %d bytes", N2ELF_NAME_MAX - 1);
				return -1;
			}
			if (s->ndsym >= N2ELF_MAX_DATA_SYMS) {
				set_err(err, "too many data imports (max %d)", N2ELF_MAX_DATA_SYMS);
				return -1;
			}
			a = n2a32_mem_alloc(s->mem, s->dtab[t][i].size ? s->dtab[t][i].size : 4u, 16);
			if (a == 0) {
				set_err(err, "guest memory exhausted allocating data import %s", name);
				return -1;
			}
			if (s->dtab[t][i].init && s->dtab[t][i].size)
				memcpy(s->mem->base + a, s->dtab[t][i].init, s->dtab[t][i].size);
			strcpy(s->dsym[s->ndsym].name, name);
			s->dsym[s->ndsym].addr = a;
			s->dsym[s->ndsym].size = s->dtab[t][i].size;
			s->ndsym++;
			*addr = a;
			return 1;
		}
	}
	return 0;
}

/* Fallback for an implementation of n2a32_hostcall_register() that refuses a NULL function pointer.
 * The call still stops the run, but with N2A32_STOP_REQUEST instead of N2A32_STOP_NOIMPORT; the slot
 * address is in stop_addr and its name is available through n2a32_hostcall_name(). */
static void n2elf_unresolved_cb(struct n2a32_cpu *c)
{
	c->stop = N2A32_STOP_NOIMPORT;
	c->stop_request = 1;
	/* Returning to the slot keeps stop_addr on the import so n2a32_hostcall_name(stop_addr) still names it. */
	if (c->r[15] >= N2A32_HOSTCALL_BASE)
		c->r[14] = c->r[15];
}

static int unresolved_slot(struct n2elf_set *s, const char *name, uint32_t *addr, char err[160])
{
	int i;

	for (i = 0; i < s->nunres; i++) {
		if (strcmp(s->unres[i].name, name) == 0) {
			*addr = s->unres[i].addr;
			return 0;
		}
	}
	if (strlen(name) >= N2ELF_NAME_MAX) {
		set_err(err, "import name longer than %d bytes", N2ELF_NAME_MAX - 1);
		return -1;
	}
	if (s->nunres >= N2ELF_MAX_UNRES) {
		set_err(err, "too many unresolved imports (max %d)", N2ELF_MAX_UNRES);
		return -1;
	}
	{
		/* A NULL implementation makes this a named but unimplemented host call: the core recognises it as
		 * N2A32_STOP_NOIMPORT and n2a32_hostcall_name(stop_addr) still yields the symbol name. */
		uint32_t a = n2a32_hostcall_register(NULL, name);

		if (a == 0)
			a = n2a32_hostcall_register(n2elf_unresolved_cb, name);
		if (a == 0) {
			set_err(err, "cannot register an unresolved host call for %s", name);
			return -1;
		}
		strcpy(s->unres[s->nunres].name, name);
		s->unres[s->nunres].addr = a;
		s->nunres++;
		*addr = a;
	}
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------
 * Link-time symbol resolution.
 * --------------------------------------------------------------------------------------------------------- */
struct linkctx {
	struct n2elf_set *s;
	struct n2elf_image *im;
};

static int resolve_cb(void *opaque, uint32_t symidx, uint32_t *value, char err[160])
{
	struct linkctx *lc = (struct linkctx *)opaque;
	struct n2elf_set *s = lc->s;
	struct n2elf_image *im = lc->im;
	struct n2e_sym sym;
	char name[N2ELF_NAME_MAX];
	int rc, j;

	if (sym_read(im, symidx, &sym, err) != 0)
		return -1;
	if (sym.st_shndx != N2E_SHN_UNDEF) {
		if (sym_addr(im, &sym, value, err) != 0)
			return -1;
		return 0;
	}
	if (symidx == 0) {
		*value = 0;
		return 0;
	}
	if (sym_name(im, &sym, name, err) != 0)
		return -1;
	if (name[0] == '\0') {
		set_err(err, "%s: undefined symbol %u has no name", im->soname, symidx);
		return -1;
	}
	/* Loaded objects, in load order (the importing object included). */
	for (j = 0; j < s->nimg; j++) {
		uint32_t v;

		rc = image_lookup(s->img[j], name, &v, err);
		if (rc < 0)
			return -1;
		if (rc > 0) {
			*value = v;
			return 0;
		}
	}
	rc = find_func_shim(s, name, value);
	if (rc < 0) {
		set_err(err, "%s: cannot register host call %s", im->soname, name);
		return -1;
	}
	if (rc > 0)
		return 0;
	rc = find_data_shim(s, name, value, err);
	if (rc != 0)
		return rc < 0 ? -1 : 0;

	{
		unsigned type = sym.st_info & 0xf;

		if (type == N2E_STT_OBJECT || type == N2E_STT_COMMON ||
		    (type == N2E_STT_NOTYPE && sym.st_size != 0)) {
			set_err(err, "%s: unresolved data import %s", im->soname, name);
			return -1;
		}
	}
	return unresolved_slot(s, name, value, err);
}

/* A page is writable after relocation if any PF_W PT_LOAD page-range covers it. */
static int page_is_writable(const struct n2elf_image *im, uint32_t page)
{
	int i;

	for (i = 0; i < im->nseg; i++) {
		const struct n2elf_seg *g = &im->seg[i];
		uint64_t a = g->vaddr & ~(uint64_t)(N2E_PAGE - 1);
		uint64_t b = ((uint64_t)g->vaddr + g->memsz + N2E_PAGE - 1) & ~(uint64_t)(N2E_PAGE - 1);

		if ((g->flags & N2E_PF_W) && page >= a && page < b)
			return 1;
	}
	return 0;
}

static int protect_image(struct n2elf_image *im, char err[160])
{
	uint32_t page, hi = im->addr_hi;
	struct n2a32_mem *mem = im->set->mem;

	for (page = im->map_lo; page < hi; ) {
		int writable = page_is_writable(im, page);
		uint32_t end = page + N2E_PAGE;

		while (end < hi && page_is_writable(im, end) == writable)
			end += N2E_PAGE;
		if (end > hi)
			end = hi;
		if (n2a32_mem_protect(mem, im->base + page, end - page, writable) != 0) {
			set_err(err, "%s: cannot set protection on guest 0x%x+0x%x",
				im->soname, im->base + page, end - page);
			return -1;
		}
		page = end;
	}
	return 0;
}

/* Load all objects and register all shim tables first: a second call only relocates images loaded since the previous
 * one; it never rebinds an import that was already written as an unresolved slot (real loaders bind once too). */
int n2elf_link(struct n2elf_set *s, unsigned *unresolved, char err[160])
{
	int i;

	if (!s) {
		set_err(err, "n2elf_link: no set");
		return -1;
	}
	for (i = 0; i < s->nimg; i++) {
		struct n2elf_image *im = s->img[i];
		struct linkctx lc;
		struct n2elf_rctx *ctx;

		if (im->failed) {
			set_err(err, "%s: a previous link attempt failed after partially applying relocations; "
				"the image cannot be retried", im->soname);
			return -1;
		}
		if (im->linked)
			continue;
		lc.s = s;
		lc.im = im;
		ctx = n2elf_rctx_create(s->mem->base, im->base, im->map_lo, im->addr_hi, resolve_cb, &lc);
		if (!ctx) {
			set_err(err, "%s: out of host memory for the relocation context", im->soname);
			im->failed = 1;
			return -1;
		}
		if (im->relsz) {
			uint8_t *p = im_ptr(im, im->rel, im->relsz, 0, "DT_REL", err);

			if (!p || n2elf_apply_plain_rel(ctx, p, im->relsz, err) != 0) {
				n2elf_rctx_destroy(ctx);
				im->failed = 1;
				return -1;
			}
		}
		if (im->android_relsz) {
			uint8_t *p = im_ptr(im, im->android_rel, im->android_relsz, 0, "DT_ANDROID_REL", err);

			if (!p || n2elf_apply_android_rel(ctx, p, im->android_relsz, err) != 0) {
				n2elf_rctx_destroy(ctx);
				im->failed = 1;
				return -1;
			}
		}
		if (im->relrsz) {
			uint8_t *p = im_ptr(im, im->relr, im->relrsz, 0, "RELR table", err);

			if (!p || n2elf_apply_relr(ctx, p, im->relrsz, im->relrent, err) != 0) {
				n2elf_rctx_destroy(ctx);
				im->failed = 1;
				return -1;
			}
		}
		if (im->pltrelsz) {
			uint8_t *p = im_ptr(im, im->jmprel, im->pltrelsz, 0, "DT_JMPREL", err);

			if (!p || n2elf_apply_plain_rel(ctx, p, im->pltrelsz, err) != 0) {
				n2elf_rctx_destroy(ctx);
				im->failed = 1;
				return -1;
			}
		}
		(void)n2elf_rctx_applied(ctx);
		n2elf_rctx_destroy(ctx);
		if (protect_image(im, err) != 0) {
			im->failed = 1;
			return -1;
		}
		im->linked = 1;
	}
	if (unresolved)
		*unresolved = (unsigned)s->nunres;
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------
 * Init arrays.
 * --------------------------------------------------------------------------------------------------------- */
static int find_soname(struct n2elf_set *s, const char *name)
{
	int i;

	for (i = 0; i < s->nimg; i++) {
		if (strcmp(s->img[i]->soname, name) == 0)
			return i;
	}
	return -1;
}

static void init_visit(struct n2elf_set *s, int idx, int *state, int *order, int *n)
{
	struct n2elf_image *im = s->img[idx];

	if (state[idx] != 0)
		return;		/* already ordered, or a dependency cycle broken at load order */
	state[idx] = 1;
	for (int k = 0; k < im->nneeded; k++) {
		int j = find_soname(s, im->needed[k]);

		if (j >= 0)
			init_visit(s, j, state, order, n);
	}
	state[idx] = 2;
	order[(*n)++] = idx;
}

static int run_one(struct n2a32_cpu *c, uint32_t fn)
{
	if (fn == 0 || fn == 1u || fn == 0xffffffffu)
		return 0;
	c->stop = N2A32_STOP_NONE;
	(void)n2a32_call(c, fn, NULL, 0, NULL);
	return c->stop == N2A32_STOP_RETURN ? 0 : -1;
}

static int run_array(struct n2a32_cpu *c, struct n2elf_image *im, uint32_t vaddr, uint32_t size,
	char err[160])
{
	uint32_t off;

	for (off = 0; off + 4u <= size; off += 4u) {
		uint8_t *p = im_ptr(im, vaddr + off, 4, 0, "init array entry", err);
		uint32_t fn;

		if (!p)
			return -1;
		memcpy(&fn, p, 4);
		if (run_one(c, fn) != 0) {
			set_err(err, "%s: init function 0x%x stopped (reason %d)", im->soname, fn,
				(int)c->stop);
			return -1;
		}
	}
	return 0;
}

int n2elf_run_init(struct n2elf_set *s, struct n2a32_cpu *c)
{
	int state[N2ELF_MAX_IMAGES];
	int order[N2ELF_MAX_IMAGES];
	int n = 0;
	char err[160];

	if (!s || !c || c->mem != s->mem)
		return -1;
	memset(state, 0, sizeof(state));
	for (int i = 0; i < s->nimg; i++)
		init_visit(s, i, state, order, &n);
	for (int i = 0; i < n; i++) {
		struct n2elf_image *im = s->img[order[i]];

		if (im->inited)
			continue;
		if (im->preinit_sz && run_array(c, im, im->preinit_array, im->preinit_sz, err) != 0)
			return -1;
		if (im->init && run_one(c, im->base + im->init) != 0) {
			set_err(err, "%s: DT_INIT 0x%x stopped (reason %d)", im->soname,
				im->base + im->init, (int)c->stop);
			return -1;
		}
		if (im->init_sz && run_array(c, im, im->init_array, im->init_sz, err) != 0)
			return -1;
		im->inited = 1;
	}
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------
 * Public accessors.
 * --------------------------------------------------------------------------------------------------------- */
uint32_t n2elf_entry(const struct n2elf_image *im)
{
	return im && im->entry ? im->base + im->entry : 0;
}

uint32_t n2elf_base(const struct n2elf_image *im)
{
	return im ? im->base : 0;
}

/* Private, for the host tests: mapped span and link-time address range. */
uint32_t n2elf__span(const struct n2elf_image *im)
{
	return im ? im->span : 0;
}

uint32_t n2elf__map_lo(const struct n2elf_image *im)
{
	return im ? im->map_lo : 0;
}

const char *n2elf__soname(const struct n2elf_image *im)
{
	return im ? im->soname : "";
}

uint32_t n2elf_sym(struct n2elf_set *s, const char *name)
{
	uint32_t addr = 0;
	char err[160];

	if (!s || !name)
		return 0;
	for (int i = 0; i < s->nimg; i++) {
		int rc = image_lookup(s->img[i], name, &addr, err);

		if (rc < 0)
			return 0;
		if (rc > 0)
			return addr;
	}
	{
		int rc = find_func_shim(s, name, &addr);

		if (rc > 0)
			return addr;
	}
	{
		int rc = find_data_shim(s, name, &addr, err);

		if (rc > 0)
			return addr;
	}
	return 0;
}

const char *n2elf_describe(struct n2elf_set *s, uint32_t addr, char *buf, size_t len)
{
	uint32_t a = addr & ~1u;
	char name[N2ELF_NAME_MAX];

	if (!buf || len == 0)
		return buf;
	buf[0] = '\0';
	if (!s) {
		snprintf(buf, len, "0x%08x", addr);
		return buf;
	}
	for (int i = 0; i < s->nimg; i++) {
		struct n2elf_image *im = s->img[i];
		uint64_t lo = (uint64_t)im->base + im->map_lo;
		uint64_t hi = (uint64_t)im->base + im->addr_hi;
		uint32_t off, best_v = 0;
		int have = 0;

		if (a < lo || (uint64_t)a >= hi)
			continue;
		off = a - im->base;
		for (uint32_t k = 0; k < im->nsyms; k++) {
			struct n2e_sym sym;
			char err[160];

			if (sym_read(im, k, &sym, err) != 0)
				break;
			if (sym.st_shndx == N2E_SHN_UNDEF ||
			    (sym.st_info & 0xf) == N2E_STT_SECTION ||
			    (sym.st_info & 0xf) == N2E_STT_FILE)
				continue;
			if (sym_name(im, &sym, name, err) != 0 || name[0] == '\0')
				continue;
			if (sym.st_value <= off && (!have || sym.st_value > best_v)) {
				best_v = sym.st_value;
				have = 1;
				snprintf(buf, len, "%s:%s+0x%x", im->soname, name, off - sym.st_value);
			}
		}
		if (!have)
			snprintf(buf, len, "%s+0x%x", im->soname, off);
		return buf;
	}
	{
		const char *hn = n2a32_hostcall_name(addr);

		if (hn)
			snprintf(buf, len, "hostcall:%s", hn);
		else
			snprintf(buf, len, "0x%08x", addr);
	}
	return buf;
}

/* ------------------------------------------------------------------------------------------------------------
 * Loading.
 * --------------------------------------------------------------------------------------------------------- */
static struct n2elf_image *load_fail(struct n2elf_image *im, char err[160], const char *msg)
{
	set_err(err, "%s", msg);
	if (im)
		free(im);
	return NULL;
}

struct n2elf_image *n2elf_load(struct n2elf_set *s, const char *host_path, const char *soname,
	char err[160])
{
	FILE *f = NULL;
	uint8_t *file = NULL;
	long fsz;
	size_t got;
	struct n2e_ehdr eh;
	struct n2e_phdr dynseg;
	struct n2e_phdr *dynph = NULL;
	struct n2elf_image *im = NULL;
	uint64_t lo_page = ~(uint64_t)0, hi_page = 0;
	uint32_t needed_v[N2ELF_MAX_NEEDED];
	int nneeded_v = 0;
	uint32_t alloc, base;
	int have_rela = 0, have_android_relr = 0;
	int have_dt_relr = 0;

	if (!s || !host_path) {
		set_err(err, "n2elf_load: NULL set or path");
		return NULL;
	}
	if (s->nimg >= N2ELF_MAX_IMAGES) {
		set_err(err, "n2elf_load: too many objects (max %d)", N2ELF_MAX_IMAGES);
		return NULL;
	}
	f = fopen(host_path, "rb");
	if (!f) {
		set_err(err, "%s: cannot open", host_path);
		return NULL;
	}
	if (fseek(f, 0, SEEK_END) != 0 || (fsz = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		set_err(err, "%s: cannot determine size", host_path);
		return NULL;
	}
	if ((unsigned long)fsz > N2ELF_MAX_FILE) {
		fclose(f);
		set_err(err, "%s: file larger than %u bytes", host_path, N2ELF_MAX_FILE);
		return NULL;
	}
	file = (uint8_t *)malloc((size_t)fsz ? (size_t)fsz : 1u);
	if (!file) {
		fclose(f);
		set_err(err, "%s: out of host memory", host_path);
		return NULL;
	}
	got = fread(file, 1, (size_t)fsz, f);
	fclose(f);
	if (got != (size_t)fsz) {
		free(file);
		set_err(err, "%s: short read", host_path);
		return NULL;
	}
	if ((size_t)fsz < sizeof(eh)) {
		free(file);
		set_err(err, "%s: shorter than an ELF header", host_path);
		return NULL;
	}
	memcpy(&eh, file, sizeof(eh));
	if (memcmp(eh.e_ident, "\177ELF", 4) != 0 || eh.e_ident[4] != 1 || eh.e_ident[5] != 1 ||
	    eh.e_ident[6] != 1) {
		free(file);
		set_err(err, "%s: not a 32-bit little-endian ELF", host_path);
		return NULL;
	}
	if (eh.e_type != N2E_ET_DYN) {
		free(file);
		set_err(err, "%s: ELF type %u is not ET_DYN", host_path, eh.e_type);
		return NULL;
	}
	if (eh.e_machine != N2E_EM_ARM) {
		free(file);
		set_err(err, "%s: machine %u is not EM_ARM", host_path, eh.e_machine);
		return NULL;
	}
	if (eh.e_phentsize != sizeof(struct n2e_phdr) || eh.e_phnum == 0 ||
	    eh.e_phnum > 64) {
		free(file);
		set_err(err, "%s: unsupported program header table (%u x %u)", host_path,
			eh.e_phnum, eh.e_phentsize);
		return NULL;
	}
	if ((uint64_t)eh.e_phoff + (uint64_t)eh.e_phnum * sizeof(struct n2e_phdr) > (uint64_t)fsz) {
		free(file);
		set_err(err, "%s: program headers outside the file", host_path);
		return NULL;
	}
	im = (struct n2elf_image *)calloc(1, sizeof(*im));
	if (!im) {
		free(file);
		set_err(err, "%s: out of host memory", host_path);
		return NULL;
	}
	im->set = s;
	im->entry = eh.e_entry;
	memcpy(im->path, host_path, strlen(host_path) < N2ELF_PATH_MAX ? strlen(host_path) + 1 :
		N2ELF_PATH_MAX - 1);
	im->path[N2ELF_PATH_MAX - 1] = '\0';
	if (soname && soname[0]) {
		if (strlen(soname) >= N2ELF_SONAME_MAX) {
			free(file);
			return load_fail(im, err, "soname too long");
		}
		strcpy(im->soname, soname);
	} else {
		strcpy(im->soname, host_path);
	}

	/* Program headers: PT_LOAD layout and PT_DYNAMIC. */
	for (unsigned i = 0; i < eh.e_phnum; i++) {
		struct n2e_phdr p;

		memcpy(&p, file + eh.e_phoff + (size_t)i * sizeof(p), sizeof(p));
		if (p.p_type == N2E_PT_DYNAMIC) {
			if (dynph) {
				free(file);
				return load_fail(im, err, "more than one PT_DYNAMIC segment");
			}
			dynseg = p;
			dynph = &dynseg;
		}
		if (p.p_type != N2E_PT_LOAD)
			continue;
		if (p.p_filesz > p.p_memsz) {
			free(file);
			return load_fail(im, err, "PT_LOAD filesz larger than memsz");
		}
		if ((uint64_t)p.p_offset + p.p_filesz > (uint64_t)fsz) {
			free(file);
			return load_fail(im, err, "PT_LOAD file range outside the file");
		}
		if ((p.p_vaddr & (N2E_PAGE - 1)) != (p.p_offset & (N2E_PAGE - 1))) {
			free(file);
			return load_fail(im, err, "PT_LOAD vaddr and offset are not page congruent");
		}
		if (im->nseg >= N2ELF_MAX_SEGS) {
			free(file);
			return load_fail(im, err, "too many PT_LOAD segments");
		}
		im->seg[im->nseg].vaddr = p.p_vaddr;
		im->seg[im->nseg].memsz = p.p_memsz;
		im->seg[im->nseg].filesz = p.p_filesz;
		im->seg[im->nseg].off = p.p_offset;
		im->seg[im->nseg].flags = p.p_flags;
		im->nseg++;
		{
			uint64_t a = (uint64_t)p.p_vaddr & ~(uint64_t)(N2E_PAGE - 1);
			uint64_t b = ((uint64_t)p.p_vaddr + p.p_memsz + N2E_PAGE - 1) &
				~(uint64_t)(N2E_PAGE - 1);

			if (a < lo_page)
				lo_page = a;
			if (b > hi_page)
				hi_page = b;
		}
	}
	if (im->nseg == 0) {
		free(file);
		return load_fail(im, err, "no PT_LOAD segments");
	}
	if (hi_page >= 0x100000000ull || hi_page <= lo_page) {
		free(file);
		return load_fail(im, err, "PT_LOAD span outside the 32-bit address space");
	}
	im->map_lo = (uint32_t)lo_page;
	im->span = (uint32_t)(hi_page - lo_page);
	im->addr_hi = (uint32_t)hi_page;
	alloc = n2a32_mem_alloc(s->mem, im->span, N2E_PAGE);
	if (alloc == 0) {
		free(file);
		return load_fail(im, err, "guest memory exhausted");
	}
	if ((uint64_t)alloc < lo_page) {
		free(file);
		return load_fail(im, err, "guest allocation below the image's link-time offset");
	}
	base = alloc - (uint32_t)lo_page;
	if ((uint64_t)base + im->addr_hi > 0x100000000ull) {
		free(file);
		return load_fail(im, err, "load bias places the image beyond 4 GiB");
	}
	im->base = base;

	/* Copy segments and zero the bss tail explicitly (pages may be shared with another segment). */
	for (int i = 0; i < im->nseg; i++) {
		struct n2elf_seg *g = &im->seg[i];

		if (g->filesz)
			memcpy(s->mem->base + im->base + g->vaddr, file + g->off, g->filesz);
		if (g->memsz > g->filesz)
			memset(s->mem->base + im->base + g->vaddr + g->filesz, 0,
				g->memsz - g->filesz);
	}

	/* Dynamic table. */
	if (dynph) {
		if ((dynph->p_filesz % sizeof(struct n2e_dyn)) != 0) {
			free(file);
			return load_fail(im, err, "PT_DYNAMIC size is not a multiple of 8");
		}
		if ((uint64_t)dynph->p_offset + dynph->p_filesz > (uint64_t)fsz) {
			free(file);
			return load_fail(im, err, "PT_DYNAMIC outside the file");
		}
		{
			uint32_t n = dynph->p_filesz / sizeof(struct n2e_dyn);
			uint32_t soname_v = 0;

			if (n > N2ELF_MAX_DYN_ENTRIES) {
				free(file);
				return load_fail(im, err, "too many dynamic entries");
			}
			for (uint32_t k = 0; k < n; k++) {
				struct n2e_dyn d;

				memcpy(&d, file + dynph->p_offset + (size_t)k * sizeof(d), sizeof(d));
				if (d.d_tag == N2E_DT_NULL)
					break;
				switch (d.d_tag) {
				case N2E_DT_NEEDED:
					if (nneeded_v >= N2ELF_MAX_NEEDED) {
						free(file);
						return load_fail(im, err, "too many DT_NEEDED entries");
					}
					needed_v[nneeded_v++] = d.d_val;
					break;
				case N2E_DT_SONAME:
					soname_v = d.d_val;
					break;
				case N2E_DT_SYMTAB: im->symtab = d.d_val; break;
				case N2E_DT_STRTAB: im->strtab = d.d_val; break;
				case N2E_DT_STRSZ: im->strsz = d.d_val; break;
				case N2E_DT_SYMENT: im->syment = d.d_val; break;
				case N2E_DT_HASH: im->sysv_hash = d.d_val; break;
				case N2E_DT_GNU_HASH: im->gnu_hash = d.d_val; im->have_gnu_hash = 1; break;
				case N2E_DT_REL: im->rel = d.d_val; break;
				case N2E_DT_RELSZ: im->relsz = d.d_val; break;
				case N2E_DT_RELENT:
					if (d.d_val != 0 && d.d_val != 8) {
						free(file);
						return load_fail(im, err, "unsupported DT_RELENT");
					}
					break;
				case N2E_DT_JMPREL: im->jmprel = d.d_val; break;
				case N2E_DT_PLTRELSZ: im->pltrelsz = d.d_val; break;
				case N2E_DT_PLTREL:
					if (d.d_val != N2E_DT_REL) {
						free(file);
						return load_fail(im, err, "DT_PLTREL is not DT_REL");
					}
					break;
				case N2E_DT_RELA:
				case N2E_DT_RELASZ:
					have_rela = 1;
					break;
				case N2E_DT_ANDROID_REL: im->android_rel = d.d_val; break;
				case N2E_DT_ANDROID_RELSZ: im->android_relsz = d.d_val; break;
				case N2E_DT_RELR: im->relr = d.d_val; have_dt_relr = 1; break;
				case N2E_DT_RELRSZ: im->relrsz = d.d_val; break;
				case N2E_DT_RELRENT: im->relrent = d.d_val; break;
				case N2E_DT_ANDROID_RELR:
					im->relr = d.d_val;
					have_android_relr = 1;
					break;
				case N2E_DT_ANDROID_RELRSZ: im->relrsz = d.d_val; break;
				case N2E_DT_ANDROID_RELRENT: im->relrent = d.d_val; break;
				case N2E_DT_INIT: im->init = d.d_val; break;
				case N2E_DT_FINI: im->fini = d.d_val; break;
				case N2E_DT_INIT_ARRAY: im->init_array = d.d_val; break;
				case N2E_DT_INIT_ARRAYSZ: im->init_sz = d.d_val; break;
				case N2E_DT_FINI_ARRAY: im->fini_array = d.d_val; break;
				case N2E_DT_FINI_ARRAYSZ: im->fini_sz = d.d_val; break;
				case N2E_DT_PREINIT_ARRAY: im->preinit_array = d.d_val; break;
				case N2E_DT_PREINIT_ARRAYSZ: im->preinit_sz = d.d_val; break;
				default:
					break;
				}
			}
			if (soname_v && (!soname || !soname[0])) {
				char tmp[N2ELF_SONAME_MAX];
				uint32_t avail;
				uint8_t *p;
				size_t len;

				if (!im->strsz || soname_v >= im->strsz) {
					free(file);
					return load_fail(im, err, "DT_SONAME outside .dynstr");
				}
				avail = im->strsz - soname_v;
				p = (uint8_t *)s->mem->base + im->base + im->strtab + soname_v;
				(void)p;
				/* Bounds are checked here, not through im_ptr(): strtab is validated below. */
				for (int i2 = 0; i2 < im->nseg; i2++) {
					struct n2elf_seg *g = &im->seg[i2];

					if ((uint64_t)im->strtab + soname_v >= g->vaddr &&
					    (uint64_t)im->strtab + soname_v + avail <=
					    (uint64_t)g->vaddr + g->memsz)
						break;
					if (i2 == im->nseg - 1) {
						free(file);
						return load_fail(im, err, "DT_SONAME outside PT_LOAD");
					}
				}
				len = n2e_strnlen((const char *)p, avail);
				if (len == avail || len >= sizeof(tmp)) {
					free(file);
					return load_fail(im, err, "DT_SONAME is not a valid string");
				}
				memcpy(tmp, p, len + 1);
				strcpy(im->soname, tmp);
			}
		}
	}
	free(file);
	file = NULL;

	if (have_rela) {
		return load_fail(im, err, "DT_RELA is not supported on AArch32");
	}
	if (have_dt_relr && have_android_relr) {
		return load_fail(im, err, "both DT_RELR and DT_ANDROID_RELR are present");
	}
	if (im->rel && !im->relsz) {
		return load_fail(im, err, "DT_REL without DT_RELSZ");
	}
	if (im->relsz && !im->rel) {
		return load_fail(im, err, "DT_RELSZ without DT_REL");
	}
	if (im->jmprel && !im->pltrelsz) {
		return load_fail(im, err, "DT_JMPREL without DT_PLTRELSZ");
	}
	if (im->pltrelsz && !im->jmprel) {
		return load_fail(im, err, "DT_PLTRELSZ without DT_JMPREL");
	}
	if (im->android_relsz && !im->android_rel) {
		return load_fail(im, err, "DT_ANDROID_RELSZ without DT_ANDROID_REL");
	}
	if (im->android_rel && !im->android_relsz) {
		return load_fail(im, err, "DT_ANDROID_REL without DT_ANDROID_RELSZ");
	}
	if (im->relsz && im->android_relsz) {
		return load_fail(im, err, "both DT_REL and DT_ANDROID_REL are present");
	}
	if (im->relrsz && !im->relr) {
		return load_fail(im, err, "RELR size without a RELR address");
	}
	if (im->relr && !im->relrsz) {
		return load_fail(im, err, "RELR address without a RELR size");
	}
	if (im->relrent != 0 && im->relrent != 4) {
		set_err(err, "%s: unsupported RELR entry size %u", im->soname, im->relrent);
		return load_fail(im, NULL, "");
	}
	if ((im->symtab || im->strtab || im->strsz) &&
	    (!im->symtab || !im->strtab || !im->strsz)) {
		return load_fail(im, err, "incomplete symbol/string table tags");
	}
	if (im->symtab) {
		if (im->syment == 0)
			im->syment = sizeof(struct n2e_sym);
		if (im->syment != sizeof(struct n2e_sym)) {
			return load_fail(im, err, "unsupported DT_SYMENT");
		}
		im->have_symtab = 1;
	}
	if (im->have_symtab && im_ptr(im, im->strtab, im->strsz, 0, "DT_STRTAB", err) == NULL)
		return load_fail(im, NULL, "");
	if (im->have_symtab && im->nsyms == 0) {
		if (im->sysv_hash) {
			uint8_t *h = im_ptr(im, im->sysv_hash, 8, 0, "DT_HASH", err);
			uint32_t nbucket, nchain;

			if (!h)
				return load_fail(im, NULL, "");
			nbucket = rd32(h);
			nchain = rd32(h + 4);
			if (nbucket == 0 || nchain == 0 ||
			    im_ptr(im, im->sysv_hash, 8u + 4u * nbucket + 4u * nchain, 0,
				   "DT_HASH table", err) == NULL)
				return load_fail(im, NULL, "");
			im->nsyms = nchain;
		} else if (im->have_gnu_hash) {
			if (gnu_nsyms(im, &im->nsyms, err) != 0)
				return load_fail(im, NULL, "");
		} else if (im->strtab > im->symtab) {
			im->nsyms = (im->strtab - im->symtab) / im->syment;
		} else {
			return load_fail(im, err, "no hash table and no way to size the symbol table");
		}
	}
	if (im->symtab && im->nsyms == 0)
		return load_fail(im, err, "empty symbol table");

	/* Validate every table once, so n2elf_link() cannot fail on a malformed range. */
	if (im->relsz) {
		if ((im->relsz % 8) != 0 || im_ptr(im, im->rel, im->relsz, 0, "DT_REL", err) == NULL)
			return load_fail(im, NULL, "");
	}
	if (im->pltrelsz) {
		if ((im->pltrelsz % 8) != 0 ||
		    im_ptr(im, im->jmprel, im->pltrelsz, 0, "DT_JMPREL", err) == NULL)
			return load_fail(im, NULL, "");
	}
	if (im->android_relsz) {
		if (im_ptr(im, im->android_rel, im->android_relsz, 0, "DT_ANDROID_REL", err) == NULL)
			return load_fail(im, NULL, "");
		if (im->android_relsz < 4 ||
		    memcmp(s->mem->base + im->base + im->android_rel, "APS2", 4) != 0)
			return load_fail(im, err, "DT_ANDROID_REL has no APS2 magic");
	}
	if (im->relrsz) {
		if ((im->relrsz % 4) != 0 || im_ptr(im, im->relr, im->relrsz, 0, "RELR", err) == NULL)
			return load_fail(im, NULL, "");
	}
	if (im->preinit_sz) {
		if ((im->preinit_sz % 4) != 0 ||
		    im_ptr(im, im->preinit_array, im->preinit_sz, 0, "DT_PREINIT_ARRAY", err) == NULL)
			return load_fail(im, NULL, "");
	}
	if (im->init_sz) {
		if ((im->init_sz % 4) != 0 ||
		    im_ptr(im, im->init_array, im->init_sz, 0, "DT_INIT_ARRAY", err) == NULL)
			return load_fail(im, NULL, "");
	}
	if (im->fini_sz) {
		if ((im->fini_sz % 4) != 0 ||
		    im_ptr(im, im->fini_array, im->fini_sz, 0, "DT_FINI_ARRAY", err) == NULL)
			return load_fail(im, NULL, "");
	}
	if (im->init && im_ptr(im, im->init, 4, 0, "DT_INIT", err) == NULL)
		return load_fail(im, NULL, "");

	/* DT_NEEDED strings. */
	for (int i = 0; i < nneeded_v; i++) {
		uint32_t avail;
		uint8_t *p;
		size_t len;

		if (!im->strsz || needed_v[i] >= im->strsz)
			return load_fail(im, err, "DT_NEEDED outside .dynstr");
		avail = im->strsz - needed_v[i];
		p = im_ptr(im, im->strtab + needed_v[i], avail, 0, "DT_NEEDED string", err);
		if (!p)
			return load_fail(im, NULL, "");
		len = n2e_strnlen((const char *)p, avail);
		if (len == avail || len >= N2ELF_SONAME_MAX)
			return load_fail(im, err, "DT_NEEDED is not a valid library name");
		memcpy(im->needed[im->nneeded], p, len + 1);
		im->nneeded++;
	}

	s->img[s->nimg++] = im;
	return im;
}

/* ------------------------------------------------------------------------------------------------------------
 * Set lifecycle and shim registration.
 * --------------------------------------------------------------------------------------------------------- */
struct n2elf_set *n2elf_set_create(struct n2a32_mem *mem)
{
	struct n2elf_set *s;

	if (!mem)
		return NULL;
	s = (struct n2elf_set *)calloc(1, sizeof(*s));
	if (!s)
		return NULL;
	s->mem = mem;
	return s;
}

void n2elf_set_destroy(struct n2elf_set *s)
{
	if (!s)
		return;
	for (int i = 0; i < s->nimg; i++)
		free(s->img[i]);
	free(s);
}

void n2elf_set_add_shims(struct n2elf_set *s, const struct n2shim_entry *table)
{
	if (!s || !table || s->nftab >= N2ELF_MAX_SHIM_TABLES)
		return;
	s->ftab[s->nftab++] = table;
}

void n2elf_set_add_data(struct n2elf_set *s, const struct n2shim_data *table)
{
	if (!s || !table || s->ndtab >= N2ELF_MAX_SHIM_TABLES)
		return;
	s->dtab[s->ndtab++] = table;
}
