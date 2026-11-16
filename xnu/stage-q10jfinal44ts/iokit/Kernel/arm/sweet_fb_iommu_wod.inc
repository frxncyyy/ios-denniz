/*
 * 0480 (memwod-20261113): wire-on-demand surfaces and the display IOMMU.  Included ONCE by sweet_fb_iommu_hw.h after n2io_map_locked.
 * SPDX-License-Identifier: MIT
 *
 * A cache entry of a pageable surface holds it wired from before the page-table walk (n2io_map_surface: a never-blocking hold, the
 * swap's own hold wired it on the SwapEnd caller's thread) until after the unmap + TLB invalidate (n2io_unmap_ent -> n2io_wod_unmapped).
 * The 96-entry cache would otherwise keep every surface ever shown wired: n2io_wod_sweep (the 2 s tick of sweet_surface_wod_kern.inc)
 * unmaps the entries of pageable surfaces that no lookup touched for idle_s, with sweet_fb_iommu_victim's safety rule (not looked up
 * in the generation being planned, not programmed in an overlay slot).  Entries of ordinary wired surfaces are left alone.
 */
extern "C" vm_offset_t ml_static_ptovirt(vm_offset_t);
static bool          n2io_wod_held[SWEET_FB_IOMMU_CACHE];       /* entry i holds its surface (n2io_lock) */
static uint64_t      n2io_wod_seen[SWEET_FB_IOMMU_CACHE];       /* its stamp at the last sweep */
static uint32_t      n2io_wod_at[SWEET_FB_IOMMU_CACHE];         /* uptime s the stamp last moved (0 = not seen yet) */

static void
n2io_wod_unmapped(struct sweet_fb_iommu_ent *e)
{
	unsigned i = (unsigned)(e - n2io_ent);
	if (i < SWEET_FB_IOMMU_CACHE && n2io_wod_held[i]) {
		n2io_wod_held[i] = false;
		sweet_surface_wod_unhold((void *)(uintptr_t)e->obj);
	}
}

/* n2io_map_locked for a surface; lock held.  NULL also when a pageable surface is not wired now (then nothing walks it). */
static struct sweet_fb_iommu_ent *
n2io_map_surface(SweetSurface *s)
{
	if (!sweet_surface_wod_hold_try(s)) {
		n2io_wod_refused++;
		return NULL;
	}
	struct sweet_fb_iommu_ent *e = n2io_map_locked(s, s->id, s->memory, s->memory->getLength());
	if (e == NULL || !s->wod.on) {
		sweet_surface_wod_unhold(s);    /* (no-op for a wired surface) */
	} else {
		unsigned i = (unsigned)(e - n2io_ent);
		n2io_wod_held[i] = true; n2io_wod_at[i] = 0;
	}
	return e;
}

/* The pixels of a mapped surface to the point of coherency (lock held, entry exists: a pageable surface is wired). */
static void
n2io_clean_surface(SweetSurface *s, IOBufferMemoryDescriptor *buf, uint32_t bytes)
{
	if (!s->wod.on) {
		CleanPoC_DcacheRegion((vm_offset_t)buf->getBytesNoCopy(), (size_t)bytes);
		return;
	}
	for (uint64_t off = 0; off < bytes;) {          /* through the physical aperture, one contiguous run at a time */
		IOByteCount sl = 0;
		uint64_t pa = buf->getPhysicalSegment((IOByteCount)off, &sl, kIOMemoryMapperNone);
		if (sl == 0 || pa < gPhysBase || pa - gPhysBase >= gPhysSize || sl > gPhysSize - (pa - gPhysBase)) { break; }
		if (sl > bytes - off) { sl = (IOByteCount)(bytes - off); }
		CleanPoC_DcacheRegion(ml_static_ptovirt((vm_offset_t)pa), (size_t)sl);
		off += sl;
	}
}

static void
n2io_wod_sweep(uint32_t now, uint32_t idle_s)
{
	if (__atomic_load_n(&n2io_state, __ATOMIC_RELAXED) != 1 || n2io_lock == NULL) { return; }
	IOLockLock(n2io_lock);
	for (unsigned i = 0; n2io_state == 1 && i < SWEET_FB_IOMMU_CACHE; i++) {
		struct sweet_fb_iommu_ent *e = &n2io_ent[i];
		if (e->obj == NULL || !n2io_wod_held[i]) { continue; }
		if (n2io_wod_at[i] == 0 || n2io_wod_seen[i] != e->stamp) {
			n2io_wod_seen[i] = e->stamp; n2io_wod_at[i] = now | 1u;
			continue;
		}
		if (sweet_wod_iommu_idle(e->stamp, n2io_gen, sweet_fb_iommu_ent_programmed(e, &n2io_prog), n2io_wod_at[i], now, idle_s)) {
			n2io_unmap_ent(e);      /* unmap, flush, TLB range invalidate, IOVA back, hold back */
			n2io_wod_swept++;
		}
	}
	IOLockUnlock(n2io_lock);
}
