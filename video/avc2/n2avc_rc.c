/*
 * n2avc_rc.c -- per-picture rate control for the H.264 encoder.
 *
 * Constant QP when the configured bitrate is 0 (the encoder's public contract).
 * Otherwise a deterministic per-picture controller:
 *   - target bits per picture = bitrate * fps_den / fps_num;
 *   - an IDR picture is budgeted four P pictures (it carries the parameter
 *     sets and a full intra picture);
 *   - a virtual buffer of one second of bits absorbs the picture-to-picture
 *     error;
 *   - the P-picture QP moves by
 *         clamp(round(6*log2(actual/target)) blended with buffer fullness,
 *               -4, +4),
 *     and an IDR is coded at QP_P - 2 (clamped 10..51), as n2avc_int.h asks;
 *   - the first QP comes from the bits-per-pixel table below unless the
 *     caller configured one.
 * No clock, no random numbers, no floating point and no libm call: log2 is
 * computed in Q16 with a 33-entry table, so the host, an arm64 build and two
 * runs of the same input always produce the same QP sequence.
 *
 * SPDX-License-Identifier: MIT
 */
#include "n2avc_int.h"

#include <stdlib.h>
#include <string.h>

struct n2avc_rc {
	uint32_t bitrate;       /* bits per second; 0 = constant QP */
	uint32_t fps_num, fps_den;
	uint32_t width, height;
	int      base_qp;       /* the current P-picture QP */
	int64_t  buffer;        /* virtual buffer fullness in bits, + = over target */
	int64_t  buffer_cap;    /* one second of bits */
	uint64_t pictures;
};

static int
clamp_qp(int qp)
{
	if (qp < 10)
		return 10;
	if (qp > 51)
		return 51;
	return qp;
}

/* round(log2(1 + i/32) * 65536), i = 0..32.  Values are literals so that the
 * table is identical on every host and the iOS link needs no libm. */
static const int32_t log2_frac_q16[33] = {
	0, 2909, 5732, 8473, 11136, 13727, 16248, 18704,
	21098, 23433, 25711, 27936, 30109, 32234, 34312, 36346,
	38336, 40286, 42196, 44068, 45904, 47705, 49472, 51207,
	52911, 54584, 56229, 57845, 59434, 60997, 62534, 64047,
	65536
};

/* round(log2(num/den) * 65536), integer only.  The ratio is clamped to a
 * range in which the shift loop cannot overflow (about 2^-31 .. 2^31). */
static int32_t
log2_q16(uint32_t num, uint32_t den)
{
	uint64_t n, d;
	int k = 0;
	uint32_t frac;
	int idx, rem;
	int32_t lo, hi, f;

	if (num == 0)
		num = 1;
	if (den == 0)
		den = 1;
	n = num;
	d = den;
	while (n < d) {
		n <<= 1;
		k--;
	}
	while (n >= d * 2) {
		n >>= 1;
		k++;
	}
	/* now n/d is in [1, 2) */
	frac = (uint32_t)(((n - d) << 16) / d);
	idx = (int)(frac >> 11);                /* 0..31 */
	rem = (int)(frac & 0x7ff);
	lo = log2_frac_q16[idx];
	hi = log2_frac_q16[idx + 1];
	f = lo + (int32_t)(((int64_t)(hi - lo) * rem) >> 11);
	return (int32_t)(k * 65536) + f;
}

/* round-to-nearest division that is symmetric about zero. */
static int32_t
round_div(int64_t num, int64_t den)
{
	if (den <= 0)
		return 0;
	if (num >= 0)
		return (int32_t)((num + den / 2) / den);
	return (int32_t)(-((-num + den / 2) / den));
}

/* round(v / 2^shift) for a signed v. */
static int32_t
round_shift(int64_t v, int shift)
{
	const int64_t half = (int64_t)1 << (shift - 1);

	if (v >= 0)
		return (int32_t)((v + half) >> shift);
	return (int32_t)(-((-v + half) >> shift));
}

/* bits-per-pixel thresholds in Q16 and the first QP they select.  The table
 * is monotone: more bits per pixel -> a lower (finer) QP. */
static const struct {
	uint32_t bpp_q16;
	int      qp;
} first_qp_table[] = {
	{ 131, 51 }, { 262, 48 }, { 524, 45 }, { 1049, 42 },
	{ 2097, 39 }, { 4194, 36 }, { 8389, 33 }, { 16777, 30 },
	{ 33554, 27 }, { 67109, 24 }, { 134218, 21 }, { 268435, 18 },
	{ 536871, 15 }, { 1073742, 12 }, { 2147484, 10 }
};

static int
first_qp_from_bpp(uint32_t bitrate, uint32_t fps_num, uint32_t fps_den,
                  uint32_t width, uint32_t height)
{
	uint64_t num, den, bpp_q16;
	size_t i;

	if (width == 0 || height == 0 || fps_num == 0 || fps_den == 0)
		return 26;
	num = (uint64_t)bitrate * (uint64_t)fps_den;
	den = (uint64_t)width * (uint64_t)height * (uint64_t)fps_num;
	if (den == 0)
		return 26;
	if (num > UINT64_MAX / 65536)
		bpp_q16 = UINT64_MAX;
	else
		bpp_q16 = (num * 65536) / den;

	for (i = 0; i < sizeof(first_qp_table) / sizeof(first_qp_table[0]); i++) {
		if (bpp_q16 <= first_qp_table[i].bpp_q16)
			return first_qp_table[i].qp;
	}
	return 10;
}

n2avc_rc *
n2avc_rc_open(const n2avc_config *cfg)
{
	n2avc_rc *rc;

	if (cfg == NULL || cfg->size < sizeof(*cfg))
		return NULL;
	rc = (n2avc_rc *)calloc(1, sizeof(*rc));
	if (rc == NULL)
		return NULL;

	rc->bitrate = cfg->bitrate;
	rc->fps_den = cfg->fps_den != 0 ? cfg->fps_den : 1;
	rc->fps_num = cfg->fps_num != 0 ? cfg->fps_num : 30;
	rc->width = cfg->width;
	rc->height = cfg->height;
	rc->buffer_cap = rc->bitrate != 0 ? (int64_t)rc->bitrate : 1;

	if (cfg->qp >= 10 && cfg->qp <= 51) {
		rc->base_qp = (int)cfg->qp;
	} else if (rc->bitrate != 0 && cfg->qp == 0) {
		rc->base_qp = first_qp_from_bpp(rc->bitrate, rc->fps_num, rc->fps_den,
		                                rc->width, rc->height);
	} else {
		rc->base_qp = 26;
	}
	return rc;
}

int
n2avc_rc_picture_qp(n2avc_rc *rc, int idr)
{
	if (rc == NULL)
		return 26;
	if (rc->bitrate == 0)
		return clamp_qp(rc->base_qp);
	if (idr)
		return clamp_qp(rc->base_qp - 2);
	return clamp_qp(rc->base_qp);
}

void
n2avc_rc_picture_done(n2avc_rc *rc, int idr, int qp, size_t bytes)
{
	int64_t target, budget, actual;
	int32_t ratio_term, buf_term, delta;
	int32_t log_ratio;

	if (rc == NULL || rc->bitrate == 0)
		return;
	(void)qp;       /* the P QP is tracked in base_qp; the argument is the coded one */

	target = (int64_t)rc->bitrate * (int64_t)rc->fps_den / (int64_t)rc->fps_num;
	if (target < 1)
		target = 1;
	budget = idr ? 4 * target : target;

	actual = (int64_t)bytes * 8;
	if (actual < 1)
		actual = 1;
	if (actual > INT32_MAX)
		actual = INT32_MAX;
	if (budget > INT32_MAX)
		budget = INT32_MAX;

	log_ratio = log2_q16((uint32_t)actual, (uint32_t)budget);
	ratio_term = round_shift((int64_t)log_ratio * 6, 16);

	rc->buffer += actual - budget;
	if (rc->buffer > rc->buffer_cap)
		rc->buffer = rc->buffer_cap;
	if (rc->buffer < -rc->buffer_cap)
		rc->buffer = -rc->buffer_cap;
	buf_term = round_div(6 * rc->buffer, rc->buffer_cap);

	/* 3/4 of the picture error, 1/4 of the buffer fullness. */
	delta = round_div(3 * (int64_t)ratio_term + (int64_t)buf_term, 4);
	if (delta > 4)
		delta = 4;
	if (delta < -4)
		delta = -4;
	rc->base_qp = clamp_qp(rc->base_qp + delta);
	rc->pictures++;
}

void
n2avc_rc_reconfigure(n2avc_rc *rc, uint32_t bitrate, uint32_t fps_num, uint32_t fps_den)
{
	if (rc == NULL)
		return;
	if (bitrate != 0) {
		rc->bitrate = bitrate;
		rc->buffer_cap = (int64_t)bitrate;
		if (rc->buffer > rc->buffer_cap)
			rc->buffer = rc->buffer_cap;
		if (rc->buffer < -rc->buffer_cap)
			rc->buffer = -rc->buffer_cap;
	}
	if (fps_num != 0)
		rc->fps_num = fps_num;
	if (fps_den != 0)
		rc->fps_den = fps_den;
}

void
n2avc_rc_close(n2avc_rc *rc)
{
	free(rc);
}
