/* SPDX-License-Identifier: MIT */
/*
 * n2shim_stdio.c -- the guest's stdio, clock and process-exit shims.  Package V33 of gpu/hwcodec (owner
 * hw-v33).  Host-only so far: this file has never run on the phone.
 *
 * The guest is 32-bit Android code.  Its FILE* values in this shim are small integers (the vendor set never
 * touches the internals of a FILE):
 *     1 = stdout, 2 = stderr, 3..63 = a fopen()ed stream.  The data import "stderr" that V34 provides is a
 * 4-byte cell holding 2, so `fprintf(stderr, ...)` arrives here with f == 2.  If some caller passes the
 * ADDRESS of that cell instead, the runner can call n2shim_stdio_set_stderr_symbol(addr) and the address is
 * recognised too.  Nothing outside this file may assume anything else about a FILE*.
 *
 * Output: stdout/stderr (printf, puts, fprintf(stderr,..), write(1)/write(2)) never go to the host's
 * terminal; they are handed to the global callback
 *     void (*n2shim_log)(int prio, const char *tag, const char *text);
 * with the Android priority numbers (4 = INFO for stdout, 6 = ERROR for stderr).  The default is NULL =
 * discard.  V34's __android_log_print uses the same global.  Library code never prints by itself.
 *
 * Formatting is V30's n2fmt()/n2fmt_vscan() (shim/n2fmt.h): guest varargs are read under AAPCS rules
 * (32-bit slots; 64-bit values on an even slot).  fscanf() therefore scans through the same FILE table and
 * needs no stdio internals in V30.
 *
 * Files: fopen() goes through n2vfs.c (VF33-internal functions declared below): registered device by exact
 * path, then the read-only virtual /proc and /sys table, then -- only for a guest path under /data or
 * /sdcard and only when $N2HW_DUMP_DIR is set -- a real file below that directory.  Every other guest path
 * is ENOENT: guest code never opens a host file (no /proc/self/maps of the daemon, no /etc/...; R1 F18).
 * open()/read()/write()/ioctl()/close() are in n2vfs.c.
 *
 * Process exit: exit(), abort() and __stack_chk_fail() (and the fortify check of __fread_chk) never exit the
 * host process.  They record the first reason (n2shim_stdio_exit_reason()), set stop_request on the calling
 * cpu, ask V32's thread shim (weak) to stop every other guest cpu, and return; the interpreter stops the
 * thread at its next instruction boundary with N2A32_STOP_REQUEST.
 *
 * clock_gettime/gettimeofday write the 32-bit-time_t layout of the Android 32-bit ABI (two 32-bit words),
 * truncated modulo 2^32 for tv_sec.  CLOCK_* ids are the Linux ones the guest uses.
 */
#define _POSIX_C_SOURCE 200809L

#include "n2elf.h"
#include "n2fmt.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

/* ---- V33-internal interface of n2vfs.c (not part of include/n2elf.h) ------------------------------------------
 * Guest fds live there; these calls are how this file uses them.  Every one returns a byte/offset count or a
 * negative guest errno, and never touches the guest's errno itself (the caller decides). */
int n2vfs_open_path(struct n2a32_cpu *c, const char *path, int flags, int mode, int allow_host);
int n2vfs_gfd_read(struct n2a32_cpu *c, int fd, void *dst, size_t len);
int n2vfs_gfd_write(struct n2a32_cpu *c, int fd, const void *src, size_t len);
int n2vfs_gfd_seek(struct n2a32_cpu *c, int fd, int64_t off, int whence);
int n2vfs_gfd_fsync(struct n2a32_cpu *c, int fd);
int n2vfs_gfd_close(int fd);
void n2vfs_sh_open(struct n2a32_cpu *c);
void n2vfs_sh___open_2(struct n2a32_cpu *c);
void n2vfs_sh_close(struct n2a32_cpu *c);
void n2vfs_sh_read(struct n2a32_cpu *c);
void n2vfs_sh_write(struct n2a32_cpu *c);
void n2vfs_sh_ioctl(struct n2a32_cpu *c);
void n2vfs_sh___errno(struct n2a32_cpu *c);

/* Guest errno values used here (Linux numbering; the guest is Android, the host may be Darwin). */
enum {
	N2_EIO = 5, N2_EBADF = 9, N2_ENOMEM = 12, N2_EFAULT = 14, N2_EINVAL = 22, N2_EMFILE = 24,
	N2_ENOTTY = 25, N2_ENAMETOOLONG = 36
};

/* Linux/ARM open flags, as the guest passes them to fopen()'s internal open. */
enum {
	N2_O_RDONLY = 0, N2_O_WRONLY = 1, N2_O_RDWR = 2, N2_O_CREAT = 0100, N2_O_TRUNC = 01000,
	N2_O_APPEND = 02000
};

#define N2_FILE_FIRST 3
#define N2_FILE_MAX 64
#define N2_FMT_CAP (1u << 20)     /* the longest formatted line this file will build (bytes) */
#define N2_PATH_MAX 1024

/* ---- the log sink --------------------------------------------------------------------------------------------- */
void (*n2shim_log)(int prio, const char *tag, const char *text) = NULL;

/* ---- process exit state --------------------------------------------------------------------------------------- */
enum n2_stdio_reason {
	N2STDIO_OK = 0,
	N2STDIO_EXIT = 1,
	N2STDIO_ABORT = 2,
	N2STDIO_STACK_CHK = 3,
	N2STDIO_FREAD_CHK = 4
};

static atomic_int g_exit_reason;
static atomic_int g_exit_status;
static atomic_uint g_exit_pc;
static atomic_uint g_stderr_symbol;

/* V32's n2shim_thread.h: ask every live guest cpu to stop.  Weak here, so this package links without V32. */
extern unsigned n2shim_thread_request_stop(void) __attribute__((weak));

int n2shim_stdio_exit_reason(void)
{
	return atomic_load(&g_exit_reason);
}

int n2shim_stdio_exit_status(void)
{
	return atomic_load(&g_exit_status);
}

uint32_t n2shim_stdio_exit_pc(void)
{
	return atomic_load(&g_exit_pc);
}

void n2shim_stdio_exit_clear(void)
{
	atomic_store(&g_exit_reason, N2STDIO_OK);
	atomic_store(&g_exit_status, 0);
	atomic_store(&g_exit_pc, 0);
}

void n2shim_stdio_set_stderr_symbol(uint32_t guest_addr)
{
	atomic_store(&g_stderr_symbol, guest_addr);
}

static void n2_stdio_fatal(struct n2a32_cpu *c, int reason, int status)
{
	int expected = N2STDIO_OK;

	if (atomic_compare_exchange_strong(&g_exit_reason, &expected, reason)) {
		atomic_store(&g_exit_status, status);
		atomic_store(&g_exit_pc, c ? c->r[15] : 0);
	}
	if (c)
		c->stop_request = 1;
	if (n2shim_thread_request_stop)
		(void)n2shim_thread_request_stop();
}

/* ---- small helpers -------------------------------------------------------------------------------------------- */
static int n2_guest_buf_ok(struct n2a32_cpu *c, uint32_t a, uint32_t len)
{
	if (!c || !c->mem)
		return 0;
	if (!a)
		return len == 0;
	if ((uint64_t)a + len > 0x100000000ull)
		return 0;
	if (len && (n2a32_is_mmio(c->mem, a) || n2a32_is_mmio(c->mem, (uint32_t)((uint64_t)a + len - 1))))
		return 0;
	return 1;
}

static int n2_guest_str(struct n2a32_cpu *c, uint32_t ga, char *out, size_t cap)
{
	size_t i;

	if (!ga || !cap)
		return -N2_EFAULT;
	for (i = 0; i + 1 < cap; i++) {
		uint8_t ch;

		if ((uint64_t)ga + i > 0xffffffffull)
			return -N2_EFAULT;
		ch = n2a32_ld8(c, ga + (uint32_t)i);
		out[i] = (char)ch;
		if (!ch)
			return 0;
	}
	out[cap - 1] = 0;
	return -N2_ENAMETOOLONG;
}

static void n2_set_errno(struct n2a32_cpu *c, int e)
{
	uint32_t loc = n2vfs_errno_location(c);

	if (loc)
		n2a32_st32(c, loc, (uint32_t)e);
}

/* ---- the FILE table ------------------------------------------------------------------------------------------- */
struct n2_file {
	int used;
	int gfd;
	int eof;
	int err;
};

static pthread_mutex_t g_file_lock = PTHREAD_MUTEX_INITIALIZER;
static struct n2_file g_files[N2_FILE_MAX];

enum n2_file_kind { N2_FK_INVALID = 0, N2_FK_STDOUT, N2_FK_STDERR, N2_FK_FILE };

/* Resolve a guest FILE* value.  For a real stream *out points at its entry (caller must not hold the lock;
 * the entry stays allocated until fclose, and one stream is never used by two threads at once - POSIX). */
static enum n2_file_kind n2_file_lookup(struct n2a32_cpu *c, uint32_t f, struct n2_file **out)
{
	struct n2_file *fe = NULL;
	uint32_t sym = atomic_load(&g_stderr_symbol);

	*out = NULL;
	(void)c;
	if (f == 1)
		return N2_FK_STDOUT;
	if (f == 2)
		return N2_FK_STDERR;
	if (sym && f == sym)
		return N2_FK_STDERR;
	if (f < N2_FILE_FIRST || f >= N2_FILE_MAX)
		return N2_FK_INVALID;
	pthread_mutex_lock(&g_file_lock);
	if (g_files[f].used)
		fe = &g_files[f];
	pthread_mutex_unlock(&g_file_lock);
	if (!fe)
		return N2_FK_INVALID;
	*out = fe;
	return N2_FK_FILE;
}

static int n2_file_alloc(int gfd)
{
	int i, v = -1;

	pthread_mutex_lock(&g_file_lock);
	for (i = N2_FILE_FIRST; i < N2_FILE_MAX; i++) {
		if (!g_files[i].used) {
			g_files[i].used = 1;
			g_files[i].gfd = gfd;
			g_files[i].eof = 0;
			g_files[i].err = 0;
			v = i;
			break;
		}
	}
	pthread_mutex_unlock(&g_file_lock);
	return v;
}

static void n2_file_free(struct n2_file *fe)
{
	pthread_mutex_lock(&g_file_lock);
	fe->used = 0;
	fe->gfd = -1;
	pthread_mutex_unlock(&g_file_lock);
}

/* ---- the formatter bridge ------------------------------------------------------------------------------------- */
/* Format a guest format string (args from slot `first`) into a host buffer.  Returns the length actually
 * stored and sets *out to the buffer (stackbuf or a malloc'd block that the caller frees when *heap is 1). */
static int n2_format(struct n2a32_cpu *c, uint32_t gf, unsigned first, char *stackbuf, size_t stackcap,
		     char **out, int *heap)
{
	struct n2fmt_args a;
	int need;

	*heap = 0;
	*out = stackbuf;
	if (!gf) {
		n2_set_errno(c, N2_EINVAL);
		return -1;
	}
	n2fmt_args_regs(&a, c, first);
	need = n2fmt(c, NULL, 0, gf, &a);
	if (need < 0) {
		n2_set_errno(c, N2_EINVAL);
		return -1;
	}
	if ((size_t)need + 1u <= stackcap) {
		n2fmt_args_regs(&a, c, first);
		return n2fmt(c, stackbuf, stackcap, gf, &a);
	}
	{
		size_t cap = (size_t)need + 1u;
		char *p;

		if (cap > N2_FMT_CAP)
			cap = N2_FMT_CAP;
		p = malloc(cap);
		if (!p) {
			n2_set_errno(c, N2_ENOMEM);
			return -1;
		}
		n2fmt_args_regs(&a, c, first);
		need = n2fmt(c, p, cap, gf, &a);
		if (need < 0) {
			free(p);
			n2_set_errno(c, N2_EINVAL);
			return -1;
		}
		if ((size_t)need >= cap)   /* n2fmt reports the untruncated length */
			need = (int)(cap - 1u);
		*out = p;
		*heap = 1;
		return need;
	}
}

/* Write `len` bytes to a resolved stream.  Returns len or -1 (guest errno set). */
static int n2_stream_write(struct n2a32_cpu *c, enum n2_file_kind kind, struct n2_file *fe, const char *buf,
			   int len)
{
	if (kind == N2_FK_STDOUT || kind == N2_FK_STDERR) {
		if (n2shim_log) {
			/* the callback takes a C string: give it a terminated copy */
			char stackbuf[512];
			char *p = stackbuf;
			int heap = 0;

			if ((size_t)len + 1u > sizeof stackbuf) {
				p = malloc((size_t)len + 1u);
				if (!p) {
					n2_set_errno(c, N2_ENOMEM);
					return -1;
				}
				heap = 1;
			}
			memcpy(p, buf, (size_t)len);
			p[len] = 0;
			n2shim_log(kind == N2_FK_STDOUT ? 4 : 6, kind == N2_FK_STDOUT ? "stdout" : "stderr", p);
			if (heap)
				free(p);
		}
		return len;
	}
	{
		int r = n2vfs_gfd_write(c, fe->gfd, buf, (size_t)len);

		if (r < 0) {
			n2_set_errno(c, -r);
			fe->err = 1;
			return -1;
		}
		if (r != len) {
			n2_set_errno(c, N2_EIO);
			fe->err = 1;
			return r;
		}
		return len;
	}
}

/* Read up to len bytes into a guest buffer.  Returns the count or -1. */
static int n2_stream_read(struct n2a32_cpu *c, enum n2_file_kind kind, struct n2_file *fe, uint32_t gbuf,
			  uint32_t len)
{
	int r;

	if (kind != N2_FK_FILE) {
		n2_set_errno(c, N2_EBADF);
		return -1;
	}
	if (!n2_guest_buf_ok(c, gbuf, len)) {
		n2_set_errno(c, N2_EFAULT);
		return -1;
	}
	r = n2vfs_gfd_read(c, fe->gfd, n2a32_ptr(c->mem, gbuf), len);
	if (r < 0) {
		n2_set_errno(c, -r);
		fe->err = 1;
		return -1;
	}
	return r;
}

/* ---- fopen / fclose ------------------------------------------------------------------------------------------- */
static int n2_mode_flags(const char *mode)
{
	int plus = strchr(mode, '+') != NULL;

	switch (mode[0]) {
	case 'r':
		return plus ? N2_O_RDWR : N2_O_RDONLY;
	case 'w':
		return (plus ? N2_O_RDWR : N2_O_WRONLY) | N2_O_CREAT | N2_O_TRUNC;
	case 'a':
		return (plus ? N2_O_RDWR : N2_O_WRONLY) | N2_O_CREAT | N2_O_APPEND;
	default:
		return -1;
	}
}

static void sh_fopen(struct n2a32_cpu *c)
{
	char path[N2_PATH_MAX], mode[8];
	int flags, gfd, f;
	int e = n2_guest_str(c, n2shim_arg(c, 0), path, sizeof path);

	if (!e)
		e = n2_guest_str(c, n2shim_arg(c, 1), mode, sizeof mode);
	if (e < 0) {
		n2_set_errno(c, -e);
		n2shim_ret(c, 0);
		return;
	}
	flags = n2_mode_flags(mode);
	if (flags < 0) {
		n2_set_errno(c, N2_EINVAL);
		n2shim_ret(c, 0);
		return;
	}
	gfd = n2vfs_open_path(c, path, flags, 0666, 1);
	if (gfd < 0) {
		n2_set_errno(c, -gfd);
		n2shim_ret(c, 0);
		return;
	}
	f = n2_file_alloc(gfd);
	if (f < 0) {
		(void)n2vfs_gfd_close(gfd);
		n2_set_errno(c, N2_EMFILE);
		n2shim_ret(c, 0);
		return;
	}
	n2shim_ret(c, (uint32_t)f);
}

static void sh_fclose(struct n2a32_cpu *c)
{
	struct n2_file *fe = NULL;
	enum n2_file_kind k = n2_file_lookup(c, n2shim_arg(c, 0), &fe);

	if (k == N2_FK_STDOUT || k == N2_FK_STDERR) {
		n2shim_ret(c, 0);
		return;
	}
	if (k != N2_FK_FILE) {
		n2_set_errno(c, N2_EBADF);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	{
		int r = n2vfs_gfd_close(fe->gfd);

		n2_file_free(fe);
		if (r < 0) {
			n2_set_errno(c, -r);
			n2shim_ret(c, 0xffffffffu);
			return;
		}
	}
	n2shim_ret(c, 0);
}

/* ---- fread / fwrite / fgets / feof / fflush / fseek / fileno / fsync ----------------------------------------- */
static uint32_t n2_fread_common(struct n2a32_cpu *c, uint32_t gbuf, uint32_t size, uint32_t nmemb, uint32_t fp)
{
	struct n2_file *fe = NULL;
	enum n2_file_kind k = n2_file_lookup(c, fp, &fe);
	uint64_t total;
	int r, got;

	if (!size || !nmemb) {
		if (k == N2_FK_INVALID) {
			n2_set_errno(c, N2_EBADF);
			return 0xffffffffu;
		}
		return 0;
	}
	if (k == N2_FK_INVALID) {
		n2_set_errno(c, N2_EBADF);
		return 0xffffffffu;
	}
	total = (uint64_t)size * nmemb;
	if (total > 0xffffffffull) {
		n2_set_errno(c, N2_EINVAL);
		return 0xffffffffu;
	}
	r = n2_stream_read(c, k, fe, gbuf, (uint32_t)total);
	if (r < 0)
		return 0xffffffffu;
	if ((uint32_t)r < total && k == N2_FK_FILE)
		fe->eof = 1;
	got = r / (int)size;
	return (uint32_t)got;
}

static void sh_fread(struct n2a32_cpu *c)
{
	n2shim_ret(c, n2_fread_common(c, n2shim_arg(c, 0), n2shim_arg(c, 1), n2shim_arg(c, 2),
				      n2shim_arg(c, 3)));
}

static void sh___fread_chk(struct n2a32_cpu *c)
{
	/* bionic: size_t __fread_chk(void *dst, size_t size, size_t nmemb, FILE *fp, size_t dst_len).
	 * The first four arguments are the fread() ones and the object size is the fifth (on the guest stack).
	 * Evidence: libvpud_vcodec.so 0xf000-0xf00e sets r0=dst, r1=1, r2=count, r3=FILE*, [sp]=0x1400000,
	 * then compares the result with r2 - which only makes sense for (dst, size, count, fp, dst_len). */
	uint32_t size = n2shim_arg(c, 1), nmemb = n2shim_arg(c, 2), dstlen = n2shim_arg(c, 4);

	if (size && nmemb > dstlen / size) {
		n2_stdio_fatal(c, N2STDIO_FREAD_CHK, 0);
		n2shim_ret(c, 0);
		return;
	}
	n2shim_ret(c, n2_fread_common(c, n2shim_arg(c, 0), size, nmemb, n2shim_arg(c, 3)));
}

static void sh_fwrite(struct n2a32_cpu *c)
{
	uint32_t gbuf = n2shim_arg(c, 0), size = n2shim_arg(c, 1), nmemb = n2shim_arg(c, 2);
	uint32_t fp = n2shim_arg(c, 3);
	struct n2_file *fe = NULL;
	enum n2_file_kind k = n2_file_lookup(c, fp, &fe);
	uint64_t total;
	int r;

	if (!size || !nmemb) {
		if (k == N2_FK_INVALID) {
			n2_set_errno(c, N2_EBADF);
			n2shim_ret(c, 0xffffffffu);
			return;
		}
		n2shim_ret(c, 0);
		return;
	}
	if (k == N2_FK_INVALID) {
		n2_set_errno(c, N2_EBADF);
		n2shim_ret(c, 0);
		return;
	}
	total = (uint64_t)size * nmemb;
	if (total > 0xffffffffull) {
		n2_set_errno(c, N2_EINVAL);
		n2shim_ret(c, 0);
		return;
	}
	if (!n2_guest_buf_ok(c, gbuf, (uint32_t)total)) {
		n2_set_errno(c, N2_EFAULT);
		n2shim_ret(c, 0);
		return;
	}
	r = n2_stream_write(c, k, fe, n2a32_ptr(c->mem, gbuf), (int)total);
	if (r < 0) {
		n2shim_ret(c, 0);
		return;
	}
	n2shim_ret(c, (uint32_t)r / size);
}

static void sh_fgets(struct n2a32_cpu *c)
{
	uint32_t gbuf = n2shim_arg(c, 0), size = n2shim_arg(c, 1), fp = n2shim_arg(c, 2);
	struct n2_file *fe = NULL;
	enum n2_file_kind k = n2_file_lookup(c, fp, &fe);
	uint32_t i = 0;

	if (!n2_guest_buf_ok(c, gbuf, size) || size < 1) {
		n2_set_errno(c, N2_EFAULT);
		n2shim_ret(c, 0);
		return;
	}
	if (k != N2_FK_FILE) {
		n2_set_errno(c, N2_EBADF);
		n2shim_ret(c, 0);
		return;
	}
	while (i + 1 < size) {
		uint8_t ch;
		int r = n2vfs_gfd_read(c, fe->gfd, &ch, 1);

		if (r < 0) {
			n2_set_errno(c, -r);
			fe->err = 1;
			if (!i) {
				n2shim_ret(c, 0);
				return;
			}
			break;
		}
		if (r == 0) {
			fe->eof = 1;
			break;
		}
		n2a32_st8(c, gbuf + i, ch);
		i++;
		if (ch == '\n')
			break;
	}
	if (!i && fe->eof) {
		n2shim_ret(c, 0);
		return;
	}
	n2a32_st8(c, gbuf + i, 0);
	n2shim_ret(c, gbuf);
}

static void sh_feof(struct n2a32_cpu *c)
{
	struct n2_file *fe = NULL;
	enum n2_file_kind k = n2_file_lookup(c, n2shim_arg(c, 0), &fe);

	n2shim_ret(c, k == N2_FK_FILE && fe->eof);
}

static void sh_fflush(struct n2a32_cpu *c)
{
	struct n2_file *fe = NULL;
	enum n2_file_kind k;

	if (n2shim_arg(c, 0) == 0) {   /* fflush(NULL): every stream, and nothing is buffered */
		n2shim_ret(c, 0);
		return;
	}
	k = n2_file_lookup(c, n2shim_arg(c, 0), &fe);
	if (k == N2_FK_INVALID) {
		n2_set_errno(c, N2_EBADF);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	n2shim_ret(c, 0);   /* every write goes straight to the fd / the log; nothing is buffered */
}

static void sh_fseek(struct n2a32_cpu *c)
{
	uint32_t fp = n2shim_arg(c, 0);
	int32_t off = (int32_t)n2shim_arg(c, 1);
	uint32_t whence = n2shim_arg(c, 2);
	struct n2_file *fe = NULL;
	enum n2_file_kind k = n2_file_lookup(c, fp, &fe);
	int r;

	if (k != N2_FK_FILE) {
		n2_set_errno(c, k == N2_FK_INVALID ? N2_EBADF : N2_EINVAL);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	r = n2vfs_gfd_seek(c, fe->gfd, off, (int)whence);
	if (r < 0) {
		n2_set_errno(c, -r);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	fe->eof = 0;
	n2shim_ret(c, 0);
}

static void sh_fileno(struct n2a32_cpu *c)
{
	struct n2_file *fe = NULL;
	enum n2_file_kind k = n2_file_lookup(c, n2shim_arg(c, 0), &fe);

	switch (k) {
	case N2_FK_STDOUT: n2shim_ret(c, 1); break;
	case N2_FK_STDERR: n2shim_ret(c, 2); break;
	case N2_FK_FILE: n2shim_ret(c, (uint32_t)fe->gfd); break;
	default:
		n2_set_errno(c, N2_EBADF);
		n2shim_ret(c, 0xffffffffu);
		break;
	}
}

static void sh_fsync(struct n2a32_cpu *c)
{
	uint32_t fd = n2shim_arg(c, 0);
	int r;

	if (fd <= 2) {
		n2shim_ret(c, 0);
		return;
	}
	r = n2vfs_gfd_fsync(c, (int)fd);
	if (r < 0) {
		n2_set_errno(c, -r);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	n2shim_ret(c, 0);
}

/* ---- fprintf / printf / puts ---------------------------------------------------------------------------------- */
static void sh_fprintf(struct n2a32_cpu *c)
{
	struct n2_file *fe = NULL;
	enum n2_file_kind k = n2_file_lookup(c, n2shim_arg(c, 0), &fe);
	char stackbuf[512];
	char *buf = NULL;
	int heap = 0, n;

	if (k == N2_FK_INVALID) {
		n2_set_errno(c, N2_EBADF);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	n = n2_format(c, n2shim_arg(c, 1), 2, stackbuf, sizeof stackbuf, &buf, &heap);
	if (n < 0) {
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	n = n2_stream_write(c, k, fe, buf, n);
	if (heap)
		free(buf);
	n2shim_ret(c, n < 0 ? 0xffffffffu : (uint32_t)n);
}

static void n2_print_common(struct n2a32_cpu *c, uint32_t gf, unsigned first, int kind)
{
	char stackbuf[512];
	char *buf = NULL;
	int heap = 0, n;

	n = n2_format(c, gf, first, stackbuf, sizeof stackbuf, &buf, &heap);
	if (n < 0) {
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	n = n2_stream_write(c, kind == 1 ? N2_FK_STDOUT : N2_FK_STDERR, NULL, buf, n);
	if (heap)
		free(buf);
	n2shim_ret(c, n < 0 ? 0xffffffffu : (uint32_t)n);
}

static void sh_printf(struct n2a32_cpu *c)
{
	n2_print_common(c, n2shim_arg(c, 0), 1, 1);
}

static void sh_puts(struct n2a32_cpu *c)
{
	uint32_t gs = n2shim_arg(c, 0);
	size_t cap = 256, n = 0;
	char *buf;
	int r;

	if (!gs) {
		n2_set_errno(c, N2_EFAULT);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	buf = malloc(cap + 1);
	if (!buf) {
		n2_set_errno(c, N2_ENOMEM);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	for (;;) {
		uint8_t ch;

		if ((uint64_t)gs + n > 0xffffffffull) {
			free(buf);
			n2_set_errno(c, N2_EFAULT);
			n2shim_ret(c, 0xffffffffu);
			return;
		}
		if (n + 2 >= cap) {
			char *nb;
			size_t ncap = cap * 2;

			if (ncap > N2_FMT_CAP) {
				free(buf);
				n2_set_errno(c, N2_ENAMETOOLONG);
				n2shim_ret(c, 0xffffffffu);
				return;
			}
			nb = realloc(buf, ncap + 1);
			if (!nb) {
				free(buf);
				n2_set_errno(c, N2_ENOMEM);
				n2shim_ret(c, 0xffffffffu);
				return;
			}
			buf = nb;
			cap = ncap;
		}
		ch = n2a32_ld8(c, gs + (uint32_t)n);
		buf[n++] = (char)ch;
		if (!ch) {
			n--;
			break;
		}
	}
	buf[n++] = '\n';
	buf[n] = 0;
	r = n2_stream_write(c, N2_FK_STDOUT, NULL, buf, (int)n);
	free(buf);
	n2shim_ret(c, r < 0 ? 0xffffffffu : 0);
}

/* ---- fscanf --------------------------------------------------------------------------------------------------- */
struct n2_scan_ctx {
	struct n2a32_cpu *c;
	int gfd;
	int pushback;
	int has_push;
	int error;
};

static int n2_scan_get(void *ctx)
{
	struct n2_scan_ctx *s = ctx;
	unsigned char ch;
	int r;

	if (s->has_push) {
		s->has_push = 0;
		return s->pushback & 0xff;
	}
	if (s->error)
		return -1;
	r = n2vfs_gfd_read(s->c, s->gfd, &ch, 1);
	if (r < 0) {
		s->error = 1;
		return -1;
	}
	if (r == 0)
		return -1;
	return ch;
}

static void n2_scan_unget(void *ctx, int byte)
{
	struct n2_scan_ctx *s = ctx;

	s->pushback = byte;
	s->has_push = 1;
}

static void sh_fscanf(struct n2a32_cpu *c)
{
	struct n2_file *fe = NULL;
	enum n2_file_kind k = n2_file_lookup(c, n2shim_arg(c, 0), &fe);
	struct n2fmt_args a;
	struct n2fmt_input in;
	struct n2_scan_ctx s;
	int r;

	if (k != N2_FK_FILE) {
		n2_set_errno(c, N2_EBADF);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	if (!n2shim_arg(c, 1)) {
		n2_set_errno(c, N2_EINVAL);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	memset(&s, 0, sizeof s);
	s.c = c;
	s.gfd = fe->gfd;
	in.get = n2_scan_get;
	in.unget = n2_scan_unget;
	in.ctx = &s;
	n2fmt_args_regs(&a, c, 2);
	r = n2fmt_vscan(c, &in, n2shim_arg(c, 1), &a);
	if (r < 0) {
		if (s.error)
			fe->err = 1;
		else
			fe->eof = 1;
	}
	n2shim_ret(c, (uint32_t)r);
}

/* ---- clock_gettime / gettimeofday ----------------------------------------------------------------------------- */
static int n2_host_clock(int id, clockid_t *out)
{
	switch (id) {
	case 0: *out = CLOCK_REALTIME; return 0;
	case 1: *out = CLOCK_MONOTONIC; return 0;
	case 2: *out = CLOCK_PROCESS_CPUTIME_ID; return 0;
	case 3: *out = CLOCK_THREAD_CPUTIME_ID; return 0;
#ifdef CLOCK_MONOTONIC_RAW
	case 4: *out = CLOCK_MONOTONIC_RAW; return 0;
#endif
#ifdef CLOCK_REALTIME_COARSE
	case 5: *out = CLOCK_REALTIME_COARSE; return 0;
#endif
#ifdef CLOCK_MONOTONIC_COARSE
	case 6: *out = CLOCK_MONOTONIC_COARSE; return 0;
#endif
#ifdef CLOCK_BOOTTIME
	case 7: *out = CLOCK_BOOTTIME; return 0;
#endif
	default: return -1;
	}
}

static void sh_clock_gettime(struct n2a32_cpu *c)
{
	uint32_t tp = n2shim_arg(c, 1);
	struct timespec ts;
	clockid_t hid;

	if (!n2_guest_buf_ok(c, tp, 8)) {
		n2_set_errno(c, N2_EFAULT);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	if (n2_host_clock((int)n2shim_arg(c, 0), &hid) < 0) {
		n2_set_errno(c, N2_EINVAL);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	if (clock_gettime(hid, &ts) != 0) {
		n2_set_errno(c, N2_EINVAL);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	n2a32_st32(c, tp, (uint32_t)ts.tv_sec);
	n2a32_st32(c, tp + 4, (uint32_t)ts.tv_nsec);
	n2shim_ret(c, 0);
}

static void sh_gettimeofday(struct n2a32_cpu *c)
{
	uint32_t tp = n2shim_arg(c, 0);
	struct timeval tv;

	if (!tp || !n2_guest_buf_ok(c, tp, 8)) {
		n2_set_errno(c, N2_EFAULT);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	if (gettimeofday(&tv, NULL) != 0) {
		n2_set_errno(c, N2_EINVAL);
		n2shim_ret(c, 0xffffffffu);
		return;
	}
	n2a32_st32(c, tp, (uint32_t)tv.tv_sec);
	n2a32_st32(c, tp + 4, (uint32_t)tv.tv_usec);
	n2shim_ret(c, 0);
}

/* ---- exit / abort / __stack_chk_fail --------------------------------------------------------------------------- */
static void sh_exit(struct n2a32_cpu *c)
{
	n2_stdio_fatal(c, N2STDIO_EXIT, (int)n2shim_arg(c, 0));
	n2shim_ret(c, 0);
}

static void sh_abort(struct n2a32_cpu *c)
{
	n2_stdio_fatal(c, N2STDIO_ABORT, 0);
	n2shim_ret(c, 0);
}

static void sh___stack_chk_fail(struct n2a32_cpu *c)
{
	n2_stdio_fatal(c, N2STDIO_STACK_CHK, 0);
	n2shim_ret(c, 0);
}

/* ---- table: the 27 stdio imports of the vendor set (facts/imports.py --table stdio) --------------------------- */
const struct n2shim_entry n2shim_stdio[] = {
	{ "open", n2vfs_sh_open },
	{ "__open_2", n2vfs_sh___open_2 },
	{ "close", n2vfs_sh_close },
	{ "read", n2vfs_sh_read },
	{ "write", n2vfs_sh_write },
	{ "ioctl", n2vfs_sh_ioctl },
	{ "__errno", n2vfs_sh___errno },
	{ "fopen", sh_fopen },
	{ "fclose", sh_fclose },
	{ "fread", sh_fread },
	{ "__fread_chk", sh___fread_chk },
	{ "fwrite", sh_fwrite },
	{ "fgets", sh_fgets },
	{ "feof", sh_feof },
	{ "fflush", sh_fflush },
	{ "fseek", sh_fseek },
	{ "fileno", sh_fileno },
	{ "fsync", sh_fsync },
	{ "fprintf", sh_fprintf },
	{ "fscanf", sh_fscanf },
	{ "printf", sh_printf },
	{ "puts", sh_puts },
	{ "clock_gettime", sh_clock_gettime },
	{ "gettimeofday", sh_gettimeofday },
	{ "exit", sh_exit },
	{ "abort", sh_abort },
	{ "__stack_chk_fail", sh___stack_chk_fail },
	{ NULL, NULL }
};
