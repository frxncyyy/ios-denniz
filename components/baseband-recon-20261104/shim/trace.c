/* LD_PRELOAD tracer for the MediaTek ccci daemons: logs every read/write/ioctl on /dev/ccci_* and the file operations they perform. freestanding, raw syscalls. */
typedef unsigned long u64; typedef long i64; typedef unsigned int u32; typedef unsigned char u8;
static inline i64 sc(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
	register i64 x8 __asm__("x8") = n; register i64 x0 __asm__("x0") = a; register i64 x1 __asm__("x1") = b;
	register i64 x2 __asm__("x2") = c; register i64 x3 __asm__("x3") = d; register i64 x4 __asm__("x4") = e; register i64 x5 __asm__("x5") = f;
	__asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory", "cc");
	return x0;
}
#define SYS_openat 56
#define SYS_close 57
#define SYS_read 63
#define SYS_write 64
#define SYS_ioctl 29
#define SYS_gettid 178
#define MAXFD 1024
static char names[MAXFD][40];
static int lfd = -1;
static void lopen(void) { if (lfd < 0) lfd = (int)sc(SYS_openat, -100, (i64)"/tmp/ccci_trace.log", 0x441 /*O_WRONLY|O_CREAT|O_APPEND*/, 0666, 0, 0); }
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int startswith(const char *s, const char *p) { while (*p) { if (*s++ != *p++) return 0; } return 1; }
static char *hexn(char *o, const u8 *p, int n) { static const char h[] = "0123456789abcdef"; for (int i = 0; i < n; i++) { *o++ = h[p[i] >> 4]; *o++ = h[p[i] & 15]; } return o; }
static char *dec(char *o, i64 v) { char t[24]; int n = 0; if (v < 0) { *o++ = '-'; v = -v; } if (!v) t[n++] = '0'; while (v) { t[n++] = '0' + v % 10; v /= 10; } while (n) *o++ = t[--n]; return o; }
static void rec(char op, int fd, const void *buf, i64 len, i64 ret, u64 extra) {
	char line[8192];
	lopen();
	char *o = line; *o++ = op; *o++ = ' '; o = dec(o, sc(SYS_gettid, 0, 0, 0, 0, 0, 0)); *o++ = ' ';
	const char *nm = (fd >= 0 && fd < MAXFD) ? names[fd] : ""; int nl = slen(nm); for (int i = 0; i < nl; i++) *o++ = nm[i];
	*o++ = ' '; o = dec(o, len); *o++ = ' '; o = dec(o, ret); *o++ = ' '; o = hexn(o, (const u8 *)&extra, 4); *o++ = ' ';
	int n = (buf && ret > 0 && (op == 'R')) ? (int)ret : (buf && (op == 'W' || op == 'A' || op == 'B') ? (int)len : 0); if (n > 2048) n = 2048;
	if (buf && n > 0) o = hexn(o, buf, n);
	*o++ = '\n'; sc(SYS_write, lfd, (i64)line, o - line, 0, 0, 0);
}
static int traced(int fd) { return fd >= 0 && fd < MAXFD && names[fd][0]; }
static void track(int fd, const char *path) { if (fd >= 0 && fd < MAXFD) { int i = 0; if (startswith(path, "/dev/ccci") || startswith(path, "/dev/tty") || startswith(path, "/mnt/vendor") || startswith(path, "/data") || startswith(path, "/vendor") || startswith(path, "/proc/ccci") || startswith(path, "/sys/kernel/ccci")) { while (path[i] && i < 39) { names[fd][i] = path[i]; i++; } names[fd][i] = 0; } else names[fd][0] = 0; } }
int openat(int dfd, const char *path, int flags, ...) { __builtin_va_list ap; __builtin_va_start(ap, flags); int mode = __builtin_va_arg(ap, int); __builtin_va_end(ap); int fd = (int)sc(SYS_openat, dfd, (i64)path, flags, mode, 0, 0); track(fd, path); if (fd >= 0 && traced(fd)) rec('O', fd, path, slen(path), fd, (u64)flags); return fd < 0 ? -1 : fd; }
int open(const char *path, int flags, ...) { __builtin_va_list ap; __builtin_va_start(ap, flags); int mode = __builtin_va_arg(ap, int); __builtin_va_end(ap); return openat(-100, path, flags, mode); }
int __openat_2(int dfd, const char *path, int flags) { return openat(dfd, path, flags, 0); }
int __open_2(const char *path, int flags) { return openat(-100, path, flags, 0); }
i64 read(int fd, void *buf, u64 n) { i64 r = sc(SYS_read, fd, (i64)buf, n, 0, 0, 0); if (traced(fd) && (names[fd][5] == 'c' || r >= 0)) rec('R', fd, buf, n, r, 0); return r < 0 ? -1 : r; }
i64 write(int fd, const void *buf, u64 n) { i64 r = sc(SYS_write, fd, (i64)buf, n, 0, 0, 0); if (traced(fd)) rec('W', fd, buf, n, r, 0); return r < 0 ? -1 : r; }
int ioctl(int fd, int req, ...) {
	__builtin_va_list ap; __builtin_va_start(ap, req); u64 arg = __builtin_va_arg(ap, u64); __builtin_va_end(ap);
	u32 sz = ((u32)req >> 16) & 0x3fff; int dirw = ((u32)req >> 30) & 1, dirr = ((u32)req >> 31) & 1;
	if (traced(fd) && dirw && arg && sz) rec('A', fd, (const void *)arg, sz, 0, (u32)req);   /* argument going in */
	i64 r = sc(SYS_ioctl, fd, (u32)req, arg, 0, 0, 0);
	if (traced(fd)) rec('I', fd, 0, 0, r, (u32)req);
	if (traced(fd) && dirr && arg && sz && r >= 0) rec('B', fd, (const void *)arg, sz, 0, (u32)req);   /* argument coming out */
	return r < 0 ? -1 : (int)r; }
int close(int fd) { i64 r = sc(SYS_close, fd, 0, 0, 0, 0, 0); if (fd >= 0 && fd < MAXFD) names[fd][0] = 0; return r < 0 ? -1 : 0; }
