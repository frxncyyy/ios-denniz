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
	SweetSurface       *fSurfaceB;
	uintptr_t           fPixelsB, fCopyDst;
	uint8_t            *fNextRec;
	sweet_fb_pending_surfaces fNextSurfaces;
	bool                fDbl, fPrepared, fTeObsSet;
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
	{ uint32_t lead = 0; if (PE_parse_boot_argn("n2telead", &lead, sizeof(lead)) && lead <= 4000) { fTeLeadUs = lead; } }
	nanoseconds_to_absolutetime(fVsyncUs * 1000ULL, &fPeriodAbs);
	/* n2swait=<0|1|2>: SwapWait semantics (0 = as before: always success, calls logged). */
	{
		uint32_t level = 0;
		if (!PE_parse_boot_argn("sweetfb_swapwait", &level, sizeof(level))) { (void)PE_parse_boot_argn("n2swait", &level, sizeof(level)); }
		fSwapWaitLevel = level > 2 ? 2 : level;
		bzero(&fRetire, sizeof(fRetire));
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
	setProperty("DisplayPipePlaneBaseAlignment", 64ULL, 32);
	registerService();
	IOLog("SWEET FB: AppleCLCD %ux%u stride %u at 0x%llx, default surface %u, presented as %ux%u\n",
	    fWidth, fHeight, fRowBytes, fPhys, fSurface->id, fLogicalW, fLogicalH);
	return true;
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
	IOLockUnlock(fLock);
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
		fNextRec = rec; fNextSurfaces = s; fNextState = 1;
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
	if (!fPowerOff && fPState == 0 && fNextState != 0) {
		/* 0156: the frame prepared during the last transfer goes first */
		fWorkRec = fNextRec; fWorkSurfaces = fNextSurfaces;
		fNextRec = NULL; sweet_fb_pending_surfaces_init(&fNextSurfaces);
		sweet_fb_retire_take(&fRetire);
		if (fNextState == 2) { fPrepared = true; fPState = 1; fStats.dbl_hits++; }
		else { fWaitStartUs = fNextWaitUs; fPState = 5; }
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
	fWaitStartUs = sweet_fb_abs_to_us(mach_absolute_time());
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
		if (fb->fPState == 3 || fb->fPState == 4) { fb->scanStep(SWEET_FB_SCAN_EV_STOP); }
		fb->dropNext();
#endif
		fb->presentDone(false, false, false);
		return;
	}
#if defined(NORD2)
	if (fb->fPState == 5) {
		/* 0155: the swap is presented when the GPU is done with it (polled here, not on the vsync thread); after SWEET_FB_BUSY_WAIT_US anyway */
		bool busy = false;
		for (uint32_t layer = 0; layer < SWEET_FB_LAYERS && !busy; layer++) {
			if (fb->fWorkSurfaces.object[layer] != NULL && nord2_gpu_surface_busy(fb->fWorkSurfaces.object[layer])) { busy = true; }
		}
		uint64_t waited = sweet_fb_abs_to_us(mach_absolute_time()) - fb->fWaitStartUs;
		if (busy && waited < SWEET_FB_BUSY_WAIT_US) { sender->setTimeoutUS(100); return; }
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
		if (wasPrepared) { fb->fPrepared = false; }
		else {
			fb->fCopyDst = fb->fDbl ? (fb->fFront ? fb->fPixels : fb->fPixelsB) : fb->fPixels;
			shown = fb->presentCopy(fb->fWorkRec, (SweetSurface *const *)fb->fWorkSurfaces.object);
		}
		uint64_t t1 = mach_absolute_time();
		fb->fCopyUs = wasPrepared ? fb->fPrepCopyUs : sweet_fb_abs_to_us(t1 - t0);
		if (!shown) { fb->presentDone(false, false, false); return; }
		if (!wasPrepared) { fb->noteDistinct(fb->fCopyDst ? fb->fCopyDst : fb->fPixels); }
		fb->fCopyDst = 0;
		if (fb->fNbReady && fb->fDsi != NULL && sweet_fb_abs_to_us(t1) >= fb->fTeOffUntilUs && !(fb->fDsi[SWEET_FB_DSI_INTSTA / 4] & SWEET_FB_DSI_BUSY)) {
			/* 0155: the transfer starts at the panel's next TE (the scan then stays behind the write: no tear lines) */
			if (fb->fDbl) {      /* 0156: the overlay reads the buffer that was just filled */
				uint8_t back = fb->fFront ? 0 : 1;
				if (fb->ovlSelect(back)) { fb->fFront = back; }
				else { IOLog("NORD2 FB DBL: overlay flip refused, back to one buffer\n"); fb->fDbl = false; (void)fb->ovlSelect(0); fb->fFront = 0; fb->dropNext(); }
			}
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
		if (fb->fNbReady && fb->xferStart()) {
			fb->fXferStart = t1;
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
		if (fb->xferStart()) {
			fb->fXferStart = mach_absolute_time();
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
	int st = sweet_fb_xfer_state(fb->xferDone(), elapsed);
#if defined(NORD2)
	if (st != SWEET_FB_XFER_WAIT && fb->fOvl[0] != NULL && fb->fOvl[1] != NULL) {
		/* 0191: sticky overlay status of the frame just sent (underrun/abnormal bits), then clear it */
		fb->fOvlFrames++;
		for (uint32_t i = 0; i < SWEET_FB_SCAN_PIPES; i++) {
			uint32_t v = fb->fOvl[i][SWEET_FB_OVL_INTSTA / 4];
			for (uint32_t b = 0; b < 32; b++) { if (v & (1u << b)) { fb->fOvlBits[i][b]++; } }
			fb->fOvl[i][SWEET_FB_OVL_INTSTA / 4] = 0;
		}
		if (fb->fDsi != NULL && (fb->fDsi[SWEET_FB_DSI_INTSTA / 4] & (1u << 12))) { fb->fDsiUnd++; fb->fDsi[SWEET_FB_DSI_INTSTA / 4] = ~(1u << 12); }
	}
#endif
	if (st == SWEET_FB_XFER_WAIT) {
#if defined(NORD2)
		if (fb->fPState == 2) { fb->prepNext(); }
#endif
		sender->setTimeoutUS(SWEET_FB_XFER_POLL_US);
		return;
	}
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

void
AppleCLCD::presentDone(bool shown, bool timeout, bool legacy)
{
	uint64_t xfer = shown ? sweet_fb_abs_to_us(mach_absolute_time() - fXferStart) : 0;
#if defined(NORD2)
	/* 0154: the presenter still owns the engine here (fPState != 0), the link is idle */
	if (shown && !legacy) {
		if (xfer > 3000 && xfer < 20000) {
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
				if (fModeState == 1) { fTePeriodUs = 11226; fTeLastUs = 0; }      /* re-acquire the TE at the new rate */
			}
			if (!fDbl && fRetuneState == 1) { (void)dblEnable(); }
		}
		else if (fRetuneState == 1 && fPresents - fRetunePresents < 40 && fDsi != NULL &&
		    (timeout || xfer > 30000 || (fDsi[SWEET_FB_DSI_INTSTA / 4] & (1u << 12)))) {
			if (fModeState == 1) { (void)panelRefresh(false); fModeState = 2; fTePeriodUs = SWEET_FB_VSYNC_US; fTeLastUs = 0; }
			linkRetune(false);
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
	fPState = 0;
	IOLockUnlock(fLock);
	IOLockWakeup(fLock, &fRetireEvent, false);                   /* SwapWait: the copy is over, its surfaces are free */
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
		    fStats.busy_forced, fStats.late_ticks, 60 / fFlushTicks, nord2_gpu_resv_expired(),
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
			fOvlFrames = 0; fDsiUnd = 0; memset(fOvlBits, 0, sizeof(fOvlBits));
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
	uint32_t ids[SWEET_FB_LAYERS];
	for (uint32_t layer = 0; layer < SWEET_FB_LAYERS; layer++) {
		ids[layer] = get32(rec, 0x54 + 4 * layer);
	}
	sweet_fb_pending_surfaces_capture(&incoming, ids, get32(rec, 0x104),
	    sweet_fb_retain_surface);
	sweet_fb_pending_surfaces_init(&replaced);
	IOLockLock(fLock);
	{
		uint64_t nu = sweet_fb_abs_to_us(mach_absolute_time());
		if (fSwapLastUs) { uint64_t g = nu - fSwapLastUs; fArrBin[g <= 8000 ? 0 : g <= 20000 ? 1 : g <= 34000 ? 2 : g <= 67000 ? 3 : 4]++; }
		fSwapLastUs = nu; fSwapArrUs = nu;
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
	nord2_dvfs_ui_kick();
	if ((++sweet_fb_swap_count % 1000) == 0) {
		IOLog("SWEET FB: %llu swaps\n", sweet_fb_swap_count);
	}
}

/* Copies the layers into the panel memory. Returns false only when the display could not be taken over
 * (nothing shown); the panel transfer is started by the caller. */
bool
AppleCLCD::presentCopy(const uint8_t *rec, SweetSurface *const surfaces[SWEET_FB_LAYERS])
{
	uint32_t mask = get32(rec, 0x104);
	uint64_t diag_present = 0;
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
		nord2_gpu_surface_cpu_acquire(s);   /* GPU-written pixels become visible to this CPU copy */
		uintptr_t src = s->kernelAddress();
		uint32_t bpe = s->bytesPerElement ? s->bytesPerElement : 4;
		if (w <= 0 || h <= 0) {
			w = (int32_t)s->width;
			h = (int32_t)s->height;
		}
		if (sx < 0 || sy < 0) {
			sx = sy = 0;
		}
		if (dx < 0 || dy < 0) {
			dx = dy = 0;
		}
		/* Logical (presented) display coordinates -> panel coordinates. */
		dx -= fOffX;
		dy -= fOffY;
		if (dx < 0) {
			sx -= dx;
			w += dx;
			dx = 0;
		}
		if (dy < 0) {
			sy -= dy;
			h += dy;
			dy = 0;
		}
		if (w > (int32_t)s->width - sx) {
			w = (int32_t)s->width - sx;
		}
		if (h > (int32_t)s->height - sy) {
			h = (int32_t)s->height - sy;
		}
		if (w > (int32_t)fWidth - dx) {
			w = (int32_t)fWidth - dx;
		}
		if (h > (int32_t)fHeight - dy) {
			h = (int32_t)fHeight - dy;
		}
		uint64_t last = (uint64_t)s->offset + (uint64_t)(sy + h - 1) * s->bytesPerRow +
		    (uint64_t)(sx + w) * 4;
		if (w <= 0 || h <= 0 || s->elementWidth != 1 || s->elementHeight != 1 ||
		    s->memory == NULL || last > s->memory->getLength()) {
			src = 0;
		}
		if (src != 0 && bpe == 4) {
			if (fDiag.enabled) { __atomic_fetch_add(&fDiag.copied_layers, 1, __ATOMIC_RELAXED); }
			for (int32_t y = 0; y < h; y++) {
				memcpy((void *)((fCopyDst ? fCopyDst : fPixels) + (uint64_t)(dy + y) * fRowBytes + (uint64_t)dx * 4),
				    (const void *)(src + s->offset + (uint64_t)(sy + y) * s->bytesPerRow + (uint64_t)sx * 4),
				    (size_t)w * 4);
			}
		}
		if (ownRef) s->release();
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
