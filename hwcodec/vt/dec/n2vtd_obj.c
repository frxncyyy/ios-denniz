/* SPDX-License-Identifier: MIT */
/*
 * n2vtd_obj.c -- the VideoToolbox decoder object: one CMBaseObject of class VTVideoDecoderGetClassID() per session,
 * class record version 2 as include/n2vt_dec_abi.h froze it, backed by vcodecd through the client seam.
 *
 * Life cycle (the order VideoToolbox actually uses; every claim below is from include/n2vt_dec_abi.h):
 *   factory        -> check the daemon (short, cached), CMDerivedObjectCreate
 *   startSession   -> parameter sets -> DEC_HEADER -> pixel buffer attributes -> output buffer pool registered
 *                     (HEVC, dp4: parameter sets -> OPEN -> pixel buffer attributes from the SPS; DEC_HEADER deferred)
 *   decodeFrame    -> (HEVC, first call: drop samples until an IRAP, then DEC_HEADER = parameter sets + that IRAP,
 *                     pool; on any failure hand the session to the software decoder, no picture has been emitted yet)
 *   decodeFrame    -> sample -> Annex B -> DEC_INPUT -> DEC_PICTURE* -> copy -> emit each frame token once
 *   finishDelayed  -> FLUSH, drain, emit whatever the daemon still holds (dropped where no picture ever came)
 *   invalidate     -> close the daemon session (idempotent); finalize destroys the mutex
 *
 * Output is NV12 (420v, or 420f when the SPS VUI says full range) at the display size the daemon reports; the copy
 * detiles MM21 when that is the layout (n2vtd_pixels.c).
 */
#define _POSIX_C_SOURCE 200809L
#include "n2vtd_vt.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "n2_apple.h"
#include "n2vtd_apple.h"
#include "n2vtd_client.h"
#include "n2vtd_log.h"
#include "n2vtd_nal.h"
#include "n2vtd_pixels.h"

/* opus-mm21's converter (gpu/hwcodec/mm21/n2_mm21.h, MIT) is byte-identical to libyuv's MM21ToNV12 and has the staged
 * fetch entry point this object needs for RWVC DMA buffers.  When its header is on the include path the object uses it;
 * otherwise n2vtd_pixels.c's own (differentially identical) detile runs. */
#if __has_include("n2_mm21.h")
#include "n2_mm21.h"
#define N2VTD_HAVE_N2_MM21 1
#else
#define N2VTD_HAVE_N2_MM21 0
#endif

#if defined(__APPLE__) && !defined(N2_HOST_FAKE)
#define N2VTD_WEAK __attribute__((weak_import))
#else
#define N2VTD_WEAK __attribute__((weak))
#endif

/* opus-robust: the software fallback's two imports, weak so a missing one only disables the fallback. */
extern const void *CMBaseObjectGetVTable(CMBaseObjectRef obj) N2VTD_WEAK;
extern OSStatus VTSelectAndCreateVideoDecoderInstance(uint32_t codecType, CFAllocatorRef allocator,
                                                      CFDictionaryRef decoderSpecification,
                                                      VTVideoDecoderRef *decoderOut) N2VTD_WEAK;

#define N2VTD_MAX_OUT_BUFFERS 32
/* dr4 (12 Nov 2026): emitted pixel buffers kept referenced for the next N2VTD_HOLD pictures.  Phone dr3d: pictures
 * changed AFTER the VideoToolbox callback (57 N2VTDEC_CHANGED within 40 ms; the HEVC I-frame showed the next
 * picture).  The detile is joined before the emit (n2_mm21_to_nv12_mt joins its threads), so the writer is our next
 * deliver_picture: VideoToolbox's pool hands the just-released buffer back to VTDecoderSessionCreatePixelBuffer while
 * the consumer (another process) still reads it -- the IOSurface "in use" accounting does not hold it on this port.
 * Holding our own reference keeps the pool from recycling a buffer until N2VTD_HOLD further pictures were emitted.
 * Bounded memory: N2VTD_HOLD_BYTES.  dr5: phone dr4 (52 MB cap = 4 held at 4K): 1080p H.264/HEVC 60/60 bit-exact, 4K 19/20 --
 * frame 14 overwritten by the flush burst (pictures 15..19 are emitted back to back by finishDelayedFrames while the
 * consumer still hashed 14).  Now 8 at every size up to 4K (4K NV12 12.4 MB x 8 = 99.5 MB; 1080p 25 MB); the cap only
 * shortens the ring above 4K. */
#define N2VTD_HOLD 8
#define N2VTD_HOLD_BYTES (104u * 1024u * 1024u)
#define N2VTD_MAX_PROPS 32
#define N2VTD_MAX_TOKENS 64
#define N2VTD_MAX_PARAMS_BYTES 4096
#define N2VTD_MAX_SCRATCH (64u * 1024u * 1024u)
/* dp4: non-IRAP samples dropped at the start of an HEVC session before the software decoder takes over (a stream
 * with no IRAP at its start, e.g. intra refresh; about 1 s at 30 fps) */
#define N2VTD_MAX_PRE_IRAP 30u

struct n2vtd_token {
	VTVideoDecoderFrame frame;
	uint64_t pts;
	uint8_t used;
	uint8_t drop;                   /* DoNotOutputFrame: decode but do not emit an image */
};

struct n2vtd_out {
	uint32_t bytes;
	uint8_t used;
};

struct n2vtd_state {
	pthread_mutex_t lock;
	uint32_t codec;
	uint32_t codec_type;            /* the CMVideoCodecType VideoToolbox asked the factory for ('avc1', 'hvc1', 'hev1') */
	int daemon_open;                /* OPEN_OK received: CLOSE is owed even if StartSession fails later */

	/* opus-robust: software fallback (see n2vtd_vt.h).  sw != NULL: every slot is forwarded to this decoder. */
	VTVideoDecoderRef sw;
	const n2_VTVideoDecoderVTable *sw_vt;
	/* base-class SetProperty calls seen before StartSession (VT makes two), replayed to sw; retained, last value wins */
	CFStringRef prop_key[N2VTD_MAX_PROPS];
	CFTypeRef prop_value[N2VTD_MAX_PROPS];
	int nprops;
	int invalidated;
	int lock_ready;
	VTVideoDecoderSession session;
	int have_session;
	int have_header;
	int unsupported_stream;         /* parameter sets outside 8-bit 4:2:0: frames fail per frame, documented */

	uint8_t params_annexb[N2VTD_MAX_PARAMS_BYTES];
	size_t params_bytes;
	struct n2vtd_sps_info sps;
	uint32_t nal_length_size;

	struct n2vtd_header hdr;
	uint32_t pixel_format;          /* N2_PIXFMT_420V / N2_PIXFMT_420F */

	void *conn;
	uint32_t in_bytes;
	uint8_t *au;                    /* Annex B access unit scratch (in_bytes) */
	uint8_t *au_src;                /* the sample as it arrived, before conversion */
	uint8_t *scratch;               /* one decoded picture, out_bytes (fallback path) */
	size_t scratch_bytes;
	uint8_t *staging;               /* n2_mm21 staged fetch buffer */
	size_t staging_bytes;

	struct n2vtd_out out[N2VTD_MAX_OUT_BUFFERS];
	int nout;

	struct n2vtd_token tok[N2VTD_MAX_TOKENS];
	int ntok;
	CVPixelBufferRef hold[N2VTD_HOLD];   /* dr4: recently emitted pictures, released N pictures later */
	unsigned hold_next;
	int logged_first_picture;
	/* dp1-fix (12 Nov 2026): the parameter sets are owed in-band on the next DEC_INPUT (set by every configure).  The
	 * vendor decoder keeps nothing from a DEC_HEADER that carried only SPS/PPS: without an SPS/PPS in the bitstream
	 * of the first frame every START succeeds and NOTHING is displayed (phone dp1: avc1 0 pictures; reproduced on the
	 * host with the phone's own vendor objects under decint, 176x144 and 1920x1080). */
	int ps_owed;
	int logged_ps_overflow;

	/* dp4 (12 Nov 2026): HEVC lazy configure.  The phone's vendor HEVC decoder refuses a DEC_HEADER that holds only
	 * VPS/SPS/PPS (eVDecDrvInit fail 6 -> -12911, phone dp3; host decint: VPS/SPS/PPS + the first frame gives the
	 * geometry and 8/8 pictures at 176x144 and 1080p).  StartSession therefore only OPENs and sets the pixel buffer
	 * attributes from the SPS; the first DecodeFrame that carries an IRAP sends DEC_HEADER = parameter sets + that
	 * access unit and registers the pool.  The same access unit is then submitted as the first DEC_INPUT. */
	int hdr_deferred;               /* OPEN done, DEC_HEADER owed with the first IRAP */
	uint32_t pre_irap_skipped;      /* samples dropped while waiting for that IRAP */
	CMVideoFormatDescriptionRef fmt;        /* retained at StartSession: a software handover at the first frame */
	uint32_t out_w, out_h;          /* size in the pixel buffer attributes (= the emitted CVPixelBuffers) */
	int attrs_set;
	uint8_t *hdrbuf;                /* parameter sets + access unit for an HEVC DEC_HEADER (in_bytes) */
};

static struct n2vtd_stats g_stats;
static pthread_mutex_t g_stats_lock = PTHREAD_MUTEX_INITIALIZER;

/* The counters are process-wide and sessions can run on several threads (VideoToolbox does that), so every update
 * takes this lock.  Tests read the struct after joining their threads. */
static void stats_inc(uint64_t *field, uint64_t n)
{
	pthread_mutex_lock(&g_stats_lock);
	*field += n;
	pthread_mutex_unlock(&g_stats_lock);
}

const struct n2vtd_stats *n2vtd_vt_stats(void)
{
	return &g_stats;
}

void n2vtd_vt_stats_reset(void)
{
	pthread_mutex_lock(&g_stats_lock);
	memset(&g_stats, 0, sizeof(g_stats));
	pthread_mutex_unlock(&g_stats_lock);
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* The client seam                                                                                                    */
/* ------------------------------------------------------------------------------------------------------------------ */

static const struct n2vtd_client *g_client;

/* DEC05's adapter, when this build has client/dec/n2vcd_dec.h and the rev-2 protocol header.  Weak: absent in a build
 * without them, and then the protocol stand-in is the default. */
extern const struct n2vtd_client *n2vtd_client_dec05(void) N2VTD_WEAK;

int n2vtd_client_install(const struct n2vtd_client *client)
{
	if (client) {
		if (!client->connect || !client->dec_open || !client->dec_header || !client->dec_add_pool ||
		    !client->dec_input || !client->dec_picture || !client->dec_release || !client->buffer_rw ||
		    !client->dec_free_buffers || !client->dec_flush || !client->dec_close || !client->disconnect)
			return -1;
		g_client = client;
	} else {
		g_client = (n2vtd_client_dec05 != NULL) ? n2vtd_client_dec05() : n2vtd_client_proto();
	}
	n2vtd_vt_availability_reset();
	return 0;
}

const struct n2vtd_client *n2vtd_client_current(void)
{
	if (!g_client)
		g_client = (n2vtd_client_dec05 != NULL) ? n2vtd_client_dec05() : n2vtd_client_proto();
	return g_client;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Availability probe: one short HELLO, cached for two seconds                                               */
/* ------------------------------------------------------------------------------------------------------------------ */

#define N2VTD_PROBE_TIMEOUT_MS 250u
#define N2VTD_PROBE_CACHE_MS 2000u

static pthread_mutex_t g_probe_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_probe_state;               /* 0 = unknown, 1 = available, 2 = not available */
static uint64_t g_probe_ms;

static uint64_t monotonic_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

void n2vtd_vt_availability_reset(void)
{
	pthread_mutex_lock(&g_probe_lock);
	g_probe_state = 0;
	g_probe_ms = 0;
	pthread_mutex_unlock(&g_probe_lock);
}

int n2vtd_vt_available(void)
{
	const struct n2vtd_client *cl;
	uint64_t now;
	int available;

	pthread_mutex_lock(&g_probe_lock);
	now = monotonic_ms();
	if (g_probe_state != 0 && now - g_probe_ms < N2VTD_PROBE_CACHE_MS) {
		available = (g_probe_state == 1);
		pthread_mutex_unlock(&g_probe_lock);
		return available;
	}
	cl = n2vtd_client_current();
	{
		void *conn = NULL;
		int rc = cl->connect(cl->ctx, &conn, NULL, NULL, N2VTD_PROBE_TIMEOUT_MS);

		if (rc == 0 && conn && cl->disconnect)
			cl->disconnect(cl->ctx, conn);
		available = (rc == 0);
	}
	g_probe_state = available ? 1 : 2;
	g_probe_ms = now;
	if (available)
		stats_inc(&g_stats.probe_ok, 1);
	else
		stats_inc(&g_stats.probe_fail, 1);
	pthread_mutex_unlock(&g_probe_lock);
	return available;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Small helpers                                                                                                      */
/* ------------------------------------------------------------------------------------------------------------------ */

static int osstatus_of(int client_rc)
{
	switch (client_rc) {
	case 0:
		return 0;
	case N2VTD_CLIENT_E_TIMEOUT:
	case N2VTD_CLIENT_E_SESSION:
	case N2VTD_CLIENT_ABSENT:
	case N2VTD_CLIENT_E_BUSY:
	case N2VTD_CLIENT_E_RESTART:
		return N2_kVTVideoDecoderNotAvailableNowErr;
	case N2VTD_CLIENT_E_NOMEM:
		return N2_kVTAllocationFailedErr;
	case N2VTD_CLIENT_E_HW:
		return N2_kVTVideoDecoderMalfunctionErr;
	default:
		return N2_kVTVideoDecoderBadDataErr;
	}
}

static void stats_status(OSStatus status)
{
	pthread_mutex_lock(&g_stats_lock);
	g_stats.last_osstatus = status;
	pthread_mutex_unlock(&g_stats_lock);
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Session teardown                                                                                                   */
/* ------------------------------------------------------------------------------------------------------------------ */

/* dr4: drop every held picture (teardown, geometry change). */
static void release_held_pictures(struct n2vtd_state *st)
{
	unsigned i;

	for (i = 0; i < N2VTD_HOLD; i++) {
		if (st->hold[i]) {
			CFRelease((CFTypeRef)st->hold[i]);
			st->hold[i] = NULL;
		}
	}
	st->hold_next = 0;
}

/* dr4: keep `pb` (our reference is transferred) until hold_count() later pictures were emitted. */
static void hold_picture(struct n2vtd_state *st, CVPixelBufferRef pb)
{
	size_t bytes = (size_t)st->hdr.width * st->hdr.height * 3u / 2u;
	unsigned n = bytes ? (unsigned)(N2VTD_HOLD_BYTES / bytes) : N2VTD_HOLD;
	unsigned slot;

	if (n > N2VTD_HOLD)
		n = N2VTD_HOLD;
	if (n < 2)
		n = 2;
	slot = st->hold_next % n;
	if (st->hold[slot])
		CFRelease((CFTypeRef)st->hold[slot]);
	st->hold[slot] = pb;
	st->hold_next = (slot + 1u) % n;
}

/* Free the registered output buffers and the input buffer through the client; the daemon forgets them on CLOSE. */
static void release_buffers(struct n2vtd_state *st)
{
	int i;

	release_held_pictures(st);

	for (i = 0; i < N2VTD_MAX_OUT_BUFFERS; i++)
		memset(&st->out[i], 0, sizeof(st->out[i]));
	st->nout = 0;
	st->in_bytes = 0;
	free(st->au);
	st->au = NULL;
	free(st->au_src);
	st->au_src = NULL;
	free(st->scratch);
	st->scratch = NULL;
	st->scratch_bytes = 0;
	free(st->staging);
	st->staging = NULL;
	st->staging_bytes = 0;
	free(st->hdrbuf);
	st->hdrbuf = NULL;
}

static void close_session(struct n2vtd_state *st)
{
	const struct n2vtd_client *cl = n2vtd_client_current();

	if (!st->conn)
		return;
	/* opus-robust: CLOSE whenever OPEN succeeded, not only for a fully started session.  A StartSession that failed
	 * after OPEN_OK (DEC_HEADER refused, pool, attributes) used to disconnect without CLOSE; the relay gives the daemon
	 * no close notice, so vcodecd kept that session (BUSY for everyone) until its 10 s idle drop. */
	if ((st->have_session || st->daemon_open) && cl->dec_close)
		(void)cl->dec_close(cl->ctx, st->conn);
	release_buffers(st);
	if (cl->disconnect)
		cl->disconnect(cl->ctx, st->conn);
	st->conn = NULL;
	st->have_session = 0;
	st->have_header = 0;
	st->daemon_open = 0;
	st->hdr_deferred = 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Parameter sets                                                                                                     */
/* ------------------------------------------------------------------------------------------------------------------ */

/* Load the parameter sets of `fmt` into st->params_annexb and parse the first SPS.  Returns 0 or a negative OSStatus. */
static OSStatus load_params(struct n2vtd_state *st, CMFormatDescriptionRef fmt)
{
	uint8_t blob[N2VTD_MAX_PARAMS_BYTES];
	struct n2vtd_params p;
	int rc, bytes;

	rc = n2vtd_params_load(fmt, st->codec, blob, sizeof(blob), &p);
	if (rc != 0)
		return N2_kVTVideoDecoderUnsupportedDataFormatErr;
	bytes = n2vtd_params_to_annexb(&p, st->params_annexb, sizeof(st->params_annexb));
	if (bytes <= 0)
		return N2_kVTVideoDecoderUnsupportedDataFormatErr;
	st->params_bytes = (size_t)bytes;
	st->nal_length_size = p.length_size;
	if (p.nsps > 0) {
		struct n2vtd_sps_info info;
		if (n2vtd_sps_parse(st->codec, p.sps[0].data, p.sps[0].size, &info) == 0) {
			st->sps = info;
			st->unsupported_stream = !(info.chroma_format_idc == 1 && info.bit_depth_luma == 8 &&
			                           info.bit_depth_chroma == 8 && !info.interlaced);
			st->pixel_format = info.full_range ? N2_PIXFMT_420F : N2_PIXFMT_420V;
			return 0;
		}
	}
	/* No SPS we can read: let the daemon tell us the geometry; default to video range 420v.  The stream is not
	 * marked unsupported here because an unreadable SPS is not evidence of an unsupported stream. */
	memset(&st->sps, 0, sizeof(st->sps));
	st->unsupported_stream = 0;
	st->pixel_format = N2_PIXFMT_420V;
	return 0;
}

static int params_are(const struct n2vtd_state *st, CMFormatDescriptionRef fmt)
{
	uint8_t blob[N2VTD_MAX_PARAMS_BYTES];
	uint8_t annexb[N2VTD_MAX_PARAMS_BYTES];
	struct n2vtd_params p;
	int bytes;

	if (n2vtd_params_load(fmt, st->codec, blob, sizeof(blob), &p) != 0)
		return 0;
	bytes = n2vtd_params_to_annexb(&p, annexb, sizeof(annexb));
	if (bytes <= 0 || (size_t)bytes != st->params_bytes)
		return 0;
	return memcmp(annexb, st->params_annexb, st->params_bytes) == 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Session bring-up                                                                                                   */
/* ------------------------------------------------------------------------------------------------------------------ */

static OSStatus set_pixel_attributes(struct n2vtd_state *st)
{
	CFMutableDictionaryRef attrs, iosurface;
	CFNumberRef number;
	int32_t v;
	OSStatus status;

	attrs = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
	                                  &kCFTypeDictionaryValueCallBacks);
	if (!attrs)
		return N2_kVTAllocationFailedErr;
	v = (int32_t)st->pixel_format;
	number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &v);
	if (!number) {
		CFRelease(attrs);
		return N2_kVTAllocationFailedErr;
	}
	CFDictionarySetValue(attrs, kCVPixelBufferPixelFormatTypeKey, number);
	CFRelease(number);
	v = (int32_t)st->out_w;
	number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &v);
	if (number) {
		CFDictionarySetValue(attrs, kCVPixelBufferWidthKey, number);
		CFRelease(number);
	}
	v = (int32_t)st->out_h;
	number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &v);
	if (number) {
		CFDictionarySetValue(attrs, kCVPixelBufferHeightKey, number);
		CFRelease(number);
	}
	iosurface = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
	                                      &kCFTypeDictionaryValueCallBacks);
	if (iosurface) {
		CFDictionarySetValue(attrs, kCVPixelBufferIOSurfacePropertiesKey, iosurface);
		CFRelease(iosurface);
	}
	status = VTDecoderSessionSetPixelBufferAttributes(st->session, attrs);
	CFRelease(attrs);
	if (status == 0)
		st->attrs_set = 1;
	return status;
}

/* dp4: decide the output size after a DEC_HEADER.  Returns 1 when the pixel buffer attributes must be (re)sent.
 * `lazy` (the HEVC header at the first IRAP): the attributes were already sent at StartSession from the SPS display
 * size (VideoToolbox rebuilds its pool lazily from the last attributes, facts/VT_DECODER_ABI.md), so they are kept when
 * the vendor agrees, and also when the vendor reports a slightly larger size (coded-size padding: the copy crops to
 * the SPS size).  Any other difference means the SPS size was wrong: the vendor's geometry wins.  Not lazy (H.264
 * StartSession, reconfigure after `changed`): the vendor's geometry, attributes always sent, as before dp4. */
static int adopt_geometry(struct n2vtd_state *st, int lazy)
{
	uint32_t w = st->hdr.width, h = st->hdr.height;

	if (lazy && st->attrs_set && st->out_w && st->out_h) {
		if (w == st->out_w && h == st->out_h)
			return 0;
		if (w >= st->out_w && h >= st->out_h && w - st->out_w < 64u && h - st->out_h < 64u) {
			n2vtd_log("%08x: vendor geometry %ux%u, output stays %ux%u (padding cropped)", st->codec, w, h,
			          st->out_w, st->out_h);
			return 0;
		}
		n2vtd_log("%08x: vendor geometry %ux%u replaces %ux%u from the SPS", st->codec, w, h, st->out_w,
		          st->out_h);
	}
	st->out_w = w;
	st->out_h = h;
	return 1;
}

/* Send DEC_HEADER with `hb` (H.264: the parameter sets; HEVC: parameter sets + an access unit with a slice) and
 * register the output buffer pool.  On entry st->conn is open.  `lazy`: see adopt_geometry. */
static OSStatus configure_session(struct n2vtd_state *st, const uint8_t *hb, uint32_t hn, int lazy)
{
	const struct n2vtd_client *cl = n2vtd_client_current();
	struct n2vtd_header hdr;
	uint32_t i;
	OSStatus status;
	int rc;

	if (!st->conn || st->params_bytes == 0 || !hb || hn == 0)
		return N2_kVTVideoDecoderNotAvailableNowErr;
	rc = cl->dec_header(cl->ctx, st->conn, hb, hn, &hdr);
	if (rc != 0) {
		stats_inc(&g_stats.client_errors, 1);
		return osstatus_of(rc);
	}
	if ((hdr.format != N2VTD_FMT_MM21 && hdr.format != N2VTD_FMT_NV12) || hdr.dpb_frames > N2VTD_MAX_OUT_BUFFERS ||
	    hdr.out_bytes > N2VTD_MAX_SCRATCH || hdr.y_bytes == 0 || hdr.y_bytes > hdr.out_bytes)
		return N2_kVTVideoDecoderMalfunctionErr;
	if (hdr.width == 0 || hdr.height == 0 || hdr.width > hdr.stride || hdr.height > hdr.buf_h)
		return N2_kVTVideoDecoderMalfunctionErr;
	st->hdr = hdr;
	if (st->scratch_bytes < hdr.out_bytes) {
		uint8_t *p = (uint8_t *)realloc(st->scratch, hdr.out_bytes);
		if (!p)
			return N2_kVTAllocationFailedErr;
		st->scratch = p;
		st->scratch_bytes = hdr.out_bytes;
	}
#if N2VTD_HAVE_N2_MM21
	if (hdr.format == N2VTD_FMT_MM21 && hdr.buf_h >= 2) {
		struct n2_mm21_frame frame;
		size_t need;

		frame.y = NULL;
		frame.uv = NULL;
		frame.y_stride = hdr.stride;
		frame.uv_stride = (hdr.out_bytes - hdr.stride * hdr.buf_h) / (hdr.buf_h / 2u);
		frame.buf_h = hdr.buf_h;
		need = n2_mm21_staging_bytes(&frame, 0, hdr.width);
		if (need > st->staging_bytes) {
			uint8_t *p = (uint8_t *)realloc(st->staging, need);
			if (!p)
				return N2_kVTAllocationFailedErr;
			st->staging = p;
			st->staging_bytes = need;
		}
	}
#endif
	rc = cl->dec_add_pool(cl->ctx, st->conn, &hdr);
	if (rc != 0) {
		stats_inc(&g_stats.client_errors, 1);
		return osstatus_of(rc);
	}
	for (i = 0; i < hdr.dpb_frames && i < N2VTD_MAX_OUT_BUFFERS; i++) {
		st->out[i].used = 1;
		st->out[i].bytes = hdr.out_bytes;
		st->nout++;
	}
	if (adopt_geometry(st, lazy)) {
		status = set_pixel_attributes(st);
		if (status != 0)
			return status;
	}
	st->have_header = 1;
	st->ps_owed = 1;
	return 0;
}

/* dp4: the HEVC DEC_HEADER payload: the format's parameter sets (Annex B) followed by one converted access unit that
 * holds a slice (host decint: exactly what makes the vendor's eVDecDrvInit succeed).  The parameter sets go in front
 * even when the AU carries its own: repeating them is legal and covers an AU with an SPS but no VPS.  Returns 0 and
 * the buffer, or -1 when it does not fit OPEN_OK.in_buf_bytes (the daemon's DEC_HEADER limit). */
static int hevc_header_payload(struct n2vtd_state *st, const uint8_t *au, uint32_t au_bytes, const uint8_t **out,
                               uint32_t *n)
{
	uint32_t pb = (uint32_t)st->params_bytes;

	if (pb == 0 || au_bytes == 0 || (uint64_t)pb + au_bytes > st->in_bytes)
		return -1;
	if (!st->hdrbuf) {
		st->hdrbuf = (uint8_t *)malloc(st->in_bytes);
		if (!st->hdrbuf)
			return -1;
	}
	memcpy(st->hdrbuf, st->params_annexb, pb);
	memcpy(st->hdrbuf + pb, au, au_bytes);
	*out = st->hdrbuf;
	*n = pb + au_bytes;
	return 0;
}

/* dp1-fix: put the session's parameter sets (Annex B, from the format description) in front of the converted access
 * unit in st->au when the vendor needs them in-band: on the first input after a (re)configure, and on every sync sample
 * (H.264 IDR, HEVC IRAP: a seek target after a flush) that carries no SPS of its own.  This is what the phone-proven
 * standalone probe does (n2vcd_dprobe sends the stream's first access unit with its SPS/PPS).  Repeating identical
 * parameter sets in-band is legal in both standards.  *bytes is updated; the AU is left alone when it would not fit. */
static void prepend_params(struct n2vtd_state *st, uint32_t *bytes, uint64_t *types)
{
	int hevc = st->codec == N2VTD_CODEC_HVC1;
	uint64_t sps_bit = (uint64_t)1 << (hevc ? 33 : 7);
	uint64_t sync_bits = hevc ? ((uint64_t)0xff << 16) : ((uint64_t)1 << 5);
	uint32_t pb = (uint32_t)st->params_bytes;

	if (pb == 0 || (*types & sps_bit) || !(st->ps_owed || (*types & sync_bits)))
		return;
	if ((uint64_t)*bytes + pb > st->in_bytes) {
		if (!st->logged_ps_overflow) {
			st->logged_ps_overflow = 1;
			n2vtd_log("%08x: access unit of %u bytes leaves no room for the %u bytes of parameter sets", st->codec,
			          *bytes, pb);
		}
		return;
	}
	memmove(st->au + pb, st->au, *bytes);
	memcpy(st->au, st->params_annexb, pb);
	*bytes += pb;
	*types |= sps_bit;
	stats_inc(&g_stats.params_inband, 1);
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Frame output                                                                                                       */
/* ------------------------------------------------------------------------------------------------------------------ */

static struct n2vtd_token *find_token(struct n2vtd_state *st, uint64_t pts)
{
	int i;

	for (i = 0; i < N2VTD_MAX_TOKENS; i++)
		if (st->tok[i].used && st->tok[i].pts == pts)
			return &st->tok[i];
	return NULL;
}

static struct n2vtd_token *add_token(struct n2vtd_state *st, VTVideoDecoderFrame frame, uint64_t pts, int drop)
{
	int i;

	for (i = 0; i < N2VTD_MAX_TOKENS; i++) {
		if (!st->tok[i].used) {
			st->tok[i].used = 1;
			st->tok[i].drop = (uint8_t)(drop ? 1 : 0);
			st->tok[i].frame = frame;
			st->tok[i].pts = pts;
			st->ntok++;
			return &st->tok[i];
		}
	}
	return NULL;
}

static void finish_token(struct n2vtd_state *st, struct n2vtd_token *t, OSStatus status, uint32_t flags,
                         CVImageBufferRef image)
{
	OSStatus rc = VTDecoderSessionEmitDecodedFrame(st->session, t->frame, status, flags, image);

	(void)rc;
	t->used = 0;
	if (st->ntok > 0)
		st->ntok--;
	if (status == 0 && !(flags & N2_kVTDecodeInfo_FrameDropped))
		stats_inc(&g_stats.tokens_emitted, 1);
	else
		stats_inc(&g_stats.tokens_emitted_dropped, 1);
}

#if N2VTD_HAVE_N2_MM21
struct n2vtd_fetch {
	const struct n2vtd_client *cl;
	void *conn;
	uint32_t index;
};

static int mm21_fetch(void *ctx, int plane, uint32_t offset, uint32_t bytes, uint8_t *staging)
{
	struct n2vtd_fetch *f = (struct n2vtd_fetch *)ctx;

	if (!f || !f->cl || !f->cl->buffer_rw || !staging)
		return -1;
	return f->cl->buffer_rw(f->cl->ctx, f->conn, f->index, plane, offset, staging, bytes, 0) == 0 ? 0 : -1;
}
#endif

/* Emit one picture: copy the daemon's pixels into a CVPixelBuffer and hand it to VideoToolbox. */
static void deliver_picture(struct n2vtd_state *st, const struct n2vtd_picture *pic)
{
	const struct n2vtd_client *cl = n2vtd_client_current();
	struct n2vtd_token *t;
	CVPixelBufferRef pb = NULL;
	OSStatus status;

	t = find_token(st, pic->pts);
	if (!t) {
		stats_inc(&g_stats.pictures_orphan, 1);
		(void)cl->dec_release(cl->ctx, st->conn, pic->index);
		return;
	}
	if (pic->index >= N2VTD_MAX_OUT_BUFFERS || !st->out[pic->index].used) {
		stats_inc(&g_stats.pictures_orphan, 1);
		finish_token(st, t, N2_kVTVideoDecoderMalfunctionErr, 0, NULL);
		return;
	}
	if (t->drop) {
		(void)cl->dec_release(cl->ctx, st->conn, pic->index);
		finish_token(st, t, 0, N2_kVTDecodeInfo_FrameDropped, NULL);
		return;
	}
	if (pic->format != N2VTD_FMT_MM21 && pic->format != N2VTD_FMT_NV12) {
		stats_inc(&g_stats.pictures_unknown_format, 1);
		(void)cl->dec_release(cl->ctx, st->conn, pic->index);
		finish_token(st, t, N2_kVTVideoDecoderUnsupportedDataFormatErr, N2_kVTDecodeInfo_FrameDropped, NULL);
		return;
	}
	if (pic->width != st->hdr.width || pic->height != st->hdr.height || pic->stride > st->hdr.stride ||
	    pic->buf_h > st->hdr.buf_h) {
		/* A picture larger than the buffer pool describes: the daemon and the header disagree. */
		stats_inc(&g_stats.pictures_unknown_format, 1);
		(void)cl->dec_release(cl->ctx, st->conn, pic->index);
		finish_token(st, t, N2_kVTVideoDecoderMalfunctionErr, N2_kVTDecodeInfo_FrameDropped, NULL);
		return;
	}
	status = VTDecoderSessionCreatePixelBuffer(st->session, NULL, &pb);
	if (status != 0 || !pb) {
		(void)cl->dec_release(cl->ctx, st->conn, pic->index);
		finish_token(st, t, status != 0 ? status : N2_kVTAllocationFailedErr, N2_kVTDecodeInfo_FrameDropped, NULL);
		return;
	}
	if (CVPixelBufferLockBaseAddress(pb, 0) != 0) {
		CFRelease(pb);
		(void)cl->dec_release(cl->ctx, st->conn, pic->index);
		finish_token(st, t, N2_kVTAllocationFailedErr, N2_kVTDecodeInfo_FrameDropped, NULL);
		return;
	}
	{
		struct n2vtd_src_plane y, c;
		uint8_t *dy = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 0);
		uint8_t *dc = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pb, 1);
		size_t ystride = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
		size_t dst_cstride = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
		size_t src_cstride = pic->stride;
		/* dp4: the CVPixelBuffer is out_w x out_h; never larger than the picture (adopt_geometry), smaller when the
		 * vendor reports a padded size. */
		uint32_t cw = (st->out_w && st->out_w < pic->width) ? st->out_w : pic->width;
		uint32_t ch = (st->out_h && st->out_h < pic->height) ? st->out_h : pic->height;
		int ok = -1;

		/* The daemon reports one buffer's total bytes; the chroma plane pitch is what is left after the luma
		 * plane.  For MM21 it must be a 16-byte tile multiple, which the converters check again. */
		if (pic->format == N2VTD_FMT_MM21 && pic->buf_h >= 2)
			src_cstride = (st->out[pic->index].bytes - (size_t)pic->stride * pic->buf_h) / (pic->buf_h / 2u);
#if N2VTD_HAVE_N2_MM21
		/* opus-decint (phone vc27: detile is the largest client cost, 8.8 ms per 4K picture single-threaded): read each
		 * plane with ONE RWVC_DMA_RW (the staged path issued one per tile row: 2 x 68 at 4K) and detile on 4 threads
		 * straight into the locked CVPixelBuffer.  The staged path stays as the fallback when the scratch is short. */
		if (dy && dc && pic->format == N2VTD_FMT_MM21 && st->scratch &&
		    st->scratch_bytes >= st->out[pic->index].bytes &&
		    st->out[pic->index].bytes > (size_t)pic->stride * pic->buf_h &&
		    cl->buffer_rw(cl->ctx, st->conn, pic->index, 0, 0, st->scratch, pic->stride * pic->buf_h, 0) == 0 &&
		    cl->buffer_rw(cl->ctx, st->conn, pic->index, 1, 0, st->scratch + (size_t)pic->stride * pic->buf_h,
		                  (uint32_t)(st->out[pic->index].bytes - (size_t)pic->stride * pic->buf_h), 0) == 0) {
			struct n2_mm21_frame frame;
			struct n2_nv12_out out;

			frame.y = st->scratch;
			frame.uv = st->scratch + (size_t)pic->stride * pic->buf_h;
			frame.y_stride = pic->stride;
			frame.uv_stride = (uint32_t)src_cstride;
			frame.buf_h = pic->buf_h;
			out.y = dy;
			out.uv = dc;
			out.y_pitch = ystride;
			out.uv_pitch = dst_cstride;
			ok = n2_mm21_to_nv12_mt(&frame, &out, 0, 0, cw, ch, 4u) == 0 ? 0 : -1;
		} else if (dy && dc && pic->format == N2VTD_FMT_MM21) {
			struct n2_mm21_frame frame;
			struct n2_nv12_out out;
			struct n2vtd_fetch f;
			size_t need;

			frame.y = NULL;
			frame.uv = NULL;
			frame.y_stride = pic->stride;
			frame.uv_stride = (uint32_t)src_cstride;
			frame.buf_h = pic->buf_h;
			need = n2_mm21_staging_bytes(&frame, 0, pic->width);
			if (need > st->staging_bytes) {
				uint8_t *p = (uint8_t *)realloc(st->staging, need);
				if (p) {
					st->staging = p;
					st->staging_bytes = need;
				}
			}
			f.cl = cl;
			f.conn = st->conn;
			f.index = pic->index;
			out.y = dy;
			out.uv = dc;
			out.y_pitch = ystride;
			out.uv_pitch = dst_cstride;
			if (st->staging && st->staging_bytes >= need)
				ok = n2_mm21_to_nv12_staged(&frame, mm21_fetch, &f, st->staging, st->staging_bytes, &out,
				                            0, 0, cw, ch);
			if (ok != 0)
				ok = -1;
		} else
#endif
		if (dy && dc && cl->buffer_rw(cl->ctx, st->conn, pic->index, 0, 0, st->scratch,
		                              pic->stride * pic->buf_h, 0) == 0 &&
		    cl->buffer_rw(cl->ctx, st->conn, pic->index, 1, 0, st->scratch + (size_t)pic->stride * pic->buf_h,
		                  st->out[pic->index].bytes - pic->stride * pic->buf_h, 0) == 0) {
			y.data = st->scratch;
			y.stride = pic->stride;
			c.data = st->scratch + (size_t)pic->stride * pic->buf_h;
			c.stride = src_cstride;
			ok = n2vtd_copy_to_nv12(pic->format, &y, &c, pic->stride, pic->buf_h, 0, 0, cw, ch, dy, ystride,
			                        dc, dst_cstride);
		}
		if (ok != 0) {
			stats_inc(&g_stats.copies_failed, 1);
			CVPixelBufferUnlockBaseAddress(pb, 0);
			CFRelease(pb);
			(void)cl->dec_release(cl->ctx, st->conn, pic->index);
			finish_token(st, t, N2_kVTVideoDecoderMalfunctionErr, N2_kVTDecodeInfo_FrameDropped, NULL);
			return;
		}
	}
	CVPixelBufferUnlockBaseAddress(pb, 0);
	stats_inc(&g_stats.pictures_out, 1);
	if (!st->logged_first_picture) {
		st->logged_first_picture = 1;
		n2vtd_log("first picture pts=%llu %ux%u", (unsigned long long)pic->pts,
		          (st->out_w && st->out_w < pic->width) ? st->out_w : pic->width,
		          (st->out_h && st->out_h < pic->height) ? st->out_h : pic->height);
	}
	/* Release the daemon buffer before emitting: VideoToolbox may call back into us from the emit. */
	(void)cl->dec_release(cl->ctx, st->conn, pic->index);
	finish_token(st, t, 0, 0, (CVImageBufferRef)pb);
	hold_picture(st, pb);   /* dr4: was CFRelease(pb): the pool must not hand it back while the consumer reads it */
}

/* Drain `count` pictures from the daemon (count 0 = drain everything it has).  Returns the number delivered. */
static int drain_pictures(struct n2vtd_state *st, uint32_t count)
{
	const struct n2vtd_client *cl = n2vtd_client_current();
	uint32_t i;
	int delivered = 0;

	for (i = 0; (count == 0) || (i < count); i++) {
		struct n2vtd_picture pic;
		int rc = cl->dec_picture(cl->ctx, st->conn, &pic);

		if (rc != 0) {
			stats_inc(&g_stats.client_errors, 1);
			break;
		}
		if (!pic.have)
			break;
		deliver_picture(st, &pic);
		delivered++;
		if (count != 0 && i + 1 >= count)
			break;
	}
	return delivered;
}

/* Invalidate/finalize: VideoToolbox is tearing the session down, so the tokens are forgotten rather than emitted. */
static void clear_tokens(struct n2vtd_state *st)
{
	int i;

	for (i = 0; i < N2VTD_MAX_TOKENS; i++)
		st->tok[i].used = 0;
	st->ntok = 0;
}

/* Emit everything still held, with FrameDropped; used by FLUSH and by the error paths. */
static void drop_pending_tokens(struct n2vtd_state *st)
{
	int i;

	for (i = 0; i < N2VTD_MAX_TOKENS; i++) {
		if (!st->tok[i].used)
			continue;
		stats_inc(&g_stats.tokens_pending_dropped, 1);
		finish_token(st, &st->tok[i], 0, N2_kVTDecodeInfo_FrameDropped, NULL);
	}
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Transport bring-up                                                                                                 */
/* ------------------------------------------------------------------------------------------------------------------ */

/* One connect + OPEN.  Returns 0 or the client's negative N2VTD_CLIENT_* code (*status gets the OSStatus). */
static int open_session_once(struct n2vtd_state *st, const char *socket_path, const char *device_path,
                             OSStatus *status)
{
	const struct n2vtd_client *cl = n2vtd_client_current();
	uint32_t in_bytes = 0;
	int rc;

	*status = 0;
	rc = cl->connect(cl->ctx, &st->conn, socket_path, device_path, 0);
	if (rc != 0) {
		st->conn = NULL;
		*status = osstatus_of(rc);
		return rc;
	}
	/* The daemon's layout is always MM21 (the vendor produces nothing else for H.264).  420v vs 420f is the range
	 * of the CVPixelBuffer we create, not a daemon layout: asking for NV12 here would fail on DEC05. */
	rc = cl->dec_open(cl->ctx, st->conn, st->codec, st->sps.display_width, st->sps.display_height,
	                  N2VTD_FMT_MM21, &in_bytes);
	if (rc != 0) {
		cl->disconnect(cl->ctx, st->conn);
		st->conn = NULL;
		*status = osstatus_of(rc);
		return rc;
	}
	st->daemon_open = 1;
	if (in_bytes == 0 || in_bytes > 64u * 1024u * 1024u) {
		close_session(st);
		*status = N2_kVTVideoDecoderMalfunctionErr;
		return N2VTD_CLIENT_E_PROTO;
	}
	st->in_bytes = in_bytes;
	st->au = (uint8_t *)malloc(in_bytes);
	st->au_src = (uint8_t *)malloc(in_bytes);
	if (!st->au || !st->au_src) {
		close_session(st);
		*status = N2_kVTAllocationFailedErr;
		return N2VTD_CLIENT_E_NOMEM;
	}
	return 0;
}

/* opus-robust: bounded retry of connect + OPEN, as P04's open_with_retry (vt/n2hwenc_vt.c) does for the encoder.
 * BUSY (another session: an encoder, or a second decoder in this or another process) and BUSY/RESTART (vcodecd
 * --restart-on-switch exits 75; launchd has a fresh daemon serving ~0.3 s later) are retried every 100 ms up to the
 * budget.  A daemon that is simply absent (connect fails at the first try) is not retried: the fallback is quicker.
 * After a RESTART/BUSY answer a failing connect is retried too (the daemon is between processes). */
static _Atomic uint32_t g_start_retry_ms = 1000u;
#define N2VTD_RETRY_SLEEP_MS 100u

void n2vtd_vt_set_start_retry_ms(uint32_t ms)
{
	atomic_store(&g_start_retry_ms, ms);
}

static OSStatus open_session(struct n2vtd_state *st, const char *socket_path, const char *device_path,
                             int *client_rc_out)
{
	uint64_t t0 = monotonic_ms();
	uint32_t budget = atomic_load(&g_start_retry_ms);
	int busy_seen = 0, attempts = 0;

	for (;;) {
		OSStatus status;
		int rc = open_session_once(st, socket_path, device_path, &status);
		uint64_t spent;

		attempts++;
		if (client_rc_out)
			*client_rc_out = rc;
		if (rc == 0) {
			if (attempts > 1)
				n2vtd_log("hardware session open after %d attempts (%llu ms)", attempts,
				          (unsigned long long)(monotonic_ms() - t0));
			return 0;
		}
		if (rc == N2VTD_CLIENT_E_BUSY || rc == N2VTD_CLIENT_E_RESTART)
			busy_seen = 1;
		else if (!(busy_seen && (rc == N2VTD_CLIENT_ABSENT || rc == N2VTD_CLIENT_E_SESSION ||
		                         rc == N2VTD_CLIENT_E_TIMEOUT)))
			return status;
		spent = monotonic_ms() - t0;
		if (spent >= budget)
			return status;
		stats_inc(&g_stats.open_retries, 1);
		{
			uint64_t left = budget - spent;
			uint64_t ms = left < N2VTD_RETRY_SLEEP_MS ? left : N2VTD_RETRY_SLEEP_MS;
			struct timespec ts;

			ts.tv_sec = (time_t)(ms / 1000u);
			ts.tv_nsec = (long)((ms % 1000u) * 1000000u);
			(void)nanosleep(&ts, NULL);
		}
	}
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Software fallback (opus-robust; n2vtd_vt.h has the contract)                                                       */
/* ------------------------------------------------------------------------------------------------------------------ */

/* While one thread is inside VTSelectAndCreateVideoDecoderInstance on behalf of a fallback, this image's factories
 * refuse on THAT thread, so the selector moves on to Apple's H264SW / VCPHEVC (or whatever is next) instead of
 * creating another hardware instance.  Other threads are not affected.  One fallback creation at a time. */
static pthread_mutex_t g_sw_lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int g_sw_active;
static _Atomic(pthread_t) g_sw_owner;

int n2vtd_vt_fallback_in_progress(void)
{
	return atomic_load(&g_sw_active) && pthread_equal(atomic_load(&g_sw_owner), pthread_self());
}

static const n2_VTVideoDecoderVTable *vtable_of(VTVideoDecoderRef dec)
{
	return CMBaseObjectGetVTable ? (const n2_VTVideoDecoderVTable *)CMBaseObjectGetVTable((CMBaseObjectRef)dec)
	                             : NULL;
}

/* st->lock held.  Returns 0 when the session now runs on VideoToolbox's next decoder. */
static OSStatus start_software(struct n2vtd_state *st, VTVideoDecoderSession session, CMVideoFormatDescriptionRef fmt,
                               OSStatus hw_status, const char *why)
{
	VTVideoDecoderRef inner = NULL;
	const n2_VTVideoDecoderVTable *vt;
	OSStatus status;

	if (!VTSelectAndCreateVideoDecoderInstance || !CMBaseObjectGetVTable) {
		stats_inc(&g_stats.sw_fallback_failed, 1);
		return hw_status;
	}
	pthread_mutex_lock(&g_sw_lock);
	atomic_store(&g_sw_owner, pthread_self());
	atomic_store(&g_sw_active, 1);
	status = VTSelectAndCreateVideoDecoderInstance(st->codec_type, kCFAllocatorDefault, NULL, &inner);
	atomic_store(&g_sw_active, 0);
	pthread_mutex_unlock(&g_sw_lock);
	vt = (status == 0 && inner) ? vtable_of(inner) : NULL;
	if (!vt || !vt->baseClass || !vt->decoderClass || !vt->decoderClass->startSession ||
	    !vt->decoderClass->decodeFrame) {
		n2vtd_log("%08x: hardware unusable (%s, %d) and no software decoder (%d)", st->codec, why, (int)hw_status,
		          (int)status);
		if (inner)
			CFRelease((CFTypeRef)inner);
		stats_inc(&g_stats.sw_fallback_failed, 1);
		return hw_status;
	}
	if (vt->baseClass->setProperty) {
		int i;
		for (i = 0; i < st->nprops; i++)
			(void)vt->baseClass->setProperty((CMBaseObjectRef)inner, st->prop_key[i], st->prop_value[i]);
	}
	status = vt->decoderClass->startSession(inner, session, fmt);
	if (status != 0) {
		n2vtd_log("%08x: hardware unusable (%s, %d); software decoder StartSession failed too (%d)", st->codec, why,
		          (int)hw_status, (int)status);
		if (vt->baseClass->invalidate)
			(void)vt->baseClass->invalidate((CMBaseObjectRef)inner);
		CFRelease((CFTypeRef)inner);
		stats_inc(&g_stats.sw_fallback_failed, 1);
		return hw_status;
	}
	st->sw = inner;
	st->sw_vt = vt;
	stats_inc(&g_stats.sw_fallbacks, 1);
	n2vtd_log("%08x: hardware unusable (%s, %d): session runs on the software decoder", st->codec, why,
	          (int)hw_status);
	return 0;
}

/* The forwarding target of a slot call, retained (NULL when the session is on the hardware path).  The object lock is
 * NOT held across the forwarded call: the inner decoder has its own locking and emits frames itself. */
static VTVideoDecoderRef sw_acquire(struct n2vtd_state *st, const n2_VTVideoDecoderVTable **vt)
{
	VTVideoDecoderRef inner = NULL;

	pthread_mutex_lock(&st->lock);
	if (st->sw && !st->invalidated) {
		inner = st->sw;
		*vt = st->sw_vt;
		CFRetain((CFTypeRef)inner);
	}
	pthread_mutex_unlock(&st->lock);
	return inner;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Class slots                                                                                                        */
/* ------------------------------------------------------------------------------------------------------------------ */

static OSStatus n2vtd_start_session(VTVideoDecoderRef dec, VTVideoDecoderSession session,
                                    CMVideoFormatDescriptionRef fmt)
{
	struct n2vtd_state *st = (struct n2vtd_state *)CMBaseObjectGetDerivedStorage((CMBaseObjectRef)dec);
	OSStatus status;
	int rc, client_rc = 0;

	if (!st)
		return N2_kVTParameterErr;
	pthread_mutex_lock(&st->lock);
	if (st->invalidated) {
		pthread_mutex_unlock(&st->lock);
		return N2_kVTInvalidSessionErr;
	}
	if (st->sw || st->have_session) {
		/* VideoToolbox starts a decoder once; a second StartSession is a caller error, not a new stream. */
		pthread_mutex_unlock(&st->lock);
		return N2_kVTParameterErr;
	}
	st->session = session;
	status = load_params(st, fmt);
	if (status != 0) {
		n2vtd_log("session rejected: parameter sets unreadable (%d)", (int)status);
		stats_status(status);
		status = start_software(st, session, fmt, status, "parameter sets unreadable");
		pthread_mutex_unlock(&st->lock);
		return status;
	}
	/* opus-robust: 10-bit / non-4:2:0 / interlaced cannot be decoded by this path (they used to fail every frame).
	 * Hand them to the software decoder before touching the daemon; without one, keep the old per-frame refusal. */
	if (st->unsupported_stream &&
	    start_software(st, session, fmt, N2_kVTVideoDecoderUnsupportedDataFormatErr, "stream format") == 0) {
		stats_status(0);
		pthread_mutex_unlock(&st->lock);
		return 0;
	}
	status = open_session(st, NULL, NULL, &client_rc);
	if (status != 0) {
		stats_status(status);
		stats_inc(&g_stats.sessions_failed, 1);
		status = start_software(st, session, fmt, status,
		                        client_rc == N2VTD_CLIENT_E_BUSY ? "daemon busy" :
		                        client_rc == N2VTD_CLIENT_E_RESTART ? "daemon restarting" :
		                        client_rc == N2VTD_CLIENT_ABSENT ? "daemon absent" : "OPEN refused");
		pthread_mutex_unlock(&st->lock);
		return status;
	}
	if (st->codec == N2VTD_CODEC_HVC1) {
		/* dp4: the vendor needs a slice in the HEVC DEC_HEADER, so it waits for the first IRAP (decodeFrame).  The
		 * pixel buffer attributes are sent now, from the SPS display size, so VideoToolbox has the output size
		 * at StartSession as with H.264; the vendor's geometry is checked against it at the first frame. */
		st->fmt = (CMVideoFormatDescriptionRef)CFRetain((CFTypeRef)fmt);
		st->out_w = st->sps.display_width;
		st->out_h = st->sps.display_height;
		if (st->out_w && st->out_h) {
			status = set_pixel_attributes(st);
			if (status != 0) {
				n2vtd_log("session start failed: pixel buffer attributes (%d)", (int)status);
				stats_status(status);
				stats_inc(&g_stats.sessions_failed, 1);
				close_session(st);
				status = start_software(st, session, fmt, status, "pixel buffer attributes");
				pthread_mutex_unlock(&st->lock);
				return status;
			}
		}
		st->hdr_deferred = 1;
		st->pre_irap_skipped = 0;
		st->have_session = 1;
		stats_inc(&g_stats.sessions_started, 1);
		stats_inc(&g_stats.headers_deferred, 1);
		stats_status(0);
		n2vtd_log("session start %08x %ux%u (SPS) %s: DEC_HEADER waits for the first IRAP", st->codec, st->out_w,
		          st->out_h, st->sps.full_range ? "full-range" : "video-range");
		pthread_mutex_unlock(&st->lock);
		return 0;
	}
	rc = configure_session(st, st->params_annexb, (uint32_t)st->params_bytes, 0);
	if ((OSStatus)rc != 0) {
		n2vtd_log("session start failed in configure (%d)", (int)rc);
		stats_status(rc);
		stats_inc(&g_stats.sessions_failed, 1);
		close_session(st);
		status = start_software(st, session, fmt, rc, "DEC_HEADER/pool refused");
		pthread_mutex_unlock(&st->lock);
		return status;
	}
	st->have_session = 1;
	stats_inc(&g_stats.sessions_started, 1);
	stats_status(0);
	n2vtd_log("session start %08x %ux%u stride %u buf_h %u %s %s", st->codec, st->hdr.width, st->hdr.height,
	          st->hdr.stride, st->hdr.buf_h, st->hdr.format == N2VTD_FMT_MM21 ? "MM21" : "NV12",
	          st->sps.full_range ? "full-range" : "video-range");
	pthread_mutex_unlock(&st->lock);
	return 0;
}

/* Re-read the header after the daemon reported a geometry change (changed == 1).  `au` is the access unit that
 * triggered it (HEVC: it goes into the DEC_HEADER with the parameter sets, dp4). */
static OSStatus reconfigure_session(struct n2vtd_state *st, const uint8_t *au, uint32_t au_bytes)
{
	const struct n2vtd_client *cl = n2vtd_client_current();

	stats_inc(&g_stats.reconfigures, 1);
	if (!st->have_session)
		return N2_kVTInvalidSessionErr;
	/* Drain what the old geometry still holds, then let the daemon forget the pool. */
	drain_pictures(st, 0);
	if (cl->dec_free_buffers) {
		int rc = cl->dec_free_buffers(cl->ctx, st->conn);
		if (rc != 0) {
			stats_inc(&g_stats.client_errors, 1);
			return osstatus_of(rc);
		}
	}
	{
		int i;
		for (i = 0; i < N2VTD_MAX_OUT_BUFFERS; i++)
			memset(&st->out[i], 0, sizeof(st->out[i]));
		st->nout = 0;
	}
	st->have_header = 0;
	if (st->codec == N2VTD_CODEC_HVC1) {
		const uint8_t *hb = NULL;
		uint32_t hn = 0;

		if (hevc_header_payload(st, au, au_bytes, &hb, &hn) != 0)
			return N2_kVTVideoDecoderBadDataErr;
		return configure_session(st, hb, hn, 0);
	}
	return configure_session(st, st->params_annexb, (uint32_t)st->params_bytes, 0);
}

/* dp4: forget a token without emitting it (the frame is handed to the software decoder, which emits it). */
static void forget_token(struct n2vtd_state *st, struct n2vtd_token *t)
{
	t->used = 0;
	if (st->ntok > 0)
		st->ntok--;
}

/* dp4: st->lock held, inside decodeFrame, before the hardware emitted any picture (the HEVC DEC_HEADER at the first
 * IRAP failed, or no IRAP came).  Close the hardware session (CLOSE is sent) and start VideoToolbox's next decoder on
 * the same session, exactly as StartSession's fallback does; the software decoder sets its own pixel buffer
 * attributes and emits every token from now on.  Returns the inner decoder retained, or NULL (the session is dead:
 * later frames fail with kVTVideoDecoderNotAvailableNowErr). */
static VTVideoDecoderRef handover_software(struct n2vtd_state *st, OSStatus hw_status, const char *why,
                                           const n2_VTVideoDecoderVTable **vt)
{
	close_session(st);
	stats_inc(&g_stats.sessions_failed, 1);
	stats_status(hw_status);
	if (!st->fmt || start_software(st, st->session, st->fmt, hw_status, why) != 0 || !st->sw)
		return NULL;
	stats_inc(&g_stats.sw_handovers, 1);
	*vt = st->sw_vt;
	CFRetain((CFTypeRef)st->sw);
	return st->sw;
}

static OSStatus n2vtd_decode_frame(VTVideoDecoderRef dec, VTVideoDecoderFrame frame, CMSampleBufferRef sample,
                                   uint32_t decodeFlags, uint32_t *infoFlagsOut)
{
	struct n2vtd_state *st = (struct n2vtd_state *)CMBaseObjectGetDerivedStorage((CMBaseObjectRef)dec);
	const struct n2vtd_client *cl = n2vtd_client_current();
	CMBlockBufferRef bb;
	size_t bytes;
	uint64_t pts = 0;
	struct n2vtd_token *tok;
	struct n2vtd_input in;
	uint64_t types = 0;
	uint32_t au_bytes;
	int rc, converted;
	int drop = (decodeFlags & N2_kVTDecodeFrame_DoNotOutputFrame) ? 1 : 0;

	if (infoFlagsOut)
		*infoFlagsOut = 0;
	if (!st)
		return N2_kVTParameterErr;
	{
		const n2_VTVideoDecoderVTable *svt = NULL;
		VTVideoDecoderRef inner = sw_acquire(st, &svt);

		if (inner) {
			OSStatus fs = svt->decoderClass->decodeFrame(inner, frame, sample, decodeFlags, infoFlagsOut);
			CFRelease((CFTypeRef)inner);
			return fs;
		}
	}
	pthread_mutex_lock(&st->lock);
	if (st->invalidated) {
		pthread_mutex_unlock(&st->lock);
		return N2_kVTInvalidSessionErr;
	}
	if (!st->have_session) {
		pthread_mutex_unlock(&st->lock);
		return N2_kVTVideoDecoderNotAvailableNowErr;
	}
	if (st->unsupported_stream) {
		pthread_mutex_unlock(&st->lock);
		return N2_kVTVideoDecoderUnsupportedDataFormatErr;
	}
	bb = CMSampleBufferGetDataBuffer(sample);
	if (!bb) {
		pthread_mutex_unlock(&st->lock);
		return N2_kVTVideoDecoderBadDataErr;
	}
	bytes = CMBlockBufferGetDataLength(bb);
	if (bytes == 0 || bytes > st->in_bytes) {
		pthread_mutex_unlock(&st->lock);
		return N2_kVTVideoDecoderBadDataErr;
	}
	{
		CMTime t = CMSampleBufferGetPresentationTimeStamp(sample);
		pts = (uint64_t)t.value;
	}
	tok = add_token(st, frame, pts, drop);
	if (!tok) {
		pthread_mutex_unlock(&st->lock);
		return N2_kVTVideoDecoderMalfunctionErr;
	}
	if (CMBlockBufferCopyDataBytes(bb, 0, bytes, st->au_src) != 0) {
		finish_token(st, tok, N2_kVTVideoDecoderBadDataErr, N2_kVTDecodeInfo_FrameDropped, NULL);
		pthread_mutex_unlock(&st->lock);
		return N2_kVTVideoDecoderBadDataErr;
	}
	converted = n2vtd_sample_to_annexb(st->codec, st->au_src, bytes, st->nal_length_size, st->au, st->in_bytes,
	                                   &types);
	if (converted <= 0) {
		stats_inc(&g_stats.frames_bad, 1);
		finish_token(st, tok, N2_kVTVideoDecoderBadDataErr, N2_kVTDecodeInfo_FrameDropped, NULL);
		pthread_mutex_unlock(&st->lock);
		return N2_kVTVideoDecoderBadDataErr;
	}
	au_bytes = (uint32_t)converted;
	if (st->hdr_deferred) {
		/* dp4: HEVC lazy configure.  IRAP = NAL types 16..23 (BLA, IDR, CRA, reserved IRAP). */
		VTVideoDecoderRef inner = NULL;
		const n2_VTVideoDecoderVTable *svt = NULL;
		OSStatus hs;

		if (!(types & ((uint64_t)0xff << 16))) {
			st->pre_irap_skipped++;
			stats_inc(&g_stats.pre_irap_dropped, 1);
			if (st->pre_irap_skipped <= N2VTD_MAX_PRE_IRAP) {
				if (st->pre_irap_skipped == 1)
					n2vtd_log("%08x: first sample is not an IRAP: dropped until one arrives", st->codec);
				finish_token(st, tok, 0, N2_kVTDecodeInfo_FrameDropped, NULL);
				pthread_mutex_unlock(&st->lock);
				return 0;
			}
			forget_token(st, tok);
			inner = handover_software(st, N2_kVTVideoDecoderBadDataErr, "no IRAP in the first samples", &svt);
			hs = N2_kVTVideoDecoderBadDataErr;
		} else {
			const uint8_t *hb = NULL;
			uint32_t hn = 0;

			hs = hevc_header_payload(st, st->au, au_bytes, &hb, &hn) == 0 ? configure_session(st, hb, hn, 1)
			                                                             : N2_kVTVideoDecoderBadDataErr;
			if (hs == 0) {
				st->hdr_deferred = 0;
				stats_inc(&g_stats.headers_with_slice, 1);
				n2vtd_log("session configured at the first IRAP: %08x %ux%u stride %u buf_h %u dpb %u, output %ux%u",
				          st->codec, st->hdr.width, st->hdr.height, st->hdr.stride, st->hdr.buf_h,
				          st->hdr.dpb_frames, st->out_w, st->out_h);
			} else {
				n2vtd_log("DEC_HEADER with the first IRAP failed (%d)", (int)hs);
				stats_inc(&g_stats.client_errors, 1);
				forget_token(st, tok);
				inner = handover_software(st, hs, "DEC_HEADER/pool refused at the first IRAP", &svt);
			}
		}
		if (st->hdr_deferred || !st->have_session) {
			/* handed over (inner != NULL) or dead: this frame belongs to the software decoder, or fails (emitted
			 * with the error, as a failed DEC_INPUT is) */
			if (!inner) {
				(void)VTDecoderSessionEmitDecodedFrame(st->session, frame, hs, N2_kVTDecodeInfo_FrameDropped,
				                                       NULL);
				stats_inc(&g_stats.tokens_emitted_dropped, 1);
				pthread_mutex_unlock(&st->lock);
				return hs;
			}
			pthread_mutex_unlock(&st->lock);
			hs = svt->decoderClass->decodeFrame(inner, frame, sample, decodeFlags, infoFlagsOut);
			CFRelease((CFTypeRef)inner);
			return hs;
		}
	}
	prepend_params(st, &au_bytes, &types);
	memset(&in, 0, sizeof(in));
	rc = cl->dec_input(cl->ctx, st->conn, st->au, au_bytes, pts, &in);
	if (rc != 0) {
		n2vtd_log("DEC_INPUT failed (%d), frame dropped", rc);
		stats_inc(&g_stats.client_errors, 1);
		finish_token(st, tok, osstatus_of(rc), N2_kVTDecodeInfo_FrameDropped, NULL);
		if (rc == N2VTD_CLIENT_E_HW || rc == N2VTD_CLIENT_E_SESSION)
			st->have_session = 0;
		pthread_mutex_unlock(&st->lock);
		return osstatus_of(rc);
	}
	stats_inc(&g_stats.frames_in, 1);
	if (in.changed) {
		OSStatus status = reconfigure_session(st, st->au, au_bytes);
		if (status != 0) {
			finish_token(st, tok, status, N2_kVTDecodeInfo_FrameDropped, NULL);
			pthread_mutex_unlock(&st->lock);
			return status;
		}
		/* The input was not consumed: submit it again to the fresh pool (with the parameter sets the new
		 * configure owes, unless the AU already carries an SPS). */
		prepend_params(st, &au_bytes, &types);
		rc = cl->dec_input(cl->ctx, st->conn, st->au, au_bytes, pts, &in);
		if (rc != 0) {
			stats_inc(&g_stats.client_errors, 1);
			finish_token(st, tok, osstatus_of(rc), N2_kVTDecodeInfo_FrameDropped, NULL);
			pthread_mutex_unlock(&st->lock);
			return osstatus_of(rc);
		}
	}
	st->ps_owed = 0;
	drain_pictures(st, in.pictures_ready);
	if (tok->used && infoFlagsOut)
		*infoFlagsOut |= N2_kVTDecodeInfo_Asynchronous;
	pthread_mutex_unlock(&st->lock);
	return 0;
}

static OSStatus n2vtd_finish_delayed_frames(VTVideoDecoderRef dec)
{
	struct n2vtd_state *st = (struct n2vtd_state *)CMBaseObjectGetDerivedStorage((CMBaseObjectRef)dec);
	const struct n2vtd_client *cl = n2vtd_client_current();
	uint32_t ready = 0;
	int rc;

	if (!st)
		return N2_kVTParameterErr;
	{
		const n2_VTVideoDecoderVTable *svt = NULL;
		VTVideoDecoderRef inner = sw_acquire(st, &svt);

		if (inner) {
			OSStatus fs = 0;
			if (svt->decoderClass->version >= 2 && svt->decoderClass->finishDelayedFrames)
				fs = svt->decoderClass->finishDelayedFrames(inner);
			CFRelease((CFTypeRef)inner);
			return fs;
		}
	}
	pthread_mutex_lock(&st->lock);
	if (st->invalidated || !st->have_session || st->hdr_deferred) {
		/* dp4: hdr_deferred = no DEC_HEADER yet, the daemon holds nothing */
		drop_pending_tokens(st);
		pthread_mutex_unlock(&st->lock);
		return 0;
	}
	stats_inc(&g_stats.flushes, 1);
	rc = cl->dec_flush(cl->ctx, st->conn, &ready);
	if (rc != 0) {
		stats_inc(&g_stats.client_errors, 1);
		drop_pending_tokens(st);
		pthread_mutex_unlock(&st->lock);
		return osstatus_of(rc);
	}
	drain_pictures(st, ready ? ready : 0);
	drop_pending_tokens(st);
	pthread_mutex_unlock(&st->lock);
	return 0;
}

static Boolean n2vtd_can_accept_format(VTVideoDecoderRef dec, CMVideoFormatDescriptionRef fmt)
{
	struct n2vtd_state *st = (struct n2vtd_state *)CMBaseObjectGetDerivedStorage((CMBaseObjectRef)dec);
	Boolean ok;

	if (!st || !fmt)
		return 0;
	{
		const n2_VTVideoDecoderVTable *svt = NULL;
		VTVideoDecoderRef inner = sw_acquire(st, &svt);

		if (inner) {
			ok = 0;
			if (svt->decoderClass->version >= 2 && svt->decoderClass->canAcceptFormatDescription)
				ok = svt->decoderClass->canAcceptFormatDescription(inner, fmt);
			CFRelease((CFTypeRef)inner);
			return ok;
		}
	}
	pthread_mutex_lock(&st->lock);
	ok = (Boolean)(!st->invalidated && st->params_bytes > 0 && params_are(st, fmt));
	pthread_mutex_unlock(&st->lock);
	return ok;
}

static OSStatus n2vtd_copy_supported_properties(VTVideoDecoderRef dec, CFDictionaryRef *dictOut)
{
	struct n2vtd_state *st = dec ? (struct n2vtd_state *)CMBaseObjectGetDerivedStorage((CMBaseObjectRef)dec) : NULL;
	CFMutableDictionaryRef dict;

	if (!dictOut)
		return N2_kVTParameterErr;
	if (st) {
		const n2_VTVideoDecoderVTable *svt = NULL;
		VTVideoDecoderRef inner = sw_acquire(st, &svt);

		if (inner) {
			OSStatus fs = N2_kVTUnimplementedErr;
			*dictOut = NULL;
			if (svt->decoderClass->copySupportedPropertyDictionary)
				fs = svt->decoderClass->copySupportedPropertyDictionary(inner, dictOut);
			CFRelease((CFTypeRef)inner);
			return fs;
		}
	}
	dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
	                                 &kCFTypeDictionaryValueCallBacks);
	if (!dict)
		return N2_kVTAllocationFailedErr;
	*dictOut = dict;
	return 0;
}

/* Returning kVTUnimplementedErr is the documented way to say "apply the keys through the base class SetProperty";
 * n2vt_dec_abi.h says SetProperties may be NULL for exactly this fallback, so a non-NULL slot with this reply is
 * equivalent and keeps the slot filled as the task asks. */
static OSStatus n2vtd_set_properties(VTVideoDecoderRef dec, CFDictionaryRef properties)
{
	struct n2vtd_state *st = dec ? (struct n2vtd_state *)CMBaseObjectGetDerivedStorage((CMBaseObjectRef)dec) : NULL;

	if (st) {
		const n2_VTVideoDecoderVTable *svt = NULL;
		VTVideoDecoderRef inner = sw_acquire(st, &svt);

		if (inner) {
			/* No slot: -12782 makes VideoToolbox use base SetProperty per key, which n2vtd_set_property forwards. */
			OSStatus fs = N2_kVTUnimplementedErr;
			if (svt->decoderClass->setProperties)
				fs = svt->decoderClass->setProperties(inner, properties);
			CFRelease((CFTypeRef)inner);
			return fs;
		}
	}
	return N2_kVTUnimplementedErr;
}

static OSStatus n2vtd_copy_serializable_properties(VTVideoDecoderRef dec, CFAllocatorRef allocator,
                                                   CFDictionaryRef *dictOut)
{
	struct n2vtd_state *st = dec ? (struct n2vtd_state *)CMBaseObjectGetDerivedStorage((CMBaseObjectRef)dec) : NULL;

	if (dictOut)
		*dictOut = NULL;
	if (st) {
		const n2_VTVideoDecoderVTable *svt = NULL;
		VTVideoDecoderRef inner = sw_acquire(st, &svt);

		if (inner) {
			OSStatus fs = N2_kVTUnimplementedErr;
			if (svt->decoderClass->copySerializableProperties)
				fs = svt->decoderClass->copySerializableProperties(inner, allocator, dictOut);
			CFRelease((CFTypeRef)inner);
			return fs;
		}
	}
	return N2_kVTUnimplementedErr;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* Base class                                                                                                         */
/* ------------------------------------------------------------------------------------------------------------------ */

static OSStatus n2vtd_copy_property(CMBaseObjectRef obj, CFStringRef key, CFAllocatorRef allocator, void *valueOut)
{
	struct n2vtd_state *st = obj ? (struct n2vtd_state *)CMBaseObjectGetDerivedStorage(obj) : NULL;

	if (!valueOut)
		return N2_kVTParameterErr;
	*(CFTypeRef *)valueOut = NULL;
	if (st) {
		const n2_VTVideoDecoderVTable *svt = NULL;
		VTVideoDecoderRef inner = sw_acquire(st, &svt);

		if (inner) {
			/* The software decoder answers for itself (UsingHardwareAcceleratedVideoDecoder = false, ...). */
			OSStatus fs = N2_kCMBaseObjectError_ValueNotAvailable;
			if (svt->baseClass->copyProperty)
				fs = svt->baseClass->copyProperty((CMBaseObjectRef)inner, key, allocator, valueOut);
			CFRelease((CFTypeRef)inner);
			return fs;
		}
	}
	if (key && CFEqual(key, N2STR("UsingHardwareAcceleratedVideoDecoder"))) {
		*(CFTypeRef *)valueOut = CFRetain((CFTypeRef)kCFBooleanTrue);
		return 0;
	}
	if (key && CFEqual(key, N2STR("IsHardwareAcceleratedVideoDecoder"))) {
		*(CFTypeRef *)valueOut = CFRetain((CFTypeRef)kCFBooleanTrue);
		return 0;
	}
	return N2_kCMBaseObjectError_ValueNotAvailable;
}

static OSStatus n2vtd_set_property(CMBaseObjectRef obj, CFStringRef key, CFTypeRef value)
{
	struct n2vtd_state *st = obj ? (struct n2vtd_state *)CMBaseObjectGetDerivedStorage(obj) : NULL;
	const n2_VTVideoDecoderVTable *svt = NULL;
	VTVideoDecoderRef inner;

	if (!st || !key)
		return 0;
	inner = sw_acquire(st, &svt);
	if (inner) {
		OSStatus fs = 0;
		if (svt->baseClass->setProperty)
			fs = svt->baseClass->setProperty((CMBaseObjectRef)inner, key, value);
		CFRelease((CFTypeRef)inner);
		return fs;
	}
	/* Unknown properties are ignored by the hardware path (VideoToolbox sets plenty of keys that only matter to Apple's
	 * decoders), but the ones set before StartSession are kept: a software fallback replays them (opus-robust).
	 * dp4: for HEVC also those set before the first IRAP, since the handover can happen there. */
	pthread_mutex_lock(&st->lock);
	if ((!st->have_session || st->hdr_deferred) && !st->invalidated && value) {
		int i;
		for (i = 0; i < st->nprops; i++)
			if (CFEqual(st->prop_key[i], key))
				break;
		if (i < st->nprops) {
			CFRelease(st->prop_value[i]);
			st->prop_value[i] = CFRetain(value);
		} else if (st->nprops < N2VTD_MAX_PROPS) {
			st->prop_key[st->nprops] = (CFStringRef)CFRetain((CFTypeRef)key);
			st->prop_value[st->nprops] = CFRetain(value);
			st->nprops++;
		}
	}
	pthread_mutex_unlock(&st->lock);
	return 0;
}

/* st->lock held: let go of the software decoder and the recorded properties (invalidate first). */
static void drop_software(struct n2vtd_state *st, int release)
{
	if (st->sw && st->sw_vt && st->sw_vt->baseClass && st->sw_vt->baseClass->invalidate && !st->invalidated)
		(void)st->sw_vt->baseClass->invalidate((CMBaseObjectRef)st->sw);
	if (release) {
		if (st->sw)
			CFRelease((CFTypeRef)st->sw);
		st->sw = NULL;
		st->sw_vt = NULL;
		while (st->nprops > 0) {
			st->nprops--;
			CFRelease((CFTypeRef)st->prop_key[st->nprops]);
			CFRelease(st->prop_value[st->nprops]);
		}
	}
}

static OSStatus n2vtd_invalidate(CMBaseObjectRef obj)
{
	struct n2vtd_state *st = (struct n2vtd_state *)CMBaseObjectGetDerivedStorage(obj);

	if (!st)
		return 0;
	pthread_mutex_lock(&st->lock);
	if (!st->invalidated) {
		drop_software(st, 0);
		st->invalidated = 1;
		close_session(st);
		clear_tokens(st);
		stats_inc(&g_stats.sessions_closed, 1);
	}
	pthread_mutex_unlock(&st->lock);
	return 0;
}

static void n2vtd_finalize(CMBaseObjectRef obj)
{
	struct n2vtd_state *st = (struct n2vtd_state *)CMBaseObjectGetDerivedStorage(obj);

	if (!st)
		return;
	if (st->lock_ready) {
		pthread_mutex_lock(&st->lock);
		if (!st->invalidated) {
			drop_software(st, 0);
			st->invalidated = 1;
			close_session(st);
			clear_tokens(st);
			stats_inc(&g_stats.sessions_closed, 1);
		}
		drop_software(st, 1);
		release_held_pictures(st);   /* dr4 */
		if (st->fmt) {
			CFRelease((CFTypeRef)st->fmt);
			st->fmt = NULL;
		}
		pthread_mutex_unlock(&st->lock);
	}
	if (st->lock_ready) {
		st->lock_ready = 0;
		pthread_mutex_destroy(&st->lock);
	}
}

static CFStringRef n2vtd_copy_debug_description(CMBaseObjectRef obj)
{
	(void)obj;
	return CFStringCreateWithCString(kCFAllocatorDefault, "[n2vtd MTK VDEC decoder]", kCFStringEncodingUTF8);
}

static const n2_CMBaseClass g_n2vtd_base_class = {
	.version = 1,
	.derivedStorageSize = sizeof(struct n2vtd_state),
	.equal = NULL,
	.invalidate = n2vtd_invalidate,
	.finalize = n2vtd_finalize,
	.copyDebugDescription = n2vtd_copy_debug_description,
	.copyProperty = n2vtd_copy_property,
	.setProperty = n2vtd_set_property,
	.reserved8 = NULL,
	.reserved9 = NULL,
	.reserved10 = NULL,
};

static const n2_VTVideoDecoderClass g_n2vtd_decoder_class = {
	.version = 2,
	.startSession = n2vtd_start_session,
	.decodeFrame = n2vtd_decode_frame,
	.copySupportedPropertyDictionary = n2vtd_copy_supported_properties,
	.setProperties = n2vtd_set_properties,
	.copySerializableProperties = n2vtd_copy_serializable_properties,
	.canAcceptFormatDescription = n2vtd_can_accept_format,
	.finishDelayedFrames = n2vtd_finish_delayed_frames,
	.startTileSession = NULL,
	.decodeTile = NULL,
	.finishDelayedTiles = NULL,
};

static const n2_VTVideoDecoderVTable g_n2vtd_vtable = {
	.reserved = NULL,
	.baseClass = &g_n2vtd_base_class,
	.decoderClass = &g_n2vtd_decoder_class,
};

/* ------------------------------------------------------------------------------------------------------------------ */
/* Factories                                                                                                          */
/* ------------------------------------------------------------------------------------------------------------------ */

static OSStatus create_instance(uint32_t codec, uint32_t codec_type, CFAllocatorRef allocator,
                                VTVideoDecoderRef *instanceOut)
{
	struct n2vtd_state *st;
	CMBaseObjectRef obj = NULL;
	OSStatus status;

	if (!instanceOut)
		return N2_kVTParameterErr;
	*instanceOut = NULL;
	status = CMDerivedObjectCreate(allocator, &g_n2vtd_vtable, VTVideoDecoderGetClassID(), &obj);
	if (status != 0 || !obj)
		return status != 0 ? status : N2_kVTVideoDecoderMalfunctionErr;
	st = (struct n2vtd_state *)CMBaseObjectGetDerivedStorage(obj);
	if (!st) {
		CFRelease((CFTypeRef)obj);
		return N2_kVTVideoDecoderMalfunctionErr;
	}
	if (pthread_mutex_init(&st->lock, NULL) != 0) {
		CFRelease((CFTypeRef)obj);
		return N2_kVTVideoDecoderMalfunctionErr;
	}
	st->lock_ready = 1;
	st->codec = codec;
	st->codec_type = codec_type;
	st->pixel_format = N2_PIXFMT_420V;
	*instanceOut = (VTVideoDecoderRef)obj;
	return 0;
}

OSStatus n2vtd_vt_create_avc(uint32_t codecType, CFAllocatorRef allocator, VTVideoDecoderRef *instanceOut)
{
	if (codecType != N2VTD_FOURCC_AVC1 && codecType != N2VTD_CODEC_AVC1)
		return N2_kVTParameterErr;
	if (n2vtd_vt_fallback_in_progress())
		return N2_kVTVideoDecoderNotAvailableNowErr;      /* our own fallback is asking for the NEXT decoder */
	if (!n2vtd_vt_available()) {
		n2vtd_log("avc1 decoder not offered: daemon unavailable, letting VT use its software decoder");
		return N2_kVTVideoDecoderNotAvailableNowErr;
	}
	return create_instance(N2VTD_CODEC_AVC1, codecType == N2VTD_CODEC_AVC1 ? N2VTD_FOURCC_AVC1 : codecType, allocator,
	                       instanceOut);
}

OSStatus n2vtd_vt_create_hevc(uint32_t codecType, CFAllocatorRef allocator, VTVideoDecoderRef *instanceOut)
{
	if (codecType != N2VTD_FOURCC_HVC1 && codecType != N2VTD_FOURCC_HEV1 && codecType != N2VTD_CODEC_HVC1)
		return N2_kVTParameterErr;
	if (n2vtd_vt_fallback_in_progress())
		return N2_kVTVideoDecoderNotAvailableNowErr;
	if (!n2vtd_vt_available()) {
		n2vtd_log("hvc1 decoder not offered: daemon unavailable, letting VT use its software decoder");
		return N2_kVTVideoDecoderNotAvailableNowErr;
	}
	return create_instance(N2VTD_CODEC_HVC1, codecType == N2VTD_CODEC_HVC1 ? N2VTD_FOURCC_HVC1 : codecType, allocator,
	                       instanceOut);
}
