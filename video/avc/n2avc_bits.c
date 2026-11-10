/* SPDX-License-Identifier: MIT */
/*
 * n2avc_bits.c -- the RBSP bit writer and the NAL unit packer of the WP03 H.264 encoder.
 *
 * ITU-T H.264 (03/2010): clause 9.1 (Exp-Golomb codes), 7.3.2.11 (rbsp_trailing_bits), 7.4.1 and 7.4.2
 * (emulation_prevention_three_byte).  No look-up tables: the Exp-Golomb codes are computed from their definition.
 */
#include "n2avc_priv.h"

#include <string.h>

void n2bw_init(n2bw *w, uint8_t *buf, size_t cap)
{
	w->buf = buf;
	w->cap = cap;
	w->pos = 0;
	w->acc = 0;
	w->nbits = 0;
	w->overflow = 0;
}

/* Appends n <= 32 bits of v, MSB first.  Whole bytes leave the accumulator immediately, so nbits stays 0..7
 * between calls and the 64-bit accumulator can never be overrun. */
void n2bw_u(n2bw *w, int n, uint32_t v)
{
	if (n <= 0)
		return;
	if (w->overflow)
		return;
	if (n > 32)
		n = 32;
	if (n < 32)
		v &= (1u << n) - 1u;
	w->acc = (w->acc << n) | v;
	w->nbits += n;
	while (w->nbits >= 8) {
		w->nbits -= 8;
		if (w->pos < w->cap)
			w->buf[w->pos++] = (uint8_t)(w->acc >> w->nbits);
		else
			w->overflow = 1;
	}
	if (w->nbits)
		w->acc &= (1ull << w->nbits) - 1ull;
	else
		w->acc = 0;
}

/* ue(v) of clause 9.1: leadingZeros zeros, then a one, then the leadingZeros low bits of v + 1. */
void n2bw_ue(n2bw *w, uint32_t v)
{
	uint32_t v1 = v + 1u;          /* v <= 2^32 - 2, so v1 != 0 */
	int k = 0;
	while (v1 >> (k + 1))
		k++;                    /* k = floor(log2(v1)), 0..31 */
	n2bw_u(w, k, 0);                /* k leading zeros, then v1 in k + 1 bits (at most 32) */
	n2bw_u(w, k + 1, v1);
}

/* se(v) of clause 9.1.1: codeNum = 2 * |v| - (v > 0 ? 1 : 0), then ue(codeNum). */
void n2bw_se(n2bw *w, int32_t v)
{
	if (v > 0)
		n2bw_ue(w, (uint32_t)(2 * (int64_t)v - 1));
	else
		n2bw_ue(w, (uint32_t)(-2 * (int64_t)v));
}

void n2bw_trailing(n2bw *w)
{
	n2bw_u(w, 1, 1);
	while (w->nbits)                /* rbsp_alignment_zero_bit, 7.3.2.11 */
		n2bw_u(w, 1, 0);
}

void n2bw_align_zero(n2bw *w)
{
	while (w->nbits)
		n2bw_u(w, 1, 0);
}

size_t n2bw_bits(const n2bw *w)
{
	return w->pos * 8u + (size_t)w->nbits;
}

size_t n2bw_flush(n2bw *w)
{
	if (w->nbits != 0)              /* the caller must finish with rbsp_trailing_bits or n2bw_align_zero */
		w->overflow = 1;
	return w->pos;
}

/* clause 7.4.1: header byte (forbidden_zero_bit 0, nal_ref_idc, nal_unit_type), then the RBSP with an
 * emulation_prevention_three_byte after any two consecutive zero bytes when the next byte is 0x00..0x03. */
size_t n2avc_nal(uint8_t *dst, size_t cap, const uint8_t *rbsp, size_t n, int nal_ref_idc, int nal_unit_type)
{
	size_t i, o = 0;
	int zeros = 0;

	if (!dst || (!rbsp && n) || cap < 1 + n)
		return 0;
	dst[o++] = (uint8_t)(((nal_ref_idc & 3) << 5) | (nal_unit_type & 31));
	for (i = 0; i < n; i++) {
		uint8_t b = rbsp[i];
		if (zeros >= 2 && b <= 3) {
			if (o + 1 > cap)
				return 0;
			dst[o++] = 3;
			zeros = 0;
		}
		if (o + 1 > cap)
			return 0;
		dst[o++] = b;
		if (b == 0)
			zeros++;
		else
			zeros = 0;
	}
	return o;
}
