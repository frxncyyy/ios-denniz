/* SPDX-License-Identifier: MIT
 * n2gpsdl_step (GNSS M1b SAFE test, kernel q10jr3 / patch 0353): opens /dev/gpsdl0 (no hardware access since 0353) and
 * runs the GNSS power-path steps 1..N one ioctl at a time (N2GD_IOC(100+k)), printing "N2GSTEP ..." before and after each
 * and pausing 3 s between steps so the kernel's own "NORD2 GNSS STEP k ..." markers reach the console first.
 *   n2gpsdl_step [--gate FILE] [--delay S] --upto N        (N = 1: always-on predicates only ... 4: full power-on)
 *   n2gpsdl_step [--gate FILE] [--delay S] --one K         (q10jr4: exactly one access, 1 none, 2 0x180601a0, 3 0x18060184, 4 0x180601ac, 5 map only)
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
#define N2GD_IOC(n) (0xc0000000ul | (4ul << 16) | ((unsigned long)'G' << 8) | (unsigned long)(n))
static void out(const char *s)
{
	int fd = open("/dev/console", 1 | 4 | 0x20000);
	if (fd >= 0) { write(fd, s, strlen(s)); write(fd, "\n", 1); close(fd); }
}
int main(int argc, char **argv)
{
	int delay = 90, upto = 1, one = 0; const char *gate = 0; char l[160];
	for (int i = 1; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "--delay")) delay = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "--upto")) upto = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "--gate")) gate = argv[i + 1];
		else if (!strcmp(argv[i], "--one")) one = atoi(argv[i + 1]);
	}
	if (gate && access(gate, 0) != 0) return 0;
	snprintf(l, sizeof l, "N2GSTEP start delay=%d upto=%d", delay, upto); out(l);
	sleep((unsigned)delay);
	int fd = open("/dev/gpsdl0", 2);
	if (fd < 0) { snprintf(l, sizeof l, "N2GSTEP open errno=%d", *__error()); out(l); return 0; }
	if (one) {                     /* 0354 (q10jr4): exactly one access, ioctl 110+one */
		snprintf(l, sizeof l, "N2GSTEP one %d: calling", one); out(l);
		sleep(3);
		u32 v = 0xffffffffu; int r = ioctl(fd, N2GD_IOC(110 + one), &v);
		snprintf(l, sizeof l, "N2GSTEP one %d: rc=%d errno=%d result=%#x", one, r, r ? *__error() : 0, v); out(l);
		close(fd); out("N2GSTEP done"); return 0;
	}
	for (int k = 1; k <= upto && k <= 4; k++) {
		snprintf(l, sizeof l, "N2GSTEP %d: calling", k); out(l);
		sleep(3);
		u32 v = 0xffffffffu; int r = ioctl(fd, N2GD_IOC(100 + k), &v);
		snprintf(l, sizeof l, "N2GSTEP %d: rc=%d errno=%d result=%#x (done=%u code=%u pred=%u)", k, r, r ? *__error() : 0, v, v & 1, (v >> 8) & 0xff, (v >> 16) & 0xff); out(l);
		if (r || !(v & 1)) break;
		sleep(3);
	}
	close(fd);
	out("N2GSTEP done");
	return 0;
}
