/*
 * n2jpeg.h -- baseline JPEG encoder (ITU-T T.81, sequential DCT, Huffman) for 4:2:0 pictures already in NV12 form.
 *
 * Input is NV12: plane 0 is 8-bit luma, one sample per pixel; plane 1 is interleaved Cb, Cr at half width and half
 * height (2 bytes per sample pair), exactly the layout CoreVideo calls '420v'/'420f'.  The encoder therefore never
 * converts colour or resamples chroma: the luma component is coded at 2x2 sampling factors and each chroma component at
 * 1x1, taking the NV12 chroma samples as they are.  Width and height must be even (NV12 requires it).
 *
 * What the file contains: SOI, an optional JFIF APP0, every caller APPn/COM segment in order, one DQT with the two
 * quantisation tables in zigzag order, SOF0 (baseline, 8-bit, three components), one DHT with the four Annex K tables,
 * DRI when restart markers are enabled, SOS, the entropy-coded data with 0xFF stuffing and RSTn markers, EOI.
 *
 * The forward DCT and the quantisation are not done here: every 8x8 block goes through n2vk_fdct8_quant() (include/n2vk.h),
 * whose exact integer arithmetic is the single definition of the coefficients.  This file only gathers the samples (with
 * edge replication for partial blocks), scales the Annex K quantisation tables, and entropy-codes T.81.
 *
 * Threading: an n2jpeg_enc is not thread-safe.  One encoder belongs to one thread at a time (the VideoToolbox glue holds
 * a mutex per encoder object).  Nothing in the library is shared between encoder objects.
 *
 * Original code written from ITU-T T.81; no third-party codec source and no Apple header is involved.
 * SPDX-License-Identifier: MIT
 */
#ifndef N2JPEG_H
#define N2JPEG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define N2JPEG_API_VERSION 1

enum {
	N2JPEG_OK          =  0,
	N2JPEG_E_ARGUMENT  = -1,        /* a parameter is outside its documented range */
	N2JPEG_E_MEMORY    = -2,        /* an allocation at open() failed */
	N2JPEG_E_TOO_SMALL = -3,        /* *out_size is the exact number of bytes the picture needs */
	N2JPEG_E_STATE     = -4,        /* the encoder is not open, or an internal invariant failed */
	N2JPEG_E_BACKEND   = -5         /* n2vk_fdct8_quant() failed */
};

/* config flags */
enum {
	N2JPEG_F_KEEP_COEF = 1u << 0,   /* keep the quantised coefficients of the last picture (tests, up to 64 MiB) */
	N2JPEG_F_NO_JFIF   = 1u << 1    /* do not write the JFIF APP0 (also implied by an APP0 in the segment list) */
};

/* One marker segment passed through to the file.  marker is 0xE0..0xEF for APP0..APP15, 0xFE for COM.  data is the
 * payload after the two-byte length field; n2jpeg_open() copies it, so the caller may release it immediately. */
typedef struct n2jpeg_segment {
	uint8_t        marker;
	uint32_t       reserved;        /* must be 0 (keeps the layout deterministic) */
	const uint8_t *data;
	size_t         size;
} n2jpeg_segment;

typedef struct n2jpeg_config {
	uint32_t              size;              /* sizeof(n2jpeg_config) */
	uint32_t              flags;             /* N2JPEG_F_* */
	int32_t               width, height;     /* NV12 picture, even, 2..65534 */
	int32_t               quality;           /* 1..100, IJG scaling of the T.81 Annex K tables */
	uint32_t              restart_interval;  /* MCUs between restart markers, 0 = none, <= 65535 */
	const n2jpeg_segment *segments;          /* APPn/COM segments, may be NULL */
	uint32_t              segment_count;     /* <= 16 */
} n2jpeg_config;

typedef struct n2jpeg_picture {
	uint32_t       size;                     /* sizeof(n2jpeg_picture) */
	const uint8_t *y;                        /* luma, width x height */
	uint32_t       y_stride;                 /* bytes per row, >= width */
	const uint8_t *uv;                       /* interleaved CbCr, (width/2) x (height/2) sample pairs */
	uint32_t       uv_stride;                /* bytes per row, >= width */
} n2jpeg_picture;

typedef struct n2jpeg_enc n2jpeg_enc;

/* Opens an encoder.  On success *out is non-NULL and must be closed with n2jpeg_close(). */
int  n2jpeg_open(const n2jpeg_config *config, n2jpeg_enc **out);

/* Changes take effect at the next n2jpeg_encode(); no allocation happens. */
int  n2jpeg_set_quality(n2jpeg_enc *enc, int quality);
int  n2jpeg_set_restart_interval(n2jpeg_enc *enc, uint32_t mcus);

/* Encodes one picture as a complete JPEG file (SOI..EOI).
 *   buf != NULL: the file is written to buf when cap is large enough; when it is not, nothing beyond cap is written
 *                and N2JPEG_E_TOO_SMALL is returned with *out_size set to the exact required size.
 *   buf == NULL: nothing is written, *out_size is the exact size, N2JPEG_OK.  (This still does the full encode.)
 * out_size is always set when the function returns, including on errors.
 * No memory is allocated here.  A successful call replaces the coefficients returned by n2jpeg_last_coefficients(). */
int  n2jpeg_encode(n2jpeg_enc *enc, const n2jpeg_picture *picture, uint8_t *buf, size_t cap, size_t *out_size);

/* A buffer size that is guaranteed to hold any picture this encoder can produce (header + worst-case entropy data
 * including 0xFF stuffing).  It is conservative: mcus * (6 * 416 + 4) + header.  Use n2jpeg_encode()'s exact answer
 * when memory matters. */
size_t n2jpeg_bound(const n2jpeg_enc *enc);
/* Bytes that precede the entropy-coded data (markers, DQT, DHT, tables), for tests and for buffer planning. */
size_t n2jpeg_header_size(const n2jpeg_enc *enc);
uint32_t n2jpeg_mcu_count(const n2jpeg_enc *enc);

/* The quantisation tables actually in use, in *natural* order (index v * 8 + u, the order n2vk_fdct8_quant takes);
 * the file stores them in zigzag order.  Either pointer may be NULL. */
void n2jpeg_quant_tables(const n2jpeg_enc *enc, uint16_t luma[64], uint16_t chroma[64]);

/* Only with N2JPEG_F_KEEP_COEF: the quantised coefficients of the last successful encode, in MCU order -- four luma
 * blocks (raster order inside the MCU), then Cb, then Cr -- each block 64 values in natural order.  NULL without the
 * flag or before the first encode.  The pointer is owned by the encoder and is valid until the next encode/close. */
const int16_t *n2jpeg_last_coefficients(const n2jpeg_enc *enc, size_t *count);

void n2jpeg_close(n2jpeg_enc *enc);

#ifdef __cplusplus
}
#endif
#endif
