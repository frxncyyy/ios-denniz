/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_transport.c -- the two transports of include/n2vcd_proto.h.
 *
 * Relay (the phone): every call is exactly one ioctl on /dev/redwood-vcodec-client; EINTR is retried, everything
 * else is returned as -errno.  DMA buffers are pool buffers allocated with RWVC_DMA_SHARED so vcodecd (minor 0) can
 * read them and look their physical addresses up with RWVC_DMA_INFO.
 *
 * Socket (host tests, and any host tool): SOCK_SEQPACKET UNIX socket.  Datagrams are one message each.  The buffers
 * are files in the directory "<socket path>.buf", one file per handle named in decimal ("1", "2", ...), created with
 * ftruncate(bytes); the server opens the same path, which is the host equivalent of the kernel pool.  vcodecd's
 * --dma-dir option points at that directory, so an unmodified daemon can serve this transport.  Darwin has no
 * SOCK_SEQPACKET for AF_UNIX; on iOS this half compiles out and returns -ENOSYS (the phone always uses the relay).
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "n2vcd_proto.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "n2vc_ioctl.h"
#include "redwood_vcodec.h"

#ifndef ENOSYS
#define ENOSYS 78
#endif
#ifndef EPROTO
#define EPROTO 100
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif

#if !defined(__APPLE__)
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
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
static n2vc_ioctl_fn g_relay_hook;
static void *g_relay_hook_ctx;

void
n2vcd_relay_test_set_ioctl(n2vc_ioctl_fn fn, void *ctx)
{
	g_relay_hook = fn;
	g_relay_hook_ctx = ctx;
}
#endif

static int
relay_ioctl(int fd, unsigned long req, void *arg)
{
	for (;;) {
#if defined(N2VC_TEST_HOOKS)
		if (g_relay_hook) {
			if (g_relay_hook(g_relay_hook_ctx, fd, req, arg) == 0) { return 0; }
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

/* ---- relay ----------------------------------------------------------------------------------------------------- */

struct relay {
	int fd;
};

static int
relay_send(void *ctx, const void *msg, uint32_t bytes)
{
	struct relay *r = (struct relay *)ctx;
	if (!r || !msg || bytes == 0 || bytes > RWVC_MSG_MAX) { return -EINVAL; }
	struct rwvc_msg m;
	memset(&m, 0, sizeof(m));
	m.user = (uint64_t)(uintptr_t)msg;
	m.bytes = bytes;
	m.client = 0;              /* minor 1: the kernel stamps the sender's client id */
	m.timeout_ms = 0;          /* SEND never blocks */
	return relay_ioctl(r->fd, RWVC_MSG_SEND, &m);
}

static int
relay_recv(void *ctx, void *msg, uint32_t capacity, uint32_t timeout_ms)
{
	struct relay *r = (struct relay *)ctx;
	if (!r || !msg || capacity == 0) { return -EINVAL; }
	struct rwvc_msg m;
	memset(&m, 0, sizeof(m));
	m.user = (uint64_t)(uintptr_t)msg;
	m.bytes = capacity;
	m.client = 0;
	m.timeout_ms = timeout_ms;
	int rc = relay_ioctl(r->fd, RWVC_MSG_RECV, &m);
	if (rc != 0) { return rc; }
	if (m.bytes > capacity) { return -EMSGSIZE; }
	return (int)m.bytes;
}

static int
relay_buf_alloc(void *ctx, uint32_t bytes, uint32_t *handle)
{
	struct relay *r = (struct relay *)ctx;
	if (!r || !handle || bytes == 0) { return -EINVAL; }
	struct rwvc_dma_alloc a;
	memset(&a, 0, sizeof(a));
	a.bytes = bytes;
	a.flags = RWVC_DMA_SHARED;   /* vcodecd (minor 0) must read this buffer */
	int rc = relay_ioctl(r->fd, RWVC_DMA_ALLOC, &a);
	if (rc != 0) { return rc; }
	if (a.out_handle == 0) { return -EIO; }
	*handle = a.out_handle;
	return 0;
}

static int
relay_buf_rw(void *ctx, uint32_t handle, uint32_t offset, void *data, uint32_t bytes, int write)
{
	struct relay *r = (struct relay *)ctx;
	if (!r || !data || handle == 0) { return -EINVAL; }
	if (bytes > 0xffffffffu - offset) { return -EINVAL; }
	struct rwvc_dma_rw w;
	memset(&w, 0, sizeof(w));
	w.handle = handle;
	w.offset = offset;
	w.user = (uint64_t)(uintptr_t)data;
	w.bytes = bytes;
	w.write = write ? 1u : 0u;
	return relay_ioctl(r->fd, RWVC_DMA_RW, &w);
}

static int
relay_buf_free(void *ctx, uint32_t handle)
{
	struct relay *r = (struct relay *)ctx;
	if (!r || handle == 0) { return -EINVAL; }
	struct rwvc_dma_free f;
	memset(&f, 0, sizeof(f));
	f.handle = handle;
	return relay_ioctl(r->fd, RWVC_DMA_FREE, &f);
}

static void
relay_close(void *ctx)
{
	struct relay *r = (struct relay *)ctx;
	if (!r) { return; }
	(void)close(r->fd);
	free(r);
}

int
n2vcd_transport_relay(struct n2vcd_transport *t, const char *dev_path)
{
	if (!t) { return -EINVAL; }
	if (!dev_path) { dev_path = "/dev/redwood-vcodec-client"; }
	int fd;
	do {
		fd = open(dev_path, O_RDWR);
	} while (fd < 0 && errno == EINTR);
	if (fd < 0) { return -errno; }
	/* Same ABI probe as n2vc_hw_open_dev(): a mismatched kernel is a clear version error, not a bad HELLO. */
	struct rwvc_info info;
	memset(&info, 0, sizeof(info));
	int rc = relay_ioctl(fd, RWVC_INFO, &info);
	if (rc != 0 || info.abi_version != RWVC_ABI_VERSION) {
		(void)close(fd);
		return rc != 0 ? rc : -EPROTO;
	}
	struct relay *r = (struct relay *)calloc(1, sizeof(*r));
	if (!r) {
		(void)close(fd);
		return -ENOMEM;
	}
	r->fd = fd;
	memset(t, 0, sizeof(*t));
	t->ctx = r;
	t->send = relay_send;
	t->recv = relay_recv;
	t->buf_alloc = relay_buf_alloc;
	t->buf_rw = relay_buf_rw;
	t->buf_free = relay_buf_free;
	t->close = relay_close;
	return 0;
}

/* ---- socket ---------------------------------------------------------------------------------------------------- */

#if defined(__APPLE__)

int
n2vcd_transport_socket(struct n2vcd_transport *t, const char *unix_path)
{
	(void)t;
	(void)unix_path;
	return -ENOSYS;             /* Darwin has no SOCK_SEQPACKET for AF_UNIX; the phone uses the relay */
}

#else

struct sockbuf {
	uint32_t handle;
	int fd;
	uint32_t bytes;
};

struct socktr {
	int fd;
	char dir[120];
	uint32_t next;
	struct sockbuf *bufs;
	uint32_t nbuf, capbuf;
	pthread_mutex_t lock;
};

static int
sock_buf_index(struct socktr *s, uint32_t handle)
{
	for (uint32_t i = 0; i < s->nbuf; i++) {
		if (s->bufs[i].handle == handle) { return (int)i; }
	}
	return -1;
}

static int
sock_send(void *ctx, const void *msg, uint32_t bytes)
{
	struct socktr *s = (struct socktr *)ctx;
	if (!s || !msg || bytes == 0 || bytes > RWVC_MSG_MAX) { return -EINVAL; }
	ssize_t n;
	do {
		n = send(s->fd, msg, bytes, MSG_NOSIGNAL);
	} while (n < 0 && errno == EINTR);
	if (n == (ssize_t)bytes) { return 0; }
	if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { return -EAGAIN; }
	return n < 0 ? -errno : -EIO;
}

static int
sock_recv(void *ctx, void *msg, uint32_t capacity, uint32_t timeout_ms)
{
	struct socktr *s = (struct socktr *)ctx;
	if (!s || !msg || capacity == 0) { return -EINVAL; }
	for (;;) {
		struct pollfd p;
		p.fd = s->fd;
		p.events = POLLIN;
		p.revents = 0;
		int pt = timeout_ms > 60000u ? 60000 : (int)timeout_ms;
		int pr = poll(&p, 1, pt);
		if (pr < 0) {
			if (errno == EINTR) { continue; }
			return -errno;
		}
		if (pr == 0) { return timeout_ms ? -ETIMEDOUT : -EAGAIN; }
		if (p.revents & POLLIN) {
			ssize_t n = recv(s->fd, msg, capacity, MSG_TRUNC);
			if (n < 0) {
				if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) { continue; }
				return -errno;
			}
			if (n == 0) { return -EPIPE; }               /* server closed the connection */
			if ((size_t)n > capacity) { return -EMSGSIZE; }
			return (int)n;
		}
		if (p.revents & (POLLHUP | POLLERR | POLLNVAL)) { return -EPIPE; }
	}
}

static int
sock_buf_alloc(void *ctx, uint32_t bytes, uint32_t *handle)
{
	struct socktr *s = (struct socktr *)ctx;
	if (!s || !handle || bytes == 0) { return -EINVAL; }
	pthread_mutex_lock(&s->lock);
	if (s->nbuf == s->capbuf) {
		uint32_t cap = s->capbuf ? s->capbuf * 2u : 8u;
		struct sockbuf *nb = (struct sockbuf *)realloc(s->bufs, (size_t)cap * sizeof(*nb));
		if (!nb) {
			pthread_mutex_unlock(&s->lock);
			return -ENOMEM;
		}
		s->bufs = nb;
		s->capbuf = cap;
	}
	for (uint32_t tries = 0; tries < 100000u; tries++) {
		uint32_t h = s->next + 1u;
		if (h == 0) { h = 1; }
		s->next = h;
		char path[160];
		int n = snprintf(path, sizeof(path), "%s/%u", s->dir, h);
		if (n <= 0 || (size_t)n >= sizeof(path)) {
			pthread_mutex_unlock(&s->lock);
			return -ENAMETOOLONG;
		}
		int fd;
		do {
			fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
		} while (fd < 0 && errno == EINTR);
		if (fd < 0) {
			if (errno == EEXIST) { continue; }        /* stale file from an earlier run: next handle */
			int e = -errno;
			pthread_mutex_unlock(&s->lock);
			return e;
		}
		int tr;
		do {
			tr = ftruncate(fd, (off_t)bytes);
		} while (tr < 0 && errno == EINTR);
		if (tr < 0) {
			int e = -errno;
			(void)close(fd);
			(void)unlink(path);
			pthread_mutex_unlock(&s->lock);
			return e;
		}
		s->bufs[s->nbuf].handle = h;
		s->bufs[s->nbuf].fd = fd;
		s->bufs[s->nbuf].bytes = bytes;
		s->nbuf++;
		*handle = h;
		pthread_mutex_unlock(&s->lock);
		return 0;
	}
	pthread_mutex_unlock(&s->lock);
	return -ENOSPC;
}

static int
sock_buf_rw(void *ctx, uint32_t handle, uint32_t offset, void *data, uint32_t bytes, int write)
{
	struct socktr *s = (struct socktr *)ctx;
	if (!s || !data || handle == 0) { return -EINVAL; }
	pthread_mutex_lock(&s->lock);
	int idx = sock_buf_index(s, handle);
	if (idx < 0) {
		pthread_mutex_unlock(&s->lock);
		return -ENOENT;
	}
	struct sockbuf *b = &s->bufs[idx];
	if (bytes > b->bytes || offset > b->bytes - bytes) {
		pthread_mutex_unlock(&s->lock);
		return -EINVAL;
	}
	uint32_t done = 0;
	int err = 0;
	while (done < bytes) {
		ssize_t n;
		if (write) {
			do {
				n = pwrite(b->fd, (const uint8_t *)data + done, bytes - done, (off_t)(offset + done));
			} while (n < 0 && errno == EINTR);
		} else {
			do {
				n = pread(b->fd, (uint8_t *)data + done, bytes - done, (off_t)(offset + done));
			} while (n < 0 && errno == EINTR);
		}
		if (n <= 0) {
			err = n < 0 ? -errno : -EIO;
			break;
		}
		done += (uint32_t)n;
	}
	pthread_mutex_unlock(&s->lock);
	return err;
}

static int
sock_buf_free(void *ctx, uint32_t handle)
{
	struct socktr *s = (struct socktr *)ctx;
	if (!s || handle == 0) { return -EINVAL; }
	pthread_mutex_lock(&s->lock);
	int idx = sock_buf_index(s, handle);
	if (idx < 0) {
		pthread_mutex_unlock(&s->lock);
		return -ENOENT;
	}
	char path[160];
	(void)snprintf(path, sizeof(path), "%s/%u", s->dir, handle);
	(void)close(s->bufs[idx].fd);
	(void)unlink(path);
	s->bufs[idx] = s->bufs[s->nbuf - 1u];
	s->nbuf--;
	pthread_mutex_unlock(&s->lock);
	return 0;
}

static void
sock_close(void *ctx)
{
	struct socktr *s = (struct socktr *)ctx;
	if (!s) { return; }
	pthread_mutex_lock(&s->lock);
	for (uint32_t i = 0; i < s->nbuf; i++) {
		char path[160];
		(void)snprintf(path, sizeof(path), "%s/%u", s->dir, s->bufs[i].handle);
		(void)close(s->bufs[i].fd);
		(void)unlink(path);
	}
	s->nbuf = 0;
	free(s->bufs);
	s->bufs = NULL;
	(void)rmdir(s->dir);              /* only succeeds when empty; a server may still hold files open */
	pthread_mutex_unlock(&s->lock);
	pthread_mutex_destroy(&s->lock);
	(void)close(s->fd);
	free(s);
}

int
n2vcd_transport_socket(struct n2vcd_transport *t, const char *unix_path)
{
	if (!t || !unix_path || !*unix_path) { return -EINVAL; }
	size_t len = strlen(unix_path);
	if (len >= sizeof(((struct sockaddr_un *)0)->sun_path) - 4u) { return -ENAMETOOLONG; }

	int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (fd < 0) { return -errno; }
	struct sockaddr_un sa;
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	memcpy(sa.sun_path, unix_path, len + 1u);
	int cr;
	for (;;) {
		cr = connect(fd, (struct sockaddr *)&sa, (socklen_t)sizeof(sa));
		if (cr == 0) { break; }
		if (errno == EINTR) { continue; }
		if (errno == EISCONN) { cr = 0; break; }        /* a retried connect that already completed */
		break;
	}
	if (cr < 0) {
		int e = -errno;
		(void)close(fd);
		return e;
	}

	struct socktr *s = (struct socktr *)calloc(1, sizeof(*s));
	if (!s) {
		(void)close(fd);
		return -ENOMEM;
	}
	int n = snprintf(s->dir, sizeof(s->dir), "%s.buf", unix_path);
	if (n <= 0 || (size_t)n >= sizeof(s->dir)) {
		(void)close(fd);
		free(s);
		return -ENAMETOOLONG;
	}
	if (mkdir(s->dir, 0700) != 0 && errno != EEXIST) {
		int e = -errno;
		(void)close(fd);
		free(s);
		return e;
	}
	if (pthread_mutex_init(&s->lock, NULL) != 0) {
		(void)close(fd);
		free(s);
		return -ENOMEM;
	}
	s->fd = fd;
	s->next = 0;

	memset(t, 0, sizeof(*t));
	t->ctx = s;
	t->send = sock_send;
	t->recv = sock_recv;
	t->buf_alloc = sock_buf_alloc;
	t->buf_rw = sock_buf_rw;
	t->buf_free = sock_buf_free;
	t->close = sock_close;
	return 0;
}

#endif /* !__APPLE__ */
