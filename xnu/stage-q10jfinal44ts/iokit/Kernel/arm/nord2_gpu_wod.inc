/*
 * 0480 (memwod-20261113): GPU import of a WIRE-ON-DEMAND surface (SweetSurface wod.on, sweet_surface_wod.h).  Included ONCE by
 * Nord2GPU.cpp in front of Nord2GPU::importSurface, which hands every such surface here.  SPDX-License-Identifier: MIT
 *
 * Same result as the wired path with three differences: (1) the surface is wired (prepare(), may page its pixels in) BEFORE any
 * physical lookup, with the GPU lock dropped (the caller's connection lock stays, so the context cannot close meanwhile; the driver
 * state is checked again); (2) the hold lasts as long as the import buffer: free_owner gives it back after the core unmapped and
 * flushed the GPU pages, so no job can read or write the pages of an unwired surface; (3) the surface has no kernel mapping, so the
 * per-page CPU address for the core's cache maintenance is the physical aperture (the pages are XNU RAM: valid_ram first).
 * Called with the connection lock and the GPU lock held; `mapped` and `charged` were computed and budgeted by the caller.
 */
static IOReturn
n2wod_import(Nord2GPU *g, unsigned c, SweetSurface *s, uint32_t offset, uint64_t mapped, uint64_t charged, uint32_t *h, uint64_t *va, uint64_t *actual)
{
        uint64_t base = (uint64_t)s->offset + offset;
        if (!s->memory || (base & 4095) || base > s->allocSize || mapped > s->allocSize - base ||
            base > s->memory->getLength() || mapped > s->memory->getLength() - base) {
                s->release(); return kIOReturnBadArgument;
        }
        unsigned count = (unsigned)(mapped / 4096); const uint32_t sid = s->id;     /* (s may be gone after free_owner) */
        IOLockUnlock(g->lock);
        bool held = sweet_surface_wod_hold(s);
        BufferOwner *owner = held ? (BufferOwner *)IOMallocZero(sizeof(*owner)) : NULL;
        n2c_page *pages = held ? (n2c_page *)IOMallocZero(count * sizeof(*pages)) : NULL;
        IOLockLock(g->lock);
        bool up = g->ready && !g->stopping && g->core;
        if (!owner || !pages || !up) {
                if (owner) IOFree(owner, sizeof(*owner));
                if (pages) IOFree(pages, count * sizeof(*pages));
                if (held) sweet_surface_wod_unhold(s);
                s->release();
                return up ? kIOReturnNoMemory : kIOReturnNotReady;
        }
        owner->surface = s; bool valid = true;
        OSIncrementAtomic((volatile SInt32 *)&s->gpuImports);     /* 0398: given back in free_owner, with the hold */
        for (unsigned i = 0; i < count; i++) {
                IOByteCount length = 0; pages[i].pa = s->memory->getPhysicalSegment(base + (uint64_t)i * 4096, &length, kIOMemoryMapperNone);
                if (!valid_ram(pages[i].pa) || length < 4096) { valid = false; break; }
                pages[i].cpu = (void *)ml_static_ptovirt((vm_offset_t)pages[i].pa);
        }
        bool adopted = false;
        enum n2c_error e = valid ? n2c_bo_map(g->core, c, pages, count, N2C_BO_GPU_READ | N2C_BO_GPU_WRITE | N2C_BO_IMPORTED,
                                             owner, charged, h, va, &adopted) : N2C_E_ARGS;
        IOFree(pages, count * sizeof(*pages)); if (!adopted) free_owner(g, owner);
        static unsigned logs; if (logs < 8 || e != N2C_OK) { logs++;
                IOLog("NORD2 GPU WOD: import ctx=%u sid=%u pages=%u valid=%d e=%d\n", c, sid, count, (int)valid, (int)e); }
        if (e == N2C_OK && g->core->track_level >= N2C_TRACK_EXPLICIT) (void)n2c_bo_track(g->core, c, *h);
        if (e == N2C_OK) *actual = mapped;
        return io_error(e);
}
