/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_server.c -- server transport of vcodecd (P03's implementation; see n2vcd_server.h).  Same API and semantics
 * as daemon/n2vcd_srv.c, including: MSG_TRUNC oversized datagrams are consumed and reported -EMSGSIZE, a dead client
 * is reported once as a 0-byte close notice, sends never raise SIGPIPE, and receives are bounded by the caller.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "n2vcd_server.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "n2vc_ioctl.h"
#include "redwood_vcodec.h"

#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif
#ifndef ENOSYS
#define ENOSYS 78
#endif

#if !defined(__APPLE__)
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
/* glibc declares these as enum values, not macros: on Linux both names always exist. */
#if !defined(__linux__)
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#ifndef MSG_TRUNC
#define MSG_TRUNC 0
#endif
#endif
#endif

#if defined(N2VC_TEST_HOOKS)
static n2vc_ioctl_fn g_srv_hook;
static void *g_srv_hook_ctx;

void
n2vcd_srv_test_set_ioctl(n2vc_ioctl_fn fn, void *ctx)
{
	g_srv_hook = fn;
	g_srv_hook_ctx = ctx;
}
#endif

static int
srv_ioctl(int fd, unsigned long req, void *arg)
{
	for (;;) {
#if defined(N2VC_TEST_HOOKS)
		if (g_srv_hook) {
			if (g_srv_hook(g_srv_hook_ctx, fd, req, arg) == 0) { return 0; }
			if (errno == EINTR) { continue; }
			return -errno;
		}
#endif
		int r = ioctl(fd, req, arg);
		if (r == 0) { return 0; }
		if (errno == EINTR) { continue; }
		return -errno;
	}
}

enum { N2VCD_SRV_SOCKET = 1, N2VCD_SRV_RELAY = 2 };

struct n2vcd_srv {
	int kind;
	int lfd;
	char path[120];
	uint32_t next_id;
	uint32_t scan;               /* round-robin client scan, so one busy client cannot starve the others */
	struct {
		int used;
		int fd;
		uint32_t id;
	} cl[N2VCD_SRV_CLIENTS];
};

static void
seterr(char *err, size_t errlen, const char *fmt, ...)
{
	if (!err || errlen == 0) { return; }
	va_list ap;
	va_start(ap, fmt);
	(void)vsnprintf(err, errlen, fmt, ap);
	va_end(ap);
}

static struct n2vcd_srv *
srv_new(int kind, int fd)
{
	struct n2vcd_srv *s = (struct n2vcd_srv *)calloc(1, sizeof(*s));
	if (!s) { return NULL; }
	s->kind = kind;
	s->lfd = fd;
	s->next_id = 1;
	for (unsigned i = 0; i < N2VCD_SRV_CLIENTS; i++) { s->cl[i].fd = -1; }
	return s;
}

struct n2vcd_srv *
n2vcd_srv_open_relay(const char *dev, char *err, size_t errlen)
{
	if (!dev) { dev = "/dev/redwood-vcodec"; }
	int fd;
	do {
		fd = open(dev, O_RDWR);
	} while (fd < 0 && errno == EINTR);
	if (fd < 0) {
		seterr(err, errlen, "open %s: %s", dev, strerror(errno));
		return NULL;
	}
	struct rwvc_info info;
	memset(&info, 0, sizeof(info));
	int rc = srv_ioctl(fd, RWVC_INFO, &info);
	if (rc != 0) {
		seterr(err, errlen, "RWVC_INFO on %s: %s", dev, strerror(-rc));
		(void)close(fd);
		return NULL;
	}
	if (info.abi_version != RWVC_ABI_VERSION) {
		seterr(err, errlen, "RWVC_INFO on %s: abi %u, want %u", dev, info.abi_version, RWVC_ABI_VERSION);
		(void)close(fd);
		return NULL;
	}
	struct n2vcd_srv *s = srv_new(N2VCD_SRV_RELAY, fd);
	if (!s) {
		(void)close(fd);
		seterr(err, errlen, "out of memory");
	}
	return s;
}

#if defined(__APPLE__)

struct n2vcd_srv *
n2vcd_srv_open_socket(const char *path, char *err, size_t errlen)
{
	(void)path;
	seterr(err, errlen, "no UNIX socket transport on iOS: vcodecd serves the kernel relay");
	return NULL;
}

#else

struct n2vcd_srv *
n2vcd_srv_open_socket(const char *path, char *err, size_t errlen)
{
	if (!path || !*path) {
		seterr(err, errlen, "empty socket path");
		return NULL;
	}
	if (strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
		seterr(err, errlen, "socket path too long (max %u)",
		       (unsigned)sizeof(((struct sockaddr_un *)0)->sun_path) - 1u);
		return NULL;
	}
	int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		seterr(err, errlen, "socket: %s", strerror(errno));
		return NULL;
	}
	struct sockaddr_un sa;
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	memcpy(sa.sun_path, path, strlen(path) + 1u);
	(void)unlink(path);
	if (bind(fd, (struct sockaddr *)&sa, (socklen_t)sizeof(sa)) < 0) {
		seterr(err, errlen, "bind %s: %s", path, strerror(errno));
		(void)close(fd);
		return NULL;
	}
	if (listen(fd, N2VCD_SRV_CLIENTS) < 0) {
		seterr(err, errlen, "listen %s: %s", path, strerror(errno));
		(void)close(fd);
		(void)unlink(path);
		return NULL;
	}
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0) { (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK); }
	struct n2vcd_srv *s = srv_new(N2VCD_SRV_SOCKET, fd);
	if (!s) {
		(void)close(fd);
		(void)unlink(path);
		seterr(err, errlen, "out of memory");
		return NULL;
	}
	(void)snprintf(s->path, sizeof(s->path), "%s", path);
	return s;
}

static void
sock_accept_all(struct n2vcd_srv *s)
{
	for (;;) {
		int fd = accept(s->lfd, NULL, NULL);
		if (fd < 0) {
			if (errno == EINTR) { continue; }
			return;                     /* EAGAIN: nothing more pending */
		}
		int flags = fcntl(fd, F_GETFL, 0);
		if (flags >= 0) { (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK); }
		int slot = -1;
		for (unsigned i = 0; i < N2VCD_SRV_CLIENTS; i++) {
			if (!s->cl[i].used) { slot = (int)i; break; }
		}
		if (slot < 0) {
			(void)close(fd);            /* full: refuse this connection, keep the established ones */
			continue;
		}
		s->cl[slot].used = 1;
		s->cl[slot].fd = fd;
		s->cl[slot].id = s->next_id++;
		if (s->next_id == 0) { s->next_id = 1; }
	}
}

static void
sock_drop(struct n2vcd_srv *s, unsigned slot)
{
	if (!s->cl[slot].used) { return; }
	(void)close(s->cl[slot].fd);
	s->cl[slot].fd = -1;
	s->cl[slot].used = 0;
	s->cl[slot].id = 0;
}

#endif /* !__APPLE__ */

int
n2vcd_srv_recv(struct n2vcd_srv *s, void *buf, uint32_t capacity, uint32_t *from, int timeout_ms)
{
	if (!s || !buf || capacity == 0 || !from) { return -EINVAL; }
	if (timeout_ms < 0) { timeout_ms = 0; }

	if (s->kind == N2VCD_SRV_RELAY) {
		struct rwvc_msg m;
		memset(&m, 0, sizeof(m));
		m.user = (uint64_t)(uintptr_t)buf;
		m.bytes = capacity;
		m.client = 0;
		m.timeout_ms = (uint32_t)timeout_ms;
		int rc = srv_ioctl(s->lfd, RWVC_MSG_RECV, &m);
		if (rc != 0) {
			if (-rc == EAGAIN || -rc == EWOULDBLOCK || -rc == ETIMEDOUT || -rc == EINTR) { return -EAGAIN; }
			return rc;
		}
		if (m.bytes > capacity) {
			*from = m.client;
			return -EMSGSIZE;
		}
		*from = m.client;
		return (int)m.bytes;                /* 0 = close notice */
	}

#if defined(__APPLE__)
	(void)timeout_ms;
	return -ENOSYS;
#else
	struct pollfd pfd[1 + N2VCD_SRV_CLIENTS];
	unsigned idx[1 + N2VCD_SRV_CLIENTS];
	unsigned n = 0;
	pfd[n].fd = s->lfd;
	pfd[n].events = POLLIN;
	pfd[n].revents = 0;
	idx[n] = 0;
	n++;
	for (unsigned k = 0; k < N2VCD_SRV_CLIENTS; k++) {
		unsigned i = (s->scan + k) % N2VCD_SRV_CLIENTS;
		if (!s->cl[i].used) { continue; }
		pfd[n].fd = s->cl[i].fd;
		pfd[n].events = POLLIN;
		pfd[n].revents = 0;
		idx[n] = i;
		n++;
	}
	int r = poll(pfd, n, timeout_ms);
	if (r < 0) { return errno == EINTR ? -EAGAIN : -errno; }
	if (r == 0) { return -EAGAIN; }
	if (pfd[0].revents & POLLIN) { sock_accept_all(s); }
	for (unsigned k = 1; k < n; k++) {
		if (!(pfd[k].revents & (POLLIN | POLLHUP | POLLERR))) { continue; }
		unsigned slot = idx[k];
		if (!s->cl[slot].used) { continue; }
		ssize_t got = recv(s->cl[slot].fd, buf, capacity, MSG_TRUNC);
		s->scan = (slot + 1u) % N2VCD_SRV_CLIENTS;
		if (got > (ssize_t)capacity) {
			*from = s->cl[slot].id;      /* consumed; the client stays connected */
			return -EMSGSIZE;
		}
		if (got > 0) {
			*from = s->cl[slot].id;
			return (int)got;
		}
		if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) { continue; }
		*from = s->cl[slot].id;
		sock_drop(s, slot);
		return 0;                           /* close notice: EOF or a broken connection */
	}
	return -EAGAIN;                          /* only a new accept happened */
#endif
}

int
n2vcd_srv_send(struct n2vcd_srv *s, uint32_t to, const void *msg, uint32_t bytes)
{
	if (!s || !msg || bytes == 0 || to == 0 || bytes > RWVC_MSG_MAX) { return -EINVAL; }
	if (s->kind == N2VCD_SRV_RELAY) {
		struct rwvc_msg m;
		memset(&m, 0, sizeof(m));
		m.user = (uint64_t)(uintptr_t)msg;
		m.bytes = bytes;
		m.client = to;
		m.timeout_ms = 0;
		int rc = srv_ioctl(s->lfd, RWVC_MSG_SEND, &m);
		if (rc != 0) {
			if (-rc == EAGAIN || -rc == EWOULDBLOCK) { return -EAGAIN; }
			if (-rc == ESRCH) { return -EPIPE; }
			return rc;
		}
		return 0;
	}

#if defined(__APPLE__)
	(void)to;
	return -ENOSYS;
#else
	for (unsigned i = 0; i < N2VCD_SRV_CLIENTS; i++) {
		if (!s->cl[i].used || s->cl[i].id != to) { continue; }
		ssize_t n;
		do {
			n = send(s->cl[i].fd, msg, bytes, MSG_NOSIGNAL);
		} while (n < 0 && errno == EINTR);
		if (n == (ssize_t)bytes) { return 0; }
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { return -EAGAIN; }   /* dropped */
		sock_drop(s, i);
		return -EPIPE;
	}
	return -EPIPE;
#endif
}

int
n2vcd_srv_dma_info(struct n2vcd_srv *s, uint32_t handle, uint32_t *pa, uint32_t *bytes)
{
	if (!s || !pa || !bytes || handle == 0) { return -EINVAL; }
#if defined(RWVC_DMA_INFO)
	if (s->kind == N2VCD_SRV_RELAY) {
		struct rwvc_dma_info info;
		memset(&info, 0, sizeof(info));
		info.handle = handle;
		int rc = srv_ioctl(s->lfd, RWVC_DMA_INFO, &info);
		if (rc != 0) { return rc; }
		*pa = info.out_pa;
		*bytes = info.bytes;
		return *pa != 0u ? 0 : -ENOENT;
	}
#endif
	return -ENOSYS;
}

void
n2vcd_srv_close(struct n2vcd_srv *s)
{
	if (!s) { return; }
	if (s->kind == N2VCD_SRV_SOCKET) {
#if !defined(__APPLE__)
		for (unsigned i = 0; i < N2VCD_SRV_CLIENTS; i++) {
			if (s->cl[i].used) { sock_drop(s, i); }
		}
#endif
	}
	if (s->lfd >= 0) { (void)close(s->lfd); }
	if (s->path[0]) { (void)unlink(s->path); }
	free(s);
}
