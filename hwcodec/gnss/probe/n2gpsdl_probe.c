/* SPDX-License-Identifier: MIT
 * n2gpsdl_probe (GNSS M1b phone test): after --delay S opens /dev/gpsdl0 (this powers the GNSS block through patch
 * 0350/0351 when nord2gnss=1 and the Wi-Fi owner is retained), asks GPSDL_IOC_QUERY_STATUS and GPSDL_IOC_GPS_HWVER and
 * prints "N2GPS ..." lines to /dev/console. Exit 0 always (one-shot job).
 */
typedef unsigned long size_t; typedef long ssize_t; typedef unsigned int u32;
extern int open(const char *, int, ...);
extern int close(int);
extern ssize_t write(int, const void *, size_t);
extern int ioctl(int, unsigned long, ...);
extern int *__error(void);
extern unsigned int sleep(unsigned int);
extern int snprintf(char *, size_t, const char *, ...);
extern int strcmp(const char *, const char *);
extern int atoi(const char *);
extern size_t strlen(const char *);
extern int access(const char *, int);
#define N2GD_IOC(n) (0xc0000000ul | (4ul << 16) | ((unsigned long)'G' << 8) | (unsigned long)(n))   /* _IOWR('G', n, uint32_t) */
static void out(const char *s)
{
	int fd = open("/dev/console", 1 | 4 | 0x20000);
	if (fd >= 0) { write(fd, s, strlen(s)); write(fd, "\n", 1); close(fd); }
}
int main(int argc, char **argv)
{
	int delay = 90; const char *gate = 0; char l[160];
	for (int i = 1; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "--delay")) delay = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "--gate")) gate = argv[i + 1];
	}
	if (gate && access(gate, 0) != 0) return 0;
	snprintf(l, sizeof l, "N2GPS probe start delay=%d", delay); out(l);
	sleep((unsigned)delay);
	for (int link = 0; link < 2; link++) {
		char dev[32]; snprintf(dev, sizeof dev, "/dev/gpsdl%d", link);
		int fd = open(dev, 2);
		if (fd < 0) { snprintf(l, sizeof l, "N2GPS %s open errno=%d", dev, *__error()); out(l); continue; }
		u32 st = 0xffffffffu, ver = 0xffffffffu;
		int r1 = ioctl(fd, N2GD_IOC(13), &st), e1 = r1 ? *__error() : 0;
		int r2 = ioctl(fd, N2GD_IOC(6), &ver), e2 = r2 ? *__error() : 0;
		snprintf(l, sizeof l, "N2GPS %s open ok status=%#x (powered=%u err=%u) rc=%d/%d hwver=%#x rc=%d/%d", dev, st, st & 1, (st >> 8) & 0xff, r1, e1, ver, r2, e2);
		out(l);
		close(fd);
	}
	out("N2GPS probe done");
	return 0;
}
