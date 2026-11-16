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
#include "sweet_fb_blend.h"           /* 0390; 0416: only the layer geometry is left in it */
#include "sweet_fb_scene.h"           /* 0392 */
#include "sweet_fb_hw.h"              /* 0396 */
#include "sweet_fb_cutout.h"          /* 0550: punch-hole status bar band rotation (overlay pieces + touch) */
#include "sweet_fb_snapc.h"           /* 0640: debug snapshot = the shown overlay plan composed in software (on USB request only) */
#include "sweet_fb_pace.h"            /* 0397 */
#include "sweet_fb_iommu.h"           /* 0401 IOMMU */
#include "sweet_fb_fpsmeter.h"         /* 0407: HW-frame content clock (measurement only) */
#include "sweet_fb_backlight.h"        /* 0495: panel brightness (AppleARMBacklight + DCS 0x51) */

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
 *   3 = H4: up to two layers (bottom opaque, upper with per-pixel alpha in the CON variant kern.n2fbhwpm, default 0 since 0414 = 0x21ff)
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
static uint32_t sweet_fb_hw_mode = 3;           /* kern.n2fbhw.  0413: default 3 (overlay composition is the only frame source; 0/1 = debug CPU path) */
static uint32_t sweet_fb_hw_pm = SWEET_FB_HW_PM_DEFAULT;     /* kern.n2fbhwpm.  0414: default 0 (CFMT 2 straight 0x21ff; 0x028031ff showed R/B swapped) */
static uint32_t sweet_fb_hw_bootargs_done;      /* 0414: boot-args n2fbhwpm / n2fbhwcsw read once */
/* 0399 (hwcolor-20261112): colour order.  kern.n2fbhwcsw bit 0 flips CON BYTE_SWAP (R<->B) on layer 1, bit 1 on layer 0, for every HW
 * frame and test; kern.n2fbhwselftest (default 1): the first switch to mode 2/3 of a boot runs the colour test (test 6) first. */
static uint32_t sweet_fb_hw_csw;
static uint32_t sweet_fb_hw_mode_gen;           /* 0397: kern.n2fbhw writes (a manual setting cancels a fault backoff) */
/*
 * 0402 (hwmulti-20261113): CPU-free composition.
 *  - kern.n2fbhwlayers (2..6, default 4): overlay slots per pipe in mode 3: OVL*_2L layers 0/1, then the cascaded 4L OVL's layers
 *    0..3 (sweet_fb_hw.h); 2 = exactly 0399.  The 4L block is used only if its state at init is a pass-through cascade stage; an
 *    underflow / DSI underrun on a frame that used it caps the pipes at 2 slots for 2, 4, 8, 16 s, then for the boot.
 *  - H6 fence: a swap is shown once the GPU writes that were submitted for its surfaces when SwapEnd arrived are done
 *    (0402c: lock-free -- SwapEnd peeks the GPU fence table, the present thread compares with the GPU job low water mark; the old
 *    locked form (write_seq/busy_why/writes_done + 0112 reservations) hard-locked the phone; while a 0112 reservation is outstanding the snapshot is
 *    taken again when it retires).  Meanwhile the panel keeps its picture; after 2 panel refreshes it is shown anyway
 *    (fence_forced).  A GPU write never sends a frame to the CPU path.
 *  - CPU frames are the emergency path only: counted by reason; a NORD2 FB CPUFREE line (FB HW: CPU-FREE PASS|FAIL|IDLE|OFF) every
 *    60 s with cpu frames per minute (PASS = HW frames and 0 CPU frames in the window; CPU frames during a fault backoff are
 *    cpu_fault, not counted against it).
 */
/* 0415 (hwmulti4l-20261113, = 0402c rebased on q10jgl + 0414): defaults ON.  The 0402/0402b hard locks (night1/2/7) are attributed to
 * the io_submap exhaustion found later (0410): every 4L register map is now ml_io_map_unmappable (kernel_map), once.  H6 is the lock-free
 * 0402c form only (no GPU-driver lock in SwapEnd or on the present thread).  The 4L ports (larb0/1 p5) translate together with the 2L
 * ports (0401 gate), so IOVA layers may use any slot.  IOMFBScalingLimits stays off: the planner shows layers 1:1 (no scaler). */
static uint32_t sweet_fb_hw_layers = 6;         /* 0402/0415: kern.n2fbhwlayers 2..6, default 6 = OVL*_2L 2 layers + cascaded 4L 4 layers per pipe */
static uint32_t sweet_fb_h6_mode = 1;           /* 0402b/0415: kern.n2fbh6, default 1 = lock-free H6 fence (0402c); 0 = 0396 fence */
extern "C" uint64_t nord2_gpu_fence_seq_peek(uint32_t sid);      /* 0402c (Nord2GPU.cpp): lock-free */
extern "C" uint64_t nord2_gpu_lowwater_peek(void);
static uint32_t sweet_fb_scaling_mode;          /* 0402b: kern.n2fbscaling (0 = no IOMFBScalingLimits; 1 = publish 1:1, effective at the next backboardd start) */
static inline bool sweet_fb_h6_on(void) { return __atomic_load_n(&sweet_fb_h6_mode, __ATOMIC_RELAXED) != 0 && __atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED) >= SWEET_FB_HW_SINGLE; }

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
#include <kern/thread_call.h>         /* 0495: thermal poll of the brightness cap */
#include <sys/proc.h>
#include <sys/sysctl.h>               /* 0396: kern.n2fbhw* */
#include <sys/errno.h>
#if defined(NORD2)
extern "C" {
#include <arm64/sweet_early.h>
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern vm_offset_t ml_io_map_unmappable(vm_offset_t, vm_size_t, uint32_t);      /* 0410 */
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

/*
 * 0398: every surface reference a swap holds comes from here and goes back through sweet_fb_release_surface (pending, prepared,
 * work, late slots, the shown HW frame's fHwHeld), so the pair is also the display's IOSurface use-count hold: CoreAnimation and
 * the pools see the surface in use from SwapEnd until the copy ended (CPU path) or the overlay stopped reading it (HW path).
 */
static void *
sweet_fb_retain_surface(uint32_t id)
{
	SweetSurface *s = sweet_surface_copy(id);       /* retained, or NULL */
	if (s != NULL) {
		(void)sweet_surface_use_kernel(s, SWEET_SURFACE_USE_DISPLAY, 1);
		if (!sweet_surface_wod_hold(s)) {       /* 0480: wired here, on the SwapEnd caller's thread; not wired = not shown */
			(void)sweet_surface_use_kernel(s, SWEET_SURFACE_USE_DISPLAY, -1);
			s->release();
			s = NULL;
		}
	}
	return s;
}

static void
sweet_fb_release_surface(void *p)
{
	(void)sweet_surface_use_kernel(p, SWEET_SURFACE_USE_DISPLAY, -1);      /* 0398 (refused, not counted twice, if none was taken) */
	sweet_surface_wod_unhold(p);            /* 0480 */
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
	uint32_t      fSwapID;
	bool          fConsolePaused;
	bool          fPanelBlank;      /* 0413 */
	uint32_t      fHwInitRetryN;      /* 0416: overlay init retried with a backoff, never given up */
	uint64_t      fHwInitRetryUs;
	uint32_t      fBlankFails;
	uint64_t      fHeldN, fHeld[SWEET_FB_HW_W_COUNT_ENUM];
	bool          takeDisplay(void);
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
	/* 0621 (perf-20261114): stutter events for the log analyser (present thread only, no lock): every present interval of 2+ panel
	 * refreshes inside an animation (gap <= SWEET_FB_PACE_IDLE_US) with its uptime in ms, the number of refreshes and why (r = the
	 * swap was there in time: presenter / TE / GPU fence lost it; n = CoreAnimation delivered late), and presents per second. */
	uint64_t fJankT[12]; uint16_t fJankK[12]; char fJankWhy[12]; uint32_t fJankN, fJankLost;
	uint64_t fSecT0Us, fSecFirstUs; uint16_t fSecCnt[8]; uint32_t fSecN, fSecCur;
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
	struct sweet_fb_fps fFps;                 /* 0407: content clock of HW frames + per-client fresh frames (present thread) */
	uint64_t            fFpsWinUs;
	volatile uint32_t  *fDsi, *fMutex;       /* panel transfer registers (NORD2) */
	bool                fNbReady;            /* the blocking path has run once: overlay removed, ownership taken */
	struct sweet_fb_present_stats fStats;
	static void presentFired(OSObject *owner, IOTimerEventSource *sender);
	static void trackFired(OSObject *owner, IOTimerEventSource *sender);      /* 0191: keeps the TE reference fresh while nothing is presented */
	void        teObserve(uint64_t obsUs, bool precise);
	void        presentDone(bool shown, bool timeout, bool legacy);
	void        dumpDisplayRegs(void);
	void        linkRetune(bool up);
	void        handOver(uint64_t count);
	bool        dsiDcs(const uint8_t *p, unsigned n);
	bool        panelRefresh(bool hz90);
	void        noteHwGen(int cls);      /* 0407 */
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
	/* 0640: debug snapshot.  fHwPlanIn/N: present thread (with fHwPlan); fHwShownIn/S/N: fLock (with fHwShownPlan; S are in fHwHeld) */
	struct sweet_fb_hw_in fHwPlanIn[SWEET_FB_HW_MAX_IN], fHwShownIn[SWEET_FB_HW_MAX_IN];
	SweetSurface       *fHwShownS[SWEET_FB_HW_MAX_IN];
	unsigned            fHwPlanN, fHwShownN;
	uint32_t            fSnapMs, fSnapFlags;
	SweetSurface       *fHwLayerS[SWEET_FB_HW_MAX_IN];
	uint32_t            fHwLayerId[SWEET_FB_HW_MAX_IN];
	uint64_t            fHwSeq[SWEET_FB_HW_MAX_IN], fHwSnap[SWEET_FB_HW_MAX_IN];      /* 0402: write_seq at plan / at SwapEnd */
	bool                fHwIdOk[SWEET_FB_HW_MAX_IN];
	/* 0402: the 4L OVL of each pipe, H6 fence state, CPU-free window */
	volatile uint32_t  *fOvl4[SWEET_FB_HW_PIPES];
	struct sweet_fb_hw_base fHwBase4[SWEET_FB_HW_PIPES];
	bool                fHw4Ok, fHw4On, fHwUses4, fHwForced, fHw4Tried;
	uint32_t            fHw4OffN, fWorkResv, fPendResv;
	uint64_t            fHw4OffUntilUs, fPendSeq[SWEET_FB_LAYERS], fWorkSeq[SWEET_FB_LAYERS];
	uint64_t            fCfT0Us, fCfBase[3 + SWEET_FB_HW_W_COUNT_ENUM], fCfLastCpm;
	uint32_t    hwCap(void) const;
	void        hw4Init(void);
	bool        hwCandidate(const uint8_t *rec, SweetSurface *const surfaces[SWEET_FB_LAYERS]);
	void        scalingPublish(bool on);
	void        hw4Off(const char *why);
	bool        hwFence5(IOTimerEventSource *sender);
	void        hwCpuFree(uint64_t nowUs);
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
		uint64_t cpu_fault, fence_wait, fence_forced, fence_resv, scaled, l4_frames, l4_faults;      /* 0402 */
		int last_fault;
	} fHwSt;
	bool        hwInit(void);
	unsigned    hwInputs(const uint8_t *rec, SweetSurface *const surfaces[SWEET_FB_LAYERS], struct sweet_fb_hw_in in[SWEET_FB_HW_MAX_IN],
	    SweetSurface *ss[SWEET_FB_HW_MAX_IN], uint32_t ids[SWEET_FB_HW_MAX_IN], int *why);
	int         hwFrameTry(uint32_t mode);
	bool        hwWriteRegs(volatile uint32_t *o, unsigned k, const struct sweet_fb_hw_reg *r, unsigned n, uint32_t *badOff);
	bool        hwApply(const struct sweet_fb_hw_plan *p, const uint32_t *ids = NULL, const int32_t *pids = NULL, unsigned nids = 0);   /* 0414: layer ids / producer pids for the CON log */
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
	bool        hwFenceBusy(void);
	void        hwFrameEnd(bool shown, bool timeout, bool legacy);
	int         hwStat(char *buf, size_t len);
	void        hwLogSurface(SweetSurface *s, uint32_t id);

	/* 0495: panel brightness.  fBl / sweet_bl_cfg_g: fBlLock.  The DCS 0x51 goes out between two frames (presentDone, the presenter
	 * owns the engine) or from the vsync thread while the presenter is idle (fPState 7 claims the engine for that one packet). */
	struct sweet_bl     fBl;
	IOLock             *fBlLock;
	thread_call_t       fBlThermCall;
	uint32_t            fBlLogN;
	uint64_t            fBlSumUs, fBlSumSends;
	void        blRequest(int32_t value, const char *who, bool raw = false, int32_t fade = 0);      /* 0501: raw = legacy "brightness" */
	void        blService(bool owned);
	void        blThermTick(void);
	int         blStat(char *buf, size_t len);
	uint8_t    *snapHw(const uint32_t want[6], uint32_t info[8]);      /* 0640 */
	void        statusWords(uint32_t w[4]);                          /* 0640 */
	void        blConfigChanged(void);
	IOReturn setProperties(OSObject *properties) APPLE_KEXT_OVERRIDE;

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

/*
 * 0495 (bright-fix-20261114): the brightness slider.  Until now nothing in the registry had backlight-control = true (boot log:
 * "IOK backboardd: matching ... backlight-control ... -> 0 found"), so CoreBrightness had no display module and the slider, Control
 * Center and Settings > Display & Brightness moved nothing.  CoreBrightness keeps a service only if it conforms to the class
 * AppleARMBacklight (-[CBDisplayContaineriOS findBacklight]), reads IODisplayParameters.brightness from it and then sets
 * "brightness-nits" (nits 16.16) on it; it also sets IOMFBBrightnessLevel (the same number) on the framebuffer.  Both end in
 * AppleCLCD::blRequest -> DCS 0x51 (sweet_fb_backlight.h).
 * Kill switches: boot-arg n2bl=0 = the service is not published (exactly the old behaviour); sysctl kern.n2bl=0 = requests are
 * recorded, nothing is sent (the panel keeps its level).  kern.n2bl_floor / kern.n2bl_max (DBV 4..2047), kern.n2bl_nits (nits at
 * DBV 2047, 100..1000), kern.n2blstat (read only).  Thermal cap from the battery gauge: >= 42 C 70 %, >= 45 C 50 % (1 C hysteresis).
 */
static uint64_t sweet_fb_abs_to_us(uint64_t abs);     /* below */
static struct sweet_bl_cfg sweet_bl_cfg_g = { 1, SWEET_BL_FLOOR_DEFAULT, SWEET_BL_MAX_DEFAULT, SWEET_BL_NITS_FULL, SWEET_BL_SAFE_DEFAULT, 0 };
/*
 * 0501 (bltorch-20261114): q10jch on the phone: the service was found, CoreBrightness set "brightness-nits" 0.00 once and nothing
 * when the slider moved.  Cause (disassembly, see sweet_fb_backlight.h): without "new-backlight-architecture" = true on this
 * service CoreBrightness runs its LEGACY path and sets {"brightness", "FadePeriod", "backlight-level"} here -- keys 0495 dropped.
 * Now "brightness" (0..2047, the range IODisplayParameters publishes) drives the panel.  boot-arg n2bl: 0 no service, 1 (default)
 * legacy architecture, 2 (or n2gdv +64, e.g. n2gdv=73) = "new-backlight-architecture" published too (CoreBrightness then sends nits;
 * untested).  In the legacy
 * architecture the nits keys are counted and ignored.  kern.n2bl_safe (DBV 300): nothing below it until one request above it.
 */
static uint32_t sweet_bl_other_logged;

/* the kernel's own macros (OSDefineMetaClassAndStructors, SYSCTL_PROC) warn under -Weverything; same warnings as the existing ones */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier"
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#pragma clang diagnostic ignored "-Wcast-function-type-strict"
#pragma clang diagnostic ignored "-Wreorder-init-list"
#pragma clang diagnostic ignored "-Wc99-designator"
class AppleARMBacklight : public IOService {
	OSDeclareDefaultStructors(AppleARMBacklight);
public:
	AppleCLCD *fFB;
	IOReturn setProperties(OSObject *properties) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(AppleARMBacklight, IOService);
#pragma clang diagnostic pop

/* a brightness number from userland: OSNumber, nits 16.16 (SInt32) */
static bool
sweet_bl_number(OSDictionary *d, const char *key, int32_t *q16)
{
	OSNumber *n = d != NULL ? OSDynamicCast(OSNumber, d->getObject(key)) : NULL;
	if (n == NULL) { return false; }
	*q16 = (int32_t)n->unsigned32BitValue();
	return true;
}

IOReturn
AppleARMBacklight::setProperties(OSObject *properties)
{
	OSDictionary *d = OSDynamicCast(OSDictionary, properties);
	int32_t q16 = 0;
	if (d == NULL || fFB == NULL) { return kIOReturnBadArgument; }
	bool known = false;
	if (sweet_bl_number(d, "brightness-nits", &q16)) {
		fFB->blRequest(q16, "brightness-nits");
		OSNumber *n = OSNumber::withNumber((unsigned long long)(uint32_t)q16, 32);
		if (n != NULL) { setProperty("brightness-nits", n); n->release(); }
		known = true;
	}
	int32_t raw = 0, fade = 0;
	if (sweet_bl_number(d, "brightness", &raw)) {      /* 0501: the legacy path (__DisplaySetPhysicalBrightnessLegacyOnThread) */
		(void)sweet_bl_number(d, "FadePeriod", &fade);
		fFB->blRequest(raw, "brightness", true, fade);
		known = true;
	}
	/* everything else CoreBrightness may set (brightness-nits-cap, DisplayBrightnessSliderPosition, ...) is accepted and ignored;
	 * 0501: the first few such dictionaries are named in the log (what else arrives here was not visible in q10jch's log) */
	if (!known && sweet_bl_other_logged < 12) {
		OSCollectionIterator *it = OSCollectionIterator::withCollection(d);
		OSSymbol *k = it != NULL ? OSDynamicCast(OSSymbol, it->getNextObject()) : NULL;
		OSNumber *v = k != NULL ? OSDynamicCast(OSNumber, d->getObject(k)) : NULL;
		sweet_bl_other_logged++;
		IOLog("NORD2 BL: property \"%s\" %s 0x%llx (%u keys in the request) accepted, not used\n", k != NULL ? k->getCStringNoCopy() : "?",
		    v != NULL ? "number" : "not a number", v != NULL ? v->unsigned64BitValue() : 0ULL, d->getCount());
		OSSafeReleaseNULL(it);
	}
	return kIOReturnSuccess;
}

IOReturn
AppleCLCD::setProperties(OSObject *properties)
{
	OSDictionary *d = OSDynamicCast(OSDictionary, properties);
	int32_t q16 = 0;
	if (d != NULL && sweet_bl_number(d, "IOMFBBrightnessLevel", &q16)) {
		blRequest(q16, "IOMFBBrightnessLevel");
		return kIOReturnSuccess;
	}
	return IOMobileFramebuffer::setProperties(properties);
}

void
AppleCLCD::blRequest(int32_t value, const char *who, bool raw, int32_t fade)
{
	if (fBlLock == NULL) { return; }
	IOLockLock(fBlLock);
	struct sweet_bl_cfg c = sweet_bl_cfg_g;
	bool was = fBl.trusted;
	uint32_t old = fBl.want_dbv, dbv = raw ? sweet_bl_request_raw(&fBl, &c, value) : sweet_bl_request(&fBl, &c, value);
	uint32_t floor = sweet_bl_floor(&fBl, &c);
	uint64_t ignored = fBl.nits_ignored;
	bool trust = fBl.trusted && !was;
	uint32_t n = fBlLogN < 24 && (dbv != old || trust) && dbv != 0 ? ++fBlLogN : 0;
	IOLockUnlock(fBlLock);
	if (dbv == 0) {      /* 0501: a nits key in the legacy architecture */
		if (ignored <= 4) {
			IOLog("NORD2 BL: %s %d.%02u nits not used (legacy architecture published: \"brightness\" is the level; n2bl=2 publishes the new one)\n",
			    who, value >> 16, (unsigned)(((uint32_t)value & 0xffffu) * 100u >> 16));
		}
		return;
	}
	if (n != 0 && raw) {
		IOLog("NORD2 BL: brightness %d of %u (FadePeriod %d) -> DBV %u (floor %u%s, max %u)%s\n", value, SWEET_BL_DBV_HW_MAX, fade, dbv, floor,
		    trust ? ": a level above kern.n2bl_safe was asked, the ordinary floor applies now" : "", c.max_dbv, n == 24 ? "; further requests in NORD2 BL SUM" : "");
	} else if (n != 0) {
		IOLog("NORD2 BL: %s %d.%02u nits -> DBV %u (floor %u max %u, %u nits at 2047, mode %u)%s\n", who, value >> 16,
		    (unsigned)(((uint32_t)value & 0xffffu) * 100u >> 16), dbv, floor, c.max_dbv, c.nits_full, c.mode, n == 24 ? "; further requests in NORD2 BL SUM" : "");
	}
}

/* owned: the presenter owns the engine (presentDone).  Otherwise the engine is claimed with fPState 7 if the presenter is idle. */
void
AppleCLCD::blService(bool owned)
{
#if defined(NORD2)
	if (fBlLock == NULL || fDsi == NULL || !fNbReady) { return; }
	uint64_t nowUs = sweet_fb_abs_to_us(mach_absolute_time());
	IOLockLock(fBlLock);
	struct sweet_bl_cfg c = sweet_bl_cfg_g;
	uint32_t dbv = sweet_bl_due(&fBl, &c, nowUs);
	IOLockUnlock(fBlLock);
	if (dbv == 0) { return; }
	if (!owned) {
		IOLockLock(fLock);
		if (fPowerOff || fPState != 0 || fPanelBlank) { IOLockUnlock(fLock); return; }
		fPState = 7;
		IOLockUnlock(fLock);
	}
	uint8_t p[3];
	sweet_bl_packet(dbv, p);
	bool ok = dsiDcs(p, 3);
	if (!owned) {
		IOLockLock(fLock);
		fPState = 0;
		IOLockUnlock(fLock);
	}
	IOLockLock(fBlLock);
	sweet_bl_sent(&fBl, dbv, ok, nowUs);
	uint64_t sends = fBl.sends, fails = fBl.fails, reqs = fBl.reqs, rawReqs = fBl.raw_reqs, nitsReqs = fBl.nits_reqs, nitsIgn = fBl.nits_ignored;
	uint32_t want = fBl.want_dbv, therm = fBl.therm;
	bool sum = nowUs - fBlSumUs >= 10000000ull && sends != fBlSumSends;
	if (sum) { fBlSumUs = nowUs; fBlSumSends = sends; }
	IOLockUnlock(fBlLock);
	if (!ok && (fails <= 3 || (fails % 256) == 0)) {
		IOLog("NORD2 BL: DCS 0x51 %02x %02x (DBV %u) FAILED (fails %llu), retried\n", p[1], p[2], dbv, fails);
	} else if (ok && (sends <= 4 || sum)) {
		IOLog("NORD2 BL SUM: DBV %u sent (want %u, thermal level %u, %s) requests %llu (brightness %llu, nits %llu of them %llu not used) sends %llu fails %llu\n",
		    dbv, want, therm, owned ? "between frames" : "idle panel", reqs, rawReqs, nitsReqs, nitsIgn, sends, fails);
	}
#else
	(void)owned;
#endif
}

#if defined(NORD2)
extern "C" bool nord2_battery_temperature_cC(int *centi);     /* Nord2GPU.cpp */
#endif

static void
sweet_bl_therm_call(thread_call_param_t p0, __unused thread_call_param_t p1)
{
	((AppleCLCD *)p0)->blThermTick();
}

/* thread call, every 5 s: the thermal cap (never from the vsync or present thread: the gauge lookup takes registry locks) */
void
AppleCLCD::blThermTick(void)
{
#if defined(NORD2)
	int cc = 0;
	bool ok = nord2_battery_temperature_cC(&cc);
	uint64_t nowUs = sweet_fb_abs_to_us(mach_absolute_time());
	IOLockLock(fBlLock);
	bool changed = sweet_bl_therm_update(&fBl, ok, cc, nowUs);
	uint32_t lvl = fBl.therm, cap = sweet_bl_cap(&fBl);
	IOLockUnlock(fBlLock);
	if (changed) {
		IOLog("NORD2 BL: thermal level %u at battery %d.%02d C: brightness cap DBV %u\n", lvl, cc / 100, (cc < 0 ? -cc : cc) % 100, cap);
	}
	uint64_t deadline = 0;
	clock_interval_to_deadline(5000, NSEC_PER_MSEC, &deadline);
	if (!__atomic_load_n(&fSwapLifecycle.stopped, __ATOMIC_ACQUIRE)) { thread_call_enter_delayed(fBlThermCall, deadline); }
#endif
}

void
AppleCLCD::blConfigChanged(void)
{
	if (fBlLock == NULL) { return; }
	IOLockLock(fBlLock);
	sweet_bl_remap(&fBl, &sweet_bl_cfg_g);
	IOLockUnlock(fBlLock);
}

int
AppleCLCD::blStat(char *buf, size_t len)
{
	if (fBlLock == NULL) { return snprintf(buf, len, "no backlight"); }
	IOLockLock(fBlLock);
	struct sweet_bl b = fBl;
	struct sweet_bl_cfg c = sweet_bl_cfg_g;
	IOLockUnlock(fBlLock);
	return snprintf(buf, len, "mode=%u arch=%s req_q16=%d req_raw=%u kind=%u ask=%u want=%u sent=%u target=%u floor=%u safe=%u trusted=%u max=%u nits=%u "
	    "therm=%u bat_cC=%d reqs=%llu raw_reqs=%llu nits_reqs=%llu nits_ignored=%llu sends=%llu fails=%llu floored=%llu capped=%llu therm_changes=%llu",
	    c.mode, c.arch ? "new" : "legacy", b.req_q16, b.req_raw, b.req_kind, b.ask_dbv, b.want_dbv, b.sent_dbv, sweet_bl_target(&b, &c),
	    c.floor_dbv, c.safe_dbv, b.trusted ? 1u : 0u, c.max_dbv, c.nits_full, b.therm, b.therm_cc, b.reqs, b.raw_reqs, b.nits_reqs, b.nits_ignored,
	    b.sends, b.fails, b.floored, b.capped, b.therm_changes);
}

static int
sweet_bl_sysctl_cfg SYSCTL_HANDLER_ARGS
{
#pragma unused(arg1)
	AppleCLCD *fb = gSweetFBSnapOwner;
	if (fb == NULL || fb->fBlLock == NULL) { return ENODEV; }
	IOLockLock(fb->fBlLock);
	struct sweet_bl_cfg c = sweet_bl_cfg_g;
	IOLockUnlock(fb->fBlLock);
	uint32_t *field = arg2 == 0 ? &c.mode : arg2 == 1 ? &c.floor_dbv : arg2 == 2 ? &c.max_dbv : arg2 == 4 ? &c.safe_dbv : &c.nits_full;
	int v = (int)*field, changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < 0 || v > 4095) { return EINVAL; }
	*field = (uint32_t)v;
	sweet_bl_cfg_fix(&c);
	IOLockLock(fb->fBlLock);
	sweet_bl_cfg_g = c;
	IOLockUnlock(fb->fBlLock);
	fb->blConfigChanged();
	IOLog("NORD2 BL: sysctl %s: mode %u floor %u max %u nits %u safe %u\n", oidp->oid_name, c.mode, c.floor_dbv, c.max_dbv, c.nits_full, c.safe_dbv);
	return 0;
}
static int
sweet_bl_sysctl_stat SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	char buf[640];
	buf[0] = 0;
	if (gSweetFBSnapOwner != NULL) { (void)gSweetFBSnapOwner->blStat(buf, sizeof(buf)); } else { snprintf(buf, sizeof(buf), "no framebuffer"); }
	return sysctl_io_string(req, buf, sizeof(buf), 0, NULL);
}
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier"
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#pragma clang diagnostic ignored "-Wcast-function-type-strict"
SYSCTL_PROC(_kern, OID_AUTO, n2bl, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_bl_sysctl_cfg, "I",
    "0495 panel brightness: 1 act on CoreBrightness requests (DCS 0x51), 0 record only");
SYSCTL_PROC(_kern, OID_AUTO, n2bl_floor, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 1, sweet_bl_sysctl_cfg, "I",
    "0495 lowest DBV sent (4..2047, default 16)");
SYSCTL_PROC(_kern, OID_AUTO, n2bl_max, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 2, sweet_bl_sysctl_cfg, "I",
    "0495 highest DBV sent (4..2047, default 2047; HBM never)");
SYSCTL_PROC(_kern, OID_AUTO, n2bl_nits, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 3, sweet_bl_sysctl_cfg, "I",
    "0495 nits that map to DBV 2047 (100..1000, default 400)");
SYSCTL_PROC(_kern, OID_AUTO, n2bl_safe, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 4, sweet_bl_sysctl_cfg, "I",
    "0501 lowest DBV shown until a request above it has been seen (0..2047, default 300; 0 = off)");
SYSCTL_PROC(_kern, OID_AUTO, n2blstat, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, NULL, 0, sweet_bl_sysctl_stat, "A",
    "0495 brightness state and counters");
#pragma clang diagnostic pop

#if defined(NORD2)
/* 0396: runtime control (no boot-arg: the UI command line is at its 607-character limit) */
static int
sweet_fb_hw_sysctl_mode SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int v = (int)__atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED), changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < (int)SWEET_FB_HW_SINGLE || v > (int)SWEET_FB_HW_MODE_MAX) { return EINVAL; }      /* 0416: 0 off / 1 dry run are gone (no CPU path) */
	uint32_t old = __atomic_exchange_n(&sweet_fb_hw_mode, (uint32_t)v, __ATOMIC_RELAXED);
	__atomic_fetch_add(&sweet_fb_hw_mode_gen, 1u, __ATOMIC_RELAXED);      /* 0397 */
	IOLog("NORD2 FB HW: mode %u -> %d (sysctl kern.n2fbhw: 2 single layer, 3 multi layer; no CPU path)\n", old, v);
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
sweet_fb_hw_sysctl_layers SYSCTL_HANDLER_ARGS      /* 0402 */
{
#pragma unused(oidp, arg1, arg2)
	int v = (int)__atomic_load_n(&sweet_fb_hw_layers, __ATOMIC_RELAXED), changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < 2 || v > (int)SWEET_FB_HW_NSLOT) { return EINVAL; }
	__atomic_store_n(&sweet_fb_hw_layers, (uint32_t)v, __ATOMIC_RELAXED);
	IOLog("NORD2 FB HW: kern.n2fbhwlayers %d\n", v);
	return 0;
}
static int
sweet_fb_h6_sysctl SYSCTL_HANDLER_ARGS      /* 0402b */
{
#pragma unused(oidp, arg1, arg2)
	int v = (int)__atomic_load_n(&sweet_fb_h6_mode, __ATOMIC_RELAXED), changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < 0 || v > 1) { return EINVAL; }
	__atomic_store_n(&sweet_fb_h6_mode, (uint32_t)v, __ATOMIC_RELAXED);
	IOLog("NORD2 FB HW: kern.n2fbh6 %d (H6 fence in modes 2/3)\n", v);
	return 0;
}
static int
sweet_fb_scaling_sysctl SYSCTL_HANDLER_ARGS      /* 0402b */
{
#pragma unused(oidp, arg1, arg2)
	int v = (int)__atomic_load_n(&sweet_fb_scaling_mode, __ATOMIC_RELAXED), changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (v < 0 || v > 1) { return EINVAL; }
	__atomic_store_n(&sweet_fb_scaling_mode, (uint32_t)v, __ATOMIC_RELAXED);
	if (gSweetFBSnapOwner != NULL) { gSweetFBSnapOwner->scalingPublish(v != 0); }
	IOLog("NORD2 FB HW: kern.n2fbscaling %d (IOMFBScalingLimits %s; CoreAnimation reads it when backboardd starts)\n", v, v ? "1:1 published" : "removed");
	return 0;
}
static int
sweet_fb_hw_sysctl_stat SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	char buf[1024];
	buf[0] = 0;
	if (gSweetFBSnapOwner != NULL) { (void)gSweetFBSnapOwner->hwStat(buf, sizeof(buf)); }
	else { snprintf(buf, sizeof(buf), "no framebuffer"); }
	return sysctl_io_string(req, buf, sizeof(buf), 0, NULL);
}
SYSCTL_PROC(_kern, OID_AUTO, n2fbhw, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_fb_hw_sysctl_mode, "I",
    "0396/0416 overlay composition: 2 single layer, 3 multi-layer up to kern.n2fbhwlayers (no CPU path)");
SYSCTL_PROC(_kern, OID_AUTO, n2fbhwpm, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_fb_hw_sysctl_pm, "I",
    "0396/0399 upper-layer CON: 0 straight 0x21ff, 1 legacy 0x008031ff, 2 premultiplied BGRA 0x028031ff, 3 DRM-table 0x038031ff");
SYSCTL_PROC(_kern, OID_AUTO, n2fbhwlayers, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_fb_hw_sysctl_layers, "I",
    "0402 overlay slots per pipe 2..6");
SYSCTL_PROC(_kern, OID_AUTO, n2fbh6, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_fb_h6_sysctl, "I", "0402b H6 fence 0/1");
SYSCTL_PROC(_kern, OID_AUTO, n2fbscaling, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_fb_scaling_sysctl, "I", "0402b IOMFBScalingLimits 0/1");
SYSCTL_PROC(_kern, OID_AUTO, n2fbhwcsw, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_fb_hw_sysctl_csw, "I",
    "0399 flip CON BYTE_SWAP (R<->B): bit 0 layer 1, bit 1 layer 0");
SYSCTL_PROC(_kern, OID_AUTO, n2fbhwstat, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, NULL, 0, sweet_fb_hw_sysctl_stat, "A",
    "0396 HW composition counters");
#include "sweet_fb_yuv_kern.inc"      /* 0490: kern.n2fbyuv */
/*
 * 0550 (cutout-20261114): status bar band rotation around the top-left punch hole (sweet_fb_cutout.h).  kern.n2cut 0/1 (kill switch;
 * boot: n2cut=0 or n2gdv +512, e.g. n2gdv=521), kern.n2cut_dx (left ear shift, panel px, default 72), kern.n2cut_band (rows, 114),
 * kern.n2cut_ear (left ear end, 227), kern.n2cut_tdx (right ear shift left, 24; 0 = off), kern.n2cut_tear (right ear start, 854),
 * kern.n2cutstat (read only).  Applied from the next frame; an invalid combination is refused (EINVAL).
 */
extern "C" {
struct sweet_fb_cut_cfg sweet_fb_cut_g = SWEET_FB_CUT_DEFAULTS;
uint32_t sweet_fb_cut_live;
}
static uint64_t sweet_fb_cut_n[SWEET_FB_CUT_NREASON];      /* present thread only (read racily by kern.n2cutstat) */
static uint32_t sweet_fb_cut_logs;
static int
sweet_fb_cut_sysctl SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1)
	struct sweet_fb_cut_cfg c = sweet_fb_cut_g;
	int32_t *const f[6] = { NULL, &c.band, &c.lead_end, &c.lead_dx, &c.trail_start, &c.trail_dx };
	int v = arg2 == 0 ? static_cast<int>(c.on) : *f[arg2], changed = 0;
	int err = sysctl_io_number(req, v, sizeof(v), &v, &changed);
	if (err != 0 || !changed) { return err; }
	if (arg2 == 0) { if (v < 0 || v > 1) { return EINVAL; } c.on = static_cast<uint32_t>(v); }
	else { *f[arg2] = v; }
	struct sweet_fb_cut_rot rt[2];
	unsigned nr = 0;
	if (!sweet_fb_cut_rots(&c, 1080u, rt, &nr)) { return EINVAL; }
	sweet_fb_cut_g = c;
	IOLog("NORD2 FB CUT: on %u band %d ear %d dx %d | trail %d tdx %d (sysctl kern.n2cut*, from the next frame)\n", c.on, c.band, c.lead_end,
	    c.lead_dx, c.trail_start, c.trail_dx);
	return 0;
}
static int
sweet_fb_cut_sysctl_stat SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	char buf[256];
	struct sweet_fb_cut_cfg c = sweet_fb_cut_g;
	snprintf(buf, sizeof(buf), "on=%u band=%d ear=%d dx=%d trail=%d tdx=%d live=%u ok=%llu off=%llu config=%llu cover=%llu yuv=%llu slots=%llu layers=%llu",
	    c.on, c.band, c.lead_end, c.lead_dx, c.trail_start, c.trail_dx, __atomic_load_n(&sweet_fb_cut_live, __ATOMIC_RELAXED),
	    sweet_fb_cut_n[SWEET_FB_CUT_OK], sweet_fb_cut_n[SWEET_FB_CUT_OFF], sweet_fb_cut_n[SWEET_FB_CUT_CFG], sweet_fb_cut_n[SWEET_FB_CUT_COVER],
	    sweet_fb_cut_n[SWEET_FB_CUT_YUV], sweet_fb_cut_n[SWEET_FB_CUT_SLOTS], sweet_fb_cut_n[SWEET_FB_CUT_LAYERS]);
	return sysctl_io_string(req, buf, sizeof(buf), 0, NULL);
}
SYSCTL_PROC(_kern, OID_AUTO, n2cut, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_fb_cut_sysctl, "I", "0550 punch-hole band rotation 0/1");
SYSCTL_PROC(_kern, OID_AUTO, n2cut_band, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 1, sweet_fb_cut_sysctl, "I", "0550 band rows");
SYSCTL_PROC(_kern, OID_AUTO, n2cut_ear, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 2, sweet_fb_cut_sysctl, "I", "0550 left ear end x");
SYSCTL_PROC(_kern, OID_AUTO, n2cut_dx, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 3, sweet_fb_cut_sysctl, "I", "0550 left ear shift right");
SYSCTL_PROC(_kern, OID_AUTO, n2cut_tear, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 4, sweet_fb_cut_sysctl, "I", "0550 right ear start x");
SYSCTL_PROC(_kern, OID_AUTO, n2cut_tdx, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 5, sweet_fb_cut_sysctl, "I", "0550 right ear shift left");
SYSCTL_PROC(_kern, OID_AUTO, n2cutstat, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, NULL, 0, sweet_fb_cut_sysctl_stat, "A", "0550 counters");
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
	fPixels = 0;      /* 0416: the panel memory is never mapped for the CPU (the overlay reads it as the boot buffer only) */
	fWorkLoop = IOWorkLoop::workLoop();
	if (fLock == NULL || fClients == NULL || fSurface == NULL || fWorkLoop == NULL) {
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
	{ uint32_t rt = 1; if (PE_parse_boot_argn("n2fbretain", &rt, sizeof(rt))) { sweet_fb_retain_mode = rt ? 1 : 0; } }   /* 0392 */
	IOLog("SWEET FB: layers outside a swap's mask kept %s (0392)\n", sweet_fb_retain_mode ? "on" : "off (n2fbretain=0)");
	sweet_fb_pending_surfaces_init(&fHwHeld);       /* 0396 */
	bzero(&fHwSt, sizeof(fHwSt)); fHwInitDone = fHwInitOk = fHwOn = fWorkHw = false; fHwN = 0; fHwTestMem = NULL; fHwTestMap = NULL;
	fHwPlanN = fHwShownN = 0; fSnapMs = fSnapFlags = 0;      /* 0640 */
	fPanelBlank = false; fBlankFails = 0; fHwInitRetryN = 0; fHwInitRetryUs = 0; fHeldN = 0; bzero(fHeld, sizeof(fHeld));      /* 0413 */
	fOvl4[0] = fOvl4[1] = NULL; fHw4Ok = fHw4On = fHwUses4 = fHwForced = fHw4Tried = false; fHw4OffN = fWorkResv = fPendResv = 0; fHw4OffUntilUs = 0; fCfT0Us = 0; fCfLastCpm = 0;      /* 0402 */
	bzero(fPendSeq, sizeof(fPendSeq)); bzero(fWorkSeq, sizeof(fWorkSeq));
	bzero(&fFps, sizeof(fFps)); fFpsWinUs = 0;      /* 0407 */
	sweet_fb_iommu_boot();      /* 0411: display IOMMU default on, armed by a thread_call later */
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
	fJankN = 0; fJankLost = 0; fSecT0Us = 0; fSecFirstUs = 0; fSecN = 0; fSecCur = 0;      /* 0621 */
	fXferWaitAbs = 0; fWorkArrUs = fNextArrUs = 0; fXferUnd = false; fPanelReq = 0; fPaceResetReq = 0;
	fHwBackoffMode = fHwBackoffGen = fHwBackoffN = fHwRaceTotalRunMax = 0; fHwBackoffUntilUs = 0;
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
	if (__atomic_load_n(&sweet_fb_scaling_mode, __ATOMIC_RELAXED) != 0) { scalingPublish(true); }      /* 0402b: off by default */
	registerService();
	IOLog("SWEET FB: AppleCLCD %ux%u stride %u at 0x%llx, default surface %u, presented as %ux%u\n",
	    fWidth, fHeight, fRowBytes, fPhys, fSurface->id, fLogicalW, fLogicalH);
	gSweetFBSnapOwner = this;   /* 0296 */
	/* 0495: the backlight service CoreBrightness looks for (boot-arg n2bl=0: not published, the slider stays dead as before) */
	sweet_bl_init(&fBl); fBlLogN = 0; fBlSumUs = 0; fBlSumSends = 0;
	fBlLock = IOLockAlloc();
	fBlThermCall = fBlLock != NULL ? thread_call_allocate(sweet_bl_therm_call, this) : NULL;
	{
		uint32_t bl = 1, gdv = 0;
		/* 0501: n2gdv +64 = n2bl=2 (the boot line has 4 characters left with n2gdv=9: n2gdv=73 fits, " n2bl=2" does not) */
		if (!PE_parse_boot_argn("n2bl", &bl, sizeof(bl)) && PE_parse_boot_argn("n2gdv", &gdv, sizeof(gdv)) && (gdv & 64) != 0) { bl = 2; }
		AppleARMBacklight *b = (bl != 0 && fBlLock != NULL) ? OSTypeAlloc(AppleARMBacklight) : NULL;
		bool published = false;
		if (b != NULL && b->init()) {
			b->fFB = this;
			b->setProperty("backlight-control", true);
			if (bl >= 2) {      /* 0501: n2bl=2: CoreBrightness' nits path (DisplayCreateWithID tests this property for kCFBooleanTrue) */
				b->setProperty("new-backlight-architecture", kOSBooleanTrue);
				sweet_bl_cfg_g.arch = 1;
			}
			OSDictionary *params = OSDictionary::withCapacity(1), *br = OSDictionary::withCapacity(3);
			OSNumber *mn = OSNumber::withNumber(0ULL, 32), *mx = OSNumber::withNumber((unsigned long long)SWEET_BL_DBV_HW_MAX, 32),
			    *val = OSNumber::withNumber(1024ULL, 32);      /* initial slider position only: CoreBrightness restores the saved level */
			if (params != NULL && br != NULL && mn != NULL && mx != NULL && val != NULL) {
				br->setObject("min", mn); br->setObject("max", mx); br->setObject("value", val);
				params->setObject("brightness", br);
				b->setProperty("IODisplayParameters", params);
			}
			OSSafeReleaseNULL(mn); OSSafeReleaseNULL(mx); OSSafeReleaseNULL(val); OSSafeReleaseNULL(br); OSSafeReleaseNULL(params);
			if (b->attach(this)) { b->registerService(); published = true; }
		}
		OSSafeReleaseNULL(b);
		IOLog("NORD2 BL: AppleARMBacklight %s (boot-arg n2bl=%u; sysctl kern.n2bl %u, floor DBV %u, max DBV %u, %u nits = DBV 2047; DCS 0x51)\n",
		    published ? "published" : "NOT published", bl, sweet_bl_cfg_g.mode, sweet_bl_cfg_g.floor_dbv, sweet_bl_cfg_g.max_dbv, sweet_bl_cfg_g.nits_full);
		IOLog("NORD2 BL ARCH: %s (0501): the level comes from %s; safe floor DBV %u until a request above it (kern.n2bl_safe)\n",
		    sweet_bl_cfg_g.arch ? "new-backlight-architecture published (n2bl=2 / n2gdv +64)" : "legacy architecture",
		    sweet_bl_cfg_g.arch ? "\"brightness-nits\" / IOMFBBrightnessLevel (and \"brightness\")" : "\"brightness\" 0..2047 (nits keys not used)",
		    sweet_bl_cfg_g.safe_dbv);
		if (published && fBlThermCall != NULL) { blThermTick(); }
	}
	/*
	 * 0502 (bltorch-20261114): the flashlight button.  q10jch has /dev/redwood-torch (0496) but Control Center shows no flashlight:
	 * FlashlightModule.bundle's Info.plist requires the device capability "camera-flash", and libMobileGestalt (20H392, the answer
	 * function at 0x19c563598) says yes only when IODeviceTree:/product/camera has a property "flash".  The Nord 2 device tree's
	 * /product (copied from the iPhone X's) has properties and no children.  The node is made here, with that one property, when the
	 * torch is not switched off (boot-arg n2torch=0: no device node, no capability).  AVFlashlight asks the same question.
	 * MobileGestalt keeps this answer in its cache file: the old "no" has to be changed there too (bltorch-20261114 guest flash1).
	 */
	{
		extern const IORegistryPlane *gIODTPlane;      /* IOKit/IODeviceTreeSupport.h (not included: its includes add header-search warnings) */
		uint32_t torch = 1;
		(void)PE_parse_boot_argn("n2torch", &torch, sizeof(torch));
		IORegistryEntry *product = IORegistryEntry::fromPath("/product", gIODTPlane);
		IORegistryEntry *cam = product != NULL ? product->childFromPath("camera", gIODTPlane) : NULL;
		const char *what = torch == 0 ? "not made (boot-arg n2torch=0)" : product == NULL ? "NOT made (no /product)" : cam != NULL ? "was there" : "NOT made";
		if (torch != 0 && product != NULL) {
			if (cam == NULL) {
				cam = OSTypeAlloc(IORegistryEntry);
				if (cam != NULL && !cam->init()) { OSSafeReleaseNULL(cam); }
				if (cam != NULL) {
					cam->setName("camera");
					(void)cam->setProperty("name", (void *)(uintptr_t)"camera", (unsigned int)sizeof("camera"));
					if (cam->attachToParent(product, gIODTPlane)) { what = "made"; } else { OSSafeReleaseNULL(cam); }
				}
			}
			if (cam != NULL) {
				uint32_t one = 1;
				(void)cam->setProperty("flash", &one, (unsigned int)sizeof(one));
			}
		}
		IOLog("NORD2 TORCH CAP: IODeviceTree:/product/camera %s, flash %s (0502: MobileGestalt camera-flash, Control Center flashlight)\n",
		    what, cam != NULL && torch != 0 ? "= 1" : "not set");
		OSSafeReleaseNULL(cam);
		OSSafeReleaseNULL(product);
	}
	/*
	 * 0610 (haptic-20261114): IODeviceTree:/product/haptics { closed-loop, supports-3rd-party-haptics }.  libMobileGestalt 20H392:
	 * DeviceSupportsHaptics = the node exists (answer 0x19c576454), DeviceSupportsClosedLoopHaptics = its property "closed-loop"
	 * (0x19c564a9c), "haptics" = "supports-3rd-party-haptics" (0x19c56ed8c).  Closed loop makes FigVibrator / CoreHaptics / UIKit
	 * play every haptic as a waveform on the HAL plug-in's "Actuator" device, which RedwoodAudio.driver writes to /dev/redwood-haptic
	 * (bsd/dev/nord2_haptic.inc).  Same layout as the D22AP /product/haptics and HackiOS mkidentity.py.  Not made with n2hap=0 or
	 * n2gdv +1024.  The cached "no" answers are in MobileGestalt's cache file (guest haptic1).
	 */
	{
		extern const IORegistryPlane *gIODTPlane;
		uint32_t hap = 1;
		int gdv = 0;
		if (!PE_parse_boot_argn("n2hap", &hap, sizeof(hap)) && PE_parse_boot_argn("n2gdv", &gdv, sizeof(gdv)) && (gdv & 1024) != 0) { hap = 0; }
		IORegistryEntry *product = IORegistryEntry::fromPath("/product", gIODTPlane);
		IORegistryEntry *hn = product != NULL ? product->childFromPath("haptics", gIODTPlane) : NULL;
		const char *what = hap == 0 ? "not made (n2hap=0 / n2gdv +1024)" : product == NULL ? "NOT made (no /product)" : hn != NULL ? "was there" : "NOT made";
		if (hap != 0 && product != NULL) {
			if (hn == NULL) {
				hn = OSTypeAlloc(IORegistryEntry);
				if (hn != NULL && !hn->init()) { OSSafeReleaseNULL(hn); }
				if (hn != NULL) {
					hn->setName("haptics");
					(void)hn->setProperty("name", (void *)(uintptr_t)"haptics", (unsigned int)sizeof("haptics"));
					if (hn->attachToParent(product, gIODTPlane)) { what = "made"; } else { OSSafeReleaseNULL(hn); }
				}
			}
			if (hn != NULL) {
				uint32_t one = 1;
				(void)hn->setProperty("closed-loop", &one, (unsigned int)sizeof(one));
				(void)hn->setProperty("supports-3rd-party-haptics", &one, (unsigned int)sizeof(one));
			}
		}
		IOLog("NORD2 HAPTIC CAP: IODeviceTree:/product/haptics %s%s (0610: MobileGestalt DeviceSupportsHaptics / ClosedLoopHaptics)\n",
		    what, hn != NULL && hap != 0 ? ", closed-loop = 1, supports-3rd-party-haptics = 1" : "");
		OSSafeReleaseNULL(hn);
		OSSafeReleaseNULL(product);
	}
	return true;
}

/*
 * 0640: debug screenshot in the overlay modes (0416 removed the CPU read of the panel buffer).  On a USB autotest request only
 * (bsd/dev/nord2_autotest.inc command 3, its own thread): the plan on the panel (fHwShownPlan, its layer ranges and surfaces) is
 * copied under fLock, the surfaces are retained, the lock is dropped at once, and the picture is composed from that copy in software
 * (sweet_fb_snapc.h; slots it cannot read or model are painted magenta).  Nothing runs per frame: display operation stays CPU-free.
 * kern.n2snap = 0 refuses (kill switch).  NULL with info[3] = reason: 20 off, 21 no overlay frame shown yet, 22 no memory.
 * info: [0] w [1] h [2] bytes [3] scale | packed << 4 [4] slots composed [5] SWEET_FB_SNAPC_F_* [6] presents during [7] panel
 * (bit 0 iOS asked display off, bit 1 DCS display off sent = dark).
 */
static uint32_t sweet_fb_snap_mode = 1;      /* kern.n2snap */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier"
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#pragma clang diagnostic ignored "-Wcast-function-type-strict"
SYSCTL_UINT(_kern, OID_AUTO, n2snap, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_fb_snap_mode, 0, "0640 debug screenshot of the overlay plan (USB autotest) 1 on / 0 off");
#pragma clang diagnostic pop
extern "C" int sweet_surface_snap_read(void *surface, uint64_t off, void *dst, uint32_t n);      /* SweetSurface.cpp (0640) */
struct sweet_fb_snap_ctx { SweetSurface *s[SWEET_FB_HW_MAX_IN]; };
static int
sweet_fb_snap_read(void *ctx, int src, uint64_t off, void *dst, uint32_t n)
{
	struct sweet_fb_snap_ctx *c = (struct sweet_fb_snap_ctx *)ctx;
	if (src < 0 || src >= (int)SWEET_FB_HW_MAX_IN || c->s[src] == NULL) { return 1; }
	return sweet_surface_snap_read(c->s[src], off, dst, n);
}

uint8_t *
AppleCLCD::snapHw(const uint32_t want[6], uint32_t info[8])
{
	static uint8_t *buf, *tmp;
	static uint32_t *line;
	if (__atomic_load_n(&sweet_fb_snap_mode, __ATOMIC_RELAXED) == 0) { info[3] = 20; return NULL; }
	if (buf == NULL) {
		buf = (uint8_t *)IOMallocData((vm_size_t)sweet_fb_snapc_bound(fWidth, fHeight));
		line = (uint32_t *)IOMallocData((vm_size_t)fWidth * 4u + 64u);
		tmp = (uint8_t *)IOMallocData(SWEET_FB_SNAPC_TMP_BYTES);
	}
	if (buf == NULL || line == NULL || tmp == NULL || fWidth > 1080u * 2u) { info[3] = 22; return NULL; }
	struct sweet_fb_hw_plan plan;
	struct sweet_fb_snapc_src src[SWEET_FB_HW_MAX_IN];
	struct sweet_fb_snap_ctx ctx;
	bzero(&ctx, sizeof(ctx));
	bzero(src, sizeof(src));
	IOLockLock(fLock);      /* only the copy is done under the lock (a few hundred bytes) */
	unsigned n = (fHwOn && fHwShownN <= SWEET_FB_HW_MAX_IN) ? fHwShownN : 0;
	plan = fHwShownPlan;
	for (unsigned i = 0; i < n; i++) {
		src[i].dev = fHwShownIn[i].pa; src[i].len = fHwShownIn[i].len;
		ctx.s[i] = fHwShownS[i];
		if (ctx.s[i] != NULL) { ctx.s[i]->retain(); }
	}
	uint32_t panel = (fPowerOff ? 1u : 0u) | (fPanelBlank ? 2u : 0u), presents = fPresents;
	IOLockUnlock(fLock);
	if (n == 0) { info[3] = 21; info[7] = panel; return NULL; }
	uint64_t t0 = mach_absolute_time();
	struct sweet_fb_snapc c;
	(void)sweet_fb_snapc_prepare(&c, &plan, src, n, fWidth, fHeight);
	uint32_t pinfo[4];
	sweet_fb_snapc_picture(&c, want, fWidth, fHeight, buf, line, tmp, sweet_fb_snap_read, &ctx, pinfo);
	for (unsigned i = 0; i < n; i++) { if (ctx.s[i] != NULL) { ctx.s[i]->release(); } }
	uint32_t slots = 0;
	for (unsigned k = 0; k < SWEET_FB_HW_PIPES; k++) { slots += (uint32_t)__builtin_popcount(plan.src_con[k] & 0x3fu); }
	uint32_t flags = c.flags | (__atomic_load_n(&sweet_fb_cut_live, __ATOMIC_RELAXED) ? SWEET_FB_SNAPC_F_CUT : 0u) |
	    ((panel & 1u) ? SWEET_FB_SNAPC_F_PANEL_OFF : 0u) | ((panel & 2u) ? SWEET_FB_SNAPC_F_BLANK : 0u);
	info[0] = pinfo[0]; info[1] = pinfo[1]; info[2] = pinfo[2]; info[3] = pinfo[3];
	info[4] = slots; info[5] = flags & 0xffu; info[6] = fPresents - presents; info[7] = panel;
	uint64_t snap_ns = 0;
	absolutetime_to_nanoseconds(mach_absolute_time() - t0, &snap_ns);
	fSnapMs = (uint32_t)(snap_ns / 1000000u); fSnapFlags = flags;
	static unsigned said;
	if (said < 20 || (flags & SWEET_FB_SNAPC_F_MARKED)) {
		said++;
		IOLog("NORD2 SNAP: %ux%u scale %u from %u layers / %u slots in %u ms, flags %#x (%u marked magenta), panel %s%s\n", pinfo[0], pinfo[1],
		    pinfo[3] & 15u, n, slots, fSnapMs, flags, c.marked, (panel & 2u) ? "dark (DCS off)" : "on", (panel & 1u) ? ", iOS asked display off" : "");
	}
	return buf;
}

/* 0640: cheap state words for the autotest status (racy relaxed reads, no lock): [0] panel/flags, [1] DBV sent | target << 16,
 * [2] brightness request nits 16.16 (-1 none), [3] presents */
void
AppleCLCD::statusWords(uint32_t w[4])
{
	struct sweet_bl_cfg cfg = sweet_bl_cfg_g;
	uint32_t sent = __atomic_load_n(&fBl.sent_dbv, __ATOMIC_RELAXED);
	struct sweet_bl b = fBl;      /* racy copy: only the fields below are used */
	w[0] = (fPowerOff ? 1u : 0u) | (fPanelBlank ? 2u : 0u) | (fHwOn ? 4u : 0u) | (__atomic_load_n(&sweet_fb_cut_live, __ATOMIC_RELAXED) ? 8u : 0u) |
	    16u | ((fSnapFlags & 0xffu) << 8) | ((fSnapMs > 0xffffu ? 0xffffu : fSnapMs) << 16);
	w[1] = (sent & 0xffffu) | ((sweet_bl_target(&b, &cfg) & 0xffffu) << 16);
	w[2] = (uint32_t)b.req_q16;
	w[3] = fPresents;
}

extern "C" void *sweet_fb_snap(const uint32_t want[6], uint32_t info[8]);
extern "C" void *
sweet_fb_snap(const uint32_t want[6], uint32_t info[8])
{
	if (gSweetFBSnapOwner == NULL) { info[3] = 21; return NULL; }
	return gSweetFBSnapOwner->snapHw(want, info);
}

extern "C" void sweet_fb_status_words(uint32_t w[4]);
extern "C" void
sweet_fb_status_words(uint32_t w[4])
{
	w[0] = w[1] = w[2] = w[3] = 0;
	if (gSweetFBSnapOwner != NULL) { gSweetFBSnapOwner->statusWords(w); }
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
#if defined(NORD2)
	if (fb->fPowerOff) {      /* 0416: display off = DCS 0x28 (panel dark); no CPU fade, no fallback; retried every vsync until it is sent */
		if (fb->fDiag.enabled) { __atomic_fetch_add(&fb->fDiag.off, 1, __ATOMIC_RELAXED); }
		bool blank = fb->fPanelBlank;
		IOLockUnlock(fb->fLock);
		if (sweet_fb_blank_action(true, blank) == SWEET_FB_BLANK_OFF && fb->fPState == 0) {
			static const uint8_t off[] = { 0x28 };
			bool ok = fb->dsiDcs(off, 1);
			if (ok) { fb->fPanelBlank = true; } else { fb->fBlankFails++; }
			if (ok || fb->fBlankFails <= 2 || (fb->fBlankFails % 900) == 0) {
				IOLog("NORD2 FB BLANK: DCS display off %s (fails %u)\n", ok ? "sent: panel dark" : "FAILED, retried at the next vsync", fb->fBlankFails);
			}
		}
		if (diag_report) { fb->reportDiagnostics("timer-off"); }
		return;
	}
#endif
	IOLockUnlock(fb->fLock);
	fb->blService(false);      /* 0495: brightness while the presenter is idle (claims the engine for one DCS) */
	/* 0155: the tick is only the fallback; swaps and a finished present hand over at once (handOver() also takes the GPU wait and the TE wait off this thread). */
	if (sweet_fb_flush_due(count, fb->fFlushTicks)) { fb->handOver(count); }
	fb->reapLate();
	if (diag_report) { fb->reportDiagnostics("timer-on"); }
}

/* 0407: a HW frame that shows re-rendered (or older) GPU content is a picture change for the FB SEEN / FB CADENCE gap histograms,
 * which the CPU-copy hash (noteDistinct) cannot feed in modes 2/3.  The different/same counts stay CPU-copy only. */
void
AppleCLCD::noteHwGen(int cls)
{
	if (cls != SWEET_FB_FPS_NEW && cls != SWEET_FB_FPS_BACK) { return; }
	uint64_t nowUs = sweet_fb_abs_to_us(mach_absolute_time());
	if (fDistinctLastUs) {
		uint64_t gap = nowUs - fDistinctLastUs;
		if (gap > fDistinctGapMaxUs) { fDistinctGapMaxUs = gap; }
		fGapBin[gap <= 20000 ? 0 : gap <= 34000 ? 1 : gap <= 67000 ? 2 : gap <= 100000 ? 3 : 4]++;
		fCad[gap <= 12500 ? 0 : gap <= 15000 ? 1 : gap <= 19000 ? 2 : gap <= 28000 ? 3 : 4]++;
	}
	fDistinctLastUs = nowUs;
}

/* 0413: display ownership from the early console (was done by the first CPU copy). */
bool
AppleCLCD::takeDisplay(void)
{
	if (fConsolePaused) { return true; }
#if defined(NORD2)
	if (!nord2_display_begin()) { return false; }
#endif
	int quiet = 0;
	fConsolePaused = true;
	if (PE_parse_boot_argn("sweetfb_noconsole", &quiet, sizeof(quiet)) && quiet != 0) { sweet_console_muted = 1; }
	IOLog("NORD2 FB HOLD: display taken from the early console (overlay-only composition, kern.n2fbhw %u)\n", __atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED));
	return true;
}

/* Takes the newest swap if the presenter is idle and starts it on the present thread (state 5: wait for the GPU, then copy, TE wait, transfer). */
void
AppleCLCD::handOver(uint64_t count)
{
	(void)count;
	if (fPresentTimer == NULL) { return; }
	IOLockLock(fLock);
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
	memcpy(fWorkSeq, fPendSeq, sizeof(fWorkSeq)); fWorkResv = fPendResv;      /* 0402: the H6 fence of this swap */
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
	if (fb->fPState == 0 || fb->fPState == 7) { return; }      /* 0495: 7 = the vsync thread sends a DCS 0x51 */
	if (__atomic_load_n(&fb->fSwapLifecycle.stopped, __ATOMIC_ACQUIRE)) {
		fb->presentDone(false, false, false);
		return;
	}
#if defined(NORD2)
	if (fb->fPState == 5 && sweet_fb_h6_on()) {      /* 0402b: only with kern.n2fbh6=1 */
		if (fb->hwFence5(sender)) { return; }      /* 0402 H6: waiting (or dropped for a newer swap) */
		fb->fPState = 1;
	}
	if (fb->fPState == 5) {
		/* 0155: the swap is presented when the GPU is done with it (polled here, not on the vsync thread); after SWEET_FB_BUSY_WAIT_US anyway */
		bool busy = false;
		for (uint32_t layer = 0; layer < SWEET_FB_LAYERS && !busy; layer++) {
			if (fb->fWorkSurfaces.object[layer] != NULL && nord2_gpu_surface_busy(fb->fWorkSurfaces.object[layer])) { busy = true; }
		}
		uint64_t waited = sweet_fb_abs_to_us(mach_absolute_time()) - fb->fWaitStartUs;
		if (busy && waited < SWEET_FB_BUSY_WAIT_US) { sender->setTimeoutUS(100); return; }
		if (busy && fb->hwCandidate(fb->fWorkRec, (SweetSurface *const *)fb->fWorkSurfaces.object)) {
			/* 0416 fence rule (kern.n2fbh6=0): the overlay never reads a surface a GPU write job is still filling, and the frame is not skipped:
			 * the present thread keeps waiting (100 us polls); only a NEWER swap (fDirty) replaces it. */
			if (!__atomic_load_n(&fb->fDirty, __ATOMIC_RELAXED)) {
				if (waited > 100000 && (fb->fHwSt.fence_skip++ % 64) == 0) {
					IOLog("NORD2 FB HW: fence wait %llu us on a layer the overlay would read (GPU write in flight), still waiting\n", waited);
				}
				sender->setTimeoutUS(100);
				return;
			}
			fb->fHwSt.why[SWEET_FB_HW_W_FENCE]++;
			fb->presentDone(false, false, false);      /* superseded: the newer swap is taken at once */
			return;
		}
		fb->fStats.busy_waits++; fb->fStats.busy_wait_us += waited;
		if (!busy) { fb->fStats.busy_wait_ok++; } else { __atomic_fetch_add(&fb->fStats.busy_forced, 1, __ATOMIC_RELAXED); }
		fb->fPState = 1;
	}
#endif
	if (fb->fPState == 1) {
		uint64_t t0 = mach_absolute_time();
		{
			uint64_t t0us = sweet_fb_abs_to_us(t0);
			uint64_t lat = t0us > fb->fSwapArrUs ? t0us - fb->fSwapArrUs : 0;
			fb->fLatBin[lat <= 8000 ? 0 : lat <= 16000 ? 1 : lat <= 25000 ? 2 : lat <= 33000 ? 3 : 4]++;
		}
		bool shown = true;
		int hw = 0;
		if (!fb->fConsolePaused && !fb->takeDisplay()) { fb->presentDone(false, false, false); return; }      /* 0413: before any frame */
		uint64_t why0[SWEET_FB_HW_W_COUNT_ENUM];
		memcpy(why0, fb->fHwSt.why, sizeof(why0));
#if defined(NORD2)
		/* 0416: the overlay is the only frame source: 1 = registers written; -1 = a GPU write on a layer is in flight: wait for the fence
		 * (state 5); 0 = not composable now: HELD (the last overlay frame stays in the panel GRAM; counted as a bug) */
		fb->fWorkHw = false;
		fb->hwBackoffPoll();      /* 0402: 4L capacity timer */
		hw = fb->hwFrameTry(__atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED));
		if (hw < 0) { fb->fWaitStartUs = sweet_fb_abs_to_us(mach_absolute_time()); fb->fPState = 5; sender->setTimeoutUS(100); return; }
		if (hw > 0) {
			fb->fWorkHw = true;
			fb->fWorkEmpty = false;
			fb->fCopyUs = 0;
			fb->fLastCopyLayers = fb->fHwN;
			/* the swap retires at FRAME_DONE (presentDone): the overlay reads its surfaces until then */
		}
#endif
		if (hw == 0) {
			int r = SWEET_FB_HW_W_STATE;
			for (int i = 0; i < (int)SWEET_FB_HW_W_COUNT_ENUM; i++) { if (fb->fHwSt.why[i] != why0[i]) { r = i; } }
			fb->fHeld[r]++; fb->fHeldN++;
			if (r != SWEET_FB_HW_W_NONE && (fb->fHeldN <= 16 || (fb->fHeldN % 600) == 0)) {
				IOLog("NORD2 FB HELD (bug counter): frame not composable (%s): last overlay frame kept (held %llu: memory %llu format %llu count %llu pitch %llu range %llu state %llu missing %llu)\n",
				    sweet_fb_hw_why_name(r), fb->fHeldN, fb->fHeld[SWEET_FB_HW_W_MEMORY], fb->fHeld[SWEET_FB_HW_W_FORMAT], fb->fHeld[SWEET_FB_HW_W_COUNT],
				    fb->fHeld[SWEET_FB_HW_W_PITCH], fb->fHeld[SWEET_FB_HW_W_RANGE], fb->fHeld[SWEET_FB_HW_W_STATE], fb->fHeld[SWEET_FB_HW_W_MISSING]);
			}
#if defined(NORD2)
			if (!fb->fNbReady) {
				IOLog("NORD2 FB HOLD: first push of the boot buffer as is (latches the panel link for the overlay; no copy)\n");
				IOSleep(50);
				int done = nord2_display_refresh();
				if (done && fb->fDsi != NULL && fb->fMutex != NULL) { fb->fNbReady = true; }
				IOLog("NORD2 FB HOLD: link %s\n", fb->fNbReady ? "latched" : "NOT latched (retried at the next frame)");
			}
#endif
			sweet_fb_fps_break(&fb->fFps);
			fb->fXferStart = mach_absolute_time();
			fb->presentDone(true, false, true);
			return;
		}
		uint64_t t1 = mach_absolute_time();
		(void)t0; (void)shown;
#if defined(NORD2)
		if (sweet_fb_blank_action(false, fb->fPanelBlank) == SWEET_FB_BLANK_ON) {      /* 0413: display on again (DCS 0x29) before the push */
			static const uint8_t on[] = { 0x29 };
			bool ok = fb->dsiDcs(on, 1);
			IOLog("NORD2 FB BLANK: DCS display on %s\n", ok ? "sent" : "FAILED (pushing anyway)");
			fb->fPanelBlank = false;
			if (fb->fBlLock != NULL) { IOLockLock(fb->fBlLock); fb->fBl.sent_dbv = 0; IOLockUnlock(fb->fBlLock); }      /* 0495: level again after display on */
		}
#endif
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
		if (fb->fWorkHw && fb->hwFenceBusy()) {      /* 0416: a GPU write started on a layer since the plan: wait for it, then plan again */
			fb->fHwSt.late_write++;
			fb->fWaitStartUs = sweet_fb_abs_to_us(mach_absolute_time()); fb->fPState = 5; sender->setTimeoutUS(100);
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
		if (fb->fWorkHw && fb->hwFenceBusy()) {      /* 0416: a GPU write started on a layer during the TE wait: wait, plan again */
			fb->fHwSt.late_write++;
			fb->fWaitStartUs = sweet_fb_abs_to_us(mach_absolute_time()); fb->fPState = 5; sender->setTimeoutUS(100);
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
			    fb->fSync.pushes, total, fb->fCopyUs, fb->fLastCopyLayers, elapsed, 0u);
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
	{   /* 0621: stutter events and presents per second (before the push moves last_push_us) */
		uint64_t lp = fPaceWin.last_push_us;
		if (lp != 0 && nowUs > lp && nowUs - lp <= SWEET_FB_PACE_IDLE_US) {
			uint32_t k = sweet_fb_pace_periods(nowUs - lp, P);
			if (k >= 2) {
				if (fJankN < 12) {
					fJankT[fJankN] = nowUs / 1000; fJankK[fJankN] = (uint16_t)(k > 0xffffu ? 0xffffu : k);
					fJankWhy[fJankN] = (fWorkArrUs != 0 && fWorkArrUs + P / 2 <= lp + P) ? 'r' : 'n';
					fJankN++;
				} else {
					fJankLost++;
				}
			}
		}
		if (fSecT0Us == 0 || nowUs >= fSecT0Us + 8000000ull) { fSecT0Us = nowUs; fSecCur = 0; }      /* long idle: a new grid */
		while (nowUs >= fSecT0Us + 1000000ull) {
			if (fSecN == 0) { fSecFirstUs = fSecT0Us; }
			if (fSecN < 8) { fSecCnt[fSecN++] = (uint16_t)(fSecCur > 0xffffu ? 0xffffu : fSecCur); }
			fSecCur = 0; fSecT0Us += 1000000ull;
		}
		fSecCur++;
	}
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
		blService(true);      /* 0495: brightness between two frames (the presenter owns the engine, the link is idle) */
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
		/* 0640: the layer ranges / surfaces of the shown plan for the debug snapshot; a surface not in fHwHeld is never named */
		fHwShownN = 0;
		for (unsigned i = 0; i < fHwPlanN && i < SWEET_FB_HW_MAX_IN; i++) {
			bool held = false;
			for (unsigned j = 0; j < SWEET_FB_PENDING_SURFACE_COUNT; j++) { held |= fHwHeld.object[j] != NULL && fHwHeld.object[j] == (void *)fHwLayerS[i]; }
			fHwShownIn[fHwShownN] = fHwPlanIn[i]; fHwShownS[fHwShownN] = held ? fHwLayerS[i] : NULL; fHwShownN++;
		}
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
		{   /* 0407: different/same = CPU-copy hash; hw_* = overlay frames by content clock; the gaps include both */
			static char fs[96], fc[400];      /* present thread only */
			(void)sweet_fb_fps_format_seen(&fFps, fs, sizeof(fs));
			IOLog("NORD2 FB SEEN: presents=%u different=%u same=%u gaps_ms<=20/34/67/100/more=%u/%u/%u/%u/%u max_gap_ms=%llu %s\n", fStats.n, fDistinct, fSame,
			    fGapBin[0], fGapBin[1], fGapBin[2], fGapBin[3], fGapBin[4], fDistinctGapMaxUs / 1000, fs);
			uint64_t win = fFpsWinUs && now_us > fFpsWinUs ? now_us - fFpsWinUs : 0;
			size_t o = 0; fc[0] = 0;
			for (unsigned k = 0; k < SWEET_FB_FPS_CLIENTS && fFps.cl[k].shown != 0 && o < sizeof(fc) - 80; k++) {
				char name[24] = "kernel";
				if (fFps.cl[k].pid > 0) { proc_name(fFps.cl[k].pid, name, sizeof(name)); }
				int w = sweet_fb_fps_format_client(&fFps.cl[k], name, win, fc + o, sizeof(fc) - o);
				if (w > 0) { o += (size_t)w; if (o < sizeof(fc) - 3) { fc[o++] = ' '; fc[o++] = '|'; fc[o++] = ' '; fc[o] = 0; } }
			}
			if (fFps.cl[0].shown != 0 && win != 0) {
				IOLog("NORD2 FB CLIENT: win_ms=%llu %sother shown=%u fresh=%u\n", win / 1000, fc, fFps.other_shown, fFps.other_fresh);
			}
			sweet_fb_fps_window_reset(&fFps); fFpsWinUs = now_us;
		}
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
				if (fJankN != 0) {      /* 0621 */
					char jb[200]; size_t jn = 0; jb[0] = 0;
					for (uint32_t i = 0; i < fJankN && jn < sizeof(jb) - 24; i++) {
						jn += snprintf(jb + jn, sizeof(jb) - jn, "%s%llu:%u%c", i ? "," : "", fJankT[i], fJankK[i], fJankWhy[i]);
					}
					IOLog("NORD2 JANK: n=%u lost=%u ev=%s\n", fJankN, fJankLost, jb);
					fJankN = 0; fJankLost = 0;
				}
				if (fSecN != 0) {
					char sb[80]; size_t sn = 0; sb[0] = 0;
					for (uint32_t i = 0; i < fSecN; i++) { sn += snprintf(sb + sn, sizeof(sb) - sn, "%s%u", i ? "/" : "", fSecCnt[i]); }
					IOLog("NORD2 PERF T: t_ms=%llu pres=%s\n", fSecFirstUs / 1000, sb);
					fSecN = 0;
				}
				uint64_t lp = fPaceWin.last_push_us, la = fPaceWin.last_arr_us;
				IOLockLock(fLock);
				sweet_fb_pace_reset(&fPaceWin);
				fPaceWin.last_push_us = lp; fPaceWin.last_arr_us = la;      /* intervals across the window edge still count */
				IOLockUnlock(fLock);
			}
			{   /* 0396 (cumulative since boot) */
				char hb[1024];
				(void)hwStat(hb, sizeof(hb));
				IOLog("NORD2 FB HW: %s\n", hb);
				hwCpuFree(now_us);      /* 0402 */
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

/* 0402: the base registers of one OVL block (nl layers) */
static void
sweet_fb_hw_read_base(volatile uint32_t *o, struct sweet_fb_hw_base *b, uint32_t nl)
{
	b->src_con = o[SWEET_FB_HW_R_SRC_CON / 4];
	b->bgclr = o[SWEET_FB_HW_R_ROI_BGCLR / 4];
	b->datapath = o[SWEET_FB_HW_R_DATAPATH / 4];
	b->roi = o[SWEET_FB_HW_R_ROI_SIZE / 4];
	for (uint32_t i = 0; i < nl; i++) {
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
}

/* Once, before the first HW frame / test: the overlay must be in the CPU path's verified layout and the OVL ports' M4U off. */
/* 0414: boot-args n2fbhwpm=0..3 / n2fbhwcsw=0..3 override the defaults once (colour variants by a plain image repack); the sysctls
 * still change them at run time afterwards. */
static void
sweet_fb_hw_bootargs(void)
{
	if (__atomic_exchange_n(&sweet_fb_hw_bootargs_done, 1u, __ATOMIC_RELAXED) != 0) { return; }
	uint32_t v = 0;
	if (PE_parse_boot_argn("n2fbhwpm", &v, sizeof(v)) && v <= 3) { __atomic_store_n(&sweet_fb_hw_pm, v, __ATOMIC_RELAXED); }
	v = 0;
	if (PE_parse_boot_argn("n2fbhwcsw", &v, sizeof(v)) && v <= 3) { __atomic_store_n(&sweet_fb_hw_csw, v, __ATOMIC_RELAXED); }
	{   /* 0550/0551: the band rotation is OFF by default since 0551 (the status bar is moved by iOS itself: guest payload sbshift1);
		 * n2cut=1, or n2gdv +512 (the boot line has 4 characters left: n2gdv=521 fits), turns it on (fallback without sbshift1) */
		uint32_t cut = 0, gdv = 0;
		if (!PE_parse_boot_argn("n2cut", &cut, sizeof(cut)) && PE_parse_boot_argn("n2gdv", &gdv, sizeof(gdv)) && (gdv & 512u) != 0) { cut = 1; }
		sweet_fb_cut_g.on = cut != 0 ? 1u : 0u;
		IOLog("NORD2 FB CUT: punch-hole band rotation %s: band %d ear %d dx %d trail %d tdx %d (0551: off by default; on with n2cut=1 / n2gdv +512 / kern.n2cut=1; single full-screen layer frames only)\n",
		    sweet_fb_cut_g.on ? "on" : "OFF", sweet_fb_cut_g.band, sweet_fb_cut_g.lead_end, sweet_fb_cut_g.lead_dx, sweet_fb_cut_g.trail_start,
		    sweet_fb_cut_g.trail_dx);
	}
	uint32_t pm = __atomic_load_n(&sweet_fb_hw_pm, __ATOMIC_RELAXED), csw = __atomic_load_n(&sweet_fb_hw_csw, __ATOMIC_RELAXED);
	IOLog("NORD2 FB HW COLOR: pm %u csw %u (0414 default pm %u; boot-args n2fbhwpm / n2fbhwcsw): layer 0 CON %08x, layer 1 CON %08x%s\n", pm, csw,
	    SWEET_FB_HW_PM_DEFAULT, SWEET_FB_HW_CON_OPAQUE ^ ((csw & 2u) ? SWEET_FB_HW_CON_BYTE_SWAP : 0u),
	    sweet_fb_hw_con_upper(pm) ^ ((csw & 1u) ? SWEET_FB_HW_CON_BYTE_SWAP : 0u),
	    sweet_fb_hw_con_verified(sweet_fb_hw_con_upper(pm) ^ ((csw & 1u) ? SWEET_FB_HW_CON_BYTE_SWAP : 0u)) ? " (verified layout)" : " (UNVERIFIED layout)");
}

bool
AppleCLCD::hwInit(void)
{
	sweet_fb_hw_bootargs();      /* 0414 */
	if (fHwInitDone) { return fHwInitOk; }
	if (fHwInitRetryUs != 0 && sweet_fb_abs_to_us(mach_absolute_time()) < fHwInitRetryUs) { return false; }      /* 0416: backoff */
	if (!fNbReady || fDsi == NULL) { return false; }       /* not latched: tried again once the panel link runs */
	fHwInitDone = true;
	const char *why = NULL;
	if (fOvl[0] == NULL) { fOvl[0] = (volatile uint32_t *)ml_io_map(SWEET_FB_OVL0_2L, 0x1000); }
	if (fOvl[1] == NULL) { fOvl[1] = (volatile uint32_t *)ml_io_map(SWEET_FB_OVL1_2L, 0x1000); }
	if (fOvl[0] == NULL || fOvl[1] == NULL) { why = "overlay not mappable"; }
	uint64_t front = fPhys;      /* 0416: the boot buffer LK scans (no CPU double buffer any more) */
	for (uint32_t k = 0; why == NULL && k < SWEET_FB_HW_PIPES; k++) {
		volatile uint32_t *o = fOvl[k];
		struct sweet_fb_hw_base *b = &fHwBase[k];
		sweet_fb_hw_read_base(o, b, SWEET_FB_HW_SLOTS);
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
		static volatile uint32_t *l0, *l1;      /* 0410: once, kernel_map (ml_io_map_unmappable, VM_WIMG_IO), not the io_submap */
		if (l0 == NULL) { l0 = (volatile uint32_t *)ml_io_map_unmappable(SWEET_FB_LARB0, 0x1000, 0x7u); IOLog("NORD2 FB IOMAP: hwInit larb0 -> %p\n", l0); IOSleep(200); }
		if (l1 == NULL) { l1 = (volatile uint32_t *)ml_io_map_unmappable(SWEET_FB_LARB1, 0x1000, 0x7u); IOLog("NORD2 FB IOMAP: hwInit larb1 -> %p\n", l1); IOSleep(200); }
		if (l0 == NULL || l1 == NULL) { why = "larb registers not mappable"; }
		else {
			uint32_t v[4] = { l0[SWEET_FB_LARB_NONSEC(5) / 4], l0[SWEET_FB_LARB_NONSEC(6) / 4], l1[SWEET_FB_LARB_NONSEC(5) / 4], l1[SWEET_FB_LARB_NONSEC(6) / 4] };
			unsigned own = sweet_fb_iommu_owned_ports();      /* 0408 IOMMU: OVL*_2L MMU_EN set by the armed display IOMMU is accepted */
			IOLog("NORD2 FB HW: init larb0 p5 %08x p6 %08x larb1 p5 %08x p6 %08x (bit0 MMU_EN must be 0 unless set by the armed display IOMMU: owned %x)\n",
			    v[0], v[1], v[2], v[3], own);
			if (!sweet_fb_iommu_init_ports_ok(v, own)) { why = "display M4U enabled on an OVL port (addresses would be IOVAs)"; }
		}
	}
	fHwInitOk = (why == NULL);
	IOLog("NORD2 FB HW: init %s%s%s\n", fHwInitOk ? "ok: overlay layers usable" : "REFUSED: ", fHwInitOk ? "" : why,
	    fHwInitOk ? " (OVL0_2L/OVL1_2L layers 0/1, physical addresses)" : "");
	if (!fHwInitOk) {      /* 0416: retried after 1, 2, 4 .. 30 s; frames are held meanwhile; the mode never changes */
		fHwSt.last_fault = SWEET_FB_HW_F_BASE;
		uint64_t ms = fHwInitRetryN < 5 ? (1000ull << fHwInitRetryN) : 30000ull;
		fHwInitRetryN++;
		fHwInitRetryUs = sweet_fb_abs_to_us(mach_absolute_time()) + ms * 1000ull;
		fHwInitDone = false;
		IOLog("NORD2 FB HW: init retry %u in %llu ms (frames held meanwhile)\n", fHwInitRetryN, ms);
	}
	return fHwInitOk;
}

/* 0402b: IOMFBScalingLimits = 1:1 (QuartzCore DisplayLimits::update_display_scaling reads RGB/YUV Layer_MinScaleFraction/_MaxScale;
 * absent = RGB 1..2x, YUV 0.25..4x).  The display never scales; with it CoreAnimation scales on the GPU.  Read when backboardd starts. */
void
AppleCLCD::scalingPublish(bool on)
{
	if (!on) { removeProperty("IOMFBScalingLimits"); return; }
	static const char *const sk[4] = { "RGBLayer_MinScaleFraction", "RGBLayer_MaxScale", "YUVLayer_MinScaleFraction", "YUVLayer_MaxScale" };
	OSDictionary *d = OSDictionary::withCapacity(4);
	OSNumber *one = OSNumber::withNumber(1ULL, 32);
	if (d != NULL && one != NULL) {
		for (unsigned i = 0; i < 4; i++) { d->setObject(sk[i], one); }
		setProperty("IOMFBScalingLimits", d);
	}
	OSSafeReleaseNULL(one);
	OSSafeReleaseNULL(d);
}

/* 0402b: the cascaded 4L OVL, once, only when more than 2 slots are asked for (kern.n2fbhwlayers > 2 or test 7) */
void
AppleCLCD::hw4Init(void)
{
	if (fHw4Tried || !fHwInitOk) { return; }
	fHw4Tried = true;
	const char *w4 = fHwInitOk ? NULL : "2L refused";
	for (uint32_t k = 0; w4 == NULL && k < SWEET_FB_HW_PIPES; k++) {      /* 0402: the cascaded 4L OVL (OVL0 0x14000000 / OVL1 0x14100000) */
		if (fOvl4[k] == NULL) {      /* 0415: kernel_map (0410 rule: never the io_submap), once, logged */
			fOvl4[k] = (volatile uint32_t *)ml_io_map_unmappable(k ? 0x14100000UL : 0x14000000UL, 0x1000, 0x7u);
			IOLog("NORD2 FB IOMAP: 4L OVL%u pa %#lx -> %p (ml_io_map_unmappable, outside io_submap)\n", k, k ? 0x14100000UL : 0x14000000UL, (void *)fOvl4[k]);
			IOSleep(200);
		}
		if (fOvl4[k] == NULL) { w4 = "not mappable"; break; }
		struct sweet_fb_hw_base *b4 = &fHwBase4[k];
		sweet_fb_hw_read_base(fOvl4[k], b4, SWEET_FB_HW_L4);
		b4->gmc_fb = b4->gmc[1] ? b4->gmc[1] : fHwBase[k].gmc[0];      /* LK configured layer 1 (its warning layer) */
		b4->fifo_fb = b4->fifo[1] ? b4->fifo[1] : fHwBase[k].fifo[0];
		IOLog("NORD2 FB HW: init 4L pipe%u src %08x dp %08x roi %08x/%08x gmc %08x fifo %08x\n", k, b4->src_con, b4->datapath, b4->roi, fHwBase[k].roi,
		    b4->gmc_fb, b4->fifo_fb);
		if (!sweet_fb_hw_base4_ok(b4, fHwBase[k].roi)) { w4 = "not pass-through"; }
	}
	fHw4Ok = (w4 == NULL);
	IOLog("NORD2 FB HW: init 4L %s%s (max slots %u)\n", fHw4Ok ? "ok" : "REFUSED: ", fHw4Ok ? "" : w4, fHw4Ok ? SWEET_FB_HW_NSLOT : SWEET_FB_HW_SLOTS);
}

/* 0402: overlay slots per pipe now */
uint32_t
AppleCLCD::hwCap(void) const
{
	uint32_t c = __atomic_load_n(&sweet_fb_hw_layers, __ATOMIC_RELAXED);
	if (c < SWEET_FB_HW_SLOTS) { c = SWEET_FB_HW_SLOTS; }
	if (c > SWEET_FB_HW_NSLOT) { c = SWEET_FB_HW_NSLOT; }
	return (!fHw4Ok || fHw4OffUntilUs != 0) ? SWEET_FB_HW_SLOTS : c;
}

/* 0402: a fault on a frame that used the 4L block: 2 slots per pipe for 2, 4, 8, 16 s, then for the boot (hwBackoffPoll re-arms) */
void
AppleCLCD::hw4Off(const char *why)
{
	fHwSt.l4_faults++;
	uint64_t ms = fHw4OffN < 4 ? 2000ull << fHw4OffN : 0;
	fHw4OffUntilUs = ms ? sweet_fb_abs_to_us(mach_absolute_time()) + ms * 1000ull : ~0ull;
	if (ms) { fHw4OffN++; }
	IOLog("NORD2 FB HW: 4L OFF (%s, ovl %08x dsi %08x) %llu ms (0 = boot), %u/4\n", why, fHwOvlOr, fHwDsiSt, ms, fHw4OffN);
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
		if (!l.format_ok) { l.format_ok = sweet_fb_yuv_layer_con(s); }      /* 0490: packed 4:2:2 (kern.n2fbyuv) */
		{   /* display reachable only as a pool surface (0394: one contiguous range below 4 GiB), by the object we hold (not by id), and
			 * only with write-combined CPU mappings (the overlay does not snoop CPU caches; n2fbpoolc=1 copy-back pools stay on the CPU path) */
			uint64_t pa = sweet_surface_pool_pa_obj(s), len = sweet_surface_pool_size_obj(s);
			if (pa != 0 && sweet_fb_hw_pool_contains(pa, len) && sweet_surface_pool_cache_obj(s) == SWEET_FB_HW_POOL_CACHE_WC) { l.pa = pa; l.len = len; }
		}
		if ((get32(a, 8) != 0 && (get32(a, 8) != get32(b, 8) || get32(a, 12) != get32(b, 12))) || (get32(rec, 0x114 + 4 * layer) & 0xfu) != 0) {
			fHwSt.scaled++;      /* 0402: scale or transform asked for: shown 1:1 like the CPU path (IOMFBScalingLimits = 1 should keep it 0) */
		}
		if (l.pa == 0 && l.format_ok) { (void)sweet_fb_iommu_layer(s, &l.pa, &l.len); }      /* 0401 IOMMU: any DRAM surface through an IOVA */
		l.offset = s->offset; l.pitch = s->bytesPerRow;
		l.sx = g.sx; l.sy = g.sy; l.dx = g.dx; l.dy = g.dy; l.w = g.w; l.h = g.h;
		in[n] = l; ss[n] = s; ids[n] = id; n++;
	}
	for (unsigned i = 0; n > 1 && sweet_fb_iommu_mode() == 2 && i < n; i++) {      /* 0401 IOMMU: mode 2 = IOVA layers in single-layer frames only */
		if (sweet_fb_iommu_in_window(in[i].pa, in[i].pa + 1u)) { in[i].pa = 0; in[i].len = 0; }
	}
	return n;
}

bool
AppleCLCD::hwCandidate(const uint8_t *rec, SweetSurface *const surfaces[SWEET_FB_LAYERS])      /* 0396 (0402b: kept for kern.n2fbh6=0) */
{
	struct sweet_fb_hw_in in[SWEET_FB_HW_MAX_IN];
	SweetSurface *ss[SWEET_FB_HW_MAX_IN];
	uint32_t ids[SWEET_FB_HW_MAX_IN];
	int why;
	unsigned n = hwInputs(rec, surfaces, in, ss, ids, &why);
	if (why != SWEET_FB_HW_OK) { return false; }
	struct sweet_fb_hw_plan p;
	return sweet_fb_hw_plan_makec(&p, in, n, __atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED), __atomic_load_n(&sweet_fb_hw_pm, __ATOMIC_RELAXED),
	           fWidth, fHeight, hwCap()) == SWEET_FB_HW_OK;
}

/* State 1 of a frame that was not prepared: 1 = HW frame programmed (no copy), -1 = eligible but a GPU write is in flight (requeue,
 * never shown), 0 = CPU path. */
bool
AppleCLCD::hwWriteRegs(volatile uint32_t *o, unsigned k, const struct sweet_fb_hw_reg *r, unsigned n, uint32_t *badOff)
{
	if (o == NULL || n > SWEET_FB_HW_MAX_REGS) { *badOff = 0xffffffffu; return false; }
	if (o == fOvl[k] ? !sweet_fb_iommu_gate_list(k, r, n) : !sweet_fb_iommu_gate_list4(k, r, n)) { *badOff = 0xfffffffeu; return false; }      /* 0401/0415 IOMMU: 2L and 4L ports MMU_EN, between transfers */
	for (unsigned i = 0; i < n; i++) { o[r[i].off / 4] = r[i].val; }
	__asm__ volatile("dsb sy" ::: "memory");
	for (unsigned i = 0; i < n; i++) {
		if ((o[r[i].off / 4] ^ r[i].val) & r[i].mask) { *badOff = r[i].off | (k << 16); return false; }
	}
	return true;
}

bool
AppleCLCD::hwApply(const struct sweet_fb_hw_plan *p, const uint32_t *ids, const int32_t *pids, unsigned nids)
{
	struct sweet_fb_hw_reg r[SWEET_FB_HW_MAX_REGS];
	uint64_t t0 = mach_absolute_time();
	uint32_t bad = 0;
	fHwOvlOr = 0; fHwDsiSt = 0;
	for (unsigned k = 0; k < SWEET_FB_HW_PIPES; k++) {
		unsigned n = sweet_fb_hw_regs(r, p, k, &fHwBase[k]);
		fHwOn = true;                                     /* from the first write on the registers are not the CPU path's */
		if (!hwWriteRegs(fOvl[k], k, r, n, &bad)) {
			IOLog("NORD2 FB HW: register read-back mismatch pipe %u reg +0x%x (wrote %u registers)\n", bad >> 16, bad & 0xffffu, n);
			hwFault(SWEET_FB_HW_F_READBACK, "apply");
			return false;
		}
	}
	bool u4 = sweet_fb_hw_plan_uses4(p);
	if ((u4 || fHw4On) && fHw4Ok) {      /* 0402: the 4L block's layers (or turned off again) */
		fHw4On = true;
		for (unsigned k = 0; k < SWEET_FB_HW_PIPES; k++) {
			unsigned n = sweet_fb_hw_regs4(r, p, k, &fHwBase4[k]);
			fOvl4[k][SWEET_FB_HW_R_INTSTA / 4] = 0;
			if (!hwWriteRegs(fOvl4[k], k, r, n, &bad)) {
				IOLog("NORD2 FB HW: 4L readback mismatch %x: 4L off for the boot\n", bad);
				for (unsigned j = 0; j < SWEET_FB_HW_PIPES; j++) { n = sweet_fb_hw_regs4_base(r, &fHwBase4[j]); (void)hwWriteRegs(fOvl4[j], j, r, n, &bad); }
				fHw4Ok = fHw4On = false;      /* 0416: frame held; later frames use the 2L slots */
				return false;
			}
		}
		fHw4On = u4;
	} else if (u4) {
		return false;      /* 0416: held (the planner cap keeps this from happening) */
	}
	fHwUses4 = u4;
	{   /* 0399: the CON values actually written, whenever they change (and the first 4 applies) */
		static uint32_t lastCon[2][2], conLogs;
		uint32_t c[2][2];
		for (unsigned k = 0; k < SWEET_FB_HW_PIPES; k++) {
			for (unsigned i = 0; i < SWEET_FB_HW_SLOTS; i++) { c[k][i] = p->slot[k][i].en ? sweet_fb_hw_con_final(p, k, i) : 0u; }
		}
		if (conLogs < 4 || memcmp(c, lastCon, sizeof(c)) != 0) {
			conLogs++;
			memcpy(lastCon, c, sizeof(c));
			/* 0414: + each layer's surface id and producer (creator) pid/name; "test" when called by an H2 test / re-apply */
			uint32_t lid[2] = { 0, 0 }; int32_t lpid[2] = { 0, 0 };
			char ln[2][20] = { "-", "-" };
			for (unsigned i = 0; i < 2 && ids != NULL && i < nids; i++) {
				lid[i] = ids[i]; lpid[i] = pids != NULL ? pids[i] : 0;
				if (lpid[i] > 0) { proc_name(lpid[i], ln[i], sizeof(ln[i])); }
			}
			IOLog("NORD2 FB HW: CON written pipe0 L0 %08x L1 %08x | pipe1 L0 %08x L1 %08x (pm %u, csw %u; BYTE_SWAP=R<->B bit 24, RGB_SWAP bit 25, MAN bit 23)"
			    " layers %u: L0 id %u pid %d %s | L1 id %u pid %d %s%s\n",
			    c[0][0], c[0][1], c[1][0], c[1][1], __atomic_load_n(&sweet_fb_hw_pm, __ATOMIC_RELAXED), __atomic_load_n(&sweet_fb_hw_csw, __ATOMIC_RELAXED),
			    ids != NULL ? nids : 0u, lid[0], lpid[0], ln[0], lid[1], lpid[1], ln[1], ids != NULL ? "" : " (test/re-apply)");
		}
		fHwLastCon[0] = c[0][0] ? c[0][0] : c[1][0]; fHwLastCon[1] = c[0][1] ? c[0][1] : c[1][1];
	}
	uint64_t us = sweet_fb_abs_to_us(mach_absolute_time() - t0);
	fHwSt.reg_us_sum += us;
	if (us > fHwSt.reg_us_max) { fHwSt.reg_us_max = us; }
	return true;
}

void
AppleCLCD::hwFault(int f, const char *where)
{
	/* 0416: logged and counted only; the overlay stays the frame source, the next frame re-programs every register (no mode 0, no restore) */
	fHwSt.faults++;
	fHwSt.last_fault = f;
	if (fHwSt.faults <= 16 || (fHwSt.faults % 256) == 0) {
		IOLog("NORD2 FB HW: FAULT %s (%s) #%llu: kept on the overlay, next frame re-programs (hw_frames=%llu ovl_int=%08x dsi=%08x race_run=%u)\n",
		    sweet_fb_hw_fault_name(f), where, fHwSt.faults, fHwSt.frames, fHwOvlOr, fHwDsiSt, fHwRaceRun);
	}

}

/* 0397: present thread, before a frame is planned */
void
AppleCLCD::hwBackoffPoll(void)
{
	if (fHw4OffUntilUs != 0 && fHw4OffUntilUs != ~0ull && sweet_fb_abs_to_us(mach_absolute_time()) >= fHw4OffUntilUs) {      /* 0402 */
		fHw4OffUntilUs = 0;
		IOLog("NORD2 FB HW: 4L back on (%u/4)\n", fHw4OffN);
	}
}

/* the GPU fence: a write job on a layer surface queued or running, a 0112 reservation, or 0395's write-pending */
bool
AppleCLCD::hwFenceBusy(void)
{
	if (!sweet_fb_h6_on()) {      /* 0396 (0402b: kern.n2fbh6=0) */
		for (unsigned i = 0; i < fHwN; i++) {
			if (nord2_gpu_surface_busy(fHwLayerS[i]) || (fHwIdOk[i] && nord2_gpu_surface_write_pending(fHwLayerId[i]))) { return true; }
		}
		return false;
	}
	return false;      /* 0402c H6: settled in state 5 (the low water mark only grows), and no GPU-driver lock here */
}

/*
 * 0402 H6, state 5 in the HW modes: true = still waiting (timer set) or dropped for a newer swap; false = present it now.  A layer
 * is ready when the GPU writes submitted for it by SwapEnd are done (write_seq snapshot); a 0112 reservation (frame promised, not
 * submitted) holds it and the snapshot is taken again when the reservation retires.  After 2 panel refreshes from the swap's
 * arrival it is shown anyway (fence_forced): the panel keeps the previous picture meanwhile, the CPU never composes it.
 */
bool
AppleCLCD::hwFence5(IOTimerEventSource *sender)      /* 0402c: lock-free (no Nord2GPU::lock, no sleep); re-polled by the present timer */
{
	const uint8_t *rec = fWorkRec;
	uint32_t mask = rec != NULL ? get32(rec, 0x104) : 0, layers = 0;
	for (uint32_t l = 0; l < SWEET_FB_LAYERS; l++) {
		if (fWorkSurfaces.object[l] != NULL && (mask & (1u << l)) && get32(rec, 0x54 + 4 * l) != 0) { layers |= 1u << l; }
	}
	uint64_t now = sweet_fb_abs_to_us(mach_absolute_time()), arr = fWorkArrUs ? fWorkArrUs : fWaitStartUs;
	int r = sweet_fb_hw_h6c(layers, fWorkSeq, nord2_gpu_lowwater_peek(), now - arr, 2 * (fTePeriodUs ? fTePeriodUs : 11111));
	fHwForced = (r == SWEET_FB_HW_H6_FORCED);
	if (r == SWEET_FB_HW_H6_READY) { return false; }
	if (fHwForced) {      /* 0416: never shown before its GPU writes are done: keep waiting (logged), a newer swap replaces it */
		fHwForced = false;
		if ((fHwSt.fence_forced++ % 256) == 0) { IOLog("NORD2 FB HW: H6 fence still pending after %llu us: waiting (no forced frame)\n", now - arr); }
	}
	if (now - fWaitStartUs < 300) { fHwSt.fence_wait++; }
	if (__atomic_load_n(&fDirty, __ATOMIC_RELAXED)) {      /* a newer swap is queued: it replaces this one */
		fHwSt.fence_skip++;
		presentDone(false, false, false);
		handOver(0);
		return true;
	}
	sender->setTimeoutUS(250);
	return true;
}

int
AppleCLCD::hwFrameTry(uint32_t mode)
{
	sweet_fb_hw_bootargs();      /* 0414: before the first plan (hwInit runs after it) */
	struct sweet_fb_hw_in in[SWEET_FB_HW_MAX_IN];
	SweetSurface *ss[SWEET_FB_HW_MAX_IN];
	uint32_t ids[SWEET_FB_HW_MAX_IN];
	int why;
	uint32_t pm = __atomic_load_n(&sweet_fb_hw_pm, __ATOMIC_RELAXED);
	if (mode >= SWEET_FB_HW_SINGLE && fNbReady && !fPowerOff && __atomic_load_n(&sweet_fb_hw_layers, __ATOMIC_RELAXED) > SWEET_FB_HW_SLOTS && hwInit()) {
		hw4Init();      /* 0402b: only when more than 2 slots are asked for */
	}
	uint32_t cap = hwCap();
	unsigned n = hwInputs(fWorkRec, (SweetSurface *const *)fWorkSurfaces.object, in, ss, ids, &why);
	struct sweet_fb_hw_plan p;
	if (why == SWEET_FB_HW_OK) {        /* H0/H1: what would go through the overlay, whatever the mode */
		if (sweet_fb_hw_plan_makec(&p, in, n, SWEET_FB_HW_SINGLE, pm, fWidth, fHeight, cap) == SWEET_FB_HW_OK) { fHwSt.dry_single++; }
		if (sweet_fb_hw_plan_makec(&p, in, n, SWEET_FB_HW_DUAL, pm, fWidth, fHeight, cap) == SWEET_FB_HW_OK) { fHwSt.dry_dual++; }
	}
	if (mode < SWEET_FB_HW_SINGLE) { mode = SWEET_FB_HW_DUAL; }      /* 0416: no off / dry modes */
	int r = why != SWEET_FB_HW_OK ? why : sweet_fb_hw_plan_makec(&p, in, n, mode, pm, fWidth, fHeight, cap);
	if (r == SWEET_FB_HW_OK && (fPowerOff || !fNbReady || !hwInit())) { r = SWEET_FB_HW_W_STATE; }
	for (unsigned i = 0; r == SWEET_FB_HW_OK && i < n; i++) {
		/* 0394/0395 look surfaces up by id: the id must still name this very surface, and its pool address be the one planned */
		SweetSurface *t = sweet_surface_copy(ids[i]);
		bool same = (t == ss[i]);
		if (t != NULL) { t->release(); }
		if (!same) { r = SWEET_FB_HW_W_MISSING; }      /* 0395's fences are by id */
	}
	if (r != SWEET_FB_HW_OK) { fHwSt.why[r]++; if (r != SWEET_FB_HW_W_NONE) { fHwSt.cpu_frames++; } return 0; }      /* 0402: an empty swap touches no pixel */
	if (!sweet_fb_h6_on()) {      /* 0396 fence (0402b: kern.n2fbh6=0): a GPU write in flight -> requeue, never shown */
		bool busy[SWEET_FB_HW_MAX_IN], pend[SWEET_FB_HW_MAX_IN];
		for (unsigned i = 0; i < n; i++) { busy[i] = nord2_gpu_surface_busy(ss[i]); pend[i] = nord2_gpu_surface_write_pending(ids[i]); }
		if (!sweet_fb_hw_fence_ok(busy, pend, n)) { fHwSt.why[SWEET_FB_HW_W_FENCE]++; fHwSt.fence_skip++; return -1; }
	}
	/* kern.n2fbh6=1: the GPU fence was settled in state 5 (hwFence5, H6) */
	for (unsigned i = 0; i < n; i++) {
		fHwLayerS[i] = ss[i]; fHwLayerId[i] = ids[i]; fHwIdOk[i] = true; fHwSnap[i] = 0;
		fHwSeq[i] = sweet_fb_h6_on() ? nord2_gpu_fence_seq_peek(ids[i]) : nord2_gpu_surface_write_seq(ids[i]);      /* 0402c: lock-free with H6 */
		for (uint32_t l = 0; l < SWEET_FB_LAYERS; l++) { if (get32(fWorkRec, 0x54 + 4 * l) == ids[i]) { fHwSnap[i] = fWorkSeq[l]; } }
	}
	fHwN = n;
	int cw;
	{   /* 0550: status bar band rotation pieces (on top of every layer; unrotated frame when they do not fit) */
		struct sweet_fb_cut_cfg cc = sweet_fb_cut_g;
		cw = sweet_fb_cut_plan(&p, in, n, &cc, pm, fWidth, cap);
		sweet_fb_cut_n[cw]++;
		if (cw != SWEET_FB_CUT_OFF && (sweet_fb_cut_logs < 6 || (cw != SWEET_FB_CUT_OK && sweet_fb_cut_n[cw] <= 4))) {
			sweet_fb_cut_logs++;
			IOLog("NORD2 FB CUT: frame %u layers %u -> %s: slots pipe0 %u pipe1 %u src_con %x/%x (band %d dx %d tdx %d)\n", get32(fWorkRec, 0x50), n,
			    sweet_fb_cut_why_name(cw), p.nslot[0], p.nslot[1], p.src_con[0], p.src_con[1], cc.band, cc.lead_dx, cc.trail_dx);
		}
	}
	sweet_fb_hw_plan_csw(&p, __atomic_load_n(&sweet_fb_hw_csw, __ATOMIC_RELAXED));      /* 0399 */
	int32_t lpid[SWEET_FB_HW_MAX_IN];      /* 0414: producer pids for the CON log */
	for (unsigned i = 0; i < n; i++) { lpid[i] = ss[i] != NULL ? ss[i]->creator : 0; }
	if (!hwApply(&p, ids, lpid, n)) { fHwN = 0; fHwSt.cpu_frames++; fHwSt.why[SWEET_FB_HW_W_STATE]++; return 0; }      /* faulted / 4L lost */
	__atomic_store_n(&sweet_fb_cut_live, cw == SWEET_FB_CUT_OK ? 1u : 0u, __ATOMIC_RELAXED);      /* 0550: touch follows the programmed frame */
	fHwPlan = p;
	fHwPlanN = n;      /* 0640: the planned layers' device ranges, copied with the plan when it is shown */
	for (unsigned i = 0; i < n; i++) { fHwPlanIn[i] = in[i]; }
	fHwSt.planned++;
	sweet_fb_yuv_note(in, n, ids, ss);      /* 0490 */
	if (fHwUses4) { fHwSt.l4_frames++; }
	if (fHwSt.plans_logged < 8 || (fHwUses4 && fHwSt.l4_frames <= 4)) {
		fHwSt.plans_logged++;
		sweet_fb_hw_log_plan(mode == SWEET_FB_HW_SINGLE ? "single" : (fHwUses4 ? "multi" : "dual"), get32(fWorkRec, 0x50), &p);
		if (fHwUses4) { IOLog("NORD2 FB HW: 4L slots %x/%x L0 %08x\n", p.src_con[0] >> 2, p.src_con[1] >> 2, p.slot[0][2].addr | p.slot[1][2].addr); }
	}
	{   /* 0407: content clock of this HW frame (measurement only: the seqs were read above, no extra GPU-driver call) */
		int32_t cpid[SWEET_FB_HW_MAX_IN];
		unsigned m = n < SWEET_FB_FPS_SLOTS ? n : SWEET_FB_FPS_SLOTS;      /* 0415: fHwSeq has SWEET_FB_HW_MAX_IN entries (0402); the meter keeps SWEET_FB_FPS_SLOTS */
		for (unsigned i = 0; i < m; i++) { cpid[i] = ss[i] != NULL ? ss[i]->creator : 0; }
		noteHwGen(sweet_fb_fps_note(&fFps, m, ids, fHwSeq, cpid));
	}
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
	bool u4 = fHwUses4 && fOvl4[0] != NULL && fOvl4[1] != NULL;
	for (uint32_t k = 0; u4 && k < SWEET_FB_HW_PIPES; k++) { ovl |= fOvl4[k][SWEET_FB_HW_R_INTSTA / 4]; fOvl4[k][SWEET_FB_HW_R_INTSTA / 4] = 0; }      /* 0402 */
	bool raced = false;
	for (unsigned i = 0; i < fHwN; i++) {      /* 0402: a write SUBMITTED during the scanout (a reserved next frame is not a race) */
		bool h6 = sweet_fb_h6_on();      /* 0402c: H6 = lock-free peeks only; H6 off = the 0396 rule */
		if (h6 ? nord2_gpu_fence_seq_peek(fHwLayerId[i]) != fHwSeq[i] : (nord2_gpu_surface_busy(fHwLayerS[i]) || nord2_gpu_surface_write_seq(fHwLayerId[i]) != fHwSeq[i])) { raced = true; }
	}
	fHwSt.frames++;
	if (raced) {
		fHwSt.race++;
		if (fHwSt.race <= 16) {
			IOLog("NORD2 FB HW: race: a GPU write to a shown layer started before FRAME_DONE (hw frame %llu, surface %u seq %llu -> %llu)\n", fHwSt.frames,
			    fHwLayerId[0], fHwSeq[0], sweet_fb_h6_on() ? nord2_gpu_fence_seq_peek(fHwLayerId[0]) : nord2_gpu_surface_write_seq(fHwLayerId[0]));
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
	if (u4 && (f == SWEET_FB_HW_F_UNDERFLOW || f == SWEET_FB_HW_F_DSI)) { hw4Off(sweet_fb_hw_fault_name(f)); f = SWEET_FB_HW_F_NONE; }      /* 0402: bandwidth of the extra layers */
	(void)sweet_fb_iommu_frame_done();      /* 0416: an M4U fault is logged, cleared and re-armed by the IOMMU code; the overlay goes on */
	fHwRaceRun = raced ? fHwRaceRun + 1 : 0;
	if (fHwRaceRun > fHwRaceTotalRunMax) { fHwRaceTotalRunMax = fHwRaceRun; }
	if (f != SWEET_FB_HW_F_NONE) { hwFault(f, "frame"); }
	fHwOvlOr = 0; fHwDsiSt = 0;
}

int
AppleCLCD::hwStat(char *buf, size_t len)
{
	const uint64_t *w = fHwSt.why;
	return snprintf(buf, len, "mode=%u pm=%u init=%u/%u on=%u hw_frames=%llu cpu_frames=%llu planned=%llu would_single=%llu would_dual=%llu "
	           "why[mode/none/count/cover/format/memory/pitch/range/missing/fence/state]=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu "
	           "fence_skip=%llu late_write=%llu race=%llu empty_kept=%llu restores=%llu faults=%llu last_fault=%s reg_us_avg=%llu reg_us_max=%llu tests=%llu "
	           "csw=%u con_l0=%08x con_l1=%08x cap=%u l4=%u/%u l4_frames=%llu l4_faults=%llu cpu_fault=%llu fence_wait=%llu fence_forced=%llu "
	           "fence_resv=%llu scaled=%llu cpu_per_min=%llu h6=%u scaling=%u",
	           __atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED), __atomic_load_n(&sweet_fb_hw_pm, __ATOMIC_RELAXED), fHwInitDone ? 1u : 0u,
	           fHwInitOk ? 1u : 0u, fHwOn ? 1u : 0u, fHwSt.frames, fHwSt.cpu_frames, fHwSt.planned, fHwSt.dry_single, fHwSt.dry_dual,
	           w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11], fHwSt.fence_skip, fHwSt.late_write, fHwSt.race,
	           fHwSt.empty_kept, fHwSt.restores, fHwSt.faults, sweet_fb_hw_fault_name(fHwSt.last_fault),
	           fHwSt.planned ? fHwSt.reg_us_sum / fHwSt.planned : 0ULL, fHwSt.reg_us_max, fHwSt.tests,
	           __atomic_load_n(&sweet_fb_hw_csw, __ATOMIC_RELAXED), fHwLastCon[0], fHwLastCon[1], hwCap(), fHw4Ok ? 1u : 0u, fHw4On ? 1u : 0u,
	           fHwSt.l4_frames, fHwSt.l4_faults, fHwSt.cpu_fault, fHwSt.fence_wait, fHwSt.fence_forced, fHwSt.fence_resv, fHwSt.scaled, fCfLastCpm,
	           __atomic_load_n(&sweet_fb_h6_mode, __ATOMIC_RELAXED), __atomic_load_n(&sweet_fb_scaling_mode, __ATOMIC_RELAXED));
}

/* 0402: the CPU-free verdict, one line per >= 60 s window (present thread, from the stats block) */
void
AppleCLCD::hwCpuFree(uint64_t nowUs)
{
	uint64_t cur[3 + SWEET_FB_HW_W_COUNT_ENUM] = { fHwSt.frames, fHwSt.cpu_frames, fHwSt.cpu_fault };
	for (unsigned i = 0; i < SWEET_FB_HW_W_COUNT_ENUM; i++) { cur[3 + i] = fHwSt.why[i]; }
	if (fCfT0Us != 0 && nowUs - fCfT0Us < 60000000ull) { return; }
	if (fCfT0Us != 0) {
		uint64_t d[3 + SWEET_FB_HW_W_COUNT_ENUM], ws = (nowUs - fCfT0Us) / 1000000ull;
		for (unsigned i = 0; i < 3 + SWEET_FB_HW_W_COUNT_ENUM; i++) { d[i] = cur[i] - fCfBase[i]; }
		fCfLastCpm = ws ? d[1] * 60ull / ws : d[1];
		uint32_t m = __atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED);
		const char *v = d[1] ? "FAIL" : d[0] ? "PASS" : (m >= SWEET_FB_HW_SINGLE || fHwBackoffMode != 0) ? "IDLE" : "OFF";
		uint64_t *w = d + 3;
		IOLog("NORD2 FB CPUFREE: FB HW: CPU-FREE %s window_s=%llu hw=%llu cpu=%llu cpu_per_min=%llu cpu_fault=%llu hw_mode=%u cap=%u "
		    "why[count/cover/format/memory/pitch/range/missing/state]=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu fence_wait=%llu fence_forced=%llu\n",
		    v, ws, d[0], d[1], fCfLastCpm, d[2], m, hwCap(), w[SWEET_FB_HW_W_COUNT], w[SWEET_FB_HW_W_COVER], w[SWEET_FB_HW_W_FORMAT],
		    w[SWEET_FB_HW_W_MEMORY], w[SWEET_FB_HW_W_PITCH], w[SWEET_FB_HW_W_RANGE], w[SWEET_FB_HW_W_MISSING], w[SWEET_FB_HW_W_STATE],
		    fHwSt.fence_wait, fHwSt.fence_forced);
	}
	fCfT0Us = nowUs;
	memcpy(fCfBase, cur, sizeof(cur));
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
	if (sweet_surface_wod_unwired(s)) { return; }      /* 0480: no physical walk of unwired memory */
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
	uint64_t wseq[SWEET_FB_LAYERS];      /* 0402 H6: the GPU writes already submitted for each layer when SwapEnd arrives, */
	uint32_t wresv = 0;                  /* and the layers whose frame is still only promised (0112 reservation) */
	bool h6 = sweet_fb_h6_on();      /* 0402c: SwapEnd takes NO GPU-driver lock: a lock-free peek of the fence table, only with kern.n2fbh6=1 */
	for (uint32_t layer = 0; layer < SWEET_FB_LAYERS; layer++) {
		wseq[layer] = (h6 && ids[layer] != 0) ? nord2_gpu_fence_seq_peek(ids[layer]) : 0;
	}
	IOLockLock(fLock);
	memcpy(fPendSeq, wseq, sizeof(wseq)); fPendResv = wresv;
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
	if (fBlThermCall != NULL) { (void)thread_call_cancel_wait(fBlThermCall); }      /* 0495 */
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
	IOLockLock(fLock); fHwShownN = 0; IOLockUnlock(fLock);      /* 0640: the snapshot no longer names the held surfaces */
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
