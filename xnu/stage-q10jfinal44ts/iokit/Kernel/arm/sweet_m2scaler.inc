/*
 * sweet_m2scaler.inc -- 0672: "AppleM2ScalerCSCDriver" for the iOS frameworks, backed by the MT6893 MDP (bsd/dev/nord2_mdp.inc).
 * Included once at the end of SweetSurface.cpp (it needs SweetSurface internals: sweet_surface_copy, wire-on-demand holds).
 *
 * Why this name [V-dsc 20H392]: IOSurfaceAcceleratorCreate() does IOServiceMatching("AppleM2ScalerCSCDriver") + IOServiceOpen(type 0);
 * IOSurfaceAcceleratorTransformSurface() = IOConnectCallStructMethod(selector 1, 0x170-byte struct, no output); VideoToolbox's
 * scalerCapabilities() reads the service property "IOSurfaceAcceleratorCapabilitiesDict".  VideoToolbox (VTPixelTransferNodeScaler),
 * CMCapture, CMPhoto, ImageIO, AppleJPEG, QuartzCore look the same name up; without it they use Metal (GPU) or vImage (CPU).
 * Selectors seen in IOSurfaceAccelerator.framework: 0 capture (0x40), 1 transform (0x170), 2 abort transfers, 3 abort captures,
 * 4 custom filter (0x20), 5 get id (scalar out), 7 performance stats, 8 kernel tests, 9 histogram.
 * Struct (prepareTransformBuffersAndOptions 0x1d3c21d14): +0 source IOSurfaceID, +4 destination IOSurfaceID, +8..+0x1f completion
 * (callback, refcon, ...: 0 = synchronous), +0x20 flags (low bits "Transform" flip/rotate: at most one bit above bit 1), +0x28..
 * rectangles (16.16 fixed point, layout not decoded), then options (alpha, border fill, histogram, dither ...).
 *
 * Policy (safe by default): registered only when nord2_mdp_caps() has N2MDP_CAP_IOVA (stage 3 passed in this boot) and
 * kern.n2mdp_svc != 0; every request we do not fully understand is refused with kIOReturnUnsupported and logged (first 16 raw
 * structs are hex-dumped): asynchronous completions, non-zero rectangles, options, unknown transform bits, formats other than
 * BGRA/RGBA/420v/420f, surfaces we cannot resolve.  Supported: whole-surface to whole-surface copy / scale / colour conversion
 * with transform 0 and [I] transform bit 0x1 = horizontal flip, 0x4 = rotate 90, 0x8 = rotate 180, 0x10 = rotate 270.
 * Log lines: "NORD2 M2: ...".  Counters kern.n2m2_ok / n2m2_refused / n2m2_failed.  Kill: kern.n2mdp_svc=0 before registration,
 * or /private/var/db/nord2-mdp-off (no MDP stage runs at all).
 * 0690: requests from several client threads really run at once now (pipe 0 / pipe 2 in nord2_mdp.inc), so the surface cache and
 * the counters are under sweet_m2_lock and a cached surface in use is pinned (busy count; the 0688 cache could evict an entry - and
 * drop its wire hold - while another thread's job still used its pages); per-request timing is local (the 0687-0689 globals mixed
 * the two streams).  Log: timing lines now in pairs (both streams), + pipe / tile width / polls / overlap; every 10 s one line per
 * pipe and one per request class (size) with average lock wait, MDP time and total.
 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"     /* the IOKit/sysctl macros below warn like everywhere else; keep the warning budget */
#include "../../../bsd/dev/nord2_mdp_api.h"
#include <sys/sysctl.h>
#include <kern/clock.h>          /* 0684: absolutetime_to_nanoseconds */
#include <mach/clock_types.h>    /* NSEC_PER_SEC */
#include <string.h>
#include "sweet_m2_req.h"
static uint64_t sweet_m2_sum_wait;
static uint32_t sweet_m2_sc_hits, sweet_m2_sc_miss, sweet_m2_sc_full;      /* 0688 (0690: full = every entry busy, not cached) */
/* 0690: per-request timing (pages = page-list build, cache = CPU cache maintenance; b = nord2_mdp_blit_pages_st statistics) */
struct sweet_m2_tm { uint32_t pages_us, cache_us; struct n2mdp_bstat b; };
static IOLock *sweet_m2_lock;                               /* 0690: surface cache + counters (allocated in sweet_m2_install) */
static inline void sweet_m2_lk(void) { if (sweet_m2_lock) { IOLockLock(sweet_m2_lock); } }
static inline void sweet_m2_ulk(void) { if (sweet_m2_lock) { IOLockUnlock(sweet_m2_lock); } }
static struct { uint32_t n; uint64_t wait, hw, run; } sweet_m2_pipe[2];      /* 0690: per pipe (0 / 2) */
#define SWEET_M2_NCLS 4u
static struct { uint32_t sw, sh, dw, dh, n, p2; uint64_t wait, hw, tot; } sweet_m2_cls[SWEET_M2_NCLS];   /* 0690: per request size */
static uint32_t sweet_m2_ovl;
static uint32_t sweet_m2_failed_scaled;     /* 0691: failed scaled requests (the MDP turns scaling off; not counted for the shutdown) */

static int      sweet_m2_svc = 1;     /* 0684: ON for the controlled run image (q10jmdp14); registered only after S3 passed in this boot */
/* 0684 safety: after SWEET_M2_MAX_FAILED failed/faulted MDP transforms, or SWEET_M2_MAX_REFUSED refusals with no success, the service
 * terminates itself (new IOSurfaceAccelerator clients then get no service = the Metal/vImage path) and refuses everything still open. */
#define SWEET_M2_MAX_FAILED  3u
#define SWEET_M2_MAX_REFUSED 64u
static bool     sweet_m2_off;
static uint64_t sweet_m2_last_stats;
static uint32_t sweet_m2_calls;
static uint32_t sweet_m2_ok, sweet_m2_refused, sweet_m2_failed, sweet_m2_dumps;
SYSCTL_INT(_kern, OID_AUTO, n2mdp_svc, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_m2_svc, 0, "0672: register AppleM2ScalerCSCDriver when the MDP stages pass (0 = never)");
SYSCTL_UINT(_kern, OID_AUTO, n2m2_ok, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_m2_ok, 0, "0672: transforms done by the MDP");
SYSCTL_UINT(_kern, OID_AUTO, n2m2_refused, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_m2_refused, 0, "0672: transforms refused (caller falls back)");
SYSCTL_UINT(_kern, OID_AUTO, n2m2_failed, CTLFLAG_RD | CTLFLAG_LOCKED, &sweet_m2_failed, 0, "0672: transforms that failed in the MDP");

#define SWEET_M2_FMT_BGRA 0x42475241u   /* 'BGRA' */
#define SWEET_M2_FMT_RGBA 0x52474241u   /* 'RGBA' */
#define SWEET_M2_FMT_420V 0x34323076u   /* '420v' */
#define SWEET_M2_FMT_420F 0x34323066u   /* '420f' */

/* Resolve a surface into an MDP page-listed image.  Holds a reference (and a wire-on-demand hold) until sweet_m2_put. */
struct sweet_m2_ref { SweetSurface *s; bool held; bool cached; int sc; uint64_t *pages; uint32_t npages; struct n2mdp_pimg img; };

/* 0688: surface cache.  Building the page list (getPhysicalSegment per 4 KiB page, ~3000 pages per camera request pair) every call
 * is replaced by a cache of 8 resolved surfaces.  An entry keeps its own retain AND wire-on-demand hold, so the physical pages cannot
 * move or be freed while it is cached; it is re-used only for the same id, the same SweetSurface object, the same memory object and
 * length, and not purged.  LRU eviction unholds / releases. */
#define SWEET_M2_SC_N 8u
static struct { uint32_t id, lru, n, busy; SweetSurface *s; IOMemoryDescriptor *mem; uint64_t len; uint64_t *pages; } sweet_m2_sc[SWEET_M2_SC_N];
static uint32_t sweet_m2_sc_tick;
static void
sweet_m2_sc_evict(unsigned i)       /* under sweet_m2_lock; never for a busy entry */
{
	if (!sweet_m2_sc[i].s || sweet_m2_sc[i].busy) { return; }
	sweet_surface_wod_unhold(sweet_m2_sc[i].s);
	sweet_m2_sc[i].s->release();
	if (sweet_m2_sc[i].pages) { IOFree(sweet_m2_sc[i].pages, sweet_m2_sc[i].n * sizeof(uint64_t)); }
	bzero(&sweet_m2_sc[i], sizeof(sweet_m2_sc[i]));
}

static int
sweet_m2_get(uint32_t id, struct sweet_m2_ref *r, struct sweet_m2_tm *tm)
{
	bzero(r, sizeof(*r));
	r->sc = -1;
	SweetSurface *s = sweet_surface_copy(id);
	if (s == NULL) { return 1; }
	r->s = s;
	uint32_t f = s->pixelFormat;
	struct n2mdp_pimg *im = &r->img;
	im->w = s->width; im->h = s->height;
	if (f == SWEET_M2_FMT_BGRA || f == SWEET_M2_FMT_RGBA) {
		im->fmt = f == SWEET_M2_FMT_BGRA ? 1u : 2u;
		im->pitch[0] = s->bytesPerRow; im->off[0] = s->offset;
	} else if ((f == SWEET_M2_FMT_420V || f == SWEET_M2_FMT_420F) && s->planeCount >= 2) {
		im->fmt = 3u; im->full_range = f == SWEET_M2_FMT_420F;
		im->pitch[0] = s->planes[0].bytesPerRow; im->off[0] = s->planes[0].offset;
		im->pitch[1] = s->planes[1].bytesPerRow; im->off[1] = s->planes[1].offset;
	} else { return 2; }
	if (s->memory == NULL || s->purged) {
		sweet_m2_lk();
		for (unsigned i = 0; i < SWEET_M2_SC_N; i++) { if (sweet_m2_sc[i].s == s) { sweet_m2_sc_evict(i); } }
		sweet_m2_ulk();
		return 3;
	}
	uint64_t len = s->memory->getLength();
	uint64_t p0 = mach_absolute_time();
	sweet_m2_lk();
	for (unsigned i = 0; i < SWEET_M2_SC_N; i++) {   /* 0688: cache hit = the same surface object, memory and size */
		if (sweet_m2_sc[i].s == s && sweet_m2_sc[i].id == id && sweet_m2_sc[i].mem == s->memory && sweet_m2_sc[i].len == len) {
			sweet_m2_sc[i].lru = ++sweet_m2_sc_tick; sweet_m2_sc_hits++; sweet_m2_sc[i].busy++;   /* 0690: pinned until sweet_m2_put */
			r->cached = true; r->sc = (int)i; r->pages = sweet_m2_sc[i].pages; r->npages = sweet_m2_sc[i].n;
			im->npages = r->npages; im->pages = r->pages;
			sweet_m2_ulk();
			goto flush;
		}
	}
	sweet_m2_ulk();
	if (!sweet_surface_wod_hold(s)) { return 4; }
	r->held = true;
	{
	uint32_t n = (uint32_t)((len + 4095u) / 4096u);
	if (n == 0 || n > N2MDP_MAX_PAGES) { return 5; }
	r->pages = (uint64_t *)IOMalloc(n * sizeof(uint64_t));
	if (r->pages == NULL) { return 6; }
	r->npages = n;
	for (uint32_t k = 0; k < n; ) {
		IOByteCount seg = 0;
		uint64_t pa = s->memory->getPhysicalSegment((IOByteCount)k * 4096u, &seg, kIOMemoryMapperNone);
		if (pa == 0 || seg == 0) { return 7; }
		for (IOByteCount o = 0; o < seg && k < n; o += 4096u) { r->pages[k++] = pa + o; }
	}
	im->npages = n; im->pages = r->pages;
	{	/* 0688: hand the resolved surface to the cache (it takes the extra retain, the hold and the page array).
		 * 0690: only a free or idle entry is replaced; with every entry busy the request keeps its own hold / pages (not cached) */
		sweet_m2_lk();
		unsigned v = SWEET_M2_SC_N; uint32_t best = 0xffffffffu;
		for (unsigned i = 0; i < SWEET_M2_SC_N; i++) {
			if (!sweet_m2_sc[i].s) { v = i; break; }
			if (!sweet_m2_sc[i].busy && sweet_m2_sc[i].lru < best) { best = sweet_m2_sc[i].lru; v = i; }
		}
		if (v < SWEET_M2_SC_N) {
			sweet_m2_sc_evict(v);
			s->retain();
			sweet_m2_sc[v].id = id; sweet_m2_sc[v].s = s; sweet_m2_sc[v].mem = s->memory; sweet_m2_sc[v].len = len; sweet_m2_sc[v].n = n;
			sweet_m2_sc[v].pages = r->pages; sweet_m2_sc[v].lru = ++sweet_m2_sc_tick; sweet_m2_sc[v].busy = 1;
			r->held = false; r->cached = true; r->sc = (int)v; sweet_m2_sc_miss++;
		} else { sweet_m2_sc_full++; }
		sweet_m2_ulk();
	}
	}
flush:
	{ uint64_t pns = 0; absolutetime_to_nanoseconds(mach_absolute_time() - p0, &pns); tm->pages_us += (uint32_t)(pns / 1000u); }
	/* the MDP is not cache coherent: clean + invalidate the pixels through the kernel mapping (PIPT: covers every alias) */
	uintptr_t va = s->kernelAddress();
	uint64_t c0 = mach_absolute_time();
	if (va) { FlushPoC_DcacheRegion((vm_offset_t)va, (unsigned)len); }
	uint64_t cns = 0; absolutetime_to_nanoseconds(mach_absolute_time() - c0, &cns); tm->cache_us += (uint32_t)(cns / 1000u);
	return 0;
}
static void
sweet_m2_put(struct sweet_m2_ref *r, bool written, struct sweet_m2_tm *tm)
{
	if (r->s && written) {
		uintptr_t va = r->s->kernelAddress();
		uint64_t c0 = mach_absolute_time();
		if (va) { FlushPoC_DcacheRegion((vm_offset_t)va, (unsigned)r->s->memory->getLength()); }
		uint64_t cns = 0; absolutetime_to_nanoseconds(mach_absolute_time() - c0, &cns); tm->cache_us += (uint32_t)(cns / 1000u);
	}
	if (r->sc >= 0) { sweet_m2_lk(); if (sweet_m2_sc[r->sc].busy) { sweet_m2_sc[r->sc].busy--; } sweet_m2_ulk(); }   /* 0690: unpin */
	if (r->pages && !r->cached) { IOFree(r->pages, r->npages * sizeof(uint64_t)); }   /* 0688: cached arrays belong to the cache */
	if (r->held) { sweet_surface_wod_unhold(r->s); }
	if (r->s) { r->s->release(); }
	bzero(r, sizeof(*r));
}

/* 0685: apply a request rectangle to a resolved image (integer pixels; NV12 needs even values); 0 or 1300 */
static int
sweet_m2_crop(struct n2mdp_pimg *im, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	if (w == 0) { return 0; }                                            /* whole surface */
	if ((uint64_t)x + w > im->w || (uint64_t)y + h > im->h || w < 2u || h < 2u) { return 1300; }
	if (im->fmt == 3u) {
		if ((x | y | w | h) & 1u) { return 1301; }
		im->off[0] += y * im->pitch[0] + x; im->off[1] += (y / 2u) * im->pitch[1] + x;
	} else {
		im->off[0] += y * im->pitch[0] + x * 4u;
	}
	im->w = w; im->h = h;
	return 0;
}
static uint32_t sweet_m2_last_sfmt, sweet_m2_last_dfmt;   /* 0685: fourcc of the last surfaces seen (refusal log) */
static int
sweet_m2_blit_rect(const struct sweet_m2_req *q, struct sweet_m2_tm *tm)
{
	struct sweet_m2_ref a, b;
	int e = sweet_m2_get(q->src, &a, tm);
	if (a.s) { sweet_m2_last_sfmt = a.s->pixelFormat; }
	int f = e ? 0 : sweet_m2_get(q->dst, &b, tm);
	if (!e && b.s) { sweet_m2_last_dfmt = b.s->pixelFormat; }
	int err;
	if (e || f) { err = 1000 + (e ? e : 100 + f); }
	else if (b.img.fmt != 1u && b.img.fmt != 2u && !(b.img.fmt == 3u && (nord2_mdp_caps() & (1u << 13)))) { err = 1200; }   /* 0686: NV12 dst when verified */
	else if ((err = sweet_m2_crop(&a.img, q->sx, q->sy, q->sw, q->sh)) != 0) { }
	else if ((err = sweet_m2_crop(&b.img, q->dx, q->dy, q->dw, q->dh)) != 0) { err += 10; }
	else { err = nord2_mdp_blit_pages_st(&a.img, &b.img, q->rot, q->flip, &tm->b); }   /* 0690: per-request statistics */
	if (!e) { sweet_m2_put(&a, false, tm); }
	if (!e && !f) { sweet_m2_put(&b, err == 0, tm); }
	return err;
}
static int
sweet_m2_blit_ids(uint32_t src_id, uint32_t dst_id, uint32_t rot, uint32_t flip, uint32_t *hw_us)
{
	struct sweet_m2_req q;
	struct sweet_m2_tm tm;
	bzero(&q, sizeof(q)); bzero(&tm, sizeof(tm));
	q.src = src_id; q.dst = dst_id; q.rot = rot; q.flip = flip;
	int e = sweet_m2_blit_rect(&q, &tm);
	if (hw_us) { *hw_us = tm.b.hw_us; }
	return e;
}
/* 0685: refusal / acceptance histogram for the stats line */
enum { SWEET_M2_H_SURF = SWEET_M2_R_N, SWEET_M2_H_SFMT, SWEET_M2_H_DFMT, SWEET_M2_H_CROP, SWEET_M2_H_MDPNO, SWEET_M2_H_FAIL, SWEET_M2_H_N };
static const char *const sweet_m2_hname[SWEET_M2_H_N] = { "ok", "short", "async", "flags", "transform", "fract", "options", "rect",
	"surface", "srcfmt", "dstfmt", "crop", "mdp-unsupported", "mdp-failed" };
static uint32_t sweet_m2_hist[SWEET_M2_H_N];
static uint32_t sweet_m2_cls_rot[4], sweet_m2_cls_scaled, sweet_m2_cls_yuv;
static uint64_t sweet_m2_sum_tot, sweet_m2_sum_hw, sweet_m2_sum_map, sweet_m2_sum_cache;   /* 0686/0687 */

class AppleM2ScalerCSCDriver : public IOService {
	OSDeclareDefaultStructors(AppleM2ScalerCSCDriver);
public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
};
static AppleM2ScalerCSCDriver *sweet_m2_drv;   /* 0684: for the automatic shutdown */
OSDefineMetaClassAndStructors(AppleM2ScalerCSCDriver, IOService);

static void
sweet_m2_set_num(OSDictionary *d, const char *k, uint32_t v)
{
	OSNumber *n = OSNumber::withNumber(v, 32);
	if (n) { d->setObject(k, n); n->release(); }
}

bool
AppleM2ScalerCSCDriver::start(IOService *provider)
{
	if (!IOService::start(provider)) { return false; }
	sweet_m2_drv = this;
	setProperty(kIOUserClientClassKey, "AppleM2ScalerCSCDriverUserClient");
	OSDictionary *caps = OSDictionary::withCapacity(16);
	if (caps) {
		sweet_m2_set_num(caps, "IOSurfaceAcceleratorCapabilitiesMaxSourceWidth", 4096);
		sweet_m2_set_num(caps, "IOSurfaceAcceleratorCapabilitiesMaxSourceHeight", 4096);
		sweet_m2_set_num(caps, "IOSurfaceAcceleratorCapabilitiesMaxDestWidth", 4096);
		sweet_m2_set_num(caps, "IOSurfaceAcceleratorCapabilitiesMaxDestHeight", 4096);
		sweet_m2_set_num(caps, "IOSurfaceAcceleratorScalerVersion", 1);
		caps->setObject("IOSurfaceAcceleratorFormatInRGBA8888", kOSBooleanTrue);
		setProperty("IOSurfaceAcceleratorCapabilitiesDict", caps);
		caps->release();
	}
	setProperty("Nord2MDPCaps", (unsigned long long)nord2_mdp_caps(), 32);
	registerService();
	IOLog("NORD2 M2: AppleM2ScalerCSCDriver registered (MDP caps %#x; whole-surface transforms only, everything else refused)\n",
	    nord2_mdp_caps());
	return true;
}

class AppleM2ScalerCSCDriverUserClient : public IOUserClient {
	OSDeclareDefaultStructors(AppleM2ScalerCSCDriverUserClient);
public:
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE { terminate(); return kIOReturnSuccess; }
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args, IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(AppleM2ScalerCSCDriverUserClient, IOUserClient);

static void
sweet_m2_dump(const uint8_t *p, uint32_t n, const char *why)
{
	if (sweet_m2_dumps >= 16) { return; }
	sweet_m2_dumps++;
	char name[24]; proc_selfname(name, sizeof(name));
	IOLog("NORD2 M2: request #%u from %s %s (%s), struct %u bytes:\n", sweet_m2_dumps, name, strcmp(why, "accepted") ? "refused" : "accepted", why, n);
	for (uint32_t o = 0; o < n; o += 32) {
		const uint32_t *w = (const uint32_t *)(const void *)(p + o);
		IOLog("NORD2 M2:  +%03x %08x %08x %08x %08x %08x %08x %08x %08x\n", o, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
	}
}

IOReturn
AppleM2ScalerCSCDriverUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args, __unused IOExternalMethodDispatch *dispatch,
    __unused OSObject *target, __unused void *reference)
{
	if (selector == 5) {                          /* GetID */
		if (args->scalarOutputCount >= 1) { args->scalarOutput[0] = 1; }
		return kIOReturnSuccess;
	}
	if (selector == 2 || selector == 3) { return kIOReturnSuccess; }   /* abort transfers / captures: nothing is ever queued */
	/* 0684: periodic counters (every >= 10 s while there is traffic) and the automatic shutdown */
	sweet_m2_calls++;
	{
		uint64_t now = mach_absolute_time(), ns = 0, dt = now - sweet_m2_last_stats;
		absolutetime_to_nanoseconds(dt, &ns);
		if (sweet_m2_last_stats == 0 || ns >= 10ull * NSEC_PER_SEC) {
			sweet_m2_last_stats = now;
			IOLog("NORD2 M2: stats calls %u ok %u refused %u failed %u (scaled failed %u)%s\n", sweet_m2_calls, sweet_m2_ok, sweet_m2_refused, sweet_m2_failed, sweet_m2_failed_scaled,
			    sweet_m2_off ? " (service OFF: automatic shutdown)" : "");
			IOLog("NORD2 M2: hist ok %u async %u flags %u transform %u fract %u options %u rect %u surface %u srcfmt %u dstfmt %u crop %u "
			    "mdp-unsupported %u mdp-failed %u | ok rot0/90/180/270 %u/%u/%u/%u scaled %u yuv-src %u | last fmt %08x -> %08x\n",
			    sweet_m2_hist[0], sweet_m2_hist[SWEET_M2_R_ASYNC], sweet_m2_hist[SWEET_M2_R_FLAGS], sweet_m2_hist[SWEET_M2_R_TRANSFORM],
			    sweet_m2_hist[SWEET_M2_R_FRACT], sweet_m2_hist[SWEET_M2_R_OPTIONS], sweet_m2_hist[SWEET_M2_R_RECT], sweet_m2_hist[SWEET_M2_H_SURF],
			    sweet_m2_hist[SWEET_M2_H_SFMT], sweet_m2_hist[SWEET_M2_H_DFMT], sweet_m2_hist[SWEET_M2_H_CROP], sweet_m2_hist[SWEET_M2_H_MDPNO],
			    sweet_m2_hist[SWEET_M2_H_FAIL], sweet_m2_cls_rot[0], sweet_m2_cls_rot[1], sweet_m2_cls_rot[2], sweet_m2_cls_rot[3],
			    sweet_m2_cls_scaled, sweet_m2_cls_yuv, sweet_m2_last_sfmt, sweet_m2_last_dfmt);
			sweet_m2_lk();   /* 0690: per pipe and per request class (averages in us) */
			if (sweet_m2_pipe[0].n || sweet_m2_pipe[1].n) {
				IOLog("NORD2 M2: pipes p0 n %u wait %u hw %u run %u | p2 n %u wait %u hw %u run %u | overlap %u | surface cache full %u\n",
				    sweet_m2_pipe[0].n, sweet_m2_pipe[0].n ? (uint32_t)(sweet_m2_pipe[0].wait / sweet_m2_pipe[0].n) : 0u,
				    sweet_m2_pipe[0].n ? (uint32_t)(sweet_m2_pipe[0].hw / sweet_m2_pipe[0].n) : 0u, sweet_m2_pipe[0].n ? (uint32_t)(sweet_m2_pipe[0].run / sweet_m2_pipe[0].n) : 0u,
				    sweet_m2_pipe[1].n, sweet_m2_pipe[1].n ? (uint32_t)(sweet_m2_pipe[1].wait / sweet_m2_pipe[1].n) : 0u,
				    sweet_m2_pipe[1].n ? (uint32_t)(sweet_m2_pipe[1].hw / sweet_m2_pipe[1].n) : 0u, sweet_m2_pipe[1].n ? (uint32_t)(sweet_m2_pipe[1].run / sweet_m2_pipe[1].n) : 0u,
				    sweet_m2_ovl, sweet_m2_sc_full);
			}
			for (unsigned c = 0; c < SWEET_M2_NCLS; c++) {
				if (!sweet_m2_cls[c].n) { continue; }
				IOLog("NORD2 M2: class %ux%u -> %ux%u n %u (pipe 2: %u) avg wait %u hw %u total %u us\n", sweet_m2_cls[c].sw, sweet_m2_cls[c].sh,
				    sweet_m2_cls[c].dw, sweet_m2_cls[c].dh, sweet_m2_cls[c].n, sweet_m2_cls[c].p2, (uint32_t)(sweet_m2_cls[c].wait / sweet_m2_cls[c].n),
				    (uint32_t)(sweet_m2_cls[c].hw / sweet_m2_cls[c].n), (uint32_t)(sweet_m2_cls[c].tot / sweet_m2_cls[c].n));
			}
			sweet_m2_ulk();
		}
	}
	if (!sweet_m2_off && (sweet_m2_failed >= SWEET_M2_MAX_FAILED || (sweet_m2_ok == 0 && sweet_m2_refused >= SWEET_M2_MAX_REFUSED))) {
		sweet_m2_off = true;
		IOLog("NORD2 M2: automatic shutdown (ok %u refused %u failed %u): service terminated, clients fall back to Metal/vImage\n",
		    sweet_m2_ok, sweet_m2_refused, sweet_m2_failed);
		if (sweet_m2_drv) { sweet_m2_drv->terminate(kIOServiceAsynchronous); }
	}
	if (sweet_m2_off || !sweet_m2_svc) { sweet_m2_refused++; return kIOReturnUnsupported; }
	if (selector != 1) { sweet_m2_refused++; return kIOReturnUnsupported; }
	if (args->structureInput == NULL || args->structureInputSize < 0x68) { sweet_m2_refused++; sweet_m2_hist[SWEET_M2_R_SHORT]++; return kIOReturnBadArgument; }
	const uint8_t *p = (const uint8_t *)args->structureInput;
	uint32_t n = args->structureInputSize;
	struct sweet_m2_req q;
	bzero(&q, sizeof(q));
	int why = sweet_m2_decode(p, n, &q);                 /* 0685: rectangles / hint flags decoded (sweet_m2_req.h) */
	if (why != SWEET_M2_OK) { sweet_m2_refused++; sweet_m2_hist[why]++; sweet_m2_dump(p, n, sweet_m2_reason[why]); return kIOReturnUnsupported; }
	struct sweet_m2_tm tm;
	bzero(&tm, sizeof(tm));
	uint64_t t_beg = mach_absolute_time();
	int e = sweet_m2_blit_rect(&q, &tm);
	uint32_t us = tm.b.hw_us;
	uint64_t t_ns = 0; absolutetime_to_nanoseconds(mach_absolute_time() - t_beg, &t_ns);
	uint32_t tot_us = (uint32_t)(t_ns / 1000u);
	if (e) {
		int h = e >= 1300 ? SWEET_M2_H_CROP : e == 1200 ? SWEET_M2_H_DFMT : (e == 1002 || e == 1102) ? SWEET_M2_H_SFMT + (e == 1102 ? 1 : 0) :
		    e >= 1000 ? SWEET_M2_H_SURF : (e == ENOTSUP || e == EINVAL || e == ENODEV) ? SWEET_M2_H_MDPNO : SWEET_M2_H_FAIL;
		sweet_m2_lk();
		sweet_m2_hist[h]++;
		if (h == SWEET_M2_H_FAIL && tm.b.scaled) { sweet_m2_failed_scaled++; sweet_m2_refused++; }   /* 0691: caller falls back; camera path unaffected */
		else if (h == SWEET_M2_H_FAIL) { sweet_m2_failed++; } else { sweet_m2_refused++; }
		sweet_m2_ulk();
		if (sweet_m2_dumps < 16) {
			IOLog("NORD2 M2: transform %u -> %u (%08x -> %08x) rot %u flip %u src %ux%u+%u+%u dst %ux%u+%u+%u not done (%d %s)\n", q.src, q.dst,
			    sweet_m2_last_sfmt, sweet_m2_last_dfmt, q.rot, q.flip, q.sw, q.sh, q.sx, q.sy, q.dw, q.dh, q.dx, q.dy, e, sweet_m2_hname[h]);
			sweet_m2_dump(p, n, sweet_m2_hname[h]);
		}
		return kIOReturnUnsupported;
	}
	uint32_t src = q.src, dst = q.dst, rot = q.rot, flip = q.flip;
	const struct n2mdp_bstat *b = &tm.b;
	sweet_m2_lk();   /* 0690: counters / sums are shared by the client threads */
	sweet_m2_hist[0]++; sweet_m2_cls_rot[q.rot & 3u]++;
	if (q.sw && q.dw && ((q.rot & 1u) ? (q.sw != q.dh || q.sh != q.dw) : (q.sw != q.dw || q.sh != q.dh))) { sweet_m2_cls_scaled++; }
	if (sweet_m2_last_sfmt == SWEET_M2_FMT_420V || sweet_m2_last_sfmt == SWEET_M2_FMT_420F) { sweet_m2_cls_yuv++; }
	bool dump = sweet_m2_dumps < 16;
	sweet_m2_sum_wait += b->lockwait_us;
	sweet_m2_sum_tot += tot_us; sweet_m2_sum_hw += us; sweet_m2_sum_map += b->map_us; sweet_m2_sum_cache += tm.cache_us;   /* 0686: timing (hw = MDP busy, total = whole request incl. page lists, IOVA map, cache maintenance) */
	uint32_t ok = ++sweet_m2_ok;
	unsigned pq = b->pipe ? 1u : 0u;
	sweet_m2_pipe[pq].n++; sweet_m2_pipe[pq].wait += b->lockwait_us; sweet_m2_pipe[pq].hw += us; sweet_m2_pipe[pq].run += b->run_us;
	if (b->overlap) { sweet_m2_ovl++; }
	for (unsigned c = 0; c < SWEET_M2_NCLS; c++) {
		if (sweet_m2_cls[c].n && (sweet_m2_cls[c].sw != q.sw || sweet_m2_cls[c].sh != q.sh || sweet_m2_cls[c].dw != q.dw || sweet_m2_cls[c].dh != q.dh)) { continue; }
		sweet_m2_cls[c].sw = q.sw; sweet_m2_cls[c].sh = q.sh; sweet_m2_cls[c].dw = q.dw; sweet_m2_cls[c].dh = q.dh;
		sweet_m2_cls[c].n++; sweet_m2_cls[c].p2 += pq; sweet_m2_cls[c].wait += b->lockwait_us; sweet_m2_cls[c].hw += us; sweet_m2_cls[c].tot += tot_us;
		break;
	}
	uint32_t a_wait = (uint32_t)(sweet_m2_sum_wait / ok), a_hw = (uint32_t)(sweet_m2_sum_hw / ok), a_map = (uint32_t)(sweet_m2_sum_map / ok);
	uint32_t a_cache = (uint32_t)(sweet_m2_sum_cache / ok), a_tot = (uint32_t)(sweet_m2_sum_tot / ok), sch = sweet_m2_sc_hits, scm = sweet_m2_sc_miss;
	sweet_m2_ulk();
	if (dump) { sweet_m2_dump(p, n, "accepted"); }
	/* 0690: the first 4, then pairs (ok % 128 == 0 and 1: the two request streams alternate) */
	if (ok <= 4 || (ok & 127u) <= 1u) {
		IOLog("NORD2 M2: timing pages %u compile %u lockwait %u (avg %u) | %ux%u -> %ux%u tiles %u: %u %u %u %u us | pipe %u tile %u px polls %u ovl %u "
		    "(surface cache hit %u miss %u)\n", tm.pages_us, b->compile_us, b->lockwait_us, a_wait, q.sw, q.sh, q.dw, q.dh, b->tiles,
		    b->tile_us[0], b->tile_us[1], b->tile_us[2], b->tile_us[3], b->pipe, b->tile_px, b->polls, b->overlap, sch, scm);
		IOLog("NORD2 M2: transform %u -> %u (%08x -> %08x) rot %u flip %u done: hw %u map %u cache %u total %u us (ok %u, avg hw %u map %u "
		    "cache %u total %u)\n", src, dst, sweet_m2_last_sfmt, sweet_m2_last_dfmt, rot, flip, us, b->map_us, tm.cache_us, tot_us,
		    ok, a_hw, a_map, a_cache, a_tot);
	}
	return kIOReturnSuccess;
}

/* called by nord2_mdp.inc (C) after the stages, from its thread */
static void
sweet_m2_publish(uint32_t caps)
{
	static bool done;
	if (done || !(caps & (1u << 11)) || !sweet_m2_svc || sweet_m2_off) {   /* 0683: may be called again via kern.n2mdp_publish */
		IOLog("NORD2 M2: not registered (caps %#x, kern.n2mdp_svc %d)\n", caps, sweet_m2_svc);
		return;
	}
	done = true;
	AppleM2ScalerCSCDriver *d = OSTypeAlloc(AppleM2ScalerCSCDriver);
	IOService *res = IOService::getResourceService();
	if (d == NULL || res == NULL || !d->init() || !d->attach(res)) { IOLog("NORD2 M2: cannot create the service\n"); if (d) { d->release(); } return; }
	if (!d->start(res)) { d->detach(res); IOLog("NORD2 M2: start failed\n"); }
	d->release();
}

/* hooks into nord2_mdp.inc; installed from IOCoreSurfaceRoot::start */
static void
sweet_m2_install(void)
{
	if (!sweet_m2_lock) { sweet_m2_lock = IOLockAlloc(); }   /* 0690 */
	nord2_mdp_set_surface_hook(sweet_m2_blit_ids);
	nord2_mdp_set_publish_hook(sweet_m2_publish);
}
#pragma clang diagnostic pop
