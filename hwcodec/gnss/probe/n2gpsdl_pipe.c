/* SPDX-License-Identifier: MIT
 * n2gpsdl_pipe (GNSS M2 phone test): the byte pipe /dev/gpsdlN that libmnl uses, exercised from user space.
 *   n2gpsdl_pipe [--gate FILE] [--delay S] [--link N] [--send FILE] [--send-hex HEX] [--read-seconds S]
 * Opens /dev/gpsdl<N> (powers the GNSS block, brings the link up), optionally writes FILE (e.g. a DSP code / MVCD
 * stream captured from Android's mnld), then reads non-blocking for S seconds and prints "N2GPIPE ..." lines: status
 * ioctl, bytes written, bytes read, first 32 bytes in hex. Without libmnl the DSP stays in RESET_DONE and sends nothing:
 * "read 0 bytes" with the link up is the expected M2 result.
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
int main(int argc, char **argv)
{
	int delay = 90, link = 0, secs = 20; const char *gate = 0, *send = 0, *hex = 0; char l[256];
	for (int i = 1; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "--delay")) delay = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "--link")) link = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "--send")) send = argv[i + 1];
		else if (!strcmp(argv[i], "--read-seconds")) secs = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "--gate")) gate = argv[i + 1];
		else if (!strcmp(argv[i], "--send-hex")) hex = argv[i + 1];
	}
	if (gate && access(gate, 0) != 0) return 0;
	snprintf(l, sizeof l, "N2GPIPE start delay=%d link=%d send=%s read=%ds", delay, link, send ? send : "-", secs); out(l);
	sleep((unsigned)delay);
	char dev[32]; snprintf(dev, sizeof dev, "/dev/gpsdl%d", link);
	int fd = open(dev, O_RDWR | O_NONBLOCK);
	if (fd < 0) { snprintf(l, sizeof l, "N2GPIPE %s open errno=%d", dev, *__error()); out(l); return 0; }
	u32 st = 0; ioctl(fd, N2GD_IOC(13), &st);
	snprintf(l, sizeof l, "N2GPIPE %s open ok status=%#x (powered=%u link=%u err=%u)", dev, st, st & 1, (st >> 1) & 1, (st >> 8) & 0xff); out(l);
	long wrote = 0;
	if (send) {
		int sf = open(send, 0);
		if (sf < 0) { snprintf(l, sizeof l, "N2GPIPE send file %s errno=%d", send, *__error()); out(l); }
		else {
			static unsigned char b[4096]; ssize_t n;
			while ((n = read(sf, b, sizeof b)) > 0) { ssize_t w = write(fd, b, (size_t)n); if (w < 0) { snprintf(l, sizeof l, "N2GPIPE write errno=%d after %ld", *__error(), wrote); out(l); break; } wrote += w; }
			close(sf);
		}
		snprintf(l, sizeof l, "N2GPIPE wrote %ld bytes", wrote); out(l);
	}
	if (hex) {                     /* e.g. libmnl's first DSP frame aaf0090005fe32000001003f01aa0f (n2mnld harness) */
		static unsigned char hb[512]; int n = 0;
		for (const char *c = hex; c[0] && c[1] && n < (int)sizeof hb; c += 2) {
			int h = (c[0] <= '9' ? c[0] - '0' : (c[0] | 32) - 'a' + 10), lo = (c[1] <= '9' ? c[1] - '0' : (c[1] | 32) - 'a' + 10);
			hb[n++] = (unsigned char)(h * 16 + lo);
		}
		ssize_t w = write(fd, hb, (size_t)n);
		snprintf(l, sizeof l, "N2GPIPE sent %d hex bytes rc=%ld errno=%d", n, (long)w, w < 0 ? *__error() : 0); out(l);
	}
	static unsigned char r[65536]; long got = 0;
	for (int t = 0; t < secs * 10; t++) {
		ssize_t n = read(fd, r + (got < 64 ? got : 64), sizeof r - 128);
		if (n > 0) { got += n; }
		else if (n < 0 && *__error() != 35 /* EAGAIN */) { snprintf(l, sizeof l, "N2GPIPE read errno=%d", *__error()); out(l); break; }
		usleep(100000);
	}
	int k = snprintf(l, sizeof l, "N2GPIPE read %ld bytes, first:", got);
	for (int i = 0; i < 64 && i < got && k < (int)sizeof l - 4; i++) k += snprintf(l + k, sizeof l - (size_t)k, " %02x", r[i]);
	out(l);
	ioctl(fd, N2GD_IOC(13), &st);
	snprintf(l, sizeof l, "N2GPIPE done status=%#x", st); out(l);
	close(fd);
	return 0;
}
