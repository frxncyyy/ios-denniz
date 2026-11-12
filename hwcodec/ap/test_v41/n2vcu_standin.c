/* SPDX-License-Identifier: MIT */
/*
 * n2vcu_standin.c -- a host stand-in for V40's vcu/n2vcu.c, exactly the part of vcu/n2vcu.h that
 * ap/n2ap_venc.c uses, plus the two daemon-side entry points a scripted fake daemon needs
 * (n2vcu_test_get / n2vcu_test_set, declared in fake_daemon.h).
 *
 * Owner: V41 (test only).  Why a stand-in: V40's n2vcu.c was not in the tree when this package was
 * written.  run_tests.sh switches to the real file as soon as it exists; n2ap_venc.c includes the
 * real vcu/n2vcu.h either way, so the API cannot drift silently.
 *
 * What it reproduces from mtk_vcu.c (the kernel behaviour the AP code depends on):
 *  - one message slot per IPI *instance* id (VCU_VDEC 0 for ids < IPI_VENC_COMMON, VCU_VENC 1 for
 *    the encoder ids), user_obj[VCU_CODEC_MAX], mtk_vcu.c:118-125, :283-299;
 *  - AP -> daemon: vcu_ipi_send() stores {id, len, 80 bytes} and wakes the daemon
 *    (mtk_vcu.c:410-433).  A second send before the fetch overwrites, as the kernel does;
 *  - daemon -> AP: VCU_SET_OBJECT dispatches to the registered handler for that id
 *    (vcu_ipi_handler, mtk_vcu.c:1396-1410) and copies the (possibly modified) payload back;
 *  - "dispatched": vcu_ipi_send() also waits for ipi_done, which vcu_ipi_get() sets at the *entry*
 *    of the daemon's *next* GET_OBJECT (mtk_vcu.c:441-446, :478-530).  This stand-in only reports
 *    success when the message was fetched AND the daemon has come back for another one, which is
 *    what makes a daemon that never answers produce a timeout instead of a false success;
 *  - the DMEM window: one guest allocation, n2vcu_map_dm_addr() bounds-checks like
 *    vcu_mapping_dm_addr() (mtk_vcu.c:1199-1230).
 */
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "n2a32.h"
#include "n2vc_hw.h"
#include "vcu/n2vcu.h"

#include "fake_daemon.h"

#define N2VCU_DMEM_BYTES 0x80000u   /* VCU_SHMEM_SIZE, mtk_vcu.c:2542 */
#define N2VCU_IPI_MAX 50
#define N2VCU_SLOTS 2

struct n2vcu_slot {
	uint8_t buf[80];
	uint32_t len;
	int id;
	int queued;
	uint64_t sent_gen;
	uint64_t fetched_gen;
	uint64_t entries_at_fetch;
	uint64_t get_entries;
};

/* ipi_desc[] in the kernel is per IPI id (mtk_vcu.c:281), while user_obj[] is per instance slot;
 * the two are different tables and mixing them would let IPI_VENC_COMMON overwrite the H.264
 * session handler (both live in slot 1). */
struct n2vcu_handler {
	n2vcu_ipi_handler fn;
	void *ctx;
	int set;
};

struct n2vcu {
	struct n2a32_mem *mem;
	struct n2vc_hw hw;
	uint32_t dmem;
	uint32_t dmem_len;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	struct n2vcu_slot slot[N2VCU_SLOTS];
	struct n2vcu_handler handler[N2VCU_IPI_MAX];
	unsigned get_timeout_ms;
	int aborted;
	uint64_t set_object, get_object;
	uint64_t handler_calls, handler_acks;
};

static struct n2vcu_slot *slot_of(struct n2vcu *v, int ipi_id)
{
	/* ipi_id_to_inst_id(), mtk_vcu.c:118-125 */
	(void)v;
	if (ipi_id < 0) {
		return NULL;
	}
	return ipi_id >= 14 ? &v->slot[1] : &v->slot[0];
}

struct n2vcu *n2vcu_create(struct n2a32_mem *mem, const struct n2vc_hw *hw)
{
	struct n2vcu *v;

	if (mem == NULL || hw == NULL || hw->dma_alloc == NULL) {
		return NULL;
	}
	v = calloc(1, sizeof(*v));
	if (v == NULL) {
		return NULL;
	}
	v->mem = mem;
	v->hw = *hw;
	v->get_timeout_ms = 10000u;
	if (pthread_mutex_init(&v->lock, NULL) != 0) {
		free(v);
		return NULL;
	}
	if (pthread_cond_init(&v->cond, NULL) != 0) {
		pthread_mutex_destroy(&v->lock);
		free(v);
		return NULL;
	}
	v->dmem = n2a32_mem_alloc(mem, N2VCU_DMEM_BYTES, 0x1000u);
	if (v->dmem == 0u) {
		pthread_cond_destroy(&v->cond);
		pthread_mutex_destroy(&v->lock);
		free(v);
		return NULL;
	}
	v->dmem_len = N2VCU_DMEM_BYTES;
	return v;
}

void n2vcu_destroy(struct n2vcu *v)
{
	if (v == NULL) {
		return;
	}
	pthread_cond_destroy(&v->cond);
	pthread_mutex_destroy(&v->lock);
	free(v);
}

int n2vcu_ap_register(struct n2vcu *v, int ipi_id, n2vcu_ipi_handler handler, void *ctx)
{
	if (v == NULL || handler == NULL || ipi_id <= 0 || ipi_id >= N2VCU_IPI_MAX) {
		return -EINVAL;
	}
	pthread_mutex_lock(&v->lock);
	v->handler[ipi_id].fn = handler;
	v->handler[ipi_id].ctx = ctx;
	v->handler[ipi_id].set = 1;
	pthread_mutex_unlock(&v->lock);
	return 0;
}

int n2vcu_ap_send(struct n2vcu *v, int ipi_id, const void *buf, uint32_t len)
{
	struct n2vcu_slot *s;

	if (v == NULL || buf == NULL || len > 80u || ipi_id <= 0 || ipi_id >= N2VCU_IPI_MAX) {
		return -EINVAL;
	}
	s = slot_of(v, ipi_id);
	pthread_mutex_lock(&v->lock);
	if (v->aborted) {
		pthread_mutex_unlock(&v->lock);
		return -EINTR;
	}
	memcpy(s->buf, buf, len);
	memset(s->buf + len, 0, sizeof(s->buf) - len);
	s->len = len;
	s->id = ipi_id;
	s->queued = 1;
	s->sent_gen++;
	pthread_cond_broadcast(&v->cond);
	pthread_mutex_unlock(&v->lock);
	return 0;
}

int n2vcu_ap_pending(struct n2vcu *v, int ipi_id)
{
	struct n2vcu_slot *s;
	int r;

	if (v == NULL || ipi_id <= 0 || ipi_id >= N2VCU_IPI_MAX) {
		return 0;
	}
	s = slot_of(v, ipi_id);
	pthread_mutex_lock(&v->lock);
	r = s->queued;
	pthread_mutex_unlock(&v->lock);
	return r;
}

static void deadline_after(struct timespec *ts, unsigned ms)
{
	clock_gettime(CLOCK_REALTIME, ts);
	ts->tv_sec += (time_t)(ms / 1000u);
	ts->tv_nsec += (long)(ms % 1000u) * 1000000L;
	if (ts->tv_nsec >= 1000000000L) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
}

int n2vcu_ap_wait_dispatched(struct n2vcu *v, int ipi_id, unsigned timeout_ms)
{
	struct n2vcu_slot *s;
	struct timespec dl;
	uint64_t gen;
	int rc = 0;

	if (v == NULL || ipi_id <= 0 || ipi_id >= N2VCU_IPI_MAX) {
		return -EINVAL;
	}
	s = slot_of(v, ipi_id);
	pthread_mutex_lock(&v->lock);
	gen = s->sent_gen;
	deadline_after(&dl, timeout_ms == 0u ? v->get_timeout_ms : timeout_ms);
	for (;;) {
		if (v->aborted) {
			rc = -EINTR;
			break;
		}
		if (s->fetched_gen >= gen && s->get_entries > s->entries_at_fetch) {
			break;
		}
		rc = pthread_cond_timedwait(&v->cond, &v->lock, &dl);
		if (rc == ETIMEDOUT) {
			rc = -ETIMEDOUT;
			break;
		}
		if (rc != 0) {
			rc = -EINTR;
			break;
		}
	}
	pthread_mutex_unlock(&v->lock);
	return rc;
}

void *n2vcu_map_dm_addr(struct n2vcu *v, uint32_t guest_addr)
{
	if (v == NULL || guest_addr == 0u || guest_addr < v->dmem ||
	    (uint64_t)guest_addr - v->dmem >= v->dmem_len) {
		return NULL;
	}
	return n2a32_ptr(v->mem, guest_addr);
}

int n2vcu_dmem(struct n2vcu *v, uint32_t *guest_addr, uint32_t *bytes)
{
	if (v == NULL) {
		return -EINVAL;
	}
	if (guest_addr != NULL) {
		*guest_addr = v->dmem;
	}
	if (bytes != NULL) {
		*bytes = v->dmem_len;
	}
	return 0;
}

void n2vcu_set_get_timeout_ms(struct n2vcu *v, unsigned ms)
{
	if (v != NULL) {
		v->get_timeout_ms = ms;
	}
}

void n2vcu_set_irq_timeout_ms(struct n2vcu *v, unsigned ms) { (void)v; (void)ms; }
void n2vcu_set_poll_timeout_ms(struct n2vcu *v, unsigned ms) { (void)v; (void)ms; }
void n2vcu_set_callback_timeout_ms(struct n2vcu *v, unsigned ms) { (void)v; (void)ms; }
void n2vcu_set_gce_threads(struct n2vcu *v, unsigned d, unsigned e) { (void)v; (void)d; (void)e; }

void n2vcu_abort(struct n2vcu *v)
{
	if (v == NULL) {
		return;
	}
	pthread_mutex_lock(&v->lock);
	v->aborted = 1;
	pthread_cond_broadcast(&v->cond);
	pthread_mutex_unlock(&v->lock);
}

void n2vcu_stats(const struct n2vcu *v, struct n2vcu_stats *out)
{
	if (out == NULL) {
		return;
	}
	memset(out, 0, sizeof(*out));
	if (v != NULL) {
		out->set_object = v->set_object;
		out->get_object = v->get_object;
		out->ipi_acks = v->handler_acks;
	}
}

/* ---- the daemon side (test only; fake_daemon.h) -------------------------------------------- */

void n2vcu_test_reget(struct n2vcu *v, int ipi_id)
{
	struct n2vcu_slot *s;

	if (v == NULL || ipi_id <= 0 || ipi_id >= N2VCU_IPI_MAX) {
		return;
	}
	s = slot_of(v, ipi_id);
	pthread_mutex_lock(&v->lock);
	s->get_entries++;
	v->get_object++;
	pthread_cond_broadcast(&v->cond);   /* the AP's ipi_done half */
	pthread_mutex_unlock(&v->lock);
}

int n2vcu_test_get(struct n2vcu *v, int ipi_id, int *out_id, void *buf, uint32_t cap,
                   uint32_t *out_len, unsigned timeout_ms)
{
	struct n2vcu_slot *s;
	struct timespec dl;
	int rc = 0;

	if (v == NULL || buf == NULL || cap < 80u) {
		return -EINVAL;
	}
	s = slot_of(v, ipi_id);
	pthread_mutex_lock(&v->lock);
	s->get_entries++;
	v->get_object++;
	pthread_cond_broadcast(&v->cond);   /* the AP's ipi_done half */
	deadline_after(&dl, timeout_ms == 0u ? v->get_timeout_ms : timeout_ms);
	while (!s->queued) {
		if (v->aborted) {
			rc = -EINTR;
			break;
		}
		rc = pthread_cond_timedwait(&v->cond, &v->lock, &dl);
		if (rc == ETIMEDOUT) {
			rc = -ETIMEDOUT;
			break;
		}
		if (rc != 0) {
			rc = -EINTR;
			break;
		}
	}
	if (rc == 0) {
		memcpy(buf, s->buf, 80u);
		if (out_id != NULL) {
			*out_id = s->id;
		}
		if (out_len != NULL) {
			*out_len = s->len;
		}
		s->fetched_gen = s->sent_gen;
		s->entries_at_fetch = s->get_entries;
		s->queued = 0;
	}
	pthread_mutex_unlock(&v->lock);
	return rc;
}

int n2vcu_test_set(struct n2vcu *v, int ipi_id, void *buf, uint32_t len)
{
	n2vcu_ipi_handler h = NULL;
	void *ctx = NULL;
	int rc;

	if (v == NULL || buf == NULL || len > 80u || ipi_id <= 0 || ipi_id >= N2VCU_IPI_MAX) {
		return -EINVAL;
	}
	pthread_mutex_lock(&v->lock);
	v->set_object++;
	if (v->handler[ipi_id].set) {
		h = v->handler[ipi_id].fn;
		ctx = v->handler[ipi_id].ctx;
	}
	pthread_mutex_unlock(&v->lock);
	if (h == NULL) {
		return -ENOENT;
	}
	/* vcu_ipi_handler: the handler gets its own copy; the kernel then copies the modified
	 * share_obj back to the daemon. */
	rc = h(ctx, ipi_id, buf, len);
	pthread_mutex_lock(&v->lock);
	v->handler_calls++;
	if (rc == 0) {
		v->handler_acks++;
	}
	pthread_mutex_unlock(&v->lock);
	return rc;
}

struct n2a32_mem *n2vcu_test_mem(struct n2vcu *v)
{
	return v != NULL ? v->mem : NULL;
}

uint32_t n2vcu_test_dmem(struct n2vcu *v)
{
	return v != NULL ? v->dmem : 0u;
}

void n2vcu_test_counters(struct n2vcu *v, uint64_t *set, uint64_t *get, uint64_t *calls,
                         uint64_t *acks)
{
	if (v == NULL) {
		return;
	}
	pthread_mutex_lock(&v->lock);
	if (set != NULL) {
		*set = v->set_object;
	}
	if (get != NULL) {
		*get = v->get_object;
	}
	if (calls != NULL) {
		*calls = v->handler_calls;
	}
	if (acks != NULL) {
		*acks = v->handler_acks;
	}
	pthread_mutex_unlock(&v->lock);
}
