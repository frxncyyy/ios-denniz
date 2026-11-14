/* SPDX-License-Identifier: MIT */
/*
 * n2jdec_parse.c -- original baseline JPEG (ITU-T T.81, SOF0) header parser.
 *
 * Scope: interchange-format baseline JPEG, 8-bit precision, 1 or 3 components, exactly one scan.
 * Parses SOI, APPn/COM (skipped), DQT, DHT, DRI, SOF0, SOS and walks the entropy-coded segment to
 * EOI (byte stuffing and RSTn handled). Progressive/extended/arithmetic frames are rejected with a
 * specific code; the caller can then fall back to software.
 *
 * No libc, no allocation, no printing. Every length is checked before use.
 */
#include "n2jdec.h"

/* ---- marker codes ----------------------------------------------------------------------- */
#define M_SOF0 0xc0
#define M_SOF1 0xc1
#define M_SOF2 0xc2
#define M_SOF3 0xc3
#define M_DHT  0xc4
#define M_SOF5 0xc5
#define M_SOF6 0xc6
#define M_SOF7 0xc7
#define M_JPG  0xc8
#define M_SOF9 0xc9
#define M_SOF10 0xca
#define M_SOF11 0xcb
#define M_DAC  0xcc
#define M_SOF13 0xcd
#define M_SOF14 0xce
#define M_SOF15 0xcf
#define M_RST0 0xd0
#define M_RST7 0xd7
#define M_SOI  0xd8
#define M_EOI  0xd9
#define M_SOS  0xda
#define M_DQT  0xdb
#define M_DNL  0xdc
#define M_DRI  0xdd
#define M_TEM  0x01

#define HMAX_LIMIT 4u
#define BLOCKS_MAX N2JDEC_MAX_BLOCKS

static uint32_t
n2j_align_u32(uint32_t v, uint32_t a)
{
	return (v + a - 1u) & ~(a - 1u);
}

const char *
n2jdec_strerror(int code)
{
	switch (code) {
	case N2JDEC_OK: return "ok";
	case N2JDEC_E_ARG: return "bad argument";
	case N2JDEC_E_TRUNC: return "truncated segment";
	case N2JDEC_E_MARKER: return "bad marker";
	case N2JDEC_E_SOF: return "bad or missing SOF0";
	case N2JDEC_E_PRECISION: return "precision is not 8 bit";
	case N2JDEC_E_COMPONENTS: return "component count is not 1 or 3";
	case N2JDEC_E_SAMPLING: return "sampling factors out of range";
	case N2JDEC_E_RANGE: return "picture or MCU count out of range";
	case N2JDEC_E_DQT: return "bad DQT";
	case N2JDEC_E_DHT: return "bad DHT or missing table";
	case N2JDEC_E_DRI: return "bad DRI";
	case N2JDEC_E_SOS: return "bad SOS";
	case N2JDEC_E_NOT_BASELINE: return "not a baseline single-scan JPEG";
	case N2JDEC_E_NO_EOI: return "no EOI";
	case N2JDEC_E_UNSUPPORTED: return "valid but not supported by the MTK decoder";
	case N2JDEC_E_ADDR: return "misaligned or out-of-range address";
	default: return "unknown error";
	}
}

/* mtk_jpeg_dec_hw.c:73-109, rewritten. */
static void
n2j_calc_mcu(n2jdec_jpeg *j)
{
	unsigned i;
	uint32_t blk = 0;
	uint32_t comp = 0;

	j->hmax = 1;
	j->vmax = 1;
	for (i = 0; i < j->ncomp; i++) {
		if (j->comp[i].h > j->hmax)
			j->hmax = j->comp[i].h;
		if (j->comp[i].v > j->vmax)
			j->vmax = j->comp[i].v;
	}

	j->mcu_w = (j->width + 8u * j->hmax - 1u) / (8u * j->hmax);
	j->mcu_h = (j->height + 8u * j->vmax - 1u) / (8u * j->vmax);
	j->total_mcu = j->mcu_w * j->mcu_h;
	j->unit_num = ((j->width + 7u) >> 3) * ((j->height + 7u) >> 3);

	j->blk_num = 0;
	for (i = 0; i < N2JDEC_MAX_COMPS; i++) {
		if (i >= j->ncomp)
			continue;
		j->comp[i].blocks_per_mcu = (uint32_t)j->comp[i].h * j->comp[i].v;
		j->comp[i].du_w = j->mcu_w * j->comp[i].h;
		j->comp[i].du_h = j->mcu_h * j->comp[i].v;
		j->comp[i].du_count = j->comp[i].du_w * j->comp[i].du_h;
		j->blk_num += j->comp[i].blocks_per_mcu;
	}

	j->membership = 0;
	for (i = 0; i < BLOCKS_MAX; i++) {
		if (i < j->blk_num && comp < j->ncomp) {
			uint32_t tmp = 0x04u + (comp & 0x3u);
			j->membership |= tmp << (i * 3u);
			blk++;
			if (blk == j->comp[comp].blocks_per_mcu) {
				comp++;
				blk = 0;
			}
		} else {
			j->membership |= 7u << (i * 3u);
		}
	}
	j->du_ctrl = j->membership;
	if (j->ncomp == 1)
		j->du_ctrl = 0x3ffffffcu;
	j->du_ctrl |= (1u << 30);                    /* gmc = 1, mtk_jpeg_dec_hw.c:391 */
	if (j->ncomp == 1)
		j->du_ctrl |= (1u << 31);            /* isgray */
}

/* mtk_jpeg_dec_hw.c:35-71 (format table) rewritten. */
static void
n2j_classify(n2jdec_jpeg *j)
{
	uint32_t sc;

	sc = ((uint32_t)j->comp[0].h << 20) | ((uint32_t)j->comp[0].v << 16);
	if (j->ncomp > 1)
		sc |= ((uint32_t)j->comp[1].h << 12) | ((uint32_t)j->comp[1].v << 8);
	if (j->ncomp > 2)
		sc |= ((uint32_t)j->comp[2].h << 4) | (uint32_t)j->comp[2].v;

	j->uv_brz_w = 0;
	j->out_fmt = N2JDEC_FMT_NONE;
	switch (sc) {
	case 0x00111111u: j->sampling = N2JDEC_S_444; j->uv_brz_w = 1; j->out_fmt = N2JDEC_FMT_422P; break;
	case 0x00412121u: j->sampling = N2JDEC_S_422X2; j->out_fmt = N2JDEC_FMT_422P; break;
	case 0x00211111u: j->sampling = N2JDEC_S_422; j->out_fmt = N2JDEC_FMT_422P; break;
	case 0x00121111u: j->sampling = N2JDEC_S_422V; j->uv_brz_w = 1; j->out_fmt = N2JDEC_FMT_420P; break;
	case 0x00222121u: j->sampling = N2JDEC_S_422VX2; j->uv_brz_w = 1; j->out_fmt = N2JDEC_FMT_420P; break;
	case 0x00221111u: j->sampling = N2JDEC_S_420; j->out_fmt = N2JDEC_FMT_420P; break;
	case 0x00110000u: j->sampling = N2JDEC_S_400; j->out_fmt = N2JDEC_FMT_GREY; break;
	default: j->sampling = N2JDEC_S_UNKNOWN; break;
	}
}

/* mtk_jpeg_dec_hw.c:111-135 (DMA grouping) rewritten. */
static void
n2j_calc_dma(n2jdec_jpeg *j)
{
	uint32_t factor_mcu = 3;

	if (j->sampling == N2JDEC_S_444 && j->out_fmt == N2JDEC_FMT_422P)
		factor_mcu = 4;
	else if (j->sampling == N2JDEC_S_422V && j->out_fmt == N2JDEC_FMT_420P)
		factor_mcu = 4;
	else if (j->sampling == N2JDEC_S_422X2 && j->out_fmt == N2JDEC_FMT_422P)
		factor_mcu = 2;
	else if (j->sampling == N2JDEC_S_400)
		factor_mcu = 4;

	j->dma_mcu = 1u << factor_mcu;
	j->dma_group = j->mcu_w / j->dma_mcu;
	j->dma_last_mcu = j->mcu_w % j->dma_mcu;
	if (j->dma_last_mcu != 0)
		j->dma_group++;
	else
		j->dma_last_mcu = j->dma_mcu;
}

/* mtk_jpeg_dec_hw.c:137-176 (destination geometry) rewritten. */
static void
n2j_calc_dst(n2jdec_jpeg *j)
{
	unsigned i;

	for (i = 0; i < N2JDEC_MAX_COMPS; i++) {
		uint32_t brz, padding_w, ds_row_h;

		if (i >= j->ncomp)
			continue;
		brz = (i == 0) ? 0u : j->uv_brz_w;
		padding_w = j->mcu_w * 8u * j->comp[i].h;
		j->comp[i].comp_w = n2j_align_u32(padding_w >> brz, 8u);
		j->comp[i].stride = (i == 0) ?
			n2j_align_u32(j->comp[i].comp_w, 32u) :
			n2j_align_u32(j->comp[i].comp_w, 16u);
		ds_row_h = 8u * j->comp[i].v;
		j->comp[i].comp_h = ds_row_h * j->mcu_h;
		j->comp[i].plane_bytes = j->comp[i].stride * j->comp[i].comp_h;
	}
}

static int
n2j_parse_sof0(const uint8_t *b, size_t seg, size_t seglen, n2jdec_jpeg *j)
{
	unsigned nf, i;
	uint32_t blocks = 0;

	if (seglen < 6)
		return N2JDEC_E_SOF;
	j->precision = b[seg];
	j->height = ((uint32_t)b[seg + 1] << 8) | b[seg + 2];
	j->width = ((uint32_t)b[seg + 3] << 8) | b[seg + 4];
	nf = b[seg + 5];
	if (seglen != 6u + 3u * (size_t)nf)
		return N2JDEC_E_SOF;
	if (j->precision != 8)
		return N2JDEC_E_PRECISION;
	if (j->width == 0 || j->height == 0)
		return N2JDEC_E_RANGE;
	if (nf != 1 && nf != 3)
		return N2JDEC_E_COMPONENTS;
	j->ncomp = (uint8_t)nf;
	j->sof_marker = M_SOF0;
	j->baseline = 1;

	for (i = 0; i < nf; i++) {
		const uint8_t *c = b + seg + 6 + 3 * i;
		unsigned h = c[1] >> 4;
		unsigned v = c[1] & 0x0fu;
		unsigned x;

		if (h < 1 || h > HMAX_LIMIT || v < 1 || v > HMAX_LIMIT)
			return N2JDEC_E_SAMPLING;
		if (c[2] > 3)
			return N2JDEC_E_DQT;
		for (x = 0; x < i; x++)
			if (j->comp[x].id == c[0])
				return N2JDEC_E_SOF;
		j->comp[i].present = 1;
		j->comp[i].id = c[0];
		j->comp[i].h = (uint8_t)h;
		j->comp[i].v = (uint8_t)v;
		j->comp[i].tq = c[2];
		blocks += h * v;
	}
	if (blocks > BLOCKS_MAX)
		return N2JDEC_E_SAMPLING;

	n2j_classify(j);
	n2j_calc_mcu(j);
	n2j_calc_dma(j);
	n2j_calc_dst(j);

	if (j->total_mcu == 0 || j->total_mcu > 0x4000000u)
		return N2JDEC_E_RANGE;
	if (j->blk_num == 0 || j->blk_num > BLOCKS_MAX)
		return N2JDEC_E_SAMPLING;
	return N2JDEC_OK;
}

static int
n2j_parse_dqt(const uint8_t *b, size_t seg, size_t seglen, n2jdec_jpeg *j)
{
	size_t p = seg, end = seg + seglen;

	while (p < end) {
		unsigned pq, tq, k;

		pq = b[p] >> 4;
		tq = b[p] & 0x0fu;
		p++;
		if (pq > 1 || tq > 3)
			return N2JDEC_E_DQT;
		if (p + (pq ? 128u : 64u) > end)
			return N2JDEC_E_DQT;
		j->dqt_present[tq] = 1;
		j->dqt_prec[tq] = (uint8_t)pq;
		for (k = 0; k < 64; k++) {
			if (pq == 0) {
				j->dqt[tq][k] = b[p++];
			} else {
				/* 16-bit values are legal in extended sequential frames; baseline
				 * requires Pq = 0. Record the high byte only and reject later. */
				j->dqt[tq][k] = b[p];
				p += 2;
			}
		}
	}
	return N2JDEC_OK;
}

static int
n2j_parse_dht(const uint8_t *b, size_t seg, size_t seglen, n2jdec_jpeg *j)
{
	size_t p = seg, end = seg + seglen;

	while (p < end) {
		unsigned tc, th, k, total = 0;

		tc = b[p] >> 4;
		th = b[p] & 0x0fu;
		p++;
		if (tc > 1 || th > 3)
			return N2JDEC_E_DHT;
		if (p + 16 > end)
			return N2JDEC_E_DHT;
		for (k = 0; k < 16; k++)
			total += b[p + k];
		if (total > 256)
			return N2JDEC_E_DHT;
		p += 16;
		if (p + total > end)
			return N2JDEC_E_DHT;
		p += total;
		if (tc == 0)
			j->dc_present[th] = 1;
		else
			j->ac_present[th] = 1;
		j->huffman_exist = 1;
	}
	return N2JDEC_OK;
}

static int
n2j_parse_sos(const uint8_t *b, size_t seg, size_t seglen, n2jdec_jpeg *j, size_t *scan_start)
{
	unsigned ns, i, x;
	uint32_t seen = 0;

	if (seglen < 1)
		return N2JDEC_E_SOS;
	ns = b[seg];
	if (ns < 1 || ns > j->ncomp || ns != j->ncomp)
		return N2JDEC_E_SOS;
	if (seglen != 4u + 2u * (size_t)ns)
		return N2JDEC_E_SOS;

	for (i = 0; i < ns; i++) {
		unsigned cs = b[seg + 1 + 2 * i];
		unsigned td = b[seg + 2 + 2 * i] >> 4;
		unsigned ta = b[seg + 2 + 2 * i] & 0x0fu;
		unsigned found = 0;

		for (x = 0; x < j->ncomp; x++) {
			if (j->comp[x].id == cs) {
				found = 1;
				if (seen & (1u << x))
					return N2JDEC_E_SOS;
				seen |= 1u << x;
				j->comp[x].td = (uint8_t)td;
				j->comp[x].ta = (uint8_t)ta;
				break;
			}
		}
		if (!found)
			return N2JDEC_E_SOS;
		if (td > 3 || ta > 3)
			return N2JDEC_E_SOS;
		if (!j->dc_present[td] || !j->ac_present[ta])
			return N2JDEC_E_DHT;
	}
	if ((seen & ((1u << j->ncomp) - 1u)) != ((1u << j->ncomp) - 1u))
		return N2JDEC_E_SOS;

	/* baseline: Ss = 0, Se = 63, Ah = Al = 0 (T.81 B.2.3) */
	if (b[seg + 1 + 2 * ns] != 0x00 || b[seg + 2 + 2 * ns] != 0x3f ||
	    b[seg + 3 + 2 * ns] != 0x00)
		return N2JDEC_E_NOT_BASELINE;

	*scan_start = seg + seglen;
	return N2JDEC_OK;
}

int
n2jdec_parse(const uint8_t *buf, size_t len, n2jdec_jpeg *out)
{
	size_t p;
	int saw_sof = 0, saw_sos = 0;
	int rc = N2JDEC_OK;

	if (out == NULL)
		return N2JDEC_E_ARG;
	/* Zero without memset (portable, and the kernel build has no libc dependency). */
	{
		uint8_t *z = (uint8_t *)out;
		size_t i;
		for (i = 0; i < sizeof(*out); i++)
			z[i] = 0;
	}
	if (buf == NULL || len < 2)
		return N2JDEC_E_ARG;
	if (buf[0] != 0xff || buf[1] != M_SOI)
		return N2JDEC_E_MARKER;
	p = 2;

	for (;;) {
		uint8_t m;
		unsigned seglen;
		size_t seg, send;

		if (p >= len)
			return N2JDEC_E_TRUNC;
		if (buf[p] != 0xff)
			return N2JDEC_E_MARKER;
		while (p < len && buf[p] == 0xff)
			p++;
		if (p >= len)
			return N2JDEC_E_TRUNC;
		m = buf[p++];
		if (m == 0x00)
			return N2JDEC_E_MARKER;

		if (m == M_EOI) {
			if (!saw_sof || !saw_sos)
				return N2JDEC_E_MARKER;
			out->file_bytes = (uint32_t)p;
			goto done;
		}
		if (m == M_SOI)
			return N2JDEC_E_MARKER;
		if (m >= M_RST0 && m <= M_RST7) {
			if (!saw_sos)
				return N2JDEC_E_MARKER;
			continue;
		}
		if (m == M_TEM)
			continue;

		if (p + 2 > len)
			return N2JDEC_E_TRUNC;
		seglen = ((unsigned)buf[p] << 8) | buf[p + 1];
		p += 2;
		if (seglen < 2)
			return N2JDEC_E_MARKER;
		if ((size_t)(seglen - 2u) > len - p)
			return N2JDEC_E_TRUNC;
		seg = p;
		send = p + (seglen - 2u);

		switch (m) {
		case M_SOF0:
			if (saw_sof)
				return N2JDEC_E_SOF;
			rc = n2j_parse_sof0(buf, seg, send - seg, out);
			if (rc != N2JDEC_OK)
				return rc;
			saw_sof = 1;
			break;
		case M_SOF1: case M_SOF2: case M_SOF3: case M_SOF5: case M_SOF6: case M_SOF7:
		case M_SOF9: case M_SOF10: case M_SOF11: case M_SOF13: case M_SOF14: case M_SOF15:
		case M_DAC:
			out->sof_marker = m;
			return N2JDEC_E_NOT_BASELINE;
		case M_DQT:
			rc = n2j_parse_dqt(buf, seg, send - seg, out);
			if (rc != N2JDEC_OK)
				return rc;
			break;
		case M_DHT:
			rc = n2j_parse_dht(buf, seg, send - seg, out);
			if (rc != N2JDEC_OK)
				return rc;
			break;
		case M_DRI:
			if (send - seg != 2)
				return N2JDEC_E_DRI;
			out->restart_interval = (uint16_t)(((unsigned)buf[seg] << 8) | buf[seg + 1]);
			break;
		case M_SOS: {
			size_t scan_start = 0;
			size_t q;
			unsigned rst = 0;
			int found_eoi = 0;

			if (!saw_sof)
				return N2JDEC_E_SOF;
			rc = n2j_parse_sos(buf, seg, send - seg, out, &scan_start);
			if (rc != N2JDEC_OK)
				return rc;
			saw_sos = 1;

			q = scan_start;
			for (;;) {
				uint8_t c;
				size_t marker_at;

				if (q >= len)
					return N2JDEC_E_NO_EOI;
				c = buf[q++];
				if (c != 0xff)
					continue;
				marker_at = q - 1;
				while (q < len && buf[q] == 0xff)
					q++;
				if (q >= len)
					return N2JDEC_E_NO_EOI;
				c = buf[q++];
				if (c == 0x00)
					continue;              /* stuffed byte */
				if (c >= M_RST0 && c <= M_RST7) {
					rst++;
					continue;
				}
				if (c == M_EOI) {
					out->restart_markers = rst;
					out->scan_bytes = (uint32_t)(marker_at - scan_start);
					out->file_bytes = (uint32_t)q;
					found_eoi = 1;
					break;
				}
				/* DHT/DQT/DRI here, or a second SOS: multiple scans. */
				return N2JDEC_E_NOT_BASELINE;
			}
			if (!found_eoi)
				return N2JDEC_E_NO_EOI;
			goto done;
		}
		default:
			/* APPn, COM, DNL, JPGn, reserved: skip by length. */
			break;
		}
		p = send;
	}

done:
	for (unsigned i = 0; i < out->ncomp; i++) {
		if (!out->dqt_present[out->comp[i].tq])
			return N2JDEC_E_DQT;
		if (out->dqt_prec[out->comp[i].tq] != 0)
			return N2JDEC_E_NOT_BASELINE;
	}
	out->qt_id_word = ((uint32_t)(out->comp[0].tq & 0x0fu) << 8) |
			  ((uint32_t)(out->comp[1].tq & 0x0fu) << 4) |
			  (uint32_t)(out->comp[2].tq & 0x0fu);
	out->comp_id_word = ((uint32_t)out->comp[0].id << 24) |
			    ((uint32_t)out->comp[1].id << 16) |
			    ((uint32_t)out->comp[2].id << 8);
	return N2JDEC_OK;
}

int
n2jdec_words_supported(const n2jdec_jpeg *j)
{
	if (j == NULL || j->ncomp == 0 || !j->baseline)
		return 0;
	/* The open driver's formulas use component 0 as the maximum-sampling
	 * component (mcu_w from sampling_w[0], unit_num from the whole picture,
	 * mtk_jpeg_dec_hw.c:78-83); a stream where it is not has no defined
	 * word set here. */
	if (j->comp[0].h != j->hmax || j->comp[0].v != j->vmax)
		return 0;
	switch (j->sampling) {
	case N2JDEC_S_400:
	case N2JDEC_S_420:
	case N2JDEC_S_422:
	case N2JDEC_S_444:
	case N2JDEC_S_422V:
	case N2JDEC_S_422VX2:
		/* The open driver's mcu_w formula uses 2 + sampling_w[0]
		 * (mtk_jpeg_dec_hw.c:78); it equals the T.81 MCU width only while
		 * H0 <= 2. 422X2 (H0 = 4) and any 4:1:1 stream are therefore
		 * refused for word generation, see REGISTERS.md. */
		if (j->comp[0].h > 2 || j->comp[0].v > 2)
			return 0;
		return 1;
	default:
		return 0;
	}
}
