/*
 * y2s98: rw_texlayout_pure.h on the build machine (rw-texlayout-host-test.sh: ASan/UBSan, -Werror).
 *
 * 1  the format table and the layouts of the sizes the phone's .atx files have (their 'astc' chunk lengths)
 * 2  refusals leave memory untouched
 * 3  fuzz: random layouts, whole levels and block-aligned regions in and out, caller strides, canaries
 * 4  the call sequences read from the 20H392 cache, with this layout in the place of AGX's:
 *    W  ImageIO ATX writer (ASTCTwiddler ctor allocate=1, copyFromLinearData level 0, finalize, twiddledData/Size)
 *       -> the bytes written are the encoder's linear bytes = what the nil-layout writer stored (its fallback)
 *    R  ImageIO reader of a pre-twiddled file (data used in place) -> CoreAnimation by reference -> window server
 *       -newTextureWithBytesNoCopy:(data, round_page(size)) -> per level -replaceRegion: arguments, checked against
 *       rwa_texture_write's rules (air_metal.c) and uploaded into a model texture
 *    C  CoreAnimation's own conversion (new_image_with_texture_data, layout path): initialize, a copy per level with the
 *       file's level offsets and row lengths, finalize -> window server as in R
 *    S  ImageIO's software decoder way back (copyFromTextureData: whole level out)
 *    N  what the nil answers did (the bug): no layout -> size 0; the server's nil texture
 */
#include "rw_texlayout_pure.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint32_t
rnd(uint32_t n)
{
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return n ? (uint32_t)((rng >> 16) % n) : 0;
}

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static const uint32_t formats[] = { 186, 187, 188, 189, 190, 192, 193, 194, 195, 196, 197, 198, 199, 200,
				    204, 205, 206, 207, 208, 210, 211, 212, 213, 214, 215, 216, 217, 218 };
static const uint8_t feet[14][2] = { { 4, 4 }, { 5, 4 }, { 5, 5 }, { 6, 5 }, { 6, 6 }, { 8, 5 }, { 8, 6 }, { 8, 8 }, { 10, 5 },
				     { 10, 6 }, { 10, 8 }, { 10, 10 }, { 12, 10 }, { 12, 12 } };

/* The model of the compiler library's texture: rwa_texture_write's checks (air_metal.c: caller_stride, the block rule) and
 * texture_subdata's copy, a level at a time. */
struct model {
	uint32_t bw, bh, width, height, levels;
	uint8_t *level[RWTL_MAX_LEVELS];
	uint32_t cols[RWTL_MAX_LEVELS], rows[RWTL_MAX_LEVELS];
	int written[RWTL_MAX_LEVELS];
};

static void
model_make(struct model *m, uint32_t format, uint32_t width, uint32_t height, uint32_t levels)
{
	memset(m, 0, sizeof(*m));
	CHECK(rwtl_block_of(format, &m->bw, &m->bh));
	m->width = width;
	m->height = height;
	m->levels = levels;
	for (uint32_t i = 0; i < levels; i++) {
		uint32_t w = rwtl_minify(width, i), h = rwtl_minify(height, i);

		m->cols[i] = (w + m->bw - 1) / m->bw;
		m->rows[i] = (h + m->bh - 1) / m->bh;
		m->level[i] = calloc((size_t)m->cols[i] * m->rows[i], 16);
		CHECK(m->level[i] != NULL);
	}
}

static void
model_free(struct model *m)
{
	for (uint32_t i = 0; i < m->levels; i++) {
		free(m->level[i]);
	}
}

/* rwa_texture_write(texture, level, 0, origin, size, bytes, stride): 0 or -1 */
static int
model_write(struct model *m, uint32_t level, const unsigned origin[3], const unsigned size[3], const void *bytes, size_t stride)
{
	if (bytes == NULL || level >= m->levels) {
		return -1;
	}
	uint32_t lw = rwtl_minify(m->width, level), lh = rwtl_minify(m->height, level);

	if (origin[2] != 0 || size[2] != 1 || size[0] == 0 || size[1] == 0 || origin[0] + size[0] > lw || origin[1] + size[1] > lh) {
		return -1;
	}
	size_t least = (size_t)((size[0] + m->bw - 1) / m->bw) * 16;       /* util_format_get_stride */
	size_t row = stride == 0 ? least : stride;

	if (row < least) {
		return -1;
	}
	if (origin[0] % m->bw || origin[1] % m->bh || (size[0] % m->bw && origin[0] + size[0] != lw) ||
	    (size[1] % m->bh && origin[1] + size[1] != lh)) {
		return -1;
	}
	uint32_t c0 = origin[0] / m->bw, r0 = origin[1] / m->bh, nc = (size[0] + m->bw - 1) / m->bw, nr = (size[1] + m->bh - 1) / m->bh;

	for (uint32_t r = 0; r < nr; r++) {
		memcpy(m->level[level] + ((size_t)(r0 + r) * m->cols[level] + c0) * 16, (const uint8_t *)bytes + (size_t)r * row,
		    (size_t)nc * 16);
	}
	if (c0 == 0 && r0 == 0 && nc == m->cols[level] && nr == m->rows[level]) {
		m->written[level] = 1;
	}
	return 0;
}

/* -newTextureWithBytesNoCopy:length:descriptor:deallocator: as redwoodmetal.m does it: 0 = texture made, -1 = nil */
static int
server_texture(struct model *m, uint32_t format, uint32_t width, uint32_t height, uint32_t levels, const void *bytes, size_t length)
{
	struct rwtl_layout l;
	const char *why = "";

	if (rwtl_init(&l, format, width, height, levels, &why) != 0 || bytes == NULL || length < l.size) {
		return -1;
	}
	model_make(m, format, width, height, levels);
	for (uint32_t level = 0; level < l.levels; level++) {
		uint32_t w = 0, h = 0;
		size_t row = 0;
		const void *from = rwtl_level_bytes(&l, bytes, length, level, &w, &h, &row, &why);
		const unsigned origin[3] = { 0, 0, 0 };
		const unsigned size[3] = { w, h, 1 };

		if (from == NULL || model_write(m, level, origin, size, from, row) != 0) {
			model_free(m);
			return -1;
		}
	}
	for (uint32_t level = 0; level < levels; level++) {
		CHECK(m->written[level]);
	}
	return 0;
}

static size_t
round_up(size_t v, size_t to)
{
	return (v + to - 1) / to * to;
}

static void
fill(uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		p[i] = (uint8_t)rnd(256);
	}
}

static void
test_table(void)
{
	uint32_t bw, bh, n = 0;

	for (uint32_t f = 0; f < 600; f++) {
		int known = 0;

		for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
			if (formats[i] == f) {
				known = 1;
				CHECK(rwtl_block_of(f, &bw, &bh) && bw == feet[i % 14][0] && bh == feet[i % 14][1]);
				n++;
			}
		}
		if (!known) {
			CHECK(!rwtl_block_of(f, &bw, &bh));
		}
	}
	CHECK(n == 28);

	struct rwtl_layout l;
	const char *why = NULL;

	/* the three sizes PosterBoard stores (mirror of 13 November): gallery 302x656, hero 192x192, full screen 1125x2436 */
	CHECK(rwtl_init(&l, 204, 302, 656, 1, &why) == 0 && l.size == 199424 && l.level[0].cols == 76 && l.level[0].rows == 164 &&
	    l.level[0].row_bytes == 1216 && l.level[0].offset == 0);
	CHECK(rwtl_init(&l, 204, 192, 192, 1, &why) == 0 && l.size == 36864);
	CHECK(rwtl_init(&l, 186, 1125, 2436, 1, &why) == 0 && l.size == 2747808 && l.level[0].cols == 282 && l.level[0].rows == 609);
	/* a chain: 10x6 blocks, 37x13 -> 18x6 -> 9x3 -> 4x1 -> 2x1 -> 1x1 */
	CHECK(rwtl_init(&l, 214, 37, 13, 6, &why) == 0);
	CHECK(l.level[0].cols == 4 && l.level[0].rows == 3 && l.level[1].cols == 2 && l.level[1].rows == 1 && l.level[5].cols == 1);
	CHECK(l.level[1].offset == 4 * 3 * 16 && l.level[2].offset == l.level[1].offset + 2 * 16 && l.size == (12 + 2 + 1 + 1 + 1 + 1) * 16);
	CHECK(rwtl_init(&l, 214, 37, 13, 7, &why) == -1);                  /* 37x13 has 6 levels */
	CHECK(rwtl_init(&l, 204, 0, 4, 1, &why) == -1 && rwtl_init(&l, 204, 4, 0, 1, &why) == -1);
	CHECK(rwtl_init(&l, 204, 16385, 4, 1, &why) == -1 && rwtl_init(&l, 204, 4, 4, 0, &why) == -1);
	CHECK(rwtl_init(&l, 80, 64, 64, 1, &why) == -1 && rwtl_init(&l, 115, 64, 64, 1, &why) == -1);     /* BGRA8, RGBA16Float */
	CHECK(rwtl_init(&l, 160, 64, 64, 1, &why) == -1 && rwtl_init(&l, 222, 64, 64, 1, &why) == -1);    /* PVRTC, ASTC HDR */
	CHECK(rwtl_init(&l, 204, 16384, 16384, 15, &why) == 0 && rwtl_full_chain(16384, 16384) == 15 && rwtl_full_chain(1, 1) == 1);
}

static void
test_refusals(void)
{
	struct rwtl_layout l;
	const char *why = NULL;
	uint8_t mem[4 * 4 * 16 + 2 * 2 * 16], before[sizeof(mem)], lin[1024], lin0[1024];

	CHECK(rwtl_init(&l, 204, 16, 16, 2, &why) == 0 && l.size == sizeof(mem));
	fill(mem, sizeof(mem));
	memcpy(before, mem, sizeof(mem));
	fill(lin, sizeof(lin));
	memcpy(lin0, lin, sizeof(lin));
#define REFUSED_IN(level, x, y, w, h, row) \
	CHECK(rwtl_copy_in(&l, mem, level, x, y, w, h, lin, 0, row, &why) == -1 && memcmp(mem, before, sizeof(mem)) == 0)
#define REFUSED_OUT(level, x, y, w, h, row) \
	CHECK(rwtl_copy_out(&l, mem, level, x, y, w, h, lin, 0, row, &why) == -1 && memcmp(lin, lin0, sizeof(lin)) == 0)
	REFUSED_IN(2, 0, 0, 4, 4, 0);          /* no such level */
	REFUSED_IN(0, 1, 0, 4, 4, 0);          /* not on a block */
	REFUSED_IN(0, 0, 2, 4, 4, 0);
	REFUSED_IN(0, 0, 0, 6, 4, 0);          /* ends inside a block, not at the edge */
	REFUSED_IN(0, 0, 0, 4, 7, 0);
	REFUSED_IN(0, 0, 0, 20, 4, 0);         /* leaves the level */
	REFUSED_IN(0, 16, 0, 4, 4, 0);
	REFUSED_IN(0, 0, 0, 0, 4, 0);          /* empty */
	REFUSED_IN(0, 0, 0, 16, 16, 63);       /* 4 blocks a row need 64 bytes */
	REFUSED_IN(1, 0, 0, 16, 16, 0);        /* level 1 is 8x8 */
	REFUSED_IN(0, (uint64_t)1 << 40, 0, 4, 4, 0);
	REFUSED_IN(0, 0, 0, (uint64_t)1 << 40, 4, 0);
	REFUSED_OUT(2, 0, 0, 4, 4, 0);
	REFUSED_OUT(0, 3, 0, 4, 4, 0);
	REFUSED_OUT(0, 0, 0, 16, 16, 48);
	CHECK(rwtl_copy_in(&l, NULL, 0, 0, 0, 16, 16, lin, 0, 0, &why) == -1);
	CHECK(rwtl_copy_in(&l, mem, 0, 0, 0, 16, 16, NULL, 0, 0, &why) == -1);

	uint32_t w, h;
	size_t row;

	CHECK(rwtl_level_bytes(&l, mem, sizeof(mem) - 1, 0, &w, &h, &row, &why) == NULL);
	CHECK(rwtl_level_bytes(&l, NULL, sizeof(mem), 0, &w, &h, &row, &why) == NULL);
	CHECK(rwtl_level_bytes(&l, mem, sizeof(mem), 2, &w, &h, &row, &why) == NULL);
	CHECK(rwtl_level_bytes(&l, mem, sizeof(mem), 1, &w, &h, &row, &why) == mem + 256 && w == 8 && h == 8 && row == 32);
}

/* A linear picture of a layout: per level its tight rows of blocks, as an encoder gives them. */
struct picture {
	uint8_t *level[RWTL_MAX_LEVELS];
};

static void
picture_make(struct picture *p, const struct rwtl_layout *l)
{
	for (uint32_t i = 0; i < l->levels; i++) {
		p->level[i] = malloc(l->level[i].size);
		CHECK(p->level[i] != NULL);
		fill(p->level[i], l->level[i].size);
	}
}

static void
picture_free(struct picture *p, const struct rwtl_layout *l)
{
	for (uint32_t i = 0; i < l->levels; i++) {
		free(p->level[i]);
	}
}

static void
random_layout(struct rwtl_layout *l)
{
	const char *why = NULL;
	uint32_t f = formats[rnd(28)], w = 1 + rnd(rnd(4) == 0 ? 700 : 90), h = 1 + rnd(rnd(4) == 0 ? 700 : 90);
	uint32_t chain = rwtl_full_chain(w, h), levels = rnd(3) == 0 ? chain : 1 + rnd(chain);

	CHECK(rwtl_init(l, f, w, h, levels, &why) == 0);
	size_t at = 0;

	for (uint32_t i = 0; i < l->levels; i++) {         /* the invariants of the layout: packed, in order, no overlap */
		CHECK(l->level[i].offset == at && l->level[i].size == (size_t)l->level[i].cols * l->level[i].rows * 16);
		CHECK(l->level[i].cols == (l->level[i].width + l->block_w - 1) / l->block_w);
		at += l->level[i].size;
	}
	CHECK(at == l->size);
}

static unsigned long fuzz_copies;

static void
test_fuzz(unsigned rounds)
{
	for (unsigned n = 0; n < rounds; n++) {
		struct rwtl_layout l;
		struct picture p;
		const char *why = NULL;

		random_layout(&l);
		picture_make(&p, &l);
		/* texture memory with canaries on both sides (and ASan around the allocation) */
		uint8_t *block = malloc(l.size + 64), *mem = block + 32;

		CHECK(block != NULL);
		memset(block, 0xa5, l.size + 64);
		/* whole levels in, with a caller stride of the exact row, 0 (tight) or longer */
		for (uint32_t i = 0; i < l.levels; i++) {
			const struct rwtl_level *v = &l.level[i];
			size_t pad = rnd(3) == 0 ? 16 * rnd(5) + rnd(7) : 0, row = v->row_bytes + pad, off = rnd(2) ? rnd(40) : 0;
			uint8_t *lin = malloc(off + row * v->rows);

			CHECK(lin != NULL);
			memset(lin, 0x5a, off + row * v->rows);
			for (uint32_t r = 0; r < v->rows; r++) {
				memcpy(lin + off + r * row, p.level[i] + r * v->row_bytes, v->row_bytes);
			}
			CHECK(rwtl_copy_in(&l, mem, i, 0, 0, v->width, v->height, lin, off, pad == 0 && rnd(2) ? 0 : row, &why) == 0);
			free(lin);
			fuzz_copies++;
		}
		for (unsigned i = 0; i < 32; i++) {
			CHECK(block[i] == 0xa5 && block[32 + l.size + i] == 0xa5);
		}
		for (uint32_t i = 0; i < l.levels; i++) {          /* the memory IS the packed linear picture */
			CHECK(memcmp(mem + l.level[i].offset, p.level[i], l.level[i].size) == 0);
		}
		/* a block-aligned region replaced, read back, and everything outside it untouched */
		for (unsigned k = 0; k < 6; k++) {
			uint32_t i = rnd(l.levels);
			const struct rwtl_level *v = &l.level[i];
			uint32_t c0 = rnd(v->cols), r0 = rnd(v->rows), nc = 1 + rnd(v->cols - c0), nr = 1 + rnd(v->rows - r0);
			uint64_t x = (uint64_t)c0 * l.block_w, y = (uint64_t)r0 * l.block_h;
			uint64_t w = c0 + nc == v->cols ? v->width - x : (uint64_t)nc * l.block_w;
			uint64_t h = r0 + nr == v->rows ? v->height - y : (uint64_t)nr * l.block_h;
			size_t row = (size_t)nc * 16 + (rnd(2) ? 16 * rnd(3) : 0);
			uint8_t *lin = malloc(row * nr), *back = malloc(row * nr), *copy = malloc(l.size);

			CHECK(lin != NULL && back != NULL && copy != NULL);
			fill(lin, row * nr);
			memset(back, 0x11, row * nr);
			memcpy(copy, mem, l.size);
			CHECK(rwtl_copy_in(&l, mem, i, x, y, w, h, lin, 0, row, &why) == 0);
			for (uint32_t r = 0; r < nr; r++) {         /* the expected memory, built independently */
				memcpy(copy + v->offset + ((size_t)(r0 + r) * v->cols + c0) * 16, lin + r * row, (size_t)nc * 16);
				memcpy(p.level[i] + ((size_t)(r0 + r) * v->cols + c0) * 16, lin + r * row, (size_t)nc * 16);
			}
			CHECK(memcmp(copy, mem, l.size) == 0);
			CHECK(rwtl_copy_out(&l, mem, i, x, y, w, h, back, 0, row, &why) == 0);
			for (uint32_t r = 0; r < nr; r++) {
				CHECK(memcmp(back + r * row, lin + r * row, (size_t)nc * 16) == 0);
				for (size_t b = (size_t)nc * 16; b < row; b++) {
					CHECK(back[r * row + b] == 0x11);         /* the caller's padding is not written */
				}
			}
			free(lin);
			free(back);
			free(copy);
			fuzz_copies += 2;
		}
		for (unsigned i = 0; i < 32; i++) {
			CHECK(block[i] == 0xa5 && block[32 + l.size + i] == 0xa5);
		}
		/* R: the window server's texture over this memory, page-rounded length as CoreAnimation gives it */
		struct model m;

		CHECK(server_texture(&m, l.format, l.width, l.height, l.levels, mem, l.size) == 0);
		for (uint32_t i = 0; i < l.levels; i++) {
			CHECK(memcmp(m.level[i], p.level[i], l.level[i].size) == 0);
		}
		model_free(&m);
		if (l.size > 0) {
			CHECK(server_texture(&m, l.format, l.width, l.height, l.levels, mem, l.size - 1) == -1);
		}
		/* S: every level out whole, as the software decoder asks */
		for (uint32_t i = 0; i < l.levels; i++) {
			uint8_t *out = malloc(l.level[i].size);

			CHECK(out != NULL);
			CHECK(rwtl_copy_out(&l, mem, i, 0, 0, l.level[i].width, l.level[i].height, out, 0, l.level[i].row_bytes, &why) == 0);
			CHECK(memcmp(out, p.level[i], l.level[i].size) == 0);
			free(out);
		}
		picture_free(&p, &l);
		free(block);
	}
}

/* W: the ATX writer with this layout, against the bytes the nil-layout writer stored (the encoder's own). */
static void
test_writer(void)
{
	static const uint32_t sizes[][2] = { { 302, 656 }, { 192, 192 }, { 1125, 2436 }, { 5, 3 }, { 4, 4 }, { 1, 1 }, { 301, 655 } };

	for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
		struct rwtl_layout l;
		const char *why = NULL;
		uint32_t w = sizes[s][0], h = sizes[s][1];

		CHECK(rwtl_init(&l, 204, w, h, 1, &why) == 0);
		size_t enc_row = (size_t)((w + 3) / 4) * 16, enc_size = enc_row * ((h + 3) / 4);    /* at_encoder's block counts */
		uint8_t *enc = malloc(enc_size);

		CHECK(enc != NULL);
		fill(enc, enc_size);
		/* ASTCTwiddler ctor: size, alignment, _ImageIO_Malloc(round up) */
		size_t size = l.size, alignment = 64;
		uint8_t *mem = calloc(1, round_up(size, alignment) + 1);

		CHECK(mem != NULL);
		/* copyFromLinearDataImp(enc, 0, enc_row, level 0): initialize (nothing), copy the whole level, finalize (nothing) */
		CHECK(rwtl_copy_in(&l, mem, 0, 0, 0, rwtl_minify(w, 0), rwtl_minify(h, 0), enc, 0, enc_row, &why) == 0);
		/* writeASTCData: 'astc', twiddledDataSize() bytes of twiddledData() -- the same bytes and count as the fallback */
		CHECK(size == enc_size && memcmp(mem, enc, enc_size) == 0);
		free(mem);
		free(enc);
	}
}

/* C: CoreAnimation's conversion of a linear mip chain (offsets and row lengths per level from CGImageTextureData). */
static void
test_ca_conversion(unsigned rounds)
{
	for (unsigned n = 0; n < rounds; n++) {
		struct rwtl_layout l;
		const char *why = NULL;

		random_layout(&l);
		/* the file's linear data: levels at arbitrary offsets with arbitrary (>= tight) row lengths */
		size_t offs[RWTL_MAX_LEVELS], rows[RWTL_MAX_LEVELS], total = rnd(64);

		for (uint32_t i = 0; i < l.levels; i++) {
			rows[i] = l.level[i].row_bytes + (rnd(3) == 0 ? 16 * rnd(4) : 0);
			offs[i] = total;
			total += rows[i] * l.level[i].rows + rnd(48);
		}
		uint8_t *file = malloc(total + 1), *mem = malloc(round_up(l.size, 16384) + 16384);

		CHECK(file != NULL && mem != NULL);
		fill(file, total);
		memset(mem, 0, round_up(l.size, 16384) + 16384);
		for (uint32_t i = 0; i < l.levels; i++) {
			/* [layout initializeTextureMemory:] before every level in ImageIO, once in CoreAnimation: must not clear */
			CHECK(rwtl_copy_in(&l, mem, i, 0, 0, l.level[i].width, l.level[i].height, file, offs[i], rows[i], &why) == 0);
		}
		struct model m;

		CHECK(server_texture(&m, l.format, l.width, l.height, l.levels, mem, round_up(l.size, 16384)) == 0);
		for (uint32_t i = 0; i < l.levels; i++) {
			for (uint32_t r = 0; r < l.level[i].rows; r++) {
				CHECK(memcmp(m.level[i] + (size_t)r * l.level[i].row_bytes, file + offs[i] + r * rows[i], l.level[i].row_bytes) == 0);
			}
		}
		model_free(&m);
		free(file);
		free(mem);
	}
}

/* N: the nil answers, as the callers took them (what the phone did until y2s98). */
static void
test_nil(void)
{
	/* [nil size] = 0, [nil alignment] = 0: CA::Render::aligned_malloc(0) = NULL -> no image; ImageIO: no twiddler memory, the
	 * 'astc' chunk gets the linear bytes and the reader hands them out as "tiled"; the server's texture is nil. With the
	 * layout the same file bytes make a texture: */
	struct rwtl_layout l;
	const char *why = NULL;
	struct model m;
	uint8_t *chunk = malloc(199424);

	CHECK(chunk != NULL);
	fill(chunk, 199424);
	CHECK(rwtl_init(&l, 204, 302, 656, 1, &why) == 0);
	CHECK(server_texture(&m, 204, 302, 656, 1, chunk, round_up(199424, 16384)) == 0);
	CHECK(memcmp(m.level[0], chunk, 199424) == 0);
	model_free(&m);
	/* a descriptor the layout does not serve stays nil (BGRA8 over caller memory: the old refusal) */
	CHECK(server_texture(&m, 80, 302, 656, 1, chunk, 302 * 656 * 4) == -1);
	free(chunk);
}

int
main(void)
{
	test_table();
	test_refusals();
	test_writer();
	test_nil();
	test_fuzz(6000);
	test_ca_conversion(3000);
	printf("rw_texlayout: ok (table 28 formats, refusals, writer = linear bytes for 7 sizes, 6000 fuzz layouts / %lu copies, "
	    "3000 CoreAnimation conversions, server texture model)\n", fuzz_copies);
	return 0;
}
