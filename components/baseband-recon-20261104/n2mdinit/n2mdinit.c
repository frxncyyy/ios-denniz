/*
 * n2mdinit: a minimal, libc-free replacement for MediaTek's ccci_mdinit + ccci_rpcd (aarch64 Linux, raw syscalls).
 * It starts the MT6893 modem (MD1) through the CCCI ioctls and serves the two user-space services the modem needs while it boots:
 *   /dev/ccci_fs  - the modem's file service (NVRAM, protection data, OTA config, CA certificates)
 *   /dev/ccci_rpc - small RPC calls (system property reads)
 * Written from a trace of the vendor daemons (see PROTOCOL.md); the same sequence is what an XNU port has to reproduce.
 * Usage: n2mdinit          (runs in the foreground; fork()s one process per service)
 */
typedef unsigned long u64; typedef long i64; typedef unsigned int u32; typedef int i32; typedef unsigned char u8; typedef unsigned short u16;
static i64 sc(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
	register i64 x8 __asm__("x8") = n; register i64 x0 __asm__("x0") = a; register i64 x1 __asm__("x1") = b;
	register i64 x2 __asm__("x2") = c; register i64 x3 __asm__("x3") = d; register i64 x4 __asm__("x4") = e; register i64 x5 __asm__("x5") = f;
	__asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory", "cc");
	return x0;
}
#define AT_FDCWD (-100)
#define O_RDONLY 0
#define O_RDWR 2
#define O_CREAT 0100
#define O_DIRECTORY 040000   /* asm-generic (arm64), not x86 */
static i64 sys_openat(const char *p, int fl, int mode) { return sc(56, AT_FDCWD, (i64)p, fl, mode, 0, 0); }
static i64 sys_close(int fd) { return sc(57, fd, 0, 0, 0, 0, 0); }
static i64 sys_read(int fd, void *b, u64 n) { return sc(63, fd, (i64)b, n, 0, 0, 0); }
static i64 sys_write(int fd, const void *b, u64 n) { return sc(64, fd, (i64)b, n, 0, 0, 0); }
static i64 sys_lseek(int fd, i64 o, int w) { return sc(62, fd, o, w, 0, 0, 0); }
static i64 sys_ioctl(int fd, u32 req, void *arg) { return sc(29, fd, req, (i64)arg, 0, 0, 0); }
static i64 sys_fstatat(const char *p, void *st, int fl) { return sc(79, AT_FDCWD, (i64)p, (i64)st, fl, 0, 0); }
static i64 sys_fstat(int fd, void *st) { return sc(80, fd, (i64)st, 0, 0, 0, 0); }
static i64 sys_getdents(int fd, void *b, u64 n) { return sc(61, fd, (i64)b, n, 0, 0, 0); }
static i64 sys_unlink(const char *p) { return sc(35, AT_FDCWD, (i64)p, 0, 0, 0, 0); }
static i64 sys_mkdirat(const char *p, int m) { return sc(34, AT_FDCWD, (i64)p, m, 0, 0, 0); }
static void sys_sleep_ms(long ms) { i64 ts[2] = { ms / 1000, (ms % 1000) * 1000000L }; sc(101, (i64)ts, 0, 0, 0, 0, 0); }
static i64 sys_fork(void) { return sc(220, 17 /* SIGCHLD */, 0, 0, 0, 0, 0); }
static void sys_exit(int c) { sc(94, c, 0, 0, 0, 0, 0); for (;;) {} }
static i64 sys_prctl(int op, const void *a) { return sc(167, op, (i64)a, 0, 0, 0, 0); }

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void mcpy(void *d, const void *s, u64 n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; }
static void mset(void *d, int v, u64 n) { u8 *a = d; while (n--) *a++ = (u8)v; }
static void say(const char *s) { sys_write(2, s, slen(s)); }
static void sayn(const char *s, i64 v) { char b[32]; int n = 0; char t[24]; int k = 0; i64 x = v; if (x < 0) { b[n++] = '-'; x = -x; } if (!x) t[k++] = '0'; while (x) { t[k++] = '0' + x % 10; x /= 10; } while (k) b[n++] = t[--k]; b[n++] = '\n'; say(s); sys_write(2, b, n); }
static void sayhex(const char *s, u32 v) { char b[16]; static const char h[] = "0123456789abcdef"; int n = 0; for (int i = 28; i >= 0; i -= 4) b[n++] = h[(v >> i) & 15]; b[n++] = '\n'; say(s); sys_write(2, b, n); }

/* ---- CCCI ioctls (ccci_core.h, magic 'C') ---- */
#define IOC_SET_MD_IMG_EXIST 0x4004431dU
#define IOC_SET_BOOT_DATA    0x4040432fU
#define IOC_GET_MD_TYPE      0x8004431fU
#define IOC_DO_START_MD      0x0000430dU
#define IOC_GET_MD_STATE     0x80044301U
#define IOC_DO_STOP_MD       0x0000430cU

/* ---- wire format ---- */
struct hdr { u32 data0, len, chseq, resv; };
#define FS_RX 14
#define MAXMSG 4096
#define BIG 131072
static u8 req[BIG + 4096], rep[BIG + 4096];
static u32 rd32(const u8 *p) { u32 v; mcpy(&v, p, 4); return v; }
struct arg { const u8 *p; u32 len; };
static int parse(const u8 *b, u32 total, struct arg *a, int max, u32 *op) {
	if (total < 24) return -1;
	*op = rd32(b + 16); u32 argc = rd32(b + 20); u32 off = 24; int n = 0;
	for (u32 i = 0; i < argc && n < max; i++) {
		if (off + 4 > total) break;
		u32 l = rd32(b + off); off += 4; if (off + l > total) break;
		a[n].p = b + off; a[n].len = l; n++; off += (l + 3) & ~3u;
	}
	return n;
}
/* reply builder */
static u32 rlen;
static void r_begin(const u8 *rq, u32 op) {
	const struct hdr *h = (const struct hdr *)rq; struct hdr o;
	o.data0 = h->data0; o.len = 0; o.chseq = (h->chseq & 0xffff0000u) | ((h->chseq & 0xffff) + 1); o.resv = h->resv;
	mcpy(rep, &o, 16); u32 v = op | 0xffff0000u; mcpy(rep + 16, &v, 4); v = 0; mcpy(rep + 20, &v, 4); rlen = 24;
}
static void r_arg(const void *d, u32 l) {
	mcpy(rep + rlen, &l, 4); rlen += 4; mcpy(rep + rlen, d, l); u32 pad = ((l + 3) & ~3u) - l; mset(rep + rlen + l, 0, pad); rlen += l + pad;
	u32 c = rd32(rep + 20) + 1; mcpy(rep + 20, &c, 4);
}
static void r_u32(u32 v) { r_arg(&v, 4); }
/* A reply larger than one CCCI packet (3476 bytes) is written as several packets: each carries the 16-byte header (same channel/seq),
 * data0 = 0x80000000 on every packet except the last, and the payload (op id, argc, arguments...) simply continues. */
#define FRAG 3476
static u8 frag[FRAG];
/* every packet is: 16-byte header, then the 4-byte op id again (the kernel treats it as per-packet overhead), then a chunk of the body */
static int r_send(int fd) {
	{ char hb[2 * 1000 + 6]; int hn = 0; static const char H[] = "0123456789abcdef"; hb[hn++] = 'R'; hb[hn++] = 'E'; hb[hn++] = 'P'; hb[hn++] = ' '; for (u32 q = 0; q < rlen && q < 1000; q++) { hb[hn++] = H[rep[q] >> 4]; hb[hn++] = H[rep[q] & 15]; } hb[hn++] = '\n'; sys_write(2, hb, hn); }
	u32 total = rlen; u32 opw = rd32(rep + 16); struct hdr h; mcpy(&h, rep, 16);
	if (total <= FRAG) { mcpy(rep + 4, &rlen, 4); return (int)sys_write(fd, rep, rlen); }
	u32 off = 20; /* body starts after the op id */
	while (off < total) {
		u32 n = total - off; int more = 0; if (n > FRAG - 20) { n = FRAG - 20; more = 1; }
		struct hdr o = h; o.data0 = more ? 0x80000000u : 0; o.len = n + 20; mcpy(frag, &o, 16); mcpy(frag + 16, &opw, 4); mcpy(frag + 20, rep + off, n);
		if (sys_write(fd, frag, n + 20) < 0) return -1; off += n;
	}
	return 0;
}

/* ---- DOS-style path -> Linux path ---- */
static int dos2linux(const struct arg *a, char *out, int max) {
	char tmp[256]; int n = 0;
	for (u32 i = 0; i + 1 < a->len && n < 250; i += 2) { u16 c = a->p[i] | (a->p[i + 1] << 8); if (!c) break; tmp[n++] = c < 128 ? (char)c : '?'; }
	tmp[n] = 0;
	const char *root = 0; int skip = 0;
	if (n >= 2 && tmp[1] == ':') {
		skip = 2;
		switch (tmp[0] | 0x20) {
		case 'x': root = "/mnt/vendor/protect_f/md"; break;
		case 'y': root = "/mnt/vendor/protect_s/md"; break;
		case 'z': root = "/mnt/vendor/nvdata/md"; break;
		case 'r': case 'q': root = "/vendor/etc/md"; break;
		case 's': root = "/data/vendor_de/md/ota_s"; break;   /* the vendor daemon never finds files here (-9): keep it empty */
		case 't': root = "/vendor/etc/mdota"; break;
		default: return -1;
		}
	} else return -1;
	int o = 0; for (const char *r = root; *r && o < max - 2; r++) out[o++] = *r;
	int last = '/'; out[o++] = '/';
	for (int i = skip; i < n && o < max - 1; i++) { char c = tmp[i] == '\\' ? '/' : tmp[i]; if (c == '/' && last == '/') continue; out[o++] = c; last = c; }
	while (o > 1 && out[o - 1] == '/') o--;
	out[o] = 0; return 0;
}

#define NH 128
static int hfd[NH];
static int halloc(int fd) { for (int i = 1; i < NH; i++) if (!hfd[i]) { hfd[i] = fd; return i; } return -1; }
static int hget(u32 h) { return h > 0 && h < NH ? hfd[h] : -1; }
struct kstat { u64 dev, ino; u32 mode, nlink, uid, gid; u64 rdev, pad1; i64 size; i32 blksize, pad2; i64 blocks; i64 at, atn, mt, mtn, ct, ctn; u32 un[2]; };
static int dirh[NH]; static char dirpath[NH][256];
static u8 dbuf[4096]; static int dpos[NH], dlen[NH];


#define MAXENT 80
#define NAMELEN 96
static char dnames[NH][MAXENT][NAMELEN]; static char dbase[NH][220]; static int dcount[NH], didx[NH]; static u8 dentbuf[8192];
static void find_reply(int h, int last_ok) {
	u32 info[13]; mset(info, 0, sizeof(info)); u16 w[NAMELEN]; int wn = 0;
	if (last_ok) {
		char full[320]; int n = 0; for (const char *q = dbase[h]; *q && n < 220; q++) full[n++] = *q; full[n++] = '/';
		const char *nm = dnames[h][didx[h]]; for (int i = 0; nm[i] && n < 318; i++) full[n++] = nm[i]; full[n] = 0;
		struct kstat st; u32 sz = 0; if (sys_fstatat(full, &st, 0) == 0) sz = (u32)st.size;
		info[3] = 0xbe790001u; info[4] = 0x5d7b; info[5] = 0xbe790000u; info[6] = 0x5d7b; info[7] = sz;
		for (int i = 0; nm[i] && wn < NAMELEN - 1; i++) w[wn++] = (u8)nm[i];
	} else { u32 sz = 0; (void)sz; info[3] = 0xbe790001u; info[4] = 0x5d7b; info[5] = 0xbe790000u; info[6] = 0x5d7b; }
	r_arg(info, sizeof(info));
	if (wn) { w[wn++] = 0; r_arg(w, (u32)wn * 2); } else r_arg("", 0);
}
static void hx(char *o, u32 v, int nib) { static const char h[] = "0123456789abcdef"; for (int i = 0; i < nib; i++) o[i] = h[(v >> (4 * (nib - 1 - i))) & 15]; }
static void fs_one(int fd, u32 total) {
	struct arg a[8]; u32 op; int na = parse(req, total, a, 8, &op); if (na < 0) return;
	char path[256]; struct kstat st; r_begin(req, op);
	switch (op) {
	case 0x1001: { /* OPEN path flags */
		u32 fl = na > 1 ? rd32(a[1].p) : 0;
		if (na < 1 || dos2linux(&a[0], path, sizeof(path))) { r_u32((u32)-18); break; }
		/* observed with the vendor daemon: 0x300/0x500/0x700 -> O_RDONLY, 0x900 -> directory, 0x20010400/0x21010400 -> O_RDWR|O_CREAT */
		int lf = (fl >= 0x10000u) ? (O_RDWR | O_CREAT) : O_RDONLY; if ((fl & 0xf00) == 0x900 && fl < 0x10000u) lf = O_RDONLY | O_DIRECTORY;

		i64 f = sys_openat(path, lf, 0666);
		if (f < 0) { r_u32((u32)-18); break; }
		int h = halloc((int)f); r_u32(h < 0 ? (u32)-1 : (u32)h); break; }
	case 0x1002: { /* SEEK h off whence */
		int f = na > 2 ? hget(rd32(a[0].p)) : -1; i64 r = f < 0 ? -1 : sys_lseek(f, (i32)rd32(a[1].p), (int)rd32(a[2].p)); r_u32((u32)r); break; }
	case 0x1003: { /* READ h n */
		int f = na > 1 ? hget(rd32(a[0].p)) : -1; u32 n = na > 1 ? rd32(a[1].p) : 0; static u8 data[BIG];
		if (n > sizeof(data)) n = sizeof(data);
		i64 r = f < 0 ? -1 : sys_read(f, data, n); if (r < 0) { r_u32((u32)-1); break; }
		r_u32(0); r_u32((u32)r); r_arg(data, (u32)r); break; }
	case 0x1004: { /* WRITE h data n */
		int f = na > 1 ? hget(rd32(a[0].p)) : -1; u32 n = na > 2 ? rd32(a[2].p) : (na > 1 ? a[1].len : 0); if (na > 1 && n > a[1].len) n = a[1].len;   /* large writes carry no explicit size */
		i64 r = f < 0 ? -1 : sys_write(f, a[1].p, n); if (r < 0) { sayn("write failed, errno/-ret ", -r); sayn("   handle ", na > 0 ? rd32(a[0].p) : 0); sayn("   asked ", n); sayn("   have ", a[1].len); sayn("   argc ", na); sayn("   total ", total); for (int q = 0; q < na; q++) sayn("   arglen ", a[q].len); if (na > 2) sayn("   arg2 ", rd32(a[2].p)); r_u32((u32)-1); break; }
		r_u32(0); r_u32((u32)r); break; }
	case 0x1005: { /* CLOSE h */
		u32 h = na > 0 ? rd32(a[0].p) : 0; int f = hget(h); if (f >= 0) { sys_close(f); hfd[h] = 0; } r_u32(0); break; }
	case 0x1009: { /* FILESIZE h */
		int f = na > 0 ? hget(rd32(a[0].p)) : -1; if (f < 0 || sys_fstat(f, &st) < 0) { r_u32((u32)-1); break; } r_u32(0); r_u32((u32)st.size); break; }
	case 0x1007: { /* MKDIR/ACCESS-like: reported "failed" by the vendor daemon in every observed call */
		r_u32((u32)-18); break; }
	case 0x100e: { /* disk info (single call per boot): block captured from the vendor daemon */
		static const u8 blk[84] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xfc, 0x39, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
		r_u32(0); r_arg(blk, sizeof(blk)); break; }
	case 0x1010: { /* GETATTR path -> attribute or -9 */
		if (na < 1 || dos2linux(&a[0], path, sizeof(path)) || sys_fstatat(path, &st, 0) < 0) { r_u32((u32)-9); break; }
		r_u32((st.mode & 0170000) == 0040000 ? 0x10u : 0x20u); break; }
	case 0x1025: { /* STAT times */
		u8 t[24]; mset(t, 0, sizeof(t));
		if (na < 1 || dos2linux(&a[0], path, sizeof(path)) || sys_fstatat(path, &st, 0) < 0) { r_u32((u32)-9); r_arg(t, 24); break; }
		u32 v = (u32)st.mt; mcpy(t, &v, 4); v = (u32)st.at; mcpy(t + 8, &v, 4); v = (u32)st.ct; mcpy(t + 16, &v, 4);   /* vendor: [0] = image mtime (2026-09-27), then atime/ctime */ r_u32(0); r_arg(t, 24); break; }
	case 0x1022: { /* secure file access: path + 40-byte descriptor; descriptor word 0 is the command */
		if (na < 2 || a[1].len < 40 || dos2linux(&a[0], path, sizeof(path))) { r_u32((u32)-9); break; }
		u32 cmd = rd32(a[1].p), want = rd32(a[1].p + 32), skip = rd32(a[1].p + 20), bufaddr = rd32(a[1].p + 16);
		static u8 data[BIG]; if (want > sizeof(data)) want = sizeof(data);
		i64 f = sys_openat(path, O_RDONLY, 0); if (f < 0) { r_u32((u32)-9); break; }
		sys_fstat((int)f, &st);
		{ u32 echo[2]; echo[0] = cmd; echo[1] = 0; r_arg(echo, 8); }
		if (cmd == 0x13) {                         /* size query */
			sys_close((int)f); r_u32((u32)st.size); r_u32(0); r_arg("", 0); break; }
		if (cmd == 0x19) skip = 0;                 /* whole-file read: word 8 = size, word 4 = the modem's buffer address (echoed back) */
		if (skip) sys_lseek((int)f, skip, 0);
		i64 r = sys_read((int)f, data, want); sys_close((int)f); if (r < 0) r = 0;
		r_u32(cmd == 0x19 ? bufaddr : (u32)st.size); r_u32((u32)r); r_arg(data, (u32)r); break; }
	case 0x100f: { /* DELETE path */
		if (na < 1 || dos2linux(&a[0], path, sizeof(path))) { r_u32((u32)-9); break; }
		i64 r = sys_unlink(path); r_u32(r < 0 ? (u32)-9 : 0); break; }
	case 0x1012: { /* FINDFIRST dir ... -> [handle, info, name] */
		if (na < 1 || dos2linux(&a[0], path, sizeof(path))) { r_u32((u32)-6); break; }
		{ int pl = slen(path), q = pl; while (q > 0 && path[q - 1] != '/') q--; for (int k = q; k < pl; k++) if (path[k] == '*' || path[k] == '?') { path[q > 1 ? q - 1 : 1] = 0; break; } }   /* "dir/*.*" -> list dir */
		i64 d = sys_openat(path, O_RDONLY | O_DIRECTORY, 0); if (d < 0) { r_u32((u32)-6); break; }
		int h = halloc((int)d); if (h < 0) { sys_close((int)d); r_u32((u32)-6); break; }
		int n = 0; for (; path[n] && n < 218; n++) dbase[h][n] = path[n]; dbase[h][n] = 0; dcount[h] = 0; didx[h] = 0;
		i64 got; while ((got = sys_getdents((int)d, dentbuf, sizeof(dentbuf))) > 0) {
			for (i64 o = 0; o < got;) { u16 rl; mcpy(&rl, dentbuf + o + 16, 2); const char *nm = (const char *)(dentbuf + o + 19);
				if (!(nm[0] == '.' && (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0))) && dcount[h] < MAXENT) { int k = 0; while (nm[k] && k < NAMELEN - 1) { dnames[h][dcount[h]][k] = nm[k]; k++; } dnames[h][dcount[h]][k] = 0; dcount[h]++; }
				o += rl; }
		}
		if (dcount[h] == 0) { sys_close((int)d); hfd[h] = 0; r_u32((u32)-6); break; }
		r_u32((u32)h); find_reply(h, 1); break; }
	case 0x1013: { /* FINDNEXT h maxlen -> [status, info, name] */
		u32 h = na > 0 ? rd32(a[0].p) : 0; if (hget(h) < 0 || h >= NH) { r_u32((u32)-6); r_arg("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 52); r_arg("", 0); break; }
		didx[h]++; if (didx[h] >= dcount[h]) { didx[h] = dcount[h] - 1; r_u32((u32)-6); find_reply(h, 0); break; }
		r_u32(0); find_reply(h, 1); break; }
	case 0x1014: { u32 h = na > 0 ? rd32(a[0].p) : 0; int f = hget(h); if (f >= 0) { sys_close(f); hfd[h] = 0; } r_u32(0); break; }
	case 0x101a: { u32 k = na > 0 ? rd32(a[0].p) : 0; r_u32(k == 4 ? 0x5au : (u32)-2); break; }
	case 0x101c: r_u32(0); break;
	default: { sayhex("n2mdinit: unknown fs op ", op); char hb[3 * 64 + 2]; int hn = 0; static const char H[] = "0123456789abcdef";
		for (int k = 0; k < na; k++) { sayhex("   arg len ", a[k].len); hn = 0; for (u32 q = 0; q < a[k].len && q < 60; q++) { hb[hn++] = H[a[k].p[q] >> 4]; hb[hn++] = H[a[k].p[q] & 15]; } hb[hn++] = '\n'; sys_write(2, hb, hn); }
		r_u32((u32)-1); break; }
	}
	{ /* trace line: op, first-argument path or u32, first reply u32 */
		char l[300]; int n = 0; l[n++] = 'f'; l[n++] = 's'; l[n++] = ' '; hx(l + n, op, 4); n += 4; l[n++] = ' ';
		if (na > 0 && dos2linux(&a[0], path, sizeof(path)) == 0) { for (int i = 0; path[i] && n < 250; i++) l[n++] = path[i]; }
		else if (na > 0 && a[0].len == 4) { hx(l + n, rd32(a[0].p), 8); n += 8; }
		if (op == 0x1001 && na > 1) { l[n++] = ' '; hx(l + n, rd32(a[1].p), 8); n += 8; }
		l[n++] = ' '; l[n++] = '-'; l[n++] = '>'; l[n++] = ' '; if (rlen >= 32) { hx(l + n, rd32(rep + 28), 8); n += 8; } l[n++] = '\n'; sys_write(2, l, n);
	}
	if (op == 0x1022) { char hb[2 * 64 + 2]; int hn = 0; static const char H[] = "0123456789abcdef"; for (u32 q = 0; q < rlen && q < 64; q++) { hb[hn++] = H[rep[q] >> 4]; hb[hn++] = H[rep[q] & 15]; } hb[hn++] = '\n'; say("rep1022 "); sys_write(2, hb, hn); }
	r_send(fd);
}
static void serve_fs(void) {
	int fd = (int)sys_openat("/dev/ccci_fs", O_RDWR, 0); if (fd < 0) { say("n2mdinit: cannot open /dev/ccci_fs\n"); sys_exit(2); }
	static u8 pkt[3476]; u32 acc = 0;
	for (;;) {
		i64 n = sys_read(fd, pkt, sizeof(pkt)); if (n < 16) { sys_sleep_ms(20); continue; }
		/* requests larger than one packet arrive as several: data0 bit 31 set on all but the last */
		u32 d0 = rd32(pkt);
		if (0) { char pl[64]; int k = 0; pl[k++] = 'p'; pl[k++] = 'k'; pl[k++] = 't'; pl[k++] = ' '; hx(pl + k, (u32)n, 4); k += 4; pl[k++] = ' '; hx(pl + k, d0, 8); k += 8; pl[k++] = ' '; hx(pl + k, rd32(pkt + 8), 8); k += 8; pl[k++] = ' '; hx(pl + k, rd32(pkt + 12), 8); k += 8; pl[k++] = ' '; hx(pl + k, n >= 20 ? rd32(pkt + 16) : 0, 8); k += 8; pl[k++] = '\n'; sys_write(2, pl, k); }
		if (acc == 0) mcpy(req, pkt, 16);
		if (acc + (u32)n > sizeof(req) - 16) { acc = 0; continue; }
		if (acc == 0) { mcpy(req + 16, pkt + 16, (u32)n - 16); acc = (u32)n - 16; }
		else { mcpy(req + 16 + acc, pkt + 20, (u32)n - 20); acc += (u32)n - 20; }   /* continuation packets repeat the op id first */
		if (d0 & 0x80000000u) continue;
		{ u32 z = 0; mcpy(req, &z, 4); u32 tl = acc + 16; mcpy(req + 4, &tl, 4); }
		fs_one(fd, acc + 16); acc = 0;
	}
}
static void serve_rpc(void) {
	int fd = (int)sys_openat("/dev/ccci_rpc", O_RDWR, 0); if (fd < 0) { say("n2mdinit: cannot open /dev/ccci_rpc\n"); sys_exit(2); }
	for (;;) {
		i64 n = sys_read(fd, req, 3476); if (n <= 0) { sys_sleep_ms(20); continue; }
		struct arg a[8]; u32 op; int na = parse(req, (u32)n, a, 8, &op); if (na < 0) continue;
		/* observed: op 0x400f with one string argument = system property read; answer "not set" */
		r_begin(req, op);
		if (op == 0x400f) { r_u32(0); r_arg("\0\0\0\0", 1); } else { sayhex("n2mdinit: unknown rpc op ", op); r_u32((u32)-1); }
		r_send(fd);
	}
}
static const u8 boot_data[64] = { 0,0,0,0,0,0,0,0, 0xff,0xff,0xff,0xff, 0,0,0,0, 0,0,0,0, 'N','/','C','/','L','f','/','L','t','/','W','/','G' };

int main(void) {
	sys_prctl(15 /* PR_SET_NAME */, "ccci_mdinit");     /* the kernel only accepts DO_START_MD from a process with this name */
	i64 p1 = sys_fork(); if (p1 == 0) { serve_fs(); sys_exit(0); }
	i64 p2 = sys_fork(); if (p2 == 0) { serve_rpc(); sys_exit(0); }
	int m = (int)sys_openat("/dev/ccci_monitor", O_RDWR, 0); if (m < 0) { say("n2mdinit: cannot open /dev/ccci_monitor\n"); return 1; }
	{ int ipc = (int)sys_openat("/dev/ccci_ipc_5", O_RDWR, 0); i64 ri = ipc >= 0 ? sys_ioctl(ipc, 0x5006, 0) : -1; sayn("n2mdinit: ipc_5 reset_recv ", ri); }   /* the vendor daemon registers IPC task 5 (kept open) */
	{ u32 zero = 0; i64 rs = sys_ioctl(m, IOC_DO_STOP_MD, &zero); sayn("n2mdinit: DO_STOP_MD ", rs); sys_sleep_ms(4000); }   /* a previous run may have left the modem in exception state */
	/* CCB (shared memory buffers) handshake: the vendor daemon opens /dev/ccci_ccb_ctrl three times, reads the 20 buffer configurations and
	 * asks the control info of user 0, 1 and 2; the last call tells the kernel the AP side is ready (smem state OK) and the modem waits for it */
	for (int pass = 0; pass < 3; pass++) {
		int cb = (int)sys_openat("/dev/ccci_ccb_ctrl", O_RDWR, 0); if (cb < 0) { say("n2mdinit: cannot open ccci_ccb_ctrl\n"); break; }
		u32 cnt = 0; sys_ioctl(cb, 0x8004433fU, &cnt);
		for (u32 i = 0; i < cnt && i < 32; i++) { u32 cfg[6]; mset(cfg, 0, sizeof(cfg)); cfg[0] = i; sys_ioctl(cb, 0xc0184340U, cfg); }
		u32 ci[4]; mset(ci, 0, sizeof(ci)); ci[0] = (u32)pass; i64 rc = sys_ioctl(cb, 0xc0104347U, ci); sayn("n2mdinit: ccb ctrl info ", rc);
		/* the descriptor stays open like in the vendor daemon */
	}
	u32 z = 0; sys_ioctl(m, IOC_SET_MD_IMG_EXIST, &z);
	u8 bd[64]; mcpy(bd, boot_data, 64); sys_ioctl(m, IOC_SET_BOOT_DATA, bd);
	u32 t = 0; sys_ioctl(m, IOC_GET_MD_TYPE, &t); sayn("n2mdinit: md type ", t);
	sys_sleep_ms(1500);
	i64 r = sys_ioctl(m, IOC_DO_START_MD, 0); sayn("n2mdinit: DO_START_MD ", r);
	for (int i = 0; i < 90; i++) { u32 s = 0; sys_ioctl(m, IOC_GET_MD_STATE, &s); if (s == 2) { say("n2mdinit: modem READY\n"); break; } sys_sleep_ms(1000); }
	for (;;) sys_sleep_ms(60000);
}
void _start(void) { sys_exit(main()); }
