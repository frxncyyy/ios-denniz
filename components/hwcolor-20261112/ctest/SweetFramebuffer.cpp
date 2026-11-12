/*
 * Built-in IOMobileFramebuffer for SWEET: the ABL's continuous-splash
 * framebuffer (1080x2400 a8r8g8b8 in RAM, the panel scans it out by
 * itself) as the main display, driven by CoreAnimation's CPU renderer.
 *
 * IOMobileFramebuffer.framework (iOS 16.7) counts /arm-io children with
 * device_type "display-subsystem", waits for that many services matching
 * IOServiceMatching("IOMobileFramebuffer"), opens each with type 0 and
 * reads canvas-width/canvas-height from the service's provider. The class
 * here (AppleCLCD, an IOMobileFramebuffer) matches the /arm-io/disp0 nub.
 *
 * User client selectors used by CoreAnimation:
 *   3  GetLayerDefaultSurface [layer, ?] -> [IOSurface port name]
 *   4  SwapBegin -> [swap id]
 *   5  SwapEnd, struct 0x46c: +0x50 swap id, +0x54 IOSurface id per layer
 *      (4 layers), +0x64 rect a / +0xc4 rect b per layer (int x,y,w,h),
 *      +0x104 enabled layer mask
 *   6  SwapWait, 7 GetID, 8 GetDisplaySize -> [w, h],
 *   12 RequestPowerChange [state]: 0 = display off (backboardd, power
 *      button / auto-lock), else on. Off fades the panel out over a few
 *      vsyncs (halving each pixel) and keeps it black until on again;
 *      the panel itself stays powered (AMOLED black pixels are off).
 *   18 IsMainDisplay,
 *   28 GetDotPitch, 72 Enable/DisableNotifications [func, refcon, type, 0]
 * Everything else is accepted and logged once.
 *
 * SwapEnd copies the enabled layers' surfaces into the panel memory (no
 * scaling, CPU copy). Vsync (notification type 5) is a 60 Hz timer that
 * sends (count, mach_absolute_time, 0) as 32-bit halves to clients that
 * enabled it, through the port they registered (IOConnectSetNotificationPort).
 *
 * The panel memory itself is published as an IOSurface and returned as the
 * default framebuffer surface. The kernel text console keeps drawing over
 * the GUI by default: boot-arg sweetfb_noconsole=1 stops drawing it from
 * the first swap on (it still reaches the persistent log; panics draw).
 */
#include "SweetSurface.h"
#include "sweet_fb_pending_surfaces.h"
#include "sweet_fb_present.h"
#include "sweet_fb_scan.h"
#include "sweet_fb_swapwait.h"
#include "sweet_fb_blend.h"           /* 0390 */
#include "sweet_fb_scene.h"           /* 0392 */
#include "sweet_fb_hw.h"              /* 0396 */
#include "sweet_fb_pace.h"            /* 0397 */

/*
 * 0397 (fbpace-20261112): keep the panel at 90 Hz and measure the present pacing.
 *   - the 90 Hz guard judges the real transfer (last not-done poll), keeps the DSI underrun bit of each transfer, reverts on a timeout or
 *     on two faults, logs why (NORD2 FB PANEL line), retries an automatic revert (sweet_fb_pace.h);
 *   - sysctl kern.n2panelhz: read = the panel rate now (90 / 60), write 90 / 60 = switch between two frames;
 *   - at 60 Hz after a revert the clients' frame clock follows the 60 Hz TE (it stayed at 11.2 ms against a 16.8 ms panel);
 *   - the 0190 safe-window width only learns from transfers whose FRAME_DONE was seen on time;
 *   - a NORD2 FB PACE line per stat window, sysctl kern.n2fbpace (since boot or the last kern.n2fbpacereset=1);
 *   - "rate_hz" in NORD2 FB PRESENT is the panel's measured refresh rate (it printed the constant 60 / present divisor).
 */

/*
 * 0396 (hwfb-20261112): the display controller composes the layers.  kern.n2fbhw (sysctl, default 0):
 *   0 = CPU path for every swap (as 0393); the planner still counts what would be eligible (NORD2 FB HW lines)
 *   1 = dry run: every swap planned, the first plans logged, no register written
 *   2 = H3: a swap whose only visible layer covers the panel and lies in the display pool (0394) is shown by OVL*_2L layer 0
 *       reading the surface itself (no copy); anything else takes the CPU path for that frame (counted by reason)
 *   3 = H4: up to two layers (bottom opaque, upper with per-pixel alpha in the CON variant kern.n2fbhwpm, default 2)
 * kern.n2fbhwtest = 1..5 runs an H2 test pattern through the overlay (1 pattern, 2/3/4 + gradient tile across the seam with CON
 * pm 0/2/1, 5 = 1..4 in a row), each held kern.n2fbhwhold ms (default 3000), then the previous picture is put back.
 * kern.n2fbhwstat (read only) = the counters.  Any underflow / DSI underrun / frame-done timeout / register read-back mismatch on
 * a HW frame (or SWEET_FB_HW_RACE_LIMIT consecutive GPU-write races) sets kern.n2fbhw back to 0 and restores the CPU path.
 * GPU fence rule: a HW frame is never shown while a GPU write job on one of its surfaces is in flight (never forced).
 */
#if __has_include("sweet_fb_hw_pool.h")
#include "sweet_fb_hw_pool.h"         /* 0394 (Track A) */
#else       /* the 0394 contract (hwcomp-20261112/sweet_fb_hw_pool.h), for a stand-alone compile check only */
#define SWEET_FB_HW_POOL_CACHE_WC 1u
static inline bool  sweet_fb_hw_pool_contains(uint64_t pa, uint64_t len) { return len != 0 && pa >= 0xC0000000ull && len <= 0x10000000ull && pa - 0xC0000000ull <= 0x10000000ull - len; }
extern "C" uint64_t sweet_surface_pool_pa_obj(const void *surface);
extern "C" uint64_t sweet_surface_pool_size_obj(const void *surface);
extern "C" uint32_t sweet_surface_pool_cache_obj(const void *surface);
#endif
#if __has_include("nord2_gpu_fence.h")
#include "nord2_gpu_fence.h"          /* 0395 (Track B) */
#else
extern "C" uint64_t nord2_gpu_surface_write_seq(uint32_t surface_id);
extern "C" bool     nord2_gpu_surface_write_pending(uint32_t surface_id);
#endif
static uint32_t sweet_fb_hw_mode;               /* kern.n2fbhw */
static uint32_t sweet_fb_hw_pm = 2;             /* kern.n2fbhwpm */
static uint32_t sweet_fb_hw_test_req;           /* kern.n2fbhwtest: requested test, taken by handOver */
static uint32_t sweet_fb_hw_test_last;          /* last test run (read back by the sysctl) */
static uint32_t sweet_fb_hw_hold_ms = 3000;     /* kern.n2fbhwhold */
/* 0399 (hwcolor-20261112): colour order.  kern.n2fbhwcsw bit 0 flips CON BYTE_SWAP (R<->B) on layer 1, bit 1 on layer 0, for every HW
 * frame and test; kern.n2fbhwselftest (default 1): the first switch to mode 2/3 of a boot runs the colour test (test 6) first. */
static uint32_t sweet_fb_hw_csw;
static uint32_t sweet_fb_hw_selftest = 1, sweet_fb_hw_selftest_done;
static uint32_t sweet_fb_hw_mode_gen;           /* 0397: kern.n2fbhw writes (a manual setting cancels a fault backoff) */
static uint32_t sweet_fb_pool_alias_mode = 1;   /* 0397: kern.n2fbpoolalias: CPU-path reads of WC pool surfaces via a read-only cacheable alias */

/* 0390: n2fbblend=0 copies the SwapEnd layers over each other as before 0390 (default 1: composite them). */
static uint32_t sweet_fb_blend_mode = 1;
/* 0391 (fbdiag-20261112): run fbfix1 -- q10jfb composited the 2-layer swaps (layer 0 = Photos' drawable, layer 1 = backboardd's frame,
 * 85% fully transparent) and the grid was still black: the CPU copy reads Photos' drawable as black while the GPU (backboardd's earlier
 * composites) and the userland CPU probes read it in colour.  n2fbinval=1 (default) cleans+invalidates the CPU cache over every layer
 * of a multi-layer present before it is read (stale CPU lines of a GPU-written surface); n2fbinval=0 = as 0390.  Diagnostics for the
 * first 40 multi-layer presents, then one in 30: 5 pixels of each layer read before the GPU acquire, after it and after a CIVAC of
 * the line, the surface seed / busy state, and the same points in the panel after the copy; the 60 swap records after the first
 * multi-layer one. */
extern "C" {
#include <arm/caches_internal.h>
}
static uint32_t sweet_fb_inval_mode = 1;
static uint64_t sweet_fb_multi_seen;
static uint32_t sweet_fb_swaplog_n;
/* 0392: the layers a swap leaves out of its mask keep their surface (sweet_fb_scene.h); n2fbretain=0 = every record is the whole scene. */
static uint32_t sweet_fb_retain_mode = 1;
static struct sweet_fb_scene sweet_fb_scene_state;
static uint32_t sweet_fb_retain_logged;

extern "C" int sweet_console_muted;     /* osfmk/console/serial_console.c */
extern "C" void nord2_dvfs_ui_kick(void);   /* bsd/dev/nord2_cpufreq.c (0184): the UI is active, the CPU governor keeps its boost floor */

#if defined(SWEET)
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/system.h>
#include <pexpert/pexpert.h>
#include <kern/clock.h>
#include <sys/proc.h>
#include <sys/sysctl.h>               /* 0396: kern.n2fbhw* */
#include <sys/errno.h>
#if defined(NORD2)
extern "C" {
#include <arm64/sweet_early.h>
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
}
/* Panel transfer registers, the same constants as osfmk/arm64/sweet_early.c (NORD2_DSI0, NORD2_DISP_MUTEX, ...). */
#define SWEET_FB_DSI0              0x1400e000UL
#define SWEET_FB_DISP_MUTEX        0x14117000UL
#define SWEET_FB_DSI_START         0x00u
#define SWEET_FB_DSI_INTSTA        0x0cu
#define SWEET_FB_DSI_FRAME_DONE    (1u << 4)
#define SWEET_FB_DSI_BUSY          (1u << 31)
#define SWEET_FB_DSI_TE_RDY        (1u << 2)
#define SWEET_FB_DSI_CMD_DONE      (1u << 1)
#define SWEET_FB_PANEL_HZ_DEFAULT  90u
#define SWEET_FB_MUTEX_EN0         0x20u
/* OVL 2L layer 0 of both pipes (osfmk/arm64/sweet_early.c nord2_display_overlay_init verified the same registers). */
#define SWEET_FB_OVL0_2L           0x14001000UL
#define SWEET_FB_OVL1_2L           0x14101000UL
#define SWEET_FB_OVL_INTSTA        0x08u
#define SWEET_FB_OVL_SIZE          0x38u
#define SWEET_FB_OVL_PITCH         0x44u
#define SWEET_FB_OVL_ADDR          0xf40u
/* 0396: larb0/larb1 NON_SEC_CON of the OVL ports (larb0 p5 OVL_RDMA0, p6 OVL_2L_RDMA1; larb1 p5 OVL_RDMA1, p6 OVL_2L_RDMA0): bit 0 MMU_EN
 * must be 0 (physical addresses) for the HW path (mt6885-larb-port.h, mtk-smi.c). */
#define SWEET_FB_LARB0             0x14118000UL
#define SWEET_FB_LARB1             0x14119000UL
#define SWEET_FB_LARB_NONSEC(p)    (0x380u + 4u * (p))
#endif

enum {
	kSweetFBGetLayerDefaultSurface  = 3,
	kSweetFBSwapBegin               = 4,
	kSweetFBSwapEnd                 = 5,
	kSweetFBSwapWait                = 6,
	kSweetFBGetID                   = 7,
	kSweetFBGetDisplaySize          = 8,
	kSweetFBRequestPowerChange      = 12,
	kSweetFBIsMainDisplay           = 18,
	kSweetFBGetDotPitch             = 28,
	kSweetFBNotifications           = 72,
};

#define SWEET_FB_SWAP_REC_SIZE          0x46c
#define SWEET_FB_LAYERS                 4
#define SWEET_FB_NOTIFY_TYPES           8
#define SWEET_FB_NOTIFY_VSYNC           5
#define SWEET_FB_VSYNC_US               16667
#define SWEET_FB_DOT_PITCH              458     /* ppi, as the iPhone X panel */
/*
 * The iOS userland only has status bar / layout data for real iPhone
 * screens (UIKit: "No status bar visual provider for screen", the red
 * rdar://45025538 bar for 360x800 points). The display is therefore
 * presented as the iPhone X panel, 1125x2436 (375x812 points @3x), and the
 * centered 1080x2400 part of it is shown on the 1080x2400 panel.
 * Boot-arg sweetfb_native=1 presents the real panel size instead.
 */
#define SWEET_FB_LOGICAL_W              1125
#define SWEET_FB_LOGICAL_H              2436
#define SWEET_FB_FORMAT_BGRA            0x42475241

static void *
sweet_fb_retain_surface(uint32_t id)
{
	return sweet_surface_copy(id);          /* retained, or NULL */
}

static void
sweet_fb_release_surface(void *p)
{
	((SweetSurface *)p)->release();
}

class IOMobileFramebuffer : public IOService {
	OSDeclareDefaultStructors(IOMobileFramebuffer);
};

OSDefineMetaClassAndStructors(IOMobileFramebuffer, IOService);

class IOMobileFramebufferUserClient;

struct SweetFBDiagnostics {
	bool enabled;
	uint64_t vsync, swaps, off, flushes, alloc_failed, presents, begin_failed;
	uint64_t empty_records, active_layers, missing_surfaces, copied_layers, non_bgra;
};

/* A finite set of callback checkpoints; no per-frame logging after these. */
static bool
sweet_fb_diag_checkpoint(uint64_t count)
{
	return count == 1 || count == 60 || count == 600 || count == 3600;
}

class AppleCLCD : public IOMobileFramebuffer {
	OSDeclareDefaultStructors(AppleCLCD);

	IOWorkLoop         *fWorkLoop;
	IOTimerEventSource *fVsync;
	uint64_t            fVsyncCount;

	static void vsyncFired(OSObject *owner, IOTimerEventSource *sender);

public:
	uint64_t      fPhys;
	uint32_t      fWidth, fHeight, fRowBytes;
	uint32_t      fLogicalW, fLogicalH;     /* size reported to userland */
	int32_t       fOffX, fOffY;             /* logical origin of the panel */
	uintptr_t     fPixels;          /* kernel mapping of the panel memory */
	IOMemoryMap  *fMap;
	SweetSurface *fSurface;         /* the panel memory as an IOSurface */
	IOLock       *fLock;            /* swaps and client list */
	OSArray      *fClients;
	uint8_t      *snap(const uint32_t want[6], uint32_t info[8]);   /* 0296 */
	uint32_t      fSwapID;
	bool          fConsolePaused;
	SweetFBDiagnostics fDiag;
	void reportDiagnostics(const char *reason);
	uint8_t       fPending[SWEET_FB_SWAP_REC_SIZE];     /* latest swap, not yet shown */
	sweet_fb_swap_lifecycle fSwapLifecycle;             /* surface refs held until the panel copy */
	bool          fDirty;
	uint32_t      fFlushTicks;    /* vsync ticks between panel copies */
	/* Panel presents run on their own thread so the vsync clock never waits for a copy or a transfer. */
	IOWorkLoop         *fPresentLoop;
	IOWorkLoop         *fTrackLoop;
	IOTimerEventSource *fTrackTimer;
	uint64_t            fLastPresentUs;
	bool                fTrackArmed;
	uint64_t            fTrackDeadlineUs;
	IOTimerEventSource *fPresentTimer;
	uint8_t            *fWorkRec;            /* the swap being presented */
	sweet_fb_pending_surfaces fWorkSurfaces;
	uint8_t             fPState;             /* 0 idle, 1 copy pending, 2 panel transfer in flight, 3 scan probe transfer, 4 scan probe hold, 5 waiting for the GPU, 6 waiting for the panel TE */
	uint32_t            fBusyTicks;
	void               *fBusyLastObj;        /* 0148: newest-swap surface at the previous skip, skips in the current run, diagnostic log count */
	uint32_t            fBusyRun, fBusyLogN;
	/* 0152 (diagnostic): swap arrival gaps (CA's delivery rate), arrival -> start of the panel copy latency, one-shot register dump */
	uint64_t            fSwapLastUs, fSwapArrUs;
	uint32_t            fArrBin[5], fLatBin[5];
	bool                fRegsDumped;
	/* 0154: panel link rate. The bootloader leaves MIPI TX0 at 600 Mbps/lane (PCW 92.31, POSDIV 2); the transfer (~11 ms) is link bound
	 * (ansuz: 6.9 ms at 934 Mbps on the same DSI IP).  Raised to 900 Mbps (= the Android 90 Hz mode's 1.5x) once, with the D-PHY timing counts scaled. */
	volatile uint32_t  *fTx;
	uint32_t            fPanelHz, fVsyncUs;       /* 0155: 90 (default) or 60 */
	uint64_t            fWaitStartUs, fTeStartUs, fTeOffUntilUs;
	/* 0156: double-buffered panel memory (the copy of the next frame overlaps the transfer of the current one), TE prediction, cadence */
	uint64_t            fPhysB, fNextWaitUs, fPrepCopyUs, fTeLastUs, fTePeriodUs, fTeObsUs, fTeLeadUs;
	/* 0190: the vsync heartbeat follows the panel's real TE period and phase, pushes start in the tear-free window (see presentFired state 6) */
	uint64_t            fTwMinUs;                 /* shortest panel transfer seen, minus the poll granularity: the safe-window width */
	int64_t             fCtlEmaUs;                /* smoothed distance of the "ready to push" phase from its target */
	int64_t             fTickCorrUs;              /* phase correction for the next vsync tick, consumed by vsyncFired */
	int64_t             fTickCorrSumUs;
	uint32_t            fDzWaits, fSafeStarts, fPhBin[3];
	uint32_t            fOvlBits[SWEET_FB_SCAN_PIPES][32], fOvlFrames, fDsiUnd;      /* 0191: OVL INTSTA bits seen after each transfer */
	uint64_t            fHbPeriodUs;              /* the heartbeat period: a slow filter of the TE period */
	/* 0397: pacing (fPaceWin per stat window, fPaceCum since boot / kern.n2fbpacereset), 90 Hz guard, true transfer bounds */
	struct sweet_fb_pace fPaceWin, fPaceCum;
	struct sweet_fb_p90  fP90;
	uint64_t            fXferWaitAbs;             /* last poll that saw the transfer still running (xferStart: its start) */
	uint64_t            fWorkArrUs, fNextArrUs;   /* arrival of the swap being presented / prepared */
	bool                fXferUnd;                 /* DSI underrun seen at the end of this transfer */
	uint32_t            fPanelReq;                /* kern.n2panelhz request (0 none, 60, 90), taken between two frames */
	uint32_t            fPaceResetReq;
	void        paceOnPush(void);
	void        panelTo60(void);
	void        publishRefresh(uint32_t hz);
public:
	int         paceStat(char *buf, size_t len);
	uint32_t    panelHzNow(void) const { return fModeState == 1 ? 90u : 60u; }
	void        panelRequest(uint32_t hz) { __atomic_store_n(&fPanelReq, hz, __ATOMIC_RELEASE); }
	void        paceResetRequest(void) { __atomic_store_n(&fPaceResetReq, 1u, __ATOMIC_RELEASE); }
	SweetSurface       *fSurfaceB;
	uintptr_t           fPixelsB, fCopyDst;
	uint8_t            *fNextRec;
	sweet_fb_pending_surfaces fNextSurfaces;
	bool                fDbl, fPrepared, fTeObsSet;
	/* 0393 (fbsync-20261112): frames with nothing composited keep the shown buffer; tear / source-race / long-present diagnostics */
	bool                fCopyEmpty, fNextEmpty, fWorkEmpty, fPushSafe;
	uint32_t            fSwNrId;                  /* SwapWait: id answered not-ready since fSwNrSinceMs (valve) */
	uint64_t            fSwNrSinceMs;
	struct {
		uint64_t pushes, safe_pushes, te_none, te_ambig, te_margin_min_us, te_margin_sum_us, te_margin_n;
		uint64_t src_race, busy_after, copy_mismatch, checked_layers, empty_frames, black_fill_rows;
		uint64_t copy_gt4, copy_gt8, total_gt16, long_logged, tear_logged, race_logged;
		uint64_t copy_max_us, total_max_us;
	} fSync;
	uint64_t            fPresentT0Us;             /* when the presenter took the frame (state 5/1 entry) */
	uint32_t            fLastCopyLayers;
	uint8_t             fFront, fNextState;      /* fFront: panel buffer the overlay reads (0 = fPixels, 1 = fPixelsB); fNextState 0 none, 1 taken / waiting for the GPU, 2 copied */
	uint32_t            fCad[5];
	uint8_t             fModeState;               /* 0 panel still at its boot mode, 1 switched to 90 Hz, 2 refused / failed */
	uint8_t             fRetuneState;        /* 0 not yet, 1 raised, 2 done / refused / reverted */
	uint32_t            fRetunePresents, fSaveCon0, fSaveTim[4];
	uint64_t            fNextTick, fPeriodAbs;
	uint64_t            fCopyUs, fXferStart;
	/* 0133: how many of the presented frames actually differ from the previous one, and the gaps between the different ones (the panel is refreshed at 60 Hz
	 * whatever the renderer delivers: this is the frame rate the user sees). */
	uint64_t            fHashLast, fDistinctLastUs, fDistinctGapMaxUs;
	uint32_t            fDistinct, fSame, fGapBin[5];
	bool                fHashHave;
	volatile uint32_t  *fDsi, *fMutex;       /* panel transfer registers (NORD2) */
	bool                fNbReady;            /* the blocking path has run once: overlay removed, ownership taken */
	struct sweet_fb_present_stats fStats;
	static void presentFired(OSObject *owner, IOTimerEventSource *sender);
	static void trackFired(OSObject *owner, IOTimerEventSource *sender);      /* 0191: keeps the TE reference fresh while nothing is presented */
	void        teObserve(uint64_t obsUs, bool precise);
	bool        presentCopy(const uint8_t *rec, SweetSurface *const surfaces[SWEET_FB_LAYERS]);
	void        presentDone(bool shown, bool timeout, bool legacy);
	void        dumpDisplayRegs(void);
	void        linkRetune(bool up);
	void        handOver(uint64_t count);
	bool        dsiDcs(const uint8_t *p, unsigned n);
	bool        panelRefresh(bool hz90);
	bool        dblEnable(void);
	bool        ovlSelect(uint8_t idx);
	void        prepNext(void);
	void        dropNext(void);
	void        noteDistinct(uintptr_t dst);
	bool        xferStart(void);
	bool        xferDone(void);
	/* SwapWait / swap retirement (boot-arg n2swait, sweet_fb_swapwait.h).  fRetire, fLateSurfaces: fLock. */
	uint32_t            fSwapWaitLevel;      /* 0 answer success (calls logged), 1 retire on copy/replace, 2 + retire on GPU */
	uint32_t            fSwapWaitLogged, fRetireEvent;
	struct sweet_fb_retire fRetire;
	sweet_fb_pending_surfaces fLateSurfaces[SWEET_FB_RETIRE_LATE_SLOTS];
	IOReturn    swapWait(const uint64_t *in, uint32_t count);
	void        reapLate(void);
	void        retireStop(void);
	/* NORD2 scan-address probe (boot-arg n2scan, off by default; sweet_fb_scan.h).  Present thread only. */
	int                 fScanState;          /* enum sweet_fb_scan_state */
	uint32_t            fScanHoldMs, fPresents;
	uint64_t            fFirstPresentMs, fScanXferUs;
	volatile uint32_t  *fOvl[SWEET_FB_SCAN_PIPES];
	uint32_t            fOvlPitchOld[SWEET_FB_SCAN_PIPES];
	struct sweet_fb_scan_plan fScanPlan;
	IOMemoryDescriptor *fScanMem;
	IOMemoryMap        *fScanMap;
	bool        scanBegin(IOTimerEventSource *sender);
	void        scanPoll(IOTimerEventSource *sender);
	void        scanStep(int event);
	void        scanRestoreRegs(void);
	bool          fPowerOff;      /* RequestPowerChange(0) */
	uint32_t      fFadeStep;      /* fade-out steps done while off */
	/* 0396: HW composition (sweet_fb_hw.h).  Present thread only, except hwRestore from the power-off fade (present thread idle). */
	struct sweet_fb_hw_base fHwBase[SWEET_FB_HW_PIPES];
	bool                fHwInitDone, fHwInitOk;
	bool                fHwOn;               /* the OVL*_2L registers hold a HW plan (not the CPU path's panel buffer) */
	bool                fWorkHw;             /* the frame being presented is a HW frame */
	struct sweet_fb_hw_plan fHwPlan, fHwShownPlan;
	uint32_t            fHwLastCon[SWEET_FB_HW_SLOTS];      /* 0399: CON of layer 0 / layer 1 last written */
	sweet_fb_pending_surfaces fHwHeld;       /* surfaces of the shown HW frame: held until the overlay stops reading them */
	SweetSurface       *fHwLayerS[SWEET_FB_HW_SLOTS];
	uint32_t            fHwLayerId[SWEET_FB_HW_SLOTS];
	uint64_t            fHwSeq[SWEET_FB_HW_SLOTS];
	bool                fHwIdOk[SWEET_FB_HW_SLOTS];
	unsigned            fHwN;
	uint32_t            fHwRaceRun, fHwOvlOr, fHwDsiSt;
	uint8_t             fHwTestPhase;
	bool                fHwTestReapply, fHwPatternDrawn;
	uint32_t            fHwTestStep, fHwTestEnd, fHwTestId;
	uint64_t            fHwTestT0;
	IOMemoryDescriptor *fHwTestMem;
	IOMemoryMap        *fHwTestMap;
	struct {
		uint64_t frames, cpu_frames, planned, dry_single, dry_dual, why[SWEET_FB_HW_W_COUNT_ENUM];
		uint64_t fence_skip, late_write, race, restores, faults, empty_kept, reg_us_max, reg_us_sum, tests, plans_logged;
		int last_fault;
	} fHwSt;
	bool        hwInit(void);
	unsigned    hwInputs(const uint8_t *rec, SweetSurface *const surfaces[SWEET_FB_LAYERS], struct sweet_fb_hw_in in[SWEET_FB_HW_MAX_IN],
	    SweetSurface *ss[SWEET_FB_HW_MAX_IN], uint32_t ids[SWEET_FB_HW_MAX_IN], int *why);
	bool        hwCandidate(const uint8_t *rec, SweetSurface *const surfaces[SWEET_FB_LAYERS]);
	int         hwFrameTry(uint32_t mode);
	bool        hwWriteRegs(unsigned k, const struct sweet_fb_hw_reg *r, unsigned n, uint32_t *badOff);
	bool        hwApply(const struct sweet_fb_hw_plan *p);
	void        hwRestore(const char *why);
	void        hwFault(int f, const char *where);
	/* 0397: a hardware fault turns the HW path off for a backoff (2, 4, 8, 16 s), then back on; GPU races never fault */
	uint32_t            fHwBackoffMode, fHwBackoffGen, fHwBackoffN, fHwRaceTotalRunMax;
	uint64_t            fHwBackoffUntilUs;
	void        hwBackoffPoll(void);
	/* 0397: read-only cacheable alias of the display pool for CPU-path reads of WC pool surfaces */
	IOMemoryDescriptor *fPoolAliasMem;
	IOMemoryMap        *fPoolAliasMap;
	uintptr_t           fPoolAliasVa;
	bool                fPoolAliasTried;
	uint64_t            fPoolAliasReads, fPoolAliasInvUs;
	uintptr_t   poolAlias(SweetSurface *s);
	bool        hwFenceBusy(void);
	void        hwFrameEnd(bool shown, bool timeout, bool legacy);
	void        hwTestPoll(IOTimerEventSource *sender);
	void        hwTestFinish(const char *how);
	int         hwStat(char *buf, size_t len);
	void        hwLogSurface(SweetSurface *s, uint32_t id);

	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void addClient(IOMobileFramebufferUserClient *client);
	void removeClient(IOMobileFramebufferUserClient *client);
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	void swap(const uint8_t *rec);
};

OSDefineMetaClassAndStructors(AppleCLCD, IOMobileFramebuffer);

class IOMobileFramebufferUserClient : public IOUserClient {
	OSDeclareDefaultStructors(IOMobileFramebufferUserClient);

public:
	AppleCLCD          *fFB;
	task_t              fTask;
	mach_port_t         fPort;
	io_user_reference_t fFunc[SWEET_FB_NOTIFY_TYPES];
	io_user_reference_t fRefcon[SWEET_FB_NOTIFY_TYPES];

	bool initWithTask(task_t owningTask, void *securityID, UInt32 type) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn registerNotificationPort(mach_port_t port, UInt32 type,
	    io_user_reference_t refCon) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
	void notify(uint32_t type, const uint64_t *values, uint32_t count);
};

OSDefineMetaClassAndStructors(IOMobileFramebufferUserClient, IOUserClient);

static AppleCLCD *gSweetFBSnapOwner;   /* 0296 */

#if defined(NORD2)
/* 0396: runtime control (no boot-arg: the UI command line is at its 607-character limit) */
static int
sweet_fb_hw_sysctl_mode SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int v = (int)__atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED), changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < 0 || v > (int)SWEET_FB_HW_MODE_MAX) { return EINVAL; }
	uint32_t old = __atomic_exchange_n(&sweet_fb_hw_mode, (uint32_t)v, __ATOMIC_RELAXED);
	__atomic_fetch_add(&sweet_fb_hw_mode_gen, 1u, __ATOMIC_RELAXED);      /* 0397 */
	IOLog("NORD2 FB HW: mode %u -> %d (sysctl kern.n2fbhw: 0 off, 1 dry run, 2 single layer, 3 two layers)\n", old, v);
	if (v >= (int)SWEET_FB_HW_SINGLE && __atomic_load_n(&sweet_fb_hw_selftest, __ATOMIC_RELAXED) != 0 &&
	    __atomic_exchange_n(&sweet_fb_hw_selftest_done, 1u, __ATOMIC_RELAXED) == 0) {
		uint32_t none = 0;
		(void)__atomic_compare_exchange_n(&sweet_fb_hw_test_req, &none, 6u, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
		IOLog("NORD2 FB HW: colour self-test (test 6) queued before the first HW frame of this boot (kern.n2fbhwselftest=0 skips it)\n");
	}
	return 0;
}
static int
sweet_fb_hw_sysctl_pm SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int v = (int)__atomic_load_n(&sweet_fb_hw_pm, __ATOMIC_RELAXED), changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < 0 || v > 3) { return EINVAL; }
	__atomic_store_n(&sweet_fb_hw_pm, (uint32_t)v, __ATOMIC_RELAXED);
	IOLog("NORD2 FB HW: upper-layer CON variant pm %d = %08x (sysctl kern.n2fbhwpm)\n", v, sweet_fb_hw_con_upper((uint32_t)v));
	return 0;
}
static int
sweet_fb_hw_sysctl_test SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int v = (int)__atomic_load_n(&sweet_fb_hw_test_last, __ATOMIC_RELAXED), changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < 1 || v > 6) { return EINVAL; }
	__atomic_store_n(&sweet_fb_hw_test_req, (uint32_t)v, __ATOMIC_RELEASE);
	IOLog("NORD2 FB HW: test %d requested (sysctl kern.n2fbhwtest), starts at the next idle present slot\n", v);
	return 0;
}
static int
sweet_fb_hw_sysctl_csw SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int v = (int)__atomic_load_n(&sweet_fb_hw_csw, __ATOMIC_RELAXED), changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < 0 || v > 3) { return EINVAL; }
	__atomic_store_n(&sweet_fb_hw_csw, (uint32_t)v, __ATOMIC_RELAXED);
	uint32_t pm = __atomic_load_n(&sweet_fb_hw_pm, __ATOMIC_RELAXED);
	IOLog("NORD2 FB HW: colour swap kern.n2fbhwcsw=%d: layer 0 CON %08x, layer 1 CON %08x (pm %u) from the next HW frame / test\n", v,
	    SWEET_FB_HW_CON_OPAQUE ^ ((v & 2) ? SWEET_FB_HW_CON_BYTE_SWAP : 0u), sweet_fb_hw_con_upper(pm) ^ ((v & 1) ? SWEET_FB_HW_CON_BYTE_SWAP : 0u), pm);
	return 0;
}
static int
sweet_fb_hw_sysctl_hold SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int v = (int)__atomic_load_n(&sweet_fb_hw_hold_ms, __ATOMIC_RELAXED), changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < 100 || v > 20000) { return EINVAL; }
	__atomic_store_n(&sweet_fb_hw_hold_ms, (uint32_t)v, __ATOMIC_RELAXED);
	return 0;
}
static int
sweet_fb_hw_sysctl_stat SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	char buf[768];
	buf[0] = 0;
	if (gSweetFBSnapOwner != NULL) { (void)gSweetFBSnapOwner->hwStat(buf, sizeof(buf)); }
	else { snprintf(buf, sizeof(buf), "no framebuffer"); }
	return sysctl_io_string(req, buf, sizeof(buf), 0, NULL);
}
SYSCTL_PROC(_kern, OID_AUTO, n2fbhw, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_fb_hw_sysctl_mode, "I",
    "0396 HW composition: 0 off, 1 dry run, 2 single layer (H3), 3 two layers (H4)");
SYSCTL_PROC(_kern, OID_AUTO, n2fbhwpm, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_fb_hw_sysctl_pm, "I",
    "0396/0399 upper-layer CON: 0 straight 0x21ff, 1 legacy 0x008031ff, 2 premultiplied BGRA 0x028031ff, 3 DRM-table 0x038031ff");
SYSCTL_PROC(_kern, OID_AUTO, n2fbhwtest, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_fb_hw_sysctl_test, "I",
    "0396/0399 H2 test: 1 pattern, 2/3/4 + alpha gradient with pm 0/2/3, 6 colour test (R|G|B on both layers), 5 = 1,2,3,4,6");
SYSCTL_PROC(_kern, OID_AUTO, n2fbhwcsw, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_fb_hw_sysctl_csw, "I",
    "0399 flip CON BYTE_SWAP (R<->B): bit 0 layer 1, bit 1 layer 0");
SYSCTL_UINT(_kern, OID_AUTO, n2fbhwselftest, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_fb_hw_selftest, 0,
    "0399 1: colour test 6 before the first HW frame of a boot");
SYSCTL_PROC(_kern, OID_AUTO, n2fbhwhold, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_fb_hw_sysctl_hold, "I",
    "0396 test pattern hold, ms");
SYSCTL_PROC(_kern, OID_AUTO, n2fbhwstat, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, NULL, 0, sweet_fb_hw_sysctl_stat, "A",
    "0396 HW composition counters");
/* 0397: panel refresh rate at runtime, pacing counters */
static int
sweet_fb_pace_sysctl_hz SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int v = gSweetFBSnapOwner != NULL ? (int)gSweetFBSnapOwner->panelHzNow() : 0, changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v != 60 && v != 90) { return EINVAL; }
	if (gSweetFBSnapOwner == NULL) { return ENODEV; }
	gSweetFBSnapOwner->panelRequest((uint32_t)v);
	IOLog("NORD2 FB PANEL HZ: request %d Hz (sysctl kern.n2panelhz), applied after the next present\n", v);
	return 0;
}
static int
sweet_fb_pace_sysctl_stat SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	char buf[768];
	buf[0] = 0;
	if (gSweetFBSnapOwner != NULL) { (void)gSweetFBSnapOwner->paceStat(buf, sizeof(buf)); }
	else { snprintf(buf, sizeof(buf), "no framebuffer"); }
	return sysctl_io_string(req, buf, sizeof(buf), 0, NULL);
}
static int
sweet_fb_pace_sysctl_reset SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int v = 0, changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v != 1) { return EINVAL; }
	if (gSweetFBSnapOwner == NULL) { return ENODEV; }
	gSweetFBSnapOwner->paceResetRequest();
	return 0;
}
static int
sweet_fb_pool_alias_sysctl SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int v = (int)__atomic_load_n(&sweet_fb_pool_alias_mode, __ATOMIC_RELAXED), changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < 0 || v > 1) { return EINVAL; }
	__atomic_store_n(&sweet_fb_pool_alias_mode, (uint32_t)v, __ATOMIC_RELAXED);
	IOLog("NORD2 FB POOLALIAS: %s (sysctl kern.n2fbpoolalias)\n", v ? "on" : "off");
	return 0;
}
SYSCTL_PROC(_kern, OID_AUTO, n2fbpoolalias, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_fb_pool_alias_sysctl, "I",
    "0397 CPU-path reads of write-combined pool surfaces through a read-only cacheable alias (1, default) or the WC mapping (0)");
SYSCTL_PROC(_kern, OID_AUTO, n2panelhz, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_fb_pace_sysctl_hz, "I",
    "0397 panel refresh: read the rate now, write 90 or 60 to switch");
SYSCTL_PROC(_kern, OID_AUTO, n2fbpace, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, NULL, 0, sweet_fb_pace_sysctl_stat, "A",
    "0397 present pacing since boot or kern.n2fbpacereset");
SYSCTL_PROC(_kern, OID_AUTO, n2fbpacereset, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_fb_pace_sysctl_reset, "I",
    "0397 write 1: reset the kern.n2fbpace counters (at the next present)");
#endif

bool
AppleCLCD::start(IOService *provider)
{
#if !defined(NORD2)
	PE_Video video;
#endif

	if (!IOService::start(provider)) {
		return false;
	}
	fDirty = false;
	sweet_fb_swap_lifecycle_init(&fSwapLifecycle);
#if defined(NORD2)
	if (!nord2_display_info(&fPhys, &fWidth, &fHeight, &fRowBytes)) {
		IOLog("NORD2 FB: display configuration refused\n");
		return false;
	}
#else
	PE_current_console(&video);
	fPhys = video.v_baseAddr;
	fWidth = (uint32_t)video.v_width;
	fHeight = (uint32_t)video.v_height;
	fRowBytes = (uint32_t)video.v_rowBytes;
#endif
	if (fPhys == 0 || fWidth == 0 || fHeight == 0 || fRowBytes < fWidth * 4) {
		IOLog("SWEET FB: no boot framebuffer\n");
		return false;
	}
	int native = 0;
	PE_parse_boot_argn("sweetfb_native", &native, sizeof(native));
	fLogicalW = fWidth;
	fLogicalH = fHeight;
	if (!native && fWidth <= SWEET_FB_LOGICAL_W && fHeight <= SWEET_FB_LOGICAL_H) {
		fLogicalW = SWEET_FB_LOGICAL_W;
		fLogicalH = SWEET_FB_LOGICAL_H;
	}
	fOffX = (int32_t)(fLogicalW - fWidth) / 2;
	fOffY = (int32_t)(fLogicalH - fHeight) / 2;
	fLock = IOLockAlloc();
	fClients = OSArray::withCapacity(4);
	fSurface = sweet_surface_create_physical(fPhys, fWidth, fHeight, fRowBytes, SWEET_FB_FORMAT_BGRA);
	fPixels = fSurface != NULL ? fSurface->kernelAddress() : 0;
	fWorkLoop = IOWorkLoop::workLoop();
	if (fLock == NULL || fClients == NULL || fPixels == 0 || fWorkLoop == NULL) {
		IOLog("SWEET FB: setup failed\n");
		OSSafeReleaseNULL(fSurface);    /* stays published: also held by the table */
		OSSafeReleaseNULL(fWorkLoop);
		return false;
	}
	fVsync = IOTimerEventSource::timerEventSource(this, &AppleCLCD::vsyncFired);
	if (fVsync == NULL || fWorkLoop->addEventSource(fVsync) != kIOReturnSuccess) {
		IOLog("SWEET FB: no vsync timer\n");
		return false;
	}
	bzero(&fDiag, sizeof(fDiag));
#if defined(NORD2)
	uint32_t diagnostics = 0;
	PE_parse_boot_argn("nord2fbdiag", &diagnostics, sizeof(diagnostics));
	fDiag.enabled = (diagnostics == 1);
#endif
	{ uint32_t bl = 1; if (PE_parse_boot_argn("n2fbblend", &bl, sizeof(bl))) { sweet_fb_blend_mode = bl ? 1 : 0; } }   /* 0390 */
	{ uint32_t iv = 1; if (PE_parse_boot_argn("n2fbinval", &iv, sizeof(iv))) { sweet_fb_inval_mode = iv ? 1 : 0; } }   /* 0391 */
	{ uint32_t rt = 1; if (PE_parse_boot_argn("n2fbretain", &rt, sizeof(rt))) { sweet_fb_retain_mode = rt ? 1 : 0; } }   /* 0392 */
	IOLog("SWEET FB: layers outside a swap's mask kept %s (0392)\n", sweet_fb_retain_mode ? "on" : "off (n2fbretain=0)");
	IOLog("SWEET FB: multi-layer source cache clean+invalidate %s (0391)\n", sweet_fb_inval_mode ? "on" : "off (n2fbinval=0)");
	IOLog("SWEET FB: layer composition %s (0390)\n", sweet_fb_blend_mode ? "on" : "off (n2fbblend=0)");
	sweet_fb_pending_surfaces_init(&fHwHeld);       /* 0396 */
	bzero(&fHwSt, sizeof(fHwSt)); fHwInitDone = fHwInitOk = fHwOn = fWorkHw = false; fHwN = 0; fHwTestMem = NULL; fHwTestMap = NULL;
	IOLog("NORD2 FB HW: overlay composition (0396) mode %u at boot (sysctl kern.n2fbhw 0 off / 1 dry / 2 single / 3 two layers; kern.n2fbhwtest 1..6, kern.n2fbhwcsw)\n",
	    sweet_fb_hw_mode);
	uint32_t hz = SWEET_FB_DEFAULT_HZ;
	/* sweetfb_hz=<n> (0/60+ = every vsync); short alias n2fbhz for the full UI command line (600/607 characters). */
	if (!PE_parse_boot_argn("sweetfb_hz", &hz, sizeof(hz))) { (void)PE_parse_boot_argn("n2fbhz", &hz, sizeof(hz)); }
	fFlushTicks = sweet_fb_flush_ticks(hz);
	fPanelHz = SWEET_FB_PANEL_HZ_DEFAULT;
	{
		uint32_t phz = 0;
		if (PE_parse_boot_argn("n2panelhz", &phz, sizeof(phz)) && (phz == 60 || phz == 90)) { fPanelHz = phz; }
		if (fFlushTicks > 1) { fPanelHz = 60; }      /* a reduced present rate keeps the 60 Hz tick grid */
	}
	fVsyncUs = fPanelHz == 90 ? 11111 : SWEET_FB_VSYNC_US;
	fTePeriodUs = SWEET_FB_VSYNC_US;      /* the panel boots at 60 Hz; set to 11111 when the 90 Hz switch is accepted */
	fTeLeadUs = SWEET_FB_TE_LEAD_US_DEFAULT;
	fTwMinUs = 6800; fCtlEmaUs = 0; fTickCorrUs = 0; fTickCorrSumUs = 0; fDzWaits = 0; fSafeStarts = 0; fPhBin[0] = fPhBin[1] = fPhBin[2] = 0; fHbPeriodUs = 0;
	sweet_fb_pace_reset(&fPaceWin); sweet_fb_pace_reset(&fPaceCum); bzero(&fP90, sizeof(fP90));      /* 0397 */
	fXferWaitAbs = 0; fWorkArrUs = fNextArrUs = 0; fXferUnd = false; fPanelReq = 0; fPaceResetReq = 0;
	fHwBackoffMode = fHwBackoffGen = fHwBackoffN = fHwRaceTotalRunMax = 0; fHwBackoffUntilUs = 0;
	fPoolAliasMem = NULL; fPoolAliasMap = NULL; fPoolAliasVa = 0; fPoolAliasTried = false; fPoolAliasReads = 0; fPoolAliasInvUs = 0;
	IOLog("NORD2 FB PACE: 0397 pacing counters on (sysctl kern.n2fbpace, kern.n2fbpacereset=1, kern.n2panelhz 90/60); 90 Hz guard: "
	    "timeout or %u faults (underrun / transfer > %u us at an on-time poll), %u retries after %u presents\n",
	    SWEET_FB_P90_FAULTS, SWEET_FB_P90_SLOW_US, SWEET_FB_P90_RETRIES, SWEET_FB_P90_RETRY_PRESENTS);
	{ uint32_t lead = 0; if (PE_parse_boot_argn("n2telead", &lead, sizeof(lead)) && lead <= 4000) { fTeLeadUs = lead; } }
	nanoseconds_to_absolutetime(fVsyncUs * 1000ULL, &fPeriodAbs);
	/* n2swait=<0|1|2>: SwapWait semantics (0 = always success, calls logged).  0393: default 1 = a swap retires when its copy ended
	 * (or it was replaced unseen); CoreAnimation (can_update_, mode 0 no-block) then never redraws a surface the copy has not read. */
	{
		uint32_t level = 1;
		if (!PE_parse_boot_argn("sweetfb_swapwait", &level, sizeof(level))) { (void)PE_parse_boot_argn("n2swait", &level, sizeof(level)); }
		fSwapWaitLevel = level > 2 ? 2 : level;
		bzero(&fRetire, sizeof(fRetire));
		bzero(&fSync, sizeof(fSync)); fSwNrId = 0; fSwNrSinceMs = 0; fPresentT0Us = 0; fLastCopyLayers = 0;      /* 0393 */
		fCopyEmpty = fNextEmpty = fWorkEmpty = fPushSafe = false;
		for (unsigned i = 0; i < SWEET_FB_RETIRE_LATE_SLOTS; i++) { sweet_fb_pending_surfaces_init(&fLateSurfaces[i]); }
	}
#if defined(NORD2)
	/* CoreAnimation follows a timer, not our vsync notifications: tell it the rate the panel really shows (reference 0123).
	 * Published before registerService() so backboardd finds it when it opens the display. */
	{
		uint32_t interval = fPanelHz == 90 ? (uint32_t)(((1ULL << 32) + 45) / 90) : sweet_fb_refresh_interval(fFlushTicks);
		if (interval != 0) {
			OSDictionary *refresh = OSDictionary::withCapacity(1);
			OSNumber *num = OSNumber::withNumber((unsigned long long)interval, 32);
			if (refresh != NULL && num != NULL) {
				refresh->setObject("displayMinRefreshInterval", num);
				setProperty("IOMFBDisplayRefresh", refresh);
				IOLog("NORD2 FB: IOMFBDisplayRefresh.displayMinRefreshInterval %u (1/%u s): CoreAnimation follows the %u Hz panel rate\n",
				    interval, fPanelHz == 90 ? 90 : 60 / fFlushTicks, fPanelHz == 90 ? 90 : 60 / fFlushTicks);
			}
			OSSafeReleaseNULL(num);
			OSSafeReleaseNULL(refresh);
		}
	}
#endif
	fPresentLoop = IOWorkLoop::workLoop();
	fPresentTimer = fPresentLoop != NULL ? IOTimerEventSource::timerEventSource(this, &AppleCLCD::presentFired) : NULL;
	if (fPresentTimer == NULL || fPresentLoop->addEventSource(fPresentTimer) != kIOReturnSuccess) {
		IOLog("SWEET FB: no present thread\n");
		return false;
	}
#if defined(NORD2)
	fTrackLoop = IOWorkLoop::workLoop();
	fTrackTimer = fTrackLoop != NULL ? IOTimerEventSource::timerEventSource(this, &AppleCLCD::trackFired) : NULL;
	if (fTrackTimer != NULL && fTrackLoop->addEventSource(fTrackTimer) != kIOReturnSuccess) { OSSafeReleaseNULL(fTrackTimer); }
	fDsi = (volatile uint32_t *)ml_io_map(SWEET_FB_DSI0, 0x1000);
	fMutex = (volatile uint32_t *)ml_io_map(SWEET_FB_DISP_MUTEX, 0x1000);
#endif
#if defined(NORD2)
	{
		uint32_t scan = 0;
		if (PE_parse_boot_argn("n2scan", &scan, sizeof(scan)) && sweet_fb_scan_hold_ms(scan) != 0) {
			fScanHoldMs = sweet_fb_scan_hold_ms(scan);
			fOvl[0] = (volatile uint32_t *)ml_io_map(SWEET_FB_OVL0_2L, 0x1000);
			fOvl[1] = (volatile uint32_t *)ml_io_map(SWEET_FB_OVL1_2L, 0x1000);
			if (fOvl[0] != NULL && fOvl[1] != NULL &&
			    sweet_fb_scan_plan_make(&fScanPlan, fPhys, fRowBytes, fWidth, fHeight, SWEET_FB_SCAN_PROBE_PITCH)) {
				fScanState = SWEET_FB_SCAN_ARMED;
				IOLog("NORD2 FB SCAN: probe armed, slot 0x%llx pitch %u hold %u ms (shows a test pattern once, then restores)\n",
				    fScanPlan.slot_pa, fScanPlan.pitch, fScanHoldMs);
			} else {
				IOLog("NORD2 FB SCAN: probe refused (overlay mapping or slot outside the carve-out)\n");
			}
		}
	}
#endif
	sweet_fb_stats_reset(&fStats, 0);
	fVsync->setTimeoutUS(fVsyncUs);
	if (fTrackTimer != NULL) { fTrackTimer->setTimeoutMS(2000); }
	IOLog("SWEET FB: panel present rate %u Hz (every %u vsyncs), present thread, %s transfer, SwapWait level %u\n", hz, fFlushTicks,
	    (fDsi != NULL && fMutex != NULL) ? "polled" : "blocking", fSwapWaitLevel);

	setProperty(kIOUserClientClassKey, "IOMobileFramebufferUserClient");
	setProperty("external", false);
	/*
	 * 0289: QuartzCore and VideoToolbox (iOS 16.7) read these two as dictionaries, DisplayPipePlaneBaseAlignment =
	 * { PlaneBaseAlignmentLinear = 64 } and DisplayPipeStrideRequirements = { StrideLinearHorizontal = 64 }; the bare
	 * number 64 published before is not what they look for.
	 */
	{
		static const char *const pipe_keys[2][2] = {
			{ "DisplayPipePlaneBaseAlignment", "PlaneBaseAlignmentLinear" },
			{ "DisplayPipeStrideRequirements", "StrideLinearHorizontal" },
		};
		for (unsigned i = 0; i < 2; i++) {
			OSDictionary *d = OSDictionary::withCapacity(1);
			OSNumber *n = OSNumber::withNumber(64ULL, 32);
			if (d != NULL && n != NULL) {
				d->setObject(pipe_keys[i][1], n);
				setProperty(pipe_keys[i][0], d);
			}
			OSSafeReleaseNULL(n);
			OSSafeReleaseNULL(d);
		}
	}
	registerService();
	IOLog("SWEET FB: AppleCLCD %ux%u stride %u at 0x%llx, default surface %u, presented as %ux%u\n",
	    fWidth, fHeight, fRowBytes, fPhys, fSurface->id, fLogicalW, fLogicalH);
	gSweetFBSnapOwner = this;   /* 0296 */
	return true;
}

/*
 * 0296: a picture of what the panel shows, for bsd/dev/nord2_autotest.inc (thread context only, one caller). want: scale (every
 * scale-th pixel of every scale-th row), packed, x, y, width, height of the part wanted in panel pixels (width 0: all). Read from
 * the buffer the overlay reads into one buffer that is allocated once and kept: R,G,B per pixel, or packed as runs (a byte c,
 * then c < 128: c + 1 pixels as they are; c >= 128: one pixel shown c - 126 times).
 * info: width, height, bytes, scale | packed << 4, front buffer before, after, presents during the copy, power off. A present
 * that starts while the rows are read can land in them (two presents: the picture may be torn; the caller sees the count).
 */
uint8_t *
AppleCLCD::snap(const uint32_t want[6], uint32_t info[8])
{
	static uint8_t *buf;
	uint32_t scale = want[0], packed = want[1] ? 1 : 0;
	uint32_t x0 = want[2], y0 = want[3], pw = want[4], ph = want[5];
	if (scale < 1 || scale > 8) { scale = 2; }
	if (pw == 0 || ph == 0 || x0 >= fWidth || y0 >= fHeight) { x0 = y0 = 0; pw = fWidth; ph = fHeight; }
	if (pw > fWidth - x0) { pw = fWidth - x0; }
	if (ph > fHeight - y0) { ph = fHeight - y0; }
	if (buf == NULL) { buf = (uint8_t *)IOMallocData((vm_size_t)fWidth * fHeight * 3 + (vm_size_t)fWidth * fHeight / 64 + 64); }
	if (buf == NULL || fPixels == 0) { return NULL; }
	uint32_t w = pw / scale, h = ph / scale;
	uint8_t front = fFront;
	uint32_t presents = fPresents;
	uintptr_t src = (fDbl && front && fPixelsB != 0) ? fPixelsB : fPixels;
	uint8_t *d = buf, *lit = NULL;
	uint32_t prev = 0, run = 0;
	for (uint32_t y = 0; y < h; y++) {
		const uint32_t *row = (const uint32_t *)(src + (uint64_t)(y0 + y * scale) * fRowBytes) + x0;
		for (uint32_t x = 0; x <= w; x++) {
			uint32_t p = 0;
			if (x < w) {
				p = row[x * scale] & 0xffffffu;
				if (!packed) { *d++ = (uint8_t)(p >> 16); *d++ = (uint8_t)(p >> 8); *d++ = (uint8_t)p; continue; }
				if (run != 0 && p == prev && run < 129) { run++; continue; }
			} else if (!packed || y + 1 < h) {
				continue;                     /* runs go on over the row ends; only the last pixel of all ends the last run */
			}
			if (run >= 2) {
				*d++ = (uint8_t)(126 + run);
				lit = NULL;
			} else if (run == 1) {
				if (lit == NULL || *lit == 127) { lit = d++; *lit = 0; } else { (*lit)++; }
			}
			if (run != 0) { *d++ = (uint8_t)(prev >> 16); *d++ = (uint8_t)(prev >> 8); *d++ = (uint8_t)prev; }
			prev = p; run = 1;
		}
	}
	info[0] = w; info[1] = h; info[2] = (uint32_t)(d - buf); info[3] = scale | packed << 4;
	info[4] = front; info[5] = fFront; info[6] = fPresents - presents; info[7] = fPowerOff ? 1 : 0;
	return buf;
}

extern "C" void *sweet_fb_snap(const uint32_t want[6], uint32_t info[8]);
extern "C" void *
sweet_fb_snap(const uint32_t want[6], uint32_t info[8])
{
	return gSweetFBSnapOwner != NULL ? gSweetFBSnapOwner->snap(want, info) : NULL;
}

/* All counters are relaxed observations; report only after releasing fLock.
 * Finite boot checkpoints and four ownership failures bound console overhead. */
void
AppleCLCD::reportDiagnostics(const char *reason)
{
#define FB_DIAG_LOAD(field) __atomic_load_n(&fDiag.field, __ATOMIC_RELAXED)
	IOLockLock(fLock);
	bool dirty = fDirty, off = fPowerOff, paused = fConsolePaused;
	IOLockUnlock(fLock);
	IOLog("NORD2 FB DIAG: %s dirty=%u poweroff=%u console_paused=%u ticks=%llu vsync=%llu swaps=%llu off_ticks=%llu flushes=%llu alloc_failed=%llu presents=%llu begin_failed=%llu empty=%llu active_layers=%llu missing=%llu copied=%llu non_bgra=%llu\n",
	    reason, dirty, off, paused, mach_absolute_time(), FB_DIAG_LOAD(vsync), FB_DIAG_LOAD(swaps),
	    FB_DIAG_LOAD(off), FB_DIAG_LOAD(flushes), FB_DIAG_LOAD(alloc_failed),
	    FB_DIAG_LOAD(presents), FB_DIAG_LOAD(begin_failed), FB_DIAG_LOAD(empty_records),
	    FB_DIAG_LOAD(active_layers), FB_DIAG_LOAD(missing_surfaces),
	    FB_DIAG_LOAD(copied_layers), FB_DIAG_LOAD(non_bgra));
#undef FB_DIAG_LOAD
}

static uint64_t
sweet_fb_abs_to_us(uint64_t abs)
{
	uint64_t ns = 0;
	absolutetime_to_nanoseconds(abs, &ns);
	return ns / 1000ULL;
}

void
AppleCLCD::vsyncFired(OSObject *owner, IOTimerEventSource *sender)
{
	AppleCLCD *fb = (AppleCLCD *)owner;
	uint64_t now = mach_absolute_time();
	/* Re-arm first, on the absolute grid: no copy, transfer or notification below can stretch the vsync clock. */
	if (sweet_fb_next_tick(&fb->fNextTick, now, fb->fPeriodAbs)) { __atomic_fetch_add(&fb->fStats.late_ticks, 1, __ATOMIC_RELAXED); }
	{   /* 0190: phase correction from the presenter (a few tens of us per tick at most): the next tick moves, and so does every later one */
		int64_t corr = __atomic_exchange_n(&fb->fTickCorrUs, 0, __ATOMIC_RELAXED);
		if (corr > 120) { corr = 120; } else if (corr < -120) { corr = -120; }
		if (corr != 0) {
			uint64_t cabs = 0;
			nanoseconds_to_absolutetime((uint64_t)(corr < 0 ? -corr : corr) * 1000ULL, &cabs);
			fb->fNextTick = corr < 0 ? fb->fNextTick - cabs : fb->fNextTick + cabs;
			fb->fTickCorrSumUs += corr;
		}
	}
	if (!__atomic_load_n(&fb->fSwapLifecycle.stopped, __ATOMIC_ACQUIRE)) { sender->wakeAtTime(fb->fNextTick); }
	uint64_t count = ++fb->fVsyncCount;
	bool diag_report = false;
	if (fb->fDiag.enabled) {
		uint64_t ticks = __atomic_add_fetch(&fb->fDiag.vsync, 1, __ATOMIC_RELAXED);
		diag_report = sweet_fb_diag_checkpoint(ticks);
	}
	uint64_t values[6] = {
		(uint32_t)count, (uint32_t)(count >> 32),
		(uint32_t)now, (uint32_t)(now >> 32),
		0, 0,
	};

	IOLockLock(fb->fLock);
	for (unsigned int i = 0; i < fb->fClients->getCount(); i++) {
		IOMobileFramebufferUserClient *client =
		    (IOMobileFramebufferUserClient *)fb->fClients->getObject(i);
		client->notify(SWEET_FB_NOTIFY_VSYNC, values, 6);
	}
	if (fb->fPowerOff) {
		if (fb->fDiag.enabled) { __atomic_fetch_add(&fb->fDiag.off, 1, __ATOMIC_RELAXED); }
		/* Fade out: halve every channel each vsync, then black, then idle. */
		uint32_t step = fb->fFadeStep;
		if (step <= 6) {
			fb->fFadeStep = step + 1;
		}
		IOLockUnlock(fb->fLock);
#if defined(NORD2)
		if (fb->fHwOn && fb->fPState == 0) {
			/* 0396: the overlay reads a HW frame's surfaces; panel buffer 0 holds an older CPU frame: back to the CPU path, straight to black */
			fb->hwRestore("power off");
			if (step < 6) { step = 6; fb->fFadeStep = 7; }
		}
		if (fb->fDbl && fb->fFront != 0 && fb->fPState == 0) { (void)fb->ovlSelect(0); fb->fFront = 0; }      /* the fade draws into buffer 0 */
		fb->dropNext();
#endif
		if (step <= 6) {
			for (uint32_t y = 0; y < fb->fHeight; y++) {
				volatile uint32_t *row = (volatile uint32_t *)(fb->fPixels + (uint64_t)y * fb->fRowBytes);
				for (uint32_t x = 0; x < fb->fWidth; x++) {
					row[x] = step < 6 ? (row[x] >> 1) & 0x7f7f7f7f : 0;
				}
			}
		}
#if defined(NORD2)
		if (step <= 6) { (void)nord2_display_refresh(); }
#endif
		if (diag_report) { fb->reportDiagnostics("timer-off"); }
		return;
	}
	IOLockUnlock(fb->fLock);
	/* 0155: the tick is only the fallback; swaps and a finished present hand over at once (handOver() also takes the GPU wait and the TE wait off this thread). */
	if (sweet_fb_flush_due(count, fb->fFlushTicks)) { fb->handOver(count); }
	fb->reapLate();
	if (diag_report) { fb->reportDiagnostics("timer-on"); }
}

/* 0156: FNV hash of the panel memory just filled -> "distinct frames" and their cadence. */
void
AppleCLCD::noteDistinct(uintptr_t dst)
{
	uint64_t h = 1469598103934665603ULL;
	for (uint32_t y = 0; y < fHeight; y += 8) {
		const volatile uint64_t *row = (const volatile uint64_t *)(dst + (uint64_t)y * fRowBytes);
		uint32_t words = fWidth / 2;
		for (uint32_t k = 0; k < 16; k++) { h = (h ^ row[(words / 16) * k]) * 1099511628211ULL; }
	}
	uint64_t nowUs = sweet_fb_abs_to_us(mach_absolute_time());
	if (fHashHave && h == fHashLast) { fSame++; }
	else {
		if (fHashHave && fDistinctLastUs) {
			uint64_t gap = nowUs - fDistinctLastUs;
			if (gap > fDistinctGapMaxUs) { fDistinctGapMaxUs = gap; }
			fGapBin[gap <= 20000 ? 0 : gap <= 34000 ? 1 : gap <= 67000 ? 2 : gap <= 100000 ? 3 : 4]++;
			fCad[gap <= 12500 ? 0 : gap <= 15000 ? 1 : gap <= 19000 ? 2 : gap <= 28000 ? 3 : 4]++;
		}
		fDistinct++; fDistinctLastUs = nowUs;
	}
	fHashLast = h; fHashHave = true;
}

/* 0156: drop a taken-but-unpresented frame (power off, stop). */
void
AppleCLCD::dropNext(void)
{
	IOLockLock(fLock);
	uint8_t *rec = fNextRec;
	sweet_fb_pending_surfaces s = fNextSurfaces;
	fNextRec = NULL; sweet_fb_pending_surfaces_init(&fNextSurfaces);
	bool had = fNextState != 0;
	fNextState = 0;
	bool requeued = false;
	if (had && rec != NULL) { requeued = sweet_fb_swap_lifecycle_requeue(&fSwapLifecycle, fDirty, &s); if (requeued) { fDirty = true; } }
	if (had) {      /* 0393 */
		uint32_t pid = 0;
		if (rec != NULL) { memcpy(&pid, rec + 0x50, 4); }
		sweet_fb_retire_prep_drop(&fRetire, requeued, pid);
	}
	IOLockUnlock(fLock);
	IOLockWakeup(fLock, &fRetireEvent, false);
	if (rec != NULL) { IOFreeData(rec, SWEET_FB_SWAP_REC_SIZE); }
	sweet_fb_pending_surfaces_release(&s, sweet_fb_release_surface);
}

/* 0156: the overlay reads panel buffer idx (both pipes), read back to prove the registers took it. Only between frames. */
bool
AppleCLCD::ovlSelect(uint8_t idx)
{
#if defined(NORD2)
	if (fOvl[0] == NULL || fOvl[1] == NULL) { return false; }
	uint64_t base = idx ? fPhysB : fPhys;
	for (uint32_t i = 0; i < SWEET_FB_SCAN_PIPES; i++) { fOvl[i][SWEET_FB_OVL_ADDR / 4] = sweet_fb_scan_base_addr(base, fWidth, i); }
	__asm__ volatile("dsb sy" ::: "memory");
	for (uint32_t i = 0; i < SWEET_FB_SCAN_PIPES; i++) { if (fOvl[i][SWEET_FB_OVL_ADDR / 4] != sweet_fb_scan_base_addr(base, fWidth, i)) { return false; } }
	return true;
#else
	(void)idx;
	return false;
#endif
}

/* 0156: second panel buffer right behind the first one in the framebuffer carve-out; refuses unless the overlay is exactly as the bootloader left it. */
bool
AppleCLCD::dblEnable(void)
{
#if defined(NORD2)
	const char *why = NULL;
	if (fDsi == NULL) { why = "no DSI"; }
	if (why == NULL && fOvl[0] == NULL) { fOvl[0] = (volatile uint32_t *)ml_io_map(SWEET_FB_OVL0_2L, 0x1000); }
	if (why == NULL && fOvl[1] == NULL) { fOvl[1] = (volatile uint32_t *)ml_io_map(SWEET_FB_OVL1_2L, 0x1000); }
	if (why == NULL && (fOvl[0] == NULL || fOvl[1] == NULL)) { why = "overlay not mappable"; }
	uint32_t want_size = (fHeight << 16) | (fWidth / SWEET_FB_SCAN_PIPES);
	for (uint32_t i = 0; why == NULL && i < SWEET_FB_SCAN_PIPES; i++) {
		volatile uint32_t *o = fOvl[i];
		if (o[SWEET_FB_OVL_ADDR / 4] != sweet_fb_scan_base_addr(fPhys, fWidth, i)) { why = "ADDR not the panel buffer"; }
		else if ((o[SWEET_FB_OVL_PITCH / 4] & 0xffffu) != fRowBytes) { why = "PITCH not the panel pitch"; }
		else if (o[SWEET_FB_OVL_SIZE / 4] != want_size) { why = "SIZE not the half panel"; }
	}
	if (why == NULL) {
		fPhysB = fPhys + (uint64_t)fRowBytes * fHeight;
		if (fPhysB + (uint64_t)fRowBytes * fHeight > SWEET_FB_SCAN_CARVE_END) { why = "no room in the carve-out"; }
	}
	if (why == NULL) {
		fSurfaceB = sweet_surface_create_physical(fPhysB, fWidth, fHeight, fRowBytes, SWEET_FB_FORMAT_BGRA);
		fPixelsB = fSurfaceB != NULL ? fSurfaceB->kernelAddress() : 0;
		if (fPixelsB == 0) { why = "second buffer not mappable"; }
	}
	if (why != NULL) {
		IOLog("NORD2 FB DBL: double buffering refused: %s (addr %08x/%08x pitch %08x/%08x size %08x/%08x)\n", why,
		    fOvl[0] ? fOvl[0][SWEET_FB_OVL_ADDR / 4] : 0, fOvl[1] ? fOvl[1][SWEET_FB_OVL_ADDR / 4] : 0,
		    fOvl[0] ? fOvl[0][SWEET_FB_OVL_PITCH / 4] : 0, fOvl[1] ? fOvl[1][SWEET_FB_OVL_PITCH / 4] : 0,
		    fOvl[0] ? fOvl[0][SWEET_FB_OVL_SIZE / 4] : 0, fOvl[1] ? fOvl[1][SWEET_FB_OVL_SIZE / 4] : 0);
		return false;
	}
	memcpy((void *)fPixelsB, (const void *)fPixels, (size_t)fRowBytes * fHeight);
	__asm__ volatile("dsb sy" ::: "memory");
	fFront = 0; fDbl = true;
	IOLog("NORD2 FB DBL: double buffering on, second buffer at 0x%llx\n", fPhysB);
	return true;
#else
	return false;
#endif
}

/* 0156: while a frame is being transferred, take the next swap (if any), wait for the GPU without blocking, and copy it into the idle buffer. */
void
AppleCLCD::prepNext(void)
{
#if defined(NORD2)
	if (!fDbl) { return; }
	/* 0396: in the HW modes the next swap is not copied ahead: it is planned (and, if eligible, shown without any copy) when it is presented */
	if (fNextState == 0 && __atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED) >= SWEET_FB_HW_SINGLE) { return; }
	if (fNextState == 0) {
		IOLockLock(fLock);
		if (fPowerOff || !fDirty || fNextState != 0) { IOLockUnlock(fLock); return; }
		uint8_t *rec = (uint8_t *)IOMallocData(SWEET_FB_SWAP_REC_SIZE);
		if (rec == NULL) { IOLockUnlock(fLock); return; }
		memcpy(rec, fPending, SWEET_FB_SWAP_REC_SIZE);
		sweet_fb_pending_surfaces s;
		sweet_fb_pending_surfaces_init(&s);
		if (!sweet_fb_swap_lifecycle_take(&fSwapLifecycle, &s)) { IOLockUnlock(fLock); IOFreeData(rec, SWEET_FB_SWAP_REC_SIZE); return; }
		fDirty = false;
		sweet_fb_retire_prep(&fRetire);      /* 0393: the retire state knows the prepare slot took this swap */
		fNextRec = rec; fNextSurfaces = s; fNextState = 1; fNextArrUs = fSwapArrUs;      /* 0397 */
		fNextWaitUs = sweet_fb_abs_to_us(mach_absolute_time());
		IOLockUnlock(fLock);
	}
	if (fNextState == 1) {
		bool busy = false;
		for (uint32_t layer = 0; layer < SWEET_FB_LAYERS && !busy; layer++) {
			if (fNextSurfaces.object[layer] != NULL && nord2_gpu_surface_busy(fNextSurfaces.object[layer])) { busy = true; }
		}
		uint64_t waited = sweet_fb_abs_to_us(mach_absolute_time()) - fNextWaitUs;
		if (busy && waited < SWEET_FB_BUSY_WAIT_US) { return; }
		fStats.busy_waits++; fStats.busy_wait_us += waited;
		if (!busy) { fStats.busy_wait_ok++; } else { __atomic_fetch_add(&fStats.busy_forced, 1, __ATOMIC_RELAXED); }
		fCopyDst = fFront ? fPixels : fPixelsB;       /* the buffer the overlay is not reading */
		uint64_t t0 = mach_absolute_time();
		bool shown = presentCopy(fNextRec, (SweetSurface *const *)fNextSurfaces.object);
		fPrepCopyUs = sweet_fb_abs_to_us(mach_absolute_time() - t0);
		fCopyDst = 0;
		if (!shown) { dropNext(); return; }
		fNextEmpty = fCopyEmpty;
		IOLockLock(fLock);
		sweet_fb_retire_prep_end(&fRetire, false);      /* 0393: copied: its surfaces are free for CoreAnimation */
		IOLockUnlock(fLock);
		IOLockWakeup(fLock, &fRetireEvent, false);
		noteDistinct(fFront ? fPixels : fPixelsB);
		fStats.dbl_preps++;
		fNextState = 2;
	}
#endif
}

/* Takes the newest swap if the presenter is idle and starts it on the present thread (state 5: wait for the GPU, then copy, TE wait, transfer). */
void
AppleCLCD::handOver(uint64_t count)
{
	(void)count;
	if (fPresentTimer == NULL) { return; }
	IOLockLock(fLock);
#if defined(NORD2)
	/* 0396: an H2 test pattern requested by sysctl runs on the present thread while it is idle (swaps wait meanwhile; SwapWait valve) */
	if (!fPowerOff && fPState == 0 && fNextState == 0 && fNbReady && __atomic_load_n(&sweet_fb_hw_test_req, __ATOMIC_ACQUIRE) != 0) {
		uint32_t t = __atomic_exchange_n(&sweet_fb_hw_test_req, 0, __ATOMIC_ACQ_REL);
		if (t >= 1 && t <= 6) {
			fHwTestId = t; fHwTestStep = t == 5 ? 1 : t; fHwTestEnd = t == 5 ? 6 : t; fHwTestPhase = 0; fHwTestReapply = fHwOn;
			fHwTestT0 = sweet_fb_abs_to_us(mach_absolute_time());
			fPState = 7;
			IOLockUnlock(fLock);
			fPresentTimer->setTimeoutUS(1);
			return;
		}
	}
#endif
	if (!fPowerOff && fPState == 0 && fNextState != 0) {
		/* 0156: the frame prepared during the last transfer goes first */
		fWorkRec = fNextRec; fWorkSurfaces = fNextSurfaces; fWorkArrUs = fNextArrUs;      /* 0397 */
		fNextRec = NULL; sweet_fb_pending_surfaces_init(&fNextSurfaces);
		/* 0393: was sweet_fb_retire_take(), which marked the NEWER pending swap as presenting */
		fPresentT0Us = fNextWaitUs;
		if (fNextState == 2) { fPrepared = true; fWorkEmpty = fNextEmpty; fPState = 1; fStats.dbl_hits++; sweet_fb_retire_prep_end(&fRetire, false); }
		else { fWaitStartUs = fNextWaitUs; fPState = 5; sweet_fb_retire_prep_end(&fRetire, true); }
		fNextState = 0;
		IOLockUnlock(fLock);
		IOLockWakeup(fLock, &fRetireEvent, false);
		fPresentTimer->setTimeoutUS(1);
		return;
	}
	if (fPowerOff || fPState != 0 || !fDirty) { IOLockUnlock(fLock); return; }
	if (fDiag.enabled) { __atomic_fetch_add(&fDiag.flushes, 1, __ATOMIC_RELAXED); }
	uint8_t *rec = (uint8_t *)IOMallocData(SWEET_FB_SWAP_REC_SIZE);
	if (rec == NULL) {
		if (fDiag.enabled) { __atomic_fetch_add(&fDiag.alloc_failed, 1, __ATOMIC_RELAXED); }
		IOLockUnlock(fLock);
		return;
	}
	memcpy(rec, fPending, SWEET_FB_SWAP_REC_SIZE);
	sweet_fb_pending_surfaces surfaces;
	sweet_fb_pending_surfaces_init(&surfaces);
	if (!sweet_fb_swap_lifecycle_take(&fSwapLifecycle, &surfaces)) {
		IOLockUnlock(fLock);
		IOFreeData(rec, SWEET_FB_SWAP_REC_SIZE);
		return;
	}
	fDirty = false;
	sweet_fb_retire_take(&fRetire);
	fWorkRec = rec;
	fWorkSurfaces = surfaces;
	fWorkArrUs = fSwapArrUs;      /* 0397: the newest swap is the one taken */
	fWaitStartUs = sweet_fb_abs_to_us(mach_absolute_time());
	fPresentT0Us = fWaitStartUs;
	fPState = 5;
	IOLockUnlock(fLock);
	IOLockWakeup(fLock, &fRetireEvent, false);        /* mode 1/2 waiters: the swap is no longer waiting */
	fPresentTimer->setTimeoutUS(1);
}

/* Panel transfer without spinning: start it here, poll for frame-done on the next present-thread wakeup.
 * Same register sequence as nord2_display_refresh() (osfmk/arm64/sweet_early.c), which has already run once
 * (overlay removal, display ownership) before this path is used. */
bool
AppleCLCD::xferStart(void)
{
#if defined(NORD2)
	if (fDsi == NULL || fMutex == NULL || panic_active()) { return false; }
	if (fDsi[SWEET_FB_DSI_INTSTA / 4] & SWEET_FB_DSI_BUSY) { return false; }
	fDsi[SWEET_FB_DSI_INTSTA / 4] = ~SWEET_FB_DSI_FRAME_DONE;     /* write-zero-to-clear */
	fMutex[SWEET_FB_MUTEX_EN0 / 4] = 1;
	fDsi[SWEET_FB_DSI_START / 4] = 0;
	__asm__ volatile("dsb sy" ::: "memory");
	fDsi[SWEET_FB_DSI_START / 4] = 1;
	fXferWaitAbs = mach_absolute_time();      /* 0397: the transfer runs from here; lower bound of its end until a poll sees it running */
	fXferUnd = false;
	return true;
#else
	return false;
#endif
}

bool
AppleCLCD::xferDone(void)
{
#if defined(NORD2)
	if (panic_active()) { return true; }
	if (fDsi[SWEET_FB_DSI_INTSTA / 4] & SWEET_FB_DSI_FRAME_DONE) {
		fDsi[SWEET_FB_DSI_INTSTA / 4] = ~SWEET_FB_DSI_FRAME_DONE;
		return true;
	}
#endif
	return false;
}

/* Present thread, one handler for both phases (copy, then polling the transfer). */
void
AppleCLCD::presentFired(OSObject *owner, IOTimerEventSource *sender)
{
	AppleCLCD *fb = (AppleCLCD *)owner;
	if (fb->fPState == 0) { return; }
	if (__atomic_load_n(&fb->fSwapLifecycle.stopped, __ATOMIC_ACQUIRE)) {
#if defined(NORD2)
		if (fb->fPState == 7) { fb->hwTestFinish("stopped"); return; }      /* 0396 */
		if (fb->fPState == 3 || fb->fPState == 4) { fb->scanStep(SWEET_FB_SCAN_EV_STOP); }
		fb->dropNext();
#endif
		fb->presentDone(false, false, false);
		return;
	}
#if defined(NORD2)
	if (fb->fPState == 7) { fb->hwTestPoll(sender); return; }      /* 0396: H2 test pattern */
	if (fb->fPState == 5) {
		/* 0155: the swap is presented when the GPU is done with it (polled here, not on the vsync thread); after SWEET_FB_BUSY_WAIT_US anyway */
		bool busy = false;
		for (uint32_t layer = 0; layer < SWEET_FB_LAYERS && !busy; layer++) {
			if (fb->fWorkSurfaces.object[layer] != NULL && nord2_gpu_surface_busy(fb->fWorkSurfaces.object[layer])) { busy = true; }
		}
		uint64_t waited = sweet_fb_abs_to_us(mach_absolute_time()) - fb->fWaitStartUs;
		if (busy && waited < SWEET_FB_BUSY_WAIT_US) { sender->setTimeoutUS(100); return; }
		if (busy && __atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED) >= SWEET_FB_HW_SINGLE &&
		    fb->hwCandidate(fb->fWorkRec, (SweetSurface *const *)fb->fWorkSurfaces.object)) {
			/* 0396 fence rule: a frame the overlay would read is never forced while a GPU write job on it is in flight.  It goes back
			 * to the queue (a newer swap replaces it; otherwise it is retried at the next tick) and the panel keeps its picture. */
			fb->fHwSt.fence_skip++;
			fb->fHwSt.why[SWEET_FB_HW_W_FENCE]++;
			fb->presentDone(false, false, false);
			return;
		}
		fb->fStats.busy_waits++; fb->fStats.busy_wait_us += waited;
		if (!busy) { fb->fStats.busy_wait_ok++; } else { __atomic_fetch_add(&fb->fStats.busy_forced, 1, __ATOMIC_RELAXED); }
		fb->fPState = 1;
	}
	if (fb->fPState == 3 || fb->fPState == 4) { fb->scanPoll(sender); return; }
	if (fb->fPState == 1 && sweet_fb_scan_due(fb->fScanState, fb->fNbReady, fb->fPresents,
	    sweet_fb_abs_to_us(mach_absolute_time()) / 1000, fb->fFirstPresentMs)) {
		if (fb->scanBegin(sender)) { return; }      /* probe frame in flight; otherwise present normally below */
	}
#endif
	if (fb->fPState == 1) {
		uint64_t t0 = mach_absolute_time();
		{
			uint64_t t0us = sweet_fb_abs_to_us(t0);
			uint64_t lat = t0us > fb->fSwapArrUs ? t0us - fb->fSwapArrUs : 0;
			fb->fLatBin[lat <= 8000 ? 0 : lat <= 16000 ? 1 : lat <= 25000 ? 2 : lat <= 33000 ? 3 : 4]++;
		}
		bool wasPrepared = fb->fPrepared;
		bool shown = true;
		int hw = 0;
#if defined(NORD2)
		/* 0396: plan the frame for the overlay (counts only in modes 0/1); 1 = registers written, no copy; -1 = eligible but a GPU write
		 * is in flight: requeued, never shown; 0 = CPU path for this frame */
		fb->fWorkHw = false;
		if (!wasPrepared) { fb->hwBackoffPoll(); hw = fb->hwFrameTry(__atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED)); }      /* 0397: backoff */
		if (hw < 0) { fb->presentDone(false, false, false); return; }
		if (hw > 0) {
			fb->fWorkHw = true;
			fb->fWorkEmpty = false;
			fb->fCopyUs = 0;
			fb->fLastCopyLayers = fb->fHwN;
			/* the swap retires at FRAME_DONE (presentDone), not at a copy end: the overlay reads its surfaces until then */
		}
#endif
		if (wasPrepared) { fb->fPrepared = false; }
		else if (hw > 0) { }
		else {
			fb->fCopyDst = fb->fDbl ? (fb->fFront ? fb->fPixels : fb->fPixelsB) : fb->fPixels;
			shown = fb->presentCopy(fb->fWorkRec, (SweetSurface *const *)fb->fWorkSurfaces.object);
			fb->fWorkEmpty = fb->fCopyEmpty;
			if (shown) {        /* 0393: the copy ended: the swap's surfaces are free (the transfer reads our own buffer) */
				IOLockLock(fb->fLock);
				sweet_fb_retire_copied(&fb->fRetire);
				IOLockUnlock(fb->fLock);
				IOLockWakeup(fb->fLock, &fb->fRetireEvent, false);
			}
		}
		uint64_t t1 = mach_absolute_time();
		fb->fCopyUs = wasPrepared ? fb->fPrepCopyUs : (hw > 0 ? 0 : sweet_fb_abs_to_us(t1 - t0));
		if (!shown) { fb->presentDone(false, false, false); return; }
		if (!wasPrepared && hw == 0) { fb->noteDistinct(fb->fCopyDst ? fb->fCopyDst : fb->fPixels); }
		fb->fCopyDst = 0;
#if defined(NORD2)
		if (hw == 0 && fb->fHwOn) {
			if (fb->fWorkEmpty) {
				/* 0396: nothing composited and the panel shows a HW frame: its GRAM keeps the picture, nothing is pushed */
				fb->fHwSt.empty_kept++;
				fb->fXferStart = mach_absolute_time();
				fb->presentDone(true, false, true);
				return;
			}
			fb->hwRestore("cpu frame");      /* the CPU frame below flips the overlay to the buffer it was copied into */
		}
#endif
		/* 0156: the overlay reads the buffer that was just filled.  0393: before EVERY push path (the TE-less and blocking paths below used to
		 * push the old front buffer while the frames went to the back one), and not when nothing was composited into it. */
		if (fb->fWorkEmpty) { fb->fSync.empty_frames++; }
		if (fb->fDbl && !fb->fWorkEmpty && hw == 0) {
			uint8_t back = fb->fFront ? 0 : 1;
			if (fb->ovlSelect(back)) { fb->fFront = back; }
			else { IOLog("NORD2 FB DBL: overlay flip refused, back to one buffer\n"); fb->fDbl = false; (void)fb->ovlSelect(0); fb->fFront = 0; fb->dropNext(); }
		}
		fb->fPushSafe = false;
		if (fb->fNbReady && fb->fDsi != NULL && sweet_fb_abs_to_us(t1) >= fb->fTeOffUntilUs && !(fb->fDsi[SWEET_FB_DSI_INTSTA / 4] & SWEET_FB_DSI_BUSY)) {
			/* 0155: the transfer starts at the panel's next TE (the scan then stays behind the write: no tear lines) */
			fb->fDsi[SWEET_FB_DSI_INTSTA / 4] = ~SWEET_FB_DSI_TE_RDY;
			fb->fTeStartUs = sweet_fb_abs_to_us(mach_absolute_time());
			{   /* 0190: where in the panel's refresh does this frame become pushable?  The vsync heartbeat (the clients' frame clock) is nudged so that this phase sits in the middle of the tear-free window */
				uint64_t nowUs = fb->fTeStartUs;
				if (fb->fTeLastUs != 0 && fb->fTePeriodUs >= 8000 && nowUs - fb->fTeLastUs < 1500000) {
					int64_t P = (int64_t)fb->fTePeriodUs, phi = (int64_t)((nowUs - fb->fTeLastUs) % (uint64_t)P);
					int64_t twc = (int64_t)fb->fTwMinUs; if (twc > P - 1500) { twc = P - 1500; }
					int64_t wlo = P - twc + 800, whi = P - 2000;
					fb->fPhBin[phi < wlo ? 0 : (phi > whi ? 2 : 1)]++;
					int64_t e = phi - ((wlo + whi) / 2);      /* target: the middle of the safe window */
					if (e > P / 2) { e -= P; } else if (e < -P / 2) { e += P; }
					fb->fCtlEmaUs += (e - fb->fCtlEmaUs) / 8;
					int64_t adj = -fb->fCtlEmaUs / 16;
					if (adj > 30) { adj = 30; } else if (adj < -30) { adj = -30; }
					__atomic_fetch_add(&fb->fTickCorrUs, adj, __ATOMIC_RELAXED);
				}
			}
			fb->fPState = 6;
			sender->setTimeoutUS(200);
			return;
		}
#if defined(NORD2)
		if (fb->fWorkHw && fb->hwFenceBusy()) {      /* 0396: a GPU write started on a layer since the plan: not shown */
			fb->fHwSt.late_write++;
			fb->presentDone(false, false, false);
			return;
		}
#endif
		if (fb->fNbReady && fb->xferStart()) {
			fb->fXferStart = t1;
			fb->paceOnPush();      /* 0397 */
			fb->fPState = 2;
			sender->setTimeoutUS(SWEET_FB_XFER_POLL_US);
			return;
		}
		/* The first present, or the panel link was still busy: the blocking path (it also takes the inherited
		 * overlay down and proves the link works before the polled path is used). */
#if defined(NORD2)
		int done = nord2_display_refresh();
		if (done && fb->fDsi != NULL && fb->fMutex != NULL) { fb->fNbReady = true; }
		fb->fXferStart = t1;
		fb->presentDone(true, !done, true);
#else
		fb->presentDone(true, false, true);
#endif
		return;
	}
#if defined(NORD2)
	if (fb->fPState == 6) {
		uint64_t nowUs = sweet_fb_abs_to_us(mach_absolute_time());
		uint64_t waited = nowUs - fb->fTeStartUs;
		bool te = (fb->fDsi[SWEET_FB_DSI_INTSTA / 4] & SWEET_FB_DSI_TE_RDY) != 0;
		if (fb->fTeLastUs != 0 && fb->fTePeriodUs >= 8000 && nowUs - fb->fTeLastUs < 1500000) {
			/* 0190: the panel scan restarts at every TE and the push is faster than the scan (Tw < P).  A push that starts X before a TE, 0 < X < Tw, can never
			 * overtake the scan of the previous refresh and is ahead of the next one: no tear.  A push that starts in [TE, TE + P - Tw) tears.  So: start at once
			 * when the phase since the last TE is in [P - Tw + 0.8 ms, P - 2.0 ms], otherwise wait for that window (at most P - ... 4 ms), instead of always waiting for a
			 * fixed lead before the next TE (which cost a whole refresh whenever the frame was 1 us late). */
			uint64_t P = fb->fTePeriodUs, tw = fb->fTwMinUs;
			if (tw > P - 1500) { tw = P - 1500; }
			uint64_t lo = P - tw + 800, hi = P - 2000;      /* margins: the scan start is only known to ~0.5 ms relative to the TE edge */
			uint64_t phi = (nowUs - fb->fTeLastUs) % P;
			if (phi < lo || phi > hi) {
				uint64_t wait = phi < lo ? lo - phi : (P - phi) + lo;
				fb->fDzWaits++;
				if (wait > 500) { sender->setTimeoutUS((uint32_t)(wait - 400)); return; }
				uint64_t until = nowUs + wait;
				while (sweet_fb_abs_to_us(mach_absolute_time()) < until) { IODelay(20); }
				nowUs = sweet_fb_abs_to_us(mach_absolute_time());
			}
			fb->fSafeStarts++;
			fb->fPushSafe = true;      /* 0393: tear check at frame done */
			fb->fStats.te_lead_starts++;
			te = true;
			fb->fDsi[SWEET_FB_DSI_INTSTA / 4] = ~SWEET_FB_DSI_TE_RDY;      /* the next TE seen is the one this push is ahead of */
		} else if (te) {
			fb->fTeLastUs = nowUs;      /* edge mode: the TE was just seen */
			fb->fDsi[SWEET_FB_DSI_INTSTA / 4] = ~SWEET_FB_DSI_TE_RDY;
		}
		if (!te && waited < SWEET_FB_TE_WAIT_US) { sender->setTimeoutUS(200); return; }
		if (!te) { fb->fStats.te_miss++; fb->fTeOffUntilUs = nowUs + 1000000; }
		fb->fStats.te_waits++; fb->fStats.te_wait_us += waited;
		if (waited > fb->fStats.te_wait_max) { fb->fStats.te_wait_max = (uint32_t)waited; }
		if (fb->fWorkHw && fb->hwFenceBusy()) {      /* 0396: a GPU write started on a layer during the TE wait: not shown */
			fb->fHwSt.late_write++;
			fb->presentDone(false, false, false);
			return;
		}
		if (fb->xferStart()) {
			fb->fXferStart = mach_absolute_time();
			fb->paceOnPush();      /* 0397 */
			fb->fPState = 2;
			sender->setTimeoutUS(SWEET_FB_XFER_POLL_US);
			return;
		}
		int done = nord2_display_refresh();      /* engine busy at the TE: the blocking path */
		fb->fXferStart = mach_absolute_time();
		fb->presentDone(true, !done, true);
		return;
	}
#endif
	uint64_t elapsed = sweet_fb_abs_to_us(mach_absolute_time() - fb->fXferStart);
#if defined(NORD2)
	if (fb->fPState == 2 && !fb->fTeObsSet && fb->fDsi != NULL && (fb->fDsi[SWEET_FB_DSI_INTSTA / 4] & SWEET_FB_DSI_TE_RDY)) {
		fb->fTeObsUs = sweet_fb_abs_to_us(mach_absolute_time()) - SWEET_FB_XFER_POLL_US / 2;      /* first TE since the push started */
		fb->fTeObsSet = true;
	}
#endif
	uint64_t pollAbs = mach_absolute_time();      /* 0397 */
	int st = sweet_fb_xfer_state(fb->xferDone(), elapsed);
#if defined(NORD2)
	if (st != SWEET_FB_XFER_WAIT && fb->fOvl[0] != NULL && fb->fOvl[1] != NULL) {
		/* 0191: sticky overlay status of the frame just sent (underrun/abnormal bits), then clear it */
		fb->fOvlFrames++;
		for (uint32_t i = 0; i < SWEET_FB_SCAN_PIPES; i++) {
			uint32_t v = fb->fOvl[i][SWEET_FB_OVL_INTSTA / 4];
			for (uint32_t b = 0; b < 32; b++) { if (v & (1u << b)) { fb->fOvlBits[i][b]++; } }
			fb->fOvl[i][SWEET_FB_OVL_INTSTA / 4] = 0;
			fb->fHwOvlOr |= v;          /* 0396: judged in hwFrameEnd for a HW frame */
		}
		if (fb->fDsi != NULL && (fb->fDsi[SWEET_FB_DSI_INTSTA / 4] & (1u << 12))) { fb->fDsiUnd++; fb->fDsi[SWEET_FB_DSI_INTSTA / 4] = ~(1u << 12); fb->fHwDsiSt |= 1u << 12; fb->fXferUnd = true; }
	}
#endif
	if (st == SWEET_FB_XFER_WAIT) {
		if (fb->fPState == 2) { fb->fXferWaitAbs = pollAbs; }      /* 0397: seen running at pollAbs (before any prepNext copy below) */
#if defined(NORD2)
		if (fb->fPState == 2) { fb->prepNext(); }
#endif
		sender->setTimeoutUS(SWEET_FB_XFER_POLL_US);
		return;
	}
#if defined(NORD2)
	{   /* 0393: tear detector.  A safe-window push starts before the panel's TE and must still be running when that TE comes (the write stays
		 * behind the scan of the old refresh, ahead of the new one).  Frame-done with no TE seen since the start = the push overtook the scan:
		 * the bottom rows showed the new frame one refresh early (a tear line).  TE first seen in the same 250 us poll as frame-done = ambiguous. */
		uint64_t nowUs = sweet_fb_abs_to_us(mach_absolute_time());
		fb->fSync.pushes++;
		if (fb->fPushSafe && fb->fDsi != NULL && st == SWEET_FB_XFER_DONE) {
			fb->fSync.safe_pushes++;
			if (fb->fTeObsSet && nowUs > fb->fTeObsUs + SWEET_FB_XFER_POLL_US) {
				uint64_t m = nowUs - fb->fTeObsUs;
				if (fb->fSync.te_margin_n == 0 || m < fb->fSync.te_margin_min_us) { fb->fSync.te_margin_min_us = m; }
				fb->fSync.te_margin_sum_us += m; fb->fSync.te_margin_n++;
			} else if (fb->fTeObsSet) {
				fb->fSync.te_ambig++;
			} else {
				fb->fSync.te_none++;
				if (fb->fSync.tear_logged < 16) {
					fb->fSync.tear_logged++;
					IOLog("NORD2 FB TEAR: push %llu done %llu us after start, no TE seen (te_period_us=%llu tw_min_us=%llu te_ref_age_us=%llu)\n",
					    fb->fSync.pushes, elapsed, fb->fTePeriodUs, fb->fTwMinUs, fb->fTeLastUs ? nowUs - fb->fTeLastUs : 0ULL);
				}
			}
		}
		fb->fPushSafe = false;
		uint64_t total = fb->fPresentT0Us && nowUs > fb->fPresentT0Us ? nowUs - fb->fPresentT0Us : 0;
		if (total > fb->fSync.total_max_us) { fb->fSync.total_max_us = total; }
		if (total > 16000) { fb->fSync.total_gt16++; }
		if ((fb->fCopyUs > 8000 || total > 16000) && fb->fSync.long_logged < 32) {
			fb->fSync.long_logged++;
			IOLog("NORD2 FB LONG: push %llu total_us=%llu copy_us=%llu layers=%u xfer_us=%llu prepared=%u (first 32 presents with copy > 8 ms or take-to-done > 16 ms)\n",
			    fb->fSync.pushes, total, fb->fCopyUs, fb->fLastCopyLayers, elapsed, fb->fDbl ? 1u : 0u);
		}
	}
#endif
	fb->presentDone(true, st == SWEET_FB_XFER_TIMEOUT, false);
}

/* 0191: one TE sighting.  A sighting made by the present thread's 250 us poll can only be late; one made by trackFired's spin loop is good to ~10 us. */
void
AppleCLCD::teObserve(uint64_t obsUs, bool precise)
{
#if defined(NORD2)
	bool obsLocked = false;
	if (fTeLastUs != 0 && obsUs > fTeLastUs && obsUs - fTeLastUs < 2000000) {
		uint64_t d = obsUs - fTeLastUs, P = fTePeriodUs, n = (d + P / 2) / P;
		int64_t err = n ? (int64_t)d - (int64_t)(n * P) : 0;
		if (n >= 1 && err > -(int64_t)(P / 4) && err < (int64_t)(P / 4)) {
			int64_t pe = err < -250 ? -250 : (err > 250 ? 250 : err);
			int64_t np = (int64_t)P + pe / (int64_t)((precise ? 24 : 48) * n);
			int64_t plo = fModeState == 1 ? 11100 : 8000, phi_ = fModeState == 1 ? 11350 : 20000;
			fTePeriodUs = (uint64_t)(np < plo ? plo : (np > phi_ ? phi_ : np));
			int64_t base = (int64_t)fTeLastUs + (int64_t)(n * P);
			if (precise) { fTeLastUs = (uint64_t)(base + err / 2); }
			else { fTeLastUs = (uint64_t)(base + (err < 0 ? err / 2 : (err > 400 ? 400 : err) / 16)); }
			obsLocked = true;
		}
	}
	if (!obsLocked) { fTeLastUs = obsUs; }
	if (fPanelHz == 90 && fModeState == 1) {
		int64_t h = fHbPeriodUs ? (int64_t)fHbPeriodUs : 11226;
		h += ((int64_t)fTePeriodUs - h) / 16;
		if (h < 11150) { h = 11150; } else if (h > 11300) { h = 11300; }
		fHbPeriodUs = (uint64_t)h;
		uint64_t pabs = 0; nanoseconds_to_absolutetime(fHbPeriodUs * 1000ULL, &pabs);
		fPeriodAbs = pabs;      /* the clients' frame clock runs at the panel's real refresh rate (it ran at 11111 us against a 11226 us panel: a dropped frame every second) */
	} else if (fModeState == 2 && obsLocked && fTePeriodUs > 15500 && fTePeriodUs < 18000) {
		/* 0397: back at 60 Hz (revert / kern.n2panelhz=60): the clock follows the 60 Hz TE instead of staying at 11.2 ms (89 Hz frames on a 59.4 Hz panel) */
		int64_t h = fHbPeriodUs ? (int64_t)fHbPeriodUs : (int64_t)fTePeriodUs;
		h += ((int64_t)fTePeriodUs - h) / 16;
		if (h < 16000) { h = 16000; } else if (h > 17500) { h = 17500; }
		fHbPeriodUs = (uint64_t)h;
		uint64_t pabs = 0; nanoseconds_to_absolutetime(fHbPeriodUs * 1000ULL, &pabs);
		fPeriodAbs = pabs;
	}
#else
	(void)obsUs; (void)precise;
#endif
}

/* 0191: while nothing is presented the TE reference would age and the first frame of the next interaction would start at the TE edge (a tear band at the top).  Every
 * 200 ms after activity (1 s when idle) the panel's TE is watched with a 150 us timer poll (no spinning: a spin on this thread delayed the GPU completion thread of the same CPU,
 * GPU waits went from 2 to 5 ms and 30 % of the presents were forced), and the reference is refreshed. */
void
AppleCLCD::trackFired(OSObject *owner, IOTimerEventSource *sender)
{
#if defined(NORD2)
	AppleCLCD *fb = (AppleCLCD *)owner;
	if (__atomic_load_n(&fb->fSwapLifecycle.stopped, __ATOMIC_ACQUIRE)) { return; }
	uint64_t nowUs = sweet_fb_abs_to_us(mach_absolute_time());
	uint32_t nextMs = (nowUs - fb->fLastPresentUs < 4000000) ? 200 : 1000;
	if (fb->fTrackArmed) {
		uint64_t P = fb->fTePeriodUs;
		if (fb->fPState != 0 || fb->fDsi == NULL) { fb->fTrackArmed = false; sender->setTimeoutMS(nextMs); return; }
		if (fb->fDsi[SWEET_FB_DSI_INTSTA / 4] & SWEET_FB_DSI_TE_RDY) {
			fb->fTrackArmed = false;
			fb->teObserve(nowUs - 75, false);
			sender->setTimeoutMS(nextMs);
			return;
		}
		(void)P;
		if (nowUs > fb->fTrackDeadlineUs) { fb->fTrackArmed = false; sender->setTimeoutMS(nextMs); return; }      /* the TE did not show up */
		sender->setTimeoutUS(150);
		return;
	}
	if (fb->fPowerOff || !fb->fNbReady || fb->fDsi == NULL || fb->fModeState != 1 || fb->fRetuneState != 1 || fb->fPState != 0 ||
	    fb->fTeLastUs == 0 || fb->fTePeriodUs < 11100 || fb->fTePeriodUs > 11350 || nowUs < fb->fTeLastUs) {
		sender->setTimeoutMS(nextMs);
		return;
	}
	uint64_t P = fb->fTePeriodUs, k = (nowUs - fb->fTeLastUs + 1200) / P + 1, tn = fb->fTeLastUs + k * P;
	if (tn > nowUs + 1500) { sender->setTimeoutUS((uint32_t)(tn - nowUs - 1000)); return; }
	fb->fDsi[SWEET_FB_DSI_INTSTA / 4] = ~SWEET_FB_DSI_TE_RDY;
	fb->fTrackArmed = true;
	fb->fTrackDeadlineUs = tn + 1800;
	sender->setTimeoutUS(150);
#else
	(void)owner; (void)sender;
#endif
}

/* 0152: read-only dump of the panel link configuration (DSI0, MIPI TX0 PLL/lanes), once, to size a 90 Hz mode (link rate, timings). */
void
AppleCLCD::dumpDisplayRegs(void)
{
#if defined(NORD2)
	if (fDsi != NULL) {
		for (uint32_t o = 0; o < 0x200; o += 0x10) {
			const volatile uint32_t *r = fDsi + o / 4;
			IOLog("NORD2 DISPREG DSI0+%03x: %08x %08x %08x %08x\n", o, r[0], r[1], r[2], r[3]);
		}
	}
	if (fDsi != NULL) {      /* 0153: the command queue (what DSI_START sends: write_memory_start) */
		for (uint32_t o = 0x200; o < 0x240; o += 0x10) {
			const volatile uint32_t *r = fDsi + o / 4;
			IOLog("NORD2 DISPREG DSI0+%03x: %08x %08x %08x %08x\n", o, r[0], r[1], r[2], r[3]);
		}
	}
	volatile uint32_t *top = (volatile uint32_t *)ml_io_map(0x10000000UL, 0x1000);
	if (top != NULL) {       /* 0153: topckgen CLK_CFG_0..CLK_CFG_8 (display / mminfra mux selects) */
		for (uint32_t o = 0x10; o < 0xb0; o += 0x10) {
			IOLog("NORD2 DISPREG TOPCKGEN+%03x: %08x %08x %08x %08x\n", o, top[o / 4], top[o / 4 + 1], top[o / 4 + 2], top[o / 4 + 3]);
		}
	}
	volatile uint32_t *tx = (volatile uint32_t *)ml_io_map(0x11e50000UL, 0x1000);
	if (tx != NULL) {
		for (uint32_t o = 0; o < 0x80; o += 0x10) {
			IOLog("NORD2 DISPREG MIPITX0+%03x: %08x %08x %08x %08x\n", o, tx[o / 4], tx[o / 4 + 1], tx[o / 4 + 2], tx[o / 4 + 3]);
		}
	}
#endif
}

/* 0154: link rate 600 -> 900 Mbps/lane between two frames (DSI idle, presenter owns the engine), or back.  Only the exact configuration read on
 * this phone is touched (PCW 0x5c4ec4ec, POSDIV 2, PLL enabled); anything else is left alone. */
void
AppleCLCD::linkRetune(bool up)
{
#if defined(NORD2)
	if (fDsi == NULL) { fRetuneState = 2; return; }
	if (fTx == NULL) { fTx = (volatile uint32_t *)ml_io_map(0x11e50000UL, 0x1000); }
	if (fTx == NULL) { fRetuneState = 2; return; }
	uint32_t con0 = fTx[0x2c / 4], con1 = fTx[0x30 / 4];
	if (up) {
		if (con0 != 0x5c4ec4ecu || ((con1 >> 8) & 7u) != 2u || !(con1 & 0x10u)) {
			fRetuneState = 2;
			IOLog("NORD2 FB LINK: retune refused (PLL_CON0 %08x CON1 %08x)\n", con0, con1);
			return;
		}
		fSaveCon0 = con0;
		uint32_t tim[4];
		for (unsigned i = 0; i < 4; i++) {
			fSaveTim[i] = fDsi[(0x110 + 4 * i) / 4];
			tim[i] = 0;
			for (unsigned b = 0; b < 4; b++) {
				uint32_t v = (fSaveTim[i] >> (8 * b)) & 0xffu;
				/* DA_HS_SYNC (TIMECON2 byte 1) and CONT_DET (byte 0) are not byte-clock timings */
				if (!(i == 2 && b < 2)) {
					v = (v * 5 + 3) / 4;      /* 0195: 750 Mbps instead of 900 */
					if (i == 0 && b == 1) { v += 1; }      /* 0197: HS_PRPR margin */
					if (i == 0 && b == 2) { v += 6; }      /* 0199: HS_ZERO margin (q9m was +2) */
					if (i == 0 && b == 3) { v += 5; }      /* 0199: HS_TRAIL margin (q9m was +2) */
					if (v > 255) { v = 255; }
				}
				tim[i] |= v << (8 * b);
			}
		}
		fTx[0x2c / 4] = (uint32_t)(((uint64_t)con0 * 5) / 4);
		fTx[0x30 / 4] = con1 & ~1u;
		__asm__ volatile("dsb sy" ::: "memory");
		IODelay(2);
		fTx[0x30 / 4] = con1 | 1u;
		__asm__ volatile("dsb sy" ::: "memory");
		IODelay(100);
		for (unsigned i = 0; i < 4; i++) { fDsi[(0x110 + 4 * i) / 4] = tim[i]; }
		__asm__ volatile("dsb sy" ::: "memory");
		IODelay(200);
		fRetuneState = 1; fRetunePresents = fPresents;
		IOLog("NORD2 FB LINK: raised to 750 Mbps (PCW %08x, timing %08x %08x %08x %08x)\n", fTx[0x2c / 4], tim[0], tim[1], tim[2], tim[3]);
	} else {
		fTx[0x2c / 4] = fSaveCon0;
		fTx[0x30 / 4] = con1 & ~1u;
		__asm__ volatile("dsb sy" ::: "memory");
		IODelay(2);
		fTx[0x30 / 4] = con1 | 1u;
		__asm__ volatile("dsb sy" ::: "memory");
		IODelay(100);
		for (unsigned i = 0; i < 4; i++) { fDsi[(0x110 + 4 * i) / 4] = fSaveTim[i]; }
		__asm__ volatile("dsb sy" ::: "memory");
		IODelay(200);
		fRetuneState = 2;
		IOLog("NORD2 FB LINK: reverted to 600 Mbps\n");
	}
#endif
}

/* 0155: one DCS packet over the DSI command queue, in LP, between two frames (engine idle).  CMDQ0 normally holds the write_memory_start
 * header the frame trigger sends (0x002c3909, size 1): saved and put back. */
bool
AppleCLCD::dsiDcs(const uint8_t *p, unsigned n)
{
#if defined(NORD2)
	if (fDsi == NULL || n == 0 || n > 8) { return false; }
	if (fDsi[SWEET_FB_DSI_INTSTA / 4] & SWEET_FB_DSI_BUSY) { return false; }
	uint32_t saved0 = fDsi[0x200 / 4], savedSize = fDsi[0x60 / 4];
	uint32_t size;
	if (n <= 2) {
		fDsi[0x200 / 4] = ((uint32_t)p[0] << 16) | ((n == 1 ? 0x05u : 0x15u) << 8) | (n == 2 ? ((uint32_t)p[1] << 24) : 0u);
		size = 1;
	} else {
		fDsi[0x200 / 4] = ((uint32_t)n << 16) | (0x39u << 8) | 2u;
		for (unsigned i = 0; i < n; i += 4) {
			uint32_t w = 0;
			for (unsigned k = 0; k < 4 && i + k < n; k++) { w |= (uint32_t)p[i + k] << (8 * k); }
			fDsi[(0x204 + i) / 4] = w;
		}
		size = 1 + (n + 3) / 4;
	}
	fDsi[0x60 / 4] = size;
	fDsi[SWEET_FB_DSI_INTSTA / 4] = ~SWEET_FB_DSI_CMD_DONE;
	__asm__ volatile("dsb sy" ::: "memory");
	fDsi[SWEET_FB_DSI_START / 4] = 0;
	__asm__ volatile("dsb sy" ::: "memory");
	fDsi[SWEET_FB_DSI_START / 4] = 1;
	bool ok = false;
	for (unsigned i = 0; i < 100; i++) {          /* up to 10 ms */
		if ((fDsi[SWEET_FB_DSI_INTSTA / 4] & SWEET_FB_DSI_CMD_DONE) && !(fDsi[SWEET_FB_DSI_INTSTA / 4] & SWEET_FB_DSI_BUSY)) { ok = true; break; }
		IODelay(100);
	}
	fDsi[0x200 / 4] = saved0;
	fDsi[0x60 / 4] = savedSize;
	fDsi[SWEET_FB_DSI_INTSTA / 4] = ~SWEET_FB_DSI_CMD_DONE;
	__asm__ volatile("dsb sy" ::: "memory");
	return ok;
#else
	(void)p; (void)n;
	return false;
#endif
}

/* The panel's own refresh switch (AMS643YE05, from the Android panel table): F0 5A 5A, 60 08 00 (90 Hz) / 60 00 00 (60 Hz), F7 0F, F0 A5 A5. */
bool
AppleCLCD::panelRefresh(bool hz90)
{
	static const uint8_t unlock[] = { 0xF0, 0x5A, 0x5A }, lock[] = { 0xF0, 0xA5, 0xA5 }, apply[] = { 0xF7, 0x0F };
	const uint8_t rate[] = { 0x60, (uint8_t)(hz90 ? 0x08 : 0x00), 0x00 };
	bool ok = dsiDcs(unlock, 3) && dsiDcs(rate, 3) && dsiDcs(apply, 2);
	bool locked = dsiDcs(lock, 3);
	IOLog("NORD2 FB PANEL: refresh switch to %u Hz %s (lock %s)\n", hz90 ? 90 : 60, ok ? "sent" : "FAILED", locked ? "ok" : "FAILED");
	return ok && locked;
}

/* 0397: panel and link back to the boot configuration (60 Hz, 600 Mbps), between two frames; the clients' clock then follows the 60 Hz TE */
void
AppleCLCD::panelTo60(void)
{
	if (fModeState == 1) { (void)panelRefresh(false); }
	fModeState = 2; fTePeriodUs = SWEET_FB_VSYNC_US; fTeLastUs = 0; fHbPeriodUs = 0;
	if (fRetuneState == 1) { linkRetune(false); } else { fRetuneState = 2; }
	publishRefresh(60);
}

/* 0397: the rate CoreAnimation reads (published at start() for 90 Hz); kept in step with the panel after a switch */
void
AppleCLCD::publishRefresh(uint32_t hz)
{
	uint32_t interval = (uint32_t)(((1ULL << 32) + hz / 2) / hz);
	OSDictionary *refresh = OSDictionary::withCapacity(1);
	OSNumber *num = OSNumber::withNumber((unsigned long long)interval, 32);
	if (refresh != NULL && num != NULL) {
		refresh->setObject("displayMinRefreshInterval", num);
		setProperty("IOMFBDisplayRefresh", refresh);
		IOLog("NORD2 FB PANEL: IOMFBDisplayRefresh.displayMinRefreshInterval %u (1/%u s) republished\n", interval, hz);
	}
	OSSafeReleaseNULL(num);
	OSSafeReleaseNULL(refresh);
}

/* 0397: a panel transfer just started (present thread) */
void
AppleCLCD::paceOnPush(void)
{
	uint64_t nowUs = sweet_fb_abs_to_us(mach_absolute_time());
	if (__atomic_exchange_n(&fPaceResetReq, 0u, __ATOMIC_ACQ_REL) != 0) {
		IOLockLock(fLock);      /* the arrival side writes under fLock */
		sweet_fb_pace_reset(&fPaceCum);
		IOLockUnlock(fLock);
		IOLog("NORD2 FB PACE: counters reset (kern.n2fbpacereset)\n");
	}
	uint64_t P = fTePeriodUs ? fTePeriodUs : SWEET_FB_VSYNC_US;
	sweet_fb_pace_push(&fPaceWin, nowUs, P, fWorkArrUs);
	sweet_fb_pace_push(&fPaceCum, nowUs, P, fWorkArrUs);
}

int
AppleCLCD::paceStat(char *buf, size_t len)
{
	struct sweet_fb_pace c;
	IOLockLock(fLock);
	c = fPaceCum;
	IOLockUnlock(fLock);
	int n = sweet_fb_pace_format(&c, buf, len, fTePeriodUs, fHbPeriodUs ? fHbPeriodUs : sweet_fb_abs_to_us(fPeriodAbs), fModeState);
	if (n > 0 && (size_t)n < len) {
		n += snprintf(buf + n, len - (size_t)n, " p90_reverts=%u retries=%u last=%s spurious_slow=%llu",
		    fP90.reverts, fP90.retries, sweet_fb_p90_reason_name(fP90.last_reason), fP90.spurious_slow);
	}
	return n;
}

void
AppleCLCD::presentDone(bool shown, bool timeout, bool legacy)
{
	uint64_t xfer = shown ? sweet_fb_abs_to_us(mach_absolute_time() - fXferStart) : 0;
	bool wasHw = fWorkHw;      /* 0396 */
	fWorkHw = false;
#if defined(NORD2)
	if (wasHw) { hwFrameEnd(shown, timeout, legacy); }
#endif
#if defined(NORD2)
	/* 0154: the presenter still owns the engine here (fPState != 0), the link is idle */
	if (shown && !legacy) {
		/* 0397: the transfer ended between the last poll that saw it running (xlo) and now (xfer); only an on-time poll measures it */
		uint64_t xlo = fXferWaitAbs > fXferStart ? sweet_fb_abs_to_us(fXferWaitAbs - fXferStart) : 0;
		bool ontime = xfer >= xlo && xfer - xlo <= SWEET_FB_PACE_ONTIME_US;
		sweet_fb_pace_xfer(&fPaceWin, xlo, xfer); sweet_fb_pace_xfer(&fPaceCum, xlo, xfer);
		if (ontime && xfer > 3000 && xfer < 20000) {
			uint64_t m = xfer > 300 ? xfer - 300 : xfer;      /* 0190: the safe-window width follows the fastest transfers */
			if (m < fTwMinUs) { fTwMinUs = m; } else { fTwMinUs += (m - fTwMinUs) / 64; }
			if (fTwMinUs < 4500) { fTwMinUs = 4500; }
		}
		if (fTeObsSet) {
			fTeObsSet = false;
			teObserve(fTeObsUs, false);
		}
		if (fRetuneState == 0 && fPresents > 30 && !timeout) {
			linkRetune(true);
			if (fRetuneState == 1 && fPanelHz == 90 && fModeState == 0) {
				fModeState = panelRefresh(true) ? 1 : 2;
				if (fModeState == 1) {
					fTePeriodUs = 11226; fTeLastUs = 0; fHbPeriodUs = 0; fP90.faults = 0;      /* re-acquire the TE at the new rate */
					publishRefresh(90);      /* 0397 */
					IOLog("NORD2 FB PANEL: 90 Hz on at present %u (switch %u, retry %u/%u)\n", fPresents, fP90.reverts + 1, fP90.retries, SWEET_FB_P90_RETRIES);
				}
			}
			if (!fDbl && fRetuneState == 1) { (void)dblEnable(); }
		}
		else if (fRetuneState == 1 && fPresents - fRetunePresents < 40 && fDsi != NULL) {
			/* 0397: was timeout || xfer > 30000 (the poll latency: a prepNext copy of 50-110 ms in the poll loop) || bit 12 (already cleared) */
			bool und = fXferUnd || (fDsi[SWEET_FB_DSI_INTSTA / 4] & (1u << 12)) != 0;
			uint32_t why = SWEET_FB_P90_R_NONE;
			bool revert = sweet_fb_p90_judge(&fP90, timeout, und, xlo, xfer, &why);
			if (revert || why != SWEET_FB_P90_R_NONE) {
				IOLog("NORD2 FB PANEL: 90 Hz %s: reason=%s xfer_lo_us=%llu xfer_hi_us=%llu underrun=%u faults=%u present %u of the guard window, retries %u/%u\n",
				    revert ? "reverted" : "fault", sweet_fb_p90_reason_name(why), xlo, xfer, und ? 1u : 0u, fP90.faults, fPresents - fRetunePresents,
				    fP90.retries, SWEET_FB_P90_RETRIES);
			}
			if (revert) { sweet_fb_p90_reverted(&fP90, why, fPresents); panelTo60(); }
		}
		/* 0397: kern.n2panelhz requests and the retry of an automatic revert (the presenter owns the engine, the link is idle) */
		uint32_t req = __atomic_exchange_n(&fPanelReq, 0u, __ATOMIC_ACQ_REL);
		if (req == 60 && fModeState != 2) {
			sweet_fb_p90_reverted(&fP90, SWEET_FB_P90_R_MANUAL, fPresents);
			IOLog("NORD2 FB PANEL: 60 Hz (sysctl kern.n2panelhz), no automatic retry\n");
			panelTo60();
		} else if (req == 90 && fModeState != 1 && fFlushTicks <= 1) {
			if (fRetuneState == 1) { linkRetune(false); }
			fPanelHz = 90; fP90.manual = false; fP90.faults = 0; fRetuneState = 0; fModeState = 0;
			IOLog("NORD2 FB PANEL: 90 Hz requested (sysctl kern.n2panelhz): link retune and panel switch at the next present\n");
		} else if (fModeState == 2 && fRetuneState == 2 && fPanelHz == 90 && sweet_fb_p90_retry_due(&fP90, fPresents)) {
			fP90.retries++; fP90.faults = 0; fRetuneState = 0; fModeState = 0;
			IOLog("NORD2 FB PANEL: 90 Hz retry %u/%u after the %s revert %u presents ago\n", fP90.retries, SWEET_FB_P90_RETRIES,
			    sweet_fb_p90_reason_name(fP90.last_reason), fPresents - fP90.revert_at);
		}
	}
#endif
	IOLockLock(fLock);
	uint8_t *rec = fWorkRec;
	sweet_fb_pending_surfaces surfaces = fWorkSurfaces;
	fWorkRec = NULL;
	sweet_fb_pending_surfaces_init(&fWorkSurfaces);
	bool requeued = false;
	if (!shown && rec != NULL) {
		/* Display ownership failed (or we are stopping): keep the refs for the retry unless a newer SwapEnd
		 * superseded this record; after stop the lifecycle releases them. */
		requeued = sweet_fb_swap_lifecycle_requeue(&fSwapLifecycle, fDirty, &surfaces);
		if (requeued) {
			fDirty = true;
		}
	}
	if (shown) { sweet_fb_retire_done(&fRetire); } else { sweet_fb_retire_requeue(&fRetire, requeued); }
	/* 0396: the overlay may read a shown HW frame's surfaces again on any later push until it is re-programmed: they are held until
	 * then (the previous HW frame's surfaces are let go).  The swap itself is retired (FRAME_DONE): the panel GRAM holds the picture.
	 * Swapped under fLock (hwRestore may run on the vsync thread once fPState is 0). */
	sweet_fb_pending_surfaces hwOld;
	sweet_fb_pending_surfaces_init(&hwOld);
	if (wasHw && shown && fHwOn) {
		hwOld = fHwHeld;
		fHwHeld = surfaces;
		sweet_fb_pending_surfaces_init(&surfaces);
		fHwShownPlan = fHwPlan;
	}
	fPState = 0;
	IOLockUnlock(fLock);
	IOLockWakeup(fLock, &fRetireEvent, false);                   /* SwapWait: the copy is over, its surfaces are free */
	sweet_fb_pending_surfaces_release(&hwOld, sweet_fb_release_surface);      /* 0396 */
	if (rec != NULL) { IOFreeData(rec, SWEET_FB_SWAP_REC_SIZE); }
	/* Safe to release only after the layer copy and the panel transfer are done. */
	sweet_fb_pending_surfaces_release(&surfaces, sweet_fb_release_surface);
	if (!shown) { return; }
	fLastPresentUs = sweet_fb_abs_to_us(mach_absolute_time());
	if (fFlushTicks <= 1) { handOver(0); }       /* 0155: the next swap, if one is waiting, starts now instead of at the next tick */
	if (fPresents++ == 0) { fFirstPresentMs = sweet_fb_abs_to_us(mach_absolute_time()) / 1000; }
	static unsigned logged;
	if (logged < 8) {
		logged++;
		IOLog("NORD2 FB: command-mode present %u done=%d (%s)\n", logged, !timeout, legacy ? "blocking" : "polled");
	}
	sweet_fb_stats_note(&fStats, fCopyUs, xfer, legacy, timeout);
	uint64_t now_us = sweet_fb_abs_to_us(mach_absolute_time());
	if (sweet_fb_stats_due(&fStats, now_us)) {
		uint32_t n = fStats.n ? fStats.n : 1;
		IOLog("NORD2 FB PRESENT: n=%u avg_us=%llu max_us=%u copy_us=%llu/%u xfer_us=%llu/%u legacy=%u timeouts=%u busy_skips=%u busy_forced=%u late_ticks=%u rate_hz=%u resv_expired=%llu busy_same=%u busy_diff=%u busy_maxrun=%u\n",
		    fStats.n, fStats.sum_us / n, fStats.max_us, fStats.sum_copy_us / n, fStats.max_copy_us,
		    fStats.sum_xfer_us / n, fStats.max_xfer_us, fStats.legacy, fStats.timeouts, fStats.busy_skips,
		    fStats.busy_forced, fStats.late_ticks, fTePeriodUs ? (uint32_t)((1000000ULL + fTePeriodUs / 2) / fTePeriodUs) : 0u, nord2_gpu_resv_expired(),      /* 0397: the panel's measured rate */
		    fStats.busy_same, fStats.busy_diff, fStats.busy_maxrun);
		IOLog("NORD2 FB ARRIVAL: swap_gaps_ms<=8/20/34/67/more=%u/%u/%u/%u/%u arrival_to_copy_ms<=8/16/25/33/more=%u/%u/%u/%u/%u\n",
		    fArrBin[0], fArrBin[1], fArrBin[2], fArrBin[3], fArrBin[4], fLatBin[0], fLatBin[1], fLatBin[2], fLatBin[3], fLatBin[4]);
		for (unsigned i = 0; i < 5; i++) { fArrBin[i] = 0; fLatBin[i] = 0; }
		if (!fRegsDumped && fPresents > 200) { fRegsDumped = true; dumpDisplayRegs(); }
		IOLog("NORD2 FB WAITS: gpu_waits=%u ok=%u avg_us=%llu te_waits=%u te_miss=%u te_avg_us=%llu te_max_us=%u panel_hz=%u mode=%u link=%u\n", fStats.busy_waits, fStats.busy_wait_ok,
		    fStats.busy_waits ? fStats.busy_wait_us / fStats.busy_waits : 0ULL, fStats.te_waits, fStats.te_miss,
		    fStats.te_waits ? fStats.te_wait_us / fStats.te_waits : 0ULL, fStats.te_wait_max, fPanelHz, fModeState, fRetuneState);
		IOLog("NORD2 FB SEEN: presents=%u different=%u same=%u gaps_ms<=20/34/67/100/more=%u/%u/%u/%u/%u max_gap_ms=%llu\n", fStats.n, fDistinct, fSame,
		    fGapBin[0], fGapBin[1], fGapBin[2], fGapBin[3], fGapBin[4], fDistinctGapMaxUs / 1000);
		IOLog("NORD2 FB CADENCE: distinct_gaps_ms<=12.5/15/19/28/more=%u/%u/%u/%u/%u dbl=%u preps=%u hits=%u te_lead_starts=%u te_period_us=%llu\n", fCad[0], fCad[1], fCad[2], fCad[3], fCad[4],
		    fDbl ? 1u : 0u, fStats.dbl_preps, fStats.dbl_hits, fStats.te_lead_starts, fTePeriodUs);
		IOLog("NORD2 FB LOCK: te_period_us=%llu tw_min_us=%llu safe_starts=%u deferred=%u ready_phase_early/in/late=%u/%u/%u ctl_err_ema_us=%lld tick_shift_us=%lld period_abs_us=%llu\n", fTePeriodUs, fTwMinUs, fSafeStarts, fDzWaits,
		    fPhBin[0], fPhBin[1], fPhBin[2], (long long)fCtlEmaUs, (long long)fTickCorrSumUs, sweet_fb_abs_to_us(fPeriodAbs));
		{
			char ob[2][200]; 
			for (uint32_t i = 0; i < SWEET_FB_SCAN_PIPES; i++) {
				size_t n = 0; ob[i][0] = 0;
				for (uint32_t b = 0; b < 32 && n < 160; b++) { if (fOvlBits[i][b]) { n += snprintf(ob[i] + n, sizeof(ob[i]) - n, "b%u=%u ", b, fOvlBits[i][b]); } }
				if (n == 0) { snprintf(ob[i], sizeof(ob[i]), "none"); }
			}
			IOLog("NORD2 FB OVL: frames=%u dsi_underrun=%u ovl0[%s] ovl1[%s]\n", fOvlFrames, fDsiUnd, ob[0], ob[1]);
			/* 0393: cumulative since boot */
			IOLog("NORD2 FB SYNC: pushes=%llu safe=%llu tear_none_te=%llu tear_ambig=%llu te_margin_min_us=%llu te_margin_avg_us=%llu src_race=%llu busy_after=%llu "
			    "copy_mismatch=%llu checked=%llu empty=%llu black_rows=%llu copy_gt4=%llu copy_gt8=%llu copy_max_us=%llu total_gt16=%llu total_max_us=%llu "
			    "swait_level=%u not_ready=%llu valve=%llu dropped=%llu\n",
			    fSync.pushes, fSync.safe_pushes, fSync.te_none, fSync.te_ambig, fSync.te_margin_min_us,
			    fSync.te_margin_n ? fSync.te_margin_sum_us / fSync.te_margin_n : 0ULL, fSync.src_race, fSync.busy_after, fSync.copy_mismatch,
			    fSync.checked_layers, fSync.empty_frames, fSync.black_fill_rows, fSync.copy_gt4, fSync.copy_gt8, fSync.copy_max_us, fSync.total_gt16,
			    fSync.total_max_us, fSwapWaitLevel, fRetire.not_ready, fRetire.valve, fRetire.dropped);
			fOvlFrames = 0; fDsiUnd = 0; memset(fOvlBits, 0, sizeof(fOvlBits));
			{   /* 0397: pacing of this window (the cumulative counters: sysctl kern.n2fbpace) */
				static char pb[640];      /* present thread only */
				(void)sweet_fb_pace_format(&fPaceWin, pb, sizeof(pb), fTePeriodUs, sweet_fb_abs_to_us(fPeriodAbs), fModeState);
				IOLog("NORD2 FB PACE: %s p90_reverts=%u retries=%u spurious_slow=%llu | hw_backoffs=%u race_run_max=%u alias_reads=%llu alias_inv_us=%llu\n", pb,
				    fP90.reverts, fP90.retries, fP90.spurious_slow, fHwBackoffN, fHwRaceTotalRunMax, fPoolAliasReads, fPoolAliasInvUs);
				uint64_t lp = fPaceWin.last_push_us, la = fPaceWin.last_arr_us;
				IOLockLock(fLock);
				sweet_fb_pace_reset(&fPaceWin);
				fPaceWin.last_push_us = lp; fPaceWin.last_arr_us = la;      /* intervals across the window edge still count */
				IOLockUnlock(fLock);
			}
			{   /* 0396 (cumulative since boot) */
				char hb[768];
				(void)hwStat(hb, sizeof(hb));
				IOLog("NORD2 FB HW: %s\n", hb);
			}
		}
		fSafeStarts = 0; fDzWaits = 0; fTickCorrSumUs = 0; fPhBin[0] = fPhBin[1] = fPhBin[2] = 0;
		for (unsigned i = 0; i < 5; i++) { fCad[i] = 0; }
		fDistinct = fSame = 0; fDistinctGapMaxUs = 0; for (unsigned i = 0; i < 5; i++) { fGapBin[i] = 0; }
		sweet_fb_stats_reset(&fStats, now_us);
		IOLockLock(fLock);
		struct sweet_fb_retire rs = fRetire;
		IOLockUnlock(fLock);
		IOLog("NORD2 FB SWAPWAIT: level=%u calls=%llu not_ready=%llu blocked=%llu timeouts=%llu swaps_dropped=%llu late_held=%llu late_forced=%llu\n",
		    fSwapWaitLevel, rs.calls, rs.not_ready, rs.blocked, rs.timeouts, rs.dropped, rs.late, rs.late_forced);
	}
}

#if defined(NORD2)
/* ---- 0396: HW composition (present thread; sweet_fb_hw.h for the plan, the register lists and the policies) ---- */
static uint32_t get32(const uint8_t *p, uint32_t off);       /* below */

static void
sweet_fb_hw_log_plan(const char *what, uint32_t swap, const struct sweet_fb_hw_plan *p)
{
	IOLog("NORD2 FB HW: plan %s swap %u layers %u: pipe0 src_con %x L0 %08x pitch %u size %08x off %08x con %08x L1 %08x size %08x off %08x con %08x | "
	    "pipe1 src_con %x L0 %08x pitch %u size %08x off %08x con %08x L1 %08x size %08x off %08x con %08x\n", what, swap, p->nlayers,
	    p->src_con[0], p->slot[0][0].addr, p->slot[0][0].pitch, p->slot[0][0].size, p->slot[0][0].offset, sweet_fb_hw_con_final(p, 0, 0),
	    p->slot[0][1].addr, p->slot[0][1].size, p->slot[0][1].offset, sweet_fb_hw_con_final(p, 0, 1),
	    p->src_con[1], p->slot[1][0].addr, p->slot[1][0].pitch, p->slot[1][0].size, p->slot[1][0].offset, sweet_fb_hw_con_final(p, 1, 0),
	    p->slot[1][1].addr, p->slot[1][1].size, p->slot[1][1].offset, sweet_fb_hw_con_final(p, 1, 1));
}

/* Once, before the first HW frame / test: the overlay must be in the CPU path's verified layout and the OVL ports' M4U off. */
bool
AppleCLCD::hwInit(void)
{
	if (fHwInitDone) { return fHwInitOk; }
	if (!fNbReady || fDsi == NULL) { return false; }       /* not latched: tried again once the panel link runs */
	fHwInitDone = true;
	const char *why = NULL;
	if (fOvl[0] == NULL) { fOvl[0] = (volatile uint32_t *)ml_io_map(SWEET_FB_OVL0_2L, 0x1000); }
	if (fOvl[1] == NULL) { fOvl[1] = (volatile uint32_t *)ml_io_map(SWEET_FB_OVL1_2L, 0x1000); }
	if (fOvl[0] == NULL || fOvl[1] == NULL) { why = "overlay not mappable"; }
	uint64_t front = (fDbl && fFront) ? fPhysB : fPhys;
	for (uint32_t k = 0; why == NULL && k < SWEET_FB_HW_PIPES; k++) {
		volatile uint32_t *o = fOvl[k];
		struct sweet_fb_hw_base *b = &fHwBase[k];
		b->src_con = o[SWEET_FB_HW_R_SRC_CON / 4];
		b->bgclr = o[SWEET_FB_HW_R_ROI_BGCLR / 4];
		for (uint32_t i = 0; i < SWEET_FB_HW_SLOTS; i++) {
			b->con[i] = o[SWEET_FB_HW_R_CON(i) / 4];
			b->size[i] = o[SWEET_FB_HW_R_SRC_SIZE(i) / 4];
			b->offset[i] = o[SWEET_FB_HW_R_OFFSET(i) / 4];
			b->pitch[i] = o[SWEET_FB_HW_R_PITCH(i) / 4];
			b->pitch_msb[i] = o[SWEET_FB_HW_R_PITCH_MSB(i) / 4];
			b->clip[i] = o[SWEET_FB_HW_R_CLIP(i) / 4];
			b->rdma_ctrl[i] = o[SWEET_FB_HW_R_RDMA_CTRL(i) / 4];
			b->gmc[i] = o[SWEET_FB_HW_R_RDMA_GMC(i) / 4];
			b->fifo[i] = o[SWEET_FB_HW_R_RDMA_FIFO(i) / 4];
			b->addr[i] = o[SWEET_FB_HW_R_ADDR(i) / 4];
		}
		IOLog("NORD2 FB HW: init pipe%u base src_con %08x bgclr %08x roi %08x | L0 con %08x size %08x off %08x pitch %08x msb %08x clip %08x rdma %08x gmc %08x fifo %08x addr %08x | "
		    "L1 con %08x size %08x off %08x pitch %08x msb %08x clip %08x rdma %08x gmc %08x fifo %08x addr %08x\n", k, b->src_con, b->bgclr,
		    o[SWEET_FB_HW_R_ROI_SIZE / 4], b->con[0], b->size[0], b->offset[0], b->pitch[0], b->pitch_msb[0], b->clip[0], b->rdma_ctrl[0],
		    b->gmc[0], b->fifo[0], b->addr[0], b->con[1], b->size[1], b->offset[1], b->pitch[1], b->pitch_msb[1], b->clip[1],
		    b->rdma_ctrl[1], b->gmc[1], b->fifo[1], b->addr[1]);
		if (!sweet_fb_hw_base_ok(b, sweet_fb_scan_base_addr(front, fWidth, k), fRowBytes, fWidth / SWEET_FB_HW_PIPES, fHeight)) {
			why = "overlay not in the CPU-path layout (layer 0 = panel buffer, panel pitch, half panel, only layer 0 on)";
		}
	}
	if (why == NULL) {      /* R11: physical addressing on the four OVL ports */
		volatile uint32_t *l0 = (volatile uint32_t *)ml_io_map(SWEET_FB_LARB0, 0x1000);
		volatile uint32_t *l1 = (volatile uint32_t *)ml_io_map(SWEET_FB_LARB1, 0x1000);
		if (l0 == NULL || l1 == NULL) { why = "larb registers not mappable"; }
		else {
			uint32_t v[4] = { l0[SWEET_FB_LARB_NONSEC(5) / 4], l0[SWEET_FB_LARB_NONSEC(6) / 4], l1[SWEET_FB_LARB_NONSEC(5) / 4], l1[SWEET_FB_LARB_NONSEC(6) / 4] };
			IOLog("NORD2 FB HW: init larb0 p5 %08x p6 %08x larb1 p5 %08x p6 %08x (bit0 MMU_EN must be 0)\n", v[0], v[1], v[2], v[3]);
			if ((v[0] | v[1] | v[2] | v[3]) & 1u) { why = "display M4U enabled on an OVL port (addresses would be IOVAs)"; }
		}
	}
	fHwInitOk = (why == NULL);
	IOLog("NORD2 FB HW: init %s%s%s\n", fHwInitOk ? "ok: overlay layers usable" : "REFUSED: ", fHwInitOk ? "" : why,
	    fHwInitOk ? " (OVL0_2L/OVL1_2L layers 0/1, physical addresses)" : "");
	if (!fHwInitOk) { fHwSt.last_fault = SWEET_FB_HW_F_BASE; (void)__atomic_exchange_n(&sweet_fb_hw_mode, 0, __ATOMIC_RELAXED); }
	return fHwInitOk;
}

/* The visible layers of a swap as planner inputs (bottom first): the same selection and clipping as presentCopy. */
unsigned
AppleCLCD::hwInputs(const uint8_t *rec, SweetSurface *const surfaces[SWEET_FB_LAYERS], struct sweet_fb_hw_in in[SWEET_FB_HW_MAX_IN],
    SweetSurface *ss[SWEET_FB_HW_MAX_IN], uint32_t ids[SWEET_FB_HW_MAX_IN], int *why)
{
	unsigned n = 0;
	*why = SWEET_FB_HW_OK;
	if (rec == NULL) { *why = SWEET_FB_HW_W_NONE; return 0; }
	uint32_t mask = get32(rec, 0x104);
	for (uint32_t layer = 0; layer < SWEET_FB_LAYERS; layer++) {
		uint32_t id = get32(rec, 0x54 + 4 * layer);
		if (!(mask & (1u << layer)) || id == 0) { continue; }
		SweetSurface *s = surfaces[layer];
		if (s == NULL) { *why = SWEET_FB_HW_W_MISSING; return n; }
		if (s == fSurface) { continue; }                 /* the CPU path skips the panel surface too */
		const uint8_t *a = rec + 0x64 + 16 * layer;
		const uint8_t *b = rec + 0xc4 + 16 * layer;
		struct sweet_fb_layer_geom g;
		if (!sweet_fb_layer_clip(&g, (int32_t)get32(a, 0), (int32_t)get32(a, 4), (int32_t)get32(b, 0), (int32_t)get32(b, 4),
		    (int32_t)get32(b, 8), (int32_t)get32(b, 12), s->width, s->height, fOffX, fOffY, fWidth, fHeight)) { continue; }
		if (n >= SWEET_FB_HW_MAX_IN) { *why = SWEET_FB_HW_W_COUNT; return n; }
		struct sweet_fb_hw_in l;
		bzero(&l, sizeof(l));
		l.format_ok = (s->pixelFormat == SWEET_FB_FORMAT_BGRA && s->planeCount == 0 && (s->bytesPerElement == 4 || s->bytesPerElement == 0) &&
		    s->elementWidth == 1 && s->elementHeight == 1) ? 1u : 0u;
		{   /* display reachable only as a pool surface (0394: one contiguous range below 4 GiB), by the object we hold (not by id), and
			 * only with write-combined CPU mappings (the overlay does not snoop CPU caches; n2fbpoolc=1 copy-back pools stay on the CPU path) */
			uint64_t pa = sweet_surface_pool_pa_obj(s), len = sweet_surface_pool_size_obj(s);
			if (pa != 0 && sweet_fb_hw_pool_contains(pa, len) && sweet_surface_pool_cache_obj(s) == SWEET_FB_HW_POOL_CACHE_WC) { l.pa = pa; l.len = len; }
		}
		l.offset = s->offset; l.pitch = s->bytesPerRow;
		l.sx = g.sx; l.sy = g.sy; l.dx = g.dx; l.dy = g.dy; l.w = g.w; l.h = g.h;
		in[n] = l; ss[n] = s; ids[n] = id; n++;
	}
	return n;
}

bool
AppleCLCD::hwCandidate(const uint8_t *rec, SweetSurface *const surfaces[SWEET_FB_LAYERS])
{
	struct sweet_fb_hw_in in[SWEET_FB_HW_MAX_IN];
	SweetSurface *ss[SWEET_FB_HW_MAX_IN];
	uint32_t ids[SWEET_FB_HW_MAX_IN];
	int why;
	unsigned n = hwInputs(rec, surfaces, in, ss, ids, &why);
	if (why != SWEET_FB_HW_OK) { return false; }
	struct sweet_fb_hw_plan p;
	return sweet_fb_hw_plan_make(&p, in, n, __atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED), __atomic_load_n(&sweet_fb_hw_pm, __ATOMIC_RELAXED),
	           fWidth, fHeight) == SWEET_FB_HW_OK;
}

/* State 1 of a frame that was not prepared: 1 = HW frame programmed (no copy), -1 = eligible but a GPU write is in flight (requeue,
 * never shown), 0 = CPU path. */
bool
AppleCLCD::hwWriteRegs(unsigned k, const struct sweet_fb_hw_reg *r, unsigned n, uint32_t *badOff)
{
	volatile uint32_t *o = fOvl[k];
	if (o == NULL || n > SWEET_FB_HW_MAX_REGS) { *badOff = 0xffffffffu; return false; }
	for (unsigned i = 0; i < n; i++) { o[r[i].off / 4] = r[i].val; }
	__asm__ volatile("dsb sy" ::: "memory");
	for (unsigned i = 0; i < n; i++) {
		if ((o[r[i].off / 4] ^ r[i].val) & r[i].mask) { *badOff = r[i].off | (k << 16); return false; }
	}
	return true;
}

bool
AppleCLCD::hwApply(const struct sweet_fb_hw_plan *p)
{
	struct sweet_fb_hw_reg r[SWEET_FB_HW_MAX_REGS];
	uint64_t t0 = mach_absolute_time();
	uint32_t bad = 0;
	fHwOvlOr = 0; fHwDsiSt = 0;
	for (unsigned k = 0; k < SWEET_FB_HW_PIPES; k++) {
		unsigned n = sweet_fb_hw_regs(r, p, k, &fHwBase[k]);
		fHwOn = true;                                     /* from the first write on the registers are not the CPU path's */
		if (!hwWriteRegs(k, r, n, &bad)) {
			IOLog("NORD2 FB HW: register read-back mismatch pipe %u reg +0x%x (wrote %u registers)\n", bad >> 16, bad & 0xffffu, n);
			hwFault(SWEET_FB_HW_F_READBACK, "apply");
			return false;
		}
	}
	{   /* 0399: the CON values actually written, whenever they change (and the first 4 applies) */
		static uint32_t lastCon[2][2], conLogs;
		uint32_t c[2][2];
		for (unsigned k = 0; k < SWEET_FB_HW_PIPES; k++) {
			for (unsigned i = 0; i < SWEET_FB_HW_SLOTS; i++) { c[k][i] = p->slot[k][i].en ? sweet_fb_hw_con_final(p, k, i) : 0u; }
		}
		if (conLogs < 4 || memcmp(c, lastCon, sizeof(c)) != 0) {
			conLogs++;
			memcpy(lastCon, c, sizeof(c));
			IOLog("NORD2 FB HW: CON written pipe0 L0 %08x L1 %08x | pipe1 L0 %08x L1 %08x (pm %u, csw %u; BYTE_SWAP=R<->B bit 24, RGB_SWAP bit 25, MAN bit 23)\n",
			    c[0][0], c[0][1], c[1][0], c[1][1], __atomic_load_n(&sweet_fb_hw_pm, __ATOMIC_RELAXED), __atomic_load_n(&sweet_fb_hw_csw, __ATOMIC_RELAXED));
		}
		fHwLastCon[0] = c[0][0] ? c[0][0] : c[1][0]; fHwLastCon[1] = c[0][1] ? c[0][1] : c[1][1];
	}
	uint64_t us = sweet_fb_abs_to_us(mach_absolute_time() - t0);
	fHwSt.reg_us_sum += us;
	if (us > fHwSt.reg_us_max) { fHwSt.reg_us_max = us; }
	return true;
}

/* Back to the CPU path's registers (layer 0 = the shown panel buffer, layer 1 off, LK's background).  Between transfers only. */
void
AppleCLCD::hwRestore(const char *why)
{
	IOLockLock(fLock);
	sweet_fb_pending_surfaces held = fHwHeld;
	sweet_fb_pending_surfaces_init(&fHwHeld);
	IOLockUnlock(fLock);
	if (fHwInitOk) {
		struct sweet_fb_hw_reg r[SWEET_FB_HW_MAX_REGS];
		uint64_t front = (fDbl && fFront) ? fPhysB : fPhys;
		uint32_t bad = 0;
		bool ok = true;
		for (unsigned k = 0; k < SWEET_FB_HW_PIPES && ok; k++) {
			unsigned n = sweet_fb_hw_regs_base(r, k, &fHwBase[k], sweet_fb_scan_base_addr(front, fWidth, k));
			ok = hwWriteRegs(k, r, n, &bad);
		}
		fHwSt.restores++;
		if (!ok || fHwSt.restores <= 8) {
			IOLog("NORD2 FB HW: restored the CPU path (%s)%s, layer 0 -> %08x/%08x\n", why, ok ? "" : " READBACK MISMATCH: HW composition off for this boot",
			    sweet_fb_scan_base_addr(front, fWidth, 0), sweet_fb_scan_base_addr(front, fWidth, 1));
		}
		if (!ok) { fHwInitOk = false; (void)__atomic_exchange_n(&sweet_fb_hw_mode, 0, __ATOMIC_RELAXED); }
	}
	fHwOn = false;
	sweet_fb_pending_surfaces_release(&held, sweet_fb_release_surface);
}

void
AppleCLCD::hwFault(int f, const char *where)
{
	fHwSt.faults++;
	fHwSt.last_fault = f;
	uint32_t old = __atomic_exchange_n(&sweet_fb_hw_mode, 0, __ATOMIC_RELAXED);
	IOLog("NORD2 FB HW: AUTO-DISABLE %s (%s): kern.n2fbhw %u -> 0, hw_frames=%llu ovl_int=%08x dsi=%08x race_run=%u\n", sweet_fb_hw_fault_name(f),
	    where, old, fHwSt.frames, fHwOvlOr, fHwDsiSt, fHwRaceRun);
	/* 0397: temporary: back on after 2, 4, 8, 16 s (4 times per boot), unless kern.n2fbhw is set by hand meanwhile */
	if (old >= SWEET_FB_HW_SINGLE && fHwBackoffN < 4) {
		uint64_t ms = 2000ull << fHwBackoffN;
		fHwBackoffMode = old; fHwBackoffGen = __atomic_load_n(&sweet_fb_hw_mode_gen, __ATOMIC_RELAXED); fHwBackoffN++;
		fHwBackoffUntilUs = sweet_fb_abs_to_us(mach_absolute_time()) + ms * 1000ull;
		IOLog("NORD2 FB HW: BACKOFF mode %u back on in %llu ms (backoff %u/4)\n", old, ms, fHwBackoffN);
	} else if (old >= SWEET_FB_HW_SINGLE) {
		fHwBackoffMode = 0;
		IOLog("NORD2 FB HW: BACKOFF exhausted (4 faults): HW composition stays off (kern.n2fbhw=%u turns it on again)\n", old);
	}
	hwRestore("auto-disable");
}

/* 0397: present thread, before a frame is planned */
void
AppleCLCD::hwBackoffPoll(void)
{
	if (fHwBackoffMode == 0) { return; }
	if (__atomic_load_n(&sweet_fb_hw_mode_gen, __ATOMIC_RELAXED) != fHwBackoffGen || __atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED) != 0 ||
	    (fHwInitDone && !fHwInitOk)) {
		fHwBackoffMode = 0;      /* set by hand meanwhile, or the overlay is unusable for this boot */
		return;
	}
	if (sweet_fb_abs_to_us(mach_absolute_time()) < fHwBackoffUntilUs) { return; }
	(void)__atomic_exchange_n(&sweet_fb_hw_mode, fHwBackoffMode, __ATOMIC_RELAXED);
	IOLog("NORD2 FB HW: BACKOFF over: kern.n2fbhw 0 -> %u after the %s fault (backoff %u/4)\n", fHwBackoffMode, sweet_fb_hw_fault_name(fHwSt.last_fault), fHwBackoffN);
	fHwBackoffMode = 0;
}

/* 0397: CPU-path source address of a write-combined pool surface through one read-only copy-back mapping of the whole pool (created on first
 * use).  0394 maps the pool WC everywhere (uncached reads: an 11 MB layer took 80-110 ms, 300 ms for a 3-layer frame).  The alias is never
 * written, so it never holds dirty lines (nothing can be written back over GPU data); presentCopy cleans+invalidates the rows it reads right
 * before reading them, so no stale line from an earlier frame or a speculative fill is used.  0 = use the surface's own mapping. */
uintptr_t
AppleCLCD::poolAlias(SweetSurface *s)
{
	if (s == NULL || __atomic_load_n(&sweet_fb_pool_alias_mode, __ATOMIC_RELAXED) == 0) { return 0; }
	if (sweet_surface_pool_cache_obj(s) != SWEET_FB_HW_POOL_CACHE_WC) { return 0; }
	uint64_t pa = sweet_surface_pool_pa_obj(s), sz = sweet_surface_pool_size_obj(s);
	if (!sweet_fb_hw_pool_contains(pa, sz)) { return 0; }
	if (fPoolAliasVa == 0) {
		if (fPoolAliasTried) { return 0; }
		fPoolAliasTried = true;
		fPoolAliasMem = IOMemoryDescriptor::withPhysicalAddress(SWEET_FB_HW_POOL_BASE, SWEET_FB_HW_POOL_SIZE, kIODirectionIn);
		fPoolAliasMap = fPoolAliasMem != NULL ? fPoolAliasMem->map(kIOMapCopybackCache | kIOMapReadOnly) : NULL;
		if (fPoolAliasMap == NULL) {
			OSSafeReleaseNULL(fPoolAliasMem);
			IOLog("NORD2 FB POOLALIAS: refused (pool not mappable copy-back): CPU-path reads stay on the WC mappings\n");
			return 0;
		}
		fPoolAliasVa = (uintptr_t)fPoolAliasMap->getVirtualAddress();
		IOLog("NORD2 FB POOLALIAS: read-only copy-back alias of the pool 0x%llx+0x%llx at %p for CPU-path reads (kern.n2fbpoolalias=0: off)\n",
		    SWEET_FB_HW_POOL_BASE, SWEET_FB_HW_POOL_SIZE, (void *)fPoolAliasVa);
	}
	return fPoolAliasVa + (uintptr_t)(pa - SWEET_FB_HW_POOL_BASE);
}

/* the GPU fence: a write job on a layer surface queued or running, a 0112 reservation, or 0395's write-pending */
bool
AppleCLCD::hwFenceBusy(void)
{
	for (unsigned i = 0; i < fHwN; i++) {
		if (nord2_gpu_surface_busy(fHwLayerS[i]) || (fHwIdOk[i] && nord2_gpu_surface_write_pending(fHwLayerId[i]))) { return true; }
	}
	return false;
}

int
AppleCLCD::hwFrameTry(uint32_t mode)
{
	struct sweet_fb_hw_in in[SWEET_FB_HW_MAX_IN];
	SweetSurface *ss[SWEET_FB_HW_MAX_IN];
	uint32_t ids[SWEET_FB_HW_MAX_IN];
	int why;
	uint32_t pm = __atomic_load_n(&sweet_fb_hw_pm, __ATOMIC_RELAXED);
	unsigned n = hwInputs(fWorkRec, (SweetSurface *const *)fWorkSurfaces.object, in, ss, ids, &why);
	struct sweet_fb_hw_plan p;
	if (why == SWEET_FB_HW_OK) {        /* H0/H1: what would go through the overlay, whatever the mode */
		if (sweet_fb_hw_plan_make(&p, in, n, SWEET_FB_HW_SINGLE, pm, fWidth, fHeight) == SWEET_FB_HW_OK) { fHwSt.dry_single++; }
		if (sweet_fb_hw_plan_make(&p, in, n, SWEET_FB_HW_DUAL, pm, fWidth, fHeight) == SWEET_FB_HW_OK) { fHwSt.dry_dual++; }
	}
	if (mode < SWEET_FB_HW_SINGLE) {
		if (mode == SWEET_FB_HW_DRY) {
			int r = why != SWEET_FB_HW_OK ? why : sweet_fb_hw_plan_make(&p, in, n, SWEET_FB_HW_DUAL, pm, fWidth, fHeight);
			fHwSt.why[r]++;
			if (r == SWEET_FB_HW_OK && fHwSt.plans_logged < 8) { fHwSt.plans_logged++; sweet_fb_hw_log_plan("dry", get32(fWorkRec, 0x50), &p); }
		}
		return 0;
	}
	int r = why != SWEET_FB_HW_OK ? why : sweet_fb_hw_plan_make(&p, in, n, mode, pm, fWidth, fHeight);
	if (r == SWEET_FB_HW_OK && (fPowerOff || !fNbReady || !hwInit())) { r = SWEET_FB_HW_W_STATE; }
	for (unsigned i = 0; r == SWEET_FB_HW_OK && i < n; i++) {
		/* 0394/0395 look surfaces up by id: the id must still name this very surface, and its pool address be the one planned */
		SweetSurface *t = sweet_surface_copy(ids[i]);
		bool same = (t == ss[i]);
		if (t != NULL) { t->release(); }
		if (!same) { r = SWEET_FB_HW_W_MISSING; }      /* 0395's fences are by id */
	}
	if (r != SWEET_FB_HW_OK) { fHwSt.why[r]++; fHwSt.cpu_frames++; return 0; }
	bool busy[SWEET_FB_HW_MAX_IN], pend[SWEET_FB_HW_MAX_IN];
	for (unsigned i = 0; i < n; i++) { busy[i] = nord2_gpu_surface_busy(ss[i]); pend[i] = nord2_gpu_surface_write_pending(ids[i]); }
	if (!sweet_fb_hw_fence_ok(busy, pend, n)) { fHwSt.why[SWEET_FB_HW_W_FENCE]++; fHwSt.fence_skip++; return -1; }
	for (unsigned i = 0; i < n && i < SWEET_FB_HW_SLOTS; i++) {
		fHwLayerS[i] = ss[i]; fHwLayerId[i] = ids[i]; fHwIdOk[i] = true; fHwSeq[i] = nord2_gpu_surface_write_seq(ids[i]);
	}
	fHwN = n;
	sweet_fb_hw_plan_csw(&p, __atomic_load_n(&sweet_fb_hw_csw, __ATOMIC_RELAXED));      /* 0399 */
	if (!hwApply(&p)) { fHwN = 0; fHwSt.cpu_frames++; return 0; }      /* faulted: mode 0, CPU path restored; this frame by CPU */
	fHwPlan = p;
	fHwSt.planned++;
	if (fHwSt.plans_logged < 8) { fHwSt.plans_logged++; sweet_fb_hw_log_plan(mode == SWEET_FB_HW_SINGLE ? "single" : "dual", get32(fWorkRec, 0x50), &p); }
	return 1;
}

/* A HW frame's transfer finished (or was dropped): underflow / underrun / timeout / GPU race -> auto-disable. */
void
AppleCLCD::hwFrameEnd(bool shown, bool timeout, bool legacy)
{
	if (!shown) { return; }
	uint32_t ovl = fHwOvlOr, dsi = fHwDsiSt;
	if (legacy && fOvl[0] != NULL && fOvl[1] != NULL) {      /* blocking push: the polled status read did not run */
		for (uint32_t k = 0; k < SWEET_FB_HW_PIPES; k++) { ovl |= fOvl[k][SWEET_FB_OVL_INTSTA / 4]; fOvl[k][SWEET_FB_OVL_INTSTA / 4] = 0; }
		if (fDsi != NULL && (fDsi[SWEET_FB_DSI_INTSTA / 4] & SWEET_FB_HW_DSI_UNDERRUN)) { dsi |= SWEET_FB_HW_DSI_UNDERRUN; fDsi[SWEET_FB_DSI_INTSTA / 4] = ~SWEET_FB_HW_DSI_UNDERRUN; }
	}
	bool raced = false;
	for (unsigned i = 0; i < fHwN; i++) {
		if (nord2_gpu_surface_busy(fHwLayerS[i]) || nord2_gpu_surface_write_seq(fHwLayerId[i]) != fHwSeq[i]) { raced = true; }
	}
	fHwSt.frames++;
	if (raced) {
		fHwSt.race++;
		if (fHwSt.race <= 16) {
			IOLog("NORD2 FB HW: race: a GPU write to a shown layer started before FRAME_DONE (hw frame %llu, surface %u seq %llu -> %llu)\n", fHwSt.frames,
			    fHwLayerId[0], fHwSeq[0], nord2_gpu_surface_write_seq(fHwLayerId[0]));
		}
	}
	if (fHwSt.frames <= 4 || (fHwSt.frames % 1000) == 0) {
		IOLog("NORD2 FB HW: frame %llu shown by the overlay (%u layers, no CPU copy), ovl_int %08x dsi %08x timeout %u\n", fHwSt.frames, fHwN, ovl, dsi, timeout ? 1u : 0u);
	}
	fHwOvlOr = ovl; fHwDsiSt = dsi;
	/* 0397: a GPU write that started on a shown layer before FRAME_DONE is the client reusing its buffer early: at worst this one frame
	 * mixes two pictures.  It is counted, never a fault (16 in a row turned HW composition off for the boot and left the UI on 80-ms CPU
	 * copies of WC pool surfaces).  Only underflow / DSI underrun / timeout fault (with a backoff, hwFault). */
	uint32_t norace = 0;
	int f = sweet_fb_hw_frame_fault(ovl, dsi, timeout, false, &norace);
	fHwRaceRun = raced ? fHwRaceRun + 1 : 0;
	if (fHwRaceRun > fHwRaceTotalRunMax) { fHwRaceTotalRunMax = fHwRaceRun; }
	if (f != SWEET_FB_HW_F_NONE) { hwFault(f, "frame"); }
	fHwOvlOr = 0; fHwDsiSt = 0;
}

/* H2: test pattern (carve-out slot 3) through the overlay, one step per call of phase 0, held, then the previous picture back. */
void
AppleCLCD::hwTestPoll(IOTimerEventSource *sender)
{
	uint64_t nowUs = sweet_fb_abs_to_us(mach_absolute_time());
	if (fHwTestPhase == 0) {
		if (!hwInit()) { hwTestFinish("refused: overlay not usable (see the init line)"); return; }
		uint64_t bytes = (uint64_t)fRowBytes * fHeight, slot = fPhys + 2 * bytes;
		if (slot + bytes > SWEET_FB_HW_FB_END || slot + bytes > 0x100000000ull) { hwTestFinish("refused: no free slot in the carve-out"); return; }
		if (fHwTestMap == NULL) {
			fHwTestMem = IOMemoryDescriptor::withPhysicalAddress(slot, bytes, kIODirectionInOut);
			fHwTestMap = fHwTestMem != NULL ? fHwTestMem->map(kIOMapWriteCombineCache) : NULL;
			if (fHwTestMap == NULL) { OSSafeReleaseNULL(fHwTestMem); hwTestFinish("refused: test slot not mappable"); return; }
		}
		if (!fHwPatternDrawn) {      /* the test pattern itself is drawn by the CPU once (it is the reference, not the UI) */
			uintptr_t base = (uintptr_t)fHwTestMap->getVirtualAddress();
			for (uint32_t y = 0; y < fHeight; y++) {
				volatile uint32_t *row = (volatile uint32_t *)(base + (uint64_t)y * fRowBytes);
				for (uint32_t x = 0; x < fWidth; x++) { row[x] = sweet_fb_hw_pattern_px(x, y, fWidth, fHeight); }
			}
			__asm__ volatile("dsb sy" ::: "memory");
			fHwPatternDrawn = true;
			fHwTestT0 = nowUs;
		}
		if (fDsi[SWEET_FB_DSI_INTSTA / 4] & SWEET_FB_DSI_BUSY) {
			if (nowUs - fHwTestT0 > 100000) { hwTestFinish("aborted: panel link busy"); return; }
			sender->setTimeoutUS(500);
			return;
		}
		struct sweet_fb_hw_in in[2];
		uint32_t pm = 0;
		unsigned n = sweet_fb_hw_test_layers(in, fHwTestStep, slot, fRowBytes, fWidth, fHeight, __atomic_load_n(&sweet_fb_hw_pm, __ATOMIC_RELAXED), &pm);
		struct sweet_fb_hw_plan p;
		int r = sweet_fb_hw_plan_make(&p, in, n, SWEET_FB_HW_DUAL, pm, fWidth, fHeight);
		if (r != SWEET_FB_HW_OK) { IOLog("NORD2 FB HW: test plan refused: %s\n", sweet_fb_hw_why_name(r)); hwTestFinish("refused: plan"); return; }
		sweet_fb_hw_plan_csw(&p, __atomic_load_n(&sweet_fb_hw_csw, __ATOMIC_RELAXED));      /* 0399 */
		if (!hwApply(&p)) { hwTestFinish("aborted: register read-back mismatch (auto-disabled)"); return; }
		IOLog("NORD2 FB HW: test %u step %u (%s) regs written and read back ok, slot %#llx pitch %u, upper CON %08x (pm %u)\n", fHwTestId, fHwTestStep,
		    fHwTestStep == 1 ? "pattern only" : (fHwTestStep == 6 ? "COLOUR TEST: R|G|B by layer 0 at rows 620-699, by layer 1 at rows 710-789, x 390-689: both must read red, green, blue left to right" :
		    "pattern + premultiplied gradient tile at 390,1000 across the seam"), slot, fRowBytes, n > 1 ? sweet_fb_hw_con_final(&p, 0, 1) : 0u, pm);
		sweet_fb_hw_log_plan("test", fHwTestStep, &p);
		if (!xferStart()) { hwTestFinish("aborted: transfer not started"); return; }
		fXferStart = mach_absolute_time();
		fHwTestPhase = 1;
		sender->setTimeoutUS(SWEET_FB_XFER_POLL_US);
		return;
	}
	if (fHwTestPhase == 1) {
		uint64_t elapsed = sweet_fb_abs_to_us(mach_absolute_time() - fXferStart);
		int st = sweet_fb_xfer_state(xferDone(), elapsed);
		if (st == SWEET_FB_XFER_WAIT) { sender->setTimeoutUS(SWEET_FB_XFER_POLL_US); return; }
		uint32_t v0 = fOvl[0][SWEET_FB_OVL_INTSTA / 4], v1 = fOvl[1][SWEET_FB_OVL_INTSTA / 4];
		fOvl[0][SWEET_FB_OVL_INTSTA / 4] = 0; fOvl[1][SWEET_FB_OVL_INTSTA / 4] = 0;
		uint32_t dsi = fDsi[SWEET_FB_DSI_INTSTA / 4] & SWEET_FB_HW_DSI_UNDERRUN;
		if (dsi) { fDsi[SWEET_FB_DSI_INTSTA / 4] = ~SWEET_FB_HW_DSI_UNDERRUN; }
		fHwOvlOr = v0 | v1; fHwDsiSt = dsi;
		uint32_t race = 0;
		int f = sweet_fb_hw_frame_fault(v0 | v1, dsi, st == SWEET_FB_XFER_TIMEOUT, false, &race);
		IOLog("NORD2 FB HW: test %u step %u frame %s in %llu us, ovl0 int %08x ovl1 int %08x dsi_underrun %u -> %s; holding %u ms (photo now)\n", fHwTestId,
		    fHwTestStep, st == SWEET_FB_XFER_TIMEOUT ? "TIMED OUT" : "done", elapsed, v0, v1, dsi ? 1u : 0u, sweet_fb_hw_fault_name(f),
		    f == SWEET_FB_HW_F_NONE ? __atomic_load_n(&sweet_fb_hw_hold_ms, __ATOMIC_RELAXED) : 0u);
		if (f != SWEET_FB_HW_F_NONE) { hwFault(f, "test"); hwTestFinish("aborted: fault"); return; }
		fHwTestPhase = 2;
		sender->setTimeoutUS(__atomic_load_n(&sweet_fb_hw_hold_ms, __ATOMIC_RELAXED) * 1000u);
		return;
	}
	/* phase 2: hold over */
	if (fHwTestStep < fHwTestEnd) { fHwTestStep = fHwTestStep == 4 ? 6 : fHwTestStep + 1; fHwTestPhase = 0; fHwTestT0 = nowUs; sender->setTimeoutUS(1); return; }
	fHwTestPhase = 3;       /* finishing: the registers below are the final ones */
	bool busy = false;
	for (unsigned i = 0; i < SWEET_FB_PENDING_SURFACE_COUNT; i++) { if (fHwHeld.object[i] != NULL && nord2_gpu_surface_busy(fHwHeld.object[i])) { busy = true; } }
	if (fHwTestReapply && !busy && sweet_fb_pending_surfaces_any(&fHwHeld) && __atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED) >= SWEET_FB_HW_SINGLE) {
		if (!hwApply(&fHwShownPlan)) { hwTestFinish("aborted: read-back mismatch putting the HW frame back"); return; }
		hwTestFinish("finished, previous HW frame back");
	} else {
		hwRestore("test end");
		hwTestFinish("finished, previous picture back");
	}
}

void
AppleCLCD::hwTestFinish(const char *how)
{
	if (fHwTestPhase < 3 && fHwOn) { hwRestore("test abort"); }
	bool pushed = false;
	if (fNbReady && !panic_active()) { pushed = nord2_display_refresh() != 0; }       /* the restored picture onto the panel (blocking, <= 20 ms) */
	if (fOvl[0] != NULL && fOvl[1] != NULL) { fOvl[0][SWEET_FB_OVL_INTSTA / 4] = 0; fOvl[1][SWEET_FB_OVL_INTSTA / 4] = 0; }
	IOLog("NORD2 FB HW: test %u %s (pushed %u, overlay %s)\n", fHwTestId, how, pushed ? 1u : 0u, fHwOn ? "HW frame" : "CPU path");
	__atomic_store_n(&sweet_fb_hw_test_last, fHwTestId, __ATOMIC_RELAXED);
	fHwSt.tests++;
	fHwTestPhase = 0;
	IOLockLock(fLock);
	fPState = 0;
	bool dirty = fDirty;
	IOLockUnlock(fLock);
	if (dirty) { handOver(0); }
}

int
AppleCLCD::hwStat(char *buf, size_t len)
{
	const uint64_t *w = fHwSt.why;
	return snprintf(buf, len, "mode=%u pm=%u init=%u/%u on=%u hw_frames=%llu cpu_frames=%llu planned=%llu would_single=%llu would_dual=%llu "
	           "why[mode/none/count/cover/format/memory/pitch/range/missing/fence/state]=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu "
	           "fence_skip=%llu late_write=%llu race=%llu empty_kept=%llu restores=%llu faults=%llu last_fault=%s reg_us_avg=%llu reg_us_max=%llu tests=%llu "
	           "csw=%u con_l0=%08x con_l1=%08x",
	           __atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED), __atomic_load_n(&sweet_fb_hw_pm, __ATOMIC_RELAXED), fHwInitDone ? 1u : 0u,
	           fHwInitOk ? 1u : 0u, fHwOn ? 1u : 0u, fHwSt.frames, fHwSt.cpu_frames, fHwSt.planned, fHwSt.dry_single, fHwSt.dry_dual,
	           w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11], fHwSt.fence_skip, fHwSt.late_write, fHwSt.race,
	           fHwSt.empty_kept, fHwSt.restores, fHwSt.faults, sweet_fb_hw_fault_name(fHwSt.last_fault),
	           fHwSt.planned ? fHwSt.reg_us_sum / fHwSt.planned : 0ULL, fHwSt.reg_us_max, fHwSt.tests,
	           __atomic_load_n(&sweet_fb_hw_csw, __ATOMIC_RELAXED), fHwLastCon[0], fHwLastCon[1]);
}

/* H0: what CoreAnimation / the apps present: size, format, memory kind, contiguity, physical range, 32-bit reachability, pool.
 * First sight of each (surface, id), at most 160 lines per boot.  SwapEnd thread. */
void
AppleCLCD::hwLogSurface(SweetSurface *s, uint32_t id)
{
	static void *seen[64];
	static uint32_t seenId[64], seenN, logged;
	for (unsigned i = 0; i < 64; i++) { if (seen[i] == (void *)s && seenId[i] == id) { return; } }
	unsigned slot = __atomic_fetch_add(&seenN, 1, __ATOMIC_RELAXED) % 64;
	seen[slot] = s; seenId[slot] = id;
	if (__atomic_add_fetch(&logged, 1, __ATOMIC_RELAXED) > 160) { return; }
	bool buffer = s->memory != NULL && OSDynamicCast(IOBufferMemoryDescriptor, s->memory) != NULL;
	uint64_t len = s->memory != NULL ? s->memory->getLength() : 0, off = 0, lo = ~0ULL, hi = 0, prevEnd = 0;
	uint32_t segs = 0;
	while (s->memory != NULL && off < len && segs < 100000) {
		IOByteCount sl = 0;
		uint64_t pa = s->memory->getPhysicalSegment(off, &sl, kIOMemoryMapperNone);
		if (pa == 0 || sl == 0) { break; }
		if (pa != prevEnd) { segs++; }
		if (pa < lo) { lo = pa; }
		if (pa + sl > hi) { hi = pa + sl; }
		prevEnd = pa + sl;
		off += sl;
	}
	if (lo == ~0ULL) { lo = 0; }
	uint64_t ppa = sweet_surface_pool_pa_obj(s);
	uint32_t pcache = sweet_surface_pool_cache_obj(s);
	bool pool = ppa != 0 && sweet_fb_hw_pool_contains(ppa, sweet_surface_pool_size_obj(s)) && pcache == SWEET_FB_HW_POOL_CACHE_WC;
	uint32_t f = s->pixelFormat;
	char cc[5] = { (char)(f >> 24), (char)(f >> 16), (char)(f >> 8), (char)f, 0 };
	for (unsigned i = 0; i < 4; i++) { if (cc[i] < 32 || cc[i] > 126) { cc[i] = '?'; } }
	bool full = s->width >= fWidth && s->height >= fHeight;
	bool fmt = f == SWEET_FB_FORMAT_BGRA && s->planeCount == 0 && (s->bytesPerElement == 4 || s->bytesPerElement == 0) && s->elementWidth == 1 && s->elementHeight == 1;
	IOLog("NORD2 FB HW: surface id %u %ux%u fmt '%s' (0x%x) bpe %u el %ux%u bpr %u offset %u planes %u alloc %u cache %u map %#x mem %s len %llu segs %u "
	    "pa %#llx..%#llx contiguous %u below_4g %u pool %u pool_pa %#llx pool_cache %u -> %s\n", id, s->width, s->height, cc, f, s->bytesPerElement, s->elementWidth,
	    s->elementHeight, s->bytesPerRow, s->offset, s->planeCount, s->allocSize, s->cacheMode, (unsigned)s->mapCache, buffer ? "buffer" : "physical",
	    len, segs, lo, hi, segs == 1 ? 1u : 0u, (hi != 0 && hi <= 0x100000000ull) ? 1u : 0u, pool ? 1u : 0u, ppa, pcache,
	    !fmt ? "not HW: format" : (!pool ? "not HW: memory not in the display pool" : (full ? "HW eligible (full screen)" : "HW eligible (two-layer modes)")));
}

/* ---- scan-address probe (present thread; see sweet_fb_scan.h for why and for the safety model) ---- */
void
AppleCLCD::scanRestoreRegs(void)
{
	for (uint32_t i = 0; i < SWEET_FB_SCAN_PIPES; i++) {
		if (fOvl[i] == NULL) { continue; }
		fOvl[i][SWEET_FB_OVL_ADDR / 4] = sweet_fb_scan_base_addr(fPhys, fWidth, i);
		fOvl[i][SWEET_FB_OVL_PITCH / 4] = sweet_fb_scan_pitch_reg(fOvl[i][SWEET_FB_OVL_PITCH / 4], fRowBytes);
	}
	__asm__ volatile("dsb sy" ::: "memory");
	IOLog("NORD2 FB SCAN: restored addr %08x/%08x pitch %08x/%08x (base %08x/%08x)\n",
	    fOvl[0] ? fOvl[0][SWEET_FB_OVL_ADDR / 4] : 0, fOvl[1] ? fOvl[1][SWEET_FB_OVL_ADDR / 4] : 0,
	    fOvl[0] ? fOvl[0][SWEET_FB_OVL_PITCH / 4] : 0, fOvl[1] ? fOvl[1][SWEET_FB_OVL_PITCH / 4] : 0,
	    sweet_fb_scan_base_addr(fPhys, fWidth, 0), sweet_fb_scan_base_addr(fPhys, fWidth, 1));
}

void
AppleCLCD::scanStep(int event)
{
	bool restore = false;
	int next = sweet_fb_scan_next(fScanState, event, &restore);
	if (restore) { scanRestoreRegs(); }
	if (next != fScanState && !sweet_fb_scan_owns_panel(next)) {
		OSSafeReleaseNULL(fScanMap);
		OSSafeReleaseNULL(fScanMem);
	}
	fScanState = next;
}

bool
AppleCLCD::scanBegin(IOTimerEventSource *sender)
{
	const char *why = NULL;
	uint32_t want_size = (fHeight << 16) | (fWidth / SWEET_FB_SCAN_PIPES);
	for (uint32_t i = 0; i < SWEET_FB_SCAN_PIPES && why == NULL; i++) {
		volatile uint32_t *o = fOvl[i];
		if (o[SWEET_FB_OVL_ADDR / 4] != sweet_fb_scan_base_addr(fPhys, fWidth, i)) { why = "ADDR not the panel buffer"; }
		else if ((o[SWEET_FB_OVL_PITCH / 4] & 0xffffu) != fRowBytes) { why = "PITCH not the panel pitch"; }
		else if (o[SWEET_FB_OVL_SIZE / 4] != want_size) { why = "SIZE not the half panel"; }
		fOvlPitchOld[i] = o[SWEET_FB_OVL_PITCH / 4];
	}
	if (why == NULL && (fDsi[SWEET_FB_DSI_INTSTA / 4] & SWEET_FB_DSI_BUSY)) { why = "link busy"; }
	if (why == NULL) {
		fScanMem = IOMemoryDescriptor::withPhysicalAddress(fScanPlan.slot_pa, fScanPlan.bytes, kIODirectionInOut);
		fScanMap = fScanMem != NULL ? fScanMem->map(kIOMapWriteCombineCache) : NULL;
		if (fScanMap == NULL) { why = "slot not mappable"; }
	}
	if (why != NULL) {
		IOLog("NORD2 FB SCAN: probe refused, %s (addr %08x/%08x pitch %08x/%08x size %08x/%08x)\n", why,
		    fOvl[0][SWEET_FB_OVL_ADDR / 4], fOvl[1][SWEET_FB_OVL_ADDR / 4], fOvl[0][SWEET_FB_OVL_PITCH / 4],
		    fOvl[1][SWEET_FB_OVL_PITCH / 4], fOvl[0][SWEET_FB_OVL_SIZE / 4], fOvl[1][SWEET_FB_OVL_SIZE / 4]);
		/* Nothing was written to the overlay: no restore needed. */
		fScanState = SWEET_FB_SCAN_FAILED;
		OSSafeReleaseNULL(fScanMap);
		OSSafeReleaseNULL(fScanMem);
		return false;
	}
	uintptr_t base = (uintptr_t)fScanMap->getVirtualAddress();
	for (uint32_t y = 0; y < fHeight; y++) {
		volatile uint32_t *row = (volatile uint32_t *)(base + (uint64_t)y * fScanPlan.pitch);
		for (uint32_t x = 0; x < fWidth; x++) { row[x] = sweet_fb_scan_pixel(x, y, fWidth, fHeight); }
	}
	__asm__ volatile("dsb sy" ::: "memory");
	bool ok = true;
	for (uint32_t i = 0; i < SWEET_FB_SCAN_PIPES; i++) {
		fOvl[i][SWEET_FB_OVL_ADDR / 4] = (uint32_t)fScanPlan.addr[i];
		fOvl[i][SWEET_FB_OVL_PITCH / 4] = sweet_fb_scan_pitch_reg(fOvlPitchOld[i], fScanPlan.pitch);
	}
	__asm__ volatile("dsb sy" ::: "memory");
	uint32_t ra[2] = { fOvl[0][SWEET_FB_OVL_ADDR / 4], fOvl[1][SWEET_FB_OVL_ADDR / 4] };
	uint32_t rp[2] = { fOvl[0][SWEET_FB_OVL_PITCH / 4], fOvl[1][SWEET_FB_OVL_PITCH / 4] };
	for (uint32_t i = 0; i < SWEET_FB_SCAN_PIPES; i++) {
		if (ra[i] != (uint32_t)fScanPlan.addr[i] || (rp[i] & 0xffffu) != fScanPlan.pitch) { ok = false; }
	}
	IOLog("NORD2 FB SCAN: wrote addr %08x/%08x pitch %u, readback addr %08x/%08x pitch %08x/%08x -> %s\n",
	    (uint32_t)fScanPlan.addr[0], (uint32_t)fScanPlan.addr[1], fScanPlan.pitch, ra[0], ra[1], rp[0], rp[1],
	    ok ? "registers took the values" : "READBACK MISMATCH");
	if (ok && !xferStart()) { ok = false; IOLog("NORD2 FB SCAN: panel link busy, probe frame not sent\n"); }
	if (!ok) {
		scanStep(SWEET_FB_SCAN_EV_BEGIN_FAIL);      /* restores the base registers */
		return false;
	}
	scanStep(SWEET_FB_SCAN_EV_BEGIN_OK);
	fXferStart = mach_absolute_time();
	fPState = 3;
	sender->setTimeoutUS(SWEET_FB_XFER_POLL_US);
	return true;
}

void
AppleCLCD::scanPoll(IOTimerEventSource *sender)
{
	if (fPState == 3) {
		uint64_t elapsed = sweet_fb_abs_to_us(mach_absolute_time() - fXferStart);
		int st = sweet_fb_xfer_state(xferDone(), elapsed);
		if (st == SWEET_FB_XFER_WAIT) { sender->setTimeoutUS(SWEET_FB_XFER_POLL_US); return; }
		if (st == SWEET_FB_XFER_TIMEOUT) {
			IOLog("NORD2 FB SCAN: probe transfer timed out after %llu us, restoring\n", elapsed);
			scanStep(SWEET_FB_SCAN_EV_XFER_TIMEOUT);
			fPState = 1;
			sender->setTimeoutUS(1);
			return;
		}
		fScanXferUs = elapsed;
		scanStep(SWEET_FB_SCAN_EV_XFER_DONE);
		fPState = 4;
		IOLog("NORD2 FB SCAN: probe frame transferred in %llu us; pattern stays %u ms (white/yellow/cyan/green/magenta/red/blue/black bars, grey ramp, striped frame)\n",
		    elapsed, fScanHoldMs);
		sender->setTimeoutUS(fScanHoldMs * 1000u);
		return;
	}
	/* fPState == 4: hold over.  Restore the panel buffer and present the pending frame the normal way. */
	scanStep(SWEET_FB_SCAN_EV_HOLD_EXPIRED);
	IOLog("NORD2 FB SCAN: probe finished (flip %s)\n", fScanState == SWEET_FB_SCAN_DONE ? "completed, judge the screen photo/video" : "aborted");
	fPState = 1;
	sender->setTimeoutUS(1);
}
#endif

/* Late swaps (level 2): retire the ones whose surfaces the GPU is done with, or that waited too long.  Lock order: fLock, then the GPU
 * lock inside nord2_gpu_surface_busy (nothing takes them the other way round). */
void
AppleCLCD::reapLate(void)
{
	if (fSwapWaitLevel < 2) { return; }
	sweet_fb_pending_surfaces done[SWEET_FB_RETIRE_LATE_SLOTS];
	bool any = false;
	for (unsigned i = 0; i < SWEET_FB_RETIRE_LATE_SLOTS; i++) { sweet_fb_pending_surfaces_init(&done[i]); }
	IOLockLock(fLock);
	if (sweet_fb_retire_any_late(&fRetire)) {
		uint64_t now_ms = sweet_fb_abs_to_us(mach_absolute_time()) / 1000;
		for (unsigned i = 0; i < SWEET_FB_RETIRE_LATE_SLOTS; i++) {
			if (fRetire.late_id[i] == 0) { continue; }
			bool busy = false;
			for (uint32_t layer = 0; layer < SWEET_FB_LAYERS && !busy; layer++) {
				if (fLateSurfaces[i].object[layer] != NULL && nord2_gpu_surface_busy(fLateSurfaces[i].object[layer])) { busy = true; }
			}
			if (sweet_fb_retire_late_check(&fRetire, i, busy, now_ms) != SWEET_FB_LATE_KEEP) {
				done[i] = fLateSurfaces[i];
				sweet_fb_pending_surfaces_init(&fLateSurfaces[i]);
				any = true;
			}
		}
	}
	IOLockUnlock(fLock);
	if (!any) { return; }
	IOLockWakeup(fLock, &fRetireEvent, false);
	for (unsigned i = 0; i < SWEET_FB_RETIRE_LATE_SLOTS; i++) { sweet_fb_pending_surfaces_release(&done[i], sweet_fb_release_surface); }
}

void
AppleCLCD::retireStop(void)
{
	sweet_fb_pending_surfaces late[SWEET_FB_RETIRE_LATE_SLOTS];
	IOLockLock(fLock);
	for (unsigned i = 0; i < SWEET_FB_RETIRE_LATE_SLOTS; i++) {
		late[i] = fLateSurfaces[i];
		sweet_fb_pending_surfaces_init(&fLateSurfaces[i]);
		fRetire.late_id[i] = 0;
	}
	fRetire.pending = fRetire.presenting = 0;
	IOLockUnlock(fLock);
	IOLockWakeup(fLock, &fRetireEvent, false);
	for (unsigned i = 0; i < SWEET_FB_RETIRE_LATE_SLOTS; i++) { sweet_fb_pending_surfaces_release(&late[i], sweet_fb_release_surface); }
}

/* SwapWait (selector 6): scalars [swap id, options, timeout ms], see sweet_fb_swapwait.h.  Level 0 answers success as before and logs the
 * raw scalars of the first calls (they confirm the layout on the device). */
IOReturn
AppleCLCD::swapWait(const uint64_t *in, uint32_t count)
{
	uint32_t id = count > 0 ? (uint32_t)in[0] : 0;
	uint32_t options = count > 1 ? (uint32_t)in[1] : 0;
	uint32_t timeout = count > 2 ? (uint32_t)in[2] : 0;
	if (__atomic_load_n(&fSwapWaitLogged, __ATOMIC_RELAXED) < 16 && __atomic_add_fetch(&fSwapWaitLogged, 1, __ATOMIC_RELAXED) <= 16) {
		IOLog("NORD2 FB SWAPWAIT: call scalars=%u [%#llx %#llx %#llx] -> id %u options %#x timeout %u (level %u)\n", count,
		    count > 0 ? in[0] : 0, count > 1 ? in[1] : 0, count > 2 ? in[2] : 0, id, options, timeout, fSwapWaitLevel);
	}
	if (fSwapWaitLevel == 0) { return kIOReturnSuccess; }
	uint64_t start_ms = sweet_fb_abs_to_us(mach_absolute_time()) / 1000;
	bool counted = false;
	for (;;) {
		reapLate();
		IOLockLock(fLock);
		uint64_t elapsed = sweet_fb_abs_to_us(mach_absolute_time()) / 1000 - start_ms;
		bool ready = sweet_fb_swap_ready(&fRetire, id, options & 0x7fffffffu, fPowerOff, __atomic_load_n(&fSwapLifecycle.stopped, __ATOMIC_ACQUIRE));
		{   /* 0393: a swap that stays not-ready for SWEET_FB_SWAPWAIT_VALVE_MS is answered ready (a wedged presenter must not freeze the UI) */
			uint64_t now_ms = sweet_fb_abs_to_us(mach_absolute_time()) / 1000;
			if (ready) { if (fSwNrId == id) { fSwNrId = 0; } }
			else if (fSwNrId != id) { fSwNrId = id; fSwNrSinceMs = now_ms; }
			else if (now_ms - fSwNrSinceMs >= SWEET_FB_SWAPWAIT_VALVE_MS) { ready = true; fRetire.valve++; fSwNrId = 0; }
		}
		int step = sweet_fb_swapwait_step(ready, options, timeout, elapsed);
		fRetire.calls++;
		if (step == SWEET_FB_WAIT_BUSY) { fRetire.not_ready++; }
		if (step == SWEET_FB_WAIT_TIMEOUT) { fRetire.timeouts++; }
		if (step == SWEET_FB_WAIT_SLEEP) {
			if (!counted) { fRetire.blocked++; counted = true; }
			uint64_t deadline = 0;
			clock_interval_to_deadline(1, NSEC_PER_MSEC, &deadline);      /* re-check at least every millisecond (GPU completion has no event) */
			(void)IOLockSleepDeadline(fLock, &fRetireEvent, deadline, THREAD_UNINT);
		}
		IOLockUnlock(fLock);
		if (step == SWEET_FB_WAIT_OK) { return kIOReturnSuccess; }
		if (step == SWEET_FB_WAIT_BUSY) { return kIOReturnBusy; }
		if (step == SWEET_FB_WAIT_TIMEOUT) { return kIOReturnTimeout; }
	}
}

void
AppleCLCD::addClient(IOMobileFramebufferUserClient *client)
{
	IOLockLock(fLock);
	fClients->setObject(client);
	IOLockUnlock(fLock);
}

void
AppleCLCD::removeClient(IOMobileFramebufferUserClient *client)
{
	IOLockLock(fLock);
	unsigned int i = fClients->getNextIndexOfObject(client, 0);
	if (i != (unsigned int)-1) {
		fClients->removeObject(i);
	}
	IOLockUnlock(fLock);
}

static uint32_t
get32(const uint8_t *p, uint32_t off)
{
	uint32_t v;
	memcpy(&v, p + off, 4);
	return v;
}

/* Only the first two accepted SwapEnd records, never pixel memory. Each
 * line carries a byte offset and valid-word count; the final partial group
 * is zero padded without reading past the fixed record. Caller released fLock. */
static void
sweet_fb_diag_record(const uint8_t *rec, uint64_t accepted)
{
	if (accepted == 0 || accepted > 2) { return; }
	for (uint32_t offset = 0; offset < SWEET_FB_SWAP_REC_SIZE; offset += 32) {
		uint32_t words[8] = { 0 }, count = 0;
		for (uint32_t i = 0; i < 8 && offset + 4 * i < SWEET_FB_SWAP_REC_SIZE; i++) {
			words[i] = get32(rec, offset + 4 * i);
			count++;
		}
		IOLog("NORD2 FB RECORD: accepted=%llu offset=%#x words=%u data=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
		    accepted, offset, count, words[0], words[1], words[2], words[3],
		    words[4], words[5], words[6], words[7]);
	}
}

static uint32_t sweet_fb_swaps_logged;
static uint64_t sweet_fb_multi_presents;     /* 0390: presents with more than one shown layer */
static uint64_t sweet_fb_swap_count;

/* Copy each enabled layer's surface into the panel memory (no scaling). */
/*
 * A swap only records the frame: copying 10 MB into the panel on every
 * swap (backboardd presents at 60 Hz) took the whole CPU. The vsync timer
 * shows the latest frame every fFlushTicks ticks (boot-arg sweetfb_hz,
 * default 10 Hz).
 */
void
AppleCLCD::swap(const uint8_t *rec)
{
	/* Pin the surfaces now: present() runs up to a flush interval (100 ms at
	 * the default 10 Hz) later, when the ids may have been recycled/destroyed. */
	sweet_fb_pending_surfaces incoming, replaced;
	/* 0392: the record becomes the whole scene (layers outside its mask keep their surface) before anything uses it */
	uint8_t scene[SWEET_FB_SWAP_REC_SIZE];
	uint32_t rawMask = get32(rec, 0x104), rawIds[SWEET_FB_LAYERS];
	for (uint32_t layer = 0; layer < SWEET_FB_LAYERS; layer++) {
		rawIds[layer] = get32(rec, 0x54 + 4 * layer);
	}
	if (sweet_fb_retain_mode != 0) {
		memcpy(scene, rec, sizeof(scene));
		IOLockLock(fLock);
		uint32_t eff = sweet_fb_scene_apply(&sweet_fb_scene_state, scene);
		IOLockUnlock(fLock);
		rec = scene;
		if ((eff & ~rawMask) != 0 && __atomic_load_n(&sweet_fb_retain_logged, __ATOMIC_RELAXED) < 24 &&
		    __atomic_add_fetch(&sweet_fb_retain_logged, 1, __ATOMIC_RELAXED) <= 24) {
			IOLog("NORD2 FB RETAIN: swap=%u mask %#x ids=%u,%u,%u,%u -> scene mask %#x ids=%u,%u,%u,%u\n", get32(rec, 0x50), rawMask,
			    rawIds[0], rawIds[1], rawIds[2], rawIds[3], eff, get32(rec, 0x54), get32(rec, 0x58), get32(rec, 0x5c), get32(rec, 0x60));
		}
	}
	uint32_t ids[SWEET_FB_LAYERS];
	for (uint32_t layer = 0; layer < SWEET_FB_LAYERS; layer++) {
		ids[layer] = get32(rec, 0x54 + 4 * layer);
	}
	sweet_fb_pending_surfaces_capture(&incoming, ids, get32(rec, 0x104),
	    sweet_fb_retain_surface);
#if defined(NORD2)
	for (uint32_t layer = 0; layer < SWEET_FB_LAYERS; layer++) {      /* 0396 H0: properties of every surface presented, first sight */
		if (incoming.object[layer] != NULL) { hwLogSurface((SweetSurface *)incoming.object[layer], ids[layer]); }
	}
#endif
	sweet_fb_pending_surfaces_init(&replaced);
	IOLockLock(fLock);
	{
		uint64_t nu = sweet_fb_abs_to_us(mach_absolute_time());
		if (fSwapLastUs) { uint64_t g = nu - fSwapLastUs; fArrBin[g <= 8000 ? 0 : g <= 20000 ? 1 : g <= 34000 ? 2 : g <= 67000 ? 3 : 4]++; }
		fSwapLastUs = nu; fSwapArrUs = nu;
		sweet_fb_pace_arrival(&fPaceWin, nu); sweet_fb_pace_arrival(&fPaceCum, nu);      /* 0397 (fLock) */
	}
	memcpy(fPending, rec, SWEET_FB_SWAP_REC_SIZE);
	if (sweet_fb_swap_lifecycle_queue(&fSwapLifecycle, &incoming, &replaced)) {
		fDirty = true;
		/* The replaced swap retires now, or (level 2) waits in a late slot, holding its surface references, until the GPU
		 * is done with them: CoreAnimation then skips frames instead of queueing them behind the GPU. */
		unsigned slot = 0;
		if (sweet_fb_retire_on_swap(&fRetire, get32(rec, 0x50), fSwapWaitLevel >= 2 && sweet_fb_pending_surfaces_any(&replaced),
		    sweet_fb_abs_to_us(mach_absolute_time()) / 1000, &slot) == SWEET_FB_RETIRE_HELD) {
			fLateSurfaces[slot] = replaced;
			sweet_fb_pending_surfaces_init(&replaced);
		}
	}
	IOLockUnlock(fLock);
	IOLockWakeup(fLock, &fRetireEvent, false);
	/* Superseded (or post-stop) references are released outside fLock. */
	sweet_fb_pending_surfaces_release(&replaced, sweet_fb_release_surface);
	if (fFlushTicks <= 1) { handOver(0); }       /* 0155 */
	reapLate();
	if (fDiag.enabled) {
		uint64_t accepted = __atomic_add_fetch(&fDiag.swaps, 1, __ATOMIC_RELAXED);
		if (accepted <= 8) {
			IOLog("NORD2 FB SWAP: accepted=%llu seq=%u mask=%#x ids=%u,%u,%u,%u\n",
			    accepted, get32(rec, 0x50), get32(rec, 0x104), get32(rec, 0x54),
			    get32(rec, 0x58), get32(rec, 0x5c), get32(rec, 0x60));
		}
		/* Finite ABI evidence, outside fLock; never alter record interpretation. */
		if (accepted <= 2) { sweet_fb_diag_record(rec, accepted); }
	}
	if (__atomic_load_n(&sweet_fb_multi_seen, __ATOMIC_RELAXED) != 0 && __atomic_load_n(&sweet_fb_swaplog_n, __ATOMIC_RELAXED) < 60 &&
	    __atomic_add_fetch(&sweet_fb_swaplog_n, 1, __ATOMIC_RELAXED) <= 60) {          /* 0391 */
		IOLog("NORD2 FB SWAPLOG: swap=%u mask=%#x ids=%u,%u,%u,%u scene mask=%#x ids=%u,%u,%u,%u multi_presents=%llu\n", get32(rec, 0x50), rawMask,
		    rawIds[0], rawIds[1], rawIds[2], rawIds[3], get32(rec, 0x104),
		    get32(rec, 0x54), get32(rec, 0x58), get32(rec, 0x5c), get32(rec, 0x60), __atomic_load_n(&sweet_fb_multi_seen, __ATOMIC_RELAXED));
	}
	nord2_dvfs_ui_kick();
	if ((++sweet_fb_swap_count % 1000) == 0) {
		IOLog("SWEET FB: %llu swaps\n", sweet_fb_swap_count);
	}
}

/* 0393: FNV-1a over SWEET_FB_SYNC_ROWS rows spread over a layer's copied rect (source side): the same value before and after the copy
 * proves those rows did not change while they were read; a different value = something (the GPU drawing the next frame into a surface
 * whose swap was answered retired) rewrote the surface during the copy. */
#define SWEET_FB_SYNC_ROWS 16
static uint64_t
sweet_fb_rows_hash(uintptr_t src, uint32_t bpr, const struct sweet_fb_layer_geom *g)
{
	uint64_t h = 1469598103934665603ULL;
	for (int32_t r = 0; r < SWEET_FB_SYNC_ROWS; r++) {
		int32_t y = g->sy + (int32_t)(((int64_t)g->h - 1) * r / (SWEET_FB_SYNC_ROWS - 1));
		const uint32_t *p = (const uint32_t *)(src + (uint64_t)y * bpr + (uint64_t)g->sx * 4);
		for (int32_t x = 0; x < g->w; x++) { h = (h ^ p[x]) * 1099511628211ULL; }
	}
	return h;
}

/* 0393: black over the panel area outside [x0,x1) x [y0,y1) (no layer there: what a display shows without a layer).  With two panel
 * buffers an unwritten area would otherwise show the frame from two presents ago, alternating with the other buffer. */
static uint64_t
sweet_fb_black_outside(uintptr_t dst, uint32_t rowBytes, uint32_t fw, uint32_t fh, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	uint64_t rows = 0;
	if (x0 <= 0 && y0 <= 0 && x1 >= (int32_t)fw && y1 >= (int32_t)fh) { return 0; }
	for (int32_t y = 0; y < (int32_t)fh; y++) {
		uint8_t *row = (uint8_t *)(dst + (uint64_t)y * rowBytes);
		if (y < y0 || y >= y1) { bzero(row, (size_t)fw * 4); rows++; continue; }
		if (x0 > 0) { bzero(row, (size_t)x0 * 4); }
		if (x1 < (int32_t)fw) { bzero(row + (uint64_t)x1 * 4, (size_t)(fw - (uint32_t)x1) * 4); }
		if (x0 > 0 || x1 < (int32_t)fw) { rows++; }
	}
	return rows;
}

/* Copies the layers into the panel memory. Returns false only when the display could not be taken over
 * (nothing shown); the panel transfer is started by the caller. */
bool
AppleCLCD::presentCopy(const uint8_t *rec, SweetSurface *const surfaces[SWEET_FB_LAYERS])
{
	uint32_t mask = get32(rec, 0x104);
	uint64_t diag_present = 0;
	fCopyEmpty = false;      /* 0393 */
	if (fDiag.enabled) {
		diag_present = __atomic_add_fetch(&fDiag.presents, 1, __ATOMIC_RELAXED);
		bool active = false;
		for (uint32_t layer = 0; layer < SWEET_FB_LAYERS; layer++) {
			if ((mask & (1u << layer)) && get32(rec, 0x54 + 4 * layer)) { active = true; }
		}
		if (!active) { __atomic_fetch_add(&fDiag.empty_records, 1, __ATOMIC_RELAXED); }
	}

	if (!fConsolePaused) {
#if defined(NORD2)
		if (!nord2_display_begin()) {
			/* vsync consumed fDirty before calling us. Retry at the normal
			 * flush cadence; never replace a newer fPending with this record. */
			if (fDiag.enabled && __atomic_add_fetch(&fDiag.begin_failed, 1, __ATOMIC_RELAXED) <= 4) {
				reportDiagnostics("ownership-failed");
			}
			return false;       /* vsyncFired re-queues the record and its refs */
		}
#endif
		int quiet = 0;
		fConsolePaused = true;
		if (PE_parse_boot_argn("sweetfb_noconsole", &quiet, sizeof(quiet)) && quiet != 0) {
			/* Not PE_initialize_console(kPEDisableScreen): its console switch
			 * writes the read-only-late cons_ops_index and panics. */
			sweet_console_muted = 1;
		}
	}

	/* 0390: collect the valid layers first, then copy (one layer, or n2fbblend=0) or composite them (sweet_fb_blend.h). */
	SweetSurface *ls[SWEET_FB_LAYERS];
	bool lown[SWEET_FB_LAYERS];
	uintptr_t lsrc[SWEET_FB_LAYERS];
	bool lalias[SWEET_FB_LAYERS] = { false, false, false, false };      /* 0397: read through the pool's copy-back alias */
	uint32_t lbpr[SWEET_FB_LAYERS], lidx[SWEET_FB_LAYERS];
	struct sweet_fb_layer_geom lg[SWEET_FB_LAYERS];
	unsigned nl = 0;
	/* 0391 */
	unsigned want = 0;
	for (uint32_t layer = 0; layer < SWEET_FB_LAYERS; layer++) {
		if ((mask & (1u << layer)) && get32(rec, 0x54 + 4 * layer)) { want++; }
	}
	uint64_t pdiagN = 0;
	if (want > 1) {
		pdiagN = __atomic_add_fetch(&sweet_fb_multi_seen, 1, __ATOMIC_RELAXED);
		if (!(pdiagN <= 40 || (pdiagN % 30) == 0)) { pdiagN = 0; }
	}
	uint32_t lpt[SWEET_FB_LAYERS][5][2];
	bool lptok[SWEET_FB_LAYERS];
	for (uint32_t layer = 0; layer < SWEET_FB_LAYERS; layer++) {
		uint32_t id = get32(rec, 0x54 + 4 * layer);
		const uint8_t *a = rec + 0x64 + 16 * layer;
		const uint8_t *b = rec + 0xc4 + 16 * layer;
		int32_t sx = (int32_t)get32(a, 0), sy = (int32_t)get32(a, 4);
		int32_t dx = (int32_t)get32(b, 0), dy = (int32_t)get32(b, 4);
		int32_t w = (int32_t)get32(b, 8), h = (int32_t)get32(b, 12);

		if (!(mask & (1u << layer)) || id == 0) {
			continue;
		}
		if (fDiag.enabled) { __atomic_fetch_add(&fDiag.active_layers, 1, __ATOMIC_RELAXED); }
		/* Use the reference captured at SwapEnd; fall back to a late lookup
		 * only for ids that were not resolvable then (we own that ref). */
		SweetSurface *s = surfaces[layer];
		bool ownRef = false;
		if (s == NULL) {
			s = sweet_surface_copy(id);
			ownRef = (s != NULL);
		}
		if (fDiag.enabled && s == NULL) { __atomic_fetch_add(&fDiag.missing_surfaces, 1, __ATOMIC_RELAXED); }
		if (fDiag.enabled && s != NULL && (s->pixelFormat != SWEET_FB_FORMAT_BGRA || s->planeCount != 0)) {
			__atomic_fetch_add(&fDiag.non_bgra, 1, __ATOMIC_RELAXED);
		}
		if (sweet_fb_swaps_logged < 16) {
			sweet_fb_swaps_logged++;
			IOLog("SWEET FB: swap %u layer %u surface %u %ux%u fmt 0x%x bpr %u a %d,%d %ux%u b %d,%d %dx%d\n",
			    get32(rec, 0x50), layer, id, s ? s->width : 0, s ? s->height : 0,
			    s ? s->pixelFormat : 0, s ? s->bytesPerRow : 0, sx, sy, get32(a, 8),
			    get32(a, 12), dx, dy, w, h);
		}
		if (s == NULL || s == fSurface) {
			if (ownRef) s->release();
			continue;
		}
		/* 0391: the layer's pixels before the acquire, after it, after a CIVAC of the line */
		uint32_t pt[5][2] = { { s->width / 2, s->height / 2 }, { s->width / 4, s->height / 4 }, { 3 * s->width / 4, s->height / 4 },
		    { s->width / 4, 3 * s->height / 4 }, { 3 * s->width / 4, 3 * s->height / 4 } };
		uint32_t pv[3][5] = { { 0 } };
		uintptr_t pkva = pdiagN ? s->kernelAddress() : 0;
		bool pxok = pkva != 0 && s->memory != NULL && s->elementWidth == 1 && s->elementHeight == 1 &&
		    (s->bytesPerElement == 4 || s->bytesPerElement == 0) && s->width != 0 && s->height != 0 &&
		    (uint64_t)s->offset + (uint64_t)s->height * s->bytesPerRow <= s->memory->getLength() && s->bytesPerRow >= s->width * 4;
		bool pbusy = pdiagN ? nord2_gpu_surface_busy(s) : false;
		if (pxok) {
			for (int k = 0; k < 5; k++) {
				pv[0][k] = *(volatile const uint32_t *)(pkva + s->offset + (uint64_t)pt[k][1] * s->bytesPerRow + (uint64_t)pt[k][0] * 4);
			}
		}
		nord2_gpu_surface_cpu_acquire(s);   /* GPU-written pixels become visible to this CPU copy */
		if (pxok) {
			for (int k = 0; k < 5; k++) {
				pv[1][k] = *(volatile const uint32_t *)(pkva + s->offset + (uint64_t)pt[k][1] * s->bytesPerRow + (uint64_t)pt[k][0] * 4);
			}
			for (int k = 0; k < 5; k++) {
				uintptr_t pa = pkva + s->offset + (uint64_t)pt[k][1] * s->bytesPerRow + (uint64_t)pt[k][0] * 4;
				FlushPoC_DcacheRegion((vm_offset_t)pa, 4);
				pv[2][k] = *(volatile const uint32_t *)pa;
			}
		}
		if (pdiagN) {
			uint32_t seed = s->state != NULL ? *(volatile const uint32_t *)((const uint8_t *)s->state->getBytesNoCopy() + 0xc) : 0;
			IOLog("NORD2 FB PX: multi=%llu swap=%u layer %u surface %u seed %u busy %u cache %u map %#x purg %u%s\n", pdiagN, get32(rec, 0x50),
			    layer, id, seed, pbusy ? 1u : 0u, s->cacheMode, (unsigned)s->mapCache, s->purgeable, pxok ? "" : " (pixels not readable)");
			if (pxok) {
				static const char *const stage[3] = { "pre-acquire", "acquired", "civac" };
				for (int st = 0; st < 3; st++) {
					IOLog("NORD2 FB PX:   layer %u %s c=%08x tl=%08x tr=%08x bl=%08x br=%08x\n", layer, stage[st],
					    pv[st][0], pv[st][1], pv[st][2], pv[st][3], pv[st][4]);
				}
			}
		}
		uintptr_t src = s->kernelAddress();
		uint32_t bpe = s->bytesPerElement ? s->bytesPerElement : 4;
		struct sweet_fb_layer_geom g;
		/* Logical (presented) display coordinates -> panel coordinates, clipped to the surface and the panel. */
		bool vis = sweet_fb_layer_clip(&g, sx, sy, dx, dy, w, h, s->width, s->height, fOffX, fOffY, fWidth, fHeight);
		uint64_t last = vis ? (uint64_t)s->offset + (uint64_t)(g.sy + g.h - 1) * s->bytesPerRow + (uint64_t)(g.sx + g.w) * 4 : 0;
		if (!vis || s->elementWidth != 1 || s->elementHeight != 1 ||
		    s->memory == NULL || last > s->memory->getLength() || bpe != 4) {
			src = 0;
		}
		if (src == 0) {
			if (ownRef) s->release();
			continue;
		}
#if defined(NORD2)
		if (s->offset == 0 && last <= sweet_surface_pool_size_obj(s)) {      /* 0397 */
			uintptr_t a = poolAlias(s);
			if (a != 0) { src = a; lalias[nl] = true; }
		}
#endif
		ls[nl] = s; lown[nl] = ownRef; lsrc[nl] = src + s->offset; lbpr[nl] = s->bytesPerRow; lidx[nl] = layer; lg[nl] = g;
		memcpy(lpt[nl], pt, sizeof(pt)); lptok[nl] = pxok;
		nl++;
	}
	uintptr_t dstBase = fCopyDst ? fCopyDst : fPixels;
	/* 0393: nothing to composite (no enabled layer, or none of its surfaces readable): the caller keeps the shown buffer */
	fLastCopyLayers = nl;
	if (nl == 0) { fCopyEmpty = true; }
	uint64_t syncHash[SWEET_FB_LAYERS];
	{   /* 0397: alias rows: clean+invalidate (CIVAC, allowed on a read-only mapping) right before they are read: memory, not an old line */
		uint64_t t0 = mach_absolute_time();
		bool any = false;
		for (unsigned i = 0; i < nl; i++) {
			if (!lalias[i]) { continue; }
			any = true;
			FlushPoC_DcacheRegion((vm_offset_t)(lsrc[i] + (uint64_t)lg[i].sy * lbpr[i]), (unsigned)((uint64_t)lg[i].h * lbpr[i]));
			fPoolAliasReads++;
		}
		if (any) { fPoolAliasInvUs += sweet_fb_abs_to_us(mach_absolute_time() - t0); }
	}
	for (unsigned i = 0; i < nl; i++) { syncHash[i] = sweet_fb_rows_hash(lsrc[i], lbpr[i], &lg[i]); }
	uint64_t syncT0 = mach_absolute_time();
	/* 0391: no stale CPU line of a GPU-written layer may satisfy the copy (CIVAC = what the GPU driver's own maintenance does). */
	uint64_t inval_us = 0;
	if (nl > 1 && sweet_fb_inval_mode != 0) {
		uint64_t t0 = mach_absolute_time();
		for (unsigned i = 0; i < nl; i++) {
			if (lalias[i]) { continue; }      /* 0397: done above */
			FlushPoC_DcacheRegion((vm_offset_t)(lsrc[i] + (uint64_t)lg[i].sy * lbpr[i]), (unsigned)((uint64_t)lg[i].h * lbpr[i]));
		}
		inval_us = sweet_fb_abs_to_us(mach_absolute_time() - t0);
	}
	uint32_t *rowBuf = NULL;
	if (nl > 1 && sweet_fb_blend_mode != 0) {
		rowBuf = (uint32_t *)IOMallocData((vm_size_t)fWidth * 4);
	}
	if (nl > 1) {
		uint64_t multi = __atomic_add_fetch(&sweet_fb_multi_presents, 1, __ATOMIC_RELAXED);
		if (multi <= 24 || (multi % 600) == 0) {
			IOLog("NORD2 FB LAYERS: multi=%llu swap=%u mask=%#x n=%u blend=%u inval=%u inval_us=%llu%s\n", multi, get32(rec, 0x50), mask, nl,
			    sweet_fb_blend_mode, sweet_fb_inval_mode, inval_us, (sweet_fb_blend_mode != 0 && rowBuf == NULL) ? " (row buffer alloc failed: copy)" : "");
			for (unsigned i = 0; i < nl && multi <= 24; i++) {
				IOLog("NORD2 FB LAYERS:   layer %u surface %u %ux%u fmt 0x%x bpr %u src %d,%d -> panel %d,%d %dx%d\n", lidx[i],
				    get32(rec, 0x54 + 4 * lidx[i]), ls[i]->width, ls[i]->height, ls[i]->pixelFormat, lbpr[i],
				    lg[i].sx, lg[i].sy, lg[i].dx, lg[i].dy, lg[i].w, lg[i].h);
			}
			if (multi <= 2) {
				for (uint32_t off = 0x40; off < 0x110; off += 32) {
					IOLog("NORD2 FB LAYERS:   rec+%#x %08x %08x %08x %08x %08x %08x %08x %08x\n", off, get32(rec, off), get32(rec, off + 4),
					    get32(rec, off + 8), get32(rec, off + 12), get32(rec, off + 16), get32(rec, off + 20), get32(rec, off + 24),
					    get32(rec, off + 28));
				}
			}
		}
	}
	if (rowBuf != NULL) {
		int32_t y0 = lg[0].dy, y1 = lg[0].dy + lg[0].h;
		for (unsigned i = 1; i < nl; i++) {
			if (lg[i].dy < y0) { y0 = lg[i].dy; }
			if (lg[i].dy + lg[i].h > y1) { y1 = lg[i].dy + lg[i].h; }
		}
		struct sweet_fb_blend_stats st = { 0, 0, 0 };
		/* 0393: rows no layer covers are black (they kept the frame of two presents ago with two panel buffers) */
		if (y0 > 0 || y1 < (int32_t)fHeight) {
			fSync.black_fill_rows += sweet_fb_black_outside(dstBase, fRowBytes, fWidth, fHeight, 0, y0 < 0 ? 0 : y0, (int32_t)fWidth, y1);
		}
		for (int32_t y = y0; y < y1; y++) {
			const uint32_t *rows[SWEET_FB_LAYERS];
			for (unsigned i = 0; i < nl; i++) {
				rows[i] = (y >= lg[i].dy && y < lg[i].dy + lg[i].h) ?
				    (const uint32_t *)(lsrc[i] + (uint64_t)(lg[i].sy + (y - lg[i].dy)) * lbpr[i] + (uint64_t)lg[i].sx * 4) : NULL;
			}
			uint32_t x0, x1;
			if (sweet_fb_compose_row(rowBuf, rows, lg, nl, &x0, &x1, &st)) {
				if (x0 > 0 || x1 < fWidth) {      /* 0393: the whole row, black outside the layers' span */
					if (x0 > 0) { bzero(rowBuf, (size_t)x0 * 4); }
					if (x1 < fWidth) { bzero(rowBuf + x1, (size_t)(fWidth - x1) * 4); }
					x0 = 0; x1 = fWidth; fSync.black_fill_rows++;
				}
				memcpy((void *)(dstBase + (uint64_t)y * fRowBytes + (uint64_t)x0 * 4), (const void *)(rowBuf + x0), (size_t)(x1 - x0) * 4);
			} else {
				bzero((void *)(dstBase + (uint64_t)y * fRowBytes), (size_t)fWidth * 4);
				fSync.black_fill_rows++;
			}
		}
		if (fDiag.enabled) { __atomic_fetch_add(&fDiag.copied_layers, nl, __ATOMIC_RELAXED); }
		uint64_t multi = __atomic_load_n(&sweet_fb_multi_presents, __ATOMIC_RELAXED);
		if (multi <= 24 || (multi % 600) == 0) {
			IOLog("NORD2 FB BLEND: multi=%llu rows=%d..%d upper-layer px opaque=%llu clear=%llu mixed=%llu\n", multi, y0, y1,
			    st.opaque, st.clear, st.mixed);
		}
		IOFreeData(rowBuf, (vm_size_t)fWidth * 4);
	} else {
		if (nl > 0) {       /* 0393: black outside the layers' bounding box */
			int32_t bx0 = lg[0].dx, by0 = lg[0].dy, bx1 = lg[0].dx + lg[0].w, by1 = lg[0].dy + lg[0].h;
			for (unsigned i = 1; i < nl; i++) {
				if (lg[i].dx < bx0) { bx0 = lg[i].dx; }
				if (lg[i].dy < by0) { by0 = lg[i].dy; }
				if (lg[i].dx + lg[i].w > bx1) { bx1 = lg[i].dx + lg[i].w; }
				if (lg[i].dy + lg[i].h > by1) { by1 = lg[i].dy + lg[i].h; }
			}
			fSync.black_fill_rows += sweet_fb_black_outside(dstBase, fRowBytes, fWidth, fHeight, bx0, by0, bx1, by1);
		}
		for (unsigned i = 0; i < nl; i++) {
			if (fDiag.enabled) { __atomic_fetch_add(&fDiag.copied_layers, 1, __ATOMIC_RELAXED); }
			for (int32_t y = 0; y < lg[i].h; y++) {
				memcpy((void *)(dstBase + (uint64_t)(lg[i].dy + y) * fRowBytes + (uint64_t)lg[i].dx * 4),
				    (const void *)(lsrc[i] + (uint64_t)(lg[i].sy + y) * lbpr[i] + (uint64_t)lg[i].sx * 4),
				    (size_t)lg[i].w * 4);
			}
		}
	}
	{   /* 0393: copy duration, source rewritten during the copy, GPU writing a layer right after it, copy verification (one layer) */
		uint64_t cus = sweet_fb_abs_to_us(mach_absolute_time() - syncT0);
		if (cus > 4000) { fSync.copy_gt4++; }
		if (cus > 8000) { fSync.copy_gt8++; }
		if (cus > fSync.copy_max_us) { fSync.copy_max_us = cus; }
		for (unsigned i = 0; i < nl; i++) {
			fSync.checked_layers++;
			bool raced = sweet_fb_rows_hash(lsrc[i], lbpr[i], &lg[i]) != syncHash[i];
			bool busy = nord2_gpu_surface_busy(ls[i]);
			if (raced) { fSync.src_race++; }
			if (busy) { fSync.busy_after++; }
			if ((raced || busy) && fSync.race_logged < 16) {
				fSync.race_logged++;
				IOLog("NORD2 FB RACE: swap=%u layer %u surface %u source %s%s during/after the copy (%llu us, %u layers)\n", get32(rec, 0x50), lidx[i],
				    get32(rec, 0x54 + 4 * lidx[i]), raced ? "rewritten" : "unchanged", busy ? ", GPU job writing it" : "", cus, nl);
			}
			if (nl == 1 && rowBuf == NULL && !raced && (fSync.checked_layers & 7) == 0) {     /* panel memory is write-combined: slow to read back, 4 rows every 8th */
				for (int32_t r = 0; r < SWEET_FB_SYNC_ROWS; r += 5) {
					int32_t yy = (int32_t)(((int64_t)lg[i].h - 1) * r / (SWEET_FB_SYNC_ROWS - 1));
					if (memcmp((const void *)(dstBase + (uint64_t)(lg[i].dy + yy) * fRowBytes + (uint64_t)lg[i].dx * 4),
					    (const void *)(lsrc[i] + (uint64_t)(lg[i].sy + yy) * lbpr[i] + (uint64_t)lg[i].sx * 4), (size_t)lg[i].w * 4) != 0) {
						fSync.copy_mismatch++;
						break;
					}
				}
			}
		}
	}
	if (pdiagN) {           /* 0391: the same points in the panel memory after the copy */
		for (unsigned i = 0; i < nl; i++) {
			if (!lptok[i]) { continue; }
			uint32_t pp[5];
			for (int k = 0; k < 5; k++) {
				int32_t px = lg[i].dx + ((int32_t)lpt[i][k][0] - lg[i].sx), py = lg[i].dy + ((int32_t)lpt[i][k][1] - lg[i].sy);
				pp[k] = (px >= 0 && py >= 0 && px < (int32_t)fWidth && py < (int32_t)fHeight) ?
				    *(volatile const uint32_t *)(dstBase + (uint64_t)py * fRowBytes + (uint64_t)px * 4) : 0xdeadbeefu;
			}
			IOLog("NORD2 FB PX:   layer %u panel c=%08x tl=%08x tr=%08x bl=%08x br=%08x\n", lidx[i], pp[0], pp[1], pp[2], pp[3], pp[4]);
		}
	}
	for (unsigned i = 0; i < nl; i++) {
		if (lown[i]) ls[i]->release();
	}
	if (diag_present && diag_present == __atomic_load_n(&fDiag.begin_failed, __ATOMIC_RELAXED) + 1) {
		reportDiagnostics("first-present");
	}
#if defined(NORD2)
	__asm__ volatile("dsb sy" ::: "memory");     /* write-combined panel memory must be complete before the transfer */
#endif
	return true;
}

void
AppleCLCD::stop(IOService *provider)
{
	sweet_fb_pending_surfaces pending;
	sweet_fb_pending_surfaces_init(&pending);
	/* Stop first so an in-flight vsync callback cannot re-arm the timer. */
	if (fLock != NULL) {
		IOLockLock(fLock);
		sweet_fb_swap_lifecycle_stop(&fSwapLifecycle, &pending);
		fDirty = false;
		IOLockUnlock(fLock);
	}
	if (fVsync != NULL) {
		fVsync->cancelTimeout();
		fVsync->disable();
	}
	if (fTrackTimer != NULL) { fTrackTimer->cancelTimeout(); fTrackTimer->disable(); }
	if (fPresentTimer != NULL) {
		/* Let the present thread notice the stop and drop its frame (copy/transfer in flight finishes first). */
		for (unsigned i = 0; i < 200 && fPState != 0; i++) {
			fPresentTimer->setTimeoutUS(1);
			IOSleep(1);
		}
		fPresentTimer->cancelTimeout();
		fPresentTimer->disable();
	}
#if defined(NORD2)
	if (sweet_fb_scan_owns_panel(fScanState)) { scanStep(SWEET_FB_SCAN_EV_STOP); }
	if (fHwOn && fPState == 0) { hwRestore("stop"); }        /* 0396 */
	sweet_fb_pending_surfaces_release(&fHwHeld, sweet_fb_release_surface);
	OSSafeReleaseNULL(fHwTestMap);
	OSSafeReleaseNULL(fHwTestMem);
#endif
	retireStop();
	sweet_fb_pending_surfaces_release(&pending, sweet_fb_release_surface);
	IOService::stop(provider);
}

bool
IOMobileFramebufferUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
	if (!IOUserClient::initWithTask(owningTask, securityID, type)) {
		return false;
	}
	fTask = owningTask;
	return true;
}

bool
IOMobileFramebufferUserClient::start(IOService *provider)
{
	fFB = OSDynamicCast(AppleCLCD, provider);
	if (fFB == NULL || !IOUserClient::start(provider)) {
		return false;
	}
	fFB->addClient(this);
	return true;
}

IOReturn
IOMobileFramebufferUserClient::clientClose(void)
{
	if (fFB != NULL) {
		fFB->removeClient(this);
		IOLockLock(fFB->fLock);
		mach_port_t old = fPort;
		fPort = MACH_PORT_NULL;
		IOLockUnlock(fFB->fLock);
		if (old != MACH_PORT_NULL) {
			releaseNotificationPort(old);
		}
	}
	terminate();
	return kIOReturnSuccess;
}

IOReturn
IOMobileFramebufferUserClient::registerNotificationPort(mach_port_t port, __unused UInt32 type,
    __unused io_user_reference_t refCon)
{
	mach_port_t old;
	IOLockLock(fFB->fLock);
	old = fPort;
	fPort = port;
	IOLockUnlock(fFB->fLock);
	if (old != MACH_PORT_NULL) {
		releaseNotificationPort(old);
	}
	return kIOReturnSuccess;
}

/* Called with the framebuffer lock held. */
void
IOMobileFramebufferUserClient::notify(uint32_t type, const uint64_t *values, uint32_t count)
{
	OSAsyncReference64 ref;

	if (type >= SWEET_FB_NOTIFY_TYPES || fFunc[type] == 0 || fPort == MACH_PORT_NULL) {
		return;
	}
	bzero(ref, sizeof(ref));
	setAsyncReference64(ref, fPort, (mach_vm_address_t)fFunc[type], fRefcon[type], fTask);
	sendAsyncResult64WithOptions(ref, kIOReturnSuccess, (io_user_reference_t *)values, count,
	    kIOUserNotifyOptionCanDrop);
}

static uint64_t sweet_fb_logged;        /* selectors already logged (< 64) */

IOReturn
IOMobileFramebufferUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    __unused IOExternalMethodDispatch *dispatch, __unused OSObject *target,
    __unused void *reference)
{
	IOReturn ret = kIOReturnSuccess;
	uint64_t out[2] = { 0, 0 };
	uint32_t outCount = 0;

	switch (selector) {
	case kSweetFBGetLayerDefaultSurface:
		out[0] = sweet_surface_port_in_task(fFB->fSurface, fTask);
		outCount = 1;
		break;
	case kSweetFBSwapBegin:
		IOLockLock(fFB->fLock);
		out[0] = ++fFB->fSwapID;
		IOLockUnlock(fFB->fLock);
		outCount = 1;
		break;
	case kSweetFBSwapEnd: {
		uint8_t rec[SWEET_FB_SWAP_REC_SIZE];
		if (args->structureInputDescriptor != NULL ||
		    args->structureInputSize < SWEET_FB_SWAP_REC_SIZE) {
			ret = kIOReturnBadArgument;
			break;
		}
		memcpy(rec, args->structureInput, sizeof(rec));
		fFB->swap(rec);
		break;
	}
	case kSweetFBGetID:
		out[0] = 0;
		outCount = 1;
		break;
	case kSweetFBGetDisplaySize:
		out[0] = fFB->fLogicalW;
		out[1] = fFB->fLogicalH;
		outCount = 2;
		break;
	case kSweetFBIsMainDisplay:
		out[0] = 1;
		outCount = 1;
		break;
	case kSweetFBGetDotPitch:
		out[0] = SWEET_FB_DOT_PITCH;
		outCount = 1;
		break;
	case kSweetFBNotifications:
		if (args->scalarInputCount >= 3 && args->scalarInput[2] < SWEET_FB_NOTIFY_TYPES) {
			uint32_t type = (uint32_t)args->scalarInput[2];
			IOLockLock(fFB->fLock);
			fFunc[type] = args->scalarInput[0];
			fRefcon[type] = args->scalarInput[1];
			IOLockUnlock(fFB->fLock);
			char name[32];
			proc_selfname(name, sizeof(name));
			IOLog("SWEET FB: %s notifications type %u %s\n", name, type,
			    args->scalarInput[0] ? "on" : "off");
		}
		break;
	case kSweetFBRequestPowerChange:
		if (args->scalarInputCount >= 1) {
			bool off = args->scalarInput[0] == 0;
			IOLockLock(fFB->fLock);
			if (off != fFB->fPowerOff) {
				fFB->fPowerOff = off;
				fFB->fFadeStep = 0;
				fFB->fDirty = true;     /* on: redraw the latest frame */
			}
			IOLockUnlock(fFB->fLock);
			IOLog("SWEET FB: display %s\n", off ? "off" : "on");
		}
		break;
	case kSweetFBSwapWait:
		ret = fFB->swapWait(args->scalarInput, args->scalarInputCount);
		break;
	default:
		break;
	}

	for (uint32_t i = 0; i < args->scalarOutputCount; i++) {
		args->scalarOutput[i] = i < outCount ? out[i] : 0;
	}
	if (args->structureOutputDescriptor == NULL && args->structureOutput != NULL) {
		bzero(args->structureOutput, args->structureOutputSize);
	}
	if (selector >= 64 || !(sweet_fb_logged & (1ULL << selector))) {
		if (selector < 64) {
			sweet_fb_logged |= 1ULL << selector;
		}
		char name[32];
		proc_selfname(name, sizeof(name));
		IOLog("SWEET FB: %s sel %u in %u/%u out %u/%u -> 0x%x\n", name, selector,
		    args->scalarInputCount, args->structureInputSize,
		    args->scalarOutputCount, args->structureOutputSize, ret);
	}
	return ret;
}
#endif /* SWEET */
