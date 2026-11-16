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
#define NORD2_GPU_FENCE_IMPL
#include "nord2_gpu_fence.h"   /* 0395: per-surface write fences + display-pool import range */
#include "sweet_surface_use.h" /* 0398: GPU use-count holds by job */
#include <sys/sysctl.h>        /* 0400: kern.n2gpu_lat */
#include <kern/thread_call.h>   /* 0400: buffer owners released off the GPU lock */
extern "C" kern_return_t kernel_thread_start_priority(thread_continue_t, void *, integer_t, thread_t *);
/* 0663: move the GPU interrupt and the bottom half to the big cluster (osfmk sched_prim / machine routines, used the same way by nord2_cpufreq.c) */
extern "C" processor_t cpu_to_processor(int cpu);
extern "C" processor_t thread_bind(processor_t processor);
extern "C" int cpu_number(void);
#define N2_IRQ_CPU 4      /* first core of the A78 cluster cpu4-6 (nord2_cpufreq.c: cluster "l"; cpu7 is the prime core 0642 gives to app threads) */
#define N2_BH_CPU 5
static bool n2_bind_cpu(int cpu) {      /* bind the calling thread to cpu and move there; false = not possible (left unbound) */
        processor_t p = cpu_to_processor(cpu);
        if (p == PROCESSOR_NULL) return false;
        (void)thread_bind(p);
        (void)thread_block(THREAD_CONTINUE_NULL);
        if (cpu_number() == cpu) return true;
        (void)thread_bind(PROCESSOR_NULL);
        (void)thread_block(THREAD_CONTINUE_NULL);
        return false;
}   /* 0400: osfmk/kern/thread.c (MACH_KERNEL_PRIVATE header) */
/* 0441 (gfxheap-20261113): the 0440 graphics heap (SweetSurface.cpp, sweet_gfx_heap_kern.inc): GPU-only buffers >= 64 KiB (ALLOC flags == 2) out of
 * the low-bank heap instead of XNU pages, and imports of heap surfaces.  Heap buffers are left out of the global private total (privateTotal, so out of
 * the 0417/0418 GPU budget too); the per-context limits still count them.  kern.n2heap_gpu = 0 (or kern.n2heap = 0) turns the placement off. */
extern "C" uint64_t sweet_heap_gpu_take(uint64_t bytes, uintptr_t *kva);
extern "C" void sweet_heap_gpu_give(uint64_t pa, uint64_t bytes);
extern "C" bool sweet_heap_gpu_page_ok(uint64_t pa);
static uint64_t gHeapBytes;             /* 0441: bytes of live (adopted) heap buffers */
static uint32_t gHeapBos, gHeapFallback, gHeapImports;
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
/* 0643: LVTS banks through ml_io_map_unmappable (no io_submap page; the io_submap has only 3 free pages). */
extern "C" vm_offset_t ml_io_map_unmappable(vm_offset_t phys_addr, vm_size_t size, uint32_t flags);   /* osfmk/arm/machine_routines.h (kernel-private) */
#ifndef VM_WIMG_IO
#define VM_WIMG_IO 0x7u        /* osfmk/arm/memory_types.h: VM_MEM_COHERENT | VM_MEM_NOT_CACHEABLE | VM_MEM_GUARDED (what ml_io_map uses) */
#endif
static bool map_bank_um(Bank *b, uint64_t pa, unsigned bytes) {
        b->bytes = bytes;
        b->p = (volatile uint32_t *)ml_io_map_unmappable(pa, bytes, VM_WIMG_IO);
        return b->p != NULL;
}
/* 0643: SoC temperature cache, written by the GPU worker thread every 2 s (Nord2GPU::lvtsTick), read by nord2_cpufreq.c and the GPU ladder */
static volatile int gSocCc, gGpuCc;
static volatile bool gSocOk, gGpuOk, gSocApprox;
static volatile uint64_t gSocMs;
static bool valid_ram(uint64_t pa) {
        return !(pa & 4095) && gPhysSize >= 4096 && pa >= gPhysBase &&
               pa - gPhysBase <= gPhysSize - 4096 && pa < (1ull << 40);
}
struct BufferOwner {
        uint8_t madv;       /* 0481: N2MADV_F_* (nord2_gpu_madv.h), under Nord2GPU::lock until the owner is detached */
        IOBufferMemoryDescriptor *md;
        SweetSurface *surface;
        bool prepared;      /* 0137: pageable write-combine memory, wired by prepare(), unwired by complete() */
        BufferOwner *next;  /* 0400: deferred-release list */
        uint64_t heapPa, heapLen;       /* 0441: graphics-heap range (md NULL), given back when the owner goes */
        bool heapCounted;               /* 0441: in gHeapBytes (set once the core adopted the buffer) */
};
/* 0441: leave gHeapBytes when the core tears the buffer down (with x->wired), not when the deferred release runs */
static void heap_uncount(BufferOwner *o) {
        if (o && o->heapCounted) { o->heapCounted = false; __atomic_fetch_sub(&gHeapBytes, o->heapLen, __ATOMIC_RELAXED); __atomic_fetch_sub(&gHeapBos, 1u, __ATOMIC_RELAXED); }
}
/* 0112: the display surface behind an imported buffer (NULL for any other buffer); only compared, never dereferenced, by the core. */
static const void *resv_token(void *, void *cookie) { return cookie ? (const void *)((BufferOwner *)cookie)->surface : NULL; }
struct BufferOwner; static void n2madv_owner_gone(BufferOwner *o);     /* 0481: nord2_gpu_madv_kern.inc */
static void free_owner(void *, void *cookie) {
        BufferOwner *o = (BufferOwner *)cookie;
        if (!o) return;
        if (o->md && o->prepared) o->md->complete(kIODirectionInOut);
        n2madv_owner_gone(o);                    /* 0481: a purged buffer's pages go now, under the client's mapping */
        sweet_surface_wod_unhold(o->surface);    /* 0480: the import's hold (the GPU pages were unmapped and flushed before the owner goes) */
        if (o->surface) OSDecrementAtomic((volatile SInt32 *)&o->surface->gpuImports);    /* 0398: pool leak attribution */
        OSSafeReleaseNULL(o->md);
        OSSafeReleaseNULL(o->surface);
        heap_uncount(o);
        if (o->heapPa) sweet_heap_gpu_give(o->heapPa, o->heapLen);     /* 0441: the GPU pages were unmapped and flushed before the owner goes */
        IOFree(o, sizeof(*o));
}
/* 0400 (gpulat-20261112): GPU job latency.  kern.n2gpu_lat (boot-arg n2lat) = bit mask of the OLD behaviours, 0 = every 0400 change on:
 *   1 no bottom-half thread (the interrupt kicks the workloop timer as in 0140)    2 buffers released before the next job starts (pre-0400 order)
 *   4 every pageable allocation zeroed again after prepare() (pre-0400)            8 buffer owners released inline under the GPU lock (pre-0400) */
static int gLatMode = 0;
static int gJsNext = 1, gJsNextApplied = 1;      /* 0630 */
/* 0632: precise jobs (ABI v3 from the gpupipe1 userland) may overtake older non-conflicting fragment jobs of their context.  Kill switch:
 * boot-arg n2gp=0 or n2gdv +32768 (v3 still accepted, the flags ignored: every job serial again); sysctl kern.n2gpipe=0 at run time. */
static int gPipe = 1, gPipeApplied = 1, gPipeAbi = 1;
/* 0660..0663 (step A of GPU_PERF_ANALYSIS.md).  The n2gdv bits are spent: each piece has a short boot-arg (default on) and a sysctl.
 *   kern.n2gpreclean / n2gpc=0   0661: CPU cleans of listed buffers in the submitting thread outside the GPU lock (0 = in the bottom half, as before)
 *   kern.n2gv4 / n2gv4=0         0661: ABI v4 (128 handles) refused -> clients fall back to v3/v2 (lists of 32 = truncated)
 *   kern.n2gprio / n2gpr=0       0662: compositor-first pick, NEXT take-back, 16 job records reserved for backboardd (0 = FIFO by job id)
 *   n2girq=0                     0663: GPU interrupt and bottom half stay where they were (no move to the big cluster; boot only) */
static int gPreclean = 1, gPrecleanApplied = 1, gV4 = 1, gPrio = 1, gPrioApplied = 1, gIrqBig = 1;
SYSCTL_INT(_kern, OID_AUTO, n2gpreclean, CTLFLAG_RW | CTLFLAG_LOCKED, &gPreclean, 0, "0661: CPU cleans at submit outside the GPU lock");
SYSCTL_INT(_kern, OID_AUTO, n2gv4, CTLFLAG_RW | CTLFLAG_LOCKED, &gV4, 0, "0661: accept GPU submit ABI v4 (128 buffers)");
SYSCTL_INT(_kern, OID_AUTO, n2gprio, CTLFLAG_RW | CTLFLAG_LOCKED, &gPrio, 0, "0662: compositor-first GPU scheduling");
static char gCtxName[N2C_MAX_CTX][17];      /* 0660: process name / pid of each context (set at open) */
static int gCtxPid[N2C_MAX_CTX];
static uint64_t gLockWaitSumUs, gLockHoldSumUs, gBhWaitSumUs, gBhWaitN, gBhPollSumUs;      /* 0660 */
static int gIrqCpu = -1, gBhCpu = -1;       /* 0663 */
/* 0665 (step C): kern.n2guv / boot-arg n2guv=0 = VGPU stays at the boot chain's 750 mV for every OPP <= 836 MHz; otherwise each OPP runs at the vendor
 * table voltage + kern.n2guvm (default 12.5 mV), raised before the clock goes up and lowered after it came down (0633's order and rail code); a GPU
 * fault / reset / timeout within 10 s of a lowering puts the boot rails back for the rest of the boot.
 * 0666 (step C): kern.n2gpg / boot-arg n2gpg=0 = the shader cores stay powered; otherwise they are powered down after kern.n2gpgus (10000 us)
 * without GPU work and up again before the next job (kbase on MT6893: 25 x 400 us). */
static int gUv = 1, gUvMargin = 12500, gPg = 1, gPgApplied = 1, gPgIdleUs = 10000;
SYSCTL_INT(_kern, OID_AUTO, n2guv, CTLFLAG_RW | CTLFLAG_LOCKED, &gUv, 0, "0665: GPU per-OPP vendor voltage");
SYSCTL_INT(_kern, OID_AUTO, n2guvm, CTLFLAG_RW | CTLFLAG_LOCKED, &gUvMargin, 0, "0665: margin above the vendor VGPU, uV (0..50000)");
SYSCTL_INT(_kern, OID_AUTO, n2gpg, CTLFLAG_RW | CTLFLAG_LOCKED, &gPg, 0, "0666: GPU shader-core power gating when idle");
SYSCTL_INT(_kern, OID_AUTO, n2gpgus, CTLFLAG_RW | CTLFLAG_LOCKED, &gPgIdleUs, 0, "0666: idle time before the shader cores are powered down, us");
/* 0750: kern.n2gfrev / boot-arg n2gfrev=0 = a client killed by an MMU fault stays FAULTED until it closes (the old behaviour: backboardd's
 * compositor dead until reboot, the flashing display); 1 (default) = revived once its faulting work is over (nord2_gpu_core.c revive_faulted).
 * Every MMU fault prints 'NORD2 GPU FAULT:' (class, AS, owner process, decoded AS_FAULTSTATUS, address against the client's buffers). */
static int gFaultRevive = 1;
SYSCTL_INT(_kern, OID_AUTO, n2gfrev, CTLFLAG_RW | CTLFLAG_LOCKED, &gFaultRevive, 0, "0750: revive a GPU client after its MMU fault (0 = stays faulted until close)");
SYSCTL_INT(_kern, OID_AUTO, n2gpipe, CTLFLAG_RW | CTLFLAG_LOCKED, &gPipe, 0, "0632: precise GPU jobs may overlap older fragment jobs of their context (1 on, 0 off)");
SYSCTL_INT(_kern, OID_AUTO, n2gjsnext, CTLFLAG_RW | CTLFLAG_LOCKED, &gJsNext, 0, "0630: GPU JS NEXT pipelining (1 on, 0 no new NEXT writes)");
SYSCTL_INT(_kern, OID_AUTO, n2gpu_lat, CTLFLAG_RW | CTLFLAG_LOCKED, &gLatMode, 0, "0400: GPU latency changes off-mask: 1 BH thread, 2 late release, 4 zero skip, 8 deferred owner free");
/* 0461: runtime GPU DVFS.  Policy: bsd/dev/nord2_gov.h (n2g_gpu_*); clock: n2clk_retune (the vendor's parking switch); voltage: never touched, only OPPs the
 * rails found at power-on already cover are used.  kern.n2gdvfs / boot-arg n2gdv: 0 off, 1 decisions logged only (default), 2 act; +4 = change the clock under a
 * running job like the vendor does (default: job starts are held, at most 50 ms, and the clock changes in the gap).  Everything runs in tick() under the driver lock.
 * 0500: n2gdv +32 = n2gctx=0 (static GPU address spaces, the kill switch of dynamic AS management; read in cache_scope).
 * 0492: +8 = UI profile (n2gdv=10 acts with it, 9 logs its decisions): never below the cold-start OPP, so the clock moves between the cold-start rate and the cap and
 * nowhere else, and while the UI animates or is touched the cap is taken at 80 % busy over 40 ms instead of 88 %.
 * The first three switches are announced 400 ms ahead (the console is pulled by the host), the first eight are checked against the GPU's own cycle counter.  Any
 * clock error, a mismatching measurement or a job fault / reset / timeout within 500 ms of a switch returns to the cold-start OPP and stops acting until reboot. */
#include "../../../bsd/dev/nord2_gov.h"
static int gGdvfs = 1;
SYSCTL_INT(_kern, OID_AUTO, n2gdvfs, CTLFLAG_RW | CTLFLAG_LOCKED, &gGdvfs, 0, "0461: GPU DVFS 0 off, 1 decisions logged, 2 act, +4 no wait for a gap between jobs, +8 UI profile: never below the cold-start OPP, up at 80 % while the UI animates (0492)");
extern "C" { extern struct n2g_gpu_report nord2_gdvfs_report; void nord2_dvfs_hints(struct n2g_hints *); }      /* bsd/dev/nord2_cpufreq.c (0460) */
static struct {
        struct n2g_gpu g; struct n2g_therm th;
        bool init, failed, dead;
        int pend, bat;
        uint64_t armUs, retryUs, busyBase, lastBusy, lastUs, slowUs, wdUs, wdCount, boostMs, busySum;
        uint32_t swaps[N2G_HIST];
        unsigned nsw, boot, switches, announced, gapMiss, errors, busyMax, polls;
        /* 0631: fast-ramp profile; gs/fs = its shadow (decisions applied to a model clock, never to the GPU), fa = its state when it acts */
        struct n2g_gpu gs; struct n2g_gfast fs, fa;
        unsigned sw2, depthMax, depthLast;
        /* 0633: highest OPP the rails cover NOW (max_idx may be higher when the voltage can be raised for it); raised = at 800/800 mV */
        unsigned covered, voltOps, voltErr; bool raised; uint32_t bootVg, bootVs;
        /* 0665: the rails now, the one-time per-OPP step at boot, its fault watchdog */
        uint32_t curVg, curVs; bool uvDone, uvFailed; uint64_t uvWdUs, uvWdCount;
        uint64_t atMs[N2G_GPU_NOPP], lastMs2;
} gGdv;
/* 0631: kern.n2gprof: 0 = the 0461/0492 policy, 1 = fast ramp (654 MHz floor, straight to the thermal cap when the GPU is hot, back after 500 ms),
 * 2 = performance (always the thermal cap: 836 MHz below 41 C battery, 805 / 654 / 512 / 350 above).  Boot: n2gdv +8192 = 1, +16384 = 2.
 * It only moves the clock when kern.n2gdvfs acts (mode 2, e.g. n2gdv=8202 or 16394); in decisions-only mode the fast profile still runs
 * as a shadow and prints NORD2 GDVFS2 (what it would have done). */
static int gGprof = 0;
/* 0633: kern.n2gvolt (boot: n2gdv +65536): 1 = the 886 MHz vendor OPP is allowed (thermal level 0 only), its 800/800 mV raised on the PWRAP owner
 * thread before the clock goes up and lowered after it comes down (the vendor order); 0 = the rails stay as the boot chain left them (<= 836). */
static int gVolt = 0;
SYSCTL_INT(_kern, OID_AUTO, n2gvolt, CTLFLAG_RW | CTLFLAG_LOCKED, &gVolt, 0, "0633: 1 = GPU 886 MHz with a live VGPU/VSRAM raise to 800 mV (acts only with kern.n2gdvfs mode 2)");
SYSCTL_INT(_kern, OID_AUTO, n2gprof, CTLFLAG_RW | CTLFLAG_LOCKED, &gGprof, 0, "0631: GPU DVFS profile 0 = 0461/0492, 1 = fast ramp, 2 = performance (acts only with kern.n2gdvfs mode 2)");
/* Owners of torn-down buffers (unwire + release of up to 32 MiB, ms) are pushed here by the core's free_owner and released by a thread call,
 * after the GPU pages were unmapped and flushed: nothing the GPU can reach is freed later than before, only the CPU-side release moves. */
static BufferOwner *volatile gOwnerList;
static thread_call_t gOwnerCall;
static volatile uint32_t gOwnerQueued;
static uint32_t gOwnerFreed, gOwnerDrainMaxUs, gOwnerKiB;
static void owner_drain(thread_call_param_t, thread_call_param_t) {
        BufferOwner *o = __atomic_exchange_n(&gOwnerList, (BufferOwner *)NULL, __ATOMIC_ACQ_REL);
        if (!o) return;
        uint64_t ns0 = 0, ns1 = 0; absolutetime_to_nanoseconds(mach_absolute_time(), &ns0);
        uint32_t n = 0, kib = 0;
        while (o) { BufferOwner *nx = o->next; if (o->md) kib += (uint32_t)(o->md->getLength() >> 10); free_owner(NULL, o); o = nx; n++; }
        absolutetime_to_nanoseconds(mach_absolute_time(), &ns1);
        uint32_t us = (uint32_t)((ns1 - ns0) / 1000ull);
        __atomic_fetch_add(&gOwnerFreed, n, __ATOMIC_RELAXED); __atomic_fetch_add(&gOwnerKiB, kib, __ATOMIC_RELAXED);
        if (us > gOwnerDrainMaxUs) gOwnerDrainMaxUs = us;
}
static void free_owner_deferred(void *ctx, void *cookie) {
        BufferOwner *o = (BufferOwner *)cookie;
        if (!o) return;
        heap_uncount(o);        /* 0441 */
        if (!gOwnerCall || (gLatMode & 8)) { free_owner(ctx, o); return; }
        BufferOwner *h = __atomic_load_n(&gOwnerList, __ATOMIC_ACQUIRE);
        do { o->next = h; } while (!__atomic_compare_exchange_n(&gOwnerList, &h, o, true, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE));
        __atomic_fetch_add(&gOwnerQueued, 1u, __ATOMIC_RELAXED);
        (void)thread_call_enter(gOwnerCall);
}
/* Bottom half: a kernel thread at BASEPRI_PREEMPT (92) woken by the job interrupt runs n2c_poll (complete, start the next job) at once, instead of
 * timer -> thread call -> workloop gate.  The 150 us tick and the waiters' own polls stay as they were (fallbacks). */
static volatile uint32_t gBhPending, gBhExit;
static bool gBhOn;
static uint32_t gBhRuns, gBhWaitMaxUs, gBhPollMaxUs;
static volatile uint64_t gBhKickUs;
/* externalMethod: time to get both locks, and the longest hold (WAIT, WAIT_SEQ and ALLOC drop the GPU lock and are not counted) */
static uint32_t gLockWaitMaxUs, gLockHoldMaxUs, gLockHoldSel, gLockCalls;
static uint32_t gZeroSkip, gZeroSkipMiB, gZeroVerifyFail, gZeroFull;
/* 0395: write fences.  gFence is guarded by Nord2GPU::lock; the job view reads core->jobs[] (same predicate as surfaceBusy). */
static struct n2gf_table gFence;
static bool gFencePool = true;          /* boot-arg n2gpool=0: imports of display-pool pages refused again */
static uint32_t fence_sid(const struct n2c_bo *b) {
        return b && (b->flags & N2C_BO_IMPORTED) && b->cookie && ((BufferOwner *)b->cookie)->surface ? ((BufferOwner *)b->cookie)->surface->id : 0;
}
static bool fence_unfinished(void *ctx, unsigned i, uint64_t *id) {
        const struct n2c_job *j = &((struct n2c_core *)ctx)->jobs[i];
        if (j->state != N2C_JOB_QUEUED && j->state != N2C_JOB_RUNNING && j->state != N2C_JOB_STOPPING) return false;
        *id = j->id; return true;
}
static bool fence_writes(void *ctx, unsigned i, uint32_t sid) {
        const struct n2c_job *j = &((struct n2c_core *)ctx)->jobs[i];
        for (unsigned k = 0; k < j->nr_bos && k < N2C_JOB_BOS; k++) if (n2c_mask_bit(j->write_mask, k) && fence_sid(j->bos[k]) == sid) return true;
        return false;
}
static struct n2gf_jobs fence_jobs(struct n2c_core *c) { struct n2gf_jobs v = { c, N2C_MAX_JOBS, fence_unfinished, fence_writes }; return v; }
/*
 * 0398 (usecount-20261112): IOSurface use-count holds of the GPU.  Every IOSurface import a job lists (read or written) is held
 * (category 0, IOSurfaceIsInUse) from the submit until the job is no longer QUEUED/RUNNING/STOPPING; the tick reaps.  gUseGpu is
 * guarded by Nord2GPU::lock; each slot keeps a retain on its surface, dropped after the lock is let go.  A full table takes no hold
 * (counted; the surface is then protected as before 0398 only).
 */
static struct sweet_use_gpu_table gUseGpu;
static void *gUseGpuOut[SWEET_USE_GPU_SLOTS];          /* the tick's reaped surfaces (tick only: one workloop thread) */
static void usehold_record(struct n2c_core *c, unsigned ctx, const struct n2c_submit *sub, uint64_t id) {
        for (unsigned k = 0; k < sub->nr_handles && k < N2C_JOB_BOS; k++) {
                const struct n2c_bo *b = n2c_bo_lookup(c, ctx, sub->handles[k]);
                SweetSurface *s = b && (b->flags & N2C_BO_IMPORTED) && b->cookie ? ((BufferOwner *)b->cookie)->surface : NULL;
                if (!s) continue;
                if (sweet_use_gpu_has(&gUseGpu, id, s)) { gUseGpu.dup++; continue; }
                int slot = sweet_use_gpu_put(&gUseGpu, id, s);
                if (slot < 0) continue;
                if (!sweet_surface_use_kernel(s, SWEET_SURFACE_USE_GPU, 1)) {   /* kern.n2surfuse < 2: no hold, no slot */
                        gUseGpu.s[slot].surface = NULL; gUseGpu.s[slot].job = 0; gUseGpu.used--; gUseGpu.recorded--;
                        continue;
                }
                s->retain();
        }
}
/* Tick, Nord2GPU::lock held: holds of jobs that left the unfinished states are given back; returns the surfaces to release. */
static unsigned usehold_reap(struct n2c_core *c) {
        if (gUseGpu.used == 0) return 0;
        uint64_t live[N2C_MAX_JOBS]; unsigned n = 0;
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                const struct n2c_job *j = &c->jobs[i];
                if (j->state == N2C_JOB_QUEUED || j->state == N2C_JOB_RUNNING || j->state == N2C_JOB_STOPPING) live[n++] = j->id;
        }
        unsigned k = sweet_use_gpu_reap(&gUseGpu, live, n, gUseGpuOut, SWEET_USE_GPU_SLOTS);
        for (unsigned i = 0; i < k; i++) (void)sweet_surface_use_kernel(gUseGpuOut[i], SWEET_SURFACE_USE_GPU, -1);
        return k;
}
/* After a successful submit (caller holds Nord2GPU::lock): every write-masked handle that is an IOSurface import. */
static void fence_record(struct n2c_core *c, unsigned ctx, const struct n2c_submit *sub, uint64_t id) {
        struct n2gf_jobs v = fence_jobs(c);
        for (unsigned k = 0; k < sub->nr_handles && k < N2C_JOB_BOS; k++)
                if (k < 32 ? ((sub->write_mask >> k) & 1u) : ((sub->write_mask_hi[(k >> 5) - 1] >> (k & 31u)) & 1u)) n2gf_record(&gFence, &v, c->next_job_id, fence_sid(n2c_bo_lookup(c, ctx, sub->handles[k])), id);
        usehold_record(c, ctx, sub, id);        /* 0398 */
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
        /* 0500: dynamic GPU address spaces (any number of Metal clients; the 7 client AS are lent at job start).  Kill switch: boot-arg n2gctx=0,
         * or n2gdv +32 (the boot line has 4 characters left with n2gdv=9: n2gdv=41 = the same DVFS mode with the old static AS model). */
        { int gx = 1, gm = 0; if (!PE_parse_boot_argn("n2gctx", &gx, sizeof(gx)) && PE_parse_boot_argn("n2gdv", &gm, sizeof(gm)) && (gm & 32)) gx = 0;
          (void)n2c_set_as_dynamic(c, gx != 0);
          IOLog("NORD2 GPU AS: mode=%s contexts=%u hw_as=%u (kill switch n2gctx=0 or n2gdv+32 = static, 7 clients)\n", c->as_dyn ? "dynamic" : "static",
                c->as_dyn ? N2C_MAX_CTX - 1 : N2C_HW_AS - 1, N2C_HW_AS); }
        /* 0630: JS NEXT pipelining (a second chain waits in the slot's NEXT registers: same context on the same slot, or another context).
         * Kill switch: boot-arg n2jn=0, or n2gdv +4096 (n2gdv=4105 = the n2gdv=9 image without it): one chain per slot, the pre-0630 paths.
         * Run time: sysctl kern.n2gjsnext=0 stops new NEXT writes (only if it was on at boot can it be turned on again). */
        { int jn = 1, gm = 0; if (!PE_parse_boot_argn("n2jn", &jn, sizeof(jn)) && PE_parse_boot_argn("n2gdv", &gm, sizeof(gm)) && (gm & 4096)) jn = 0;
          (void)n2c_set_jsnext(c, jn != 0); gJsNext = c->jsnext ? 1 : 0; gJsNextApplied = gJsNext;
          IOLog("NORD2 GPU JSNEXT: %s (kill switch n2jn=0 or n2gdv+4096; sysctl kern.n2gjsnext)\n", c->jsnext ? "on" : "off"); }
        { int gp = 1, gm = 0; if (!PE_parse_boot_argn("n2gp", &gp, sizeof(gp)) && PE_parse_boot_argn("n2gdv", &gm, sizeof(gm)) && (gm & 32768)) gp = 0;
          c->pipe = gp != 0; gPipeAbi = gp != 0; gPipe = gPipeApplied = gp != 0;
          IOLog("NORD2 GPU PIPE: %s (pass overlap for precise ABI-v3 jobs; kill switch n2gp=0 or n2gdv+32768; sysctl kern.n2gpipe)\n", c->pipe ? "on" : "off"); }
        { int v = 1; if (PE_parse_boot_argn("n2gpc", &v, sizeof(v))) gPreclean = v != 0; c->preclean = gPreclean != 0; gPrecleanApplied = gPreclean;
          v = 1; if (PE_parse_boot_argn("n2gv4", &v, sizeof(v))) gV4 = v != 0;
          v = 1; if (PE_parse_boot_argn("n2gpr", &v, sizeof(v))) gPrio = v != 0; c->prio_on = gPrio != 0; gPrioApplied = gPrio;
          v = 1; if (PE_parse_boot_argn("n2girq", &v, sizeof(v))) gIrqBig = v != 0;
          v = 1; if (PE_parse_boot_argn("n2guv", &v, sizeof(v))) gUv = v != 0;
          { int fr = 1; if (PE_parse_boot_argn("n2gfrev", &fr, sizeof(fr))) gFaultRevive = fr != 0; c->revive_on = gFaultRevive != 0; }      /* 0750 */
          v = 1; if (PE_parse_boot_argn("n2gpg", &v, sizeof(v))) gPg = v != 0; c->pg_on = gPg != 0; c->pg_idle_us = (uint32_t)gPgIdleUs; gPgApplied = gPg;
          IOLog("NORD2 GPU POWER: per-OPP voltage %s (margin %d uV), shader-core gating %s after %d us idle (n2guv=0 / n2gpg=0 off)\n", gUv ? "on" : "off", gUvMargin, gPg ? "on" : "off", gPgIdleUs);
          IOLog("NORD2 GPUPROF: step A on: preclean=%d abi_v4=%d prio=%d irq_big=%d (boot-args n2gpc/n2gv4/n2gpr/n2girq =0 to switch off)\n", gPreclean, gV4, gPrio, gIrqBig); }
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
        void gdvfsPoll(uint64_t nowUs);      /* 0461 */
        void gdvfsSwitch(uint64_t nowUs);
        uint32_t gdvfsMeasure(uint32_t khz);
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
        static int railSet(void *);    /* 0633: n2rail_set on the PWRAP owner thread (input thread) */
        bool gdvfsVolt(uint32_t vg, uint32_t vs, const char *why);      /* 0633 */
        uint32_t gdvfsOppVg(unsigned idx);                              /* 0665 */
        /* Optional job interrupt (boot-arg nord2_gpu_irq=1; polling stays the fallback). */
        bool irqWanted = false, irqTried = false, irqArmed = false;
        uint32_t irqSpi = NORD2_GPU_JOB_SPI_DEFAULT, irqIntid = 0, irqLearnMisses = 0;
        enum nord2_irq_state irqState = NORD2_IRQ_OFF;
        struct nord2_irq_guard irqGuard = {};
        volatile uint32_t irqFlag = 0, irqTops = 0;
        uint32_t irqSleeps = 0, irqTimeouts = 0;
        /* LVTS thermal sensing (boot-arg nord2_lvts=1 read-only | 2 start the GPU controller | 3 also open the clock gate | 4 auto (0643 default):
         * read-only when the controllers already sense, else start them (never the clock gate); n2gdv +16 = off). */
        struct n2lvts lvts = {};
        Bank lvtsAp = {}, lvtsMcu = {};
        unsigned lvtsMode = 0, lvtsState = 0;     /* state: 0 off, 1 reading, 2 started by us, 3 failed */
        uint32_t lvtsMask = N2LVTS_MASK_GPU; int lvtsMaxC = 65; bool lvtsTried = false;
        uint64_t lvtsLastMs = 0;                  /* 0643 */
        void lvtsTick(uint64_t nowMs);
        void lvtsSetup();
        bool lvtsGpu(int32_t *mC);
        int lvtsHottest(uint32_t mask);
        static uint32_t lvtsRd(void *, enum n2lvts_block, uint32_t);
        static void lvtsWr(void *, enum n2lvts_block, uint32_t, uint32_t);
        void irqSetup();
        void irqLearnCheck();
        void irqArm();
        void irqRearm();     /* 0400: unmask only */
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
#include "nord2_gpu_madv.h"      /* 0481: MADVISE for GPU buffers (kern.n2gpu_madv, default off) */
#include "nord2_gpu_madv_kern.inc"
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
        n2c_ops mo = { allocTable, freeTable, flushTable, alloc_host, free_host, sync_page, free_owner_deferred, now_ms, log_msg, now_us, resv_token };   /* 0400: deferred owner release */
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
/* 0400 bottom half and LAT lines */
static bool core_unfinished(struct n2c_core *c) {
        for (unsigned i = 0; i < N2C_MAX_CTX; i++) if (c->ctxs[i].state != N2C_CTX_FREE && c->ctxs[i].jobs) return true;
        return false;
}
static void bh_main(void *p, wait_result_t) {
        Nord2GPU *d = (Nord2GPU *)p;
        if (gIrqBig && n2_bind_cpu(N2_BH_CPU)) gBhCpu = N2_BH_CPU;      /* 0663: the bottom half next to the interrupt, on the big cluster */
        for (;;) {
                assert_wait((event_t)&gBhPending, THREAD_UNINT);
                if (__atomic_load_n(&gBhPending, __ATOMIC_ACQUIRE) || gBhExit) clear_wait(current_thread(), THREAD_AWAKENED);
                else (void)thread_block(THREAD_CONTINUE_NULL);
                if (gBhExit) break;
                __atomic_store_n(&gBhPending, 0u, __ATOMIC_RELEASE);
                uint64_t t0 = now_us(NULL), kick = gBhKickUs; gBhKickUs = 0;
                IOLockLock(d->lock);
                uint64_t t1 = now_us(NULL);
                if (d->ready && !d->stopping && d->core && d->core->state == N2C_CORE_READY) {
                        d->core->lat_release_late = !(gLatMode & 2);
                        n2c_poll(d->core);
                        /* re-arm the job interrupt (mask only: irqFlag belongs to the waiters' protocol, see waitStep) */
                        if (d->irqState == NORD2_IRQ_ACTIVE && core_unfinished(d->core)) d->irqRearm();
                        gBhRuns++;
                }
                uint64_t t2 = now_us(NULL);
                IOLockUnlock(d->lock);
                if (kick && t0 >= kick && t0 - kick > gBhWaitMaxUs) gBhWaitMaxUs = (uint32_t)(t0 - kick);
                if (t2 - t1 > gBhPollMaxUs) gBhPollMaxUs = (uint32_t)(t2 - t1);
                if (kick && t0 >= kick) { gBhWaitSumUs += t0 - kick; gBhWaitN++; }      /* 0660 */
                gBhPollSumUs += t2 - t1;
        }
        d->release();
        thread_terminate(current_thread());
}
/* 0500: address-space line, at most every 2 s and only when a counter moved: what to read on the phone (open/peak > 7 with no "context refused"). */
static void as_emit(Nord2GPU *d) {
        static uint64_t last_us, last_sum;
        if (!d->core) return;
        const n2c_as_stats &a = d->core->as_stats; const n2c_stats &s = d->core->stats;
        uint64_t sum = a.binds + a.evictions + a.bind_waits + a.bind_failures + a.orphan_faults + a.ctx_refused + a.ctx_open + a.ctx_peak + s.mmu_faults + s.resets + s.flush_failures +
                       a.trail_faults + a.revives + a.revive_refused;      /* 0750 */
        uint64_t t = now_us(NULL);
        if (sum == last_sum || (last_us && t - last_us < 2000000ull)) return;
        last_sum = sum; last_us = t;
        IOLog("NORD2 GPU AS: mode=%s open=%u peak=%u bound=%u binds=%llu evict=%llu waits=%llu bindfail=%llu refused=%llu mmu_faults=%llu orphan=%llu resets=%llu flushfail=%llu trail=%llu revived=%llu revive_held=%llu faulted_refused=%llu\n",
              d->core->as_dyn ? "dynamic" : "static", a.ctx_open, a.ctx_peak, a.bound, (unsigned long long)a.binds, (unsigned long long)a.evictions,
              (unsigned long long)a.bind_waits, (unsigned long long)a.bind_failures, (unsigned long long)a.ctx_refused, (unsigned long long)s.mmu_faults,
              (unsigned long long)a.orphan_faults, (unsigned long long)s.resets, (unsigned long long)s.flush_failures,
              (unsigned long long)a.trail_faults, (unsigned long long)a.revives, (unsigned long long)a.revive_refused, (unsigned long long)a.faulted_refused);
}
/* 0750: one line per MMU fault (the core keeps the last N2C_FAULT_RING; called under d->lock right after n2c_poll). */
static const char *fault_exc_name(uint32_t st) {
        unsigned e = st & 0xffu;
        if (e >= 0xc0 && e <= 0xc7) return "TRANSLATION_FAULT";
        if (e >= 0xc8 && e <= 0xcf) return "PERMISSION_FAULT";
        if (e >= 0xd0 && e <= 0xd7) return "TRANSTAB_BUS_FAULT";
        if (e >= 0xd8 && e <= 0xdf) return "ACCESS_FLAG_FAULT";
        if (e >= 0xe0 && e <= 0xe7) return "ADDRESS_SIZE_FAULT_IN";
        if (e >= 0xe8 && e <= 0xef) return "ADDRESS_SIZE_FAULT_OUT";
        if (e >= 0xf0 && e <= 0xff) return "MEMORY_ATTRIBUTE_FAULT";
        return "UNKNOWN";
}
static void fault_emit(Nord2GPU *d) {
        static uint64_t last;
        if (!d->core) return;
        d->core->revive_on = gFaultRevive != 0;
        static const char *const cls[4] = { "?", "context-killed", "trailing", "orphan" };
        static const char *const wh[4] = { "in no buffer of the client", "INSIDE buffer", "in the guard page / past the end of buffer", "in a range UNMAPPED" };
        static const char *const acc[4] = { "atomic", "execute", "read", "write" };
        static const char *const kn[4] = { "none", "compute", "render", "fragment" };
        n2c_fault_rec r;
        unsigned n = 0;
        while (d->core->fault_seq > last) {
                if (!n2c_fault_get(d->core, last, &r)) { last = d->core->fault_seq; break; }
                last = r.seq;
                const char *nm = r.ctx < N2C_MAX_CTX && gCtxName[r.ctx][0] ? gCtxName[r.ctx] : "?";
                int pid = r.ctx < N2C_MAX_CTX ? gCtxPid[r.ctx] : -1;
                if (++n > 4) continue;  /* at most 4 lines per tick; the AS line keeps the counts */
                IOLog("NORD2 GPU FAULT: #%llu %s AS%u ctx=%u %s[%d] status=0x%08x %s level %u %s%s src=0x%04x addr=0x%010llx %s handle=0x%x va=0x%010llx size=0x%llx (+0x%llx) freed_ago_ms=%llu"
                      " | job %s phase %u slot %u head=0x%010llx | client bufs=%u jobs=%u | gdvfs %u kHz vgpu %u uV uv_failed=%u\n",
                      (unsigned long long)r.seq, cls[r.cls & 3u], r.as, r.ctx, nm, pid, r.status, fault_exc_name(r.status), r.status & 7u,
                      acc[(r.status >> 8) & 3u], r.bus ? " BUS" : "", r.status >> 16, (unsigned long long)r.address, wh[r.where & 3u], r.bo_handle,
                      (unsigned long long)r.bo_va, (unsigned long long)r.bo_size, (unsigned long long)(r.address - r.bo_va),
                      (unsigned long long)r.freed_ago_ms,
                      kn[r.kind & 3u], r.phase, r.slot, (unsigned long long)r.head, r.ctx_bos, r.ctx_jobs, n2g_gpu_khz[gGdv.g.cur < N2G_GPU_NOPP ? gGdv.g.cur : 0], gGdv.curVg, (unsigned)gGdv.uvFailed);
        }
}
static void lat_emit(Nord2GPU *d) {
        n2c_lat l;
        if (!d->core || !n2c_lat_take(d->core, &l, 2000000ull)) return;
        gGdv.busyBase += l.busy_us;      /* 0461: the busy time stays cumulative for the DVFS poll */
        uint64_t w = l.window_start_us ? l.window_start_us : 1;
        IOLog("NORD2 GPU LAT: n=%llu win_ms=%llu irq_us=%llu/%u hand_us=%llu/%u phase_us=%llu/%u(%u) depth=%llu/%u busy_pm=%llu kinds=c%u/r%u/f%u same=%u rel_us=%llu/%u(%u) def=%u\n",
              l.submits, w / 1000, l.irq_n ? l.irq_sum / l.irq_n : 0ull, l.irq_max, l.hand_n ? l.hand_sum / l.hand_n : 0ull, l.hand_max,
              l.phase_n ? l.phase_sum / l.phase_n : 0ull, l.phase_max, l.phase_n, l.submits ? l.depth_sum / l.submits : 0ull, l.depth_max,
              l.busy_us * 1000 / w, l.kind[N2C_JOB_COMPUTE], l.kind[N2C_JOB_RENDER], l.kind[N2C_JOB_FRAGMENT], l.same_slot,
              l.rel_n ? l.rel_sum / l.rel_n : 0ull, l.rel_max, l.rel_n, l.deferred);
        IOLog("NORD2 GPU LAT2: mode=%d bh=%u runs=%u wake_max_us=%u poll_max_us=%u lock calls=%u wait_max_us=%u hold_max_us=%u sel=%u owners q=%u freed=%u KiB=%u drain_max_us=%u zero skip=%u MiB=%u vfail=%u full=%u\n",
              gLatMode, (unsigned)gBhOn, gBhRuns, gBhWaitMaxUs, gBhPollMaxUs, gLockCalls, gLockWaitMaxUs, gLockHoldMaxUs, gLockHoldSel,
              gOwnerQueued, gOwnerFreed, gOwnerKiB, gOwnerDrainMaxUs, gZeroSkip, gZeroSkipMiB, gZeroVerifyFail, gZeroFull);
        if (d->core->jsnext_rb)     /* 0630: what to read: same+cross = chains that waited in NEXT; promoted = handoffs without a software turn */
                IOLog("NORD2 GPU LAT3: jsnext=%u next_same=%u next_cross=%u promoted=%u evicted=%u stops=%u two_done=%u spurious=%u hand0_share_pm=%llu pipe=%u precise=%u overlap=%u held=%u\n",
                      (unsigned)d->core->jsnext, l.nx_same, l.nx_cross, l.nx_promoted, l.nx_evicted, l.nx_stops, l.nx_two, l.nx_spurious,
                      l.hand_n ? (unsigned long long)l.nx_promoted * 1000ull / l.hand_n : 0ull, (unsigned)d->core->pipe, l.precise_jobs, l.ov_starts, l.ov_blocked);
        /* 0660 (T1): per-context GPU time (top 4), run-time histogram per slot, CPU clean volume by reason, driver path averages.  Short lines: the
         * console cuts long ones.  gpuprof.py (gpusched-20261114/tools) reads them. */
        {
                struct n2c_prof pf; n2c_prof_take(d->core, &pf);
                unsigned top[4] = { N2C_MAX_CTX, N2C_MAX_CTX, N2C_MAX_CTX, N2C_MAX_CTX };
                for (unsigned i = 0; i < N2C_MAX_CTX; i++) {
                        const struct n2c_ctx *x = &d->core->ctxs[i];
                        if (x->state == N2C_CTX_FREE || !x->prof_jobs) continue;
                        for (unsigned k = 0; k < 4; k++) {
                                if (top[k] == N2C_MAX_CTX || x->prof_run_us > d->core->ctxs[top[k]].prof_run_us) {
                                        for (unsigned m = 3; m > k; m--) top[m] = top[m - 1];
                                        top[k] = i; break;
                                }
                        }
                }
                for (unsigned k = 0; k < 4 && top[k] < N2C_MAX_CTX; k++) {
                        const struct n2c_ctx *x = &d->core->ctxs[top[k]];
                        IOLog("NORD2 GPUPROF CTX: rank=%u name=%s pid=%d prio=%u run_us=%llu q_us=%llu qmax_us=%u jobs=%u win_ms=%llu\n", k, gCtxName[top[k]][0] ? gCtxName[top[k]] : "?",
                              gCtxPid[top[k]], (unsigned)x->prio, x->prof_run_us, x->prof_q_us, x->prof_q_max_us, x->prof_jobs, w / 1000);
                }
                for (unsigned i = 0; i < N2C_MAX_CTX; i++) { struct n2c_ctx *x = &d->core->ctxs[i]; x->prof_run_us = x->prof_q_us = 0; x->prof_q_max_us = x->prof_jobs = 0; }
                IOLog("NORD2 GPUPROF HIST: s0_100=%u s0_250=%u s0_500=%u s0_1k=%u s0_4k=%u s0_16k=%u s0_big=%u s1_100=%u s1_250=%u s1_500=%u s1_1k=%u s1_4k=%u s1_16k=%u s1_big=%u\n",
                      pf.hist[0][0], pf.hist[0][1], pf.hist[0][2], pf.hist[0][3], pf.hist[0][4], pf.hist[0][5], pf.hist[0][6],
                      pf.hist[1][0], pf.hist[1][1], pf.hist[1][2], pf.hist[1][3], pf.hist[1][4], pf.hist[1][5], pf.hist[1][6]);
                IOLog("NORD2 GPUPROF MAINT: cl_fullhard=%llu cl_legacy=%llu cl_implicit=%llu cl_dirty=%llu cl_full=%llu cl_presub=%llu cl_max_kib=%u inv_kib=%llu presub_us=%llu jobs=%u full=%u trunc=%u precise=%u\n",
                      pf.clean_pages[N2C_CR_FULLHARD] * 4, pf.clean_pages[N2C_CR_LEGACY] * 4, pf.clean_pages[N2C_CR_IMPLICIT] * 4, pf.clean_pages[N2C_CR_DIRTY] * 4,
                      pf.clean_pages[N2C_CR_FULL] * 4, pf.clean_pages[N2C_CR_PRESUBMIT] * 4, pf.clean_max_pages * 4, pf.inval_pages * 4, pf.preclean_us,
                      pf.jobs, pf.full_jobs, pf.truncated_jobs, pf.precise_jobs);
                IOLog("NORD2 GPUPROF PWR: pg=%u offs=%u ons=%u on_max_us=%u off_ms=%llu fail=%u vgpu_uv=%u vsram_uv=%u uv=%d uv_failed=%u\n", (unsigned)d->core->pg_on, pf.pg_offs, pf.pg_ons,
                      pf.pg_on_max_us, pf.pg_off_us / 1000, pf.pg_fail, gGdv.curVg, gGdv.curVs, gUv, (unsigned)gGdv.uvFailed);
                IOLog("NORD2 GPUPROF DRV: lock_wait_avg_us=%llu lock_hold_avg_us=%llu bh_wake_avg_us=%llu bh_poll_avg_us=%llu irq_cpu=%d bh_cpu=%d prio=%u evict=%u aged=%u reserve=%u\n",
                      gLockCalls ? gLockWaitSumUs / gLockCalls : 0ull, gLockCalls ? gLockHoldSumUs / gLockCalls : 0ull, gBhWaitN ? gBhWaitSumUs / gBhWaitN : 0ull,
                      gBhRuns ? gBhPollSumUs / gBhRuns : 0ull, gIrqCpu, gBhCpu, (unsigned)d->core->prio_on, l.pr_evicted, l.pr_aged, l.pr_reserved);
                gLockWaitSumUs = gLockHoldSumUs = gBhWaitSumUs = gBhWaitN = gBhPollSumUs = 0;
        }
        gBhRuns = gBhWaitMaxUs = gBhPollMaxUs = 0; gLockCalls = gLockWaitMaxUs = gLockHoldMaxUs = gLockHoldSel = 0;
        gOwnerDrainMaxUs = 0;
}
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
                d->core->lat_release_late = !(gLatMode & 2);   /* 0400 */
                if (gPipe != gPipeApplied) { d->core->pipe = gPipe != 0; gPipeApplied = gPipe; }   /* 0632 */
                if (gPreclean != gPrecleanApplied) { d->core->preclean = gPreclean != 0; gPrecleanApplied = gPreclean; }   /* 0661 */
                if (gPrio != gPrioApplied) { d->core->prio_on = gPrio != 0; gPrioApplied = gPrio; }   /* 0662 */
                if (gPg != gPgApplied || d->core->pg_idle_us != (uint32_t)gPgIdleUs) {      /* 0666 */
                        if (gPgIdleUs < 1000) gPgIdleUs = 1000;
                        d->core->pg_on = gPg != 0; d->core->pg_idle_us = (uint32_t)gPgIdleUs; gPgApplied = gPg;
                }
                if (gJsNext != gJsNextApplied) { (void)n2c_set_jsnext(d->core, gJsNext != 0); gJsNext = gJsNextApplied = d->core->jsnext ? 1 : 0; }   /* 0630 */
                n2c_poll(d->core);
                d->emitJobStats();
                lat_emit(d);
                as_emit(d);      /* 0500 */
                fault_emit(d);   /* 0750 */
                d->gdvfsPoll(now_us(NULL));      /* 0461 */
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
        /* 0398: use-count holds of finished jobs (a core that is no longer ready finishes nothing: all of them are given back) */
        unsigned reaped = 0;
        if (gUseGpu.used) {
                if (d->ready) reaped = usehold_reap(d->core);
                else {
                        reaped = sweet_use_gpu_reap(&gUseGpu, NULL, 0, gUseGpuOut, SWEET_USE_GPU_SLOTS);
                        for (unsigned i = 0; i < reaped; i++) (void)sweet_surface_use_kernel(gUseGpuOut[i], SWEET_SURFACE_USE_GPU, -1);
                }
        }
        IOLockUnlock(d->lock);
        for (unsigned i = 0; i < reaped; i++) { ((SweetSurface *)gUseGpuOut[i])->release(); gUseGpuOut[i] = NULL; }
}
bool Nord2GPU::start(IOService *provider) {
        if (!IOAcceleratorES::start(provider)) return false;
        lock = IOLockAlloc(); if (!lock) return false;
        int opt = 0; PE_parse_boot_argn("nord2_gpu", &opt, sizeof(opt)); enabled = opt == 1;
        int stats = 1; PE_parse_boot_argn("nord2_gpu_stats", &stats, sizeof(stats)); jobStats = stats != 0;
        if (!PE_parse_boot_argn("n2pf", &pfMode, sizeof(pfMode))) pfMode = 0;
        /* 0643: default mode 4 (auto) on the CPU-big, CPU-big2, CPU-little and GPU controllers; n2gdv +16 = LVTS off and the battery-only ladder */
        int lv = 4; { int gm = 0; if (PE_parse_boot_argn("n2gdv", &gm, sizeof(gm)) && (gm & 16)) { lv = 0; n2g_therm_soc_off = true; } }
        if (!PE_parse_boot_argn("nord2_lvts", &lv, sizeof(lv))) (void)PE_parse_boot_argn("n2lvts", &lv, sizeof(lv)); lvtsMode = (lv >= 1 && lv <= 4) ? (unsigned)lv : 0;
        if (lvtsMode == 4) lvtsMask = 0x17u;
        int lm = 0; if (PE_parse_boot_argn("nord2_lvts_mask", &lm, sizeof(lm)) && lm > 0 && (lm & ~(int)N2LVTS_MASK_ALL) == 0) lvtsMask = (uint32_t)lm;
        int lc = 0; if (PE_parse_boot_argn("nord2_lvts_max_c", &lc, sizeof(lc)) && lc >= 30 && lc <= 100) lvtsMaxC = lc;
        if (lvtsMode) IOLog("NORD2 GPU: LVTS thermal sensing requested: mode %u mask %x cold-start guard %d C\n", lvtsMode, lvtsMask, lvtsMaxC);
        { int gm = 0; if (PE_parse_boot_argn("n2gdv", &gm, sizeof(gm))) { gGdvfs = gm & 15; gGprof = (gm & 16384) ? 2 : (gm & 8192) ? 1 : 0; gVolt = (gm & 65536) ? 1 : 0; } }      /* 0631, 0633 */      /* 0461, 0492: +8; +16 belongs to nord2_cpufreq.c (0493: DRAM rate probe and watch) */
        int irq = 1; PE_parse_boot_argn("nord2_gpu_irq", &irq, sizeof(irq)); irqWanted = irq == 1;   /* 0132: on by default (confirmed on the phone: SPI 364 / intid 396 asserts with the job line); nord2_gpu_irq=0 turns it off, which also frees the 16 characters the boot line needed for it */
        int spi = 0; if (PE_parse_boot_argn("nord2_gpu_irq_spi", &spi, sizeof(spi)) && spi >= 32 && spi <= 900) irqSpi = (uint32_t)spi;
        if (irqWanted) IOLog("NORD2 GPU: job interrupt requested (spi %u), polling remains the fallback\n", irqSpi);
        { int gp = 1; PE_parse_boot_argn("n2gpool", &gp, sizeof(gp)); gFencePool = gp != 0;    /* 0395 */
          IOLog("NORD2 GPU FENCE: write fences on (%u slots), display-pool import 0x%llx+0x%llx %s\n", N2GF_SLOTS, (unsigned long long)N2GF_POOL_BASE, (unsigned long long)N2GF_POOL_SIZE, gFencePool ? "accepted" : "refused (n2gpool=0)"); }
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
                        /* 0400 */
                        int lm = 0; if (PE_parse_boot_argn("n2lat", &lm, sizeof(lm))) gLatMode = lm & 15;
                        gOwnerCall = thread_call_allocate_with_priority(owner_drain, NULL, THREAD_CALL_PRIORITY_KERNEL);
                        thread_t th = THREAD_NULL; retain();
                        if (kernel_thread_start_priority(bh_main, this, 92 /* BASEPRI_PREEMPT */, &th) == KERN_SUCCESS) { gBhOn = true; thread_deallocate(th); }
                        else release();
                        IOLog("NORD2 GPU LAT: 0400 bottom-half thread %s, owner release %s, mode n2lat=%d (1 BH off, 2 early release, 4 zero again, 8 inline owner free)\n",
                              gBhOn ? "on" : "FAILED", gOwnerCall ? "deferred" : "inline", gLatMode);
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
        if (gBhOn) { gBhOn = false; __atomic_store_n(&gBhExit, 1u, __ATOMIC_RELEASE); thread_wakeup((event_t)&gBhPending); }   /* 0400 */
        owner_drain(NULL, NULL);
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
        uint64_t heap = __atomic_load_n(&gHeapBytes, __ATOMIC_RELAXED);      /* 0441: heap buffers are not XNU memory */
        return total > heap ? total - heap : 0;
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
                                busy = n2c_mask_bit(j->write_mask, k) && (b->flags & N2C_BO_IMPORTED) && b->cookie &&
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
                                m = n2c_mask_bit(j->write_mask, k) && (b->flags & N2C_BO_IMPORTED) && b->cookie &&
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
/* 0395: write-fence API (nord2_gpu_fence.h).  Each call holds the driver lock for one scan of the 128 job records at most. */
enum { N2GF_Q_SEQ, N2GF_Q_PEND, N2GF_Q_DONE, N2GF_Q_LOW };
static uint64_t fence_query(int q, uint32_t sid, uint64_t upto) {
        Nord2GPU *g = gSurfaceGpu; uint64_t r = 0;
        if (!g || !g->lock) return 0;
        IOLockLock(g->lock);
        if (g->ready && !g->stopping && g->core) {
                struct n2gf_jobs v = fence_jobs(g->core);
                if (q == N2GF_Q_SEQ) r = n2gf_write_seq(&gFence, &v, sid);
                else if (q == N2GF_Q_LOW) r = n2gf_low_water(&gFence, &v, g->core->next_job_id);
                else { r = sid && n2gf_pending(&v, sid, q == N2GF_Q_PEND ? ~0ull : upto, NULL);
                       if (q == N2GF_Q_PEND) { gFence.st.q_pend++; gFence.st.q_pend_true += r; } else { gFence.st.q_done++; gFence.st.q_done_false += r; } }
        }
        IOLockUnlock(g->lock);
        return r;
}
extern "C" uint64_t nord2_gpu_surface_write_seq(uint32_t sid) { return fence_query(N2GF_Q_SEQ, sid, 0); }
extern "C" bool nord2_gpu_surface_write_pending(uint32_t sid) { return fence_query(N2GF_Q_PEND, sid, 0) != 0; }
extern "C" bool nord2_gpu_surface_writes_done(uint32_t sid, uint64_t upto) { return fence_query(N2GF_Q_DONE, sid, upto) == 0; }
/* 0402c (hwmulti-20261113-on0401c): LOCK-FREE peeks for the display's H6 fence -- the framebuffer must not take Nord2GPU::lock from
 * SwapEnd (backboardd's render thread) or poll it from the present thread (0402/0402b H6 did both and hard-locked the phone).  Only
 * aligned loads of fields the GPU path writes under the lock (gFence.s[].seq/.sid, core->next_job_id, core->jobs[].state/.id), no
 * pointer is followed: a stale value only makes the display wait a little longer or (rarely, bounded by its hold limit) show early. */
extern "C" uint64_t nord2_gpu_fence_seq_peek(uint32_t sid);
extern "C" uint64_t nord2_gpu_lowwater_peek(void);
extern "C" uint64_t nord2_gpu_fence_seq_peek(uint32_t sid) {
        uint64_t best = 0;
        for (unsigned i = 0; sid && i < N2GF_SLOTS; i++) {
                uint64_t s = __atomic_load_n(&gFence.s[i].seq, __ATOMIC_RELAXED);
                if (s > best && __atomic_load_n(&gFence.s[i].sid, __ATOMIC_RELAXED) == sid) best = s;
        }
        return best;
}
extern "C" uint64_t nord2_gpu_lowwater_peek(void) {      /* every job id <= this has left QUEUED/RUNNING/STOPPING; ~0 = no GPU */
        Nord2GPU *g = __atomic_load_n(&gSurfaceGpu, __ATOMIC_ACQUIRE);
        struct n2c_core *c = g ? g->core : NULL;
        if (!c) return ~0ull;
        uint64_t lo = __atomic_load_n(&c->next_job_id, __ATOMIC_ACQUIRE);
        lo = lo ? lo - 1 : ~0ull;
        for (unsigned i = 0; i < N2C_MAX_JOBS; i++) {
                uint8_t st = __atomic_load_n(&c->jobs[i].state, __ATOMIC_ACQUIRE);
                if (st != N2C_JOB_QUEUED && st != N2C_JOB_RUNNING && st != N2C_JOB_STOPPING) continue;
                uint64_t id = __atomic_load_n(&c->jobs[i].id, __ATOMIC_RELAXED);
                if (id && id - 1 < lo) lo = id - 1;
        }
        return lo;
}
extern "C" uint64_t nord2_gpu_completed_seq(void) { return fence_query(N2GF_Q_LOW, 0, 0); }
static bool fence_wait_pending(void *ctx) { return nord2_gpu_surface_write_pending((uint32_t)(uintptr_t)ctx); }
static void fence_wait_sleep(void *, uint32_t us) { IODelay(us); }
extern "C" bool nord2_gpu_surface_wait_idle(uint32_t sid, uint32_t timeout_us) {
        struct n2gf_wait_ops o = { (void *)(uintptr_t)sid, now_us, fence_wait_sleep, fence_wait_pending };
        uint32_t el = 0; bool idle = n2gf_wait(&o, timeout_us, &el);
        Nord2GPU *g = gSurfaceGpu;
        if (g && g->lock) { IOLockLock(g->lock); n2gf_wait_account(&gFence, idle, el); IOLockUnlock(g->lock); }
        return idle;
}
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
        /* 0663: run this on a big core, so the SPI is routed there (it was the PE that happened to run irqSetup, often a little core) */
        bool moved = gIrqBig && n2_bind_cpu(N2_IRQ_CPU);
        disable_preemption();   /* the SPI is routed to, and the handler installed on, the PE running this code */
        gIrqCpu = cpu_number();
        ok = nord2_gic_spi_configure(&g, irqIntid, nord2_gic_irouter_for_mpidr(__builtin_arm_rsr64("MPIDR_EL1")), NORD2_GPU_IRQ_PRIORITY);
        if (ok) { irqOwner = this; ml_install_interrupt_handler(this, 0, this, (IOInterruptHandler)irqDispatch, NULL); }
        enable_preemption();
        if (moved) { (void)thread_bind(PROCESSOR_NULL); (void)thread_block(THREAD_CONTINUE_NULL); }
        IOLog("NORD2 GPU IRQ: routed to cpu %d (%s)\n", gIrqCpu, moved ? "big cluster, 0663" : "the configuring cpu");
        irqState = ok ? NORD2_IRQ_LEARN : NORD2_IRQ_FAILED;
        IOLog("NORD2 GPU IRQ: spi=%u intid=%u configure=%s state=%s\n", irqSpi, irqIntid, ok ? "ok" : "refused",
              ok ? "learning (SPI stays disabled until seen pending)" : "polling");
}
void Nord2GPU::irqArm() {
        if (core->state != N2C_CORE_READY || !core->gpu_powered) return;
        irqFlag = 0;
        write_bank(&gpu, N2HW_JOB_IRQ_MASK, NORD2_GPU_JOB_IRQ_ALL); irqArmed = true;
}
void Nord2GPU::irqRearm() {
        if (core->state != N2C_CORE_READY || !core->gpu_powered) return;
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
        uint64_t tnow = now_us(NULL);
        if (core) n2c_lat_irq(core, tnow);                     /* 0400: completion time for NORD2 GPU LAT */
        if (gBhOn && !(gLatMode & 1)) { if (!gBhKickUs) gBhKickUs = tnow; __atomic_store_n(&gBhPending, 1u, __ATOMIC_RELEASE); thread_wakeup((event_t)&gBhPending); }
        else if (timer) timer->setTimeout(1, kMicrosecondScale);   /* 0140: the bottom half (n2c_poll: complete this job, start the next) runs now, not at the next tick */
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
        if (!map_bank_um(&lvtsAp, N2LVTS_AP_PHYS, N2LVTS_BLOCK_BYTES) ||
            ((lvtsMask & 7u) && !map_bank_um(&lvtsMcu, N2LVTS_MCU_PHYS, N2LVTS_BLOCK_BYTES)) || !n2lvts_init(&lvts, &o, this)) {
                lvtsState = 3; IOLog("NORD2 GPU LVTS: cannot map thermal controllers\n"); return;
        }
        int golden = 0;
        if (PE_parse_boot_argn("nord2_lvts_golden", &golden, sizeof(golden)) && golden > 0) lvts.cal.golden = (uint32_t)golden;
        enum n2lvts_error e = N2LVTS_OK;
        unsigned pre = lvtsMode == 4 ? n2lvts_read(&lvts, lvtsMask) : 0u;     /* 0643 auto: already sensing (read-only, gate checked) -> no writes */
        if (lvtsMode == 4 && pre) {
                lvtsState = 1;
        } else if (lvtsMode >= 2) {
                e = n2lvts_start(&lvts, lvtsMask, lvtsMode == 3);
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
        IOLog("NORD2 GPU LVTS: mode=%u mask=%x start=%s state=%u gate_open=%u cal=%s valid_sensors=%u gpu_ctrl clken=%x monctl0=%x id=%x msr0=%x step=%u pre=%u\n",
              lvtsMode, lvtsMask, n2lvts_error_string(e), lvtsState, pr ? 1u : (unsigned)probe.gate_open,
              lvts.cal.defaulted ? "default(approx)" : "efuse", good, probe.clken, probe.monctl0, probe.id, probe.msr[0], lvts.step, pre);
        for (unsigned c = 0; c < N2LVTS_CTRL_COUNT; c++) if (lvtsMask & (1u << c))     /* 0643: every sensor once */
                for (unsigned j = 0; j < n2lvts_ctrl[c].nsens; j++) { unsigned sn = n2lvts_ctrl[c].first_sensor + j;
                        IOLog("NORD2 GPU LVTS: %s sensor %u raw %x %s %d mC\n", n2lvts_ctrl[c].role, sn, lvts.raw[sn], lvts.temp_ok[sn] ? "ok" : "invalid", (int)lvts.temp_mC[sn]); }
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
/* 0643: every 2 s on the GPU worker thread: the hottest valid sensor of lvtsMask and the GPU sensor into the cache (the only LVTS reader after setup). */
void Nord2GPU::lvtsTick(uint64_t nowMs) {
        if (!lvtsMode || !lvtsTried || lvtsState == 0 || lvtsState == 3) return;
        if (lvtsLastMs && nowMs - lvtsLastMs < 2000) return;
        lvtsLastMs = nowMs ? nowMs : 1;
        int hot = lvtsHottest(lvtsMask);
        int32_t gmC = 0; bool g = lvtsGpu(&gmC);
        gGpuOk = g; gGpuCc = g ? gmC / 10 : 0;
        gSocApprox = lvts.cal.defaulted;
        gSocCc = hot; gSocOk = hot != (int)0x80000000; gSocMs = nowMs;
}
extern "C" bool nord2_soc_temperature_cC(int *centi, bool *approx) {
        uint64_t t = gSocMs, n = now_ms(NULL);
        if (!gSocOk || !t || n < t || n - t > 5000) return false;
        *centi = gSocCc; if (approx) *approx = gSocApprox; return true;
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
        int32_t gmC = gGpuCc * 10; bool gpu_ok = gGpuOk;                       /* 0643: the cache of lvtsTick (no LVTS access from client threads) */
        int hot = gSocOk ? gSocCc : (int)0x80000000;
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
        { struct n2gf_stats &f = gFence.st; struct n2gf_jobs v = fence_jobs(core); unsigned live = 0;    /* 0395 */
          for (unsigned i = 0; i < N2GF_SLOTS; i++) live += gFence.s[i].seq != 0;
          if (f.records || f.q_seq || f.q_pend || f.q_done || f.waits || f.pool_imports || f.pool_refused)
                IOLog("NORD2 GPU FENCE: rec=%llu hit=%llu fill=%llu ev=%llu/%llu live=%u miss=%llu q=%llu/%llu:%llu/%llu:%llu wait=%llu/%llu/%llu us=%llu/%u done=%llu next=%llu pool=%llu/%llu/%llu\n",
                      f.records, f.hits, f.fills, f.evict_done, f.evict_pending, live, f.miss_scans, f.q_seq, f.q_pend, f.q_pend_true, f.q_done, f.q_done_false,
                      f.waits, f.wait_ok, f.wait_timeout, f.waits ? f.wait_sum_us / f.waits : 0ull, f.wait_max_us, n2gf_low_water(&gFence, &v, core->next_job_id),
                      core->next_job_id, f.pool_imports, f.pool_pages, f.pool_refused);
          f.wait_max_us = 0; }
        if (gUseGpu.recorded || gUseGpu.full)    /* 0398: GPU use-count holds (used must come back to 0 when the GPU is idle) */
                IOLog("NORD2 SURF USE: gpu holds used=%u max=%u recorded=%llu reaped=%llu same-job=%llu table-full=%llu\n",
                      gUseGpu.used, gUseGpu.max_used, gUseGpu.recorded, gUseGpu.reaped, gUseGpu.dup, gUseGpu.full);
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
/* 0461: the shader cores' real clock over 500 us (CYCLE_COUNT, as clockCheck); up to three tries, the first within 5 % of khz wins.  Lock held, GPU idle or held. */
/* 0633: rail change on the PWRAP owner thread (sweet_touch's input thread polls the queue), like power-on.  Called from tick() under the lock
 * with job starts held; the input thread needs neither lock.  Any failure: rails marked uncertain by the rail module, GPU DVFS stops. */
struct RailSetReq { Nord2GPU *d; uint32_t vg, vs; int rc; };
int Nord2GPU::railSet(void *p) {
        RailSetReq *q = (RailSetReq *)p;
        q->rc = (int)n2rail_set(&q->d->rail, q->vg, q->vs);
        return q->rc;
}
/* 0665: VGPU of OPP idx: vendor table (PTPOD-off, max of segments) + kern.n2guvm, within [650 mV, the boot chain's VGPU]; 886 MHz = 800 mV (0633) */
uint32_t Nord2GPU::gdvfsOppVg(unsigned idx) {
        auto &v = gGdv;
        if (idx >= 5u) return 800000u;
        if (!gUv || v.uvFailed || v.bootVg != 750000u || v.bootVs != 750000u) return v.bootVg;
        unsigned oi = 0;
        if (!n2rail_opp_index_exact(n2g_gpu_khz[idx], &oi)) return v.bootVg;
        int m = gUvMargin < 0 ? 0 : gUvMargin > 50000 ? 50000 : gUvMargin;
        uint32_t t = n2rail_opp_table[oi].vgpu_uv + (uint32_t)m;      /* table in uV (OPP() scales mV x 100) */
        if (t < N2RAIL_SET_VGPU_MIN_UV) t = N2RAIL_SET_VGPU_MIN_UV;
        if (t > v.bootVg) t = v.bootVg;
        return n2rail_vgpu_uv(n2rail_vgpu_sel_for_uv(t), &t) ? t : v.bootVg;      /* the regulator's step (6.25 mV, rounded up) */
}
bool Nord2GPU::gdvfsVolt(uint32_t vg, uint32_t vs, const char *why) {
        auto &v = gGdv;
        RailSetReq q = { this, vg, vs, -1 };
        int result = -1;
        uint64_t t0 = now_us(NULL);
        n2owner_wait wait = { now_ms, sleep_ms, NULL };
        bool ok = n2owner_ready(&gpu_owner_queue) && n2owner_call(&gpu_owner_queue, Nord2GPU::railSet, &q, &wait, 1000, &result) && !result && q.rc == 0;
        v.voltOps++;
        const struct n2rail_snapshot &a = rail.after;
        if (v.voltOps <= 16 || !ok)
                IOLog("NORD2 GDVFS VOLT: %s %u/%u mV %s in %llu us (rail step %u, uncertain %u, VGPU da_sel %02x, VSRAM vosel1 %04x)\n", why, vg / 1000, vs / 1000,
                      ok ? "ok" : n2rail_error_string((enum n2rail_error)q.rc), now_us(NULL) - t0, rail.step, (unsigned)rail.uncertain, a.vgpu_da_vosel, a.vsram_vosel1);
        if (!ok) { v.voltErr++; if (rail.uncertain) { v.failed = true; v.dead = true; } }
        return ok;
}
uint32_t Nord2GPU::gdvfsMeasure(uint32_t khz) {
        uint32_t got = 0;
        write_bank(&gpu, 0x30, 5u);
        for (unsigned k = 0; k < 3; k++) {
                uint64_t c[2], t[2];
                for (unsigned i = 0; i < 2; i++) {
                        uint32_t hi, lo;
                        do { hi = read_bank(&gpu, 0x94); lo = read_bank(&gpu, 0x90); } while (hi != read_bank(&gpu, 0x94));
                        c[i] = ((uint64_t)hi << 32) | lo; t[i] = now_us(NULL);
                        if (!i) IODelay(500);
                }
                if (t[1] <= t[0] || c[1] <= c[0]) continue;
                got = (uint32_t)((c[1] - c[0]) * 1000ull / (t[1] - t[0]));
                if ((uint64_t)got * 100u >= (uint64_t)khz * 95u && (uint64_t)got * 100u <= (uint64_t)khz * 105u) break;
        }
        return got;
}
void Nord2GPU::gdvfsSwitch(uint64_t now) {
        auto &v = gGdv;
        if (now < v.armUs) return;
        if (v.dead) { v.pend = -1; core->hold_start = false; return; }
        bool busy = false;
        for (unsigned s = 0; s < 16; s++) busy |= core->running[s] != 0;
        if (busy && !(gGdvfs & 4)) {
                core->hold_start = true;
                if (now - v.armUs > 50000ull) { core->hold_start = false; v.gapMiss++; v.pend = -1; v.retryUs = now + 500000ull; n2c_poll(core); }      /* a long job: try again in 500 ms */
                return;
        }
        unsigned to = (unsigned)v.pend, from = v.g.cur, oi = 0;
        uint32_t khz = n2g_gpu_khz[to], meas = 0;
        v.pend = -1;
        if (to != v.boot && (v.failed || (gGdvfs & 3) != 2 || to > v.g.cap)) { core->hold_start = false; n2c_poll(core); return; }      /* switched off or capped since it was announced */
        enum n2clk_error e = N2CLK_E_PLL_RANGE;
        /* 0633 / 0665: voltage before frequency on the way up (the OPP's voltage: vendor table + margin, 800/800 mV for 886) */
        uint32_t vt = gdvfsOppVg(to), vst = vt > v.bootVs ? vt : v.bootVs;
        if (to > v.covered || vt > v.curVg) {
                if ((to == 5u && !gVolt) || !gdvfsVolt(vt, vst, to == 5u ? "raise for 886 MHz" : "raise before the clock")) {
                        core->hold_start = false;
                        if (to == 5u) { v.g.max_idx = (uint8_t)(v.covered < 4u ? v.covered : 4u); IOLog("NORD2 GDVFS VOLT: no raise: 886 MHz off until reboot\n"); }
                        n2c_poll(core); return;
                }
                v.curVg = vt; v.curVs = vst; v.covered = n2g_gpu_max_idx(vt, vst); v.raised = vt > v.bootVg;
        }
        if (to <= v.g.max_idx && to <= v.covered && n2rail_opp_index_exact(khz, &oi)) e = n2clk_retune(&clk, khz, n2rail_opp_table[oi].posdiv_power);
        uint64_t t1 = now_us(NULL);
        bool ok = e == N2CLK_OK;
        if (ok && v.switches < 8) { meas = gdvfsMeasure(khz); ok = (uint64_t)meas * 100u >= (uint64_t)khz * 95u && (uint64_t)meas * 100u <= (uint64_t)khz * 105u; }
        /* 0633 / 0665: frequency before voltage on the way down (also after a failed switch): the voltage of the OPP the PLL really runs */
        {
                int pi2 = n2g_gpu_index(n2rail_mfgpll_khz(read_bank(&clocks[N2CLK_APMIXED], N2CLK_MFGPLL_CON1)));
                uint32_t vl = gdvfsOppVg(pi2 >= 0 ? (unsigned)pi2 : v.boot), vls = vl > v.bootVs ? vl : v.bootVs;
                if (pi2 >= 0 && vl < v.curVg) {
                        if (gdvfsVolt(vl, vls, vl == v.bootVg ? "back to the boot rails" : "lower after the clock")) {
                                v.curVg = vl; v.curVs = vls; v.covered = n2g_gpu_max_idx(vl, vls); v.raised = vl > v.bootVg;
                                if (vl < v.bootVg) { v.uvWdUs = now_us(NULL) + 10000000ull; v.uvWdCount = core->stats.faulted + core->stats.resets + core->stats.timeouts; }
                        }      /* else left high: safe for every lower OPP */
                }
        }
        core->hold_start = false;
        if (ok) {
                n2g_gpu_applied(&v.g, to, t1 / 1000);
                v.wdUs = t1 + 500000ull; v.wdCount = core->stats.faulted + core->stats.resets + core->stats.timeouts;
                if (v.switches++ < 8) IOLog("NORD2 GDVFS switch %u: %u -> %u kHz in %llu us%s, core clock measures %u kHz\n", v.switches, n2g_gpu_khz[from], khz, t1 - now, busy ? " under a running job" : "", meas);
        } else {
                uint32_t pll = n2rail_mfgpll_khz(read_bank(&clocks[N2CLK_APMIXED], N2CLK_MFGPLL_CON1));
                int pi = n2g_gpu_index(pll);
                IOLog("NORD2 GDVFS FALLBACK: switch %u -> %u kHz failed (clk error %u step %u, measured %u kHz, PLL %u kHz, mux %u): %s\n", n2g_gpu_khz[from], khz, (unsigned)e, clk.retune_step, meas, pll,
                      (read_bank(&clocks[N2CLK_TOPCK], N2CLK_CFG_5) >> N2CLK_MFG_MUX_SHIFT) & N2CLK_MFG_MUX_MASK, v.failed || pi < 0 ? "clock left alone until reboot" : "back to the cold-start OPP, no more switching");
                v.errors++;
                if (v.failed || pi < 0) v.dead = true;
                if (pi >= 0) v.g.cur = (uint8_t)pi;
                v.failed = true;
        }
        n2c_poll(core);
}
void Nord2GPU::gdvfsPoll(uint64_t now) {
        static const char *const wn[N2G_NWHY] = { "hold", "util", "floor", "burst", "heavy", "therm", "rate", "up", "down", "boost" };
        auto &v = gGdv;
        lvtsTick(now / 1000);                    /* 0643: before any early return (GPU idle/off still updates the SoC temperature) */
        int mode = gGdvfs & 3;
        if (!core || core->state != N2C_CORE_READY || !board.powered) { if (core) core->hold_start = false; v.pend = -1; return; }
        if (!v.init) {
                uint32_t vg = 0, vs = 0;
                const struct n2rail_snapshot &a = rail.after;
                bool rails = rail.on && !rail.uncertain && (a.valid & 0x1ffu) == 0x1ffu && (a.vgpu_da_en & 1u) && (a.vsram_mon & 1u) &&
                             n2rail_vgpu_uv(a.vgpu_da_vosel, &vg) && n2rail_vsram_uv((uint8_t)((a.vsram_vosel1 >> 8) & 0x7fu), &vs);
                int bi = n2g_gpu_index(clk.after.khz);
                v.init = true; v.pend = -1; v.bat = -1;
                n2g_therm_init(&v.th);
                v.boot = bi < 0 ? 0u : (unsigned)bi;
                v.g.cur = (uint8_t)v.boot;
                v.g.max_idx = (uint8_t)(rails ? n2g_gpu_max_idx(vg, vs) : v.boot);
                v.covered = v.g.max_idx; v.bootVg = vg; v.bootVs = vs; v.curVg = vg; v.curVs = vs;      /* 0665 */
                if (gVolt && rails && vg == 750000u && vs == 750000u && v.g.max_idx == 4u) v.g.max_idx = 5;      /* 0633: 886 with a live raise */
                v.failed = v.dead = !rails || bi < 0 || !clk.mux_on || !clk.cg_on || v.g.max_idx < v.boot;
                IOLog("NORD2 GDVFS: kern.n2gdvfs=%d (0 off, 1 decisions only, 2 act, +4 no idle wait, +8 UI profile), cold-start %u kHz, rails VGPU %u uV / VSRAM %u uV cover up to %u kHz%s\n", gGdvfs, clk.after.khz, vg, vs,
                      n2g_gpu_khz[v.g.max_idx], v.dead ? ": NOT usable (rails unread, or the clock is not an exact OPP): decisions only" : "");
        }
        if (v.pend >= 0) gdvfsSwitch(now);
        /* 0665: once at boot, the OPP the GPU runs (654 MHz) goes to its vendor voltage; a fault within 10 s of any lowering -> boot rails for good */
        if (!v.dead && v.pend < 0 && gUv && !v.uvDone && !v.uvFailed && v.bootVg == 750000u && v.bootVs == 750000u) {
                v.uvDone = true;
                int pi3 = n2g_gpu_index(n2rail_mfgpll_khz(read_bank(&clocks[N2CLK_APMIXED], N2CLK_MFGPLL_CON1)));
                uint32_t t = gdvfsOppVg(pi3 >= 0 ? (unsigned)pi3 : v.boot);
                if (pi3 >= 0 && t < v.curVg && gdvfsVolt(t, v.curVs, "per-OPP voltage (vendor table)")) {
                        v.curVg = t; v.covered = n2g_gpu_max_idx(t, v.curVs);
                        v.uvWdUs = now + 10000000ull; v.uvWdCount = core->stats.faulted + core->stats.resets + core->stats.timeouts;
                }
        }
        if (v.uvWdUs && now < v.uvWdUs && !v.uvFailed && core->stats.faulted + core->stats.resets + core->stats.timeouts != v.uvWdCount) {
                v.uvFailed = true; v.uvWdUs = 0;
                IOLog("NORD2 GDVFS VOLT: a GPU fault / reset / timeout within 10 s of a voltage step at %u uV: back to the boot rails, per-OPP voltage off\n", v.curVg);
                IOLog("NORD2 GDVFS VOLT: (0750) trigger: jobs faulted=%llu resets=%llu timeouts=%llu mmu_faults=%llu at %u kHz; a fault that recurs at the boot rails is not the voltage (see 'NORD2 GPU FAULT:')\n",
                      (unsigned long long)core->stats.faulted, (unsigned long long)core->stats.resets, (unsigned long long)core->stats.timeouts,
                      (unsigned long long)core->stats.mmu_faults, n2g_gpu_khz[v.g.cur < N2G_GPU_NOPP ? v.g.cur : 0]);
                if (v.curVg < v.bootVg && gdvfsVolt(v.bootVg, v.bootVs, "fault fallback")) { v.curVg = v.bootVg; v.curVs = v.bootVs; v.covered = n2g_gpu_max_idx(v.bootVg, v.bootVs); }
        }
        if (v.lastUs && now - v.lastUs < N2G_GPU_POLL_MS * 1000ull) return;
        uint64_t busy = v.busyBase + core->lat.busy_us + (core->lat_slots_busy && now > core->lat_busy_since ? now - core->lat_busy_since : 0ull);
        unsigned pm = v.lastUs && busy >= v.lastBusy ? (unsigned)((busy - v.lastBusy) * 1000ull / (now - v.lastUs)) : 0u;
        bool first = !v.lastUs;
        v.lastBusy = busy; v.lastUs = now;
        if (first) return;
        uint64_t ms = now / 1000;
        bool slow = !v.slowUs || now - v.slowUs >= 2000000ull;
        if (slow) { int c = 0, sc = 0; bool ap = false; bool okb = battery_temperature(&c), oks = nord2_soc_temperature_cC(&sc, &ap);
                    (void)n2g_therm_update2(&v.th, okb, c, oks, sc, ap); v.bat = okb ? c : -1; }      /* 0643: battery + SoC ladder */
        struct n2g_hints h = {};
        nord2_dvfs_hints(&h);
        uint32_t old = v.nsw >= 5 ? v.swaps[(v.nsw - 5) % N2G_HIST] : h.swaps;
        v.swaps[v.nsw++ % N2G_HIST] = h.swaps;
        if (h.swaps - old >= N2G_ANIM_N || (h.touch_ms && ms >= h.touch_ms && ms - h.touch_ms < N2G_TOUCH_MS)) v.boostMs = ms ? ms : 1;
        n2g_gpu_sample(&v.g, pm);
        v.g.min_idx = (uint8_t)((gGdvfs & 8) ? v.boot : 0u); v.g.up_boost_pm = (uint16_t)((gGdvfs & 8) ? 800u : 0u);      /* 0492: UI profile */
        v.busySum += pm; v.polls++; if (pm > v.busyMax) v.busyMax = pm;
        bool uiBoost = v.boostMs && ms - v.boostMs < N2G_ANIM_MS;
        /* 0631: queue depth = unfinished jobs of every context; the fast profile's shadow runs on every poll */
        unsigned depth = 0;
        for (unsigned i = 0; i < N2C_MAX_CTX; i++) if (core->ctxs[i].state != N2C_CTX_FREE) depth += core->ctxs[i].jobs;
        v.depthLast = depth; if (depth > v.depthMax) v.depthMax = depth;
        if (!v.lastMs2) { v.gs.cur = v.g.cur; v.lastMs2 = ms; }
        n2g_gpu_sample(&v.gs, pm);
        v.gs.max_idx = v.g.max_idx; v.gs.min_idx = (uint8_t)v.boot;
        v.atMs[v.gs.cur % N2G_GPU_NOPP] += ms - v.lastMs2; v.lastMs2 = ms;
        unsigned want2 = n2g_gpu_decide_fast(&v.gs, &v.fs, ms, uiBoost, v.th.level, depth);
        if (want2 != v.gs.cur) { n2g_gpu_applied(&v.gs, want2, ms); v.sw2++; }
        unsigned want;
        if (gGprof == 1 || gGprof == 2) {
                v.g.min_idx = (uint8_t)v.boot; v.fa.perf = gGprof == 2;
                want = n2g_gpu_decide_fast(&v.g, &v.fa, ms, uiBoost, v.th.level, depth);
        } else want = n2g_gpu_decide(&v.g, ms, uiBoost, v.th.level);
        if (v.wdUs && now < v.wdUs && !v.failed && core->stats.faulted + core->stats.resets + core->stats.timeouts != v.wdCount) {
                IOLog("NORD2 GDVFS FALLBACK: a job fault, reset or timeout within 500 ms of a switch to %u kHz: back to the cold-start OPP, no more switching\n", n2g_gpu_khz[v.g.cur]);
                v.failed = true; v.errors++;
        }
        int go = -1;
        if (v.failed || mode != 2) { if (v.g.cur != v.boot) go = (int)v.boot; }      /* not acting: the cold-start OPP */
        else if (want != v.g.cur) go = (int)want;
        if (go >= 0 && v.pend < 0 && !v.dead && now >= v.retryUs) {
                v.pend = go; v.armUs = now;
                if (v.announced < 3) {
                        v.announced++; v.armUs = now + 400000ull;
                        IOLog("NORD2 GDVFS access: next in 400 ms is %u -> %u kHz (mfg_sel to the 218 MHz parking clock, MFGPLL_CON1, 20 us, mfg_sel back; job starts held)\n", n2g_gpu_khz[v.g.cur], n2g_gpu_khz[go]);
                }
        }
        struct n2g_gpu_report r = { n2g_gpu_khz[want], n2g_gpu_khz[v.g.cur], n2g_gpu_khz[v.g.cap], v.g.u_down, (uint32_t)(v.failed ? 8 | mode : mode), v.g.why, v.switches, v.errors };
        nord2_gdvfs_report = r;
        if (!slow) return;
        v.slowUs = now;
        if (mode)
                IOLog("NORD2 GDVFS: mode=%d%s cur=%u want=%u cap=%u floor=%u kHz pll=%u mux=%u busy_pm avg=%llu max=%u now=%u why=%s therm=%u bat_cC=%d switches=%u gap_miss=%u err=%u\n", gGdvfs,
                      v.dead ? " DEAD" : (v.failed ? " FAILED" : ""), n2g_gpu_khz[v.g.cur], n2g_gpu_khz[want], n2g_gpu_khz[v.g.cap], n2g_gpu_khz[v.g.floor],
                      n2rail_mfgpll_khz(read_bank(&clocks[N2CLK_APMIXED], N2CLK_MFGPLL_CON1)), (read_bank(&clocks[N2CLK_TOPCK], N2CLK_CFG_5) >> N2CLK_MFG_MUX_SHIFT) & N2CLK_MFG_MUX_MASK,
                      v.polls ? v.busySum / v.polls : 0ull, v.busyMax, v.g.u_down, wn[v.g.why % N2G_NWHY], v.th.level, v.bat, v.switches, v.gapMiss, v.errors);
        if (mode)       /* 0631: the fast profile as a shadow (or, with kern.n2gprof 1/2 and mode 2, what acts): time per OPP since the last line */
                IOLog("NORD2 GDVFS2: prof=%d shadow_fast cur=%u want=%u cap=%u switches=%u ms_at 512/654/805/836/886=%llu/%llu/%llu/%llu/%llu depth=%u/%u volt=%d covered=%u raised=%u ops=%u err=%u\n", gGprof,
                      n2g_gpu_khz[v.gs.cur], n2g_gpu_khz[v.gs.want], n2g_gpu_khz[v.gs.cap], v.sw2, v.atMs[1], v.atMs[2], v.atMs[3], v.atMs[4], v.atMs[5],
                      v.depthLast, v.depthMax, gVolt, n2g_gpu_khz[v.covered % N2G_GPU_NOPP], (unsigned)v.raised, v.voltOps, v.voltErr);
        for (unsigned i = 0; i < N2G_GPU_NOPP; i++) v.atMs[i] = 0;
        v.depthMax = 0;
        v.busySum = 0; v.polls = v.busyMax = 0;
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
                for (unsigned i = 1; i < N2C_MAX_CTX; i++) { unsigned used = 0; if (core->ctxs[i].state == N2C_CTX_FREE || !core->ctxs[i].bos) continue; for (unsigned k = 0; k < N2C_MAX_BOS; k++) { struct n2c_bo *b = core->ctxs[i].bos[k]; if (b) { used++; if (!b->refs && b->state == N2C_BO_MAPPED) held++; } } if (used > fullest) fullest = used; }
                IOLog("NORD2 MEM GPU: bos_live=%llu released_held=%u fullest_handle_table=%u/%u private=%llu MiB heap=%u/%llu MiB fallback=%u imports=%u\n", (unsigned long long)core->stats.bos_live, held, fullest, (unsigned)N2C_MAX_BOS, (unsigned long long)(privateTotal() >> 20),
                      gHeapBos, (unsigned long long)(gHeapBytes >> 20), gHeapFallback, gHeapImports);      /* 0441 */
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
        /* 0660 (T1a): the counter indices of GPU_PERF_ANALYSIS.md 4.5 (Mesa G77.xml) */
        IOLog("NORD2 PRFCNT F js0j=%llu js0t=%llu js0wr=%llu js0wi=%llu js0wd=%llu js0wf=%llu js1j=%llu js1wr=%llu js1wi=%llu js1wd=%llu js1wf=%llu js2=%llu\n",
              jm[8], jm[9], jm[12], jm[13], jm[14], jm[15], jm[16], jm[20], jm[21], jm[22], jm[23], jm[26]);
        IOLog("NORD2 PRFCNT G tri=%llu vis=%llu cull=%llu clip=%llu tbrd=%llu tbwr=%llu mmu=%llu rdstall=%llu xrd=%llu xwr=%llu arq1=%llu arq2=%llu arq3=%llu\n",
              ti[6], ti[11], ti[12], ti[13], ti[17], ti[19], ms[4], ms[17], ms[29], ms[42], ms[34], ms[35], ms[36]);
        IOLog("NORD2 PRFCNT H ezt=%llu ezk=%llu lzt=%llu lzk=%llu r64=%llu te=%llu fpkk=%llu comp=%llu cstarv=%llu astarv=%llu blsh=%llu icm=%llu\n",
              sh[12], sh[14], sh[15], sh[16], sh[17], sh[19], sh[20], sh[22], sh[25], sh[33], sh[34], sh[32]);
        IOLog("NORD2 PRFCNT I lsrf=%llu lsrs=%llu lsw=%llu rdlsc=%llu rdlscx=%llu rdtexx=%llu rdftcx=%llu v32=%llu v16=%llu\n",
              sh[44], sh[45], sh[46], sh[56], sh[57], sh[59], sh[55], sh[50], sh[51]);
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
        bool heapTry = dropLock && flags == 2u && bytes >= 0x10000ull;     /* 0441: a client's GPU-only buffer: the heap first (lock dropped), global check only on fallback */
        if (!heapTry && !nord2_gpu_global_private_ok(privateTotal(), bytes, globalLimit)) {
                budgetReject("global-private", c, requested, globalLimit); return kIOReturnNoMemory;
        }
        /* 0143: building the memory (allocation, wiring, zero fill, page walk) is the expensive part, a few ms and up to 100 ms for a big buffer: it
         * does not touch the core, so the global lock (job dispatch, completions) is released meanwhile when the caller is a client's ALLOC. */
        uint64_t t0 = now_us(NULL), t1 = t0, t2 = t0, t3 = t0, t4 = t0;
        if (dropLock) IOLockUnlock(lock);
        uintptr_t heapKva = 0;
        uint64_t heapPa = heapTry ? sweet_heap_gpu_take(bytes, &heapKva) : 0;
        if (heapPa && !heapKva) { sweet_heap_gpu_give(heapPa, bytes); heapPa = 0; }
        if (heapTry && !heapPa) {       /* 0441: heap off/full: normal memory, after the global check skipped above */
                __atomic_fetch_add(&gHeapFallback, 1u, __ATOMIC_RELAXED);
                IOLockLock(lock);
                if (!ready || stopping || !core) return kIOReturnNotReady;
                if (!nord2_gpu_global_private_ok(privateTotal(), bytes, globalLimit)) {
                        budgetReject("global-private", c, requested, globalLimit); return kIOReturnNoMemory;
                }
                IOLockUnlock(lock);
        }
        static unsigned extLogs; bool ext = (flags & 6u) != 0; bool lg = ext && extLogs < 4; if (lg) extLogs++;
        bool pageableWc = (flags & 4u) && (gAllocExt & 4u);
        if (lg) { IOLog("NORD2 GPU EXT: flags=%u bytes=%llu pageable=%d stage=md-create\n", flags, bytes, (int)pageableWc); }
        IOBufferMemoryDescriptor *md;
        if (heapPa) {
                md = NULL;              /* 0441: heap range, zeroed by the heap; kernel view = the heap's write-combined mapping */
        } else if (pageableWc) {
                /* Pageable memory has a VM object of its own, so its cache mode can be set without touching the kernel's shared object */
                md = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionInOut | kIOMemoryPageable | kIOMapWriteCombineCache | n2madv_md_options(), (vm_size_t)bytes, 16384);   /* 0481: + purgeable */
        } else {
                md = IOBufferMemoryDescriptor::withOptions(kIODirectionInOut | kIOMemoryKernelUserShared | ((flags & 4u) ? kIOMapWriteCombineCache : 0), (vm_size_t)bytes, 16384);
        }
        if (lg) { IOLog("NORD2 GPU EXT: flags=%u md=%p stage=md-created\n", flags, (void *)md); }
        t1 = now_us(NULL);
        if (!md && !heapPa) { if (dropLock) IOLockLock(lock); return kIOReturnNoMemory; }
        BufferOwner *owner = (BufferOwner *)IOMallocZero(sizeof(*owner));
        unsigned count = (unsigned)(bytes / 4096);
        n2c_page *pages = (n2c_page *)IOMallocZero(count * sizeof(*pages));
        if (!owner || !pages) { if (owner) IOFree(owner, sizeof(*owner)); if (pages) IOFree(pages, count*sizeof(*pages)); OSSafeReleaseNULL(md); if (heapPa) sweet_heap_gpu_give(heapPa, bytes); if (dropLock) IOLockLock(lock); return kIOReturnNoMemory; }
        owner->heapPa = heapPa; owner->heapLen = heapPa ? bytes : 0;     /* 0441: from here on free_owner gives the range back */
        if (pageableWc) {
                if (md->prepare(kIODirectionInOut) != kIOReturnSuccess) { IOFree(owner, sizeof(*owner)); IOFree(pages, count*sizeof(*pages)); md->release(); if (dropLock) IOLockLock(lock); return kIOReturnNoMemory; }
                owner->prepared = true;
                t2 = now_us(NULL);
                if (lg) { IOLog("NORD2 GPU EXT: flags=%u stage=prepared\n", flags); }
                /* 0400: fresh pageable kernel memory is anonymous VM: prepare() wired it by zero-fill faults, so the second zero pass (1-10 ms for
                 * 3-32 MiB, write-combined copies) is skipped.  Safety net: 65 sampled 16-byte reads must be zero, otherwise the full pass runs. */
                bool zskip = !(gLatMode & 4);
                for (unsigned k = 0; zskip && k <= 64; k++) {
                        uint64_t pg = (uint64_t)count * k / 64; if (pg >= count) pg = count - 1;
                        uint64_t v[2] = { 1, 1 };
                        if (md->readBytes(pg * 4096 + ((k * 1360u) & 4080u), v, sizeof(v)) != sizeof(v) || v[0] || v[1]) {
                                zskip = false; gZeroVerifyFail++;
                                static unsigned vlogs; if (vlogs < 4) { vlogs++; IOLog("NORD2 GPU LAT: pageable allocation not zero after prepare (bytes=%llu page=%llu): full zero pass\n", bytes, pg); }
                        }
                }
                static unsigned fullChecks;     /* the first 4 skipped buffers are read back completely */
                if (zskip && __atomic_fetch_add(&fullChecks, 1u, __ATOMIC_RELAXED) < 4) {
                        uint64_t *chk = (uint64_t *)IOMallocData(16384);
                        if (!chk) zskip = false;         /* cannot check: zero it (not a verification failure) */
                        else {
                                for (uint64_t off = 0; zskip && off < bytes; off += 16384) {
                                        IOByteCount n = (IOByteCount)(bytes - off < 16384 ? bytes - off : 16384);
                                        if (md->readBytes(off, chk, n) != n) { zskip = false; break; }
                                        for (unsigned i = 0; i < n / 8; i++) if (chk[i]) { zskip = false; break; }
                                }
                                IOFreeData(chk, 16384);
                                if (!zskip) { gZeroVerifyFail++; IOLog("NORD2 GPU LAT: full read-back of a pageable allocation not zero (bytes=%llu): full zero pass\n", bytes); }
                        }
                }
                if (!zskip && !(gLatMode & 4) && gZeroVerifyFail) gLatMode |= 4;   /* one failure turns the skip off for the rest of the boot */
                if (zskip) { gZeroSkip++; gZeroSkipMiB += (uint32_t)(bytes >> 20); }
                else {
                        gZeroFull++;
                        static uint8_t zeros[65536];   /* 0141: 64 KiB per call, not 4 KiB */
                        for (uint64_t off = 0; off < bytes; off += sizeof(zeros)) { uint64_t n = bytes - off < sizeof(zeros) ? bytes - off : sizeof(zeros); md->writeBytes(off, zeros, (IOByteCount)n); }
                }
        } else if (!heapPa) {
                if (lg) { IOLog("NORD2 GPU EXT: flags=%u stage=bzero\n", flags); }
                bzero(md->getBytesNoCopy(), (size_t)bytes);
        }
        t3 = now_us(NULL);
        if (lg) { IOLog("NORD2 GPU EXT: flags=%u stage=zeroed\n", flags); }
        bool valid = true;
        for (unsigned i = 0; i < count; i++) {
                if (heapPa) {           /* 0441: one contiguous heap range (the GPU MMU maps it page by page like any buffer) */
                        pages[i].pa = heapPa + (uint64_t)i * 4096; pages[i].cpu = (uint8_t *)heapKva + (uint64_t)i * 4096;
                        if (!sweet_heap_gpu_page_ok(pages[i].pa)) valid = false;
                        continue;
                }
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
        if (adopted && heapPa) {        /* 0441: counted while the core holds it (heap_uncount at teardown) */
                owner->heapCounted = true; __atomic_fetch_add(&gHeapBytes, bytes, __ATOMIC_RELAXED); __atomic_fetch_add(&gHeapBos, 1u, __ATOMIC_RELAXED);
        }
        if (heapPa) { static unsigned heapLogs; if (heapLogs < 8 || e != N2C_OK) { heapLogs++;
                IOLog("NORD2 GPU HEAP: ctx=%u GPU-only buffer %llu KiB -> heap pa 0x%llx e=%d (heap buffers %u, %llu MiB; fallbacks %u)\n", c, bytes >> 10, heapPa, (int)e,
                      gHeapBos, gHeapBytes >> 20, gHeapFallback); } }
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
#include "nord2_gpu_wod.inc"    /* 0480: import of a wire-on-demand surface */
IOReturn Nord2GPU::importSurface(unsigned c, uint32_t id, uint32_t offset, uint32_t bytes,
                                uint32_t *h, uint64_t *va, uint64_t *actual) {
        if (!ready || !core || c >= N2C_MAX_CTX || !bytes || (offset & 4095)) return kIOReturnBadArgument;
        uint64_t mapped = ((uint64_t)bytes + 4095) & ~4095ull, charged;
        /* IOSurface owns these pages: charge the import budget, not the private cap. */
        if (!nord2_gpu_import_size(mapped, core->ctxs[c].imported, &charged)) {
                budgetReject("import", c, mapped, NORD2_GPU_IMPORT_LIMIT); return kIOReturnNoMemory;
        }
        SweetSurface *s = sweet_surface_copy(id); if (!s) return kIOReturnNotFound;
        if (s->wod.on) return n2wod_import(this, c, s, offset, mapped, charged, h, va, actual);     /* 0480 */
        uint64_t base = (uint64_t)s->offset + offset;
        if (!s->memory || (base & 4095) || base > s->allocSize || mapped > s->allocSize - base ||
            base > s->memory->getLength() || mapped > s->memory->getLength() - base || !s->kernelAddress()) {
                s->release(); return kIOReturnBadArgument;
        }
        BufferOwner *owner = (BufferOwner *)IOMallocZero(sizeof(*owner));
        unsigned count = (unsigned)(mapped / 4096); n2c_page *pages = (n2c_page *)IOMallocZero(count * sizeof(*pages));
        if (!owner || !pages) { if (owner) IOFree(owner, sizeof(*owner)); if (pages) IOFree(pages,count*sizeof(*pages)); s->release(); return kIOReturnNoMemory; }
        owner->surface = s; bool valid = true;
        OSIncrementAtomic((volatile SInt32 *)&s->gpuImports);     /* 0398: given back in free_owner */
        unsigned inPool = 0, inRam = 0, nearPool = 0, inHeap = 0;    /* 0395: display-pool pages (sweet_fb_hw_pool) are normal DRAM below XNU's RAM */
        for (unsigned i=0; i<count; i++) {
                IOByteCount length; pages[i].pa = s->memory->getPhysicalSegment((uint64_t)s->offset+offset+(uint64_t)i*4096, &length, kIOMemoryMapperNone);
                pages[i].cpu = (void *)(s->kernelAddress()+base+(uint64_t)i*4096);
                bool ram = valid_ram(pages[i].pa), pool = !ram && gFencePool && n2gf_pool_page(pages[i].pa);
                bool heap = !ram && !pool && sweet_heap_gpu_page_ok(pages[i].pa);     /* 0441: 0440 heap surface (contiguous, write-combined CPU view) */
                inRam += ram; inPool += pool; inHeap += heap; nearPool += !ram && pages[i].pa - N2GF_POOL_BASE < N2GF_POOL_SIZE;
                if (!(ram || pool || heap) || length < 4096) valid = false;
        }
        if ((inPool || inHeap) && inRam) valid = false;  /* a pool/heap surface is one physical range: never mixed with XNU pages */
        bool adopted = false;
        enum n2c_error e = valid ? n2c_bo_map(core,c,pages,count,N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_IMPORTED,
                                             owner,charged,h,va,&adopted) : N2C_E_ARGS;
        if (inPool || nearPool) {
                static unsigned poolLogs;
                if (e == N2C_OK) { gFence.st.pool_imports++; gFence.st.pool_pages += count; } else gFence.st.pool_refused++;
                if (poolLogs < 16) { poolLogs++;
                        IOLog("NORD2 GPU FENCE: pool import ctx=%u sid=%u pa=0x%llx pages=%u pool=%u ram=%u e=%d%s\n", c, id, pages[0].pa, count, inPool, inRam, (int)e,
                              gFencePool ? "" : " (n2gpool=0)"); }
        }
        if (inHeap) {   /* 0441 */
                static unsigned heapImportLogs; if (e == N2C_OK) __atomic_fetch_add(&gHeapImports, 1u, __ATOMIC_RELAXED);
                if (heapImportLogs < 8 || e != N2C_OK) { heapImportLogs++;
                        IOLog("NORD2 GPU HEAP: import ctx=%u sid=%u pa=0x%llx pages=%u heap=%u e=%d (heap imports %u)\n", c, id, pages[0].pa, count, inHeap, (int)e, gHeapImports); }
        }
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
                if(result==kIOReturnSuccess)fence_record(core,c,&sub,*id);   /* 0395 */
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
        enum n2c_error e=n2c_ctx_open(gpu->core,(uint64_t)(uintptr_t)task,NORD2_GPU_CONTEXT_LIMIT,false,&context);
        if(e==N2C_E_NO_CTX){
                /* 0500: "no free context" is a refusal, not "not ready": NoResources (ENOMEM in libredwoodair's kerr) ends its ALLOC at once
                 * instead of the 100 x 100 ms EIO retry loop that blocked the process ~10 s (FrontBoard watchdog kills). */
                const n2c_as_stats &st=gpu->core->as_stats;
                if(st.ctx_refused<=16||!(st.ctx_refused&63)){char name[17]={};proc_selfname(name,sizeof(name));
                        IOLog("NORD2 GPU AS: context refused pid=%d(%s) open=%u peak=%u refused=%llu mode=%s\n",proc_selfpid(),name,st.ctx_open,st.ctx_peak,
                              (unsigned long long)st.ctx_refused,gpu->core->as_dyn?"dynamic":"static");}
                return kIOReturnNoResources;
        }
        if(e==N2C_OK&&context<N2C_MAX_CTX){      /* 0662: compositor first; 0660: the name for the per-context GPU time lines */
                char name[17]={};proc_selfname(name,sizeof(name));memcpy(gCtxName[context],name,sizeof(gCtxName[0]));gCtxPid[context]=proc_selfpid();
                unsigned pr=N2C_PRIO_NORMAL;
                if(!strncmp(name,"backboardd",16))pr=N2C_PRIO_HIGH;
                else if(!strncmp(name,"mediaserverd",16)||!strncmp(name,"VTDecoderXPCSer",16)||!strncmp(name,"VTEncoderXPCSer",16))pr=N2C_PRIO_LOW;
                (void)n2c_ctx_set_prio(gpu->core,context,pr);
        }
        static unsigned peak_logged;
        if(e==N2C_OK&&gpu->core->as_stats.ctx_open>N2C_HW_AS-1&&gpu->core->as_stats.ctx_open>peak_logged){
                peak_logged=gpu->core->as_stats.ctx_open;char name[17]={};proc_selfname(name,sizeof(name));
                IOLog("NORD2 GPU AS: context %u opened pid=%d(%s): %u client contexts open (more than the %u hardware client AS)\n",context,proc_selfpid(),name,
                      gpu->core->as_stats.ctx_open,N2C_HW_AS-1);
        }
        return io_error(e);
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
        if(sel==NORD2_GPU_SEL_MADVISE)return n2madv_call(gpu,lock,&closed,&context,a);   /* 0481 */
        if(!nord2_gpu_call_valid(sel,a->scalarInputCount,a->scalarOutputCount,a->structureInputSize,a->structureOutputSize,
                                 a->scalarInput,a->scalarOutput,a->structureInput,a->structureOutput) ||
           a->structureInputDescriptor || a->structureOutputDescriptor || a->asyncWakePort!=MACH_PORT_NULL)return kIOReturnBadArgument;
        uint64_t lk0=now_us(NULL);IOLockLock(lock);IOLockLock(gpu->lock);uint64_t lk1=now_us(NULL);IOReturn result=kIOReturnUnsupported;bool clientHeld=true;
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
                struct n2c_submit sub={};uint64_t id;
                if(a->structureInputSize==sizeof(nord2_gpu_submit_request4)){      /* 0661: v4, 128 handles */
                        const nord2_gpu_submit_request4 *r4=(const nord2_gpu_submit_request4 *)a->structureInput;
                        if(r4->version!=4||r4->nr_handles>NORD2_GPU_SUBMIT4_MAX||(r4->flags&~(uint32_t)(N2C_SUBMIT_PRECISE|N2C_SUBMIT_TRUNCATED))||r4->reserved[0]||r4->reserved[1]||!gV4){result=kIOReturnBadArgument;break;}
                        sub.kind=r4->kind;sub.nr_handles=r4->nr_handles;sub.heads[0]=r4->heads[0];sub.heads[1]=r4->heads[1];
                        memcpy(sub.handles,r4->handles,sizeof(uint32_t)*r4->nr_handles);
                        sub.write_mask=r4->write_mask[0];for(unsigned w=1;w<4;w++)sub.write_mask_hi[w-1]=r4->write_mask[w];
                        sub.flags=r4->flags&N2C_SUBMIT_TRUNCATED;
                        if(gPipeAbi){sub.flags|=r4->flags&N2C_SUBMIT_PRECISE;sub.touch_mask=r4->touch_mask[0];for(unsigned w=1;w<4;w++)sub.touch_mask_hi[w-1]=r4->touch_mask[w];}
                }else{
                        nord2_gpu_submit_request3 r;bzero(&r,sizeof(r));memcpy(&r,a->structureInput,a->structureInputSize<sizeof(r)?a->structureInputSize:sizeof(r));
                        bool v3=a->structureInputSize==sizeof(nord2_gpu_submit_request3);      /* 0632 */
                        if(v3?(r.version!=3||(r.flags&~(uint32_t)N2C_SUBMIT_PRECISE)||r.reserved[0]||r.reserved[1]):r.version!=2){result=kIOReturnBadArgument;break;}
                        if(r.nr_handles>32){result=kIOReturnBadArgument;break;}
                        sub.kind=r.kind;sub.nr_handles=r.nr_handles;sub.heads[0]=r.heads[0];sub.heads[1]=r.heads[1];
                        sub.write_mask=r.write_mask;memcpy(sub.handles,r.handles,sizeof(r.handles));
                        if(v3&&gPipeAbi){sub.flags=r.flags;sub.touch_mask=r.touch_mask;}
                        if(r.nr_handles==32)sub.flags|=N2C_SUBMIT_TRUNCATED;      /* 0661: the old maximum = the producer may have dropped buffers */
                }
                result=io_error(n2c_submit(gpu->core,context,&sub,&id));if(result==kIOReturnSuccess){fence_record(gpu->core,context,&sub,id);a->scalarOutput[0]=id;a->scalarOutputCount=1;
                        /* 0661: the job's CPU clean runs here, in the submitting thread, with the GPU lock dropped (the per-connection lock stays held, so
                         * this client can neither close nor release meanwhile; the job holds references on every buffer it cleans) */
                        const struct n2c_job *pj=NULL;for(unsigned i=0;i<N2C_MAX_JOBS;i++)if(gpu->core->jobs[i].state==N2C_JOB_QUEUED&&gpu->core->jobs[i].id==id)pj=&gpu->core->jobs[i];
                        if(pj&&pj->prep){
                                struct n2c_prep *pp=(struct n2c_prep *)IOMallocData(sizeof(struct n2c_prep));
                                if(pp&&n2c_prep_begin(gpu->core,context,id,pp)){IOLockUnlock(gpu->lock);n2c_prep_run(gpu->core,pp);IOLockLock(gpu->lock);}
                                else if(pp){pp->n=0;pp->pages=0;pp->us=0;}
                                if(pp){n2c_prep_commit(gpu->core,context,id,pp);IOFreeData(pp,sizeof(struct n2c_prep));}
                                else{struct n2c_prep none;none.n=0;none.pages=0;none.us=0;n2c_prep_commit(gpu->core,context,id,&none);}
                        }
                        if(gpu->irqState==NORD2_IRQ_ACTIVE)gpu->irqArm();}break;
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
        if(sel!=NORD2_GPU_SEL_WAIT && sel!=NORD2_GPU_SEL_WAIT_SEQ && sel!=NORD2_GPU_SEL_ALLOC){   /* 0400: lock wait / hold (these three drop the GPU lock) */
                uint64_t lk2=now_us(NULL);gLockCalls++;
                if(lk1-lk0>gLockWaitMaxUs)gLockWaitMaxUs=(uint32_t)(lk1-lk0);
                gLockWaitSumUs+=lk1-lk0;gLockHoldSumUs+=lk2-lk1;      /* 0660 */
                if(lk2-lk1>gLockHoldMaxUs){gLockHoldMaxUs=(uint32_t)(lk2-lk1);gLockHoldSel=sel;}
        }
        IOLockUnlock(gpu->lock);if(clientHeld)IOLockUnlock(lock);return result;
}
