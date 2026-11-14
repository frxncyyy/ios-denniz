/* SPDX-License-Identifier: MIT */
/*
 * vcodecd.c -- the daemon that hosts the MT6893 VENC through the interpreted vendor library and serves it to VideoToolbox
 * clients over include/n2vcd_proto.h.
 *
 * One process.  It opens the hardware back end (T02's fake with --fake, /dev/redwood-vcodec otherwise), brings the hosted
 * vendor set up through n2vhost_start() (IV), then serves datagrams from the kernel relay (phone) or a UNIX socket (host
 * tests):
 *
 *   HELLO          version + capabilities
 *   OPEN           one encoder session at a time (N2VCD_E_BUSY for a second one); decoder requests are E_UNSUPPORTED in v1
 *   ENC_FRAME      hands the client's DMA handles to n2ap_venc_encode() (V41), waits for the asynchronous result with
 *                  n2ap_venc_wait_done(), and on a key frame the access unit starts with SPS/PPS(/VPS) and the reply says
 *                  how many bytes that is (header_bytes)
 *   ENC_SET        run-time bitrate/framerate/force-key through VENC_SET_PARAM_ADJUST_BITRATE/FRAMERATE and FORCE_INTRA
 *   FLUSH          no-op success: one encode is complete before its reply, nothing is ever queued
 *   CLOSE          session teardown
 *
 * The session opens in the stock separate-header mode: after n2ap_venc_start() one N2AP_ENC_SEQ_HDR job fills a private
 * DMA buffer, the daemon caches those bytes and prepends them before every IDR that does not already carry them.  Nothing
 * depends on the vendor's CPU-side joined-header memmove reading a fresh DMA mirror.
 *
 * A guest stop (n2vhost_join(h, 0) == 0: UNDEF/fault/missing import) answers N2VCD_E_GUEST to the owner and every other
 * client, logs one line with the stop address described by n2elf_describe(), drops the VENC power references and exits 70;
 * launchd restarts the daemon.  A hardware timeout answers N2VCD_E_HW, tears the session down and keeps serving.
 * --selftest runs the user-mode interpreter checks before any hardware or vendor file is touched.
 *
 * Log lines go to stderr and /dev/console, prefix "N2VCD ".
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef N2IP_TEST_HOOKS
#include <sys/stat.h>
#endif

#include "n2vcd_proto.h"
#include "n2vc_hw.h"
#include "redwood_vcodec.h"

#include "n2ip_deps.h"
#include "n2vcd_srv.h"
#include "n2memlimit.h"        /* opus-guestmem: own jetsam limit + footprint log */
#include "n2vcd_logcap.h"      /* opus-robust: vcodecd.log size cap (4 MiB + one .1 backup) */
#include "n2vcd_vclock.h"      /* vcodec-20261114: automatic VENC clock (kern.n2vc.venc_fast) */
#ifdef __APPLE__
extern int sysctlbyname(const char *, void *, size_t *, void *, size_t);   /* <sys/sysctl.h> needs the BSD types _POSIX_C_SOURCE hides */
#endif
/* opus-enc4k: V40's phase counters and VENC power hold (vcu/n2vcu.c); weak so test builds with the fake vhost link */
struct n2vcu;
extern void n2vcu_venc_phases(struct n2vcu *vcu, uint64_t out[7]) __attribute__((weak));
extern void n2vcu_set_venc_hold(struct n2vcu *vcu, int hold) __attribute__((weak));
extern void n2vcu_venc_release(struct n2vcu *vcu) __attribute__((weak));
#include "n2vcd_selftest.h"

/* opus-decint (2026-11-11): the decoder service of daemon/dec (DEC05) over DEC01's ap/n2ap_vdec, behind --decoder.
 * Built only when the build defines N2VCD_WITH_DECODER and links daemon/dec/n2vcd_dec_service.c,
 * daemon/dec/n2vcd_dec_ap_n2ap.c and ap/n2ap_vdec.c (ios/build.sh does); other builds refuse --decoder. */
#ifdef N2VCD_WITH_DECODER
#include "n2vcd_dec_service.h"
#include "n2vcd_dec_ap_n2ap.h"
#include "n2vcu.h"            /* n2vcu_stats for the per-START perf line */
#endif

#if defined(N2IP_TEST_HOOKS) && defined(N2IP_FAKE_VENC)
#include "n2venc_fake.h"        /* opus-venc's behavioural VENC model, test builds only */
#endif
#if defined(N2IP_TEST_HOOKS) && defined(N2IP_FAKE_VDEC_HEVC)
#include "n2vdec_hevc_fake.h"   /* opus-hevcdec's HEVC VDEC model, test builds only (--fake-vdec-hevc) */
#endif
#if defined(N2IP_TEST_HOOKS) && defined(N2IP_FAKE_VDEC_FMT)
#include "n2vdec_fmt_fake.h"    /* opus-fmt: VP8/VP9/AV1/MPEG/H.263 sub-block model on top of the HEVC model (--fake-vdec-fmt) */
#endif

/* --selftest lives in its own source but is part of the daemon binary.  Included here (rather than compiled as a
 * second object) so every build of this file carries it unchanged: build_ios.sh, ios/build.sh and the host test. */
#include "n2vcd_selftest.c"

/* The gpu/video SDK errno.h is a subset.  Darwin values. */
#ifndef ENOSYS
#define ENOSYS 78
#endif
#ifndef ENOTTY
#define ENOTTY 25
#endif

/* The wire layout is frozen; make a header change fail here instead of on the phone. */
_Static_assert(sizeof(struct n2vcd_hdr) == 16, "n2vcd_hdr layout");
_Static_assert(sizeof(struct n2vcd_hello) == 24, "n2vcd_hello layout");
_Static_assert(sizeof(struct n2vcd_hello_ok) == 40, "n2vcd_hello_ok layout");
_Static_assert(sizeof(struct n2vcd_open) == 80, "n2vcd_open layout");
_Static_assert(sizeof(struct n2vcd_open_ok) == 32, "n2vcd_open_ok layout");
_Static_assert(sizeof(struct n2vcd_enc_frame) == 48, "n2vcd_enc_frame layout");
_Static_assert(sizeof(struct n2vcd_enc_frame_ok) == 48, "n2vcd_enc_frame_ok layout");
_Static_assert(sizeof(struct n2vcd_enc_set) == 32, "n2vcd_enc_set layout");
_Static_assert(sizeof(struct n2vcd_simple) == 24, "n2vcd_simple layout");
_Static_assert(sizeof(struct n2vcd_open) <= RWVC_MSG_MAX, "protocol datagram fits RWVC_MSG_MAX");

#define N2VCD_SESSION_NONE  0u
#define N2VCD_MAX_WIDTH     3840u
#define N2VCD_MAX_HEIGHT    2160u          /* the VENC's 4K limit; advertised before any session exists */
/* opus-enc4k, 12 Nov: the vendor library's own QUERY_CAP (host runner, AP_IPIMSG_ENC_QUERY_CAP frame sizes) says H.264 and
 * H.265 16..3840 step 16 x 16..2176 step 8, max level enum 15 (H.264 5.1) / 8 (HEVC 5.1), H.264 profiles Baseline|Main|High,
 * HEVC Main only.  4096-wide encodes are therefore refused here (E_ARGS) instead of reaching the hardware. (Was 4096.) */
#define N2VCD_HDR_RESERVE   65536u          /* room left at the start of the output buffer for SPS/PPS(/VPS) */
#define N2VCD_HDR_MAX       512u
#define N2VCD_DEFAULT_HW_TIMEOUT_MS 2000
#define N2VCD_OPEN_IPI_MIN_MS 10000u      /* per-half IPI bound for a session opened with the default hw timeout */
#define N2VCD_DEFAULT_IDLE_MS 10000
#define N2VCD_LOOP_SLICE_MS 100

static uint64_t
now_us(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) { return 0; }
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static uint32_t
align16(uint32_t v)
{
	return (v + 15u) & ~15u;
}

struct session {
	int active;
	uint32_t id;
	uint32_t client;
	uint32_t codec, fourcc, width, height;
	uint32_t fps_num, fps_den, gop, bitrate, profile, level, flags;
	uint32_t in_bytes, out_bytes;
	struct n2ap_venc *enc;
	uint64_t frames, out_total, hw_us_total;
	uint64_t opened_us;
	uint8_t hdr[N2VCD_HDR_MAX];
	uint32_t hdr_len;
	uint8_t *bounce;
	uint32_t bounce_cap;
	uint32_t last_status, last_detail;
	int dma_warned;
	int dma_reg_warned;
	uint64_t last_activity_us;
	uint32_t reg_handle[16];            /* client DMA handles registered with V41's pa-only registry */
	uint32_t reg_pa[16];
	uint64_t reg_use[16];               /* LRU stamp; a client pool larger than 16 evicts the oldest */
	uint64_t use_ctr;
	unsigned nreg;
};

static struct {
	struct n2vc_hw hw;                  /* the back end as opened (fake or /dev/redwood-vcodec) */	const struct n2vc_hw *vhw;          /* IV's wrapper, the one the AP side must use */
	struct n2vhost *vh;
	struct n2a32_mem *mem;
	struct n2vcu *vcu;
	struct n2vcd_srv *srv;
	int console_fd;
	int hw_timeout_ms;
	int hw_timeout_explicit;            /* --hw-timeout-ms given: no OPEN floor */
	int session_idle_ms;
	uint64_t start_us;
	volatile sig_atomic_t stop;
	uint32_t next_session;
	int dma_info_broken;                /* the relay does not implement RWVC_DMA_INFO (old kernel / host) */
	char dma_dir[256];                  /* test build only: --dma-dir, also the pa source for client handles */
	unsigned last_milestone;
	uint32_t seen[16];                  /* clients that have sent at least one datagram (for the fatal broadcast) */
	unsigned nseen;
	struct session s;
	int decoder;                        /* --decoder: serve decoder sessions (daemon/dec) */
	int fake_vdec_irq;                  /* --fake-vdec-irq (test builds, --fake only): WAITISR answered done */
	int dec_wc;                         /* --dec-wc off|on|alt: DEC04 write-combining during decoder sessions */
	int restart_on_switch;              /* --restart-on-switch (opus-enc4k): exit 75 instead of opening another codec */
	int venc_keep_power;                /* --venc-keep-power (opus-enc4k): V40 holds VENC power for the whole session */
	int vclk_mode;                      /* --venc-clock (n2vcd_vclock.h), default auto; N2VCLK_FILE overrides per OPEN */
	uint64_t vclk_thr;
	int vclk_raised;                    /* this process wrote kern.n2vc.venc_fast = 1 for the open session */
	uint32_t last_codec;                /* codec of the last encoder session opened by this process, 0 = none */
#ifdef N2VCD_WITH_DECODER
	struct n2vcd_dec_service *dec;
	struct n2vcd_dec_ap_n2ap *dec_ap;
	unsigned dec_trace_lines;
	uint64_t dec_t_start_us;
	uint64_t dec_t_lock_us, dec_t_lat_us, dec_t_core_us, dec_t_unlock_us;   /* per-START phase stamps (perf) */
	uint64_t dec_r0, dec_w0, dec_c0;                                         /* vcu mmio/ioctl counters at START */
	uint32_t dec_inputs;
#endif
} g;

/* 1 while a decoder session exists: the encoder and the decoder are serialised (one vendor process, one interpreter
 * thread serving both; DEC05 STATUS item 4). */
static int
decoder_busy(void)
{
#ifdef N2VCD_WITH_DECODER
	return g.dec != NULL && n2vcd_dec_service_session(g.dec) != 0u;
#else
	return 0;
#endif
}

#if defined(N2IP_TEST_HOOKS) && defined(N2IP_FAKE_VENC)
/* --fake-venc: the real vendor library + its AP path on top of T02's register fake and opus-venc's VENC model.
 * Test builds only; the product binary has neither symbol. */
static struct n2vc_hw g_fv_inner;
static struct n2vc_hw g_fv_outer;
static struct n2venc_fake *g_fv;
#endif

static void n2vcd_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static struct n2vcd_logcap g_logcap;  /* opus-robust: armed in main() when fd 2 is vcodecd.log */

static void
n2vcd_log(const char *fmt, ...)
{
	char buf[768];
	uint64_t ms = (now_us() - g.start_us) / 1000u;
	int n = snprintf(buf, sizeof(buf), "N2VCD %llu.%03llu ",
	                 (unsigned long long)(ms / 1000u), (unsigned long long)(ms % 1000u));
	if (n < 0) { return; }
	size_t used = (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1u;
	va_list ap;
	va_start(ap, fmt);
	int m = vsnprintf(buf + used, sizeof(buf) - used, fmt, ap);
	va_end(ap);
	if (m > 0) { used += (size_t)m < sizeof(buf) - used ? (size_t)m : sizeof(buf) - used - 1u; }
	if (used + 1u < sizeof(buf)) { buf[used++] = '\n'; buf[used] = '\0'; }
	(void)write(2, buf, used);
	if (g.console_fd >= 0) { (void)write(g.console_fd, buf, used); }
	n2vcd_logcap_account(&g_logcap, used);
}

/* opus-decint: --guest-log N puts the first N distinct lines of the vendor's own __android_log_print (V33's sink, a weak
 * pointer defined by IV) into vcodecd.log: the vendor names its own failures ("ion_import failed", "Wait PicNum rst
 * fail", ...), which no register trace shows.  Consecutive repeats are counted, not printed. */
extern void (*n2shim_log)(int prio, const char *tag, const char *text) __attribute__((weak));
static unsigned g_guest_log_max, g_guest_log_n, g_guest_log_rep;
static char g_guest_log_prev[160];
static pthread_mutex_t g_guest_log_mu = PTHREAD_MUTEX_INITIALIZER;
static void n2vcd_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void
guest_log_sink(int prio, const char *tag, const char *text)
{
	if (!text) { return; }
	pthread_mutex_lock(&g_guest_log_mu);
	if (strncmp(g_guest_log_prev, text, sizeof(g_guest_log_prev) - 1u) == 0) {
		g_guest_log_rep++;
		pthread_mutex_unlock(&g_guest_log_mu);
		return;
	}
	unsigned rep = g_guest_log_rep;
	g_guest_log_rep = 0;
	(void)snprintf(g_guest_log_prev, sizeof(g_guest_log_prev), "%s", text);
	int show = g_guest_log_n < g_guest_log_max;
	if (show) { g_guest_log_n++; }
	pthread_mutex_unlock(&g_guest_log_mu);
	if (show) {
		if (rep) { n2vcd_log("guest: (previous line repeated %u times)", rep); }
		n2vcd_log("guest %s/%d %.300s", tag ? tag : "-", prio, text);
	}
}

/* opus-guestmem: n2a32_mem diagnostics (mprotect failures, big maps) into the daemon log */
static void vcodecd_mem_log(const char *line) { n2vcd_log("%s", line); }

static void
on_signal(int sig)
{
	(void)sig;
	g.stop = 1;
}

/* IV reports the guest's end through n2vhost_join(h, 0) (0 = ended, -1 = still running) and the numeric
 * stop reason through n2vhost_stop_reason().  The frozen protocol's `detail` field carries that enum. */
static uint32_t
guest_stop_code(void)
{
	if (!g.vh) { return 0; }
	return (uint32_t)n2vhost_stop_reason(g.vh);
}

static int
guest_stopped(void)
{
	return g.vh && n2vhost_join(g.vh, 0) == 0;
}

static int
session_owned(uint32_t session, uint32_t client)
{
	return g.s.active && session == g.s.id && client == g.s.client;
}

static uint64_t
session_avg_hw_us(void)
{
	return g.s.frames ? g.s.hw_us_total / g.s.frames : 0;
}

static void session_forget_handles(void);

/* vcodec-20261114: kern.n2vc.venc_fast access (0312).  Test builds can redirect it to a file (N2VCLK_TEST_FILE) so the
 * host test sees every write; without the sysctl (Linux) the policy only logs. */
static int
vclk_get(unsigned *v)
{
	const char *tf = getenv("N2VCLK_TEST_FILE");
	if (tf) {
		FILE *f = fopen(tf, "r");
		if (!f) { *v = 0; return 0; }
		int r = fscanf(f, "%u", v) == 1 ? 0 : -1;
		fclose(f);
		return r;
	}
#ifdef __APPLE__
	size_t l = sizeof(*v);
	return sysctlbyname(N2VCLK_SYSCTL, v, &l, NULL, 0) == 0 ? 0 : -errno;
#else
	return -ENOENT;
#endif
}
static int
vclk_set(unsigned v)
{
	const char *tf = getenv("N2VCLK_TEST_FILE");
	if (tf) {
		FILE *f = fopen(tf, "w");
		if (!f) { return -errno; }
		fprintf(f, "%u\n", v);
		fclose(f);
		return 0;
	}
#ifdef __APPLE__
	return sysctlbyname(N2VCLK_SYSCTL, NULL, NULL, &v, sizeof(v)) == 0 ? 0 : -errno;
#else
	(void)v;
	return -ENOENT;
#endif
}
static void
vclk_session_open(uint32_t w, uint32_t h, uint32_t fps_num, uint32_t fps_den)
{
	int mode = g.vclk_mode;
	uint64_t thr = g.vclk_thr;
	const char *path = getenv("N2VCLK_FILE_PATH");
	int fr = n2vclk_read_file(path ? path : N2VCLK_FILE, &mode, &thr);
	int want = n2vclk_want(mode, thr, w, h, fps_num, fps_den);
	g.vclk_raised = 0;
	if (want < 0) {
		n2vcd_log("venc clock: mode keep%s: kern.n2vc.venc_fast left alone", fr > 0 ? " (file)" : "");
		return;
	}
	unsigned cur = 0;
	int gr = vclk_get(&cur);
	if (gr != 0) {
		n2vcd_log("venc clock: mode %s%s wants %u for %ux%u@%u/%u but %s is not readable (%d): left alone",
		          n2vclk_mode_name(mode), fr > 0 ? " (file)" : fr < 0 ? " (file invalid, ignored)" : "", (unsigned)want,
		          w, h, fps_num, fps_den, N2VCLK_SYSCTL, gr);
		return;
	}
	int sr = 0;
	if (cur != (unsigned)want) { sr = vclk_set((unsigned)want); }
	if (sr == 0 && want == 1 && cur == 0u) { g.vclk_raised = 1; }
	n2vcd_log("venc clock: mode %s%s, %ux%u@%u/%u = %llu px/s (threshold %llu) -> kern.n2vc.venc_fast %u -> %u (%s)%s",
	          n2vclk_mode_name(mode), fr > 0 ? " (file)" : fr < 0 ? " (file invalid, ignored)" : "", w, h, fps_num, fps_den,
	          (unsigned long long)((uint64_t)w * h * (fps_num ? fps_num : 30u) / (fps_den ? fps_den : 1u)),
	          (unsigned long long)thr, cur, (unsigned)want, want ? "624 MHz" : "273 MHz",
	          sr ? " WRITE FAILED" : "");
}
static void
vclk_session_close(void)
{
	if (!g.vclk_raised) { return; }
	g.vclk_raised = 0;
	int sr = vclk_set(0u);
	n2vcd_log("venc clock: session closed, kern.n2vc.venc_fast 1 -> 0 (273 MHz)%s", sr ? " WRITE FAILED" : "");
}

static void
session_end(uint32_t status, uint32_t detail, const char *why)
{
	if (!g.s.active) { return; }
	uint32_t id = g.s.id;
	if (g.s.enc) {
		(void)n2ap_venc_close(g.s.enc);
		g.s.enc = NULL;
	}
	session_forget_handles();
	if (g.venc_keep_power && n2vcu_venc_release && g.vcu) { n2vcu_venc_release(g.vcu); }   /* opus-enc4k: the held cores */
	/* Every VENC power reference this open file holds must be gone after the AP close.  K02 counts references per open file
	 * and returns EINVAL when none is left, so these bounded loops can only drop our own leftovers.  Both cores are
	 * covered because V41's power path references VENC and VENC_C1 together (mt6885/mtk_vcodec_enc_pm.c:273-279). */
	if (g.hw.power) {
		const struct n2vc_hw *phw = g.vhw ? g.vhw : &g.hw;
		static const unsigned blocks[] = { RWVC_BLK_VENC_C1, RWVC_BLK_VENC };
		for (unsigned b = 0; b < sizeof(blocks) / sizeof(blocks[0]); b++) {
			for (unsigned i = 0; i < 16u; i++) {
				if (phw->power(phw->ctx, blocks[b], 0) != 0) { break; }
			}
		}
	}
	n2vcd_log("session %u close frames=%llu bytes=%llu avg_hw_us=%llu%s%s",
	          id, (unsigned long long)g.s.frames, (unsigned long long)g.s.out_total, (unsigned long long)session_avg_hw_us(),
	          status != N2VCD_E_OK ? " reason=" : "", status != N2VCD_E_OK ? why : "");
	if (g.s.bounce) {
		free(g.s.bounce);
		g.s.bounce = NULL;
		g.s.bounce_cap = 0;
	}
	g.s.active = 0;
	vclk_session_close();
	g.s.last_status = status;
	g.s.last_detail = detail;
	g.s.id = N2VCD_SESSION_NONE;
}

static void
send_error(uint32_t to, uint32_t seq, uint32_t session, uint32_t status, uint32_t detail);

/* A send that fails because the client vanished is the same event as a close notice: tear its session down. */
static int
send_msg(uint32_t to, const void *msg, uint32_t bytes)
{
	int r = n2vcd_srv_send(g.srv, to, msg, bytes);
	if (r == -EPIPE) {
		n2vcd_log("client %u gone while sending", to);
		if (g.s.active && g.s.client == to) { session_end(N2VCD_E_SESSION, 0, "client gone"); }
	} else if (r == -EAGAIN) {
		n2vcd_log("client %u queue full, reply dropped (%u bytes)", to, bytes);
	} else if (r < 0) {
		n2vcd_log("send to client %u failed: %d", to, r);
	}
	return r;
}

static void
reply_simple(uint32_t to, uint32_t seq, uint32_t session, uint32_t type, uint32_t status, uint32_t detail)
{
	struct n2vcd_simple m;
	memset(&m, 0, sizeof(m));
	m.h.magic = N2VCD_MAGIC;
	m.h.type = type;
	m.h.seq = seq;
	m.h.session = session;
	m.status = status;
	m.detail = detail;
	send_msg(to, &m, (uint32_t)sizeof(m));
}

static void
send_error(uint32_t to, uint32_t seq, uint32_t session, uint32_t status, uint32_t detail)
{
	reply_simple(to, seq, session, N2VCD_ERROR, status, detail);
}

/* Clients that have sent a datagram; used for the fatal guest-stop broadcast.  The socket transport could enumerate
 * its connections, but the relay cannot, and this list covers every client that has identified itself by traffic. */
static void
note_client(uint32_t id)
{
	if (id == 0u) { return; }
	for (unsigned i = 0; i < g.nseen; i++) {
		if (g.seen[i] == id) { return; }
	}
	if (g.nseen < 16u) { g.seen[g.nseen++] = id; }
}

static void
fatal_send(uint32_t to, uint32_t seq, uint32_t session, uint32_t code)
{
	struct n2vcd_simple m;
	memset(&m, 0, sizeof(m));
	m.h.magic = N2VCD_MAGIC;
	m.h.type = N2VCD_ERROR;
	m.h.seq = seq;
	m.h.session = session;
	m.status = N2VCD_E_GUEST;
	m.detail = code;
	(void)n2vcd_srv_send(g.srv, to, &m, (uint32_t)sizeof(m));
}

/* n2vhost_stop_text() always prints the stop address as 0x%08x; the last such number is the stop address (the UNDEF
 * text names the failing instruction first and the last stop second).  0 when the text carries none. */
static uint32_t
stop_address_from_text(const char *text)
{
	uint32_t last = 0;
	if (text == NULL) { return 0; }
	for (const char *p = text; (p = strstr(p, "0x")) != NULL; p += 2) {
		char *end = NULL;
		unsigned long v = strtoul(p + 2, &end, 16);
		if (end != p + 2 && v <= 0xfffffffful) { last = (uint32_t)v; }
	}
	return last;
}

/* The lead's rule: a guest stop is fatal to the process.  The interpreter's fault recovery can leave host shim
 * state half-updated, and IV refuses a second hosted set in one process, so the only safe recovery is launchd
 * starting a fresh daemon.  Answer every known client, drop the VENC power references (bounded, raw: do not enter
 * the AP close path against a dead guest), log one line, then exit 70. */
static void __attribute__((noreturn))
fatal_guest_stop(uint32_t owner, uint32_t seq, uint32_t session)
{
	uint32_t code = guest_stop_code();
	const char *text = g.vh ? n2vhost_stop_text(g.vh) : "guest stopped";
	char why[200];
	(void)snprintf(why, sizeof(why), "%s", text ? text : "?");
	if (g.srv) {
		fatal_send(owner, seq, session, code);
		for (unsigned i = 0; i < g.nseen; i++) {
			if (g.seen[i] != owner) { fatal_send(g.seen[i], 0, session, code); }
		}
	}
	/* One line with the loader's own description of the stop address (soname:symbol+0x..., hostcall:name, or hex). */
	uint32_t addr = stop_address_from_text(why);
	char desc[192];
	desc[0] = '\0';
	struct n2elf_set *elf = g.vh ? n2vhost_elf(g.vh) : NULL;
	if (elf != NULL && addr != 0u) { (void)n2elf_describe(elf, addr, desc, sizeof(desc)); }
	n2vcd_log("fatal guest stop session=%u code=%u addr=0x%08x (%s) reason=%s", session, code, addr,
	          desc[0] ? desc : "no image describes the address", why);
	g.s.active = 0;
	g.s.enc = NULL;                     /* the guest is dead; n2ap_venc_close would only wait for it */
	/* Do not touch V41's DMA registry here: a fault taken inside a host shim can leave that mutex held. */
	if (g.hw.power) {
		const struct n2vc_hw *phw = g.vhw ? g.vhw : &g.hw;
		static const unsigned blocks[] = { RWVC_BLK_VENC_C1, RWVC_BLK_VENC };
		for (unsigned b = 0; b < sizeof(blocks) / sizeof(blocks[0]); b++) {
			for (unsigned i = 0; i < 16u; i++) {
				if (phw->power(phw->ctx, blocks[b], 0) != 0) { break; }
			}
		}
		if (g.decoder) {
			/* opus-decint: the decoder's LOCK references (n2ap_vdec power role); CORE before its SOC parent */
			static const unsigned dblocks[] = { RWVC_BLK_VDEC_CORE, RWVC_BLK_VDEC_SOC };
			for (unsigned b = 0; b < sizeof(dblocks) / sizeof(dblocks[0]); b++) {
				for (unsigned i = 0; i < 16u; i++) {
					if (phw->power(phw->ctx, dblocks[b], 0) != 0) { break; }
				}
			}
		}
	}
	_exit(70);
}

/* IVs milestone evidence can be long; keep the line bounded for the console path. */
static void
log_milestone(void)
{
	if (!g.vh) { return; }
	unsigned ms = n2vhost_milestone(g.vh);
	if (ms == g.last_milestone || ms == N2VHOST_MS_NONE) { return; }
	g.last_milestone = ms;
	const char *ev = n2vhost_milestone_evidence(g.vh, ms);
	char buf[220];
	(void)snprintf(buf, sizeof(buf), "%s", ev ? ev : "");
	n2vcd_log("milestone %s %s", n2vhost_milestone_name(ms), buf);
}

/* ---- parameter sets ------------------------------------------------------------------------------------------- */

static int
start_code_len(const uint8_t *p, uint32_t n, uint32_t off)
{
	if (off + 3u > n) { return 0; }
	if (p[off] == 0 && p[off + 1] == 0 && p[off + 2] == 1) { return 3; }
	if (off + 4u <= n && p[off] == 0 && p[off + 1] == 0 && p[off + 2] == 0 && p[off + 3] == 1) { return 4; }
	return 0;
}

/* Length of the leading SPS/PPS(/VPS) NAL units at offset 0, or 0 if the access unit does not start with parameter sets.
 * Only the first `n` bytes are examined; a unit whose parameter sets do not fit gives 0 (report no header bytes rather
 * than guessing). */
static uint32_t
leading_params(const uint8_t *p, uint32_t n, uint32_t codec)
{
	if (n < 5) { return 0; }
	uint32_t off = 0;
	int seen = 0;
	while (off + 4u <= n) {
		int sc = start_code_len(p, n, off);
		if (!sc) { return 0; }                           /* garbage or a code split at the window edge */
		uint32_t nal = off + (uint32_t)sc;
		if (nal + 1u >= n) { return 0; }
		unsigned type;
		int is_param;
		if (codec == N2VCD_CODEC_H264) {
			type = p[nal] & 0x1fu;
			is_param = (type == 7u || type == 8u);
		} else {
			type = (p[nal] >> 1) & 0x3fu;
			is_param = (type == 32u || type == 33u || type == 34u);
		}
		if (!is_param) { return seen ? off : 0; }
		seen = 1;
		uint32_t next = nal + 1u;
		while (next + 3u <= n && start_code_len(p, n, next) == 0) { next++; }
		if (next + 3u > n) { return 0; }                 /* cannot prove the end: do not cache a partial unit */
		off = next;
	}
	return 0;
}

static int
shift_up(struct session *s, uint32_t handle, uint32_t bytes, uint32_t by)
{
	if (by == 0 || bytes == 0) { return 0; }
	if (!s->bounce) {
		s->bounce_cap = 64u * 1024u;
		s->bounce = (uint8_t *)malloc(s->bounce_cap);
		if (!s->bounce) { s->bounce_cap = 0; return -ENOMEM; }
	}
	uint32_t pos = bytes;
	while (pos > 0) {
		uint32_t chunk = pos > s->bounce_cap ? s->bounce_cap : pos;
		uint32_t from = pos - chunk;
		if (g.hw.dma_rw(g.hw.ctx, handle, from, s->bounce, chunk, 0) != 0) { return -EIO; }
		if (g.hw.dma_rw(g.hw.ctx, handle, from + by, s->bounce, chunk, 1) != 0) { return -EIO; }
		pos = from;
	}
	return 0;
}

/* ---- profile/level mapping: the protocol carries H.264 profile_idc/level_idc, V41 wants the V4L2 enum values ---- */

static uint32_t
map_profile(uint32_t codec, uint32_t idc)
{
	if (idc == 0) { return 0; }
	if (codec == N2VCD_CODEC_H264) {
		/* profile_idc -> enum v4l2_mpeg_video_h264_profile (v4l2-controls.h:569-587). */
		switch (idc) {
		case 66: return 0;              /* BASELINE */
		case 77: return 2;              /* MAIN */
		case 88: return 3;              /* EXTENDED */
		case 100: return 4;             /* HIGH */
		case 110: return 5;             /* HIGH_10 */
		case 122: return 6;             /* HIGH_422 */
		case 244: return 7;             /* HIGH_444_PREDICTIVE */
		default: return UINT32_MAX;     /* unknown profile is rejected, not silently changed */
		}
	}
	/* enum v4l2_mpeg_video_hevc_profile (v4l2-controls.h:754-758). */
	switch (idc) {                      /* HEVC profile_idc: 1 Main, 2 Main10, 3 Main Still Picture */
	case 1: return 0;                  /* MAIN */
	case 2: return 2;                  /* MAIN_10 */
	case 3: return 1;                  /* MAIN_STILL_PICTURE */
	default: return UINT32_MAX;
	}
}

static uint32_t
map_level(uint32_t codec, uint32_t idc)
{
	if (idc == 0) { return 0; }
	if (codec == N2VCD_CODEC_H264) {
		/* level_idc -> enum v4l2_mpeg_video_h264_level (v4l2-controls.h:538-559). */
		switch (idc) {
		case 9: return 1;              /* 1b */
		case 10: return 0; case 11: return 2; case 12: return 3; case 13: return 4;
		case 20: return 5; case 21: return 6; case 22: return 7;
		case 30: return 8; case 31: return 9; case 32: return 10;
		case 40: return 11; case 41: return 12; case 42: return 13;
		case 50: return 14; case 51: return 15; case 52: return 16;
		case 60: return 17; case 61: return 18; case 62: return 19;
		default: return UINT32_MAX;
		}
	}
	/* level_idc -> enum v4l2_mpeg_video_hevc_level (v4l2-controls.h:760-774); level_idc = 30 * level
	 * for the integer levels and 30 * level + 3 for the .1 ones. */
	switch (idc) {
	case 30: return 0;  case 60: return 1;  case 63: return 2;
	case 90: return 3;  case 93: return 4;
	case 120: return 5; case 123: return 6;
	case 150: return 7; case 153: return 8; case 156: return 9;
	case 180: return 10; case 183: return 11; case 186: return 12;
	default: return UINT32_MAX;
	}
}

/* ---- automatic level for OPEN.level == 0 (opus-enc4k, 11 Nov) ----
 * The vendor library writes the requested level into the SPS verbatim (vc14: level_idc 10 at 640x480; host 4K run: 10), and
 * VideoToolbox/Camera clients send 0, so "0" must not mean the V4L2 default (H.264 enum 0 = level 1.0).  The smallest level
 * of the ladder below whose limits hold for the picture size, the macroblock/luma-sample rate (fps_num/fps_den, exact) and the
 * bit rate is chosen.  H.264: Table A-1 MaxFS / MaxMBPS / MaxBR (cpbBrVclFactor 1000, 1250 for the High profiles), floor 3.1.
 * HEVC Main tier: Table A.8 MaxLumaPs / MaxLumaSr / MaxBR (1000 per kbit/s), floor 4.0 (the kernel default this daemon used
 * so far; every host/phone HEVC session ran at 4.0).  Returns level_idc, mapped by map_level() like a client's value. */
static uint32_t
auto_level_idc(uint32_t codec, uint32_t w, uint32_t h, uint32_t fps_num, uint32_t fps_den, uint32_t bitrate, uint32_t profile_idc)
{
	if (fps_den == 0u) { fps_den = 1u; }
	if (codec == N2VCD_CODEC_H264) {
		static const struct { uint32_t idc, max_fs, max_mbps, max_br_kbps; } t[] = {
			{ 31, 3600, 108000, 14000 }, { 40, 8192, 245760, 20000 }, { 41, 8192, 245760, 50000 },
			{ 42, 8704, 522240, 50000 }, { 50, 22080, 589824, 135000 }, { 51, 36864, 983040, 240000 },
			};
		uint64_t mbs = (uint64_t)((w + 15u) / 16u) * ((h + 15u) / 16u);
		uint64_t factor = (profile_idc == 100u || profile_idc == 110u || profile_idc == 122u || profile_idc == 244u) ? 1250u : 1000u;
		for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
			if (mbs <= t[i].max_fs && mbs * fps_num <= (uint64_t)t[i].max_mbps * fps_den &&
			    (uint64_t)bitrate <= (uint64_t)t[i].max_br_kbps * factor) {
				return t[i].idc;
			}
		}
		return 51;                  /* the vendor's max H.264 level is 5.1 (QUERY_CAP level enum 15) */
	}
	static const struct { uint32_t idc, max_ps, max_sr, max_br_kbps; } t[] = {
		{ 120, 2228224, 66846720, 12000 }, { 123, 2228224, 133693440, 20000 }, { 150, 8912896, 267386880, 25000 },
		{ 153, 8912896, 534773760, 40000 },
	};
	uint64_t ps = (uint64_t)w * h;
	for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		if (ps <= t[i].max_ps && ps * fps_num <= (uint64_t)t[i].max_sr * fps_den && (uint64_t)bitrate <= (uint64_t)t[i].max_br_kbps * 1000u) {
			return t[i].idc;
		}
	}
	return 153;                     /* the vendor's max HEVC level is 5.1 (QUERY_CAP level enum 8) */
}

/* A complete sequence-header blob as the separate N2AP_ENC_SEQ_HDR job writes it: Annex B, starts with a start code and
 * contains at least one parameter-set NAL.  Unlike leading_params() this does not need a following non-parameter NAL:
 * the job's whole output *is* the header.  SEI NALs are accepted around the sets. */
static int
header_blob_ok(const uint8_t *p, uint32_t n, uint32_t codec)
{
	if (n < 4u || start_code_len(p, n, 0u) == 0) { return 0; }
	int seen = 0;
	uint32_t off = 0;
	while (off + 3u <= n) {
		int sc = start_code_len(p, n, off);
		if (!sc) { off++; continue; }             /* payload byte; the next start code ends the NAL */
		uint32_t nal = off + (uint32_t)sc;
		if (nal + 1u >= n) { break; }
		unsigned type = codec == N2VCD_CODEC_H264 ? (p[nal] & 0x1fu) : ((p[nal] >> 1) & 0x3fu);
		if (codec == N2VCD_CODEC_H264) {
			if (type == 7u || type == 8u) { seen = 1; }
		} else if (type == 32u || type == 33u || type == 34u) {
			seen = 1;
		}
		off = nal + 1u;
	}
	return seen;
}

/* One separate sequence-header job after VENC_SET_PARAM_ENC: the stock V4L2 separate-header mode
 * (n2ap_enc_param_1080p30_h264 starts with seq_hdr_mode = 0; mtk_vcodec_enc.c:2598-2615).  The bytes are cached and
 * prepended by the daemon before every IDR, so nothing depends on the vendor's CPU-side joined-header memmove. */
static int
read_sequence_header(struct n2ap_venc *enc, uint32_t codec, uint8_t *out, uint32_t cap, uint32_t *out_len)
{
	const struct n2vc_hw *hw = g.vhw ? g.vhw : &g.hw;
	uint32_t handle = 0, pa = 0, guest = 0, bytes = 256u * 1024u;
	*out_len = 0;
	if (hw->dma_alloc == NULL || hw->dma_free == NULL || hw->dma_rw == NULL) { return -ENOSYS; }
	int r;
	if (g.mem != NULL) {
		/* Allocate through V41 so the handle is also in the AP's DMA registry (n2ap_venc_encode() needs its
		 * pa); this is what the host runner and the phone daemon have in common. */
		r = n2vc_dma_guest_alloc(hw, g.mem, bytes, 0, &handle, &pa, &guest);
	} else {
		r = hw->dma_alloc(hw->ctx, bytes, 0, &handle, &pa);
	}
	if (r != 0) { return r; }
	struct n2ap_enc_result res;
	memset(&res, 0, sizeof(res));
	r = n2ap_venc_encode(enc, 0, handle, N2AP_ENC_SEQ_HDR, &res);
	if (r == 0 && res.bytes == 0u) {
		/* Some daemons answer the header job asynchronously like a frame. */
		r = n2ap_venc_wait_done(enc, (unsigned)g.hw_timeout_ms, &res);
	}
	if (r == 0 && (res.bytes == 0u || res.bytes > bytes || res.bytes > cap)) { r = -EIO; }
	if (r == 0) {
		uint32_t want = res.bytes;
		r = hw->dma_rw(hw->ctx, handle, 0, out, want, 0);
		if (r == 0 && !header_blob_ok(out, want, codec)) { r = -EIO; }
		if (r == 0) { *out_len = want; }
	}
	(void)hw->dma_free;
	if (guest != 0u) {
		(void)n2vc_dma_guest_forget(hw, handle);      /* frees the DMA handle it allocated */
	} else {
		(void)hw->dma_free(hw->ctx, handle);
	}
	return r;
}

/* Test builds with --fake-venc: teach the behavioural VENC the session's real parameter sets, so the synthetic
 * slices it writes match the headers the library produced (otherwise it falls back to Baseline defaults). */
static void
fake_venc_set_headers(const uint8_t *hdr, uint32_t bytes)
{
#if defined(N2IP_TEST_HOOKS) && defined(N2IP_FAKE_VENC)
	if (g_fv != NULL && hdr != NULL && bytes != 0u) { (void)n2venc_fake_set_headers(g_fv, hdr, bytes); }
#else
	(void)hdr;
	(void)bytes;
#endif
}

/* ---- requests ------------------------------------------------------------------------------------------------- */

static void
handle_hello(uint32_t from, const struct n2vcd_hello *req, uint32_t bytes)
{
	if (bytes != (uint32_t)sizeof(*req)) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_ARGS, (uint32_t)sizeof(*req));
		return;
	}
	if (req->version != N2VCD_VERSION) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_VERSION, req->version);
		return;
	}
	struct n2vcd_hello_ok ok;
	memset(&ok, 0, sizeof(ok));
	ok.h.magic = N2VCD_MAGIC;
	ok.h.type = N2VCD_HELLO_OK;
	ok.h.seq = req->h.seq;
	ok.h.session = 0;
	ok.version = N2VCD_VERSION;
	ok.enc_codecs = (1u << 0) | (1u << 1);      /* H.264 + HEVC; OPEN reports a codec the AP side cannot run */
	ok.dec_codecs = g.decoder ? N2VCD_DEC_CODECS_ALL : 0u;   /* --decoder: every format of daemon/dec (opus-fmt) */
	ok.max_width = N2VCD_MAX_WIDTH;
	ok.max_height = N2VCD_MAX_HEIGHT;
	send_msg(from, &ok, (uint32_t)sizeof(ok));
}

static uint32_t
map_error(int err)
{
	switch (err) {
	case -EOPNOTSUPP: return N2VCD_E_UNSUPPORTED;
	case -ENOMEM: return N2VCD_E_NOMEM;
	case -EINVAL: return N2VCD_E_ARGS;
	case -EBUSY: return N2VCD_E_BUSY;
	case -ETIMEDOUT: return N2VCD_E_HW;
	default: return N2VCD_E_HW;
	}
}

/* V41 semantics: -ETIMEDOUT aborts the session; -EIO is a hardware failure (power/wait_irq).  -EPROTO is
 * a wrong reply and leaves the session usable, so it must NOT tear the session down. */
static int
err_fatal(int r)
{
	return r == -ETIMEDOUT || r == -EIO;
}

static void
handle_open(uint32_t from, const struct n2vcd_open *req, uint32_t bytes)
{
	if (bytes != (uint32_t)sizeof(*req)) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_ARGS, (uint32_t)sizeof(*req));
		return;
	}
	if (g.s.active) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_BUSY, g.s.id);
		return;
	}
	if (req->encoder == 1u && decoder_busy()) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_BUSY, 0u);   /* a decoder session is open */
		return;
	}
	if (req->encoder != 1u || (req->codec != N2VCD_CODEC_H264 && req->codec != N2VCD_CODEC_HEVC)) {
		send_error(from, req->h.seq, 0, N2VCD_E_UNSUPPORTED, req->codec);
		return;
	}
	if (req->flags != 0u) {
		/* OPEN.flags bit 0 (low latency) has no VENC mapping in v1; do not accept it silently. */
		send_error(from, req->h.seq, 0, N2VCD_E_UNSUPPORTED, req->flags);
		return;
	}
	if (req->width < 16u || req->height < 16u || req->width > N2VCD_MAX_WIDTH || req->height > N2VCD_MAX_HEIGHT ||
	    (req->width & 1u) || (req->height & 1u)) {
		send_error(from, req->h.seq, 0, N2VCD_E_ARGS, req->width << 16 | (req->height & 0xffffu));
		return;
	}
	/* Keep the values that are multiplied/configured downstream in a sane range. */
	if (req->fps_num > 1000000u || req->fps_den > 1000000u || req->gop > 100000u || req->bitrate > 400000000u) {
		send_error(from, req->h.seq, 0, N2VCD_E_ARGS, req->gop);
		return;
	}
	if (guest_stopped()) {
		fatal_guest_stop(from, req->h.seq, 0);
	}
	if (!g.vcu || !g.vhw) {
		send_error(from, req->h.seq, 0, N2VCD_E_HW, 0);
		return;
	}
	/* opus-enc4k, phone run vc18: after two H.264 sessions an HEVC OPEN in the same process timed out in its
	 * sequence-header job (-60) and the guest was stopped; HEVC alone (vc17) and H.264 -> H.264 (1080p -> 4K) work.
	 * The vendor state does not survive a codec switch, so with --restart-on-switch (launchd KeepAlive
	 * {SuccessfulExit: false}) the daemon answers BUSY/N2VCD_DETAIL_RESTART, powers the VENC down and exits 75; launchd
	 * starts a fresh process (about 0.3 s to "serving") and the client repeats OPEN (n2vcd_probe does, up to 30 s). */
	/* vc24: an HEVC session after an HEVC session in the same process hung too (hvc1 1080p then hvc1 4K: frame 0's GCE wait
	 * for VENC_EOF timed out, detail 7011 ms), like vc18's HEVC after H.264 (sequence-header job -60); H.264 after H.264
	 * (1080p -> 4K, vc18/vc24) works.  So any session after an HEVC one also gets a fresh process. */
	if (g.restart_on_switch && g.last_codec != 0u && (req->codec != g.last_codec || g.last_codec == N2VCD_CODEC_HEVC)) {
		n2vcd_log("open: %s %s -> %s in one process: answering BUSY/restart and exiting 75 for a fresh vendor state",
		          req->codec != g.last_codec ? "codec switch" : "reopen after", g.last_codec == N2VCD_CODEC_H264 ? "h264" : "hevc",
		          req->codec == N2VCD_CODEC_H264 ? "h264" : "hevc");
		send_error(from, req->h.seq, 0, N2VCD_E_BUSY, N2VCD_DETAIL_RESTART);
		if (g.hw.power) {
			const struct n2vc_hw *phw = g.vhw ? g.vhw : &g.hw;
			static const unsigned blocks[] = { RWVC_BLK_VENC_C1, RWVC_BLK_VENC };
			for (unsigned b = 0; b < sizeof(blocks) / sizeof(blocks[0]); b++) {
				for (unsigned i = 0; i < 16u; i++) {
					if (phw->power(phw->ctx, blocks[b], 0) != 0) { break; }
				}
			}
		}
		_exit(75);
	}

	uint32_t fps_num = req->fps_num ? req->fps_num : 30u;
	uint32_t fps_den = req->fps_den ? req->fps_den : 1u;
	uint32_t fps = fps_num / fps_den;
	if (fps == 0u) { fps = 1u; }
	if (fps > 1000u) {
		send_error(from, req->h.seq, 0, N2VCD_E_ARGS, fps_num);
		return;
	}
	vclk_session_open(req->width, req->height, fps_num, fps_den);   /* before ENC_INIT: the header job powers the VENC too */
	uint32_t gop = req->gop ? req->gop : fps;
	uint32_t bitrate = req->bitrate;
	if (bitrate == 0) {
		uint64_t b = (uint64_t)req->width * req->height * 4u;
		bitrate = b > 1000000u ? (uint32_t)b : 1000000u;
	}
	uint32_t fourcc = req->codec == N2VCD_CODEC_H264 ? N2AP_FOURCC_H264 : N2AP_FOURCC_H265;

	struct n2ap_venc *enc = NULL;
	/* V41 bounds each half of one IPI send by this value, so a send waits at most 2 * timeout.  Passing half
	 * the daemon's hardware limit makes one encode's worst case the configured limit; open/start/close are
	 * several sends but each is bounded, so OPEN cannot hang forever. */
	unsigned ipi_half_ms = (unsigned)((g.hw_timeout_ms + 1) / 2);
	if (ipi_half_ms == 0u) { ipi_half_ms = 1u; }
	/* The vendor daemon runs under the interpreter: its ENC_INIT on the phone (first touch of its code paths, several
	 * /dev/vcu allocations) is far slower than one hardware job, and the host runner already allows 20 s.  With the
	 * default --hw-timeout-ms the IPI bound is therefore raised to N2VCD_OPEN_IPI_MIN_MS per half; an explicit
	 * --hw-timeout-ms is used as given. */
	if (!g.hw_timeout_explicit && ipi_half_ms < N2VCD_OPEN_IPI_MIN_MS) { ipi_half_ms = N2VCD_OPEN_IPI_MIN_MS; }
	uint64_t t_open = now_us();
	n2vcd_log("open: codec=%s %ux%u fps=%u gop=%u bitrate=%u; sending AP_IPIMSG_ENC_INIT (ipi bound %u ms per half), "
	          "guest milestone=%s", req->codec == N2VCD_CODEC_H264 ? "h264" : "hevc", req->width, req->height, fps, gop,
	          bitrate, ipi_half_ms, n2vhost_milestone_name(n2vhost_milestone(g.vh)));
	int r = n2ap_venc_open(&enc, g.vcu, g.vhw, fourcc, ipi_half_ms);
	n2vcd_log("open: ENC_INIT -> %d after %llu ms (guest milestone=%s, guest %s)", r,
	          (unsigned long long)((now_us() - t_open) / 1000u), n2vhost_milestone_name(n2vhost_milestone(g.vh)),
	          guest_stopped() ? "STOPPED" : "running");
	if (r != 0) {
		if (guest_stopped()) {
			fatal_guest_stop(from, req->h.seq, 0);
		}
		n2vcd_log("encoder open failed: %d (%s; -22 = the vendor answered INIT with FAIL, see the hw lines above; "
		          "-60 = no INIT_DONE within the bound; -5 = bad vsi address)", r, strerror(-r));
		send_error(from, req->h.seq, 0, map_error(r), (uint32_t)(-r));
		return;
	}

	struct n2ap_enc_param p;
	/* HEVC keeps the kernel's HEVC control defaults, not the H.264 struct with two fields overwritten: profile 0
	 * (MAIN), level 5 (V4L2_MPEG_VIDEO_HEVC_LEVEL_4, mtk_vcodec_enc.c:2769-2772), tier 0 (MAIN).  A client that sends
	 * level_idc 0 ("let the encoder choose") gets auto_level_idc() (H.264 >= 3.1, HEVC >= 4.0), never the enum's level 1;
	 * profile 0 stays the kernel default (H.264 BASELINE, HEVC MAIN). */
	if (req->codec == N2VCD_CODEC_HEVC) {
		n2ap_enc_param_1080p30_hevc(&p);
	} else {
		n2ap_enc_param_1080p30_h264(&p);
	}
	p.width = req->width;
	p.height = req->height;
	p.buf_width = align16(req->width);
	p.buf_height = align16(req->height);
	p.max_w = req->width;
	p.max_h = req->height;
	p.frm_rate = fps;
	p.bitrate = bitrate;
	p.gop_size = gop;
	p.intra_period = gop;
	p.slbc_ready = 0;                            /* on-chip SRAM ports routed to DRAM; vendor must not use SLBC */
	if (req->profile != 0u) { p.profile = map_profile(req->codec, req->profile); }
	uint32_t level_idc = req->level;
	if (level_idc == 0u) {
		level_idc = auto_level_idc(req->codec, req->width, req->height, fps_num, fps_den, bitrate, req->profile);
		n2vcd_log("open: level 0 -> auto level_idc %u (%ux%u %u/%u fps %u bps)", level_idc, req->width, req->height,
		          fps_num, fps_den, bitrate);
	}
	p.level = map_level(req->codec, level_idc);
	if (p.profile == UINT32_MAX || p.level == UINT32_MAX) {
		(void)n2ap_venc_close(enc);
		send_error(from, req->h.seq, 0, N2VCD_E_ARGS, req->profile << 8 | (req->level & 0xffu));
		return;
	}
	p.seq_hdr_mode = 0;                          /* separate sequence-header job below */
	p.prependheader = 0;                         /* the daemon prepends the cached sets itself */
	p.operationrate = 0;
	n2vcd_log("open: INIT_DONE, vsi mapped; SET_PARAM %ux%u buf %ux%u profile=%u level=%u",
	          p.width, p.height, p.buf_width, p.buf_height, p.profile, p.level);
	r = n2ap_venc_start(enc, &p);
	n2vcd_log("open: start (SET_PARAM) -> %d", r);
	if (r != 0) {
		if (guest_stopped()) {
			fatal_guest_stop(from, req->h.seq, 0);
		}
		n2vcd_log("encoder start failed: %d", r);
		(void)n2ap_venc_close(enc);
		send_error(from, req->h.seq, 0, map_error(r), (uint32_t)(-r));
		return;
	}
	uint8_t hdr_bytes[N2VCD_HDR_MAX];
	uint32_t hdr_len = 0;
	r = read_sequence_header(enc, req->codec, hdr_bytes, sizeof(hdr_bytes), &hdr_len);
	n2vcd_log("open: sequence header -> %d (%u bytes)", r, hdr_len);
	if (r != 0) {
		if (guest_stopped()) {
			fatal_guest_stop(from, req->h.seq, 0);
		}
		n2vcd_log("sequence-header job failed: %d", r);
		(void)n2ap_venc_close(enc);
		send_error(from, req->h.seq, 0, map_error(r), (uint32_t)(-r));
		return;
	}
	fake_venc_set_headers(hdr_bytes, hdr_len);

	uint32_t id = ++g.next_session;
	if (id == N2VCD_SESSION_NONE) { id = ++g.next_session; }
	memset(&g.s, 0, sizeof(g.s));
	g.s.active = 1;
	g.last_codec = req->codec;
	g.s.id = id;
	g.s.client = from;
	g.s.codec = req->codec;
	g.s.fourcc = fourcc;
	g.s.width = req->width;
	g.s.height = req->height;
	g.s.fps_num = fps_num;
	g.s.fps_den = fps_den;
	g.s.gop = gop;
	g.s.bitrate = bitrate;
	g.s.profile = req->profile;
	g.s.level = req->level;
	g.s.flags = req->flags;
	g.s.enc = enc;
	g.s.opened_us = now_us();
	g.s.last_activity_us = g.s.opened_us;
	if (hdr_len > 0u) {
		memcpy(g.s.hdr, hdr_bytes, hdr_len);
		g.s.hdr_len = hdr_len;
	}
	g.s.in_bytes = align16(req->width) * align16(req->height) * 3u / 2u;
	uint64_t suggested64 = (uint64_t)req->width * req->height * 3u / 2u;
	uint32_t suggested = suggested64 > (16u << 20) ? (16u << 20) : (uint32_t)suggested64;
	if (suggested < (1u << 20)) { suggested = 1u << 20; }
	g.s.out_bytes = suggested + N2VCD_HDR_RESERVE;

	struct n2vcd_open_ok ok;
	memset(&ok, 0, sizeof(ok));
	ok.h.magic = N2VCD_MAGIC;
	ok.h.type = N2VCD_OPEN_OK;
	ok.h.seq = req->h.seq;
	ok.h.session = id;
	ok.status = N2VCD_E_OK;
	ok.in_buf_bytes = g.s.in_bytes;
	ok.out_buf_bytes = g.s.out_bytes;
	n2vcd_log("session %u open client=%u %s %ux%u@%u/%u %ubps gop=%u",
	          id, from, req->codec == N2VCD_CODEC_H264 ? "avc1" : "hvc1",
	          req->width, req->height, fps_num, fps_den, bitrate, gop);
	n2vcd_log("session %u sequence header %u bytes (separate job)", id, hdr_len);
	if (send_msg(from, &ok, (uint32_t)sizeof(ok)) != 0) {
		n2vcd_log("session %u: OPEN_OK was not delivered; tearing the session down", id);
		session_end(N2VCD_E_SESSION, 0, "OPEN_OK not delivered");
	}
	log_milestone();
}

/* V41 needs pa + bytes for each DMA handle in its n2vc_dma registry.  The handle arrives in N2VCD_ENC_FRAME; the pa is
 * known to the kernel (it allocated the client buffer), so the daemon asks the relay with the proposed RWVC_DMA_INFO and
 * registers a pa-only entry.  Until that ioctl exists (header macro absent / kernel returns ENOSYS) this only reports the
 * gap once per session; the AP side then refuses the handle with -EINVAL and vcodecd answers N2VCD_E_ARGS. */
static int
session_find_handle(uint32_t handle)
{
	for (unsigned i = 0; i < g.s.nreg; i++) {
		if (g.s.reg_handle[i] == handle) { return (int)i; }
	}
	return -1;
}

static void
session_drop_handle(unsigned idx)
{
	const struct n2vc_hw *vhw = g.vhw ? g.vhw : &g.hw;
	if (idx < g.s.nreg) {
		if (vhw) { (void)n2vc_dma_guest_forget(vhw, g.s.reg_handle[idx]); }
		g.s.reg_handle[idx] = g.s.reg_handle[g.s.nreg - 1];
		g.s.reg_pa[idx] = g.s.reg_pa[g.s.nreg - 1];
		g.s.reg_use[idx] = g.s.reg_use[g.s.nreg - 1];
		g.s.nreg--;
	}
}

static void
register_dma_handle(uint32_t handle, uint32_t bytes)
{
	uint32_t pa = 0, guest = 0, len = 0;
	const struct n2vc_hw *vhw = g.vhw ? g.vhw : &g.hw;
	int idx = session_find_handle(handle);
	int q = -ENOSYS;

#ifdef N2IP_TEST_HOOKS
	if (g.dma_dir[0]) {
		/* Host oracle only: --dma-dir makes the test's client handles files.  Give each a stable synthetic
		 * pa so V41/the fake VENC can resolve it; the file-backed dma_rw above performs the transfer. */
		char path[320];
		struct stat st;
		int pn = snprintf(path, sizeof(path), "%s/%u", g.dma_dir, handle);
		if (pn > 0 && (size_t)pn < sizeof(path) && stat(path, &st) == 0 &&
		    st.st_size > 0 && (uintmax_t)st.st_size <= 0xffffffffu) {
			pa = 0x60000000u + handle * 0x200000u;
			len = (uint32_t)st.st_size;
			q = 0;
		}
	}
#endif
	if (q != 0 && !g.dma_info_broken) {
		q = n2vcd_srv_dma_info(g.srv, handle, &pa, &len);
		if (q == -ENOSYS || q == -ENOTTY) { g.dma_info_broken = 1; }
	}
	if (q == 0 && pa != 0u && len != 0u) {
		if (idx >= 0 && g.s.reg_pa[idx] == pa) {
			g.s.reg_use[idx] = ++g.s.use_ctr;
			return;
		}
		if (idx >= 0) { session_drop_handle((unsigned)idx); }
		int r = n2vc_dma_guest_register(vhw, NULL, handle, pa, len, NULL);
		if (r == -EEXIST) {
			(void)n2vc_dma_guest_forget(vhw, handle);
			r = n2vc_dma_guest_register(vhw, NULL, handle, pa, len, NULL);
		}
		if (r == 0) {
			if (g.s.nreg >= 16u) {
				unsigned oldest = 0;
				for (unsigned i = 1; i < g.s.nreg; i++) {
					if (g.s.reg_use[i] < g.s.reg_use[oldest]) { oldest = i; }
				}
				session_drop_handle(oldest);
			}
			g.s.reg_handle[g.s.nreg] = handle;
			g.s.reg_pa[g.s.nreg] = pa;
			g.s.reg_use[g.s.nreg] = ++g.s.use_ctr;
			g.s.nreg++;
			return;
		}
		if (!g.s.dma_reg_warned) {
			g.s.dma_reg_warned = 1;
			n2vcd_log("session %u: register DMA handle %u pa=0x%x bytes=%u failed: %d",
			          g.s.id, handle, pa, len, r);
		}
	} else if (idx >= 0) {
		session_drop_handle((unsigned)idx);   /* the ioctl no longer knows it: stale entry */
	}

	/* A buffer that some in-process allocator already registered is usable and not ours to forget. */
	if (n2vc_dma_lookup_handle(handle, &pa, &guest, &len) == 0 && pa != 0u) { return; }

	if (!g.s.dma_warned) {
		g.s.dma_warned = 1;
		n2vcd_log("session %u: DMA handle %u (%u bytes) has no PA in the AP registry; "
		          "n2ap_venc_encode needs RWVC_DMA_INFO (board: IP -> lead)", g.s.id, handle, bytes);
	}
}

static void
session_forget_handles(void)
{
	while (g.s.nreg > 0) {
		session_drop_handle(g.s.nreg - 1u);
	}
}

static void
handle_enc_frame(uint32_t from, const struct n2vcd_enc_frame *req, uint32_t bytes)
{
	if (bytes != (uint32_t)sizeof(*req)) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_ARGS, (uint32_t)sizeof(*req));
		return;
	}
	if (!session_owned(req->h.session, from)) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_SESSION, g.s.id);
		return;
	}
	g.s.last_activity_us = now_us();
	uint32_t want_stride = align16(g.s.width);
	if (req->in_handle == 0 || req->out_handle == 0 || req->out_capacity < 4096u ||
	    req->out_capacity < g.s.out_bytes || req->in_handle == req->out_handle || req->stride != want_stride) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_ARGS, req->stride);
		return;
	}
	uint32_t enc_cap;
	register_dma_handle(req->in_handle, g.s.in_bytes);
	register_dma_handle(req->out_handle, req->out_capacity);

	uint32_t in_actual = 0, out_actual = 0, dummy = 0;
	if (n2vc_dma_lookup_handle(req->in_handle, &dummy, &dummy, &in_actual) == 0 &&
	    in_actual != 0u && in_actual < g.s.in_bytes) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_ARGS, in_actual);
		return;
	}
	uint32_t out_limit = req->out_capacity;
	if (n2vc_dma_lookup_handle(req->out_handle, &dummy, &dummy, &out_actual) == 0 &&
	    out_actual != 0u && out_actual < out_limit) {
		out_limit = out_actual;
	}
	if (out_actual != 0u && out_actual < g.s.out_bytes) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_ARGS, out_actual);
		return;
	}
	enc_cap = out_limit;
	if (enc_cap > N2VCD_HDR_RESERVE) { enc_cap -= N2VCD_HDR_RESERVE; }

	if (req->force_key) {
		/* Per-frame request: VENC_SET_PARAM_FORCE_INTRA before this picture, exactly as the V4L2 control does. */
		int fk = n2ap_venc_set_param(g.s.enc, VENC_SET_PARAM_FORCE_INTRA, NULL);
		if (fk != 0) {
			if (guest_stopped()) {
				fatal_guest_stop(from, req->h.seq, req->h.session);
			}
			n2vcd_log("session %u force-key failed: %d", g.s.id, fk);
			uint32_t st = map_error(fk);
			send_error(from, req->h.seq, req->h.session, st, (uint32_t)(-fk));
			if (err_fatal(fk)) { session_end(st, (uint32_t)(-fk), "force-key failed"); }
			return;
		}
	}

	struct n2ap_enc_result res;
	memset(&res, 0, sizeof(res));
	uint64_t ph0[7] = { 0 }, ph1[7] = { 0 };
	if (n2vcu_venc_phases && g.vcu) { n2vcu_venc_phases(g.vcu, ph0); }
	uint64_t t0 = now_us();
	int r = n2ap_venc_encode(g.s.enc, req->in_handle, req->out_handle, N2AP_ENC_FRAME, &res);
	if (r == 0 && res.bytes == 0u) {
		/* On the real GCE path the ENCODE is ACKed before the job runs; size/key arrive later in list_free
		 * (VCU_IPIMSG_ENC_PUT_BUFFER).  n2ap_venc_wait_done() polls that and syncs the bitstream from device. */
		r = n2ap_venc_wait_done(g.s.enc, (unsigned)g.hw_timeout_ms, &res);
	}
	uint64_t elapsed = now_us() - t0;

	if (r == 0 && elapsed > (uint64_t)g.hw_timeout_ms * 1000u) {
		/* The AP's own bound (2 * ipi_half_ms) is the authoritative limit; a success is never discarded. */
		n2vcd_log("session %u slow encode: %llu ms (limit %d)", g.s.id,
		          (unsigned long long)(elapsed / 1000u), g.hw_timeout_ms);
	}
	if (r != 0) {
		if (guest_stopped()) {
			fatal_guest_stop(from, req->h.seq, req->h.session);
		}
		uint32_t st = map_error(r);
		uint32_t detail = (uint32_t)(-r);
		if (r == -ETIMEDOUT && elapsed > 0u) { detail = (uint32_t)(elapsed / 1000u); }
		if (r == -ETIMEDOUT) {
			n2vcd_log("session %u hardware timeout: encode waited %llu ms (limit %d)",
			          g.s.id, (unsigned long long)(elapsed / 1000u), g.hw_timeout_ms);
		} else {
			n2vcd_log("session %u encode failed: %d after %llu ms", g.s.id, r,
			          (unsigned long long)(elapsed / 1000u));
		}
		send_error(from, req->h.seq, req->h.session, st, detail);
		if (err_fatal(r)) { session_end(st, detail, "encode failed"); }
		return;
	}
	if (res.bytes == 0 || res.bytes > enc_cap) {
		n2vcd_log("session %u encoder returned %u bytes (capacity %u)", g.s.id, res.bytes, enc_cap);
		send_error(from, req->h.seq, req->h.session, N2VCD_E_HW, res.bytes);
		session_end(N2VCD_E_HW, res.bytes, "bad encode size");
		return;
	}

	uint32_t out_bytes = res.bytes;
	uint32_t header_bytes = 0;
	if (res.is_key_frm) {
		uint8_t head[N2VCD_HDR_MAX];
		uint32_t want = out_bytes < sizeof(head) ? out_bytes : (uint32_t)sizeof(head);
		uint32_t len = 0;
		if (g.hw.dma_rw(g.hw.ctx, req->out_handle, 0, head, want, 0) == 0) {
			len = leading_params(head, want, g.s.codec);
		}
		if (len > 0 && len <= sizeof(g.s.hdr)) {
			/* The encoder already put the parameter sets at offset 0 (PREPEND_SPSPPS_TO_IDR): report and cache. */
			memcpy(g.s.hdr, head, len);
			g.s.hdr_len = len;
			header_bytes = len;
		} else if (g.s.hdr_len > 0 && out_bytes + g.s.hdr_len <= out_limit) {
			if (shift_up(&g.s, req->out_handle, out_bytes, g.s.hdr_len) == 0 &&
			    g.hw.dma_rw(g.hw.ctx, req->out_handle, 0, g.s.hdr, g.s.hdr_len, 1) == 0) {
				out_bytes += g.s.hdr_len;
				header_bytes = g.s.hdr_len;
			} else {
				n2vcd_log("session %u prepend of parameter sets failed", g.s.id);
				send_error(from, req->h.seq, req->h.session, N2VCD_E_HW, 0);
				session_end(N2VCD_E_HW, 0, "parameter-set prepend failed");
				return;
			}
		}
	}

	g.s.frames++;
	g.s.out_total += out_bytes;
	g.s.hw_us_total += (uint32_t)elapsed;
	g.s.last_activity_us = now_us();

	struct n2vcd_enc_frame_ok ok;
	memset(&ok, 0, sizeof(ok));
	ok.h.magic = N2VCD_MAGIC;
	ok.h.type = N2VCD_ENC_FRAME_OK;
	ok.h.seq = req->h.seq;
	ok.h.session = g.s.id;
	ok.status = N2VCD_E_OK;
	ok.out_bytes = out_bytes;
	ok.key_frame = res.is_key_frm ? 1u : 0u;
	ok.hw_us = (uint32_t)elapsed;          /* V41's IPI result has no timing: wall time of the encode call */
	ok.pts = req->pts;
	ok.header_bytes = header_bytes;
	if ((g.s.frames % 30u) == 1u || res.is_key_frm) {
		n2vcd_log("session %u frame %llu key=%u bytes=%u hw_us=%u",
		          g.s.id, (unsigned long long)g.s.frames, res.is_key_frm ? 1u : 0u, out_bytes, ok.hw_us);
	}
	/* opus-enc4k: where the encode call's wall time goes (frames 1-3, then every 10th): power-on ioctl(s), the GCE list
	 * run (register batches + interrupt waits), the waits alone, power-off, and the rest = IPI + interpreted vendor code
	 * + wait_done polling.  'lists c0/c1' says which VENC core(s) ran this frame. */
	if (n2vcu_venc_phases && g.vcu && (g.s.frames <= 3u || g.s.frames % 10u == 0u)) {
		n2vcu_venc_phases(g.vcu, ph1);
		uint64_t on = ph1[0] - ph0[0], off = ph1[1] - ph0[1], run = ph1[3] - ph0[3], wait = ph1[4] - ph0[4];
		uint64_t other = elapsed > on + off + run ? elapsed - on - off - run : 0;
		n2vcd_log("session %u frame %llu phases us: total %llu = power_on %llu + list %llu (irq wait %llu, regs %llu) + "
		          "power_off %llu + other %llu; power ops %llu, lists c0 %llu c1 %llu%s", g.s.id, (unsigned long long)g.s.frames,
		          (unsigned long long)elapsed, (unsigned long long)on, (unsigned long long)run, (unsigned long long)wait,
		          (unsigned long long)(run > wait ? run - wait : 0), (unsigned long long)off, (unsigned long long)other,
		          (unsigned long long)(ph1[2] - ph0[2]), (unsigned long long)(ph1[5] - ph0[5]),
		          (unsigned long long)(ph1[6] - ph0[6]), g.venc_keep_power ? " (keep-power)" : "");
	}
	send_msg(from, &ok, (uint32_t)sizeof(ok));
	log_milestone();
}

static void
handle_enc_set(uint32_t from, const struct n2vcd_enc_set *req, uint32_t bytes)
{
	if (bytes != (uint32_t)sizeof(*req)) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_ARGS, (uint32_t)sizeof(*req));
		return;
	}
	if (!session_owned(req->h.session, from)) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_SESSION, g.s.id);
		return;
	}
	g.s.last_activity_us = now_us();
	struct n2ap_enc_param p;
	memset(&p, 0, sizeof(p));
	if (req->bitrate > 400000000u) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_ARGS, req->bitrate);
		return;
	}
	if ((req->fps_num == 0) != (req->fps_den == 0) || req->fps_num > 1000000u || req->fps_den > 1000000u) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_ARGS, req->fps_num);
		return;
	}
	uint32_t fps = req->fps_num ? req->fps_num / req->fps_den : 0u;
	if (req->fps_num && fps == 0u) { fps = 1u; }
	if (req->fps_num && fps > 1000u) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_ARGS, req->fps_num);
		return;
	}
	int r = 0;
	if (req->bitrate) {
		p.bitrate = req->bitrate;
		r = n2ap_venc_set_param(g.s.enc, VENC_SET_PARAM_ADJUST_BITRATE, &p);
	}
	if (r == 0 && req->fps_num && req->fps_den) {
		memset(&p, 0, sizeof(p));
		p.frm_rate = fps;
		r = n2ap_venc_set_param(g.s.enc, VENC_SET_PARAM_ADJUST_FRAMERATE, &p);
	}
	if (r == 0 && req->force_key) {
		r = n2ap_venc_set_param(g.s.enc, VENC_SET_PARAM_FORCE_INTRA, NULL);
	}
	if (r != 0) {
		if (guest_stopped()) {
			fatal_guest_stop(from, req->h.seq, req->h.session);
		}
		uint32_t st = map_error(r);
		send_error(from, req->h.seq, req->h.session, st, (uint32_t)(-r));
		if (err_fatal(r)) { session_end(st, (uint32_t)(-r), "ENC_SET failed"); }
		return;
	}
	if (req->bitrate) { g.s.bitrate = req->bitrate; }
	if (req->fps_num && req->fps_den) { g.s.fps_num = req->fps_num; g.s.fps_den = req->fps_den; }
	reply_simple(from, req->h.seq, g.s.id, N2VCD_ENC_SET_OK, N2VCD_E_OK, 0);
}

static void
handle_flush(uint32_t from, const struct n2vcd_simple *req, uint32_t bytes)
{
	if (bytes != (uint32_t)sizeof(*req)) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_ARGS, (uint32_t)sizeof(*req));
		return;
	}
	if (!session_owned(req->h.session, from)) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_SESSION, g.s.id);
		return;
	}
	g.s.last_activity_us = now_us();
	/* Encode is synchronous (AP_IPIMSG_ENC_ENCODE is answered before the reply leaves): there is never a queued frame,
	 * so a flush has nothing to drop.  V41 has no flush and none is needed in v1. */
	reply_simple(from, req->h.seq, g.s.id, N2VCD_FLUSH_OK, N2VCD_E_OK, 0);
}

static void
handle_close(uint32_t from, const struct n2vcd_simple *req, uint32_t bytes)
{
	if (bytes != (uint32_t)sizeof(*req)) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_ARGS, (uint32_t)sizeof(*req));
		return;
	}
	if (!session_owned(req->h.session, from)) {
		send_error(from, req->h.seq, req->h.session, N2VCD_E_SESSION, g.s.id);
		return;
	}
	uint32_t id = g.s.id;
	reply_simple(from, req->h.seq, id, N2VCD_CLOSE_OK, N2VCD_E_OK, 0);
	session_end(N2VCD_E_OK, 0, "");
}

static void
handle_message(const uint8_t *buf, uint32_t bytes, uint32_t from)
{
	if (bytes < sizeof(struct n2vcd_hdr)) { return; }             /* not even a header: nothing to answer to */
	struct n2vcd_hdr h;
	memcpy(&h, buf, sizeof(h));
	if (h.magic != N2VCD_MAGIC) {
		send_error(from, h.seq, h.session, N2VCD_E_ARGS, h.magic);
		return;
	}
	if (h.seq == 0u) {
		/* seq 0 is reserved for the daemon's unsolicited session-death event; a client request must not use it. */
		send_error(from, 0, h.session, N2VCD_E_ARGS, 0);
		return;
	}
#ifdef N2VCD_WITH_DECODER
	if (g.dec) {
		uint32_t ds = n2vcd_dec_service_session(g.dec);
		int mine = n2vcd_dec_service_owns(buf, bytes) ||
		           ((h.type == N2VCD_FLUSH || h.type == N2VCD_CLOSE) && ds != 0u && h.session == ds);
		if (mine) {
			if (h.type == N2VCD_OPEN && g.s.active) {
				send_error(from, h.seq, h.session, N2VCD_E_BUSY, g.s.id);   /* the encoder session is open */
				return;
			}
			int r = n2vcd_dec_service_handle(g.dec, from, buf, bytes);
			if (r == -ENODEV) { fatal_guest_stop(from, h.seq, h.session); }
			if (r == 0) { return; }
			/* -EOPNOTSUPP: not the service's after all; fall through to the encoder switch */
		}
	}
#endif
	switch (h.type) {
	case N2VCD_HELLO: {
		if (bytes < sizeof(struct n2vcd_hello)) { send_error(from, h.seq, h.session, N2VCD_E_ARGS, bytes); return; }
		struct n2vcd_hello req;
		memcpy(&req, buf, sizeof(req));
		handle_hello(from, &req, bytes);
		return;
	}
	case N2VCD_OPEN: {
		if (bytes < sizeof(struct n2vcd_open)) { send_error(from, h.seq, h.session, N2VCD_E_ARGS, bytes); return; }
		struct n2vcd_open req;
		memcpy(&req, buf, sizeof(req));
		handle_open(from, &req, bytes);
		return;
	}
	case N2VCD_ENC_FRAME: {
		if (bytes < sizeof(struct n2vcd_enc_frame)) { send_error(from, h.seq, h.session, N2VCD_E_ARGS, bytes); return; }
		struct n2vcd_enc_frame req;
		memcpy(&req, buf, sizeof(req));
		handle_enc_frame(from, &req, bytes);
		return;
	}
	case N2VCD_ENC_SET: {
		if (bytes < sizeof(struct n2vcd_enc_set)) { send_error(from, h.seq, h.session, N2VCD_E_ARGS, bytes); return; }
		struct n2vcd_enc_set req;
		memcpy(&req, buf, sizeof(req));
		handle_enc_set(from, &req, bytes);
		return;
	}
	case N2VCD_FLUSH: {
		if (bytes < sizeof(struct n2vcd_simple)) { send_error(from, h.seq, h.session, N2VCD_E_ARGS, bytes); return; }
		struct n2vcd_simple req;
		memcpy(&req, buf, sizeof(req));
		handle_flush(from, &req, bytes);
		return;
	}
	case N2VCD_CLOSE: {
		if (bytes < sizeof(struct n2vcd_simple)) { send_error(from, h.seq, h.session, N2VCD_E_ARGS, bytes); return; }
		struct n2vcd_simple req;
		memcpy(&req, buf, sizeof(req));
		handle_close(from, &req, bytes);
		return;
	}
	case N2VCD_DEC_HEADER:
	case N2VCD_DEC_FRAME:
		send_error(from, h.seq, h.session, N2VCD_E_UNSUPPORTED, h.type);
		return;
	default:
		send_error(from, h.seq, h.session, N2VCD_E_ARGS, h.type);
		return;
	}
}

/* ---- decoder service glue (opus-decint) ------------------------------------------------------------------------ */
#ifdef N2VCD_WITH_DECODER
static int
dec_send(void *ctx, uint32_t to, const void *msg, uint32_t bytes)
{
	(void)ctx;
	int r = n2vcd_srv_send(g.srv, to, msg, bytes);
	if (r < 0) { n2vcd_log("dec: send to client %u failed: %d", to, r); }
	return r;
}

static int
dec_dma_info(void *ctx, uint32_t handle, uint32_t *pa, uint32_t *bytes)
{
	(void)ctx;
#ifdef N2IP_TEST_HOOKS
	if (g.dma_dir[0]) {
		/* host oracle: the same synthetic pa register_dma_handle() gives a --dma-dir file handle */
		char path[320];
		struct stat st;
		int pn = snprintf(path, sizeof(path), "%s/%u", g.dma_dir, handle);
		if (pn > 0 && (size_t)pn < sizeof(path) && stat(path, &st) == 0 && st.st_size > 0 &&
		    (uintmax_t)st.st_size <= 0xffffffffu) {
			*pa = 0x60000000u + handle * 0x200000u;
			*bytes = (uint32_t)st.st_size;
			return 0;
		}
	}
#endif
	int r = n2vcd_srv_dma_info(g.srv, handle, pa, bytes);
	if (r != 0) { n2vcd_log("dec: RWVC_DMA_INFO handle %u -> %d", handle, r); }
	return r;
}

static void
dec_log(void *ctx, const char *line)
{
	(void)ctx;
	n2vcd_log("dec: %s", line);
}

static void __attribute__((noreturn))
dec_fatal(void *ctx, uint32_t code)
{
	(void)ctx;
	/* the service has answered the owner and told the other clients; same exit as the encoder's guest stop */
	g.srv = NULL;                       /* no second broadcast from fatal_guest_stop */
	n2vcd_log("dec: fatal guest stop code=%u", code);
	fatal_guest_stop(0, 0, 0);
}

static int
dec_guest_stopped(void *ctx, uint32_t *code)
{
	(void)ctx;
	if (!guest_stopped()) { return 0; }
	if (code) { *code = guest_stop_code(); }
	return 1;
}

/* n2ap_vdec's event hook: the first 96 events of the daemon's life (power, LOCK, WAITISR order), one line per decode with its
 * wall time and the vendor's changed bits, and every timeout/protocol/hardware error. */
static void
dec_trace(void *arg, const char *event, uint32_t a, uint32_t b, uint32_t c)
{
	(void)arg;
	uint64_t tnow = now_us();
	if (strcmp(event, "decode") == 0) {
		struct n2vcu_stats vs;
		g.dec_t_start_us = tnow;
		g.dec_t_lock_us = g.dec_t_lat_us = g.dec_t_core_us = g.dec_t_unlock_us = 0;
		memset(&vs, 0, sizeof(vs));
		if (g.vcu) { n2vcu_stats(g.vcu, &vs); }
		g.dec_r0 = vs.mmio_reads; g.dec_w0 = vs.mmio_writes; g.dec_c0 = vs.hw_regs_calls;
	} else if (strcmp(event, "lock") == 0 && a == 1u && !g.dec_t_lock_us) {
		g.dec_t_lock_us = tnow;                       /* LOCK_CORE answered: both blocks powered */
	} else if (strcmp(event, "wait_isr") == 0) {
		if (a == 1u) { g.dec_t_lat_us = tnow; } else { g.dec_t_core_us = tnow; }
	} else if (strcmp(event, "unlock") == 0 && b == 0u) {
		g.dec_t_unlock_us = tnow;
	} else if (strcmp(event, "decode_done") == 0 && g.dec_t_start_us != 0u) {
		/* opus-decint perf (vc22): one line per START with the phase split and the register traffic it cost:
		 * lock = START -> LOCK_CORE answered (vendor software before the hardware), lat/core = time to each done,
		 * unlock = after CORE done until the last UNLOCK, regs = guest MMIO reads/writes and back-end ioctls. */
		struct n2vcu_stats vs;
		uint64_t t0 = g.dec_t_start_us, us = tnow - t0;
#define N2D_MS(x) ((x) ? (unsigned long long)(((x) - t0) / 1000u) : 0ull), ((x) ? (unsigned)(((x) - t0) % 1000u) : 0u)
		memset(&vs, 0, sizeof(vs));
		if (g.vcu) { n2vcu_stats(g.vcu, &vs); }
		/* opus-robust: this was one ~200-byte line per decoded picture (to vcodecd.log AND /dev/console), i.e. 30-60
		 * synchronous writes per second during playback and ~27 MB of log per hour of 4K.  Keep the first 16 STARTs
		 * of the daemon's life, then every 60th, plus any START slower than 40 ms (a stall worth seeing). */
		uint32_t nth = g.dec_inputs++;
		if (!(nth < 16u || nth % 60u == 0u || us >= 40000u)) {
			g.dec_t_start_us = 0;
			return;
		}
		n2vcd_log("dec: START %u done changed=0x%x in %llu.%03llu ms (lock %llu.%03u lat %llu.%03u core %llu.%03u "
		          "unlock %llu.%03u) regs r=%llu w=%llu ioctls=%llu", nth, a,
		          (unsigned long long)(us / 1000u), (unsigned long long)(us % 1000u), N2D_MS(g.dec_t_lock_us),
		          N2D_MS(g.dec_t_lat_us), N2D_MS(g.dec_t_core_us), N2D_MS(g.dec_t_unlock_us),
		          (unsigned long long)(vs.mmio_reads - g.dec_r0), (unsigned long long)(vs.mmio_writes - g.dec_w0),
		          (unsigned long long)(vs.hw_regs_calls - g.dec_c0));
#undef N2D_MS
		g.dec_t_start_us = 0;
		return;
	}
	if (strcmp(event, "wc") == 0) {
		n2vcd_log("dec: session %u write-combining %s", b, a ? "ON" : "off");
		return;
	}
	int always = strcmp(event, "timeout") == 0 || strcmp(event, "proto") == 0 || strcmp(event, "hw") == 0 ||
	             strcmp(event, "fourcc") == 0 || (strcmp(event, "wait_isr") == 0 && b != 0u) ||
	             (strcmp(event, "power") == 0 && c != 0u);
	if (always || g.dec_trace_lines < 96u) {
		g.dec_trace_lines++;
		n2vcd_log("dec: ev %s 0x%x 0x%x 0x%x", event, a, b, c);
	}
}

static int
decoder_start(void)
{
	if (!g.vcu || !g.vhw) {
		n2vcd_log("dec: no vcu/vendor hw (bring-up incomplete): decoder service not started");
		return -1;
	}
	struct n2vcd_dec_ap_n2ap_cfg acfg;
	memset(&acfg, 0, sizeof(acfg));
	acfg.vcu = g.vcu;
	acfg.hw = g.vhw;
	acfg.timeout_ms = (unsigned)g.hw_timeout_ms > 6000u ? (unsigned)g.hw_timeout_ms : 6000u;
	acfg.guest_stopped = dec_guest_stopped;
	acfg.guest_ctx = NULL;
	acfg.fake_irq = g.fake_vdec_irq;
	acfg.mem = g.mem;
	acfg.write_combining = g.dec_wc;
	acfg.trace = dec_trace;
	acfg.trace_arg = NULL;
	g.dec_ap = n2vcd_dec_ap_n2ap_create(&acfg);
	if (!g.dec_ap) { n2vcd_log("dec: adapter create failed"); return -1; }
	struct n2vcd_dec_service_cfg dcfg;
	memset(&dcfg, 0, sizeof(dcfg));
	dcfg.send = dec_send;
	dcfg.dma_info = dec_dma_info;
	dcfg.ctx = NULL;
	dcfg.ap = n2vcd_dec_ap_n2ap_ops();
	dcfg.ap_ctx = g.dec_ap;
	dcfg.log = dec_log;
	dcfg.fatal = dec_fatal;
	dcfg.session_idle_ms = (uint32_t)(g.session_idle_ms > 0 ? g.session_idle_ms : 0);
	dcfg.in_buf_bytes = 2u << 20;      /* per-START device->guest refresh + DEC01 to_device sync copy the whole buffer */
	g.dec = n2vcd_dec_service_create(&dcfg);
	if (!g.dec) { n2vcd_log("dec: service create failed"); return -1; }
	n2vcd_log("dec: decoder service up (ipi timeout %u ms%s, write-combining %s)", acfg.timeout_ms,
	          g.fake_vdec_irq ? ", FAKE VDEC IRQ" : "", g.dec_wc == 1 ? "on" : g.dec_wc == 2 ? "alternating per session" : "off");
	return 0;
}
#endif

/* ---- watchdog ------------------------------------------------------------------------------------------------- */

/* opus-lead, 11 Nov (first phone encode): the main thread handles one request at a time and every wait under it is
 * bounded, but nothing proved the bounds compose on the phone.  If one request stays in progress longer than any legal
 * OPEN/ENC_FRAME (10 IPI bounds + 10 s), exit: the kernel's last-close path (n2vc_open_destroy) drops every VENC/VENC_C1
 * reference of this process and powers the domain off.  No power call from this thread: the guest may be inside one. */
static _Atomic uint64_t g_busy_since_us;

static void *
stuck_watchdog(void *arg)
{
	(void)arg;
	for (;;) {
		struct timespec t = { 0, 500000000L };
		(void)nanosleep(&t, NULL);
		uint64_t since = atomic_load(&g_busy_since_us);
		uint64_t limit_us = ((uint64_t)g.hw_timeout_ms * 10u + 10000u) * 1000u;
		if (since != 0u && now_us() - since > limit_us) {
			n2vcd_log("watchdog: request stuck for %llu ms (limit %llu), exiting so the kernel powers VENC off",
			          (unsigned long long)((now_us() - since) / 1000u), (unsigned long long)(limit_us / 1000u));
			_exit(71);
		}
	}
	return NULL;
}

static void
watchdog_poll(void)
{
	if (!g.s.active) { return; }
	if (guest_stopped()) {
		fatal_guest_stop(g.s.client, 0, g.s.id);
	}
	if (g.session_idle_ms > 0 && g.s.last_activity_us != 0) {
		uint64_t idle_us = now_us() - g.s.last_activity_us;
		if (idle_us > (uint64_t)g.session_idle_ms * 1000u) {
			uint32_t client = g.s.client;
			uint32_t id = g.s.id;
			n2vcd_log("session %u idle for %llu ms (limit %d), dropping it",
			          id, (unsigned long long)(idle_us / 1000u), g.session_idle_ms);
			send_error(client, 0, id, N2VCD_E_SESSION, 0);
			session_end(N2VCD_E_SESSION, 0, "idle timeout");
		}
	}
}

/* ---- test-only file-backed DMA -------------------------------------------------------------------------------- */

#ifdef N2IP_TEST_HOOKS
struct filehw {
	struct n2vc_hw inner;
	char dir[256];
};

static int fh_power(void *ctx, unsigned block, int on)
{ struct filehw *f = (struct filehw *)ctx; return f->inner.power(f->inner.ctx, block, on); }
static int fh_regs(void *ctx, struct rwvc_reg_op *ops, unsigned count, unsigned *done)
{ struct filehw *f = (struct filehw *)ctx; return f->inner.regs(f->inner.ctx, ops, count, done); }
static int fh_wait_irq(void *ctx, unsigned irq, unsigned timeout_ms, uint32_t *status)
{ struct filehw *f = (struct filehw *)ctx; return f->inner.wait_irq(f->inner.ctx, irq, timeout_ms, status); }
static int fh_irq_clear(void *ctx, unsigned irq)
{ struct filehw *f = (struct filehw *)ctx; return f->inner.irq_clear(f->inner.ctx, irq); }
static int fh_dma_alloc(void *ctx, uint32_t bytes, uint32_t flags, uint32_t *handle, uint32_t *pa)
{ struct filehw *f = (struct filehw *)ctx; return f->inner.dma_alloc(f->inner.ctx, bytes, flags, handle, pa); }
static int fh_dma_free(void *ctx, uint32_t handle)
{ struct filehw *f = (struct filehw *)ctx; return f->inner.dma_free(f->inner.ctx, handle); }
static int fh_dma_rw(void *ctx, uint32_t handle, uint32_t offset, void *host, uint32_t bytes, int write)
{
	struct filehw *f = (struct filehw *)ctx;
	char path[320];
	int n = snprintf(path, sizeof(path), "%s/%u", f->dir, handle);
	if (n <= 0 || (size_t)n >= sizeof(path)) { return -EINVAL; }
	int fd = open(path, O_RDWR);
	if (fd < 0) { return f->inner.dma_rw(f->inner.ctx, handle, offset, host, bytes, write); }   /* not a client buffer */
	ssize_t got;
	if (write) { got = pwrite(fd, host, bytes, (off_t)offset); }
	else { got = pread(fd, host, bytes, (off_t)offset); }
	(void)close(fd);
	return got == (ssize_t)bytes ? 0 : -EIO;
}
static void fh_close(void *ctx)
{
	struct filehw *f = (struct filehw *)ctx;
	if (f->inner.close) { f->inner.close(f->inner.ctx); }
	free(f);
}
static int
n2ip_filehw_wrap(struct n2vc_hw *hw, const char *dir)
{
	struct filehw *f = (struct filehw *)calloc(1, sizeof(*f));
	if (!f) { return -1; }
	f->inner = *hw;
	(void)snprintf(f->dir, sizeof(f->dir), "%s", dir);
	hw->ctx = f;
	hw->power = fh_power;
	hw->regs = fh_regs;
	hw->wait_irq = fh_wait_irq;
	hw->irq_clear = fh_irq_clear;
	hw->dma_alloc = fh_dma_alloc;
	hw->dma_free = fh_dma_free;
	hw->dma_rw = fh_dma_rw;
	hw->close = fh_close;
	return 0;
}
#else
static int n2ip_filehw_wrap(struct n2vc_hw *hw, const char *dir) { (void)hw; (void)dir; return -1; }
#endif

/* ---- always-on hardware-call log (phone diagnostics) ------------------------------------------------------------
 * Phone run vc10: OPEN failed with -EINVAL and the only evidence was a kernel "VCT =22" after an RWVC_DMA_ALLOC.  This
 * wrapper puts every DMA allocation (first 64), every power call and every failing hardware call (first 32 per kind)
 * into vcodecd.log with its arguments and errno, so the next phone log names the failing ioctl directly. */
struct loghw {
	struct n2vc_hw inner;
	unsigned allocs, alloc_fail, other_fail, regs_fail, irq_fail, powers;
};
static struct loghw g_loghw;

static int lh_power(void *ctx, unsigned block, int on)
{
	struct loghw *l = (struct loghw *)ctx;
	int r = l->inner.power(l->inner.ctx, block, on);
	if (r != 0 || l->powers < 64u) { l->powers++; n2vcd_log("hw power block=%u on=%d -> %d", block, on, r); }
	return r;
}
static int lh_regs(void *ctx, struct rwvc_reg_op *ops, unsigned count, unsigned *done)
{
	struct loghw *l = (struct loghw *)ctx;
	int r = l->inner.regs(l->inner.ctx, ops, count, done);
	if (r != 0 && l->regs_fail++ < 32u) {
		n2vcd_log("hw regs count=%u done=%u first_op=%u off=0x%x -> %d", count, done ? *done : 0u,
		          count ? (unsigned)ops[0].op : 0u, count ? (unsigned)ops[0].offset : 0u, r);
	}
	return r;
}
static int lh_wait_irq(void *ctx, unsigned irq, unsigned timeout_ms, uint32_t *status)
{
	struct loghw *l = (struct loghw *)ctx;
	int r = l->inner.wait_irq(l->inner.ctx, irq, timeout_ms, status);
	if (r != 0 && l->irq_fail++ < 32u) { n2vcd_log("hw wait_irq irq=%u timeout_ms=%u -> %d", irq, timeout_ms, r); }
	return r;
}
static int lh_irq_clear(void *ctx, unsigned irq)
{
	struct loghw *l = (struct loghw *)ctx;
	int r = l->inner.irq_clear(l->inner.ctx, irq);
	if (r != 0 && l->other_fail++ < 32u) { n2vcd_log("hw irq_clear irq=%u -> %d", irq, r); }
	return r;
}
static int lh_dma_alloc(void *ctx, uint32_t bytes, uint32_t flags, uint32_t *handle, uint32_t *pa)
{
	struct loghw *l = (struct loghw *)ctx;
	int r = l->inner.dma_alloc(l->inner.ctx, bytes, flags, handle, pa);
	if (r != 0) {
		if (l->alloc_fail++ < 32u) { n2vcd_log("hw dma_alloc bytes=%u flags=0x%x -> %d FAILED", bytes, flags, r); }
	} else if (l->allocs++ < 64u) {
		n2vcd_log("hw dma_alloc bytes=%u flags=0x%x -> handle=0x%x pa=0x%08x", bytes, flags,
		          handle ? *handle : 0u, pa ? *pa : 0u);
	}
	return r;
}
static int lh_dma_free(void *ctx, uint32_t handle)
{
	struct loghw *l = (struct loghw *)ctx;
	int r = l->inner.dma_free(l->inner.ctx, handle);
	if (r != 0 && l->other_fail++ < 32u) { n2vcd_log("hw dma_free handle=0x%x -> %d", handle, r); }
	return r;
}
static int lh_dma_rw(void *ctx, uint32_t handle, uint32_t offset, void *host, uint32_t bytes, int write)
{
	struct loghw *l = (struct loghw *)ctx;
	int r = l->inner.dma_rw(l->inner.ctx, handle, offset, host, bytes, write);
	if (r != 0 && l->other_fail++ < 32u) {
		n2vcd_log("hw dma_rw handle=0x%x off=%u bytes=%u write=%d -> %d", handle, offset, bytes, write, r);
	}
	return r;
}
static void lh_close(void *ctx)
{
	struct loghw *l = (struct loghw *)ctx;
	if (l->inner.close) { l->inner.close(l->inner.ctx); }
}
/* Wraps only the methods the back end has (a NULL method stays NULL, so feature tests downstream are unchanged). */
static void
loghw_wrap(struct n2vc_hw *hw)
{
	struct loghw *l = &g_loghw;
	memset(l, 0, sizeof(*l));
	l->inner = *hw;
	hw->ctx = l;
	if (hw->power) { hw->power = lh_power; }
	if (hw->regs) { hw->regs = lh_regs; }
	if (hw->wait_irq) { hw->wait_irq = lh_wait_irq; }
	if (hw->irq_clear) { hw->irq_clear = lh_irq_clear; }
	if (hw->dma_alloc) { hw->dma_alloc = lh_dma_alloc; }
	if (hw->dma_free) { hw->dma_free = lh_dma_free; }
	if (hw->dma_rw) { hw->dma_rw = lh_dma_rw; }
	hw->close = lh_close;
}

/* ---- main ----------------------------------------------------------------------------------------------------- */

static void
usage(FILE *out)
{
	fprintf(out,
	        "usage: vcodecd [--fake] [--dev PATH] [--socket PATH] [--vendor DIR] [--record PATH]\n"
	        "               [--dma-dir DIR] [--hw-timeout-ms N] [--session-idle-ms N] [--selftest]\n"
	        "               [--no-console] [--help]\n"
	        "  --fake          test builds only: T02's fake n2vc_hw instead of /dev/redwood-vcodec\n"
	        "  --dev PATH      hardware device (default /dev/redwood-vcodec)\n"
	        "  --socket PATH   serve a SOCK_SEQPACKET UNIX socket instead of the kernel relay (host only)\n"
	        "  --vendor DIR    vendor directory for n2vhost_start (default $N2HW_VENDOR_DIR or\n"
	        "                  /private/var/db/nord2-vcodec/vendor)\n"
	        "  --gate PATH     start only if PATH exists; otherwise log and exit 0 (launchd gate)\n"
	        "  --record PATH   test build only, --fake: register/DMA recorder file\n"
	        "  --dma-dir DIR   test build only: DMA handle N reads/writes file DIR/N\n"
	        "  --fake-venc     test build only, --fake: opus-venc's behavioural VENC model on top of the fake\n"
	        "  --hw-timeout-ms N  encode wall-clock limit (default %d)\n"
	        "  --session-idle-ms N  drop a session idle for N ms (default %d, 0 = never)\n"
	        "  --restart-on-switch  an encoder OPEN of another codec than the last session's, or any OPEN after an HEVC\n"
	        "                  session, is answered BUSY/RESTART and\n"
	        "                  the daemon exits 75 (needs launchd KeepAlive {SuccessfulExit: false})\n"
	        "  --venc-clock M  auto (default) | auto:PIXELS_PER_S | slow | fast | keep: kern.n2vc.venc_fast per encoder session\n"
	        "                  (624 MHz above 1080p30 or above 30 fps at > 640x480); the first word of " N2VCLK_FILE "\n"
	        "                  overrides it at every OPEN\n"
	        "  --venc-keep-power  keep each VENC core powered from its first GCE list to the session's end (no power cycle per frame)\n"
	        "  --selftest      no hardware, no vendor files: 4 GiB guest space, two interpreted guest threads,\n"
	        "                  1000000 instructions, one recovered fault, per-thread TLS; exit 0/1\n"
	        "  --no-console    do not duplicate log lines to /dev/console\n"
	        "  --decoder       also serve decoder sessions (daemon/dec over ap/n2ap_vdec; builds with N2VCD_WITH_DECODER)\n"
	        "  --guest-log N   copy the first N distinct vendor log lines (__android_log_print) into the log\n"
	        "  --log-file PATH the file fd 2 is (launchd StandardErrorPath; default on iOS " N2VCD_LOGCAP_PHONE_PATH "):\n"
	        "                  keep it under --log-max bytes, older lines in PATH.1 (rotated at start and when full)\n"
	        "  --log-max N     log cap in bytes (default 4194304; 0 = no cap)\n"
	        "  --fake-vdec-hevc test build only, --fake: opus-hevcdec's HEVC VDEC model (raises the VDEC interrupts)\n"
	        "  --dec-wc MODE   off|on|alt: combine guest register stores during decoder sessions (alt: every 2nd session)\n"
	        "  --fake-vdec-irq test build only, --fake: answer the decoder's WAITISR at once (the fake raises no VDEC IRQ)\n",
	        N2VCD_DEFAULT_HW_TIMEOUT_MS, N2VCD_DEFAULT_IDLE_MS);
}

int
main(int argc, char **argv)
{
	memset(&g, 0, sizeof(g));
	g.start_us = now_us();
	g.console_fd = -1;
	g.hw_timeout_ms = N2VCD_DEFAULT_HW_TIMEOUT_MS;
	g.session_idle_ms = N2VCD_DEFAULT_IDLE_MS;
	g.vclk_mode = N2VCLK_AUTO;
	g.vclk_thr = N2VCLK_AUTO_PPS;

	const char *dev = "/dev/redwood-vcodec";
	const char *sock = NULL;
	const char *record = NULL;
	const char *dma_dir = NULL;
	const char *gate = NULL;
	const char *vendor = getenv("N2HW_VENDOR_DIR");
	int fake = 0;
	int fake_venc = 0;
	int fake_vdec_hevc = 0;
#if defined(N2IP_TEST_HOOKS) && defined(N2IP_FAKE_VDEC_HEVC)
	struct n2vdec_hevc_fake *hevc_model = NULL;
#endif
	int fake_vdec_fmt = 0;
#if defined(N2IP_TEST_HOOKS) && defined(N2IP_FAKE_VDEC_FMT)
	struct n2vdec_fmt_fake *fmt_model = NULL;
#endif
	int no_console = 0;
	int selftest = 0;
#ifdef __APPLE__
	const char *log_file = N2VCD_LOGCAP_PHONE_PATH;   /* launchd's StandardErrorPath (guest-vcodec.sh) */
#else
	const char *log_file = NULL;
#endif
	unsigned long long log_max = N2VCD_LOGCAP_DEFAULT_MAX;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (strcmp(a, "--fake") == 0) { fake = 1; }
		else if (strcmp(a, "--fake-venc") == 0) { fake_venc = 1; }
		else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) { usage(stdout); return 0; }
		else if (strcmp(a, "--no-console") == 0) { no_console = 1; }
		else if (strcmp(a, "--selftest") == 0) { selftest = 1; }
		else if (strcmp(a, "--decoder") == 0) { g.decoder = 1; }
		else if (strcmp(a, "--fake-vdec-irq") == 0) { g.fake_vdec_irq = 1; }
		else if (strcmp(a, "--dec-wc") == 0 && i + 1 < argc) {
			const char *m = argv[++i];
			g.dec_wc = !strcmp(m, "on") ? 1 : !strcmp(m, "alt") ? 2 : 0;
		}
		else if (strcmp(a, "--fake-vdec-hevc") == 0) { fake_vdec_hevc = 1; }
		else if (strcmp(a, "--fake-vdec-fmt") == 0) { fake_vdec_fmt = 1; }
		else if (strcmp(a, "--restart-on-switch") == 0) { g.restart_on_switch = 1; }
		else if (strcmp(a, "--venc-keep-power") == 0) { g.venc_keep_power = 1; }
		else if (strcmp(a, "--venc-clock") == 0 && i + 1 < argc) {
			if (n2vclk_parse(argv[++i], &g.vclk_mode, &g.vclk_thr) != 0) { fprintf(stderr, "vcodecd: bad --venc-clock %s\n", argv[i]); return 2; }
		}
		else if (strcmp(a, "--log-file") == 0 && i + 1 < argc) { log_file = argv[++i]; }
		else if (strcmp(a, "--log-max") == 0 && i + 1 < argc) { log_max = strtoull(argv[++i], NULL, 10); }
		else if (strcmp(a, "--guest-log") == 0 && i + 1 < argc) { g_guest_log_max = (unsigned)strtoul(argv[++i], NULL, 10); }
		else if (strcmp(a, "--dev") == 0 && i + 1 < argc) { dev = argv[++i]; }
		else if (strcmp(a, "--socket") == 0 && i + 1 < argc) { sock = argv[++i]; }
		else if (strcmp(a, "--vendor") == 0 && i + 1 < argc) { vendor = argv[++i]; }
		else if (strcmp(a, "--gate") == 0 && i + 1 < argc) { gate = argv[++i]; }
		else if (strcmp(a, "--record") == 0 && i + 1 < argc) { record = argv[++i]; }
		else if (strcmp(a, "--dma-dir") == 0 && i + 1 < argc) { dma_dir = argv[++i]; }
		else if (strcmp(a, "--hw-timeout-ms") == 0 && i + 1 < argc) {
			long v = strtol(argv[++i], NULL, 10);
			if (v < 1 || v > 600000) { fprintf(stderr, "vcodecd: bad --hw-timeout-ms\n"); return 2; }
			g.hw_timeout_ms = (int)v;
			g.hw_timeout_explicit = 1;
		} else if (strcmp(a, "--session-idle-ms") == 0 && i + 1 < argc) {
			long v = strtol(argv[++i], NULL, 10);
			if (v < 0 || v > 3600000) { fprintf(stderr, "vcodecd: bad --session-idle-ms\n"); return 2; }
			g.session_idle_ms = (int)v;
		} else {
			fprintf(stderr, "vcodecd: unknown argument %s\n", a);
			usage(stderr);
			return 2;
		}
	}
	if (!vendor) { vendor = "/private/var/db/nord2-vcodec/vendor"; }
#ifndef N2VCD_WITH_DECODER
	if (g.decoder) { fprintf(stderr, "vcodecd: --decoder: this build has no decoder service\n"); return 2; }
#endif
#ifndef N2IP_TEST_HOOKS
	if (g.fake_vdec_irq) { fprintf(stderr, "vcodecd: --fake-vdec-irq is a test-build option\n"); return 2; }
#endif
	if (g.fake_vdec_irq && !fake) { fprintf(stderr, "vcodecd: --fake-vdec-irq needs --fake\n"); return 2; }

	/* First thing the main session runs on the phone: no hardware, no vendor files, no daemon state. */
	if (selftest) { return n2vcd_selftest(); }

	(void)signal(SIGPIPE, SIG_IGN);
	(void)signal(SIGINT, on_signal);
	(void)signal(SIGTERM, on_signal);
	(void)signal(SIGHUP, on_signal);

	if (!no_console) {
		g.console_fd = open("/dev/console", O_WRONLY | O_NOCTTY | O_NONBLOCK);
	}
	{
		/* opus-robust: cap vcodecd.log (KeepAlive restarts used to append forever).  Only when fd 2 is that file. */
		char note[700];
		int lr = n2vcd_logcap_init(&g_logcap, log_file, (uint64_t)log_max, 2, note, sizeof(note));
		if (note[0] && (lr != 0 || log_file != NULL)) { n2vcd_log("%s", note); }
	}
	if (gate && access(gate, F_OK) != 0) {
		n2vcd_log("gate %s absent: not starting", gate);
		if (g.console_fd >= 0) { (void)close(g.console_fd); }
		return 0;
	}

	if (fake) {
#ifdef N2IP_TEST_HOOKS
		int r = n2vc_hw_open_fake(&g.hw, record);
		if (r != 0) {
			n2vcd_log("cannot open the fake hardware back end: %d", r);
			return 1;
		}
#else
		(void)record;
		n2vcd_log("--fake is only available in the test build (N2IP_TEST_HOOKS); refusing");
		return 2;
#endif
	} else {
		int r = n2vc_hw_open_dev(&g.hw, dev);
		if (r != 0) {
			n2vcd_log("cannot open %s: %d", dev, r);
			return 1;
		}
	}
	if (dma_dir) {
		if (n2ip_filehw_wrap(&g.hw, dma_dir) != 0) {
			n2vcd_log("this build has no test DMA hook (--dma-dir)");
			if (g.hw.close) { g.hw.close(g.hw.ctx); }
			return 1;
		}
		(void)snprintf(g.dma_dir, sizeof(g.dma_dir), "%s", dma_dir);
	}
	if (fake_venc) {
#if defined(N2IP_TEST_HOOKS) && defined(N2IP_FAKE_VENC)
		g_fv_inner = g.hw;
		if (n2venc_fake_wrap(&g_fv_inner, &g_fv_outer, &g_fv, NULL) != 0) {
			n2vcd_log("cannot attach the fake VENC model");
			if (g.hw.close) { g.hw.close(g.hw.ctx); }
			return 1;
		}
		g.hw = g_fv_outer;
		n2vcd_log("fake VENC model attached (test-only)");
#else
		n2vcd_log("--fake-venc is only available in test builds with fakevenc/ (N2IP_FAKE_VENC); refusing");
		if (g.hw.close) { g.hw.close(g.hw.ctx); }
		return 2;
#endif
	}

	if (fake_vdec_hevc) {
#if defined(N2IP_TEST_HOOKS) && defined(N2IP_FAKE_VDEC_HEVC)
		struct n2vc_hw inner = g.hw;
		if (!fake || n2vdec_hevc_fake_open(&g.hw, &inner, NULL, &hevc_model) != 0) {
			n2vcd_log("cannot attach the HEVC VDEC model (needs --fake)");
			if (inner.close) { inner.close(inner.ctx); }
			return 1;
		}
		n2vcd_log("HEVC VDEC model attached (test-only, kernel checks by the inner fake)");
#else
		n2vcd_log("--fake-vdec-hevc is only available in test builds with fakevdec_hevc/ (N2IP_FAKE_VDEC_HEVC); refusing");
		if (g.hw.close) { g.hw.close(g.hw.ctx); }
		return 2;
#endif
	}

	if (fake_vdec_fmt) {
#if defined(N2IP_TEST_HOOKS) && defined(N2IP_FAKE_VDEC_FMT)
		struct n2vc_hw inner = g.hw;
		if (!fake || n2vdec_fmt_fake_open(&g.hw, &inner, getenv("N2VCD_FMT_TRACE"), &fmt_model) != 0) {
			n2vcd_log("cannot attach the format VDEC model (needs --fake)");
			if (inner.close) { inner.close(inner.ctx); }
			return 1;
		}
		n2vcd_log("format VDEC model attached (test-only, opus-fmt)");
#else
		n2vcd_log("--fake-vdec-fmt is only available in test builds with fmt/ (N2IP_FAKE_VDEC_FMT); refusing");
		if (g.hw.close) { g.hw.close(g.hw.ctx); }
		return 2;
#endif
	}

	n2vcd_log("start dev=%s vendor=%s fake=%d socket=%s", dev, vendor, fake, sock ? sock : "-");

	{
		/* opus-guestmem: phone run 2 was killed with OS_REASON_JETSAM / per-process limit while loading the first vendor
		 * library; the launchd JetsamMemoryLimit evidently did not apply.  Root may set its own limit. */
		char ml[400], u[96];
		int lr = n2ml_raise(3072, ml, sizeof ml);
		n2vcd_log("jetsam %s %s; %s", lr == 0 ? "raised" : "NOT raised", ml, n2ml_usage_str(u, sizeof u));
		n2a32_mem_set_log(vcodecd_mem_log, 2);
	}
	loghw_wrap(&g.hw);                   /* every DMA allocation / power call / failing hw call into vcodecd.log */
	if (g_guest_log_max && &n2shim_log != NULL) { n2shim_log = guest_log_sink; }
	int r = n2vhost_start(&g.vh, &g.hw, vendor);
	{
		char u[96];
		n2vcd_log("mem after n2vhost_start=%d %s", r, n2ml_usage_str(u, sizeof u));
	}
	if (r < 0) {
		n2vcd_log("n2vhost_start failed for vendor %s: %s", vendor,
		          g.vh ? n2vhost_error(g.vh) : "load or link error (see the log above)");
		if (g.vh) { n2vhost_stop(g.vh); }
		if (g.hw.close) { g.hw.close(g.hw.ctx); }
		return 1;
	}
	if (r == 1) {
		n2vcd_log("guest set stopped before the end of bring-up: %s (serving; OPEN answers E_GUEST)",
		          n2vhost_stop_text(g.vh));
	}
	g.vcu = n2vhost_vcu(g.vh);
	if (g.venc_keep_power) {
		if (n2vcu_set_venc_hold && g.vcu) { n2vcu_set_venc_hold(g.vcu, 1); }
		n2vcd_log("venc keep-power: %s", n2vcu_set_venc_hold && g.vcu ? "on (VENC cores held from the first list to session end)"
		                                                               : "unavailable in this build");
	}
	g.vhw = n2vhost_hw(g.vh);
	g.mem = n2vhost_mem(g.vh);
#if defined(N2IP_TEST_HOOKS) && defined(N2IP_FAKE_VDEC_HEVC)
	if (hevc_model) { n2vdec_hevc_fake_set_mem(hevc_model, g.mem); }
#endif
#if defined(N2IP_TEST_HOOKS) && defined(N2IP_FAKE_VDEC_FMT)
	if (fmt_model) { n2vdec_fmt_fake_set_mem(fmt_model, g.mem); }
#endif
	g.last_milestone = n2vhost_milestone(g.vh);
	n2vcd_log("host set: vcu=%s vendor_hw=%s unresolved=%u milestone=%s",
	          g.vcu ? "yes" : "no", g.vhw ? "yes" : "no", n2vhost_unresolved(g.vh),
	          n2vhost_milestone_name(n2vhost_milestone(g.vh)));
	/* IV's report names the shim tables that actually linked; this is how a missing V32/V33 member is visible. */
	(void)n2vhost_report(g.vh, stderr);

	char err[256];
	memset(err, 0, sizeof(err));
	if (sock) { g.srv = n2vcd_srv_open_socket(sock, err, sizeof(err)); }
	else { g.srv = n2vcd_srv_open_relay(dev, err, sizeof(err)); }
	if (!g.srv) {
		n2vcd_log("server transport: %s", err[0] ? err : "unknown error");
		n2vhost_stop(g.vh);
		if (g.hw.close) { g.hw.close(g.hw.ctx); }
		return 1;
	}
#ifdef N2VCD_WITH_DECODER
	if (g.decoder && decoder_start() != 0) {
		n2vcd_srv_close(g.srv);
		n2vhost_stop(g.vh);
		if (g.hw.close) { g.hw.close(g.hw.ctx); }
		return 1;
	}
#endif
	n2vcd_log("serving %s%s", sock ? sock : dev, g.decoder ? " (encoder + decoder)" : "");
	{
		pthread_t wt;
		if (pthread_create(&wt, NULL, stuck_watchdog, NULL) == 0) { (void)pthread_detach(wt); }
		else { n2vcd_log("watchdog thread not started"); }
	}

	while (!g.stop) {
		_Alignas(16) uint8_t buf[RWVC_MSG_MAX];
		uint32_t from = 0;
		int got = n2vcd_srv_recv(g.srv, buf, sizeof(buf), &from, N2VCD_LOOP_SLICE_MS);
		if (got > 0) {
			note_client(from);
			atomic_store(&g_busy_since_us, now_us() | 1u);
			handle_message(buf, (uint32_t)got, from);
			atomic_store(&g_busy_since_us, 0u);
		} else if (got == 0) {
			n2vcd_log("client %u closed", from);
			if (g.s.active && g.s.client == from) { session_end(N2VCD_E_SESSION, 0, "client closed"); }
#ifdef N2VCD_WITH_DECODER
			if (g.dec) { n2vcd_dec_service_client_gone(g.dec, from); }
#endif
		} else if (got != -EAGAIN) {
			if (got == -EMSGSIZE) {
				/* A datagram above RWVC_MSG_MAX is already consumed; do not let a spammer fill the log. */
				static unsigned oversize;
				oversize++;
				if ((oversize & 255u) == 1u) {
					n2vcd_log("oversized datagrams ignored: %u", oversize);
				}
			} else {
				n2vcd_log("receive failed: %d", got);
				if (got == -ENXIO) { break; }
			}
		}
		watchdog_poll();
#ifdef N2VCD_WITH_DECODER
		if (g.dec && n2vcd_dec_service_tick(g.dec, now_us() / 1000u) == -ENODEV) {
			fatal_guest_stop(0, 0, 0);
		}
#endif
	}

	n2vcd_log("stopping");
	session_end(N2VCD_E_OK, 0, "");
#ifdef N2VCD_WITH_DECODER
	if (g.dec) { n2vcd_dec_service_destroy(g.dec); g.dec = NULL; }
	if (g.dec_ap) { n2vcd_dec_ap_n2ap_destroy(g.dec_ap); g.dec_ap = NULL; }
#endif
	n2vcd_srv_close(g.srv);
	g.srv = NULL;
	n2vhost_stop(g.vh);
	g.vh = NULL;
	if (g.hw.close) { g.hw.close(g.hw.ctx); }
	n2vcd_log("exit");
	if (g.console_fd >= 0) { (void)close(g.console_fd); }
	return 0;
}
