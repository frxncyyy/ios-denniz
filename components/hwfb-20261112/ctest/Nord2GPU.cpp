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
#include <kern/sched_prim.h>
#include <kern/thread.h>
#include <kern/processor.h>
#include <mach/processor_info.h>
#include <mach/host_info.h>
#include <mach/vm_statistics.h>
#include <kern/cpu_data.h>
#include <sys/proc.h>
#include "SweetSurface.h"
extern "C" {
extern unsigned long gPhysBase, gPhysSize;
extern int PE_parse_boot_argn(const char *, void *, int);
static uint32_t gAllocExt = 5;   /* 0139: GPU-only and pageable write-combine on by default */   /* 0136/0137: boot-arg n2ext, which ALLOC flags beyond 'executable' are accepted: 1 GPU-only, 2 write-combine (kernel-allocated), 4 write-combine (pageable, wired) */
extern int proc_selfpid(void);
extern void proc_selfname(char *, int);
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
#include "../../../bsd/dev/nord2_gpu_board.h"
#include "../../../bsd/dev/nord2_gpu_core.h"
#include "../../../bsd/dev/nord2_gpu_owner.h"
#include "../../../bsd/dev/nord2_gpu_pmif.h"
#include "../../../bsd/dev/nord2_gpu_jm.h"
#include "../../../bsd/dev/nord2_gpu_transport.h"
#include "../../../bsd/dev/nord2_gpu_irq.h"
#include "../../../bsd/dev/nord2_gpu_lvts.h"
}

/* 0146: system telemetry every 2 s -- per-CPU busy time and the VM paging load (page-ins are file reads from storage; zero-fill and
 * copy-on-write faults are memory the apps touch for the first time) -- to tell storage or CPU stalls from GPU ones. */
extern "C" kern_return_t host_statistics64(host_t, host_flavor_t, host_info64_t, mach_msg_type_number_t *);
extern "C" char n2_realhost[] __asm__("_realhost");   /* the host data the in-kernel callers pass */
extern "C" void n2_processor_cpu_load(processor_t, natural_t ticks[CPU_STATE_MAX]) __asm__("_processor_cpu_load_info");   /* the struct of the same name hides the function in C++ */
/* 0183: busy permille of one CPU over the last ~100 ms for the DVFS governor.  Called every 50 ms per CPU by the governor thread only; keeps the last three samples of the
 * cumulative user+system / total ticks (10 ms ticks), so the window is two calls and the result is good to about +-10 points. */
extern "C" int nord2_cpu_util_permille(int cpu) {
        static struct { uint32_t busy[3], tot[3]; unsigned n; int last; } h[16];
        if (cpu < 0 || cpu >= 16) return -1;
        processor_t p = cpu_to_processor(cpu); if (!p) return -1;
        natural_t t[CPU_STATE_MAX] = {};
        n2_processor_cpu_load(p, t);
        uint32_t busy = t[CPU_STATE_USER] + t[CPU_STATE_SYSTEM], tot = busy + t[CPU_STATE_IDLE];
        unsigned k = h[cpu].n % 3; h[cpu].busy[k] = busy; h[cpu].tot[k] = tot; h[cpu].n++;
        if (h[cpu].n < 3) { h[cpu].last = -1; return -1; }
        unsigned old = h[cpu].n % 3;
        uint32_t dt = tot - h[cpu].tot[old], db = busy - h[cpu].busy[old];
        if (dt < 5) return h[cpu].last;
        int u = (int)(1000ull * db / dt); if (u > 1000) u = 1000;
        h[cpu].last = u; return u;
}
static void n2_sys_telemetry(uint64_t nowUs) {
        static uint64_t lastUs = 0;
        static natural_t cpuPrev[16][CPU_STATE_MAX] = {};
        static uint64_t vmPrev[6] = {};
        if (lastUs && nowUs - lastUs < 2000000ull) return;
        uint64_t winUs = lastUs ? nowUs - lastUs : 0; lastUs = nowUs;
        unsigned ncpu = (unsigned)ml_get_max_cpu_number() + 1; if (ncpu > 16) ncpu = 16;
        char busy[96]; unsigned o = 0; unsigned sum = 0, big = 0;
        for (unsigned i = 0; i < ncpu; i++) {
                natural_t t[CPU_STATE_MAX] = {};
                processor_t p = cpu_to_processor((int)i); if (!p) continue;
                n2_processor_cpu_load(p, t);
                uint64_t du = (uint64_t)(t[CPU_STATE_USER] - cpuPrev[i][CPU_STATE_USER]) + (t[CPU_STATE_SYSTEM] - cpuPrev[i][CPU_STATE_SYSTEM]);
                uint64_t di = (uint64_t)(t[CPU_STATE_IDLE] - cpuPrev[i][CPU_STATE_IDLE]);
                for (unsigned k = 0; k < CPU_STATE_MAX; k++) cpuPrev[i][k] = t[k];
                unsigned pct = du + di ? (unsigned)(100 * du / (du + di)) : 0;
                if (i >= 4) big += pct; sum += pct;
                if (o + 8 < sizeof(busy)) o += (unsigned)snprintf(busy + o, sizeof(busy) - o, "%s%u", i ? "/" : "", pct);
        }
        vm_statistics64_data_t v = {}; mach_msg_type_number_t vc = HOST_VM_INFO64_COUNT; (void)host_statistics64((host_t)n2_realhost, HOST_VM_INFO64, (host_info64_t)&v, &vc);
        uint64_t cur[6] = { v.pageins, v.pageouts, v.faults, v.zero_fill_count, v.cow_faults, v.decompressions };
        uint64_t d[6]; for (unsigned k = 0; k < 6; k++) { d[k] = cur[k] - vmPrev[k]; vmPrev[k] = cur[k]; }
        if (winUs)
                IOLog("NORD2 SYS: win_ms=%llu cpu_busy%%=%s (avg %u, big %u) | vm pageins=%llu pageouts=%llu faults=%llu zerofill=%llu cow=%llu decomp=%llu\n",
                      winUs / 1000, busy, ncpu ? sum / ncpu : 0, ncpu > 4 ? big / (ncpu - 4) : 0, d[0], d[1], d[2], d[3], d[4], d[5]);
}

static struct n2owner gpu_owner_queue;
/* Sole caller is ni_thread, between existing PWRAP transactions. */
extern "C" void nord2_gpu_owner_poll(void) { n2owner_poll(&gpu_owner_queue); }
static uint64_t now_ms(void *) {
        uint64_t ns = 0;
        absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
        return ns / 1000000ull;
}
static uint64_t now_us(void *) {
        uint64_t ns = 0;
        absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
        return ns / 1000ull;
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
        bool prepared;      /* 0137: pageable write-combine memory, wired by prepare(), unwired by complete() */
};
/* 0112: the display surface behind an imported buffer (NULL for any other buffer); only compared, never dereferenced, by the core. */
static const void *resv_token(void *, void *cookie) { return cookie ? (const void *)((BufferOwner *)cookie)->surface : NULL; }
static void free_owner(void *, void *cookie) {
        BufferOwner *o = (BufferOwner *)cookie;
        if (!o) return;
        if (o->md && o->prepared) o->md->complete(kIODirectionInOut);
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
/* Cache maintenance is scoped to each job's submitted buffer list.  Boot-arg
 * nord2_gpu_cache=1 restores whole-context maintenance for every job. */
static bool cache_scope(struct n2c_core *c) {
        int v = 0; PE_parse_boot_argn("nord2_gpu_cache", &v, sizeof(v));
        { int x = 0; if (PE_parse_boot_argn("n2ext", &x, sizeof(x))) gAllocExt = (uint32_t)x & 7u; IOLog("NORD2 GPU: alloc extensions n2ext=%u (1 GPU-only, 2 write-combine)\n", gAllocExt); }
        c->full_cache_maint = v == 1;
        /* Tracking level (0087 scoping is level 0): 1 = buffers the CPU can never touch are maintained once;
         * 2 = plus IOSurface lock/unlock-bracketed CPU access.  nord2_gpu_track=<n>, short alias n2trk. */
        int tr = 2, got = 0;     /* P8: level 2 is the default (it ran on the device in every P6/P7 UI image); n2trk=1 gives the old default */
        got = PE_parse_boot_argn("nord2_gpu_track", &tr, sizeof(tr)); if (!got) got = PE_parse_boot_argn("n2trk", &tr, sizeof(tr));
        if (tr < 0 || tr > 2) tr = 1;
        (void)n2c_set_track_level(c, (unsigned)tr);
        int wm = 0;      /* 0110: nord2_gpu_wmask=1 (alias n2wm) trusts a job's write_mask for the post-job invalidate */
        got = PE_parse_boot_argn("nord2_gpu_wmask", &wm, sizeof(wm)); if (!got) got = PE_parse_boot_argn("n2wm", &wm, sizeof(wm));
        (void)n2c_set_wmask_inval(c, wm == 1);
        int rs = 1;      /* 0112: nord2_gpu_resv=0 (alias n2resv) disables the display-surface reservation selectors */
        got = PE_parse_boot_argn("nord2_gpu_resv", &rs, sizeof(rs)); if (!got) got = PE_parse_boot_argn("n2resv", &rs, sizeof(rs));
        n2c_set_resv_enabled(c, rs != 0);
        IOLog("NORD2 GPU: cache maintenance %s, CPU-access tracking level %d, invalidate scope %s, surface reservations %s\n", c->full_cache_maint ? "whole-context (boot-arg)" : "per-job buffer list", tr,
              c->wmask_inval ? "declared writes (write_mask)" : "every listed GPU_WRITE buffer", rs ? "on" : "off");
        return true;
}
static bool battery_temperature(int *centi);
/* Cold-start GPU clock: boot-arg nord2_gpu_khz=<kHz> selects an exact vendor OPP
 * (350000..886000, see n2rail_opp_table).  Default (absent) keeps the 350 MHz minimum.
 * Honoured only when the boot chain left MFGPLL unusable and every MFG domain off. */
static bool opp_target(struct nord2_gpu_board *b) {
        int khz = 0; PE_parse_boot_argn("nord2_gpu_khz", &khz, sizeof(khz));
        b->target_khz = khz > 0 ? (uint32_t)khz : 0;
        int mhz = 0; uint32_t alias = 0;
        if (!b->target_khz && PE_parse_boot_argn("n2mhz", &mhz, sizeof(mhz)) && nord2_gpu_mhz_alias_to_khz(mhz, &alias)) b->target_khz = alias;
        if (!b->target_khz) {                       /* P8 policy (nord2_gpu_transport.h): UI images start at 654 MHz, others at the minimum */
                int metal = 0; PE_parse_boot_argn("nord2_metal", &metal, sizeof(metal));
                b->target_khz = nord2_gpu_default_opp_khz(metal == 1);
                if (b->target_khz) IOLog("NORD2 GPU: cold-start OPP default %u kHz (UI image; n2mhz=350 selects the old minimum)\n", b->target_khz);
        } else IOLog("NORD2 GPU: cold-start OPP requested: %u kHz (exact vendor entry or refused)\n", b->target_khz);
        return true;
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
class Nord2GPU;
static Nord2GPU *gSurfaceGpu;   /* the ready driver instance, for the IOSurface CPU-access hooks */
class IOAcceleratorES : public IOService { OSDeclareDefaultStructors(IOAcceleratorES); };
OSDefineMetaClassAndStructors(IOAcceleratorES, IOService);
class Nord2GPU : public IOAcceleratorES {
        OSDeclareDefaultStructors(Nord2GPU);
public:
        IOLock *lock = NULL;
        struct n2c_core *core = NULL;
        bool ready = false, stopping = false;
        uint64_t privateLimit = NORD2_GPU_PRIVATE_LIMIT, globalLimit = NORD2_GPU_GLOBAL_PRIVATE_LIMIT;
        uint64_t lastBudgetLog = 0; unsigned budgetSuppressed = 0;
        /* Compact job-timing line (kernel clocks only, ~one line per 2 s while jobs run).
         * w_* are measured around WAIT: gap = submit -> WAIT entered, dur = time inside
         * WAIT, ret = submit -> WAIT returned (what the client sees per job). */
        bool jobStats = true;
        /* patch 0122 (n2pf=1): Mali hardware counters, manual sampling once per stats window; read-only for the GPU, the dump page is a kernel-only mapping in AS0 */
        int pfMode = 0;
        bool pfFailed = false;
        struct nord2_gpu_mmu pfmmu = {};
        IOBufferMemoryDescriptor *pfMd = NULL, *pfXmd[3] = {};      /* 0129: 4 x 16 KiB of dump space, the GPU may write more than the 14.8 KiB seen so far */
        uint64_t pfPa = 0;
        uint8_t *pfCpu = NULL;
        unsigned pfBytes = 0;
        uint64_t pfLastUs = 0, pfWinUs = 0;
        bool pfPending = false, pfArmed = false;
        uint64_t ckCycles = 0, ckStamp = 0, ckUs = 0;      /* 0127: GPU cycle counter / timestamp at the previous stats window */
        bool ckRunning = false;
        void clockCheck();
        unsigned pfWaits = 0, pfArms = 0, pfFails = 0, pfSinceArm = 0;
        void prfcntSample();
        uint32_t waitN = 0, waitGapMax = 0, waitDurMax = 0, waitRetMax = 0;
        uint64_t waitGapSum = 0, waitDurSum = 0, waitRetSum = 0;
        /* WAIT_SEQ: waits, jobs covered, time inside the wait */
        uint32_t seqWaits = 0, seqJobs = 0, seqDurMax = 0; uint64_t seqDurSum = 0;
        void emitJobStats();
        void surfaceHook(void *surface, bool acquire);
        bool surfaceBusy(void *surface);
        void surfaceBusyWhy(void *surface, uint32_t *match, uint64_t *oldestAgeUs, uint64_t *oldestId, uint32_t *oldestState, uint32_t *inflight, uint32_t *queued, uint32_t *resv);
        static int pmicRead(void *);   /* 0116: one PMIC register read on the PWRAP owner thread (CPU DVFS init voltages) */
        /* Optional job interrupt (boot-arg nord2_gpu_irq=1; polling stays the fallback). */
        bool irqWanted = false, irqTried = false, irqArmed = false;
        uint32_t irqSpi = NORD2_GPU_JOB_SPI_DEFAULT, irqIntid = 0, irqLearnMisses = 0;
        enum nord2_irq_state irqState = NORD2_IRQ_OFF;
        struct nord2_irq_guard irqGuard = {};
        volatile uint32_t irqFlag = 0, irqTops = 0;
        uint32_t irqSleeps = 0, irqTimeouts = 0;
        /* LVTS thermal sensing (boot-arg nord2_lvts=1 read-only | 2 start the GPU controller | 3 also open the clock gate). */
        struct n2lvts lvts = {};
        Bank lvtsAp = {}, lvtsMcu = {};
        unsigned lvtsMode = 0, lvtsState = 0;     /* state: 0 off, 1 reading, 2 started by us, 3 failed */
        uint32_t lvtsMask = N2LVTS_MASK_GPU; int lvtsMaxC = 65; bool lvtsTried = false;
        void lvtsSetup();
        bool lvtsGpu(int32_t *mC);
        int lvtsHottest(uint32_t mask);
        static uint32_t lvtsRd(void *, enum n2lvts_block, uint32_t);
        static void lvtsWr(void *, enum n2lvts_block, uint32_t, uint32_t);
        void irqSetup();
        void irqLearnCheck();
        void irqArm();
        void irqTop();
        void waitStep(uint64_t elapsed_us);
        static void irqDispatch(void *, void *, void *, int);
        static Nord2GPU *irqOwner;
        uint64_t privateTotal();
        void budgetReject(const char *, unsigned, uint64_t, uint64_t);
        bool start(IOService *) APPLE_KEXT_OVERRIDE;
        void stop(IOService *) APPLE_KEXT_OVERRIDE;
        void free() APPLE_KEXT_OVERRIDE;
        IOReturn mapBuffer(unsigned, uint64_t, uint32_t, uint32_t *, uint64_t *, uint64_t *, bool dropLock = false, volatile bool *closedFlag = NULL);
        IOReturn importSurface(unsigned, uint32_t, uint32_t, uint32_t, uint32_t *, uint64_t *, uint64_t *);
        IOReturn compute(unsigned, const nord2_gpu_compute_request *, uint64_t *);
        void status(nord2_gpu_status *);
        void hwinfo(nord2_gpu_hwinfo *);
        IOReturn probe(unsigned);
private:
        bool enabled = false, attempted = false, pinned = false;
        IOWorkLoop *loop = NULL;
        IOTimerEventSource *timer = NULL;
        Bank gpu = {}, spm = {}, infra = {}, clocks[3] = {}, spmi = {}, pwrap = {}, gicd = {};
        struct n2pmif pm15 = {}, pm59 = {};
        struct n2rail rail = {};
        struct n2clk clk = {};
        struct n2hw hw = {};
        struct nord2_scpsys scpsys = {};
        struct nord2_gpu_board board = {};
        IOBufferMemoryDescriptor **tables = NULL;             /* 0126: 2048 entries (was an inline array of 1024; the class must stay below 16 KiB), allocated on first use; every table is a 16 KiB descriptor */
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
        if (!d->tables) { d->tables = (IOBufferMemoryDescriptor **)IOMallocZero(2048 * sizeof(IOBufferMemoryDescriptor *)); if (!d->tables) return false; }
        if (d->table_count == 2048) return false;
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
        n2c_ops mo = { allocTable, freeTable, flushTable, alloc_host, free_host, sync_page, free_owner, now_ms, log_msg, now_us, resv_token };
        core = (n2c_core *)IOMallocZero(sizeof(*core));
        return core && n2pmif_init(&pm15, N2PMIF_SPMI, &po, &spmi) &&
               n2pmif_init(&pm59, N2PMIF_PWRAP, &po, &pwrap) &&
               n2rail_init(&rail, &ro, this) && n2clk_init(&clk, &co, this) &&
               n2hw_init(&hw, &ho, &gpu) && nord2_scpsys_init(&scpsys, &so, this, 200000) &&
               nord2_gpu_board_init(&board, &rail, &clk, &scpsys, &hw, log_msg, this) &&
               opp_target(&board) &&
               n2c_init(core, &mo, this, &hw) &&
               cache_scope(core) &&
               n2c_set_wa_payload(core, valhall_wa_payload, sizeof(valhall_wa_payload));
}
int Nord2GPU::powerOn(void *p) {
        Nord2GPU *d = (Nord2GPU *)p;
        if (d->lvtsMode && !d->lvtsTried) d->lvtsSetup();
        if (d->board.target_khz > N2CLK_BOOT_KHZ) {
                /* Battery-gauge guard (the only temperature available without LVTS): a hot start gets the minimum OPP. */
                int bat = 0; bool bok = battery_temperature(&bat);
                if (!nord2_gpu_opp_battery_allows(d->board.target_khz, bok, bat)) {
                        IOLog("NORD2 GPU: battery %d.%02d C above the cold-start limit for %u kHz: using the minimum OPP\n", bat / 100, bat % 100, d->board.target_khz);
                        d->board.target_khz = 0;
                } else if (bok) IOLog("NORD2 GPU: battery gauge %d.%02d C: cold-start OPP %u kHz allowed\n", bat / 100, bat % 100, d->board.target_khz);
                else IOLog("NORD2 GPU: battery gauge not readable yet: cold-start OPP %u kHz not blocked\n", d->board.target_khz);
        }
        if (d->board.target_khz > N2CLK_BOOT_KHZ) {
                /* Cold-start high-OPP guard: a known-hot GPU gets the minimum OPP.  An unreadable sensor never blocks. */
                int32_t gmC = 0; bool ok = d->lvtsGpu(&gmC);
                if (ok && !n2lvts_allows_cold_start_opp(gmC, true, !d->lvts.cal.defaulted, d->lvtsMaxC * 1000)) {
                        IOLog("NORD2 GPU: GPU %d.%d C%s above the cold-start limit %d C: using the minimum OPP instead of %u kHz\n",
                              gmC / 1000, (gmC % 1000) / 100, d->lvts.cal.defaulted ? " (approximate, 10 C margin)" : "", d->lvtsMaxC, d->board.target_khz);
                        d->board.target_khz = 0;
                } else if (ok) IOLog("NORD2 GPU: GPU %d.%d C: cold-start OPP %u kHz allowed\n", gmC / 1000, (gmC % 1000) / 100, d->board.target_khz);
        }
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
                    n2c_start(d->core) == N2C_OK) { d->ready = true; gSurfaceGpu = d; }
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
        n2_sys_telemetry(now_us(NULL));
        if (d->ready) {
                n2c_poll(d->core);
                d->emitJobStats();
                if (d->core->state == N2C_CORE_BROKEN) {
                        d->ready = false; d->setProperty("Nord2GPUState", "fault-memory-pinned");
                }
        }
        /* Queued/running jobs are started and completed by polling: tick at 1 ms
         * while any exist so an async chain keeps the GPU fed between WAIT calls. */
        unsigned unfinished = 0;
        if (d->ready) for (unsigned i = 0; i < N2C_MAX_CTX; i++) if (d->core->ctxs[i].state != N2C_CTX_FREE) unfinished += d->core->ctxs[i].jobs;
        /* 0131: with unfinished jobs the poll that notices a completion and starts the next job runs every 150 us (was 1 ms): a job takes ~0.7 ms of GPU time but used to cost ~2.5 ms of turnaround. */
        if (d->ready && unfinished && d->irqState == NORD2_IRQ_ACTIVE) d->irqArm();   /* 0140: completion interrupts for asynchronous clients */
        if (d->ready && unfinished) sender->setTimeout(150, kMicrosecondScale); else sender->setTimeoutMS(d->ready ? 5 : 1000);
        IOLockUnlock(d->lock);
}
bool Nord2GPU::start(IOService *provider) {
        if (!IOAcceleratorES::start(provider)) return false;
        lock = IOLockAlloc(); if (!lock) return false;
        int opt = 0; PE_parse_boot_argn("nord2_gpu", &opt, sizeof(opt)); enabled = opt == 1;
        int stats = 1; PE_parse_boot_argn("nord2_gpu_stats", &stats, sizeof(stats)); jobStats = stats != 0;
        if (!PE_parse_boot_argn("n2pf", &pfMode, sizeof(pfMode))) pfMode = 0;
        int lv = 0; if (!PE_parse_boot_argn("nord2_lvts", &lv, sizeof(lv))) (void)PE_parse_boot_argn("n2lvts", &lv, sizeof(lv)); lvtsMode = (lv >= 1 && lv <= 3) ? (unsigned)lv : 0;
        int lm = 0; if (PE_parse_boot_argn("nord2_lvts_mask", &lm, sizeof(lm)) && lm > 0 && (lm & ~(int)N2LVTS_MASK_ALL) == 0) lvtsMask = (uint32_t)lm;
        int lc = 0; if (PE_parse_boot_argn("nord2_lvts_max_c", &lc, sizeof(lc)) && lc >= 30 && lc <= 100) lvtsMaxC = lc;
        if (lvtsMode) IOLog("NORD2 GPU: LVTS thermal sensing requested: mode %u mask %x cold-start guard %d C\n", lvtsMode, lvtsMask, lvtsMaxC);
        int irq = 1; PE_parse_boot_argn("nord2_gpu_irq", &irq, sizeof(irq)); irqWanted = irq == 1;   /* 0132: on by default (confirmed on the phone: SPI 364 / intid 396 asserts with the job line); nord2_gpu_irq=0 turns it off, which also frees the 16 characters the boot line needed for it */
        int spi = 0; if (PE_parse_boot_argn("nord2_gpu_irq_spi", &spi, sizeof(spi)) && spi >= 32 && spi <= 900) irqSpi = (uint32_t)spi;
        if (irqWanted) IOLog("NORD2 GPU: job interrupt requested (spi %u), polling remains the fallback\n", irqSpi);
        int mib = 0; PE_parse_boot_argn("nord2_gpu_priv_mb", &mib, sizeof(mib));
        /* patch 0125: the 160/256 MiB defaults were sized for the 992 MiB window; with 1.5 GiB or more backboardd hit the per-client cap (GPU BUDGET rejections), so the boot-arg ceilings become the defaults */
        bool bigRam = gPhysSize >= (1536ull << 20);
        privateLimit = nord2_gpu_budget_mib(mib > 0 ? (uint64_t)mib : 0, 16, NORD2_GPU_PRIVATE_MAX >> 20, bigRam ? NORD2_GPU_PRIVATE_MAX : NORD2_GPU_PRIVATE_LIMIT);
        mib = 0; PE_parse_boot_argn("nord2_gpu_glob_mb", &mib, sizeof(mib));
        globalLimit = nord2_gpu_budget_mib(mib > 0 ? (uint64_t)mib : 0, 16, NORD2_GPU_GLOBAL_PRIVATE_MAX >> 20, bigRam ? NORD2_GPU_GLOBAL_PRIVATE_MAX : NORD2_GPU_GLOBAL_PRIVATE_LIMIT);
        IOLog("NORD2 GPU: budgets private=%llu MiB/client global-private=%llu MiB import=%llu MiB/client ctx-limit=%llu MiB ram=%lu MiB\n",
              privateLimit >> 20, globalLimit >> 20, (uint64_t)NORD2_GPU_IMPORT_LIMIT >> 20, (uint64_t)NORD2_GPU_CONTEXT_LIMIT >> 20, gPhysSize >> 20);
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
        if (lock) { IOLockLock(lock); stopping = true; ready = false; if (gSurfaceGpu == this) gSurfaceGpu = NULL; IOLockUnlock(lock); }
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
void Nord2GPU::hwinfo(nord2_gpu_hwinfo *w) {
        bzero(w, sizeof(*w)); w->version = 1; w->thread_max_threads = hw.id.thread_max_threads;
        w->thread_max_workgroup_size = hw.id.thread_max_workgroup_size; w->thread_features = hw.id.thread_features;
        w->coherency_features = hw.id.coherency_features; w->as_present = hw.id.as_present;
        for (unsigned i = 0; i < 4; i++) w->texture_features[i] = hw.id.texture_features[i];
        for (unsigned i = 0; i < 16; i++) w->js_features[i] = hw.id.js_features[i];
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
/* Private bytes of every live context (device-wide private budget). */
uint64_t Nord2GPU::privateTotal() {
        uint64_t total = 0;
        for (unsigned i = 0; i < N2C_MAX_CTX; i++)
                if (core->ctxs[i].state != N2C_CTX_FREE) total += core->ctxs[i].wired - core->ctxs[i].imported;
        return total;
}
/* Rate-limited (1/s) diagnostic for budget rejections; counts what it skipped. */
/* ---- optional GPU job interrupt (boot-arg nord2_gpu_irq=1) -------------------------------
 * The kernel enables no SPI and sleh_irq only forwards unknown INTIDs to the CPU's installed
 * interrupt handler, so the driver (a) programs the GICD entry itself (level, Group 1, routed
 * to the PE running irqSetup, still disabled), (b) CONFIRMS the INTID by seeing the SPI pending
 * while a real job's RAWSTAT is set with the JOB mask armed (a wrong SPI number is never enabled),
 * (c) enables it and installs irqDispatch on that PE.  Top half: mask the job line at the device,
 * count, wake the waiter.  Bottom half is the unchanged n2c_poll().  Timeouts keep polling alive. */
Nord2GPU *Nord2GPU::irqOwner = NULL;
/* IOSurface CPU-access protocol (level 2).  acquire: the CPU is about to read the surface (IOSurfaceLock, or the
 * framebuffer copy): invalidate its lines if the GPU wrote it since.  release: the CPU wrote it (IOSurfaceUnlock
 * without read-only): clean it before the GPU's next read.  Imports of the surface in any context are covered. */
void Nord2GPU::surfaceHook(void *surface, bool acquire) {
        if (!surface || !ready || !core || core->track_level < N2C_TRACK_EXPLICIT) return;
        IOLockLock(lock);
        if (!stopping) {
                for (unsigned i = 0; i < N2C_MAX_CTX; i++) {
                        if (core->ctxs[i].state == N2C_CTX_FREE) continue;
                        for (unsigned j = 0; j < N2C_MAX_BOS; j++) {
                                struct n2c_bo *b = core->ctxs[i].bos[j];
                                if (!b || b->state != N2C_BO_MAPPED || !(b->flags & N2C_BO_IMPORTED) || !b->cookie ||
                                    ((BufferOwner *)b->cookie)->surface != (SweetSurface *)surface) continue;
                                if (acquire) (void)n2c_bo_cpu_acquire(core, i, b->handle, NULL);
                                else (void)n2c_bo_cpu_dirty(core, i, b->handle);
                        }
                }
        }
        IOLockUnlock(lock);
}
/* True while a not-yet-finished job declared (write_mask) that it writes an import of this surface.  The framebuffer
 * keeps showing the previous complete frame meanwhile (WAIT/WAIT_SEQ completion is what clears it). */
bool Nord2GPU::surfaceBusy(void *surface) {
        if (!surface || !ready || !core) return false;
        bool busy = false;
        IOLockLock(lock);
        if (!stopping) {
                for (unsigned i = 0; i < N2C_MAX_JOBS && !busy; i++) {
                        const struct n2c_job *j = &core->jobs[i];
                        if (j->state != N2C_JOB_QUEUED && j->state != N2C_JOB_RUNNING && j->state != N2C_JOB_STOPPING) continue;
                        for (unsigned k = 0; k < j->nr_bos && !busy; k++) {
                                const struct n2c_bo *b = j->bos[k];
                                busy = ((j->write_mask >> k) & 1u) && (b->flags & N2C_BO_IMPORTED) && b->cookie &&
                                       ((BufferOwner *)b->cookie)->surface == (SweetSurface *)surface;
                        }
                }
                if (!busy) busy = n2c_resv_busy(core, surface);   /* 0112: a frame that has not been submitted yet but is promised */
        }
        IOLockUnlock(lock);
        return busy;
}
extern "C" bool nord2_gpu_surface_busy(void *surface) { return gSurfaceGpu ? gSurfaceGpu->surfaceBusy(surface) : false; }
/* 0148: the same predicate as surfaceBusy, but reporting what holds the surface (called only from the present thread when a swap is skipped or forced). */
void Nord2GPU::surfaceBusyWhy(void *surface, uint32_t *match, uint64_t *oldestAgeUs, uint64_t *oldestId, uint32_t *oldestState, uint32_t *inflight, uint32_t *queued, uint32_t *resv) {
        *match = 0; *oldestAgeUs = 0; *oldestId = 0; *oldestState = 0; *inflight = 0; *queued = 0; *resv = 0;
        if (!surface || !ready || !core) return;
        IOLockLock(lock);
        if (!stopping) {
                uint64_t t = now_us(NULL);
                for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                        const struct n2c_job *j = &core->jobs[i];
                        if (j->state != N2C_JOB_QUEUED && j->state != N2C_JOB_RUNNING && j->state != N2C_JOB_STOPPING) continue;
                        (*inflight)++;
                        if (j->state == N2C_JOB_QUEUED) (*queued)++;
                        bool m = false;
                        for (unsigned k = 0; k < j->nr_bos && !m; k++) {
                                const struct n2c_bo *b = j->bos[k];
                                m = ((j->write_mask >> k) & 1u) && (b->flags & N2C_BO_IMPORTED) && b->cookie &&
                                    ((BufferOwner *)b->cookie)->surface == (SweetSurface *)surface;
                        }
                        if (!m) continue;
                        (*match)++;
                        uint64_t age = t > j->t_submit_us ? t - j->t_submit_us : 0;
                        if (age >= *oldestAgeUs) { *oldestAgeUs = age; *oldestId = j->id; *oldestState = j->state; }
                }
                *resv = n2c_resv_busy(core, surface) ? 1 : 0;
        }
        IOLockUnlock(lock);
}
extern "C" void nord2_gpu_surface_busy_why(void *surface, uint32_t *match, uint64_t *oldest_age_us, uint64_t *oldest_id, uint32_t *oldest_state,
    uint32_t *inflight, uint32_t *queued, uint32_t *resv) {
        if (gSurfaceGpu) { gSurfaceGpu->surfaceBusyWhy(surface, match, oldest_age_us, oldest_id, oldest_state, inflight, queued, resv); }
        else { *match = 0; *oldest_age_us = 0; *oldest_id = 0; *oldest_state = 0; *inflight = 0; *queued = 0; *resv = 0; }
}
extern "C" uint64_t nord2_gpu_resv_expired(void) { return gSurfaceGpu && gSurfaceGpu->core ? gSurfaceGpu->core->stats.resv_expired : 0; }
extern "C" void nord2_gpu_surface_cpu_acquire(void *surface) { if (gSurfaceGpu) gSurfaceGpu->surfaceHook(surface, true); }
extern "C" void nord2_gpu_surface_cpu_release(void *surface) { if (gSurfaceGpu) gSurfaceGpu->surfaceHook(surface, false); }
static uint32_t gic_rd32(void *c, uint32_t off) { return read_bank(c, off); }
static void gic_wr32(void *c, uint32_t off, uint32_t v) { write_bank(c, off, v); }
static uint64_t gic_rd64(void *c, uint32_t off) {
        Bank *b = (Bank *)c; if (!b->p || (off & 7) || off + 8 > b->bytes) return 0;
        uint64_t v = *(volatile uint64_t *)((volatile uint8_t *)b->p + off); __asm__ volatile("dmb oshld" ::: "memory"); return v;
}
static void gic_wr64(void *c, uint32_t off, uint64_t v) {
        Bank *b = (Bank *)c; if (!b->p || (off & 7) || off + 8 > b->bytes) return;
        __asm__ volatile("dmb oshst" ::: "memory"); *(volatile uint64_t *)((volatile uint8_t *)b->p + off) = v; __asm__ volatile("dsb osh" ::: "memory");
}
void Nord2GPU::irqSetup() {
        irqTried = true;
        DTEntry dt; const void *prop = NULL; unsigned len = 0; uint64_t reg[4];
        if (SecureDTLookupEntry(NULL, "/arm-io/gic", &dt) != kSuccess || SecureDTGetProperty(dt, "reg", &prop, &len) != kSuccess ||
            len < sizeof(reg)) { irqState = NORD2_IRQ_FAILED; IOLog("NORD2 GPU IRQ: GIC node unavailable, polling\n"); return; }
        memcpy(reg, prop, sizeof(reg));
        uint64_t size = reg[1] > 0x10000 ? 0x10000 : reg[1];
        if (size < 0x7000 || !map_bank(&gicd, reg[0] + 0x01000000ull, (unsigned)size)) {
                irqState = NORD2_IRQ_FAILED; IOLog("NORD2 GPU IRQ: cannot map GICD, polling\n"); return;
        }
        struct nord2_gic_ops g = { gic_rd32, gic_wr32, gic_rd64, gic_wr64, &gicd };
        irqIntid = nord2_gic_intid_for_spi(irqSpi);
        bool ok;
        disable_preemption();   /* the SPI is routed to, and the handler installed on, the PE running this code */
        ok = nord2_gic_spi_configure(&g, irqIntid, nord2_gic_irouter_for_mpidr(__builtin_arm_rsr64("MPIDR_EL1")), NORD2_GPU_IRQ_PRIORITY);
        if (ok) { irqOwner = this; ml_install_interrupt_handler(this, 0, this, (IOInterruptHandler)irqDispatch, NULL); }
        enable_preemption();
        irqState = ok ? NORD2_IRQ_LEARN : NORD2_IRQ_FAILED;
        IOLog("NORD2 GPU IRQ: spi=%u intid=%u configure=%s state=%s\n", irqSpi, irqIntid, ok ? "ok" : "refused",
              ok ? "learning (SPI stays disabled until seen pending)" : "polling");
}
void Nord2GPU::irqArm() {
        if (core->state != N2C_CORE_READY || !core->gpu_powered) return;
        irqFlag = 0;
        write_bank(&gpu, N2HW_JOB_IRQ_MASK, NORD2_GPU_JOB_IRQ_ALL); irqArmed = true;
}
void Nord2GPU::irqTop() {   /* interrupt context: MMIO + wakeup only */
        uint32_t st = read_bank(&gpu, N2HW_JOB_IRQ_STATUS);
        write_bank(&gpu, N2HW_JOB_IRQ_MASK, 0); irqArmed = false;
        irqTops++;
        if (!nord2_irq_guard_note(&irqGuard, st)) {
                struct nord2_gic_ops g = { gic_rd32, gic_wr32, gic_rd64, gic_wr64, &gicd };
                nord2_gic_spi_enable(&g, irqIntid, false);
        }
        irqFlag = 1; thread_wakeup((event_t)&irqFlag);
        if (timer) timer->setTimeout(1, kMicrosecondScale);   /* 0140: the bottom half (n2c_poll: complete this job, start the next) runs now, not at the next tick */
}
void Nord2GPU::irqDispatch(void *, void *, void *, int source) {
        Nord2GPU *d = irqOwner;
        if (d && (uint32_t)source == d->irqIntid) d->irqTop();
}
void Nord2GPU::irqLearnCheck() {   /* before n2c_poll clears RAWSTAT */
        if (irqState != NORD2_IRQ_LEARN || !irqArmed || !read_bank(&gpu, N2HW_JOB_IRQ_RAWSTAT)) return;
        struct nord2_gic_ops g = { gic_rd32, gic_wr32, gic_rd64, gic_wr64, &gicd };
        for (unsigned i = 0; i < 8; i++) {
                if (nord2_gic_spi_pending(&g, irqIntid)) {
                        irqState = NORD2_IRQ_ACTIVE; nord2_gic_spi_enable(&g, irqIntid, true);
                        IOLog("NORD2 GPU IRQ: intid %u pending with the job line asserted: confirmed, interrupt completion active\n", irqIntid);
                        return;
                }
                IODelay(5);
        }
        if (++irqLearnMisses >= NORD2_GPU_IRQ_LEARN_JOBS) {
                irqState = NORD2_IRQ_FAILED; write_bank(&gpu, N2HW_JOB_IRQ_MASK, 0); irqArmed = false;
                IOLog("NORD2 GPU IRQ: intid %u never pending (%u jobs): wrong SPI mapping or masked in GIC, polling\n", irqIntid, irqLearnMisses);
        }
}
/* One wait iteration, gpu->lock held on entry and exit. */
void Nord2GPU::waitStep(uint64_t elapsed_us) {
        if (irqWanted && !irqTried && ready) irqSetup();
        if (irqState == NORD2_IRQ_ACTIVE && irqGuard.tripped) {
                irqState = NORD2_IRQ_FAILED; write_bank(&gpu, N2HW_JOB_IRQ_MASK, 0); irqArmed = false;
                IOLog("NORD2 GPU IRQ: %u consecutive empty interrupts (%u total): SPI disabled, polling\n", NORD2_GPU_IRQ_SPURIOUS_MAX, irqGuard.total);
        }
        unsigned step = nord2_gpu_wait_step_us(elapsed_us);
        if (irqState == NORD2_IRQ_LEARN) irqArm();              /* arm now, observe the SPI on the next iteration */
        if (irqState == NORD2_IRQ_ACTIVE) {
                irqArm();                                       /* flag cleared first, then armed, then (caller) re-polled below */
                IOLockUnlock(lock);
                irqSleeps++;
                wait_result_t r = assert_wait_timeout((event_t)&irqFlag, THREAD_UNINT, nord2_gpu_irq_slice_us(elapsed_us), NSEC_PER_USEC);
                if (r == THREAD_WAITING) {
                        if (irqFlag) clear_wait(current_thread(), THREAD_AWAKENED);
                        else if (thread_block(THREAD_CONTINUE_NULL) == THREAD_TIMED_OUT) irqTimeouts++;
                }
                IOLockLock(lock);
                return;
        }
        IOLockUnlock(lock); if (step) IODelay(step); else if (elapsed_us < 40000) IODelay(250); else IOSleep(1); IOLockLock(lock);   /* 0131: was IOSleep(1) for every wait longer than 6 ms */
}
/* ---- LVTS thermal sensing (opt-in; see nord2_gpu_lvts.h for the safety model) ---- */
uint32_t Nord2GPU::lvtsRd(void *p, enum n2lvts_block b, uint32_t off) {
        Nord2GPU *d = (Nord2GPU *)p;
        return read_bank(b == N2LVTS_AP ? &d->lvtsAp : b == N2LVTS_MCU ? &d->lvtsMcu : &d->infra, off);
}
void Nord2GPU::lvtsWr(void *p, enum n2lvts_block b, uint32_t off, uint32_t v) {
        Nord2GPU *d = (Nord2GPU *)p;
        write_bank(b == N2LVTS_AP ? &d->lvtsAp : b == N2LVTS_MCU ? &d->lvtsMcu : &d->infra, off, v);
}
void Nord2GPU::lvtsSetup() {
        lvtsTried = true;
        if (!lvtsMode) return;
        n2lvts_ops o = { lvtsRd, lvtsWr, delay_us, log_msg };
        if (!map_bank(&lvtsAp, N2LVTS_AP_PHYS, N2LVTS_BLOCK_BYTES) ||
            ((lvtsMask & 7u) && !map_bank(&lvtsMcu, N2LVTS_MCU_PHYS, N2LVTS_BLOCK_BYTES)) || !n2lvts_init(&lvts, &o, this)) {
                lvtsState = 3; IOLog("NORD2 GPU LVTS: cannot map thermal controllers\n"); return;
        }
        int golden = 0;
        if (PE_parse_boot_argn("nord2_lvts_golden", &golden, sizeof(golden)) && golden > 0) lvts.cal.golden = (uint32_t)golden;
        enum n2lvts_error e = N2LVTS_OK;
        if (lvtsMode >= 2) {
                e = n2lvts_start(&lvts, lvtsMask, lvtsMode >= 3);
                if (e == N2LVTS_OK) {
                        lvtsState = 2;
                        for (unsigned i = 0; i < 40; i++) {            /* first sensor interval is ~20 ms; allow 200 ms */
                                if (n2lvts_read(&lvts, lvtsMask)) break;
                                IOSleep(5);
                        }
                } else lvtsState = 3;
        } else lvtsState = 1;
        n2lvts_probe probe = {}; bool pr = (n2lvts_probe_ctrl(&lvts, N2LVTS_CTRL_GPU, &probe) == N2LVTS_OK);
        unsigned good = n2lvts_read(&lvts, lvtsMask);
        IOLog("NORD2 GPU LVTS: mode=%u mask=%x start=%s state=%u gate_open=%u cal=%s valid_sensors=%u gpu_ctrl clken=%x monctl0=%x id=%x msr0=%x step=%u\n",
              lvtsMode, lvtsMask, n2lvts_error_string(e), lvtsState, pr ? 1u : (unsigned)probe.gate_open,
              lvts.cal.defaulted ? "default(approx)" : "efuse", good, probe.clken, probe.monctl0, probe.id, probe.msr[0], lvts.step);
}
bool Nord2GPU::lvtsGpu(int32_t *mC) {
        if (!lvtsMode || lvtsState == 3 || !lvtsState) return false;
        n2lvts_read(&lvts, N2LVTS_MASK_GPU);
        bool a = lvts.temp_ok[10], b = lvts.temp_ok[11];
        if (!a && !b) return false;
        *mC = a && b ? (lvts.temp_mC[10] > lvts.temp_mC[11] ? lvts.temp_mC[10] : lvts.temp_mC[11]) : (a ? lvts.temp_mC[10] : lvts.temp_mC[11]);
        return true;
}
int Nord2GPU::lvtsHottest(uint32_t mask) {   /* centi-C of the hottest valid sensor of the controllers in mask, or INT32_MIN */
        int best = (int)0x80000000; n2lvts_read(&lvts, mask);
        for (unsigned c = 0; c < N2LVTS_CTRL_COUNT; c++) if (mask & (1u << c))
                for (unsigned j = 0; j < n2lvts_ctrl[c].nsens; j++) {
                        unsigned s = n2lvts_ctrl[c].first_sensor + j;
                        if (lvts.temp_ok[s] && lvts.temp_mC[s] / 10 > best) best = lvts.temp_mC[s] / 10;
                }
        return best;
}
/* Battery fuel-gauge temperature (centi-C) from the SweetBattery power source, if it is a real
 * measurement; the SoC/GPU thermal sensors (LVTS) are not driven by this port. */
static bool battery_temperature(int *centi) {
        OSDictionary *m = IOService::serviceMatching("SweetBattery");
        if (!m) return false;
        OSPtr<IOService> b = IOService::copyMatchingService(m); m->release();
        if (!b) return false;
        OSNumber *t = OSDynamicCast(OSNumber, b->getProperty("Temperature"));
        OSBoolean *v = OSDynamicCast(OSBoolean, b->getProperty("Nord2BatteryTemperatureValid"));
        if (!t || !v || v != kOSBooleanTrue) return false;
        *centi = (int)(int32_t)t->unsigned32BitValue(); return true;
}
/* 0116, used by bsd/dev/nord2_cpufreq.c (CPU DVFS init): read-only access to the PMIC registers the stock regulators read for the init voltages.  bus 0 = MT6315 on SPMI
 * (8-bit register of slave `sid`), bus 1 = MT6359P on PWRAP (16-bit register).  Runs on the owner thread like every other rail transaction; returns 0 and the value, or
 * -1 when the GPU driver is not ready, the owner thread is not polling, or the read failed.  Never writes. */
struct PmicReq { Nord2GPU *d; unsigned bus, sid, reg, val; int rc; };
int Nord2GPU::pmicRead(void *p) {
        PmicReq *q = (PmicReq *)p;
        if (q->bus == 0) { uint8_t v = 0; q->rc = n2pmif_spmi_read(&q->d->pm15, (uint8_t)q->sid, (uint16_t)q->reg, &v); q->val = v; }
        else { uint16_t v = 0; q->rc = n2pmif_pwrap_read(&q->d->pm59, (uint16_t)q->reg, &v); q->val = v; }
        return q->rc;
}
extern "C" int nord2_gpu_pmic_ready(void) {
        Nord2GPU *d = gSurfaceGpu;
        return d && d->ready && !d->stopping && n2owner_ready(&gpu_owner_queue);
}
extern "C" int nord2_pmic_read(unsigned bus, unsigned sid, unsigned reg, unsigned *val) {
        Nord2GPU *d = gSurfaceGpu;
        if (!d || !d->ready || d->stopping || !val || bus > 1 || !n2owner_ready(&gpu_owner_queue)) return -1;
        PmicReq q = { d, bus, sid, reg, 0, -1 };
        int result = -1;
        IOLockLock(d->lock);
        n2owner_wait wait = { now_ms, sleep_ms, NULL };
        bool ok = !d->stopping && n2owner_call(&gpu_owner_queue, Nord2GPU::pmicRead, &q, &wait, 1000, &result);
        IOLockUnlock(d->lock);
        if (!ok || result || q.rc) return -1;
        *val = q.val;
        return 0;
}
/* Used by bsd/dev/nord2_cpufreq.c (patch 0104): the battery gauge is the only temperature the CPU DVFS guard has. */
extern "C" bool nord2_battery_temperature_cC(int *centi) { return battery_temperature(centi); }
void Nord2GPU::emitJobStats() {
        if (!jobStats || !core) return;
        n2c_timing t;
        if (!n2c_timing_take(core, &t, 2000000ull)) return;
        /* Context for the numbers below: PLL frequency actually programmed (APMIXED, read-only),
         * the GPU mux parent (1 = MFGPLL) and the battery temperature. */
        uint32_t pll_khz = n2rail_mfgpll_khz(read_bank(&clocks[N2CLK_APMIXED], N2CLK_MFGPLL_CON1));
        uint32_t mux = (read_bank(&clocks[N2CLK_TOPCK], N2CLK_CFG_5) >> N2CLK_MFG_MUX_SHIFT) & N2CLK_MFG_MUX_MASK;
        int bat = 0; bool bat_ok = battery_temperature(&bat);
        if (lvtsMode && !lvtsTried) lvtsSetup();
        int32_t gmC = 0; bool gpu_ok = lvtsGpu(&gmC);
        int hot = (lvtsMode && lvtsState && lvtsState != 3) ? lvtsHottest(lvtsMask) : (int)0x80000000;
        uint32_t n = t.jobs ? t.jobs : 1, w = waitN ? waitN : 1;
        /* 0131: three short lines instead of one of ~900 characters, which the console cut after about 500 (the cache tracking and reservation fields were lost). */
        IOLog("NORD2 GPU JOBS: n=%u fault=%u full=%u win_ms=%llu q_us=%llu/%u run_us=%llu/%u tot_us=%llu/%u clean_us=%llu/%u inv_us=%llu/%u clean_KiB=%llu inv_KiB=%llu\n",
              t.jobs, t.faulted, t.full_jobs, t.window_start_us / 1000, t.sum_queue_us / n, t.max_queue_us,
              t.sum_run_us / n, t.max_run_us, t.sum_total_us / n, t.max_total_us, t.sum_clean_us / n, t.max_clean_us,
              t.sum_inval_us / n, t.max_inval_us, t.sum_clean_pages * 4, t.sum_inval_pages * 4);
        IOLog("NORD2 GPU JOBS2: waits=%u gap_us=%llu/%u wait_us=%llu/%u ret_us=%llu/%u seqw=%u/%u/%llu/%u gpu_khz=%u mux=%u irq=%u:%u/%u/%u/%u\n",
              waitN, waitGapSum / w, waitGapMax, waitDurSum / w, waitDurMax, waitRetSum / w, waitRetMax,
              seqWaits, seqJobs, seqWaits ? seqDurSum / seqWaits : 0ull, seqDurMax, pll_khz, mux,
              (unsigned)irqState, irqTops, irqGuard.spurious_total, irqSleeps, irqTimeouts);
        IOLog("NORD2 GPU JOBS3: bat_cC=%d bat_ok=%u gpu_cC=%d soc_cC=%d lvts=%u%s trk=%u skipC_KiB=%llu skipI_KiB=%llu defI_KiB=%llu acqI_KiB=%llu wmI_KiB=%llu resv=%llu/%llu/%llu/%llu\n",
              bat_ok ? bat : 0, (unsigned)bat_ok, gpu_ok ? gmC / 10 : -9999, hot != (int)0x80000000 ? hot : -9999,
              lvtsState, (lvtsState && lvts.cal.defaulted) ? "~" : "",
              (unsigned)core->track_level, core->stats.cache_skip_clean_pages * 4, core->stats.cache_skip_inval_pages * 4,
              core->stats.cache_deferred_pages * 4, core->stats.cache_acquire_pages * 4, core->stats.cache_skip_wmask_pages * 4,
              core->stats.resv_reserves, core->stats.resv_retires, core->stats.resv_cancels, core->stats.resv_expired);
        waitN = waitGapMax = waitDurMax = waitRetMax = 0; waitGapSum = waitDurSum = waitRetSum = 0;
        seqWaits = seqJobs = seqDurMax = 0; seqDurSum = 0;
        if (pfMode > 0) prfcntSample();
}
/* patch 0127: the real GPU core clock.  GPU_CONTROL CYCLE_COUNT (0x90/0x94) counts core cycles once GPU_COMMAND 5 (CYCLE_COUNT_START) was issued, TIMESTAMP (0x98/0x9c)
 * runs on a fixed reference.  cycles per microsecond of CPU time between two stats windows is the frequency the shader cores really run at; the PLL value the driver
 * logs (gpu_khz) says what was programmed.  Read-only apart from the harmless start command. */
void Nord2GPU::clockCheck() {
        if (!core || core->state != N2C_CORE_READY || !core->gpu_powered) { ckRunning = false; return; }
        if (!ckRunning) {
                write_bank(&gpu, 0x30, 5u);
                ckCycles = ((uint64_t)read_bank(&gpu, 0x94) << 32) | read_bank(&gpu, 0x90);
                ckStamp = ((uint64_t)read_bank(&gpu, 0x9c) << 32) | read_bank(&gpu, 0x98);
                ckUs = now_us(NULL); ckRunning = true; return;
        }
        uint32_t lo = read_bank(&gpu, 0x90), hi = read_bank(&gpu, 0x94);
        uint64_t cyc = ((uint64_t)hi << 32) | lo;
        uint64_t st = ((uint64_t)read_bank(&gpu, 0x9c) << 32) | read_bank(&gpu, 0x98);
        uint64_t t = now_us(NULL);
        if (cyc >= ckCycles && t > ckUs) {
                uint64_t dc = cyc - ckCycles, ds = st - ckStamp, dt = t - ckUs;
                IOLog("NORD2 GPUCLK: win_us=%llu cycles=%llu core_khz=%llu stamp_ticks=%llu stamp_khz=%llu\n", dt, dc, dc * 1000 / dt, ds, ds * 1000 / dt);
        } else IOLog("NORD2 GPUCLK: counter restarted (cycles %llu -> %llu)\n", ckCycles, cyc);
        ckCycles = cyc; ckStamp = st; ckUs = t;
}
/* patch 0123 (n2pf=1): hardware counters, v2.  The v1 sampler (0122) spun up to 20 ms with the driver lock held and, after its first sample timeout, left the job
 * manager waiting behind the unfinished dump (job queue delays of 2..13 s).  v2 never waits: it issues PRFCNT_SAMPLE in one stats window and collects the dump in the next
 * one, drops everything when the GPU was power-cycled in between, and on a stuck sample prints the fault registers, switches the counters off (PRFCNT_CONFIG = 0) and stops.
 * GPU_CONTROL: PRFCNT_BASE 0x60/0x64, PRFCNT_CONFIG 0x68 (mode 1 = manual, AS in bits 7:4), JM_EN 0x6c, SHADER_EN 0x70, TILER_EN 0x74, MMU_L2_EN 0x7c; GPU_COMMAND 3 = clear,
 * 4 = sample; GPU_IRQ_RAWSTAT bit 16 = sample completed.  Dump layout seen on the device: block 0 job manager, 1 tiler, 2..5 memory system (4 slices), then one block per
 * shader core at 6 + core index (present mask 0x660067 = cores 0,1,2,5,6,17,18,21,22); 64 counters per block, offsets as in Mesa's G77.xml. */
#define N2PF_VA 0x200000000ull
#define N2PF_SHADER_BASE 6u
extern "C" {
extern unsigned int vm_page_free_count, vm_page_active_count, vm_page_inactive_count, vm_page_wire_count, vm_page_speculative_count;
extern unsigned int vm_page_pageable_internal_count, vm_page_pageable_external_count, vm_page_throttled_count, vm_page_free_target;
extern unsigned int memorystatus_available_pages, c_segment_pages_compressed;
}
void Nord2GPU::prfcntSample() {
        /* Memory state of the iOS window (16 KiB pages), once per stats window. */
        IOLog("NORD2 MEM: free=%u wired=%u active=%u inactive=%u spec=%u throttled=%u pageable_int=%u pageable_ext=%u avail=%u compressed=%u free_target=%u (16K pages)\n",
              vm_page_free_count, vm_page_wire_count, vm_page_active_count, vm_page_inactive_count, vm_page_speculative_count, vm_page_throttled_count,
              vm_page_pageable_internal_count, vm_page_pageable_external_count, memorystatus_available_pages, c_segment_pages_compressed, vm_page_free_target);
        if (core) {   /* 0185: released-but-still-mapped buffers (held for older jobs), live buffers, handle table fill of the fullest context */
                unsigned held = 0, fullest = 0;
                for (unsigned i = 1; i < N2C_MAX_CTX; i++) { unsigned used = 0; for (unsigned k = 0; k < N2C_MAX_BOS; k++) { struct n2c_bo *b = core->ctxs[i].bos[k]; if (b) { used++; if (!b->refs && b->state == N2C_BO_MAPPED) held++; } } if (used > fullest) fullest = used; }
                IOLog("NORD2 MEM GPU: bos_live=%llu released_held=%u fullest_handle_table=%u/%u private=%llu MiB\n", (unsigned long long)core->stats.bos_live, held, fullest, (unsigned)N2C_MAX_BOS, (unsigned long long)(privateTotal() >> 20));
        }
        clockCheck();
        if (pfMode == 2 || pfFailed || !core) return;                       /* n2pf=2: memory statistics and clock only, the GPU counters stay off */
        /* patch 0128 (n2pf=1): v3.  v2 issued PRFCNT_SAMPLE and looked at the completion bit one stats window (2 s) later; when the sample did not complete the job manager
         * held every new job behind it (job queue delays of 2.1 s) until the counters were switched off.  v3 polls for at most ~1 ms in the same call (the caller holds the
         * driver lock, jobs are not being started meanwhile), and on a timeout switches PRFCNT off at once (that releases the job manager), prints which job slots were busy
         * and the job/MMU registers, re-arms in the next window and gives up after 8 failures. */
        if (!(core->state == N2C_CORE_READY && core->gpu_powered)) { pfArmed = false; return; }
        if (!pfMd) {
                pfMd = IOBufferMemoryDescriptor::withOptions(kIODirectionInOut, 16384, 16384);
                IOByteCount length = 0;
                if (pfMd) { pfPa = pfMd->getPhysicalSegment(0, &length, kIOMemoryMapperNone); pfCpu = (uint8_t *)pfMd->getBytesNoCopy(); }
                if (!pfMd || !valid_ram(pfPa) || length < 16384) { IOLog("NORD2 PRFCNT: no dump memory\n"); pfFailed = true; return; }
                pfBytes = 16384;
                const nord2_gpu_mmu_ops mo = { allocTable, freeTable, flushTable, alloc_host, free_host, log_msg };
                bool ok = nord2_gpu_mmu_init(&pfmmu, &mo, this, 4);
                for (unsigned i = 0; ok && i < pfBytes / 4096; i++)
                        ok = nord2_gpu_mmu_map(&pfmmu, N2PF_VA + i * 4096ull, pfPa + i * 4096ull, 4096, NORD2_GPU_MEMATTR_NON_CACHEABLE, true, false);
                for (unsigned k = 0; ok && k < 3; k++) {
                        pfXmd[k] = IOBufferMemoryDescriptor::withOptions(kIODirectionInOut, 16384, 16384);
                        IOByteCount xl = 0;
                        uint64_t xpa = pfXmd[k] ? pfXmd[k]->getPhysicalSegment(0, &xl, kIOMemoryMapperNone) : 0;
                        if (!pfXmd[k] || !valid_ram(xpa) || xl < 16384) { ok = false; break; }
                        bzero(pfXmd[k]->getBytesNoCopy(), 16384);
                        sync_page(NULL, xpa, pfXmd[k]->getBytesNoCopy(), 16384, true);
                        for (unsigned i = 0; ok && i < 4; i++)
                                ok = nord2_gpu_mmu_map(&pfmmu, N2PF_VA + (k + 1) * 16384ull + i * 4096ull, xpa + i * 4096ull, 4096, NORD2_GPU_MEMATTR_NON_CACHEABLE, true, false);
                }
                if (!ok) { IOLog("NORD2 PRFCNT: mapping failed\n"); pfFailed = true; return; }
        }
        if (!pfArmed || read_bank(&gpu, 0x68) != 1u) {      /* first use, after a power cycle or after a failed sample: program AS0 and the counters, the window is only armed */
                write_bank(&gpu, N2HW_MMU_IRQ_CLEAR, 0x00010001u);         /* a stale AS0 fault would keep the address space blocked */
                if (n2hw_as_configure(&hw, 0, pfmmu.root_pa) != N2HW_OK) { IOLog("NORD2 PRFCNT: AS0 configure failed\n"); pfFailed = true; return; }
                write_bank(&gpu, 0x60, (uint32_t)N2PF_VA); write_bank(&gpu, 0x64, (uint32_t)(N2PF_VA >> 32));
                write_bank(&gpu, 0x6c, 0xffffffffu); write_bank(&gpu, 0x70, 0xffffffffu);
                write_bank(&gpu, 0x74, 0xffffffffu); write_bank(&gpu, 0x7c, 0xffffffffu);
                write_bank(&gpu, 0x68, 1u);
                write_bank(&gpu, 0x30, 3u);
                pfArmed = true; pfSinceArm = 0; pfLastUs = now_us(NULL); pfArms++;
                if (pfArms <= 3) IOLog("NORD2 PRFCNT: armed (AS0, VA %llx), l2_features=%08x shader_present=%08x%08x\n", N2PF_VA, read_bank(&gpu, N2HW_L2_FEATURES),
                                       read_bank(&gpu, N2HW_SHADER_PRESENT_HI), read_bank(&gpu, N2HW_SHADER_PRESENT_LO));
                return;
        }
        if (read_bank(&gpu, N2HW_MMU_IRQ_RAWSTAT) & 1u) { IOLog("NORD2 PRFCNT: stale AS0 fault before the sample: status %08x address %08x%08x extra %08x\n", read_bank(&gpu, 0x241c), read_bank(&gpu, 0x2424), read_bank(&gpu, 0x2420), read_bank(&gpu, 0x2438)); write_bank(&gpu, N2HW_MMU_IRQ_CLEAR, 0x00010001u); pfArmed = false; return; }
        /* 0130: the hardware advances its dump pointer by one dump (about 7.4 KiB) per PRFCNT_SAMPLE and only restarts at PRFCNT_BASE; v4 read offset 0 every time, so one sample in
         * eight carried data and the ninth wrote past the 64 KiB mapping (AS0 translation fault 0x240003c3 at VA+0x10000).  Rewrite BASE before every sample, clear all four dump areas
         * and re-arm after six samples anyway. */
        if (++pfSinceArm > 6) { pfArmed = false; pfSinceArm = 0; return; }
        write_bank(&gpu, 0x60, (uint32_t)N2PF_VA); write_bank(&gpu, 0x64, (uint32_t)(N2PF_VA >> 32));
        bzero(pfCpu, pfBytes);
        sync_page(NULL, pfPa, pfCpu, pfBytes, true);
        for (unsigned k = 0; k < 3; k++) if (pfXmd[k]) { bzero(pfXmd[k]->getBytesNoCopy(), 16384); sync_page(NULL, 0, pfXmd[k]->getBytesNoCopy(), 16384, true); }
        write_bank(&gpu, 0x24, 1u << 16);
        unsigned busy = (n2hw_js_idle(&hw, 0) ? 0u : 1u) | (n2hw_js_idle(&hw, 1) ? 0u : 2u);
        uint64_t t0 = now_us(NULL);
        write_bank(&gpu, 0x30, 4u);
        unsigned spin = 0;
        while (!(read_bank(&gpu, 0x20) & (1u << 16)) && spin++ < 500) IODelay(1);
        uint64_t t1 = now_us(NULL);
        if (!(read_bank(&gpu, 0x20) & (1u << 16))) {
                pfFails++;
                IOLog("NORD2 PRFCNT: incomplete after %llu us busy=%u/%u fail=%u\n", t1 - t0, busy, (n2hw_js_idle(&hw, 0) ? 0u : 1u) | (n2hw_js_idle(&hw, 1) ? 0u : 2u), pfFails);
                IOLog("NORD2 PRFCNT: job_irq=%08x js_state=%08x js0=%08x js1=%08x gpu_irq=%08x mmu_irq=%08x gpu_fault=%08x\n", read_bank(&gpu, N2HW_JOB_IRQ_RAWSTAT), read_bank(&gpu, 0x1010),
                      read_bank(&gpu, 0x1824), read_bank(&gpu, 0x18a4), read_bank(&gpu, 0x20), read_bank(&gpu, N2HW_MMU_IRQ_RAWSTAT), read_bank(&gpu, N2HW_GPU_FAULTSTATUS));
                IOLog("NORD2 PRFCNT: AS0 fault status %08x address %08x%08x extra %08x\n", read_bank(&gpu, 0x241c), read_bank(&gpu, 0x2424), read_bank(&gpu, 0x2420), read_bank(&gpu, 0x2438));
                write_bank(&gpu, 0x68, 0u);                         /* releases the job manager */
                write_bank(&gpu, N2HW_MMU_IRQ_CLEAR, 0x00010001u);
                pfArmed = false;
                if (pfFails >= 8) pfFailed = true;
                return;
        }
        write_bank(&gpu, 0x24, 1u << 16);
        sync_page(NULL, pfPa, pfCpu, pfBytes, true);
        uint64_t winUs = t0 - pfLastUs; pfLastUs = t0;
        const uint32_t *w = (const uint32_t *)pfCpu;
        uint64_t jm[64] = {}, ti[64] = {}, ms[64] = {}, sh[64] = {};
        unsigned cores = 0;
        for (unsigned i = 0; i < 64; i++) { jm[i] = w[i]; ti[i] = w[64 + i]; }
        for (unsigned b = 2; b < N2PF_SHADER_BASE; b++) for (unsigned i = 0; i < 64; i++) ms[i] += w[b * 64 + i];
        uint64_t present = ((uint64_t)read_bank(&gpu, N2HW_SHADER_PRESENT_HI) << 32) | read_bank(&gpu, N2HW_SHADER_PRESENT_LO);
        for (unsigned c = 0; c < 29 && (N2PF_SHADER_BASE + c + 1) * 64 <= pfBytes / 4; c++) if (present >> c & 1) {
                cores++;
                for (unsigned i = 0; i < 64; i++) sh[i] += w[(N2PF_SHADER_BASE + c) * 64 + i];
        }
        /* Short lines only: the console cuts long lines and interleaves them with other output. */
        IOLog("NORD2 PRFCNT A win_us=%llu took_us=%llu busy=%u cores=%u gpu=%llu js0=%llu js1=%llu til=%llu\n", winUs, t1 - t0, busy, cores, jm[6], jm[10], jm[18], ti[4]);
        IOLog("NORD2 PRFCNT B l2look=%llu rlook=%llu wlook=%llu rbeat=%llu wbeat=%llu arstall=%llu wstall=%llu\n", ms[25], ms[26], ms[27], ms[32], ms[47], ms[33], ms[48]);
        IOLog("NORD2 PRFCNT C lat0_127=%llu lat128_191=%llu lat192_255=%llu lat256_319=%llu lat320_383=%llu\n", ms[37], ms[38], ms[39], ms[40], ms[41]);
        IOLog("NORD2 PRFCNT D frag=%llu exec=%llu fpk=%llu quads=%llu warps=%llu ptiles=%llu fma=%llu\n", sh[4], sh[26], sh[7], sh[11], sh[9], sh[18], sh[27]);
        IOLog("NORD2 PRFCNT E texflit=%llu texdfch=%llu texstall=%llu texstarve=%llu rdtex=%llu wrtib=%llu rdftc=%llu\n", sh[35], sh[36], sh[37], sh[38], sh[58], sh[62], sh[54]);
}
void Nord2GPU::budgetReject(const char *what, unsigned c, uint64_t request, uint64_t limit) {
        uint64_t t = now_ms(NULL);
        if (lastBudgetLog && t - lastBudgetLog < 1000) { budgetSuppressed++; return; }
        char name[17] = {}; proc_selfname(name, sizeof(name));
        IOLog("NORD2 GPU BUDGET: %s rejected pid=%d(%s) ctx=%u request=%llu KiB private=%llu KiB imported=%llu KiB all-private=%llu KiB limit=%llu KiB bos=%llu suppressed=%u\n",
              what, proc_selfpid(), name, c, request >> 10, (core->ctxs[c].wired - core->ctxs[c].imported) >> 10,
              core->ctxs[c].imported >> 10, privateTotal() >> 10, limit >> 10, core->stats.bos_live, budgetSuppressed);
        lastBudgetLog = t; budgetSuppressed = 0;
}
IOReturn Nord2GPU::mapBuffer(unsigned c, uint64_t bytes, uint32_t flags,
                            uint32_t *h, uint64_t *va, uint64_t *actual, bool dropLock, volatile bool *closedFlag) {
        if (!ready || !core || c >= N2C_MAX_CTX) return kIOReturnNotReady;
        uint64_t requested = bytes;
        if ((flags & ~7u) || ((flags & 2u) && (flags & 4u))) return kIOReturnBadArgument;   /* bit0 executable, bit1 GPU-only (never CPU-mapped), bit2 CPU write-combine (no CPU cache maintenance) */
        if (!nord2_gpu_allocation_size_limit(bytes, core->ctxs[c].wired - core->ctxs[c].imported, privateLimit, &bytes)) {
                budgetReject("private", c, requested, privateLimit); return kIOReturnBadArgument;
        }
        if (!nord2_gpu_global_private_ok(privateTotal(), bytes, globalLimit)) {
                budgetReject("global-private", c, requested, globalLimit); return kIOReturnNoMemory;
        }
        /* 0143: building the memory (allocation, wiring, zero fill, page walk) is the expensive part, a few ms and up to 100 ms for a big buffer: it
         * does not touch the core, so the global lock (job dispatch, completions) is released meanwhile when the caller is a client's ALLOC. */
        uint64_t t0 = now_us(NULL), t1 = t0, t2 = t0, t3 = t0, t4 = t0;
        if (dropLock) IOLockUnlock(lock);
        static unsigned extLogs; bool ext = (flags & 6u) != 0; bool lg = ext && extLogs < 4; if (lg) extLogs++;
        bool pageableWc = (flags & 4u) && (gAllocExt & 4u);
        if (lg) { IOLog("NORD2 GPU EXT: flags=%u bytes=%llu pageable=%d stage=md-create\n", flags, bytes, (int)pageableWc); }
        IOBufferMemoryDescriptor *md;
        if (pageableWc) {
                /* Pageable memory has a VM object of its own, so its cache mode can be set without touching the kernel's shared object */
                md = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionInOut | kIOMemoryPageable | kIOMapWriteCombineCache, (vm_size_t)bytes, 16384);
        } else {
                md = IOBufferMemoryDescriptor::withOptions(kIODirectionInOut | kIOMemoryKernelUserShared | ((flags & 4u) ? kIOMapWriteCombineCache : 0), (vm_size_t)bytes, 16384);
        }
        if (lg) { IOLog("NORD2 GPU EXT: flags=%u md=%p stage=md-created\n", flags, (void *)md); }
        t1 = now_us(NULL);
        if (!md) { if (dropLock) IOLockLock(lock); return kIOReturnNoMemory; }
        BufferOwner *owner = (BufferOwner *)IOMallocZero(sizeof(*owner));
        unsigned count = (unsigned)(bytes / 4096);
        n2c_page *pages = (n2c_page *)IOMallocZero(count * sizeof(*pages));
        if (!owner || !pages) { if (owner) IOFree(owner, sizeof(*owner)); if (pages) IOFree(pages, count*sizeof(*pages)); md->release(); if (dropLock) IOLockLock(lock); return kIOReturnNoMemory; }
        if (pageableWc) {
                if (md->prepare(kIODirectionInOut) != kIOReturnSuccess) { IOFree(owner, sizeof(*owner)); IOFree(pages, count*sizeof(*pages)); md->release(); if (dropLock) IOLockLock(lock); return kIOReturnNoMemory; }
                owner->prepared = true;
                t2 = now_us(NULL);
                if (lg) { IOLog("NORD2 GPU EXT: flags=%u stage=prepared\n", flags); }
                static uint8_t zeros[65536];   /* 0141: 64 KiB per call, not 4 KiB */
                for (uint64_t off = 0; off < bytes; off += sizeof(zeros)) { uint64_t n = bytes - off < sizeof(zeros) ? bytes - off : sizeof(zeros); md->writeBytes(off, zeros, (IOByteCount)n); }
        } else {
                if (lg) { IOLog("NORD2 GPU EXT: flags=%u stage=bzero\n", flags); }
                bzero(md->getBytesNoCopy(), (size_t)bytes);
        }
        t3 = now_us(NULL);
        if (lg) { IOLog("NORD2 GPU EXT: flags=%u stage=zeroed\n", flags); }
        bool valid = true;
        for (unsigned i = 0; i < count; i++) {
                IOByteCount length; pages[i].pa = md->getPhysicalSegment((uint64_t)i * 4096, &length, kIOMemoryMapperNone);
                pages[i].cpu = pageableWc ? NULL : (uint8_t *)md->getBytesNoCopy() + (uint64_t)i * 4096;
                if (!valid_ram(pages[i].pa) || length < 4096) valid = false;
        }
        t4 = now_us(NULL);
        if (lg) { IOLog("NORD2 GPU EXT: flags=%u stage=segments valid=%d\n", flags, (int)valid); }
        if (dropLock) {
                IOLockLock(lock);
                if (!ready || stopping || !core) { IOFree(pages, count * sizeof(*pages)); owner->md = md; free_owner(this, owner); return kIOReturnNotReady; }
                /* 0144: the connection may have been closed while the lock was down (its context could even be reused): the buffer is not mapped then */
                if (closedFlag && __atomic_load_n(closedFlag, __ATOMIC_ACQUIRE)) { IOFree(pages, count * sizeof(*pages)); owner->md = md; free_owner(this, owner); return kIOReturnNotOpen; }
        }
        owner->md = md; bool adopted = false;
        enum n2c_error e = valid ? n2c_bo_map(core, c, pages, count, N2C_BO_GPU_READ | N2C_BO_GPU_WRITE |
                ((flags & 1) ? N2C_BO_GPU_EXEC : 0) | ((flags & 2) ? N2C_BO_GPU_ONLY : 0) | ((flags & 4u) ? N2C_BO_CPU_UNCACHED : 0), owner, bytes, h, va, &adopted) : N2C_E_ARGS;
        IOFree(pages, count * sizeof(*pages)); if (!adopted) free_owner(this, owner);
        if (lg) { IOLog("NORD2 GPU EXT: flags=%u stage=mapped e=%d\n", flags, (int)e); }
        { static unsigned slowLogs; uint64_t t5 = now_us(NULL);
          if (t5 - t0 >= 2000 && slowLogs < 60) { slowLogs++;
                IOLog("NORD2 GPU ALLOC SLOW: bytes=%llu flags=%u md=%llu prepare=%llu zero=%llu segments=%llu map=%llu total=%llu us (lock %s)\n", bytes, flags,
                      t1 - t0, t2 > t1 ? t2 - t1 : 0, t3 > t2 ? t3 - t2 : 0, t4 - t3, t5 - t4, t5 - t0, dropLock ? "dropped" : "held"); } }
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
        if (!ready || !core || c >= N2C_MAX_CTX || !bytes || (offset & 4095)) return kIOReturnBadArgument;
        uint64_t mapped = ((uint64_t)bytes + 4095) & ~4095ull, charged;
        /* IOSurface owns these pages: charge the import budget, not the private cap. */
        if (!nord2_gpu_import_size(mapped, core->ctxs[c].imported, &charged)) {
                budgetReject("import", c, mapped, NORD2_GPU_IMPORT_LIMIT); return kIOReturnNoMemory;
        }
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
        /* Level 2: the surface's CPU access is bracketed by IOSurfaceLock/Unlock (see surfaceHook). */
        if (e==N2C_OK && core->track_level>=N2C_TRACK_EXPLICIT) (void)n2c_bo_track(core,c,*h);
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
                /* ABI v1: shader/FAU/resource tables hold raw pointers to buffers that are not listed. */
                sub.flags=N2C_SUBMIT_FULL_MAINT;
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
        IOExternalTrap *getTargetAndTrapForIndex(IOService **,UInt32) APPLE_KEXT_OVERRIDE;
        IOReturn trapCall(void *,void *,void *,void *,void *,void *);
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
        return io_error(n2c_ctx_open(gpu->core,(uint64_t)(uintptr_t)task,NORD2_GPU_CONTEXT_LIMIT,false,&context));
}
void Nord2GPUUserClient::closeContext() {
        if(!lock)return;
        IOLockLock(lock);__atomic_store_n(&closed,true,__ATOMIC_RELEASE);
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
        if(b && (b->flags & N2C_BO_GPU_ONLY))b=NULL;               /* GPU-only buffers have no CPU mapping, by promise */
        if(b)(void)n2c_bo_cpu_visible(gpu->core,context,type);     /* from now on the CPU may touch it: normal maintenance */
        BufferOwner *o=b?(BufferOwner *)b->cookie:NULL;
        if(b && (b->flags & N2C_BO_CPU_UNCACHED))*options=kIOMapWriteCombineCache;   /* 0135: the user mapping has the memory's own cache mode */
        { static unsigned wl; if(b && (b->flags & N2C_BO_CPU_UNCACHED) && wl<2){wl++;IOLog("NORD2 GPU EXT: clientMemoryForType type=%u wc-mapping-requested\n",(unsigned)type);} }
        if(o && o->md){*memory=o->md;(*memory)->retain();}
        IOLockUnlock(gpu->lock);IOLockUnlock(lock);return *memory?kIOReturnSuccess:kIOReturnNotFound;
}
/* IOConnectTrap6 fast path (0111, nord2_gpu_transport.h): the same handler as externalMethod, on the caller's thread, no Mach message.  Everything the user
 * passed is copied in first and validated (nord2_gpu_trap_call_ok); the shared handler then validates selector, counts and buffers exactly like the MIG
 * path.  The IOReturn of the method goes to `result`; the trap itself returns success once the call ran (`done` is the proof), and a refusal before
 * running leaves `done` unset so the caller falls back to IOConnectCallMethod. */
IOExternalTrap *Nord2GPUUserClient::getTargetAndTrapForIndex(IOService **target,UInt32 index) {
        static IOExternalTrap trap={NULL,(IOTrap)&Nord2GPUUserClient::trapCall};
        if(index!=NORD2_GPU_TRAP_CALL||!target)return NULL;
        *target=this;return &trap;
}
IOReturn Nord2GPUUserClient::trapCall(void *p1,void *p2,void *,void *,void *,void *) {
        struct nord2_gpu_trap_call c;user_addr_t ua=(user_addr_t)(uintptr_t)p1;
        if((uint64_t)(uintptr_t)p2!=sizeof(c)||!ua||copyin(ua,&c,sizeof(c))!=0||!nord2_gpu_trap_call_ok(&c,sizeof(c)))return kIOReturnBadArgument;
        uint8_t inline_in[256];uint8_t *in=NULL;size_t in_bytes=(size_t)c.struct_in_size;bool heap_in=false;
        if(in_bytes){
                if(in_bytes<=sizeof(inline_in))in=inline_in;
                else{in=(uint8_t *)IOMallocData(in_bytes);if(!in)return kIOReturnNoMemory;heap_in=true;}
                if(copyin((user_addr_t)c.struct_in,in,in_bytes)!=0){if(heap_in)IOFreeData(in,in_bytes);return kIOReturnBadArgument;}
        }
        uint8_t out[NORD2_GPU_TRAP_STRUCT_OUT_MAX];bzero(out,sizeof(out));uint64_t sout[NORD2_GPU_TRAP_SCALARS]={0};
        /* Prove the reply memory is writable BEFORE running anything: a refusal after the method ran would make the caller repeat it through MIG (a double SUBMIT). */
        if((c.struct_out_size&&copyout(out,(user_addr_t)c.struct_out,(size_t)c.struct_out_size)!=0)||copyout(&c,ua,sizeof(c))!=0){if(heap_in)IOFreeData(in,in_bytes);return kIOReturnBadArgument;}
        IOExternalMethodArguments a;bzero(&a,sizeof(a));
        a.version=kIOExternalMethodArgumentsCurrentVersion;a.selector=c.selector;
        a.scalarInput=c.scalar_in;a.scalarInputCount=c.scalar_in_count;
        a.structureInput=in_bytes?in:NULL;a.structureInputSize=(uint32_t)in_bytes;
        a.scalarOutput=sout;a.scalarOutputCount=c.scalar_out_count;
        a.structureOutput=c.struct_out_size?out:NULL;a.structureOutputSize=(uint32_t)c.struct_out_size;
        IOReturn r=externalMethod(c.selector,&a,NULL,NULL,NULL);
        if(heap_in)IOFreeData(in,in_bytes);
        uint64_t nout=nord2_gpu_trap_finish(&c,(int32_t)r,sout,a.scalarOutputCount,a.structureOutputSize);
        if(nout>(uint64_t)a.structureOutputSize||nout>sizeof(out))nout=0;
        /* Both copy-outs were proven possible above; a failure now can only be a concurrent unmap by the caller itself. */
        if(nout&&copyout(out,(user_addr_t)c.struct_out,(size_t)nout)!=0)return kIOReturnBadArgument;
        return copyout(&c,ua,sizeof(c))==0?kIOReturnSuccess:kIOReturnBadArgument;
}
IOReturn Nord2GPUUserClient::externalMethod(uint32_t sel,IOExternalMethodArguments *a,IOExternalMethodDispatch *,OSObject *,void *) {
        if(!a || !gpu || !lock)return kIOReturnBadArgument;
        if(!nord2_gpu_call_valid(sel,a->scalarInputCount,a->scalarOutputCount,a->structureInputSize,a->structureOutputSize,
                                 a->scalarInput,a->scalarOutput,a->structureInput,a->structureOutput) ||
           a->structureInputDescriptor || a->structureOutputDescriptor || a->asyncWakePort!=MACH_PORT_NULL)return kIOReturnBadArgument;
        IOLockLock(lock);IOLockLock(gpu->lock);IOReturn result=kIOReturnUnsupported;bool clientHeld=true;
        if(closed || gpu->stopping){result=kIOReturnNotOpen;goto out;}
        if(sel==NORD2_GPU_SEL_STATUS){gpu->status((nord2_gpu_status *)a->structureOutput);a->structureOutputSize=sizeof(nord2_gpu_status);result=kIOReturnSuccess;goto out;}
        if(sel==NORD2_GPU_SEL_HWINFO){
                gpu->hwinfo((nord2_gpu_hwinfo *)a->structureOutput);
                a->structureOutputSize=sizeof(nord2_gpu_hwinfo);result=kIOReturnSuccess;goto out;
        }
        if(sel==NORD2_GPU_SEL_LOG){
                if(a->structureInputSize>262144-logBytes){result=kIOReturnNoSpace;goto out;}   /* 0141: 256 KiB of diagnostic text per connection (was 16 KiB) */
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
                if((a->scalarInput[1] & ~1ull) && (a->scalarInput[1]>7 || (((a->scalarInput[1]>>1)&1u) && !(gAllocExt&1u)) || (((a->scalarInput[1]>>2)&1u) && !(gAllocExt&6u)))){result=kIOReturnBadArgument;break;}   /* 0135/0136: bit0 exec, bit1 GPU-only (n2ext&1), bit2 CPU write-combine (n2ext&2) */
                /* 0144: like WAIT, no connection lock during the slow part: submits and maps of this connection run meanwhile (closeContext() takes gpu->lock) */
                const unsigned actx=context;IOLockUnlock(lock);clientHeld=false;
                uint32_t h;uint64_t va,size;result=gpu->mapBuffer(actx,a->scalarInput[0],(uint32_t)a->scalarInput[1],&h,&va,&size,true,&closed);
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
                result=io_error(n2c_submit(gpu->core,context,&sub,&id));if(result==kIOReturnSuccess){a->scalarOutput[0]=id;a->scalarOutputCount=1;if(gpu->irqState==NORD2_IRQ_ACTIVE)gpu->irqArm();}break;
        }
        case NORD2_GPU_SEL_WAIT: {
                if(a->scalarInput[1]>5000){result=kIOReturnBadArgument;break;}
                /* Wait WITHOUT the per-connection lock: the frontend submits from one thread while a finish thread waits on the same
                 * connection (n2mb_wait_seq), and holding the client lock for the whole wait made every other call of the connection
                 * queue behind it (GPU idle between jobs).  ctx stays valid because closeContext() needs gpu->lock for n2c_ctx_close and
                 * every iteration re-checks 'closed' under gpu->lock before touching the context. */
                const unsigned ctx=context;IOLockUnlock(lock);clientHeld=false;
                uint32_t state=0,status=0;uint64_t id=a->scalarInput[0],enter_us=now_us(NULL),limit_us=a->scalarInput[1]*1000ull;
                for(;;){
                        if(__atomic_load_n(&closed,__ATOMIC_ACQUIRE) || gpu->stopping){result=kIOReturnNotOpen;break;}
                        gpu->irqLearnCheck();
                        n2c_poll(gpu->core);result=io_error(n2c_job_state(gpu->core,ctx,id,false,&state,&status));
                        uint64_t elapsed=now_us(NULL)-enter_us;
                        if(result!=kIOReturnSuccess || state>=N2C_JOB_DONE || gpu->core->state==N2C_CORE_BROKEN || elapsed>=limit_us)break;
                        gpu->waitStep(elapsed);
                }
                if(result==kIOReturnSuccess){
                        a->scalarOutput[0]=state;a->scalarOutputCount=1;
                        if(a->structureOutputSize){nord2_gpu_fault_info f={};f.job_id_lo=(uint32_t)id;f.job_id_hi=(uint32_t)(id>>32);
                                f.state=state;f.status=status;f.fault_status=gpu->core->ctxs[ctx].fault_status;f.fault_address=gpu->core->ctxs[ctx].fault_address;
                                memcpy(a->structureOutput,&f,sizeof(f));a->structureOutputSize=sizeof(f);}
                        if(state>=N2C_JOB_DONE){
                                n2c_job_times jt;uint64_t ret_us=now_us(NULL);
                                if(n2c_job_get_times(gpu->core,ctx,id,&jt)==N2C_OK && state==N2C_JOB_DONE){
                                        uint64_t gap=enter_us>jt.submit_us?enter_us-jt.submit_us:0,dur=ret_us-enter_us,ret=ret_us>jt.submit_us?ret_us-jt.submit_us:0;
                                        gpu->waitN++;gpu->waitGapSum+=gap;gpu->waitDurSum+=dur;gpu->waitRetSum+=ret;
                                        if(gap>gpu->waitGapMax)gpu->waitGapMax=(uint32_t)gap;if(dur>gpu->waitDurMax)gpu->waitDurMax=(uint32_t)dur;if(ret>gpu->waitRetMax)gpu->waitRetMax=(uint32_t)ret;
                                }
                                (void)n2c_job_state(gpu->core,ctx,id,true,NULL,NULL);gpu->emitJobStats();
                        }
                }break;
        }
        case NORD2_GPU_SEL_WAIT_SEQ: {
                if(a->scalarInput[1]>5000){result=kIOReturnBadArgument;break;}
                const unsigned ctx=context;IOLockUnlock(lock);clientHeld=false;     /* see SEL_WAIT: no connection lock while waiting */
                uint64_t seq=a->scalarInput[0],enter_us=now_us(NULL),limit_us=a->scalarInput[1]*1000ull;n2c_seq_result r={};
                for(;;){
                        if(__atomic_load_n(&closed,__ATOMIC_ACQUIRE) || gpu->stopping){result=kIOReturnNotOpen;break;}
                        gpu->irqLearnCheck();
                        n2c_poll(gpu->core);result=io_error(n2c_seq_state(gpu->core,ctx,seq,false,&r));
                        uint64_t elapsed=now_us(NULL)-enter_us;
                        if(result!=kIOReturnSuccess || !r.pending || gpu->core->state==N2C_CORE_BROKEN || elapsed>=limit_us)break;
                        gpu->waitStep(elapsed);
                }
                if(result==kIOReturnSuccess){
                        uint32_t state=r.pending?N2C_JOB_RUNNING:(r.failed?r.first_failed_state:N2C_JOB_DONE);
                        a->scalarOutput[0]=state;a->scalarOutput[1]=r.done+r.failed;a->scalarOutput[2]=r.first_failed_id;a->scalarOutputCount=3;
                        if(a->structureOutputSize){nord2_gpu_fault_info f={};f.job_id_lo=(uint32_t)r.first_failed_id;f.job_id_hi=(uint32_t)(r.first_failed_id>>32);
                                f.state=r.first_failed_state;f.status=r.first_failed_status;f.fault_status=gpu->core->ctxs[ctx].fault_status;f.fault_address=gpu->core->ctxs[ctx].fault_address;
                                memcpy(a->structureOutput,&f,sizeof(f));a->structureOutputSize=sizeof(f);}
                        if(!r.pending){
                                gpu->seqWaits++;gpu->seqJobs+=r.done+r.failed;uint64_t dur=now_us(NULL)-enter_us;gpu->seqDurSum+=dur;if(dur>gpu->seqDurMax)gpu->seqDurMax=(uint32_t)dur;
                                (void)n2c_seq_state(gpu->core,ctx,seq,true,&r);gpu->emitJobStats();
                        }
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
        case NORD2_GPU_SEL_SURFACE_RESERVE: {
                if(gpu->core->resv_off){result=kIOReturnUnsupported;break;}
                uint32_t id=0,n=(uint32_t)a->scalarInput[0];   /* the validator bounded n by the structure size it demanded */
                result=io_error(n2c_resv_create(gpu->core,context,(const uint32_t *)a->structureInput,n,&id));
                if(result==kIOReturnSuccess){a->scalarOutput[0]=id;a->scalarOutputCount=1;}
                break;
        }
        case NORD2_GPU_SEL_SURFACE_RETIRE:
                if(gpu->core->resv_off){result=kIOReturnUnsupported;break;}
                if(a->scalarInput[0]>UINT32_MAX){result=kIOReturnBadArgument;break;}
                result=io_error(n2c_resv_retire(gpu->core,context,(uint32_t)a->scalarInput[0],a->scalarInput[1]));break;
        case NORD2_GPU_SEL_PROBE:
                result=gpu->probe(context);break;
        case NORD2_GPU_SEL_SYNC:
                if(a->scalarInput[0]>UINT32_MAX || a->scalarInput[1]>1){result=kIOReturnBadArgument;break;}
                result=io_error(n2c_bo_sync(gpu->core,context,(uint32_t)a->scalarInput[0],a->scalarInput[1]!=0));break;
        /* Cross-client reset is deliberately not a public user-client operation. */
        default: break;
        }
 out:
        IOLockUnlock(gpu->lock);if(clientHeld)IOLockUnlock(lock);return result;
}
