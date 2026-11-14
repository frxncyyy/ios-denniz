/* SPDX-License-Identifier: MIT */
/*
 * n2_vdec_vld.c -- reference model of the MT6893 VDEC VLD barrel shifter / AVC_VLD helpers (see n2_vdec_vld.h and
 * gpu/hwcodec/facts/VDEC_REGISTERS.md).  Written from the register usage in the vendor binary; no vendor code.
 */
#include "n2_vdec_vld.h"
#include <string.h>

/* VLD sub-block (byte offsets) */
#define VLD_RPTR      0x0b0u   /* reg 0x2c: read pointer, byte PA */
#define VLD_VSTART    0x0b4u   /* reg 0x2d: vfifo start >> 6 */
#define VLD_VEND      0x0b8u   /* reg 0x2e: vfifo end >> 6 */
#define VLD_FETCH     0x08cu   /* reg 0x23: 0x100000 start init fetch, 0x800000 load barrel shifter */
#define VLD_RESET     0x108u   /* reg 0x42: 0x100 then 0 after a new read pointer */
#define VLD_FETCH_RDY 0x0e8u   /* reg 0x3a: bit 0 init fetch ready */
#define VLD_PROC      0x0f4u   /* reg 0x3d: bit 15 "busy flag valid", bit 0 done */
#define VLD_SRAM      0x0ecu   /* reg 0x3b: [7:0] sram rd idx, [15:8] sram wr idx, [25:24] word idx */
#define VLD_SRAM2     0x0d0u   /* reg 0x34: [23:16] sram level correction */
#define VLD_DRAMPTR   0x0fcu   /* reg 0x3f: DRAM fetch pointer (byte PA) */
/* AVC_VLD sub-block */
#define AVC_BARL_END  0x080u   /* reg n (0..32) at 4n: shift n bits, return next 32 bits */
#define AVC_CTRL      0x084u   /* reg 0x21: written 0xa880c007; read [21:16] bit count, bit 31 must read 0 */
#define AVC_UE        0x23cu   /* reg 0x8f */
#define AVC_SE        0x240u   /* reg 0x90 */
#define AVC_TRIG      0x238u   /* reg 0x8e: 1 = decode start */
#define AVC_SC_STAT   0x2c8u   /* reg 0xb2 */
#define AVC_SC_SEARCH 0x2d8u   /* reg 0xb6: write 1 = search next start code, bit 0 busy */
#define LARB_SLP      0x00cu   /* SMI larb +0x00c: bit 0 sleep request, bit 16 sleep ack */

static uint64_t ring(const struct n2vld_unit *u)
{
	return u->vend > u->vstart ? u->vend - u->vstart : 0;
}

static int byte_at(struct n2vld *m, const struct n2vld_unit *u, uint64_t p, uint8_t *b)
{
	uint64_t sz = ring(u);
	*b = 0;
	if (!sz || !m->mem_read)
		return -1;
	return m->mem_read(m->mem_ctx, u->vstart + p % sz, b, 1) ? -1 : 0;
}

static int is_epb(struct n2vld *m, const struct n2vld_unit *u, uint64_t p)
{
	uint8_t a, b, c;
	if (!m->epb || p < u->l0 + 2)
		return 0;
	if (byte_at(m, u, p, &c) || c != 3)
		return 0;
	if (byte_at(m, u, p - 1, &b) || b)
		return 0;
	if (byte_at(m, u, p - 2, &a) || a)
		return 0;
	return 1;
}

/* read n (0..32) payload bits at *pos (raw bit position), skipping emulation-prevention bytes; advances *pos */
static uint32_t take(struct n2vld *m, struct n2vld_unit *u, uint64_t *pos, unsigned n, int count)
{
	uint64_t v = 0;
	for (unsigned i = 0; i < n; i++) {
		uint8_t byte;
		while ((*pos & 7) == 0 && is_epb(m, u, *pos >> 3)) {
			*pos += 8;
			if (count)
				m->st.epb_skipped++;
		}
		byte_at(m, u, *pos >> 3, &byte);
		v = (v << 1) | ((byte >> (7 - (*pos & 7))) & 1u);
		(*pos)++;
	}
	return (uint32_t)v;
}

static uint32_t peek32(struct n2vld *m, struct n2vld_unit *u)
{
	uint64_t p = u->abit;
	m->st.peeks++;
	return take(m, u, &p, 32, 0);
}

static uint32_t ue(struct n2vld *m, struct n2vld_unit *u)
{
	unsigned z = 0;
	while (z < 32 && take(m, u, &u->abit, 1, 1) == 0)
		z++;
	if (z >= 32)
		return 0xffffffffu;   /* the vendor never asks: it tests the window for 0 first */
	if (z == 0)
		return 0;
	return (uint32_t)(((uint64_t)1 << z) - 1 + take(m, u, &u->abit, z, 1));
}

static void latch(struct n2vld_unit *u)
{
	uint64_t sz = ring(u), r = u->vld[VLD_RPTR / 4];
	u->abit = (sz && r >= u->vstart) ? ((r - u->vstart) % sz) * 8 : 0;
	u->l0 = u->abit >> 3;
	u->loaded = 1;
}

/* move to the first 00 00 01 at or after raw byte p (no EPB logic); 0 if none within one ring */
static int search(struct n2vld *m, struct n2vld_unit *u, uint64_t p)
{
	uint64_t sz = ring(u);
	uint8_t a = 0xff, b = 0xff, c;
	m->st.startcode_searches++;
	for (uint64_t q = p; sz && q < p + sz + 2; q++) {
		if (byte_at(m, u, q, &c))
			break;
		if (q >= p + 2 && a == 0 && b == 0 && c == 1) {
			u->abit = (q - 2) * 8;
			return 1;
		}
		a = b;
		b = c;
	}
	m->st.startcode_misses++;
	return 0;
}

static int vld_access(struct n2vld_unit *u, uint32_t r, int w, uint32_t *val)
{
	switch (r) {
	case VLD_RPTR: case VLD_VSTART: case VLD_VEND: case VLD_SRAM: case VLD_SRAM2:
		if (w) {
			u->vld[r / 4] = *val;
			if (r == VLD_VSTART)
				u->vstart = (uint64_t)*val << 6;
			if (r == VLD_VEND)
				u->vend = (uint64_t)*val << 6;
		} else if (r == VLD_SRAM) {
			*val = u->vld[r / 4] & ~0x0300ffffu;   /* sram indices equal, word index 0 */
		} else if (r == VLD_SRAM2) {
			*val = u->vld[r / 4] & ~0x00ff0000u;
		} else {
			*val = u->vld[r / 4];
		}
		return 1;
	case VLD_RESET:
		if (!w)
			return 0;
		u->vld[r / 4] = *val;
		if (*val & 0x100u)
			latch(u);
		return 1;
	case VLD_FETCH:
		if (!w)
			return 0;
		u->vld[r / 4] = *val;
		if (*val & 0x100000u)
			u->fetch_ready = 1;
		if (*val & 0x800000u)
			latch(u);
		return 1;
	case VLD_FETCH_RDY:
		if (w)
			return 0;
		*val = (u->vld[r / 4] & ~1u) | (u->fetch_ready & 1u);
		return 1;
	case VLD_PROC:
		if (w)
			return 0;
		*val = 0;   /* bit 15 clear: "process flag" wait returns at once */
		return 1;
	case VLD_DRAMPTR: {
		uint64_t sz = ring(u);
		if (w)
			return 0;
		/* vendor (vdec_hal_read_vld_rptr): rptr = this - 0x1c + bitcnt/8 (+ ring if below start) */
		*val = (uint32_t)(u->vstart + (sz ? (((u->abit >> 5) << 2) + 0x1c) % sz : 0));
		return 1;
	}
	default:
		return 0;
	}
}

static int avc_access(struct n2vld *m, struct n2vld_unit *u, uint32_t r, int w, uint32_t *val)
{
	if (r <= AVC_BARL_END) {
		if (w) {
			u->avc[r / 4] = *val;
			return 1;
		}
		if (r) {
			take(m, u, &u->abit, r / 4, 1);
			m->st.shifts++;
		}
		*val = peek32(m, u);
		return 1;
	}
	switch (r) {
	case AVC_CTRL:
		if (w)
			u->avc[r / 4] = *val;
		else
			*val = (u->avc[r / 4] & ~0x803f0000u) | ((uint32_t)(u->abit & 31) << 16);
		return 1;
	case AVC_UE: case AVC_SE: {
		uint32_t k;
		if (w)
			return 1;
		k = ue(m, u);
		if (r == AVC_UE) {
			m->st.ue++;
			*val = k;
		} else {
			m->st.se++;
			*val = (k & 1) ? (k + 1) / 2 : (uint32_t)-(int64_t)(k / 2);
		}
		return 1;
	}
	case AVC_SC_SEARCH:
		if (w) {
			u->avc[r / 4] = *val & ~1u;
			if (*val & 1u)
				search(m, u, (u->abit + 7) >> 3);
		} else {
			*val = u->avc[r / 4] & ~1u;   /* never busy */
		}
		return 1;
	case AVC_SC_STAT:
		if (w)
			u->avc[r / 4] = *val;
		else
			*val = u->avc[r / 4] & ~0x800u;
		return 1;
	case AVC_TRIG:
		if (!w)
			return 0;
		u->avc[r / 4] = *val;
		if (*val & 1u) {
			/* no pixels: the "decode" consumes the slice, i.e. the reader moves to the next start code [I] */
			m->st.triggers++;
			search(m, u, ((u->abit + 7) >> 3) + 3);
		}
		return 0;   /* let the fake also record the trigger (its done/IRQ logic) */
	default:
		return 0;
	}
}

void n2vld_init(struct n2vld *m, n2vld_mem_read_fn rd, void *ctx)
{
	memset(m, 0, sizeof(*m));
	m->mem_read = rd;
	m->mem_ctx = ctx;
	m->epb = 1;
}

int n2vld_access(struct n2vld *m, uint32_t off, int is_write, uint32_t *val)
{
	static const struct { uint32_t vld, avc, misc, larb; } base[N2VLD_UNITS] = {
		{ N2VLD_CORE_VLD, N2VLD_CORE_AVC_VLD, N2VLD_CORE_MISC, N2VLD_CORE_LARB },
		{ N2VLD_LAT_VLD, N2VLD_LAT_AVC_VLD, N2VLD_LAT_MISC, N2VLD_LAT_LARB },
	};
	int r = 0;
	if (off & 3)
		return 0;
	for (int i = 0; i < N2VLD_UNITS && !r; i++) {
		struct n2vld_unit *u = &m->u[i];
		if (off - base[i].vld < 0x400u)
			r = vld_access(u, off - base[i].vld, is_write, val);
		else if (off - base[i].avc < 0x1000u)
			r = avc_access(m, u, off - base[i].avc, is_write, val);
		else if (!is_write && off - base[i].misc >= 0x110u && off - base[i].misc <= 0x13cu) {
			*val = 0;   /* "all DRAM and bits processed" wait: every register 0x110..0x13c must read 0 */
			r = 1;
		} else if (off == base[i].larb + LARB_SLP) {
			if (is_write)
				u->larb_slp = *val;
			else
				*val = (u->larb_slp & ~0x10000u) | ((u->larb_slp & 1u) << 16);
			r = 1;
		}
	}
	if (r)
		m->st.claimed++;
	return r;
}

uint64_t n2vld_rptr(const struct n2vld *m, int unit)
{
	const struct n2vld_unit *u = &m->u[unit];
	uint64_t sz = ring(u);
	return u->vstart + (sz ? (u->abit >> 3) % sz : 0);
}
