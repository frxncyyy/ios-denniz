/*
 * 0420 (memgov-20261113): NORD2 MEMGOV, a low-memory governor for a kernel without jetsam.  SPDX-License-Identifier: MIT
 *
 * Why (Fable memory study, findings 1/2/4, plan step 1.2a): our XNU is built without CONFIG_JETSAM, so kern_memorystatus.c is the
 * non-jetsam variant (max_kill_priority = IDLE, no memorystatus_pages_update, no page-shortage kills); the compressor runs mode 4
 * (swap) but swap files cannot be created ("Failed to open swap file 2"), and pressure notes reach SpringBoard only.  Nothing frees
 * memory when pages run out: the system thrashes on file-backed pages instead.  The memorystatus bands ARE kept (runningboardd's
 * MEMORYSTATUS_CMD_SET_PRIORITY_PROPERTIES is compiled in, memstat_bucket[] / p_memstat_effectivepriority are maintained), so a
 * governor can pick victims the way jetsam would: lowest band first.
 *
 * Every 250 ms: A = free + pageable external pages (the jetsam formula with dynamic paging; speculative pages are already counted in
 * vm_page_pageable_external_count, purgeable pages are logged but not counted, as in VM_CHECK_MEMORYSTATUS with the compressor on).
 *   A < low  (kern.n2memgov_low  permille of RAM, default 200): kick the SweetSurface purge pass (0417/0418) now.
 *   A < crit (kern.n2memgov_crit permille of RAM, default 120) for 2 samples in a row: one victim, at most every 2 s, from the lowest
 *            band <= kern.n2memgov_maxband (default 30 = BACKGROUND), biggest footprint first in that band.  Never: pid <= 1,
 *            P_MEMSTAT_INTERNAL / TERMINATED / LOCKED / ERROR / SKIP, P_SYSTEM, exiting procs, the protected names below.
 * kern.n2memgov: 0 off, 1 log only ("NORD2 MEMGOV would kill band=.. pid=.. name=..") = default, 2 kill.
 *
 * Pure: no kernel headers; shared by kern_exec.c and memgov-20261113/test/memgov_host_test.c.
 */
#ifndef NORD2_MEMGOV_H
#define NORD2_MEMGOV_H
/* (uint*_t / int*_t / bool come from the includer) */

enum { N2MG_OFF = 0, N2MG_LOG = 1, N2MG_ACT = 2 };
enum { N2MG_LEVEL_OK = 0, N2MG_LEVEL_LOW = 1, N2MG_LEVEL_CRIT = 2 };

#define N2MG_PERIOD_MS          250u
#define N2MG_KILL_GAP_MS        2000u   /* one victim per 2 s at most */
#define N2MG_CRIT_SAMPLES       2u      /* A under crit this many samples in a row before a victim is chosen */
#define N2MG_BOOT_GRACE_S       90u     /* no victims in the first 90 s of uptime (boot surge, launchd still spawning) */
#define N2MG_DEF_LOW_PM         200u
#define N2MG_DEF_CRIT_PM        120u
#define N2MG_DEF_MAXBAND        30      /* JETSAM_PRIORITY_BACKGROUND */
#define N2MG_BAND_FOREGROUND    100     /* JETSAM_PRIORITY_FOREGROUND: never, whatever maxband says */

/* p_memstat_state bits that exclude a process (kern_memorystatus.h values, repeated for the host test) */
#define N2MG_ST_ERROR           0x00000008u     /* P_MEMSTAT_ERROR */
#define N2MG_ST_LOCKED          0x00000010u     /* P_MEMSTAT_LOCKED */
#define N2MG_ST_TERMINATED      0x00000020u     /* P_MEMSTAT_TERMINATED */
#define N2MG_ST_INTERNAL        0x00001000u     /* P_MEMSTAT_INTERNAL (launchd, kernel: system critical) */
#define N2MG_ST_SKIP            0x00080000u     /* P_MEMSTAT_SKIP */
#define N2MG_ST_EXCLUDE         (N2MG_ST_ERROR | N2MG_ST_LOCKED | N2MG_ST_TERMINATED | N2MG_ST_INTERNAL | N2MG_ST_SKIP)

enum {
	N2MG_OK = 0,
	N2MG_NO_PID,            /* pid <= 1 (kernel_task, launchd) */
	N2MG_NO_BAND,           /* band over maxband, or >= FOREGROUND */
	N2MG_NO_STATE,          /* memstat state excludes it (internal/critical, terminating, locked, error, skip) */
	N2MG_NO_SYSTEM,         /* P_SYSTEM or exiting */
	N2MG_NO_NAME,           /* protected name */
	N2MG_NREASONS
};

/* Names never killed, whatever their band (p_name / p_comm, exact match). */
static const char *const n2mg_protected[] = {
	"kernel_task", "launchd", "SpringBoard", "backboardd", "mediaserverd", "runningboardd", "logd", "configd", "notifyd",
	"securityd", "cfprefsd", "UserEventAgent", "powerd", "thermalmonitord", "lockdownd", "CommCenter", "wifid", "locationd",
	"fseventsd", "containermanagerd", "keybagd", "audiomxd", "assertiond", "FrontBoard", "PineBoard", "watchdogd", "syslogd",
	"aggregated", "mobileassetd",
	/* 0484 (integ-20261114): our own daemons, the CommCenter helpers, code-signing/trust, and every system process seen in
	 * autocycle/runs/lag2 + r5d (p_name and its 16-character p_comm form).  The class rule below protects them anyway. */
	"vcodecd", "n2sample", "CommCenterMobileHelper", "CommCenterRootHelper", "CommCenterMobileH", "CommCenterRootHel", "amfid", "trustd",
	"AccessibilityUIServer", "AccessibilityUIS", "Camera", "MobileSlideShow", "PosterBoard", "Preferences", "ProtectedCloudKeySyncing",
	"ProtectedCloudKe", "SafariBookmarksSyncAgent", "SafariBookmarksS", "ScreenTimeAgent", "UsageTrackingAgent", "UsageTrackingAge",
	"accessoryupdaterd", "accessoryupdater", "adprivacyd", "amsaccountsd", "amsengagementd", "appleaccountd", "assistantd", "axassetsd",
	"carkitd", "chronod", "cloudpaird", "com.apple.WebKit.GPU", "contextstored", "coreduetd", "coreidvd", "dasd", "donotdisturbd",
	"driverkitd", "duetexpertd", "financed", "fmfd", "gamed", "healthd", "homed", "identityservicesd", "identityservices", "imagent",
	"mediaanalysisd", "passd", "photoanalysisd", "searchpartyd", "siriactionsd", "siriinferenced", "siriknowledged", "sociallayerd",
	"suggestd", "syncdefaultsd", "transparencyd", "triald",
};

static inline bool
n2mg_streq(const char *a, const char *b)
{
	while (*a != 0 && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static inline bool
n2mg_name_protected(const char *name)
{
	if (name == 0 || name[0] == 0) {
		return true;    /* unknown: keep */
	}
	for (unsigned i = 0; i < sizeof(n2mg_protected) / sizeof(n2mg_protected[0]); i++) {
		if (n2mg_streq(name, n2mg_protected[i])) {
			return true;
		}
	}
	return false;
}

/*
 * 0484 (integ-20261114): victim CLASS.  Default protect: only a process whose executable is a third-party application (installed bundle
 * under .../containers/Bundle/Application/) may be chosen; everything else (daemons, system apps in /Applications, our own tools) is
 * N2MG_NO_CLASS.  The class is noted at exec (kern_exec.c) by p_uniqueid, which is never reused.
 */
#define N2MG_NO_CLASS           N2MG_NREASONS   /* reported in why[] after the 0420 reasons */
#define N2MG_NREASONS2          (N2MG_NREASONS + 1)
static inline bool
n2mg_prefix(const char *s, const char *p)
{
	while (*p != 0) {
		if (*s++ != *p++) return false;
	}
	return true;
}
static inline bool
n2mg_path_is_app(const char *path)
{
	if (path == 0) return false;
	if (n2mg_prefix(path, "/private")) path += 8;
	return n2mg_prefix(path, "/var/containers/Bundle/Application/");
}
/* Kill budget: at most N2MG_BUDGET_KILLS victims within N2MG_BUDGET_WINDOW_MS, then none for N2MG_BUDGET_PAUSE_MS (lag1-like pressure
 * would otherwise choose one every 2 s).  Counts log-only picks the same way, so mode 1 shows what mode 2 would do. */
#define N2MG_BUDGET_KILLS       3u
#define N2MG_BUDGET_WINDOW_MS   60000u
#define N2MG_BUDGET_PAUSE_MS    120000u
struct n2mg_budget {
	uint64_t at[N2MG_BUDGET_KILLS];         /* times of the last picks, 0 = free slot */
	uint64_t pause_until;
};
/* true = a pick is allowed now; *paused_now set when this call starts a pause (log it once). */
static inline bool
n2mg_budget_ok(struct n2mg_budget *b, uint64_t now_ms, bool *paused_now)
{
	*paused_now = false;
	if (b->pause_until != 0) {
		if (now_ms < b->pause_until) return false;
		b->pause_until = 0;
		for (unsigned i = 0; i < N2MG_BUDGET_KILLS; i++) b->at[i] = 0;
	}
	unsigned in_window = 0;
	for (unsigned i = 0; i < N2MG_BUDGET_KILLS; i++) {
		if (b->at[i] != 0 && now_ms - b->at[i] < N2MG_BUDGET_WINDOW_MS) in_window++;
	}
	if (in_window >= N2MG_BUDGET_KILLS) {
		b->pause_until = now_ms + N2MG_BUDGET_PAUSE_MS;
		*paused_now = true;
		return false;
	}
	return true;
}
static inline void
n2mg_budget_note(struct n2mg_budget *b, uint64_t now_ms)
{
	unsigned old = 0;
	for (unsigned i = 1; i < N2MG_BUDGET_KILLS; i++) {
		if (b->at[i] < b->at[old]) old = i;
	}
	b->at[old] = now_ms ? now_ms : 1;
}

struct n2mg_limits {
	uint32_t pages, low, crit;      /* RAM pages, thresholds in pages */
};

/* Thresholds from the RAM size (gPhysSize) and the permille sysctls; crit is clamped under low, both to [1, 900] permille. */
static inline void
n2mg_limits_for(uint64_t ram_bytes, uint32_t page_bytes, uint32_t low_pm, uint32_t crit_pm, struct n2mg_limits *l)
{
	uint64_t pages = page_bytes ? ram_bytes / page_bytes : 0;
	if (low_pm == 0 || low_pm > 900) low_pm = N2MG_DEF_LOW_PM;
	if (crit_pm == 0 || crit_pm > 900) crit_pm = N2MG_DEF_CRIT_PM;
	if (crit_pm > low_pm) crit_pm = low_pm;
	l->pages = (uint32_t)pages;
	l->low = (uint32_t)(pages * low_pm / 1000);
	l->crit = (uint32_t)(pages * crit_pm / 1000);
}

static inline uint32_t
n2mg_avail(uint32_t free_pages, uint32_t pageable_ext)
{
	uint64_t a = (uint64_t)free_pages + pageable_ext;
	return a > 0xffffffffu ? 0xffffffffu : (uint32_t)a;
}

static inline int
n2mg_level(uint32_t avail, const struct n2mg_limits *l)
{
	if (avail < l->crit) return N2MG_LEVEL_CRIT;
	if (avail < l->low) return N2MG_LEVEL_LOW;
	return N2MG_LEVEL_OK;
}

struct n2mg_cand {
	int32_t  pid, band;
	uint32_t memstat_state;
	bool     system_or_exiting;
	uint64_t footprint;
	const char *name;
};

static inline uint32_t
n2mg_verdict(const struct n2mg_cand *c, int32_t maxband)
{
	if (c->pid <= 1) return N2MG_NO_PID;
	if (maxband >= N2MG_BAND_FOREGROUND) maxband = N2MG_BAND_FOREGROUND - 1;
	if (c->band < 0 || c->band > maxband) return N2MG_NO_BAND;
	if (c->memstat_state & N2MG_ST_EXCLUDE) return N2MG_NO_STATE;
	if (c->system_or_exiting) return N2MG_NO_SYSTEM;
	if (n2mg_name_protected(c->name)) return N2MG_NO_NAME;
	return N2MG_OK;
}

/* 0484: the 0420 verdict, then the class rule (app = a third-party application by its exec path). */
static inline uint32_t
n2mg_verdict_class(const struct n2mg_cand *c, int32_t maxband, bool app)
{
	uint32_t v = n2mg_verdict(c, maxband);
	if (v == N2MG_OK && !app) return N2MG_NO_CLASS;
	return v;
}

/* true = a is the better victim: lower band first, then the bigger footprint, then the higher pid (younger). */
static inline bool
n2mg_better(const struct n2mg_cand *a, const struct n2mg_cand *b)
{
	if (a->band != b->band) return a->band < b->band;
	if (a->footprint != b->footprint) return a->footprint > b->footprint;
	return a->pid > b->pid;
}

/*
 * One 250 ms sample.  Returns true when a victim should be chosen now.  *crit_run counts consecutive samples under crit;
 * last_kill_ms 0 = never.
 */
static inline bool
n2mg_should_pick(int level, uint32_t *crit_run, uint64_t now_ms, uint64_t last_kill_ms, uint32_t uptime_s)
{
	if (level != N2MG_LEVEL_CRIT) {
		*crit_run = 0;
		return false;
	}
	if (*crit_run < 0xffffffffu) (*crit_run)++;
	if (*crit_run < N2MG_CRIT_SAMPLES) return false;
	if (uptime_s < N2MG_BOOT_GRACE_S) return false;
	if (last_kill_ms != 0 && now_ms - last_kill_ms < N2MG_KILL_GAP_MS) return false;
	return true;
}
#endif /* NORD2_MEMGOV_H */
