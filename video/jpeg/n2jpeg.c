/*
 * n2jpeg.c -- baseline JPEG encoder (ITU-T T.81 sequential DCT, Huffman, 8-bit) for NV12 pictures.
 *
 * The whole-picture arithmetic (level shift, forward DCT, quantisation and rounding) is n2vk_fdct8_quant() from
 * include/n2vk.h; this file gathers 8x8 blocks out of the two NV12 planes with edge replication on partial blocks,
 * scales the Annex K quantisation tables, and writes the marker segments and the entropy-coded data of T.81.
 *
 * Bit-exactness rules followed here and checked by jpeg/test:
 *   - Huffman codes are the T.81 Annex K tables, canonical codes, emitted MSB first;
 *   - coefficients are scanned in the zigzag order of T.81 figure A.6, DQT values are stored in the same zigzag order;
 *   - DC is differential per component, predictors reset to 0 after every restart marker;
 *   - every 0xFF produced in the entropy-coded data is followed by one 0x00 (T.81 B.1.1.5); restart markers are not
 *     stuffed;
 *   - the entropy-coded segment is padded to a byte boundary with 1-bits before a restart marker and before EOI.
 *
 * SPDX-License-Identifier: MIT
 */
#include "n2jpeg.h"

#include "n2vk.h"

#include <stdlib.h>
#include <string.h>

#define N2JPEG_MAX_SEGMENTS   16
#define N2JPEG_MAX_DIMENSION  65534u
#define N2JPEG_MAX_RESTART    65535u
#define N2JPEG_BLOCK_BOUND    416u    /* 64 symbols x 26 bits = 208 bytes, doubled for worst-case 0xFF stuffing */
#define N2JPEG_FIXED_HEADER   613u    /* SOI + JFIF + DQT + SOF0 + DHT + DRI + SOS, without caller segments */

/* -------------------------------------------------------------------------------------------------------------------------------
 * T.81 Annex K tables, natural order (index v * 8 + u).  A typo here is caught by the quality-50 test in jpeg/test, where
 * the IJG scaling is the identity.
 * ----------------------------------------------------------------------------------------------------------------------------- */

static const uint16_t k_quant_luma[64] = {
	16, 11, 10, 16,  24,  40,  51,  61,
	12, 12, 14, 19,  26,  58,  60,  55,
	14, 13, 16, 24,  40,  57,  69,  56,
	14, 17, 22, 29,  51,  87,  80,  62,
	18, 22, 37, 56,  68, 109, 103,  77,
	24, 35, 55, 64,  81, 104, 113,  92,
	49, 64, 78, 87, 103, 121, 120, 101,
	72, 92, 95, 98, 112, 100, 103,  99,
};

static const uint16_t k_quant_chroma[64] = {
	17, 18, 24, 47, 99, 99, 99, 99,
	18, 21, 26, 66, 99, 99, 99, 99,
	24, 26, 56, 99, 99, 99, 99, 99,
	47, 66, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
};

/* K.3: DC luminance */
static const uint8_t k_dc_luma_bits[16] = { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
static const uint8_t k_dc_luma_vals[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

/* K.4: AC luminance */
static const uint8_t k_ac_luma_bits[16] = { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
static const uint8_t k_ac_luma_vals[162] = {
	0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
	0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
	0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
	0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
	0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
	0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
	0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
	0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
	0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
	0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
	0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
	0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
	0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
	0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
	0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa,
};

/* K.5: DC chrominance */
static const uint8_t k_dc_chroma_bits[16] = { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
static const uint8_t k_dc_chroma_vals[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

/* K.6: AC chrominance */
static const uint8_t k_ac_chroma_bits[16] = { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
static const uint8_t k_ac_chroma_vals[162] = {
	0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
	0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
	0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
	0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
	0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
	0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
	0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
	0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
	0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
	0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
	0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
	0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
	0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
	0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
	0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
	0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
	0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
	0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
	0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa,
};

/* T.81 figure A.6: zigzag position -> natural index. */
static const uint8_t k_zigzag[64] = {
	 0,  1,  8, 16,  9,  2,  3, 10,
	17, 24, 32, 25, 18, 11,  4,  5,
	12, 19, 26, 33, 40, 48, 41, 34,
	27, 20, 13,  6,  7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36,
	29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46,
	53, 60, 61, 54, 47, 55, 62, 63,
};

struct n2huff {
	uint16_t code[256];
	uint8_t  len[256];
};

struct n2jpeg_enc {
	uint32_t width, height;
	uint32_t chroma_w, chroma_h;
	uint32_t mcus_x, mcus_y;
	size_t   mcus;
	int      quality;
	uint32_t restart_interval;
	uint32_t flags;

	uint16_t q_luma[64];            /* natural order */
	uint16_t q_chroma[64];

	struct n2huff dc[2];            /* [0] luma, [1] chroma */
	struct n2huff ac[2];

	uint8_t *segments;              /* fully formed marker segments, concatenated */
	size_t   segments_len;

	uint8_t *header;                /* SOI .. SOS, rebuilt by set_quality/set_restart_interval */
	size_t   header_len;
	size_t   header_cap;

	size_t   bound;

	int16_t *coefs;                 /* N2JPEG_F_KEEP_COEF: mcus * 6 * 64 */
	size_t   coef_count;
};

/* -------------------------------------------------------------------------------------------------------------------------------
 * Huffman tables: canonical codes from BITS/HUFFVAL (T.81 Annex C).
 * ----------------------------------------------------------------------------------------------------------------------------- */

static int
build_huffman(struct n2huff *h, const uint8_t bits[16], const uint8_t *vals, unsigned count)
{
	uint32_t code = 0;
	unsigned k = 0;
	unsigned l, i;

	memset(h, 0, sizeof(*h));
	for (l = 1; l <= 16; l++) {
		for (i = 0; i < bits[l - 1]; i++) {
			unsigned symbol;

			if (k >= count)
				return -1;
			symbol = vals[k++];
			if (code >= (1u << l))
				return -1;                              /* over-subscribed table */
			if (code == (1u << l) - 1u)
				return -1;                              /* the all-ones code is reserved (T.81 C.2) */
			h->code[symbol] = (uint16_t)code;
			h->len[symbol] = (uint8_t)l;
			code++;
		}
		if (code > (1u << l))
			return -1;
		code <<= 1;
	}
	return k == count ? 0 : -1;
}

/* IJG's quality scaling of a T.81 Annex K table (the de-facto meaning of "quality" everywhere, cjpeg included). */
static void
scale_table(const uint16_t base[64], int quality, uint16_t out[64])
{
	int scale;
	int i;

	if (quality < 50)
		scale = 5000 / quality;
	else
		scale = 200 - quality * 2;

	for (i = 0; i < 64; i++) {
		int value = (base[i] * scale + 50) / 100;

		if (value < 1)
			value = 1;
		if (value > 255)
			value = 255;
		out[i] = (uint16_t)value;
	}
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * Marker segments
 * ----------------------------------------------------------------------------------------------------------------------------- */

static uint8_t *
put_segment_header(uint8_t *p, uint8_t marker, size_t payload)
{
	*p++ = 0xff;
	*p++ = marker;
	*p++ = (uint8_t)((payload + 2) >> 8);
	*p++ = (uint8_t)((payload + 2) & 0xff);
	return p;
}

static int
build_header(struct n2jpeg_enc *e)
{
	uint8_t *p = e->header;
	int have_app0 = 0;
	uint32_t i, k;

	/* An APP0 supplied by the caller is a JFIF/JFXX header of its own: do not add a second one. */
	if (e->segments != NULL) {
		const uint8_t *s = e->segments;
		const uint8_t *end = e->segments + e->segments_len;

		while (s + 4 <= end) {
			if (s[1] == 0xe0)
				have_app0 = 1;
			s += 4 + (((size_t)s[2] << 8) | s[3]) - 2;
		}
	}

	*p++ = 0xff;
	*p++ = 0xd8;                                    /* SOI */

	if ((e->flags & N2JPEG_F_NO_JFIF) == 0 && !have_app0) {
		p = put_segment_header(p, 0xe0, 14);
		*p++ = 'J'; *p++ = 'F'; *p++ = 'I'; *p++ = 'F'; *p++ = 0x00;
		*p++ = 0x01; *p++ = 0x01;                 /* version 1.01 */
		*p++ = 0x00;                              /* units: aspect ratio only */
		*p++ = 0x00; *p++ = 0x01;                 /* X density 1 */
		*p++ = 0x00; *p++ = 0x01;                 /* Y density 1 */
		*p++ = 0x00; *p++ = 0x00;                 /* no thumbnail */
	}

	if (e->segments != NULL && e->segments_len != 0) {
		memcpy(p, e->segments, e->segments_len);
		p += e->segments_len;
	}

	/* DQT: two 8-bit tables, values in zigzag order. */
	p = put_segment_header(p, 0xdb, 2 * 65);
	for (i = 0; i < 2; i++) {
		const uint16_t *q = (i == 0) ? e->q_luma : e->q_chroma;

		*p++ = (uint8_t)i;
		for (k = 0; k < 64; k++)
			*p++ = (uint8_t)q[k_zigzag[k]];
	}

	/* SOF0: baseline, 8-bit, three components, luma 2x2, chroma 1x1. */
	p = put_segment_header(p, 0xc0, 15);
	*p++ = 8;
	*p++ = (uint8_t)(e->height >> 8);
	*p++ = (uint8_t)(e->height & 0xff);
	*p++ = (uint8_t)(e->width >> 8);
	*p++ = (uint8_t)(e->width & 0xff);
	*p++ = 3;
	*p++ = 1; *p++ = 0x22; *p++ = 0;
	*p++ = 2; *p++ = 0x11; *p++ = 1;
	*p++ = 3; *p++ = 0x11; *p++ = 1;

	/* DHT: the four Annex K tables in one segment. */
	p = put_segment_header(p, 0xc4, 4 * 17 + 12 + 162 + 12 + 162);
	{
		static const struct {
			uint8_t        tc_th;
			const uint8_t *bits;
			const uint8_t *vals;
			unsigned       count;
		} tables[4] = {
			{ 0x00, k_dc_luma_bits,   k_dc_luma_vals,   12  },
			{ 0x10, k_ac_luma_bits,   k_ac_luma_vals,   162 },
			{ 0x01, k_dc_chroma_bits, k_dc_chroma_vals, 12  },
			{ 0x11, k_ac_chroma_bits, k_ac_chroma_vals, 162 },
		};

		for (i = 0; i < 4; i++) {
			*p++ = tables[i].tc_th;
			memcpy(p, tables[i].bits, 16);
			p += 16;
			memcpy(p, tables[i].vals, tables[i].count);
			p += tables[i].count;
		}
	}

	if (e->restart_interval != 0) {
		p = put_segment_header(p, 0xdd, 2);
		*p++ = (uint8_t)(e->restart_interval >> 8);
		*p++ = (uint8_t)(e->restart_interval & 0xff);
	}

	/* SOS: three components, one Huffman table each, sequential DCT, full spectral selection. */
	p = put_segment_header(p, 0xda, 10);
	*p++ = 3;
	*p++ = 1; *p++ = 0x00;
	*p++ = 2; *p++ = 0x11;
	*p++ = 3; *p++ = 0x11;
	*p++ = 0;
	*p++ = 63;
	*p++ = 0;

	e->header_len = (size_t)(p - e->header);
	return e->header_len <= e->header_cap ? 0 : -1;
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * Output: byte sink with 0xFF stuffing and a bit accumulator.
 * ----------------------------------------------------------------------------------------------------------------------------- */

struct n2w {
	uint8_t *buf;
	size_t   cap;
	size_t   len;                   /* bytes the file needs, whether or not they fit */
	int      overflow;
};

static void
w_byte(struct n2w *w, unsigned value)
{
	if (w->len < w->cap)
		w->buf[w->len] = (uint8_t)value;
	else
		w->overflow = 1;
	w->len++;
}

static void
w_stuffed(struct n2w *w, unsigned value)
{
	w_byte(w, value & 0xffu);
	if ((value & 0xffu) == 0xffu)
		w_byte(w, 0x00);
}

static void
w_data(struct n2w *w, const uint8_t *p, size_t n)
{
	if (n == 0)
		return;
	if (w->len + n <= w->cap) {
		memcpy(w->buf + w->len, p, n);
	} else {
		w->overflow = 1;
		if (w->len < w->cap)
			memcpy(w->buf + w->len, p, w->cap - w->len);
	}
	w->len += n;
}

struct n2bw {
	struct n2w *w;
	uint64_t    acc;
	unsigned    nbits;
};

static void
bw_put(struct n2bw *b, uint32_t value, unsigned n)
{
	if (n == 0)
		return;
	b->acc = (b->acc << n) | (value & ((1u << n) - 1u));
	b->nbits += n;
	while (b->nbits >= 8) {
		unsigned byte = (unsigned)((b->acc >> (b->nbits - 8)) & 0xffu);

		w_stuffed(b->w, byte);
		b->nbits -= 8;
	}
	if (b->nbits != 0)
		b->acc &= (1ull << b->nbits) - 1ull;
	else
		b->acc = 0;
}

/* Pad the last byte with 1-bits (T.81 B.1.1.5 / F.1.2.3) and forget the accumulator. */
static void
bw_align(struct n2bw *b)
{
	if (b->nbits != 0) {
		unsigned pad = 8 - b->nbits;
		unsigned byte = (unsigned)((b->acc << pad) | ((1u << pad) - 1u));

		w_stuffed(b->w, byte & 0xffu);
		b->acc = 0;
		b->nbits = 0;
	}
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * Block coding
 * ----------------------------------------------------------------------------------------------------------------------------- */

static unsigned
bit_length(int32_t value)
{
	uint32_t magnitude = (value < 0) ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
	unsigned bits = 0;

	while (magnitude != 0) {
		bits++;
		magnitude >>= 1;
	}
	return bits;
}

static int
encode_block(struct n2bw *b, const int16_t *coef, int16_t *predictor,
             const struct n2huff *dc, const struct n2huff *ac)
{
	int32_t diff = (int32_t)coef[0] - (int32_t)*predictor;
	unsigned size;
	uint32_t amplitude;
	unsigned run = 0;
	unsigned k;

	*predictor = coef[0];
	size = bit_length(diff);
	if (size > 11)
		return -1;
	if (dc->len[size] == 0)
		return -1;
	bw_put(b, dc->code[size], dc->len[size]);
	amplitude = (diff < 0) ? (uint32_t)(diff + (1 << size) - 1) : (uint32_t)diff;
	bw_put(b, amplitude, size);

	for (k = 1; k < 64; k++) {
		int32_t value = coef[k_zigzag[k]];

		if (value == 0) {
			run++;
			continue;
		}
		while (run > 15) {
			if (ac->len[0xf0] == 0)
				return -1;
			bw_put(b, ac->code[0xf0], ac->len[0xf0]);
			run -= 16;
		}
		size = bit_length(value);
		if (size > 10)
			return -1;
		{
			unsigned symbol = (run << 4) | size;

			if (ac->len[symbol] == 0)
				return -1;
			bw_put(b, ac->code[symbol], ac->len[symbol]);
		}
		amplitude = (value < 0) ? (uint32_t)(value + (1 << size) - 1) : (uint32_t)value;
		bw_put(b, amplitude, size);
		run = 0;
	}
	if (run != 0) {
		if (ac->len[0x00] == 0)
			return -1;
		bw_put(b, ac->code[0x00], ac->len[0x00]);
	}
	return 0;
}

/* Gather an 8x8 block with edge replication: sample (x, y) of the plane maps to src[y * stride + x * step]. */
static void
gather8(uint8_t *dst, const uint8_t *src, size_t stride, unsigned step,
        uint32_t x0, uint32_t y0, uint32_t plane_w, uint32_t plane_h)
{
	unsigned y, x;

	for (y = 0; y < 8; y++) {
		uint32_t sy = y0 + y;

		if (sy >= plane_h)
			sy = plane_h - 1;
		for (x = 0; x < 8; x++) {
			uint32_t sx = x0 + x;

			if (sx >= plane_w)
				sx = plane_w - 1;
			dst[y * 8 + x] = src[(size_t)sy * stride + (size_t)sx * step];
		}
	}
}

static void
gather16(uint8_t *dst, const uint8_t *src, size_t stride,
         uint32_t x0, uint32_t y0, uint32_t plane_w, uint32_t plane_h)
{
	unsigned y, x;

	for (y = 0; y < 16; y++) {
		uint32_t sy = y0 + y;

		if (sy >= plane_h)
			sy = plane_h - 1;
		for (x = 0; x < 16; x++) {
			uint32_t sx = x0 + x;

			if (sx >= plane_w)
				sx = plane_w - 1;
			dst[y * 16 + x] = src[(size_t)sy * stride + sx];
		}
	}
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------------------------------------------------------------- */

int
n2jpeg_open(const n2jpeg_config *config, n2jpeg_enc **out)
{
	n2jpeg_enc *e;
	uint32_t count;
	size_t segs_len = 0;
	size_t coef_blocks;

	if (out == NULL)
		return N2JPEG_E_ARGUMENT;
	*out = NULL;
	if (config == NULL || config->size < sizeof(n2jpeg_config))
		return N2JPEG_E_ARGUMENT;
	if (config->width < 2 || config->height < 2 ||
	    config->width > (int32_t)N2JPEG_MAX_DIMENSION || config->height > (int32_t)N2JPEG_MAX_DIMENSION)
		return N2JPEG_E_ARGUMENT;
	if ((config->width & 1) != 0 || (config->height & 1) != 0)
		return N2JPEG_E_ARGUMENT;
	if (config->quality < 1 || config->quality > 100)
		return N2JPEG_E_ARGUMENT;
	if (config->restart_interval > N2JPEG_MAX_RESTART)
		return N2JPEG_E_ARGUMENT;
	if (config->flags & ~(uint32_t)(N2JPEG_F_KEEP_COEF | N2JPEG_F_NO_JFIF))
		return N2JPEG_E_ARGUMENT;
	if (config->segment_count > N2JPEG_MAX_SEGMENTS)
		return N2JPEG_E_ARGUMENT;
	if (config->segment_count != 0 && config->segments == NULL)
		return N2JPEG_E_ARGUMENT;

	for (count = 0; count < config->segment_count; count++) {
		const n2jpeg_segment *s = &config->segments[count];

		if (s->reserved != 0)
			return N2JPEG_E_ARGUMENT;
		if (s->marker != 0xfe && (s->marker < 0xe0 || s->marker > 0xef))
			return N2JPEG_E_ARGUMENT;
		if (s->size > 65533)
			return N2JPEG_E_ARGUMENT;
		if (s->size != 0 && s->data == NULL)
			return N2JPEG_E_ARGUMENT;
		segs_len += 4 + s->size;
	}

	e = (n2jpeg_enc *)calloc(1, sizeof(*e));
	if (e == NULL)
		return N2JPEG_E_MEMORY;

	e->width = (uint32_t)config->width;
	e->height = (uint32_t)config->height;
	e->chroma_w = e->width / 2;
	e->chroma_h = e->height / 2;
	e->mcus_x = (e->width + 15) / 16;
	e->mcus_y = (e->height + 15) / 16;
	e->mcus = (size_t)e->mcus_x * (size_t)e->mcus_y;
	e->quality = config->quality;
	e->restart_interval = config->restart_interval;
	e->flags = config->flags;
	e->header_cap = N2JPEG_FIXED_HEADER + segs_len + 16;

	e->header = (uint8_t *)malloc(e->header_cap);
	if (e->header == NULL)
		goto memory;
	if (segs_len != 0) {
		uint8_t *p;

		e->segments = (uint8_t *)malloc(segs_len);
		if (e->segments == NULL)
			goto memory;
		p = e->segments;
		for (count = 0; count < config->segment_count; count++) {
			const n2jpeg_segment *s = &config->segments[count];

			p = put_segment_header(p, s->marker, s->size);
			if (s->size != 0) {
				memcpy(p, s->data, s->size);
				p += s->size;
			}
		}
		e->segments_len = segs_len;
	}

	if (build_huffman(&e->dc[0], k_dc_luma_bits, k_dc_luma_vals, 12) != 0 ||
	    build_huffman(&e->ac[0], k_ac_luma_bits, k_ac_luma_vals, 162) != 0 ||
	    build_huffman(&e->dc[1], k_dc_chroma_bits, k_dc_chroma_vals, 12) != 0 ||
	    build_huffman(&e->ac[1], k_ac_chroma_bits, k_ac_chroma_vals, 162) != 0)
		goto state;

	scale_table(k_quant_luma, e->quality, e->q_luma);
	scale_table(k_quant_chroma, e->quality, e->q_chroma);
	if (build_header(e) != 0)
		goto state;

	if (e->mcus > (SIZE_MAX - e->header_len - 16) / (6u * N2JPEG_BLOCK_BOUND + 4u))
		goto state;
	e->bound = e->header_len + 16 + e->mcus * (6u * (size_t)N2JPEG_BLOCK_BOUND + 4u);

	if (e->flags & N2JPEG_F_KEEP_COEF) {
		coef_blocks = e->mcus * 6u;
		if (coef_blocks > SIZE_MAX / (64u * sizeof(int16_t)))
			goto state;
		e->coefs = (int16_t *)calloc(coef_blocks * 64u, sizeof(int16_t));
		if (e->coefs == NULL)
			goto memory;
		e->coef_count = coef_blocks;
	}

	*out = e;
	return N2JPEG_OK;

state:
	n2jpeg_close(e);
	return N2JPEG_E_STATE;
memory:
	n2jpeg_close(e);
	return N2JPEG_E_MEMORY;
}

int
n2jpeg_set_quality(n2jpeg_enc *e, int quality)
{
	if (e == NULL)
		return N2JPEG_E_ARGUMENT;
	if (quality < 1 || quality > 100)
		return N2JPEG_E_ARGUMENT;
	e->quality = quality;
	scale_table(k_quant_luma, quality, e->q_luma);
	scale_table(k_quant_chroma, quality, e->q_chroma);
	return build_header(e) == 0 ? N2JPEG_OK : N2JPEG_E_STATE;
}

int
n2jpeg_set_restart_interval(n2jpeg_enc *e, uint32_t mcus)
{
	if (e == NULL || mcus > N2JPEG_MAX_RESTART)
		return N2JPEG_E_ARGUMENT;
	e->restart_interval = mcus;
	return build_header(e) == 0 ? N2JPEG_OK : N2JPEG_E_STATE;
}

int
n2jpeg_encode(n2jpeg_enc *e, const n2jpeg_picture *picture, uint8_t *buf, size_t cap, size_t *out_size)
{
	struct n2w w;
	struct n2bw bw;
	int16_t predictor[3] = { 0, 0, 0 };
	uint32_t mx, my;
	size_t mcu_index = 0;
	size_t coef_index = 0;
	uint8_t luma16[256];
	uint8_t block8[64];
	int16_t luma_coef[256];
	int16_t chroma_coef[64];
	int status = N2JPEG_OK;

	if (out_size != NULL)
		*out_size = 0;
	if (e == NULL || picture == NULL || out_size == NULL)
		return N2JPEG_E_ARGUMENT;
	if (picture->size < sizeof(n2jpeg_picture))
		return N2JPEG_E_ARGUMENT;
	if (picture->y == NULL || picture->uv == NULL)
		return N2JPEG_E_ARGUMENT;
	if (picture->y_stride < e->width || picture->uv_stride < e->width)
		return N2JPEG_E_ARGUMENT;

	w.buf = buf;
	w.cap = (buf != NULL) ? cap : 0;
	w.len = 0;
	w.overflow = 0;
	bw.w = &w;
	bw.acc = 0;
	bw.nbits = 0;

	w_data(&w, e->header, e->header_len);

	for (my = 0; my < e->mcus_y; my++) {
		for (mx = 0; mx < e->mcus_x; mx++, mcu_index++) {
			uint32_t bx = mx * 16;
			uint32_t by = my * 16;
			uint32_t cbx = mx * 8;
			uint32_t cby = my * 8;

			if (e->restart_interval != 0 && mcu_index != 0 && (mcu_index % e->restart_interval) == 0) {
				unsigned rst = (unsigned)((mcu_index / e->restart_interval - 1) % 8);

				bw_align(&bw);
				w_byte(&w, 0xff);
				w_byte(&w, 0xd0 + rst);
				predictor[0] = predictor[1] = predictor[2] = 0;
			}

			gather16(luma16, picture->y, picture->y_stride, bx, by, e->width, e->height);
			if (n2vk_fdct8_quant(luma16, 16, 16, 16, e->q_luma, luma_coef) != N2VK_OK) {
				status = N2JPEG_E_BACKEND;
				goto done;
			}

			gather8(block8, picture->uv, picture->uv_stride, 2, cbx, cby, e->chroma_w, e->chroma_h);
			if (n2vk_fdct8_quant(block8, 8, 8, 8, e->q_chroma, chroma_coef) != N2VK_OK) {
				status = N2JPEG_E_BACKEND;
				goto done;
			}
			{
				int16_t cb_coef[64];

				memcpy(cb_coef, chroma_coef, sizeof(cb_coef));

				gather8(block8, picture->uv + 1, picture->uv_stride, 2, cbx, cby, e->chroma_w, e->chroma_h);
				if (n2vk_fdct8_quant(block8, 8, 8, 8, e->q_chroma, chroma_coef) != N2VK_OK) {
					status = N2JPEG_E_BACKEND;
					goto done;
				}

				if (e->coefs != NULL) {
					memcpy(e->coefs + coef_index, luma_coef, sizeof(luma_coef));
					memcpy(e->coefs + coef_index + 256, cb_coef, sizeof(cb_coef));
					memcpy(e->coefs + coef_index + 256 + 64, chroma_coef, sizeof(chroma_coef));
					coef_index += 6u * 64u;
				}

				if (encode_block(&bw, luma_coef, &predictor[0], &e->dc[0], &e->ac[0]) != 0 ||
				    encode_block(&bw, luma_coef + 64, &predictor[0], &e->dc[0], &e->ac[0]) != 0 ||
				    encode_block(&bw, luma_coef + 128, &predictor[0], &e->dc[0], &e->ac[0]) != 0 ||
				    encode_block(&bw, luma_coef + 192, &predictor[0], &e->dc[0], &e->ac[0]) != 0 ||
				    encode_block(&bw, cb_coef, &predictor[1], &e->dc[1], &e->ac[1]) != 0 ||
				    encode_block(&bw, chroma_coef, &predictor[2], &e->dc[1], &e->ac[1]) != 0) {
					status = N2JPEG_E_STATE;
					goto done;
				}
			}
		}
	}

	bw_align(&bw);
	w_byte(&w, 0xff);
	w_byte(&w, 0xd9);                               /* EOI */

done:
	*out_size = w.len;
	if (status != N2JPEG_OK)
		return status;
	if (buf == NULL)
		return N2JPEG_OK;
	if (w.overflow)
		return N2JPEG_E_TOO_SMALL;
	return N2JPEG_OK;
}

size_t
n2jpeg_bound(const n2jpeg_enc *e)
{
	return e != NULL ? e->bound : 0;
}

size_t
n2jpeg_header_size(const n2jpeg_enc *e)
{
	return e != NULL ? e->header_len : 0;
}

uint32_t
n2jpeg_mcu_count(const n2jpeg_enc *e)
{
	return e != NULL ? (uint32_t)e->mcus : 0;
}

void
n2jpeg_quant_tables(const n2jpeg_enc *e, uint16_t luma[64], uint16_t chroma[64])
{
	if (e == NULL)
		return;
	if (luma != NULL)
		memcpy(luma, e->q_luma, sizeof(e->q_luma));
	if (chroma != NULL)
		memcpy(chroma, e->q_chroma, sizeof(e->q_chroma));
}

const int16_t *
n2jpeg_last_coefficients(const n2jpeg_enc *e, size_t *count)
{
	if (e == NULL || e->coefs == NULL) {
		if (count != NULL)
			*count = 0;
		return NULL;
	}
	if (count != NULL)
		*count = e->coef_count;
	return e->coefs;
}

void
n2jpeg_close(n2jpeg_enc *e)
{
	if (e == NULL)
		return;
	free(e->coefs);
	free(e->header);
	free(e->segments);
	free(e);
}
