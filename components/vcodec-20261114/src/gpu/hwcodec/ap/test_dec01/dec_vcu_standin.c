/* SPDX-License-Identifier: MIT */
/*
 * dec_vcu_standin.c -- see dec_vcu_standin.h.  One queued message per IPI id (the kernel's share
 * area per instance), the AP handler invoked on the caller's thread, and a DMEM window the tests can
 * map with n2vcu_map_dm_addr() (vcu_mapping_dm_addr).
 */
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dec_vcu_standin.h"

struct n2vcu *decv_create(struct n2a32_mem *mem, const struct n2vc_hw *hw, uint32_t dmem_bytes)
{
	struct n2vcu *v;
	uint32_t guest;

	if (mem == NULL || hw == NULL || dmem_bytes == 0u)
		return NULL;
	v = calloc(1, sizeof *v);
	if (v == NULL)
		return NULL;
	if (pthread_mutex_init(&v->lock, NULL) != 0) {
		free(v);
		return NULL;
	}
	if (pthread_cond_init(&v->cv, NULL) != 0) {
		pthread_mutex_destroy(&v->lock);
		free(v);
		return NULL;
	}
	v->mem = mem;
	v->hw = *hw;
	v->dmem_bytes = dmem_bytes;
	v->dmem = calloc(1, dmem_bytes);
	guest = n2a32_mem_alloc(mem, dmem_bytes, 0x1000u);
	if (v->dmem == NULL || guest == 0u) {
		free(v->dmem);
		pthread_cond_destroy(&v->cv);
		pthread_mutex_destroy(&v->lock);
		free(v);
		return NULL;
	}
	v->dmem_guest = guest;
	return v;
}

void decv_destroy(struct n2vcu *v)
{
	if (v == NULL)
		return;
	pthread_cond_destroy(&v->cv);
	pthread_mutex_destroy(&v->lock);
	free(v->dmem);
	free(v);
}

uint32_t decv_dmem_alloc(struct n2vcu *v, uint32_t bytes, uint32_t align)
{
	uint32_t off;

	if (v == NULL || bytes == 0u)
		return 0;
	if (align < 8u)
		align = 8u;   /* struct vdec_vsi is 64-bit aligned (facts/VDEC_PROTOCOL.md 2.2) */
	off = (v->dmem_used + align - 1u) & ~(align - 1u);
	if (off + bytes > v->dmem_bytes)
		return 0;
	v->dmem_used = off + bytes;
	return v->dmem_guest + off;
}

void *n2vcu_map_dm_addr(struct n2vcu *v, uint32_t guest_addr)
{
	if (v == NULL || guest_addr < v->dmem_guest)
		return NULL;
	if ((uint64_t)(guest_addr - v->dmem_guest) >= v->dmem_bytes)
		return NULL;
	return v->dmem + (guest_addr - v->dmem_guest);
}

int n2vcu_dmem(struct n2vcu *v, uint32_t *guest_addr, uint32_t *bytes)
{
	if (v == NULL)
		return -EINVAL;
	if (guest_addr != NULL)
		*guest_addr = v->dmem_guest;
	if (bytes != NULL)
		*bytes = v->dmem_bytes;
	return 0;
}

int n2vcu_ap_register(struct n2vcu *v, int ipi_id, n2vcu_ipi_handler handler, void *ctx)
{
	if (v == NULL || ipi_id < 0 || ipi_id >= DECV_IPI_MAX || handler == NULL)
		return -EINVAL;
	pthread_mutex_lock(&v->lock);
	v->fn[ipi_id] = handler;
	v->fn_ctx[ipi_id] = ctx;
	pthread_mutex_unlock(&v->lock);
	return 0;
}

int n2vcu_ap_send(struct n2vcu *v, int ipi_id, const void *buf, uint32_t len)
{
	if (v == NULL || buf == NULL || ipi_id < 0 || ipi_id >= DECV_IPI_MAX ||
	    len == 0u || len > DECV_SHARE_MAX)
		return -EINVAL;
	pthread_mutex_lock(&v->lock);
	if (v->q_pending[ipi_id])
		v->overwritten++;
	v->q[ipi_id].ipi_id = ipi_id;
	v->q[ipi_id].len = len;
	memcpy(v->q[ipi_id].buf, buf, len);
	v->q_pending[ipi_id] = 1;
	v->fetched[ipi_id] = 0;
	v->sends++;
	pthread_cond_broadcast(&v->cv);
	pthread_mutex_unlock(&v->lock);
	return 0;
}

int n2vcu_ap_pending(struct n2vcu *v, int ipi_id)
{
	int p;

	if (v == NULL || ipi_id < 0 || ipi_id >= DECV_IPI_MAX)
		return 0;
	pthread_mutex_lock(&v->lock);
	p = v->q_pending[ipi_id];
	pthread_mutex_unlock(&v->lock);
	return p;
}

int n2vcu_ap_wait_dispatched(struct n2vcu *v, int ipi_id, unsigned timeout_ms)
{
	struct timespec dl;
	uint64_t ns;
	int rc = 0;

	if (v == NULL || ipi_id < 0 || ipi_id >= DECV_IPI_MAX)
		return -EINVAL;
	if (clock_gettime(CLOCK_REALTIME, &dl) != 0)
		return -EIO;
	ns = (uint64_t)dl.tv_nsec + (uint64_t)timeout_ms * 1000000ull;
	dl.tv_sec += (time_t)(ns / 1000000000ull);
	dl.tv_nsec = (long)(ns % 1000000000ull);
	pthread_mutex_lock(&v->lock);
	while (!v->fetched[ipi_id] && rc == 0)
		rc = pthread_cond_timedwait(&v->cv, &v->lock, &dl);
	if (v->fetched[ipi_id])
		rc = 0;
	else if (rc == ETIMEDOUT)
		rc = -ETIMEDOUT;
	else if (rc != 0)
		rc = -EIO;
	pthread_mutex_unlock(&v->lock);
	return rc;
}

int decv_daemon_fetch(struct n2vcu *v, int ipi_id, struct decv_msg *m, unsigned timeout_ms)
{
	struct timespec dl;
	uint64_t ns;
	int rc = 0;

	if (v == NULL || m == NULL || ipi_id < 0 || ipi_id >= DECV_IPI_MAX)
		return -EINVAL;
	if (clock_gettime(CLOCK_REALTIME, &dl) != 0)
		return -EIO;
	ns = (uint64_t)dl.tv_nsec + (uint64_t)timeout_ms * 1000000ull;
	dl.tv_sec += (time_t)(ns / 1000000000ull);
	dl.tv_nsec = (long)(ns % 1000000000ull);
	pthread_mutex_lock(&v->lock);
	while (!v->q_pending[ipi_id] && rc == 0)
		rc = pthread_cond_timedwait(&v->cv, &v->lock, &dl);
	if (rc != 0 && !v->q_pending[ipi_id]) {
		pthread_mutex_unlock(&v->lock);
		return rc == ETIMEDOUT ? -ETIMEDOUT : -EIO;
	}
	*m = v->q[ipi_id];
	v->q_pending[ipi_id] = 0;
	v->fetched[ipi_id] = 1;
	v->fetches++;
	pthread_cond_broadcast(&v->cv);
	pthread_mutex_unlock(&v->lock);
	return 0;
}

int decv_daemon_fetch_any(struct n2vcu *v, struct decv_msg *m, unsigned timeout_ms)
{
	struct timespec dl;
	uint64_t ns;
	int rc = 0, id = -1, i;

	if (v == NULL || m == NULL)
		return -EINVAL;
	if (clock_gettime(CLOCK_REALTIME, &dl) != 0)
		return -EIO;
	ns = (uint64_t)dl.tv_nsec + (uint64_t)timeout_ms * 1000000ull;
	dl.tv_sec += (time_t)(ns / 1000000000ull);
	dl.tv_nsec = (long)(ns % 1000000000ull);
	pthread_mutex_lock(&v->lock);
	for (;;) {
		for (i = 1; i < DECV_IPI_MAX; i++) {
			if (v->q_pending[i]) {
				id = i;
				break;
			}
		}
		if (id >= 0 || rc != 0)
			break;
		rc = pthread_cond_timedwait(&v->cv, &v->lock, &dl);
	}
	if (id < 0) {
		pthread_mutex_unlock(&v->lock);
		return rc == ETIMEDOUT ? -ETIMEDOUT : -EIO;
	}
	*m = v->q[id];
	v->q_pending[id] = 0;
	v->fetched[id] = 1;
	v->fetches++;
	pthread_cond_broadcast(&v->cv);
	pthread_mutex_unlock(&v->lock);
	return 0;
}

int decv_daemon_reply(struct n2vcu *v, int ipi_id, struct decv_msg *m)
{
	n2vcu_ipi_handler fn;
	void *ctx;
	int rc;

	if (v == NULL || m == NULL || ipi_id < 0 || ipi_id >= DECV_IPI_MAX)
		return -EINVAL;
	pthread_mutex_lock(&v->lock);
	fn = v->fn[ipi_id];
	ctx = v->fn_ctx[ipi_id];
	pthread_mutex_unlock(&v->lock);
	if (fn == NULL) {
		v->no_handler++;
		return -ENOENT;
	}
	v->replies++;
	rc = fn(ctx, ipi_id, m->buf, m->len);
	return rc;
}

int decv_daemon_request(struct n2vcu *v, int ipi_id, struct decv_msg *m)
{
	n2vcu_ipi_handler fn;
	void *ctx;
	int rc;

	if (v == NULL || m == NULL || ipi_id < 0 || ipi_id >= DECV_IPI_MAX)
		return -EINVAL;
	pthread_mutex_lock(&v->lock);
	fn = v->fn[ipi_id];
	ctx = v->fn_ctx[ipi_id];
	pthread_mutex_unlock(&v->lock);
	if (fn == NULL) {
		v->no_handler++;
		return -ENOENT;
	}
	v->requests++;
	m->ipi_id = ipi_id;
	rc = fn(ctx, ipi_id, m->buf, m->len);
	return rc;
}
