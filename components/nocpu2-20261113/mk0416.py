#!/usr/bin/env python3
"""0416 source transform (overlay + IOMMU only, no CPU rendering/presentation path, no fallback modes).
   mk0416.py ARMDIR   -- edits ARMDIR/SweetFramebuffer.cpp, sweet_fb_hw.h, sweet_fb_iommu.h, sweet_fb_iommu_hw.h, SweetSurface.cpp in place.
Every edit is anchored (exactly one match) so a changed base fails loudly instead of silently."""
import re, sys
D = sys.argv[1].rstrip('/') + '/'

def rd(f): return open(D + f).read()
def wr(f, s): open(D + f, 'w').write(s)

def rep(s, a, b, cnt=1, tag=''):
    n = s.count(a)
    if n != cnt: raise SystemExit('ANCHOR %s: %d matches (want %d): %r' % (tag, n, cnt, a[:120]))
    return s.replace(a, b)

def func_span(s, header):
    """span of a top-level function whose definition line starts with header (e.g. 'AppleCLCD::snap('), including the return-type line
    above it and the comment block directly above that."""
    i = s.find('\n' + header)
    if i < 0 or s.find('\n' + header, i + 1) >= 0: raise SystemExit('FUNC %s: not exactly one' % header)
    start = s.rfind('\n', 0, i) + 1          # return-type line
    while True:                               # adjacent comment block above
        pe = start - 1
        if pe <= 0: break
        ps = s.rfind('\n', 0, pe) + 1
        line = s[ps:pe].strip()
        if line.startswith('//'): start = ps; continue
        if line.endswith('*/'):
            k = ps
            while '/*' not in s[k:s.find('\n', k)]:
                k = s.rfind('\n', 0, k - 1) + 1
            start = k
            continue
        break
    b = s.find('{', i)
    depth = 0; k = b
    while True:
        c = s[k]
        if c == '{': depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0: break
        k += 1
    end = k + 1
    while end < len(s) and s[end] == '\n': end += 1
    return start, end

def delete_func(s, header):
    a, b = func_span(s, header)
    return s[:a] + s[b:]

def delete_line(s, needle, cnt=1):
    lines = s.split('\n'); hit = [i for i, l in enumerate(lines) if needle in l]
    if len(hit) != cnt: raise SystemExit('LINE %r: %d matches' % (needle, len(hit)))
    for i in reversed(hit): del lines[i]
    return '\n'.join(lines)

# ============================================================================ SweetFramebuffer.cpp
f = 'SweetFramebuffer.cpp'; s = rd(f)
# --- CPU composition / copy / pixel access functions: deleted
for h in ('AppleCLCD::snap(', 'AppleCLCD::noteDistinct(', 'AppleCLCD::cpuAllowed(', 'AppleCLCD::dropNext(', 'AppleCLCD::ovlSelect(',
          'AppleCLCD::dblEnable(', 'AppleCLCD::prepNext(', 'AppleCLCD::poolAlias(', 'AppleCLCD::hwRestore(',
          'AppleCLCD::hwTestPoll(', 'AppleCLCD::hwTestFinish(', 'AppleCLCD::scanRestoreRegs(', 'AppleCLCD::scanStep(', 'AppleCLCD::scanBegin(',
          'AppleCLCD::scanPoll(', 'AppleCLCD::presentCopy(', 'sweet_fb_rows_hash(', 'sweet_fb_black_outside('):
    s = delete_func(s, h)
for d in ('	uint8_t      *snap(const uint32_t want[6], uint32_t info[8]);   /* 0296 */',
          '	bool          cpuAllowed(void);',
          '	bool        presentCopy(const uint8_t *rec, SweetSurface *const surfaces[SWEET_FB_LAYERS]);',
          '	bool        dblEnable(void);', '	bool        ovlSelect(uint8_t idx);', '	void        prepNext(void);', '	void        dropNext(void);',
          '	void        noteDistinct(uintptr_t dst);', '	bool        scanBegin(IOTimerEventSource *sender);', '	void        scanPoll(IOTimerEventSource *sender);',
          '	void        scanStep(int event);', '	void        scanRestoreRegs(void);', '	void        hwRestore(const char *why);',
          '	uintptr_t   poolAlias(SweetSurface *s);', '	void        hwTestPoll(IOTimerEventSource *sender);', '	void        hwTestFinish(const char *how);'):
    s = delete_line(s, d)
# screenshots: the panel is never read by the CPU any more; the autotest's entry point stays and answers "no picture"
s = rep(s, 'extern "C" void *sweet_fb_snap(const uint32_t want[6], uint32_t info[8]);\nextern "C" void *\nsweet_fb_snap(const uint32_t want[6], uint32_t info[8])\n{\n\treturn gSweetFBSnapOwner != NULL ? gSweetFBSnapOwner->snap(want, info) : NULL;\n}\n',
        '/* 0416: no CPU read of the panel: screenshots are gone (bsd/dev/nord2_autotest.inc gets NULL = "no picture") */\n'
        'extern "C" void *sweet_fb_snap(const uint32_t want[6], uint32_t info[8]);\n'
        'extern "C" void *\nsweet_fb_snap(const uint32_t want[6], uint32_t info[8])\n{\n\t(void)want; (void)info;\n\treturn NULL;\n}\n', tag='snap')
# 0390 blend / 0391 / 0392 copy-path switches
s = rep(s, '#include "sweet_fb_blend.h"           /* 0390 */', '#include "sweet_fb_blend.h"           /* 0390; 0416: only the layer geometry is left in it */', tag='blend include')
for n in ('if (PE_parse_boot_argn("n2fbblend"', 'if (PE_parse_boot_argn("n2fbinval"',
          'IOLog("SWEET FB: multi-layer source cache clean+invalidate', 'IOLog("SWEET FB: layer composition %s (0390)'):
    s = delete_line(s, n)
# modes 0/1, emergency, tests, selftest
s = rep(s, 'static uint32_t sweet_fb_cpu_emerg = 1;         /* 0413: kern.n2fbcpuemerg: CPU copy only if the overlay is unusable for the whole boot (0 = hold even then) */\n', '', tag='emerg var')
s = delete_line(s, 'SYSCTL_UINT(_kern, OID_AUTO, n2fbcpuemerg')
s = rep(s, '	if (v < 0 || v > (int)SWEET_FB_HW_MODE_MAX) { return EINVAL; }\n	uint32_t old = __atomic_exchange_n(&sweet_fb_hw_mode',
        '	if (v < (int)SWEET_FB_HW_SINGLE || v > (int)SWEET_FB_HW_MODE_MAX) { return EINVAL; }      /* 0416: 0 off / 1 dry run are gone (no CPU path) */\n'
        '	uint32_t old = __atomic_exchange_n(&sweet_fb_hw_mode', tag='mode range')
s = rep(s, 'IOLog("NORD2 FB HW: mode %u -> %d (sysctl kern.n2fbhw: 0 off, 1 dry run, 2 single layer, 3 two layers)\\n", old, v);',
        'IOLog("NORD2 FB HW: mode %u -> %d (sysctl kern.n2fbhw: 2 single layer, 3 multi layer; no CPU path)\\n", old, v);', tag='mode log')
a, b = s.find('	if (v >= (int)SWEET_FB_HW_SINGLE && __atomic_load_n(&sweet_fb_hw_selftest'), s.find('	return 0;\n}\nstatic int\nsweet_fb_hw_sysctl_pm')
if a < 0 or b < 0 or b < a: raise SystemExit('selftest block')
s = s[:a] + s[b:]
s = delete_func(s, 'sweet_fb_hw_sysctl_test SYSCTL_HANDLER_ARGS')
s = delete_func(s, 'sweet_fb_hw_sysctl_hold SYSCTL_HANDLER_ARGS')
for n in ('SYSCTL_PROC(_kern, OID_AUTO, n2fbhwtest,', 'SYSCTL_UINT(_kern, OID_AUTO, n2fbhwselftest,', 'SYSCTL_PROC(_kern, OID_AUTO, n2fbhwhold,'):
    i = s.find(n); j = s.find('\n', s.find('\n', i) + 1)       # two-line macros
    if i < 0: raise SystemExit('sysctl ' + n)
    s = s[:i] + s[j + 1:]
s = rep(s, '"0396 HW composition: 0 off, 1 dry run, 2 single layer (H3), 3 multi-layer (H4/H5, kern.n2fbhwlayers)"', '"0396/0416 overlay composition: 2 single layer, 3 multi-layer up to kern.n2fbhwlayers (no CPU path)"', tag='mode descr')
# start(): scan probe (CPU test pattern) gone
a = s.find('#if defined(NORD2)\n	{\n		uint32_t scan = 0;'); b = s.find('#endif\n	sweet_fb_stats_reset(&fStats, 0);', a)
if a < 0 or b < 0: raise SystemExit('scan start block')
s = s[:a] + s[b + len('#endif\n'):]
s = rep(s, '	fHwDead = fPanelBlank = fCpuEmergLogged = false; fBlankFails = 0;', '	fPanelBlank = false; fBlankFails = 0; fHwInitRetryN = 0; fHwInitRetryUs = 0;', tag='init members')
s = rep(s, '	bool          fHwDead, fPanelBlank, fCpuEmergLogged;      /* 0413 */', '	bool          fPanelBlank;      /* 0413 */\n	uint32_t      fHwInitRetryN;      /* 0416: overlay init retried with a backoff, never given up */\n	uint64_t      fHwInitRetryUs;', tag='members')
s = rep(s, '	fPoolAliasMem = NULL; fPoolAliasMap = NULL; fPoolAliasVa = 0; fPoolAliasTried = false; fPoolAliasReads = 0; fPoolAliasInvUs = 0;\n', '', tag='poolalias init')
# --- vsync: display off = DCS display off only (no CPU fade, no fallback)
a = s.find('	if (fb->fPowerOff && !sweet_fb_hw_fade_may_run(fb->fPState)) {'); b = s.find('	IOLockUnlock(fb->fLock);\n	/* 0155: the tick is only the fallback;', a)
if a < 0 or b < 0: raise SystemExit('fade block')
s = s[:a] + '''#if defined(NORD2)
	if (fb->fPowerOff) {      /* 0416: display off = DCS 0x28 (panel dark); no CPU fade, no fallback; retried every vsync until it is sent */
		if (fb->fDiag.enabled) { __atomic_fetch_add(&fb->fDiag.off, 1, __ATOMIC_RELAXED); }
		bool blank = fb->fPanelBlank;
		IOLockUnlock(fb->fLock);
		if (!blank && fb->fPState == 0) {
			static const uint8_t off[] = { 0x28 };
			bool ok = fb->dsiDcs(off, 1);
			if (ok) { fb->fPanelBlank = true; } else { fb->fBlankFails++; }
			if (ok || fb->fBlankFails <= 2 || (fb->fBlankFails % 900) == 0) {
				IOLog("NORD2 FB BLANK: DCS display off %s (fails %u)\\n", ok ? "sent: panel dark" : "FAILED, retried at the next vsync", fb->fBlankFails);
			}
		}
		if (diag_report) { fb->reportDiagnostics("timer-off"); }
		return;
	}
#endif
''' + s[b:]
# --- handOver: no test patterns, no prepared next frame
a = s.find('#if defined(NORD2)\n	/* 0396: an H2 test pattern requested by sysctl runs on the present thread'); b = s.find('	if (fPowerOff || fPState != 0 || !fDirty) { IOLockUnlock(fLock); return; }', a)
if a < 0 or b < 0: raise SystemExit('handOver block')
s = s[:a] + s[b:]
# --- presentFired
s = rep(s, '''#if defined(NORD2)
		if (fb->fPState == 7) { fb->hwTestFinish("stopped"); return; }      /* 0396 */
		if (fb->fPState == 3 || fb->fPState == 4) { fb->scanStep(SWEET_FB_SCAN_EV_STOP); }
		fb->dropNext();
#endif
''', '', tag='stopped')
s = rep(s, '	if (fb->fPState == 7) { fb->hwTestPoll(sender); return; }      /* 0396: H2 test pattern */\n', '', tag='state7')
s = rep(s, '''	if (fb->fPState == 3 || fb->fPState == 4) { fb->scanPoll(sender); return; }
	if (fb->fPState == 1 && sweet_fb_scan_due(fb->fScanState, fb->fNbReady, fb->fPresents,
	    sweet_fb_abs_to_us(mach_absolute_time()) / 1000, fb->fFirstPresentMs)) {
		if (fb->scanBegin(sender)) { return; }      /* probe frame in flight; otherwise present normally below */
	}
''', '', tag='scan present')

# --- presentFired state 5: the GPU write fence is WAITED for, never skipped (a newer swap replaces the frame meanwhile)
a = s.find('		if (busy && __atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED) >= SWEET_FB_HW_SINGLE &&'); b = s.find('		fb->fStats.busy_waits++;', a)
if a < 0 or b < 0: raise SystemExit('fence block')
s = s[:a] + """		if (busy && fb->hwCandidate(fb->fWorkRec, (SweetSurface *const *)fb->fWorkSurfaces.object)) {
			/* 0416 fence rule (kern.n2fbh6=0): the overlay never reads a surface a GPU write job is still filling, and the frame is not skipped:
			 * the present thread keeps waiting (100 us polls); only a NEWER swap (fDirty) replaces it. */
			if (!__atomic_load_n(&fb->fDirty, __ATOMIC_RELAXED)) {
				if (waited > 100000 && (fb->fHwSt.fence_skip++ % 64) == 0) {
					IOLog("NORD2 FB HW: fence wait %llu us on a layer the overlay would read (GPU write in flight), still waiting\\n", waited);
				}
				sender->setTimeoutUS(100);
				return;
			}
			fb->fHwSt.why[SWEET_FB_HW_W_FENCE]++;
			fb->presentDone(false, false, false);      /* superseded: the newer swap is taken at once */
			return;
		}
""" + s[b:]
# H6 (default): never "forced" -- the fence is waited for until it is done or a newer swap replaces the frame
s = rep(s, '	if (fHwForced) { fHwSt.fence_forced++; return false; }',
        '	if (fHwForced) {      /* 0416: never shown before its GPU writes are done: keep waiting (logged), a newer swap replaces it */\n'
        '		fHwForced = false;\n		if ((fHwSt.fence_forced++ % 256) == 0) { IOLog("NORD2 FB HW: H6 fence still pending after %llu us: waiting (no forced frame)\\n", now - arr); }\n	}', tag='h6 no force')
a = s.find('		bool wasPrepared = fb->fPrepared;'); b = s.find('#if defined(NORD2)\n		if (sweet_fb_blank_action(false, fb->fPanelBlank, true, 0) == SWEET_FB_BLANK_ON) {', a)
if a < 0 or b < 0: raise SystemExit('present body')
s = s[:a] + """		bool shown = true;
		int hw = 0;
		if (!fb->fConsolePaused && !fb->takeDisplay()) { fb->presentDone(false, false, false); return; }      /* 0413: before any frame */
		uint64_t why0[SWEET_FB_HW_W_COUNT_ENUM];
		memcpy(why0, fb->fHwSt.why, sizeof(why0));
#if defined(NORD2)
		/* 0416: the overlay is the only frame source: 1 = registers written; -1 = a GPU write on a layer is in flight: wait for the fence
		 * (state 5); 0 = not composable now: HELD (the last overlay frame stays in the panel GRAM; counted as a bug) */
		fb->fWorkHw = false;
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
				IOLog("NORD2 FB HELD (bug counter): frame not composable (%s): last overlay frame kept (held %llu: memory %llu format %llu count %llu pitch %llu range %llu state %llu missing %llu)\\n",
				    sweet_fb_hw_why_name(r), fb->fHeldN, fb->fHeld[SWEET_FB_HW_W_MEMORY], fb->fHeld[SWEET_FB_HW_W_FORMAT], fb->fHeld[SWEET_FB_HW_W_COUNT],
				    fb->fHeld[SWEET_FB_HW_W_PITCH], fb->fHeld[SWEET_FB_HW_W_RANGE], fb->fHeld[SWEET_FB_HW_W_STATE], fb->fHeld[SWEET_FB_HW_W_MISSING]);
			}
#if defined(NORD2)
			if (!fb->fNbReady) {
				IOLog("NORD2 FB HOLD: first push of the boot buffer as is (latches the panel link for the overlay; no copy)\\n");
				IOSleep(50);
				int done = nord2_display_refresh();
				if (done && fb->fDsi != NULL && fb->fMutex != NULL) { fb->fNbReady = true; }
				IOLog("NORD2 FB HOLD: link %s\\n", fb->fNbReady ? "latched" : "NOT latched (retried at the next frame)");
			}
#endif
			sweet_fb_fps_break(&fb->fFps);
			fb->fXferStart = mach_absolute_time();
			fb->presentDone(true, false, true);
			return;
		}
		uint64_t t1 = mach_absolute_time();
		(void)t0; (void)shown;
""" + s[b:]
s = rep(s, '		if (fb->fWorkHw && fb->hwFenceBusy()) {      /* 0396: a GPU write started on a layer since the plan: not shown */\n			fb->fHwSt.late_write++;\n			fb->presentDone(false, false, false);\n			return;\n		}',
        '		if (fb->fWorkHw && fb->hwFenceBusy()) {      /* 0416: a GPU write started on a layer since the plan: wait for it, then plan again */\n			fb->fHwSt.late_write++;\n			fb->fWaitStartUs = sweet_fb_abs_to_us(mach_absolute_time()); fb->fPState = 5; sender->setTimeoutUS(100);\n			return;\n		}', tag='late write 1')
s = rep(s, '		if (fb->fWorkHw && fb->hwFenceBusy()) {      /* 0396: a GPU write started on a layer during the TE wait: not shown */\n			fb->fHwSt.late_write++;\n			fb->presentDone(false, false, false);\n			return;\n		}',
        '		if (fb->fWorkHw && fb->hwFenceBusy()) {      /* 0416: a GPU write started on a layer during the TE wait: wait, plan again */\n			fb->fHwSt.late_write++;\n			fb->fWaitStartUs = sweet_fb_abs_to_us(mach_absolute_time()); fb->fPState = 5; sender->setTimeoutUS(100);\n			return;\n		}', tag='late write 2')
s = rep(s, '#if defined(NORD2)\n		if (fb->fPState == 2) { fb->prepNext(); }\n#endif\n', '', tag='prepNext poll')
s = rep(s, 'fb->fSync.pushes, total, fb->fCopyUs, fb->fLastCopyLayers, elapsed, fb->fDbl ? 1u : 0u);', 'fb->fSync.pushes, total, fb->fCopyUs, fb->fLastCopyLayers, elapsed, 0u);', tag='long log')
s = rep(s, '			if (!fDbl && fRetuneState == 1) { (void)dblEnable(); }\n', '', tag='dblEnable call')
# --- overlay init: refused -> retried with a logged backoff (1, 2, 4 .. 30 s), never given up, never a mode change
s = rep(s, '	if (fHwInitDone) { return fHwInitOk; }\n', '	if (fHwInitDone) { return fHwInitOk; }\n	if (fHwInitRetryUs != 0 && sweet_fb_abs_to_us(mach_absolute_time()) < fHwInitRetryUs) { return false; }      /* 0416: backoff */\n', tag='init retry gate')
s = rep(s, '	if (!fHwInitOk) { fHwSt.last_fault = SWEET_FB_HW_F_BASE; (void)__atomic_exchange_n(&sweet_fb_hw_mode, 0, __ATOMIC_RELAXED); fHwDead = true; }      /* 0413 */',
        '	if (!fHwInitOk) {      /* 0416: retried after 1, 2, 4 .. 30 s; frames are held meanwhile; the mode never changes */\n'
        '		fHwSt.last_fault = SWEET_FB_HW_F_BASE;\n		uint64_t ms = fHwInitRetryN < 5 ? (1000ull << fHwInitRetryN) : 30000ull;\n		fHwInitRetryN++;\n'
        '		fHwInitRetryUs = sweet_fb_abs_to_us(mach_absolute_time()) + ms * 1000ull;\n		fHwInitDone = false;\n'
        '		IOLog("NORD2 FB HW: init retry %u in %llu ms (frames held meanwhile)\\n", fHwInitRetryN, ms);\n	}', tag='init refused')
# --- hwFault: logged and counted; the overlay stays the frame source (no mode 0, no backoff, no restore)
a = s.find('\nAppleCLCD::hwFault(int f, const char *where)\n{'); b = s.find('\n}\n', a)
if a < 0: raise SystemExit('hwFault')
s = s[:a] + """
AppleCLCD::hwFault(int f, const char *where)
{
	/* 0416: logged and counted only; the overlay stays the frame source, the next frame re-programs every register (no mode 0, no restore) */
	fHwSt.faults++;
	fHwSt.last_fault = f;
	if (fHwSt.faults <= 16 || (fHwSt.faults % 256) == 0) {
		IOLog("NORD2 FB HW: FAULT %s (%s) #%llu: kept on the overlay, next frame re-programs (hw_frames=%llu ovl_int=%08x dsi=%08x race_run=%u)\\n",
		    sweet_fb_hw_fault_name(f), where, fHwSt.faults, fHwSt.frames, fHwOvlOr, fHwDsiSt, fHwRaceRun);
	}
""" + s[b:]
a = s.find('	if (mode < SWEET_FB_HW_SINGLE) {\n		if (mode == SWEET_FB_HW_DRY) {'); b = s.find('		return 0;\n	}\n', a)
if a < 0 or b < 0: raise SystemExit('dry block')
s = s[:a] + '	if (mode < SWEET_FB_HW_SINGLE) { mode = SWEET_FB_HW_DUAL; }      /* 0416: no off / dry modes */\n' + s[b + len('		return 0;\n	}\n'):]
s = rep(s, '	if (sweet_fb_iommu_frame_done() && f == SWEET_FB_HW_F_NONE) { f = SWEET_FB_HW_F_READBACK; }      /* 0401 IOMMU: M4U fault -> restore */',
        '	(void)sweet_fb_iommu_frame_done();      /* 0416: an M4U fault is logged, cleared and re-armed by the IOMMU code; the overlay goes on */', tag='iommu frame done')
s = rep(s, '	if (fHwOn && fPState == 0) { hwRestore("stop"); }        /* 0396 */\n', '', tag='stop restore')
s = delete_line(s, 'if (sweet_fb_scan_owns_panel(fScanState)) { scanStep(SWEET_FB_SCAN_EV_STOP); }')
open('/tmp/claude-1000/-home-user/b6fc0670-1b3b-4379-9bef-96b78e4e9ee7/scratchpad/mk0416.stage1.cpp', 'w').write(s)

for n in ('static uint32_t sweet_fb_hw_test_req;', 'static uint32_t sweet_fb_hw_test_last;', 'static uint32_t sweet_fb_hw_hold_ms = 3000;',
          'static uint32_t sweet_fb_hw_selftest = 0, sweet_fb_hw_selftest_done;', 'static uint32_t sweet_fb_pool_alias_mode = 1;',
          '/* 0390: n2fbblend=0 copies the SwapEnd layers', 'static uint32_t sweet_fb_blend_mode = 1;', 'static uint32_t sweet_fb_inval_mode = 1;',
          'static uint32_t sweet_fb_swaps_logged;', 'static uint64_t sweet_fb_multi_presents;'):
    s = delete_line(s, n)
s = delete_func(s, 'sweet_fb_pool_alias_sysctl SYSCTL_HANDLER_ARGS')
i = s.find('SYSCTL_PROC(_kern, OID_AUTO, n2fbpoolalias,'); j = s.find('\n', s.find('\n', i) + 1)
if i < 0: raise SystemExit('poolalias sysctl')
s = s[:i] + s[j + 1:]


# 0415's 4L code: a 4L readback mismatch / unusable 4L block holds this frame (4L off), no restore of a CPU layout
s = rep(s, '				fHw4Ok = fHw4On = false;\n				hwRestore("4l readback");\n				return false;', '				fHw4Ok = fHw4On = false;      /* 0416: frame held; later frames use the 2L slots */\n				return false;', tag='4l readback')
s = rep(s, '	} else if (u4) {\n		hwRestore("4l not usable");\n		return false;\n	}', '	} else if (u4) {\n		return false;      /* 0416: held (the planner cap keeps this from happening) */\n	}', tag='4l unusable')
# hwBackoffPoll: only the 4L capacity timer is left (no mode backoff any more)
a = s.find('\nAppleCLCD::hwBackoffPoll(void)\n{'); b = s.find('\n}\n', a)
if a < 0: raise SystemExit('backoffpoll')
s = s[:a] + """
AppleCLCD::hwBackoffPoll(void)
{
	if (fHw4OffUntilUs != 0 && fHw4OffUntilUs != ~0ull && sweet_fb_abs_to_us(mach_absolute_time()) >= fHw4OffUntilUs) {      /* 0402 */
		fHw4OffUntilUs = 0;
		IOLog("NORD2 FB HW: 4L back on (%u/4)\\n", fHw4OffN);
	}""" + s[b:]
s = rep(s, '		hw = fb->hwFrameTry(__atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED));\n		if (hw < 0) {', '		fb->hwBackoffPoll();      /* 0402: 4L capacity timer */\n		hw = fb->hwFrameTry(__atomic_load_n(&sweet_fb_hw_mode, __ATOMIC_RELAXED));\n		if (hw < 0) {', tag='backoffpoll call')
s = rep(s, '	fPixels = fSurface != NULL ? fSurface->kernelAddress() : 0;\n', '	fPixels = 0;      /* 0416: the panel memory is never mapped for the CPU (the overlay reads it as the boot buffer only) */\n', tag='no fPixels map')
s = rep(s, '	if (fLock == NULL || fClients == NULL || fPixels == 0 || fWorkLoop == NULL) {', '	if (fLock == NULL || fClients == NULL || fSurface == NULL || fWorkLoop == NULL) {', tag='start check')
wr(f, s)
print('SweetFramebuffer ok')
# ============================================================================ sweet_fb_blend.h: the CPU blend code is deleted, the geometry stays
f = 'sweet_fb_blend.h'; s = rd(f)
for h in ('sweet_fb_blend_px(', 'sweet_fb_blend_row(', 'sweet_fb_compose_row('):
    s = delete_func(s, h)
a = s.find('struct sweet_fb_blend_stats {'); b = s.find('};\n', a)
s = s[:a] + s[b + 3:]
s = rep(s, ' * 0390 (fbblend-20261112): the SwapEnd layers are composited, not copied over each other.  Pure and host-testable.',
        ' * 0416: the CPU blend/compose code of 0390 is deleted (overlay-only composition); only the layer geometry (sweet_fb_layer_clip)\n'
        ' * used by the overlay planner is left.  History of 0390 below.\n *\n * 0390 (fbblend-20261112): the SwapEnd layers are composited, not copied over each other.  Pure and host-testable.', tag='blend hdr')
wr(f, s)
print('blend ok')

# ============================================================================ sweet_fb_hw.h: CPU-path helpers deleted, blank rule without a CPU fade
f = 'sweet_fb_hw.h'; s = rd(f)
for h in ('sweet_fb_hw_fade_may_run(', 'sweet_fb_hw_test_end_action(', 'sweet_fb_hw_front_base(', 'sweet_fb_hw_cpu_allowed('):
    s = delete_func(s, h)
s = delete_line(s, 'struct sweet_fb_hw_test_end { bool select_buffer0, push, reapply; };')
x0, x1 = func_span(s, 'sweet_fb_blank_action(')
e = s.rfind('enum { SWEET_FB_BLANK_NONE = 0, SWEET_FB_BLANK_OFF, SWEET_FB_BLANK_ON, SWEET_FB_BLANK_CPU_FADE };', 0, x1)
c = s.rfind('/* 0413: panel blank on display off', 0, x1)
if e < 0 or c < 0: raise SystemExit('blank rule')
s = s[:c] + """/* 0416: panel blank (DCS 0x28 display off / 0x29 on); no CPU fade and no fallback: a failed DCS command is retried. */
enum { SWEET_FB_BLANK_NONE = 0, SWEET_FB_BLANK_OFF, SWEET_FB_BLANK_ON };
static inline int
sweet_fb_blank_action(bool power_off, bool blanked)
{
	if (power_off) { return blanked ? SWEET_FB_BLANK_NONE : SWEET_FB_BLANK_OFF; }
	return blanked ? SWEET_FB_BLANK_ON : SWEET_FB_BLANK_NONE;
}

""" + s[x1:]
wr(f, s)
f = 'SweetFramebuffer.cpp'; s = rd(f)
s = rep(s, 'if (sweet_fb_blank_action(false, fb->fPanelBlank, true, 0) == SWEET_FB_BLANK_ON) {      /* 0413: display on again (DCS 0x29) before the push */',
        'if (sweet_fb_blank_action(false, fb->fPanelBlank) == SWEET_FB_BLANK_ON) {      /* 0413: display on again (DCS 0x29) before the push */', tag='blank on')
s = rep(s, '		if (!blank && fb->fPState == 0) {', '		if (sweet_fb_blank_action(true, blank) == SWEET_FB_BLANK_OFF && fb->fPState == 0) {', tag='blank off')
s = rep(s, '	uint64_t front = (fDbl && fFront) ? fPhysB : fPhys;', '	uint64_t front = fPhys;      /* 0416: the boot buffer LK scans (no CPU double buffer any more) */', tag='init front')
wr(f, s)
print('sweet_fb_hw ok')
# ============================================================================ sweet_fb_iommu_hw.h: faults re-armed (never disabled), boot arm retried, no CPU test copy
f = 'sweet_fb_iommu_hw.h'; s = rd(f)
s = rep(s, """	n2io_wr(n2io_m4u, N2IO_INT_CTRL0, n2io_rd(n2io_m4u, N2IO_INT_CTRL0) | (1u << 12));
	n2io_tlb_all();
	n2io_auto_disable("M4U fault");
	return true;""", """	n2io_wr(n2io_m4u, N2IO_INT_CTRL0, n2io_rd(n2io_m4u, N2IO_INT_CTRL0) | (1u << 12));      /* clear */
	n2io_tlb_all();                                                                           /* re-arm: every walk starts again */
	if (n2io_rd(n2io_m4u, N2IO_PT_BASE) != n2io_ptbase_val) {                                  /* 0416: state lost: PT_BASE/TFRP back */
		n2io_wr(n2io_m4u, N2IO_PT_BASE, n2io_ptbase_val);
		n2io_wr(n2io_m4u, N2IO_TFRP, (uint32_t)(n2io_scratch_pa & 0xffffff80u));
		n2io_tlb_all();
	}
	if (n2io_st.faults <= 16 || (n2io_st.faults % 256) == 0) {
		IOLog("NORD2 FB IOMMU: fault #%u cleared, TLB invalidated, PT_BASE %08x: the IOMMU stays on (0416: no disable, no other path)\\n", n2io_st.faults,
		    n2io_rd(n2io_m4u, N2IO_PT_BASE));
	}
	return true;""", tag='fault re-arm')
s = rep(s, '		if (n2io_rd(n2io_m4u, N2IO_PT_BASE) != n2io_ptbase_val) { n2io_auto_disable("PT_BASE does not stick"); d = SWEET_FB_IOMMU_G_REFUSE; }',
        '		if (n2io_rd(n2io_m4u, N2IO_PT_BASE) != n2io_ptbase_val) { IOLog("NORD2 FB IOMMU: PT_BASE does not stick: frame held, retried\\n"); d = SWEET_FB_IOMMU_G_REFUSE; }', tag='ptbase stick')
s = delete_func(s, 'n2io_auto_disable(')
s = delete_func(s, 'sweet_fb_iommu_test_slot(')
s = delete_line(s, 'static int           n2io_test;                 /* kern.n2fbiommutest */')
s = delete_line(s, 'static IOBufferMemoryDescriptor *n2io_test_buf;')
s = delete_line(s, 'static uint32_t      n2io_test_iova;')
s = delete_line(s, 'SYSCTL_INT(_kern, OID_AUTO, n2fbiommutest,')
# boot arm: refused -> retried with a logged backoff (5, 10, 20, 40, 60 s ...), frames held meanwhile; never mode 0
s = rep(s, """	if (!ok) { n2io_mode = 0; }
	n2io_boot_pending = false;""", """	if (!ok && n2io_state == 2) { n2io_state = 0; }      /* 0416: a refusal is not final: retried */
	if (!ok) {
		static uint32_t tries;
		uint32_t sec = tries < 4 ? (5u << tries) : 60u;
		tries++;
		uint64_t dl = 0;
		clock_interval_to_deadline(sec, kSecondScale, &dl);
		thread_call_enter_delayed(n2io_boot_tc, dl);
		IOLog("NORD2 FB IOMMU: BOOT ARM REFUSED (%s): retry %u in %u s, frames held meanwhile (no other path)\\n", n2io_why, tries, sec);
		IOLockUnlock(n2io_lock);
		return;
	}
	n2io_boot_pending = false;""", tag='boot retry')
wr(f, s)
f = 'sweet_fb_iommu.h'; s = rd(f)
a = s.find('/* H2 test through the IOMMU (kern.n2fbiommutest=1)'); b = s.find('\n', s.find('uint64_t sweet_fb_iommu_test_slot(', a)) + 1
if a < 0: raise SystemExit('test slot decl')
s = s[:a] + s[b:]
wr(f, s)
f = 'sweet_fb_iommu.h'; s = rd(f)
s = rep(s, '#define SWEET_FB_IOMMU_CACHE            48u', '#define SWEET_FB_IOMMU_CACHE            96u      /* 0416: every surface goes through the IOMMU (was 48) */', tag='cache')
wr(f, s)
print('iommu ok')
# ============================================================================ SweetSurface.cpp: every surface through the IOMMU (pool not used for surfaces)
f = 'SweetSurface.cpp'; s = rd(f)
s = rep(s, 'static int           sweet_pool_enabled = 1;    /* kern.n2fbhw_pool (boot-arg n2fbhw_pool).  0411: default 1 = the physical fallback if the IOMMU is refused */',
        'static int           sweet_pool_enabled = 0;    /* kern.n2fbhw_pool (boot-arg n2fbhw_pool).  0416: default 0: every surface is shown through the IOMMU (no pool-only mode) */', tag='pool default')
wr(f, s)
print('surface ok')
