/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_logcap.h -- size cap for vcodecd.log (opus-robust, 12 Nov 2026).  Header-only (static functions) so the daemon
 * and its host test include it without touching any build script.
 *
 * On the phone launchd opens /private/var/db/nord2-vcodec/vcodecd.log as the job's StandardOutPath/StandardErrorPath
 * (guest-vcodec.sh) and the daemon only writes to fd 2, so under KeepAlive the file grew without bound.  The cap:
 *
 *   start:  if fd 2 IS that file (same st_dev/st_ino as the path; nothing happens for a terminal, a pipe or another
 *           file) and it is larger than the cap, rename it to vcodecd.log.1 (replacing the old .1) and continue in a
 *           fresh vcodecd.log (fd 1 too when it is the same file).
 *   run:    every log line is counted; when the file passes the cap the same rotation happens (one thread does it,
 *           the others keep writing to whichever file their descriptor names at that instant: no line is lost).
 *
 * Disk use is therefore bounded by about 2 x cap (the live file + one backup).  Defaults: 4 MiB, the phone path; the
 * daemon's --log-file PATH / --log-max BYTES override both (--log-max 0 disables the cap).  The rotation writes one
 * marker line into the new file with the size of the old one.
 */
#ifndef N2VCD_LOGCAP_H
#define N2VCD_LOGCAP_H

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define N2VCD_LOGCAP_DEFAULT_MAX (4ull * 1024ull * 1024ull)
#define N2VCD_LOGCAP_PHONE_PATH "/private/var/db/nord2-vcodec/vcodecd.log"

struct n2vcd_logcap {
	char path[512];                 /* empty: disabled */
	char path1[520];                /* path + ".1" */
	uint64_t max;
	int fd;                         /* the descriptor that is the log (2) */
	_Atomic uint64_t bytes;         /* current size of the live file (estimate: counted writes) */
	_Atomic int rotating;
	_Atomic int off;                /* a rotation failed (read-only, ENOSPC): stop trying, keep writing */
	_Atomic uint32_t rotations;
};

/* Is `fd` the regular file at `path`? */
static int
n2vcd_logcap_is_file(int fd, const char *path, struct stat *out)
{
	struct stat a, b;

	if (fstat(fd, &a) != 0 || !S_ISREG(a.st_mode) || stat(path, &b) != 0)
		return 0;
	if (a.st_dev != b.st_dev || a.st_ino != b.st_ino)
		return 0;
	if (out)
		*out = a;
	return 1;
}

/* Move the live file to .1 and point fd (and fd 1 when it is the same file) at a fresh one.  Returns 0 or -errno. */
static int
n2vcd_logcap_rotate(struct n2vcd_logcap *c, uint64_t old_bytes)
{
	struct stat st1;
	int same1 = n2vcd_logcap_is_file(1, c->path, &st1);
	int nfd;

	if (rename(c->path, c->path1) != 0)
		return -errno;
	nfd = open(c->path, O_WRONLY | O_CREAT | O_APPEND | O_TRUNC, 0644);
	if (nfd < 0)
		return -errno;
	if (same1 && nfd != 1)
		(void)dup2(nfd, 1);
	if (nfd != c->fd) {
		(void)dup2(nfd, c->fd);
		(void)close(nfd);
	}
	{
		char line[700];
		int n = snprintf(line, sizeof(line), "N2VCD log rotated: previous %llu bytes are in %s (cap %llu)\n",
		                 (unsigned long long)old_bytes, c->path1, (unsigned long long)c->max);
		if (n > 0) {
			size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1u;
			(void)!write(c->fd, line, len);
			atomic_store(&c->bytes, (uint64_t)len);
		} else {
			atomic_store(&c->bytes, 0);
		}
	}
	atomic_fetch_add(&c->rotations, 1u);
	return 0;
}

/* Arm the cap for `fd`.  Returns 1 armed, 0 not armed (path NULL/empty, max 0, fd is not that file), -errno on a failed
 * start rotation.  `note` receives one human-readable line for the daemon's log. */
static int
n2vcd_logcap_init(struct n2vcd_logcap *c, const char *path, uint64_t max, int fd, char *note, size_t note_len)
{
	struct stat st;

	memset(c, 0, sizeof(*c));
	c->fd = fd;
	if (note && note_len)
		note[0] = '\0';
	if (!path || !path[0] || max == 0)
		return 0;
	if (strlen(path) >= sizeof(c->path))
		return 0;
	if (!n2vcd_logcap_is_file(fd, path, &st)) {
		if (note)
			(void)snprintf(note, note_len, "log cap off: fd %d is not %s", fd, path);
		return 0;
	}
	(void)snprintf(c->path, sizeof(c->path), "%s", path);
	(void)snprintf(c->path1, sizeof(c->path1), "%s.1", path);
	c->max = max;
	atomic_store(&c->bytes, (uint64_t)st.st_size);
	if ((uint64_t)st.st_size > max) {
		int r = n2vcd_logcap_rotate(c, (uint64_t)st.st_size);
		if (r != 0) {
			if (note)
				(void)snprintf(note, note_len, "log cap: rotating %s (%llu bytes) failed: %d", path,
				               (unsigned long long)st.st_size, r);
			c->path[0] = '\0';
			return r;
		}
		if (note)
			(void)snprintf(note, note_len, "log cap %llu bytes on %s: rotated %llu bytes at start", (unsigned long long)max,
			               path, (unsigned long long)st.st_size);
		return 1;
	}
	if (note)
		(void)snprintf(note, note_len, "log cap %llu bytes on %s (now %llu)", (unsigned long long)max, path,
		               (unsigned long long)st.st_size);
	return 1;
}

/* Count `n` bytes just written to the log; rotate when the file passed the cap.  Cheap when armed and below the cap
 * (one atomic add), nothing when not armed. */
static void
n2vcd_logcap_account(struct n2vcd_logcap *c, size_t n)
{
	uint64_t now;
	int expected = 0;

	if (!c->path[0] || atomic_load(&c->off))
		return;
	now = atomic_fetch_add(&c->bytes, (uint64_t)n) + (uint64_t)n;
	if (now <= c->max)
		return;
	if (!atomic_compare_exchange_strong(&c->rotating, &expected, 1))
		return;
	/* Somebody else may have rotated between our add and the exchange. */
	now = atomic_load(&c->bytes);
	if (now > c->max && n2vcd_logcap_rotate(c, now) != 0)
		atomic_store(&c->off, 1);
	atomic_store(&c->rotating, 0);
}

#endif /* N2VCD_LOGCAP_H */
