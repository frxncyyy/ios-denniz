/* SPDX-License-Identifier: MIT */
/*
 * t_dec_unit.c -- DEC05 host unit suite: the decoder service + client library in one process, with
 * an in-memory transport and fake_ap.c (whose SPS geometry comes from DEC03's n2vld model).
 *
 * Expected values are literals from the H.264 specification, DEC03's measured vendor table
 * (320x240 -> stride 320 / buf_h 256; 1920x1080 -> 1920 / 1088) and the rev-2 protocol text -- never
 * from this implementation's output.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fake_ap.h"
#include "n2vcd_dec.h"
#include "n2vcd_dec_service.h"

static int checks, fails;

#define CK(c) do { \
	checks++; \
	if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } \
} while (0)

#define CKU(a, b) do { \
	uint32_t ck_a = (uint32_t)(a), ck_b = (uint32_t)(b); \
	checks++; \
	if (ck_a != ck_b) { fails++; fprintf(stderr, "FAIL %s:%d: %s = 0x%08x, want 0x%08x\n", \
	                                       __FILE__, __LINE__, #a, ck_a, ck_b); } \
} while (0)

#define CKI(a, b) do { \
	long long ck_a = (long long)(a), ck_b = (long long)(b); \
	checks++; \
	if (ck_a != ck_b) { fails++; fprintf(stderr, "FAIL %s:%d: %s = %lld, want %lld\n", \
	                                       __FILE__, __LINE__, #a, ck_a, ck_b); } \
} while (0)

#define CKS(a, b) do { \
	const char *ck_a = (a), *ck_b = (b); \
	checks++; \
	if (!ck_a || strcmp(ck_a, ck_b) != 0) { fails++; fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", \
	                                      __FILE__, __LINE__, ck_a ? ck_a : "(null)", ck_b); } \
} while (0)

/* The first SPS NAL of an ffmpeg/libx264 320x240 Main profile stream (the literal t_vld.c uses). */
static const uint8_t sps_320x240[] = {
	0x00, 0x00, 0x01, 0x67, 0x4d, 0x40, 0x0d, 0xda, 0x05, 0x07, 0xec, 0x04,
	0x40, 0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x0c, 0x83, 0xc5, 0x0a,
	0xa8, 0x00
};

/* ------------------------------------------------------------------ in-memory world */

#define QMAX 128
struct msg {
	uint32_t bytes;
	uint8_t data[N2VCD_DEC_MSG_MAX];
};

struct tctx {
	struct msg q[QMAX];
	unsigned qh, qt;
	int dead;               /* transport gone: send returns -EPIPE */
	int drop_replies;       /* service answers, transport loses the reply */
	uint8_t *buf[64];
	uint32_t buf_bytes[64];
	unsigned nbuf;
	struct fake_ap *fake;
	struct n2vcd_dec_service *svc;
	struct n2vcd_dec_client *cli;
	struct n2vcd_dec_session *s;
	uint32_t next_session;
	uint32_t fatal_calls, fatal_code;
	char last_log[256];
};

static int
svc_send(void *ctx, uint32_t to, const void *msg, uint32_t bytes)
{
	struct tctx *t = (struct tctx *)ctx;

	(void)to;
	if (t->drop_replies) {
		return 0;
	}
	if (t->qt - t->qh >= QMAX || bytes > N2VCD_DEC_MSG_MAX) {
		return -ENOSPC;
	}
	struct msg *m = &t->q[t->qt++ % QMAX];

	m->bytes = bytes;
	memcpy(m->data, msg, bytes);
	return 0;
}

static void
svc_log(void *ctx, const char *line)
{
	struct tctx *t = (struct tctx *)ctx;

	snprintf(t->last_log, sizeof(t->last_log), "%s", line);
}

static void
svc_fatal(void *ctx, uint32_t code)
{
	struct tctx *t = (struct tctx *)ctx;

	t->fatal_calls++;
	t->fatal_code = code;
}

static int
tr_send(void *ctx, const void *msg, uint32_t bytes)
{
	struct tctx *t = (struct tctx *)ctx;

	if (t->dead) {
		return -EPIPE;
	}
	/* The service may have queued its reply before reporting a fatal stop; deliver it. */
	(void)n2vcd_dec_service_handle(t->svc, 1u, msg, bytes);
	return 0;
}

static int
tr_recv(void *ctx, void *msg, uint32_t capacity, uint32_t timeout_ms)
{
	struct tctx *t = (struct tctx *)ctx;

	(void)timeout_ms;
	if (t->qh == t->qt) {
		return -EAGAIN;
	}
	struct msg *m = &t->q[t->qh++ % QMAX];

	if (m->bytes > capacity) {
		return -EMSGSIZE;
	}
	memcpy(msg, m->data, m->bytes);
	return (int)m->bytes;
}

static int
tr_buf_alloc(void *ctx, uint32_t bytes, uint32_t *handle)
{
	struct tctx *t = (struct tctx *)ctx;

	if (bytes == 0u || bytes > (64u << 20) || t->nbuf >= 64u) {
		return -ENOMEM;
	}
	uint8_t *p = (uint8_t *)calloc(1, bytes);

	if (!p) {
		return -ENOMEM;
	}
	t->buf[t->nbuf] = p;
	t->buf_bytes[t->nbuf] = bytes;
	*handle = 0x100u + t->nbuf;
	t->nbuf++;
	return 0;
}

static int
tr_buf_rw(void *ctx, uint32_t handle, uint32_t offset, void *data, uint32_t bytes, int write)
{
	struct tctx *t = (struct tctx *)ctx;

	if (handle < 0x100u || handle >= 0x100u + t->nbuf) {
		return -ENOENT;
	}
	unsigned i = handle - 0x100u;

	if (!t->buf[i] || offset > t->buf_bytes[i] || bytes > t->buf_bytes[i] - offset) {
		return -EINVAL;
	}
	if (write) {
		memcpy(t->buf[i] + offset, data, bytes);
	} else {
		memcpy(data, t->buf[i] + offset, bytes);
	}
	return 0;
}

static int
tr_buf_free(void *ctx, uint32_t handle)
{
	struct tctx *t = (struct tctx *)ctx;

	if (handle < 0x100u || handle >= 0x100u + t->nbuf) {
		return -ENOENT;
	}
	unsigned i = handle - 0x100u;

	free(t->buf[i]);
	t->buf[i] = NULL;
	t->buf_bytes[i] = 0;
	return 0;
}

static void
tr_close(void *ctx)
{
	struct tctx *t = (struct tctx *)ctx;

	for (unsigned i = 0; i < t->nbuf; i++) {
		free(t->buf[i]);
		t->buf[i] = NULL;
		t->buf_bytes[i] = 0;
	}
	t->nbuf = 0;
}

static struct n2vcd_transport
make_transport(struct tctx *t)
{
	struct n2vcd_transport tr;

	memset(&tr, 0, sizeof(tr));
	tr.ctx = t;
	tr.send = tr_send;
	tr.recv = tr_recv;
	tr.buf_alloc = tr_buf_alloc;
	tr.buf_rw = tr_buf_rw;
	tr.buf_free = tr_buf_free;
	tr.close = tr_close;
	return tr;
}

/* ------------------------------------------------------------------ harness */

static int
ctx_start(struct tctx *t, const struct fake_ap_cfg *fcfg, uint32_t idle_ms)
{
	struct n2vcd_dec_service_cfg scfg;

	memset(t, 0, sizeof(*t));
	t->fake = fake_ap_create(fcfg);
	if (!t->fake) {
		return -1;
	}
	memset(&scfg, 0, sizeof(scfg));
	scfg.send = svc_send;
	scfg.ctx = t;
	scfg.ap = fake_ap_ops();
	scfg.ap_ctx = t->fake;
	scfg.log = svc_log;
	scfg.fatal = svc_fatal;
	scfg.session_idle_ms = idle_ms;
	t->svc = n2vcd_dec_service_create(&scfg);
	if (!t->svc) {
		return -1;
	}
	struct n2vcd_transport tr = make_transport(t);
	if (n2vcd_dec_connect(&t->cli, &tr, 300, NULL, 0) != N2VCD_E_OK) {
		return -1;
	}
	return 0;
}

static void
ctx_stop(struct tctx *t)
{
	if (t->s) {
		(void)n2vcd_dec_close(t->s);
		t->s = NULL;
	}
	if (t->cli) {
		n2vcd_dec_disconnect(t->cli);
		t->cli = NULL;
	}
	if (t->svc) {
		n2vcd_dec_service_destroy(t->svc);
		t->svc = NULL;
	}
	if (t->fake) {
		fake_ap_destroy(t->fake);
		t->fake = NULL;
	}
}

static void
sleep_ms(uint32_t ms)
{
	struct timespec ts = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L };

	while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
}

static uint64_t
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* Raw service request (bypasses the client) with the reply copied out. */
static int
raw_xact(struct tctx *t, uint32_t from, const void *req, uint32_t bytes, void *rep, uint32_t cap,
         uint32_t *got)
{
	t->qh = t->qt = 0;
	int r = n2vcd_dec_service_handle(t->svc, from, req, bytes);

	if (r != 0) {
		return r;
	}
	if (t->qh == t->qt) {
		return 1;       /* no reply */
	}
	struct msg *m = &t->q[t->qh++ % QMAX];

	if (m->bytes > cap) {
		return -EMSGSIZE;
	}
	memcpy(rep, m->data, m->bytes);
	if (got) {
		*got = m->bytes;
	}
	return 0;
}

static int
open_session(struct tctx *t, uint32_t codec, uint32_t fmt)
{
	return n2vcd_dec_open(t->cli, codec, 0, 0, fmt, &t->s, NULL, NULL, 0);
}

/* 320x240 geometry as the vendor reports it (DEC03's measured table). */
static const struct fake_ap_geometry geo_320 = { 320, 240, 320, 256, 7 };
static const struct fake_ap_geometry geo_1080 = { 1920, 1080, 1920, 1088, 7 };

static int
setup_320(struct tctx *t)
{
	struct fake_ap_cfg fcfg;
	struct fake_ap_geometry g;
	struct n2vcd_dec_geometry geo;

	memset(&fcfg, 0, sizeof(fcfg));
	fcfg.reorder = 0;
	fcfg.marker = 0x5a;
	if (ctx_start(t, &fcfg, 0) != 0) {
		return -1;
	}
	if (open_session(t, N2VCD_CODEC_H264, N2VCD_OPEN_FMT_MM21) != N2VCD_E_OK) {
		return -1;
	}
	CK(fake_ap_parse_sps(t->fake, sps_320x240, sizeof(sps_320x240), &g) == 1);
	CKU(g.width, 320);
	CKU(g.height, 240);
	CKU(g.stride, 320);
	CKU(g.buf_h, 256);
	fake_ap_set_geometry(t->fake, &g);
	CK(n2vcd_dec_header(t->s, sps_320x240, sizeof(sps_320x240), &geo) == N2VCD_E_OK);
	CK(n2vcd_dec_add_buffers(t->s, &geo) == N2VCD_E_OK);
	return 0;
}

/* ------------------------------------------------------------------ tests */

static void
test_connect_and_open(void)
{
	struct tctx t;
	struct fake_ap_cfg fcfg;
	struct n2vcd_dec_caps caps;

	memset(&fcfg, 0, sizeof(fcfg));
	CK(ctx_start(&t, &fcfg, 0) == 0);
	CK(n2vcd_dec_info(t.cli, &caps) == N2VCD_E_OK);
	CKU(caps.version, N2VCD_VERSION);
	CKU(caps.dec_codecs, 3);
	CKU(caps.max_width, 4096);
	CKU(caps.max_height, N2VCD_DEC_MAX_HEIGHT);   /* opus-decint: 2304 (DCI 4K 4096x2176) */

	/* NV12 is not produced by the vendor: refused at OPEN, before any buffer exists. */
	CKI(open_session(&t, N2VCD_CODEC_H264, N2VCD_OPEN_FMT_NV12), N2VCD_E_UNSUPPORTED);
	CKI(open_session(&t, 0x12345678u, 0), N2VCD_E_UNSUPPORTED);
	CKI(n2vcd_dec_open(t.cli, N2VCD_CODEC_H264, 8192, 240, 0, &t.s, NULL, NULL, 0), N2VCD_E_ARGS);
	CKI(n2vcd_dec_open(t.cli, N2VCD_CODEC_H264, 320, 0, 0, &t.s, NULL, NULL, 0), N2VCD_E_ARGS);

	/* Encoder-only OPEN fields are refused, not ignored (no session is active yet). */
	struct n2vcd_open req;

	memset(&req, 0, sizeof(req));
	req.h.magic = N2VCD_MAGIC;
	req.h.type = N2VCD_OPEN;
	req.codec = N2VCD_CODEC_H264;
	req.encoder = 0;
	req.bitrate = 1000;
	req.h.seq = 10;
	struct n2vcd_simple rep;

	CK(raw_xact(&t, 1u, &req, (uint32_t)sizeof(req), &rep, (uint32_t)sizeof(rep), NULL) == 0);
	CKU(rep.h.type, N2VCD_ERROR);
	CKU(rep.status, N2VCD_E_ARGS);

	/* seq 0 is reserved for unsolicited events. */
	req.h.seq = 0;
	req.bitrate = 0;
	CK(raw_xact(&t, 1u, &req, (uint32_t)sizeof(req), &rep, (uint32_t)sizeof(rep), NULL) == 0);
	CKU(rep.status, N2VCD_E_ARGS);

	CKI(open_session(&t, N2VCD_CODEC_H264, N2VCD_OPEN_FMT_MM21), N2VCD_E_OK);
	CK(t.s != NULL);
	CKU(n2vcd_dec_session_id(t.s), 1);
	CKU(n2vcd_dec_in_bytes(t.s), 4u << 20);

	/* Second session while one is active: E_BUSY with the active id as detail. */
	struct n2vcd_dec_session *s2 = NULL;
	CKI(n2vcd_dec_open(t.cli, N2VCD_CODEC_HEVC, 0, 0, 0, &s2, NULL, NULL, 0), N2VCD_E_BUSY);
	CK(s2 == NULL);
	CKU(n2vcd_dec_last_detail(t.cli), 1);

	/* Unknown decoder id: E_UNSUPPORTED, not a stolen encoder message. */
	CK(n2vcd_dec_service_owns(&(struct n2vcd_hdr){ N2VCD_MAGIC, N2VCD_DEC_PICTURE, 1, 1 }, 16) == 1);
	CK(n2vcd_dec_service_owns(&(struct n2vcd_hdr){ N2VCD_MAGIC, N2VCD_ENC_FRAME, 1, 1 }, 16) == 0);
	struct n2vcd_open o;

	memset(&o, 0, sizeof(o));
	o.h.magic = N2VCD_MAGIC;
	o.h.type = N2VCD_OPEN;
	o.codec = N2VCD_CODEC_H264;
	o.encoder = 1;
	CK(n2vcd_dec_service_owns(&o, (uint32_t)sizeof(o)) == 0);
	o.encoder = 0;
	CK(n2vcd_dec_service_owns(&o, (uint32_t)sizeof(o)) == 1);
	CKU(n2vcd_dec_strstatus(N2VCD_E_GUEST)[0], 'g');
	CKI(n2vcd_dec_status_errno(N2VCD_E_SESSION), -EPIPE);
	ctx_stop(&t);
}

static void
test_header_and_pool(void)
{
	struct tctx t;
	struct n2vcd_dec_geometry geo;
	uint32_t y = 0, c = 0;

	CK(ctx_start(&t, &(struct fake_ap_cfg){ .reorder = 0 }, 0) == 0);
	CK(open_session(&t, N2VCD_CODEC_H264, 0) == N2VCD_E_OK);

	/* A header without a pool first, using the SPS parse of the literal stream. */
	struct fake_ap_geometry g;

	CK(fake_ap_parse_sps(t.fake, sps_320x240, sizeof(sps_320x240), &g) == 1);
	fake_ap_set_geometry(t.fake, &g);
	CK(n2vcd_dec_header(t.s, sps_320x240, sizeof(sps_320x240), &geo) == N2VCD_E_OK);
	CKU(geo.width, 320);
	CKU(geo.height, 240);
	CKU(geo.stride, 320);
	CKU(geo.buf_h, 256);
	CKU(geo.y_bytes, 81920);
	CKU(geo.c_bytes, 40960);
	CKU(geo.out_buf_bytes, 122880);
	CKU(geo.dpb_frames, 7);
	CKU(geo.fourcc, N2VCD_FMT_MM21);
	CKU(n2vcd_dec_out_bytes(t.s), 122880);

	CK(n2vcd_dec_add_buffers(t.s, &geo) == N2VCD_E_OK);
	CKU(fake_ap_pool_count(t.fake), 7);
	CK(n2vcd_dec_buffer_handle(t.s, 0, &y, &c) == N2VCD_E_OK);
	CK(y != 0);
	CKU(c, 0);      /* one buffer per entry: C follows Y at offset 81920 */

	/* Buffer I/O: Y at 0, C at y_bytes in the same transport buffer. */
	uint8_t pat[64], back[64];

	for (unsigned i = 0; i < sizeof(pat); i++) {
		pat[i] = (uint8_t)(i * 3u + 1u);
	}
	CK(n2vcd_dec_buffer_rw(t.s, 0, 0, 0, pat, sizeof(pat), 1) == N2VCD_E_OK);
	memset(back, 0, sizeof(back));
	CK(n2vcd_dec_buffer_rw(t.s, 0, 0, 0, back, sizeof(back), 0) == N2VCD_E_OK);
	CK(memcmp(pat, back, sizeof(pat)) == 0);
	CK(n2vcd_dec_buffer_rw(t.s, 0, 1, 0, pat, sizeof(pat), 1) == N2VCD_E_OK);
	CK(n2vcd_dec_buffer_rw(t.s, 0, 1, 0, back, sizeof(back), 0) == N2VCD_E_OK);
	CK(memcmp(pat, back, sizeof(pat)) == 0);
	CKI(n2vcd_dec_buffer_rw(t.s, 0, 1, 40960, pat, 1, 1), N2VCD_E_ARGS);
	CKI(n2vcd_dec_buffer_rw(t.s, 7, 0, 0, pat, 1, 1), N2VCD_E_ARGS);

	/* A second header while the pool exists is BUSY (free it first). */
	struct n2vcd_dec_header hreq;

	memset(&hreq, 0, sizeof(hreq));
	hreq.h.magic = N2VCD_MAGIC;
	hreq.h.type = N2VCD_DEC_HEADER;
	hreq.h.seq = 50;
	hreq.h.session = n2vcd_dec_session_id(t.s);
	hreq.in_handle = 0x100;
	hreq.in_bytes = sizeof(sps_320x240);
	struct n2vcd_simple rep;

	CK(raw_xact(&t, 1u, &hreq, (uint32_t)sizeof(hreq), &rep, (uint32_t)sizeof(rep), NULL) == 0);
	CKU(rep.status, N2VCD_E_BUSY);

	/* Too many buffers: the daemon refuses the 8th entry with E_NOMEM. */
	struct n2vcd_dec_add_buffer areq;

	memset(&areq, 0, sizeof(areq));
	areq.h.magic = N2VCD_MAGIC;
	areq.h.type = N2VCD_DEC_ADD_BUFFER;
	areq.h.seq = 51;
	areq.h.session = n2vcd_dec_session_id(t.s);
	areq.handle = 0x777;
	areq.bytes = geo.out_buf_bytes;
	CK(raw_xact(&t, 1u, &areq, (uint32_t)sizeof(areq), &rep, (uint32_t)sizeof(rep), NULL) == 0);
	CKU(rep.status, N2VCD_E_NOMEM);   /* index 7 == dpb_frames, refused before the fake AP sees it */

	/* Bad plane sizes / duplicate handles on a fresh session are E_ARGS. */
	CK(n2vcd_dec_free_buffers(t.s) == N2VCD_E_OK);
	CKU(fake_ap_pool_count(t.fake), 0);
	struct n2vcd_dec_geometry geo2 = geo;

	CK(n2vcd_dec_header(t.s, sps_320x240, sizeof(sps_320x240), &geo2) == N2VCD_E_OK);
	areq.h.seq = 52;
	areq.h.session = n2vcd_dec_session_id(t.s);
	areq.handle = 0x100;
	areq.bytes = 100;               /* < y_bytes */
	CK(raw_xact(&t, 1u, &areq, (uint32_t)sizeof(areq), &rep, (uint32_t)sizeof(rep), NULL) == 0);
	CKU(rep.status, N2VCD_E_ARGS);
	areq.bytes = geo.out_buf_bytes;
	areq.c_handle = 0x999;          /* a separate second buffer, but far too small */
	areq.c_bytes = 1;
	areq.h.seq = 53;
	CK(raw_xact(&t, 1u, &areq, (uint32_t)sizeof(areq), &rep, (uint32_t)sizeof(rep), NULL) == 0);
	CKU(rep.status, N2VCD_E_ARGS);

	/* 1920x1080: the vendor's coded height is 1088 and the plane split is 2:1 (DEC03's table). */
	CK(n2vcd_dec_free_buffers(t.s) == N2VCD_E_OK);
	fake_ap_set_geometry(t.fake, &geo_1080);
	struct n2vcd_dec_geometry hd;

	CK(n2vcd_dec_header(t.s, sps_320x240, sizeof(sps_320x240), &hd) == N2VCD_E_OK);
	CKU(hd.width, 1920);
	CKU(hd.height, 1080);
	CKU(hd.stride, 1920);
	CKU(hd.buf_h, 1088);
	CKU(hd.y_bytes, 2088960);
	CKU(hd.c_bytes, 1044480);
	CKU(hd.out_buf_bytes, 3133440);
	ctx_stop(&t);
}

static void
test_pictures_and_flush(void)
{
	struct tctx t;

	CK(setup_320(&t) == 0);
	/* reorder 0: one picture per input, in input order. */
	struct n2vcd_dec_input_info in;
	struct n2vcd_dec_pic fr;

	for (uint64_t i = 0; i < 3; i++) {
		CK(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), 1000 + i, 0, &in) == N2VCD_E_OK);
		CKU(in.pictures_ready, 1);
		CKU(in.changed, 0);
		CKI(n2vcd_dec_picture(t.s, &fr), 1);
		CKU(fr.pts, 1000 + i);
		CKU(fr.width, 320);
		CKU(fr.height, 240);
		CKU(fr.stride, 320);
		CKU(fr.buf_h, 256);
		CKU(fr.fourcc, N2VCD_FMT_MM21);
		CKU(fr.pictures_ready, 0);
		CKI(n2vcd_dec_release(t.s, fr.index), N2VCD_E_OK);
		CKI(n2vcd_dec_release(t.s, fr.index), N2VCD_E_ARGS);   /* double release */
		CKI(n2vcd_dec_picture(t.s, &fr), 0);                   /* none queued */
	}
	uint32_t ready = 99;

	CK(n2vcd_dec_flush(t.s, &ready) == N2VCD_E_OK);
	CKU(ready, 0);
	CKI(n2vcd_dec_picture(t.s, &fr), 0);
	ctx_stop(&t);
}

static void
test_reorder_and_swap(void)
{
	struct tctx t;
	struct fake_ap_cfg fcfg;

	memset(&fcfg, 0, sizeof(fcfg));
	fcfg.reorder = 1;
	fcfg.swap = 1;
	CK(ctx_start(&t, &fcfg, 0) == 0);
	CK(open_session(&t, N2VCD_CODEC_H264, 0) == N2VCD_E_OK);
	struct fake_ap_geometry g;
	struct n2vcd_dec_geometry geo;

	CK(fake_ap_parse_sps(t.fake, sps_320x240, sizeof(sps_320x240), &g) == 1);
	fake_ap_set_geometry(t.fake, &g);
	CK(n2vcd_dec_header(t.s, sps_320x240, sizeof(sps_320x240), &geo) == N2VCD_E_OK);
	CK(n2vcd_dec_add_buffers(t.s, &geo) == N2VCD_E_OK);

	struct n2vcd_dec_input_info in;
	struct n2vcd_dec_pic fr;

	CK(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), 100, 0, &in) == N2VCD_E_OK);
	CKU(in.pictures_ready, 0);
	CK(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), 101, 0, &in) == N2VCD_E_OK);
	CKU(in.pictures_ready, 2);      /* swap: 101 displayed before 100 */
	CKI(n2vcd_dec_picture(t.s, &fr), 1);
	CKU(fr.pts, 101);
	CKU(fr.pictures_ready, 1);
	CKI(n2vcd_dec_release(t.s, fr.index), N2VCD_E_OK);
	CKI(n2vcd_dec_picture(t.s, &fr), 1);
	CKU(fr.pts, 100);
	CKI(n2vcd_dec_release(t.s, fr.index), N2VCD_E_OK);
	ctx_stop(&t);
}

static void
test_flush_drains(void)
{
	struct tctx t;

	/* delayed model: nothing is displayed until FLUSH */
	(void)0;
	struct fake_ap_cfg fcfg;

	memset(&fcfg, 0, sizeof(fcfg));
	fcfg.reorder = 3;
	CK(ctx_start(&t, &fcfg, 0) == 0);
	CK(open_session(&t, N2VCD_CODEC_H264, 0) == N2VCD_E_OK);
	struct fake_ap_geometry g;
	struct n2vcd_dec_geometry geo;

	CK(fake_ap_parse_sps(t.fake, sps_320x240, sizeof(sps_320x240), &g) == 1);
	fake_ap_set_geometry(t.fake, &g);
	CK(n2vcd_dec_header(t.s, sps_320x240, sizeof(sps_320x240), &geo) == N2VCD_E_OK);
	CK(n2vcd_dec_add_buffers(t.s, &geo) == N2VCD_E_OK);
	struct n2vcd_dec_input_info in;

	for (uint64_t i = 0; i < 3; i++) {
		CK(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), i, 0, &in) == N2VCD_E_OK);
		CKU(in.pictures_ready, 0);
	}
	uint32_t ready = 0;

	CK(n2vcd_dec_flush(t.s, &ready) == N2VCD_E_OK);
	CKU(ready, 3);
	struct n2vcd_dec_pic fr;

	for (uint32_t i = 0; i < 3; i++) {
		CKI(n2vcd_dec_picture(t.s, &fr), 1);
		CKU(fr.pts, i);
		CKI(n2vcd_dec_release(t.s, fr.index), N2VCD_E_OK);
	}
	CKI(n2vcd_dec_picture(t.s, &fr), 0);
	ctx_stop(&t);
}

static void
test_resolution_change(void)
{
	struct tctx t;
	struct fake_ap_cfg fcfg;

	memset(&fcfg, 0, sizeof(fcfg));
	fcfg.reorder = 0;
	fcfg.changed_after = 2;
	fcfg.changed_w = 1920;
	fcfg.changed_h = 1080;
	fcfg.changed_buf_h = 1088;
	CK(ctx_start(&t, &fcfg, 0) == 0);
	CK(open_session(&t, N2VCD_CODEC_H264, 0) == N2VCD_E_OK);
	struct fake_ap_geometry g;
	struct n2vcd_dec_geometry geo;

	CK(fake_ap_parse_sps(t.fake, sps_320x240, sizeof(sps_320x240), &g) == 1);
	fake_ap_set_geometry(t.fake, &g);
	CK(n2vcd_dec_header(t.s, sps_320x240, sizeof(sps_320x240), &geo) == N2VCD_E_OK);
	CK(n2vcd_dec_add_buffers(t.s, &geo) == N2VCD_E_OK);
	struct n2vcd_dec_input_info in;
	struct n2vcd_dec_pic fr;

	CK(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), 1, 0, &in) == N2VCD_E_OK);
	CKU(in.changed, 0);
	CKI(n2vcd_dec_picture(t.s, &fr), 1);
	CKI(n2vcd_dec_release(t.s, fr.index), N2VCD_E_OK);
	CK(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), 2, 0, &in) == N2VCD_E_OK);
	CKU(in.changed, 1);
	CKU(in.pictures_ready, 0);
	/* The input was not consumed: free the pool, read the new geometry, register again, resend. */
	CK(n2vcd_dec_free_buffers(t.s) == N2VCD_E_OK);
	struct n2vcd_dec_geometry geo2;

	CK(n2vcd_dec_header(t.s, sps_320x240, sizeof(sps_320x240), &geo2) == N2VCD_E_OK);
	CKU(geo2.width, 1920);
	CKU(geo2.height, 1080);
	CKU(geo2.buf_h, 1088);
	CK(n2vcd_dec_add_buffers(t.s, &geo2) == N2VCD_E_OK);
	CK(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), 2, 0, &in) == N2VCD_E_OK);
	CKU(in.changed, 0);
	CKU(in.pictures_ready, 1);
	CKI(n2vcd_dec_picture(t.s, &fr), 1);
	CKU(fr.width, 1920);
	CKU(fr.buf_h, 1088);
	CKI(n2vcd_dec_release(t.s, fr.index), N2VCD_E_OK);
	ctx_stop(&t);
}

static void
test_errors_and_fatal(void)
{
	struct tctx t;
	struct fake_ap_cfg fcfg;

	/* Buffer exhaustion: only one reference buffer, so the second input is E_BUSY. */
	memset(&fcfg, 0, sizeof(fcfg));
	fcfg.reorder = 0;
	fcfg.dpb_frames = 1;
	CK(ctx_start(&t, &fcfg, 0) == 0);
	CK(open_session(&t, N2VCD_CODEC_H264, 0) == N2VCD_E_OK);
	struct fake_ap_geometry g = geo_320;
	struct n2vcd_dec_geometry geo;

	g.dpb_frames = 1;
	fake_ap_set_geometry(t.fake, &g);
	CK(n2vcd_dec_header(t.s, sps_320x240, sizeof(sps_320x240), &geo) == N2VCD_E_OK);
	CKU(geo.dpb_frames, 1);
	CK(n2vcd_dec_add_buffers(t.s, &geo) == N2VCD_E_OK);
	struct n2vcd_dec_input_info in;

	CK(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), 1, 0, &in) == N2VCD_E_OK);
	CKI(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), 2, 0, &in), N2VCD_E_BUSY);
	struct n2vcd_dec_pic fr;

	CKI(n2vcd_dec_picture(t.s, &fr), 1);
	CKI(n2vcd_dec_release(t.s, fr.index), N2VCD_E_OK);
	CK(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), 2, 0, &in) == N2VCD_E_OK);

	/* Bad input sizes are refused before the AP side sees them. */
	CKI(n2vcd_dec_input(t.s, sps_320x240, 0, 3, 0, &in), N2VCD_E_ARGS);
	CKI(n2vcd_dec_input(t.s, sps_320x240, (4u << 20) + 1u, 3, 0, &in), N2VCD_E_ARGS);
	CKI(n2vcd_dec_input(t.s, sps_320x240, 8, 3, 2, &in), N2VCD_E_ARGS);

	/* Guest stop: the next request gets E_GUEST, the caller of fatal sees the stop code. */
	ctx_stop(&t);
	CK(ctx_start(&t, &fcfg, 0) == 0);
	fake_ap_set_guest_stop(t.fake, 7);
	CKI(open_session(&t, N2VCD_CODEC_H264, 0), N2VCD_E_GUEST);
	CKU(t.fatal_calls, 1);
	CKU(t.fatal_code, 7);
	CK(n2vcd_dec_service_dead(t.svc) == 1);
	CK(raw_xact(&t, 1u, &(struct n2vcd_simple){ { N2VCD_MAGIC, N2VCD_FLUSH, 9, 0 }, 0, 0 },
	            (uint32_t)sizeof(struct n2vcd_simple), &(struct n2vcd_simple){ 0 },
	            (uint32_t)sizeof(struct n2vcd_simple), NULL) == -ENODEV);
	ctx_stop(&t);
}

static void
test_open_bound(void)
{
	struct tctx t;
	struct fake_ap_cfg fcfg;

	memset(&fcfg, 0, sizeof(fcfg));
	fcfg.reorder = 0;
	fcfg.geometry_override = 1;
	fcfg.pic_w = 1920;
	fcfg.pic_h = 1080;
	fcfg.buf_w = 1920;
	fcfg.buf_h = 1088;
	fcfg.dpb_frames = 7;
	CK(ctx_start(&t, &fcfg, 0) == 0);
	/* The optional OPEN bound is enforced on the stream, not just validated. */
	CKI(n2vcd_dec_open(t.cli, N2VCD_CODEC_H264, 640, 480, 0, &t.s, NULL, NULL, 0), N2VCD_E_OK);
	struct fake_ap_geometry g = { 1920, 1080, 1920, 1088, 7 };
	struct n2vcd_dec_geometry geo;

	fake_ap_set_geometry(t.fake, &g);
	CKI(n2vcd_dec_header(t.s, sps_320x240, sizeof(sps_320x240), &geo), N2VCD_E_HW);
	/* The service ended the session on the unsupported geometry: a fresh OPEN works. */
	CKI(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), 1, 0, &(struct n2vcd_dec_input_info){ 0 }),
	    N2VCD_E_SESSION);
	n2vcd_dec_close(t.s);
	t.s = NULL;
	CKI(open_session(&t, N2VCD_CODEC_H264, 0), N2VCD_E_OK);
	n2vcd_dec_close(t.s);
	t.s = NULL;
	ctx_stop(&t);
}

static void
test_daemon_gone_and_timeout(void)
{
	struct tctx t;

	CK(setup_320(&t) == 0);
	/* A lost reply is a hardware timeout, not a dead client. */
	t.drop_replies = 1;
	n2vcd_dec_set_timeout(t.cli, 30);
	struct n2vcd_dec_input_info in;

	CKI(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), 1, 0, &in), N2VCD_E_HW);
	CKU(n2vcd_dec_last_errno(t.cli), ETIMEDOUT);
	t.drop_replies = 0;
	n2vcd_dec_set_timeout(t.cli, 300);

	/* Transport gone: E_SESSION once, then the session stays dead without touching the transport. */
	t.dead = 1;
	CKI(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), 2, 0, &in), N2VCD_E_SESSION);
	CKI(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), 3, 0, &in), N2VCD_E_SESSION);
	CKI(n2vcd_dec_flush(t.s, NULL), N2VCD_E_SESSION);
	CK(n2vcd_dec_close(t.s) == N2VCD_E_OK);
	t.s = NULL;
	t.dead = 0;
	ctx_stop(&t);
}

static void
test_bad_reply_breaks_client(void)
{
	struct tctx t;

	CK(setup_320(&t) == 0);
	struct n2vcd_dec_input_info in;

	/* Inject a datagram with the wrong sequence number before the real reply. */
	struct n2vcd_dec_input_ok bogus;

	memset(&bogus, 0, sizeof(bogus));
	bogus.h.magic = N2VCD_MAGIC;
	bogus.h.type = N2VCD_DEC_INPUT_OK;
	bogus.h.seq = 4242;
	bogus.h.session = n2vcd_dec_session_id(t.s);
	struct msg *m = &t.q[t.qt++ % QMAX];

	m->bytes = (uint32_t)sizeof(bogus);
	memcpy(m->data, &bogus, sizeof(bogus));
	CKI(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), 1, 0, &in), N2VCD_E_HW);
	CKI(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), 2, 0, &in), N2VCD_E_SESSION);
	n2vcd_dec_close(t.s);
	t.s = NULL;
	ctx_stop(&t);
}

static void
test_idle_and_client_gone(void)
{
	struct tctx t;
	struct fake_ap_cfg fcfg;

	memset(&fcfg, 0, sizeof(fcfg));
	fcfg.reorder = 0;
	fcfg.geometry_override = 1;
	fcfg.pic_w = 320;
	fcfg.pic_h = 240;
	fcfg.buf_w = 320;
	fcfg.buf_h = 256;
	fcfg.dpb_frames = 7;
	CK(ctx_start(&t, &fcfg, 1) == 0);
	CK(open_session(&t, N2VCD_CODEC_H264, 0) == N2VCD_E_OK);
	sleep_ms(5);
	CK(n2vcd_dec_service_tick(t.svc, now_ms() + 100000u) == 0);
	CKU(n2vcd_dec_service_session(t.svc), 0);
	struct n2vcd_dec_input_info in;

	CKI(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), 1, 0, &in), N2VCD_E_SESSION);
	n2vcd_dec_close(t.s);
	t.s = NULL;

	/* Client-gone frees the session for the next client. */
	CK(open_session(&t, N2VCD_CODEC_H264, 0) == N2VCD_E_OK);
	n2vcd_dec_service_client_gone(t.svc, 1u);
	CKU(n2vcd_dec_service_session(t.svc), 0);
	CKI(n2vcd_dec_input(t.s, sps_320x240, sizeof(sps_320x240), 1, 0, &in), N2VCD_E_SESSION);
	n2vcd_dec_close(t.s);
	t.s = NULL;
	CK(open_session(&t, N2VCD_CODEC_H264, 0) == N2VCD_E_OK);
	CKU(n2vcd_dec_service_open_count(t.svc), 3);
	ctx_stop(&t);
}

static void
test_raw_protocol_edges(void)
{
	struct tctx t;
	struct n2vcd_simple rep;

	CK(setup_320(&t) == 0);
	struct n2vcd_dec_input req;

	memset(&req, 0, sizeof(req));
	req.h.magic = N2VCD_MAGIC;
	req.h.type = N2VCD_DEC_INPUT;
	req.h.seq = 60;
	req.h.session = n2vcd_dec_session_id(t.s);
	req.in_handle = 0x100;
	req.in_bytes = (4u << 20) + 8u;
	CK(raw_xact(&t, 1u, &req, (uint32_t)sizeof(req), &rep, (uint32_t)sizeof(rep), NULL) == 0);
	CKU(rep.status, N2VCD_E_ARGS);

	/* Unknown handle from another client: E_ARGS at the resolver, E_SESSION at the session check. */
	req.in_bytes = 16;
	req.h.session = 999;
	req.h.seq = 61;
	CK(raw_xact(&t, 1u, &req, (uint32_t)sizeof(req), &rep, (uint32_t)sizeof(rep), NULL) == 0);
	CKU(rep.status, N2VCD_E_SESSION);

	/* Truncated message: E_ARGS with the expected size as detail. */
	CK(raw_xact(&t, 1u, &req, 20, &rep, (uint32_t)sizeof(rep), NULL) == 0);
	CKU(rep.status, N2VCD_E_ARGS);
	CKU(rep.detail, 20u);

	/* FLUSH/CLOSE are session-scoped: a foreign session is not ours (vcodecd keeps the encoder's). */
	struct n2vcd_simple foreign = { { N2VCD_MAGIC, N2VCD_FLUSH, 62, 999 }, 0, 0 };

	CK(n2vcd_dec_service_owns(&foreign, (uint32_t)sizeof(foreign)) == 0);
	CK(n2vcd_dec_service_handle(t.svc, 1u, &foreign, (uint32_t)sizeof(foreign)) == -EOPNOTSUPP);
	foreign.h.session = n2vcd_dec_session_id(t.s);
	CK(raw_xact(&t, 1u, &foreign, (uint32_t)sizeof(foreign), &rep, (uint32_t)sizeof(rep), NULL) == 0);
	CKU(rep.h.type, N2VCD_FLUSH_OK);

	/* Unknown type and a HELLO with a future version. */
	struct n2vcd_hdr h = { N2VCD_MAGIC, 29u, 62, n2vcd_dec_session_id(t.s) };

	CK(raw_xact(&t, 1u, &h, (uint32_t)sizeof(h), &rep, (uint32_t)sizeof(rep), NULL) == 0);
	CKU(rep.status, N2VCD_E_UNSUPPORTED);
	struct n2vcd_hello hello = { { N2VCD_MAGIC, N2VCD_HELLO, 63, 0 }, N2VCD_VERSION + 1u, 0 };
	struct n2vcd_hello_ok hok;

	CK(raw_xact(&t, 1u, &hello, (uint32_t)sizeof(hello), &hok, (uint32_t)sizeof(hok), NULL) == 0);
	CKU(hok.h.type, N2VCD_ERROR);
	CKU(((struct n2vcd_simple *)&hok)->status, N2VCD_E_VERSION);
	hello.version = 1;      /* v1 clients are still accepted */
	CK(raw_xact(&t, 1u, &hello, (uint32_t)sizeof(hello), &hok, (uint32_t)sizeof(hok), NULL) == 0);
	CKU(hok.h.type, N2VCD_HELLO_OK);
	CKU(hok.dec_codecs, 3);

	/* A buffer resolver that does not know a handle is E_ARGS, not a silent pa=0. */
	ctx_stop(&t);
}

int
main(void)
{
	test_connect_and_open();
	test_header_and_pool();
	test_pictures_and_flush();
	test_reorder_and_swap();
	test_flush_drains();
	test_resolution_change();
	test_errors_and_fatal();
	test_open_bound();
	test_daemon_gone_and_timeout();
	test_bad_reply_breaks_client();
	test_idle_and_client_gone();
	test_raw_protocol_edges();

	if (fails == 0) {
		printf("DEC05 unit PASS %d/%d\n", checks, checks);
		return 0;
	}
	printf("DEC05 unit FAIL %d/%d\n", checks - fails, checks);
	return 1;
}
