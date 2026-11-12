/* SPDX-License-Identifier: MIT */
/*
 * t_fakevenc_hevc.c -- unit test of n2venc_fake_hevc.c.  Owner: opus-hevc.
 *
 * Writes its own VPS/SPS/PPS for several configurations (the first one is the configuration the vendor
 * library writes for 320x240, facts/HEVC_ENC_PROTOCOL.md section 4), encodes a synthetic NV12 picture
 * through n2venc_hevc_encode_idr() and through the register/DMA entry n2venc_hevc_frame(), and leaves
 * per case in DIR:  caseN.h265 (headers + IDR), caseN.rec (the encoder's reconstruction, yuv420p),
 * caseN.nv12 (the source), caseN.txt ("W H dbk" : dbk 1 = deblocking stays on, so only PSNR applies).
 * run_tests_hevc.sh decodes each stream with ffmpeg and compares.
 *
 *   t_fakevenc_hevc DIR        last line: "N2TEST fakevenc-hevc-unit PASS n/n"
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n2venc_fake_hevc.h"

static unsigned pass, total;
#define CHECK(c, ...) do { total++; if (c) pass++; else { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

struct bw { uint8_t b[256]; unsigned pos; };

static void u(struct bw *w, unsigned n, uint32_t v)
{
	while (n--) {
		if (((v >> n) & 1u))
			w->b[w->pos >> 3] |= (uint8_t)(0x80u >> (w->pos & 7u));
		w->pos++;
	}
}

static void ue(struct bw *w, uint32_t v)
{
	unsigned len = 0;

	while ((v + 1u) >> (len + 1u))
		len++;
	u(w, len, 0);
	u(w, len + 1u, v + 1u);
}

static void se(struct bw *w, int v)
{
	ue(w, v > 0 ? (uint32_t)(2 * v - 1) : (uint32_t)(-2 * v));
}

static void ptl(struct bw *w)
{
	u(w, 2, 0);
	u(w, 1, 0);
	u(w, 5, 1);                     /* Main */
	u(w, 32, 0x60000000u);
	u(w, 4, 0x9);                   /* progressive, !interlaced, !non_packed, frame_only */
	u(w, 32, 0);
	u(w, 12, 0);
	u(w, 8, 93);
}

/* Append NAL `type` with the payload of w (adds rbsp trailing bits and emulation prevention). */
static unsigned nal(uint8_t *out, unsigned type, struct bw *w)
{
	unsigned o = 0, z = 0, n;

	u(w, 1, 1);
	while (w->pos & 7u)
		u(w, 1, 0);
	n = w->pos >> 3;
	out[o++] = 0; out[o++] = 0; out[o++] = 0; out[o++] = 1;
	out[o++] = (uint8_t)(type << 1);
	out[o++] = 1;
	for (unsigned i = 0; i < n; i++) {
		if (z >= 2 && w->b[i] <= 3) {
			out[o++] = 3;
			z = 0;
		}
		out[o++] = w->b[i];
		z = w->b[i] ? 0 : z + 1;
	}
	return o;
}

struct cfg {
	unsigned w, h, min_cb, ctb, min_tb, max_tb, depth_intra, sao, pcm, amp;
	unsigned sign_hiding, tskip, cu_qp_delta, qp_depth, tq_bypass, dbk_ctrl, dbk_override;
	int init_qp, cb_off, cr_off;
};

static unsigned headers(const struct cfg *c, uint8_t *out)
{
	struct bw w;
	unsigned o = 0;

	memset(&w, 0, sizeof w);                         /* VPS */
	u(&w, 4, 0); u(&w, 1, 1); u(&w, 1, 1); u(&w, 6, 0); u(&w, 3, 0); u(&w, 1, 1); u(&w, 16, 0xffff);
	ptl(&w);
	u(&w, 1, 1); ue(&w, 0); ue(&w, 0); ue(&w, 0);
	u(&w, 6, 0); ue(&w, 0); u(&w, 1, 0); u(&w, 1, 0);
	o += nal(out + o, 32, &w);
	memset(&w, 0, sizeof w);                         /* SPS */
	u(&w, 4, 0); u(&w, 3, 0); u(&w, 1, 1);
	ptl(&w);
	ue(&w, 0); ue(&w, 1); ue(&w, c->w); ue(&w, c->h); u(&w, 1, 0);
	ue(&w, 0); ue(&w, 0); ue(&w, 4);
	u(&w, 1, 1); ue(&w, 0); ue(&w, 0); ue(&w, 0);
	ue(&w, c->min_cb - 3); ue(&w, c->ctb - c->min_cb); ue(&w, c->min_tb - 2); ue(&w, c->max_tb - c->min_tb);
	ue(&w, 1); ue(&w, c->depth_intra);
	u(&w, 1, 0); u(&w, 1, c->amp); u(&w, 1, c->sao); u(&w, 1, c->pcm);
	if (c->pcm) {
		u(&w, 4, 7); u(&w, 4, 7); ue(&w, 0); ue(&w, 1); u(&w, 1, 0);   /* PCM CB sizes 8..16 */
	}
	ue(&w, 0); u(&w, 1, 0); u(&w, 1, 0); u(&w, 1, 1); u(&w, 1, 0); u(&w, 1, 0);
	o += nal(out + o, 33, &w);
	memset(&w, 0, sizeof w);                         /* PPS */
	ue(&w, 0); ue(&w, 0); u(&w, 1, 0); u(&w, 1, 0); u(&w, 3, 0); u(&w, 1, c->sign_hiding); u(&w, 1, 0);
	ue(&w, 0); ue(&w, 0); se(&w, c->init_qp - 26); u(&w, 1, 0); u(&w, 1, c->tskip); u(&w, 1, c->cu_qp_delta);
	if (c->cu_qp_delta)
		ue(&w, c->qp_depth);
	se(&w, c->cb_off); se(&w, c->cr_off); u(&w, 1, 0); u(&w, 1, 0); u(&w, 1, 0);
	u(&w, 1, c->tq_bypass); u(&w, 1, 0); u(&w, 1, 0); u(&w, 1, 1); u(&w, 1, c->dbk_ctrl);
	if (c->dbk_ctrl) {
		u(&w, 1, c->dbk_override); u(&w, 1, 0); se(&w, 0); se(&w, 0);
	}
	u(&w, 1, 0); u(&w, 1, 0); ue(&w, 3); u(&w, 1, 0); u(&w, 1, 0);
	o += nal(out + o, 34, &w);
	return o;
}

static void picture(uint8_t *y, uint8_t *uv, unsigned w, unsigned h, unsigned seed)
{
	uint32_t r = seed * 2654435761u + 1u;

	for (unsigned j = 0; j < h; j++)
		for (unsigned i = 0; i < w; i++) {
			int v = (int)((i * 255) / w + (j * 64) / h) / 2 + (((i / 8 + j / 8) & 1) ? 40 : 0);

			r = r * 1103515245u + 12345u;
			v += (int)((r >> 16) % 9) - 4;
			if (i > w / 2 && j > h / 3 && j < (2 * h) / 3)
				v = 230 - (int)((i ^ j) & 31);
			y[j * w + i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
		}
	for (unsigned j = 0; j < h / 2; j++)
		for (unsigned i = 0; i < w / 2; i++) {
			uv[j * w + 2 * i] = (uint8_t)(64 + (i * 128) / (w / 2));
			uv[j * w + 2 * i + 1] = (uint8_t)(192 - (j * 128) / (h / 2));
		}
}

/* --- fake register file + DMA for n2venc_hevc_frame */
static uint8_t *g_mem;
static uint32_t g_mem_bytes;
#define MEM_PA 0x40000000u

static int t_dma(void *ctx, uint32_t pa, void *buf, uint32_t bytes, int write)
{
	(void)ctx;
	if (pa < MEM_PA || pa - MEM_PA > g_mem_bytes || bytes > g_mem_bytes - (pa - MEM_PA))
		return -ERANGE;
	if (write)
		memcpy(g_mem + (pa - MEM_PA), buf, bytes);
	else
		memcpy(buf, g_mem + (pa - MEM_PA), bytes);
	return 0;
}

static int t_avail(void *ctx, uint32_t pa, uint32_t *avail)
{
	(void)ctx;
	if (pa < MEM_PA || pa - MEM_PA >= g_mem_bytes)
		return -ENOENT;
	/* three 1 MiB buffers: source luma, source chroma, bitstream */
	*avail = 0x100000u - ((pa - MEM_PA) & 0xfffffu);
	return 0;
}

static int write_file(const char *dir, const char *name, const void *p, size_t n)
{
	char path[512];
	FILE *f;

	snprintf(path, sizeof path, "%s/%s", dir, name);
	f = fopen(path, "wb");
	if (!f)
		return -1;
	if (fwrite(p, 1, n, f) != n) {
		fclose(f);
		return -1;
	}
	return fclose(f);
}

int main(int argc, char **argv)
{
	static const struct cfg cases[] = {
		/* 0: what libvcodecdrv.so writes for 320x240 (CTB 32, CB 16, TB 8..16, intra depth 0, SAO,
		 *    sign hiding, cu_qp_delta, deblocking override) */
		{ 320, 240, 4, 5, 3, 4, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 1, 26, 0, 0 },
		/* 1: CTB 16 = CU, TB 4..16, depth 1, no SAO/sign hiding/dqp, init_qp 30 */
		{ 176, 144, 3, 4, 2, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 30, 0, 0 },
		/* 2: CTB 64, CB 8, max TB 8 (forced split, chroma 4x4 with transform_skip_flag), PCM, bypass,
		 *    dqp depth 1, chroma QP offsets, 200x120 (partial CTBs, CB-8 boundary) */
		{ 200, 120, 3, 6, 2, 3, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 22, 3, -2 },
		/* 3: as 0 but no deblocking override: deblocking runs in the decoder (PSNR only) */
		{ 320, 240, 4, 5, 3, 4, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 26, 0, 0 },
		/* 4: 1920x1080 in CB 8 units, CTB 64, TB up to 32 (the size a phone encode uses) */
		{ 1920, 1080, 3, 6, 2, 5, 1, 1, 0, 0, 1, 0, 1, 0, 0, 1, 1, 26, 0, 0 },
	};
	const char *dir = argc > 1 ? argv[1] : ".";

	CHECK(n2venc_hevc_set_headers((const uint8_t *)"\0\0\1\x67\x42", 5) == -EINVAL, "H.264 SPS rejected");
	for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; k++) {
		const struct cfg *c = &cases[k];
		uint8_t hdr[512];
		unsigned hn = headers(c, hdr), w = c->w, h = c->h;
		size_t ys = (size_t)w * h, frame_cap = ys * 3;
		uint8_t *y = malloc(ys), *uv = malloc(ys / 2), *rec = malloc(ys * 3 / 2), *out = malloc(hn + frame_cap);
		uint8_t *rec2 = malloc(ys * 3 / 2), *nv12 = malloc(ys * 3 / 2);
		uint32_t n, n2 = 0;
		char name[64];
		struct n2venc_hevc_stats st;

		if (!y || !uv || !rec || !out || !rec2 || !nv12)
			return 2;
		picture(y, uv, w, h, k + 1);
		n2venc_hevc_reset();
		CHECK(n2venc_hevc_set_headers(hdr, hn) == 0, "case %u: headers accepted", k);
		memcpy(out, hdr, hn);
		n = n2venc_hevc_encode_idr(y, uv, w, h, out + hn, (uint32_t)frame_cap, rec);
		CHECK(n > 6, "case %u: encoded %u bytes", k, n);
		CHECK(n2venc_hevc_encode_idr(y, uv, w, h, rec2, 64, NULL) == 0, "case %u: small buffer -> 0", k);
		n2venc_hevc_get_stats(&st);
		CHECK(st.width == w && st.height == h && st.unsupported == 0, "case %u: stats %ux%u unsupported %u",
		      k, st.width, st.height, st.unsupported);
		memcpy(nv12, y, ys);
		memcpy(nv12 + ys, uv, ys / 2);
		snprintf(name, sizeof name, "case%u.h265", k);
		CHECK(write_file(dir, name, out, hn + n) == 0, "write %s", name);
		snprintf(name, sizeof name, "case%u.rec", k);
		CHECK(write_file(dir, name, rec, ys * 3 / 2) == 0, "write %s", name);
		snprintf(name, sizeof name, "case%u.nv12", k);
		CHECK(write_file(dir, name, nv12, ys * 3 / 2) == 0, "write %s", name);
		{
			char txt[64];
			int l = snprintf(txt, sizeof txt, "%u %u %u\n", w, h, c->dbk_override ? 0u : 1u);

			snprintf(name, sizeof name, "case%u.txt", k);
			CHECK(write_file(dir, name, txt, (size_t)l) == 0, "write %s", name);
		}
		if (k == 0) {   /* register/DMA entry: learn headers from the bitstream buffer, then encode */
			uint32_t reg[0x3000 / 4] = { 0 }, cap = 0;
			struct n2venc_hevc_io io = { NULL, reg, t_dma, t_avail };

			g_mem_bytes = 3u << 20;
			g_mem = calloc(g_mem_bytes, 1);
			if (!g_mem)
				return 2;
			memcpy(g_mem, y, ys);
			memcpy(g_mem + 0x100000, uv, ys / 2);
			memcpy(g_mem + 0x200000, hdr, hn);
			reg[0x06c / 4] = MEM_PA >> 4;
			reg[0x070 / 4] = (MEM_PA + 0x100000) >> 4;
			reg[0x064 / 4] = (MEM_PA + 0x200000) >> 4;
			reg[0x068 / 4] = 0x2000;                /* 1 MiB >> 7 */
			reg[0x028 / 4] = ((w / 16) << 20) | ((w / 16) << 11);
			reg[0x000 / 4] = 0x0f68fff5;            /* HEVC_SetHWModeSel value of the vendor run */
			reg[0x114c / 4] = (h << 16) | w;
			n2venc_hevc_reset();
			n2 = (uint32_t)n2venc_hevc_frame(&io, &cap);
			CHECK(n2 == n && cap == 0x100000, "frame entry: %u bytes (direct %u), cap %u", n2, n, cap);
			CHECK(n2 == n && memcmp(g_mem + 0x200000, out + hn, n) == 0, "frame entry: same slice");
			n2venc_hevc_get_stats(&st);
			CHECK(st.headers_learned == 1 && st.frames == 1 && st.dma_errors == 0,
			      "frame entry stats learned %u frames %u dma %u", st.headers_learned, st.frames, st.dma_errors);
			reg[0x068 / 4] = 1;                     /* 128-byte buffer: overflow */
			n2 = (uint32_t)n2venc_hevc_frame(&io, &cap);
			n2venc_hevc_get_stats(&st);
			CHECK(n2 == 0 && cap == 128 && st.overflow == 1, "overflow: %u bytes cap %u", n2, cap);
			/* joined headers (PREPEND): nothing in the buffer -> the vendor configuration, which is
			 * case 0's, so the slice must be the same */
			n2venc_hevc_reset();
			memset(g_mem + 0x200000, 0, 0x1000);
			reg[0x068 / 4] = 0x2000;
			n2 = (uint32_t)n2venc_hevc_frame(&io, &cap);
			n2venc_hevc_get_stats(&st);
			CHECK(n2 == n && memcmp(g_mem + 0x200000, out + hn, n) == 0 && st.defaults == 1,
			      "no headers: vendor defaults, %u bytes (direct %u) defaults %u", n2, n, st.defaults);
			n2venc_hevc_reset();                    /* an H.264 frame: not ours */
			reg[0x000 / 4] = 0x0f60bf70;
			memcpy(g_mem + 0x200000, hdr, hn);
			CHECK(n2venc_hevc_frame(&io, &cap) == -1, "H.264 mode register -> -1");
			free(g_mem);
		}
		free(y); free(uv); free(rec); free(out); free(rec2); free(nv12);
	}
	printf("N2TEST fakevenc-hevc-unit %s %u/%u\n", pass == total ? "PASS" : "FAIL", pass, total);
	return pass == total ? 0 : 1;
}
