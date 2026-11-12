/* SPDX-License-Identifier: MIT
 * n2mnld -- the Nord2 iOS GNSS daemon: hosts MediaTek's own positioning engine libmnl.so (embedded, see n2mnl_image.S)
 * natively on iOS, in place of Android's mnld. Flow:
 *   1. load: RELR (+base), the 300 JUMP_SLOT GOT entries -> n2shim.c, 161 self-bound PLT slots; layout check.
 *   2. /dev/gpsdl0 (O_NONBLOCK). If the GNSS block is not powered yet (QUERY_STATUS bit 0): upload the libmnl DSP code
 *      frames (N2GD_IOC_CHUNK) and run kernel ladder K=31 (ioctl N2GD_IOC(171): power-on, DSP code download, post-boot
 *      command, HOLD) -- the in-kernel boot proven on the phone (gl11a, 50/50 fragments acked).
 *   3. mtk_gps_sys_function_register(callbacks) + mtk_gps_mnl_run(cfg1, cfg2) with the mnld config (n2mnld/STATUS.md).
 *      libmnl then runs its own _BootDSP_UART_V2: the BOOT PROXY answers it with the DSP's recorded acks (0xFE31 header,
 *      0xFE32 seq k) and checks that libmnl's frames equal what the kernel uploaded; after the last fragment (and the
 *      post-boot command) the proxy turns into a pass-through and the live DSP stream reaches libmnl.
 *   4. NMEA (callback slot 0) -> FIFO /private/var/run/n2gnss.nmea (n2locbridge -> locationd). Engine text log
 *      (slot 1) -> DIR/n2mnld.log. Console lines "N2MNLD ...".
 * usage: n2mnld [--gate F] [--dir D] [--dev P] [--nmea P] [--delay S] [--kboot K|0] [--no-proxy] [--poll-ms N]
 *               [--run-seconds N] [--mnl-log 0|1] [--trace-seconds N (default 10, 0 = off)]
 */
#include "n2sys.h"
#include "build/mnl_tab.h"
extern void *memchr(const void *, int, size_t); extern size_t strnlen(const char *, size_t);
#include "../../probe/n2gpsdl_bootdata.h"

#define N2GD_IOC(n) (0xc0000000ul | (4ul << 16) | ((unsigned long)'G' << 8) | (unsigned long)(n))
struct n2gb_chunk { uint32_t which, off, len, sum; unsigned char data[4096]; };
#define N2GD_IOC_CHUNK (0x80000000ul | ((unsigned long)sizeof(struct n2gb_chunk) << 16) | ((unsigned long)'G' << 8) | 180ul)

extern char n2mnl_base[], n2mnl_data[];
static const char *g_dev = "/dev/gpsdl0", *g_nmea = "/private/var/run/n2gnss.nmea";
/* gnssd7: gnss5/gnss6 (gnssd6, post-boot 0xFE05 passed) hung the whole phone right after the boot: the first-ever pipe TX in the
 * HOLD state. Until the kernel TX path is understood, libmnl writes are DROPPED by default (logged with id/len/bytes, counted,
 * reported to libmnl as written) and the post-boot 0xFE05 is swallowed again (= gnssd5, stable in gnss4). --tx-pass / --pass-post
 * restore the risky behaviour. --inject-post-reply FILE: when the post-boot 0xFE05 is swallowed, the DSP's answer recorded by the
 * kernel's S11 capture (gnss4 log, 253 bytes: fe2c fe62 fe28 fe29 fe30) is queued on libmnl's read side, no DSP command at all. */
static int g_devfd = -1, g_poll_ms = 10, g_mnl_log = 1, g_proxy = 1, g_swallow_post = 1, g_tx_pass = 0;
static const char *g_inject_path;
static long st_txdrop;
static int g_no_dev_read;
static int g_kboot_after, g_replay; static volatile int g_booted = 1;   /* gnssd9: --replay-boot = n2mnld itself boots the DSP over /dev/gpsdl0 (no libmnl) */
   /* gnssd9: --kboot-after S = kernel DSP boot S s AFTER libmnl started */
static const char *g_pmtk; static int g_pmtk_delay = 20;   /* gnssd8: --pmtk CMD [--pmtk-delay S]: mtk_gps_nmea_input(CMD) S s after pass-through (mnld relays PMTK the same way) */
#define N2MNL_EXP_mtk_gps_nmea_input_ 0x423250u           /* llvm-nm -D libmnl.so (sha 9a1e1be7...): T mtk_gps_nmea_input */   /* gnssd7: --no-dev-read = never read /dev/gpsdl0 after the boot (no RX DMA in HOLD): discriminator for the hang */
static long st_txfr;
static uint64_t g_t0;
static uint64_t now_ms(void) { struct timespec t; clock_gettime(D_CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u; }

/* ------------------------------------------------------------------ logging */
static pthread_mutex_t g_logm; static int g_logfd = -1; static long g_logsz;
static int g_nofsync; static volatile int g_hush;
static int g_textcon = 15, g_lna_after; static long st_kw, st_textcon;
extern unsigned long strtoul(const char *, char **, int);
static const char *g_setparam; static long st_lrd, st_lrd_bytes;
static int g_cfgfull, g_chip = 0x6893;   /* gnssd17: --cfg-full, --chip 6885|6893 */   /* gnssd16: --set-param "ID=V,..."; libmnl read() stats */   /* gnssd13: --text-console N, --lna-after S */   /* gnssd10: --no-fsync = never fsync the log; g_hush = no log I/O at all (console/file) while the pipe boot runs */
static char g_logpath[512];
static void logline(const char *s, int console)
{
	size_t n = strlen(s);
	if (g_hush) return;
	pthread_mutex_lock(&g_logm);
	if (console) {
		int fd = open("/dev/console", D_O_WRONLY | D_O_NONBLOCK | D_O_NOCTTY);
		if (fd >= 0) { write(fd, s, n); write(fd, "\n", 1); close(fd); }
	}
	if (g_logfd >= 0) {
		if (g_logsz > (4l << 20)) {                       /* keep at most ~8 MB: n2mnld.log + n2mnld.log.1 */
			close(g_logfd); char old[530]; snprintf(old, sizeof old, "%s.1", g_logpath); rename(g_logpath, old);
			g_logfd = open(g_logpath, D_O_WRONLY | D_O_CREAT | D_O_TRUNC | D_O_APPEND, 0644); g_logsz = 0;
		}
		if (g_logfd >= 0) {
			char ts[32]; int k = snprintf(ts, sizeof ts, "%7.3f ", (double)(now_ms() - g_t0) / 1000.0);
			write(g_logfd, ts, (size_t)k); write(g_logfd, s, n); write(g_logfd, "\n", 1); g_logsz += (long)n + k + 1;
			if (console && !g_nofsync && now_ms() - g_t0 < 240000) fsync(g_logfd);   /* gnssd7: console lines of the first 4 min reach the disk at once (hang forensics) */
		}
	}
	pthread_mutex_unlock(&g_logm);
}
void n2_log_raw(const char *s, size_t n)          /* fault handler: no lock, no allocation */
{
	if (g_logfd >= 0) { write(g_logfd, s, n); write(g_logfd, "\n", 1); }
	int fd = open("/dev/console", D_O_WRONLY | D_O_NONBLOCK | D_O_NOCTTY);
	if (fd >= 0) { write(fd, s, n); write(fd, "\n", 1); close(fd); }
}
void n2_log(const char *f, ...)
{
	char b[600]; int k = snprintf(b, sizeof b, "N2MNLD "); va_list a; va_start(a, f); vsnprintf(b + k, sizeof b - (size_t)k, f, a); va_end(a);
	logline(b, 1);
}
void n2_logq(const char *f, ...)
{
	char b[1200]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a);
	logline(b, 0);
}

/* ------------------------------------------------------------------ image */
static int load_image(void)
{
	char *base = n2mnl_base;
	if ((uintptr_t)(n2mnl_data - n2mnl_base) != N2MNL_DATA_START || ((uintptr_t)n2mnl_data & 0x3fff)) {
		n2_log("FATAL image layout: data - text = %#lx (want %#x), data %p", (long)(n2mnl_data - n2mnl_base), N2MNL_DATA_START, (void *)n2mnl_data);
		return -1;
	}
	for (size_t i = 0; i < sizeof n2mnl_relr / sizeof n2mnl_relr[0]; i++) *(uint64_t *)(void *)(base + n2mnl_relr[i]) += (uint64_t)(uintptr_t)base;
	int miss = 0;
	for (size_t i = 0; i < sizeof n2mnl_jumps / sizeof n2mnl_jumps[0]; i++) {
		void *f = n2_shim_lookup(n2mnl_imports[n2mnl_jumps[i].imp]);
		if (!f) { if (miss++ < 10) n2_log("FATAL no shim for import %s", n2mnl_imports[n2mnl_jumps[i].imp]); continue; }
		*(void **)(void *)(base + n2mnl_jumps[i].got) = n2_trace_wrap(n2mnl_imports[n2mnl_jumps[i].imp], f);   /* n2trace.c thunk */
	}
	if (miss) return -1;
	for (int i = 0; i < N2MNL_NSELF; i++) *(uint64_t *)(void *)(base + n2mnl_selfb[i].got) = (uint64_t)(uintptr_t)base + n2mnl_selfb[i].value;
	n2_log("libmnl image ok: base %p, %zu RELR, %zu GOT slots -> %zu shims, %d self-bound, %d TPIDR_EL0 canary reads -> TPIDRRO_EL0 (libmnl sha %.16s)", (void *)base,
	    sizeof n2mnl_relr / sizeof n2mnl_relr[0], sizeof n2mnl_jumps / sizeof n2mnl_jumps[0], sizeof n2mnl_imports / sizeof n2mnl_imports[0], N2MNL_NSELF, N2MNL_TPIDR_PATCHED, N2MNL_SHA256);
	uint64_t tsd, cpu; __asm__ volatile("mrs %0, TPIDRRO_EL0" : "=r"(tsd)); __asm__ volatile("mrs %0, TPIDR_EL0" : "=r"(cpu));
	n2_log("thread registers: TPIDRRO_EL0 (TSD base, libmnl canary base) %#llx%s, canary word [+0x28] %#llx; TPIDR_EL0 %#llx",
	    (unsigned long long)tsd, (tsd & 7) ? " LOW BITS SET: canary may move with the CPU" : "", tsd ? (unsigned long long)*(volatile uint64_t *)(uintptr_t)(tsd + 0x28) : 0ull, (unsigned long long)cpu);
	return 0;
}

/* ------------------------------------------------------------------ MTK binary frames (AA F0 <stuffed len id payload cks> AA 0F) */
static int unstuff(const unsigned char *in, size_t n, unsigned char *out, size_t cap)
{
	size_t k = 0;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = in[i];
		if (c == 0xde && i + 1 < n) { c = in[i + 1] == 0xdf ? 0xaa : 0xde; i++; }
		if (k >= cap) return -1;
		out[k++] = c;
	}
	return (int)k;
}
/* id and payload of one whole frame b[0..n), -1 if b is not exactly one frame with a good checksum */
static int frame_decode(const unsigned char *b, size_t n, unsigned *id, unsigned char *pl, int *pln)
{
	if (n < 10 || b[0] != 0xaa || b[1] != 0xf0 || b[n - 2] != 0xaa || b[n - 1] != 0x0f) return -1;
	unsigned char u[2048]; int k = unstuff(b + 2, n - 4, u, sizeof u);
	if (k < 6) return -1;
	unsigned sum = 0; for (int i = 0; i < k - 2; i++) sum += u[i];
	if ((sum & 0xffff) != (unsigned)(u[k - 2] | (u[k - 1] << 8))) return -1;
	*id = u[2] | (u[3] << 8); *pln = k - 6; if (pl) memcpy(pl, u + 4, (size_t)(k - 6));
	return 0;
}
static size_t frame_make(unsigned id, const unsigned char *pl, int pln, unsigned char *out)
{
	unsigned char body[64]; int k = 0; unsigned len = (unsigned)pln + 4, sum = 0;
	body[k++] = (unsigned char)len; body[k++] = (unsigned char)(len >> 8); body[k++] = (unsigned char)id; body[k++] = (unsigned char)(id >> 8);
	for (int i = 0; i < pln; i++) body[k++] = pl[i];
	for (int i = 0; i < k; i++) sum += body[i];
	body[k++] = (unsigned char)sum; body[k++] = (unsigned char)(sum >> 8);
	size_t o = 0; out[o++] = 0xaa; out[o++] = 0xf0;
	for (int i = 0; i < k; i++) {
		if (body[i] == 0xaa) { out[o++] = 0xde; out[o++] = 0xdf; } else if (body[i] == 0xde) { out[o++] = 0xde; out[o++] = 0xe0; } else out[o++] = body[i];
	}
	out[o++] = 0xaa; out[o++] = 0x0f; return o;
}

/* ------------------------------------------------------------------ RX statistics (frames from the DSP) */
static long st_rx, st_tx, st_rxfr, st_rxbad, st_nmea, st_rd_err;
static unsigned st_ids[16], st_idn[16];
static unsigned char rxf[2048]; static int rxn = -1, rxprev = -1;
static void rx_stat(const unsigned char *b, ssize_t n)
{
	for (ssize_t i = 0; i < n; i++) {
		int c = b[i];
		if (rxprev == 0xaa && c == 0xf0) rxn = 0;
		else if (rxprev == 0xaa && c == 0x0f && rxn > 0) {
			unsigned char w[2052]; w[0] = 0xaa; w[1] = 0xf0; memcpy(w + 2, rxf, (size_t)rxn - 1); w[rxn + 1] = 0xaa; w[rxn + 2] = 0x0f;
			unsigned id; int pln;
			if (frame_decode(w, (size_t)rxn + 3, &id, 0, &pln) == 0) {
				st_rxfr++;
				{ static int k; if (st_txfr > 0 && k < 64) { k++; n2_log("DSP frame after libmnl's first write: id %#x, %d payload bytes (rx frame #%ld)", id, pln, st_rxfr); } }
				int j; for (j = 0; j < 16 && st_idn[j] && st_ids[j] != id; j++) { }
				if (j < 16) { st_ids[j] = id; st_idn[j]++; }
			} else st_rxbad++;
			rxn = -1;
		} else if (rxn >= 0) { if (rxn < (int)sizeof rxf) rxf[rxn++] = (unsigned char)c; else rxn = -1; }
		rxprev = c;
	}
}

/* ------------------------------------------------------------------ the boot proxy */
enum { PX_BOOT, PX_POST, PX_RUN };
static pthread_mutex_t g_pm; static int px_state = PX_BOOT;
static unsigned char inj[4096]; static size_t inj_len;
static int px_hdr, px_comd4, px_frag, px_diff, px_other;
static const unsigned char ack_fe31[20] = { 0xaa, 0xf0, 0x0e, 0x00, 0x31, 0xfe, 0x00, 0x00, 0x13, 0x19, 0, 0, 0, 0, 0, 0, 0x69, 0x01, 0xaa, 0x0f };  /* gl11a */
static void inject(const unsigned char *b, size_t n) { if (inj_len + n <= sizeof inj) { memcpy(inj + inj_len, b, n); inj_len += n; } }
static int blob_index(const unsigned char *b, size_t n)
{
	for (int i = 0; i < N2B_NFRAMES; i++) if (n2b_off[i][1] == n && !memcmp(n2b_data + n2b_off[i][0], b, n)) return i;
	return -1;
}
static void px_to_run(const char *why)
{
	px_state = PX_RUN;
	n2_log("boot proxy -> pass-through (%s): header acks %d, COMD4 %d, fragments acked %d/50, frames unlike the kernel blob %d, other %d",
	    why, px_hdr, px_comd4, px_frag, px_diff, px_other);
}
static void ack_frag(int seq)
{
	unsigned char pl[2] = { (unsigned char)seq, (unsigned char)(seq >> 8) }, f[32];
	inject(f, frame_make(0xfe32, pl, 2, f));
}
/* called with g_pm held, px_state != PX_RUN. Returns 1 if the frame was consumed (not sent to the device). */
static int px_frame(const unsigned char *b, size_t n)
{
	int i = blob_index(b, n);
	if (px_state == PX_POST) {
		/* gnssd6: gnss4 showed libmnl idle after the boot (0 writes, no NMEA): it waits for the DSP's answer to ITS post-boot 0xFE05
		 * (DSP version check "MT3332 DSPver CK"), which the kernel's S11 capture had consumed. Now libmnl's own 0xFE05 goes to the
		 * DSP (sent twice in total: the S11 boot loop already sends COMD4 0xFE05 up to 3 times) and the reply reaches libmnl. */
		if (i == 52) {
			if (g_swallow_post) {
				if (g_inject_path) {
					int fd = open(g_inject_path, D_O_RDONLY); unsigned char rb[2048]; ssize_t k = fd >= 0 ? read(fd, rb, sizeof rb) : -1; if (fd >= 0) close(fd);
					if (k > 0) { inject(rb, (size_t)k); n2_log("post-boot reply INJECTED from %s: %zd recorded bytes (no DSP command)", g_inject_path, k); }
					else n2_log("post-boot reply file %s unreadable (errno %d): nothing injected", g_inject_path, *__error());
				}
				px_to_run("post-boot command 0xFE05 swallowed: the kernel already sent it"); return 1;
			}
			px_to_run("post-boot command 0xFE05 PASSED to the DSP (gnssd6): libmnl gets the DSP's own answer"); return 0;
		}
		px_to_run("first runtime frame"); return 0;
	}
	if (i == 0) { px_hdr++; inject(ack_fe31, sizeof ack_fe31); return 1; }
	if (i == 1) { px_comd4++; return 1; }
	if (i >= 2 && i <= 51) {
		ack_frag(i - 1); px_frag++;
		if (i == 51) { px_state = PX_POST; n2_log("boot proxy: libmnl sent all 50 fragments (identical to the kernel blob)"); }
		return 1;
	}
	unsigned id; unsigned char pl[2048]; int pln;
	if (frame_decode(b, n, &id, pl, &pln) == 0) {
		if (id == 0xfe08) { if (px_diff++ < 3) n2_log("BOOTDIFF header (%zu bytes) differs from the kernel blob; acked anyway (DSP runs the kernel's copy)", n); px_hdr++; inject(ack_fe31, sizeof ack_fe31); return 1; }
		if (id == 0xfe05 && pln == 2) { px_comd4++; return 1; }
		if (id == 0xfe0c && pln >= 2) {
			int seq = pl[0] | (pl[1] << 8);
			if (px_diff++ < 3) n2_log("BOOTDIFF fragment seq %d (%zu bytes) differs from the kernel blob; acked anyway", seq, n);
			ack_frag(seq); px_frag++;
			if (seq >= 50) px_state = PX_POST;
			return 1;
		}
		if (px_other++ < 5) n2_log("boot proxy: frame id %#x (%zu bytes) during boot passed to the device", id, n);
		return 0;
	}
	if (px_other++ < 5) n2_log("boot proxy: non-frame write of %zu bytes during boot passed to the device", n);
	return 0;
}

/* ------------------------------------------------------------------ data link ops for libmnl (n2shim.c routes cfg2's fd here) */
static ssize_t dev_write_all(const unsigned char *b, size_t n)
{
	size_t w = 0;
	while (w < n) {
		ssize_t r = write(g_devfd, b + w, n - w);
		if (r > 0) { w += (size_t)r; continue; }
		int e = *__error();
		if (e == D_EAGAIN || e == D_EINTR) { usleep(2000); continue; }
		static int nerr; if (nerr++ < 10) n2_log("device write errno %d after %zu of %zu bytes", e, w, n);
		return -n2_d2l_errno(e);
	}
	return (ssize_t)n;
}
static ssize_t op_write(const void *buf, size_t n)
{
	const unsigned char *b = buf;
	pthread_mutex_lock(&g_pm);
	if (g_proxy && px_state != PX_RUN) {
		int eaten = px_frame(b, n);
		pthread_mutex_unlock(&g_pm);
		if (eaten) return (ssize_t)n;
	} else pthread_mutex_unlock(&g_pm);
	if (!g_tx_pass) {                                        /* gnssd7: never reaches the kernel TX path */
		st_txdrop++; st_txfr++;
		if (st_txdrop <= 30) {
			unsigned id = 0; int pln = 0; char hx[3 * 24 + 1]; int q = 0;
			for (size_t i = 0; i < n && i < 24; i++) q += snprintf(hx + q, sizeof hx - (size_t)q, "%02x ", b[i]);
			n2_log("libmnl write #%ld DROPPED (not sent to the DSP): %zu bytes, id %#x: %s", st_txdrop, n, frame_decode(b, n, &id, 0, &pln) ? 0u : id, hx);
		}
		return (ssize_t)n;
	}
	ssize_t r = dev_write_all(b, n);
	if (r > 0) { st_tx += r; st_txfr++; if (st_txfr <= 200) { unsigned id2 = 0; int pln2 = 0; char hx[3 * 32 + 1]; int q = 0; for (size_t i = 0; i < n && i < 32; i++) q += snprintf(hx + q, sizeof hx - (size_t)q, "%02x ", b[i]);
		n2_log("libmnl frame #%ld SENT to the DSP: %zd bytes, id %#x: %s", st_txfr, r, frame_decode(b, n, &id2, 0, &pln2) ? 0u : id2, hx); } if (st_txfr <= 5) { unsigned id; int pln; n2_logq("tx frame #%ld %zu bytes id %#x", st_txfr, n, frame_decode(b, n, &id, 0, &pln) ? 0 : id); } }
	return r;
}
static ssize_t op_read(void *buf, size_t n, int nonblock)
{
	for (;;) {
		pthread_mutex_lock(&g_pm);
		if (inj_len) {
			size_t k = inj_len < n ? inj_len : n; memcpy(buf, inj, k); memmove(inj, inj + k, inj_len - k); inj_len -= k;
			pthread_mutex_unlock(&g_pm); return (ssize_t)k;
		}
		int run = !g_proxy || px_state == PX_RUN;
		pthread_mutex_unlock(&g_pm);
		if (run) {
			/* always non-blocking: the driver holds its lock during a blocking read (up to 1 s), which would stall writes */
			if (g_no_dev_read || !g_booted) { if (nonblock) return -11; usleep((useconds_t)g_poll_ms * 1000u); continue; }
			ssize_t r = read(g_devfd, buf, n);
			if (r > 0) { if (!st_rx) n2_log("first DSP bytes from /dev/gpsdl0: %zd (live data link)", r); st_rx += r; rx_stat(buf, r);
				st_lrd++; st_lrd_bytes += r;
				if (st_lrd <= 40 || st_lrd % 500 == 0) { const unsigned char *q = buf; n2_log("libmnl read #%ld got %zd B (asked %zu): %02x %02x %02x %02x %02x %02x %02x %02x", st_lrd, r, n, q[0], r > 1 ? q[1] : 0, r > 2 ? q[2] : 0, r > 3 ? q[3] : 0, r > 4 ? q[4] : 0, r > 5 ? q[5] : 0, r > 6 ? q[6] : 0, r > 7 ? q[7] : 0); }
				return r; }
			int e = r < 0 ? *__error() : D_EAGAIN;
			if (e != D_EAGAIN && e != D_EINTR) {
				if (st_rd_err++ < 10) n2_log("device read errno %d%s", e, e == 5 ? " (data link disabled by the kernel guard)" : "");
				usleep(200000); return -n2_d2l_errno(e);
			}
		}
		if (nonblock) return -11;
		usleep((useconds_t)g_poll_ms * 1000u);
	}
}
static int op_ioctl(unsigned long req, void *arg)
{
	static int seen[32];
	unsigned n = (unsigned)req;
	(void)arg;
	switch (n) {
	case 6: case 7: case 8: case 9: case 10: case 20:        /* compiled out on MT6893 (gps_each_device.c) -> EINVAL, libmnl carries on */
		return -22;
	case 13: case 14: case 15: {                             /* re-encoded for the XNU driver: _IOWR('G', n, uint32_t) */
		uint32_t v = 0; int r = ioctl(g_devfd, N2GD_IOC(n), &v);
		if (n < 32 && seen[n]++ < 2) n2_log("libmnl ioctl %u -> N2GD_IOC(%u) rc %d errno %d value %#x", n, n, r, r ? *__error() : 0, v);
		return r ? -n2_d2l_errno(*__error()) : 0;
	}
	case 11: case 12: case 18: case 19:                      /* CO_CLOCK_FLAG, TRIGGER_ASSERT, HW_SUSPEND/RESUME: no-ops here */
		if (seen[n]++ < 2) n2_log("libmnl ioctl %u answered 0 locally", n);
		return 0;
	}
	if (n < 32 && seen[n]++ < 2) n2_log("libmnl ioctl %#lx unsupported -> EINVAL", req);
	return -22;
}
static const struct n2_dev_ops dev_ops = { op_read, op_write, op_ioctl };

/* ------------------------------------------------------------------ kernel DSP boot (q10jr11 / q10jfg, patch 0364) */
static int kboot(int k)
{
	static struct n2gb_chunk c; uint32_t sum = 0, total = (uint32_t)sizeof n2b_data, v = 0;
	if (ioctl(g_devfd, N2GD_IOC(13), &v) == 0 && (v & 1)) { n2_log("GNSS already powered (status %#x): DSP boot skipped (daemon restart)", v); return 0; }
	for (uint32_t i = 0; i < total; i++) sum = sum * 31u + n2b_data[i];
	for (uint32_t o = 0; o < total; o += 4096) {
		c.which = 0; c.off = o; c.len = total - o < 4096 ? total - o : 4096; c.sum = 0; memcpy(c.data, n2b_data + o, c.len);
		if (ioctl(g_devfd, N2GD_IOC_CHUNK, &c)) { n2_log("kboot chunk @%u errno %d", o, *__error()); return -1; }
	}
	uint32_t *h = (uint32_t *)(void *)c.data; h[0] = N2B_NFRAMES; h[1] = total; h[2] = sum;
	for (int i = 0; i < N2B_NFRAMES; i++) { h[3 + 2 * i] = n2b_off[i][0]; h[4 + 2 * i] = n2b_off[i][1]; }
	c.which = 1; c.off = 0; c.len = (uint32_t)(4 * (3 + 2 * N2B_NFRAMES)); c.sum = sum;
	if (ioctl(g_devfd, N2GD_IOC_CHUNK, &c)) { n2_log("kboot table errno %d", *__error()); return -1; }
	n2_log("kboot: uploaded %u bytes, %d frames, sum %#x; kernel ladder K=%d (ioctl %d) ...", total, N2B_NFRAMES, sum, k, 140 + k);
	v = 0xffffffffu; int r = ioctl(g_devfd, N2GD_IOC(140 + k), &v);
	n2_log("kboot K=%d rc %d errno %d result %#x (done=%u step=%u)", k, r, r ? *__error() : 0, v, v & 1, (v >> 8) & 0xff);
	if (r) return -1;
	if (v & 0x01000000u) return 1;                           /* Wi-Fi/conninfra guard refused: retry later */
	return (v & 1) ? 0 : -1;
}

/* gnssd9 --replay-boot: after ladder K=32/35 (HOLD, DSP in ROM) n2mnld sends the blob frames itself through /dev/gpsdl0 (write()
 * = the same pipe TX path libmnl uses), each paced by the DSP's ack (header 0xFE31, fragment k 0xFE32 seq k, read polled every 1 ms),
 * BEFORE libmnl starts: splits "pipe path" from "libmnl running concurrently". libmnl then runs behind the boot proxy as with K=31. */
static int rp_prev = -1, rp_n = -1; static unsigned char rp_f[2052];
static int rp_wait(unsigned want_id, int want_seq, int ms)
{
	uint64_t t0 = now_ms(); unsigned char b[512];
	while ((int)(now_ms() - t0) < ms) {
		ssize_t r = read(g_devfd, b, sizeof b);
		if (r <= 0) { usleep(1000); continue; }
		for (ssize_t i = 0; i < r; i++) {
			int c = b[i];
			if (rp_prev == 0xaa && c == 0xf0) rp_n = 0;
			else if (rp_prev == 0xaa && c == 0x0f && rp_n > 0) {
				unsigned char w[2060]; w[0] = 0xaa; w[1] = 0xf0; memcpy(w + 2, rp_f, (size_t)rp_n - 1); w[rp_n + 1] = 0xaa; w[rp_n + 2] = 0x0f;
				unsigned id; unsigned char pl[2048]; int pln = 0;
				int ok = frame_decode(w, (size_t)rp_n + 3, &id, pl, &pln) == 0;
				rp_n = -1;
				if (ok && id == want_id && (want_seq < 0 || (pln >= 2 && (pl[0] | (pl[1] << 8)) == want_seq))) { rp_prev = c; return 1; }
			} else if (rp_n >= 0) { if (rp_n < (int)sizeof rp_f) rp_f[rp_n++] = (unsigned char)c; else rp_n = -1; }
			rp_prev = c;
		}
	}
	return 0;
}
static int g_quiet_boot, g_xchg;
/* gnssd11 --xchg-boot: after ladder K=41 (0591) the boot is driven frame by frame with ioctl 190: the KERNEL runs the ladder's own
 * n2gb_xchg for frame i (RX close/arm, n2gl_tx, ack poll on the uploaded blob) and returns 1 on the ack; between frames the call
 * returns to user space (the only difference left from the in-kernel S11 loop). */
static int xchg_boot(void)
{
	static const int order[] = { 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
		31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52 };
	for (unsigned j = 0; j < sizeof order / sizeof order[0]; j++) {
		uint32_t v = (uint32_t)order[j];
		int r = ioctl(g_devfd, N2GD_IOC(190), &v);
		if (r || (v & 0xffu) != 1u) { g_hush = 0; n2_log("gnssd11 xchg boot: frame %d failed (rc %d errno %d v %#x)", order[j], r, r ? *__error() : 0, v); return -1; }
	}
	return 0;
}
static int replay_boot_inner(void);
static int replay_boot(void)
{
	n2_log("gnssd9 replay: n2mnld boots the DSP through /dev/gpsdl0 (header, 50 fragments, 0xFE05), libmnl NOT started yet%s",
	    g_quiet_boot ? "; gnssd10 --quiet-boot: NO log I/O (console, file, fsync) until the boot ends" : "");
	if (g_quiet_boot) { if (g_logfd >= 0) fsync(g_logfd); usleep(200000); g_hush = 1; }
	int r = g_xchg ? xchg_boot() : replay_boot_inner();
	g_hush = 0;
	n2_log("gnssd10 replay result %d (log I/O was %s during the boot)", r, g_quiet_boot ? "OFF" : "on");
	return r;
}
static int replay_boot_inner(void)
{
	int ok = 0;
	for (int t = 0; t < 3 && !ok; t++) { if (dev_write_all(n2b_data + n2b_off[0][0], n2b_off[0][1]) < 0) return -1; ok = rp_wait(0xfe31, 0, 300); }
	n2_log("gnssd9 replay: header %s", ok ? "ACK 0xFE31" : "NO ACK");
	if (!ok) return -1;
	for (int k = 1; k <= 50; k++) {
		ok = 0;
		for (int t = 0; t < 3 && !ok; t++) { if (dev_write_all(n2b_data + n2b_off[1 + k][0], n2b_off[1 + k][1]) < 0) return -1; ok = rp_wait(0xfe32, k, 300); }
		if (k <= 3 || k % 10 == 0 || !ok) n2_log("gnssd9 replay: fragment %d/50 %s", k, ok ? "ACK 0xFE32" : "NO ACK");
		if (!ok) return -1;
	}
	if (dev_write_all(n2b_data + n2b_off[52][0], n2b_off[52][1]) < 0) return -1;
	n2_log("gnssd9 replay: 50/50 fragments acked, post-boot 0xFE05 sent through the pipe; DSP booted by the PIPE path, phone alive");
	return 0;
}

/* gnssd9: the deferred kernel DSP boot, while libmnl's threads run (reads are held back until it is done) */
static void *kboot_late(void *p)
{
	int k = *(int *)p, r;
	sleep((unsigned)g_kboot_after);
	n2_log("gnssd9: starting the deferred kernel DSP boot K=%d now (libmnl running)", k);
	for (int t = 0; (r = kboot(k)) == 1 && t < 6; t++) { n2_log("kboot refused by the conninfra guard: retry in 30 s"); sleep(30); }
	n2_log("gnssd9: deferred kernel DSP boot K=%d %s (%d); device reads enabled", k, r ? "FAILED" : "done, phone alive", r);
	g_booted = 1;
	return 0;
}

/* ------------------------------------------------------------------ NMEA out */
static int g_fifo = -1; static uint64_t g_fifo_next; static long st_fifo_lines, st_fifo_drop;
static char last_gga[100], last_rmc[100]; static int fix_q = -1, sv_used = -1, sv_view[8]; static char sv_talk[8][3];
static int snr_n, snr_max, snr_sum, snr_pos;                 /* gnssd5: C/N0 (GSV SNR fields) since the last stats line */
static char rmc_stat = '-';

static void fifo_put(const char *s, size_t n)
{
	uint64_t t = now_ms();
	if (g_fifo < 0) {
		if (t < g_fifo_next) { st_fifo_drop++; return; }
		g_fifo_next = t + 2000;
		unsigned char st[160];
		if (stat(g_nmea, st) != 0) { if (mkfifo(g_nmea, 0644) != 0) { st_fifo_drop++; return; } }
		else { uint16_t m; memcpy(&m, st + 4, 2); if ((m & 0170000) != 0010000) { static int w; if (!w++) n2_log("%s exists and is not a FIFO: NMEA not forwarded", g_nmea); st_fifo_drop++; return; } }
		g_fifo = open(g_nmea, D_O_WRONLY | D_O_NONBLOCK);
		if (g_fifo < 0) { st_fifo_drop++; return; }                   /* ENXIO: no reader (n2locbridge) yet */
		n2_log("NMEA FIFO %s opened (reader present)", g_nmea);
	}
	ssize_t r = write(g_fifo, s, n);
	if (r == (ssize_t)n) { st_fifo_lines++; return; }
	st_fifo_drop++;
	if (r < 0 && *__error() != D_EAGAIN) { close(g_fifo); g_fifo = -1; n2_log("NMEA FIFO reader went away (errno %d)", *__error()); }
}
static int field(const char *s, int k, char *out, int cap)      /* k-th comma field of an NMEA sentence */
{
	int i = 0; while (k > 0 && *s) { if (*s++ == ',') k--; }
	if (k) { out[0] = 0; return 0; }
	while (*s && *s != ',' && *s != '*' && i < cap - 1) out[i++] = *s++;
	out[i] = 0; return i;
}
static void nmea_line(const char *s)
{
	char f[24];
	if (st_nmea == 1) n2_log("first NMEA from libmnl: %.90s", s);
	if (strlen(s) < 7 || s[0] != '$') return;
	const char *t = s + 3;
	if (!strncmp(t, "GGA", 3)) { snprintf(last_gga, sizeof last_gga, "%.95s", s); field(s, 6, f, sizeof f); fix_q = f[0] ? f[0] - '0' : -1; field(s, 7, f, sizeof f); sv_used = f[0] ? (f[0] - '0') * (f[1] ? 10 : 1) + (f[1] ? f[1] - '0' : 0) : -1; }
	else if (!strncmp(t, "RMC", 3)) { snprintf(last_rmc, sizeof last_rmc, "%.95s", s); field(s, 2, f, sizeof f); rmc_stat = f[0] ? f[0] : '-'; }
	else if (!strncmp(t, "GSV", 3)) {
		int j; for (j = 0; j < 8 && sv_talk[j][0] && strncmp(sv_talk[j], s + 1, 2); j++) { }
		if (j < 8) { sv_talk[j][0] = s[1]; sv_talk[j][1] = s[2]; field(s, 3, f, sizeof f); int v = 0; for (char *p = f; *p >= '0' && *p <= '9'; p++) v = v * 10 + (*p - '0'); sv_view[j] = v; }
		for (int k = 7; k <= 19; k += 4) {                       /* sat blocks: PRN, elev, azim, SNR (dB-Hz, empty = not tracked) */
			if (!field(s, k, f, sizeof f)) continue;
			int v = 0; for (char *p = f; *p >= '0' && *p <= '9'; p++) v = v * 10 + (*p - '0');
			snr_n++; if (v > 0) { snr_pos++; snr_sum += v; if (v > snr_max) snr_max = v; }
		}
	}
}
static char nm_acc[1024]; static size_t nm_len;
static long st_text, st_notify; static unsigned notify_n[32]; static long notify_last = -1;
static void nmea_feed(const char *buf, size_t n)
{
	fifo_put(buf, n);
	for (size_t i = 0; i < n; i++) {
		char c = buf[i];
		if (c == '\n' || c == '\r') { if (nm_len) { nm_acc[nm_len] = 0; st_nmea++; nmea_line(nm_acc); nm_len = 0; } }
		else if (c && nm_len < sizeof nm_acc - 1) nm_acc[nm_len++] = c;
	}
	if (nm_len && nm_acc[0] == '$' && memchr(nm_acc, '*', nm_len)) { fifo_put("\r\n", 2); nm_acc[nm_len] = 0; st_nmea++; nmea_line(nm_acc); nm_len = 0; }
}
/* gnssd5: callback slot 0 is NOT NMEA: libmnl's mnl main thread passes table[0] to mtk_gps_run() as the engine NOTIFICATION
 * callback (int (*)(int type); libmnl 0x410580..0x410594 -> mtk_gps_run 0x416314 stores it at 0x62c6f0; calls with type 3, 4, 9,
 * 10 at 0x3f3960, 0x5241f8, 0x3f79a4, 0x3f8bc0, 0x3fe4d8). gnssd4 treated it as (buf, len) -> strlen(9) -> SIGSEGV (gnss3 run). */
static long cb_notify(long type)
{
	st_notify++;
	unsigned t = (unsigned)type;
	if (t < 32) notify_n[t]++;
	if (type != notify_last || st_notify <= 5) { static int k; if (k++ < 60) n2_log("libmnl notification type %ld (#%ld)", type, st_notify); }
	notify_last = type;
	return 0;
}
/* slot 1 = mtk_gps_sys_nmea_output_to_app(buf, len) (libmnl 0x410194 formats and calls it): NMEA sentences and engine text */
static pthread_mutex_t g_tm;
static long cb_text_locked(const char *buf, long len);
static long cb_text(const char *buf, long len) { pthread_mutex_lock(&g_tm); long r = cb_text_locked(buf, len); pthread_mutex_unlock(&g_tm); return r; }
static long cb_text_locked(const char *buf, long len)
{
	if (!buf || (uintptr_t)buf < 0x10000) return 0;
	size_t n = len > 0 && len < 4096 ? (size_t)len : strnlen(buf, 4096);
	st_text++;
	size_t i = 0; while (i < n && (buf[i] == '\r' || buf[i] == '\n' || buf[i] == ' ')) i++;
	if (i < n && buf[i] == '$') { nmea_feed(buf + i, n - i); return 0; }
	if (!g_mnl_log) return 0;
	while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == 0)) n--;
	n2_logq("mnl: %.*s", (int)n, buf);
	{	/* gnssd13: first g_textcon lines on the console, then only lines with keywords (max 20 per 10 s) */
		static const char *const kw[] = { "track", " sv", "sat", "fix", "search", "ttff", "acq", "antenna", "lna", "clock", "time", "position",
			"nmea", "error", "err", "fail", "timeout", "wait", "pmtk", "ack", "epo", "dsp", "freq", "tcxo", "rtc", "agc", "cn0", "snr", "l1", "l5" };
		int hit = 0;
		for (unsigned k = 0; k < sizeof kw / sizeof kw[0] && !hit; k++) {
			size_t kl = strlen(kw[k]);
			for (size_t a = 0; a + kl <= n && !hit; a++) { size_t b = 0; while (b < kl && (buf[a + b] | 0x20) == kw[k][b]) b++; if (b == kl) hit = 1; }
		}
		if (hit) st_kw++;
		static uint64_t win; static int inwin; uint64_t nowm = now_ms();
		if (nowm - win > 10000) { win = nowm; inwin = 0; }
		if (st_text <= g_textcon || (hit && inwin < 20)) { if (st_text > g_textcon) inwin++; st_textcon++; n2_log("libmnl text #%ld: %.*s", st_text, (int)(n > 200 ? 200 : n), buf); }
	}
	return 0;
}
/* gnssd14: the sys callback slots decoded from libmnl (mtk_gps_sys_function_register 0x415f20 copies table[k] to the globals that its
 * call sites load through GOT 0x576000+off): slot 6 (GOT 0x5766f0) = message to mnld (u32 type, u32 len, const void *data): 0x3e81b4
 * passes a u16 type + a malloc'd copy, 0x3f8088 type 0 + an encoded text, 0x420240 type 9 len 1 byte 1; slot 8 (GOT 0x576b60) = encode
 * (const char *src, char *dst, len): 0x3f8070 encodes a stack string into a 50-byte buffer that is then sent with slot 6; slot 16
 * (GOT 0x576b18) = clock-extension hook (mtk_gps_clk_extention_set(w0, w1) calls it first). The old stubs returned 0 and left slot 8's
 * dst unwritten. Now: slot 8 copies src to dst (NUL-terminated); every slot logs its call on the console (first 20 each) with a
 * printable dump of the pointer arguments. */
static void cb_dump(char *o, size_t cap, long p)
{
	o[0] = 0;
	if ((unsigned long)p < 0x100000000ul || (unsigned long)p > 0x0000fffffffffffful) return;
	const unsigned char *q = (const unsigned char *)p; size_t k = 0;
	for (int i = 0; i < 48 && k + 4 < cap; i++) { unsigned c = q[i]; if (!c) break; o[k++] = (c >= 32 && c < 127) ? (char)c : '.'; }
	o[k] = 0;
}
static long st_s6, st_s6nmea; static long s6_type[16];
static long cb_any(int k, long a, long b, long c, long d)
{
	static int n[20];
	/* gnssd15: slot 6 = the engine's message channel to mnld (type, len, data); gnss18n showed the engine's own sentences there
	 * ('$PMTK010,001' startup, '$PMTK764,...'), not on slot 1: every '$' sentence of type 0 now feeds the NMEA path (FIFO -> locbridge,
	 * FIX SUMMARY parser); all messages counted per type, the first 200 logged on the console. */
	if (k == 6) {
		st_s6++; if ((unsigned long)a < 16) s6_type[a]++;
		if ((unsigned long)c >= 0x100000000ul && b > 0 && b < 4096) {
			const char *q = (const char *)c; size_t L = (size_t)b; while (L && (q[L - 1] == 0 || q[L - 1] == '\n' || q[L - 1] == '\r')) L--;
			if (st_s6 <= 200) n2_log("engine msg #%ld type %ld len %ld: %.*s", st_s6, a, b, (int)(L > 180 ? 180 : L), q);
			if (a == 0 && L && q[0] == '$') { st_s6nmea++; pthread_mutex_lock(&g_tm); nmea_feed(q, L); nmea_feed("\r\n", 2); pthread_mutex_unlock(&g_tm); }
		}
		return 0;
	}
	if (k == 8 && (unsigned long)a >= 0x100000000ul && (unsigned long)b >= 0x100000000ul && c > 0 && c < 4096) {   /* encode: src -> dst */
		memcpy((void *)b, (const void *)a, (size_t)c); ((char *)b)[c] = 0;
	}
	if (k < 20 && n[k]++ < 20) {
		char da[60], db[60], dc[60], dd[60];
		cb_dump(da, sizeof da, a); cb_dump(db, sizeof db, b); cb_dump(dc, sizeof dc, c); cb_dump(dd, sizeof dd, d);
		if (dd[0]) n2_log("callback slot %d d: '%s'", k, dd);
		n2_log("callback slot %d (%#lx, %#lx, %#lx, %#lx)%s%s%s%s%s%s", k, a, b, c, d, da[0] ? " a='" : "", da, da[0] ? "'" : "",
		    db[0] ? " b='" : "", db, db[0] ? "'" : "");
		if (dc[0]) n2_log("callback slot %d data: '%s'", k, dc);
	}
	return k == 8 ? c : 0;
}
#define CBSTUB(k) static long cb_##k(long a, long b, long c, long d) { return cb_any(k, a, b, c, d); }
CBSTUB(2) CBSTUB(3) CBSTUB(4) CBSTUB(5) CBSTUB(6) CBSTUB(7) CBSTUB(8) CBSTUB(9) CBSTUB(10) CBSTUB(11) CBSTUB(12) CBSTUB(13)
CBSTUB(14) CBSTUB(15) CBSTUB(16)

static void stats(void)
{
	char ids[200]; int k = 0; ids[0] = 0;
	for (int j = 0; j < 16 && st_idn[j]; j++) k += snprintf(ids + k, sizeof ids - (size_t)k, " %04x:%u", st_ids[j] & 0xffff, st_idn[j]);
	char sv[100]; int q = 0; sv[0] = 0;
	for (int j = 0; j < 8 && sv_talk[j][0]; j++) q += snprintf(sv + q, sizeof sv - (size_t)q, " %c%c:%d", sv_talk[j][0], sv_talk[j][1], sv_view[j]);
	n2_log("t=%llus rx %ld B (%ld frames, %ld bad;%s) tx %ld B (%ld writes) | NMEA %ld lines, fifo %ld sent %ld dropped | fix %d used %d inview%s | proxy %s",
	    (unsigned long long)((now_ms() - g_t0) / 1000), st_rx, st_rxfr, st_rxbad, ids, st_tx, st_txfr, st_nmea, st_fifo_lines, st_fifo_drop, fix_q, sv_used,
	    q ? sv : " -", px_state == PX_RUN ? "pass-through" : px_state == PX_POST ? "post-boot" : "boot");
	char nt[160]; int z = 0; nt[0] = 0;
	for (int j = 0; j < 32; j++) if (notify_n[j]) z += snprintf(nt + z, sizeof nt - (size_t)z, " %d:%u", j, notify_n[j]);
	n2_log("FIX SUMMARY t=%llus: fix %s (GGA q %d, RMC %c), sats used %d, in view%s | C/N0 last 10 s: %d SNR fields, %d tracked, max %d avg %d dB-Hz | notifications%s | text %ld | libmnl writes %ld (dropped %ld)",
	    (unsigned long long)((now_ms() - g_t0) / 1000), fix_q > 0 ? "YES" : "no", fix_q, rmc_stat, sv_used, q ? sv : " -", snr_n, snr_pos, snr_max,
	    snr_pos ? snr_sum / snr_pos : 0, z ? nt : " -", st_text, st_txfr, st_txdrop);
	snr_n = snr_pos = snr_max = snr_sum = 0;
	if (last_gga[0]) n2_log("last %.90s", last_gga);
	if (last_rmc[0]) n2_log("last %.90s", last_rmc);
	pthread_mutex_lock(&g_logm); if (g_logfd >= 0) fsync(g_logfd); pthread_mutex_unlock(&g_logm);   /* gnssd4: the gnssd3 log was lost with the phone */
}

int main(int argc, char **argv)
{
	const char *gate = 0; int delay = 0, kb = 31, runsec = 0, tracesec = 10;
	g_t0 = now_ms();
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : 0;
		if (!strcmp(a, "--no-proxy")) { g_proxy = 0; continue; }
		if (!strcmp(a, "--swallow-post")) { g_swallow_post = 1; continue; }
		if (!strcmp(a, "--pass-post")) { g_swallow_post = 0; continue; }
		if (!strcmp(a, "--tx-pass")) { g_tx_pass = 1; continue; }
		if (!strcmp(a, "--no-dev-read")) { g_no_dev_read = 1; continue; }
		if (!strcmp(a, "--kboot-after") && i + 1 < argc) { g_kboot_after = atoi(argv[++i]); continue; }
		if (!strcmp(a, "--replay-boot")) { g_replay = 1; continue; }
		if (!strcmp(a, "--no-fsync")) { g_nofsync = 1; continue; }
		if (!strcmp(a, "--quiet-boot")) { g_quiet_boot = 1; continue; }
		if (!strcmp(a, "--xchg-boot")) { g_xchg = 1; g_replay = 1; continue; }
		if (!strcmp(a, "--text-console") && i + 1 < argc) { g_textcon = atoi(argv[++i]); continue; }
		if (!strcmp(a, "--lna-after") && i + 1 < argc) { g_lna_after = atoi(argv[++i]); continue; }
		if (!strcmp(a, "--set-param") && i + 1 < argc) { g_setparam = argv[++i]; continue; }
		if (!strcmp(a, "--cfg-full")) { g_cfgfull = 1; continue; }
		if (!strcmp(a, "--chip") && i + 1 < argc) { g_chip = (int)strtoul(argv[++i], 0, 16); continue; }
		if (!v) break;
		if (!strcmp(a, "--gate")) gate = v; else if (!strcmp(a, "--dir")) n2_dir = v; else if (!strcmp(a, "--dev")) g_dev = v;
		else if (!strcmp(a, "--nmea")) g_nmea = v; else if (!strcmp(a, "--delay")) delay = atoi(v); else if (!strcmp(a, "--kboot")) kb = atoi(v);
		else if (!strcmp(a, "--poll-ms")) g_poll_ms = atoi(v) > 0 ? atoi(v) : 10; else if (!strcmp(a, "--run-seconds")) runsec = atoi(v);
		else if (!strcmp(a, "--mnl-log")) g_mnl_log = atoi(v); else if (!strcmp(a, "--inject-post-reply")) g_inject_path = v; else if (!strcmp(a, "--pmtk")) g_pmtk = v; else if (!strcmp(a, "--pmtk-delay")) g_pmtk_delay = atoi(v); else if (!strcmp(a, "--trace-seconds")) tracesec = atoi(v);
		i++;
	}
	if (gate && access(gate, 0) != 0) return 0;
	pthread_mutex_init(&g_logm, 0); pthread_mutex_init(&g_pm, 0); pthread_mutex_init(&g_tm, 0);
	n2_shim_init();
	mkdir(n2_dir, 0700);
	if (chdir(n2_dir) != 0) { n2_log("FATAL chdir %s errno %d", n2_dir, *__error()); return 1; }
	snprintf(g_logpath, sizeof g_logpath, "%s/n2mnld.log", n2_dir);
	g_logfd = open(g_logpath, D_O_WRONLY | D_O_CREAT | D_O_APPEND, 0644);
	n2_fault_install();
	n2_log("start gnssd17 (cfg-full %d chip %x; gnssd16 set-param %s, libmnl read log; gnssd15 slot-6", g_cfgfull, g_chip, g_setparam ? g_setparam : "-"); n2_log("start gnssd15-compatible (slot-6 engine messages -> NMEA path; gnssd14 decoded sys callbacks; gnssd13 text-console %d, lna-after %d s; gnssd12 multi-PMTK;", g_textcon, g_lna_after); n2_log("start gnssd12-compatible (multi-PMTK; gnssd11 / gnssd10 kboot-after %d s, replay-boot %d, xchg-boot %d, no-fsync %d, quiet-boot %d)", g_kboot_after, g_replay, g_xchg, g_nofsync, g_quiet_boot); n2_log("start gnssd8-compatible (tx %s, post-boot %s%s%s; dir %s, dev %s, nmea %s, kboot K=%d, proxy %d, delay %d s, import trace %d s, fault handler on)", g_tx_pass ? "PASS (writes reach the DSP)" : "DROP (libmnl writes logged, never sent)", g_swallow_post ? "swallowed" : "PASSED", g_inject_path ? " + recorded reply injected" : "", g_no_dev_read ? ", NO DEVICE READS after the boot" : "", n2_dir, g_dev, g_nmea, kb, g_proxy, delay, tracesec);
	if (load_image()) return 1;
	if (delay > 0) sleep((unsigned)delay);
	g_devfd = open(g_dev, D_O_RDWR | D_O_NONBLOCK);
	if (g_devfd < 0) { n2_log("FATAL open %s errno %d (kernel without the GNSS set / boot-arg n2gps=1?)", g_dev, *__error()); return 1; }
	if (kb > 0 && g_kboot_after > 0) { g_booted = 0; n2_log("gnssd9: kernel DSP boot K=%d deferred: it runs %d s after libmnl started (environment discriminator)", kb, g_kboot_after); }
	else if (kb > 0) {
		int r;
		for (int t = 0; (r = kboot(kb)) == 1 && t < 6; t++) { n2_log("kboot refused by the conninfra guard (Wi-Fi not up?): retry in 30 s"); sleep(30); }
		if (r) { n2_log("FATAL DSP boot failed (%d): engine not started", r); return 3; }
		if (g_replay && replay_boot()) { n2_log("FATAL gnssd9 replay boot failed: engine not started"); return 3; }
	}
	n2_l1_fd = g_devfd;
	n2_l5_fd = open("/dev/null", D_O_RDWR);
	n2_dev = &dev_ops;
	static void *table[20];
	table[0] = (void *)cb_notify; table[1] = (void *)cb_text;
	void *stubs[] = { cb_2, cb_3, cb_4, cb_5, cb_6, cb_7, cb_8, cb_9, cb_10, cb_11, cb_12, cb_13, cb_14, cb_15, cb_16 };
	for (int i = 0; i < 15; i++) table[2 + i] = stubs[i];                      /* mnld fills 0..16, 17..19 stay NULL */
	static unsigned char cfg1[0x200], cfg2[0x500];
	uint32_t one = 1, x = 0x250; uint16_t hundred = 100; uint32_t baud = 115200; uint64_t y = 0xffff6893ull;
	memcpy(cfg1 + 0x00, &one, 4); memcpy(cfg1 + 0x14, &hundred, 2); memcpy(cfg1 + 0x20, &baud, 4);
	memcpy(cfg1 + 0x2c, &x, 4); memcpy(cfg1 + 0x30, &x, 4); memcpy(cfg1 + 0x34, &y, 8); memcpy(cfg1 + 0x3c, &y, 8);
	memcpy(cfg2 + 0x280, &n2_l1_fd, 4); memcpy(cfg2 + 0x284, &n2_l5_fd, 4); memcpy(cfg2 + 0xfd, "UseCallback", 12);
	if (g_cfgfull) {
		/* gnssd17: the rest of the two structs exactly as the vendor mnld fills them before mtk_gps_mnl_run (mnld 0x430a0.. zero-init +
		 * defaults, 0x43654.. clock block, 0x44980.. chip/feature block, 0x44a30.. strings from its static config at 0x60978; values read
		 * from mnld's data segment): cfg1 +0x0c=2, +0x17=0xff, +0x18=2000, +0x1c=26000000 (26 MHz clock), +0x24=1000, +0x44=2, +0x58=1,
		 * +0x66=1; chip block: --chip 6885 = mnld's MT6885/6893 branch (+0x2c/+0x30 = 0x200, feature bit 0x80000000 from table 0x1af34,
		 * +0x34/+0x3c = 0xffff6885), --chip 6893 keeps the old 0x250 / 0xffff6893; cfg2 path strings (the shim maps every /data/vendor/gps
		 * and /vendor/etc file into the n2gnss dir). */
		uint32_t two = 2, f2000 = 2000, clk = 26000000u, t1000 = 1000, one1 = 1; uint8_t ff = 0xff, b1 = 1;
		memcpy(cfg1 + 0x0c, &two, 4); memcpy(cfg1 + 0x17, &ff, 1); memcpy(cfg1 + 0x18, &f2000, 2); memcpy(cfg1 + 0x1c, &clk, 4);
		memcpy(cfg1 + 0x24, &t1000, 4); memcpy(cfg1 + 0x44, &two, 4); memcpy(cfg1 + 0x58, &one1, 4); memcpy(cfg1 + 0x66, &b1, 1);
		if (g_chip == 0x6885) { uint32_t f0 = 0x80000200u, f1 = 0x200; uint64_t c = 0xffff6885ull;
			memcpy(cfg1 + 0x2c, &f0, 4); memcpy(cfg1 + 0x30, &f1, 4); memcpy(cfg1 + 0x34, &c, 8); memcpy(cfg1 + 0x3c, &c, 8); }
		static const struct { unsigned off, cap; const char *v; } st[] = {
			{ 0x005, 30, "/data/vendor/gps/mtkgps.dat" }, { 0x023, 30, "/data/vendor/gps/mtkgps_ofl.dat" }, { 0x041, 127, "/data/vendor/gps/" },
			{ 0x11b, 30, "/dev/ttygserial" }, { 0x139, 30, "/data/vendor/gps/" }, { 0x157, 47, "/data/vendor/gps/EPO.DAT" },
			{ 0x187, 47, "/data/vendor/gps/EPOHAL.DAT" }, { 0x3ec, 30, "/vendor/etc/" }, { 0x40a, 30, "/data/vendor/gps/" } };
		for (unsigned i = 0; i < sizeof st / sizeof st[0]; i++) { size_t L = strlen(st[i].v); if (L > st[i].cap - 1) L = st[i].cap - 1; memcpy(cfg2 + st[i].off, st[i].v, L); cfg2[st[i].off + L] = 0; }
		n2_log("gnssd17: full mnl config (mnld layout), chip profile %#x, clock 26 MHz, dirs set", g_chip);
	}
	int (*reg)(void *) = (int (*)(void *))(void *)(n2mnl_base + N2MNL_EXP_mtk_gps_sys_function_register);
	int (*run)(void *, void *) = (int (*)(void *, void *))(void *)(n2mnl_base + N2MNL_EXP_mtk_gps_mnl_run);
	n2_trace_start(tracesec);
	int r1 = reg(table);
	n2_log("mtk_gps_sys_function_register -> %d; starting mtk_gps_mnl_run", r1);
	uint64_t t_run = now_ms();
	int r2 = run(cfg1, cfg2);
	n2_log("mtk_gps_mnl_run -> %d (engine threads running)", r2);
	if (g_setparam) {                                          /* gnssd16: mtk_gps_set_param(id, &value) like mnld after mtk_gps_mnl_run */
		int (*sp)(int, void *) = (int (*)(int, void *))(void *)(n2mnl_base + 0x3fe39cu);   /* llvm-nm -D libmnl.so: T mtk_gps_set_param */
		char lst[256]; snprintf(lst, sizeof lst, "%s", g_setparam); char *q = lst;
		while (*q) { char *e = q; while (*e && *e != ',') e++; char c = *e; *e = 0;
			char *eq = strchr(q, '='); if (eq) { *eq = 0; int id = (int)strtoul(q, 0, 0); static uint32_t vals[16]; static int nv; uint32_t *vp = &vals[nv < 15 ? nv++ : 15]; *vp = (uint32_t)strtoul(eq + 1, 0, 0);
				int rr = sp(id, vp); n2_log("mtk_gps_set_param(%#x, &%u) -> %d", id, *vp, rr); }
			if (!c) break; q = e + 1; }
	}
	if (!g_booted) { static int kbk; kbk = kb; pthread_t th; if (pthread_create(&th, 0, kboot_late, &kbk)) n2_log("gnssd9: kboot thread failed"); }
	for (uint64_t last = now_ms(), nxt = 3000;;) {
		usleep(250000);
		uint64_t t = now_ms();
		pthread_mutex_lock(&g_pm);
		if (g_proxy && px_state != PX_RUN && t - t_run > 120000) px_to_run(px_hdr ? "boot did not finish in 120 s" : "libmnl did not start a DSP boot in 120 s");
		pthread_mutex_unlock(&g_pm);
		n2_trace_summary(0);
		{ static uint64_t t_run0, t_last; static int idx; static char list[1024]; static char *pm_next;
		  /* gnssd12: --pmtk "CMD1;CMD2;..." = several PMTK sentences, the first --pmtk-delay s after pass-through (or after the engine
		   * started when the boot proxy is off), then one every 3 s; '*XX' checksum and CRLF added when missing */
		  if (g_pmtk && !pm_next && !idx) { snprintf(list, sizeof list, "%s", g_pmtk); pm_next = list; }
		  if (g_pmtk && pm_next && *pm_next) { pthread_mutex_lock(&g_pm); int run = !g_proxy || px_state == PX_RUN; pthread_mutex_unlock(&g_pm);
		    if (run && !t_run0) t_run0 = t;
		    if (t_run0 && t - t_run0 >= (uint64_t)g_pmtk_delay * 1000u && (!t_last || t - t_last >= 3000u)) {
		      char *e = pm_next; while (*e && *e != ';') e++;
		      char one[200]; size_t L = (size_t)(e - pm_next) < sizeof one - 8 ? (size_t)(e - pm_next) : sizeof one - 8; memcpy(one, pm_next, L); one[L] = 0;
		      pm_next = *e ? e + 1 : e; t_last = t; idx++;
		      { char *u = strstr(one, "@UTC");                  /* gnssd13: @UTC -> current UTC "YYYY,MM,DD,hh,mm,ss" (PMTK740/741) */
		        if (u) { struct timeval tv; gettimeofday(&tv, 0); long long sec = (long long)tv.tv_sec, days = sec / 86400, sod = sec % 86400;
		          long long z = days + 719468, era = z / 146097, doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
		          long long y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
		          if (m <= 2) y++;
		          char tail[200]; snprintf(tail, sizeof tail, "%s", u + 4);
		          snprintf(u, sizeof one - (size_t)(u - one), "%04lld,%02lld,%02lld,%02lld,%02lld,%02lld%s", y, m, d, sod / 3600, (sod / 60) % 60, sod % 60, tail); } }
		      char cmd[220];
		      if (!strchr(one, '*')) { unsigned ck = 0; for (const char *q = one + (one[0] == '$'); *q; q++) ck ^= (unsigned char)*q; snprintf(cmd, sizeof cmd, "%s*%02X\r\n", one, ck); }
		      else snprintf(cmd, sizeof cmd, "%s\r\n", one);
		      int (*nin)(const char *, int) = (int (*)(const char *, int))(void *)(n2mnl_base + N2MNL_EXP_mtk_gps_nmea_input_);
		      int rr = nin(cmd, (int)strlen(cmd)); n2_log("PMTK #%d injected into the engine: %.*s -> %d", idx, (int)strlen(cmd) - 2, cmd, rr); } } }
		{ static uint64_t tb; static int lna_done;              /* gnssd13: --lna-after S = ioctl 192 (0593) S s after libmnl's 52nd write */
		  if (g_lna_after > 0 && !lna_done) { if (!tb && st_txfr >= 52) tb = t;
		    if (tb && t - tb >= (uint64_t)g_lna_after * 1000u) { lna_done = 1; uint32_t lv = 0; int lr = ioctl(g_devfd, N2GD_IOC(192), &lv);
		      n2_log("gnssd13: L1 eLNA handed to the DSP after the boot (ioctl 192): rc %d errno %d v %#x", lr, lr ? *__error() : 0, lv); } } }
		{ static uint64_t ts; if (t - ts >= 30000) { ts = t; n2_log("libmnl text summary: %ld lines (%ld with keywords, %ld shown), notifications %ld | engine msgs (slot 6) %ld, $-sentences %ld, types 0:%ld 1:%ld 2:%ld 3:%ld 9:%ld | libmnl reads %ld (%ld B)", st_text, st_kw, st_textcon, st_notify, st_s6, st_s6nmea, s6_type[0], s6_type[1], s6_type[2], s6_type[3], s6_type[9], st_lrd, st_lrd_bytes); } }
		if (t - last >= nxt) { stats(); last = t; nxt = 10000; }
		if (runsec && t - g_t0 >= (uint64_t)runsec * 1000u) { n2_trace_summary(1); stats(); n2_log("run-seconds reached, exit"); exit(0); }
	}
}
