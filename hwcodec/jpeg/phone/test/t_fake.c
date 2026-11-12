/* SPDX-License-Identifier: MIT */
/*
 * t_fake.c -- host fake of /dev/redwood-jpeg for n2jpeg_phone (opus-jpeg).  Not shipped.
 *
 *  - RWJ_INFO / RWJ_ENCODE validation: K01's rules (nord2_jpegdev.inc): even 16..8192, w*h <= RWJ_MAX_PIXELS, quality 1..100,
 *    stride >= width, chroma_stride 0 or >= width, dst_capacity >= w*h/2 + 4096 (ENOSPC), EINVAL otherwise; no job counted.
 *  - valid encodes go to J02's cjpeg-backed fake (test_j02/fake_dev.c), so every output is a real JPEG.
 *  - power model: powered while a job ran less than RWJ_IDLE_OFF_MS ago.
 *  - N2JPGP_FAKE_MAXPIX=N: encodes above N pixels fail like a block limit (EIO, out_status RWJ_E_TIMEOUT, counted failed).
 *  - RWJ_DECODE: N2JPGP_FAKE_DEC unset/none -> ENOTSUP (the real kernel today); "ffmpeg" -> J04's proposed semantics
 *    (query when dst_stride == 0; dst_c = Cb plane then Cr at dst_capacity_c/2; hardware strides from jdec) with the pixels
 *    decoded by ffmpeg into the hardware layout.
 *  - RWJD_JOB (kernel 0315): N2JPGP_FAKE_JD unset/none -> absent (RWJ_INFO.reserved[0] 0, ENOTTY); e2 | e3 | e4b | fail:
 *    present; the kernel's register checks (allow-list, address class inside the decoder claim, [I] scalars < 2^24); PROBE
 *    returns off*3; a job "decodes" only when the list is the named step (e2: 2015 set with 0x0f8/0x16c; e3: hybrid list with
 *    0x0fc and 0x330 = stride; e4b: 0x334 = height), the pixels decoded by ffmpeg into the planes at the 0x14c/0x150/0x154
 *    addresses (f5: + this picture's 0x160..0x170, kept across jobs; f5i: + index tables 0x5000 apart above 1200 MCUs);
 *    otherwise EIO + RWJ_E_TIMEOUT and nothing copied out, like the kernel.
 *  - unknown ioctl -> ENOTTY.
 */
#define _DEFAULT_SOURCE 1
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rwjpeg.h"
#include "fake_dev.h"
#include "../../../jdec/n2jdec.h"
#include "redwood_jpegdec.h"

static uint64_t last_job_ms, jobs_done, jobs_failed;
static uint32_t last_hw_us;

static uint64_t
ms_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int
enc_valid(const struct rwj_encode *r)
{
	if (r->width < 16 || r->height < 16 || r->width > RWJ_MAX_WIDTH || r->height > RWJ_MAX_HEIGHT ||
	    (r->width | r->height) & 1u || (uint64_t)r->width * r->height > RWJ_MAX_PIXELS)
		return EINVAL;
	if (r->format != RWJ_FMT_NV12 && r->format != RWJ_FMT_NV21)
		return EINVAL;
	if (r->quality < 1 || r->quality > 100 || r->stride < r->width ||
	    (r->chroma_stride != 0 && r->chroma_stride < r->width) || r->flags & ~1u || !r->src_y || !r->src_c || !r->dst)
		return EINVAL;
	if (r->dst_capacity < r->width * r->height / 2u + 4096u)
		return ENOSPC;
	return 0;
}

static int
fake_decode(struct rwj_decode *d)
{
	const char *mode = getenv("N2JPGP_FAKE_DEC");
	n2jdec_jpeg j;
	uint32_t cy, cc, cw = 0, ch = 0;

	if (mode == NULL || strcmp(mode, "ffmpeg") != 0) {
		errno = ENOTSUP;
		return -1;
	}
	if (!d->src || d->src_bytes == 0 || n2jdec_parse((const uint8_t *)(uintptr_t)d->src, d->src_bytes, &j) != 0 ||
	    !n2jdec_words_supported(&j)) {
		errno = EINVAL;
		return -1;
	}
	cy = j.comp[0].plane_bytes;
	cc = j.ncomp == 3 ? 2u * j.comp[1].plane_bytes : 0;
	if (d->dst_stride == 0) {
		d->out_width = j.width; d->out_height = j.height; d->out_format = j.out_fmt;
		d->dst_stride = j.comp[0].stride; d->dst_capacity_y = cy; d->dst_capacity_c = cc; d->out_status = 0;
		return 0;
	}
	if (d->dst_stride != j.comp[0].stride || d->dst_capacity_y < cy || d->dst_capacity_c < cc || !d->dst_y ||
	    (cc && !d->dst_c)) {
		errno = EINVAL;
		return -1;
	}
	/* pixels: ffmpeg -> raw planar in the stream's own sampling */
	const char *pix = j.out_fmt == N2JDEC_FMT_420P ? "yuvj420p" : j.out_fmt == N2JDEC_FMT_422P ? "yuvj422p" : "gray";
	if (j.out_fmt == N2JDEC_FMT_420P) { cw = (j.width + 1) / 2; ch = (j.height + 1) / 2; }
	if (j.out_fmt == N2JDEC_FMT_422P) { cw = (j.width + 1) / 2; ch = j.height; }
	if (j.sampling == N2JDEC_S_444 || j.sampling == N2JDEC_S_422V) {   /* the block scales these (uv_brz_w); not modelled */
		errno = EINVAL;
		return -1;
	}
	char in[256], out[256], cmd[700];
	snprintf(in, sizeof(in), "%s/n2jpgp-dec-%d.jpg", fake_scratch(), (int)getpid());
	snprintf(out, sizeof(out), "%s/n2jpgp-dec-%d.yuv", fake_scratch(), (int)getpid());
	FILE *f = fopen(in, "wb");
	if (!f) { errno = EIO; return -1; }
	fwrite((const void *)(uintptr_t)d->src, 1, d->src_bytes, f);
	fclose(f);
	snprintf(cmd, sizeof(cmd), "ffmpeg -v error -y -i %s -f rawvideo -pix_fmt %s %s", in, pix, out);
	if (system(cmd) != 0) { errno = EIO; return -1; }
	f = fopen(out, "rb");
	if (!f) { errno = EIO; return -1; }
	uint8_t *py = (uint8_t *)(uintptr_t)d->dst_y, *pc = (uint8_t *)(uintptr_t)d->dst_c;
	for (uint32_t y = 0; y < j.height; y++)
		if (fread(py + (size_t)y * d->dst_stride, 1, j.width, f) != j.width) { fclose(f); errno = EIO; return -1; }
	for (int p = 0; p < 2 && cw; p++)
		for (uint32_t y = 0; y < ch; y++)
			if (fread(pc + (size_t)p * (d->dst_capacity_c / 2u) + (size_t)y * j.comp[1].stride, 1, cw, f) != cw) {
				fclose(f); errno = EIO; return -1;
			}
	fclose(f);
	unlink(in);
	unlink(out);
	d->out_width = j.width; d->out_height = j.height; d->out_format = j.out_fmt; d->out_status = 0;
	return 0;
}


/* ---- RWJD_JOB (kernel 0315) ---- */
#define W_(o) (1ull << ((o) >> 2 & 63u))
static const uint64_t jd_ok[4] = { W_(0x090) | W_(0x0f8) | W_(0x0fc), 0xffull << 16 | 3ull << 24 | 3ull << 27 | 1ull << 31,
	W_(0x200) | W_(0x20c) | W_(0x210) | W_(0x224) | W_(0x23c) | W_(0x240) | W_(0x248) | W_(0x24c) | W_(0x270) | W_(0x274),
	W_(0x31c) | W_(0x320) | W_(0x330) | W_(0x334) | W_(0x338) | W_(0x33c) | W_(0x344) | W_(0x36c) | W_(0x370) | W_(0x374) };
static const uint64_t jd_addr[4] = { 0, 0x3full << 16, W_(0x200) | W_(0x248), W_(0x31c) | W_(0x338) | W_(0x36c) | W_(0x370) | W_(0x374) };
static const uint64_t jd_scal[4] = { 0, 1ull << 31, 0, W_(0x330) | W_(0x334) | W_(0x33c) | W_(0x344) };
uint64_t n2jpgp_fake_jd_jobs;

static int
jd_check(const struct rwjd_reg *r)
{
	uint32_t o = r->off;
	if ((o & 3u) || o >= 0x380u) return EINVAL;
	uint64_t b = W_(o); unsigned i = o >> 8;
	if (!(jd_ok[i] & b)) return EPERM;
	if ((jd_addr[i] & b) && r->value && (r->value < RWJD_PA || r->value >= RWJD_LEGAL_END || (r->value & 15u))) return EFAULT;
	if ((jd_scal[i] & b) && r->value >= 0x01000000u) return EFAULT;
	return 0;
}

static int
jd_val(const struct rwjd_reg *r, unsigned n, uint32_t off, uint32_t *v)
{
	for (unsigned i = 0; i < n; i++) if (r[i].off == off) { *v = r[i].value; return 1; }
	return 0;
}

static int
fake_jd(struct rwjd_job *d)
{
	const char *mode = getenv("N2JPGP_FAKE_JD");
	struct rwjd_reg *r = (struct rwjd_reg *)(uintptr_t)d->regs;
	if (!mode || !strcmp(mode, "none")) { errno = ENOTTY; return -1; }
	if ((d->flags & ~1u) || !d->nregs || d->nregs > RWJD_REGS_MAX || !d->regs || d->timeout_ms > RWJD_TIMEOUT_MAX_MS) { errno = EINVAL; return -1; }
	if (d->flags & RWJD_PROBE) {
		for (unsigned i = 0; i < d->nregs; i++) { if ((r[i].off & 3u) || r[i].off >= 0x400u) { errno = EINVAL; return -1; } r[i].value = r[i].off * 3u; }
		d->out_status = 0;
		return 0;
	}
	if (!d->src || d->src_bytes < 4 || d->src_bytes > RWJD_SRC_MAX - 256u || d->dst_bytes > RWJD_OUT_MAX) { errno = EINVAL; return -1; }
	for (unsigned i = 0; i < d->nregs; i++) { int e = jd_check(&r[i]); if (e) { errno = e; return -1; } }
	if (r[d->nregs - 1].off != 0x240u) { errno = EINVAL; return -1; }    /* stricter than the kernel: the trigger must be last */
	n2jpgp_fake_jd_jobs++;
	n2jdec_jpeg j;
	if (n2jdec_parse((const uint8_t *)(uintptr_t)d->src, d->src_bytes, &j) != 0) { errno = EIO; d->out_status = RWJ_E_STALL; return -1; }
	uint32_t v, y = 0, u = 0, w = 0;
	int is_e2 = jd_val(r, d->nregs, 0x0f8, &v) && jd_val(r, d->nregs, 0x16c, &v);
	int is_hy = jd_val(r, d->nregs, 0x0fc, &v);
	int ok = 0;
	/* jp6: the F steps run first; "e2"/"e3" stay the J04 lists (0x344 = 0 / total_mcu-1), "f1" = 2015 set + DU_NUM +
	 * 0x344 = file bytes and no other hybrid-only register */
	if (!strcmp(mode, "e2")) ok = is_e2 && jd_val(r, d->nregs, 0x344, &v) && v == 0;
	else if (!strcmp(mode, "f1")) ok = is_e2 && is_hy && jd_val(r, d->nregs, 0x344, &v) && v == j.file_bytes && !jd_val(r, d->nregs, 0x330, &v) && !jd_val(r, d->nregs, 0x31c, &v);
	else if (!strcmp(mode, "e3")) ok = is_hy && jd_val(r, d->nregs, 0x344, &v) && v == j.total_mcu - 1u && jd_val(r, d->nregs, 0x330, &v) && v == j.comp[0].stride && jd_val(r, d->nregs, 0x334, &v) && v == (j.ncomp == 3 ? j.comp[1].stride : 0);
	else if (!strcmp(mode, "f5") || !strcmp(mode, "f5i")) {
		/* jp6 model: the reset keeps 0x160/0x164/0x16c/0x170 (power-on 0/0/0/0x7ffffff); a job decodes when the hybrid
		 * list has 0x344 = file bytes AND those four hold this picture's values, written now or left by an earlier job */
		static uint32_t keep[4] = { 0, 0, 0, 0x7ffffffu };
		static const uint32_t ko[4] = { 0x160, 0x164, 0x16c, 0x170 };
		n2jdec_addr a = { RWJD_SRC_PA, RWJD_OUT_PA, RWJD_OUT_PA + 0x1000000u, RWJD_OUT_PA + 0x2000000u, RWJD_IDX_PA, 16384u };
		n2jdec_reg s[48]; unsigned sn = 0, good = 0;
		for (unsigned i = 0; i < 4; i++) (void)jd_val(r, d->nregs, ko[i], &keep[i]);
		if (n2jdec_supplementary(&j, &a, s, 48, &sn) == 0)
			for (unsigned i = 0; i < sn; i++) for (unsigned q = 0; q < 4; q++) if (s[i].off == ko[q] && s[i].value == keep[q]) good++;
		ok = is_hy && good == 4 && jd_val(r, d->nregs, 0x344, &v) && v == j.file_bytes && jd_val(r, d->nregs, 0x330, &v);
		/* f5i (jp8 hypothesis 3): pictures above 1200 MCUs also need the index tables 0x5000 apart */
		if (ok && mode[2] == 'i' && j.total_mcu > 1200u) ok = jd_val(r, d->nregs, 0x370, &v) && v == RWJD_IDX_PA + 0x5000u;
	}
	else if (!strcmp(mode, "e4b")) ok = is_hy && jd_val(r, d->nregs, 0x334, &v) && v == j.height;
	d->out_hw_us = 1000; d->out_mcu = ok ? j.total_mcu : 7; d->out_irq = ok ? 1 : 0; d->out_state = 0;
	if (!ok) { d->out_status = RWJ_E_TIMEOUT; errno = EIO; return -1; }
	if (!jd_val(r, d->nregs, 0x14c, &y) && !jd_val(r, d->nregs, 0x140, &y)) { errno = EIO; return -1; }
	(void)(jd_val(r, d->nregs, 0x150, &u) || jd_val(r, d->nregs, 0x144, &u));
	(void)(jd_val(r, d->nregs, 0x154, &w) || jd_val(r, d->nregs, 0x148, &w));
	/* decode with the J04-semantics fake into a scratch layout, then place the planes */
	struct rwj_decode q; memset(&q, 0, sizeof(q));
	q.src = d->src; q.src_bytes = d->src_bytes;
	setenv("N2JPGP_FAKE_DEC", "ffmpeg", 1);
	if (fake_decode(&q) != 0) { errno = EIO; return -1; }
	uint8_t *py = calloc(1, q.dst_capacity_y), *pc = calloc(1, q.dst_capacity_c ? q.dst_capacity_c : 1);
	q.dst_y = (uint64_t)(uintptr_t)py; q.dst_c = (uint64_t)(uintptr_t)pc;
	int rc = fake_decode(&q);
	unsetenv("N2JPGP_FAKE_DEC");
	uint8_t *dst = (uint8_t *)(uintptr_t)d->dst;
	if (rc == 0 && dst) {
		uint32_t cb = q.dst_capacity_c / 2u;
		if (y - RWJD_OUT_PA + q.dst_capacity_y <= d->dst_bytes) memcpy(dst + (y - RWJD_OUT_PA), py, q.dst_capacity_y);
		if (cb && u - RWJD_OUT_PA + cb <= d->dst_bytes) memcpy(dst + (u - RWJD_OUT_PA), pc, cb);
		if (cb && w - RWJD_OUT_PA + cb <= d->dst_bytes) memcpy(dst + (w - RWJD_OUT_PA), pc + cb, cb);
	}
	free(py); free(pc);
	if (rc) { errno = EIO; return -1; }
	d->out_status = 0;
	return 0;
}

int
n2jpgp_test_ioctl(unsigned long request, void *arg)
{
	if (arg == NULL) { errno = EFAULT; return -1; }
	if (request == RWJ_INFO) {
		struct rwj_info *in = (struct rwj_info *)arg;
		memset(in, 0, sizeof(*in));
		in->abi_version = RWJ_ABI_VERSION;
		in->max_pixels = RWJ_MAX_PIXELS;
		in->jobs_done = (uint32_t)jobs_done;
		in->jobs_failed = (uint32_t)jobs_failed;
		in->powered = last_job_ms && ms_now() - last_job_ms < RWJ_IDLE_OFF_MS;
		in->last_hw_us = last_hw_us;
		in->caps = 1u;
		in->reserved[0] = getenv("N2JPGP_FAKE_JD") && strcmp(getenv("N2JPGP_FAKE_JD"), "none") ? RWJD_REV : 0;
		return 0;
	}
	if (request == RWJ_ENCODE) {
		struct rwj_encode *r = (struct rwj_encode *)arg;
		int e = enc_valid(r);
		const char *mp = getenv("N2JPGP_FAKE_MAXPIX");
		if (e) { errno = e; return -1; }
		last_job_ms = ms_now();
		if (mp && (uint64_t)r->width * r->height > strtoull(mp, NULL, 10)) {
			jobs_failed++;
			r->out_status = RWJ_E_TIMEOUT; r->out_bytes = 0; r->out_hw_us = 2000000;
			errno = EIO;
			return -1;
		}
		if (fake_ioctl(&n2fake_dev, request, arg) != 0) { jobs_failed++; return -1; }
		r->out_hw_us = (uint32_t)((uint64_t)r->width * r->height / 450u);   /* ~2.2 ms/MP like the phone's M1 */
		last_hw_us = r->out_hw_us;
		jobs_done++;
		return 0;
	}
	if (request == RWJ_DECODE)
		return fake_decode((struct rwj_decode *)arg);
	if (request == RWJD_JOB)
		return fake_jd((struct rwjd_job *)arg);
	errno = ENOTTY;
	return -1;
}

/* the library handle used by J02's selftest/bench verbs goes through the same fake */
static int lib_ioctl(void *ctx, unsigned long request, void *arg) { (void)ctx; return n2jpgp_test_ioctl(request, arg); }

int
n2rwj_test_open(rwjpeg **out)
{
	return rwjpeg_open_ioctl(out, lib_ioctl, NULL);
}
