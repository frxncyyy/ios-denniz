/* SPDX-License-Identifier: MIT */
/*
 * dec_hw_model.c -- see dec_hw_model.h.  A register file per frozen window, the reference-counted
 * power model of the kernel's blocks (a VDEC_CORE reference holds VDEC_SOC, exactly the "hidden
 * parent reference" of kern/bsd/dev/nord2_vcodec.inc and of T02's fake), and a DMA pool.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "dec_hw_model.h"

/* Which power block each window needs (redwood_vcodec.h / kern/bsd/dev/nord2_vcodec.inc). */
static const struct {
	const char *name;
	uint32_t bytes;
	unsigned blk;
} DECM_WIN[RWVC_WIN_COUNT] = {
	[RWVC_WIN_VENC]          = { "VENC", 0x2000, RWVC_BLK_VENC },
	[RWVC_WIN_VENC_C1]       = { "VENC_C1", 0x2000, RWVC_BLK_VENC_C1 },
	[RWVC_WIN_VDEC_SYS]      = { "VDEC_SYS", 0x1000, RWVC_BLK_VDEC_CORE },
	[RWVC_WIN_VDEC_BASE]     = { "VDEC_BASE", 0x1000, RWVC_BLK_VDEC_CORE },
	[RWVC_WIN_VDEC_VLD]      = { "VDEC_VLD", 0x1000, RWVC_BLK_VDEC_CORE },
	[RWVC_WIN_VDEC_MC]       = { "VDEC_MC", 0x1000, RWVC_BLK_VDEC_CORE },
	[RWVC_WIN_VDEC_MV]       = { "VDEC_MV", 0x1000, RWVC_BLK_VDEC_CORE },
	[RWVC_WIN_VDEC_MISC]     = { "VDEC_MISC", 0x4000, RWVC_BLK_VDEC_CORE },
	[RWVC_WIN_VDEC_LAT_MISC] = { "VDEC_LAT_MISC", 0x1000, RWVC_BLK_VDEC_SOC },
	[RWVC_WIN_VDEC_LAT_VLD]  = { "VDEC_LAT_VLD", 0x0400, RWVC_BLK_VDEC_SOC },
	[RWVC_WIN_VDEC_RACING]   = { "VDEC_RACING", 0x1000, RWVC_BLK_VDEC_SOC },
	[RWVC_WIN_VDEC_SOC_GCON] = { "VDEC_SOC_GCON", 0x1000, RWVC_BLK_VDEC_SOC },
};

static int decm_power(void *ctx, unsigned blk, int on)
{
	struct decm *m = ctx;
	unsigned parent = RWVC_BLK_COUNT;

	if (m == NULL || blk >= RWVC_BLK_COUNT)
		return -EINVAL;
	if (blk == RWVC_BLK_VENC_C1)
		parent = RWVC_BLK_VENC;
	else if (blk == RWVC_BLK_VDEC_CORE)
		parent = RWVC_BLK_VDEC_SOC;
	if (on) {
		if (m->user_refs[blk] == 0)
			m->power_on_calls[blk]++;
		m->user_refs[blk]++;
		if (parent != RWVC_BLK_COUNT)
			m->hidden_refs[parent]++;
		return 0;
	}
	if (m->user_refs[blk] == 0) {
		m->power_denied++;
		return -EINVAL;
	}
	m->user_refs[blk]--;
	m->power_off_calls[blk]++;
	if (parent != RWVC_BLK_COUNT && m->hidden_refs[parent] > 0)
		m->hidden_refs[parent]--;
	return 0;
}

static int decm_regs(void *ctx, struct rwvc_reg_op *ops, unsigned count, unsigned *done)
{
	struct decm *m = ctx;
	unsigned i;

	if (m == NULL || (count != 0 && ops == NULL)) {
		if (done)
			*done = 0;
		return -EINVAL;
	}
	for (i = 0; i < count; i++) {
		struct rwvc_reg_op *o = &ops[i];
		unsigned win = o->window;
		unsigned blk;

		if (win >= RWVC_WIN_COUNT) {
			m->unknown_window++;
			if (done)
				*done = i;
			return -EINVAL;
		}
		blk = (win == RWVC_WIN_VDEC_RACING) ? m->racing_blk : DECM_WIN[win].blk;
		if (o->offset + 4u > DECM_WIN[win].bytes) {
			m->unknown_window++;
			if (done)
				*done = i;
			return -EINVAL;
		}
		if (!(m->user_refs[blk] != 0 || m->hidden_refs[blk] != 0)) {
			m->access_denied++;
			if (done)
				*done = i;
			return -EACCES;   /* the frozen ABI's rule: the window's block must be powered */
		}
		{
			uint32_t *cell = &m->reg[win][o->offset / 4u];

			switch (o->op) {
			case RWVC_OP_READ:
				o->value = *cell;
				m->reads++;
				break;
			case RWVC_OP_WRITE:
			case RWVC_OP_WRITE_MASK:
				if (o->op == RWVC_OP_WRITE)
					*cell = o->value;
				else
					*cell = (*cell & ~o->mask) | (o->value & o->mask);
				m->writes++;
				/* The one piece of hardware behaviour the model needs: the SMI-style
				 * acknowledgement of the decoder's HW break.  mtk_vcodec_dec_pm.c:330-340
				 * sets bit 0 of WIN+0x100 and polls WIN+0x104 for 0x11; the real block
				 * raises those bits when it has drained. */
				if ((o->value & 1u) != 0u) {
					if (win == RWVC_WIN_VDEC_MISC && o->offset == 0x100u) {
						m->reg[win][0x104u / 4u] = m->hwbreak_done;
						m->hwbreak_acks++;
					} else if (win == RWVC_WIN_VDEC_LAT_MISC && o->offset == 0x100u) {
						m->reg[win][0x104u / 4u] = m->hwbreak_done;
						m->hwbreak_acks++;
					}
				}
				break;
			case RWVC_OP_POLL: {
				unsigned guard = 0;

				m->polls++;
				while ((*cell & o->mask) != (o->value & o->mask)) {
					/* The one scripted behaviour: the HW-break ack. */
					if (o->offset == 0x104u) {
						m->hwbreak_polls++;
						*cell = m->hwbreak_done;
						if ((*cell & o->mask) == (o->value & o->mask))
							break;
					}
					if (++guard > 1000000u) {
						if (done)
							*done = i;
						return -ETIMEDOUT;
					}
				}
				break;
			}
			case RWVC_OP_DELAY_US:
				m->delays++;
				break;
			default:
				if (done)
					*done = i;
				return -EINVAL;
			}
		}
		m->reg_ops++;
		m->win_ops[win]++;
	}
	if (done)
		*done = count;
	return 0;
}

static int decm_wait_irq(void *ctx, unsigned irq, unsigned timeout_ms, uint32_t *status)
{
	struct decm *m = ctx;

	if (m == NULL)
		return -EINVAL;
	m->irq_timeout_ms = timeout_ms;
	m->irq_waits++;
	m->irq = irq;
	if (m->irq_timeout_after) {
		m->irq_timeout_after = 0;
		return -ETIMEDOUT;
	}
	if (status != NULL)
		*status = m->irq_status_value;
	return 0;
}

static int decm_irq_clear(void *ctx, unsigned irq)
{
	struct decm *m = ctx;

	if (m == NULL)
		return -EINVAL;
	(void)irq;
	m->irq_clear_calls++;
	return 0;
}

static int decm_dma_alloc(void *ctx, uint32_t bytes, uint32_t flags, uint32_t *handle, uint32_t *pa)
{
	struct decm *m = ctx;
	unsigned i;

	(void)flags;
	if (m == NULL || bytes == 0 || handle == NULL || pa == NULL)
		return -EINVAL;
	for (i = 0; i < DECM_DMA; i++) {
		if (!m->dma[i].used) {
			m->dma[i].bytes = (bytes + 0xfffu) & ~0xfffu;
			m->dma[i].host = calloc(1, m->dma[i].bytes);
			if (m->dma[i].host == NULL)
				return -ENOMEM;
			m->dma[i].handle = ++m->next_handle;   /* never reuse a handle: the DMA registry
			                                        * above the back end keys buffers by it */
			if (m->dma[i].handle == 0u)
				m->dma[i].handle = ++m->next_handle;
			m->dma[i].pa = m->next_pa;
			m->next_pa += m->dma[i].bytes;
			m->dma[i].used = 1;
			*handle = m->dma[i].handle;
			*pa = m->dma[i].pa;
			return 0;
		}
	}
	return -ENOMEM;
}

static int decm_dma_free(void *ctx, uint32_t handle)
{
	struct decm *m = ctx;
	unsigned i;

	if (m == NULL || handle == 0)
		return -EINVAL;
	for (i = 0; i < DECM_DMA; i++) {
		if (m->dma[i].used && m->dma[i].handle == handle) {
			free(m->dma[i].host);
			memset(&m->dma[i], 0, sizeof m->dma[i]);
			return 0;
		}
	}
	return -EINVAL;
}

static int decm_dma_rw(void *ctx, uint32_t handle, uint32_t off, void *host, uint32_t bytes, int write)
{
	struct decm *m = ctx;
	unsigned i;

	if (m == NULL || handle == 0 || host == NULL)
		return -EINVAL;
	for (i = 0; i < DECM_DMA; i++) {
		if (m->dma[i].used && m->dma[i].handle == handle)
			break;
	}
	if (i == DECM_DMA || off + bytes > m->dma[i].bytes)
		return -EINVAL;
	if (write) {
		memcpy(m->dma[i].host + off, host, bytes);
		m->dma_rw_to_dev++;
	} else {
		memcpy(host, m->dma[i].host + off, bytes);
		m->dma_rw_from_dev++;
	}
	return 0;
}

/* Handles must be unique for the whole process, not just this model: ap/n2vc_dma.c's registry is a
 * process global that outlives a fixture, and it keys buffers by handle. */
static uint32_t g_handle_seq;

void decm_init(struct decm *m, uint32_t pa_base)
{
	memset(m, 0, sizeof *m);
	m->next_handle = __atomic_add_fetch(&g_handle_seq, 16u, __ATOMIC_RELAXED);
	m->hw.ctx = m;
	m->hw.power = decm_power;
	m->hw.regs = decm_regs;
	m->hw.wait_irq = decm_wait_irq;
	m->hw.irq_clear = decm_irq_clear;
	m->hw.dma_alloc = decm_dma_alloc;
	m->hw.dma_free = decm_dma_free;
	m->hw.dma_rw = decm_dma_rw;
	m->hw.close = NULL;
	m->next_pa = pa_base != 0 ? pa_base : 0xda800000u;
	m->irq_status_value = 0x00010000u;
	m->racing_blk = RWVC_BLK_VDEC_CORE;   /* the frozen mapping */
	m->hwbreak_done = 0x11u;
}

void decm_destroy(struct decm *m)
{
	unsigned i;

	if (m == NULL)
		return;
	for (i = 0; i < DECM_DMA; i++) {
		if (m->dma[i].used) {
			free(m->dma[i].host);
			memset(&m->dma[i], 0, sizeof m->dma[i]);
		}
	}
}

unsigned decm_window_ops(const struct decm *m, unsigned win)
{
	return win < RWVC_WIN_COUNT ? m->win_ops[win] : 0u;
}

uint32_t decm_get(const struct decm *m, unsigned win, uint32_t off)
{
	if (win >= RWVC_WIN_COUNT || off + 4u > DECM_WIN[win].bytes)
		return 0;
	return m->reg[win][off / 4u];
}

void decm_set(struct decm *m, unsigned win, uint32_t off, uint32_t v)
{
	if (win < RWVC_WIN_COUNT && off + 4u <= DECM_WIN[win].bytes)
		m->reg[win][off / 4u] = v;
}

unsigned decm_power_on(const struct decm *m, unsigned blk)
{
	return blk < RWVC_BLK_COUNT ? m->power_on_calls[blk] : 0u;
}

unsigned decm_power_off(const struct decm *m, unsigned blk)
{
	return blk < RWVC_BLK_COUNT ? m->power_off_calls[blk] : 0u;
}

int decm_is_on(const struct decm *m, unsigned blk)
{
	if (blk >= RWVC_BLK_COUNT)
		return 0;
	return m->user_refs[blk] != 0 || m->hidden_refs[blk] != 0;
}

int decm_all_off(const struct decm *m)
{
	unsigned i;

	for (i = 0; i < RWVC_BLK_COUNT; i++)
		if (m->user_refs[i] != 0 || m->hidden_refs[i] != 0)
			return 0;
	return 1;
}
