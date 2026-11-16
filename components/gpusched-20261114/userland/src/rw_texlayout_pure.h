/*
 * rw_texlayout_pure.h (y2s98): the memory layout this driver gives a block-compressed texture, as plain C.
 *
 * WHY IT EXISTS.  CoreAnimation and ImageIO do not hand an ASTC picture (an .atx / .ktx file: every PosterBoard gallery and
 * wallpaper snapshot, every SpringBoard application snapshot) to Metal through -replaceRegion:.  They ask the DEVICE for a
 * texture layout (-newTextureLayoutWithDescriptor:isHeapOrBufferBacked:, protocol MTLTextureLayout), copy the blocks into
 * "texture memory" with it, and the window server then makes the texture straight over that memory
 * (-newTextureWithBytesNoCopy:length:descriptor:deallocator:).  Read from the 20H392 cache:
 *   ImageIO   ASTCTwiddler::ASTCTwiddler (0x18bac8bd0)        -size -alignment, ATX writer and reader
 *             ASTCTwiddlerImpl::copyFromLinearDataImp         -initializeTextureMemory: -copyFromLinearBytes:... -finalizeTextureMemory:
 *             ASTCTwiddlerImpl::copyFromTextureDataImp        -copyFromTextureMemory:... (the software decoder's way back)
 *             ASTCTwiddler::getWatermark                      -watermark (16 bytes; stored in the file, compared on reading)
 *   QuartzCore CA::Render::CompressedImage::new_image_with_texture_data (0x18835864c), CA::OGL::MetalContext::update_texture
 *             (0x188483ffc: bytes = the image's data, length = its size rounded up to a page, deallocator nil)
 * A device that answers nil to both leaves every such picture without a texture: the layer is composited as nothing
 * (transparent), which is what the wallpaper gallery showed.
 *
 * THE LAYOUT.  Mali samples ASTC from ordinary rows of blocks, so the "texture memory" of this driver is the linear one:
 * level 0 first, each level its rows of blocks top to bottom, a row exactly ceil(width / block width) blocks of 16 bytes,
 * the levels packed one after the other with no padding.  For a one-level picture that is byte for byte the file's linear
 * encoder output, and it is also what ImageIO's ATX writer stored under the 'astc' (pre-twiddled) tag while this driver
 * answered nil (ATXWritePlugin::writeASTCData falls back to the linear bytes when the twiddler has no memory, and the header
 * keeps watermark 0) -- so the files already on the phone ARE in this layout, and the watermark of this layout is 0 so that
 * they stay valid.  A file twiddled on Apple hardware carries AGX's watermark and is refused by ImageIO, as it should be.
 *
 * Everything here is arithmetic and memcpy; the Objective-C in redwoodmetal.m only forwards.  Host test:
 * rw_texlayout_host_test.c (rw-texlayout-host-test.sh).
 */
#ifndef RW_TEXLAYOUT_PURE_H
#define RW_TEXLAYOUT_PURE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RWTL_MAX_LEVELS 15u            /* a 16384 texel side has 15 levels; Metal's limit on this device is 16384 */
#define RWTL_MAX_SIDE   16384u
#define RWTL_BLOCK_BYTES 16u           /* every ASTC block, whatever its footprint */

struct rwtl_level {
	uint32_t width, height;        /* texels */
	uint32_t cols, rows;           /* blocks */
	size_t row_bytes;              /* cols * 16 */
	size_t offset, size;           /* in the texture memory */
};

struct rwtl_layout {
	uint32_t format;               /* MTLPixelFormat */
	uint32_t block_w, block_h;     /* texels a block covers */
	uint32_t width, height, levels;
	size_t size;                   /* the whole texture memory */
	struct rwtl_level level[RWTL_MAX_LEVELS];
};

/*
 * The block footprint of a Metal pixel format this layout serves, or 0: the ASTC LDR family, sRGB (186..200) and linear
 * (204..218), exactly the rows of air_metal.c's format table (y2s34).  Metal has no 191/201..203/209.
 */
static inline int
rwtl_block_of(uint32_t format, uint32_t *bw, uint32_t *bh)
{
	static const uint8_t foot[15][2] = {
		{ 4, 4 }, { 5, 4 }, { 5, 5 }, { 6, 5 }, { 6, 6 }, { 0, 0 }, { 8, 5 }, { 8, 6 }, { 8, 8 },
		{ 10, 5 }, { 10, 6 }, { 10, 8 }, { 10, 10 }, { 12, 10 }, { 12, 12 },
	};
	uint32_t i;

	if (format >= 186 && format <= 200) {
		i = format - 186;
	} else if (format >= 204 && format <= 218) {
		i = format - 204;
	} else {
		return 0;
	}
	if (foot[i][0] == 0) {
		return 0;
	}
	*bw = foot[i][0];
	*bh = foot[i][1];
	return 1;
}

static inline uint32_t
rwtl_minify(uint32_t side, uint32_t level)
{
	uint32_t v = side >> level;

	return v != 0 ? v : 1;
}

/* How many levels a full chain of this size has (Metal's own rule: down to 1x1). */
static inline uint32_t
rwtl_full_chain(uint32_t width, uint32_t height)
{
	uint32_t side = width > height ? width : height, n = 1;

	while (side > 1) {
		side >>= 1;
		n++;
	}
	return n;
}

/*
 * The layout of a 2D texture of `format`, `width` x `height`, `levels` levels.  0, or -1 with *why naming what is not served
 * (and the layout untouched but for garbage: the caller does not use it).
 */
static inline int
rwtl_init(struct rwtl_layout *l, uint32_t format, uint32_t width, uint32_t height, uint32_t levels, const char **why)
{
	uint32_t bw = 0, bh = 0;
	size_t at = 0;

	if (!rwtl_block_of(format, &bw, &bh)) {
		*why = "not one of the ASTC LDR formats (186..200, 204..218)";
		return -1;
	}
	if (width == 0 || height == 0 || width > RWTL_MAX_SIDE || height > RWTL_MAX_SIDE) {
		*why = "a side of 0 or of more than 16384 texels";
		return -1;
	}
	if (levels == 0 || levels > RWTL_MAX_LEVELS || levels > rwtl_full_chain(width, height)) {
		*why = "no level, or more levels than the size has";
		return -1;
	}
	memset(l, 0, sizeof(*l));
	l->format = format;
	l->block_w = bw;
	l->block_h = bh;
	l->width = width;
	l->height = height;
	l->levels = levels;
	for (uint32_t i = 0; i < levels; i++) {
		struct rwtl_level *v = &l->level[i];

		v->width = rwtl_minify(width, i);
		v->height = rwtl_minify(height, i);
		v->cols = (v->width + bw - 1) / bw;
		v->rows = (v->height + bh - 1) / bh;
		v->row_bytes = (size_t)v->cols * RWTL_BLOCK_BYTES;
		v->offset = at;
		v->size = v->row_bytes * v->rows;          /* <= 4096 * 16 * 4096 = 2^28: no overflow in size_t */
		at += v->size;
	}
	l->size = at;
	return 0;
}

/*
 * A region of one level in blocks.  Metal's rule for a compressed region (and rwa_texture_write's): it starts on a block and
 * ends on one or at the level's edge.  0, or -1 with *why.
 */
static inline int
rwtl_region(const struct rwtl_layout *l, uint32_t level, uint64_t x, uint64_t y, uint64_t w, uint64_t h,
    uint32_t *col, uint32_t *row, uint32_t *cols, uint32_t *rows, const char **why)
{
	const struct rwtl_level *v;

	if (level >= l->levels) {
		*why = "a level the layout does not have";
		return -1;
	}
	v = &l->level[level];
	if (w == 0 || h == 0 || x >= v->width || y >= v->height || w > v->width - x || h > v->height - y) {
		*why = "a region that is empty or leaves the level";
		return -1;
	}
	if (x % l->block_w != 0 || y % l->block_h != 0 || (w % l->block_w != 0 && x + w != v->width) ||
	    (h % l->block_h != 0 && y + h != v->height)) {
		*why = "a region that does not start on a block and end on one or at the level's edge";
		return -1;
	}
	*col = (uint32_t)(x / l->block_w);
	*row = (uint32_t)(y / l->block_h);
	*cols = (uint32_t)((w + l->block_w - 1) / l->block_w);
	*rows = (uint32_t)((h + l->block_h - 1) / l->block_h);
	return 0;
}

/* The caller's row length: 0 is Metal's "tightly packed"; anything shorter than the region's blocks is refused. */
static inline int
rwtl_linear_row(uint32_t cols, size_t linear_row_bytes, size_t *row, const char **why)
{
	size_t least = (size_t)cols * RWTL_BLOCK_BYTES;

	if (linear_row_bytes == 0) {
		*row = least;
		return 0;
	}
	if (linear_row_bytes < least) {
		*why = "fewer bytes a row than the region's blocks";
		return -1;
	}
	*row = linear_row_bytes;
	return 0;
}

/*
 * -copyFromLinearBytes:linearOffset:linearBytesPerRow:linearBytesPerImage:toTextureMemory:textureSlice:textureLevel:textureRegion:
 * `linear + linear_offset` is the first block row of the region, `linear_row_bytes` from one block row to the next.
 */
static inline int
rwtl_copy_in(const struct rwtl_layout *l, void *memory, uint32_t level, uint64_t x, uint64_t y, uint64_t w, uint64_t h,
    const void *linear, size_t linear_offset, size_t linear_row_bytes, const char **why)
{
	uint32_t col, row, cols, rows;
	size_t lrow;

	if (memory == NULL || linear == NULL) {
		*why = "no memory";
		return -1;
	}
	if (rwtl_region(l, level, x, y, w, h, &col, &row, &cols, &rows, why) != 0 ||
	    rwtl_linear_row(cols, linear_row_bytes, &lrow, why) != 0) {
		return -1;
	}
	const struct rwtl_level *v = &l->level[level];
	const uint8_t *src = (const uint8_t *)linear + linear_offset;
	uint8_t *dst = (uint8_t *)memory + v->offset + (size_t)row * v->row_bytes + (size_t)col * RWTL_BLOCK_BYTES;

	for (uint32_t r = 0; r < rows; r++) {
		memcpy(dst + (size_t)r * v->row_bytes, src + (size_t)r * lrow, (size_t)cols * RWTL_BLOCK_BYTES);
	}
	return 0;
}

/* -copyFromTextureMemory:textureSlice:textureLevel:textureRegion:toLinearBytes:linearOffset:linearBytesPerRow:linearBytesPerImage: */
static inline int
rwtl_copy_out(const struct rwtl_layout *l, const void *memory, uint32_t level, uint64_t x, uint64_t y, uint64_t w, uint64_t h,
    void *linear, size_t linear_offset, size_t linear_row_bytes, const char **why)
{
	uint32_t col, row, cols, rows;
	size_t lrow;

	if (memory == NULL || linear == NULL) {
		*why = "no memory";
		return -1;
	}
	if (rwtl_region(l, level, x, y, w, h, &col, &row, &cols, &rows, why) != 0 ||
	    rwtl_linear_row(cols, linear_row_bytes, &lrow, why) != 0) {
		return -1;
	}
	const struct rwtl_level *v = &l->level[level];
	const uint8_t *src = (const uint8_t *)memory + v->offset + (size_t)row * v->row_bytes + (size_t)col * RWTL_BLOCK_BYTES;
	uint8_t *dst = (uint8_t *)linear + linear_offset;

	for (uint32_t r = 0; r < rows; r++) {
		memcpy(dst + (size_t)r * lrow, src + (size_t)r * v->row_bytes, (size_t)cols * RWTL_BLOCK_BYTES);
	}
	return 0;
}

/*
 * -newTextureWithBytesNoCopy:length:descriptor:deallocator: -- where level `level` of a texture memory of `length` bytes is,
 * as -replaceRegion: wants it (whole level, `*row_bytes` a block row).  NULL with *why when the memory cannot hold the layout.
 */
static inline const void *
rwtl_level_bytes(const struct rwtl_layout *l, const void *memory, size_t length, uint32_t level, uint32_t *width,
    uint32_t *height, size_t *row_bytes, const char **why)
{
	if (memory == NULL || length < l->size) {
		*why = "the memory is shorter than the layout";
		return NULL;
	}
	if (level >= l->levels) {
		*why = "a level the layout does not have";
		return NULL;
	}
	*width = l->level[level].width;
	*height = l->level[level].height;
	*row_bytes = l->level[level].row_bytes;
	return (const uint8_t *)memory + l->level[level].offset;
}

#endif
