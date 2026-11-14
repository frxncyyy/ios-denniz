/* SPDX-License-Identifier: MIT */
/*
 * n2vc_hw.h -- FROZEN: the hardware back end seen by the codec host (the /dev/vcu model, the ion shims, the AP-side driver).
 * Owner: hwcodec-lead. Two implementations: host/fake (T02: register model + recorder, used by every PC test) and
 * phone/real (P03: ioctls of include/redwood_vcodec.h). Everything above this line of the stack is identical on PC and phone.
 */
#ifndef N2VC_HW_H
#define N2VC_HW_H
#include <stdint.h>
#include "redwood_vcodec.h"

struct n2vc_hw {
	void *ctx;
	int (*power)(void *ctx, unsigned block, int on);                                    /* RWVC_BLK_*; 0 or -errno */
	int (*regs)(void *ctx, struct rwvc_reg_op *ops, unsigned count, unsigned *done);    /* as RWVC_REGS */
	int (*wait_irq)(void *ctx, unsigned irq, unsigned timeout_ms, uint32_t *status);    /* 0, -ETIMEDOUT, -errno */
	int (*irq_clear)(void *ctx, unsigned irq);
	int (*dma_alloc)(void *ctx, uint32_t bytes, uint32_t flags, uint32_t *handle, uint32_t *pa);
	int (*dma_free)(void *ctx, uint32_t handle);
	int (*dma_rw)(void *ctx, uint32_t handle, uint32_t offset, void *host, uint32_t bytes, int write);
	void (*close)(void *ctx);
};
static inline uint32_t n2vc_hw_rd(const struct n2vc_hw *h, unsigned win, uint32_t off)
{ struct rwvc_reg_op o = {RWVC_OP_READ, (uint8_t)win, 0, off, 0, 0}; unsigned d; return h->regs(h->ctx, &o, 1, &d) ? 0 : o.value; }
static inline int n2vc_hw_wr(const struct n2vc_hw *h, unsigned win, uint32_t off, uint32_t v)
{ struct rwvc_reg_op o = {RWVC_OP_WRITE, (uint8_t)win, 0, off, v, 0}; unsigned d; return h->regs(h->ctx, &o, 1, &d); }

int n2vc_hw_open_fake(struct n2vc_hw *out, const char *record_path);   /* T02; record_path NULL = no recording */
int n2vc_hw_open_dev(struct n2vc_hw *out, const char *dev_path);       /* P03; "/dev/redwood-vcodec" */

/*
 * Guest DMA mirror (V41). The vendor code touches DMA buffers through guest pointers, the hardware through `pa`. A guest
 * mapping of a DMA buffer is plain guest memory plus this registry; the AP-side driver calls n2vc_dma_sync() around every
 * hardware job (to_device before the trigger, from_device after the done interrupt) for the buffers the job uses.
 */
struct n2a32_mem;
int n2vc_dma_guest_map(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t handle, uint32_t bytes, uint32_t *guest_addr);
int n2vc_dma_guest_unmap(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t guest_addr);
int n2vc_dma_sync(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t guest_addr, uint32_t offset, uint32_t bytes, int to_device);
int n2vc_dma_lookup_pa(uint32_t pa, uint32_t *handle, uint32_t *guest_addr, uint32_t *bytes);   /* which buffer holds this hardware address */
#endif
