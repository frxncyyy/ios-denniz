/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_srv.c -- server transport of vcodecd: a SOCK_SEQPACKET UNIX socket (host tests) or the kernel relay of
 * /dev/redwood-vcodec minor 0 (the phone).  See n2vcd_srv.h for the contract; include/n2vcd_proto.h is the wire format.
 *
 * On iOS only the relay exists: Darwin has no SOCK_SEQPACKET for AF_UNIX and the phone path never needs a socket, so the
 * socket half is compiled out under __APPLE__.  The host build compiles both and selects at run time.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "n2vcd_srv.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if !defined(__APPLE__)
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif

#include "redwood_vcodec.h"

/* The gpu/video SDK errno.h is a subset: two names the transport compares against are missing there. */
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif
#ifndef ENOSYS
#define ENOSYS 78
#endif

#ifdef __APPLE__
/* Darwin's ioctl(2); gpu/video/sdk/sysinc has no sys/ioctl.h and the daemon must not depend on the SDK's private headers. */
extern int ioctl(int fd, unsigned long request, ...);
#endif

enum { N2VCD_SRV_SOCKET = 1, N2VCD_SRV_RELAY = 2 };

struct n2vcd_srv {
	int kind;
	int lfd;                          /* listening socket, or the relay device */
	char path[108];                   /* socket path to unlink on close, empty for the relay */
	uint32_t next_id;
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
	int fd = open(dev, O_RDWR);
	if (fd < 0) {
		seterr(err, errlen, "open %s: %s", dev, strerror(errno));
		return NULL;
	}
	struct rwvc_info info;
	memset(&info, 0, sizeof(info));
	if (ioctl(fd, RWVC_INFO, &info) < 0) {
		seterr(err, errlen, "RWVC_INFO on %s: %s", dev, strerror(errno));
		(void)close(fd);
		return NULL;
	}
	if (info.abi_version != RWVC_ABI_VERSION) {
		seterr(err, errlen, "RWVC_INFO on %s: abi %u, want %u", dev, (unsigned)info.abi_version, RWVC_ABI_VERSION);
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
#endif

#if !defined(__APPLE__)

struct n2vcd_srv *
n2vcd_srv_open_socket(const char *path, char *err, size_t errlen)
{
	if (!path || !*path) {
		seterr(err, errlen, "empty socket path");
		return NULL;
	}
	if (strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
		seterr(err, errlen, "socket path too long (max %u)", (unsigned)sizeof(((struct sockaddr_un *)0)->sun_path) - 1);
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
	memcpy(sa.sun_path, path, strlen(path) + 1);
	(void)unlink(path);
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
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
	/* accept() must never block: the loop accepts everything pending and returns to poll(). */
	int lflags = fcntl(fd, F_GETFL, 0);
	if (lflags >= 0) { (void)fcntl(fd, F_SETFL, lflags | O_NONBLOCK); }
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
			return;                    /* EAGAIN or a transient error: nothing more to accept now */
		}
		int flags = fcntl(fd, F_GETFL, 0);
		if (flags >= 0) { (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK); }
		int slot = -1;
		for (unsigned i = 0; i < N2VCD_SRV_CLIENTS; i++) {
			if (!s->cl[i].used) { slot = (int)i; break; }
		}
		if (slot < 0) {
			(void)close(fd);           /* too many clients: refuse this one, the others keep their ids */
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
		if (ioctl(s->lfd, RWVC_MSG_RECV, &m) < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT || errno == EINTR) { return -EAGAIN; }
			return -errno;
		}
		if (m.bytes > capacity) { return -EMSGSIZE; }
		*from = m.client;
		return (int)m.bytes;               /* 0 = close notice, as the kernel documents */
	}

#if defined(__APPLE__)
	(void)timeout_ms;
	return -ENOSYS;                        /* only the relay exists on iOS; --socket is refused earlier */
#else
	struct pollfd pfd[1 + N2VCD_SRV_CLIENTS];
	unsigned idx[1 + N2VCD_SRV_CLIENTS];
	unsigned n = 0;
	pfd[n].fd = s->lfd;
	pfd[n].events = POLLIN;
	pfd[n].revents = 0;
	idx[n] = 0;
	n++;
	for (unsigned i = 0; i < N2VCD_SRV_CLIENTS; i++) {
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
		if (got > (ssize_t)capacity) {
			*from = s->cl[slot].id;      /* the datagram is consumed; the client stays connected */
			return -EMSGSIZE;
		}
		if (got > 0) {
			*from = s->cl[slot].id;
			return (int)got;
		}
		if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) { continue; }
		*from = s->cl[slot].id;
		sock_drop(s, slot);
		return 0;                      /* close notice: EOF or a broken connection */
	}
	return -EAGAIN;                        /* only a new accept happened */
#endif
}

int
n2vcd_srv_send(struct n2vcd_srv *s, uint32_t to, const void *msg, uint32_t bytes)
{
	if (!s || !msg || bytes == 0) { return -EINVAL; }
	if (s->kind == N2VCD_SRV_RELAY) {
		struct rwvc_msg m;
		memset(&m, 0, sizeof(m));
		m.user = (uint64_t)(uintptr_t)msg;
		m.bytes = bytes;
		m.client = to;
		m.timeout_ms = 0;
		if (ioctl(s->lfd, RWVC_MSG_SEND, &m) < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) { return -EAGAIN; }
			if (errno == ESRCH) { return -EPIPE; }
			return -errno;
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
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { return -EAGAIN; }   /* dropped, client stays */
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
		if (ioctl(s->lfd, RWVC_DMA_INFO, &info) < 0) { return -errno; }
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
