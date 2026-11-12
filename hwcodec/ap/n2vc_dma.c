/* SPDX-License-Identifier: MIT */
/*
 * n2vc_dma.c -- the guest DMA mirror of include/n2vc_hw.h: a registry of {handle, physical
 * address, bytes, guest address} plus the copy direction of a hardware job.
 *
 * Owner: V41 (gpu/hwcodec/ap).  Consumers: the daemon's ion shim (V34), the /dev/vcu model (V40),
 * n2ap_venc.c and the host runner (IV).
 *
 * Why a registry at all: the vendor code addresses its buffers with guest pointers, the hardware
 * with 32-bit physical addresses (the larb ports run with the IOMMU bypassed).  A guest mapping is
 * plain guest memory from n2a32_mem_alloc() plus this table; n2vc_dma_sync() copies between the
 * two through hw->dma_rw(), in the direction the caller states.  The AP side calls to_device=1
 * before it hands a buffer to the hardware and to_device=0 after the done interrupt; the frozen
 * n2vc_hw.h asks for exactly that ("to_device before the trigger, from_device after the done
 * interrupt").
 *
 * Interface limitation (posted to hw-lead): the frozen n2vc_dma_guest_map() takes a handle but no
 * physical address, so although it can create a guest mapping (and this implementation does, for
 * the /dev/vcu model's VCU_PA_ALLOCATION path), it cannot record the `pa` that n2vc_dma_lookup_pa()
 * needs.  n2vc_dma_guest_register()/n2vc_dma_guest_alloc() (declared in ap/n2ap_venc.h) supply it;
 * register() on an already-mapped handle upgrades the entry in place and keeps the same guest
 * address.  register() with m == NULL records a pa-only entry (no guest address; the path vcodecd
 * takes for its client buffers), which guest_map() can later upgrade with a guest mapping and
 * n2vc_dma_guest_forget() releases.  Entries created by guest_map() alone are invisible to
 * lookup_pa() by design.
 *
 * Threading: one process-global table behind one mutex; every entry point is safe to call from any
 * thread.  The table is 256 entries, enough for 32 in-flight frame buffers with a plane or two
 * each; a full table is -ENOMEM, never silent reuse.  There is no n2a32_mem_free() in the frozen
 * n2a32.h, so unmap() releases the DMA handle (when this table allocated it) and the table slot,
 * but not the guest address space; a long-running daemon that maps and unmaps thousands of buffers
 * would exhaust the 4 GiB guest address space (documented in dsh-state/workers/V41/STATUS.md).
 */
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "n2a32.h"
#include "n2ap_venc.h"
#include "n2vc_hw.h"

#define N2VC_DMA_SLOTS 256u

struct n2vc_dma_entry {
	uint32_t handle;
	uint32_t pa;
	uint32_t bytes;
	uint32_t guest;
	uint32_t guest_bytes;   /* page-rounded allocation behind guest */
	struct n2a32_mem *mem;  /* address space the guest address lives in (n2ap_venc passes NULL) */
	uint8_t used;
	uint8_t owns;           /* 1: this table allocated the DMA handle (guest_alloc) */
	uint8_t ap_owned;       /* 1: registered by the AP (guest_alloc/guest_register); a guest
	                         * ion_munmap only drops the importer's view, not the entry */
	uint8_t pa_known;       /* 0: mapped by guest_map() without a physical address */
};

static struct {
	pthread_mutex_t lock;
	struct n2vc_dma_entry e[N2VC_DMA_SLOTS];
} n2vc_dma_state = { PTHREAD_MUTEX_INITIALIZER, { { 0 } } };

static uint32_t n2vc_dma_page_up(uint32_t bytes)
{
	if (bytes == 0u || bytes > 0xfffff000u) {
		return 0;
	}
	return (bytes + 0xfffu) & ~0xfffu;
}

static struct n2vc_dma_entry *n2vc_dma_find_handle_locked(uint32_t handle)
{
	for (unsigned i = 0; i < N2VC_DMA_SLOTS; i++) {
		if (n2vc_dma_state.e[i].used && n2vc_dma_state.e[i].handle == handle) {
			return &n2vc_dma_state.e[i];
		}
	}
	return NULL;
}

static struct n2vc_dma_entry *n2vc_dma_find_guest_locked(uint32_t guest)
{
	for (unsigned i = 0; i < N2VC_DMA_SLOTS; i++) {
		struct n2vc_dma_entry *d = &n2vc_dma_state.e[i];

		if (d->used && guest >= d->guest && guest < d->guest + d->guest_bytes) {
			return d;
		}
	}
	return NULL;
}

static int n2vc_dma_register_internal(const struct n2vc_hw *h, struct n2a32_mem *m,
                                      uint32_t handle, uint32_t pa, uint32_t bytes, int owns,
                                      uint32_t *guest_addr)
{
	struct n2vc_dma_entry *slot = NULL, *old;
	uint32_t guest = 0, span;
	int rc = 0;

	if (h == NULL || handle == 0u || bytes == 0u) {
		return -EINVAL;
	}
	span = n2vc_dma_page_up(bytes);
	if (span == 0u) {
		return -EINVAL;
	}

	pthread_mutex_lock(&n2vc_dma_state.lock);
	old = n2vc_dma_find_handle_locked(handle);
	if (old != NULL) {
		/* Idempotent for the same buffer.  An entry created by guest_map() or by a pa-only
		 * register() has no guest mapping, or none yet; registering fills the pa in and
		 * keeps the guest address, which is the path the /dev/vcu model takes for its own
		 * VCU_PA_ALLOCATION buffers.  `m == NULL` only adds the pa, so it never conflicts
		 * with an existing address space. */
		if (old->bytes < bytes || (old->pa_known && old->pa != pa) ||
		    (m != NULL && old->guest != 0u && old->mem != m)) {
			pthread_mutex_unlock(&n2vc_dma_state.lock);
			return -EEXIST;
		}
		if (!old->pa_known) {
			old->pa = pa;
			old->pa_known = 1;
		}
		if (owns) {
			old->owns = 1;
		}
		old->ap_owned = 1;
		if (guest_addr != NULL) {
			*guest_addr = old->guest;
		}
		pthread_mutex_unlock(&n2vc_dma_state.lock);
		return 0;
	}
	for (unsigned i = 0; i < N2VC_DMA_SLOTS; i++) {
		if (!n2vc_dma_state.e[i].used) {
			slot = &n2vc_dma_state.e[i];
			break;
		}
	}
	if (slot == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	/* m == NULL records the pa without a guest address (vcodecd's client buffers). */
	if (m != NULL) {
		guest = n2a32_mem_alloc(m, span, 0x1000u);
		if (guest == 0u) {
			rc = -ENOMEM;
			goto out;
		}
	}
	slot->used = 1;
	slot->owns = owns ? 1 : 0;
	slot->ap_owned = 1;
	slot->handle = handle;
	slot->pa = pa;
	slot->bytes = bytes;
	slot->guest = guest;
	slot->guest_bytes = m != NULL ? span : 0u;
	slot->mem = m;
	slot->pa_known = 1;
	if (guest_addr != NULL) {
		*guest_addr = guest;
	}
out:
	pthread_mutex_unlock(&n2vc_dma_state.lock);
	return rc;
}

int n2vc_dma_guest_register(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t handle,
                            uint32_t pa, uint32_t bytes, uint32_t *guest_addr)
{
	return n2vc_dma_register_internal(h, m, handle, pa, bytes, 0, guest_addr);
}

int n2vc_dma_guest_alloc(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t bytes,
                         uint32_t flags, uint32_t *handle, uint32_t *pa, uint32_t *guest_addr)
{
	uint32_t hnd = 0, phys = 0, guest = 0;
	int rc;

	if (h == NULL || m == NULL || h->dma_alloc == NULL || bytes == 0u || handle == NULL ||
	    guest_addr == NULL) {
		return -EINVAL;
	}
	rc = h->dma_alloc(h->ctx, bytes, flags, &hnd, &phys);
	if (rc != 0) {
		return rc;
	}
	if (hnd == 0u) {
		return -EIO;
	}
	rc = n2vc_dma_register_internal(h, m, hnd, phys, bytes, 1, &guest);
	if (rc != 0) {
		(void)h->dma_free(h->ctx, hnd);
		return rc;
	}
	*handle = hnd;
	if (pa != NULL) {
		*pa = phys;
	}
	*guest_addr = guest;
	return 0;
}

int n2vc_dma_guest_map(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t handle,
                       uint32_t bytes, uint32_t *guest_addr)
{
	struct n2vc_dma_entry *slot = NULL, *d;
	uint32_t guest, span;

	if (h == NULL || m == NULL || handle == 0u || bytes == 0u || guest_addr == NULL) {
		return -EINVAL;
	}
	span = n2vc_dma_page_up(bytes);
	if (span == 0u) {
		return -EINVAL;
	}
	pthread_mutex_lock(&n2vc_dma_state.lock);
	d = n2vc_dma_find_handle_locked(handle);
	if (d != NULL) {
		if (bytes > d->bytes) {
			pthread_mutex_unlock(&n2vc_dma_state.lock);
			return -EINVAL;
		}
		if (d->guest != 0u) {
			if (d->mem != m) {
				pthread_mutex_unlock(&n2vc_dma_state.lock);
				return -EINVAL;   /* the guest address belongs to another address space */
			}
			*guest_addr = d->guest;
			pthread_mutex_unlock(&n2vc_dma_state.lock);
			return 0;
		}
		/* A pa-only entry (registered with m == NULL): attach a guest mapping to it now,
		 * page-rounded to the whole registered buffer, and keep the pa. */
		span = n2vc_dma_page_up(d->bytes);
		guest = span != 0u ? n2a32_mem_alloc(m, span, 0x1000u) : 0u;
		if (guest == 0u) {
			pthread_mutex_unlock(&n2vc_dma_state.lock);
			return -ENOMEM;
		}
		d->guest = guest;
		d->guest_bytes = span;
		d->mem = m;
		*guest_addr = guest;
		pthread_mutex_unlock(&n2vc_dma_state.lock);
		return 0;
	}
	for (unsigned i = 0; i < N2VC_DMA_SLOTS; i++) {
		if (!n2vc_dma_state.e[i].used) {
			slot = &n2vc_dma_state.e[i];
			break;
		}
	}
	if (slot == NULL) {
		pthread_mutex_unlock(&n2vc_dma_state.lock);
		return -ENOMEM;
	}
	guest = n2a32_mem_alloc(m, span, 0x1000u);
	if (guest == 0u) {
		pthread_mutex_unlock(&n2vc_dma_state.lock);
		return -ENOMEM;
	}
	slot->used = 1;
	slot->owns = 0;
	slot->handle = handle;
	slot->pa = 0;
	slot->pa_known = 0;
	slot->bytes = bytes;
	slot->guest = guest;
	slot->guest_bytes = span;
	slot->mem = m;
	*guest_addr = guest;
	pthread_mutex_unlock(&n2vc_dma_state.lock);
	return 0;
}

int n2vc_dma_guest_unmap(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t guest_addr)
{
	struct n2vc_dma_entry *d;
	uint32_t handle = 0;
	int owns = 0;

	if (h == NULL || m == NULL || guest_addr == 0u) {
		return -EINVAL;
	}
	pthread_mutex_lock(&n2vc_dma_state.lock);
	d = n2vc_dma_find_guest_locked(guest_addr);
	if (d == NULL) {
		pthread_mutex_unlock(&n2vc_dma_state.lock);
		return -ENOENT;
	}
	if (d->guest != guest_addr) {
		pthread_mutex_unlock(&n2vc_dma_state.lock);
		return -EINVAL;   /* only the base of a mapping can be unmapped */
	}
	handle = d->handle;
	owns = d->owns;
	if (d->ap_owned) {
		/* Board: IV -> V41 (11 November).  The vendor daemon calls ion_munmap() after every
		 * job; that is the importer dropping its view, not the AP releasing the buffer (the
		 * guest address and the handle are still the AP's, and the next job syncs through the
		 * same mapping).  Clearing the entry here made the second n2ap_venc_encode() fail with
		 * -EINVAL before it sent anything.  The AP releases the entry with guest_forget(). */
		pthread_mutex_unlock(&n2vc_dma_state.lock);
		return 0;
	}
	memset(d, 0, sizeof(*d));
	pthread_mutex_unlock(&n2vc_dma_state.lock);

	if (owns && h->dma_free != NULL) {
		(void)h->dma_free(h->ctx, handle);
	}
	return 0;
}

int n2vc_dma_guest_forget(const struct n2vc_hw *h, uint32_t handle)
{
	struct n2vc_dma_entry *d;
	int owns = 0;

	if (h == NULL || handle == 0u) {
		return -EINVAL;
	}
	pthread_mutex_lock(&n2vc_dma_state.lock);
	d = n2vc_dma_find_handle_locked(handle);
	if (d == NULL) {
		pthread_mutex_unlock(&n2vc_dma_state.lock);
		return -ENOENT;
	}
	owns = d->owns;
	memset(d, 0, sizeof(*d));
	pthread_mutex_unlock(&n2vc_dma_state.lock);

	if (owns && h->dma_free != NULL) {
		(void)h->dma_free(h->ctx, handle);
	}
	return 0;
}

int n2vc_dma_sync(const struct n2vc_hw *h, struct n2a32_mem *m, uint32_t guest_addr,
                  uint32_t offset, uint32_t bytes, int to_device)
{
	struct n2vc_dma_entry *d;
	uint8_t *host;
	const struct n2a32_mem *cm;
	int rc;

	if (h == NULL || h->dma_rw == NULL || guest_addr == 0u) {
		return -EINVAL;
	}
	pthread_mutex_lock(&n2vc_dma_state.lock);
	d = n2vc_dma_find_guest_locked(guest_addr);
	if (d == NULL) {
		pthread_mutex_unlock(&n2vc_dma_state.lock);
		return -ENOENT;
	}
	if (d->guest != guest_addr) {
		pthread_mutex_unlock(&n2vc_dma_state.lock);
		return -EINVAL;   /* only the base of a mapping; `offset` is relative to it */
	}
	if (m != NULL && d->mem != m) {
		pthread_mutex_unlock(&n2vc_dma_state.lock);
		return -EINVAL;
	}
	if ((uint64_t)offset + bytes > d->bytes) {
		pthread_mutex_unlock(&n2vc_dma_state.lock);
		return -ERANGE;
	}
	cm = d->mem != NULL ? d->mem : m;
	host = (uint8_t *)n2a32_ptr(cm, guest_addr + offset);
	if (host == NULL) {
		pthread_mutex_unlock(&n2vc_dma_state.lock);
		return -EFAULT;
	}
	rc = 0;
	if (bytes != 0u) {
		rc = h->dma_rw(h->ctx, d->handle, offset, host, bytes, to_device ? 1 : 0);
	}
	pthread_mutex_unlock(&n2vc_dma_state.lock);
	return rc;
}

int n2vc_dma_lookup_pa(uint32_t pa, uint32_t *handle, uint32_t *guest_addr, uint32_t *bytes)
{
	int rc = -ENOENT;
	uint64_t p = pa;

	pthread_mutex_lock(&n2vc_dma_state.lock);
	for (unsigned i = 0; i < N2VC_DMA_SLOTS; i++) {
		struct n2vc_dma_entry *d = &n2vc_dma_state.e[i];

		if (!d->used || !d->pa_known) {
			continue;
		}
		if (p >= (uint64_t)d->pa && p - d->pa < (uint64_t)d->bytes) {
			if (handle != NULL) {
				*handle = d->handle;
			}
			if (guest_addr != NULL) {
				/* A pa-only entry has no guest address: report 0, not pa's offset. */
				*guest_addr = d->guest != 0u ? d->guest + (uint32_t)(p - d->pa) : 0u;
			}
			if (bytes != NULL) {
				*bytes = d->bytes - (uint32_t)(p - d->pa);
			}
			rc = 0;
			break;
		}
	}
	pthread_mutex_unlock(&n2vc_dma_state.lock);
	return rc;
}

int n2vc_dma_lookup_handle(uint32_t handle, uint32_t *pa, uint32_t *guest_addr, uint32_t *bytes)
{
	struct n2vc_dma_entry *d;
	int rc = -ENOENT;

	if (handle == 0u) {
		return -EINVAL;
	}
	pthread_mutex_lock(&n2vc_dma_state.lock);
	d = n2vc_dma_find_handle_locked(handle);
	if (d != NULL) {
		if (pa != NULL) {
			*pa = d->pa;
		}
		if (guest_addr != NULL) {
			*guest_addr = d->guest;
		}
		if (bytes != NULL) {
			*bytes = d->bytes;
		}
		rc = 0;
	}
	pthread_mutex_unlock(&n2vc_dma_state.lock);
	return rc;
}
