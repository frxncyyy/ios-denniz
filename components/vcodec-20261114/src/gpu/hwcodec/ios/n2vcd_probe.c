/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_probe.c -- boot-time, hands-free test client of vcodecd on the phone (CRITICAL_PATH item 10).  Owner: opus-ios.
 *
 * Written only against the frozen ABIs (include/redwood_vcodec.h relay + DMA ioctls, include/n2vcd_proto.h), so it does not
 * depend on P03's client library or CLI flags.  Started by the launchd job com.nord2.vcodec-probe (guest-vcodec.sh):
 *
 *   gate (one-shot: removed BEFORE any request, so a panic during the encode cannot repeat at the next boot)
 *   -> start delay -> wait for vcodecd (RWVC_INFO.server_present) -> HELLO -> OPEN avc1/hvc1 WxH
 *   -> two shared DMA buffers -> N frames of a moving NV12 test pattern -> ENC_FRAME each (frame 0 forced key)
 *   -> Annex B appended to <out>/probe-<pid>.<h264|h265>, per-frame line and hex of the first access unit on the console
 *   -> CLOSE.
 *
 * Every line goes to stdout and /dev/console (kernel log), prefix "N2VCDP".  Last line "N2VCDP DONE ok=1 ..." or "... ok=0 ...".
 *
 *   n2vcd_probe [--gate PATH] [--keep-gate] [--delay S] [--wait-server S] [--codec avc1|hvc1] [--size WxH] [--frames N]
 *               [--fps N] [--bitrate BPS] [--level IDC] [--profile IDC] [--open-timeout S] [--frame-timeout S]
 *               [--console-every N] [--out DIR] [--hex BYTES] [--relay DEV] [--no-console]
 *
 *   opus-enc4k (11 Nov, 1080p/4K): --bitrate 0 (default) = vcodecd's choice (w*h*4 bps: 1080p 8.3 M, 4K 33 M);
 *   --level 0 (default) = picked from size*fps (H.264 level_idc 31/40/42/51/52, HEVC 93/120/123/150/153), so a 4K session is
 *   not opened at the V4L2 default level 1.0 (vc14's stream says level_idc 10); -1 = send 0 (old behaviour).  --hex is capped
 *   at 1024 bytes (only frame 0); per-frame lines go to stdout (probe.log, complete) and to the console only for frames 0..2,
 *   every --console-every'th (default 10) and failures.  The pattern is n2vcd_pattern.h (host twin: n2vcd_pattern_gen.c).
 *   --venc-fast 0|1 writes sysctl kern.n2vc.venc_fast (0312: VENC 273 / 624 MHz, applied by the kernel at the next VENC
 *   power-on with VEN off) before OPEN and logs the value read back; default: not touched.
 *   --then "CODEC:WxH[:FRAMES[:FPS[:PROFILE[:LEVEL[:fast|slow]]]]],..." runs further sessions in the same boot: after this session's DONE line the probe
 *   re-executes itself (same pid, no gate, no delay, --wait-server 30) for the next entry with the common options
 *   (--bitrate/--profile/--out/--hex/timeouts/transport, --level -1), whatever the previous result; otherwise level auto per entry.  Output files are
 *   <out>/probe-<pid>-<index>-<codec>-<WxH>.<h264|h265> (index 0 = the first session), so vcodec_check.py stats knows each size.
 *   OPEN answered BUSY/N2VCD_DETAIL_RESTART (vcodecd --restart-on-switch) or E_GUEST, or the server lost under OPEN: the probe
 *   re-executes itself for the same session (no gate/delay, --wait-server 30), at most twice (--retried N).
 *               [--socket PATH --dma-dir DIR]      host test only: vcodecd's test transport (handle N = file DIR/N)
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#include "n2vcd_proto.h"
#include "redwood_vcodec.h"
#include "n2memlimit.h"     /* opus-decint (vc22): raise our own jetsam limit; the launchd key does not apply */
#include "n2vcd_pattern.h"

static int g_console = -1;
static int g_console_mute;   /* 1: this say() goes to stdout only */
static uint64_t g_t0;

static uint64_t now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
	char buf[320];
	uint64_t t = now_us() - g_t0;
	int n = snprintf(buf, sizeof buf, "N2VCDP %llu.%03llu ", (unsigned long long)(t / 1000000u),
	                 (unsigned long long)(t / 1000u % 1000u));
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf + n, sizeof buf - (size_t)n - 1, fmt, ap);
	va_end(ap);
	size_t len = strnlen(buf, sizeof buf - 2);
	for (size_t i = 0; i < len; i++) if ((unsigned char)buf[i] < 0x20) buf[i] = ' ';
	buf[len++] = '\n';
	fwrite(buf, 1, len, stdout);
	fflush(stdout);
	if (g_console >= 0 && !g_console_mute) (void)!write(g_console, buf, len);
}

static void sleep_ms(unsigned ms)
{
	struct timespec ts = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L };
	while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
}

/* ------------------------------------------------------------------------------------------- transports */

struct tp {
	int fd;
	int socket_mode;
	const char *dma_dir;
	uint32_t next_handle;
};

static int tp_send(struct tp *t, const void *msg, uint32_t bytes)
{
	if (t->socket_mode) {
		ssize_t w = send(t->fd, msg, bytes, 0);
		return w == (ssize_t)bytes ? 0 : -errno;
	}
	struct rwvc_msg m;
	memset(&m, 0, sizeof m);
	m.user = (uint64_t)(uintptr_t)msg;
	m.bytes = bytes;
	return ioctl(t->fd, RWVC_MSG_SEND, &m) == 0 ? 0 : -errno;
}

static int tp_recv(struct tp *t, void *buf, uint32_t cap, uint32_t timeout_ms)
{
	if (t->socket_mode) {
		struct pollfd p = { t->fd, POLLIN, 0 };
		int r = poll(&p, 1, (int)timeout_ms);
		if (r == 0) return -ETIMEDOUT;
		if (r < 0) return -errno;
		ssize_t n = recv(t->fd, buf, cap, 0);
		if (n < 0) return -errno;
		if (n == 0) return -ENXIO;
		return (int)n;
	}
	struct rwvc_msg m;
	memset(&m, 0, sizeof m);
	m.user = (uint64_t)(uintptr_t)buf;
	m.bytes = cap;
	m.timeout_ms = timeout_ms;
	if (ioctl(t->fd, RWVC_MSG_RECV, &m) != 0) return -errno;
	return (int)m.bytes;
}

static int tp_alloc(struct tp *t, uint32_t bytes, uint32_t *handle, uint32_t *pa)
{
	if (t->socket_mode) {
		char path[512];
		uint32_t h = t->next_handle++;
		snprintf(path, sizeof path, "%s/%u", t->dma_dir, h);
		int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
		if (fd < 0) return -errno;
		int r = ftruncate(fd, bytes);
		close(fd);
		if (r != 0) return -errno;
		*handle = h;
		*pa = 0;
		return 0;
	}
	struct rwvc_dma_alloc a;
	memset(&a, 0, sizeof a);
	a.bytes = bytes;
	a.flags = RWVC_DMA_SHARED;
	if (ioctl(t->fd, RWVC_DMA_ALLOC, &a) != 0) return -errno;
	*handle = a.out_handle;
	*pa = a.out_pa;
	return 0;
}

static int tp_rw(struct tp *t, uint32_t handle, uint32_t off, void *data, uint32_t bytes, int write)
{
	if (t->socket_mode) {
		char path[512];
		snprintf(path, sizeof path, "%s/%u", t->dma_dir, handle);
		int fd = open(path, write ? O_WRONLY : O_RDONLY);
		if (fd < 0) return -errno;
		ssize_t n = write ? pwrite(fd, data, bytes, off) : pread(fd, data, bytes, off);
		close(fd);
		return n == (ssize_t)bytes ? 0 : -EIO;
	}
	struct rwvc_dma_rw r;
	memset(&r, 0, sizeof r);
	r.handle = handle;
	r.offset = off;
	r.user = (uint64_t)(uintptr_t)data;
	r.bytes = bytes;
	r.write = write ? 1u : 0u;
	return ioctl(t->fd, RWVC_DMA_RW, &r) == 0 ? 0 : -errno;
}

static void tp_free(struct tp *t, uint32_t handle)
{
	if (t->socket_mode || handle == 0) return;
	struct rwvc_dma_free f = { handle, 0 };
	(void)ioctl(t->fd, RWVC_DMA_FREE, &f);
}

/* ------------------------------------------------------------------------------------------- protocol */

static uint32_t g_seq = 1, g_session;

static void hdr(struct n2vcd_hdr *h, uint32_t type)
{
	h->magic = N2VCD_MAGIC;
	h->type = type;
	h->seq = g_seq++;
	h->session = g_session;
}

/* request/reply; returns reply bytes, or -errno; an N2VCD_ERROR reply is returned as is (caller checks type) */
static int rpc(struct tp *t, const void *req, uint32_t bytes, void *reply, uint32_t cap, uint32_t timeout_ms)
{
	const struct n2vcd_hdr *rq = (const struct n2vcd_hdr *)req;
	int r = tp_send(t, req, bytes);
	if (r < 0) return r;
	uint64_t deadline = now_us() + (uint64_t)timeout_ms * 1000u;
	for (;;) {
		uint64_t now = now_us();
		if (now >= deadline) return -ETIMEDOUT;
		uint32_t left = (uint32_t)((deadline - now) / 1000u) + 1u;
		int n = tp_recv(t, reply, cap, left > 5000u ? 5000u : left);
		if (n == -ETIMEDOUT || n == -EAGAIN) continue;
		if (n < 0) return n;
		if ((uint32_t)n < sizeof(struct n2vcd_hdr)) { say("short reply %d bytes ignored", n); continue; }
		struct n2vcd_hdr h;
		memcpy(&h, reply, sizeof h);
		if (h.magic != N2VCD_MAGIC) { say("bad magic %08x ignored", h.magic); continue; }
		if (h.seq == 0 && h.type == N2VCD_ERROR) {
			struct n2vcd_simple e;
			memset(&e, 0, sizeof e);
			memcpy(&e, reply, (uint32_t)n < sizeof e ? (uint32_t)n : sizeof e);
			say("unsolicited ERROR status=%u detail=%u: session is dead", e.status, e.detail);
			return -EPIPE;
		}
		if (h.seq != rq->seq) { say("reply seq %u for request %u ignored", h.seq, rq->seq); continue; }
		return n;
	}
}

static int is_error(const void *reply, int n, const char *what)
{
	struct n2vcd_hdr h;
	memcpy(&h, reply, sizeof h);
	if (h.type != N2VCD_ERROR) return 0;
	struct n2vcd_simple e;
	memset(&e, 0, sizeof e);
	memcpy(&e, reply, (uint32_t)n < sizeof e ? (uint32_t)n : sizeof e);
	static const char *const names[] = { "OK", "VERSION", "UNSUPPORTED", "BUSY", "ARGS", "HW", "GUEST", "NOMEM", "SESSION" };
	say("%s -> ERROR status=%u (%s) detail=%u", what, e.status, e.status < 9 ? names[e.status] : "?", e.detail);
	return 1;
}

/* ------------------------------------------------------------------------------------------- pattern */

static void make_nv12(uint8_t *p, uint32_t w, uint32_t h, uint32_t stride, uint32_t frame)
{
	n2vcd_pattern_nv12(p, w, h, stride, frame);   /* same pixels as vc14's per-pixel loop (n2vcd_pattern_gen --selftest) */
}

/* level_idc for w x h at fps when the caller gave none (H.264 Table A-1 MaxFS/MaxMBPS; HEVC Table A.8 MaxLumaPs/SR) */
static uint32_t auto_level(int hevc, uint32_t w, uint32_t h, uint32_t fps)
{
	uint64_t mbs = (uint64_t)((w + 15u) / 16u) * ((h + 15u) / 16u), mbps = mbs * fps;
	uint64_t ps = (uint64_t)w * h, sr = ps * fps;
	if (hevc) {
		if (ps <= 983040u && sr <= 33177600u) return 93;      /* 3.1 */
		if (ps <= 2228224u && sr <= 66846720u) return 120;    /* 4   */
		if (ps <= 2228224u && sr <= 133693440u) return 123;   /* 4.1 */
		if (ps <= 8912896u && sr <= 267386880u) return 150;   /* 5   */
		return 153;                                           /* 5.1 = the vendor's max (QUERY_CAP) */
	}
	if (mbs <= 3600u && mbps <= 108000u) return 31;
	if (mbs <= 8192u && mbps <= 245760u) return 40;
	if (mbs <= 8704u && mbps <= 522240u) return 42;
	return 51;                                            /* 5.1 = the vendor's max (QUERY_CAP level enum 15) */
}

static void nal_summary(const uint8_t *b, uint32_t n, int hevc, char *out, size_t cap)
{
	size_t o = 0;
	out[0] = 0;
	for (uint32_t i = 0; i + 3 < n && o + 8 < cap; i++) {
		if (b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1) {
			unsigned t = hevc ? (b[i + 3] >> 1) & 63u : b[i + 3] & 31u;
			o += (size_t)snprintf(out + o, cap - o, "%s%u", o ? "," : "", t);
			i += 2;
		}
	}
}

/* ---------------------------------------------------------------------------------------------- main */

int main(int argc, char **argv)
{
	const char *gate = NULL, *outdir = "/private/var/db/nord2-vcodec/out", *relay = "/dev/redwood-vcodec-client";
	const char *sock = NULL, *dma_dir = NULL, *codec_s = "avc1", *then = NULL;
	unsigned index = 0, retried = 0;
	int venc_fast = -1;
	int want_retry = 0;
	int bitrate_given = 0;
	int keep_gate = 0, no_console = 0;
	unsigned delay_s = 0, wait_s = 90, frames = 10, fps = 30, hex_bytes = 96, open_s = 60, frame_s = 10, con_every = 10;
	uint32_t w = 640, h = 480, bitrate = 0, profile = 0;
	long level_arg = 0;
	g_t0 = now_us();
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *v = i + 1 < argc ? argv[i + 1] : NULL;
		if (!strcmp(a, "--gate") && v) { gate = v; i++; }
		else if (!strcmp(a, "--keep-gate")) keep_gate = 1;
		else if (!strcmp(a, "--delay") && v) { delay_s = (unsigned)strtoul(v, NULL, 0); i++; }
		else if (!strcmp(a, "--wait-server") && v) { wait_s = (unsigned)strtoul(v, NULL, 0); i++; }
		else if (!strcmp(a, "--codec") && v) { codec_s = v; i++; }
		else if (!strcmp(a, "--size") && v) { if (sscanf(v, "%ux%u", &w, &h) != 2) w = 0; i++; }
		else if (!strcmp(a, "--frames") && v) { frames = (unsigned)strtoul(v, NULL, 0); i++; }
		else if (!strcmp(a, "--fps") && v) { fps = (unsigned)strtoul(v, NULL, 0); i++; }
		else if (!strcmp(a, "--bitrate") && v) { bitrate = (uint32_t)strtoul(v, NULL, 0); bitrate_given = 1; i++; }
		else if (!strcmp(a, "--then") && v) { then = v; i++; }
		else if (!strcmp(a, "--venc-fast") && v) { venc_fast = (int)strtol(v, NULL, 0); i++; }
		else if (!strcmp(a, "--index") && v) { index = (unsigned)strtoul(v, NULL, 0); i++; }
		else if (!strcmp(a, "--retried") && v) { retried = (unsigned)strtoul(v, NULL, 0); i++; }
		else if (!strcmp(a, "--level") && v) { level_arg = strtol(v, NULL, 0); i++; }
		else if (!strcmp(a, "--profile") && v) { profile = (uint32_t)strtoul(v, NULL, 0); i++; }
		else if (!strcmp(a, "--open-timeout") && v) { open_s = (unsigned)strtoul(v, NULL, 0); i++; }
		else if (!strcmp(a, "--frame-timeout") && v) { frame_s = (unsigned)strtoul(v, NULL, 0); i++; }
		else if (!strcmp(a, "--console-every") && v) { con_every = (unsigned)strtoul(v, NULL, 0); i++; }
		else if (!strcmp(a, "--out") && v) { outdir = v; i++; }
		else if (!strcmp(a, "--hex") && v) { hex_bytes = (unsigned)strtoul(v, NULL, 0); i++; }
		else if (!strcmp(a, "--relay") && v) { relay = v; i++; }
		else if (!strcmp(a, "--socket") && v) { sock = v; i++; }
		else if (!strcmp(a, "--dma-dir") && v) { dma_dir = v; i++; }
		else if (!strcmp(a, "--no-console")) no_console = 1;
		else { fprintf(stderr, "n2vcd_probe: bad argument %s (see the head comment of n2vcd_probe.c)\n", a); return 2; }
	}
	int hevc = !strcmp(codec_s, "hvc1");
	if ((!hevc && strcmp(codec_s, "avc1")) || w < 16 || h < 16 || w > 4096 || h > 2160 || (w & 1) || (h & 1) || !frames ||
	    frames > 600 || !fps || fps > 240 || venc_fast < -1 || venc_fast > 1 || !open_s || !frame_s || level_arg < -1 || level_arg > 255 || (sock && !dma_dir)) {
		fprintf(stderr, "n2vcd_probe: bad codec/size/frames/fps or --socket without --dma-dir\n");
		return 2;
	}
	if (hex_bytes > 1024u) hex_bytes = 1024u;
	uint32_t level = level_arg < 0 ? 0u : level_arg ? (uint32_t)level_arg : auto_level(hevc, w, h, fps);
	if (!no_console) g_console = open("/dev/console", O_WRONLY | O_NOCTTY | O_NONBLOCK);
	{
		/* opus-decint: vc22 killed n2vcd_dprobe at 1080p (jetsam per-process limit, 6 MB effective); 4K NV12 frames
		 * here are 12 MB, so this probe needs the same 3072 MB limit vcodecd sets for itself */
		char ml[400], u[96];
		int lr = n2ml_raise(3072, ml, sizeof ml);
		say("jetsam %s %s; %s", lr == 0 ? "raised" : "NOT raised", ml, n2ml_usage_str(u, sizeof u));
	}
	if (gate) {
		if (access(gate, F_OK) != 0) { say("gate %s absent: not running", gate); return 0; }
		/* one-shot, and BEFORE anything reaches the hardware: if this run takes the phone down, the next boot does not repeat it */
		if (!keep_gate) say("gate %s %s (one run per installation)", gate, unlink(gate) == 0 ? "removed" : "NOT removed");
	}
	say("start index=%u venc_fast=%d pid=%d codec=%s %ux%u frames=%u fps=%u bitrate=%u%s level=%u profile=%u transport=%s delay=%us open_timeout=%us",
	    index, venc_fast, (int)getpid(), codec_s, w, h, frames, fps, bitrate, bitrate ? "" : "(auto)", level, profile, sock ? sock : relay, delay_s,
	    open_s);
	if (delay_s) {
		sleep_ms(delay_s * 1000u);
		/* q10ja bisect: q10jy/q10jz died here with no kernel trace line; each step is logged and followed by 2 s so the
		 * live USB poll fetches it before the next step can take the phone down */
		say("woke after %us", delay_s);
		sleep_ms(2000);
	}

	struct tp t;
	memset(&t, 0, sizeof t);
	t.fd = -1;
	t.next_handle = 1000;
	int ok = 0;
	uint32_t in_h = 0, out_h = 0;
	uint8_t *pic = NULL, *au = NULL;
	FILE *of = NULL;
	char path[600] = "-";
	uint64_t total = 0, t_frames = 0, gen_us_sum = 0, wr_us_sum = 0;
	unsigned done = 0, keys = 0, last_key = 0, max_gap = 0;
	uint32_t min_b = UINT32_MAX, max_b = 0, max_hw = 0;
	uint64_t hw_us_sum = 0;

	/* ---- connect and wait for the server */
	uint64_t wait_end = now_us() + (uint64_t)wait_s * 1000000u;
	if (sock) {
		t.socket_mode = 1;
		t.dma_dir = dma_dir;
		for (;;) {
			int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
			struct sockaddr_un sa;
			memset(&sa, 0, sizeof sa);
			sa.sun_family = AF_UNIX;
			snprintf(sa.sun_path, sizeof sa.sun_path, "%s", sock);
			if (fd >= 0 && connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0) { t.fd = fd; break; }
			if (fd >= 0) close(fd);
			if (now_us() >= wait_end) { say("server socket %s not reachable within %us", sock, wait_s); goto out; }
			sleep_ms(200);
		}
	} else {
#ifdef __APPLE__
		/* q10jz breadcrumbs: re-arm the kernel's per-entry log of the vcodec nodes (kern.n2vctrace, 64 entries) so the
		 * live USB log shows this process's first open/ioctl/close even if vcodecd used the budget; absent on q10jy. */
		{
			say("sysctl kern.n2vctrace=0");
			sleep_ms(2000);
			unsigned int v = 0;
			int sr = sysctlbyname("kern.n2vctrace", NULL, NULL, &v, sizeof v);
			say("sysctl -> %d errno %d; open %s next", sr, sr ? errno : 0, relay);
			sleep_ms(2000);
		}
#endif
		for (;;) {
			t.fd = open(relay, O_RDWR);
			if (t.fd >= 0) {
				struct rwvc_info info;
				memset(&info, 0, sizeof info);
				int r = ioctl(t.fd, RWVC_INFO, &info);
				if (r == 0 && info.server_present) {
					say("relay %s abi=%u server_present=1 powered=%#x pool_pa=%#llx pool=%u free=%u irq_mode=%u", relay,
					    info.abi_version, info.powered_mask, (unsigned long long)info.pool_pa, info.pool_bytes,
					    info.pool_free, info.irq_mode);
					break;
				}
				if (r != 0) say("RWVC_INFO failed errno=%d (%s)", errno, strerror(errno));
				close(t.fd);
				t.fd = -1;
			} else if (errno != ENOENT && errno != ENXIO) {
				say("open %s errno=%d (%s)", relay, errno, strerror(errno));
			}
			if (now_us() >= wait_end) {
				say("vcodecd not serving %s within %us (no node, or server_present=0)", relay, wait_s);
				goto out;
			}
			sleep_ms(1000);
		}
	}

	/* ---- VENC clock (0312 kern.n2vc.venc_fast), before the session powers the VENC */
	if (venc_fast >= 0) {
#ifdef __APPLE__
		unsigned int want = (unsigned int)venc_fast, got = 0;
		size_t gl = sizeof got;
		int sr = sysctlbyname("kern.n2vc.venc_fast", NULL, NULL, &want, sizeof want);
		int se = sr ? errno : 0;
		int gr = sysctlbyname("kern.n2vc.venc_fast", &got, &gl, NULL, 0);
		say("venc_fast: set %u -> %d errno %d (%s); read back %s%u", want, sr, se, se == ENOENT ? "kernel without 0312" : "ok",
		    gr ? "FAILED " : "", gr ? 0u : got);
#else
		say("venc_fast %d: no sysctl on this host", venc_fast);
#endif
	}

	/* ---- HELLO */
	_Alignas(16) uint8_t rep[RWVC_MSG_MAX];
	struct n2vcd_hello hel;
	memset(&hel, 0, sizeof hel);
	hdr(&hel.h, N2VCD_HELLO);
	hel.version = N2VCD_VERSION;
	int n = rpc(&t, &hel, sizeof hel, rep, sizeof rep, 5000);
	if (n < 0) { say("HELLO failed %d (%s)", n, strerror(-n)); goto out; }
	if (is_error(rep, n, "HELLO")) goto out;
	struct n2vcd_hello_ok hok;
	memset(&hok, 0, sizeof hok);
	memcpy(&hok, rep, (uint32_t)n < sizeof hok ? (uint32_t)n : sizeof hok);
	say("HELLO_OK version=%u enc=%#x dec=%#x max=%ux%u", hok.version, hok.enc_codecs, hok.dec_codecs, hok.max_width, hok.max_height);

	/* ---- OPEN (the vendor encoder init: M-d happens here) */
	struct n2vcd_open op;
	memset(&op, 0, sizeof op);
	hdr(&op.h, N2VCD_OPEN);
	op.codec = hevc ? N2VCD_CODEC_HEVC : N2VCD_CODEC_H264;
	op.encoder = 1;
	op.width = w;
	op.height = h;
	op.fps_num = fps;
	op.fps_den = 1;
	op.bitrate = bitrate;
	op.gop = fps;
	op.profile = profile;
	op.level = level;
	op.flags = 0;                         /* vcodecd v1 refuses bit 0 (low latency): vcodecd.c:442 */
	uint64_t t_open = now_us();
	n = rpc(&t, &op, sizeof op, rep, sizeof rep, open_s * 1000u);
	if (n < 0) {
		say("OPEN failed %d (%s) after %llums", n, strerror(-n), (unsigned long long)((now_us() - t_open) / 1000u));
		want_retry = n != -ETIMEDOUT;             /* the server went away under the request (restart / guest stop) */
		goto out;
	}
	if (is_error(rep, n, "OPEN")) {
		struct n2vcd_simple e;
		memset(&e, 0, sizeof e);
		memcpy(&e, rep, (uint32_t)n < sizeof e ? (uint32_t)n : sizeof e);
		/* vcodecd --restart-on-switch: BUSY/RESTART = it exits now and launchd starts a fresh one; a guest stop (E_GUEST)
		 * also ends that process (exit 70; restarted when KeepAlive is on) */
		want_retry = (e.status == N2VCD_E_BUSY && e.detail == N2VCD_DETAIL_RESTART) || e.status == N2VCD_E_GUEST;
		goto out;
	}
	struct n2vcd_open_ok ook;
	memset(&ook, 0, sizeof ook);
	memcpy(&ook, rep, (uint32_t)n < sizeof ook ? (uint32_t)n : sizeof ook);
	g_session = ook.h.session;
	say("OPEN_OK session=%u in_buf=%u out_buf=%u in %llums", g_session, ook.in_buf_bytes, ook.out_buf_bytes,
	    (unsigned long long)((now_us() - t_open) / 1000u));

	uint32_t stride = (w + 15u) & ~15u, ha = (h + 15u) & ~15u;
	uint32_t pic_bytes = stride * ha * 3u / 2u;
	uint32_t in_bytes = ook.in_buf_bytes > pic_bytes ? ook.in_buf_bytes : pic_bytes;
	uint32_t out_bytes = ook.out_buf_bytes ? ook.out_buf_bytes : 4u << 20;
	uint32_t in_pa = 0, out_pa = 0;
	int r = tp_alloc(&t, in_bytes, &in_h, &in_pa);
	if (r == 0) r = tp_alloc(&t, out_bytes, &out_h, &out_pa);
	if (r != 0) { say("DMA alloc failed %d (%s)", r, strerror(-r)); goto close_session; }
	say("buffers in=%u (%u B pa %#x) out=%u (%u B pa %#x)", in_h, in_bytes, in_pa, out_h, out_bytes, out_pa);
	pic = (uint8_t *)calloc(1, in_bytes);
	au = (uint8_t *)malloc(out_bytes);
	if (!pic || !au) { say("out of memory"); goto close_session; }

	(void)mkdir(outdir, 0755);
	snprintf(path, sizeof path, "%s/probe-%d-%u-%s-%ux%u.%s", outdir, (int)getpid(), index, codec_s, w, h, hevc ? "h265" : "h264");
	of = fopen(path, "wb");
	if (!of) { say("cannot create %s errno=%d", path, errno); goto close_session; }

	/* ---- frames (M-e / M-f / the first bit stream) */
	t_frames = now_us();
	for (unsigned f = 0; f < frames; f++) {
		g_console_mute = !(f < 3u || f + 1u == frames || (con_every && f % con_every == 0u));
		uint64_t t_g = now_us();
		make_nv12(pic, w, h, stride, f);
		uint64_t t_w = now_us();
		r = tp_rw(&t, in_h, 0, pic, pic_bytes, 1);
		uint64_t wr_us = now_us() - t_w;
		gen_us_sum += t_w - t_g;
		wr_us_sum += wr_us;
		g_console_mute = g_console_mute && r == 0;
		if (r != 0) { say("frame %u: DMA write failed %d", f, r); break; }
		struct n2vcd_enc_frame ef;
		memset(&ef, 0, sizeof ef);
		hdr(&ef.h, N2VCD_ENC_FRAME);
		ef.in_handle = in_h;
		ef.stride = stride;
		ef.out_handle = out_h;
		ef.out_capacity = out_bytes;
		ef.pts = (uint64_t)f * 1000000u / fps;
		ef.force_key = f == 0;
		uint64_t t_f = now_us();
		n = rpc(&t, &ef, sizeof ef, rep, sizeof rep, frame_s * 1000u);
		uint64_t ms = (now_us() - t_f) / 1000u;
		if (n < 0) { g_console_mute = 0; say("frame %u: ENC_FRAME failed %d (%s) after %llums", f, n, strerror(-n), (unsigned long long)ms); break; }
		char what[32];
		snprintf(what, sizeof what, "frame %u ENC_FRAME", f);
		if (n >= (int)sizeof(struct n2vcd_hdr) && ((const struct n2vcd_hdr *)(const void *)rep)->type == N2VCD_ERROR) g_console_mute = 0;
		if (is_error(rep, n, what)) break;
		struct n2vcd_enc_frame_ok eo;
		memset(&eo, 0, sizeof eo);
		memcpy(&eo, rep, (uint32_t)n < sizeof eo ? (uint32_t)n : sizeof eo);
		if (eo.out_bytes == 0 || eo.out_bytes > out_bytes) { g_console_mute = 0; say("frame %u: out_bytes=%u invalid", f, eo.out_bytes); break; }
		r = tp_rw(&t, out_h, 0, au, eo.out_bytes, 0);
		if (r != 0) { g_console_mute = 0; say("frame %u: DMA read failed %d", f, r); break; }
		if (fwrite(au, 1, eo.out_bytes, of) != eo.out_bytes) { g_console_mute = 0; say("frame %u: write %s failed", f, path); break; }
		fflush(of);
		char nals[96];
		nal_summary(au, eo.out_bytes, hevc, nals, sizeof nals);
		say("frame %u bytes=%u key=%u hdr=%u hw_us=%u rtt=%llums gen_ms=%llu wr_ms=%llu nal=%s", f, eo.out_bytes, eo.key_frame,
		    eo.header_bytes, eo.hw_us, (unsigned long long)ms, (unsigned long long)((t_w - t_g) / 1000u),
		    (unsigned long long)(wr_us / 1000u), nals[0] ? nals : "NONE");
		g_console_mute = 0;
		if (f == 0) {
			unsigned lim = hex_bytes < eo.out_bytes ? hex_bytes : eo.out_bytes;
			for (unsigned o = 0; o < lim; o += 32) {
				char hx[72];
				unsigned k, e = o + 32 < lim ? o + 32 : lim;
				for (k = o; k < e; k++) snprintf(hx + 2 * (k - o), 3, "%02x", au[k]);
				hx[2 * (e - o)] = 0;
				say("hex %04x %s", o, hx);
			}
		}
		total += eo.out_bytes;
		hw_us_sum += eo.hw_us;
		if (eo.out_bytes < min_b) min_b = eo.out_bytes;
		if (eo.out_bytes > max_b) max_b = eo.out_bytes;
		if (eo.hw_us > max_hw) max_hw = eo.hw_us;
		if (eo.key_frame) { if (keys && f - last_key > max_gap) max_gap = f - last_key; last_key = f; }
		keys += eo.key_frame != 0;
		done++;
	}
	ok = done == frames && keys >= 1;
	g_console_mute = 0;
	if (done) {
		uint64_t wall = now_us() - t_frames;
		say("stats frames=%u bytes min/avg/max=%u/%llu/%u keys=%u max_key_gap=%u hw_ms avg/max=%llu.%03llu/%u.%03u gen_ms avg=%llu "
		    "wr_ms avg=%llu wall=%llums fps=%llu.%01llu kbps@%ufps=%llu", done, min_b, (unsigned long long)(total / done), max_b, keys,
		    max_gap, (unsigned long long)(hw_us_sum / done / 1000u), (unsigned long long)(hw_us_sum / done % 1000u),
		    max_hw / 1000u, max_hw % 1000u, (unsigned long long)(gen_us_sum / done / 1000u),
		    (unsigned long long)(wr_us_sum / done / 1000u), (unsigned long long)(wall / 1000u),
		    (unsigned long long)(wall ? done * 1000000ull / wall : 0), (unsigned long long)(wall ? done * 10000000ull / wall % 10u : 0),
		    fps, (unsigned long long)(total * 8u * fps / done / 1000u));
	}

close_session:
	if (g_session) {
		struct n2vcd_simple cl;
		memset(&cl, 0, sizeof cl);
		hdr(&cl.h, N2VCD_CLOSE);
		n = rpc(&t, &cl, sizeof cl, rep, sizeof rep, 5000);
		if (n < 0) say("CLOSE failed %d", n);
		else if (!is_error(rep, n, "CLOSE")) say("CLOSE_OK");
	}
out:
	if (of) { fsync(fileno(of)); fclose(of); }
	tp_free(&t, in_h);
	tp_free(&t, out_h);
	if (t.fd >= 0) close(t.fd);
	free(pic);
	free(au);
	say("DONE ok=%d frames=%u/%u keys=%u bytes=%llu hw_ms=%llu file=%s index=%u codec=%s size=%ux%u%s", ok, done, frames, keys,
	    (unsigned long long)total, (unsigned long long)(hw_us_sum / 1000u), done ? path : "-", index, codec_s, w, h,
	    then && *then ? " (chain continues)" : "");
	if (!done && path[0] == '/') unlink(path);
	if (want_retry && retried < 2u) {
		/* same session again in a fresh image of this program: no gate, no delay, wait up to 30 s for the new server */
		const char *nv[128];
		char nret[12];
		int k = 0;
		snprintf(nret, sizeof nret, "%u", retried + 1u);
		nv[k++] = argv[0];
		for (int i = 1; i < argc && k < 120; i++) {
			if ((!strcmp(argv[i], "--gate") || !strcmp(argv[i], "--delay") || !strcmp(argv[i], "--retried") ||
			     !strcmp(argv[i], "--wait-server")) && i + 1 < argc) { i++; continue; }
			if (!strcmp(argv[i], "--keep-gate")) continue;
			nv[k++] = argv[i];
		}
		nv[k++] = "--wait-server"; nv[k++] = "30";
		nv[k++] = "--retried"; nv[k++] = nret;
		nv[k] = NULL;
		say("retry: OPEN of session %u refused/lost (server restarting): reconnecting, attempt %u of 2", index, retried + 1u);
		sleep_ms(1500);                           /* let the old server exit and launchd start the new one */
		if (g_console >= 0) close(g_console);
		fflush(stdout);
		execv(argv[0], (char *const *)nv);
		fprintf(stdout, "N2VCDP retry: execv %s failed errno=%d\n", argv[0], errno);
	}
	if (then && *then) {
		/* next entry "codec:WxH[:frames[:fps]]" of --then; the rest of the list is passed on */
		char cur[64], nsize[24], nframes[12], nfps[12], nidx[12], nbr[16], nprof[12], nhex[12], nos[12], nfs[12], nce[12];
		const char *comma = strchr(then, ',');
		size_t len = comma ? (size_t)(comma - then) : strlen(then);
		if (len >= sizeof cur) len = sizeof cur - 1;
		memcpy(cur, then, len);
		cur[len] = 0;
		char ncodec[8] = "";
		unsigned nw = 0, nh = 0, nf = frames, nr = fps, np = profile;
		int nl = level_arg < 0 ? -1 : 0, have_l = 0;
		char nspeed[8] = "";
		int got = sscanf(cur, "%7[a-z0-9]:%ux%u:%u:%u:%u:%d:%7[a-z]", ncodec, &nw, &nh, &nf, &nr, &np, &nl, nspeed);
		int nfast = !strcmp(nspeed, "fast") ? 1 : !strcmp(nspeed, "slow") ? 0 : venc_fast;   /* else inherit */
		have_l = got >= 7 || level_arg < 0;
		if (got < 3) {
			say("--then entry '%s' unreadable: stopping the chain", cur);
			return ok ? 0 : 1;
		}
		snprintf(nsize, sizeof nsize, "%ux%u", nw, nh);
		snprintf(nframes, sizeof nframes, "%u", nf);
		snprintf(nfps, sizeof nfps, "%u", nr);
		snprintf(nidx, sizeof nidx, "%u", index + 1u);
		snprintf(nbr, sizeof nbr, "%u", bitrate);
		snprintf(nprof, sizeof nprof, "%u", np);
		char nlev[12];
		snprintf(nlev, sizeof nlev, "%d", nl);
		snprintf(nhex, sizeof nhex, "%u", hex_bytes);
		snprintf(nos, sizeof nos, "%u", open_s);
		snprintf(nfs, sizeof nfs, "%u", frame_s);
		snprintf(nce, sizeof nce, "%u", con_every);
		const char *nv[64];
		int k = 0;
		nv[k++] = argv[0];
		nv[k++] = "--index"; nv[k++] = nidx;
		nv[k++] = "--codec"; nv[k++] = ncodec;
		nv[k++] = "--size"; nv[k++] = nsize;
		nv[k++] = "--frames"; nv[k++] = nframes;
		nv[k++] = "--fps"; nv[k++] = nfps;
		if (bitrate_given) { nv[k++] = "--bitrate"; nv[k++] = nbr; }
		nv[k++] = "--profile"; nv[k++] = nprof;
		/* per-entry LEVEL (7th field; -1 = send 0 = vcodecd's choice), else --level -1 for the whole chain, else probe auto */
		if (have_l) { nv[k++] = "--level"; nv[k++] = nlev; }
		char nvf[4];
		if (nfast >= 0) { snprintf(nvf, sizeof nvf, "%d", nfast); nv[k++] = "--venc-fast"; nv[k++] = nvf; }
		nv[k++] = "--hex"; nv[k++] = nhex;
		nv[k++] = "--open-timeout"; nv[k++] = nos;
		nv[k++] = "--frame-timeout"; nv[k++] = nfs;
		nv[k++] = "--console-every"; nv[k++] = nce;
		nv[k++] = "--wait-server"; nv[k++] = "30";
		nv[k++] = "--out"; nv[k++] = outdir;
		if (sock) { nv[k++] = "--socket"; nv[k++] = sock; nv[k++] = "--dma-dir"; nv[k++] = dma_dir; }
		else { nv[k++] = "--relay"; nv[k++] = relay; }
		if (no_console) nv[k++] = "--no-console";
		if (comma && comma[1]) { nv[k++] = "--then"; nv[k++] = comma + 1; }
		nv[k] = NULL;
		say("chain: session %u ok=%d; next %s (index %u)", index, ok, cur, index + 1u);
		if (g_console >= 0) close(g_console);
		fflush(stdout);
		execv(argv[0], (char *const *)nv);
		fprintf(stdout, "N2VCDP chain: execv %s failed errno=%d\n", argv[0], errno);
	}
	return ok ? 0 : 1;
}
