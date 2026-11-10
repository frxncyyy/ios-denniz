/*
 * probe_core.c -- test runner, result file, console lines and the small portable helpers of the probe.
 *
 * Runs each test in a forked child.  The child reports fields to the parent over a pipe with this framing:
 *   uint32 big-endian key length, uint32 big-endian value length, key bytes, value bytes
 * so a field value may contain any byte (quotes, backslashes, newlines: they are JSON-escaped on the way out).
 * The child's exit code is the result: 0 pass, 1 fail, 2 skipped; a signal or any other exit code is a crash;
 * the parent's own deadline makes it a timeout.
 *
 * Output: one JSON object per test, appended and fsync'ed, and one line on /dev/console per test
 * (the path can be overridden with N2VIDEO_CONSOLE, used by the host test).
 *
 * Depends on libSystem only.  SPDX-License-Identifier: MIT
 */
#define _POSIX_C_SOURCE 200809L
#include "probe_core.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PROBE_DEFAULT_TIMEOUT_S 20u
#define PROBE_MAX_KEY           128u
#define PROBE_MAX_VALUE         (64u * 1024u)
#define PROBE_MAX_FIELDS        128u
#define PROBE_MAX_INBUF         (2u * 1024u * 1024u)
#define PROBE_REPORT_MAX        (4u * 1024u * 1024u)

struct probe_ctx {
	int fd;                 /* write end of the field pipe in the child, -1 in the parent */
	char *mem;              /* the in-process fallback: same framing, this buffer instead of the pipe */
	size_t mem_len, mem_cap;
};

/* (lead) When set (probe_main.c sets it on the phone), each test runs in a child that EXECs this program again
 * ("--child <test> <fd>") instead of only forking. NULL: plain fork (the host test). */
const char *probe_exec_path;
extern int execv(const char *path, char *const argv[]);

/* ------------------------------------------------------------------ small growable string ------------------------------- */

struct buf {
	char *p;
	size_t n, cap;
	int oom;
};

static void buf_reserve(struct buf *b, size_t extra)
{
	size_t need = b->n + extra + 1;
	size_t cap;
	char *np;

	if (b->oom || need <= b->cap)
		return;
	cap = b->cap ? b->cap : 256;
	while (cap < need)
		cap *= 2;
	if (cap > PROBE_REPORT_MAX) {
		b->oom = 1;
		return;
	}
	np = realloc(b->p, cap);
	if (!np) {
		b->oom = 1;
		return;
	}
	b->p = np;
	b->cap = cap;
}

static void buf_bytes(struct buf *b, const char *s, size_t n)
{
	buf_reserve(b, n);
	if (b->oom)
		return;
	memcpy(b->p + b->n, s, n);
	b->n += n;
	b->p[b->n] = 0;
}

static void buf_puts(struct buf *b, const char *s)
{
	buf_bytes(b, s, strlen(s));
}

static void buf_putc(struct buf *b, char c)
{
	buf_bytes(b, &c, 1);
}

/* JSON string.  Valid UTF-8 passes through; control characters are escaped; bytes >= 0x80 are escaped as \u00XX
 * so that a stray binary value can never make the line invalid JSON. */
static void buf_json_str(struct buf *b, const char *s, size_t n)
{
	static const char hex[] = "0123456789abcdef";
	size_t i;

	buf_putc(b, '"');
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		switch (c) {
		case '"':  buf_puts(b, "\\\""); break;
		case '\\': buf_puts(b, "\\\\"); break;
		case '\n': buf_puts(b, "\\n"); break;
		case '\r': buf_puts(b, "\\r"); break;
		case '\t': buf_puts(b, "\\t"); break;
		case '\b': buf_puts(b, "\\b"); break;
		case '\f': buf_puts(b, "\\f"); break;
		default:
			if (c < 0x20 || c >= 0x7f) {
				char e[6] = { '\\', 'u', '0', '0', hex[c >> 4], hex[c & 15] };
				buf_bytes(b, e, 6);
			} else {
				buf_putc(b, (char)c);
			}
		}
	}
	buf_putc(b, '"');
}

/* ------------------------------------------------------------------ fields ----------------------------------------------- */

struct field {
	char *key;
	char *val;
};

struct fieldset {
	struct field f[PROBE_MAX_FIELDS];
	unsigned n;
};

static void fieldset_add(struct fieldset *fs, const char *key, size_t klen, const char *val, size_t vlen)
{
	unsigned i;

	if (klen == 0 || klen > PROBE_MAX_KEY || vlen > PROBE_MAX_VALUE)
		return;
	for (i = 0; i < fs->n; i++) {
		if (strlen(fs->f[i].key) == klen && memcmp(fs->f[i].key, key, klen) == 0) {
			char *nv = malloc(vlen + 1);
			if (!nv)
				return;
			memcpy(nv, val, vlen);
			nv[vlen] = 0;
			free(fs->f[i].val);
			fs->f[i].val = nv;
			return;
		}
	}
	if (fs->n >= PROBE_MAX_FIELDS)
		return;
	fs->f[fs->n].key = malloc(klen + 1);
	fs->f[fs->n].val = malloc(vlen + 1);
	if (!fs->f[fs->n].key || !fs->f[fs->n].val) {
		free(fs->f[fs->n].key);
		free(fs->f[fs->n].val);
		return;
	}
	memcpy(fs->f[fs->n].key, key, klen);
	fs->f[fs->n].key[klen] = 0;
	memcpy(fs->f[fs->n].val, val, vlen);
	fs->f[fs->n].val[vlen] = 0;
	fs->n++;
}

static void fieldset_free(struct fieldset *fs)
{
	unsigned i;
	for (i = 0; i < fs->n; i++) {
		free(fs->f[i].key);
		free(fs->f[i].val);
	}
	fs->n = 0;
}

/* Parse every complete frame at the front of buf; returns the number of bytes consumed. */
static size_t fields_parse(struct fieldset *fs, const char *buf, size_t n)
{
	size_t off = 0;

	while (n - off >= 8) {
		const unsigned char *p = (const unsigned char *)buf + off;
		size_t klen = ((size_t)p[0] << 24) | ((size_t)p[1] << 16) | ((size_t)p[2] << 8) | (size_t)p[3];
		size_t vlen = ((size_t)p[4] << 24) | ((size_t)p[5] << 16) | ((size_t)p[6] << 8) | (size_t)p[7];
		if (klen > PROBE_MAX_KEY || vlen > PROBE_MAX_VALUE) {
			/* protocol error: no way to resynchronise, drop the rest */
			return n;
		}
		if (n - off - 8 < klen + vlen)
			break;
		fieldset_add(fs, buf + off + 8, klen, buf + off + 8 + klen, vlen);
		off += 8 + klen + vlen;
	}
	return off;
}

/* ------------------------------------------------------------------ child side ------------------------------------------- */

void probe_say(struct probe_ctx *c, const char *key, const char *fmt, ...)
{
	char val[4096];
	char hdr[8];
	unsigned klen, vlen, i;
	va_list ap;
	int n;

	if (!c || (c->fd < 0 && (!c->mem || c->mem_len == c->mem_cap)) || !key || !fmt)
		return;
	klen = (unsigned)strlen(key);
	if (klen > PROBE_MAX_KEY)
		klen = PROBE_MAX_KEY;
	va_start(ap, fmt);
	n = vsnprintf(val, sizeof val, fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	vlen = (unsigned)n;
	if (vlen > sizeof val - 1)
		vlen = (unsigned)sizeof val - 1;
	hdr[0] = (char)(klen >> 24); hdr[1] = (char)(klen >> 16); hdr[2] = (char)(klen >> 8); hdr[3] = (char)klen;
	hdr[4] = (char)(vlen >> 24); hdr[5] = (char)(vlen >> 16); hdr[6] = (char)(vlen >> 8); hdr[7] = (char)vlen;
	if (c->fd < 0) {
		/* in-process fallback: append the same record to the context's buffer, never block */
		if (c->mem_len + 8 + klen + vlen > c->mem_cap)
			return;
		memcpy(c->mem + c->mem_len, hdr, 8);
		memcpy(c->mem + c->mem_len + 8, key, klen);
		memcpy(c->mem + c->mem_len + 8 + klen, val, vlen);
		c->mem_len += 8 + klen + vlen;
		return;
	}
	if (write(c->fd, hdr, 8) != 8)
		return;
	if (write(c->fd, key, klen) != (ssize_t)klen)
		return;
	for (i = 0; i < vlen; ) {
		ssize_t w = write(c->fd, val + i, vlen - i);
		if (w <= 0) {
			if (w < 0 && errno == EINTR)
				continue;
			return;
		}
		i += (unsigned)w;
	}
}

/* ------------------------------------------------------------------ console ---------------------------------------------- */

static const char *console_path(void)
{
	static const char *path;
	if (!path) {
		const char *env = getenv("N2VIDEO_CONSOLE");
		path = (env && env[0]) ? env : "/dev/console";
	}
	return path;
}

static void console_line(const char *msg)
{
	const char *path = console_path();
	size_t n = strlen(msg);
#ifdef __APPLE__
	int fd = open(path, O_WRONLY | O_NONBLOCK | O_NOCTTY);         /* the console device: never create, never block */
#else
	int fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0644);
#endif
	if (fd < 0)
		return;
	while (n) {
		ssize_t w = write(fd, msg, n);
		if (w <= 0) {
			if (w < 0 && errno == EINTR)
				continue;
			break;
		}
		msg += w;
		n -= (size_t)w;
	}
	close(fd);
}

/* Flatten a field value for one console line: no control characters, short. */
static void console_value(char *dst, size_t cap, const char *src)
{
	size_t i, o = 0;
	for (i = 0; src[i] && o + 1 < cap; i++) {
		unsigned char c = (unsigned char)src[i];
		dst[o++] = (c < 0x20 || c == 0x7f) ? ' ' : (char)c;
	}
	dst[o] = 0;
}

/* ------------------------------------------------------------------ JSON line -------------------------------------------- */

static void json_result(struct buf *b, const char *test, const char *result, int sig, long ms,
                        const struct fieldset *fs)
{
	unsigned i;

	buf_puts(b, "{\"test\":");
	buf_json_str(b, test, strlen(test));
	buf_puts(b, ",\"result\":\"");
	buf_puts(b, result);
	buf_puts(b, "\",\"signal\":");
	{
		char tmp[32];
		snprintf(tmp, sizeof tmp, "%d", sig);
		buf_puts(b, tmp);
	}
	buf_puts(b, ",\"ms\":");
	{
		char tmp[32];
		snprintf(tmp, sizeof tmp, "%ld", ms);
		buf_puts(b, tmp);
	}
	buf_puts(b, ",\"fields\":{");
	for (i = 0; i < fs->n; i++) {
		if (i)
			buf_putc(b, ',');
		buf_json_str(b, fs->f[i].key, strlen(fs->f[i].key));
		buf_putc(b, ':');
		buf_json_str(b, fs->f[i].val, strlen(fs->f[i].val));
	}
	buf_puts(b, "}}\n");
}

static int write_all(int fd, const char *p, size_t n)
{
	while (n) {
		ssize_t w = write(fd, p, n);
		if (w <= 0) {
			if (w < 0 && errno == EINTR)
				continue;
			return -1;
		}
		p += w;
		n -= (size_t)w;
	}
	return 0;
}

/* ------------------------------------------------------------------ clock ------------------------------------------------- */

uint64_t probe_now_ns(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* ------------------------------------------------------------------ the runner ------------------------------------------- */

static int run_one(const struct probe_test *t, int out_fd)
{
	struct fieldset fs;
	struct buf line = { 0 };
	uint64_t t0, t1;
	unsigned timeout_s = (t && t->timeout_s) ? t->timeout_s : PROBE_DEFAULT_TIMEOUT_S;
	const char *result = "crash";
	const char *name = (t && t->name) ? t->name : "(null)";
	int sig = 0;
	long ms;
	int pipefd[2];
	pid_t pid;
	int status = 0;
	int killed = 0;
	uint64_t deadline;
	char *inbuf = NULL;
	size_t inlen = 0;

	memset(&fs, 0, sizeof fs);
	t0 = probe_now_ns();
	deadline = t0 + (uint64_t)timeout_s * 1000000000ull;

	if (!t || !t->fn) {
		fieldset_add(&fs, "error", 5, "no test function", strlen("no test function"));
		result = "crash";
		goto emit;
	}
	if (pipe(pipefd) != 0) {
		fieldset_add(&fs, "error", 5, "pipe() failed", strlen("pipe() failed"));
		goto emit;
	}
	fflush(NULL);
	pid = fork();
	if (pid < 0) {
		/* fork is not available in this process (a sandbox may deny it).  Run the test here instead: no timeout and a
		 * crash takes the probe with it, but the result is not lost.  The field says so. */
		struct probe_ctx ctx;
		int rc;

		fieldset_add(&fs, "nofork", 6, "fork unavailable, ran in process without a timeout",
		             strlen("fork unavailable, ran in process without a timeout"));
		memset(&ctx, 0, sizeof ctx);
		ctx.fd = -1;
		ctx.mem = malloc(1u << 20);
		ctx.mem_cap = ctx.mem ? (1u << 20) : 0;
		rc = t->fn(&ctx);
		if (ctx.mem && ctx.mem_len)
			fields_parse(&fs, ctx.mem, ctx.mem_len);
		free(ctx.mem);
		close(pipefd[0]);
		close(pipefd[1]);
		if (rc == 0)
			result = "pass";
		else if (rc == 1)
			result = "fail";
		else if (rc == 2)
			result = "skip";
		else
			result = "crash";
		goto emit;
	}
	if (pid == 0) {
		struct probe_ctx ctx;
		int rc;
		close(pipefd[0]);
		if (probe_exec_path) {
			/* (lead, 11 Nov) a forked child that goes on to use CoreFoundation, XPC and libdispatch without an exec is
			 * not supported on Darwin: start the program again for this one test. Falls through if exec fails. */
			char fdtext[16];
			char *args[5];
			snprintf(fdtext, sizeof fdtext, "%d", pipefd[1]);
			args[0] = (char *)probe_exec_path;
			args[1] = (char *)"--child";
			args[2] = (char *)t->name;
			args[3] = fdtext;
			args[4] = NULL;
			execv(probe_exec_path, args);
		}
		ctx.fd = pipefd[1];
		signal(SIGPIPE, SIG_IGN);
		rc = t->fn(&ctx);
		if (rc != 0 && rc != 1 && rc != 2)
			rc = 1;
		close(pipefd[1]);
		_exit(rc);
	}
	close(pipefd[1]);

	inbuf = malloc(65536);
	if (inbuf)
		inlen = 0;
	for (;;) {
		struct pollfd pfd;
		uint64_t now;
		int pr;
		pid_t w;

		if (inbuf && inlen < PROBE_MAX_INBUF) {
			size_t room = PROBE_MAX_INBUF - inlen;
			if (room > 65536)
				room = 65536;
			pfd.fd = pipefd[0];
			pfd.events = POLLIN;
			pfd.revents = 0;
			pr = poll(&pfd, 1, 20);
			if (pr > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
				ssize_t n = read(pipefd[0], inbuf + inlen, room);
				if (n > 0) {
					size_t used;
					inlen += (size_t)n;
					used = fields_parse(&fs, inbuf, inlen);
					if (used) {
						memmove(inbuf, inbuf + used, inlen - used);
						inlen -= used;
					}
				}
			}
		} else {
			pr = 0;
		}
		w = waitpid(pid, &status, WNOHANG);
		if (w == pid)
			break;
		now = probe_now_ns();
		if (now >= deadline) {
			kill(pid, SIGKILL);
			killed = 1;
			while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
				;
			break;
		}
		if (pr <= 0)
			{ struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = 1000000L; nanosleep(&ts, NULL); }
	}
	/* drain whatever the child wrote before it exited */
	if (inbuf) {
		for (;;) {
			struct pollfd pfd;
			ssize_t n;
			size_t used;
			pfd.fd = pipefd[0];
			pfd.events = POLLIN;
			pfd.revents = 0;
			if (poll(&pfd, 1, 0) <= 0 || !(pfd.revents & (POLLIN | POLLHUP)))
				break;
			if (inlen >= PROBE_MAX_INBUF)
				break;
			n = read(pipefd[0], inbuf + inlen, PROBE_MAX_INBUF - inlen);
			if (n <= 0)
				break;
			inlen += (size_t)n;
			used = fields_parse(&fs, inbuf, inlen);
			if (used) {
				memmove(inbuf, inbuf + used, inlen - used);
				inlen -= used;
			}
		}
		free(inbuf);
	}
	close(pipefd[0]);

	if (killed) {
		result = "timeout";
		sig = SIGKILL;
	} else if (WIFSIGNALED(status)) {
		result = "crash";
		sig = WTERMSIG(status);
	} else if (WIFEXITED(status)) {
		switch (WEXITSTATUS(status)) {
		case 0: result = "pass"; break;
		case 1: result = "fail"; break;
		case 2: result = "skip"; break;
		default:
			result = "crash";
			{
				char tmp[32];
				snprintf(tmp, sizeof tmp, "%d", WEXITSTATUS(status));
				fieldset_add(&fs, "exit", 4, tmp, strlen(tmp));
			}
			break;
		}
	}
emit:
	t1 = probe_now_ns();
	ms = (long)((t1 - t0) / 1000000ull);
	json_result(&line, name, result, sig, ms, &fs);
	{
		struct buf cons = { 0 };
		unsigned i;
		buf_puts(&cons, "N2VIDEO probe: ");
		buf_puts(&cons, name);
		buf_putc(&cons, ' ');
		buf_puts(&cons, result);
		for (i = 0; i < fs.n && i < 3; i++) {
			char v[161];
			console_value(v, sizeof v, fs.f[i].val);
			buf_putc(&cons, ' ');
			buf_puts(&cons, fs.f[i].key);
			buf_putc(&cons, '=');
			buf_puts(&cons, v);
		}
		buf_putc(&cons, '\n');
		if (!cons.oom && cons.p)
			console_line(cons.p);
		free(cons.p);
	}
	/* (lead, 11 Nov) every field, packed into lines of at most ~300 characters: the console is the hands-free channel
	 * (autotest's `phone.py logs`), the .jsonl is only read at the next snapshot. */
	{
		struct buf cons = { 0 };
		unsigned i;
		for (i = 0; i < fs.n; i++) {
			char v[161];
			console_value(v, sizeof v, fs.f[i].val);
			if (cons.n == 0) {
				buf_puts(&cons, "N2VIDEO probe: + ");
				buf_puts(&cons, name);
			}
			buf_puts(&cons, " | ");
			buf_puts(&cons, fs.f[i].key);
			buf_putc(&cons, '=');
			buf_puts(&cons, v);
			if (cons.n > 300 || i + 1 == fs.n) {
				buf_putc(&cons, '\n');
				if (!cons.oom && cons.p)
					console_line(cons.p);
				cons.n = 0;
				if (cons.p)
					cons.p[0] = 0;
			}
		}
		free(cons.p);
	}
	if (!line.oom && line.p && out_fd >= 0) {
		if (write_all(out_fd, line.p, line.n) == 0)
			fsync(out_fd);
	}
	free(line.p);
	fieldset_free(&fs);
	return strcmp(result, "pass") == 0 || strcmp(result, "skip") == 0 ? 0 : 1;
}

int probe_run(const struct probe_test *tests, unsigned count, const char *out_path)
{
	int fd;
	unsigned i;
	int bad = 0;

	if (!out_path)
		return -1;
	fd = open(out_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd < 0)
		return -1;
	for (i = 0; i < count; i++)
		bad |= run_one(&tests[i], fd);
	close(fd);
	return bad;
}

/* (lead) The child side of probe_exec_path: runs the test called `name` with its fields going to descriptor fd.
 * Returns the test's result (0, 1, 2), or -1 when no test of the array has that name. Declared by probe_main.c. */
int probe_child(const struct probe_test *tests, unsigned count, const char *name, int fd)
{
	unsigned i;

	for (i = 0; i < count; i++) {
		if (strcmp(tests[i].name, name) == 0) {
			struct probe_ctx ctx;
			int rc;
			memset(&ctx, 0, sizeof ctx);
			ctx.fd = fd;
			signal(SIGPIPE, SIG_IGN);
			rc = tests[i].fn(&ctx);
			if (rc != 0 && rc != 1 && rc != 2)
				rc = 1;
			close(fd);
			return rc;
		}
	}
	return -1;
}

/* ------------------------------------------------------------------ next output number ----------------------------------- *
 * Not part of the frozen header: probe_main.c and test/t_core.c declare it themselves.
 * 1 + the highest N of the files named probe-<N>.jsonl in dir.  Missing or unreadable directory: 1. */

unsigned probe_next_index(const char *dir)
{
	DIR *d;
	struct dirent *de;
	unsigned long best = 0;

	d = dir ? opendir(dir) : NULL;
	if (!d)
		return 1;
	while ((de = readdir(d)) != NULL) {
		const char *s;
		char *end;
		unsigned long v;

		if (strncmp(de->d_name, "probe-", 6) != 0)
			continue;
		s = de->d_name + 6;
		if (!(*s >= '0' && *s <= '9'))
			continue;
		v = strtoul(s, &end, 10);
		if (!end || strcmp(end, ".jsonl") != 0)
			continue;
		if (v > 1000000ul)
			continue;
		if (v > best)
			best = v;
	}
	closedir(d);
	return (unsigned)(best + 1);
}

/* ------------------------------------------------------------------ helpers ---------------------------------------------- */

void probe_fill_nv12(uint8_t *y, size_t ystride, uint8_t *uv, size_t uvstride, int w, int h, int frame)
{
	int x, yy;
	int ox = 16 + 4 * frame;
	int oy = 16 + 2 * frame;

	if (!y || !uv || w <= 0 || h <= 0)
		return;
	for (yy = 0; yy < h; yy++) {
		uint8_t *row = y + (size_t)yy * ystride;
		for (x = 0; x < w; x++)
			row[x] = (uint8_t)((x + 2 * yy + 3 * frame) & 255);
	}
	for (yy = 0; yy < h / 2; yy++) {
		uint8_t *row = uv + (size_t)yy * uvstride;
		for (x = 0; x < w / 2; x++) {
			row[2 * x + 0] = (uint8_t)(128 + ((x + frame) & 31));
			row[2 * x + 1] = (uint8_t)(128 - (yy & 31));
		}
	}
	for (yy = oy; yy < oy + 32; yy++) {
		if (yy < 0 || yy >= h)
			continue;
		for (x = ox; x < ox + 32; x++) {
			if (x < 0 || x >= w)
				continue;
			y[(size_t)yy * ystride + x] = 235;
		}
	}
}

double probe_psnr(const uint8_t *a, size_t astride, const uint8_t *b, size_t bstride, int w, int h)
{
	uint64_t sse = 0;
	int x, yy;
	double mse, psnr;

	if (!a || !b || w <= 0 || h <= 0)
		return 0.0;
	for (yy = 0; yy < h; yy++) {
		const uint8_t *ra = a + (size_t)yy * astride;
		const uint8_t *rb = b + (size_t)yy * bstride;
		for (x = 0; x < w; x++) {
			int d = (int)ra[x] - (int)rb[x];
			sse += (uint64_t)(d * d);
		}
	}
	if (sse == 0)
		return 99.0;
	mse = (double)sse / ((double)w * (double)h);
	psnr = 10.0 * __builtin_log10(255.0 * 255.0 / mse);
	if (psnr > 99.0)
		psnr = 99.0;
	return psnr;
}

/* ------------------------------------------------------------------ MD5 (RFC 1321) --------------------------------------- */

struct md5_state {
	uint32_t a, b, c, d;
	uint64_t len;           /* bytes */
	unsigned char buf[64];
	size_t have;
};

static const uint32_t md5_k[64] = {
	0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
	0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
	0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
	0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
	0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
	0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
	0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
	0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
	0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
	0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
	0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
	0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
	0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
	0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
	0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
	0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u
};

/* Shift amount for step i: round (i / 16) picks the row, i mod 4 the column (RFC 1321 section 3.4). */
static const unsigned md5_shift[16] = {
	7, 12, 17, 22,
	5,  9, 14, 20,
	4, 11, 16, 23,
	6, 10, 15, 21
};


static uint32_t md5_rotl(uint32_t x, unsigned c)
{
	return (x << c) | (x >> (32 - c));
}

static void md5_block(struct md5_state *s, const unsigned char *p)
{
	uint32_t m[16];
	uint32_t a = s->a, b = s->b, c = s->c, d = s->d;
	unsigned i;

	for (i = 0; i < 16; i++)
		m[i] = (uint32_t)p[4 * i] | ((uint32_t)p[4 * i + 1] << 8) |
		       ((uint32_t)p[4 * i + 2] << 16) | ((uint32_t)p[4 * i + 3] << 24);

	for (i = 0; i < 64; i++) {
		uint32_t f;
		unsigned g;
		if (i < 16) {
			f = (b & c) | (~b & d);
			g = i;
		} else if (i < 32) {
			f = (d & b) | (~d & c);
			g = (5 * i + 1) & 15;
		} else if (i < 48) {
			f = b ^ c ^ d;
			g = (3 * i + 5) & 15;
		} else {
			f = c ^ (b | ~d);
			g = (7 * i) & 15;
		}
		{
			uint32_t tmp = d;
			d = c;
			c = b;
			b = b + md5_rotl(a + f + md5_k[i] + m[g], md5_shift[(i >> 4) * 4 + (i & 3)]);
			a = tmp;
		}
	}
	s->a += a;
	s->b += b;
	s->c += c;
	s->d += d;
}

static void md5_init(struct md5_state *s)
{
	s->a = 0x67452301u;
	s->b = 0xefcdab89u;
	s->c = 0x98badcfeu;
	s->d = 0x10325476u;
	s->len = 0;
	s->have = 0;
}

static void md5_update(struct md5_state *s, const void *data, size_t n)
{
	const unsigned char *p = data;
	s->len += n;
	if (s->have) {
		size_t take = 64 - s->have;
		if (take > n)
			take = n;
		memcpy(s->buf + s->have, p, take);
		s->have += take;
		p += take;
		n -= take;
		if (s->have == 64) {
			md5_block(s, s->buf);
			s->have = 0;
		}
	}
	while (n >= 64) {
		md5_block(s, p);
		p += 64;
		n -= 64;
	}
	if (n) {
		memcpy(s->buf, p, n);
		s->have = n;
	}
}

static void md5_final(struct md5_state *s, unsigned char out[16])
{
	unsigned char tail[128];
	size_t n = s->have;
	uint64_t bits = s->len * 8;
	size_t i;

	memcpy(tail, s->buf, n);
	tail[n++] = 0x80;
	while ((n & 63) != 56)
		tail[n++] = 0;
	for (i = 0; i < 8; i++)
		tail[n++] = (unsigned char)(bits >> (8 * i));
	md5_block(s, tail);
	if (n > 64)
		md5_block(s, tail + 64);
	for (i = 0; i < 4; i++) {
		out[i]      = (unsigned char)(s->a >> (8 * i));
		out[i + 4]  = (unsigned char)(s->b >> (8 * i));
		out[i + 8]  = (unsigned char)(s->c >> (8 * i));
		out[i + 12] = (unsigned char)(s->d >> (8 * i));
	}
}

void probe_md5(const void *data, size_t n, char hex[33])
{
	static const char digits[] = "0123456789abcdef";
	struct md5_state st;
	unsigned char d[16];
	unsigned i;

	md5_init(&st);
	md5_update(&st, data, n);
	md5_final(&st, d);
	for (i = 0; i < 16; i++) {
		hex[2 * i] = digits[d[i] >> 4];
		hex[2 * i + 1] = digits[d[i] & 15];
	}
	hex[32] = 0;
}
