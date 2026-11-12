/* SPDX-License-Identifier: MIT */
/* hw_fake.h -- a recording n2vc_hw for the V41 host test (power reference counts, DMA pool,
 * interrupt injection, an ordered event trace and failure injection). */
#ifndef V41_HW_FAKE_H
#define V41_HW_FAKE_H
#include <pthread.h>
#include <stdint.h>
#include "n2vc_hw.h"

#define HWFAKE_MAX_DMA 64u
#define HWFAKE_MAX_EVENTS 512u

enum hwfake_ev_kind {
	HWFAKE_EV_POWER = 1,
	HWFAKE_EV_WAIT_IRQ,
	HWFAKE_EV_IRQ_CLEAR,
	HWFAKE_EV_DMA_ALLOC,
	HWFAKE_EV_DMA_FREE,
	HWFAKE_EV_DMA_RW
};

struct hwfake_event {
	uint32_t kind;
	uint32_t a;
	uint32_t b;
	uint32_t c;
};

struct hwfake {
	pthread_mutex_t lock;
	uint32_t refs[RWVC_BLK_COUNT];
	uint32_t power_calls;
	uint32_t violations;
	int fail_power;          /* VENC power-on returns -EIO */
	int fail_wait;           /* wait_irq returns -EIO */
	int irq_pending;
	uint32_t irq_status;
	uint32_t wait_irq_calls;
	uint32_t wait_irq_timeouts;
	struct {
		int used;
		uint32_t handle;
		uint32_t pa;
		uint32_t bytes;
		uint8_t *host;
	} dma[HWFAKE_MAX_DMA];
	uint32_t next_handle;
	uint32_t next_pa;
	uint32_t pa_limit;
	struct hwfake_event ev[HWFAKE_MAX_EVENTS];
	unsigned nev;
};

void hwfake_init(struct n2vc_hw *h, struct hwfake *f, uint32_t pa_base, uint32_t pa_limit);
void hwfake_fail_power(struct hwfake *f, int on);
void hwfake_fail_wait(struct hwfake *f, int on);
void hwfake_set_irq(struct hwfake *f, uint32_t status);
uint32_t hwfake_refs(struct hwfake *f, unsigned block);
unsigned hwfake_events(struct hwfake *f, struct hwfake_event *out, unsigned cap);
unsigned hwfake_count_events(struct hwfake *f, uint32_t kind);
int hwfake_dma_write(struct hwfake *f, uint32_t handle, uint32_t off, const void *src, uint32_t n);
int hwfake_dma_write_pa(struct hwfake *f, uint32_t pa, uint32_t off, const void *src, uint32_t n);
int hwfake_dma_read(struct hwfake *f, uint32_t handle, uint32_t off, void *dst, uint32_t n);

#endif
