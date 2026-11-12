/* SPDX-License-Identifier: MIT
 * n2shim.c -- bionic (Linux AArch64) -> Darwin (iOS 16 arm64) layer for the 139 imports of MediaTek's libmnl.so
 * (vendor/lib64/mt6893, sha 9a1e1be7). n2_shim_lookup(name) gives the function n2mnld writes into libmnl's GOT slot.
 *
 * Three kinds of entries (list in GNSS_ENGINE.md section 6):
 *  - D(): the Darwin function itself (same C ABI and same data layout: string/memory/math/stdio on FILE pointers, time, struct tm ...);
 *  - S(): a wrapper n2s_NAME here (errno, flags, struct layouts, pthread objects by pointer, timers, epoll, sockets,
 *         paths, the data-link descriptors);
 *  - V(): a variadic import: asm trampoline n2v_NAME (n2shim_va.S) -> n2vi_NAME(saved regs, caller stack) here, which
 *         reads the arguments the LINUX way (struct lva = the AAPCS64 va_list).
 * Rules: every Linux-visible errno is the Linux number (n2_set_lerrno); every pthread_* return value is mapped too;
 * no guest format string is ever handed to Darwin printf with guest arguments: n2_lfmt() formats conversion by
 * conversion with typed values; scanf gets a Darwin va_list built only from pointers.
 */
#include "n2sys.h"

/* ------------------------------------------------------------------ errno */
static const uint8_t d2l_tab[] = {   /* Darwin errno -> Linux errno, index = Darwin number (0..106) */
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 35, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
	31, 32, 33, 34, 11, 115, 114, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106,
	107, 108, 109, 110, 111, 40, 36, 112, 113, 39, 5, 5, 122, 116, 5, 5, 5, 5, 5, 5, 37, 38, 5, 5, 5, 5, 5, 75, 5, 5, 5,
	5, 125, 5, 5, 84, 5, 5, 5, 5, 5, 5, 5, 5, 5, 95, 5, 5, 5, 5 };
int n2_d2l_errno(int d) { return (d >= 0 && d < (int)sizeof d2l_tab) ? d2l_tab[d] : 5; }
static int l2d_errno(int l)
{
	if (l > 0 && l <= 34 && l != 11) return l;
	for (int d = 0; d < (int)sizeof d2l_tab; d++) if (d2l_tab[d] == l) return d;
	return l;
}
static unsigned long errkey; static int errkey_ok;
static int *lerrp(void)
{
	int *p = errkey_ok ? pthread_getspecific(errkey) : 0;
	if (!p) { p = calloc(1, 16); if (errkey_ok) pthread_setspecific(errkey, p); }
	return p;
}
void n2_set_lerrno(int e) { *lerrp() = e; }
static long fail(void) { n2_set_lerrno(n2_d2l_errno(*__error())); return -1; }   /* after a failed Darwin call */
static long chk(long r) { return r < 0 ? fail() : r; }
static int pmap(int r) { return r ? n2_d2l_errno(r) : 0; }                          /* pthread_* return values */
static int *n2s___errno(void) { return lerrp(); }

/* ------------------------------------------------------------------ Linux va_list */
struct lva { void *stack; void *gr_top; void *vr_top; int gr_offs; int vr_offs; };
static void lva_from(struct lva *a, uint64_t *saved, void *stack, int nfixed)
{
	a->stack = stack; a->gr_top = (char *)saved + 64; a->vr_top = (char *)saved + 192;
	a->gr_offs = -(8 - nfixed) * 8; a->vr_offs = -128;
}
static uint64_t lva_gp(struct lva *a)
{
	uint64_t v;
	if (a->gr_offs < 0) { memcpy(&v, (char *)a->gr_top + a->gr_offs, 8); a->gr_offs += 8; return v; }
	memcpy(&v, a->stack, 8); a->stack = (char *)a->stack + 8; return v;
}
static double lva_fp(struct lva *a)
{
	double v;
	if (a->vr_offs < 0) { memcpy(&v, (char *)a->vr_top + a->vr_offs, 8); a->vr_offs += 16; return v; }
	memcpy(&v, a->stack, 8); a->stack = (char *)a->stack + 8; return v;
}
static void lva_ld(struct lva *a)        /* long double (16 bytes on Linux): consumed, value not representable here */
{
	if (a->vr_offs < 0) { a->vr_offs += 16; return; }
	a->stack = (void *)(((uintptr_t)a->stack + 15) & ~(uintptr_t)15); a->stack = (char *)a->stack + 16;
}

/* printf over a Linux va_list. Returns the full length (like snprintf); out may be NULL when cap == 0. */
static size_t lfmt(char *out, size_t cap, const char *f, struct lva *ap)
{
	size_t pos = 0;
#define ROOM (pos < cap ? cap - pos : 0)
#define DST (pos < cap ? out + pos : (char *)0)
	if (!f) f = "(null fmt)";
	while (*f) {
		if (*f != '%') { if (pos + 1 < cap) out[pos] = *f; pos++; f++; continue; }
		const char *s = f++;
		char spec[48]; int k = 0; spec[k++] = '%';
		while (*f && strchr("-+ #0'", *f)) { if (k < 8) spec[k++] = *f; f++; }
		if (*f == '*') { int w = (int)lva_gp(ap); f++; k += snprintf(spec + k, sizeof spec - k, "%d", w < -4096 ? -4096 : w > 4096 ? 4096 : w); }
		else { int w = 0; while (*f >= '0' && *f <= '9') { w = w * 10 + (*f++ - '0'); if (w > 4096) w = 4096; } if (w) k += snprintf(spec + k, sizeof spec - k, "%d", w); }
		if (*f == '.') {
			f++; int p = 0;
			if (*f == '*') { p = (int)lva_gp(ap); f++; } else while (*f >= '0' && *f <= '9') { p = p * 10 + (*f++ - '0'); if (p > 4096) p = 4096; }
			if (p >= 0) k += snprintf(spec + k, sizeof spec - k, ".%d", p > 4096 ? 4096 : p);
		}
		int len = 0;                                       /* 0 none, 1 hh, 2 h, 3 l/ll/q/j/z/t, 4 L */
		for (;;) {
			if (*f == 'h') { len = len == 2 ? 1 : 2; f++; }
			else if (*f == 'l' || *f == 'q' || *f == 'j' || *f == 'z' || *f == 't') { len = 3; f++; }
			else if (*f == 'L') { len = 4; f++; }
			else break;
		}
		char c = *f; if (c) f++;
		switch (c) {
		case 'd': case 'i': {
			uint64_t v = lva_gp(ap); long long x = len == 1 ? (signed char)v : len == 2 ? (short)v : len == 3 ? (long long)v : (int)v;
			spec[k++] = 'l'; spec[k++] = 'l'; spec[k++] = 'd'; spec[k] = 0; pos += (size_t)snprintf(DST, ROOM, spec, x); break; }
		case 'u': case 'o': case 'x': case 'X': {
			uint64_t v = lva_gp(ap); unsigned long long x = len == 1 ? (unsigned char)v : len == 2 ? (unsigned short)v : len == 3 ? v : (unsigned)v;
			spec[k++] = 'l'; spec[k++] = 'l'; spec[k++] = c; spec[k] = 0; pos += (size_t)snprintf(DST, ROOM, spec, x); break; }
		case 'c': spec[k++] = 'c'; spec[k] = 0; pos += (size_t)snprintf(DST, ROOM, spec, (int)(unsigned char)lva_gp(ap)); break;
		case 's': { const char *p = (const char *)(uintptr_t)lva_gp(ap); spec[k++] = 's'; spec[k] = 0; pos += (size_t)snprintf(DST, ROOM, spec, p ? p : "(null)"); break; }
		case 'p': spec[k++] = 'p'; spec[k] = 0; pos += (size_t)snprintf(DST, ROOM, spec, (void *)(uintptr_t)lva_gp(ap)); break;
		case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
			if (len == 4) { lva_ld(ap); pos += (size_t)snprintf(DST, ROOM, "%s", "(ldbl)"); break; }
			spec[k++] = c; spec[k] = 0; pos += (size_t)snprintf(DST, ROOM, spec, lva_fp(ap)); break;
		case 'n': { void *p = (void *)(uintptr_t)lva_gp(ap);
			if (p) { if (len == 1) *(signed char *)p = (signed char)pos; else if (len == 2) { short v = (short)pos; memcpy(p, &v, 2); } else if (len == 3) { long v = (long)pos; memcpy(p, &v, 8); } else { int v = (int)pos; memcpy(p, &v, 4); } }
			break; }
		case 'm': pos += (size_t)snprintf(DST, ROOM, "%s", strerror(l2d_errno(*lerrp()))); break;
		case '%': if (pos + 1 < cap) out[pos] = '%'; pos++; break;
		default:                                           /* unknown conversion: copied literally, no argument used */
			for (const char *q = s; q < f; q++) { if (pos + 1 < cap) out[pos] = *q; pos++; }
		}
	}
	if (cap) out[pos < cap ? pos : cap - 1] = 0;
	return pos;
#undef ROOM
#undef DST
}
/* scanf: every variadic argument is a pointer -> Darwin va_list (char * over 8-byte slots) */
static int scan_args(const char *f, struct lva *ap, uint64_t *slot, int max)
{
	int n = 0;
	for (; f && *f; f++) {
		if (*f != '%') continue;
		f++;
		if (*f == '%') continue;
		int suppress = 0;
		if (*f == '*') { suppress = 1; f++; }
		while (*f >= '0' && *f <= '9') f++;
		while (*f && strchr("hlLqjztm", *f)) f++;
		if (*f == '[') { f++; if (*f == '^') f++; if (*f == ']') f++; while (*f && *f != ']') f++; }
		if (!*f) break;
		if (!suppress && n < max) slot[n++] = lva_gp(ap);
	}
	for (int i = n; i < max; i++) slot[i] = 0;
	return n;
}

/* ------------------------------------------------------------------ the data-link descriptors */
int n2_l1_fd = -1, n2_l5_fd = -1;
const struct n2_dev_ops *n2_dev;
static int l1_nonblock;
static int is_dl(int fd) { return fd >= 0 && (fd == n2_l1_fd || fd == n2_l5_fd); }
static ssize_t dl_read(int fd, void *b, size_t n)
{
	if (fd == n2_l1_fd && n2_dev) { ssize_t r = n2_dev->read(b, n, l1_nonblock); if (r < 0) { n2_set_lerrno((int)-r); return -1; } return r; }
	for (;;) sleep(3600);                                /* L5: no L5 link on this configuration (L1 only) */
}
static ssize_t dl_write(int fd, const void *b, size_t n)
{
	if (fd == n2_l1_fd && n2_dev) { ssize_t r = n2_dev->write(b, n); if (r < 0) { n2_set_lerrno((int)-r); return -1; } return r; }
	return (ssize_t)n;
}
static ssize_t n2s_read(int fd, void *b, size_t n) { return is_dl(fd) ? dl_read(fd, b, n) : chk(read(fd, b, n)); }
static ssize_t n2s___read_chk(int fd, void *b, size_t n, size_t bl) { if (n > bl) { n2_log("FATAL __read_chk overflow"); abort(); } return n2s_read(fd, b, n); }
static ssize_t n2s_write(int fd, const void *b, size_t n) { return is_dl(fd) ? dl_write(fd, b, n) : chk(write(fd, b, n)); }
static ssize_t n2s___write_chk(int fd, const void *b, size_t n, size_t bl) { if (n > bl) { n2_log("FATAL __write_chk overflow"); abort(); } return n2s_write(fd, b, n); }
static int n2s_close(int fd) { return is_dl(fd) ? 0 : (int)chk(close(fd)); }

/* ------------------------------------------------------------------ paths: everything lives in n2_dir */
const char *n2_dir = "/private/var/db/n2gnss";
static int nmapped;
static const char *mpath(const char *p, char *buf, size_t cap)
{
	if (!p || p[0] != '/') return p;                     /* relative: cwd is n2_dir */
	if (!strncmp(p, "/private/var/db/n2gnss", 22) || !strncmp(p, "/private/var/run/n2gnss", 23)) return p;
	if (!strncmp(p, "/dev/", 5) || !strncmp(p, "/proc/", 6) || !strncmp(p, "/sys/", 5)) {
		if (nmapped++ < 40) n2_logq("path %s refused", p);
		return 0;
	}
	const char *b = strrchr(p, '/'); b = b ? b + 1 : p;
	if (!*b) b = "root";
	snprintf(buf, cap, "%s/%s", n2_dir, b);
	if (nmapped++ < 40) n2_logq("path %s -> %s", p, buf);
	return buf;
}
#define MP(p) char mp_[512]; const char *P = mpath((p), mp_, sizeof mp_); if (!P) { n2_set_lerrno(2); return 0; }
static FILE *n2s_fopen(const char *p, const char *m) { MP(p); FILE *f = fopen(P, m); if (!f) fail(); return f; }
static int n2s_access(const char *p, int m) { char b[512]; const char *P = mpath(p, b, sizeof b); if (!P) { n2_set_lerrno(2); return -1; } return (int)chk(access(P, m)); }
static int n2s_chmod(const char *p, unsigned m) { char b[512]; const char *P = mpath(p, b, sizeof b); if (!P) { n2_set_lerrno(2); return -1; } return (int)chk(chmod(P, (unsigned short)m)); }
static int n2s_mkdir(const char *p, unsigned m) { char b[512]; const char *P = mpath(p, b, sizeof b); if (!P) { n2_set_lerrno(2); return -1; } return (int)chk(mkdir(P, (unsigned short)m)); }
static int n2s_remove(const char *p) { char b[512]; const char *P = mpath(p, b, sizeof b); if (!P) { n2_set_lerrno(2); return -1; } return (int)chk(remove(P)); }
static int n2s_rename(const char *a, const char *b)
{
	char x[512], y[512]; const char *A = mpath(a, x, sizeof x), *B = mpath(b, y, sizeof y);
	if (!A || !B) { n2_set_lerrno(2); return -1; }
	return (int)chk(rename(A, B));
}
static int oflags_l2d(int l)
{
	int d = l & 3;
	if (l & 0100) d |= D_O_CREAT;
	if (l & 0200) d |= D_O_EXCL;
	if (l & 0400) d |= D_O_NOCTTY;
	if (l & 01000) d |= D_O_TRUNC;
	if (l & 02000) d |= D_O_APPEND;
	if (l & 04000) d |= D_O_NONBLOCK;
	if (l & 040000) d |= D_O_DIRECTORY;                  /* arm64 Linux values */
	if (l & 0100000) d |= D_O_NOFOLLOW;
	if (l & 02000000) d |= D_O_CLOEXEC;
	return d;
}
static int oflags_d2l(int d)
{
	int l = d & 3;
	if (d & D_O_NONBLOCK) l |= 04000;
	if (d & D_O_APPEND) l |= 02000;
	return l;
}
static int n2s___open_2(const char *p, int fl)
{
	char b[512]; const char *P = mpath(p, b, sizeof b);
	if (!P) { n2_set_lerrno(2); return -1; }
	return (int)chk(open(P, oflags_l2d(fl), 0644));
}
/* struct stat: Linux arm64 generic (128 B) <- Darwin 64-bit-inode stat (144 B) */
static int n2s_stat(const char *p, unsigned char *ls)
{
	char b[512]; const char *P = mpath(p, b, sizeof b);
	if (!P) { n2_set_lerrno(2); return -1; }
	unsigned char d[160]; memset(d, 0, sizeof d);
	if (stat(P, d) < 0) return (int)fail();
	int32_t dev; uint16_t mode, nlink; uint64_t ino; uint32_t uid, gid; int32_t rdev; int64_t size, blocks; int32_t blksize;
	memcpy(&dev, d + 0, 4); memcpy(&mode, d + 4, 2); memcpy(&nlink, d + 6, 2); memcpy(&ino, d + 8, 8); memcpy(&uid, d + 16, 4);
	memcpy(&gid, d + 20, 4); memcpy(&rdev, d + 24, 4); memcpy(&size, d + 96, 8); memcpy(&blocks, d + 104, 8); memcpy(&blksize, d + 112, 4);
	memset(ls, 0, 128);
	uint64_t v64 = (uint32_t)dev; memcpy(ls + 0, &v64, 8); memcpy(ls + 8, &ino, 8);
	uint32_t m32 = mode, n32 = nlink; memcpy(ls + 16, &m32, 4); memcpy(ls + 20, &n32, 4); memcpy(ls + 24, &uid, 4); memcpy(ls + 28, &gid, 4);
	v64 = (uint32_t)rdev; memcpy(ls + 32, &v64, 8); memcpy(ls + 48, &size, 8); memcpy(ls + 56, &blksize, 4); memcpy(ls + 64, &blocks, 8);
	memcpy(ls + 72, d + 32, 16); memcpy(ls + 88, d + 48, 16); memcpy(ls + 104, d + 64, 16);   /* atime, mtime, ctime */
	return 0;
}
struct ldir { DIR *d; unsigned char ent[288]; };
static struct ldir *n2s_opendir(const char *p)
{
	char b[512]; const char *P = mpath(p, b, sizeof b);
	if (!P) { n2_set_lerrno(2); return 0; }
	DIR *d = opendir(P); if (!d) { fail(); return 0; }
	struct ldir *l = calloc(1, sizeof *l); l->d = d; return l;
}
static void *n2s_readdir(struct ldir *l)
{
	if (!l) return 0;
	unsigned char *e = readdir(l->d);
	if (!e) return 0;
	uint16_t nl; memcpy(&nl, e + 18, 2); if (nl > 255) nl = 255;
	memset(l->ent, 0, sizeof l->ent); memcpy(l->ent, e, 8);              /* d_ino */
	uint16_t rl = 280; memcpy(l->ent + 16, &rl, 2); l->ent[18] = e[20]; memcpy(l->ent + 19, e + 21, nl);
	return l->ent;
}
static int n2s_closedir(struct ldir *l) { if (!l) return -1; int r = closedir(l->d); free(l); return r; }
static char *n2s_strerror(int e) { return strerror(l2d_errno(e)); }

/* ------------------------------------------------------------------ fcntl / ioctl (variadic) */
long n2vi_fcntl(uint64_t *s, void *stk)
{
	struct lva a; lva_from(&a, s, stk, 2);
	int fd = (int)s[0], cmd = (int)s[1]; long arg = (long)lva_gp(&a);
	if (is_dl(fd)) {
		if (cmd == 3) return 2 | (fd == n2_l1_fd && l1_nonblock ? 04000 : 0);
		if (cmd == 4) { if (fd == n2_l1_fd) l1_nonblock = !!(arg & 04000); return 0; }
		if (cmd == 1 || cmd == 2) return 0;
		n2_set_lerrno(22); return -1;
	}
	switch (cmd) {
	case 1: case 2: return chk(fcntl(fd, cmd, (int)arg));        /* F_GETFD / F_SETFD, FD_CLOEXEC = 1 on both */
	case 3: { int r = fcntl(fd, 3); return r < 0 ? fail() : oflags_d2l(r); }
	case 4: return chk(fcntl(fd, 4, oflags_l2d((int)arg) & (D_O_NONBLOCK | D_O_APPEND)));
	}
	n2_logq("fcntl fd %d cmd %d unsupported", fd, cmd);
	n2_set_lerrno(22); return -1;
}
long n2vi_ioctl(uint64_t *s, void *stk)
{
	struct lva a; lva_from(&a, s, stk, 2);
	int fd = (int)s[0]; unsigned long req = (unsigned)s[1]; void *arg = (void *)(uintptr_t)lva_gp(&a);
	if (is_dl(fd)) {
		if (fd != n2_l1_fd || !n2_dev) { n2_set_lerrno(22); return -1; }
		int r = n2_dev->ioctl(req, arg);
		if (r < 0) { n2_set_lerrno(-r); return -1; }
		return r;
	}
	if (req == 0x5421) return chk(ioctl(fd, D_FIONBIO, arg));      /* FIONBIO */
	if (req == 0x541B) return chk(ioctl(fd, D_FIONREAD, arg));     /* FIONREAD */
	n2_logq("ioctl fd %d req %#lx unsupported", fd, req);
	n2_set_lerrno(22); return -1;
}

/* ------------------------------------------------------------------ printf family */
long n2vi_printf(uint64_t *s, void *stk)
{
	struct lva a; lva_from(&a, s, stk, 1);
	char b[1024]; size_t n = lfmt(b, sizeof b, (const char *)(uintptr_t)s[0], &a);
	n2_logq("libmnl printf: %s", b); return (long)n;
}
long n2vi_fprintf(uint64_t *s, void *stk)
{
	struct lva a, a2; lva_from(&a, s, stk, 2); a2 = a;
	const char *f = (const char *)(uintptr_t)s[1]; FILE *fp = (FILE *)(uintptr_t)s[0];
	char b[2048]; size_t n = lfmt(b, sizeof b, f, &a);
	if (n < sizeof b) return (long)fwrite(b, 1, n, fp) == (long)n ? (long)n : -1;
	char *h = malloc(n + 1); if (!h) return -1;
	lfmt(h, n + 1, f, &a2); long r = (long)fwrite(h, 1, n, fp) == (long)n ? (long)n : -1; free(h); return r;
}
long n2vi_sscanf(uint64_t *s, void *stk)
{
	struct lva a; lva_from(&a, s, stk, 2); uint64_t slot[40];
	const char *f = (const char *)(uintptr_t)s[1]; scan_args(f, &a, slot, 40);
	return vsscanf((const char *)(uintptr_t)s[0], f, (va_list)(void *)slot);
}
long n2vi_fscanf(uint64_t *s, void *stk)
{
	struct lva a; lva_from(&a, s, stk, 2); uint64_t slot[40];
	const char *f = (const char *)(uintptr_t)s[1]; scan_args(f, &a, slot, 40);
	return vfscanf((FILE *)(uintptr_t)s[0], f, (va_list)(void *)slot);
}
static int n2s_vsnprintf(char *b, size_t n, const char *f, struct lva *ap) { struct lva c = *ap; return (int)lfmt(b, n, f, &c); }
static int n2s___vsnprintf_chk(char *b, size_t n, int fl, size_t bl, const char *f, struct lva *ap)
{
	(void)fl; if (n > bl) { n2_log("FATAL __vsnprintf_chk overflow"); abort(); }
	struct lva c = *ap; return (int)lfmt(b, n, f, &c);
}
static int n2s___vsprintf_chk(char *b, int fl, size_t bl, const char *f, struct lva *ap)
{
	(void)fl; struct lva c = *ap, c2 = *ap; size_t cap = bl > (1u << 20) ? (1u << 20) : bl;
	size_t n = lfmt(0, 0, f, &c2);
	if (n >= cap) { n2_log("FATAL __vsprintf_chk overflow (%zu >= %zu)", n, cap); abort(); }
	return (int)lfmt(b, cap, f, &c);
}

/* ------------------------------------------------------------------ fortify helpers */
static void chkfail(const char *w) { n2_log("FATAL %s: buffer overflow detected in libmnl", w); abort(); }
static void *n2s___memcpy_chk(void *d, const void *s, size_t n, size_t dl) { if (n > dl) chkfail("__memcpy_chk"); return memcpy(d, s, n); }
static void *n2s___memset_chk(void *d, int c, size_t n, size_t dl) { if (n > dl) chkfail("__memset_chk"); return memset(d, c, n); }
static size_t n2s___strlen_chk(const char *s, size_t sl) { size_t n = strlen(s); if (n >= sl) chkfail("__strlen_chk"); return n; }
static char *n2s___strcpy_chk(char *d, const char *s, size_t dl) { if (strlen(s) >= dl) chkfail("__strcpy_chk"); return strcpy(d, s); }
static char *n2s___strcat_chk(char *d, const char *s, size_t dl) { if (strlen(d) + strlen(s) >= dl) chkfail("__strcat_chk"); return strcat(d, s); }
static char *n2s___strncat_chk(char *d, const char *s, size_t n, size_t dl)
{
	size_t sl = 0; while (sl < n && s[sl]) sl++;
	if (strlen(d) + sl >= dl) chkfail("__strncat_chk");
	return strncat(d, s, n);
}
static char *n2s___strncpy_chk(char *d, const char *s, size_t n, size_t dl) { if (n > dl) chkfail("__strncpy_chk"); return strncpy(d, s, n); }
static char *n2s___strncpy_chk2(char *d, const char *s, size_t n, size_t dl, size_t sl) { (void)sl; if (n > dl) chkfail("__strncpy_chk2"); return strncpy(d, s, n); }
static void n2s___stack_chk_fail(void) { chkfail("__stack_chk_fail"); }
static void n2s___cxa_finalize(void *d) { (void)d; }
static int n2s___libc_current_sigrtmin(void) { return 34; }

/* ------------------------------------------------------------------ time */
static int clk_l2d(int c)
{
	switch (c) {
	case 0: case 5: return D_CLOCK_REALTIME;                 /* REALTIME, REALTIME_COARSE */
	case 1: case 6: case 7: case 8: return D_CLOCK_MONOTONIC;/* MONOTONIC(_COARSE), BOOTTIME(_ALARM): Darwin's counts sleep */
	case 4: return D_CLOCK_MONOTONIC_RAW;
	case 2: return D_CLOCK_PROCESS_CPUTIME_ID;
	case 3: return D_CLOCK_THREAD_CPUTIME_ID;
	}
	return D_CLOCK_MONOTONIC;
}
/* guest pointers are never assumed aligned (gnssd2 SIGBUS): 8-byte values in/out of libmnl memory go through memcpy */
static int n2s_clock_gettime(int c, void *t) { struct timespec v; if (clock_gettime(clk_l2d(c), &v) < 0) return (int)fail(); if (t) memcpy(t, &v, sizeof v); return 0; }
static int n2s_gettimeofday(long *tv, void *tz)
{
	struct timeval d; if (gettimeofday(&d, 0) < 0) return (int)fail();
	if (tv) { long x[2] = { d.tv_sec, d.tv_usec }; memcpy(tv, x, sizeof x); }
	if (tz) memset(tz, 0, 8);
	return 0;
}
static uint64_t mono_ns(void) { struct timespec t; clock_gettime(D_CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }
static uint64_t real_ns(void) { struct timespec t; clock_gettime(D_CLOCK_REALTIME, &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }

/* ------------------------------------------------------------------ pthread objects: side table + 32-bit handle in the guest word
 * gnssd2 (phone): SIGBUS alignment fault (ESR 0x92000021) at `ldar x22,[x0]` in the old obj_get: bionic arm64 pthread_mutex_t
 * is int32_t[10] and pthread_cond_t int32_t[12], so libmnl's objects are only 4-byte aligned (0x...76c) and a 64-bit
 * load-acquire/CAS on them faults (ARMv8.2: no LSE2). Now NOTHING 8-byte or 64-bit-atomic touches guest memory: the Darwin
 * object (64 B, calloc = 16-aligned) lives in n2mnld's table, the guest word 0 (int32, 4-aligned) holds only
 * H_TAG | index, read/CAS'd with 32-bit atomics (legal at 4-byte alignment). Bionic static initialisers have bits 16..31 = 0
 * (0, 0x4000 recursive, 0x8000 errorcheck), so a word with H_TAG is ours. Destroyed indices are reused (free list). */
#define H_TAG 0x80000000u
#define H_CHUNK 1024
#define H_NCHUNK 256                                  /* up to 262144 live objects */
static void **h_chunk[H_NCHUNK];
static pthread_mutex_t h_m; static int h_m_ok;      /* our own (static, 8-aligned) lock for allocation only */
static uint32_t h_next = 1, *h_free; static uint32_t h_nfree, h_capfree;
static long h_live, h_peak;
static void h_lock(void) { if (!h_m_ok) { n2_log("FATAL pthread shim used before n2_shim_init"); abort(); } pthread_mutex_lock(&h_m); }
static void *h_at(uint32_t i) { void **c = __atomic_load_n(&h_chunk[i / H_CHUNK], __ATOMIC_ACQUIRE); return c ? __atomic_load_n(&c[i % H_CHUNK], __ATOMIC_ACQUIRE) : 0; }
static uint32_t h_put(void *o)                       /* -> index (0 = table full) */
{
	h_lock();
	uint32_t i = h_nfree ? h_free[--h_nfree] : (h_next < H_CHUNK * H_NCHUNK ? h_next++ : 0);
	if (i) {
		void **c = h_chunk[i / H_CHUNK];
		if (!c) { c = calloc(H_CHUNK, sizeof *c); if (!c) { pthread_mutex_unlock(&h_m); return 0; } __atomic_store_n(&h_chunk[i / H_CHUNK], c, __ATOMIC_RELEASE); }
		__atomic_store_n(&c[i % H_CHUNK], o, __ATOMIC_RELEASE);
		if (++h_live > h_peak) { h_peak = h_live; if ((h_peak & (h_peak - 1)) == 0 && h_peak >= 64) n2_logq("pthread shim: %ld live mutex/cond objects", h_peak); }
	}
	pthread_mutex_unlock(&h_m);
	return i;
}
static void h_del(uint32_t i)
{
	h_lock();
	void **c = h_chunk[i / H_CHUNK];
	if (c && c[i % H_CHUNK]) {
		__atomic_store_n(&c[i % H_CHUNK], (void *)0, __ATOMIC_RELEASE); h_live--;
		if (h_nfree == h_capfree) { uint32_t n = h_capfree ? h_capfree * 2 : 256; uint32_t *f = malloc(n * sizeof *f); if (f) { if (h_nfree) memcpy(f, h_free, h_nfree * sizeof *f); free(h_free); h_free = f; h_capfree = n; } }
		if (h_nfree < h_capfree) h_free[h_nfree++] = i;    /* else leaked index (out of memory) */
	}
	pthread_mutex_unlock(&h_m);
}
static void *obj_new(int is_cond, int recursive)
{
	void *o = calloc(1, 64);
	if (!o) { n2_log("FATAL pthread shim: out of memory"); abort(); }
	if (is_cond) pthread_cond_init(o, 0);
	else if (recursive) { pthread_mutexattr_t at; pthread_mutexattr_init(&at); pthread_mutexattr_settype(&at, D_PTHREAD_MUTEX_RECURSIVE); pthread_mutex_init(o, &at); }
	else pthread_mutex_init(o, 0);
	return o;
}
static void obj_free(void *o, int is_cond) { if (is_cond) pthread_cond_destroy(o); else pthread_mutex_destroy(o); free(o); }
static void *obj_get(void *lobj, int is_cond)
{
	uint32_t *slot = lobj;                             /* 4-byte aligned guest word: 32-bit atomics only */
	uint32_t w = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
	for (;;) {
		if ((w & H_TAG) && (w & ~H_TAG)) { void *o = h_at(w & ~H_TAG); if (o) return o; }
		void *o = obj_new(is_cond, !is_cond && ((w >> 14) & 3) == 1);   /* bionic PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP */
		uint32_t i = h_put(o);
		if (!i) { n2_log("FATAL pthread shim: object table full"); abort(); }
		if (__atomic_compare_exchange_n(slot, &w, H_TAG | i, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return o;
		h_del(i); obj_free(o, is_cond);                  /* another thread won: w now holds its handle */
	}
}
static void obj_destroy(void *lobj, int is_cond, size_t lsize)
{
	uint32_t *slot = lobj, w = __atomic_exchange_n(slot, 0u, __ATOMIC_ACQ_REL);
	if ((w & H_TAG) && (w & ~H_TAG)) { void *o = h_at(w & ~H_TAG); if (o) { h_del(w & ~H_TAG); obj_free(o, is_cond); } }
	memset((char *)lobj + 4, 0, lsize - 4);           /* byte-wise: no alignment assumption */
}
static int n2s_pthread_mutex_init(void *m, const long *attr)
{
	long a = 0; if (attr) memcpy(&a, attr, sizeof a);
	/* the old content is NOT trusted (uninitialised or reused malloc memory may hold a stale handle that now names another
	 * live object): overwrite like bionic does; a re-init of a live mutex leaks one 64-byte object at worst. */
	uint32_t w = (a & 0xf) == 1 ? (1u << 14) : 0;     /* bionic mutexattr type RECURSIVE = 1 */
	memset((char *)m + 4, 0, 36);
	__atomic_store_n((uint32_t *)m, w, __ATOMIC_RELEASE);
	obj_get(m, 0); return 0;
}
static int n2s_pthread_mutex_destroy(void *m) { obj_destroy(m, 0, 40); return 0; }
static int n2s_pthread_mutex_lock(void *m) { return pmap(pthread_mutex_lock(obj_get(m, 0))); }
static int n2s_pthread_mutex_unlock(void *m) { return pmap(pthread_mutex_unlock(obj_get(m, 0))); }
static int n2s_pthread_cond_init(void *c, const void *a) { (void)a; memset((char *)c + 4, 0, 44); __atomic_store_n((uint32_t *)c, 0u, __ATOMIC_RELEASE); obj_get(c, 1); return 0; }
static int n2s_pthread_cond_destroy(void *c) { obj_destroy(c, 1, 48); return 0; }
static int n2s_pthread_cond_signal(void *c) { return pmap(pthread_cond_signal(obj_get(c, 1))); }
static int n2s_pthread_cond_wait(void *c, void *m) { return pmap(pthread_cond_wait(obj_get(c, 1), obj_get(m, 0))); }
static int n2s_pthread_cond_timedwait(void *c, void *m, const void *t)
{
	struct timespec v; if (!t) { n2_set_lerrno(22); return 22; } memcpy(&v, t, sizeof v);
	return pmap(pthread_cond_timedwait(obj_get(c, 1), obj_get(m, 0), &v));
}
static int n2s_pthread_create(long *lt, const unsigned char *lattr, void *(*fn)(void *), void *arg)
{
	pthread_attr_t at; pthread_attr_init(&at);
	size_t ss = 1u << 20;                                  /* bionic default is 1 MiB - guard; Darwin's 512 KiB is too small */
	if (lattr) {
		uint32_t fl; size_t bs; memcpy(&fl, lattr, 4); memcpy(&bs, lattr + 16, 8);
		if (bs > ss && bs < (64u << 20)) ss = bs;
		if (fl & 1) pthread_attr_setdetachstate(&at, D_PTHREAD_CREATE_DETACHED);
	}
	pthread_attr_setstacksize(&at, ss);
	pthread_t t; int r = pthread_create(&t, &at, fn, arg); pthread_attr_destroy(&at);
	if (!r && lt) { long v = (long)t; memcpy(lt, &v, sizeof v); }
	n2_logq("pthread_create fn=%p -> %d", (void *)fn, r);
	return pmap(r);
}
extern int pthread_join(pthread_t, void **);
static int n2s_pthread_join(long t, void **rv) { void *v = 0; int r = pthread_join((pthread_t)t, &v); if (!r && rv) memcpy(rv, &v, sizeof v); return pmap(r); }
static int nkill;
static int n2s_pthread_kill(long t, int sig) { if (nkill++ < 5) n2_log("libmnl pthread_kill(%#lx, %d) ignored (n2mnld never stops the engine)", t, sig); return 0; }
static int n2s_pthread_getschedparam(long t, int *pol, int *prm) { (void)t; if (pol) *pol = 0; if (prm) *prm = 0; return 0; }
static int n2s_pthread_setschedparam(long t, int pol, const int *prm) { (void)t; (void)pol; (void)prm; return 0; }
static int n2s_sched_get_priority_min(int p) { return p ? 1 : 0; }
static int n2s_sched_get_priority_max(int p) { return p ? 99 : 0; }

/* ------------------------------------------------------------------ signals: accepted, never installed */
static int n2s_sigemptyset(void *s) { if (s) memset(s, 0, 8); return 0; }
static int n2s_sigaddset(void *s, int n) { if (!s || n < 1 || n > 64) { n2_set_lerrno(22); return -1; } uint64_t v; memcpy(&v, s, 8); v |= 1ull << (n - 1); memcpy(s, &v, 8); return 0; }
static int n2s_sigprocmask(int how, const void *s, void *o) { (void)how; (void)s; if (o) memset(o, 0, 8); return 0; }
static int n2s_sigaction(int n, const void *a, void *o) { (void)n; (void)a; if (o) memset(o, 0, 32); return 0; }
static void *n2s_signal(int n, void *h) { (void)n; (void)h; return 0; }

/* ------------------------------------------------------------------ POSIX timers (SIGEV_THREAD only) */
struct ltimer {
	pthread_mutex_t m; pthread_cond_t c; void (*fn)(uint64_t); uint64_t val;
	int armed, dead, clk; uint64_t next_ns, ival_ns;
};
static void *timer_thread(void *p)
{
	struct ltimer *t = p;
	pthread_mutex_lock(&t->m);
	while (!t->dead) {
		if (!t->armed) { pthread_cond_wait(&t->c, &t->m); continue; }
		uint64_t now = mono_ns();
		if (now < t->next_ns) {
			uint64_t d = t->next_ns - now; struct timespec r = { (time_t)(d / 1000000000ull), (long)(d % 1000000000ull) };
			pthread_cond_timedwait_relative_np(&t->c, &t->m, &r); continue;
		}
		if (t->ival_ns) { t->next_ns += t->ival_ns; if (t->next_ns < now) t->next_ns = now + t->ival_ns; } else t->armed = 0;
		void (*fn)(uint64_t) = t->fn; uint64_t v = t->val;
		pthread_mutex_unlock(&t->m);
		fn(v);
		pthread_mutex_lock(&t->m);
	}
	pthread_mutex_unlock(&t->m);
	return 0;
}
static int n2s_timer_create(int clk, const unsigned char *sev, void *tid)
{
	int notify; if (sev) memcpy(&notify, sev + 12, 4); else notify = 0;
	if (!sev || notify != 2) { n2_log("timer_create notify %d unsupported (only SIGEV_THREAD)", sev ? notify : -1); n2_set_lerrno(22); return -1; }
	struct ltimer *t = calloc(1, sizeof *t);
	pthread_mutex_init(&t->m, 0); pthread_cond_init(&t->c, 0); t->clk = clk;
	memcpy(&t->val, sev, 8); memcpy(&t->fn, sev + 16, 8);
	pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 1u << 20); pthread_attr_setdetachstate(&at, D_PTHREAD_CREATE_DETACHED);
	pthread_t th; int r = pthread_create(&th, &at, timer_thread, t); pthread_attr_destroy(&at);
	if (r) { n2_set_lerrno(n2_d2l_errno(r)); return -1; }
	memcpy(tid, &t, sizeof t); return 0;
}
static int n2s_timer_settime(struct ltimer *t, int flags, const void *itsp, void *old)
{
	if (!t || !itsp) { n2_set_lerrno(22); return -1; }
	int64_t its[4]; memcpy(its, itsp, sizeof its);
	pthread_mutex_lock(&t->m);
	if (old) memset(old, 0, 32);
	uint64_t ival = (uint64_t)its[0] * 1000000000ull + (uint64_t)its[1], val = (uint64_t)its[2] * 1000000000ull + (uint64_t)its[3];
	if (!val) t->armed = 0;
	else {
		uint64_t now = mono_ns();
		if (flags & 1) {                                   /* TIMER_ABSTIME on the timer's clock */
			uint64_t cnow = t->clk == 0 ? real_ns() : now;
			t->next_ns = val > cnow ? now + (val - cnow) : now;
		} else t->next_ns = now + val;
		t->ival_ns = ival; t->armed = 1;
	}
	pthread_cond_signal(&t->c); pthread_mutex_unlock(&t->m);
	return 0;
}
static int n2s_timer_delete(struct ltimer *t)
{
	if (!t) { n2_set_lerrno(22); return -1; }
	pthread_mutex_lock(&t->m); t->dead = 1; t->armed = 0; pthread_cond_signal(&t->c); pthread_mutex_unlock(&t->m);
	return 0;                                             /* the struct is leaked on purpose (the thread may still hold it) */
}

/* ------------------------------------------------------------------ epoll -> kqueue */
static int n2s_epoll_create(int n) { (void)n; return (int)chk(kqueue()); }
static int n2s_epoll_ctl(int ep, int op, int fd, const unsigned char *ev)
{
	uint32_t evs = 0; uint64_t data = 0;
	if (ev) { memcpy(&evs, ev, 4); memcpy(&data, ev + 8, 8); }
	struct kevent k[2]; int n = 0; uint16_t extra = (evs & (1u << 30) ? D_EV_ONESHOT : 0) | (evs & (1u << 31) ? D_EV_CLEAR : 0);
	if (op == 2) {                                         /* DEL */
		struct kevent d[2] = { { (uintptr_t)fd, D_EVFILT_READ, D_EV_DELETE, 0, 0, 0 }, { (uintptr_t)fd, D_EVFILT_WRITE, D_EV_DELETE, 0, 0, 0 } };
		kevent(ep, d, 1, 0, 0, 0); kevent(ep, d + 1, 1, 0, 0, 0); return 0;
	}
	if (op != 1 && op != 3) { n2_set_lerrno(22); return -1; }
	if (op == 3) { struct kevent d[2] = { { (uintptr_t)fd, D_EVFILT_READ, D_EV_DELETE, 0, 0, 0 }, { (uintptr_t)fd, D_EVFILT_WRITE, D_EV_DELETE, 0, 0, 0 } }; kevent(ep, d, 1, 0, 0, 0); kevent(ep, d + 1, 1, 0, 0, 0); }
	if (evs & 0x1) k[n++] = (struct kevent){ (uintptr_t)fd, D_EVFILT_READ, (uint16_t)(D_EV_ADD | D_EV_ENABLE | extra), 0, 0, (void *)(uintptr_t)data };
	if (evs & 0x4) k[n++] = (struct kevent){ (uintptr_t)fd, D_EVFILT_WRITE, (uint16_t)(D_EV_ADD | D_EV_ENABLE | extra), 0, 0, (void *)(uintptr_t)data };
	if (n && kevent(ep, k, n, 0, 0, 0) < 0) return (int)fail();
	return 0;
}
static int n2s_epoll_pwait(int ep, unsigned char *evs, int max, int ms, const void *sm)
{
	(void)sm; struct kevent k[32]; if (max > 32) max = 32; if (max < 1) { n2_set_lerrno(22); return -1; }
	struct timespec to = { ms / 1000, (long)(ms % 1000) * 1000000L };
	int n = kevent(ep, 0, 0, k, max, ms < 0 ? 0 : &to);
	if (n < 0) return (int)fail();
	for (int i = 0; i < n; i++) {
		uint32_t e = k[i].filter == D_EVFILT_READ ? 0x1 : k[i].filter == D_EVFILT_WRITE ? 0x4 : 0;
		if (k[i].flags & D_EV_EOF) e |= 0x10;
		if (k[i].flags & D_EV_ERROR) e |= 0x8;
		uint64_t d = (uint64_t)(uintptr_t)k[i].udata;
		memset(evs + 16 * i, 0, 16); memcpy(evs + 16 * i, &e, 4); memcpy(evs + 16 * i + 8, &d, 8);
	}
	return n;
}

/* ------------------------------------------------------------------ sockets (debug servers): loopback only */
static int n2s_socket(int dom, int type, int proto)
{
	if (dom != 1 && dom != 2) { n2_log("socket domain %d refused (only AF_UNIX / AF_INET loopback)", dom); n2_set_lerrno(97); return -1; }
	int s = socket(dom, type & 0xf, proto);
	if (s < 0) return (int)fail();
	if (type & 04000) fcntl(s, 4, D_O_NONBLOCK);
	if (type & 02000000) fcntl(s, 2, 1);
	int one = 1; setsockopt(s, D_SOL_SOCKET, 0x1022, &one, 4);   /* SO_NOSIGPIPE */
	return s;
}
static int sa_l2d(const unsigned char *l, unsigned ll, unsigned char *d, unsigned *dl)
{
	uint16_t fam; if (!l || ll < 2) return -1; memcpy(&fam, l, 2);
	if (fam == 2 && ll >= 8) {
		struct d_sockaddr_in a; memset(&a, 0, sizeof a); a.len = 16; a.family = 2; memcpy(&a.port, l + 2, 2);
		a.addr = 0x0100007fu;                              /* forced 127.0.0.1: the engine's debug ports never leave the phone */
		memcpy(d, &a, 16); *dl = 16; return 0;
	}
	if (fam == 1) {
		struct d_sockaddr_un a; memset(&a, 0, sizeof a); a.family = 1;
		const char *p = (const char *)l + 2; unsigned pl = ll > 2 ? ll - 2 : 0; if (pl > 107) pl = 107;
		if (pl && p[0] == 0) snprintf(a.path, sizeof a.path, "%s/abs-%.*s", n2_dir, (int)(pl - 1 < 60 ? pl - 1 : 60), p + 1);
		else { char tmp[128]; memcpy(tmp, p, pl); tmp[pl] = 0; char b[512]; const char *P = mpath(tmp, b, sizeof b); if (!P) return -1; snprintf(a.path, sizeof a.path, "%s", P); }
		a.len = (uint8_t)(2 + strlen(a.path) + 1); memcpy(d, &a, sizeof a); *dl = a.len; return 0;
	}
	return -1;
}
static int n2s_bind(int s, const unsigned char *a, unsigned al)
{
	unsigned char d[128]; unsigned dl;
	if (sa_l2d(a, al, d, &dl)) { n2_set_lerrno(97); return -1; }
	if (d[1] == 2) { uint16_t pt; memcpy(&pt, d + 2, 2); n2_log("libmnl debug socket bound to 127.0.0.1:%u (loopback only)", (unsigned)((pt >> 8) | ((pt & 0xff) << 8))); }
	else n2_log("libmnl unix socket %s", (const char *)d + 2);
	return (int)chk(bind(s, d, dl));
}
static int n2s_listen(int s, int n) { return (int)chk(listen(s, n)); }
static int n2s_accept(int s, unsigned char *a, unsigned *al)
{
	unsigned char d[128]; unsigned dl = sizeof d;
	int r = accept(s, d, &dl);
	if (r < 0) return (int)fail();
	int one = 1; setsockopt(r, D_SOL_SOCKET, 0x1022, &one, 4);
	if (a && al && *al >= 16 && d[1] == 2) { memset(a, 0, 16); uint16_t f = 2; memcpy(a, &f, 2); memcpy(a + 2, d + 2, 6); *al = 16; }
	else if (a && al && *al >= 2) { uint16_t f = d[1]; memcpy(a, &f, 2); *al = 2; }
	return r;
}
static int n2s_setsockopt(int s, int lvl, int opt, const void *v, unsigned vl)
{
	if (lvl != 1) return (int)chk(setsockopt(s, lvl, opt, v, vl));       /* IPPROTO_TCP etc.: same numbers */
	int d;
	switch (opt) {
	case 2: d = 0x4; break; case 9: d = 0x8; break; case 7: d = 0x1001; break; case 8: d = 0x1002; break;
	case 15: d = 0x200; break; case 20: d = 0x1006; break; case 21: d = 0x1005; break; case 13: d = 0x80; break;
	default: n2_logq("setsockopt SOL_SOCKET %d ignored", opt); return 0;
	}
	if (d == 0x1006 || d == 0x1005) {                      /* struct timeval: Linux {long, long} -> Darwin {long, int} */
		struct timeval tv = { 0, 0 }; if (v && vl >= 16) { long x[2]; memcpy(x, v, 16); tv.tv_sec = x[0]; tv.tv_usec = (int)x[1]; }
		return (int)chk(setsockopt(s, D_SOL_SOCKET, d, &tv, sizeof tv));
	}
	return (int)chk(setsockopt(s, D_SOL_SOCKET, d, v, vl));
}
static int n2s_pipe(int *fds) { return (int)chk(pipe(fds)); }

/* ------------------------------------------------------------------ termios (only used for a real UART name; not on our fd path) */
static int ntty;
static int n2s_tcgetattr(int fd, void *t) { if (ntty++ < 3) n2_log("libmnl tcgetattr(%d): no UART on this path", fd); if (t) memset(t, 0, 36); return 0; }
static int n2s_tcsetattr(int fd, int a, const void *t) { (void)fd; (void)a; (void)t; return 0; }
static int n2s_tcflush(int fd, int q) { (void)fd; (void)q; return 0; }
static int n2s_cfsetispeed(void *t, unsigned s) { (void)t; (void)s; return 0; }
static int n2s_cfsetospeed(void *t, unsigned s) { (void)t; (void)s; return 0; }

/* ------------------------------------------------------------------ misc */
static int n2s_property_get(const char *k, char *v, const char *def)
{
	const char *r = def ? def : "";
	if (k && !strcmp(k, "ro.build.type")) r = "user";
	size_t n = strlen(r); if (n > 91) n = 91;
	memcpy(v, r, n); v[n] = 0; return (int)n;
}
static void n2s_sincos(double x, void *s, void *c) { double a = sin(x), b = cos(x); memcpy(s, &a, 8); memcpy(c, &b, 8); }

/* ------------------------------------------------------------------ the table */
extern long n2v_printf(void), n2v_fprintf(void), n2v_sscanf(void), n2v_fscanf(void), n2v_fcntl(void), n2v_ioctl(void);
extern double exp2(double), atof(const char *), log10(double), log(double), fmod(double, double), asin(double), atan2(double, double);
extern double exp(double), pow(double, double), tan(double), atan(double), acos(double), modf(double, double *);
extern float fmodf(float, float);
extern int atoi(const char *), fclose(FILE *), fseek(FILE *, long, int), fputs(const char *, FILE *), fputc(int, FILE *), puts(const char *);
extern int rand(void), pthread_equal(pthread_t, pthread_t);
extern void srand(unsigned), setbuf(FILE *, char *), qsort(void *, size_t, size_t, int (*)(const void *, const void *));
extern void pthread_exit(void *) __attribute__((noreturn));
extern size_t fread(void *, size_t, size_t, FILE *); extern long ftell(FILE *);
extern char *fgets(char *, int, FILE *), *strtok(char *, const char *), *inet_ntoa(uint32_t), *ctime(const time_t *);
extern unsigned long strtoul(const char *, char **, int); extern time_t time(time_t *), mktime(void *);
extern void *gmtime(const time_t *), *localtime(const time_t *); extern pthread_t pthread_self(void);
static const struct { const char *n; void *f; } tab[] = {
#define D(x) { #x, (void *)x }
#define S(x) { #x, (void *)n2s_##x }
#define V(x) { #x, (void *)n2v_##x }
	D(memset), D(memcpy), D(exp2), D(strcmp), D(atoi), D(strlen), D(abort), D(pthread_self), D(usleep), D(exit), D(strncmp),
	D(malloc), D(free), D(setbuf), D(fflush), D(fclose), D(fseek), D(fread), D(fwrite), D(strstr), D(strtok), D(atof), D(fputs),
	D(strchr), D(strtoul), D(fputc), D(time), D(gmtime), D(log10), D(srand), D(rand), D(qsort), D(mktime), D(pthread_equal),
	D(strrchr), D(strncpy), D(localtime), D(sleep), D(pthread_exit), D(inet_ntoa), D(log), D(fmod), D(fmodf), D(cos), D(asin),
	D(sin), D(atan2), D(exp), D(pow), D(tan), D(atan), D(puts), D(acos), D(strncat), D(ftell), D(ctime), D(strcpy), D(modf),
	D(fgets),
	S(__cxa_finalize), S(__stack_chk_fail), S(__write_chk), S(vsnprintf), S(socket), S(bind), S(listen), S(epoll_create),
	S(pipe), S(__errno), S(epoll_ctl), S(close), S(accept), S(write), S(sigemptyset), S(sigaddset), S(sigprocmask),
	S(epoll_pwait), S(read), S(__strlen_chk), S(__vsnprintf_chk), S(__memcpy_chk), S(fopen), S(remove), S(strerror),
	S(__strncpy_chk2), S(__strncat_chk), S(access), S(chmod), S(rename), S(clock_gettime), S(pthread_mutex_init),
	S(pthread_mutex_destroy), S(pthread_mutex_lock), S(pthread_mutex_unlock), S(__read_chk), S(__vsprintf_chk),
	S(__strncpy_chk), S(__open_2), S(tcgetattr), S(tcflush), S(tcsetattr), S(cfsetispeed), S(cfsetospeed), S(stat),
	S(opendir), S(readdir), S(closedir), S(mkdir), S(sigaction), S(timer_settime), S(timer_delete), S(setsockopt),
	S(pthread_cond_signal), S(pthread_cond_wait), S(pthread_getschedparam), S(sched_get_priority_min),
	S(sched_get_priority_max), S(pthread_setschedparam), S(__libc_current_sigrtmin), S(pthread_kill), S(signal),
	S(pthread_join), S(pthread_create), S(property_get), S(timer_create), S(pthread_cond_destroy), S(pthread_cond_init),
	S(gettimeofday), S(pthread_cond_timedwait), S(sincos), S(__memset_chk), S(__strcpy_chk), S(__strcat_chk),
	V(sscanf), V(fcntl), V(printf), V(fprintf), V(fscanf), V(ioctl),
#undef D
#undef S
#undef V
};
void *n2_shim_lookup(const char *name)
{
	for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++) if (!strcmp(tab[i].n, name)) return tab[i].f;
	return 0;
}
void n2_shim_init(void)
{
	if (!pthread_key_create(&errkey, 0)) errkey_ok = 1;
	if (!pthread_mutex_init(&h_m, 0)) h_m_ok = 1;
	signal(D_SIGPIPE, D_SIG_IGN);                          /* NMEA FIFO / debug sockets without a reader */
}
