/* SPDX-License-Identifier: MIT */
/*
 * t_real.c -- the second half of the package IP host oracle: vcodecd built against IV's REAL
 * host/n2vhost.c + the rest of the track V/T set (libn2vhost.a), the phone's own vendor set read
 * at run time, T02's fake n2vc_hw and (in this build) opus-venc's behavioural VENC model.
 * No fake_vhost.c is linked here.
 *
 * The test asserts what vcodecd must guarantee at this milestone: it starts, serves HELLO, answers OPEN with
 * either OPEN_OK or one of the documented errors, and - when OPEN succeeds - runs the separate sequence-header
 * job plus two ENC_FRAMEs through the real vendor library, writing both access units to <work>/frames.h264 for
 * the ffmpeg check in run_tests.sh.  A guest stop exits 70.  A bounded, documented OPEN error is not a crash.
 *
 * usage: t_real VCODEC_BIN WORKDIR VENDOR_DIR
 * Last line: N2TEST IP-REAL PASS n/n or N2TEST IP-REAL FAIL ...
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

struct rep {
	uint32_t type, seq, session, status, detail;
	uint32_t version, enc_codecs;
	uint32_t in_buf_bytes, out_buf_bytes;
	uint32_t out_bytes, header_bytes, key_frame;
};

static void
decode(const uint8_t *buf, int bytes, struct rep *r)
{
	memset(r, 0, sizeof(*r));
	if (bytes < (int)sizeof(struct n2vcd_hdr)) { return; }
	const struct n2vcd_hdr *h = (const struct n2vcd_hdr *)buf;
	r->type = h->type;
	r->seq = h->seq;
	r->session = h->session;
	if (h->type == N2VCD_HELLO_OK && bytes >= (int)sizeof(struct n2vcd_hello_ok)) {
		const struct n2vcd_hello_ok *m = (const struct n2vcd_hello_ok *)buf;
		r->version = m->version;
		r->enc_codecs = m->enc_codecs;
	} else if (h->type == N2VCD_OPEN_OK && bytes >= (int)sizeof(struct n2vcd_open_ok)) {
		const struct n2vcd_open_ok *m = (const struct n2vcd_open_ok *)buf;
		r->status = m->status;
		r->in_buf_bytes = m->in_buf_bytes;
		r->out_buf_bytes = m->out_buf_bytes;
	} else if (h->type == N2VCD_ENC_FRAME_OK && bytes >= (int)sizeof(struct n2vcd_enc_frame_ok)) {
		const struct n2vcd_enc_frame_ok *m = (const struct n2vcd_enc_frame_ok *)buf;
		r->status = m->status;
		r->out_bytes = m->out_bytes;
		r->header_bytes = m->header_bytes;
		r->key_frame = m->key_frame;
	} else if (bytes >= (int)sizeof(struct n2vcd_simple)) {
		const struct n2vcd_simple *s = (const struct n2vcd_simple *)buf;
		r->status = s->status;
		r->detail = s->detail;
	}
}

static int
cli_connect(const char *path)
{
	uint64_t deadline = now_ms() + 8000u;
	for (;;) {
		int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
		if (fd >= 0) {
			struct sockaddr_un sa;
			memset(&sa, 0, sizeof(sa));
			sa.sun_family = AF_UNIX;
			(void)snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", path);
			if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0) { return fd; }
			(void)close(fd);
		}
		if (now_ms() > deadline) { return -1; }
		sleep_ms(20);
	}
}

static int
request(int fd, const void *msg, uint32_t bytes, struct rep *out, int timeout_ms)
{
	const struct n2vcd_hdr *req = (const struct n2vcd_hdr *)msg;
	if (send(fd, msg, bytes, 0) != (ssize_t)bytes) { return -1; }
	uint64_t deadline = now_ms() + (uint64_t)timeout_ms;
	uint8_t buf[256];
	for (;;) {
		uint64_t now = now_ms();
		int remaining = now < deadline ? (int)(deadline - now) : 0;
		struct pollfd p = {fd, POLLIN, 0};
		int pr = poll(&p, 1, remaining);
		if (pr <= 0) { return -1; }
		ssize_t n = recv(fd, buf, sizeof(buf), 0);
		if (n <= 0) { return -1; }
		decode(buf, (int)n, out);
		if (out->seq == req->seq) { return 0; }
		if (now_ms() >= deadline) { return -1; }
	}
}

static int
hello(int fd, uint32_t seq, struct rep *out)
{
	struct n2vcd_hello m;
	memset(&m, 0, sizeof(m));
	m.h.magic = N2VCD_MAGIC;
	m.h.type = N2VCD_HELLO;
	m.h.seq = seq;
	m.version = N2VCD_VERSION;
	return request(fd, &m, (uint32_t)sizeof(m), out, 3000);
}

static int
open_session(int fd, uint32_t seq, struct rep *out, int timeout_ms)
{
	struct n2vcd_open m;
	memset(&m, 0, sizeof(m));
	m.h.magic = N2VCD_MAGIC;
	m.h.type = N2VCD_OPEN;
	m.h.seq = seq;
	m.codec = N2VCD_CODEC_H264;
	m.encoder = 1;
	m.width = 64;
	m.height = 48;
	m.fps_num = 30;
	m.fps_den = 1;
	m.bitrate = 500000;
	m.gop = 6;
	return request(fd, &m, (uint32_t)sizeof(m), out, timeout_ms);
}

static void
close_session(int fd, uint32_t session, uint32_t seq)
{
	struct n2vcd_simple m;
	memset(&m, 0, sizeof(m));
	m.h.magic = N2VCD_MAGIC;
	m.h.type = N2VCD_CLOSE;
	m.h.seq = seq;
	m.h.session = session;
	struct rep r;
	(void)request(fd, &m, (uint32_t)sizeof(m), &r, 3000);
}

static int
start_daemon(const char *bin, const char *sock, const char *work, const char *vendor, const char *log)
{
	g_pid = fork();
	if (g_pid == 0) {
		int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) { (void)dup2(fd, 2); (void)close(fd); }
		int nfd = open("/dev/null", O_RDONLY);
		if (nfd >= 0) { (void)dup2(nfd, 0); (void)close(nfd); }
		char dma[512];
		(void)snprintf(dma, sizeof(dma), "%s/dma", work);
		execl(bin, bin, "--fake", "--fake-venc", "--socket", sock, "--vendor", vendor, "--dma-dir", dma,
		      "--no-console", "--hw-timeout-ms", "3000", (char *)NULL);
		_exit(127);
	}
	return g_pid > 0 ? 0 : -1;
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
		if (now_ms() > deadline) {
			(void)kill(g_pid, SIGKILL);
			(void)waitpid(g_pid, &status, 0);
			g_pid = 0;
			return -1;
		}
		sleep_ms(20);
	}
}

static int
wait_daemon_exit(int timeout_ms)
{
	if (g_pid <= 0) { return -1; }
	uint64_t deadline = now_ms() + (uint64_t)timeout_ms;
	int status = 0;
	for (;;) {
		pid_t r = waitpid(g_pid, &status, WNOHANG);
		if (r == g_pid) { g_pid = 0; return status; }
		if (now_ms() > deadline) {
			(void)kill(g_pid, SIGKILL);
			(void)waitpid(g_pid, &status, 0);
			g_pid = 0;
			return -1;
		}
		sleep_ms(20);
	}
}

static int
log_contains(const char *path, const char *needle)
{
	FILE *f = fopen(path, "r");
	if (!f) { return 0; }
	char line[2048];
	int found = 0;
	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, needle)) { found = 1; break; }
	}
	(void)fclose(f);
	return found;
}

static int
log_has_sanitizer(const char *path)
{
	return log_contains(path, "ERROR: AddressSanitizer") || log_contains(path, "ERROR: LeakSanitizer") ||
	       log_contains(path, "runtime error:") || log_contains(path, "UndefinedBehaviorSanitizer");
}

/* ---- two real frames through the real vendor library ------------------------------------------------ */

static int
dma_path(char *out, size_t cap, const char *work, uint32_t handle)
{
	int n = snprintf(out, cap, "%s/dma/%u", work, handle);
	return n > 0 && (size_t)n < cap ? 0 : -1;
}

static int
dma_create(const char *work, uint32_t handle, uint32_t bytes)
{
	char p[700];
	if (dma_path(p, sizeof(p), work, handle) != 0) { return -1; }
	int fd = open(p, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) { return -1; }
	int r = ftruncate(fd, (off_t)bytes);
	(void)close(fd);
	return r;
}

static int
dma_rw(const char *work, uint32_t handle, uint32_t off, void *buf, uint32_t bytes, int write)
{
	char p[700];
	if (dma_path(p, sizeof(p), work, handle) != 0) { return -1; }
	int fd = open(p, write ? O_WRONLY : O_RDONLY);
	if (fd < 0) { return -1; }
	ssize_t n = write ? pwrite(fd, buf, bytes, (off_t)off) : pread(fd, buf, bytes, (off_t)off);
	(void)close(fd);
	return n == (ssize_t)bytes ? 0 : -1;
}

static int
enc_frame(int fd, uint32_t session, uint32_t in, uint32_t stride, uint32_t out, uint32_t cap,
          uint64_t pts, uint32_t force_key, uint32_t seq, struct rep *r, int timeout_ms)
{
	struct n2vcd_enc_frame m;
	memset(&m, 0, sizeof(m));
	m.h.magic = N2VCD_MAGIC;
	m.h.type = N2VCD_ENC_FRAME;
	m.h.seq = seq;
	m.h.session = session;
	m.in_handle = in;
	m.out_handle = out;
	m.out_capacity = cap;
	m.stride = stride;
	m.pts = pts;
	m.force_key = force_key;
	return request(fd, &m, (uint32_t)sizeof(m), r, timeout_ms);
}

/* Two ENC_FRAMEs through vcodecd -> V41 -> the phone's libvcodecdrv.so under the interpreter -> opus-venc's VENC
 * model.  The daemon opened the session in separate-header mode, so frame 0 can only start with SPS/PPS if the
 * cached OPEN header job was prepended by the daemon itself.  Both AUs are appended to <work>/frames.h264 for the
 * ffmpeg check in run_tests.sh. */
static void
run_real_frames(int fd, uint32_t session, uint32_t in_bytes, uint32_t out_bytes, const char *work)
{
	const uint32_t IN_HANDLE = 0x1001, OUT_HANDLE = 0x1002;
	char path[700];
	struct rep r;

	(void)snprintf(path, sizeof(path), "%s/frames.h264", work);
	(void)unlink(path);
	uint8_t *in = (uint8_t *)malloc(in_bytes);
	CHECK(in != NULL, "real frames: input buffer");
	if (!in) { return; }
	for (uint32_t y = 0; y < 48u; y++)
		for (uint32_t x = 0; x < 64u; x++) { in[y * 64u + x] = (uint8_t)(16 + ((x * 5u + y * 3u) & 0xdfu)); }
	memset(in + 64u * 48u, 128, 64u * 24u);
	CHECK(dma_create(work, IN_HANDLE, in_bytes) == 0, "real frames: create the input DMA file");
	CHECK(dma_create(work, OUT_HANDLE, out_bytes) == 0, "real frames: create the output DMA file");
	CHECK(dma_rw(work, IN_HANDLE, 0, in, in_bytes, 1) == 0, "real frames: write the NV12 picture");
	free(in);

	FILE *f = fopen(path, "wb");
	CHECK(f != NULL, "real frames: create frames.h264");
	if (!f) { return; }
	uint8_t out[8];

	CHECK(enc_frame(fd, session, IN_HANDLE, 64, OUT_HANDLE, out_bytes, 0, 1, 20, &r, 15000) == 0,
	      "real frames: frame 0 answered");
	CHECK(r.type == N2VCD_ENC_FRAME_OK && r.status == N2VCD_E_OK,
	      "real frames: frame 0 ok (type %u status %u detail %u)", r.type, r.status, r.detail);
	CHECK(r.out_bytes > r.header_bytes && r.header_bytes > 0,
	      "real frames: frame 0 bytes=%u header_bytes=%u", r.out_bytes, r.header_bytes);
	if (r.type == N2VCD_ENC_FRAME_OK) {
		CHECK(dma_rw(work, OUT_HANDLE, 0, out, sizeof(out), 0) == 0, "real frames: read the AU prefix");
		CHECK(out[0] == 0 && out[1] == 0 && out[2] == 0 && out[3] == 1 && (out[4] & 0x1f) == 7,
		      "real frames: frame 0 starts 00 00 00 01 67 (SPS), got %02x %02x %02x %02x %02x",
		      out[0], out[1], out[2], out[3], out[4]);
		uint8_t *au = (uint8_t *)malloc(r.out_bytes);
		if (au && dma_rw(work, OUT_HANDLE, 0, au, r.out_bytes, 0) == 0) {
			(void)fwrite(au, 1, r.out_bytes, f);
		} else {
			CHECK(0, "real frames: read frame 0 for the stream");
		}
		free(au);
	}

	CHECK(enc_frame(fd, session, IN_HANDLE, 64, OUT_HANDLE, out_bytes, 1000, 0, 21, &r, 15000) == 0,
	      "real frames: frame 1 answered");
	CHECK(r.type == N2VCD_ENC_FRAME_OK && r.status == N2VCD_E_OK && r.out_bytes > 0,
	      "real frames: frame 1 ok (type %u status %u bytes %u)", r.type, r.status, r.out_bytes);
	CHECK(!r.key_frame || r.header_bytes > 0,
	      "real frames: a key frame carries parameter sets (key %u header %u)", r.key_frame, r.header_bytes);
	if (r.type == N2VCD_ENC_FRAME_OK) {
		uint8_t *au = (uint8_t *)malloc(r.out_bytes);
		if (au && dma_rw(work, OUT_HANDLE, 0, au, r.out_bytes, 0) == 0) {
			(void)fwrite(au, 1, r.out_bytes, f);
		} else {
			CHECK(0, "real frames: read frame 1 for the stream");
		}
		free(au);
	}
	(void)fclose(f);
}

/* FP is test-only: see p03_absent.c. */

int
main(int argc, char **argv)
{
	if (argc != 4) {
		fprintf(stderr, "usage: t_real VCODEC_BIN WORKDIR VENDOR_DIR\n");
		return 2;
	}
	const char *bin = argv[1];
	const char *work = argv[2];
	const char *vendor = argv[3];
	char sock[600], log[700], dma[600];
	(void)snprintf(sock, sizeof(sock), "%s/vcodecd-real.sock", work);
	(void)snprintf(log, sizeof(log), "%s/vcodecd-real.log", work);
	(void)snprintf(dma, sizeof(dma), "%s/dma", work);
	(void)mkdir(work, 0755);
	(void)mkdir(dma, 0755);

	if (start_daemon(bin, sock, work, vendor, log) != 0) {
		printf("FAIL cannot fork the daemon\n");
		return 1;
	}
	int fd = cli_connect(sock);
	CHECK(fd >= 0, "real vhost: connect to the daemon");
	if (fd >= 0) {
		struct rep r;
		CHECK(hello(fd, 1, &r) == 0, "real vhost: HELLO answered");
		CHECK(r.type == N2VCD_HELLO_OK && r.version == N2VCD_VERSION && (r.enc_codecs & 1u) != 0,
		      "real vhost: HELLO_OK v%u enc=0x%x (type %u)", r.version, r.enc_codecs, r.type);
		/* Let the hosted vpud finish its guest init before the first AP message. */
		sleep_ms(2500);

		CHECK(open_session(fd, 2, &r, 12000) == 0,
		      "real vhost: OPEN answered within 12 s");
		int ok = r.type == N2VCD_OPEN_OK && r.status == N2VCD_E_OK;
		int guest_stop = r.type == N2VCD_ERROR && r.status == N2VCD_E_GUEST;
		int known_error = r.type == N2VCD_ERROR &&
		                  (r.status == N2VCD_E_GUEST || r.status == N2VCD_E_HW ||
		                   r.status == N2VCD_E_ARGS || r.status == N2VCD_E_UNSUPPORTED ||
		                   r.status == N2VCD_E_NOMEM);
		CHECK(ok || known_error,
		      "real vhost: OPEN is OPEN_OK or a documented ERROR (type %u status %u)", r.type, r.status);
		CHECK(!ok || r.session != 0, "real vhost: OPEN_OK assigned a session");
		CHECK(!ok || (r.in_buf_bytes > 0 && r.out_buf_bytes > 0),
		      "real vhost: OPEN_OK sizes in=%u out=%u", r.in_buf_bytes, r.out_buf_bytes);
		if (ok) {
			/* The separate-header job ran against the real vendor library: it must have produced bytes and the
			 * daemon must have cached them (the first key frame depends on this cache). */
			CHECK(log_contains(log, "<separate job>") || log_contains(log, "(separate job)"),
			      "real vhost: the separate sequence-header job is logged");
			CHECK(!log_contains(log, "sequence header 0 bytes"),
			      "real vhost: the sequence-header job produced bytes");
			run_real_frames(fd, r.session, r.in_buf_bytes, r.out_buf_bytes, work);
			close_session(fd, r.session, 3);
		} else {
			printf("note: OPEN at IV's current milestone: status %u detail %u\n", r.status, r.detail);
		}
		if (!guest_stop) {
			CHECK(hello(fd, 4, &r) == 0 && r.type == N2VCD_HELLO_OK,
			      "real vhost: the daemon still serves after a non-fatal OPEN (type %u)", r.type);
		}
		(void)close(fd);
		if (guest_stop) {
			int status = wait_daemon_exit(5000);
			CHECK(status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 70,
			      "real vhost: guest stop exits 70 (0x%x)", status);
		} else {
			int status = stop_daemon();
			CHECK(status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0,
			      "real vhost: clean exit after SIGTERM (0x%x)", status);
		}
		CHECK(log_contains(log, "N2VCD ") && log_contains(log, "host set:"),
		      "real vhost: log has the start and host-set lines");
		CHECK(!guest_stop || log_contains(log, "fatal guest stop"),
		      "real vhost: a fatal guest stop is logged");
		CHECK(log_contains(log, vendor), "real vhost: log names the vendor directory");
		CHECK(log_contains(log, "unresolved="), "real vhost: log reports the unresolved import count");
		CHECK(log_contains(log, "unresolved=0"), "real vhost: zero unresolved imports");
		CHECK(log_contains(log, "thread=") && !log_contains(log, "thread=0("),
		      "real vhost: the thread shim table is non-empty");
		CHECK(log_contains(log, "stdio=") && !log_contains(log, "stdio=0("),
		      "real vhost: the stdio shim table is non-empty");
		CHECK(log_contains(log, "serving "), "real vhost: log says it is serving");
		CHECK(!log_has_sanitizer(log), "real vhost: no sanitizer report in the daemon log");
	} else {
		(void)stop_daemon();
	}

	printf("checks: %d, failures: %d\n", g_checks, g_fails);
	if (g_fails == 0) {
		printf("N2TEST IP-REAL PASS %d/%d\n", g_checks, g_checks);
		return 0;
	}
	printf("N2TEST IP-REAL FAIL %d of %d checks\n", g_fails, g_checks);
	return 1;
}
