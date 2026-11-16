/* Nord 2 Mali-G77 IOKit transport, ABI v2. Default off (nord2_gpu=1 opts in).
 * Per-client AS, acknowledged DMA lifetimes and owner-thread PMIC access.
 * Metal registration is explicit opt-in; offline/device probes work without it.
 * SPDX-License-Identifier: MIT */
#include "../../../bsd/dev/valhall_wa_firmware.h"
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <pexpert/arm64/board_config.h>
#include <pexpert/device_tree.h>
#include <arm/machine_routines.h>
extern "C" {
#include <arm/caches_internal.h>
}
#include <kern/clock.h>
#include "SweetSurface.h"
extern "C" {
extern unsigned long gPhysBase, gPhysSize;
extern int PE_parse_boot_argn(const char *, void *, int);
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
#include "../../../bsd/dev/nord2_gpu_board.h"
#include "../../../bsd/dev/nord2_gpu_core.h"
#include "../../../bsd/dev/nord2_gpu_owner.h"
#include "../../../bsd/dev/nord2_gpu_pmif.h"
#include "../../../bsd/dev/nord2_gpu_jm.h"
#include "../../../bsd/dev/nord2_gpu_transport.h"
}

static struct n2owner gpu_owner_queue;
/* Sole caller is ni_thread, between existing PWRAP transactions. */
extern "C" void nord2_gpu_owner_poll(void) { n2owner_poll(&gpu_owner_queue); }
static uint64_t now_ms(void *) {
        uint64_t ns = 0;
        absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
        return ns / 1000000ull;
}
static void sleep_ms(void *, unsigned ms) { IOSleep(ms); }
static void delay_us(void *, unsigned us) { IODelay(us); }
static void log_msg(void *, const char *s) { IOLog("NORD2 GPU: %s\n", s); }
struct Bank { volatile uint32_t *p; unsigned bytes; };
static uint32_t read_bank(void *ctx, uint32_t off) {
        Bank *b = (Bank *)ctx;
        if (!b->p || (off & 3) || off >= b->bytes) return 0;
        uint32_t v = b->p[off / 4];
        __asm__ volatile("dmb oshld" ::: "memory");
        return v;
}
static void write_bank(void *ctx, uint32_t off, uint32_t v) {
        Bank *b = (Bank *)ctx;
        if (!b->p || (off & 3) || off >= b->bytes) return;
        __asm__ volatile("dmb oshst" ::: "memory");
        b->p[off / 4] = v;
        __asm__ volatile("dsb osh" ::: "memory");
}
static bool map_bank(Bank *b, uint64_t pa, unsigned bytes) {
        b->bytes = bytes;
        b->p = (volatile uint32_t *)ml_io_map(pa, bytes);
        return b->p != NULL;
}
static bool valid_ram(uint64_t pa) {
        return !(pa & 4095) && gPhysSize >= 4096 && pa >= gPhysBase &&
               pa - gPhysBase <= gPhysSize - 4096 && pa < (1ull << 40);
}
struct BufferOwner {
        IOBufferMemoryDescriptor *md;
        SweetSurface *surface;
};
static void free_owner(void *, void *cookie) {
        BufferOwner *o = (BufferOwner *)cookie;
        if (!o) return;
        OSSafeReleaseNULL(o->md);
        OSSafeReleaseNULL(o->surface);
        IOFree(o, sizeof(*o));
}
static bool alloc_host(void *, size_t bytes, void **p) {
        *p = IOMallocZero(bytes); return *p != NULL;
}
static void free_host(void *, void *p, size_t bytes) { if (p) IOFree(p, bytes); }
static void sync_page(void *, uint64_t, void *cpu, size_t bytes, bool) {
        /* CIVAC: also discards stale CPU lines following GPU END_FLUSH. */
        FlushPoC_DcacheRegion((vm_offset_t)cpu, bytes);
}
static IOReturn io_error(enum n2c_error e) {
        switch (e) {
        case N2C_OK: return kIOReturnSuccess;
        case N2C_E_ARGS: case N2C_E_RANGE: return kIOReturnBadArgument;
        case N2C_E_BUSY: return kIOReturnBusy;
        case N2C_E_NO_HANDLE: case N2C_E_NO_JOB: return kIOReturnNotFound;
        case N2C_E_NOMEM: case N2C_E_QUOTA: return kIOReturnNoMemory;
        case N2C_E_STATE: return kIOReturnNotReady;
        default: return kIOReturnIOError;
        }
}
class IOAcceleratorES : public IOService { OSDeclareDefaultStructors(IOAcceleratorES); };
OSDefineMetaClassAndStructors(IOAcceleratorES, IOService);
class Nord2GPU : public IOAcceleratorES {
        OSDeclareDefaultStructors(Nord2GPU);
public:
        IOLock *lock = NULL;
        struct n2c_core *core = NULL;
        bool ready = false, stopping = false;
        bool start(IOService *) APPLE_KEXT_OVERRIDE;
        void stop(IOService *) APPLE_KEXT_OVERRIDE;
        void free() APPLE_KEXT_OVERRIDE;
        IOReturn mapBuffer(unsigned, uint64_t, uint32_t, uint32_t *, uint64_t *, uint64_t *);
        IOReturn importSurface(unsigned, uint32_t, uint32_t, uint32_t, uint32_t *, uint64_t *, uint64_t *);
        IOReturn compute(unsigned, const nord2_gpu_compute_request *, uint64_t *);
        void status(nord2_gpu_status *);
        IOReturn probe(unsigned);
private:
        bool enabled = false, attempted = false, pinned = false;
        IOWorkLoop *loop = NULL;
        IOTimerEventSource *timer = NULL;
        Bank gpu = {}, spm = {}, infra = {}, clocks[3] = {}, spmi = {}, pwrap = {};
        struct n2pmif pm15 = {}, pm59 = {};
        struct n2rail rail = {};
        struct n2clk clk = {};
        struct n2hw hw = {};
        struct nord2_scpsys scpsys = {};
        struct nord2_gpu_board board = {};
        IOBufferMemoryDescriptor *tables[1024] = {};
        unsigned table_count = 0;
        bool configure(IOService *);
        static void tick(OSObject *, IOTimerEventSource *);
        static int powerOn(void *);
        static int powerOff(void *);
        static uint32_t clockRead(void *, enum n2clk_block, uint32_t);
        static void clockWrite(void *, enum n2clk_block, uint32_t, uint32_t);
        static uint32_t spmRead(void *p, uint32_t o) { return read_bank(&((Nord2GPU *)p)->spm, o); }
        static void spmWrite(void *p, uint32_t o, uint32_t v) { write_bank(&((Nord2GPU *)p)->spm, o, v); }
        static uint32_t infraRead(void *p, uint32_t o) { return read_bank(&((Nord2GPU *)p)->infra, o); }
        static void infraWrite(void *p, uint32_t o, uint32_t v) { write_bank(&((Nord2GPU *)p)->infra, o, v); }
        static int read15(void *p, uint16_t a, uint8_t *v) { return n2pmif_spmi_read(&((Nord2GPU *)p)->pm15, 7, a, v); }
        static int write15(void *p, uint16_t a, uint8_t v) { return n2pmif_spmi_write(&((Nord2GPU *)p)->pm15, 7, a, v); }
        static int read59(void *p, uint16_t a, uint16_t *v) { return n2pmif_pwrap_read(&((Nord2GPU *)p)->pm59, a, v); }
        static int write59(void *p, uint16_t a, uint16_t v) { return n2pmif_pwrap_write(&((Nord2GPU *)p)->pm59, a, v); }
        static bool allocTable(void *, uint64_t *, void **);
        static void freeTable(void *, uint64_t, void *);
        static void flushTable(void *, uint64_t, size_t);
};
OSDefineMetaClassAndStructors(Nord2GPU, IOAcceleratorES);
uint32_t Nord2GPU::clockRead(void *p, enum n2clk_block b, uint32_t o) {
        return b <= N2CLK_MFGCFG ? read_bank(&((Nord2GPU *)p)->clocks[b], o) : 0;
}
void Nord2GPU::clockWrite(void *p, enum n2clk_block b, uint32_t o, uint32_t v) {
        if (b <= N2CLK_MFGCFG) write_bank(&((Nord2GPU *)p)->clocks[b], o, v);
}
bool Nord2GPU::allocTable(void *p, uint64_t *pa, void **cpu) {
        Nord2GPU *d = (Nord2GPU *)p;
        if (d->table_count == 1024) return false;
        auto md = IOBufferMemoryDescriptor::withOptions(kIODirectionInOut, 16384, 16384);
        if (!md) return false;
        IOByteCount length = 0;
        *pa = md->getPhysicalSegment(0, &length, kIOMemoryMapperNone);
        if (!valid_ram(*pa) || length < 4096) { md->release(); return false; }
        *cpu = md->getBytesNoCopy(); bzero(*cpu, 16384);
        d->tables[d->table_count++] = md;
        return true;
}
void Nord2GPU::freeTable(void *p, uint64_t, void *cpu) {
        Nord2GPU *d = (Nord2GPU *)p;
        for (unsigned i = 0; i < d->table_count; i++) if (d->tables[i]->getBytesNoCopy() == cpu) {
                d->tables[i]->release(); d->tables[i] = d->tables[--d->table_count];
                d->tables[d->table_count] = NULL; return;
        }
}
void Nord2GPU::flushTable(void *p, uint64_t pa, size_t bytes) {
        Nord2GPU *d = (Nord2GPU *)p;
        for (unsigned i = 0; i < d->table_count; i++) {
                IOByteCount length;
                if (d->tables[i]->getPhysicalSegment(0, &length, kIOMemoryMapperNone) == pa) {
                        sync_page(p, pa, d->tables[i]->getBytesNoCopy(), bytes, true); return;
                }
        }
}
bool Nord2GPU::configure(IOService *) {
        DTEntry dt; const void *prop = NULL; unsigned len = 0; uint64_t reg[2];
        nord2_gpu_board_config cfg = {};
        if (SecureDTLookupEntry(NULL, "/arm-io/mali", &dt) != kSuccess ||
            SecureDTGetProperty(dt, "reg", &prop, &len) != kSuccess || len != sizeof(reg)) return false;
        memcpy(reg, prop, sizeof(reg)); cfg.reg_base = reg[0] + 0x01000000ull; cfg.reg_size = reg[1];
        if (SecureDTGetProperty(dt, "interrupts", &prop, &len) != kSuccess || len != sizeof(cfg.irq)) return false;
        memcpy(cfg.irq, prop, sizeof(cfg.irq));
        cfg.mfg_base = NORD2_GPU_MFG_BASE; cfg.mfg_size = NORD2_GPU_MFG_SIZE; cfg.expected_arch_major = 9;
        if (!nord2_gpu_board_config_valid(&cfg)) return false;
        if (!map_bank(&gpu, cfg.reg_base, (unsigned)cfg.reg_size) ||
            !map_bank(&spm, 0x10006000, 0x1000) || !map_bank(&infra, 0x10001000, 0x1000) ||
            !map_bank(&clocks[0], N2CLK_TOPCK_BASE, 0x1000) ||
            !map_bank(&clocks[1], N2CLK_APMIXED_BASE, 0x1000) ||
            !map_bank(&clocks[2], N2CLK_MFGCFG_BASE, 0x1000) ||
            !map_bank(&spmi, N2PMIF_SPMI_BASE, 0x1000) ||
            !map_bank(&pwrap, N2PMIF_PWRAP_BASE, 0x1000)) return false;
        n2pmif_ops po = { read_bank, write_bank, delay_us };
        n2rail_ops ro = { read15, write15, read59, write59, delay_us, log_msg };
        n2clk_ops co = { clockRead, clockWrite, delay_us, log_msg };
        n2hw_ops ho = { read_bank, write_bank, delay_us, log_msg };
        nord2_scpsys_ops so = { spmRead, spmWrite, infraRead, infraWrite, delay_us, log_msg };
        n2c_ops mo = { allocTable, freeTable, flushTable, alloc_host, free_host, sync_page, free_owner, now_ms, log_msg };
        core = (n2c_core *)IOMallocZero(sizeof(*core));
        return core && n2pmif_init(&pm15, N2PMIF_SPMI, &po, &spmi) &&
               n2pmif_init(&pm59, N2PMIF_PWRAP, &po, &pwrap) &&
               n2rail_init(&rail, &ro, this) && n2clk_init(&clk, &co, this) &&
               n2hw_init(&hw, &ho, &gpu) && nord2_scpsys_init(&scpsys, &so, this, 200000) &&
               nord2_gpu_board_init(&board, &rail, &clk, &scpsys, &hw, log_msg, this) &&
               n2c_init(core, &mo, this, &hw) &&
               n2c_set_wa_payload(core, valhall_wa_payload, sizeof(valhall_wa_payload));
}
int Nord2GPU::powerOn(void *p) {
        Nord2GPU *d = (Nord2GPU *)p;
        bool preflight = nord2_gpu_board_preflight(&d->board);
        const nord2_gpu_board_report &r = d->board.report;
        /* Cached owner-thread observations only: do not touch an unpowered
         * GPU/MFG bank merely to diagnose a failed clock/rail check. */
        IOLog("NORD2 GPU CLOCK: ok=%u stage=%u error=%u mfg=%08x cfg5=%08x "
              "con0=%08x con1=%08x con3=%08x khz=%u posdiv=%u normalize=%u\n",
              preflight, r.stage, r.error, r.mtcmos_before,
              r.clk_before.clk_cfg_5, r.clk_before.mfgpll_con0,
              r.clk_before.mfgpll_con1, r.clk_before.mfgpll_con3,
              r.clk_before.khz, (r.clk_before.mfgpll_con1 >> 24) & 7u,
              r.normalize_pll);
        IOLog("NORD2 GPU RAIL: valid=%x id=%02x en=%02x sel=%02x da_en=%02x "
              "da_sel=%02x vsram=%04x/%04x/%04x/%04x target=%u/%u opp=%u\n",
              r.rail_before.valid, r.rail_before.swcid_h, r.rail_before.vgpu_en,
              r.rail_before.vgpu_vosel, r.rail_before.vgpu_da_en,
              r.rail_before.vgpu_da_vosel, r.rail_before.vsram_con0,
              r.rail_before.vsram_mon, r.rail_before.vsram_vosel1,
              r.rail_before.vsram_elr, r.vgpu_uv, r.vsram_uv, r.opp_index);
        bool on = preflight && nord2_gpu_board_power_on(&d->board);
        IOLog("NORD2 GPU CLOCK AFTER: cfg5=%08x con0=%08x con1=%08x con3=%08x khz=%u\n",
              r.clk_after.clk_cfg_5, r.clk_after.mfgpll_con0,
              r.clk_after.mfgpll_con1, r.clk_after.mfgpll_con3, r.clk_after.khz);
        IOLog("NORD2 GPU RAIL AFTER: step=%u writes=%u uncertain=%u valid=%x "
              "en=%02x sel=%02x da_en=%02x da_sel=%02x vsram=%04x/%04x/%04x/%04x\n",
              d->rail.step, d->rail.writes, d->rail.uncertain, r.rail_after.valid,
              r.rail_after.vgpu_en, r.rail_after.vgpu_vosel,
              r.rail_after.vgpu_da_en, r.rail_after.vgpu_da_vosel,
              r.rail_after.vsram_con0, r.rail_after.vsram_mon,
              r.rail_after.vsram_vosel1, r.rail_after.vsram_elr);
        IOLog("NORD2 GPU PMIF: spmi_sta=%08x reads=%u writes=%u errors=%u "
              "pwrap_sta=%08x reads=%u writes=%u errors=%u\n",
              d->pm15.last_sta, d->pm15.reads, d->pm15.writes, d->pm15.errors,
              d->pm59.last_sta, d->pm59.reads, d->pm59.writes, d->pm59.errors);
        IOLog("NORD2 GPU POWER: ok=%u stage=%u error=%u domain=%u "
              "mfg=%08x/%08x rollback=%u uncertain=%u id=%08x shaders=%llx\n",
              on, r.stage, r.error, r.mtcmos_failed_domain, r.mtcmos_before,
              r.mtcmos_after, r.rollback_clean, d->board.uncertain,
              r.gpu_id, (unsigned long long)r.shader_present);
        IOLog("NORD2 GPU HW: error=%u offset=%04x value=%08x reset=%u/%u "
              "slots=%u/%u/%u l2=%u shaders=%llx\n",
              d->hw.last_error, d->hw.last_offset, d->hw.last_value,
              d->hw.resets_soft, d->hw.resets_hard, d->hw.slot_fragment,
              d->hw.slot_vertex_tiler, d->hw.slot_compute, d->hw.l2_on,
              (unsigned long long)d->hw.shaders_on);
        return on ? 0 : -1;
}
int Nord2GPU::powerOff(void *p) { return nord2_gpu_board_power_off(&((Nord2GPU *)p)->board) ? 0 : -1; }
void Nord2GPU::tick(OSObject *owner, IOTimerEventSource *sender) {
        Nord2GPU *d = OSDynamicCast(Nord2GPU, owner); if (!d) return;
        IOLockLock(d->lock);
        if (d->stopping) { IOLockUnlock(d->lock); return; }
        if (!d->attempted && n2owner_ready(&gpu_owner_queue)) {
                d->attempted = true;
                n2owner_wait wait = { now_ms, sleep_ms, NULL }; int result = -1;
                if (n2owner_call(&gpu_owner_queue, powerOn, d, &wait, 1000, &result) && !result &&
                    n2c_start(d->core) == N2C_OK) d->ready = true;
                IOLog("NORD2 GPU CORE: state=%u wa_required=%u wa_done=%u runs=%u "
                      "hw_error=%u offset=%04x value=%08x\n", d->core->state,
                      d->hw.has_issue_ttrx_3485, d->hw.wa_complete, d->hw.wa_runs,
                      d->hw.last_error, d->hw.last_offset, d->hw.last_value);
                d->setProperty("Nord2GPUState", d->ready ? "transport-ready" : "power-failed");
                d->setProperty("Nord2GPUStage", (uint64_t)d->board.report.stage, 32);
                d->setProperty("Nord2GPUError", (uint64_t)d->board.report.error, 32);
                IOLog("NORD2 GPU: ready=%u stage=%u error=%u id=%08x\n", d->ready,
                      d->board.report.stage, d->board.report.error, d->hw.id.gpu_id);
        }
        if (d->ready) {
                n2c_poll(d->core);
                if (d->core->state == N2C_CORE_BROKEN) {
                        d->ready = false; d->setProperty("Nord2GPUState", "fault-memory-pinned");
                }
        }
        sender->setTimeoutMS(d->ready ? 5 : 1000);
        IOLockUnlock(d->lock);
}
bool Nord2GPU::start(IOService *provider) {
        if (!IOAcceleratorES::start(provider)) return false;
        lock = IOLockAlloc(); if (!lock) return false;
        int opt = 0; PE_parse_boot_argn("nord2_gpu", &opt, sizeof(opt)); enabled = opt == 1;
        setProperty(kIOUserClientClassKey, "Nord2GPUUserClient");
        setProperty("Nord2TransportVersion", (uint64_t)NORD2_GPU_TRANSPORT_VERSION, 32);
        setProperty("Nord2GPUState", "disabled");
        if (enabled && configure(provider)) {
                loop = IOWorkLoop::workLoop();
                timer = IOTimerEventSource::timerEventSource(this, tick);
                if (loop && timer && loop->addEventSource(timer) == kIOReturnSuccess) {
                        setProperty("Nord2GPUState", "waiting-pmic-owner"); timer->setTimeoutMS(1);
                }
        }
        int metal = 0;
        PE_parse_boot_argn("nord2_metal", &metal, sizeof(metal));
        if (enabled && metal == 1) {
                setProperty("MetalPluginName", "Nord2Metal");
                setProperty("MetalPluginClassName", "TeguComputeDevice");
        }
        registerService(); return true;
}
void Nord2GPU::stop(IOService *provider) {
        if (lock) { IOLockLock(lock); stopping = true; ready = false; IOLockUnlock(lock); }
        if (timer) { timer->cancelTimeout(); if (loop) loop->removeEventSource(timer); }
        if (lock) IOLockLock(lock);
        if (core && core->ops.alloc_table) {
                bool safe = true;
                for (unsigned i = 0; i < N2C_MAX_CTX; i++) if (core->ctxs[i].state != N2C_CTX_FREE) {
                        enum n2c_error e = n2c_ctx_close(core, i);
                        if (e == N2C_E_BUSY && n2c_reset(core) == N2C_OK) e = n2c_ctx_close(core, i);
                        if (e != N2C_OK) safe = false;
                }
                if (safe && board.powered && n2c_stop(core) == N2C_OK) {
                        n2owner_wait wait = {now_ms, sleep_ms, NULL}; int result = -1;
                        safe = n2owner_call(&gpu_owner_queue, powerOff, this, &wait, 1000, &result) && !result;
                }
                if (!safe || core->stats.bos_live || table_count) {
                        if (!pinned) { retain(); pinned = true; }
                        IOLog("NORD2 GPU: stop retains provider and uncertain DMA ownership\n");
                }
        }
        if (lock) IOLockUnlock(lock);
        IOAcceleratorES::stop(provider);
}
void Nord2GPU::free() {
        OSSafeReleaseNULL(timer); OSSafeReleaseNULL(loop);
        if (core) IOFree(core, sizeof(*core)); core = NULL;
        if (lock) IOLockFree(lock); lock = NULL;
        IOAcceleratorES::free();
}
void Nord2GPU::status(nord2_gpu_status *s) {
        bzero(s, sizeof(*s)); s->abi_version = NORD2_GPU_TRANSPORT_VERSION;
        s->enabled = enabled; s->powered = board.powered; s->arch_ok = hw.identified;
        s->gpu_id = hw.id.gpu_id; s->l2_features = hw.id.l2_features;
        s->core_features = hw.id.core_features; s->tiler_features = hw.id.tiler_features;
        s->mem_features = hw.id.mem_features; s->mmu_features = hw.id.mmu_features;
        s->js_present = hw.id.js_present;
        s->shader_present_lo = (uint32_t)hw.id.shader_present; s->shader_present_hi = (uint32_t)(hw.id.shader_present >> 32);
        s->tiler_present_lo = (uint32_t)hw.id.tiler_present; s->tiler_present_hi = (uint32_t)(hw.id.tiler_present >> 32);
        if (core) {
                s->jobs_completed = (uint32_t)core->stats.completed; s->jobs_faulted = (uint32_t)core->stats.faulted;
                s->jobs_timed_out = (uint32_t)core->stats.timeouts; s->buffers_live = (uint32_t)core->stats.bos_live;
                s->quarantine = (uint32_t)(core->stats.quarantined - core->stats.drained);
        }
        if (spm.p) { s->mfg_pwr_status = read_bank(&spm, 0x16c); s->mfg_pwr_status_2nd = read_bank(&spm, 0x170); }
}
IOReturn Nord2GPU::mapBuffer(unsigned c, uint64_t bytes, uint32_t flags,
                            uint32_t *h, uint64_t *va, uint64_t *actual) {
        if (!ready || !core || c >= N2C_MAX_CTX) return kIOReturnNotReady;
        if ((flags & ~1u) || !nord2_gpu_allocation_size(bytes, core->ctxs[c].wired, &bytes)) return kIOReturnBadArgument;
        auto md = IOBufferMemoryDescriptor::withOptions(kIODirectionInOut | kIOMemoryKernelUserShared, (vm_size_t)bytes, 16384);
        if (!md) return kIOReturnNoMemory;
        BufferOwner *owner = (BufferOwner *)IOMallocZero(sizeof(*owner));
        unsigned count = (unsigned)(bytes / 4096);
        n2c_page *pages = (n2c_page *)IOMallocZero(count * sizeof(*pages));
        if (!owner || !pages) { if (owner) IOFree(owner, sizeof(*owner)); if (pages) IOFree(pages, count*sizeof(*pages)); md->release(); return kIOReturnNoMemory; }
        bzero(md->getBytesNoCopy(), (size_t)bytes);
        bool valid = true;
        for (unsigned i = 0; i < count; i++) {
                IOByteCount length; pages[i].pa = md->getPhysicalSegment((uint64_t)i * 4096, &length, kIOMemoryMapperNone);
                pages[i].cpu = (uint8_t *)md->getBytesNoCopy() + (uint64_t)i * 4096;
                if (!valid_ram(pages[i].pa) || length < 4096) valid = false;
        }
        owner->md = md; bool adopted = false;
        enum n2c_error e = valid ? n2c_bo_map(core, c, pages, count, N2C_BO_GPU_READ | N2C_BO_GPU_WRITE |
                ((flags & 1) ? N2C_BO_GPU_EXEC : 0), owner, bytes, h, va, &adopted) : N2C_E_ARGS;
        IOFree(pages, count * sizeof(*pages)); if (!adopted) free_owner(this, owner);
        if (e == N2C_OK) {
                *actual = bytes;
                if ((*h >> 8) <= 8 || bytes >= (8ull << 20)) {
                        uint64_t pa = 0, pte = 0;
                        bool walked = nord2_gpu_mmu_walk(&core->ctxs[c].mmu, *va, &pa, &pte);
                        IOLog("NORD2 GPU MAP: ctx=%u bytes=%llu walk=%u leaf_attr=0x%llx policy=inner-default\n",
                              c, bytes, (unsigned)walked, pte & ((1ull << 54) | 0xfffull));
                }
        }
        return io_error(e);
}
IOReturn Nord2GPU::importSurface(unsigned c, uint32_t id, uint32_t offset, uint32_t bytes,
                                uint32_t *h, uint64_t *va, uint64_t *actual) {
        if (!ready || !bytes || (offset & 4095)) return kIOReturnBadArgument;
        uint64_t mapped = ((uint64_t)bytes + 4095) & ~4095ull, charged;
        if (!nord2_gpu_allocation_size(mapped, core->ctxs[c].wired, &charged)) return kIOReturnNoMemory;
        SweetSurface *s = sweet_surface_copy(id); if (!s) return kIOReturnNotFound;
        uint64_t base = (uint64_t)s->offset + offset;
        if (!s->memory || (base & 4095) || base > s->allocSize || mapped > s->allocSize - base ||
            base > s->memory->getLength() || mapped > s->memory->getLength() - base || !s->kernelAddress()) {
                s->release(); return kIOReturnBadArgument;
        }
        BufferOwner *owner = (BufferOwner *)IOMallocZero(sizeof(*owner));
        unsigned count = (unsigned)(mapped / 4096); n2c_page *pages = (n2c_page *)IOMallocZero(count * sizeof(*pages));
        if (!owner || !pages) { if (owner) IOFree(owner, sizeof(*owner)); if (pages) IOFree(pages,count*sizeof(*pages)); s->release(); return kIOReturnNoMemory; }
        owner->surface = s; bool valid = true;
        for (unsigned i=0; i<count; i++) {
                IOByteCount length; pages[i].pa = s->memory->getPhysicalSegment((uint64_t)s->offset+offset+(uint64_t)i*4096, &length, kIOMemoryMapperNone);
                pages[i].cpu = (void *)(s->kernelAddress()+base+(uint64_t)i*4096);
                if (!valid_ram(pages[i].pa) || length < 4096) valid = false;
        }
        bool adopted = false;
        enum n2c_error e = valid ? n2c_bo_map(core,c,pages,count,N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_IMPORTED,
                                             owner,charged,h,va,&adopted) : N2C_E_ARGS;
        IOFree(pages,count*sizeof(*pages)); if (!adopted) free_owner(this,owner);
        if (e==N2C_OK) *actual=mapped; return io_error(e);
}
IOReturn Nord2GPU::compute(unsigned c, const nord2_gpu_compute_request *r, uint64_t *id) {
        if (r->version != 1 || r->flags || r->reserved || r->tls_handle || r->fau_count > 128 ||
            r->allow_merging > 1 || r->shader_handle > UINT32_MAX || r->resource_handle > UINT32_MAX || r->fau_handle > UINT32_MAX) return kIOReturnBadArgument;
        const n2c_bo *shader = n2c_bo_lookup(core,c,(uint32_t)r->shader_handle);
        const n2c_bo *res = r->resource_handle ? n2c_bo_lookup(core,c,(uint32_t)r->resource_handle) : NULL;
        const n2c_bo *fau = r->fau_handle ? n2c_bo_lookup(core,c,(uint32_t)r->fau_handle) : NULL;
        if (!shader || !(shader->flags & N2C_BO_GPU_EXEC) || shader->size < 4 || r->shader_offset > shader->size-4 ||
            (r->resource_handle && (!res || r->resource_offset >= res->size)) ||
            (r->fau_count && !fau) || (r->fau_handle && !fau) ||
            (fau && (r->fau_offset >= fau->size || (uint64_t)r->fau_count*8 > fau->size-r->fau_offset))) return kIOReturnBadArgument;
        uint64_t threads=1; for(unsigned i=0;i<3;i++) {
                if (!r->workgroup_size[i] || !r->workgroup_count[i] || r->workgroup_size[i]>1024/threads) return kIOReturnBadArgument;
                threads*=r->workgroup_size[i];
        }
        uint32_t h=0; uint64_t va=0,size=0; IOReturn result=mapBuffer(c,NORD2_GPU_COMPUTE_PROGRAM_BYTES,0,&h,&va,&size);
        if (result!=kIOReturnSuccess) return result;
        const n2c_bo *cmd=n2c_bo_lookup(core,c,h);
        nord2_gpu_compute_chain chain={};
        for(unsigned i=0;i<3;i++){chain.workgroup_size[i]=r->workgroup_size[i];chain.workgroup_count[i]=r->workgroup_count[i];}
        chain.allow_merging=r->allow_merging;chain.resources=res?res->va+r->resource_offset:0;
        chain.shader=shader->va+r->shader_offset;chain.fau=fau?fau->va+r->fau_offset:0;
        chain.fau_count=(uint8_t)r->fau_count;chain.attribute_offset=r->attribute_offset;chain.chain_pa=va;chain.index=1;
        if (!nord2_gpu_jm_build_compute_program((uint8_t *)((BufferOwner *)cmd->cookie)->md->getBytesNoCopy(),(size_t)size,&chain)) result=kIOReturnBadArgument;
        else {
                struct n2c_submit sub={};sub.kind=N2C_JOB_COMPUTE;sub.heads[0]=va;
                sub.handles[sub.nr_handles++]=h;sub.handles[sub.nr_handles++]=shader->handle;
                const n2c_bo *extra[2]={res,fau};
                for(unsigned i=0;i<2;i++)if(extra[i]){
                        bool seen=false;for(unsigned k=0;k<sub.nr_handles;k++)if(sub.handles[k]==extra[i]->handle)seen=true;
                        if(!seen)sub.handles[sub.nr_handles++]=extra[i]->handle;
                }
                sub.write_mask=(1u<<sub.nr_handles)-1;
                result=io_error(n2c_submit(core,c,&sub,id));
        }
        (void)n2c_bo_release(core,c,h); return result;
}

/* Authored store-token shader, byte-checked against pinned Mesa arch9. */
static const uint8_t n2_token_code[] = {
        0xc0, 0x44, 0x52, 0x4f, 0x4e, 0xc0, 0x10, 0x01, 0x80, 0x00, 0x00, 0x00,
        0x00, 0xc2, 0x91, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0xc3, 0x91, 0x00,
        0x42, 0x00, 0x00, 0x18, 0x02, 0x40, 0x61, 0x78, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
IOReturn Nord2GPU::probe(unsigned c) {
        uint32_t handles[3]={}; uint64_t vas[3]={},bytes=0,id=0;
        IOReturn result=kIOReturnIOError;
        for(unsigned i=0;i<3;i++) {
                result=mapBuffer(c,4096,i==0?1:0,&handles[i],&vas[i],&bytes);
                if(result!=kIOReturnSuccess)break;
        }
        if(result==kIOReturnSuccess) {
                const n2c_bo *shader=n2c_bo_lookup(core,c,handles[0]);
                const n2c_bo *fau=n2c_bo_lookup(core,c,handles[2]);
                memcpy(((BufferOwner *)shader->cookie)->md->getBytesNoCopy(),n2_token_code,sizeof(n2_token_code));
                *(uint64_t *)((BufferOwner *)fau->cookie)->md->getBytesNoCopy()=vas[1];
                nord2_gpu_compute_request r={};r.version=1;r.shader_handle=handles[0];r.fau_handle=handles[2];r.fau_count=1;
                for(unsigned i=0;i<3;i++){r.workgroup_size[i]=1;r.workgroup_count[i]=1;}
                result=compute(c,&r,&id);
        }
        if(result==kIOReturnSuccess) {
                uint64_t started=now_ms(NULL);uint32_t state=0,status=0;
                for(;;) {
                        n2c_poll(core);
                        result=io_error(n2c_job_state(core,c,id,false,&state,&status));
                        if(result!=kIOReturnSuccess || state>=N2C_JOB_DONE || core->state==N2C_CORE_BROKEN ||
                           now_ms(NULL)-started>5000)break;
                        IOLockUnlock(lock);IOSleep(1);IOLockLock(lock);
                }
                result=kIOReturnIOError;
                if(state==N2C_JOB_DONE && n2c_bo_sync(core,c,handles[1],false)==N2C_OK) {
                        const n2c_bo *out=n2c_bo_lookup(core,c,handles[1]);
                        if(out && *(volatile uint32_t *)((BufferOwner *)out->cookie)->md->getBytesNoCopy()==0x4e4f5244u)
                                result=kIOReturnSuccess;
                }
                if(state>=N2C_JOB_DONE)(void)n2c_job_state(core,c,id,true,NULL,NULL);
        }
        for(unsigned i=0;i<3;i++)if(handles[i])(void)n2c_bo_release(core,c,handles[i]);
        return result;
}

class Nord2GPUUserClient : public IOUserClient {
        OSDeclareDefaultStructors(Nord2GPUUserClient);
        Nord2GPU *gpu=NULL; IOLock *lock=NULL; task_t task=TASK_NULL;
        unsigned context=N2C_MAX_CTX, logBytes=0; bool closed=false;
        IOReturn ensureContext();
        void closeContext();
public:
        bool initWithTask(task_t,void *,UInt32) APPLE_KEXT_OVERRIDE;
        bool start(IOService *) APPLE_KEXT_OVERRIDE;
        void free() APPLE_KEXT_OVERRIDE;
        IOReturn clientClose() APPLE_KEXT_OVERRIDE;
        IOReturn clientDied() APPLE_KEXT_OVERRIDE { return clientClose(); }
        IOReturn clientMemoryForType(UInt32,IOOptionBits *,IOMemoryDescriptor **) APPLE_KEXT_OVERRIDE;
        IOReturn externalMethod(uint32_t,IOExternalMethodArguments *,IOExternalMethodDispatch *,OSObject *,void *) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(Nord2GPUUserClient,IOUserClient);
bool Nord2GPUUserClient::initWithTask(task_t t,void *security,UInt32 type) {
        if(type || !IOUserClient::initWithTask(t,security,type))return false;
        task=t;lock=IOLockAlloc();return lock!=NULL;
}
bool Nord2GPUUserClient::start(IOService *provider) {
        if(!IOUserClient::start(provider))return false;
        gpu=OSDynamicCast(Nord2GPU,provider);if(!gpu)return false;gpu->retain();return true;
}
IOReturn Nord2GPUUserClient::ensureContext() {
        if(!gpu->ready || gpu->stopping)return kIOReturnNotReady;
        if(context<N2C_MAX_CTX)return kIOReturnSuccess;
        return io_error(n2c_ctx_open(gpu->core,(uint64_t)(uintptr_t)task,256ull<<20,false,&context));
}
void Nord2GPUUserClient::closeContext() {
        if(!lock)return;
        IOLockLock(lock);closed=true;
        if(gpu && context<N2C_MAX_CTX) {
                enum n2c_error e;
                do {
                        IOLockLock(gpu->lock);e=n2c_ctx_close(gpu->core,context);IOLockUnlock(gpu->lock);
                        if(e==N2C_E_BUSY)IOSleep(1);
                } while(e==N2C_E_BUSY);
                context=N2C_MAX_CTX; /* core retains any unacknowledged DMA */
        }
        IOLockUnlock(lock);
}
IOReturn Nord2GPUUserClient::clientClose() {
        closeContext(); terminate(); return kIOReturnSuccess;
}
void Nord2GPUUserClient::free() {
        if(!closed)closeContext();
        OSSafeReleaseNULL(gpu);if(lock)IOLockFree(lock);lock=NULL;IOUserClient::free();
}
IOReturn Nord2GPUUserClient::clientMemoryForType(UInt32 type,IOOptionBits *options,IOMemoryDescriptor **memory) {
        if(!options || !memory || !gpu || !lock)return kIOReturnBadArgument;
        *memory=NULL;*options=kIOMapDefaultCache;IOLockLock(lock);IOLockLock(gpu->lock);
        const n2c_bo *b=!closed && context<N2C_MAX_CTX?n2c_bo_lookup(gpu->core,context,type):NULL;
        BufferOwner *o=b?(BufferOwner *)b->cookie:NULL;
        if(o && o->md){*memory=o->md;(*memory)->retain();}
        IOLockUnlock(gpu->lock);IOLockUnlock(lock);return *memory?kIOReturnSuccess:kIOReturnNotFound;
}
IOReturn Nord2GPUUserClient::externalMethod(uint32_t sel,IOExternalMethodArguments *a,IOExternalMethodDispatch *,OSObject *,void *) {
        if(!a || !gpu || !lock)return kIOReturnBadArgument;
        if(!nord2_gpu_call_valid(sel,a->scalarInputCount,a->scalarOutputCount,a->structureInputSize,a->structureOutputSize,
                                 a->scalarInput,a->scalarOutput,a->structureInput,a->structureOutput) ||
           a->structureInputDescriptor || a->structureOutputDescriptor || a->asyncWakePort!=MACH_PORT_NULL)return kIOReturnBadArgument;
        IOLockLock(lock);IOLockLock(gpu->lock);IOReturn result=kIOReturnUnsupported;
        if(closed || gpu->stopping){result=kIOReturnNotOpen;goto out;}
        if(sel==NORD2_GPU_SEL_STATUS){gpu->status((nord2_gpu_status *)a->structureOutput);a->structureOutputSize=sizeof(nord2_gpu_status);result=kIOReturnSuccess;goto out;}
        if(sel==NORD2_GPU_SEL_LOG){
                if(a->structureInputSize>16384-logBytes){result=kIOReturnNoSpace;goto out;}
                logBytes+=(unsigned)a->structureInputSize;
                /* DEVELOPMENT disables kprintf without enable_kprintf_spam.
                 * IOLog reaches the same console used by the live USB stream. */
                IOLog("NORD2 GPU USER: %.*s",(int)a->structureInputSize,(const char *)a->structureInput);
                result=kIOReturnSuccess;goto out;
        }
        result=ensureContext();if(result!=kIOReturnSuccess)goto out;
        result=kIOReturnUnsupported;
        switch(sel){
        case NORD2_GPU_SEL_ALLOC: {
                if(a->scalarInput[1]>1){result=kIOReturnBadArgument;break;}
                uint32_t h;uint64_t va,size;result=gpu->mapBuffer(context,a->scalarInput[0],(uint32_t)a->scalarInput[1],&h,&va,&size);
                if(result==kIOReturnSuccess){a->scalarOutput[0]=h;a->scalarOutput[1]=va;a->scalarOutput[2]=size;a->scalarOutputCount=3;}break;
        }
        case NORD2_GPU_SEL_RELEASE:
                if(a->scalarInput[0]>UINT32_MAX){result=kIOReturnBadArgument;break;}
                result=io_error(n2c_bo_release(gpu->core,context,(uint32_t)a->scalarInput[0]));
                if(result==kIOReturnBusy)result=kIOReturnSuccess; /* handle consumed, backing retained */
                break;
        case NORD2_GPU_SEL_SUBMIT_COMPUTE: {
                nord2_gpu_compute_request r;memcpy(&r,a->structureInput,sizeof(r));uint64_t id;
                result=gpu->compute(context,&r,&id);if(result==kIOReturnSuccess){a->scalarOutput[0]=id;a->scalarOutputCount=1;}break;
        }
        case NORD2_GPU_SEL_SUBMIT_JM: {
                nord2_gpu_submit_request r;memcpy(&r,a->structureInput,sizeof(r));if(r.version!=2){result=kIOReturnBadArgument;break;}
                struct n2c_submit sub={};sub.kind=r.kind;sub.nr_handles=r.nr_handles;sub.heads[0]=r.heads[0];sub.heads[1]=r.heads[1];
                sub.write_mask=r.write_mask;memcpy(sub.handles,r.handles,sizeof(sub.handles));uint64_t id;
                result=io_error(n2c_submit(gpu->core,context,&sub,&id));if(result==kIOReturnSuccess){a->scalarOutput[0]=id;a->scalarOutputCount=1;}break;
        }
        case NORD2_GPU_SEL_WAIT: {
                if(a->scalarInput[1]>5000){result=kIOReturnBadArgument;break;}
                uint32_t state=0,status=0;uint64_t start=now_ms(NULL),id=a->scalarInput[0];
                for(;;){
                        n2c_poll(gpu->core);result=io_error(n2c_job_state(gpu->core,context,id,false,&state,&status));
                        if(result!=kIOReturnSuccess || state>=N2C_JOB_DONE || gpu->core->state==N2C_CORE_BROKEN || now_ms(NULL)-start>=a->scalarInput[1])break;
                        IOLockUnlock(gpu->lock);IOSleep(1);IOLockLock(gpu->lock);
                }
                if(result==kIOReturnSuccess){
                        a->scalarOutput[0]=state;a->scalarOutputCount=1;
                        if(a->structureOutputSize){nord2_gpu_fault_info f={};f.job_id_lo=(uint32_t)id;f.job_id_hi=(uint32_t)(id>>32);
                                f.state=state;f.status=status;f.fault_status=gpu->core->ctxs[context].fault_status;f.fault_address=gpu->core->ctxs[context].fault_address;
                                memcpy(a->structureOutput,&f,sizeof(f));a->structureOutputSize=sizeof(f);}
                        if(state>=N2C_JOB_DONE)(void)n2c_job_state(gpu->core,context,id,true,NULL,NULL);
                }break;
        }
        case NORD2_GPU_SEL_FAULT: {
                nord2_gpu_fault_info f={};f.fault_status=gpu->core->ctxs[context].fault_status;f.fault_address=gpu->core->ctxs[context].fault_address;
                memcpy(a->structureOutput,&f,sizeof(f));a->structureOutputSize=sizeof(f);result=kIOReturnSuccess;break;
        }
        case NORD2_GPU_SEL_IMPORT_SURFACE: {
                if(a->scalarInput[0]>UINT32_MAX || a->scalarInput[1]>UINT32_MAX || a->scalarInput[2]>UINT32_MAX || a->scalarInput[3]){result=kIOReturnBadArgument;break;}
                uint32_t h;uint64_t va,size;result=gpu->importSurface(context,(uint32_t)a->scalarInput[0],(uint32_t)a->scalarInput[1],(uint32_t)a->scalarInput[2],&h,&va,&size);
                if(result==kIOReturnSuccess){a->scalarOutput[0]=h;a->scalarOutput[1]=va;a->scalarOutput[2]=size;a->scalarOutputCount=3;}break;
        }
        case NORD2_GPU_SEL_PROBE:
                result=gpu->probe(context);break;
        case NORD2_GPU_SEL_SYNC:
                if(a->scalarInput[0]>UINT32_MAX || a->scalarInput[1]>1){result=kIOReturnBadArgument;break;}
                result=io_error(n2c_bo_sync(gpu->core,context,(uint32_t)a->scalarInput[0],a->scalarInput[1]!=0));break;
        /* Cross-client reset is deliberately not a public user-client operation. */
        default: break;
        }
 out:
        IOLockUnlock(gpu->lock);IOLockUnlock(lock);return result;
}
