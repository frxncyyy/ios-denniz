/* SPDX-License-Identifier: MIT */
/*
 * n2vc_hw_dev.c -- the phone hardware back end of include/n2vc_hw.h: each struct n2vc_hw method is exactly one
 * RWVC_* ioctl of include/redwood_vcodec.h on /dev/redwood-vcodec (minor 0).  EINTR is retried; every other error
 * is returned as -errno.  No caching, no register filtering and no policy: the kernel owns the pool, the windows and
 * the power references.
 *
 * n2vc_hw.regs() has no timeout parameter, so RWVC_REGS.poll_timeout_us is set to 1000000 (the kernel's
 * N2VC_POLL_MAX_US, the largest value it accepts).  A POLL op that does not match within that budget makes the
 * batch return -ETIMEDOUT with *done = the number of ops completed before it.
 */
#define _POSIX_C_SOURCE 200809L

#include "n2vc_hw.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "n2vc_ioctl.h"

#ifndef EPROTO
#define EPROTO 100
#endif

#define N2VC_DEV_POLL_TIMEOUT_US 1000000u

#if defined(N2VC_TEST_HOOKS)
static n2vc_ioctl_fn g_dev_hook;
static void *g_dev_hook_ctx;

void
n2vc_dev_test_set_ioctl(n2vc_ioctl_fn fn, void *ctx)
{
	g_dev_hook = fn;
	g_dev_hook_ctx = ctx;
}
#endif

static int
dev_ioctl(int fd, unsigned long req, void *arg)
{
	for (;;) {
#if defined(N2VC_TEST_HOOKS)
		if (g_dev_hook) {
			if (g_dev_hook(g_dev_hook_ctx, fd, req, arg) == 0) { return 0; }
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

struct dev {
	int fd;
};

static int
dev_power(void *ctx, unsigned block, int on)
{
	struct dev *d = (struct dev *)ctx;
	if (!d) { return -EINVAL; }
	struct rwvc_power p;
	memset(&p, 0, sizeof(p));
	p.block = block;
	p.on = on ? 1u : 0u;
	return dev_ioctl(d->fd, RWVC_POWER, &p);
}

static int
dev_regs(void *ctx, struct rwvc_reg_op *ops, unsigned count, unsigned *done)
{
	struct dev *d = (struct dev *)ctx;
	if (!d) { return -EINVAL; }
	if (count && !ops) { return -EINVAL; }
	struct rwvc_reg_batch b;
	memset(&b, 0, sizeof(b));
	b.ops = (uint64_t)(uintptr_t)ops;
	b.count = count;
	b.out_done = 0;
	b.poll_timeout_us = N2VC_DEV_POLL_TIMEOUT_US;
	int rc = dev_ioctl(d->fd, RWVC_REGS, &b);
	if (done) { *done = b.out_done; }
	return rc;
}

static int
dev_wait_irq(void *ctx, unsigned irq, unsigned timeout_ms, uint32_t *status)
{
	struct dev *d = (struct dev *)ctx;
	if (!d) { return -EINVAL; }
	struct rwvc_wait_irq w;
	memset(&w, 0, sizeof(w));
	w.irq = irq;
	w.timeout_ms = timeout_ms;
	int rc = dev_ioctl(d->fd, RWVC_WAIT_IRQ, &w);
	if (status) { *status = w.out_status; }
	return rc;
}

static int
dev_irq_clear(void *ctx, unsigned irq)
{
	struct dev *d = (struct dev *)ctx;
	if (!d) { return -EINVAL; }
	struct rwvc_irq_clear c;
	memset(&c, 0, sizeof(c));
	c.irq = irq;
	return dev_ioctl(d->fd, RWVC_IRQ_CLEAR, &c);
}

static int
dev_dma_alloc(void *ctx, uint32_t bytes, uint32_t flags, uint32_t *handle, uint32_t *pa)
{
	struct dev *d = (struct dev *)ctx;
	if (!d || !handle || !pa || bytes == 0) { return -EINVAL; }
	struct rwvc_dma_alloc a;
	memset(&a, 0, sizeof(a));
	a.bytes = bytes;
	a.flags = flags;
	int rc = dev_ioctl(d->fd, RWVC_DMA_ALLOC, &a);
	if (rc != 0) { return rc; }
	if (a.out_handle == 0) { return -EIO; }
	*handle = a.out_handle;
	*pa = a.out_pa;
	return 0;
}

static int
dev_dma_free(void *ctx, uint32_t handle)
{
	struct dev *d = (struct dev *)ctx;
	if (!d || handle == 0) { return -EINVAL; }
	struct rwvc_dma_free f;
	memset(&f, 0, sizeof(f));
	f.handle = handle;
	return dev_ioctl(d->fd, RWVC_DMA_FREE, &f);
}

static int
dev_dma_rw(void *ctx, uint32_t handle, uint32_t offset, void *host, uint32_t bytes, int write)
{
	struct dev *d = (struct dev *)ctx;
	if (!d || !host || handle == 0) { return -EINVAL; }
	if (bytes > 0xffffffffu - offset) { return -EINVAL; }
	struct rwvc_dma_rw w;
	memset(&w, 0, sizeof(w));
	w.handle = handle;
	w.offset = offset;
	w.user = (uint64_t)(uintptr_t)host;
	w.bytes = bytes;
	w.write = write ? 1u : 0u;
	return dev_ioctl(d->fd, RWVC_DMA_RW, &w);
}

static void
dev_close(void *ctx)
{
	struct dev *d = (struct dev *)ctx;
	if (!d) { return; }
	(void)close(d->fd);
	free(d);
}

int
n2vc_hw_open_dev(struct n2vc_hw *out, const char *dev_path)
{
	if (!out) { return -EINVAL; }
	memset(out, 0, sizeof(*out));
	if (!dev_path) { dev_path = "/dev/redwood-vcodec"; }
	int fd;
	do {
		fd = open(dev_path, O_RDWR);
	} while (fd < 0 && errno == EINTR);
	if (fd < 0) { return -errno; }

	struct rwvc_info info;
	memset(&info, 0, sizeof(info));
	int rc = dev_ioctl(fd, RWVC_INFO, &info);
	if (rc != 0) {
		(void)close(fd);
		return rc;
	}
	if (info.abi_version != RWVC_ABI_VERSION) {
		(void)close(fd);
		return -EPROTO;
	}

	struct dev *d = (struct dev *)calloc(1, sizeof(*d));
	if (!d) {
		(void)close(fd);
		return -ENOMEM;
	}
	d->fd = fd;
	out->ctx = d;
	out->power = dev_power;
	out->regs = dev_regs;
	out->wait_irq = dev_wait_irq;
	out->irq_clear = dev_irq_clear;
	out->dma_alloc = dev_dma_alloc;
	out->dma_free = dev_dma_free;
	out->dma_rw = dev_dma_rw;
	out->close = dev_close;
	return 0;
}
