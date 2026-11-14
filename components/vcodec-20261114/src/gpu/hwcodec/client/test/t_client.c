/* SPDX-License-Identifier: MIT */
/*
 * t_client.c -- host test of package P03.
 *
 * 1. n2vc_hw_open_dev() and n2vcd_transport_relay() against a recording fake ioctl: every request number and every
 *    input struct is compared byte for byte with a literal built from include/redwood_vcodec.h; EINTR retries,
 *    error returns, and the exact ioctl per n2vc_hw method are checked.
 * 2. The socket transport: SOCK_SEQPACKET datagrams and the "<socket>.buf/<handle>" buffer files.
 * 3. n2vcd_server.h: accept/recv-from-any/send-to-client for both transports, close notices, oversized datagrams.
 * 4. n2vcd_client.[ch]: a scripted fake server (literal replies, including errors, short replies, a wrong sequence
 *    number, silence and an unsolicited seq-0 error), the NV12 packing with padded and unpadded strides, run-time
 *    settings, close, and two threads sharing one session.
 *
 * Every expected value is a literal from include/n2vcd_proto.h or include/redwood_vcodec.h, never a value produced by
 * the code under test.  Nothing here opens a real device.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "n2vc_hw.h"
#include "n2vc_ioctl.h"
#include "n2vcd_client.h"
#include "n2vcd_proto.h"
#include "n2vcd_server.h"
#include "redwood_vcodec.h"

static int g_checks;
static int g_fails;

#define CHECK(cond, ...)                                                                       \
	do {                                                                                       \
		g_checks++;                                                                            \
		if (!(cond)) {                                                                         \
			g_fails++;                                                                         \
			printf("FAIL %s:%d: ", __FILE__, __LINE__);                                         \
			printf(__VA_ARGS__);                                                               \
			printf("\n");                                                                      \
		}                                                                                      \
	} while (0)

static char g_dir[256];

static uint64_t
now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void
sleep_ms(uint32_t ms)
{
	struct timespec ts;
	ts.tv_sec = (time_t)(ms / 1000u);
	ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
	while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
}

static int
path_join(char *out, size_t cap, const char *name)
{
	int n = snprintf(out, cap, "%s/%s", g_dir, name);
	return n > 0 && (size_t)n < cap ? 0 : -1;
}

static int
read_exact(const char *path, void *buf, uint32_t bytes)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) { return -1; }
	uint32_t done = 0;
	while (done < bytes) {
		ssize_t n = read(fd, (uint8_t *)buf + done, bytes - done);
		if (n <= 0) { (void)close(fd); return -1; }
		done += (uint32_t)n;
	}
	(void)close(fd);
	return 0;
}

static int
write_at(const char *path, const void *data, uint32_t bytes)
{
	int fd = open(path, O_WRONLY);
	if (fd < 0) { return -1; }
	uint32_t done = 0;
	while (done < bytes) {
		ssize_t n = pwrite(fd, (const uint8_t *)data + done, bytes - done, (off_t)done);
		if (n <= 0) { (void)close(fd); return -1; }
		done += (uint32_t)n;
	}
	(void)close(fd);
	return 0;
}

/* =================================================================================================================
 * 1. Recording fake ioctl
 * ================================================================================================================= */

struct fakeio {
	int calls;
	unsigned long req;
	uint32_t n;
	uint8_t in[256];
	int last_fd;

	/* fault injection, each applies fail_left times (0 = never) */
	unsigned long intr_req;
	int intr_left;
	unsigned long fail_req;
	int fail_errno;
	int fail_left;

	/* replies */
	uint32_t abi;
	uint32_t irq_status;
	uint32_t read_value;
	uint32_t alloc_handle, alloc_pa;
	uint32_t info_pa, info_bytes;
	const void *rx_data;
	uint32_t rx_len;
	int rx_errno;
	uint32_t rx_client;
	struct rwvc_msg sent;
	int sent_valid;
};

static size_t
req_bytes(unsigned long req)
{
	return (size_t)((req >> 16) & 0x1fffu);      /* RWVC_IOC encodes sizeof(type) */
}

static int
fake_ioctl(void *ctx, int fd, unsigned long req, void *arg)
{
	struct fakeio *f = (struct fakeio *)ctx;
	f->calls++;
	f->last_fd = fd;
	f->req = req;
	f->n = (uint32_t)req_bytes(req);
	if (f->n > sizeof(f->in)) { f->n = sizeof(f->in); }
	if (arg && f->n) { memcpy(f->in, arg, f->n); }

	if (f->intr_left > 0 && req == f->intr_req) {
		f->intr_left--;
		errno = EINTR;
		return -1;
	}
	if (f->fail_left > 0 && req == f->fail_req) {
		f->fail_left--;
		errno = f->fail_errno;
		return -1;
	}

	switch (req) {
	case RWVC_INFO: {
		struct rwvc_info *i = (struct rwvc_info *)arg;
		memset(i, 0, sizeof(*i));
		i->abi_version = f->abi;
		i->irq_mode = 1;
		return 0;
	}
	case RWVC_POWER:
	case RWVC_IRQ_CLEAR:
	case RWVC_DMA_FREE:
		return 0;
	case RWVC_REGS: {
		struct rwvc_reg_batch *b = (struct rwvc_reg_batch *)arg;
		b->out_done = b->count;
		if (b->count && b->ops) {
			struct rwvc_reg_op *ops = (struct rwvc_reg_op *)(uintptr_t)b->ops;
			for (uint32_t i = 0; i < b->count; i++) {
				if (ops[i].op == RWVC_OP_READ) { ops[i].value = f->read_value; }
			}
		}
		return 0;
	}
	case RWVC_WAIT_IRQ: {
		struct rwvc_wait_irq *w = (struct rwvc_wait_irq *)arg;
		w->out_count = 1;
		w->out_status = f->irq_status;
		return 0;
	}
	case RWVC_DMA_ALLOC: {
		struct rwvc_dma_alloc *a = (struct rwvc_dma_alloc *)arg;
		a->out_handle = f->alloc_handle;
		a->out_pa = f->alloc_pa;
		return 0;
	}
	case RWVC_DMA_RW:
		return 0;
	case RWVC_DMA_INFO: {
		struct rwvc_dma_info *i = (struct rwvc_dma_info *)arg;
		i->bytes = f->info_bytes;
		i->out_pa = f->info_pa;
		return 0;
	}
	case RWVC_MSG_SEND: {
		struct rwvc_msg *m = (struct rwvc_msg *)arg;
		f->sent = *m;
		f->sent_valid = 1;
		return 0;
	}
	case RWVC_MSG_RECV: {
		struct rwvc_msg *m = (struct rwvc_msg *)arg;
		if (f->rx_errno) {
			errno = f->rx_errno;
			return -1;
		}
		if (!f->rx_data || f->rx_len > m->bytes) {
			errno = EINVAL;
			return -1;
		}
		if (f->rx_len) { memcpy((void *)(uintptr_t)m->user, f->rx_data, f->rx_len); }
		m->bytes = f->rx_len;
		m->client = f->rx_client;
		return 0;
	}
	default:
		errno = EINVAL;
		return -1;
	}
}

static int
make_file(const char *name, char *path, size_t cap)
{
	if (path_join(path, cap, name) != 0) { return -1; }
	int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) { return -1; }
	(void)close(fd);
	return 0;
}

static void
t_hw_dev(void)
{
	char path[300];
	CHECK(make_file("dev", path, sizeof(path)) == 0, "cannot create fake device file");
	struct fakeio f;
	memset(&f, 0, sizeof(f));
	f.abi = RWVC_ABI_VERSION;
	n2vc_dev_test_set_ioctl(fake_ioctl, &f);

	struct n2vc_hw hw;
	memset(&hw, 0xee, sizeof(hw));
	int r = n2vc_hw_open_dev(&hw, path);
	CHECK(r == 0, "n2vc_hw_open_dev: %d", r);
	struct rwvc_info zero_info;
	memset(&zero_info, 0, sizeof(zero_info));
	CHECK(f.calls == 1 && f.req == RWVC_INFO && f.n == sizeof(zero_info) &&
	      memcmp(f.in, &zero_info, sizeof(zero_info)) == 0,
	      "open must do exactly one RWVC_INFO with a zeroed struct (calls=%d req=0x%lx n=%u)", f.calls, f.req, f.n);

	/* power: exact struct, EINTR retried, error propagated */
	struct rwvc_power exp_power = { .block = RWVC_BLK_VENC, .on = 1 };
	f.calls = 0;
	r = hw.power(hw.ctx, RWVC_BLK_VENC, 1);
	CHECK(r == 0, "power: %d", r);
	CHECK(f.calls == 1 && f.req == RWVC_POWER && f.n == sizeof(exp_power) &&
	      memcmp(f.in, &exp_power, sizeof(exp_power)) == 0, "RWVC_POWER request bytes");
	exp_power.on = 0;
	f.calls = 0;
	r = hw.power(hw.ctx, RWVC_BLK_VENC, 0);
	CHECK(r == 0 && f.calls == 1 && memcmp(f.in, &exp_power, sizeof(exp_power)) == 0, "RWVC_POWER off bytes");

	f.intr_req = RWVC_POWER;
	f.intr_left = 1;
	f.calls = 0;
	r = hw.power(hw.ctx, RWVC_BLK_VENC_C1, 1);
	CHECK(r == 0 && f.calls == 2, "EINTR must be retried (calls=%d r=%d)", f.calls, r);
	f.fail_req = RWVC_POWER;
	f.fail_errno = EACCES;
	f.fail_left = 1;
	f.calls = 0;
	r = hw.power(hw.ctx, RWVC_BLK_VENC_C1, 1);
	CHECK(r == -EACCES && f.calls == 1, "ioctl error must be -errno (r=%d)", r);

	/* regs: pointer, count and the documented 1 s poll timeout */
	struct rwvc_reg_op ops[2];
	memset(ops, 0, sizeof(ops));
	ops[0].op = RWVC_OP_WRITE;
	ops[0].window = RWVC_WIN_VENC;
	ops[0].offset = 0x10;
	ops[0].value = 0x12345678u;
	ops[1].op = RWVC_OP_POLL;
	ops[1].window = RWVC_WIN_VENC;
	ops[1].offset = 0x1c;
	ops[1].value = 0x2;
	ops[1].mask = 0x2;
	unsigned done = 99;
	f.calls = 0;
	f.read_value = 0;
	r = hw.regs(hw.ctx, ops, 2, &done);
	struct rwvc_reg_batch exp_batch;
	memset(&exp_batch, 0, sizeof(exp_batch));
	exp_batch.ops = (uint64_t)(uintptr_t)ops;
	exp_batch.count = 2;
	exp_batch.out_done = 0;
	exp_batch.poll_timeout_us = 1000000u;
	CHECK(r == 0 && done == 2, "regs: r=%d done=%u", r, done);
	CHECK(f.calls == 1 && f.req == RWVC_REGS && f.n == sizeof(exp_batch) &&
	      memcmp(f.in, &exp_batch, sizeof(exp_batch)) == 0, "RWVC_REGS request bytes");

	/* wait_irq: status is returned, timeout forwarded */
	struct rwvc_wait_irq exp_irq;
	memset(&exp_irq, 0, sizeof(exp_irq));
	exp_irq.irq = RWVC_IRQ_VENC;
	exp_irq.timeout_ms = 234;
	uint32_t status = 0;
	f.irq_status = 0xdeadbeefu;
	f.calls = 0;
	r = hw.wait_irq(hw.ctx, RWVC_IRQ_VENC, 234, &status);
	CHECK(r == 0 && status == 0xdeadbeefu, "wait_irq: r=%d status=0x%x", r, status);
	CHECK(f.calls == 1 && f.req == RWVC_WAIT_IRQ && memcmp(f.in, &exp_irq, sizeof(exp_irq)) == 0,
	      "RWVC_WAIT_IRQ request bytes");

	struct rwvc_irq_clear exp_clear = { .irq = RWVC_IRQ_VDEC, .reserved = 0 };
	f.calls = 0;
	r = hw.irq_clear(hw.ctx, RWVC_IRQ_VDEC);
	CHECK(r == 0 && f.calls == 1 && f.req == RWVC_IRQ_CLEAR &&
	      memcmp(f.in, &exp_clear, sizeof(exp_clear)) == 0, "RWVC_IRQ_CLEAR request bytes");

	struct rwvc_dma_alloc exp_alloc;
	memset(&exp_alloc, 0, sizeof(exp_alloc));
	exp_alloc.bytes = 65536;
	exp_alloc.flags = RWVC_DMA_SHARED;
	f.alloc_handle = 5;
	f.alloc_pa = 0xda800000u;
	uint32_t handle = 0, pa = 0;
	f.calls = 0;
	r = hw.dma_alloc(hw.ctx, 65536, RWVC_DMA_SHARED, &handle, &pa);
	CHECK(r == 0 && handle == 5 && pa == 0xda800000u, "dma_alloc: r=%d h=%u pa=0x%x", r, handle, pa);
	CHECK(f.calls == 1 && f.req == RWVC_DMA_ALLOC && memcmp(f.in, &exp_alloc, sizeof(exp_alloc)) == 0,
	      "RWVC_DMA_ALLOC request bytes");

	uint8_t scratch[32];
	memset(scratch, 0x5a, sizeof(scratch));
	struct rwvc_dma_rw exp_rw;
	memset(&exp_rw, 0, sizeof(exp_rw));
	exp_rw.handle = 5;
	exp_rw.offset = 16;
	exp_rw.user = (uint64_t)(uintptr_t)scratch;
	exp_rw.bytes = 32;
	exp_rw.write = 1;
	f.calls = 0;
	r = hw.dma_rw(hw.ctx, 5, 16, scratch, 32, 1);
	CHECK(r == 0 && f.calls == 1 && f.req == RWVC_DMA_RW &&
	      memcmp(f.in, &exp_rw, sizeof(exp_rw)) == 0, "RWVC_DMA_RW write request bytes");
	exp_rw.write = 0;
	f.calls = 0;
	r = hw.dma_rw(hw.ctx, 5, 16, scratch, 32, 0);
	CHECK(r == 0 && f.calls == 1 && memcmp(f.in, &exp_rw, sizeof(exp_rw)) == 0, "RWVC_DMA_RW read request bytes");

	struct rwvc_dma_free exp_free = { .handle = 5, .reserved = 0 };
	f.calls = 0;
	r = hw.dma_free(hw.ctx, 5);
	CHECK(r == 0 && f.calls == 1 && f.req == RWVC_DMA_FREE &&
	      memcmp(f.in, &exp_free, sizeof(exp_free)) == 0, "RWVC_DMA_FREE request bytes");

	int devfd = f.last_fd;
	hw.close(hw.ctx);
	CHECK(fcntl(devfd, F_GETFD) < 0, "close must close the device fd");

	/* ABI mismatch is refused and the fd is closed. */
	f.abi = RWVC_ABI_VERSION + 1;
	f.calls = 0;
	memset(&hw, 0, sizeof(hw));
	r = n2vc_hw_open_dev(&hw, path);
	CHECK(r == -EPROTO && hw.ctx == NULL, "ABI mismatch must be -EPROTO with no context (r=%d)", r);
	CHECK(fcntl(f.last_fd, F_GETFD) < 0, "ABI mismatch must close the fd");
	f.abi = RWVC_ABI_VERSION;
}

/* =================================================================================================================
 * 2. Relay transport
 * ================================================================================================================= */

static void
t_relay(void)
{
	char path[300];
	CHECK(make_file("relay", path, sizeof(path)) == 0, "cannot create fake relay file");
	struct fakeio f;
	memset(&f, 0, sizeof(f));
	f.abi = RWVC_ABI_VERSION;
	n2vcd_relay_test_set_ioctl(fake_ioctl, &f);

	struct n2vcd_transport t;
	memset(&t, 0, sizeof(t));
	int r = n2vcd_transport_relay(&t, path);
	CHECK(r == 0 && t.send && t.recv && t.buf_alloc && t.buf_rw && t.buf_free && t.close,
	      "transport_relay: r=%d", r);
	CHECK(f.calls == 1 && f.req == RWVC_INFO, "relay open must probe RWVC_INFO (calls=%d req=0x%lx)",
	      f.calls, f.req);

	f.abi = RWVC_ABI_VERSION + 1;
	struct n2vcd_transport bad;
	memset(&bad, 0, sizeof(bad));
	r = n2vcd_transport_relay(&bad, path);
	CHECK(r == -EPROTO && fcntl(f.last_fd, F_GETFD) < 0, "relay ABI mismatch: r=%d", r);
	f.abi = RWVC_ABI_VERSION;

	uint8_t msg[24];
	memset(msg, 0x11, sizeof(msg));
	struct rwvc_msg exp_msg;
	memset(&exp_msg, 0, sizeof(exp_msg));
	exp_msg.user = (uint64_t)(uintptr_t)msg;
	exp_msg.bytes = sizeof(msg);
	f.calls = 0;
	r = t.send(t.ctx, msg, sizeof(msg));
	CHECK(r == 0 && f.calls == 1 && f.req == RWVC_MSG_SEND && f.n == sizeof(exp_msg) &&
	      memcmp(f.in, &exp_msg, sizeof(exp_msg)) == 0, "RWVC_MSG_SEND request bytes");

	f.fail_req = RWVC_MSG_SEND;
	f.fail_errno = EAGAIN;
	f.fail_left = 1;
	r = t.send(t.ctx, msg, sizeof(msg));
	CHECK(r == -EAGAIN, "SEND EAGAIN must be returned as -EAGAIN (r=%d)", r);
	r = t.send(t.ctx, msg, sizeof(msg));
	CHECK(r == 0, "SEND after EAGAIN: %d", r);

	uint8_t reply[24];
	memset(reply, 0x22, sizeof(reply));
	uint8_t got[64];
	memset(got, 0, sizeof(got));
	f.rx_data = reply;
	f.rx_len = sizeof(reply);
	f.rx_client = 0;
	struct rwvc_msg exp_recv;
	memset(&exp_recv, 0, sizeof(exp_recv));
	exp_recv.user = (uint64_t)(uintptr_t)got;
	exp_recv.bytes = sizeof(got);
	exp_recv.timeout_ms = 123;
	f.calls = 0;
	r = t.recv(t.ctx, got, sizeof(got), 123);
	CHECK(r == (int)sizeof(reply) && memcmp(got, reply, sizeof(reply)) == 0, "recv: r=%d", r);
	CHECK(f.calls == 1 && f.req == RWVC_MSG_RECV && f.n == sizeof(exp_recv) &&
	      memcmp(f.in, &exp_recv, sizeof(exp_recv)) == 0, "RWVC_MSG_RECV request bytes");

	f.rx_errno = ETIMEDOUT;
	r = t.recv(t.ctx, got, sizeof(got), 5);
	CHECK(r == -ETIMEDOUT, "recv timeout: %d", r);
	f.rx_errno = 0;

	f.intr_req = RWVC_MSG_RECV;
	f.intr_left = 1;
	f.calls = 0;
	r = t.recv(t.ctx, got, sizeof(got), 5);
	CHECK(r == (int)sizeof(reply) && f.calls == 2, "recv EINTR retried (calls=%d r=%d)", f.calls, r);

	struct rwvc_dma_alloc exp_alloc;
	memset(&exp_alloc, 0, sizeof(exp_alloc));
	exp_alloc.bytes = 8192;
	exp_alloc.flags = RWVC_DMA_SHARED;
	f.alloc_handle = 9;
	f.alloc_pa = 0xda900000u;
	uint32_t handle = 0;
	f.calls = 0;
	r = t.buf_alloc(t.ctx, 8192, &handle);
	CHECK(r == 0 && handle == 9, "buf_alloc: r=%d h=%u", r, handle);
	CHECK(f.calls == 1 && f.req == RWVC_DMA_ALLOC && memcmp(f.in, &exp_alloc, sizeof(exp_alloc)) == 0,
	      "buf_alloc must set RWVC_DMA_SHARED");

	struct rwvc_dma_rw exp_rw;
	memset(&exp_rw, 0, sizeof(exp_rw));
	exp_rw.handle = 9;
	exp_rw.offset = 64;
	exp_rw.user = (uint64_t)(uintptr_t)got;
	exp_rw.bytes = 128;
	exp_rw.write = 0;
	f.calls = 0;
	r = t.buf_rw(t.ctx, 9, 64, got, 128, 0);
	CHECK(r == 0 && f.calls == 1 && f.req == RWVC_DMA_RW &&
	      memcmp(f.in, &exp_rw, sizeof(exp_rw)) == 0, "buf_rw request bytes");

	struct rwvc_dma_free exp_free = { .handle = 9, .reserved = 0 };
	f.calls = 0;
	r = t.buf_free(t.ctx, 9);
	CHECK(r == 0 && f.calls == 1 && f.req == RWVC_DMA_FREE &&
	      memcmp(f.in, &exp_free, sizeof(exp_free)) == 0, "buf_free request bytes");

	int fd = f.last_fd;
	t.close(t.ctx);
	CHECK(fcntl(fd, F_GETFD) < 0, "relay close must close the fd");

	CHECK(n2vcd_transport_relay(NULL, path) == -EINVAL, "transport_relay(NULL)");
}

/* =================================================================================================================
 * 3. Socket transport and server helpers
 * ================================================================================================================= */

struct sockpeer {
	char path[300];
	_Atomic int ready;
	const void *expect;
	uint32_t expect_len;
	const void *reply;
	uint32_t reply_len;
	int expect_bytes;
	int fail;
	char why[200];
};

static int
listen_seqpacket(const char *path)
{
	int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (fd < 0) { return -1; }
	struct sockaddr_un sa;
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	memcpy(sa.sun_path, path, strlen(path) + 1u);
	(void)unlink(path);
	if (bind(fd, (struct sockaddr *)&sa, (socklen_t)sizeof(sa)) < 0) { (void)close(fd); return -1; }
	if (listen(fd, 4) < 0) { (void)close(fd); return -1; }
	return fd;
}

static int
poll_read(int fd, int timeout_ms)
{
	struct pollfd p;
	p.fd = fd;
	p.events = POLLIN;
	p.revents = 0;
	int r;
	do {
		r = poll(&p, 1, timeout_ms);
	} while (r < 0 && errno == EINTR);
	if (r <= 0) { return r; }
	return (p.revents & POLLIN) ? 1 : -1;
}

static void *
sockpeer_thread(void *arg)
{
	struct sockpeer *p = (struct sockpeer *)arg;
	int lfd = listen_seqpacket(p->path);
	atomic_store(&p->ready, lfd < 0 ? -1 : 1);
	if (lfd < 0) { p->fail = 1; (void)snprintf(p->why, sizeof(p->why), "listen failed"); return NULL; }
	if (poll_read(lfd, 3000) != 1) { p->fail = 1; (void)snprintf(p->why, sizeof(p->why), "no connect"); (void)close(lfd); return NULL; }
	int fd = accept(lfd, NULL, NULL);
	(void)close(lfd);
	if (fd < 0) { p->fail = 1; (void)snprintf(p->why, sizeof(p->why), "accept failed"); return NULL; }
	uint8_t buf[2048];
	if (poll_read(fd, 3000) != 1) { p->fail = 1; (void)snprintf(p->why, sizeof(p->why), "no request"); }
	else {
		ssize_t n = recv(fd, buf, sizeof(buf), 0);
		if (n != (ssize_t)p->expect_len || memcmp(buf, p->expect, p->expect_len) != 0) {
			p->fail = 1;
			(void)snprintf(p->why, sizeof(p->why), "request mismatch (n=%zd)", n);
		} else if (p->reply && send(fd, p->reply, p->reply_len, 0) != (ssize_t)p->reply_len) {
			p->fail = 1;
			(void)snprintf(p->why, sizeof(p->why), "reply send failed");
		}
	}
	(void)close(fd);
	return NULL;
}

static void
t_socket_transport(void)
{
	char sock[300];
	CHECK(path_join(sock, sizeof(sock), "s1") == 0, "path");
	struct sockpeer peer;
	memset(&peer, 0, sizeof(peer));
	(void)snprintf(peer.path, sizeof(peer.path), "%s", sock);
	static const char question[] = "ping-1234";
	static const char answer[] = "pong-5678";
	peer.expect = question;
	peer.expect_len = sizeof(question);
	peer.reply = answer;
	peer.reply_len = sizeof(answer);
	pthread_t th;
	CHECK(pthread_create(&th, NULL, sockpeer_thread, &peer) == 0, "thread");
	for (int i = 0; i < 3000 && atomic_load(&peer.ready) == 0; i++) { sleep_ms(1); }
	CHECK(atomic_load(&peer.ready) == 1, "socket peer did not start");

	struct n2vcd_transport t;
	memset(&t, 0, sizeof(t));
	int r = n2vcd_transport_socket(&t, sock);
	CHECK(r == 0, "transport_socket: %d", r);
	if (r == 0) {
		r = t.send(t.ctx, question, sizeof(question));
		CHECK(r == 0, "socket send: %d", r);
		char buf[64];
		memset(buf, 0, sizeof(buf));
		r = t.recv(t.ctx, buf, sizeof(buf), 2000);
		CHECK(r == (int)sizeof(answer) && memcmp(buf, answer, sizeof(answer)) == 0, "socket recv: %d", r);

		uint32_t h = 0;
		r = t.buf_alloc(t.ctx, 4096, &h);
		CHECK(r == 0 && h != 0, "socket buf_alloc: r=%d h=%u", r, h);
		char fpath[400];
		(void)snprintf(fpath, sizeof(fpath), "%s.buf/%u", sock, h);
		struct stat st;
		CHECK(stat(fpath, &st) == 0 && st.st_size == 4096, "socket buffer file size");
		uint8_t pat[256];
		for (size_t i = 0; i < sizeof(pat); i++) { pat[i] = (uint8_t)(i * 7u + 3u); }
		r = t.buf_rw(t.ctx, h, 0, pat, sizeof(pat), 1);
		CHECK(r == 0, "socket buf_rw write: %d", r);
		uint8_t back[256];
		memset(back, 0, sizeof(back));
		CHECK(read_exact(fpath, back, sizeof(back)) == 0 && memcmp(back, pat, sizeof(pat)) == 0,
		      "buffer file holds what buf_rw wrote");
		memset(back, 0, sizeof(back));
		r = t.buf_rw(t.ctx, h, 0, back, sizeof(back), 0);
		CHECK(r == 0 && memcmp(back, pat, sizeof(pat)) == 0, "socket buf_rw read: %d", r);
		CHECK(t.buf_rw(t.ctx, h, 4000, back, 256, 0) == -EINVAL, "buf_rw past the end must be EINVAL");
		r = t.buf_free(t.ctx, h);
		CHECK(r == 0 && access(fpath, F_OK) != 0, "buf_free unlinks the file");
		t.close(t.ctx);
	}
	pthread_join(th, NULL);
	CHECK(peer.fail == 0, "socket peer: %s", peer.why);
	(void)unlink(sock);
	char bd[320];
	(void)snprintf(bd, sizeof(bd), "%s.buf", peer.path);
	(void)rmdir(bd);                                        /* best effort; the transport removed it */
}

static int
raw_connect(const char *path)
{
	int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (fd < 0) { return -1; }
	struct sockaddr_un sa;
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	memcpy(sa.sun_path, path, strlen(path) + 1u);
	if (connect(fd, (struct sockaddr *)&sa, (socklen_t)sizeof(sa)) < 0) { (void)close(fd); return -1; }
	return fd;
}

static void
t_server_helpers(void)
{
	char sock[300];
	CHECK(path_join(sock, sizeof(sock), "s2") == 0, "path");
	char err[256];
	struct n2vcd_srv *s = n2vcd_srv_open_socket(sock, err, sizeof(err));
	CHECK(s != NULL, "server open_socket: %s", err);
	if (!s) { return; }
	int c1 = raw_connect(sock);
	int c2 = raw_connect(sock);
	CHECK(c1 >= 0 && c2 >= 0, "raw connect c1=%d c2=%d", c1, c2);

	CHECK(send(c1, "one", 3, 0) == 3, "send one");
	CHECK(send(c2, "two", 3, 0) == 3, "send two");
	char buf[64];
	uint32_t from1 = 0, from2 = 0;
	uint32_t id_one = 0, id_two = 0;
	int r = 0;
	int got_one = 0, got_two = 0;
	for (int i = 0; i < 20 && !(got_one && got_two); i++) {
		/* The first call after a connect only accepts (returns -EAGAIN); a later one reads. */
		r = n2vcd_srv_recv(s, buf, sizeof(buf), &from1, 500);
		if (r == -EAGAIN) { continue; }
		if (r == 3 && !got_one && memcmp(buf, "one", 3) == 0) { id_one = from1; got_one = 1; continue; }
		if (r == 3 && !got_two && memcmp(buf, "two", 3) == 0) { id_two = from1; got_two = 1; continue; }
		CHECK(0, "unexpected datagram r=%d from=%u", r, from1);
		break;
	}
	CHECK(got_one && got_two && id_one != 0 && id_two != 0 && id_one != id_two,
	      "recv-from-any: one=%d/%u two=%d/%u", got_one, id_one, got_two, id_two);

	CHECK(n2vcd_srv_send(s, id_one, "R1", 3) == 0, "send to client 1");
	CHECK(poll_read(c1, 1000) == 1 && recv(c1, buf, sizeof(buf), 0) == 3 && memcmp(buf, "R1", 3) == 0,
	      "client 1 got its reply");
	CHECK(n2vcd_srv_send(s, id_two, "R2", 3) == 0, "send to client 2");
	CHECK(poll_read(c2, 1000) == 1 && recv(c2, buf, sizeof(buf), 0) == 3 && memcmp(buf, "R2", 3) == 0,
	      "client 2 got its reply");
	CHECK(n2vcd_srv_send(s, 999, "x", 1) == -EPIPE, "send to an unknown id must be -EPIPE");

	CHECK(close(c1) == 0, "close c1");
	r = n2vcd_srv_recv(s, buf, sizeof(buf), &from1, 1000);
	CHECK(r == 0 && from1 == id_one, "close notice c1: r=%d from=%u (c1 id %u)", r, from1, id_one);

	/* oversized datagram: consumed, reported, client kept */
	uint8_t big[1500];
	memset(big, 0x77, sizeof(big));
	CHECK(send(c2, big, sizeof(big), 0) == (ssize_t)sizeof(big), "send oversized");
	r = n2vcd_srv_recv(s, buf, sizeof(buf), &from2, 1000);
	CHECK(r == -EMSGSIZE && from2 == id_two, "oversized datagram: r=%d from=%u", r, from2);
	CHECK(send(c2, "ok", 2, 0) == 2, "send after oversize");
	r = n2vcd_srv_recv(s, buf, sizeof(buf), &from2, 1000);
	CHECK(r == 2 && memcmp(buf, "ok", 2) == 0 && from2 == id_two, "client kept after oversize: r=%d", r);

	r = n2vcd_srv_recv(s, buf, sizeof(buf), &from2, 20);
	CHECK(r == -EAGAIN, "empty recv must be -EAGAIN (r=%d)", r);

	(void)close(c2);
	r = n2vcd_srv_recv(s, buf, sizeof(buf), &from2, 1000);
	CHECK(r == 0 && from2 == id_two, "close notice c2: r=%d from=%u", r, from2);
	n2vcd_srv_close(s);
	CHECK(access(sock, F_OK) != 0, "server close unlinks the socket");
	char bd[320];
	(void)snprintf(bd, sizeof(bd), "%s.buf", sock);
	(void)rmdir(bd);

	/* relay server helper with the recording ioctl */
	char dev[300];
	CHECK(make_file("relay2", dev, sizeof(dev)) == 0, "fake relay file");
	struct fakeio f;
	memset(&f, 0, sizeof(f));
	f.abi = RWVC_ABI_VERSION;
	n2vcd_srv_test_set_ioctl(fake_ioctl, &f);
	struct n2vcd_srv *sr = n2vcd_srv_open_relay(dev, err, sizeof(err));
	CHECK(sr != NULL, "server open_relay: %s", err);
	if (!sr) { return; }
	uint8_t dat[24];
	memset(dat, 0x33, sizeof(dat));
	f.rx_data = dat;
	f.rx_len = sizeof(dat);
	f.rx_client = 7;
	r = n2vcd_srv_recv(sr, buf, sizeof(buf), &from1, 50);
	CHECK(r == (int)sizeof(dat) && from1 == 7 && memcmp(buf, dat, sizeof(dat)) == 0,
	      "relay recv: r=%d from=%u", r, from1);

	char hello[5] = { 'h', 'e', 'l', 'l', 'o' };
	f.sent_valid = 0;
	r = n2vcd_srv_send(sr, 7, hello, sizeof(hello));
	struct rwvc_msg exp_msg;
	memset(&exp_msg, 0, sizeof(exp_msg));
	exp_msg.user = (uint64_t)(uintptr_t)hello;
	exp_msg.bytes = 5;
	exp_msg.client = 7;
	CHECK(r == 0 && f.sent_valid && f.req == RWVC_MSG_SEND &&
	      memcmp(&f.sent, &exp_msg, sizeof(exp_msg)) == 0, "relay server send bytes");

	uint32_t pa = 0, bytes = 0;
	struct rwvc_dma_info exp_info;
	memset(&exp_info, 0, sizeof(exp_info));
	exp_info.handle = 42;
	f.info_pa = 0xdaa00000u;
	f.info_bytes = 8192;
	f.calls = 0;
	r = n2vcd_srv_dma_info(sr, 42, &pa, &bytes);
	CHECK(r == 0 && pa == 0xdaa00000u && bytes == 8192, "dma_info: r=%d pa=0x%x bytes=%u", r, pa, bytes);
	CHECK(f.calls == 1 && f.req == RWVC_DMA_INFO && f.n == sizeof(exp_info) &&
	      memcmp(f.in, &exp_info, sizeof(exp_info)) == 0, "RWVC_DMA_INFO request bytes");

	f.rx_len = 0;
	r = n2vcd_srv_recv(sr, buf, sizeof(buf), &from1, 50);
	CHECK(r == 0 && from1 == 7, "relay close notice: r=%d from=%u", r, from1);
	n2vcd_srv_close(sr);
}

/* =================================================================================================================
 * 4. Client library against a scripted fake server
 * ================================================================================================================= */

enum scenario {
	SC_HAPPY = 1,
	SC_HELLO_ERROR,
	SC_HELLO_BAD_VERSION,
	SC_HELLO_SHORT,
	SC_OPEN_BUSY,
	SC_FRAME_GUEST,
	SC_FRAME_WRONG_SEQ,
	SC_FRAME_WRONG_SESSION,
	SC_FRAME_SHORT,
	SC_SILENT,
	SC_FRAME_SEQ0,
	SC_FRAME_OVERSIZE,
	SC_CAP_LOCAL,
	SC_THREADS
};

struct fakesrv {
	char path[300];
	char bufdir[320];
	_Atomic int ready;
	int scenario;
	pthread_t th;
	int fail;
	char why[240];
};

static void
fs_fail(struct fakesrv *f, const char *fmt, ...)
{
	if (f->fail) { return; }
	f->fail = 1;
	va_list ap;
	va_start(ap, fmt);
	(void)vsnprintf(f->why, sizeof(f->why), fmt, ap);
	va_end(ap);
}

static int
fs_recv(struct fakesrv *f, int fd, void *buf, uint32_t cap, int timeout_ms, const char *what)
{
	if (poll_read(fd, timeout_ms) != 1) {
		fs_fail(f, "%s: no datagram", what);
		return -1;
	}
	ssize_t n = recv(fd, buf, cap, 0);
	if (n <= 0) {
		fs_fail(f, "%s: recv %zd", what, n);
		return -1;
	}
	return (int)n;
}

static int
fs_send(struct fakesrv *f, int fd, const void *msg, uint32_t bytes, const char *what)
{
	if (send(fd, msg, bytes, 0) != (ssize_t)bytes) {
		fs_fail(f, "%s: send failed", what);
		return -1;
	}
	return 0;
}

static int
fs_accept(struct fakesrv *f)
{
	int lfd = listen_seqpacket(f->path);
	atomic_store(&f->ready, lfd < 0 ? -1 : 1);
	if (lfd < 0) { fs_fail(f, "listen"); return -1; }
	if (poll_read(lfd, 3000) != 1) { fs_fail(f, "connect timeout"); (void)close(lfd); return -1; }
	int fd = accept(lfd, NULL, NULL);
	(void)close(lfd);
	if (fd < 0) { fs_fail(f, "accept"); }
	return fd;
}

static void
fs_hello(struct fakesrv *f, int fd)
{
	uint8_t buf[256];
	int n = fs_recv(f, fd, buf, sizeof(buf), 2000, "HELLO");
	if (n < 0) { return; }
	struct n2vcd_hello exp;
	memset(&exp, 0, sizeof(exp));
	exp.h.magic = N2VCD_MAGIC;
	exp.h.type = N2VCD_HELLO;
	exp.h.seq = 1;
	exp.h.session = 0;
	exp.version = N2VCD_VERSION;
	if (n != (int)sizeof(exp) || memcmp(buf, &exp, sizeof(exp)) != 0) {
		fs_fail(f, "HELLO bytes (n=%d)", n);
		return;
	}
	struct n2vcd_hello_ok ok;
	memset(&ok, 0, sizeof(ok));
	ok.h.magic = N2VCD_MAGIC;
	ok.h.type = N2VCD_HELLO_OK;
	ok.h.seq = 1;
	ok.h.session = 0;
	ok.version = N2VCD_VERSION;
	ok.enc_codecs = 3;
	ok.dec_codecs = 0;
	ok.max_width = 4096;
	ok.max_height = 2160;
	(void)fs_send(f, fd, &ok, (uint32_t)sizeof(ok), "HELLO_OK");
}

static void
fs_open(struct fakesrv *f, int fd, uint32_t session, uint32_t in_bytes, uint32_t out_bytes)
{
	uint8_t buf[256];
	int n = fs_recv(f, fd, buf, sizeof(buf), 2000, "OPEN");
	if (n < 0) { return; }
	struct n2vcd_open exp;
	memset(&exp, 0, sizeof(exp));
	exp.h.magic = N2VCD_MAGIC;
	exp.h.type = N2VCD_OPEN;
	exp.h.seq = 2;
	exp.h.session = 0;
	exp.codec = N2VCD_CODEC_H264;
	exp.encoder = 1;
	exp.width = 650;
	exp.height = 482;
	exp.fps_num = 30;
	exp.fps_den = 1;
	exp.bitrate = 1000000;
	exp.gop = 0;
	exp.profile = 0;
	exp.level = 0;
	exp.flags = 0;
	if (n != (int)sizeof(exp) || memcmp(buf, &exp, sizeof(exp)) != 0) {
		fs_fail(f, "OPEN bytes (n=%d)", n);
		return;
	}
	struct n2vcd_open_ok ok;
	memset(&ok, 0, sizeof(ok));
	ok.h.magic = N2VCD_MAGIC;
	ok.h.type = N2VCD_OPEN_OK;
	ok.h.seq = 2;
	ok.h.session = session;
	ok.status = N2VCD_E_OK;
	ok.in_buf_bytes = in_bytes;
	ok.out_buf_bytes = out_bytes;
	(void)fs_send(f, fd, &ok, (uint32_t)sizeof(ok), "OPEN_OK");
}

static int
fs_read_frame(struct fakesrv *f, int fd, struct n2vcd_enc_frame *req, uint32_t seq, uint32_t session,
              uint64_t pts, uint32_t force_key)
{
	uint8_t buf[256];
	int n = fs_recv(f, fd, buf, sizeof(buf), 2000, "ENC_FRAME");
	if (n < 0) { return -1; }
	if (n != (int)sizeof(*req)) {
		fs_fail(f, "ENC_FRAME size %d", n);
		return -1;
	}
	memcpy(req, buf, sizeof(*req));
	if (req->h.magic != N2VCD_MAGIC || req->h.type != N2VCD_ENC_FRAME || req->h.seq != seq ||
	    req->h.session != session || req->stride != 656u || req->out_capacity != 200000u ||
	    req->pts != pts || req->force_key != force_key || req->in_handle == 0 || req->out_handle == 0 ||
	    req->in_handle == req->out_handle || req->reserved != 0) {
		fs_fail(f, "ENC_FRAME fields seq=%u session=%u stride=%u cap=%u pts=%llu fk=%u", req->h.seq,
		        req->h.session, req->stride, req->out_capacity, (unsigned long long)req->pts,
		        req->force_key);
		return -1;
	}
	return 0;
}

static uint8_t
pat_y(uint32_t row, uint32_t col, uint32_t frame)
{
	return (uint8_t)(row * 7u + col * 3u + frame * 11u + 1u);
}

static uint8_t
pat_c(uint32_t row, uint32_t col, uint32_t frame)
{
	return (uint8_t)(row * 5u + col * 9u + frame * 13u + 3u);
}

/* Check the NV12 picture the client copied into the shared buffer.  650x482 with stride 656: the 6 padding bytes
 * of every row, the last 14 rows (482..495) and everything past the chroma plane must be zero. */
static void
fs_verify_input(struct fakesrv *f, uint32_t handle, uint32_t frame)
{
	uint32_t in_bytes = 488064;
	uint8_t *buf = (uint8_t *)malloc(in_bytes);
	if (!buf) { fs_fail(f, "malloc"); return; }
	char path[400];
	(void)snprintf(path, sizeof(path), "%s/%u", f->bufdir, handle);
	if (read_exact(path, buf, in_bytes) != 0) {
		fs_fail(f, "read input buffer %s", path);
		free(buf);
		return;
	}
	for (uint32_t row = 0; row < 482 && !f->fail; row++) {
		for (uint32_t col = 0; col < 650; col++) {
			if (buf[row * 656u + col] != pat_y(row, col, frame)) {
				fs_fail(f, "luma %u,%u = %02x, want %02x (frame %u)", row, col,
				        buf[row * 656u + col], pat_y(row, col, frame), frame);
				break;
			}
		}
		for (uint32_t col = 650; col < 656; col++) {
			if (buf[row * 656u + col] != 0) {
				fs_fail(f, "luma padding %u,%u = %02x", row, col, buf[row * 656u + col]);
				break;
			}
		}
	}
	uint32_t cbase = 656u * 496u;
	for (uint32_t row = 0; row < 241 && !f->fail; row++) {
		for (uint32_t col = 0; col < 650; col++) {
			if (buf[cbase + row * 656u + col] != pat_c(row, col, frame)) {
				fs_fail(f, "chroma %u,%u = %02x, want %02x (frame %u)", row, col,
				        buf[cbase + row * 656u + col], pat_c(row, col, frame), frame);
				break;
			}
		}
		for (uint32_t col = 650; col < 656; col++) {
			if (buf[cbase + row * 656u + col] != 0) {
				fs_fail(f, "chroma padding %u,%u = %02x", row, col, buf[cbase + row * 656u + col]);
				break;
			}
		}
	}
	for (uint32_t i = cbase + 241u * 656u; i < in_bytes && !f->fail; i++) {
		if (buf[i] != 0) { fs_fail(f, "tail byte %u = %02x", i, buf[i]); break; }
	}
	free(buf);
}

static void
fs_write_out(struct fakesrv *f, uint32_t handle, const void *data, uint32_t bytes)
{
	char path[400];
	(void)snprintf(path, sizeof(path), "%s/%u", f->bufdir, handle);
	if (write_at(path, data, bytes) != 0) { fs_fail(f, "write output buffer %s", path); }
}

static void
fs_frame_ok(struct fakesrv *f, int fd, uint32_t seq, uint32_t session, uint32_t out_bytes, uint32_t key,
            uint32_t hw_us, uint64_t pts, uint32_t header_bytes)
{
	struct n2vcd_enc_frame_ok ok;
	memset(&ok, 0, sizeof(ok));
	ok.h.magic = N2VCD_MAGIC;
	ok.h.type = N2VCD_ENC_FRAME_OK;
	ok.h.seq = seq;
	ok.h.session = session;
	ok.status = N2VCD_E_OK;
	ok.out_bytes = out_bytes;
	ok.key_frame = key;
	ok.hw_us = hw_us;
	ok.pts = pts;
	ok.header_bytes = header_bytes;
	(void)fs_send(f, fd, &ok, (uint32_t)sizeof(ok), "ENC_FRAME_OK");
}

static void
fs_simple(struct fakesrv *f, int fd, uint32_t type, uint32_t seq, uint32_t session, uint32_t status,
          uint32_t detail)
{
	struct n2vcd_simple s;
	memset(&s, 0, sizeof(s));
	s.h.magic = N2VCD_MAGIC;
	s.h.type = type;
	s.h.seq = seq;
	s.h.session = session;
	s.status = status;
	s.detail = detail;
	(void)fs_send(f, fd, &s, (uint32_t)sizeof(s), "simple");
}

static void
sc_happy(struct fakesrv *f, int fd)
{
	fs_hello(f, fd);
	if (f->fail) { return; }
	fs_open(f, fd, 42, 488064, 200000);
	if (f->fail) { return; }

	static const uint8_t au0[12] = { 0, 0, 0, 1, 0x67, 0x64, 0x00, 0x1f, 0x68, 0xee, 0x3c, 0x80 };
	static const uint8_t au1[5] = { 0, 0, 0, 1, 0x41 };

	struct n2vcd_enc_frame req;
	memset(&req, 0, sizeof(req));
	if (fs_read_frame(f, fd, &req, 3, 42, 1000, 1) != 0) { return; }
	fs_verify_input(f, req.in_handle, 0);
	if (f->fail) { return; }
	fs_write_out(f, req.out_handle, au0, sizeof(au0));
	fs_frame_ok(f, fd, 3, 42, sizeof(au0), 1, 777, 1000, 8);
	if (f->fail) { return; }

	if (fs_read_frame(f, fd, &req, 4, 42, 2000, 0) != 0) { return; }
	fs_verify_input(f, req.in_handle, 1);
	if (f->fail) { return; }
	fs_write_out(f, req.out_handle, au1, sizeof(au1));
	fs_frame_ok(f, fd, 4, 42, sizeof(au1), 0, 1000, 2000, 0);
	if (f->fail) { return; }

	uint8_t buf[256];
	int n = fs_recv(f, fd, buf, sizeof(buf), 2000, "ENC_SET");
	if (n < 0) { return; }
	struct n2vcd_enc_set exp_set;
	memset(&exp_set, 0, sizeof(exp_set));
	exp_set.h.magic = N2VCD_MAGIC;
	exp_set.h.type = N2VCD_ENC_SET;
	exp_set.h.seq = 5;
	exp_set.h.session = 42;
	exp_set.bitrate = 500000;
	exp_set.fps_num = 15;
	exp_set.fps_den = 1;
	exp_set.force_key = 1;
	if (n != (int)sizeof(exp_set) || memcmp(buf, &exp_set, sizeof(exp_set)) != 0) {
		fs_fail(f, "ENC_SET bytes (n=%d)", n);
		return;
	}
	fs_simple(f, fd, N2VCD_ENC_SET_OK, 5, 42, N2VCD_E_OK, 0);
	if (f->fail) { return; }

	n = fs_recv(f, fd, buf, sizeof(buf), 2000, "FLUSH");
	if (n < 0) { return; }
	struct n2vcd_simple exp_flush;
	memset(&exp_flush, 0, sizeof(exp_flush));
	exp_flush.h.magic = N2VCD_MAGIC;
	exp_flush.h.type = N2VCD_FLUSH;
	exp_flush.h.seq = 6;
	exp_flush.h.session = 42;
	if (n != (int)sizeof(exp_flush) || memcmp(buf, &exp_flush, sizeof(exp_flush)) != 0) {
		fs_fail(f, "FLUSH bytes (n=%d)", n);
		return;
	}
	fs_simple(f, fd, N2VCD_FLUSH_OK, 6, 42, N2VCD_E_OK, 0);
	if (f->fail) { return; }

	n = fs_recv(f, fd, buf, sizeof(buf), 2000, "CLOSE");
	if (n < 0) { return; }
	struct n2vcd_simple exp_close;
	memset(&exp_close, 0, sizeof(exp_close));
	exp_close.h.magic = N2VCD_MAGIC;
	exp_close.h.type = N2VCD_CLOSE;
	exp_close.h.seq = 7;
	exp_close.h.session = 42;
	if (n != (int)sizeof(exp_close) || memcmp(buf, &exp_close, sizeof(exp_close)) != 0) {
		fs_fail(f, "CLOSE bytes (n=%d)", n);
		return;
	}
	fs_simple(f, fd, N2VCD_CLOSE_OK, 7, 42, N2VCD_E_OK, 0);
}

static void
sc_hello_error(struct fakesrv *f, int fd)
{
	uint8_t buf[256];
	int n = fs_recv(f, fd, buf, sizeof(buf), 2000, "HELLO");
	if (n < 0) { return; }
	fs_simple(f, fd, N2VCD_ERROR, 1, 0, N2VCD_E_VERSION, 7);
}

static void
sc_hello_bad_version(struct fakesrv *f, int fd)
{
	uint8_t buf[256];
	int n = fs_recv(f, fd, buf, sizeof(buf), 2000, "HELLO");
	if (n < 0) { return; }
	struct n2vcd_hello_ok ok;
	memset(&ok, 0, sizeof(ok));
	ok.h.magic = N2VCD_MAGIC;
	ok.h.type = N2VCD_HELLO_OK;
	ok.h.seq = 1;
	ok.h.session = 0;
	ok.version = N2VCD_VERSION + 1;
	ok.enc_codecs = 3;
	ok.max_width = 4096;
	ok.max_height = 2160;
	(void)fs_send(f, fd, &ok, (uint32_t)sizeof(ok), "HELLO_OK bad version");
}

static void
sc_hello_short(struct fakesrv *f, int fd)
{
	uint8_t buf[256];
	int n = fs_recv(f, fd, buf, sizeof(buf), 2000, "HELLO");
	if (n < 0) { return; }
	struct n2vcd_hdr h;
	memset(&h, 0, sizeof(h));
	h.magic = N2VCD_MAGIC;
	h.type = N2VCD_HELLO_OK;
	h.seq = 1;
	(void)fs_send(f, fd, &h, (uint32_t)sizeof(h), "short HELLO_OK");
}

static void
sc_open_busy(struct fakesrv *f, int fd)
{
	fs_hello(f, fd);
	if (f->fail) { return; }
	uint8_t buf[256];
	int n = fs_recv(f, fd, buf, sizeof(buf), 2000, "OPEN");
	if (n < 0) { return; }
	fs_simple(f, fd, N2VCD_ERROR, 2, 9, N2VCD_E_BUSY, 1);
}

/* Reads the first ENC_FRAME of a 650x482 session (for scenarios that do not care about the pixels). */
static int
sc_first_frame(struct fakesrv *f, int fd, uint32_t session)
{
	struct n2vcd_enc_frame req;
	memset(&req, 0, sizeof(req));
	if (fs_read_frame(f, fd, &req, 3, session, 1000, 0) != 0) { return -1; }
	return 0;
}

static void
sc_frame_guest(struct fakesrv *f, int fd)
{
	fs_hello(f, fd);
	if (f->fail) { return; }
	fs_open(f, fd, 42, 488064, 200000);
	if (f->fail || sc_first_frame(f, fd, 42) != 0) { return; }
	fs_simple(f, fd, N2VCD_ERROR, 3, 42, N2VCD_E_GUEST, 2);
	if (f->fail) { return; }
	/* the session is dead: the client must not send a second frame or a CLOSE */
	if (poll_read(fd, 300) == 1) {
		uint8_t buf[64];
		ssize_t n = recv(fd, buf, sizeof(buf), 0);
		if (n > 0) { fs_fail(f, "client sent %zd bytes after E_GUEST", n); }
	}
}

static void
sc_frame_wrong_seq(struct fakesrv *f, int fd)
{
	fs_hello(f, fd);
	if (f->fail) { return; }
	fs_open(f, fd, 42, 488064, 200000);
	if (f->fail || sc_first_frame(f, fd, 42) != 0) { return; }
	fs_frame_ok(f, fd, 4, 42, 4, 0, 5, 1000, 0);   /* request was seq 3 */
	if (f->fail) { return; }
	if (poll_read(fd, 300) == 1) {
		uint8_t b[64];
		if (recv(fd, b, sizeof(b), 0) > 0) { fs_fail(f, "client sent after a wrong-seq reply"); }
	}
}

static void
sc_frame_wrong_session(struct fakesrv *f, int fd)
{
	fs_hello(f, fd);
	if (f->fail) { return; }
	fs_open(f, fd, 42, 488064, 200000);
	if (f->fail || sc_first_frame(f, fd, 42) != 0) { return; }
	fs_frame_ok(f, fd, 3, 43, 4, 0, 5, 1000, 0);   /* request was session 42 */
	if (f->fail) { return; }
	if (poll_read(fd, 300) == 1) {
		uint8_t b[64];
		if (recv(fd, b, sizeof(b), 0) > 0) { fs_fail(f, "client sent after a wrong-session reply"); }
	}
}

static void
sc_frame_short(struct fakesrv *f, int fd)
{
	fs_hello(f, fd);
	if (f->fail) { return; }
	fs_open(f, fd, 42, 488064, 200000);
	if (f->fail || sc_first_frame(f, fd, 42) != 0) { return; }
	struct n2vcd_enc_frame_ok ok;
	memset(&ok, 0, sizeof(ok));
	ok.h.magic = N2VCD_MAGIC;
	ok.h.type = N2VCD_ENC_FRAME_OK;
	ok.h.seq = 3;
	ok.h.session = 42;
	(void)fs_send(f, fd, &ok, 24, "short ENC_FRAME_OK");
	if (f->fail) { return; }
	if (poll_read(fd, 300) == 1) {
		uint8_t b[64];
		if (recv(fd, b, sizeof(b), 0) > 0) { fs_fail(f, "client sent after a short reply"); }
	}
}

static void
sc_silent(struct fakesrv *f, int fd)
{
	fs_hello(f, fd);
	if (f->fail) { return; }
	fs_open(f, fd, 42, 488064, 200000);
	if (f->fail || sc_first_frame(f, fd, 42) != 0) { return; }
	/* do not answer; the client times out and then sends CLOSE (the session is still alive) */
	uint8_t buf[256];
	int n = fs_recv(f, fd, buf, sizeof(buf), 2000, "CLOSE after timeout");
	if (n < 0) { return; }
	if (n != (int)sizeof(struct n2vcd_simple)) {
		fs_fail(f, "CLOSE after timeout size %d", n);
		return;
	}
	struct n2vcd_simple exp;
	memset(&exp, 0, sizeof(exp));
	exp.h.magic = N2VCD_MAGIC;
	exp.h.type = N2VCD_CLOSE;
	exp.h.seq = 4;
	exp.h.session = 42;
	if (memcmp(buf, &exp, sizeof(exp)) != 0) { fs_fail(f, "CLOSE after timeout bytes"); return; }
	fs_simple(f, fd, N2VCD_CLOSE_OK, 4, 42, N2VCD_E_OK, 0);
}

static void
sc_frame_seq0(struct fakesrv *f, int fd)
{
	fs_hello(f, fd);
	if (f->fail) { return; }
	fs_open(f, fd, 42, 488064, 200000);
	if (f->fail || sc_first_frame(f, fd, 42) != 0) { return; }
	fs_simple(f, fd, N2VCD_ERROR, 0, 42, N2VCD_E_GUEST, 5);
	if (f->fail) { return; }
	if (poll_read(fd, 300) == 1) {
		uint8_t b[64];
		if (recv(fd, b, sizeof(b), 0) > 0) { fs_fail(f, "client sent after an unsolicited seq-0 error"); }
	}
}

static void
sc_frame_oversize(struct fakesrv *f, int fd)
{
	fs_hello(f, fd);
	if (f->fail) { return; }
	fs_open(f, fd, 42, 488064, 200000);
	if (f->fail || sc_first_frame(f, fd, 42) != 0) { return; }
	fs_frame_ok(f, fd, 3, 42, 200001, 0, 5, 1000, 0);
	if (f->fail) { return; }
	if (poll_read(fd, 300) == 1) {
		uint8_t b[64];
		if (recv(fd, b, sizeof(b), 0) > 0) { fs_fail(f, "client sent after an oversized out_bytes"); }
	}
}

static void
sc_cap_local(struct fakesrv *f, int fd)
{
	fs_hello(f, fd);
	if (f->fail) { return; }
	fs_open(f, fd, 42, 488064, 200000);
	if (f->fail) { return; }
	/* the cap-too-small call must not send; the next datagram is the valid frame with pts 2000 */
	struct n2vcd_enc_frame req;
	memset(&req, 0, sizeof(req));
	if (fs_read_frame(f, fd, &req, 3, 42, 2000, 0) != 0) { return; }
	static const uint8_t au[4] = { 0, 0, 0, 1 };
	fs_write_out(f, req.out_handle, au, sizeof(au));
	fs_frame_ok(f, fd, 3, 42, sizeof(au), 0, 6, 2000, 0);
	if (f->fail) { return; }
	uint8_t buf[256];
	int n = fs_recv(f, fd, buf, sizeof(buf), 2000, "CLOSE");
	if (n < 0) { return; }
	fs_simple(f, fd, N2VCD_CLOSE_OK, 4, 42, N2VCD_E_OK, 0);
}

static void
sc_threads(struct fakesrv *f, int fd)
{
	fs_hello(f, fd);
	if (f->fail) { return; }
	fs_open(f, fd, 42, 488064, 200000);
	if (f->fail) { return; }
	uint32_t last_seq = 2;
	for (int i = 0; i < 40; i++) {
		uint8_t buf[256];
		int n = fs_recv(f, fd, buf, sizeof(buf), 3000, "thread ENC_FRAME");
		if (n < 0) { return; }
		struct n2vcd_enc_frame req;
		if (n != (int)sizeof(req)) { fs_fail(f, "thread frame size %d", n); return; }
		memcpy(&req, buf, sizeof(req));
		if (req.h.type != N2VCD_ENC_FRAME || req.h.session != 42 || req.h.seq != last_seq + 1u) {
			fs_fail(f, "thread frame seq %u (last %u)", req.h.seq, last_seq);
			return;
		}
		last_seq = req.h.seq;
		if (!((req.pts >= 1001 && req.pts <= 1020) || (req.pts >= 2002 && req.pts <= 2021))) {
			fs_fail(f, "thread pts %llu", (unsigned long long)req.pts);
			return;
		}
		uint8_t out[16];
		for (unsigned k = 0; k < 16; k++) { out[k] = (uint8_t)(req.pts + k); }
		fs_write_out(f, req.out_handle, out, sizeof(out));
		fs_frame_ok(f, fd, req.h.seq, 42, sizeof(out), 0, (uint32_t)(req.pts & 0xffffu), req.pts, 0);
		if (f->fail) { return; }
	}
	uint8_t buf[256];
	int n = fs_recv(f, fd, buf, sizeof(buf), 2000, "thread CLOSE");
	if (n < 0) { return; }
	fs_simple(f, fd, N2VCD_CLOSE_OK, last_seq + 1u, 42, N2VCD_E_OK, 0);
}

static void *
fakesrv_thread(void *arg)
{
	struct fakesrv *f = (struct fakesrv *)arg;
	int fd = fs_accept(f);
	if (fd < 0) { return NULL; }
	switch (f->scenario) {
	case SC_HAPPY: sc_happy(f, fd); break;
	case SC_HELLO_ERROR: sc_hello_error(f, fd); break;
	case SC_HELLO_BAD_VERSION: sc_hello_bad_version(f, fd); break;
	case SC_HELLO_SHORT: sc_hello_short(f, fd); break;
	case SC_OPEN_BUSY: sc_open_busy(f, fd); break;
	case SC_FRAME_GUEST: sc_frame_guest(f, fd); break;
	case SC_FRAME_WRONG_SEQ: sc_frame_wrong_seq(f, fd); break;
	case SC_FRAME_WRONG_SESSION: sc_frame_wrong_session(f, fd); break;
	case SC_FRAME_SHORT: sc_frame_short(f, fd); break;
	case SC_SILENT: sc_silent(f, fd); break;
	case SC_FRAME_SEQ0: sc_frame_seq0(f, fd); break;
	case SC_FRAME_OVERSIZE: sc_frame_oversize(f, fd); break;
	case SC_CAP_LOCAL: sc_cap_local(f, fd); break;
	case SC_THREADS: sc_threads(f, fd); break;
	default: fs_fail(f, "bad scenario"); break;
	}
	(void)close(fd);
	return NULL;
}

static int
start_fake(struct fakesrv *f, int scenario, const char *name)
{
	memset(f, 0, sizeof(*f));
	if (path_join(f->path, sizeof(f->path), name) != 0) { return -1; }
	(void)snprintf(f->bufdir, sizeof(f->bufdir), "%s.buf", f->path);
	f->scenario = scenario;
	int r = pthread_create(&f->th, NULL, fakesrv_thread, f);
	if (r != 0) { return r; }
	for (int i = 0; i < 3000; i++) {
		int ready = atomic_load(&f->ready);
		if (ready < 0) { fs_fail(f, "listen failed"); return 0; }
		if (ready > 0) { return 0; }
		sleep_ms(1);
	}
	fs_fail(f, "server did not start");
	return 0;
}

static void
finish_fake(struct fakesrv *f)
{
	pthread_join(f->th, NULL);
	CHECK(f->fail == 0, "fake server %s: %s", f->path, f->why);
}

static void
fill_y(uint8_t *y, uint32_t pitch, uint32_t w, uint32_t h, uint32_t frame, int zero_padding)
{
	if (zero_padding) { memset(y, 0, (size_t)pitch * h); }
	for (uint32_t row = 0; row < h; row++) {
		for (uint32_t col = 0; col < w; col++) { y[(size_t)row * pitch + col] = pat_y(row, col, frame); }
	}
}

static void
fill_c(uint8_t *c, uint32_t pitch, uint32_t w, uint32_t h, uint32_t frame, int zero_padding)
{
	uint32_t rows = h / 2u;
	if (zero_padding) { memset(c, 0, (size_t)pitch * rows); }
	for (uint32_t row = 0; row < rows; row++) {
		for (uint32_t col = 0; col < w; col++) { c[(size_t)row * pitch + col] = pat_c(row, col, frame); }
	}
}

static void
t_client_happy(void)
{
	struct fakesrv f;
	CHECK(start_fake(&f, SC_HAPPY, "happy") == 0, "start happy server");
	struct n2vcd_transport t;
	int r = n2vcd_transport_socket(&t, f.path);
	CHECK(r == 0, "socket transport: %d", r);
	struct n2vcd_client *c = NULL;
	char err[256];
	r = n2vcd_connect(&c, &t, 0, err, sizeof(err));
	CHECK(r == N2VCD_E_OK && c != NULL, "connect: %s", err);
	if (r != N2VCD_E_OK) { t.close(t.ctx); pthread_join(f.th, NULL); return; }
	struct n2vcd_caps caps;
	CHECK(n2vcd_info(c, &caps) == 0 && caps.version == 1 && caps.enc_codecs == 3 && caps.dec_codecs == 0 &&
	      caps.max_width == 4096 && caps.max_height == 2160, "capabilities");

	struct n2vcd_enc_params p;
	memset(&p, 0, sizeof(p));
	p.codec = N2VCD_CODEC_H264;
	p.encoder = 1;
	p.width = 650;
	p.height = 482;
	p.fps_num = 30;
	p.fps_den = 1;
	p.bitrate = 1000000;
	struct n2vcd_session *s = NULL;
	r = n2vcd_enc_open(c, &p, &s, err, sizeof(err));
	CHECK(r == N2VCD_E_OK && s != NULL, "enc_open: %s", err);
	if (r != N2VCD_E_OK) { n2vcd_disconnect(c); finish_fake(&f); return; }
	CHECK(n2vcd_session_id(s) == 42 && n2vcd_session_stride(s) == 656 &&
	      n2vcd_session_in_bytes(s) == 488064 && n2vcd_session_out_bytes(s) == 200000,
	      "session geometry id=%u stride=%u in=%u out=%u", n2vcd_session_id(s), n2vcd_session_stride(s),
	      n2vcd_session_in_bytes(s), n2vcd_session_out_bytes(s));

	uint8_t *y0 = (uint8_t *)malloc((size_t)650 * 482);
	uint8_t *c0 = (uint8_t *)malloc((size_t)650 * 241);
	uint8_t *y1 = (uint8_t *)malloc((size_t)656 * 482);
	uint8_t *c1 = (uint8_t *)malloc((size_t)656 * 241);
	uint8_t *out = (uint8_t *)malloc(200000);
	CHECK(y0 && c0 && y1 && c1 && out, "alloc");
	if (!y0 || !c0 || !y1 || !c1 || !out) { abort(); }

	fill_y(y0, 650, 650, 482, 0, 0);
	fill_c(c0, 650, 650, 482, 0, 0);
	fill_y(y1, 656, 650, 482, 1, 1);
	fill_c(c1, 656, 650, 482, 1, 1);

	uint32_t bytes = 0;
	int key = 0;
	r = n2vcd_enc_frame(s, y0, c0, 650, 650, 1000, 1, out, 200000, &bytes, &key);
	CHECK(r == N2VCD_E_OK && bytes == 12 && key == 1, "frame 0: r=%d bytes=%u key=%d", r, bytes, key);
	static const uint8_t au0[12] = { 0, 0, 0, 1, 0x67, 0x64, 0x00, 0x1f, 0x68, 0xee, 0x3c, 0x80 };
	CHECK(memcmp(out, au0, sizeof(au0)) == 0, "frame 0 access unit");

	r = n2vcd_enc_frame(s, y1, c1, 656, 656, 2000, 0, out, 200000, &bytes, &key);
	CHECK(r == N2VCD_E_OK && bytes == 5 && key == 0, "frame 1: r=%d bytes=%u key=%d", r, bytes, key);
	static const uint8_t au1[5] = { 0, 0, 0, 1, 0x41 };
	CHECK(memcmp(out, au1, sizeof(au1)) == 0, "frame 1 access unit");

	r = n2vcd_enc_set(s, 500000, 15, 1, 1);
	CHECK(r == N2VCD_E_OK, "enc_set: %d", r);
	r = n2vcd_flush(s);
	CHECK(r == N2VCD_E_OK, "flush: %d", r);
	r = n2vcd_close(s);
	CHECK(r == N2VCD_E_OK, "close: %d", r);

	free(y0);
	free(c0);
	free(y1);
	free(c1);
	free(out);
	n2vcd_disconnect(c);
	finish_fake(&f);
}

/* Small helper: a client connected to a freshly started scenario, with no session. */
struct fixture {
	struct fakesrv f;
	struct n2vcd_transport t;
	struct n2vcd_client *c;
};

static int
fixture_start(struct fixture *x, int scenario, const char *name, char *err, size_t errlen)
{
	if (start_fake(&x->f, scenario, name) != 0) { return -1; }
	int r = n2vcd_transport_socket(&x->t, x->f.path);
	if (r != 0) { (void)snprintf(err, errlen, "transport %d", r); return -1; }
	r = n2vcd_connect(&x->c, &x->t, 0, err, errlen);
	if (r != N2VCD_E_OK) {
		x->t.close(x->t.ctx);
		x->t.close = NULL;
		x->t.ctx = NULL;
		return r;
	}
	return 0;
}

static void
fixture_end(struct fixture *x)
{
	if (x->c) { n2vcd_disconnect(x->c); x->c = NULL; }
	finish_fake(&x->f);
}

static void
t_client_errors(void)
{
	char err[256];

	/* HELLO answered with an ERROR: the status is returned, the transport is not closed by connect */
	struct fixture x;
	memset(&x, 0, sizeof(x));
	int r = fixture_start(&x, SC_HELLO_ERROR, "e_hello", err, sizeof(err));
	CHECK(r == N2VCD_E_VERSION && x.c == NULL, "HELLO ERROR status: r=%d err=%s", r, err);
	finish_fake(&x.f);

	/* HELLO_OK with a different version */
	memset(&x, 0, sizeof(x));
	r = fixture_start(&x, SC_HELLO_BAD_VERSION, "e_ver", err, sizeof(err));
	CHECK(r == N2VCD_E_VERSION && x.c == NULL, "HELLO bad version: r=%d err=%s", r, err);
	finish_fake(&x.f);

	/* short HELLO_OK */
	memset(&x, 0, sizeof(x));
	r = fixture_start(&x, SC_HELLO_SHORT, "e_short", err, sizeof(err));
	CHECK(r == N2VCD_E_HW && x.c == NULL, "short HELLO_OK: r=%d err=%s", r, err);
	finish_fake(&x.f);

	/* OPEN answered E_BUSY */
	memset(&x, 0, sizeof(x));
	r = fixture_start(&x, SC_OPEN_BUSY, "e_busy", err, sizeof(err));
	CHECK(r == 0 && x.c != NULL, "busy fixture connect: %s", err);
	if (x.c) {
		struct n2vcd_enc_params p;
		memset(&p, 0, sizeof(p));
		p.codec = N2VCD_CODEC_H264;
		p.encoder = 1;
		p.width = 650;
		p.height = 482;
		p.fps_num = 30;
		p.fps_den = 1;
		p.bitrate = 1000000;
		struct n2vcd_session *s = NULL;
		r = n2vcd_enc_open(x.c, &p, &s, err, sizeof(err));
		CHECK(r == N2VCD_E_BUSY && s == NULL, "OPEN E_BUSY: r=%d err=%s", r, err);
		CHECK(n2vcd_last_detail(x.c) == 1, "OPEN E_BUSY detail");
		r = n2vcd_enc_open(x.c, &p, &s, err, sizeof(err));
		CHECK(r == N2VCD_E_HW || r == N2VCD_E_SESSION,
		      "a second OPEN without a reply is a transport failure: %d", r);
	}
	fixture_end(&x);

	/* ENC_FRAME answered E_GUEST: the session dies, the next call is E_SESSION with no datagram */
	memset(&x, 0, sizeof(x));
	r = fixture_start(&x, SC_FRAME_GUEST, "e_guest", err, sizeof(err));
	CHECK(r == 0 && x.c != NULL, "guest fixture connect: %s", err);
	if (x.c) {
		struct n2vcd_enc_params p;
		memset(&p, 0, sizeof(p));
		p.codec = N2VCD_CODEC_H264;
		p.encoder = 1;
		p.width = 650;
		p.height = 482;
		p.fps_num = 30;
		p.fps_den = 1;
		p.bitrate = 1000000;
		struct n2vcd_session *s = NULL;
		r = n2vcd_enc_open(x.c, &p, &s, err, sizeof(err));
		CHECK(r == N2VCD_E_OK && s, "guest open: %s", err);
		uint8_t *y = (uint8_t *)calloc(1, 650u * 482u);
		uint8_t *cb = (uint8_t *)calloc(1, 650u * 241u);
		uint8_t *out = (uint8_t *)malloc(200000);
		CHECK(y && cb && out, "alloc");
		uint32_t bytes = 0;
		int key = 0;
		r = n2vcd_enc_frame(s, y, cb, 650, 650, 1000, 0, out, 200000, &bytes, &key);
		CHECK(r == N2VCD_E_GUEST, "ENC_FRAME E_GUEST: %d", r);
		CHECK(n2vcd_last_detail(x.c) == 2, "E_GUEST detail");
		r = n2vcd_enc_frame(s, y, cb, 650, 650, 1000, 0, out, 200000, &bytes, &key);
		CHECK(r == N2VCD_E_SESSION, "dead session: %d", r);
		r = n2vcd_enc_set(s, 1, 1, 1, 1);
		CHECK(r == N2VCD_E_SESSION, "dead session set: %d", r);
		r = n2vcd_close(s);
		CHECK(r == N2VCD_E_OK, "close of a dead session is cleanup: %d", r);
		free(y);
		free(cb);
		free(out);
	}
	fixture_end(&x);

	/* wrong sequence number */
	memset(&x, 0, sizeof(x));
	r = fixture_start(&x, SC_FRAME_WRONG_SEQ, "e_seq", err, sizeof(err));
	CHECK(r == 0, "wrong-seq connect");
	if (x.c) {
		struct n2vcd_enc_params p;
		memset(&p, 0, sizeof(p));
		p.codec = N2VCD_CODEC_H264;
		p.encoder = 1;
		p.width = 650;
		p.height = 482;
		p.fps_num = 30;
		p.fps_den = 1;
		p.bitrate = 1000000;
		struct n2vcd_session *s = NULL;
		r = n2vcd_enc_open(x.c, &p, &s, err, sizeof(err));
		CHECK(r == N2VCD_E_OK, "wrong-seq open");
		uint8_t *y = (uint8_t *)calloc(1, 650u * 482u);
		uint8_t *cb = (uint8_t *)calloc(1, 650u * 241u);
		uint8_t *out = (uint8_t *)malloc(200000);
		uint32_t bytes = 0;
		int key = 0;
		r = n2vcd_enc_frame(s, y, cb, 650, 650, 1000, 0, out, 200000, &bytes, &key);
		CHECK(r == N2VCD_E_HW, "wrong seq: %d", r);
		r = n2vcd_enc_set(s, 1, 1, 1, 1);
		CHECK(r == N2VCD_E_SESSION, "client broken after wrong seq: %d", r);
		CHECK(n2vcd_close(s) == N2VCD_E_OK, "close after wrong seq");
		free(y);
		free(cb);
		free(out);
	}
	fixture_end(&x);

	/* a typed reply for another session: the central xact check kills the client */
	memset(&x, 0, sizeof(x));
	r = fixture_start(&x, SC_FRAME_WRONG_SESSION, "e_sess", err, sizeof(err));
	CHECK(r == 0, "wrong-session connect");
	if (x.c) {
		struct n2vcd_enc_params p;
		memset(&p, 0, sizeof(p));
		p.codec = N2VCD_CODEC_H264;
		p.encoder = 1;
		p.width = 650;
		p.height = 482;
		p.fps_num = 30;
		p.fps_den = 1;
		p.bitrate = 1000000;
		struct n2vcd_session *s = NULL;
		r = n2vcd_enc_open(x.c, &p, &s, err, sizeof(err));
		CHECK(r == N2VCD_E_OK, "wrong-session open");
		uint8_t *y = (uint8_t *)calloc(1, 650u * 482u);
		uint8_t *cb = (uint8_t *)calloc(1, 650u * 241u);
		uint8_t *out = (uint8_t *)malloc(200000);
		uint32_t bytes = 0;
		int key = 0;
		r = n2vcd_enc_frame(s, y, cb, 650, 650, 1000, 0, out, 200000, &bytes, &key);
		CHECK(r == N2VCD_E_SESSION, "wrong-session reply: %d", r);
		r = n2vcd_enc_set(s, 1, 1, 1, 1);
		CHECK(r == N2VCD_E_SESSION, "dead after wrong-session reply: %d", r);
		CHECK(n2vcd_close(s) == N2VCD_E_OK, "close after wrong-session reply");
		free(y);
		free(cb);
		free(out);
	}
	fixture_end(&x);

	/* short ENC_FRAME_OK */
	memset(&x, 0, sizeof(x));
	r = fixture_start(&x, SC_FRAME_SHORT, "e_fshort", err, sizeof(err));
	CHECK(r == 0, "short-reply connect");
	if (x.c) {
		struct n2vcd_enc_params p;
		memset(&p, 0, sizeof(p));
		p.codec = N2VCD_CODEC_H264;
		p.encoder = 1;
		p.width = 650;
		p.height = 482;
		p.fps_num = 30;
		p.fps_den = 1;
		p.bitrate = 1000000;
		struct n2vcd_session *s = NULL;
		r = n2vcd_enc_open(x.c, &p, &s, err, sizeof(err));
		CHECK(r == N2VCD_E_OK, "short-reply open");
		uint8_t *y = (uint8_t *)calloc(1, 650u * 482u);
		uint8_t *cb = (uint8_t *)calloc(1, 650u * 241u);
		uint8_t *out = (uint8_t *)malloc(200000);
		uint32_t bytes = 0;
		int key = 0;
		r = n2vcd_enc_frame(s, y, cb, 650, 650, 1000, 0, out, 200000, &bytes, &key);
		CHECK(r == N2VCD_E_HW, "short reply: %d", r);
		CHECK(n2vcd_close(s) == N2VCD_E_OK, "close after short reply");
		free(y);
		free(cb);
		free(out);
	}
	fixture_end(&x);
}

static void
t_client_more_errors(void)
{
	char err[256];
	struct n2vcd_enc_params p;
	memset(&p, 0, sizeof(p));
	p.codec = N2VCD_CODEC_H264;
	p.encoder = 1;
	p.width = 650;
	p.height = 482;
	p.fps_num = 30;
	p.fps_den = 1;
	p.bitrate = 1000000;

	/* silence: every receive is bounded by the timeout */
	struct fixture x;
	memset(&x, 0, sizeof(x));
	int r = fixture_start(&x, SC_SILENT, "e_silent", err, sizeof(err));
	CHECK(r == 0, "silent connect: %s", err);
	if (x.c) {
		struct n2vcd_session *s = NULL;
		r = n2vcd_enc_open(x.c, &p, &s, err, sizeof(err));
		CHECK(r == N2VCD_E_OK, "silent open");
		n2vcd_set_timeout(x.c, 100);
		uint8_t *y = (uint8_t *)calloc(1, 650u * 482u);
		uint8_t *cb = (uint8_t *)calloc(1, 650u * 241u);
		uint8_t *out = (uint8_t *)malloc(200000);
		uint32_t bytes = 0;
		int key = 0;
		uint64_t t0 = now_ms();
		r = n2vcd_enc_frame(s, y, cb, 650, 650, 1000, 0, out, 200000, &bytes, &key);
		uint64_t dt = now_ms() - t0;
		CHECK(r == N2VCD_E_HW && dt >= 90 && dt < 2000, "silence: r=%d after %llu ms", r,
		      (unsigned long long)dt);
		CHECK(n2vcd_last_errno(x.c) == ETIMEDOUT, "silence errno %u", n2vcd_last_errno(x.c));
		CHECK(n2vcd_close(s) == N2VCD_E_OK, "close after silence (CLOSE still sent)");
		free(y);
		free(cb);
		free(out);
	}
	fixture_end(&x);

	/* unsolicited seq-0 ERROR while waiting for a reply */
	memset(&x, 0, sizeof(x));
	r = fixture_start(&x, SC_FRAME_SEQ0, "e_seq0", err, sizeof(err));
	CHECK(r == 0, "seq0 connect");
	if (x.c) {
		struct n2vcd_session *s = NULL;
		r = n2vcd_enc_open(x.c, &p, &s, err, sizeof(err));
		CHECK(r == N2VCD_E_OK, "seq0 open");
		uint8_t *y = (uint8_t *)calloc(1, 650u * 482u);
		uint8_t *cb = (uint8_t *)calloc(1, 650u * 241u);
		uint8_t *out = (uint8_t *)malloc(200000);
		uint32_t bytes = 0;
		int key = 0;
		r = n2vcd_enc_frame(s, y, cb, 650, 650, 1000, 0, out, 200000, &bytes, &key);
		CHECK(r == N2VCD_E_GUEST && n2vcd_last_detail(x.c) == 5, "seq0 error: r=%d detail=%u", r,
		      n2vcd_last_detail(x.c));
		r = n2vcd_enc_frame(s, y, cb, 650, 650, 1000, 0, out, 200000, &bytes, &key);
		CHECK(r == N2VCD_E_SESSION, "session dead after seq0: %d", r);
		CHECK(n2vcd_close(s) == N2VCD_E_OK, "close after seq0");
		free(y);
		free(cb);
		free(out);
	}
	fixture_end(&x);

	/* out_bytes larger than the declared output buffer */
	memset(&x, 0, sizeof(x));
	r = fixture_start(&x, SC_FRAME_OVERSIZE, "e_over", err, sizeof(err));
	CHECK(r == 0, "oversize connect");
	if (x.c) {
		struct n2vcd_session *s = NULL;
		r = n2vcd_enc_open(x.c, &p, &s, err, sizeof(err));
		CHECK(r == N2VCD_E_OK, "oversize open");
		uint8_t *y = (uint8_t *)calloc(1, 650u * 482u);
		uint8_t *cb = (uint8_t *)calloc(1, 650u * 241u);
		uint8_t *out = (uint8_t *)malloc(200000);
		uint32_t bytes = 0;
		int key = 0;
		r = n2vcd_enc_frame(s, y, cb, 650, 650, 1000, 0, out, 200000, &bytes, &key);
		CHECK(r == N2VCD_E_HW, "oversized out_bytes: %d", r);
		CHECK(n2vcd_close(s) == N2VCD_E_OK, "close after oversize");
		free(y);
		free(cb);
		free(out);
	}
	fixture_end(&x);

	/* cap smaller than the session output: rejected locally, nothing is sent */
	memset(&x, 0, sizeof(x));
	r = fixture_start(&x, SC_CAP_LOCAL, "e_cap", err, sizeof(err));
	CHECK(r == 0, "cap connect");
	if (x.c) {
		struct n2vcd_session *s = NULL;
		r = n2vcd_enc_open(x.c, &p, &s, err, sizeof(err));
		CHECK(r == N2VCD_E_OK, "cap open");
		uint8_t *y = (uint8_t *)calloc(1, 650u * 482u);
		uint8_t *cb = (uint8_t *)calloc(1, 650u * 241u);
		uint8_t *out = (uint8_t *)malloc(200000);
		uint32_t bytes = 123;
		int key = 7;
		r = n2vcd_enc_frame(s, y, cb, 650, 650, 1000, 0, out, 1000, &bytes, &key);
		CHECK(r == N2VCD_E_ARGS && bytes == 0 && key == 0, "cap too small: r=%d bytes=%u key=%d", r, bytes, key);
		r = n2vcd_enc_frame(s, y, cb, 650, 650, 2000, 0, out, 200000, &bytes, &key);
		CHECK(r == N2VCD_E_OK && bytes == 4 && key == 0, "valid frame after the local reject: r=%d bytes=%u", r, bytes);
		CHECK(n2vcd_close(s) == N2VCD_E_OK, "cap close");
		free(y);
		free(cb);
		free(out);
	}
	fixture_end(&x);
}

struct tharg {
	struct n2vcd_session *s;
	uint64_t pts;
	int fails;
	char why[128];
};

static void *
frame_thread(void *arg)
{
	struct tharg *a = (struct tharg *)arg;
	uint8_t *y = (uint8_t *)calloc(1, 650u * 482u);
	uint8_t *cb = (uint8_t *)calloc(1, 650u * 241u);
	uint8_t *out = (uint8_t *)malloc(200000);
	if (!y || !cb || !out) {
		a->fails++;
		(void)snprintf(a->why, sizeof(a->why), "alloc");
		free(y);
		free(cb);
		free(out);
		return NULL;
	}
	for (int i = 0; i < 20; i++) {
		memset(out, 0xcc, 16);
		uint32_t bytes = 0;
		int key = 0;
		int r = n2vcd_enc_frame(a->s, y, cb, 650, 650, a->pts + (uint64_t)i, 0, out, 200000,
		                        &bytes, &key);
		uint64_t expect_pts = a->pts + (uint64_t)i;
		if (r != N2VCD_E_OK || bytes != 16) {
			a->fails++;
			(void)snprintf(a->why, sizeof(a->why), "call %d: r=%d bytes=%u", i, r, bytes);
			break;
		}
		for (unsigned k = 0; k < 16; k++) {
			if (out[k] != (uint8_t)(expect_pts + k)) {
				a->fails++;
				(void)snprintf(a->why, sizeof(a->why), "call %d: output is not this call's pts", i);
				break;
			}
		}
	}
	free(y);
	free(cb);
	free(out);
	return NULL;
}

static void
t_client_threads(void)
{
	char err[256];
	struct fixture x;
	memset(&x, 0, sizeof(x));
	int r = fixture_start(&x, SC_THREADS, "e_threads", err, sizeof(err));
	CHECK(r == 0, "threads connect: %s", err);
	if (!x.c) { finish_fake(&x.f); return; }
	struct n2vcd_enc_params p;
	memset(&p, 0, sizeof(p));
	p.codec = N2VCD_CODEC_H264;
	p.encoder = 1;
	p.width = 650;
	p.height = 482;
	p.fps_num = 30;
	p.fps_den = 1;
	p.bitrate = 1000000;
	struct n2vcd_session *s = NULL;
	r = n2vcd_enc_open(x.c, &p, &s, err, sizeof(err));
	CHECK(r == N2VCD_E_OK, "threads open: %s", err);
	if (r == N2VCD_E_OK && s) {
		struct tharg a1 = { .s = s, .pts = 1001 };
		struct tharg a2 = { .s = s, .pts = 2002 };
		pthread_t t1, t2;
		CHECK(pthread_create(&t1, NULL, frame_thread, &a1) == 0, "thread 1");
		CHECK(pthread_create(&t2, NULL, frame_thread, &a2) == 0, "thread 2");
		pthread_join(t1, NULL);
		pthread_join(t2, NULL);
		CHECK(a1.fails == 0, "thread 1: %s", a1.why);
		CHECK(a2.fails == 0, "thread 2: %s", a2.why);
		CHECK(n2vcd_close(s) == N2VCD_E_OK, "threads close");
	}
	n2vcd_disconnect(x.c);
	finish_fake(&x.f);
}

static void
t_connect_path(void)
{
	char err[256];
	CHECK(n2vcd_status_errno(N2VCD_E_OK) == 0, "status_errno OK");
	CHECK(n2vcd_status_errno(N2VCD_E_VERSION) == -EPROTO, "status_errno version");
	CHECK(n2vcd_status_errno(N2VCD_E_UNSUPPORTED) == -EOPNOTSUPP, "status_errno unsupported");
	CHECK(n2vcd_status_errno(N2VCD_E_BUSY) == -EBUSY, "status_errno busy");
	CHECK(n2vcd_status_errno(N2VCD_E_ARGS) == -EINVAL, "status_errno args");
	CHECK(n2vcd_status_errno(N2VCD_E_HW) == -EIO, "status_errno hardware");
	CHECK(n2vcd_status_errno(N2VCD_E_GUEST) == -EIO, "status_errno guest");
	CHECK(n2vcd_status_errno(N2VCD_E_NOMEM) == -ENOMEM, "status_errno nomem");
	CHECK(n2vcd_status_errno(N2VCD_E_SESSION) == -EPIPE, "status_errno session");
	CHECK(n2vcd_status_errno(99) == -EIO, "status_errno unknown");

	struct n2vcd_client *c = NULL;
	int r = n2vcd_connect_path(&c, "/x", "/y", 0, err, sizeof(err));
	CHECK(r == N2VCD_E_ARGS && c == NULL, "connect_path rejects both paths: %d", r);
	r = n2vcd_connect_path(&c, NULL, "/tmp/n2hw-P03/definitely-not-a-device", 0, err, sizeof(err));
	CHECK(r == N2VCD_E_HW && c == NULL, "connect_path relay open failure: %d (%s)", r, err);
	r = n2vcd_connect_path(&c, "/tmp/n2hw-P03/no-such-socket", NULL, 0, err, sizeof(err));
	CHECK(r == N2VCD_E_HW && c == NULL, "connect_path socket open failure: %d (%s)", r, err);

	struct fakesrv f;
	CHECK(start_fake(&f, SC_OPEN_BUSY, "e_path") == 0, "start connect_path server");
	r = n2vcd_connect_path(&c, f.path, NULL, 0, err, sizeof(err));
	CHECK(r == N2VCD_E_OK && c != NULL, "connect_path socket: %s", err);
	if (r == N2VCD_E_OK && c) {
		struct n2vcd_enc_params p;
		memset(&p, 0, sizeof(p));
		p.codec = N2VCD_CODEC_H264;
		p.encoder = 1;
		p.width = 650;
		p.height = 482;
		p.fps_num = 30;
		p.fps_den = 1;
		p.bitrate = 1000000;
		struct n2vcd_session *s = NULL;
		int rr = n2vcd_enc_open(c, &p, &s, err, sizeof(err));
		CHECK(rr == N2VCD_E_BUSY && s == NULL, "connect_path client works: open=%d", rr);
		n2vcd_disconnect(c);
	}
	finish_fake(&f);
}

/* =================================================================================================================
 * main
 * ================================================================================================================= */

int
main(void)
{
	(void)mkdir("/tmp/n2hw-P03", 0700);
	int n = snprintf(g_dir, sizeof(g_dir), "/tmp/n2hw-P03/t-%ld", (long)getpid());
	if (n <= 0 || (size_t)n >= sizeof(g_dir) || mkdir(g_dir, 0700) != 0) {
		printf("N2TEST P03 FAIL cannot create test directory\n");
		return 1;
	}

	t_hw_dev();
	t_relay();
	t_socket_transport();
	t_server_helpers();
	t_client_happy();
	t_client_errors();
	t_client_more_errors();
	t_connect_path();
	t_client_threads();

	/* best-effort cleanup of the directory (files are unlinked by the code under test) */
	(void)rmdir(g_dir);

	printf("checks: %d, failures: %d\n", g_checks, g_fails);
	if (g_fails) {
		printf("N2TEST P03-TEST FAIL %d/%d\n", g_checks - g_fails, g_checks);
		return 1;
	}
	printf("N2TEST P03-TEST PASS %d/%d\n", g_checks, g_checks);
	return 0;
}
