/*
 * t_jpeg.c -- host tests for the n2jpeg baseline encoder (R1).
 *
 * The tests do not trust the encoder with its own answers:
 *   - the Annex K tables are written out literally here (natural order) and compared with what the encoder reports at
 *     quality 50, where the IJG scaling is the identity;
 *   - the quality-90 scaling is recomputed here from the formula;
 *   - the marker segments of the produced file are parsed by an independent reader, and the DQT/DHT payloads are
 *     compared with the literal tables and with independently generated canonical Huffman codes;
 *   - an independent Huffman decoder recovers every coefficient of every MCU with the tables *from the file*, and the
 *     result is compared with (a) n2jpeg_last_coefficients() and (b) coefficients recomputed here by gathering the
 *     blocks with this file's own edge-replication code and calling n2vk_ref_fdct8_quant();
 *   - restart markers, 0xFF stuffing, the buffer contract and the argument checks are exercised explicitly.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n2jpeg.h"
#include "n2vk.h"

static int checks;
static int failures;

#define CHECK(cond, ...) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

/* -------------------------------------------------------------------------------------------------------------------------------
 * Literal T.81 Annex K tables, natural order (v * 8 + u).
 * ----------------------------------------------------------------------------------------------------------------------------- */

static const uint16_t lit_q_luma[64] = {
	16, 11, 10, 16,  24,  40,  51,  61,
	12, 12, 14, 19,  26,  58,  60,  55,
	14, 13, 16, 24,  40,  57,  69,  56,
	14, 17, 22, 29,  51,  87,  80,  62,
	18, 22, 37, 56,  68, 109, 103,  77,
	24, 35, 55, 64,  81, 104, 113,  92,
	49, 64, 78, 87, 103, 121, 120, 101,
	72, 92, 95, 98, 112, 100, 103,  99,
};

static const uint16_t lit_q_chroma[64] = {
	17, 18, 24, 47, 99, 99, 99, 99,
	18, 21, 26, 66, 99, 99, 99, 99,
	24, 26, 56, 99, 99, 99, 99, 99,
	47, 66, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
};

struct huff_table {
	uint8_t bits[16];
	const uint8_t *vals;
	unsigned count;
};

static const uint8_t lit_dc_luma_bits[16] = { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
static const uint8_t lit_dc_luma_vals[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const uint8_t lit_ac_luma_bits[16] = { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
static const uint8_t lit_ac_luma_vals[162] = {
	0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
	0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
	0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
	0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
	0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
	0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa,
};
static const uint8_t lit_dc_chroma_bits[16] = { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
static const uint8_t lit_dc_chroma_vals[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const uint8_t lit_ac_chroma_bits[16] = { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
static const uint8_t lit_ac_chroma_vals[162] = {
	0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
	0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
	0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
	0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
	0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
	0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
	0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
	0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
	0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa,
};

/* T.81 figure A.6, written out independently of the encoder. */
static const uint8_t lit_zigzag[64] = {
	 0,  1,  8, 16,  9,  2,  3, 10,
	17, 24, 32, 25, 18, 11,  4,  5,
	12, 19, 26, 33, 40, 48, 41, 34,
	27, 20, 13,  6,  7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36,
	29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46,
	53, 60, 61, 54, 47, 55, 62, 63,
};

/* -------------------------------------------------------------------------------------------------------------------------------
 * A deterministic test picture (not the encoder's gather code, not WP09's generator).
 * ----------------------------------------------------------------------------------------------------------------------------- */

struct pic {
	uint32_t w, h;
	uint8_t *y;
	uint8_t *uv;
};

static void
pic_make(struct pic *p, uint32_t w, uint32_t h)
{
	uint32_t x, y, cw = w / 2, ch = h / 2;

	p->w = w;
	p->h = h;
	p->y = (uint8_t *)malloc((size_t)w * h);
	p->uv = (uint8_t *)malloc((size_t)w * h / 2);
	if (p->y == NULL || p->uv == NULL) {
		printf("FAIL test picture allocation\n");
		exit(2);
	}
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			int v = (int)((x * 7 + y * 3 + ((x ^ y) & 15) * 5) & 255);

			if (x >= w / 4 && x < w / 4 + 9 && y >= h / 5 && y < h / 5 + 7)
				v = 235;                        /* a hard edge */
			if (x >= 3 * w / 5 && x < 3 * w / 5 + 5 && y >= h / 2 && y < h / 2 + 5)
				v = 16;
			p->y[(size_t)y * w + x] = (uint8_t)v;
		}
	}
	for (y = 0; y < ch; y++) {
		for (x = 0; x < cw; x++) {
			p->uv[(size_t)y * w + 2 * x] = (uint8_t)(128 + ((x * 5 - y * 3 + 64) & 31) - 16);
			p->uv[(size_t)y * w + 2 * x + 1] = (uint8_t)(128 - ((x * 2 + y * 7 + 64) & 31) + 16);
		}
	}
}

static void
pic_free(struct pic *p)
{
	free(p->y);
	free(p->uv);
	p->y = p->uv = NULL;
}

static void
pic_view(const struct pic *p, n2jpeg_picture *v)
{
	memset(v, 0, sizeof(*v));
	v->size = (uint32_t)sizeof(*v);
	v->y = p->y;
	v->y_stride = p->w;
	v->uv = p->uv;
	v->uv_stride = p->w;
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * Independent JPEG parser and Huffman decoder.
 * ----------------------------------------------------------------------------------------------------------------------------- */

#define DEC_MAX_BLOCKS 4096

struct jdec {
	const uint8_t *data;
	size_t         size;
	size_t         pos;

	int            width, height;
	int            components;
	uint8_t        comp_id[4];
	uint8_t        comp_h[4], comp_v[4], comp_tq[4];
	uint16_t       q[4][64];                /* zigzag order, as stored */
	struct huff_table ht[4];                /* index (tc << 1) | th */
	int            have_dht[4];
	int            have_dqt[4];
	int            restart_interval;
	int            saw_dri;
	int            saw_sof;
	int            saw_sos;
	int            jfif;
	size_t         entropy_start;
	size_t         entropy_end;

	/* bit reader */
	size_t         bp;
	uint64_t       acc;
	int            nbits;

	/* decoded coefficients: MCUs * 6 blocks * 64, natural order */
	int16_t       *coef;
	size_t         blocks;
	size_t         max_blocks;
	int            error;
};

static uint16_t
rd16(const uint8_t *p)
{
	return (uint16_t)(((unsigned)p[0] << 8) | p[1]);
}

static int
dec_parse(struct jdec *d, const uint8_t *data, size_t size)
{
	size_t i = 2;

	memset(d, 0, sizeof(*d));
	d->data = data;
	d->size = size;
	d->max_blocks = DEC_MAX_BLOCKS;
	d->coef = (int16_t *)calloc(d->max_blocks * 64, sizeof(int16_t));
	if (d->coef == NULL)
		return -1;
	if (size < 4 || data[0] != 0xff || data[1] != 0xd8) {
		d->error = 1;
		return -1;
	}
	while (i + 4 <= size) {
		unsigned marker;

		if (data[i] != 0xff) {
			d->error = 1;
			return -1;
		}
		marker = data[i + 1];
		if (marker == 0xd9) {                   /* EOI */
			d->pos = i;
			if (d->saw_sos)
				d->entropy_end = i;
			return 0;
		}
		if (marker == 0xd8 || (marker >= 0xd0 && marker <= 0xd7) || marker == 0x01) {
			i += 2;                          /* standalone markers */
			continue;
		}
		{
			size_t length = rd16(data + i + 2);
			const uint8_t *p = data + i + 4;
			size_t payload = length - 2;

			if (length < 2 || i + 2 + length > size) {
				d->error = 1;
				return -1;
			}
			switch (marker) {
			case 0xe0:
				if (payload >= 5 && memcmp(p, "JFIF\0", 5) == 0)
					d->jfif = 1;
				break;
			case 0xdb: {                     /* DQT */
				size_t k = 0;

				while (k + 65 <= payload) {
					unsigned pq = p[k] >> 4, tq = p[k] & 15;
					unsigned j;

					if (pq != 0 || tq > 3) {
						d->error = 1;
						return -1;
					}
					for (j = 0; j < 64; j++)
						d->q[tq][j] = p[k + 1 + j];
					d->have_dqt[tq] = 1;
					k += 65;
				}
				break;
			}
			case 0xc4: {                     /* DHT */
				size_t k = 0;

				while (k + 17 <= payload) {
					unsigned tc = p[k] >> 4, th = p[k] & 15;
					unsigned n = 0, j;
					int idx = (int)((tc << 1) | th);

					if (tc > 1 || th > 1 || idx > 3) {
						d->error = 1;
						return -1;
					}
					memcpy(d->ht[idx].bits, p + k + 1, 16);
					for (j = 0; j < 16; j++)
						n += d->ht[idx].bits[j];
					if (n > 256 || k + 17 + n > payload) {
						d->error = 1;
						return -1;
					}
					d->ht[idx].vals = p + k + 17;
					d->ht[idx].count = n;
					d->have_dht[idx] = 1;
					k += 17 + n;
				}
				break;
			}
			case 0xc0: {                     /* SOF0 */
				unsigned nf;

				if (payload < 6 || p[0] != 8) {
					d->error = 1;
					return -1;
				}
				d->height = rd16(p + 1);
				d->width = rd16(p + 3);
				nf = p[5];
				if (nf < 1 || nf > 4 || payload < 6 + 3 * (size_t)nf) {
					d->error = 1;
					return -1;
				}
				d->components = (int)nf;
				for (unsigned c = 0; c < nf; c++) {
					d->comp_id[c] = p[6 + 3 * c];
					d->comp_h[c] = p[7 + 3 * c] >> 4;
					d->comp_v[c] = p[7 + 3 * c] & 15;
					d->comp_tq[c] = p[8 + 3 * c];
				}
				d->saw_sof = 1;
				break;
			}
			case 0xdd:
				d->restart_interval = rd16(p);
				d->saw_dri = 1;
				break;
			case 0xda:                      /* SOS */
				if (payload < 1 + 2 * (size_t)d->components + 3) {
					d->error = 1;
					return -1;
				}
				d->saw_sos = 1;
				d->entropy_start = i + 2 + length;
				d->pos = d->entropy_start;
				{
					/* The first FF D9 after SOS is EOI: every other 0xFF in the entropy-coded
					 * data is followed by 0x00 or is an RSTn marker. */
					size_t j = d->entropy_start;

					while (j + 1 < size) {
						if (data[j] == 0xff && data[j + 1] == 0xd9) {
							d->entropy_end = j;
							break;
						}
						j++;
					}
					if (d->entropy_end == 0) {
						d->error = 1;
						return -1;
					}
				}
				return 0;
			default:
				break;
			}
			i += 2 + length;
		}
	}
	d->error = 1;
	return -1;
}

static void
dec_free(struct jdec *d)
{
	free(d->coef);
	d->coef = NULL;
}

/* ---- entropy bit reader with 0xFF unstuffing ---- */

static int
dec_next_byte(struct jdec *d)
{
	const uint8_t *p = d->data;

	if (d->bp >= d->entropy_end)
		return -1;
	if (p[d->bp] == 0xff) {
		if (d->bp + 1 >= d->entropy_end)
			return -1;
		if (p[d->bp + 1] != 0x00)
			return -1;               /* a marker inside a block: handled by the caller at boundaries only */
		d->bp += 2;
		return 0xff;
	}
	return p[d->bp++];
}

static int
dec_get_bit(struct jdec *d)
{
	int byte;

	if (d->nbits == 0) {
		byte = dec_next_byte(d);
		if (byte < 0) {
			d->error = 1;
			return 0;
		}
		d->acc = (uint64_t)byte;
		d->nbits = 8;
	}
	d->nbits--;
	return (int)((d->acc >> d->nbits) & 1u);
}

static uint32_t
dec_get_bits(struct jdec *d, int n)
{
	uint32_t v = 0;

	while (n-- > 0)
		v = (v << 1) | (uint32_t)dec_get_bit(d);
	return v;
}

/* Discard the padding bits before a marker; every one of them must be 1 (T.81 B.1.1.5 / F.1.2.3). */
static int
dec_align_ones(struct jdec *d)
{
	while (d->nbits > 0) {
		d->nbits--;
		if (((d->acc >> d->nbits) & 1u) != 1u)
			return -1;
	}
	return 0;
}

/* Read FF Dn at the current byte position (no unstuffing). */
static int
dec_expect_rst(struct jdec *d, int n)
{
	if (d->bp + 2 > d->entropy_end)
		return -1;
	if (d->data[d->bp] != 0xff || d->data[d->bp + 1] != 0xd0 + n)
		return -1;
	d->bp += 2;
	return 0;
}

static int
dec_huff(struct jdec *d, int index)
{
	const struct huff_table *h = &d->ht[index];
	uint32_t code = 0;
	int first = 0, k = 0;
	int l;

	if (!d->have_dht[index])
		return -1;
	for (l = 1; l <= 16; l++) {
		int count = h->bits[l - 1];
		int offset;

		code = (code << 1) | (uint32_t)dec_get_bit(d);
		offset = (int)code - first;
		if (offset >= 0 && offset < count) {
			if ((unsigned)(k + offset) >= h->count)
				return -1;
			return h->vals[k + offset];
		}
		k += count;
		first = (first + count) << 1;
	}
	return -1;
}

/* Reconstruct one block.  Returns 0, or -1 on a malformed stream. */
static int
dec_block(struct jdec *d, int component, int16_t *coef, int32_t *predictor)
{
	int dc_index = (component == 0) ? 0 : 1;
	int ac_index = (component == 0) ? 2 : 3;   /* (tc << 1) | th: 2 = AC luma, 3 = AC chroma */
	int s = dec_huff(d, dc_index);
	int32_t diff;

	if (s < 0 || s > 11)
		return -1;
	diff = (s == 0) ? 0 : (int32_t)dec_get_bits(d, s);
	if (s != 0 && diff < (1 << (s - 1)))
		diff -= (1 << s) - 1;
	*predictor += diff;
	coef[0] = (int16_t)*predictor;

	{
		int k = 1;

		while (k < 64) {
			int rs = dec_huff(d, ac_index);
			int r, size;
			int32_t value;

			if (rs < 0)
				return -1;
			r = rs >> 4;
			size = rs & 15;
			if (size == 0) {
				if (r == 15) {
					k += 16;
					continue;
				}
				break;                  /* EOB */
			}
			k += r;
			if (k > 63)
				return -1;
			value = (int32_t)dec_get_bits(d, size);
			if (value < (1 << (size - 1)))
				value -= (1 << size) - 1;
			coef[lit_zigzag[k]] = (int16_t)value;
			k++;
		}
	}
	return 0;
}

/* Decode every MCU; d->coef receives 6 blocks per MCU in MCU order, natural order inside each block. */
static int
dec_scan(struct jdec *d)
{
	int mcu_w, mcu_h, mx, my, expected_rst = 0;
	int32_t predictor[3] = { 0, 0, 0 };
	size_t block = 0;

	if (!d->saw_sof || !d->saw_sos || !d->have_dht[0] || !d->have_dht[1] ||
	    !d->have_dht[2] || !d->have_dht[3])
		return -1;
	if (d->components != 3 || d->comp_h[0] != 2 || d->comp_v[0] != 2 ||
	    d->comp_h[1] != 1 || d->comp_v[1] != 1 || d->comp_h[2] != 1 || d->comp_v[2] != 1)
		return -1;
	if (!d->have_dqt[d->comp_tq[0]] || !d->have_dqt[d->comp_tq[1]] || !d->have_dqt[d->comp_tq[2]])
		return -1;

	mcu_w = (d->width + 15) / 16;
	mcu_h = (d->height + 15) / 16;
	d->bp = d->entropy_start;
	d->acc = 0;
	d->nbits = 0;
	d->blocks = 0;

	for (my = 0; my < mcu_h; my++) {
		for (mx = 0; mx < mcu_w; mx++) {
			size_t base = block;
			int b;

			if (d->restart_interval != 0 && (my * mcu_w + mx) != 0 &&
			    ((my * mcu_w + mx) % d->restart_interval) == 0) {
				if (dec_align_ones(d) != 0)
					return -1;
				if (dec_expect_rst(d, expected_rst) != 0)
					return -1;
				expected_rst = (expected_rst + 1) % 8;
				predictor[0] = predictor[1] = predictor[2] = 0;
				block = base;
			}
			if (block + 6 > d->max_blocks)
				return -1;
			for (b = 0; b < 6; b++) {
				int component = (b < 4) ? 0 : (b == 4 ? 1 : 2);

				if (dec_block(d, component, d->coef + (block + (size_t)b) * 64, &predictor[component]) != 0)
					return -1;
			}
			block += 6;
		}
	}
	d->blocks = block;
	if (dec_align_ones(d) != 0)
		return -1;
	return 0;
}

/* -------------------------------------------------------------------------------------------------------------------------------
 * Tests
 * ----------------------------------------------------------------------------------------------------------------------------- */

static void
test_tables(void)
{
	n2jpeg_config cfg;
	n2jpeg_enc *e = NULL;
	uint16_t luma[64], chroma[64];
	int i;

	memset(&cfg, 0, sizeof(cfg));
	cfg.size = (uint32_t)sizeof(cfg);
	cfg.width = 64;
	cfg.height = 48;
	cfg.quality = 50;
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_OK && e != NULL, "open at quality 50");
	if (e == NULL)
		return;
	n2jpeg_quant_tables(e, luma, chroma);
	CHECK(memcmp(luma, lit_q_luma, sizeof(luma)) == 0, "quality 50 luma table is Annex K.1");
	CHECK(memcmp(chroma, lit_q_chroma, sizeof(chroma)) == 0, "quality 50 chroma table is Annex K.2");
	for (i = 0; i < 64; i++) {
		if (luma[i] != lit_q_luma[i]) {
			CHECK(0, "luma[%d] = %u, table says %u", i, luma[i], lit_q_luma[i]);
			break;
		}
	}

	/* Quality 90: independent IJG scaling. */
	CHECK(n2jpeg_set_quality(e, 90) == N2JPEG_OK, "set_quality(90)");
	n2jpeg_quant_tables(e, luma, chroma);
	{
		int ok = 1;

		for (i = 0; i < 64; i++) {
			int expect_l = (lit_q_luma[i] * 20 + 50) / 100;
			int expect_c = (lit_q_chroma[i] * 20 + 50) / 100;

			if (expect_l < 1) expect_l = 1;
			if (expect_l > 255) expect_l = 255;
			if (expect_c < 1) expect_c = 1;
			if (expect_c > 255) expect_c = 255;
			if (luma[i] != expect_l || chroma[i] != expect_c)
				ok = 0;
		}
		CHECK(ok, "quality 90 scaling matches (base * 20 + 50) / 100");
	}
	CHECK(n2jpeg_set_quality(e, 1) == N2JPEG_OK, "set_quality(1)");
	n2jpeg_quant_tables(e, luma, chroma);
	{
		int ok = 1;

		for (i = 0; i < 64; i++) {
			int expect_l = (lit_q_luma[i] * 5000 + 50) / 100;

			if (expect_l < 1) expect_l = 1;
			if (expect_l > 255) expect_l = 255;
			if (luma[i] != expect_l)
				ok = 0;
		}
		CHECK(ok, "quality 1 scaling matches the 5000/quality branch");
	}
	CHECK(n2jpeg_set_quality(e, 0) == N2JPEG_E_ARGUMENT, "quality 0 rejected");
	CHECK(n2jpeg_set_quality(e, 101) == N2JPEG_E_ARGUMENT, "quality 101 rejected");
	n2jpeg_close(e);
}

static void
test_markers(const struct pic *p)
{
	n2jpeg_config cfg;
	n2jpeg_enc *e = NULL;
	n2jpeg_picture view;
	uint8_t *out;
	size_t size = 0, capacity;
	struct jdec dec;
	int i;

	memset(&cfg, 0, sizeof(cfg));
	cfg.size = (uint32_t)sizeof(cfg);
	cfg.width = (int32_t)p->w;
	cfg.height = (int32_t)p->h;
	cfg.quality = 90;
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_OK && e != NULL, "open for the marker test");
	if (e == NULL)
		return;
	capacity = n2jpeg_bound(e);
	out = (uint8_t *)malloc(capacity);
	pic_view(p, &view);
	CHECK(n2jpeg_encode(e, &view, out, capacity, &size) == N2JPEG_OK, "encode");
	CHECK(size > 100 && size == n2jpeg_header_size(e) + (size - n2jpeg_header_size(e)), "size sanity");

	CHECK(dec_parse(&dec, out, size) == 0 && dec.error == 0, "independent parser accepts the file");
	if (!dec.error) {
		CHECK(dec.saw_sof && dec.saw_sos, "SOF0 and SOS present");
		CHECK(dec.width == (int)p->w && dec.height == (int)p->h, "SOF0 dimensions %dx%d", dec.width, dec.height);
		CHECK(dec.components == 3, "three components");
		CHECK(dec.comp_h[0] == 2 && dec.comp_v[0] == 2 && dec.comp_h[1] == 1 && dec.comp_v[1] == 1 &&
		      dec.comp_h[2] == 1 && dec.comp_v[2] == 1, "4:2:0 sampling factors");
		CHECK(dec.comp_tq[0] == 0 && dec.comp_tq[1] == 1 && dec.comp_tq[2] == 1, "quant table selectors");
		CHECK(dec.jfif == 1, "JFIF APP0 present");
		CHECK(dec.saw_dri == 0, "no DRI without restart markers");

		/* DHT payloads against the literal tables. */
		{
			struct huff_table expect[4];

			memset(expect, 0, sizeof(expect));
			memcpy(expect[0].bits, lit_dc_luma_bits, 16);
			expect[0].vals = lit_dc_luma_vals;
			expect[0].count = 12;
			memcpy(expect[1].bits, lit_dc_chroma_bits, 16);
			expect[1].vals = lit_dc_chroma_vals;
			expect[1].count = 12;
			memcpy(expect[2].bits, lit_ac_luma_bits, 16);
			expect[2].vals = lit_ac_luma_vals;
			expect[2].count = 162;
			memcpy(expect[3].bits, lit_ac_chroma_bits, 16);
			expect[3].vals = lit_ac_chroma_vals;
			expect[3].count = 162;

			for (i = 0; i < 4; i++) {
				int ok = dec.have_dht[i] && dec.ht[i].count == expect[i].count &&
				         memcmp(dec.ht[i].bits, expect[i].bits, 16) == 0 &&
				         memcmp(dec.ht[i].vals, expect[i].vals, expect[i].count) == 0;

				CHECK(ok, "DHT table %d is the Annex K table", i);
			}
		}

		/* DQT payloads: file stores zigzag, the encoder reports natural. */
		{
			uint16_t luma[64], chroma[64];
			int ok = 1;

			n2jpeg_quant_tables(e, luma, chroma);
			for (i = 0; i < 64; i++) {
				if (dec.q[0][i] != luma[lit_zigzag[i]])
					ok = 0;
				if (dec.q[1][i] != chroma[lit_zigzag[i]])
					ok = 0;
			}
			CHECK(dec.have_dqt[0] && dec.have_dqt[1] && ok, "DQT values are the natural tables in zigzag order");
		}
	}
	dec_free(&dec);
	free(out);
	n2jpeg_close(e);
}

static void
test_headers_equal_cjpeg_layout(void)
{
	/* Every value of the DQT/DHT/SOF0/SOS is already compared with literal tables above.  This test only pins the
	 * segment order and the absence of anything after EOI. */
	n2jpeg_config cfg;
	n2jpeg_enc *e = NULL;
	n2jpeg_picture view;
	struct pic p;
	uint8_t *out;
	size_t size = 0, capacity;
	size_t i;
	int order[16], n = 0;

	pic_make(&p, 48, 32);
	memset(&cfg, 0, sizeof(cfg));
	cfg.size = (uint32_t)sizeof(cfg);
	cfg.width = (int32_t)p.w;
	cfg.height = (int32_t)p.h;
	cfg.quality = 75;
	cfg.restart_interval = 3;
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_OK && e != NULL, "open for segment order");
	if (e == NULL) {
		pic_free(&p);
		return;
	}
	capacity = n2jpeg_bound(e);
	out = (uint8_t *)malloc(capacity);
	pic_view(&p, &view);
	CHECK(n2jpeg_encode(e, &view, out, capacity, &size) == N2JPEG_OK, "encode with restarts");
	CHECK(out[0] == 0xff && out[1] == 0xd8, "starts with SOI");
	CHECK(out[size - 2] == 0xff && out[size - 1] == 0xd9, "ends with EOI");
	i = 2;
	while (i + 4 <= size && n < 16) {
		unsigned marker = out[i + 1];
		size_t length = rd16(out + i + 2);

		if (marker == 0xda) {
			order[n++] = (int)marker;
			break;
		}
		order[n++] = (int)marker;
		i += 2 + length;
	}
	CHECK(n >= 6 && order[0] == 0xe0 && order[1] == 0xdb && order[2] == 0xc0 && order[3] == 0xc4 &&
	      order[4] == 0xdd && order[5] == 0xda, "segment order APP0 DQT SOF0 DHT DRI SOS");
	CHECK(n2jpeg_set_restart_interval(e, 0) == N2JPEG_OK, "restart interval back to 0");
	CHECK(n2jpeg_encode(e, &view, out, capacity, &size) == N2JPEG_OK, "encode without restarts");
	{
		int saw_dri = 0;

		i = 2;
		while (i + 4 <= size) {
			unsigned marker = out[i + 1];
			size_t length = rd16(out + i + 2);

			if (marker == 0xdd)
				saw_dri = 1;
			if (marker == 0xda)
				break;
			i += 2 + length;
		}
		CHECK(saw_dri == 0, "DRI disappears when restart_interval is 0");
	}
	free(out);
	n2jpeg_close(e);
	pic_free(&p);
}

/* Independent coefficients: this test's own gather + n2vk_ref_fdct8_quant. */
static int16_t *
expected_coefficients(const struct pic *p, int quality, size_t *blocks_out)
{
	n2jpeg_config cfg;
	n2jpeg_enc *e = NULL;
	uint16_t ql[64], qc[64];
	uint32_t mcu_w = (p->w + 15) / 16, mcu_h = (p->h + 15) / 16;
	uint32_t mx, my, bx, by, x, y;
	size_t blocks = (size_t)mcu_w * mcu_h * 6;
	int16_t *coef = (int16_t *)calloc(blocks * 64, sizeof(int16_t));
	uint8_t blk[64];
	size_t index = 0;

	if (coef == NULL)
		return NULL;
	memset(&cfg, 0, sizeof(cfg));
	cfg.size = (uint32_t)sizeof(cfg);
	cfg.width = (int32_t)p->w;
	cfg.height = (int32_t)p->h;
	cfg.quality = quality;
	if (n2jpeg_open(&cfg, &e) != N2JPEG_OK) {
		free(coef);
		return NULL;
	}
	n2jpeg_quant_tables(e, ql, qc);
	n2jpeg_close(e);

	for (my = 0; my < mcu_h; my++) {
		for (mx = 0; mx < mcu_w; mx++) {
			for (by = 0; by < 2; by++) {
				for (bx = 0; bx < 2; bx++) {
					for (y = 0; y < 8; y++) {
						uint32_t sy = my * 16 + by * 8 + y;

						if (sy >= p->h)
							sy = p->h - 1;
						for (x = 0; x < 8; x++) {
							uint32_t sx = mx * 16 + bx * 8 + x;

							if (sx >= p->w)
								sx = p->w - 1;
							blk[y * 8 + x] = p->y[(size_t)sy * p->w + sx];
						}
					}
					if (n2vk_ref_fdct8_quant(blk, 8, 8, 8, ql, coef + index * 64) != N2VK_OK)
						goto fail;
					index++;
				}
			}
			for (int c = 0; c < 2; c++) {
				uint32_t cw = p->w / 2, ch = p->h / 2;

				for (y = 0; y < 8; y++) {
					uint32_t sy = my * 8 + y;

					if (sy >= ch)
						sy = ch - 1;
					for (x = 0; x < 8; x++) {
						uint32_t sx = mx * 8 + x;

						if (sx >= cw)
							sx = cw - 1;
						blk[y * 8 + x] = p->uv[(size_t)sy * p->w + 2 * sx + c];
					}
				}
				if (n2vk_ref_fdct8_quant(blk, 8, 8, 8, qc, coef + index * 64) != N2VK_OK)
					goto fail;
				index++;
			}
		}
	}
	*blocks_out = blocks;
	return coef;
fail:
	free(coef);
	return NULL;
}

static void
test_coefficients_and_entropy(const struct pic *p, int quality)
{
	n2jpeg_config cfg;
	n2jpeg_enc *e = NULL;
	n2jpeg_picture view;
	uint8_t *out;
	size_t size = 0, capacity, stored_blocks = 0, expect_blocks = 0;
	const int16_t *stored;
	int16_t *expect;
	struct jdec dec;
	size_t i;
	int diff_runs = 0;

	memset(&cfg, 0, sizeof(cfg));
	cfg.size = (uint32_t)sizeof(cfg);
	cfg.width = (int32_t)p->w;
	cfg.height = (int32_t)p->h;
	cfg.quality = quality;
	cfg.flags = N2JPEG_F_KEEP_COEF;
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_OK && e != NULL, "open with KEEP_COEF (q%d)", quality);
	if (e == NULL)
		return;
	capacity = n2jpeg_bound(e);
	out = (uint8_t *)malloc(capacity);
	pic_view(p, &view);
	CHECK(n2jpeg_encode(e, &view, out, capacity, &size) == N2JPEG_OK, "encode (q%d)", quality);
	stored = n2jpeg_last_coefficients(e, &stored_blocks);
	CHECK(stored != NULL && stored_blocks == (size_t)((p->w + 15) / 16) * ((p->h + 15) / 16) * 6,
	      "KEEP_COEF keeps %zu blocks", stored_blocks);

	expect = expected_coefficients(p, quality, &expect_blocks);
	CHECK(expect != NULL && expect_blocks == stored_blocks, "independent coefficient count");
	if (expect != NULL && stored != NULL) {
		size_t bad = 0, first = 0;

		for (i = 0; i < stored_blocks * 64; i++) {
			if (expect[i] != stored[i]) {
				if (bad == 0)
					first = i;
				bad++;
			}
		}
		CHECK(bad == 0, "%zu of %zu coefficients differ from n2vk_ref_fdct8_quant (first at %zu: %d vs %d)",
		      bad, stored_blocks * 64, first, expect[first], stored[first]);
	}

	/* Independent entropy decode: the file's own tables must reproduce the same coefficients. */
	CHECK(dec_parse(&dec, out, size) == 0 && !dec.error, "parser accepts q%d file", quality);
	if (!dec.error) {
		dec.max_blocks = 8192;
		free(dec.coef);
		dec.coef = (int16_t *)calloc(dec.max_blocks * 64, sizeof(int16_t));
		CHECK(dec.coef != NULL, "decoder allocation");
		CHECK(dec_scan(&dec) == 0 && !dec.error, "independent Huffman decode of every MCU");
		CHECK(dec.blocks == stored_blocks, "decoded %zu blocks, encoder says %zu", dec.blocks, stored_blocks);
		if (dec.blocks == stored_blocks && stored != NULL) {
			size_t bad = 0;

			for (i = 0; i < stored_blocks * 64; i++) {
				if (dec.coef[i] != stored[i]) {
					bad++;
					if (bad <= 3)
						printf("  entropy mismatch at %zu: decoded %d, encoder %d\n", i, dec.coef[i], stored[i]);
				}
			}
			CHECK(bad == 0, "%zu coefficients differ between the entropy stream and the encoder", bad);
		}
		/* Padding before EOI must be 1-bits; dec_scan already checked it. */
	}
	dec_free(&dec);

	/* 0xFF stuffing: every 0xFF in the entropy segment is followed by 0x00, RSTn or EOI. */
	{
		size_t start = dec.entropy_start, end = dec.entropy_end;
		int ff = 0, bad = 0;

		for (i = start; i + 1 < end; i++) {
			if (out[i] == 0xff) {
				ff++;
				if (out[i + 1] != 0x00 && !(out[i + 1] >= 0xd0 && out[i + 1] <= 0xd7)) {
					bad++;
					if (bad == 1)
						printf("  illegal FF %02x at %zu\n", out[i + 1], i);
				}
				i++;
			}
		}
		CHECK(bad == 0, "%d FF bytes in the entropy stream, all stuffed or restart markers", ff);
		if (quality == 100)
			CHECK(ff > 0, "quality 100 exercises 0xFF stuffing");
	}

	/* Determinism. */
	{
		uint8_t *again = (uint8_t *)malloc(capacity);
		size_t size2 = 0;

		CHECK(n2jpeg_encode(e, &view, again, capacity, &size2) == N2JPEG_OK && size2 == size &&
		      memcmp(again, out, size) == 0, "same input gives the same bytes");
		free(again);
	}
	(void)diff_runs;
	free(expect);
	free(out);
	n2jpeg_close(e);
}

static void
test_restart_intervals(const struct pic *p)
{
	uint32_t intervals[] = { 1, 2, 3, 7, 1000 };
	unsigned t;
	n2jpeg_picture view;
	size_t first_size = 0;
	int16_t *first_coef = NULL;
	size_t first_blocks = 0;

	pic_view(p, &view);
	for (t = 0; t < sizeof(intervals) / sizeof(intervals[0]); t++) {
		n2jpeg_config cfg;
		n2jpeg_enc *e = NULL;
		uint8_t *out;
		size_t size = 0, capacity, blocks = 0;
		const int16_t *coef;
		struct jdec dec;
		int ok;

		memset(&cfg, 0, sizeof(cfg));
		cfg.size = (uint32_t)sizeof(cfg);
		cfg.width = (int32_t)p->w;
		cfg.height = (int32_t)p->h;
		cfg.quality = 80;
		cfg.restart_interval = intervals[t];
		cfg.flags = N2JPEG_F_KEEP_COEF;
		CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_OK && e != NULL, "open restart=%u", intervals[t]);
		if (e == NULL)
			continue;
		capacity = n2jpeg_bound(e);
		out = (uint8_t *)malloc(capacity);
		CHECK(n2jpeg_encode(e, &view, out, capacity, &size) == N2JPEG_OK, "encode restart=%u", intervals[t]);
		coef = n2jpeg_last_coefficients(e, &blocks);

		ok = dec_parse(&dec, out, size) == 0 && !dec.error;
		CHECK(ok, "parse restart=%u", intervals[t]);
		if (ok) {
			dec.max_blocks = 8192;
			free(dec.coef);
			dec.coef = (int16_t *)calloc(dec.max_blocks * 64, sizeof(int16_t));
			CHECK(dec.restart_interval == (int)intervals[t], "DRI says %d", dec.restart_interval);
			CHECK(dec_scan(&dec) == 0 && !dec.error, "decode with RSTn every %u MCU(s)", intervals[t]);
			CHECK(dec.blocks == blocks && coef != NULL, "block count with restarts");
			if (dec.blocks == blocks && coef != NULL) {
				size_t i, bad = 0;

				for (i = 0; i < blocks * 64; i++)
					if (dec.coef[i] != coef[i])
						bad++;
				CHECK(bad == 0, "restart markers do not change the coefficients (%zu bad)", bad);
			}
			if (first_coef == NULL) {
				first_coef = (int16_t *)malloc(blocks * 64 * sizeof(int16_t));
				memcpy(first_coef, coef, blocks * 64 * sizeof(int16_t));
				first_blocks = blocks;
				first_size = size;
			} else {
				CHECK(blocks == first_blocks && memcmp(coef, first_coef, blocks * 64 * sizeof(int16_t)) == 0,
				      "restart interval does not change coefficients");
			}
		}
		dec_free(&dec);
		free(out);
		n2jpeg_close(e);
	}
	(void)first_size;
	free(first_coef);
}

static void
test_buffer_contract(const struct pic *p)
{
	n2jpeg_config cfg;
	n2jpeg_enc *e = NULL;
	n2jpeg_picture view;
	uint8_t small[16];
	uint8_t *out;
	size_t size = 0, size_query = 0, capacity;
	int r;

	memset(&cfg, 0, sizeof(cfg));
	cfg.size = (uint32_t)sizeof(cfg);
	cfg.width = (int32_t)p->w;
	cfg.height = (int32_t)p->h;
	cfg.quality = 90;
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_OK && e != NULL, "open for buffer contract");
	if (e == NULL)
		return;
	capacity = n2jpeg_bound(e);
	out = (uint8_t *)malloc(capacity);
	pic_view(p, &view);

	r = n2jpeg_encode(e, &view, NULL, 0, &size_query);
	CHECK(r == N2JPEG_OK && size_query > 0, "NULL buffer returns the exact size (%zu)", size_query);
	r = n2jpeg_encode(e, &view, small, sizeof(small), &size);
	CHECK(r == N2JPEG_E_TOO_SMALL && size == size_query, "small buffer: E_TOO_SMALL with the same exact size");
	r = n2jpeg_encode(e, &view, out, capacity, &size);
	CHECK(r == N2JPEG_OK && size == size_query, "full buffer: OK and the same size");
	CHECK(size <= n2jpeg_bound(e), "bound (%zu) covers the file (%zu)", n2jpeg_bound(e), size);
	free(out);
	n2jpeg_close(e);
}

static void
test_arguments(void)
{
	n2jpeg_config cfg;
	n2jpeg_enc *e = NULL;
	uint8_t payload[4] = { 1, 2, 3, 4 };
	n2jpeg_segment seg;

	memset(&cfg, 0, sizeof(cfg));
	cfg.size = (uint32_t)sizeof(cfg);
	cfg.width = 64;
	cfg.height = 48;
	cfg.quality = 90;
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_OK, "valid config");
	n2jpeg_close(e);
	e = NULL;
	cfg.width = 65;
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_E_ARGUMENT && e == NULL, "odd width rejected");
	cfg.width = 0;
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_E_ARGUMENT, "zero width rejected");
	cfg.width = 65536;
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_E_ARGUMENT, "width 65536 rejected");
	cfg.width = 64;
	cfg.height = 1;
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_E_ARGUMENT, "height 1 rejected");
	cfg.height = 48;
	cfg.quality = 0;
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_E_ARGUMENT, "quality 0 rejected at open");
	cfg.quality = 90;
	cfg.restart_interval = 65536;
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_E_ARGUMENT, "restart 65536 rejected");
	cfg.restart_interval = 0;
	cfg.segment_count = 1;
	cfg.segments = &seg;
	memset(&seg, 0, sizeof(seg));
	seg.marker = 0xc0;                          /* not APPn/COM */
	seg.data = payload;
	seg.size = sizeof(payload);
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_E_ARGUMENT, "a non-APPn segment is rejected");
	seg.marker = 0xe1;
	seg.size = 65534;
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_E_ARGUMENT, "a segment longer than 65533 is rejected");
	seg.size = sizeof(payload);
	seg.data = NULL;
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_E_ARGUMENT, "a NULL payload is rejected");
	seg.data = payload;
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_OK, "a valid APP1 segment is accepted");
	n2jpeg_close(e);
	e = NULL;
	cfg.segment_count = 0;
	cfg.segments = NULL;
	cfg.size = 0;
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_E_ARGUMENT, "zero size (version) rejected");
	CHECK(n2jpeg_open(NULL, &e) == N2JPEG_E_ARGUMENT, "NULL config rejected");
	CHECK(n2jpeg_open(&cfg, NULL) == N2JPEG_E_ARGUMENT, "NULL out rejected");
	CHECK(n2jpeg_set_quality(NULL, 50) == N2JPEG_E_ARGUMENT, "NULL encoder set_quality");
	CHECK(n2jpeg_set_restart_interval(NULL, 1) == N2JPEG_E_ARGUMENT, "NULL encoder set_restart_interval");
	n2jpeg_close(NULL);
}

static void
test_segments(void)
{
	static const uint8_t app1[] = { 'E', 'x', 'i', 'f', 0, 0, 0x4d, 0x4d, 0, 0x2a };
	static const uint8_t app0[] = { 'J', 'F', 'X', 'X', 0, 1, 2 };
	static const uint8_t com[] = { 'n', '2', 'j', 'p', 'e', 'g' };
	n2jpeg_segment segs[3];
	n2jpeg_config cfg;
	n2jpeg_enc *e = NULL;
	n2jpeg_picture view;
	struct pic p;
	uint8_t *out;
	size_t size = 0, capacity, i;
	int app0_count = 0, app1_at = -1, com_at = -1, dqt_at = -1;

	pic_make(&p, 32, 24);
	memset(segs, 0, sizeof(segs));
	segs[0].marker = 0xe0;
	segs[0].data = app0;
	segs[0].size = sizeof(app0);
	segs[1].marker = 0xe1;
	segs[1].data = app1;
	segs[1].size = sizeof(app1);
	segs[2].marker = 0xfe;
	segs[2].data = com;
	segs[2].size = sizeof(com);

	memset(&cfg, 0, sizeof(cfg));
	cfg.size = (uint32_t)sizeof(cfg);
	cfg.width = (int32_t)p.w;
	cfg.height = (int32_t)p.h;
	cfg.quality = 90;
	cfg.segments = segs;
	cfg.segment_count = 3;
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_OK && e != NULL, "open with three segments");
	if (e == NULL) {
		pic_free(&p);
		return;
	}
	capacity = n2jpeg_bound(e);
	out = (uint8_t *)malloc(capacity);
	pic_view(&p, &view);
	CHECK(n2jpeg_encode(e, &view, out, capacity, &size) == N2JPEG_OK, "encode with segments");

	i = 2;
	while (i + 4 <= size) {
		unsigned marker = out[i + 1];
		size_t length = rd16(out + i + 2);
		const uint8_t *payload = out + i + 4;

		if (marker == 0xe0) {
			app0_count++;
			CHECK(length - 2 == sizeof(app0) && memcmp(payload, app0, sizeof(app0)) == 0,
			      "APP0 payload is the caller's (no second JFIF APP0)");
		} else if (marker == 0xe1) {
			app1_at = (int)i;
			CHECK(length - 2 == sizeof(app1) && memcmp(payload, app1, sizeof(app1)) == 0,
			      "APP1 (EXIF) payload passed through");
		} else if (marker == 0xfe) {
			com_at = (int)i;
			CHECK(length - 2 == sizeof(com) && memcmp(payload, com, sizeof(com)) == 0, "COM payload passed through");
		} else if (marker == 0xdb) {
			dqt_at = (int)i;
		}
		if (marker == 0xda)
			break;
		i += 2 + length;
	}
	CHECK(app0_count == 1, "exactly one APP0 (the caller's)");
	CHECK(app1_at > 0 && com_at > app1_at && dqt_at > com_at, "segments sit between SOI and DQT in order");
	free(out);
	n2jpeg_close(e);
	pic_free(&p);
}

static void
test_segment_ownership(void)
{
	/* n2jpeg_open copies the segment payloads: freeing the caller's buffer before the encode must change nothing. */
	static const uint8_t reference_payload[9] = { 'c', 'o', 'p', 'i', 'e', 'd', '!', 0x00, 0x7f };
	n2jpeg_segment segment;
	n2jpeg_config cfg;
	n2jpeg_enc *e = NULL;
	n2jpeg_picture view;
	struct pic p;
	uint8_t *payload = (uint8_t *)malloc(sizeof(reference_payload));
	uint8_t *first = NULL, *second = NULL;
	size_t capacity, size1 = 0, size2 = 0;

	pic_make(&p, 32, 32);
	memset(&segment, 0, sizeof(segment));
	segment.marker = 0xe2;
	segment.data = payload;
	segment.size = sizeof(reference_payload);
	memset(&cfg, 0, sizeof(cfg));
	cfg.size = (uint32_t)sizeof(cfg);
	cfg.width = (int32_t)p.w;
	cfg.height = (int32_t)p.h;
	cfg.quality = 80;
	cfg.segments = &segment;
	cfg.segment_count = 1;
	CHECK(payload != NULL, "payload allocation");
	if (payload == NULL) {
		pic_free(&p);
		return;
	}
	memcpy(payload, reference_payload, sizeof(reference_payload));
	CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_OK && e != NULL, "open with a heap segment");
	if (e != NULL) {
		memset(payload, 0xaa, sizeof(reference_payload));
		free(payload);
		payload = NULL;
		capacity = n2jpeg_bound(e);
		first = (uint8_t *)malloc(capacity);
		pic_view(&p, &view);
		CHECK(n2jpeg_encode(e, &view, first, capacity, &size1) == N2JPEG_OK, "encode after freeing the payload");
		second = (uint8_t *)malloc(capacity);
		CHECK(n2jpeg_encode(e, &view, second, capacity, &size2) == N2JPEG_OK && size2 == size1 &&
		      memcmp(first, second, size1) == 0, "stable output after the caller's buffer is gone");
		{
			size_t at, found = 0;

			for (at = 0; at + sizeof(reference_payload) <= size1; at++) {
				if (memcmp(first + at, reference_payload, sizeof(reference_payload)) == 0) {
					found = 1;
					break;
				}
			}
			CHECK(found, "the copied APP2 payload is in the file");
		}
		n2jpeg_close(e);
	}
	free(payload);
	free(first);
	free(second);
	pic_free(&p);
}

static void
test_edge_sizes(void)
{
	static const uint32_t sizes[][2] = { { 2, 2 }, { 16, 16 }, { 18, 18 }, { 178, 146 }, { 34, 20 } };
	unsigned t;

	for (t = 0; t < sizeof(sizes) / sizeof(sizes[0]); t++) {
		struct pic p;
		n2jpeg_config cfg;
		n2jpeg_enc *e = NULL;
		n2jpeg_picture view;
		uint8_t *out;
		size_t size = 0, capacity, blocks = 0;
		const int16_t *coef;
		int16_t *expect;
		size_t expect_blocks = 0, i, bad = 0;

		pic_make(&p, sizes[t][0], sizes[t][1]);
		memset(&cfg, 0, sizeof(cfg));
		cfg.size = (uint32_t)sizeof(cfg);
		cfg.width = (int32_t)p.w;
		cfg.height = (int32_t)p.h;
		cfg.quality = 85;
		cfg.flags = N2JPEG_F_KEEP_COEF;
		CHECK(n2jpeg_open(&cfg, &e) == N2JPEG_OK && e != NULL, "open %ux%u", p.w, p.h);
		if (e == NULL) {
			pic_free(&p);
			continue;
		}
		capacity = n2jpeg_bound(e);
		out = (uint8_t *)malloc(capacity);
		pic_view(&p, &view);
		CHECK(n2jpeg_encode(e, &view, out, capacity, &size) == N2JPEG_OK, "encode %ux%u", p.w, p.h);
		coef = n2jpeg_last_coefficients(e, &blocks);
		expect = expected_coefficients(&p, 85, &expect_blocks);
		CHECK(expect != NULL && blocks == expect_blocks, "block count %ux%u", p.w, p.h);
		if (expect != NULL && coef != NULL) {
			for (i = 0; i < blocks * 64; i++)
				if (expect[i] != coef[i])
					bad++;
			CHECK(bad == 0, "%ux%u: coefficients match the reference on replicated edges (%zu bad)", p.w, p.h, bad);
		}
		{
			struct jdec dec;

			if (dec_parse(&dec, out, size) == 0 && !dec.error) {
				dec.max_blocks = 8192;
				free(dec.coef);
				dec.coef = (int16_t *)calloc(dec.max_blocks * 64, sizeof(int16_t));
				CHECK(dec_scan(&dec) == 0, "%ux%u entropy decode", p.w, p.h);
				dec_free(&dec);
			} else {
				CHECK(0, "%ux%u parse", p.w, p.h);
				dec_free(&dec);
			}
		}
		free(expect);
		free(out);
		n2jpeg_close(e);
		pic_free(&p);
	}
}

int
main(void)
{
	struct pic p;

	test_tables();
	test_arguments();
	test_segments();
	test_segment_ownership();

	pic_make(&p, 56, 40);
	test_markers(&p);
	test_coefficients_and_entropy(&p, 90);
	test_coefficients_and_entropy(&p, 100);
	test_coefficients_and_entropy(&p, 20);
	test_restart_intervals(&p);
	test_buffer_contract(&p);
	pic_free(&p);

	test_headers_equal_cjpeg_layout();
	test_edge_sizes();

	if (failures != 0) {
		printf("T_JPEG FAIL %d of %d\n", failures, checks);
		return 1;
	}
	printf("T_JPEG PASS %d\n", checks);
	return 0;
}
