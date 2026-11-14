/* SPDX-License-Identifier: MIT */
/* hw_fake.c -- the recording n2vc_hw behind ap/test_v41.  Not a register model: n2ap_venc.c never
 * touches a codec register (the daemon owns them), so only power, wait_irq and DMA are needed.
 * Power follows the block dependency the kernel model uses: VENC_C1 requires VENC. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hw_fake.h"

static void ev(struct hwfake *f, uint32_t kind, uint32_t a, uint32_t b, uint32_t c)
{
	if (f->nev < HWFAKE_MAX_EVENTS) {
		f->ev[f->nev].kind = kind;
		f->ev[f->nev].a = a;
		f->ev[f->nev].b = b;
		f->ev[f->nev].c = c;
		f->nev++;
	}
}

static int fake_power(void *ctx, unsigned block, int on)
{
	struct hwfake *f = ctx;

	if (f == NULL || block >= RWVC_BLK_COUNT) {
		return -EINVAL;
	}
	pthread_mutex_lock(&f->lock);
	f->power_calls++;
	if (on) {
		if (block == RWVC_BLK_VENC && f->fail_power) {
			f->violations++;
			pthread_mutex_unlock(&f->lock);
			return -EIO;
		}
		if (block == RWVC_BLK_VENC_C1 && f->refs[RWVC_BLK_VENC] == 0u) {
			f->violations++;
			pthread_mutex_unlock(&f->lock);
			return -EPERM;
		}
		f->refs[block]++;
	} else {
		if (f->refs[block] == 0u) {
			f->violations++;
			pthread_mutex_unlock(&f->lock);
			return -EINVAL;
		}
		f->refs[block]--;
	}
	ev(f, HWFAKE_EV_POWER, block, (uint32_t)on, f->refs[block]);
	pthread_mutex_unlock(&f->lock);
	return 0;
}

static int fake_regs(void *ctx, struct rwvc_reg_op *ops, unsigned count, unsigned *done)
{
	(void)ctx;
	(void)ops;
	(void)count;
	if (done != NULL) {
		*done = 0;
	}
	return -ENOSYS;
}

static int fake_wait_irq(void *ctx, unsigned irq, unsigned timeout_ms, uint32_t *status)
{
	struct hwfake *f = ctx;

	if (f == NULL || irq != RWVC_IRQ_VENC || status == NULL) {
		return -EINVAL;
	}
	pthread_mutex_lock(&f->lock);
	f->wait_irq_calls++;
	if (f->fail_wait) {
		ev(f, HWFAKE_EV_WAIT_IRQ, irq, timeout_ms, 0xffffffffu);
		pthread_mutex_unlock(&f->lock);
		return -EIO;
	}
	if (!f->irq_pending) {
		f->wait_irq_timeouts++;
		ev(f, HWFAKE_EV_WAIT_IRQ, irq, timeout_ms, 0u);
		pthread_mutex_unlock(&f->lock);
		return -ETIMEDOUT;
	}
	*status = f->irq_status;
	f->irq_pending = 0;
	ev(f, HWFAKE_EV_WAIT_IRQ, irq, timeout_ms, *status);
	pthread_mutex_unlock(&f->lock);
	return 0;
}

static int fake_irq_clear(void *ctx, unsigned irq)
{
	struct hwfake *f = ctx;

	if (f == NULL || irq != RWVC_IRQ_VENC) {
		return -EINVAL;
	}
	pthread_mutex_lock(&f->lock);
	f->irq_pending = 0;
	ev(f, HWFAKE_EV_IRQ_CLEAR, irq, 0, 0);
	pthread_mutex_unlock(&f->lock);
	return 0;
}

static int fake_dma_alloc(void *ctx, uint32_t bytes, uint32_t flags, uint32_t *handle,
                          uint32_t *pa)
{
	struct hwfake *f = ctx;
	uint32_t span, chosen = UINT32_MAX;
	uint8_t *host;

	(void)flags;
	if (f == NULL || handle == NULL || pa == NULL || bytes == 0u) {
		return -EINVAL;
	}
	span = (bytes + 0xfffu) & ~0xfffu;
	pthread_mutex_lock(&f->lock);
	for (unsigned i = 0; i < HWFAKE_MAX_DMA; i++) {
		if (!f->dma[i].used) {
			chosen = i;
			break;
		}
	}
	if (chosen == UINT32_MAX || f->next_pa + span > f->pa_limit) {
		pthread_mutex_unlock(&f->lock);
		return -ENOMEM;
	}
	host = calloc(1, span);
	if (host == NULL) {
		pthread_mutex_unlock(&f->lock);
		return -ENOMEM;
	}
	f->next_handle++;
	if (f->next_handle == 0u) {
		f->next_handle = 1u;
	}
	f->dma[chosen].used = 1;
	f->dma[chosen].handle = f->next_handle;
	f->dma[chosen].pa = f->next_pa;
	f->dma[chosen].bytes = span;
	f->dma[chosen].host = host;
	*handle = f->next_handle;
	*pa = f->next_pa;
	ev(f, HWFAKE_EV_DMA_ALLOC, *handle, *pa, span);
	f->next_pa += span;
	pthread_mutex_unlock(&f->lock);
	return 0;
}

static int fake_dma_free(void *ctx, uint32_t handle)
{
	struct hwfake *f = ctx;
	int rc = -ENOENT;

	if (f == NULL || handle == 0u) {
		return -EINVAL;
	}
	pthread_mutex_lock(&f->lock);
	for (unsigned i = 0; i < HWFAKE_MAX_DMA; i++) {
		if (f->dma[i].used && f->dma[i].handle == handle) {
			ev(f, HWFAKE_EV_DMA_FREE, handle, f->dma[i].pa, f->dma[i].bytes);
			free(f->dma[i].host);
			memset(&f->dma[i], 0, sizeof(f->dma[i]));
			rc = 0;
			break;
		}
	}
	pthread_mutex_unlock(&f->lock);
	return rc;
}

static int dma_copy(struct hwfake *f, uint32_t handle, uint32_t off, void *other, uint32_t bytes,
                    int write)
{
	int rc = -ENOENT;

	if (f == NULL || other == NULL) {
		return -EINVAL;
	}
	pthread_mutex_lock(&f->lock);
	for (unsigned i = 0; i < HWFAKE_MAX_DMA; i++) {
		if (f->dma[i].used && f->dma[i].handle == handle) {
			if ((uint64_t)off + bytes > f->dma[i].bytes) {
				rc = -ERANGE;
			} else {
				if (write) {
					memcpy(f->dma[i].host + off, other, bytes);
				} else {
					memcpy(other, f->dma[i].host + off, bytes);
				}
				rc = 0;
			}
			ev(f, HWFAKE_EV_DMA_RW, handle, off, (bytes & 0x7fffffffu) |
			   (write ? 0x80000000u : 0u));
			break;
		}
	}
	pthread_mutex_unlock(&f->lock);
	return rc;
}

static int fake_dma_rw(void *ctx, uint32_t handle, uint32_t offset, void *host, uint32_t bytes,
                       int write)
{
	return dma_copy(ctx, handle, offset, host, bytes, write);
}

static void fake_close(void *ctx)
{
	(void)ctx;
}

void hwfake_init(struct n2vc_hw *h, struct hwfake *f, uint32_t pa_base, uint32_t pa_limit)
{
	memset(f, 0, sizeof(*f));
	pthread_mutex_init(&f->lock, NULL);
	f->next_pa = pa_base;
	f->pa_limit = pa_limit;
	memset(h, 0, sizeof(*h));
	h->ctx = f;
	h->power = fake_power;
	h->regs = fake_regs;
	h->wait_irq = fake_wait_irq;
	h->irq_clear = fake_irq_clear;
	h->dma_alloc = fake_dma_alloc;
	h->dma_free = fake_dma_free;
	h->dma_rw = fake_dma_rw;
	h->close = fake_close;
}

void hwfake_fail_power(struct hwfake *f, int on)
{
	pthread_mutex_lock(&f->lock);
	f->fail_power = on;
	pthread_mutex_unlock(&f->lock);
}

void hwfake_fail_wait(struct hwfake *f, int on)
{
	pthread_mutex_lock(&f->lock);
	f->fail_wait = on;
	pthread_mutex_unlock(&f->lock);
}

void hwfake_set_irq(struct hwfake *f, uint32_t status)
{
	pthread_mutex_lock(&f->lock);
	f->irq_pending = 1;
	f->irq_status = status;
	pthread_mutex_unlock(&f->lock);
}

uint32_t hwfake_refs(struct hwfake *f, unsigned block)
{
	uint32_t r;

	pthread_mutex_lock(&f->lock);
	r = block < RWVC_BLK_COUNT ? f->refs[block] : 0u;
	pthread_mutex_unlock(&f->lock);
	return r;
}

unsigned hwfake_events(struct hwfake *f, struct hwfake_event *out, unsigned cap)
{
	unsigned n;

	pthread_mutex_lock(&f->lock);
	n = f->nev < cap ? f->nev : cap;
	memcpy(out, f->ev, n * sizeof(*out));
	pthread_mutex_unlock(&f->lock);
	return n;
}

unsigned hwfake_count_events(struct hwfake *f, uint32_t kind)
{
	unsigned n = 0;

	pthread_mutex_lock(&f->lock);
	for (unsigned i = 0; i < f->nev; i++) {
		if (f->ev[i].kind == kind) {
			n++;
		}
	}
	pthread_mutex_unlock(&f->lock);
	return n;
}

int hwfake_dma_write(struct hwfake *f, uint32_t handle, uint32_t off, const void *src,
                     uint32_t n)
{
	return dma_copy(f, handle, off, (void *)src, n, 1);
}

int hwfake_dma_write_pa(struct hwfake *f, uint32_t pa, uint32_t off, const void *src, uint32_t n)
{
	struct hwfake *ff = f;
	uint32_t handle = 0;
	int found = 0;

	pthread_mutex_lock(&ff->lock);
	for (unsigned i = 0; i < HWFAKE_MAX_DMA; i++) {
		if (ff->dma[i].used && ff->dma[i].pa == pa) {
			handle = ff->dma[i].handle;
			found = 1;
			break;
		}
	}
	pthread_mutex_unlock(&ff->lock);
	return found ? dma_copy(f, handle, off, (void *)src, n, 1) : -ENOENT;
}

int hwfake_dma_read(struct hwfake *f, uint32_t handle, uint32_t off, void *dst, uint32_t n)
{
	return dma_copy(f, handle, off, dst, n, 0);
}
