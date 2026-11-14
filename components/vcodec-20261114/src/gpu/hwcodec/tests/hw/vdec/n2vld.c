/* SPDX-License-Identifier: MIT */
/*
 * n2vld.c -- DEC03: host model of the MT6893 VDEC bitstream reader and AVC_VLD / HEVC_VLD
 * syntax engines.  Evidence and the [V]/[I] mark of every rule: REGISTERS.md.
 *
 * The model is deliberately small: it is the part of the block the vendor's per-slice path uses
 * (barrel-shifter init, start-code search, ue(v)/se(v), read-pointer readback), not a bit-exact
 * VDEC.  It runs behind T02's register hook; the fake keeps power, IRQ and every register this
 * model does not claim.
 */
#include "n2vld.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N2VLD_MAX_BYTES (8u * 1024u * 1024u)
#define N2VLD_VLD_WORDS (0x400u / 4u)
#define N2VLD_AVC_WORDS (0x1000u / 4u)
#define N2VLD_HEVC_WORDS (0x400u / 4u)
#define N2VLD_MAX_WINS  6u

struct n2vld_win {
	unsigned win;
	uint32_t base;
	uint32_t bytes;
};

struct n2vld_unit {
	uint32_t vld[N2VLD_VLD_WORDS];
	uint32_t avc[N2VLD_AVC_WORDS];
	uint32_t hevc[N2VLD_HEVC_WORDS];
	uint32_t larb_slp;

	uint32_t vstart, vend;       /* vfifo ring, byte PAs */
	uint64_t abit;               /* raw bit position relative to vstart (may run past one ring) */
	uint64_t l0;                 /* raw byte index where the EPB context starts */
	uint32_t fetch_ready;
	int loaded;

	uint8_t *raw;
	uint32_t raw_cap, raw_len, raw_start;
};

struct n2vld {
	struct n2vld_bus bus;
	int have_bus;
	struct n2vld_win map[N2VLD_MAX_WINS];
	unsigned nmap;

	uint8_t *direct;
	uint32_t direct_cap, direct_len;
	int have_direct;

	uint8_t *rbsp;
	uint32_t rbsp_cap, rbsp_len;

	struct n2vld_unit u[N2VLD_UNITS];
	char err[160];
	struct n2vld_stats st;
};

static void set_err(struct n2vld *v, const char *what, int rc)
{
	if (rc)
		snprintf(v->err, sizeof v->err, "%s: %d", what, rc);
	else
		snprintf(v->err, sizeof v->err, "%s", what);
}

static int ensure(uint8_t **p, uint32_t *cap, uint32_t need)
{
	uint8_t *n;
	uint32_t sz;

	if (*cap >= need)
		return 0;
	sz = *cap ? *cap : 256u;
	while (sz < need) {
		if (sz > N2VLD_MAX_BYTES / 2u)
			return -ENOMEM;
		sz *= 2u;
	}
	n = realloc(*p, sz);
	if (!n)
		return -ENOMEM;
	*p = n;
	*cap = sz;
	return 0;
}

static void strip_epb(const uint8_t *src, uint32_t n, uint8_t *dst, uint32_t *out)
{
	uint32_t i, j = 0;

	for (i = 0; i < n; i++) {
		if (i >= 2 && src[i] == 3u && src[i - 1] == 0u && src[i - 2] == 0u &&
		    i + 1 < n && src[i + 1] <= 3u)
			continue;
		dst[j++] = src[i];
	}
	*out = j;
}

static uint32_t ring_size(const struct n2vld_unit *u)
{
	return u->vend > u->vstart ? u->vend - u->vstart : 0u;
}

/* Fetch the unit's ring once.  Returns 0, or -errno. */
static int ensure_raw(struct n2vld *v, struct n2vld_unit *u)
{
	uint32_t size = ring_size(u);
	int rc;

	if (!size)
		return -EINVAL;
	if (u->raw && u->raw_len == size && u->raw_start == u->vstart)
		return 0;
	if (size > N2VLD_MAX_BYTES) {
		set_err(v, "vfifo ring larger than the model limit", 0);
		return -ENOMEM;
	}
	rc = ensure(&u->raw, &u->raw_cap, size);
	if (rc) {
		set_err(v, "vfifo raw buffer", rc);
		return rc;
	}
	if (!v->have_bus) {
		u->raw_len = 0;
		set_err(v, "vfifo programmed without a bus", 0);
		return -EINVAL;
	}
	rc = v->bus.read_pa(v->bus.ctx, u->vstart, u->raw, size);
	if (rc) {
		v->st.bus_errors++;
		u->raw_len = 0;
		set_err(v, "vfifo read_pa", rc);
		return rc;
	}
	u->raw_len = size;
	u->raw_start = u->vstart;
	v->st.reloads++;
	set_err(v, "", 0);
	return 0;
}

/* Byte `pos` of the stream (relative to the vfifo start).  Returns 1 when the byte exists. */
static int stream_byte(struct n2vld *v, struct n2vld_unit *u, uint64_t pos, uint8_t *b)
{
	if (v->have_direct) {
		if (pos >= v->direct_len)
			return 0;
		*b = v->direct[pos];
		return 1;
	}
	{
		uint32_t size = ring_size(u);
		int rc;

		if (!size)
			return 0;
		rc = ensure_raw(v, u);
		if (rc)
			return 0;
		if (u->raw_len != size)
			return 0;
		*b = u->raw[pos % size];
		return 1;
	}
}

static int is_epb(struct n2vld *v, struct n2vld_unit *u, uint64_t byte_pos)
{
	uint8_t a, b, c;

	if (byte_pos < u->l0 + 2u)
		return 0;
	if (!stream_byte(v, u, byte_pos, &c) || c != 3u)
		return 0;
	if (!stream_byte(v, u, byte_pos - 1u, &b) || b != 0u)
		return 0;
	if (!stream_byte(v, u, byte_pos - 2u, &a) || a != 0u)
		return 0;
	if (!stream_byte(v, u, byte_pos + 1u, &c))
		return 0;                 /* no following byte: not an EPB */
	return c <= 3u;
}

/* Next payload bit at *pos; skips emulation-prevention bytes on byte boundaries. */
static unsigned next_bit(struct n2vld *v, struct n2vld_unit *u, uint64_t *pos)
{
	unsigned tries = 0;
	uint8_t b;

	for (;;) {
		if (++tries > 4096u) {
			(*pos)++;
			return 0;
		}
		if (v->have_direct) {
			if ((*pos >> 3) >= v->direct_len) {
				(*pos)++;
				return 0;
			}
		} else if (!ring_size(u)) {
			(*pos)++;
			return 0;
		}
		if ((*pos & 7u) == 0u && is_epb(v, u, *pos >> 3)) {
			*pos += 8u;
			v->st.epb_skipped++;
			continue;
		}
		if (!stream_byte(v, u, *pos >> 3, &b)) {
			(*pos)++;
			return 0;
		}
		b = (uint8_t)((b >> (7u - (*pos & 7u))) & 1u);
		(*pos)++;
		return b;
	}
}

static uint32_t peek_bits(struct n2vld *v, struct n2vld_unit *u, uint64_t pos, unsigned n)
{
	uint32_t out = 0;
	unsigned i;

	for (i = 0; i < n; i++)
		out = (out << 1) | next_bit(v, u, &pos);
	return out;
}

static uint32_t ue(struct n2vld *v, struct n2vld_unit *u)
{
	unsigned z;
	unsigned found = 0;
	uint64_t lim = v->have_direct ? (uint64_t)v->direct_len * 8u : 0u;
	uint32_t val;

	for (z = 0; z < 32u; z++) {
		if (v->have_direct && u->abit >= lim)
			break;
		if (next_bit(v, u, &u->abit)) {
			found = 1;
			break;
		}
	}
	if (!found) {
		set_err(v, "ue past the end of the bitstream", 0);
		v->st.ue++;
		return N2VLD_ERR;
	}
	val = peek_bits(v, u, u->abit, z);
	u->abit += z;
	v->st.ue++;
	v->st.bits_read += 2u * (uint64_t)z + 1u;
	set_err(v, "", 0);
	return ((1u << z) - 1u) + val;
}

static uint32_t se(struct n2vld *v, struct n2vld_unit *u)
{
	uint32_t k = ue(v, u);
	int32_t r;

	if (k == N2VLD_ERR)
		return N2VLD_ERR;
	r = (k & 1u) ? (int32_t)((k + 1u) >> 1) : -(int32_t)(k >> 1);
	v->st.se++;
	return (uint32_t)r;
}

static void latch(struct n2vld_unit *u)
{
	uint32_t size = ring_size(u);
	uint32_t rptr = u->vld[N2VLD_VLD_RPTR];

	if (size && rptr >= u->vstart)
		u->abit = (uint64_t)((rptr - u->vstart) % size) * 8u;
	else
		u->abit = 0;
	u->l0 = u->abit >> 3;
	u->loaded = 1;
	/* The LAT trace latches the reader (0x42 = 0x101) and then polls 0x3a without an
	 * explicit 0x23 = 0x100000; treat a latched reader as fetch-ready.  [I] */
	u->fetch_ready = 1;
	/* The ring may have been refilled since the last latch; drop the cached copy. */
	u->raw_len = 0;
	u->raw_start = 0;
}

/* Find the first 00 00 01 at or after raw byte `from`; 0 if none within one ring. */
static int search(struct n2vld *v, struct n2vld_unit *u, uint64_t from)
{
	uint32_t size = ring_size(u);
	uint64_t limit = v->have_direct ? v->direct_len : size;
	uint8_t a = 0xff, b = 0xff, c;
	uint64_t q;

	v->st.startcode_searches++;
	if (!limit)
		return 0;
	for (q = from; q < from + limit + 2u; q++) {
		if (!stream_byte(v, u, q, &c))
			break;
		if (q >= 2u && a == 0u && b == 0u && c == 1u) {
			u->abit = (q - 2u) * 8u;
			u->l0 = q - 2u;
			return 1;
		}
		a = b;
		b = c;
	}
	v->st.startcode_misses++;
	return 0;
}

static uint32_t rptr_value(const struct n2vld_unit *u)
{
	uint32_t size = ring_size(u);

	if (!size)
		return u->vstart;
	return u->vstart + (uint32_t)((u->abit >> 3) % size);
}

/* ---- VLD sub-block ------------------------------------------------------------------------- */

static int vld_op(struct n2vld *v, struct n2vld_unit *u, uint32_t reg, int wr, uint32_t *val)
{
	v->st.vld_reads += wr ? 0u : 1u;
	v->st.vld_writes += wr ? 1u : 0u;

	if (reg >= N2VLD_VLD_WORDS)
		return 0;

	switch (reg) {
	case N2VLD_VLD_VFIFO_START:
	case N2VLD_VLD_VFIFO_END:
		if (wr) {
			u->vld[reg] = *val;
			if (reg == N2VLD_VLD_VFIFO_START)
				u->vstart = *val << 6;
			else
				u->vend = *val << 6;
			u->abit = 0;
			u->l0 = 0;
			u->loaded = 0;
			u->fetch_ready = 0;
			u->raw_len = 0;
			u->raw_start = 0;
		} else {
			*val = u->vld[reg];
		}
		return 1;
	case N2VLD_VLD_RPTR:
	case N2VLD_VLD_DCM:
	case N2VLD_VLD_0CC:
	case N2VLD_VLD_SRAM:
	case N2VLD_VLD_SRAM2:
		if (wr)
			u->vld[reg] = *val;
		else if (reg == N2VLD_VLD_SRAM)
			*val = u->vld[reg] & ~0x0300ffffu;
		else if (reg == N2VLD_VLD_SRAM2)
			*val = u->vld[reg] & ~0x00ff0000u;
		else
			*val = u->vld[reg];
		return 1;
	case N2VLD_VLD_FETCH_CTRL:
		if (wr) {
			u->vld[reg] = *val;
			if (*val & (1u << 20))
				u->fetch_ready = 1;
			if (*val & (1u << 23))
				latch(u);
		} else {
			*val = u->vld[reg];
		}
		return 1;
	case N2VLD_VLD_RESET:
		if (wr) {
			u->vld[reg] = *val;
			if (*val & 0x100u)
				latch(u);
		} else {
			*val = u->vld[reg];
		}
		return 1;
	case N2VLD_VLD_FETCH_OK:
		if (wr) {
			u->vld[reg] = *val;
		} else {
			*val = (u->vld[reg] & ~1u) | (u->fetch_ready & 1u);
		}
		return 1;
	case N2VLD_VLD_PROC:
		if (wr)
			u->vld[reg] = *val;
		else
			*val = 0;       /* bit 15 clear: the vendor's "process flag" wait returns at once */
		return 1;
	case N2VLD_VLD_DRAMPTR:
		if (wr) {
			u->vld[reg] = *val;
		} else {
			uint32_t size = ring_size(u);
			uint32_t off = 0;

			if (size)
				off = (uint32_t)(((u->abit >> 5) * 4u + 0x1cu) % size);
			*val = u->vstart + off;
		}
		return 1;
	default:
		return 0;
	}
}

/* ---- AVC_VLD / HEVC_VLD syntax engine ------------------------------------------------------ */

static int avc_op(struct n2vld *v, struct n2vld_unit *u, uint32_t reg, int wr, uint32_t *val)
{
	v->st.avc_reads += wr ? 0u : 1u;
	v->st.avc_writes += wr ? 1u : 0u;

	if (reg >= N2VLD_AVC_WORDS)
		return 0;
	if (reg <= N2VLD_AVC_BITS_MAX) {
		if (wr) {
			u->avc[reg] = *val;
			return 1;
		}
		if (reg) {
			unsigned i;

			for (i = 0; i < reg; i++)
				(void)next_bit(v, u, &u->abit);
			v->st.shifts++;
			v->st.bits_read += reg;
		}
		*val = peek_bits(v, u, u->abit, 32u);
		v->st.peeks++;
		return 1;
	}
	switch (reg) {
	case N2VLD_AVC_CTRL:
		if (wr)
			u->avc[reg] = *val;
		else
			*val = (u->avc[reg] & ~0x803f0000u) |
			       ((uint32_t)(u->abit & 31u) << 16);
		return 1;
	case N2VLD_AVC_RESET:
		/* Written 1 then 0 by the init sequence.  It resets the syntax engine, not the
		 * reader position: the position is latched by VLD 0x2c/0x42/0x8c, and the LAT
		 * trace writes 0xa3 = 0 after the latch.  [I] */
		if (wr)
			u->avc[reg] = *val;
		else
			*val = u->avc[reg];
		return 1;
	case N2VLD_AVC_UE:
		if (wr)
			return 0;
		*val = ue(v, u);
		return 1;
	case N2VLD_AVC_SE:
		if (wr)
			return 0;
		*val = se(v, u);
		return 1;
	case N2VLD_AVC_SC_SEARCH:
		if (wr) {
			u->avc[reg] = *val & ~1u;
			if (*val & 1u)
				(void)search(v, u, (u->abit + 7u) >> 3);
		} else {
			*val = u->avc[reg] & ~1u;
		}
		return 1;
	case N2VLD_AVC_SC_STAT:
		if (wr)
			u->avc[reg] = *val;
		else
			*val = u->avc[reg] & ~0x800u;
		return 1;
	case N2VLD_AVC_TRIG:
		if (wr && (*val & 1u)) {
			v->st.triggers++;
			/* No pixels: stand in for "the slice was consumed" by moving to the next
			 * start code.  The fake still owns the register write and its IRQ logic. */
			(void)search(v, u, ((u->abit + 7u) >> 3) + 3u);
		}
		return 0;
	default:
		return 0;
	}
}

static int hevc_op(struct n2vld *v, struct n2vld_unit *u, uint32_t reg, int wr, uint32_t *val)
{
	v->st.hevc_reads += wr ? 0u : 1u;
	v->st.hevc_writes += wr ? 1u : 0u;

	if (reg >= N2VLD_HEVC_WORDS)
		return 0;
	if (reg <= N2VLD_AVC_BITS_MAX) {
		if (wr) {
			u->hevc[reg] = *val;
			return 1;
		}
		if (reg) {
			unsigned i;

			for (i = 0; i < reg; i++)
				(void)next_bit(v, u, &u->abit);
			v->st.shifts++;
			v->st.bits_read += reg;
		}
		*val = peek_bits(v, u, u->abit, 32u);
		v->st.peeks++;
		return 1;
	}
	switch (reg) {
	case N2VLD_AVC_CTRL:
		if (wr)
			u->hevc[reg] = *val;
		else
			*val = (u->hevc[reg] & ~0x700u) |
			       ((uint32_t)(u->abit & 7u) << 8);
		return 1;
	case N2VLD_HEVC_UE:
		if (wr)
			return 0;
		*val = ue(v, u);
		return 1;
	case N2VLD_HEVC_SE:
		if (wr)
			return 0;
		*val = se(v, u);
		return 1;
	default:
		return 0;
	}
}

/* ---- MISC waits and larb sleep-protect ----------------------------------------------------- */

static int misc_op(uint32_t off, int wr, uint32_t *val)
{
	if (wr)
		return 0;
	if (off >= 0x110u && off <= 0x13cu) {
		*val = 0;                 /* "all DRAM and bits processed" wait reads 0 */
		return 1;
	}
	return 0;
}

static int larb_op(struct n2vld_unit *u, int wr, uint32_t *val)
{
	if (wr)
		u->larb_slp = *val;
	else
		*val = (u->larb_slp & ~0x10000u) | ((u->larb_slp & 1u) << 16);
	return 1;
}

/* ---- dispatch ------------------------------------------------------------------------------ */

static int in_range(uint32_t abs, uint32_t base, uint32_t bytes)
{
	return abs >= base && abs - base < bytes;
}

static int dispatch(struct n2vld *v, uint32_t abs, int wr, uint32_t *val)
{
	if (abs & 3u)
		return 0;
	if (in_range(abs, 0x16000000u + N2VLD_VLD_OFF, 0x400u))
		return vld_op(v, &v->u[N2VLD_UNIT_CORE], (abs - (0x16000000u + N2VLD_VLD_OFF)) / 4u, wr, val);
	if (in_range(abs, 0x16000000u + N2VLD_AVC_VLD_OFF, 0x1000u))
		return avc_op(v, &v->u[N2VLD_UNIT_CORE], (abs - (0x16000000u + N2VLD_AVC_VLD_OFF)) / 4u, wr, val);
	if (in_range(abs, 0x16000000u + N2VLD_HEVC_VLD_OFF, 0x400u))
		return hevc_op(v, &v->u[N2VLD_UNIT_CORE], (abs - (0x16000000u + N2VLD_HEVC_VLD_OFF)) / 4u, wr, val);
	if (in_range(abs, 0x16000000u + N2VLD_CORE_MISC_OFF, 0x4000u))
		return misc_op(abs - (0x16000000u + N2VLD_CORE_MISC_OFF), wr, val);
	if (in_range(abs, 0x16000000u + N2VLD_LAT_VLD_OFF, 0x400u))
		return vld_op(v, &v->u[N2VLD_UNIT_LAT], (abs - (0x16000000u + N2VLD_LAT_VLD_OFF)) / 4u, wr, val);
	if (in_range(abs, 0x16000000u + N2VLD_LAT_AVC_VLD_OFF, 0x1000u))
		return avc_op(v, &v->u[N2VLD_UNIT_LAT], (abs - (0x16000000u + N2VLD_LAT_AVC_VLD_OFF)) / 4u, wr, val);
	if (in_range(abs, 0x16000000u + N2VLD_LAT_MISC_OFF, 0x1000u))
		return misc_op(abs - (0x16000000u + N2VLD_LAT_MISC_OFF), wr, val);
	if (abs == 0x16000000u + N2VLD_LARB4_OFF + 0x00cu)
		return larb_op(&v->u[N2VLD_UNIT_CORE], wr, val);
	if (abs == 0x16000000u + N2VLD_LARB5_OFF + 0x00cu)
		return larb_op(&v->u[N2VLD_UNIT_LAT], wr, val);
	return 0;
}

/* ---- public API ---------------------------------------------------------------------------- */

struct n2vld *n2vld_create(const struct n2vld_bus *bus)
{
	struct n2vld *v = calloc(1, sizeof *v);

	if (!v)
		return NULL;
	if (bus) {
		v->bus = *bus;
		v->have_bus = bus->read_pa != NULL;
	}
	v->map[0].win = N2VLD_WIN_VDEC_VLD;
	v->map[0].base = 0x16000000u + N2VLD_VLD_OFF;
	v->map[0].bytes = N2VLD_WIN_BYTES;
	v->map[1].win = N2VLD_WIN_VDEC_MISC;
	v->map[1].base = 0x16000000u + N2VLD_CORE_MISC_OFF;
	v->map[1].bytes = 0x4000u;
	v->map[2].win = N2VLD_WIN_VDEC_LAT_MISC;
	v->map[2].base = 0x16000000u + N2VLD_LAT_MISC_OFF;
	v->map[2].bytes = N2VLD_WIN_BYTES;
	v->map[3].win = N2VLD_WIN_VDEC_LAT_VLD;
	v->map[3].base = 0x16000000u + N2VLD_LAT_VLD_OFF;
	v->map[3].bytes = 0x400u;
	v->map[4].win = N2VLD_WIN_VDEC_ALL;
	v->map[4].base = 0x16000000u;
	v->map[4].bytes = 0x30000u;
	v->nmap = 5;
	return v;
}

void n2vld_destroy(struct n2vld *v)
{
	unsigned i;

	if (!v)
		return;
	for (i = 0; i < N2VLD_UNITS; i++)
		free(v->u[i].raw);
	free(v->direct);
	free(v->rbsp);
	free(v);
}

void n2vld_reset(struct n2vld *v)
{
	unsigned i;

	if (!v)
		return;
	for (i = 0; i < N2VLD_UNITS; i++) {
		v->u[i].abit = 0;
		v->u[i].l0 = 0;
		v->u[i].fetch_ready = 0;
		v->u[i].loaded = 0;
		v->u[i].raw_len = 0;
		v->u[i].raw_start = 0;
	}
	v->have_direct = 0;
	v->direct_len = 0;
	v->rbsp_len = 0;
	memset(&v->st, 0, sizeof v->st);
	set_err(v, "", 0);
}

int n2vld_map_window(struct n2vld *v, unsigned win, uint32_t base, uint32_t bytes)
{
	unsigned i;

	if (!v || bytes < 4u)
		return -EINVAL;
	for (i = 0; i < v->nmap; i++) {
		if (v->map[i].win == win) {
			v->map[i].base = base;
			v->map[i].bytes = bytes;
			return 0;
		}
	}
	if (v->nmap >= N2VLD_MAX_WINS)
		return -ENOSPC;
	v->map[v->nmap].win = win;
	v->map[v->nmap].base = base;
	v->map[v->nmap].bytes = bytes;
	v->nmap++;
	return 0;
}

static int find_win(const struct n2vld *v, unsigned win, uint32_t *base, uint32_t *bytes)
{
	unsigned i;

	for (i = 0; i < v->nmap; i++) {
		if (v->map[i].win == win) {
			*base = v->map[i].base;
			*bytes = v->map[i].bytes;
			return 1;
		}
	}
	return 0;
}

int n2vld_hook(void *ctx, unsigned win, uint32_t off, int is_write, uint32_t *value)
{
	struct n2vld *v = ctx;
	uint32_t base, bytes;

	if (!v || !value)
		return -EINVAL;
	if (!find_win(v, win, &base, &bytes))
		return 0;
	if (off > bytes || bytes - off < 4u)
		return 0;

	v->st.reads += is_write ? 0u : 1u;
	v->st.writes += is_write ? 1u : 0u;
	return dispatch(v, base + off, is_write, value);
}

int n2vld_set_bytes(struct n2vld *v, const void *bytes, uint32_t len)
{
	unsigned i;
	int rc;

	if (!v || (!bytes && len))
		return -EINVAL;
	rc = ensure(&v->direct, &v->direct_cap, len ? len : 1u);
	if (rc) {
		set_err(v, "direct buffer", rc);
		return rc;
	}
	if (len)
		memcpy(v->direct, bytes, len);
	v->direct_len = len;
	v->have_direct = 1;

	rc = ensure(&v->rbsp, &v->rbsp_cap, len ? len : 1u);
	if (rc) {
		set_err(v, "rbsp buffer", rc);
		return rc;
	}
	if (len)
		strip_epb(bytes, len, v->rbsp, &v->rbsp_len);
	else
		v->rbsp_len = 0;
	for (i = 0; i < N2VLD_UNITS; i++) {
		v->u[i].abit = 0;
		v->u[i].l0 = 0;
		v->u[i].fetch_ready = 0;
		v->u[i].loaded = 0;
	}
	set_err(v, "", 0);
	return 0;
}

int n2vld_set_vfifo_unit(struct n2vld *v, unsigned unit, uint32_t start, uint32_t end)
{
	unsigned win;
	uint32_t val;

	if (!v || unit >= N2VLD_UNITS || (start & 63u) || (end & 63u))
		return -EINVAL;
	win = unit == N2VLD_UNIT_CORE ? N2VLD_WIN_VDEC_VLD : N2VLD_WIN_VDEC_LAT_VLD;
	val = start >> 6;
	if (n2vld_hook(v, win, N2VLD_VLD_VFIFO_START * 4u, 1, &val) != 1)
		return -EINVAL;
	val = end >> 6;
	if (n2vld_hook(v, win, N2VLD_VLD_VFIFO_END * 4u, 1, &val) != 1)
		return -EINVAL;
	return 0;
}

int n2vld_set_vfifo(struct n2vld *v, uint32_t start, uint32_t end)
{
	return n2vld_set_vfifo_unit(v, N2VLD_UNIT_CORE, start, end);
}

const uint8_t *n2vld_rbsp(const struct n2vld *v, uint32_t *len)
{
	if (len)
		*len = v ? v->rbsp_len : 0;
	return v ? v->rbsp : NULL;
}

uint32_t n2vld_rbsp_len(const struct n2vld *v)
{
	return v ? v->rbsp_len : 0;
}

uint64_t n2vld_bitpos_unit(const struct n2vld *v, unsigned unit)
{
	return v && unit < N2VLD_UNITS ? v->u[unit].abit : 0;
}

uint64_t n2vld_bitpos(const struct n2vld *v)
{
	return n2vld_bitpos_unit(v, N2VLD_UNIT_CORE);
}

uint32_t n2vld_vfifo_start_unit(const struct n2vld *v, unsigned unit)
{
	return v && unit < N2VLD_UNITS ? v->u[unit].vstart : 0;
}

uint32_t n2vld_vfifo_end_unit(const struct n2vld *v, unsigned unit)
{
	return v && unit < N2VLD_UNITS ? v->u[unit].vend : 0;
}

uint32_t n2vld_vfifo_start(const struct n2vld *v)
{
	return n2vld_vfifo_start_unit(v, N2VLD_UNIT_CORE);
}

uint32_t n2vld_vfifo_end(const struct n2vld *v)
{
	return n2vld_vfifo_end_unit(v, N2VLD_UNIT_CORE);
}

uint32_t n2vld_rptr_unit(const struct n2vld *v, unsigned unit)
{
	if (!v || unit >= N2VLD_UNITS)
		return 0;
	return rptr_value(&v->u[unit]);
}

uint32_t n2vld_reg(const struct n2vld *v, unsigned win, uint32_t reg)
{
	if (!v)
		return 0;
	if (win == N2VLD_WIN_VDEC_VLD)
		return reg < N2VLD_VLD_WORDS ? v->u[N2VLD_UNIT_CORE].vld[reg] : 0;
	if (win == N2VLD_WIN_VDEC_LAT_VLD)
		return reg < N2VLD_VLD_WORDS ? v->u[N2VLD_UNIT_LAT].vld[reg] : 0;
	if (win == N2VLD_WIN_VDEC_ALL) {
		uint32_t abs;

		if (reg > 0x30000u / 4u)
			return 0;
		abs = reg * 4u;
		if (abs >= N2VLD_VLD_OFF && abs < N2VLD_VLD_OFF + 0x400u)
			return v->u[N2VLD_UNIT_CORE].vld[(abs - N2VLD_VLD_OFF) / 4u];
		if (abs >= N2VLD_AVC_VLD_OFF && abs < N2VLD_AVC_VLD_OFF + 0x1000u)
			return v->u[N2VLD_UNIT_CORE].avc[(abs - N2VLD_AVC_VLD_OFF) / 4u];
		if (abs >= N2VLD_LAT_VLD_OFF && abs < N2VLD_LAT_VLD_OFF + 0x400u)
			return v->u[N2VLD_UNIT_LAT].vld[(abs - N2VLD_LAT_VLD_OFF) / 4u];
		if (abs >= N2VLD_LAT_AVC_VLD_OFF && abs < N2VLD_LAT_AVC_VLD_OFF + 0x1000u)
			return v->u[N2VLD_UNIT_LAT].avc[(abs - N2VLD_LAT_AVC_VLD_OFF) / 4u];
	}
	return 0;
}

void n2vld_stats(const struct n2vld *v, struct n2vld_stats *out)
{
	if (!out)
		return;
	if (!v) {
		memset(out, 0, sizeof *out);
		return;
	}
	*out = v->st;
}

const char *n2vld_last_error(const struct n2vld *v)
{
	return v ? v->err : "";
}
