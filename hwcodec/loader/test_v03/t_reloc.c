/* SPDX-License-Identifier: MIT */
/*
 * t_reloc -- unit tests for the three relocation encodings in n2elf_reloc.c with hand-built tables.
 *
 * The Android packed streams here cover every group-flag combination (none, GROUPED_BY_INFO,
 * GROUPED_BY_OFFSET_DELTA, both, HAS_ADDEND, HAS_ADDEND|GROUPED_BY_ADDEND) plus malformed streams.
 * Expected values are literals derived from the format definition; the resolver returns
 * 0x20000000 + 4*symidx so symbol relocations are distinguishable from bias-relative ones.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct n2elf_rctx;
struct n2elf_rctx *n2elf_rctx_create(uint8_t *g0, uint32_t bias, uint32_t vaddr_lo, uint32_t vaddr_hi,
	int (*resolve)(void *, uint32_t, uint32_t *, char *), void *opaque);
void n2elf_rctx_destroy(struct n2elf_rctx *ctx);
unsigned n2elf_rctx_applied(const struct n2elf_rctx *ctx);
int n2elf_apply_plain_rel(struct n2elf_rctx *ctx, const uint8_t *table, uint32_t size, char err[160]);
int n2elf_apply_android_rel(struct n2elf_rctx *ctx, const uint8_t *table, uint32_t size, char err[160]);
int n2elf_apply_relr(struct n2elf_rctx *ctx, const uint8_t *table, uint32_t size, uint32_t ent, char err[160]);

#define BIAS 0x1000u
static uint8_t mem[0x2000];
static int checks;
static int trace_calls;
static uint32_t trace_last_where, trace_last_type, trace_last_value;

static void trace(uint32_t where, uint32_t type, uint32_t value)
{
	trace_calls++;
	trace_last_where = where;
	trace_last_type = type;
	trace_last_value = value;
}
extern void (*n2elf_reloc_trace)(uint32_t, uint32_t, uint32_t);

static void ck(int cond, const char *what)
{
	checks++;
	if (!cond) {
		printf("FAIL %s\n", what);
		exit(1);
	}
}

static void ck_u32(uint32_t got, uint32_t want, const char *what)
{
	checks++;
	if (got != want) {
		printf("FAIL %s: got %08x, want %08x\n", what, got, want);
		exit(1);
	}
}

/* The context maps link-time address 0 to BIAS, so the backing store for link-time address a is mem[BIAS + a]. */
static uint32_t ld(uint32_t a)
{
	uint32_t v;

	memcpy(&v, mem + BIAS + a, 4);
	return v;
}

static void st(uint32_t a, uint32_t v)
{
	memcpy(mem + BIAS + a, &v, 4);
}

static int resolver(void *opaque, uint32_t sym, uint32_t *value, char *err)
{
	(void)opaque;
	(void)err;
	*value = 0x20000000u + 4u * sym;
	return 0;
}

static struct n2elf_rctx *fresh(void)
{
	memset(mem, 0, sizeof(mem));
	trace_calls = 0;
	n2elf_reloc_trace = trace;
	return n2elf_rctx_create(mem, BIAS, 0, 0x1000u, resolver, NULL);
}

static void emit_sleb(uint8_t *b, uint32_t *pos, int64_t v)
{
	int more = 1;

	while (more) {
		uint8_t byte = (uint8_t)(v & 0x7f);

		v >>= 7;
		if ((v == 0 && (byte & 0x40) == 0) || (v == -1 && (byte & 0x40) != 0))
			more = 0;
		else
			byte |= 0x80;
		b[(*pos)++] = byte;
	}
}

static void put32(uint8_t *b, uint32_t *pos, uint32_t v)
{
	memcpy(b + *pos, &v, 4);
	*pos += 4;
}

static void put_rel(uint8_t *t, unsigned i, uint32_t off, uint32_t info)
{
	memcpy(t + i * 8, &off, 4);
	memcpy(t + i * 8 + 4, &info, 4);
}

static void test_plain(void)
{
	uint8_t table[64];
	char err[160];
	struct n2elf_rctx *c;

	c = fresh();
	st(0x100, 0x44);
	st(0x104, 8);
	put_rel(table, 0, 0x100, 23);
	put_rel(table, 1, 0x104, 2 | (5u << 8));
	put_rel(table, 2, 0x108, 21 | (7u << 8));
	ck(n2elf_apply_plain_rel(c, table, 24, err) == 0, "plain REL applies");
	ck(n2elf_rctx_applied(c) == 3, "plain REL applied count");
	ck_u32(ld(0x100), 0x1044, "R_ARM_RELATIVE = bias + in-place addend");
	ck_u32(ld(0x104), 0x2000001c, "R_ARM_ABS32 = S + in-place addend");
	ck_u32(ld(0x108), 0x2000001c, "R_ARM_GLOB_DAT = S");
	ck(trace_calls == 3, "trace hook called per relocation");
	ck_u32(trace_last_where, 0x1108, "trace address is the guest address");
	ck_u32(trace_last_type, 21, "trace type");
	ck_u32(trace_last_value, 0x2000001c, "trace value");
	n2elf_rctx_destroy(c);

	c = fresh();
	put_rel(table, 0, 0x100, 42);
	err[0] = '\0';
	ck(n2elf_apply_plain_rel(c, table, 8, err) != 0, "R_ARM_PREL31 is rejected");
	ck(strstr(err, "R_ARM_PREL31") != NULL, "error names R_ARM_PREL31");
	ck(strstr(err, "(42)") != NULL, "error names the numeric type");
	ck(n2elf_rctx_applied(c) == 0, "nothing applied after the type error");
	n2elf_rctx_destroy(c);

	c = fresh();
	ck(n2elf_apply_plain_rel(c, table, 0, err) == 0, "empty REL table is accepted");
	ck(n2elf_apply_plain_rel(c, table, 4, err) != 0, "REL size not multiple of 8 is rejected");
	ck(strstr(err, "multiple of 8") != NULL, "REL size error text");
	n2elf_rctx_destroy(c);
}

/* Android packed stream cases. */
static void test_android(void)
{
	uint8_t b[256];
	char err[160];
	uint32_t p;
	struct n2elf_rctx *c;

	/* a) no grouped flags: per-item offset delta, then r_info. */
	c = fresh();
	st(0x100, 0x1234);
	p = 0;
	memcpy(b + p, "APS2", 4); p += 4;
	emit_sleb(b, &p, 3);          /* relocation count */
	emit_sleb(b, &p, 0);          /* relocations start offset */
	emit_sleb(b, &p, 3);          /* group size */
	emit_sleb(b, &p, 0);          /* flags */
	emit_sleb(b, &p, 0x100); emit_sleb(b, &p, 23);
	emit_sleb(b, &p, 4);     emit_sleb(b, &p, 23);
	emit_sleb(b, &p, 4);     emit_sleb(b, &p, 21 | (2u << 8));
	ck(n2elf_apply_android_rel(c, b, p, err) == 0, "packed no-flags applies");
	ck(n2elf_rctx_applied(c) == 3, "packed no-flags count");
	ck_u32(ld(0x100), 0x2234, "packed RELATIVE value");
	ck_u32(ld(0x104), 0x1000, "packed second RELATIVE value");
	ck_u32(ld(0x108), 0x20000008, "packed GLOB_DAT value");
	n2elf_rctx_destroy(c);

	/* b) GROUPED_BY_INFO: one info word, per-item deltas. */
	c = fresh();
	p = 0;
	memcpy(b + p, "APS2", 4); p += 4;
	emit_sleb(b, &p, 2);
	emit_sleb(b, &p, 0);
	emit_sleb(b, &p, 2);
	emit_sleb(b, &p, 1);          /* GROUPED_BY_INFO */
	emit_sleb(b, &p, 23);
	emit_sleb(b, &p, 0x100);
	emit_sleb(b, &p, 4);
	ck(n2elf_apply_android_rel(c, b, p, err) == 0, "packed grouped-info applies");
	ck_u32(ld(0x100), 0x1000, "packed grouped-info first");
	ck_u32(ld(0x104), 0x1000, "packed grouped-info second");
	n2elf_rctx_destroy(c);

	/* c) GROUPED_BY_OFFSET_DELTA: one delta, per-item info. */
	c = fresh();
	p = 0;
	memcpy(b + p, "APS2", 4); p += 4;
	emit_sleb(b, &p, 2);
	emit_sleb(b, &p, 0);
	emit_sleb(b, &p, 2);
	emit_sleb(b, &p, 2);          /* GROUPED_BY_OFFSET_DELTA */
	emit_sleb(b, &p, 4);
	emit_sleb(b, &p, 23);
	emit_sleb(b, &p, 21 | (3u << 8));
	ck(n2elf_apply_android_rel(c, b, p, err) == 0, "packed grouped-delta applies");
	ck_u32(ld(0x04), 0x1000, "packed grouped-delta first");
	ck_u32(ld(0x08), 0x2000000c, "packed grouped-delta second");
	n2elf_rctx_destroy(c);

	/* d) both grouped. */
	c = fresh();
	p = 0;
	memcpy(b + p, "APS2", 4); p += 4;
	emit_sleb(b, &p, 2);
	emit_sleb(b, &p, 0);
	emit_sleb(b, &p, 2);
	emit_sleb(b, &p, 3);
	emit_sleb(b, &p, 23);
	emit_sleb(b, &p, 8);
	ck(n2elf_apply_android_rel(c, b, p, err) == 0, "packed both-grouped applies");
	ck_u32(ld(0x08), 0x1000, "packed both-grouped first");
	ck_u32(ld(0x10), 0x1000, "packed both-grouped second");
	n2elf_rctx_destroy(c);

	/* e) HAS_ADDEND per item: parsed, then ignored for Elf32_Rel (bionic behaviour). */
	c = fresh();
	st(0x100, 0x99);
	p = 0;
	memcpy(b + p, "APS2", 4); p += 4;
	emit_sleb(b, &p, 1);
	emit_sleb(b, &p, 0);
	emit_sleb(b, &p, 1);
	emit_sleb(b, &p, 8);          /* HAS_ADDEND */
	emit_sleb(b, &p, 0x100);
	emit_sleb(b, &p, 23);
	emit_sleb(b, &p, 0x5000);
	ck(n2elf_apply_android_rel(c, b, p, err) == 0, "packed per-item addend applies");
	ck_u32(ld(0x100), 0x1099, "explicit addend is ignored for a RELATIVE relocation");
	n2elf_rctx_destroy(c);

	/* f) HAS_ADDEND|GROUPED_BY_ADDEND: group addend parsed once. */
	c = fresh();
	st(0x104, 4);
	p = 0;
	memcpy(b + p, "APS2", 4); p += 4;
	emit_sleb(b, &p, 1);
	emit_sleb(b, &p, 0);
	emit_sleb(b, &p, 1);
	emit_sleb(b, &p, 12);
	emit_sleb(b, &p, 0x7000);
	emit_sleb(b, &p, 0x104);
	emit_sleb(b, &p, 2 | (1u << 8));
	ck(n2elf_apply_android_rel(c, b, p, err) == 0, "packed grouped addend applies");
	ck_u32(ld(0x104), 0x20000008, "group addend is ignored; in-place addend used");
	n2elf_rctx_destroy(c);

	/* g) a target outside the image is rejected before any write. */
	c = fresh();
	p = 0;
	memcpy(b + p, "APS2", 4); p += 4;
	emit_sleb(b, &p, 1);
	emit_sleb(b, &p, 0);
	emit_sleb(b, &p, 1);
	emit_sleb(b, &p, 0);
	emit_sleb(b, &p, 0x70000000);
	emit_sleb(b, &p, 23);
	ck(n2elf_apply_android_rel(c, b, p, err) != 0, "packed target outside the image rejected");
	ck(strstr(err, "outside") != NULL, "packed target error text");
	ck(n2elf_rctx_applied(c) == 0, "nothing applied for an out-of-range packed target");
	n2elf_rctx_destroy(c);

	/* h) malformed streams. */
	c = fresh();
	memcpy(b, "APS3", 4);
	ck(n2elf_apply_android_rel(c, b, 4, err) != 0, "bad packed magic rejected");
	ck(strstr(err, "APS2") != NULL, "bad magic error text");
	n2elf_rctx_destroy(c);

	c = fresh();
	p = 0;
	memcpy(b + p, "APS2", 4); p += 4;
	emit_sleb(b, &p, 1);
	ck(n2elf_apply_android_rel(c, b, p, err) != 0, "truncated packed stream rejected");
	ck(strstr(err, "truncated") != NULL, "truncation error text");
	n2elf_rctx_destroy(c);

	c = fresh();
	p = 0;
	memcpy(b + p, "APS2", 4); p += 4;
	emit_sleb(b, &p, 1);
	emit_sleb(b, &p, 0);
	emit_sleb(b, &p, 2);          /* group larger than the count */
	emit_sleb(b, &p, 0);
	ck(n2elf_apply_android_rel(c, b, p, err) != 0, "group larger than count rejected");
	ck(strstr(err, "overruns") != NULL, "group overrun error text");
	n2elf_rctx_destroy(c);

	c = fresh();
	p = 0;
	memcpy(b + p, "APS2", 4); p += 4;
	emit_sleb(b, &p, 1);
	emit_sleb(b, &p, 0);
	emit_sleb(b, &p, 1);
	emit_sleb(b, &p, 0x10);       /* unknown group flag */
	ck(n2elf_apply_android_rel(c, b, p, err) != 0, "unknown group flags rejected");
	ck(strstr(err, "flags") != NULL, "unknown flags error text");
	n2elf_rctx_destroy(c);

	c = fresh();
	p = 0;
	memcpy(b + p, "APS2", 4); p += 4;
	for (int i = 0; i < 12; i++)
		b[p++] = 0x80;        /* endless SLEB128 continuation */
	b[p++] = 0x00;
	ck(n2elf_apply_android_rel(c, b, p, err) != 0, "SLEB128 overflow rejected");
	ck(strstr(err, "SLEB128") != NULL, "SLEB128 error text");
	n2elf_rctx_destroy(c);
}

static void test_relr(void)
{
	uint8_t t[64];
	char err[160];
	uint32_t p = 0;
	struct n2elf_rctx *c;

	c = fresh();
	st(0x100, 0x11);
	put32(t, &p, 0x100);
	put32(t, &p, 0x7);            /* bits 1,2 -> 0x104, 0x108 */
	put32(t, &p, 0x200);
	put32(t, &p, 0xffffffffu);    /* 31 targets 0x204..0x27c */
	ck(n2elf_apply_relr(c, t, p, 4, err) == 0, "RELR applies");
	ck(n2elf_rctx_applied(c) == 35, "RELR target count (1 + 2 + 1 + 31)");
	ck_u32(ld(0x100), 0x1011, "RELR address word keeps the in-place addend");
	ck_u32(ld(0x104), 0x1000, "RELR bitmap bit 1");
	ck_u32(ld(0x108), 0x1000, "RELR bitmap bit 2");
	ck_u32(ld(0x204), 0x1000, "RELR all-ones bitmap first");
	ck_u32(ld(0x27c), 0x1000, "RELR all-ones bitmap last");
	n2elf_rctx_destroy(c);

	c = fresh();
	p = 0;
	put32(t, &p, 0x1);            /* bitmap with only bit 0 set: no targets */
	ck(n2elf_apply_relr(c, t, p, 0, err) == 0, "RELR entry size 0 means 4");
	ck(n2elf_rctx_applied(c) == 0, "RELR bitmap word has no bit-0 target");
	n2elf_rctx_destroy(c);

	c = fresh();
	p = 0;
	put32(t, &p, 0x100);
	ck(n2elf_apply_relr(c, t, p, 8, err) != 0, "RELR entry size 8 rejected for AArch32");
	ck(strstr(err, "entry size") != NULL, "RELR entry size error text");
	ck(n2elf_apply_relr(c, t, 6, 4, err) != 0, "RELR size not a multiple rejected");
	n2elf_rctx_destroy(c);

	c = fresh();
	p = 0;
	put32(t, &p, 0xfffffffc);     /* even word: a target past the image */
	ck(n2elf_apply_relr(c, t, p, 4, err) != 0, "RELR target outside the image rejected");
	ck(strstr(err, "outside") != NULL, "RELR target error text");
	n2elf_rctx_destroy(c);
}

int main(void)
{
	test_plain();
	test_android();
	test_relr();
	printf("V03 reloc unit: %d checks\n", checks);
	return 0;
}
