/* SPDX-License-Identifier: MIT */
/*
 * t_daemon.c -- host end-to-end test of vcodecd: the daemon process (built from the same sources) speaks
 * include/n2vcd_proto.h over a SOCK_SEQPACKET UNIX socket, with T02's fake n2vc_hw, the fake vhost/AP encoder of
 * daemon/test/fake_vhost.c and the canned H.264 stream made by ffmpeg in run_tests.sh.
 *
 * Covered: HELLO/version, OPEN/OPEN_OK, ENC_FRAME (input handle reaches the AP model, output bytes and parameter sets in the
 * output file), ENC_SET force-key, FLUSH, CLOSE, BUSY for a second client, foreign session, client killed mid-session, decoder
 * and unknown requests, malformed and short messages, guest stop inside a request, guest stop detected by the watchdog,
 * hardware timeout, 10000 random datagrams, and a clean SIGTERM.
 *
 * It is a test driver, not the P03 client library: it exists because client/ is not in the tree yet.  The checks are
 * literals from include/n2vcd_proto.h, not values produced by the daemon's own code.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "n2vcd_proto.h"

static int g_checks;
static int g_fails;
static pid_t g_pid;
static char g_dir[512];
static char g_sock[600];
static char g_log[700];
static char g_fake[600];
static char g_dma[600];
static char g_stream[700];
static char g_seen[700];
static char g_rec[700];
static char g_params[700];
static char g_seqhdr[700];
static char g_cli_path[700];

static int file_contains(const char *path, const char *needle);
static int log_contains(const char *needle);
static void set_int_file(const char *name, int value);
static void clear_file(const char *name);
static pid_t start_daemon_at(const char *bin, const char *sock, const char *dma, const char *log);
static int stop_pid(pid_t pid);
static int start_daemon(const char *vcodecd);
static int start_daemon_x(const char *vcodecd, const char *extra);
static int stop_daemon(void);
static int wait_daemon_exit(int timeout_ms);

#define CHECK(cond, ...)                                                                       \
	do {                                                                                       \
		g_checks++;                                                                            \
		if (!(cond)) {                                                                         \
			g_fails++;                                                                         \
			printf("FAIL %s:%d: ", __FILE__, __LINE__);                                         \
			printf(__VA_ARGS__);                                                               \
			printf("\n");                                                                      \
		}                                                                                      \
	} while (0)

/* One decoded reply.  ERROR is always a n2vcd_simple; the typed replies fill the fields they own. */
struct rep {
	uint32_t type, seq, session;
	uint32_t status, detail;
	uint32_t version, enc_codecs, in_buf_bytes, out_buf_bytes;
	uint32_t out_bytes, key_frame, hw_us, header_bytes;
	uint64_t pts;
	int bytes;
};

static uint64_t
now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void
sleep_ms(long ms)
{
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000L;
	while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
}

static int
path_join(char *out, size_t cap, const char *dir, const char *name)
{
	int n = snprintf(out, cap, "%s/%s", dir, name);
	return n > 0 && (size_t)n < cap ? 0 : -1;
}

static int
write_file(const char *path, const void *data, size_t bytes)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) { return -1; }
	size_t done = 0;
	while (done < bytes) {
		ssize_t n = write(fd, (const uint8_t *)data + done, bytes - done);
		if (n <= 0) { (void)close(fd); return -1; }
		done += (size_t)n;
	}
	(void)close(fd);
	return 0;
}

/* ---- client -------------------------------------------------------------------------------------------------- */

static int
cl_connect(void)
{
	uint64_t deadline = now_ms() + 5000u;
	for (;;) {
		int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
		if (fd < 0) { return -1; }
		struct sockaddr_un sa;
		memset(&sa, 0, sizeof(sa));
		sa.sun_family = AF_UNIX;
		(void)snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", g_sock);
		if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0) { return fd; }
		(void)close(fd);
		if (now_ms() > deadline) { return -1; }
		sleep_ms(20);
	}
}

static int
cl_send(int fd, const void *msg, uint32_t bytes)
{
	ssize_t n = send(fd, msg, bytes, 0);
	return n == (ssize_t)bytes ? 0 : -1;
}

static int
cl_recv(int fd, void *buf, uint32_t cap, int timeout_ms)
{
	struct pollfd p = {fd, POLLIN, 0};
	int r = poll(&p, 1, timeout_ms);
	if (r <= 0) { return r; }
	ssize_t n = recv(fd, buf, cap, 0);
	return (int)n;
}

static void
decode(const uint8_t *buf, int bytes, struct rep *r)
{
	memset(r, 0, sizeof(*r));
	r->bytes = bytes;
	if (bytes < (int)sizeof(struct n2vcd_hdr)) { return; }
	const struct n2vcd_hdr *h = (const struct n2vcd_hdr *)buf;
	r->type = h->type;
	r->seq = h->seq;
	r->session = h->session;
	switch (h->type) {
	case N2VCD_ERROR:
		if (bytes >= (int)sizeof(struct n2vcd_simple)) {
			const struct n2vcd_simple *s = (const struct n2vcd_simple *)buf;
			r->status = s->status;
			r->detail = s->detail;
		}
		break;
	case N2VCD_HELLO_OK:
		if (bytes >= (int)sizeof(struct n2vcd_hello_ok)) {
			const struct n2vcd_hello_ok *m = (const struct n2vcd_hello_ok *)buf;
			r->version = m->version;
			r->enc_codecs = m->enc_codecs;
			r->out_buf_bytes = m->max_width;      /* max_width */
			r->in_buf_bytes = m->max_height;      /* max_height */
		}
		break;
	case N2VCD_OPEN_OK:
		if (bytes >= (int)sizeof(struct n2vcd_open_ok)) {
			const struct n2vcd_open_ok *m = (const struct n2vcd_open_ok *)buf;
			r->status = m->status;
			r->in_buf_bytes = m->in_buf_bytes;
			r->out_buf_bytes = m->out_buf_bytes;
		}
		break;
	case N2VCD_ENC_FRAME_OK:
		if (bytes >= (int)sizeof(struct n2vcd_enc_frame_ok)) {
			const struct n2vcd_enc_frame_ok *m = (const struct n2vcd_enc_frame_ok *)buf;
			r->status = m->status;
			r->out_bytes = m->out_bytes;
			r->key_frame = m->key_frame;
			r->hw_us = m->hw_us;
			r->pts = m->pts;
			r->header_bytes = m->header_bytes;
		}
		break;
	default:
		if (bytes >= (int)sizeof(struct n2vcd_simple)) {
			const struct n2vcd_simple *s = (const struct n2vcd_simple *)buf;
			r->status = s->status;
			r->detail = s->detail;
		}
		break;
	}
}

static int
request(int fd, const void *msg, uint32_t bytes, struct rep *out, int timeout_ms)
{
	const struct n2vcd_hdr *req = (const struct n2vcd_hdr *)msg;
	uint32_t want = req->seq;
	if (cl_send(fd, msg, bytes) != 0) { return -1; }
	uint64_t deadline = now_ms() + (uint64_t)(timeout_ms > 0 ? timeout_ms : 0);
	_Alignas(16) uint8_t buf[128];
	for (;;) {
		uint64_t now = now_ms();
		int remaining = now < deadline ? (int)(deadline - now) : 0;
		int n = cl_recv(fd, buf, sizeof(buf), remaining);
		if (n <= 0) { return -1; }
		decode(buf, n, out);
		if (out->seq == want) { return 0; }
		/* A late reply to an earlier request (the fuzz leaves some in flight): skip it. */
		if (now >= deadline) { return -1; }
	}
}

static void
mk_hdr(struct n2vcd_hdr *h, uint32_t type, uint32_t seq, uint32_t session)
{
	h->magic = N2VCD_MAGIC;
	h->type = type;
	h->seq = seq;
	h->session = session;
}

static int
hello(int fd, uint32_t version, uint32_t seq, struct rep *out, int timeout_ms)
{
	struct n2vcd_hello m;
	memset(&m, 0, sizeof(m));
	mk_hdr(&m.h, N2VCD_HELLO, seq, 0);
	m.version = version;
	return request(fd, &m, (uint32_t)sizeof(m), out, timeout_ms);
}

static int
open_session(int fd, uint32_t codec, uint32_t encoder, uint32_t w, uint32_t h, uint32_t seq, struct rep *out,
             int timeout_ms)
{
	struct n2vcd_open m;
	memset(&m, 0, sizeof(m));
	mk_hdr(&m.h, N2VCD_OPEN, seq, 0);
	m.codec = codec;
	m.encoder = encoder;
	m.width = w;
	m.height = h;
	m.fps_num = 30;
	m.fps_den = 1;
	m.bitrate = 500000;
	m.gop = 6;
	return request(fd, &m, (uint32_t)sizeof(m), out, timeout_ms);
}

static int
open_session_pl(int fd, uint32_t codec, uint32_t w, uint32_t h, uint32_t profile, uint32_t level, uint32_t seq,
                struct rep *out, int timeout_ms)
{
	struct n2vcd_open m;
	memset(&m, 0, sizeof(m));
	mk_hdr(&m.h, N2VCD_OPEN, seq, 0);
	m.codec = codec;
	m.encoder = 1;
	m.width = w;
	m.height = h;
	m.fps_num = 30;
	m.fps_den = 1;
	m.bitrate = 500000;
	m.gop = 6;
	m.profile = profile;
	m.level = level;
	return request(fd, &m, (uint32_t)sizeof(m), out, timeout_ms);
}

/* opus-enc4k: OPEN with an explicit rate, for the automatic level of OPEN.level == 0 */
static int
open_session_rate(int fd, uint32_t codec, uint32_t w, uint32_t h, uint32_t profile, uint32_t level, uint32_t fps_num,
                  uint32_t fps_den, uint32_t bitrate, uint32_t seq, struct rep *out, int timeout_ms)
{
	struct n2vcd_open m;
	memset(&m, 0, sizeof(m));
	mk_hdr(&m.h, N2VCD_OPEN, seq, 0);
	m.codec = codec;
	m.encoder = 1;
	m.width = w;
	m.height = h;
	m.fps_num = fps_num;
	m.fps_den = fps_den;
	m.bitrate = bitrate;
	m.profile = profile;
	m.level = level;
	return request(fd, &m, (uint32_t)sizeof(m), out, timeout_ms);
}

static int
enc_frame(int fd, uint32_t session, uint32_t in, uint32_t stride, uint32_t out, uint32_t cap, uint64_t pts,
          uint32_t key, uint32_t seq, struct rep *reply, int timeout_ms)
{
	struct n2vcd_enc_frame m;
	memset(&m, 0, sizeof(m));
	mk_hdr(&m.h, N2VCD_ENC_FRAME, seq, session);
	m.in_handle = in;
	m.stride = stride;
	m.out_handle = out;
	m.out_capacity = cap;
	m.pts = pts;
	m.force_key = key;
	return request(fd, &m, (uint32_t)sizeof(m), reply, timeout_ms);
}

static int
simple_req(int fd, uint32_t type, uint32_t session, uint32_t seq, struct rep *reply, int timeout_ms)
{
	struct n2vcd_simple m;
	memset(&m, 0, sizeof(m));
	mk_hdr(&m.h, type, seq, session);
	return request(fd, &m, (uint32_t)sizeof(m), reply, timeout_ms);
}

static int
enc_set(int fd, uint32_t session, uint32_t bitrate, uint32_t fps_num, uint32_t fps_den, uint32_t key, uint32_t seq,
        struct rep *reply, int timeout_ms)
{
	struct n2vcd_enc_set m;
	memset(&m, 0, sizeof(m));
	mk_hdr(&m.h, N2VCD_ENC_SET, seq, session);
	m.bitrate = bitrate;
	m.fps_num = fps_num;
	m.fps_den = fps_den;
	m.force_key = key;
	return request(fd, &m, (uint32_t)sizeof(m), reply, timeout_ms);
}

/* ---- DMA buffers are files named by handle (the daemon test hook) -------------------------------------------- */

static int
buf_path(char *out, size_t cap, uint32_t handle)
{
	char name[32];
	(void)snprintf(name, sizeof(name), "%u", handle);
	return path_join(out, cap, g_dma, name);
}

static int
buf_create(uint32_t handle, uint32_t bytes)
{
	char path[700];
	if (buf_path(path, sizeof(path), handle) != 0) { return -1; }
	int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) { return -1; }
	if (ftruncate(fd, (off_t)bytes) != 0) { (void)close(fd); return -1; }
	(void)close(fd);
	return 0;
}

static int
buf_io(uint32_t handle, uint32_t offset, void *data, uint32_t bytes, int write)
{
	char path[700];
	if (buf_path(path, sizeof(path), handle) != 0) { return -1; }
	int fd = open(path, write ? O_WRONLY : O_RDONLY);
	if (fd < 0) { return -1; }
	ssize_t n;
	if (write) { n = pwrite(fd, data, bytes, (off_t)offset); }
	else { n = pread(fd, data, bytes, (off_t)offset); }
	(void)close(fd);
	return n == (ssize_t)bytes ? 0 : -1;
}

static void
fill_nv12(uint8_t *p, uint32_t stride, uint32_t h)
{
	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < stride; x++) { p[y * stride + x] = (uint8_t)((x * 7u + y * 13u) & 0xffu); }
	}
	uint32_t luma = stride * ((h + 15u) & ~15u);
	memset(p + luma, 128, stride * ((h + 15u) & ~15u) / 2u);
}

/* ---- scenarios ----------------------------------------------------------------------------------------------- */

static void
scenario_hello(void)
{
	int fd = cl_connect();
	CHECK(fd >= 0, "connect for HELLO");
	if (fd < 0) { return; }
	struct rep r;
	CHECK(hello(fd, N2VCD_VERSION, 1, &r, 2000) == 0, "HELLO answered");
	CHECK(r.type == N2VCD_HELLO_OK, "HELLO reply type %u", r.type);
	CHECK(r.version == N2VCD_VERSION, "HELLO version %u", r.version);
	CHECK((r.enc_codecs & 1u) != 0, "HELLO advertises H.264 (0x%x)", r.enc_codecs);
	CHECK(r.out_buf_bytes >= 1920 && r.in_buf_bytes >= 1080, "HELLO geometry %ux%u", r.out_buf_bytes, r.in_buf_bytes);
	CHECK(hello(fd, 99, 2, &r, 2000) == 0, "bad-version HELLO answered");
	CHECK(r.type == N2VCD_ERROR && r.status == N2VCD_E_VERSION, "bad version -> ERROR/E_VERSION (type %u status %u)",
	      r.type, r.status);
	(void)close(fd);
}

static void
scenario_encode(void)
{
	set_int_file("milestone", 4);   /* N2VHOST_MS_D: the fake reports an advance on the next OPEN */
	int fd = cl_connect();
	CHECK(fd >= 0, "connect for encode");
	if (fd < 0) { return; }
	struct rep h;
	CHECK(hello(fd, N2VCD_VERSION, 10, &h, 2000) == 0, "HELLO before encode");
	struct rep o;
	CHECK(open_session(fd, N2VCD_CODEC_H264, 1, 64, 48, 11, &o, 2000) == 0, "OPEN answered");
	CHECK(o.type == N2VCD_OPEN_OK && o.status == N2VCD_E_OK, "OPEN ok (type %u status %u)", o.type, o.status);
	CHECK(o.session != 0, "OPEN assigned a session");
	CHECK(o.in_buf_bytes >= 64u * 48u * 3u / 2u, "in_buf_bytes %u", o.in_buf_bytes);
	CHECK(o.out_buf_bytes > 4096u, "out_buf_bytes %u", o.out_buf_bytes);
	if (o.type != N2VCD_OPEN_OK) { (void)close(fd); return; }

	uint8_t *in = (uint8_t *)malloc(o.in_buf_bytes);
	CHECK(in != NULL, "input buffer malloc");
	if (!in) { (void)close(fd); return; }
	fill_nv12(in, 64, 48);
	CHECK(buf_create(1, o.in_buf_bytes) == 0, "create DMA file 1");
	CHECK(buf_create(2, o.out_buf_bytes) == 0, "create DMA file 2");
	CHECK(buf_io(1, 0, in, o.in_buf_bytes, 1) == 0, "write the input picture");
	free(in);

	struct rep f;
	struct rep small;
	CHECK(enc_frame(fd, o.session, 1, 64, 2, o.out_buf_bytes - 1u, 0, 0, 22, &small, 2000) == 0,
	      "under-capacity frame answered");
	CHECK(small.type == N2VCD_ERROR && small.status == N2VCD_E_ARGS,
	      "out_capacity below OPEN_OK.out_buf_bytes -> E_ARGS (type %u status %u)", small.type, small.status);
	CHECK(enc_frame(fd, o.session, 1, 64, 1, o.out_buf_bytes, 0, 0, 25, &small, 2000) == 0,
	      "in==out handle frame answered");
	CHECK(small.type == N2VCD_ERROR && small.status == N2VCD_E_ARGS,
	      "in_handle == out_handle -> E_ARGS (type %u status %u)", small.type, small.status);

	CHECK(enc_frame(fd, o.session, 1, 64, 2, o.out_buf_bytes, 1000, 1, 12, &f, 3000) == 0, "frame 0 answered");
	CHECK(f.type == N2VCD_ENC_FRAME_OK && f.status == N2VCD_E_OK, "frame 0 ok (type %u status %u)", f.type, f.status);
	CHECK(f.out_bytes > 0, "frame 0 bytes %u", f.out_bytes);
	CHECK(f.key_frame == 1, "frame 0 is a key frame");
	CHECK(f.header_bytes > 0, "frame 0 reports parameter sets (%u)", f.header_bytes);
	CHECK(f.pts == 1000, "frame 0 pts echo %llu", (unsigned long long)f.pts);
	uint8_t head[8];
	CHECK(buf_io(2, 0, head, sizeof(head), 0) == 0, "read the frame header");
	CHECK(head[0] == 0 && head[1] == 0 && head[2] == 0 && head[3] == 1, "frame 0 starts with a 4-byte start code");
	CHECK((head[4] & 0x1f) == 7, "frame 0 starts with SPS (nal type %u)", head[4] & 0x1f);

	CHECK(enc_frame(fd, o.session, 1, 64, 2, o.out_buf_bytes, 2000, 0, 13, &f, 3000) == 0, "frame 1 answered");
	CHECK(f.type == N2VCD_ENC_FRAME_OK && f.status == N2VCD_E_OK, "frame 1 ok");
	CHECK(f.out_bytes > 0, "frame 1 bytes %u", f.out_bytes);

	CHECK(enc_frame(fd, o.session, 1, 64, 2, o.out_buf_bytes, 3000, 0, 14, &f, 3000) == 0, "frame 2 answered");
	CHECK(f.type == N2VCD_ENC_FRAME_OK, "frame 2 ok");

	struct rep s;
	CHECK(enc_set(fd, o.session, 0, 0, 0, 1, 15, &s, 2000) == 0, "ENC_SET force-key answered");
	CHECK(s.type == N2VCD_ENC_SET_OK && s.status == N2VCD_E_OK, "ENC_SET ok (type %u)", s.type);
	CHECK(enc_frame(fd, o.session, 1, 64, 2, o.out_buf_bytes, 4000, 0, 16, &f, 3000) == 0, "forced frame answered");
	CHECK(f.key_frame == 1, "ENC_SET force-key produced a key frame");
	CHECK(f.header_bytes > 0, "forced key frame starts with parameter sets (%u)", f.header_bytes);

	/* ENC_FRAME.force_key is the per-frame request; it must reach VENC_SET_PARAM_FORCE_INTRA too. */
	CHECK(enc_frame(fd, o.session, 1, 64, 2, o.out_buf_bytes, 5000, 0, 19, &f, 3000) == 0, "frame before per-frame force");
	CHECK(f.type == N2VCD_ENC_FRAME_OK, "frame before per-frame force ok (type %u status %u)", f.type, f.status);
	CHECK(enc_frame(fd, o.session, 1, 64, 2, o.out_buf_bytes, 6000, 1, 20, &f, 3000) == 0, "force_key frame answered");
	CHECK(f.type == N2VCD_ENC_FRAME_OK && f.key_frame == 1,
	      "ENC_FRAME.force_key produced a key frame (type %u key %u)", f.type, f.key_frame);
	CHECK(f.pts == 6000, "force_key frame pts echo %llu", (unsigned long long)f.pts);

	/* Cached parameter sets: a key AU without in-band SPS/PPS must get the cached ones prepended. */
	CHECK(enc_set(fd, o.session, 0, 0, 0, 1, 23, &s, 2000) == 0, "force-key for the cached-header test answered");
	set_int_file("strip_params", 1);
	CHECK(enc_frame(fd, o.session, 1, 64, 2, o.out_buf_bytes, 7000, 0, 24, &f, 3000) == 0, "stripped key frame answered");
	clear_file("strip_params");
	CHECK(f.type == N2VCD_ENC_FRAME_OK && f.key_frame == 1, "stripped key frame is still a key (type %u)", f.type);
	CHECK(f.header_bytes > 0, "cached SPS/PPS were prepended (%u bytes)", f.header_bytes);
	CHECK(buf_io(2, 0, head, sizeof(head), 0) == 0, "read the prepended frame header");
	CHECK(head[0] == 0 && head[1] == 0 && head[2] == 0 && head[3] == 1 && (head[4] & 0x1f) == 7,
	      "prepended output starts with SPS (nal %u)", head[4] & 0x1f);

	/* ENC_SET bitrate and framerate (no force-key this time). */
	CHECK(enc_set(fd, o.session, 2000000, 60, 1, 0, 21, &s, 2000) == 0, "ENC_SET bitrate/fps answered");
	CHECK(s.type == N2VCD_ENC_SET_OK && s.status == N2VCD_E_OK, "ENC_SET bitrate/fps ok (type %u status %u)", s.type,
	      s.status);
	CHECK(enc_set(fd, o.session, 500000000, 0, 0, 0, 69, &s, 2000) == 0, "oversize ENC_SET bitrate answered");
	CHECK(s.type == N2VCD_ERROR && s.status == N2VCD_E_ARGS, "ENC_SET bitrate cap -> E_ARGS (type %u status %u)",
	      s.type, s.status);
	CHECK(enc_set(fd, o.session, 0, 60, 0, 0, 70, &s, 2000) == 0, "ENC_SET fps_den=0 answered");
	CHECK(s.type == N2VCD_ERROR && s.status == N2VCD_E_ARGS, "ENC_SET fps_den=0 -> E_ARGS (type %u status %u)", s.type,
	      s.status);
	CHECK(enc_set(fd, o.session, 0, 1000000, 1, 0, 71, &s, 2000) == 0, "ENC_SET oversize fps answered");
	CHECK(s.type == N2VCD_ERROR && s.status == N2VCD_E_ARGS, "ENC_SET fps cap -> E_ARGS (type %u status %u)", s.type,
	      s.status);

	CHECK(simple_req(fd, N2VCD_FLUSH, o.session, 17, &s, 2000) == 0, "FLUSH answered");
	CHECK(s.type == N2VCD_FLUSH_OK, "FLUSH ok (type %u)", s.type);
	CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 18, &s, 2000) == 0, "CLOSE answered");
	CHECK(s.type == N2VCD_CLOSE_OK, "CLOSE ok (type %u)", s.type);

	/* the AP model recorded the first 16 input bytes and their sum */
	uint8_t seen[20];
	memset(seen, 0, sizeof(seen));
	FILE *pf = fopen(g_seen, "rb");
	CHECK(pf != NULL, "AP model wrote the input proof");
	if (pf) {
		size_t n = fread(seen, 1, sizeof(seen), pf);
		(void)fclose(pf);
		CHECK(n == sizeof(seen), "input proof is 20 bytes (%zu)", n);
		if (n == sizeof(seen)) {
			uint32_t sum = 0;
			for (unsigned i = 0; i < 16u; i++) { sum += seen[i]; }
			uint32_t stored = (uint32_t)seen[16] | (uint32_t)seen[17] << 8 | (uint32_t)seen[18] << 16 |
			                  (uint32_t)seen[19] << 24;
			CHECK(sum == stored, "input proof checksum 0x%x == 0x%x", sum, stored);
			for (unsigned i = 0; i < 16u; i++) {
				uint8_t want = (uint8_t)((i * 7u) & 0xffu);       /* y = 0 row of fill_nv12 */
				CHECK(seen[i] == want, "input byte %u = 0x%02x, want 0x%02x", i, seen[i], want);
			}
		}
	}
	(void)close(fd);
}

static void
scenario_separate_headers(void)
{
	/* The vendor's separate-header job (N2AP_ENC_SEQ_HDR) at OPEN supplies the parameter sets; every frame the
	 * model then emits has its in-band SPS/PPS stripped, so a key frame can only be correct if the daemon cached
	 * the header and prepended it itself.  This is the path the first phone encode uses. */
	(void)unlink(g_seqhdr);
	set_int_file("strip_params", 1);
	int fd = cl_connect();
	CHECK(fd >= 0, "connect for separate headers");
	if (fd < 0) { clear_file("strip_params"); return; }
	struct rep h, o;
	CHECK(hello(fd, N2VCD_VERSION, 140, &h, 2000) == 0, "HELLO for separate headers");
	CHECK(open_session(fd, N2VCD_CODEC_H264, 1, 64, 48, 141, &o, 2000) == 0, "OPEN with the header job");
	CHECK(o.type == N2VCD_OPEN_OK && o.session != 0, "OPEN with the header job ok (type %u)", o.type);
	if (o.type == N2VCD_OPEN_OK) {
		uint8_t *in = (uint8_t *)malloc(o.in_buf_bytes);
		CHECK(in != NULL, "input buffer malloc (headers)");
		if (in) {
			fill_nv12(in, 64, 48);
			CHECK(buf_create(1, o.in_buf_bytes) == 0, "create DMA file 1 (headers)");
			CHECK(buf_create(2, o.out_buf_bytes) == 0, "create DMA file 2 (headers)");
			CHECK(buf_io(1, 0, in, o.in_buf_bytes, 1) == 0, "write the input picture (headers)");
			free(in);
		}
		struct rep f;
		CHECK(enc_frame(fd, o.session, 1, 64, 2, o.out_buf_bytes, 8100, 1, 142, &f, 3000) == 0,
		      "stripped-key frame answered");
		CHECK(f.type == N2VCD_ENC_FRAME_OK && f.key_frame == 1, "stripped-key frame ok (type %u key %u)",
		      f.type, f.key_frame);
		CHECK(f.header_bytes > 0, "header from the OPEN header job was prepended (%u bytes)", f.header_bytes);
		uint8_t head[8];
		CHECK(buf_io(2, 0, head, sizeof(head), 0) == 0, "read the output prefix");
		CHECK(head[0] == 0 && head[1] == 0 && head[2] == 0 && head[3] == 1 && (head[4] & 0x1f) == 7,
		      "output starts with SPS from the cached header (nal %u)", head[4] & 0x1f);
		CHECK(file_contains(g_seqhdr, "fourcc="), "the AP model saw a sequence-header job at OPEN");
		CHECK(log_contains("sequence header"), "the daemon logged the separate sequence header");

		/* Asynchronous completion: the encode ACK carries 0 bytes and n2ap_venc_wait_done() supplies the
		 * size/key afterwards, exactly what the real GCE path does (V41). */
		set_int_file("async", 1);
		CHECK(enc_frame(fd, o.session, 1, 64, 2, o.out_buf_bytes, 8200, 0, 143, &f, 3000) == 0,
		      "async frame answered");
		clear_file("async");
		CHECK(f.type == N2VCD_ENC_FRAME_OK && f.status == N2VCD_E_OK && f.out_bytes > 0,
		      "wait_done completed the async frame (type %u status %u bytes %u)", f.type, f.status, f.out_bytes);

		struct rep s;
		CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 144, &s, 2000) == 0, "CLOSE after separate headers");
		CHECK(s.type == N2VCD_CLOSE_OK, "CLOSE ok after separate headers");
	}
	(void)close(fd);
	clear_file("strip_params");
}

static void
scenario_two_clients(void)
{
	int a = cl_connect();
	int b = cl_connect();
	CHECK(a >= 0 && b >= 0, "two clients connect");
	if (a < 0 || b < 0) { if (a >= 0) { (void)close(a); } if (b >= 0) { (void)close(b); } return; }
	struct rep h, oa, ob;
	CHECK(hello(a, N2VCD_VERSION, 20, &h, 2000) == 0, "A HELLO");
	CHECK(hello(b, N2VCD_VERSION, 21, &h, 2000) == 0, "B HELLO");
	CHECK(open_session(a, N2VCD_CODEC_H264, 1, 64, 48, 22, &oa, 2000) == 0, "A OPEN answered");
	CHECK(oa.type == N2VCD_OPEN_OK, "A got the session");
	CHECK(open_session(b, N2VCD_CODEC_H264, 1, 64, 48, 23, &ob, 2000) == 0, "B OPEN answered");
	CHECK(ob.type == N2VCD_ERROR && ob.status == N2VCD_E_BUSY, "B OPEN -> BUSY (type %u status %u)", ob.type, ob.status);

	struct rep f;
	CHECK(enc_frame(b, oa.session, 1, 64, 2, 2u << 20, 0, 0, 24, &f, 2000) == 0, "B frame on A's session answered");
	CHECK(f.type == N2VCD_ERROR && f.status == N2VCD_E_SESSION, "B frame -> E_SESSION (type %u status %u)", f.type,
	      f.status);
	struct rep s;
	CHECK(simple_req(a, N2VCD_CLOSE, oa.session, 25, &s, 2000) == 0, "A CLOSE answered");
	CHECK(s.type == N2VCD_CLOSE_OK, "A CLOSE ok");
	(void)close(b);
	(void)close(a);
}

static void
scenario_client_killed(void)
{
	int p[2];
	CHECK(pipe(p) == 0, "kill scenario pipe");
	int child = fork();
	if (child == 0) {
		(void)close(p[0]);
		int fd = cl_connect();
		if (fd < 0) { _exit(1); }
		struct rep h, o;
		if (hello(fd, N2VCD_VERSION, 30, &h, 2000) != 0) { _exit(1); }
		if (open_session(fd, N2VCD_CODEC_H264, 1, 64, 48, 31, &o, 2000) != 0 || o.type != N2VCD_OPEN_OK) { _exit(1); }
		(void)write(p[1], &o.session, sizeof(o.session));
		for (;;) { pause(); }
	}
	(void)close(p[1]);
	uint32_t session = 0;
	CHECK(read(p[0], &session, sizeof(session)) == (ssize_t)sizeof(session), "child opened a session");
	(void)close(p[0]);
	CHECK(session != 0, "child session %u", session);
	CHECK(kill(child, SIGKILL) == 0, "kill the child");
	int status = 0;
	(void)waitpid(child, &status, 0);
	sleep_ms(400);                     /* the daemon sees the EOF on its next loop */

	int fd = cl_connect();
	CHECK(fd >= 0, "reconnect after a killed client");
	if (fd < 0) { return; }
	struct rep h, o, s;
	CHECK(hello(fd, N2VCD_VERSION, 32, &h, 2000) == 0, "HELLO after kill");
	CHECK(open_session(fd, N2VCD_CODEC_H264, 1, 64, 48, 33, &o, 2000) == 0, "OPEN after kill answered");
	CHECK(o.type == N2VCD_OPEN_OK, "OPEN after kill succeeded (type %u status %u)", o.type, o.status);
	CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 34, &s, 2000) == 0, "CLOSE after kill");
	(void)close(fd);
}

static void
scenario_unsupported(void)
{
	int fd = cl_connect();
	CHECK(fd >= 0, "connect for unsupported");
	if (fd < 0) { return; }
	struct rep h, s;
	CHECK(hello(fd, N2VCD_VERSION, 40, &h, 2000) == 0, "HELLO for unsupported");
	struct n2vcd_dec_header m;
	memset(&m, 0, sizeof(m));
	mk_hdr(&m.h, N2VCD_DEC_HEADER, 41, 0);
	m.in_handle = 1;
	m.in_bytes = 16;
	CHECK(request(fd, &m, (uint32_t)sizeof(m), &s, 2000) == 0, "DEC_HEADER answered");
	CHECK(s.type == N2VCD_ERROR && s.status == N2VCD_E_UNSUPPORTED, "DEC_HEADER -> E_UNSUPPORTED (type %u status %u)",
	      s.type, s.status);
	struct n2vcd_dec_frame df;
	memset(&df, 0, sizeof(df));
	mk_hdr(&df.h, N2VCD_DEC_FRAME, 43, 0);
	df.in_handle = 1;
	df.in_bytes = 16;
	df.out_handle = 2;
	df.out_capacity = 4096;
	CHECK(request(fd, &df, (uint32_t)sizeof(df), &s, 2000) == 0, "DEC_FRAME answered");
	CHECK(s.type == N2VCD_ERROR && s.status == N2VCD_E_UNSUPPORTED, "DEC_FRAME -> E_UNSUPPORTED (type %u status %u)",
	      s.type, s.status);
	struct rep o;
	CHECK(open_session(fd, N2VCD_CODEC_H264, 0, 64, 48, 42, &o, 2000) == 0, "decoder OPEN answered");
	CHECK(o.type == N2VCD_ERROR && o.status == N2VCD_E_UNSUPPORTED, "decoder OPEN -> E_UNSUPPORTED (type %u status %u)",
	      o.type, o.status);
	(void)close(fd);
}

static void
scenario_malformed(void)
{
	int fd = cl_connect();
	CHECK(fd >= 0, "connect for malformed");
	if (fd < 0) { return; }
	uint8_t tiny[4] = {0x4e, 0x56, 0x43, 0x44};
	CHECK(cl_send(fd, tiny, sizeof(tiny)) == 0, "send 4 bytes");
	_Alignas(16) uint8_t buf[256];
	CHECK(cl_recv(fd, buf, sizeof(buf), 300) == 0, "a 4-byte datagram gets no reply");

	struct n2vcd_hello m;
	memset(&m, 0, sizeof(m));
	mk_hdr(&m.h, N2VCD_HELLO, 50, 0);
	m.version = N2VCD_VERSION;
	CHECK(cl_send(fd, &m, sizeof(struct n2vcd_hdr)) == 0, "send a truncated HELLO");
	struct rep s;
	int n = cl_recv(fd, buf, sizeof(buf), 1000);
	CHECK(n > 0, "truncated HELLO answered (%d)", n);
	if (n > 0) {
		decode(buf, n, &s);
		CHECK(s.type == N2VCD_ERROR && s.status == N2VCD_E_ARGS, "truncated HELLO -> E_ARGS (type %u status %u)", s.type,
		      s.status);
	}

	struct n2vcd_hdr bad;
	mk_hdr(&bad, 999, 51, 0);
	CHECK(cl_send(fd, &bad, sizeof(bad)) == 0, "send unknown type");
	n = cl_recv(fd, buf, sizeof(buf), 1000);
	CHECK(n > 0, "unknown type answered");
	if (n > 0) {
		decode(buf, n, &s);
		CHECK(s.type == N2VCD_ERROR && s.status == N2VCD_E_ARGS, "unknown type -> E_ARGS");
	}

	struct n2vcd_hdr wrongmagic;
	mk_hdr(&wrongmagic, N2VCD_HELLO, 52, 0);
	wrongmagic.magic = 0x12345678u;
	CHECK(cl_send(fd, &wrongmagic, sizeof(wrongmagic)) == 0, "send wrong magic");
	n = cl_recv(fd, buf, sizeof(buf), 1000);
	CHECK(n > 0, "wrong magic answered");
	if (n > 0) {
		decode(buf, n, &s);
		CHECK(s.type == N2VCD_ERROR, "wrong magic -> ERROR");
	}

	struct n2vcd_open o;
	memset(&o, 0, sizeof(o));
	mk_hdr(&o.h, N2VCD_OPEN, 53, 0);
	o.codec = N2VCD_CODEC_H264;
	o.encoder = 1;
	o.width = 63;                       /* odd: refused before any hardware access */
	o.height = 48;
	o.fps_den = 1;
	CHECK(request(fd, &o, (uint32_t)sizeof(o), &s, 1000) == 0, "odd-width OPEN answered");
	CHECK(s.type == N2VCD_ERROR && s.status == N2VCD_E_ARGS, "odd width -> E_ARGS (type %u status %u)", s.type, s.status);

	struct n2vcd_open huge;
	memset(&huge, 0, sizeof(huge));
	mk_hdr(&huge.h, N2VCD_OPEN, 55, 0);
	huge.codec = N2VCD_CODEC_H264;
	huge.encoder = 1;
	huge.width = 64;
	huge.height = 48;
	huge.fps_num = 0xffffffffu;
	huge.fps_den = 1;
	CHECK(request(fd, &huge, (uint32_t)sizeof(huge), &s, 1000) == 0, "huge-fps OPEN answered");
	CHECK(s.type == N2VCD_ERROR && s.status == N2VCD_E_ARGS, "huge fps -> E_ARGS (type %u status %u)", s.type, s.status);

	struct n2vcd_open lowlat;
	memset(&lowlat, 0, sizeof(lowlat));
	mk_hdr(&lowlat.h, N2VCD_OPEN, 56, 0);
	lowlat.codec = N2VCD_CODEC_H264;
	lowlat.encoder = 1;
	lowlat.width = 64;
	lowlat.height = 48;
	lowlat.fps_num = 30;
	lowlat.fps_den = 1;
	lowlat.flags = 1;
	CHECK(request(fd, &lowlat, (uint32_t)sizeof(lowlat), &s, 1000) == 0, "flags=low-latency OPEN answered");
	CHECK(s.type == N2VCD_ERROR && s.status == N2VCD_E_UNSUPPORTED,
	      "unsupported OPEN flags -> E_UNSUPPORTED (type %u status %u)", s.type, s.status);

	/* A datagram above RWVC_MSG_MAX is consumed and ignored; the client stays connected.  Repeat to
	 * exercise the log-rate limiter (one line per 256) without a 300-line log. */
	uint8_t big[1500];
	memset(big, 0x5a, sizeof(big));
	int sent_all = 1;
	for (unsigned i = 0; i < 300u; i++) {
		if (cl_send(fd, big, sizeof(big)) != 0) { sent_all = 0; break; }
	}
	CHECK(sent_all, "sent 300 oversized datagrams");
	CHECK(cl_recv(fd, buf, sizeof(buf), 300) == 0, "oversized datagrams get no reply");
	CHECK(hello(fd, N2VCD_VERSION, 54, &s, 2000) == 0 && s.type == N2VCD_HELLO_OK,
	      "client still connected after 300 oversized datagrams (type %u)", s.type);

	CHECK(hello(fd, N2VCD_VERSION, 0, &s, 1000) == 0, "seq 0 HELLO answered");
	CHECK(s.type == N2VCD_ERROR && s.status == N2VCD_E_ARGS, "client seq 0 is reserved -> E_ARGS (type %u status %u)",
	      s.type, s.status);
	(void)close(fd);
}

static void
set_int_file(const char *name, int value)
{
	char path[700], body[32];
	if (path_join(path, sizeof(path), g_fake, name) != 0) { return; }
	int n = snprintf(body, sizeof(body), "%d\n", value);
	(void)write_file(path, body, (size_t)n);
}

static void
clear_file(const char *name)
{
	char path[700];
	if (path_join(path, sizeof(path), g_fake, name) == 0) { (void)unlink(path); }
}

static void
set_stop(int code)
{
	set_int_file("stop", code);
}

static void
clear_stop(void)
{
	clear_file("stop");
}

static void
scenario_fatal_request(const char *vcodecd)
{
	clear_stop();
	CHECK(start_daemon(vcodecd) == 0, "start a daemon for the fatal request test");
	int fd = cl_connect();
	CHECK(fd >= 0, "connect for fatal request");
	if (fd < 0) { return; }
	struct rep h, o, f;
	CHECK(hello(fd, N2VCD_VERSION, 60, &h, 2000) == 0, "HELLO for fatal request");
	CHECK(open_session(fd, N2VCD_CODEC_H264, 1, 64, 48, 61, &o, 2000) == 0 && o.type == N2VCD_OPEN_OK,
	      "OPEN before the fatal request");
	int fd2 = cl_connect();
	CHECK(fd2 >= 0, "second client connects before the fatal stop");
	if (fd2 >= 0) {
		CHECK(hello(fd2, N2VCD_VERSION, 62, &h, 2000) == 0, "second client HELLO");
	}
	set_int_file("stop_on_encode", 1);  /* the AP model stops the guest inside ENC_FRAME */
	CHECK(enc_frame(fd, o.session, 1, 64, 2, 2u << 20, 0, 0, 64, &f, 3000) == 0, "frame during a fatal stop answered");
	CHECK(f.type == N2VCD_ERROR && f.status == N2VCD_E_GUEST, "guest stop -> E_GUEST (type %u status %u)", f.type,
	      f.status);
	CHECK(f.session == o.session, "E_GUEST names the session");
	if (fd2 >= 0) {
		_Alignas(16) uint8_t buf[128];
		int n = cl_recv(fd2, buf, sizeof(buf), 3000);
		CHECK(n > 0, "the other client is notified (%d)", n);
		if (n > 0) {
			struct rep r;
			decode(buf, n, &r);
			CHECK(r.type == N2VCD_ERROR && r.seq == 0 && r.status == N2VCD_E_GUEST,
			      "other client gets seq 0 E_GUEST (type %u seq %u status %u)", r.type, r.seq, r.status);
		}
		(void)close(fd2);
	}
	clear_file("stop_on_encode");
	clear_stop();
	int status = wait_daemon_exit(5000);
	CHECK(status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 70,
	      "guest stop exits 70 (status 0x%x)", status);
	CHECK(log_contains("fatal guest stop"), "the fatal guest-stop line is logged");
	CHECK(log_contains("addr=0x00001000"), "the fatal line carries the stop address");
	CHECK(log_contains("no image describes the address"), "the fatal line names the describe result");
	CHECK(file_contains(g_rec, "POWER VENC off"), "the fatal path powered VENC off");
	(void)close(fd);
}

/* opus-enc4k: --restart-on-switch.  Same codec reopens in the process (vc18: H.264 1080p -> 4K worked); another codec is
 * answered BUSY/N2VCD_DETAIL_RESTART, VENC is powered off and the process exits 75 for launchd to restart it; a fresh
 * process then opens that codec. */
static void
scenario_restart_on_switch(const char *vcodecd)
{
	CHECK(start_daemon_x(vcodecd, "--restart-on-switch") == 0, "start a daemon with --restart-on-switch");
	int fd = cl_connect();
	CHECK(fd >= 0, "connect for restart-on-switch");
	if (fd < 0) { return; }
	struct rep h, o, s;
	CHECK(hello(fd, N2VCD_VERSION, 500, &h, 2000) == 0, "HELLO for restart-on-switch");
	CHECK(open_session_pl(fd, N2VCD_CODEC_H264, 64, 48, 0, 0, 501, &o, 2000) == 0 && o.type == N2VCD_OPEN_OK,
	      "first H.264 OPEN");
	CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 502, &s, 2000) == 0, "first H.264 CLOSE");
	CHECK(open_session_pl(fd, N2VCD_CODEC_H264, 96, 64, 0, 0, 503, &o, 2000) == 0 && o.type == N2VCD_OPEN_OK,
	      "second H.264 OPEN (other size) in the same process");
	CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 504, &s, 2000) == 0, "second H.264 CLOSE");
	CHECK(open_session_pl(fd, N2VCD_CODEC_HEVC, 64, 48, 0, 0, 505, &o, 2000) == 0, "HEVC OPEN after H.264 answered");
	CHECK(o.type == N2VCD_ERROR && o.status == N2VCD_E_BUSY && o.detail == N2VCD_DETAIL_RESTART,
	      "codec switch -> BUSY/RESTART (type %u status %u detail 0x%x)", o.type, o.status, o.detail);
	int status = wait_daemon_exit(5000);
	CHECK(status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 75, "codec switch exits 75 (status 0x%x)", status);
	CHECK(log_contains("codec switch h264 -> hevc"), "the switch line is logged");
	(void)close(fd);
	/* launchd's part: a new process serves the HEVC OPEN */
	CHECK(start_daemon_x(vcodecd, "--restart-on-switch") == 0, "restarted daemon");
	fd = cl_connect();
	CHECK(fd >= 0, "reconnect after the restart");
	if (fd < 0) { return; }
	CHECK(hello(fd, N2VCD_VERSION, 510, &h, 2000) == 0, "HELLO after the restart");
	CHECK(open_session_pl(fd, N2VCD_CODEC_HEVC, 64, 48, 0, 0, 511, &o, 2000) == 0 && o.type == N2VCD_OPEN_OK,
	      "HEVC OPEN in the fresh process");
	CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 512, &s, 2000) == 0, "HEVC CLOSE");
	/* vc24: HEVC after HEVC in one process hung on the phone -> also a restart */
	CHECK(open_session_pl(fd, N2VCD_CODEC_HEVC, 96, 64, 0, 0, 513, &o, 2000) == 0, "second HEVC OPEN answered");
	CHECK(o.type == N2VCD_ERROR && o.status == N2VCD_E_BUSY && o.detail == N2VCD_DETAIL_RESTART,
	      "HEVC reopen -> BUSY/RESTART (type %u status %u detail 0x%x)", o.type, o.status, o.detail);
	status = wait_daemon_exit(5000);
	CHECK(status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 75, "HEVC reopen exits 75 (status 0x%x)", status);
	CHECK(log_contains("reopen after hevc -> hevc"), "the reopen line is logged");
	(void)close(fd);
}

static void
scenario_fatal_watchdog(const char *vcodecd)
{
	clear_stop();
	CHECK(start_daemon(vcodecd) == 0, "start a daemon for the fatal watchdog test");
	int fd = cl_connect();
	CHECK(fd >= 0, "connect for fatal watchdog");
	if (fd < 0) { return; }
	struct rep h, o, s;
	CHECK(hello(fd, N2VCD_VERSION, 70, &h, 2000) == 0, "HELLO for fatal watchdog");
	CHECK(open_session(fd, N2VCD_CODEC_H264, 1, 64, 48, 71, &o, 2000) == 0 && o.type == N2VCD_OPEN_OK,
	      "OPEN for fatal watchdog");
	set_stop(3);                        /* N2A32_STOP_FAULT */
	_Alignas(16) uint8_t buf[128];
	int n = cl_recv(fd, buf, sizeof(buf), 3000);
	CHECK(n > 0, "watchdog sent an unsolicited error (%d)", n);
	if (n > 0) {
		decode(buf, n, &s);
		CHECK(s.type == N2VCD_ERROR && s.seq == 0 && s.status == N2VCD_E_GUEST && s.detail == 3,
		      "watchdog ERROR seq 0 E_GUEST detail 3 (type %u seq %u status %u detail %u)", s.type, s.seq, s.status,
		      s.detail);
	}
	clear_stop();
	int status = wait_daemon_exit(5000);
	CHECK(status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 70,
	      "watchdog guest stop exits 70 (status 0x%x)", status);
	CHECK(log_contains("fatal guest stop"), "the fatal watchdog line is logged");
	(void)close(fd);
}

static void
scenario_idle(void)
{
	int fd = cl_connect();
	CHECK(fd >= 0, "connect for idle timeout");
	if (fd < 0) { return; }
	struct rep h, o, s;
	CHECK(hello(fd, N2VCD_VERSION, 80, &h, 2000) == 0, "HELLO for idle timeout");
	CHECK(open_session(fd, N2VCD_CODEC_H264, 1, 64, 48, 81, &o, 2000) == 0 && o.type == N2VCD_OPEN_OK,
	      "OPEN for idle timeout");
	sleep_ms(1500);                     /* idle limit in the test daemon is 1000 ms */
	_Alignas(16) uint8_t buf[128];
	int n = cl_recv(fd, buf, sizeof(buf), 3000);
	CHECK(n > 0, "idle timeout notified (%d)", n);
	if (n > 0) {
		decode(buf, n, &s);
		CHECK(s.type == N2VCD_ERROR && s.seq == 0 && s.status == N2VCD_E_SESSION,
		      "idle timeout is seq 0 E_SESSION (type %u seq %u status %u)", s.type, s.seq, s.status);
	}
	CHECK(simple_req(fd, N2VCD_FLUSH, o.session, 82, &s, 2000) == 0, "FLUSH after idle timeout answered");
	CHECK(s.type == N2VCD_ERROR && s.status == N2VCD_E_SESSION, "the idle session is gone");
	CHECK(open_session(fd, N2VCD_CODEC_H264, 1, 64, 48, 83, &o, 2000) == 0 && o.type == N2VCD_OPEN_OK,
	      "OPEN works after the idle timeout");
	CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 84, &s, 2000) == 0, "CLOSE after idle recovery");
	(void)close(fd);
}

static void
scenario_timeout(void)
{
	char path[700], body[32];
	if (path_join(path, sizeof(path), g_fake, "hang_ms") != 0) { return; }
	int n = snprintf(body, sizeof(body), "200\n");
	(void)write_file(path, body, (size_t)n);

	int fd = cl_connect();
	CHECK(fd >= 0, "connect for timeout");
	if (fd < 0) { return; }
	struct rep h, o, f, s;
	CHECK(hello(fd, N2VCD_VERSION, 80, &h, 2000) == 0, "HELLO for timeout");
	CHECK(open_session(fd, N2VCD_CODEC_H264, 1, 64, 48, 81, &o, 2000) == 0 && o.type == N2VCD_OPEN_OK, "OPEN for timeout");
	CHECK(enc_frame(fd, o.session, 1, 64, 2, 2u << 20, 0, 0, 82, &f, 3000) == 0, "hung frame answered");
	CHECK(f.type == N2VCD_ERROR && f.status == N2VCD_E_HW, "hung frame -> E_HW (type %u status %u)", f.type, f.status);
	CHECK(f.detail >= 50, "E_HW detail is the elapsed ms (%u)", f.detail);
	(void)unlink(path);
	CHECK(simple_req(fd, N2VCD_FLUSH, o.session, 83, &s, 2000) == 0, "request after the timeout answered");
	CHECK(s.type == N2VCD_ERROR && s.status == N2VCD_E_SESSION, "timeout tore the session down");
	CHECK(open_session(fd, N2VCD_CODEC_H264, 1, 64, 48, 84, &o, 2000) == 0 && o.type == N2VCD_OPEN_OK,
	      "OPEN works again after the timeout");
	CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 85, &s, 2000) == 0, "CLOSE after timeout recovery");
	(void)close(fd);
}

static void
scenario_hw_errors(void)
{
	clear_stop();
	int fd = cl_connect();
	CHECK(fd >= 0, "connect for hardware errors");
	if (fd < 0) { return; }
	struct rep h, o, s, f;
	CHECK(hello(fd, N2VCD_VERSION, 100, &h, 2000) == 0, "HELLO for hardware errors");

	/* -ETIMEDOUT from a run-time SET_PARAM aborts the session (V41 semantics). */
	CHECK(open_session(fd, N2VCD_CODEC_H264, 1, 64, 48, 101, &o, 2000) == 0 && o.type == N2VCD_OPEN_OK,
	      "OPEN for set-param timeout");
	set_int_file("set_fail", 1);
	CHECK(enc_set(fd, o.session, 2000000, 0, 0, 0, 102, &s, 2000) == 0, "timed-out ENC_SET answered");
	CHECK(s.type == N2VCD_ERROR && s.status == N2VCD_E_HW, "timed-out ENC_SET -> E_HW (type %u status %u)", s.type,
	      s.status);
	clear_file("set_fail");
	CHECK(simple_req(fd, N2VCD_FLUSH, o.session, 103, &s, 2000) == 0, "FLUSH after timed-out ENC_SET answered");
	CHECK(s.type == N2VCD_ERROR && s.status == N2VCD_E_SESSION, "timed-out ENC_SET tore the session down");

	/* -EPROTO is a wrong reply and must not tear the session down (V41 semantics). */
	CHECK(open_session(fd, N2VCD_CODEC_H264, 1, 64, 48, 104, &o, 2000) == 0 && o.type == N2VCD_OPEN_OK,
	      "OPEN for set-param EPROTO");
	set_int_file("set_fail", 2);
	CHECK(enc_set(fd, o.session, 0, 0, 0, 1, 105, &s, 2000) == 0, "EPROTO ENC_SET answered");
	CHECK(s.type == N2VCD_ERROR && s.status == N2VCD_E_HW, "EPROTO ENC_SET -> E_HW (type %u status %u)", s.type, s.status);
	clear_file("set_fail");
	CHECK(simple_req(fd, N2VCD_FLUSH, o.session, 106, &s, 2000) == 0, "FLUSH after EPROTO ENC_SET answered");
	CHECK(s.type == N2VCD_FLUSH_OK, "EPROTO ENC_SET left the session alive (type %u)", s.type);
	CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 107, &s, 2000) == 0, "CLOSE after EPROTO ENC_SET");

	/* -EPROTO from encode: error to the client, session still usable. */
	CHECK(open_session(fd, N2VCD_CODEC_H264, 1, 64, 48, 108, &o, 2000) == 0 && o.type == N2VCD_OPEN_OK,
	      "OPEN for encode EPROTO");
	set_int_file("enc_err", 71);
	CHECK(enc_frame(fd, o.session, 1, 64, 2, 2u << 20, 0, 0, 109, &f, 3000) == 0, "EPROTO encode answered");
	CHECK(f.type == N2VCD_ERROR && f.status == N2VCD_E_HW, "EPROTO encode -> E_HW (type %u status %u)", f.type, f.status);
	clear_file("enc_err");
	CHECK(simple_req(fd, N2VCD_FLUSH, o.session, 110, &s, 2000) == 0, "FLUSH after EPROTO encode");
	CHECK(s.type == N2VCD_FLUSH_OK, "EPROTO encode left the session alive");
	CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 111, &s, 2000) == 0, "CLOSE after EPROTO encode");

	/* -EIO from encode is a hardware failure: session torn down, daemon keeps serving. */
	CHECK(open_session(fd, N2VCD_CODEC_H264, 1, 64, 48, 112, &o, 2000) == 0 && o.type == N2VCD_OPEN_OK,
	      "OPEN for encode EIO");
	set_int_file("enc_err", 5);
	CHECK(enc_frame(fd, o.session, 1, 64, 2, 2u << 20, 0, 0, 113, &f, 3000) == 0, "EIO encode answered");
	CHECK(f.type == N2VCD_ERROR && f.status == N2VCD_E_HW, "EIO encode -> E_HW (type %u status %u)", f.type, f.status);
	clear_file("enc_err");
	CHECK(simple_req(fd, N2VCD_FLUSH, o.session, 114, &s, 2000) == 0, "FLUSH after EIO encode");
	CHECK(s.type == N2VCD_ERROR && s.status == N2VCD_E_SESSION, "EIO encode tore the session down");
	CHECK(open_session(fd, N2VCD_CODEC_H264, 1, 64, 48, 115, &o, 2000) == 0 && o.type == N2VCD_OPEN_OK,
	      "OPEN works again after the hardware error");
	CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 116, &s, 2000) == 0, "CLOSE after hardware-error recovery");
	(void)close(fd);
}

static void
scenario_profiles(void)
{
	(void)unlink(g_params);
	int fd = cl_connect();
	CHECK(fd >= 0, "connect for profile/level mapping");
	if (fd < 0) { return; }
	struct rep h, o, s;
	CHECK(hello(fd, N2VCD_VERSION, 120, &h, 2000) == 0, "HELLO for profiles");

	/* H.264 profile_idc/level_idc -> enum v4l2_mpeg_video_h264_* (v4l2-controls.h:538-587). */
	CHECK(open_session_pl(fd, N2VCD_CODEC_H264, 64, 48, 100, 41, 121, &o, 2000) == 0 &&
	      o.type == N2VCD_OPEN_OK, "OPEN H.264 high 4.1");
	CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 122, &s, 2000) == 0, "CLOSE H.264 high 4.1");
	CHECK(open_session_pl(fd, N2VCD_CODEC_H264, 64, 48, 77, 9, 123, &o, 2000) == 0 &&
	      o.type == N2VCD_OPEN_OK, "OPEN H.264 main 1b");
	CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 124, &s, 2000) == 0, "CLOSE H.264 main 1b");
	CHECK(open_session_pl(fd, N2VCD_CODEC_H264, 64, 48, 110, 9, 125, &o, 2000) == 0 &&
	      o.type == N2VCD_OPEN_OK, "OPEN H.264 high10 1b");
	CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 126, &s, 2000) == 0, "CLOSE H.264 high10 1b");

	/* HEVC profile_idc/level_idc -> enum v4l2_mpeg_video_hevc_* (v4l2-controls.h:754-774). */
	CHECK(open_session_pl(fd, N2VCD_CODEC_HEVC, 64, 48, 2, 153, 127, &o, 2000) == 0 &&
	      o.type == N2VCD_OPEN_OK, "OPEN HEVC main10 5.1");
	CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 128, &s, 2000) == 0, "CLOSE HEVC main10 5.1");
	CHECK(open_session_pl(fd, N2VCD_CODEC_HEVC, 64, 48, 1, 30, 129, &o, 2000) == 0 &&
	      o.type == N2VCD_OPEN_OK, "OPEN HEVC main 1.0");
	CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 130, &s, 2000) == 0, "CLOSE HEVC main 1.0");
	/* profile_idc 0 / level_idc 0 = "leave the encoder default": HEVC must get the kernel's LEVEL_4 (enum 5),
	 * never map_level(0) = enum 0 = level 1 (opus-hevc's first-encode item). */
	CHECK(open_session_pl(fd, N2VCD_CODEC_HEVC, 64, 48, 0, 0, 133, &o, 2000) == 0 &&
	      o.type == N2VCD_OPEN_OK, "OPEN HEVC with default profile/level");
	CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 134, &s, 2000) == 0, "CLOSE HEVC defaults");

	CHECK(open_session_pl(fd, N2VCD_CODEC_H264, 64, 48, 44, 41, 131, &o, 2000) == 0,
	      "unknown H.264 profile answered");
	CHECK(o.type == N2VCD_ERROR && o.status == N2VCD_E_ARGS, "unknown H.264 profile -> E_ARGS (type %u status %u)",
	      o.type, o.status);
	CHECK(open_session_pl(fd, N2VCD_CODEC_HEVC, 64, 48, 1, 31, 132, &o, 2000) == 0,
	      "unknown HEVC level answered");
	CHECK(o.type == N2VCD_ERROR && o.status == N2VCD_E_ARGS, "unknown HEVC level -> E_ARGS (type %u status %u)",
	      o.type, o.status);

	CHECK(file_contains(g_params, "profile=4 level=12"), "H.264 profile_idc 100/41 -> V4L2 4/12");
	CHECK(file_contains(g_params, "fps=30 bitrate=500000 gop=6"), "fps/bitrate/gop reach the AP model");
	CHECK(file_contains(g_params, "profile=2 level=1"), "H.264 profile_idc 77/9 -> V4L2 2/1");
	CHECK(file_contains(g_params, "profile=5 level=1"), "H.264 profile_idc 110/9 -> V4L2 5/1");
	CHECK(file_contains(g_params, "profile=2 level=8"), "HEVC profile_idc 2/153 -> V4L2 2/8");
	CHECK(file_contains(g_params, "profile=0 level=0"), "HEVC profile_idc 1/30 -> V4L2 0/0");
	CHECK(file_contains(g_params, "profile=0 level=5"), "HEVC default level 0 -> V4L2 HEVC LEVEL_4 (5)");
	(void)close(fd);
}

/* opus-enc4k: OPEN.level 0 (VideoToolbox/Camera) gets the smallest level whose MaxFS/MaxMBPS (HEVC MaxLumaPs/Sr) and
 * MaxBR hold; an explicit level is kept as sent.  Expected V4L2 enums: H.264 3.1=9 4.0=11 4.1=12 4.2=13 5.0=14 5.1=15
 * 5.2=16; HEVC (2*enum, tier main) 4.0=5 4.1=6 5.0=7 5.1=8 5.2=9.  Bitrate 0 = the daemon's w*h*4 default. */
static void
scenario_auto_level(void)
{
	static const struct {
		uint32_t codec, w, h, profile, level, fps_num, fps_den, bitrate;
		const char *want, *what;
	} c[] = {
		{ N2VCD_CODEC_H264, 64, 48, 0, 0, 30, 1, 500000, "level=9 width=64 height=48", "H.264 64x48@30 -> 3.1" },
		{ N2VCD_CODEC_H264, 1280, 720, 0, 0, 30, 1, 0, "level=9 width=1280 height=720", "H.264 720p30 -> 3.1" },
		{ N2VCD_CODEC_H264, 1280, 720, 0, 0, 60, 1, 0, "level=11 width=1280 height=720 fps=60", "H.264 720p60 -> 4.0 (MaxMBPS; 3.2 not in the ladder)" },
		{ N2VCD_CODEC_H264, 1920, 1080, 0, 0, 30, 1, 0, "level=11 width=1920 height=1080 fps=30", "H.264 1080p30 8.3M -> 4.0" },
		{ N2VCD_CODEC_H264, 1920, 1080, 0, 0, 30, 1, 25000000, "level=12 width=1920 height=1080 fps=30 bitrate=25000000",
		  "H.264 1080p30 25M -> 4.1 (MaxBR)" },
		{ N2VCD_CODEC_H264, 1920, 1080, 0, 0, 60, 1, 0, "level=13 width=1920 height=1080 fps=60", "H.264 1080p60 -> 4.2" },
		{ N2VCD_CODEC_H264, 1920, 1080, 0, 0, 30000, 1001, 0, "level=11 width=1920 height=1080 fps=29", "H.264 1080p29.97 -> 4.0" },
		{ N2VCD_CODEC_H264, 2560, 1440, 0, 0, 30, 1, 0, "level=14 width=2560 height=1440", "H.264 1440p30 -> 5.0" },
		{ N2VCD_CODEC_H264, 3840, 2160, 0, 0, 30, 1, 0, "level=15 width=3840 height=2160 fps=30", "H.264 4K30 -> 5.1" },
		{ N2VCD_CODEC_H264, 3840, 2160, 0, 0, 60, 1, 0, "level=15 width=3840 height=2160 fps=60", "H.264 4K60 -> 5.1 (vendor max)" },
		{ N2VCD_CODEC_H264, 3840, 2160, 0, 41, 30, 1, 0, "level=12 width=3840 height=2160", "H.264 explicit 4.1 kept at 4K" },
		{ N2VCD_CODEC_HEVC, 640, 480, 0, 0, 30, 1, 0, "level=5 width=640 height=480", "HEVC 640x480 -> 4.0 (floor)" },
		{ N2VCD_CODEC_HEVC, 1920, 1080, 0, 0, 30, 1, 0, "level=5 width=1920 height=1080 fps=30", "HEVC 1080p30 -> 4.0" },
		{ N2VCD_CODEC_HEVC, 1920, 1080, 0, 0, 60, 1, 0, "level=6 width=1920 height=1080 fps=60", "HEVC 1080p60 -> 4.1" },
		{ N2VCD_CODEC_HEVC, 3840, 2160, 0, 0, 30, 1, 20000000, "level=7 width=3840 height=2160 fps=30 bitrate=20000000",
		  "HEVC 4K30 20M -> 5.0" },
		{ N2VCD_CODEC_HEVC, 3840, 2160, 0, 0, 30, 1, 0, "level=8 width=3840 height=2160 fps=30", "HEVC 4K30 33M -> 5.1 (MaxBR)" },
		{ N2VCD_CODEC_HEVC, 3840, 2160, 0, 0, 60, 1, 0, "level=8 width=3840 height=2160 fps=60", "HEVC 4K60 -> 5.1" },
		{ N2VCD_CODEC_HEVC, 3840, 2160, 1, 150, 30, 1, 0, "level=7 width=3840 height=2160", "HEVC explicit 5.0 kept" },
	};
	int fd = cl_connect();
	CHECK(fd >= 0, "connect for automatic levels");
	if (fd < 0) { return; }
	struct rep h, o, s;
	CHECK(hello(fd, N2VCD_VERSION, 400, &h, 2000) == 0, "HELLO for automatic levels");
	for (unsigned i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
		(void)unlink(g_params);
		int r = open_session_rate(fd, c[i].codec, c[i].w, c[i].h, c[i].profile, c[i].level, c[i].fps_num, c[i].fps_den,
		                          c[i].bitrate, 401u + 2u * i, &o, 2000);
		CHECK(r == 0 && o.type == N2VCD_OPEN_OK, "OPEN %s", c[i].what);
		CHECK(file_contains(g_params, c[i].want), "%s (want '%s')", c[i].what, c[i].want);
		if (r == 0 && o.type == N2VCD_OPEN_OK) {
			CHECK(simple_req(fd, N2VCD_CLOSE, o.session, 402u + 2u * i, &s, 2000) == 0, "CLOSE %s", c[i].what);
		}
	}
	/* vendor QUERY_CAP: H.264/H.265 width 16..3840; 4096 is refused before the hardware */
	CHECK(open_session_rate(fd, N2VCD_CODEC_H264, 4096, 2160, 0, 0, 25, 1, 0, 480, &o, 2000) == 0 &&
	      o.type == N2VCD_ERROR && o.status == N2VCD_E_ARGS, "4096x2160 -> E_ARGS (type %u status %u)", o.type, o.status);
	CHECK(h.type == N2VCD_HELLO_OK, "HELLO answered for the caps check");
	(void)close(fd);
}

static void
scenario_p03_cli(const char *vcodecd)
{
	if (!g_cli_path[0]) {
		printf("note: P03 CLI not supplied; client-library end-to-end check skipped\n");
		return;
	}
	char sock[700], dma[720], log[700], in[700], out[700], cli_log[700];
	(void)snprintf(sock, sizeof(sock), "%s/cli.sock", g_dir);
	(void)snprintf(log, sizeof(log), "%s/cli-daemon.log", g_dir);
	(void)snprintf(in, sizeof(in), "%s/cli-in.yuv", g_dir);
	(void)snprintf(out, sizeof(out), "%s/cli-out.h264", g_dir);
	(void)snprintf(cli_log, sizeof(cli_log), "%s/cli-out.log", g_dir);
	/* P03's socket transport stores buffers in "<socket>.buf". */
	(void)snprintf(dma, sizeof(dma), "%s.buf", sock);
	(void)mkdir(dma, 0755);

	const uint32_t w = 64, h = 48;
	uint32_t frame = w * h * 3u / 2u;
	uint8_t *yuv = (uint8_t *)malloc(frame * 2u);
	CHECK(yuv != NULL, "CLI input buffer malloc");
	if (!yuv) { return; }
	fill_nv12(yuv, w, h);
	fill_nv12(yuv + frame, w, h);
	CHECK(write_file(in, yuv, frame * 2u) == 0, "write the CLI NV12 input");
	free(yuv);

	pid_t dpid = start_daemon_at(vcodecd, sock, dma, log);
	CHECK(dpid > 0, "start a daemon for the P03 CLI");
	if (dpid <= 0) { return; }

	pid_t cpid = fork();
	if (cpid == 0) {
		int fd = open(cli_log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) { (void)dup2(fd, 1); (void)dup2(fd, 2); (void)close(fd); }
		execl(g_cli_path, g_cli_path, "enc", in, "64x48", "30", "500000", out, "--socket", sock, (char *)NULL);
		_exit(127);
	}
	uint64_t deadline = now_ms() + 15000u;
	int status = 0;
	int done = 0;
	while (cpid > 0) {
		pid_t r = waitpid(cpid, &status, WNOHANG);
		if (r == cpid) { done = 1; break; }
		if (now_ms() > deadline) { (void)kill(cpid, SIGKILL); (void)waitpid(cpid, &status, 0); break; }
		sleep_ms(20);
	}
	CHECK(cpid > 0 && done && WIFEXITED(status) && WEXITSTATUS(status) == 0,
	      "P03 n2vcd_cli enc exits 0 (status 0x%x)", status);
	CHECK(file_contains(cli_log, "N2VCD CLI done frames=2"),
	      "P03 CLI reports two encoded frames");
	struct stat st;
	CHECK(stat(out, &st) == 0 && st.st_size > 16, "P03 CLI wrote an H.264 file (%lld bytes)",
	      (long long)(stat(out, &st) == 0 ? st.st_size : -1));
	uint8_t head[8];
	if (stat(out, &st) == 0 && st.st_size >= 8) {
		FILE *f = fopen(out, "rb");
		size_t n = f ? fread(head, 1, sizeof(head), f) : 0;
		if (f) { (void)fclose(f); }
		CHECK(n == sizeof(head) && head[0] == 0 && head[1] == 0 && head[2] == 0 && head[3] == 1,
		      "P03 CLI output starts with an Annex B start code");
		CHECK(n == sizeof(head) && (head[4] & 0x1f) == 7, "P03 CLI output starts with SPS (nal %u)", head[4] & 0x1f);
	}
	CHECK(stop_pid(dpid) == 0, "stop the CLI daemon");
	(void)unlink(sock);
}

static void
scenario_fuzz(void)
{
	int fd = cl_connect();
	CHECK(fd >= 0, "connect for fuzz");
	if (fd < 0) { return; }
	uint32_t seed = 0x12345678u;
	for (unsigned i = 0; i < 10000u; i++) {
		seed = seed * 1664525u + 1013904223u;
		uint32_t len = 1u + (seed >> 8) % 1024u;
		uint8_t buf[1024];
		for (uint32_t k = 0; k < len; k++) {
			seed = seed * 1664525u + 1013904223u;
			buf[k] = (uint8_t)(seed >> 16);
		}
		if ((i & 3u) == 0) {
			struct n2vcd_hdr h;
			mk_hdr(&h, (seed >> 12) % 120u, i, (seed >> 4) % 3u);
			uint32_t copy = len < sizeof(h) ? len : (uint32_t)sizeof(h);
			memcpy(buf, &h, copy);
		}
		if (cl_send(fd, buf, len) != 0) { CHECK(0, "fuzz send %u failed", i); break; }
		if ((i % 256u) == 255u) {
			uint8_t rbuf[1200];
			for (unsigned k = 0; k < 64u; k++) {
				if (cl_recv(fd, rbuf, sizeof(rbuf), 0) <= 0) { break; }
			}
		}
	}
	uint8_t rbuf[1200];
	for (unsigned k = 0; k < 4096u; k++) {
		if (cl_recv(fd, rbuf, sizeof(rbuf), 0) <= 0) { break; }
	}
	CHECK(kill(g_pid, 0) == 0, "daemon survived 10000 random datagrams");
	struct rep h;
	CHECK(hello(fd, N2VCD_VERSION, 90, &h, 3000) == 0, "HELLO after the fuzz");
	CHECK(h.type == N2VCD_HELLO_OK, "HELLO after the fuzz is HELLO_OK");
	(void)close(fd);
}

/* ---- daemon lifecycle ---------------------------------------------------------------------------------------- */

static void
scenario_gate(const char *vcodecd)
{
	char gate[700], log[700], sock[700];
	(void)snprintf(gate, sizeof(gate), "%s/no-gate", g_dir);
	(void)snprintf(log, sizeof(log), "%s/gate.log", g_dir);
	(void)snprintf(sock, sizeof(sock), "%s/gate.sock", g_dir);
	(void)unlink(gate);
	pid_t pid = fork();
	if (pid == 0) {
		int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) { (void)dup2(fd, 2); (void)close(fd); }
		int nfd = open("/dev/null", O_RDONLY);
		if (nfd >= 0) { (void)dup2(nfd, 0); (void)close(nfd); }
		execl(vcodecd, vcodecd, "--fake", "--gate", gate, "--socket", sock, "--vendor", g_dir,
		      "--no-console", (char *)NULL);
		_exit(127);
	}
	int status = 0;
	uint64_t deadline = now_ms() + 5000u;
	while (pid > 0) {
		pid_t r = waitpid(pid, &status, WNOHANG);
		if (r == pid) { break; }
		if (now_ms() > deadline) { (void)kill(pid, SIGKILL); (void)waitpid(pid, &status, 0); break; }
		sleep_ms(20);
	}
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, "the missing gate exits 0 (status 0x%x)", status);
	CHECK(file_contains(log, "absent: not starting"), "the missing gate is logged");
	CHECK(access(sock, F_OK) != 0, "the missing gate does not create a server socket");
}

static int
start_daemon(const char *vcodecd)
{
	return start_daemon_x(vcodecd, NULL);
}

/* the same daemon with one extra option (NULL = none) */
static int
start_daemon_x(const char *vcodecd, const char *extra)
{
	g_pid = fork();
	if (g_pid == 0) {
		int fd = open(g_log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) { (void)dup2(fd, 2); (void)close(fd); }
		int nfd = open("/dev/null", O_RDONLY);
		if (nfd >= 0) { (void)dup2(nfd, 0); (void)close(nfd); }
		execl(vcodecd, vcodecd, "--fake", "--socket", g_sock, "--vendor", g_dir, "--dma-dir", g_dma,
		      "--record", g_rec, "--no-console", "--hw-timeout-ms", "50", "--session-idle-ms", "1000", extra, (char *)NULL);
		_exit(127);
	}
	uint64_t deadline = now_ms() + 5000u;
	for (;;) {
		int fd = cl_connect();
		if (fd >= 0) { (void)close(fd); return 0; }
		if (now_ms() > deadline) { return -1; }
		sleep_ms(20);
	}
}

static int
stop_daemon(void)
{
	if (g_pid <= 0) { return -1; }
	(void)kill(g_pid, SIGTERM);
	uint64_t deadline = now_ms() + 3000u;
	int status = 0;
	for (;;) {
		pid_t r = waitpid(g_pid, &status, WNOHANG);
		if (r == g_pid) { g_pid = 0; return status; }
		if (now_ms() > deadline) { (void)kill(g_pid, SIGKILL); (void)waitpid(g_pid, &status, 0); g_pid = 0; return -1; }
		sleep_ms(20);
	}
}

/* Wait for the daemon to exit on its own (fatal guest stop) and return its wait status, or -1 on timeout. */
static int
wait_daemon_exit(int timeout_ms)
{
	if (g_pid <= 0) { return -1; }
	uint64_t deadline = now_ms() + (uint64_t)timeout_ms;
	int status = 0;
	for (;;) {
		pid_t r = waitpid(g_pid, &status, WNOHANG);
		if (r == g_pid) { g_pid = 0; return status; }
		if (now_ms() > deadline) { (void)kill(g_pid, SIGKILL); (void)waitpid(g_pid, &status, 0); g_pid = 0; return -1; }
		sleep_ms(20);
	}
}

/* Start a second daemon with explicit paths (the P03 CLI test), wait for its socket, return its pid or -1. */
static pid_t
start_daemon_at(const char *bin, const char *sock, const char *dma, const char *log)
{
	pid_t pid = fork();
	if (pid == 0) {
		int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) { (void)dup2(fd, 2); (void)close(fd); }
		int nfd = open("/dev/null", O_RDONLY);
		if (nfd >= 0) { (void)dup2(nfd, 0); (void)close(nfd); }
		execl(bin, bin, "--fake", "--socket", sock, "--vendor", g_dir, "--dma-dir", dma,
		      "--no-console", "--hw-timeout-ms", "2000", (char *)NULL);
		_exit(127);
	}
	if (pid < 0) { return -1; }
	uint64_t deadline = now_ms() + 5000u;
	for (;;) {
		int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
		if (fd >= 0) {
			struct sockaddr_un sa;
			memset(&sa, 0, sizeof(sa));
			sa.sun_family = AF_UNIX;
			(void)snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", sock);
			int ok = connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0;
			(void)close(fd);
			if (ok) { return pid; }
		}
		if (now_ms() > deadline) { (void)kill(pid, SIGKILL); (void)waitpid(pid, NULL, 0); return -1; }
		sleep_ms(20);
	}
}

static int
stop_pid(pid_t pid)
{
	if (pid <= 0) { return -1; }
	(void)kill(pid, SIGTERM);
	uint64_t deadline = now_ms() + 3000u;
	int status = 0;
	for (;;) {
		pid_t r = waitpid(pid, &status, WNOHANG);
		if (r == pid) { return status; }
		if (now_ms() > deadline) { (void)kill(pid, SIGKILL); (void)waitpid(pid, &status, 0); return -1; }
		sleep_ms(20);
	}
}

static int
file_contains(const char *path, const char *needle)
{
	FILE *f = fopen(path, "r");
	if (!f) { return 0; }
	char line[1024];
	int found = 0;
	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, needle)) { found = 1; break; }
	}
	(void)fclose(f);
	return found;
}

static int
log_contains(const char *needle)
{
	return file_contains(g_log, needle);
}

int
main(int argc, char **argv)
{
	if (argc != 3 && argc != 4) {
		fprintf(stderr, "usage: t_daemon VCODEC_BINARY WORKDIR [P03_CLI]\n");
		return 2;
	}
	const char *vcodecd = argv[1];
	if (argc == 4) { (void)snprintf(g_cli_path, sizeof(g_cli_path), "%s", argv[3]); }
	(void)snprintf(g_dir, sizeof(g_dir), "%s", argv[2]);
	(void)snprintf(g_sock, sizeof(g_sock), "%s/vcodecd.sock", g_dir);
	(void)snprintf(g_log, sizeof(g_log), "%s/vcodecd.log", g_dir);
	(void)snprintf(g_fake, sizeof(g_fake), "%s/fake", g_dir);
	(void)snprintf(g_dma, sizeof(g_dma), "%s/dma", g_dir);
	(void)snprintf(g_stream, sizeof(g_stream), "%s/stream.au", g_dir);
	(void)snprintf(g_seen, sizeof(g_seen), "%s/input_seen", g_dir);
	(void)snprintf(g_rec, sizeof(g_rec), "%s/hw-record.txt", g_dir);
	(void)snprintf(g_params, sizeof(g_params), "%s/params.txt", g_dir);
	(void)snprintf(g_seqhdr, sizeof(g_seqhdr), "%s/seqhdr.log", g_fake);

	(void)mkdir(g_dir, 0755);
	(void)mkdir(g_fake, 0755);
	(void)mkdir(g_dma, 0755);
	char dummy[700];
	if (path_join(dummy, sizeof(dummy), g_dir, "vendor-lib.so") == 0) {
		const char *x = "not a real vendor library: the fake vhost only checks the directory\n";
		(void)write_file(dummy, x, strlen(x));
	}
	(void)unlink(g_seen);
	(void)setenv("N2IP_FAKE_DIR", g_fake, 1);
	(void)setenv("N2IP_FAKE_STREAM", g_stream, 1);
	(void)setenv("N2IP_FAKE_INPUT_SEEN", g_seen, 1);
	(void)setenv("N2IP_FAKE_PARAMS", g_params, 1);

	if (access(g_stream, F_OK) != 0) {
		printf("FAIL no canned stream at %s (run_tests.sh makes it)\n", g_stream);
		return 1;
	}
	scenario_gate(vcodecd);
	if (start_daemon(vcodecd) != 0) {
		printf("FAIL the daemon did not start (log %s)\n", g_log);
		return 1;
	}

	scenario_hello();
	scenario_encode();
	scenario_separate_headers();
	scenario_two_clients();
	scenario_client_killed();
	scenario_unsupported();
	scenario_malformed();
	scenario_idle();
	scenario_timeout();
	scenario_hw_errors();
	scenario_profiles();
	scenario_auto_level();
	scenario_p03_cli(vcodecd);
	scenario_fuzz();

	int status = stop_daemon();
	CHECK(status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0, "daemon exit status 0x%x", status);
	CHECK(log_contains("N2VCD ") && log_contains("start dev="), "log has the start line");
	CHECK(log_contains("session ") && log_contains("open client="), "log has the session line");
	CHECK(log_contains("hardware timeout"), "log has the timeout line");
	CHECK(log_contains("exit"), "log has the exit line");
	CHECK(log_contains("milestone M-d"), "a milestone advance after bring-up is logged");
	CHECK(file_contains(g_rec, "POWER VENC on"), "the daemon powered VENC on for the session");
	CHECK(file_contains(g_rec, "POWER VENC off"), "the daemon powered VENC off before exit");
	CHECK(log_contains("has no PA in the AP registry"), "the client-DMA PA gap is logged once");

	/* A guest stop is fatal by the lead's rule; these must be the last scenarios because the daemon exits 70. */
	scenario_restart_on_switch(vcodecd);
	scenario_fatal_request(vcodecd);
	scenario_fatal_watchdog(vcodecd);

	printf("checks: %d, failures: %d\n", g_checks, g_fails);
	if (g_fails == 0) {
		printf("N2TEST IP PASS %d/%d\n", g_checks, g_checks);
		return 0;
	}
	printf("N2TEST IP FAIL %d of %d checks\n", g_fails, g_checks);
	return 1;
}
