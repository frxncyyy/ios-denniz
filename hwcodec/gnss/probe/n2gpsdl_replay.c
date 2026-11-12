/* SPDX-License-Identifier: MIT
 * n2gpsdl_replay (GNSS q10jr9 / patch 0362, payload gnssl9b): multi-frame replay over the V9 data link.
 *   n2gpsdl_replay [--gate FILE] [--delay S] [--hold K] OPS...
 * After --delay: opens /dev/gpsdl0, ioctl N2GD_IOC(140+K) (default K=24 = V9 HOLD: power-on, MCUB RESET_DONE ack, EMI_REQ,
 * RX DMA2 armed; /dev/gpsdl0 becomes the byte pipe), then runs OPS in order:
 *   --tx HEX              write the bytes (the kernel pads to 32-bit words, A2D DMA)
 *   --txfile F OFF LEN    write LEN bytes of file F from offset OFF, in chunks of up to 4096
 *   --rx MS               read (non-blocking, every 10 ms) for MS milliseconds, hex-print everything received
 *   --sleep MS            just wait
 *   --boot MS             libmnl DSP code download (embedded MT6893 L1 frames), MS = wait per fragment ack
 * Console lines start with "N2GRPL". Installed as /usr/local/libexec/n2gpsdl_step (same guest/owned/undo as gnssstep).
 */
typedef unsigned long size_t; typedef long ssize_t; typedef unsigned int u32;
extern int open(const char *, int, ...);
extern int close(int);
extern ssize_t read(int, void *, size_t);
extern ssize_t write(int, const void *, size_t);
extern int ioctl(int, unsigned long, ...);
extern int *__error(void);
extern unsigned int sleep(unsigned int);
extern int usleep(unsigned int);
extern int snprintf(char *, size_t, const char *, ...);
extern int strcmp(const char *, const char *);
extern int atoi(const char *);
extern size_t strlen(const char *);
extern int access(const char *, int);
#define N2GD_IOC(n) (0xc0000000ul | (4ul << 16) | ((unsigned long)'G' << 8) | (unsigned long)(n))
#define O_RDWR 2
#define O_NONBLOCK 4
static void out(const char *s)
{
	int fd = open("/dev/console", 1 | O_NONBLOCK | 0x20000);
	if (fd >= 0) { write(fd, s, strlen(s)); write(fd, "\n", 1); close(fd); }
}
static long logged, total_rx;
static void hexline(const char *what, const unsigned char *b, long n, long at)
{
	char l[256];
	for (long o = 0; o < n; o += 48) {
		if (logged > 16384) { if (logged < 1000000) { out("N2GRPL (hex log budget reached, counting only)"); logged = 1000000; } return; }
		int k = snprintf(l, sizeof l, "N2GRPL %s +%ld:", what, at + o);
		for (long i = o; i < n && i < o + 48 && k < (int)sizeof l - 4; i++) k += snprintf(l + k, sizeof l - (size_t)k, " %02x", b[i]);
		out(l); logged += 48;
	}
}
/* MTK binary frames from the DSP: AA F0 <stuffed: len(2) id(2) payload cks(2)> AA 0F; AA = DE DF, DE = DE E0 (libmnl
 * hal_MTK_Binary_EncodePacket / Host_Based_Data_Dec). Every complete frame is printed; the last id/seq is kept for --boot. */
static unsigned char fb_[4096]; static int fbn_ = -1, prev_ = -1; static unsigned last_id, last_seq, nframes;
static void frame_done(void)
{
	unsigned char u[4096]; int n = 0; char l[256];
	for (int i = 0; i < fbn_; i++) {
		if (fb_[i] == 0xde && i + 1 < fbn_) { u[n++] = fb_[i + 1] == 0xdf ? 0xaa : 0xde; i++; } else u[n++] = fb_[i];
	}
	if (n < 6) return;
	unsigned len = u[0] | (u[1] << 8), id = u[2] | (u[3] << 8), cks = u[n - 2] | (u[n - 1] << 8), sum = 0;
	for (int i = 0; i < n - 2; i++) sum += u[i];
	last_id = id; last_seq = n >= 8 ? (unsigned)(u[4] | (u[5] << 8)) : 0; nframes++;
	int k = snprintf(l, sizeof l, "N2GRPL frame #%u id=%#06x len=%#x cks=%s payload:", nframes, id, len, (sum & 0xffff) == cks ? "ok" : "BAD");
	for (int i = 4; i < n - 2 && i < 4 + 40 && k < (int)sizeof l - 4; i++) k += snprintf(l + k, sizeof l - (size_t)k, " %02x", u[i]);
	if (logged < 16384) { out(l); logged += 64; }
}
static void parse(const unsigned char *b, long n)
{
	for (long i = 0; i < n; i++) {
		int c = b[i];
		if (prev_ == 0xaa && c == 0xf0) { fbn_ = 0; }
		else if (prev_ == 0xaa && c == 0x0f && fbn_ >= 0) { fbn_--; frame_done(); fbn_ = -1; }   /* drop the AA already stored */
		else if (fbn_ >= 0) { if (fbn_ < (int)sizeof fb_) fb_[fbn_++] = (unsigned char)c; else fbn_ = -1; }
		prev_ = c;
	}
}
static int unhex(int c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }
static void do_rx(int fd, int ms)
{
	static unsigned char r[8192]; char l[160]; long got = 0;
	for (int t = 0; t < ms; t += 10) {
		ssize_t n = read(fd, r, sizeof r);
		if (n > 0) { hexline("rx", r, n, total_rx); parse(r, n); total_rx += n; got += n; }
		else if (n < 0 && *__error() != 35 /* EAGAIN */) { snprintf(l, sizeof l, "N2GRPL rx errno=%d", *__error()); out(l); return; }
		usleep(10000);
	}
	snprintf(l, sizeof l, "N2GRPL rx %dms: %ld bytes (total %ld)", ms, got, total_rx); out(l);
}
static void do_tx(int fd, const unsigned char *b, long n, const char *what)
{
	char l[200]; long w = 0;
	while (w < n) {
		long m = n - w > 4096 ? 4096 : n - w;
		ssize_t r = write(fd, b + w, (size_t)m);
		if (r <= 0) { snprintf(l, sizeof l, "N2GRPL tx %s errno=%d after %ld of %ld", what, *__error(), w, n); out(l); return; }
		w += r;
	}
	snprintf(l, sizeof l, "N2GRPL tx %s %ld bytes ok", what, n); out(l);
}
/* read until a frame with id (and seq, if seq >= 0) arrives or ms pass; 1 if it came */
static int rx_ack(int fd, int ms, unsigned id, int seq)
{
	static unsigned char r[8192]; unsigned before = nframes;
	for (int t = 0; t < ms; t += 2) {
		ssize_t n = read(fd, r, sizeof r);
		if (n > 0) { hexline("rx", r, n, total_rx); total_rx += n; parse(r, n); }
		else if (n < 0 && *__error() != 35) return 0;
		if (nframes != before && last_id == id && (seq < 0 || (int)last_seq == seq)) return 1;
		usleep(2000);
	}
	return 0;
}
#include "n2gpsdl_bootdata.h"
static void txf(int fd, int i, const char *what) { do_tx(fd, n2b_data + n2b_off[i][0], n2b_off[i][1], what); }
/* libmnl _BootDSP_UART_V2 order (n2mnld/runs/bootdsp_replay.json): header (0xFE08), COMD4 x3 + 500 ms, header, ack 0xFE31
 * seq 0; fragments 1..50 (0xFE0C) each acked by 0xFE32 seq k (one resend if not); then the post-boot command 0xFE05. */
static void do_boot(int fd, int frag_ms)
{
	char l[200]; int acked = 0, hdr = 0;
	out("N2GRPL BOOT start (MT6893_E1 L1 DSP blob, 50 fragments)");
	txf(fd, 0, "header"); hdr = rx_ack(fd, 300, 0xfe31, 0);
	for (int r = 0; r < 5 && !hdr; r++) {
		txf(fd, 1, "COMD4"); txf(fd, 1, "COMD4"); txf(fd, 1, "COMD4");
		do_rx(fd, 500 * (r + 1));
		txf(fd, 0, "header"); hdr = rx_ack(fd, 1000, 0xfe31, 0);
		snprintf(l, sizeof l, "N2GRPL BOOT header round %d: %s", r + 1, hdr ? "ACK 0xFE31" : "no ack"); out(l);
	}
	if (!hdr) { out("N2GRPL BOOT RESULT: no header ack (0xFE31) from the DSP ROM; not sending the code"); do_rx(fd, 1000); return; }
	out("N2GRPL BOOT header ACKED (0xFE31 seq 0): DSP ROM speaks the MVCD protocol");
	for (int k = 1; k <= 50; k++) {
		int ok = 0;
		for (int t = 0; t < 3 && !ok; t++) { txf(fd, 1 + k, t ? "fragment(resend)" : "fragment"); ok = rx_ack(fd, frag_ms, 0xfe32, k); }
		if (ok) acked++;
		snprintf(l, sizeof l, "N2GRPL BOOT fragment %d/50 %s (last id %#x seq %u)", k, ok ? "ACK" : "NO ACK", last_id, last_seq); out(l);
		if (!ok && k > 1 && acked < k - 3) { out("N2GRPL BOOT too many missing fragment acks, stopping the download"); break; }
	}
	snprintf(l, sizeof l, "N2GRPL BOOT RESULT: %d/50 fragments acked", acked); out(l);
	do_rx(fd, 500);
	txf(fd, 52, "post-boot command 0xFE05 (old frame 1)");
	do_rx(fd, 3000);
}
/* 0364 (q10jr11): upload the frames into the kernel (N2GD_IOC_CHUNK, 4 KiB _IOW chunks), then ladder ioctl 140+K
 * (K=30 boot + rollback, K=31 boot + HOLD). Returns 0 if the ladder reported success (bit 0). */
struct n2gb_chunk { u32 which, off, len, sum; unsigned char data[4096]; };
#define N2GD_IOC_CHUNK (0x80000000ul | ((unsigned long)sizeof(struct n2gb_chunk) << 16) | ((unsigned long)'G' << 8) | 180ul)
static int kboot(int fd, int k)
{
	static struct n2gb_chunk c; char l[200]; u32 sum = 0, total = (u32)sizeof n2b_data;
	for (u32 i = 0; i < total; i++) sum = sum * 31u + n2b_data[i];
	for (u32 o = 0; o < total; o += 4096) {
		c.which = 0; c.off = o; c.len = total - o < 4096 ? total - o : 4096; c.sum = 0;
		for (u32 i = 0; i < c.len; i++) c.data[i] = n2b_data[o + i];
		if (ioctl(fd, N2GD_IOC_CHUNK, &c)) { snprintf(l, sizeof l, "N2GRPL chunk @%u errno=%d", o, *__error()); out(l); return 1; }
	}
	u32 *h = (u32 *)(void *)c.data; h[0] = N2B_NFRAMES; h[1] = total; h[2] = sum;
	for (int i = 0; i < N2B_NFRAMES; i++) { h[3 + 2 * i] = n2b_off[i][0]; h[4 + 2 * i] = n2b_off[i][1]; }
	c.which = 1; c.off = 0; c.len = (u32)(4 * (3 + 2 * N2B_NFRAMES)); c.sum = sum;
	if (ioctl(fd, N2GD_IOC_CHUNK, &c)) { snprintf(l, sizeof l, "N2GRPL table errno=%d", *__error()); out(l); return 1; }
	snprintf(l, sizeof l, "N2GRPL uploaded %u bytes, %d frames, sum %#x; running ladder K=%d (ioctl %d)", total, N2B_NFRAMES, sum, k, 140 + k); out(l);
	u32 v = 0xffffffffu; int r = ioctl(fd, N2GD_IOC(140 + k), &v);
	snprintf(l, sizeof l, "N2GRPL kboot K=%d rc=%d errno=%d result=%#x (done=%u step=%u code=%u)", k, r, r ? *__error() : 0, v, v & 1, (v >> 8) & 0xff, (v >> 16) & 0xff); out(l);
	return (r || !(v & 1)) ? 1 : 0;
}
int main(int argc, char **argv)
{
	int delay = 90, hold = 24, first = 1; const char *gate = 0; char l[256];
	for (; first + 1 < argc; first += 2) {
		if (!strcmp(argv[first], "--delay")) delay = atoi(argv[first + 1]);
		else if (!strcmp(argv[first], "--hold")) hold = atoi(argv[first + 1]);
		else if (!strcmp(argv[first], "--gate")) gate = argv[first + 1];
		else break;
	}
	if (gate && access(gate, 0) != 0) return 0;
	snprintf(l, sizeof l, "N2GRPL start delay=%d hold K=%d ops=%d", delay, hold, argc - first); out(l);
	sleep((unsigned)delay);
	int fd = open("/dev/gpsdl0", O_RDWR | O_NONBLOCK);
	if (fd < 0) { snprintf(l, sizeof l, "N2GRPL open errno=%d", *__error()); out(l); out("N2GRPL done"); return 0; }
	u32 v = 0xffffffffu; int r;
	if (hold > 0) {                                  /* --hold 0: no HOLD ioctl (use --kboot) */
		r = ioctl(fd, N2GD_IOC(140 + hold), &v);
		snprintf(l, sizeof l, "N2GRPL hold K=%d rc=%d errno=%d result=%#x", hold, r, r ? *__error() : 0, v); out(l);
		if (r || !(v & 1)) { close(fd); out("N2GRPL done (hold failed)"); return 0; }
		do_rx(fd, 200);                              /* anything the DSP sent right after RESET_DONE ack */
	}
	for (int i = first; i < argc; i++) {
		if (!strcmp(argv[i], "--tx") && i + 1 < argc) {
			static unsigned char hb[4096]; long n = 0; const char *c = argv[++i];
			for (; c[0] && c[1] && n < (long)sizeof hb; c += 2) hb[n++] = (unsigned char)(unhex(c[0]) * 16 + unhex(c[1]));
			hexline("tx", hb, n < 48 ? n : 48, 0);
			do_tx(fd, hb, n, "hex");
		} else if (!strcmp(argv[i], "--txfile") && i + 3 < argc) {
			const char *f = argv[i + 1]; long off = atoi(argv[i + 2]), len = atoi(argv[i + 3]); i += 3;
			static unsigned char fb[65536]; int sf = open(f, 0);
			if (sf < 0) { snprintf(l, sizeof l, "N2GRPL txfile %s errno=%d", f, *__error()); out(l); continue; }
			while (off > 0) { ssize_t n = read(sf, fb, (size_t)(off < (long)sizeof fb ? off : (long)sizeof fb)); if (n <= 0) break; off -= n; }   /* no lseek in the stub */
			while (len > 0) {
				ssize_t n = read(sf, fb, (size_t)(len < (long)sizeof fb ? len : (long)sizeof fb));
				if (n <= 0) break;
				do_tx(fd, fb, n, "file"); len -= n;
			}
			close(sf);
		} else if (!strcmp(argv[i], "--rx") && i + 1 < argc) {
			do_rx(fd, atoi(argv[++i]));
		} else if (!strcmp(argv[i], "--kboot") && i + 1 < argc) {
			int k = atoi(argv[++i]);
			if (kboot(fd, k)) { out("N2GRPL kboot failed, stopping"); break; }
		} else if (!strcmp(argv[i], "--boot") && i + 1 < argc) {
			do_boot(fd, atoi(argv[++i]));
		} else if (!strcmp(argv[i], "--sleep") && i + 1 < argc) {
			usleep((unsigned)atoi(argv[++i]) * 1000u);
		} else { snprintf(l, sizeof l, "N2GRPL unknown op %s", argv[i]); out(l); }
	}
	ioctl(fd, N2GD_IOC(13), &v);
	snprintf(l, sizeof l, "N2GRPL status=%#x rx total %ld", v, total_rx); out(l);
	close(fd);
	out("N2GRPL done");
	return 0;
}
