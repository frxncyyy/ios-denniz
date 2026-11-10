/*
 * probe_vt.c -- the probe's VideoToolbox measurements.
 *
 *   T01 vt.encoders        : VTCopyVideoEncoderList
 *   T02 vt.decode.<codec>  : decode the embedded golden streams (WP09) and compare the luma md5
 *   T03 vt.encode.<codec>.<w>x<h> : 30 synthetic pictures through VTCompressionSession, then decode the result
 *   T03b vt.encode.<codec>.req-hw / .en-hw : the same create call with the hardware encoder keys
 *   T04 vt.transfer        : VTPixelTransferSession 1920x1080 420v -> 640x360 BGRA
 *
 * Everything is a measurement first: each field says what was asked and what came back.  A test returns 0 only when
 * the whole round trip worked and the PSNR is >= 30 dB.
 *
 * SPDX-License-Identifier: MIT
 */
#include "probe_core.h"
#include "probe_apple.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__has_include)
#  if __has_include("../vectors/embedded_vectors.h")
#    include "../vectors/embedded_vectors.h"
#    define PROBE_HAVE_VECTORS 1
#  endif
#endif
#ifndef PROBE_HAVE_VECTORS
#  define PROBE_HAVE_VECTORS 0
#endif

#define PROBE_MAX_OUT_SAMPLES 64

/* ------------------------------------------------------------------ small helpers ------------------------------------------ */

static void fourcc4(uint32_t v, char out[5])
{
	out[0] = (char)((v >> 24) & 0xff);
	out[1] = (char)((v >> 16) & 0xff);
	out[2] = (char)((v >> 8) & 0xff);
	out[3] = (char)(v & 0xff);
	out[4] = 0;
}

static void say4cc(struct probe_ctx *c, const char *key, uint32_t v)
{
	char s[5];
	fourcc4(v, s);
	probe_say(c, key, "%s", s);
}

static void cf_to_c(CFStringRef s, char *buf, size_t cap)
{
	if (!buf || cap == 0)
		return;
	buf[0] = 0;
	if (!s || CFGetTypeID(s) != CFStringGetTypeID())
		return;
	if (!CFStringGetCString(s, buf, (CFIndex)cap, kCFStringEncodingUTF8))
		buf[0] = 0;
}

static void say_cfstr(struct probe_ctx *c, const char *key, CFStringRef s)
{
	char buf[256];
	cf_to_c(s, buf, sizeof buf);
	probe_say(c, key, "%s", buf[0] ? buf : "(none)");
}

static int cf_num(CFTypeRef v, int32_t *out)
{
	if (!v || CFGetTypeID(v) != CFNumberGetTypeID())
		return 0;
	return CFNumberGetValue((CFNumberRef)v, kCFNumberSInt32Type, out) ? 1 : 0;
}

static int cf_bool(CFTypeRef v, int *out)
{
	if (!v || CFGetTypeID(v) != CFBooleanGetTypeID())
		return 0;
	*out = CFBooleanGetValue((CFBooleanRef)v) ? 1 : 0;
	return 1;
}

/* A VideoToolbox constant by name, or a plain string with the same text when the constant is not exported. */
static CFStringRef vt_key(const char *symbol, const char *literal)
{
	void *p = dlsym(N2_RTLD_DEFAULT, symbol);
	if (p)
		return *(const CFStringRef *)p;
	/* not an exported constant: a plain CFString with the documented text (created once, lives until exit) */
	return CFStringCreateWithCString(NULL, literal, kCFStringEncodingUTF8);
}

static CFStringRef k_list_hw(void)
{
	static CFStringRef k;
	if (!k)
		k = vt_key("kVTVideoEncoderList_IsHardwareAccelerated", "IsHardwareAccelerated");
	return k;
}

static CFStringRef k_encoder_id_prop(void)
{
	static CFStringRef k;
	if (!k)
		k = vt_key("kVTCompressionPropertyKey_EncoderID", "EncoderID");
	return k;
}

static CFStringRef k_using_hw_enc(void)
{
	static CFStringRef k;
	if (!k)
		k = vt_key("kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder",
		           "UsingHardwareAcceleratedVideoEncoder");
	return k;
}

static CFStringRef k_using_hw_dec(void)
{
	static CFStringRef k;
	if (!k)
		k = vt_key("kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder",
		           "UsingHardwareAcceleratedVideoDecoder");
	return k;
}

static CFStringRef k_spec_require_hw(void)
{
	static CFStringRef k;
	if (!k)
		k = vt_key("kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder",
		           "RequireHardwareAcceleratedVideoEncoder");
	return k;
}

static CFStringRef k_spec_enable_hw(void)
{
	static CFStringRef k;
	if (!k)
		k = vt_key("kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder",
		           "EnableHardwareAcceleratedVideoEncoder");
	return k;
}

static CFStringRef k_prop_avg_bitrate(void)
{
	static CFStringRef k;
	if (!k)
		k = vt_key("kVTCompressionPropertyKey_AverageBitRate", "AverageBitRate");
	return k;
}

static CFStringRef k_prop_realtime(void)
{
	static CFStringRef k;
	if (!k)
		k = vt_key("kVTCompressionPropertyKey_RealTime", "RealTime");
	return k;
}

static CFStringRef k_prop_max_keyint(void)
{
	static CFStringRef k;
	if (!k)
		k = vt_key("kVTCompressionPropertyKey_MaxKeyFrameInterval", "MaxKeyFrameInterval");
	return k;
}

static CFStringRef k_prop_allow_reorder(void)
{
	static CFStringRef k;
	if (!k)
		k = vt_key("kVTCompressionPropertyKey_AllowFrameReordering", "AllowFrameReordering");
	return k;
}

static CFStringRef k_pixfmt(void)
{
	static CFStringRef k;
	if (!k)
		k = kCVPixelBufferPixelFormatTypeKey;
	return k;
}

/* A dictionary {PixelFormatType: 420v} for a decompression session's destination. */
static CFDictionaryRef make_420v_attrs(void)
{
	CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 1, &kCFTypeDictionaryKeyCallBacks,
	                                                     &kCFTypeDictionaryValueCallBacks);
	int32_t fmt = (int32_t)N2_PIXFMT_420V;
	CFNumberRef n;
	if (!d)
		return NULL;
	n = CFNumberCreate(NULL, kCFNumberSInt32Type, &fmt);
	if (n) {
		CFDictionarySetValue(d, k_pixfmt(), n);
		CFRelease(n);
	}
	return d;
}

/* md5 of the luma plane, width bytes of each row (what WP09's luma_md5_first_frame means). */
static int luma_md5(CVImageBufferRef img, char hex[33])
{
	size_t w, h, stride, y;
	uint8_t *base;
	uint8_t *copy;
	int ok = 0;

	if (!img)
		return 0;
	w = CVPixelBufferGetWidth(img);
	h = CVPixelBufferGetHeight(img);
	if (CVPixelBufferGetPlaneCount(img) < 1 || w == 0 || h == 0 || w > 8192 || h > 8192)
		return 0;
	if (CVPixelBufferLockBaseAddress(img, kCVPixelBufferLock_ReadOnly) != 0)
		return 0;
	base = CVPixelBufferGetBaseAddressOfPlane(img, 0);
	stride = CVPixelBufferGetBytesPerRowOfPlane(img, 0);
	copy = (base && stride >= w) ? malloc(w * h) : NULL;
	if (copy) {
		for (y = 0; y < h; y++)
			memcpy(copy + y * w, base + y * stride, w);
		probe_md5(copy, w * h, hex);
		ok = 1;
	}
	CVPixelBufferUnlockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
	free(copy);
	return ok;
}

/* PSNR of the luma plane against probe_fill_nv12(frame); the chroma plane of `src` is scratch space. */
static int luma_psnr(CVImageBufferRef img, int frame, uint8_t *scratch, size_t scratch_size, double *out)
{
	size_t w, h, stride;
	uint8_t *base;
	uint8_t *yplane;
	int ok = 0;

	if (!img || !scratch || !out)
		return 0;
	w = CVPixelBufferGetWidth(img);
	h = CVPixelBufferGetHeight(img);
	if (w == 0 || h == 0 || w * h + (w + 2) * (h / 2 + 2) > scratch_size)
		return 0;
	if (CVPixelBufferGetPlaneCount(img) < 1)
		return 0;
	yplane = scratch;
	probe_fill_nv12(yplane, w, scratch + w * h, w, (int)w, (int)h, frame);
	if (CVPixelBufferLockBaseAddress(img, kCVPixelBufferLock_ReadOnly) != 0)
		return 0;
	base = CVPixelBufferGetBaseAddressOfPlane(img, 0);
	stride = CVPixelBufferGetBytesPerRowOfPlane(img, 0);
	if (base && stride >= w) {
		*out = probe_psnr(base, stride, yplane, w, (int)w, (int)h);
		ok = 1;
	}
	CVPixelBufferUnlockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
	return ok;
}

/* ------------------------------------------------------------------ T01 encoders ------------------------------------------- */

static int t_encoders(struct probe_ctx *c)
{
	CFArrayRef list = NULL;
	OSStatus st;
	CFIndex n, i;

	st = VTCopyVideoEncoderList(NULL, &list);
	probe_say(c, "status", "%d", (int)st);
	if (st != 0 || !list) {
		probe_say(c, "list", "absent");
		return 1;
	}
	n = CFArrayGetCount(list);
	probe_say(c, "count", "%ld", (long)n);
	for (i = 0; i < n && i < 64; i++) {
		CFDictionaryRef d = (CFDictionaryRef)CFArrayGetValueAtIndex(list, i);
		CFTypeRef v;
		int32_t codec = 0;
		int hw = -1;
		char key[16], id[160] = "(none)", name[160] = "(none)", disp[160] = "(none)";
		char cc[5] = "????";

		if (!d || CFGetTypeID(d) != CFDictionaryGetTypeID())
			continue;
		v = CFDictionaryGetValue(d, kVTVideoEncoderList_CodecType);
		if (cf_num(v, &codec))
			fourcc4((uint32_t)codec, cc);
		cf_to_c((CFStringRef)CFDictionaryGetValue(d, kVTVideoEncoderList_EncoderID), id, sizeof id);
		cf_to_c((CFStringRef)CFDictionaryGetValue(d, kVTVideoEncoderList_EncoderName), name, sizeof name);
		cf_to_c((CFStringRef)CFDictionaryGetValue(d, kVTVideoEncoderList_DisplayName), disp, sizeof disp);
		{
			CFStringRef hk = k_list_hw();
			CFTypeRef hwv = hk ? CFDictionaryGetValue(d, hk) : NULL;
			if (!cf_bool(hwv, &hw))
				hw = -1;
		}
		snprintf(key, sizeof key, "e%ld", (long)i);
		probe_say(c, key, "%s|%s|%s|%s|hw=%d", cc, id, name, disp, hw);
	}
	CFRelease(list);
	return 0;
}

/* ------------------------------------------------------------------ T02 decode --------------------------------------------- */

struct dec_state {
	int got;
	int status_first;
	int w, h;
	char md5[33];
	int md5_ok;
};

static void dec_cb(void *refcon, void *src, OSStatus status, uint32_t flags, CVImageBufferRef img, CMTime pts,
                   CMTime dur)
{
	struct dec_state *s = (struct dec_state *)refcon;
	(void)src;
	(void)flags;
	(void)dur;
	if (!s)
		return;
	s->got++;
	if (status != 0) {
		if (s->status_first == 0)
			s->status_first = (int)status;
		return;
	}
	if (!s->md5_ok && img) {
		char hex[33];
		if (luma_md5(img, hex)) {
			memcpy(s->md5, hex, sizeof hex);
			s->md5_ok = 1;
			s->w = (int)CVPixelBufferGetWidth(img);
			s->h = (int)CVPixelBufferGetHeight(img);
		}
	}
	(void)pts;
}

static CMSampleBufferRef make_sample(const uint8_t *data, size_t size, CMFormatDescriptionRef fmt, int index)
{
	CMBlockBufferRef bb = NULL;
	CMSampleBufferRef sb = NULL;
	CMSampleTimingInfo timing;
	OSStatus st;

	st = CMBlockBufferCreateWithMemoryBlock(NULL, NULL, size, kCFAllocatorDefault, NULL, 0, size,
	                                        N2_kCMBlockBufferAssureMemoryNowFlag, &bb);
	if (st != 0 || !bb)
		return NULL;
	st = CMBlockBufferReplaceDataBytes(data, bb, 0, size);
	if (st != 0) {
		CFRelease(bb);
		return NULL;
	}
	memset(&timing, 0, sizeof timing);
	timing.duration.value = 1;
	timing.duration.timescale = 30;
	timing.duration.flags = N2_CMTIME_VALID;
	timing.presentationTimeStamp.value = index;
	timing.presentationTimeStamp.timescale = 30;
	timing.presentationTimeStamp.flags = N2_CMTIME_VALID;
	timing.decodeTimeStamp.flags = 0;
	st = CMSampleBufferCreateReady(NULL, bb, fmt, 1, 1, &timing, 1, &size, &sb);
	CFRelease(bb);
	if (st != 0)
		return NULL;
	return sb;
}

#if PROBE_HAVE_VECTORS

static int decode_stream(struct probe_ctx *c, const struct n2vec_stream *v, int idx)
{
	CMFormatDescriptionRef fmt = NULL;
	VTDecompressionSessionRef session = NULL;
	VTDecompressionOutputCallbackRecord cb;
	CFDictionaryRef attrs = NULL;
	OSStatus st;
	unsigned i;
	int pass = 1;
	struct dec_state st_dec;
	const uint8_t *ps[4];
	size_t sizes[4];

	(void)idx;
	memset(&st_dec, 0, sizeof st_dec);
	if (!v || v->ps_count > 4 || v->frame_count == 0) {
		probe_say(c, "bad", "stream %s has no parameter sets or frames", v ? v->name : "?");
		return 1;
	}
	for (i = 0; i < v->ps_count; i++) {
		ps[i] = v->ps[i];
		sizes[i] = v->ps_size[i];
	}
	if (v->codec == N2_CODEC_AVC1)
		st = CMVideoFormatDescriptionCreateFromH264ParameterSets(NULL, v->ps_count, ps, sizes,
		                                                         (int)v->nal_length_size, &fmt);
	else if (v->codec == N2_CODEC_HVC1)
		st = CMVideoFormatDescriptionCreateFromHEVCParameterSets(NULL, v->ps_count, ps, sizes,
		                                                         (int)v->nal_length_size, NULL, &fmt);
	else {
		probe_say(c, "create", "codec %08x not supported by this probe", v->codec);
		return 2;
	}
	probe_say(c, "create", "%d", (int)st);
	if (st != 0 || !fmt)
		return 1;
	attrs = make_420v_attrs();
	memset(&cb, 0, sizeof cb);
	cb.decompressionOutputRefCon = &st_dec;
	cb.decompressionOutputCallback = dec_cb;
	st = VTDecompressionSessionCreate(NULL, fmt, NULL, attrs, &cb, &session);
	probe_say(c, "session", "%d", (int)st);
	if (st != 0 || !session) {
		if (attrs)
			CFRelease(attrs);
		CFRelease(fmt);
		return 1;
	}
	{
		CFTypeRef hw = NULL;
		OSStatus hst = VTSessionCopyProperty((VTSessionRef)session, k_using_hw_dec(), NULL, &hw);
		int b = -1;
		if (hst == 0 && hw) {
			if (!cf_bool(hw, &b))
				b = -1;
			CFRelease(hw);
		}
		probe_say(c, "dec_hw", "%d", b);
	}
	{
		char per[160];
		size_t used = 0;
		per[0] = 0;
		for (i = 0; i < v->frame_count; i++) {
			CMSampleBufferRef sb = make_sample(v->frames[i].data, v->frames[i].size, fmt, (int)i);
			VTDecodeInfoFlags info = 0;
			if (!sb) {
				probe_say(c, "sample", "alloc failed at %u", i);
				pass = 0;
				break;
			}
			st = VTDecompressionSessionDecodeFrame(session, sb, 0, NULL, &info);
			CFRelease(sb);
			if (used + 12 < sizeof per)
				used += (size_t)snprintf(per + used, sizeof per - used, "%s%d", used ? "," : "", (int)st);
			if (st != 0) {
				probe_say(c, "decode", "frame %u status %d", i, (int)st);
				pass = 0;
				break;
			}
		}
		probe_say(c, "statuses", "%s", per[0] ? per : "(none)");
	}
	st = VTDecompressionSessionWaitForAsynchronousFrames(session);
	probe_say(c, "wait", "%d", (int)st);
	probe_say(c, "frames", "%d/%u", st_dec.got, v->frame_count);
	if (st_dec.status_first)
		probe_say(c, "first_status", "%d", st_dec.status_first);
	if (st_dec.md5_ok) {
		probe_say(c, "md5", "%s", st_dec.md5);
		probe_say(c, "wh", "%dx%d", st_dec.w, st_dec.h);
		if (v->luma_md5_first && v->luma_md5_first[0]) {
			probe_say(c, "golden", "%s", v->luma_md5_first);
			probe_say(c, "match", "%d", strcmp(st_dec.md5, v->luma_md5_first) == 0);
			if (strcmp(st_dec.md5, v->luma_md5_first) != 0)
				pass = 0;
		} else {
			probe_say(c, "golden", "absent");
		}
	} else {
		probe_say(c, "md5", "none");
		pass = 0;
	}
	if (st_dec.got != (int)v->frame_count)
		pass = 0;
	VTDecompressionSessionInvalidate(session);
	CFRelease(session);
	CFRelease(fmt);
	if (attrs)
		CFRelease(attrs);
	return pass ? 0 : 1;
}

static int t_decode(struct probe_ctx *c, uint32_t codec)
{
	unsigned i, found = 0, ok = 0, skipped = 0;
	int pass = 1;
	char key[32];

	for (i = 0; i < n2vec_stream_count(); i++) {
		const struct n2vec_stream *v = &n2vec_streams[i];
		int r;
		if (v->codec != codec)
			continue;
		found++;
		snprintf(key, sizeof key, "s%u", found - 1);
		probe_say(c, key, "%s", v->name);
		r = decode_stream(c, v, (int)i);
		if (r == 0)
			ok++;
		else if (r == 2)
			skipped++;
		else
			pass = 0;
	}
	if (!found) {
		probe_say(c, "golden", "absent");
		probe_say(c, "streams", "0");
		return 2;
	}
	probe_say(c, "streams", "%u", found);
	probe_say(c, "ok", "%u", ok);
	if (skipped)
		probe_say(c, "skipped", "%u", skipped);
	say4cc(c, "codec", codec);
	if (!pass)
		return 1;
	return ok ? 0 : 2;
}

#else /* no vectors */

static int t_decode(struct probe_ctx *c, uint32_t codec)
{
	say4cc(c, "codec", codec);
	probe_say(c, "golden", "absent");
	probe_say(c, "streams", "0");
	probe_say(c, "note", "vectors/embedded_vectors.h not built yet");
	return 2;
}

#endif

static int t_decode_avc1(struct probe_ctx *c)
{
	return t_decode(c, N2_CODEC_AVC1);
}

static int t_decode_hvc1(struct probe_ctx *c)
{
	return t_decode(c, N2_CODEC_HVC1);
}

static int t_decode_av01(struct probe_ctx *c)
{
	return t_decode(c, N2_CODEC_AV01);
}

static int t_decode_mp4v(struct probe_ctx *c)
{
	return t_decode(c, N2_CODEC_MP4V);
}

/* ------------------------------------------------------------------ T03 encode --------------------------------------------- */

struct enc_state {
	int callbacks;
	int frames;
	int keyframes;
	OSStatus first_err;
	size_t bytes;
	CMSampleBufferRef samples[PROBE_MAX_OUT_SAMPLES];
	int n_samples;
};

static void enc_cb(void *refcon, void *src, OSStatus status, uint32_t flags, CMSampleBufferRef sb)
{
	struct enc_state *s = (struct enc_state *)refcon;
	(void)src;
	(void)flags;
	if (!s)
		return;
	s->callbacks++;
	if (status != 0) {
		if (s->first_err == 0)
			s->first_err = status;
		return;
	}
	if (!sb)
		return;
	s->frames++;
	{
		CMBlockBufferRef bb = CMSampleBufferGetDataBuffer(sb);
		if (bb)
			s->bytes += CMBlockBufferGetDataLength(bb);
	}
	{
		CFArrayRef arr = CMSampleBufferGetSampleAttachmentsArray(sb, 0);
		int not_sync = 0;
		if (arr && CFArrayGetCount(arr) > 0) {
			CFDictionaryRef d = (CFDictionaryRef)CFArrayGetValueAtIndex(arr, 0);
			CFTypeRef b = d ? CFDictionaryGetValue(d, kCMSampleAttachmentKey_NotSync) : NULL;
			if (!cf_bool(b, &not_sync))
				not_sync = 0;
		}
		if (!not_sync)
			s->keyframes++;
	}
	if (s->n_samples < PROBE_MAX_OUT_SAMPLES)
		s->samples[s->n_samples++] = (CMSampleBufferRef)CFRetain(sb);
}

struct dec_psnr {
	int frames;
	double first;
	int have_first;
	double last;
	int have_last;
	OSStatus first_err;
};

static void dec_psnr_cb(void *refcon, void *src, OSStatus status, uint32_t flags, CVImageBufferRef img, CMTime pts,
                        CMTime dur)
{
	struct dec_psnr *d = (struct dec_psnr *)refcon;
	(void)src;
	(void)flags;
	(void)dur;
	if (!d)
		return;
	d->frames++;
	if (status != 0) {
		if (d->first_err == 0)
			d->first_err = status;
		return;
	}
	if (img && pts.timescale == 30 && pts.value >= 0 && pts.value < 100000) {
		int frame = (int)pts.value;
		double psnr;
		size_t w = CVPixelBufferGetWidth(img), h = CVPixelBufferGetHeight(img);
		uint8_t *scratch;
		if (w == 0 || h == 0 || w > 4096 || h > 2304)
			return;
		scratch = malloc(w * h * 2 + 4 * w + 64);       /* (lead) luma AND the chroma plane probe_fill_nv12 writes (run 2 overran) */
		if (scratch) {
			if (luma_psnr(img, frame, scratch, w * h * 2 + 4 * w + 64, &psnr)) {
				if (!d->have_first) {
					d->first = psnr;
					d->have_first = 1;
				}
				d->last = psnr;
				d->have_last = 1;
			}
			free(scratch);
		}
	}
}

static int encode_roundtrip(struct probe_ctx *c, uint32_t codec, int w, int h, int mode)
{
	VTCompressionSessionRef session = NULL;
	CFDictionaryRef spec = NULL;
	VTCompressionOutputCallbackRecord cb;
	struct enc_state es;
	struct dec_psnr ds;
	OSStatus st, create_status;
	uint64_t t0, t1;
	int i, pass = 1;
	char name[5];

	memset(&es, 0, sizeof es);
	memset(&ds, 0, sizeof ds);
	fourcc4(codec, name);
	if (mode != 0) {
		CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 1, &kCFTypeDictionaryKeyCallBacks,
		                                                     &kCFTypeDictionaryValueCallBacks);
		if (d) {
			if (mode == 3)          /* (lead) ask for OUR H.264 encoder by its ID, then the whole round trip */
				CFDictionarySetValue(d, N2STR("EncoderID"), N2STR("com.nord2.videotoolbox.videoencoder.avc1.n2avc"));
			else
				CFDictionarySetValue(d, mode == 1 ? k_spec_require_hw() : k_spec_enable_hw(), kCFBooleanTrue);
			spec = d;
		}
		if (mode == 3)
			mode = 0;
	}
	memset(&cb, 0, sizeof cb);
	cb.compressionOutputRefCon = &es;
	cb.compressionOutputCallback = enc_cb;
	create_status = VTCompressionSessionCreate(NULL, w, h, codec, spec, NULL, NULL, cb.compressionOutputCallback,
	                                           cb.compressionOutputRefCon, &session);
	probe_say(c, "create", "%d", (int)create_status);
	if (spec)
		CFRelease(spec);
	if (create_status != 0 || !session) {
		/* T03b stops here: the create status is the whole measurement, and "no hardware encoder" is an answer. */
		return mode == 0 ? 1 : 0;
	}
	if (mode != 0) {
		CFTypeRef id = NULL;
		if (VTSessionCopyProperty((VTSessionRef)session, k_encoder_id_prop(), NULL, &id) == 0 && id) {
			say_cfstr(c, "id", (CFStringRef)id);
			CFRelease(id);
		}
		probe_say(c, "created", "yes");
		VTCompressionSessionInvalidate(session);
		CFRelease(session);
		return 0;
	}
	{
		CFTypeRef id = NULL;
		int hw = -1;
		if (VTSessionCopyProperty((VTSessionRef)session, k_encoder_id_prop(), NULL, &id) == 0 && id) {
			say_cfstr(c, "id", (CFStringRef)id);
			CFRelease(id);
		} else {
			probe_say(c, "id", "(none)");
		}
		id = NULL;
		if (VTSessionCopyProperty((VTSessionRef)session, k_using_hw_enc(), NULL, &id) == 0 && id) {
			if (!cf_bool(id, &hw))
				hw = -1;
			CFRelease(id);
		}
		probe_say(c, "hw", "%d", hw);
	}
	{
		int32_t bitrate = (int32_t)((int64_t)w * h * 3);
		CFNumberRef n = CFNumberCreate(NULL, kCFNumberSInt32Type, &bitrate);
		st = n ? VTSessionSetProperty((VTSessionRef)session, k_prop_avg_bitrate(), n) : -1;
		probe_say(c, "set.bitrate", "%d", (int)st);
		if (n)
			CFRelease(n);
	}
	st = VTSessionSetProperty((VTSessionRef)session, k_prop_realtime(), kCFBooleanTrue);
	probe_say(c, "set.realtime", "%d", (int)st);
	{
		int32_t keyint = 30;
		CFNumberRef n = CFNumberCreate(NULL, kCFNumberSInt32Type, &keyint);
		st = n ? VTSessionSetProperty((VTSessionRef)session, k_prop_max_keyint(), n) : -1;
		probe_say(c, "set.keyint", "%d", (int)st);
		if (n)
			CFRelease(n);
	}
	st = VTSessionSetProperty((VTSessionRef)session, k_prop_allow_reorder(), kCFBooleanFalse);
	probe_say(c, "set.reorder", "%d", (int)st);

	{
		uint8_t *y = malloc((size_t)w * h);
		uint8_t *uv = malloc((size_t)w * h / 2);
		CMTime dur;
		dur.value = 1;
		dur.timescale = 30;
		dur.flags = N2_CMTIME_VALID;
		dur.epoch = 0;
		t0 = probe_now_ns();
		for (i = 0; i < 30; i++) {
			CVPixelBufferRef pb = NULL;
			int32_t cv;
			CMTime pts;
			VTEncodeInfoFlags info = 0;
			if (!y || !uv) {
				probe_say(c, "alloc", "failed");
				pass = 0;
				break;
			}
			probe_fill_nv12(y, (size_t)w, uv, (size_t)w, w, h, i);
			cv = CVPixelBufferCreate(NULL, (size_t)w, (size_t)h, N2_PIXFMT_420V, NULL, &pb);
			if (cv != 0 || !pb) {
				probe_say(c, "pixelbuffer", "frame %d status %d", i, (int)cv);
				pass = 0;
				break;
			}
			if (CVPixelBufferLockBaseAddress(pb, 0) != 0) {
				probe_say(c, "pixelbuffer", "frame %d does not lock", i);
				CFRelease(pb);
				pass = 0;
				break;
			}
			{
				uint8_t *dy = CVPixelBufferGetBaseAddressOfPlane(pb, 0);
				uint8_t *duv = CVPixelBufferGetBaseAddressOfPlane(pb, 1);
				size_t sy = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
				size_t suv = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
				int yy;
				if (!dy || !duv || sy < (size_t)w || suv < (size_t)w) {
					probe_say(c, "pixelbuffer", "frame %d has no usable planes", i);
					CVPixelBufferUnlockBaseAddress(pb, 0);
					CFRelease(pb);
					pass = 0;
					break;
				}
				for (yy = 0; yy < h; yy++)
					memcpy(dy + (size_t)yy * sy, y + (size_t)yy * w, (size_t)w);
				for (yy = 0; yy < h / 2; yy++)
					memcpy(duv + (size_t)yy * suv, uv + (size_t)yy * w, (size_t)w);
			}
			CVPixelBufferUnlockBaseAddress(pb, 0);
			pts.value = i;
			pts.timescale = 30;
			pts.flags = N2_CMTIME_VALID;
			pts.epoch = 0;
			st = VTCompressionSessionEncodeFrame(session, pb, pts, dur, NULL, NULL, &info);
			CFRelease(pb);
			if (st != 0) {
				probe_say(c, "encode", "frame %d status %d", i, (int)st);
				pass = 0;
				break;
			}
		}
		{
			CMTime t;
			memset(&t, 0, sizeof t);
			st = VTCompressionSessionCompleteFrames(session, t);
			probe_say(c, "complete", "%d", (int)st);
		}
		t1 = probe_now_ns();
		free(y);
		free(uv);
	}
	probe_say(c, "frames", "%d", es.frames);
	probe_say(c, "bytes", "%zu", es.bytes);
	probe_say(c, "keyframes", "%d", es.keyframes);
	probe_say(c, "ms_frame", "%.1f", (double)(t1 - t0) / 1e6 / 30.0);
	if (es.first_err)
		probe_say(c, "err", "%d", (int)es.first_err);
	if (es.frames != 30 || es.bytes == 0)
		pass = 0;

	/* decode the produced samples and compare with the source pictures */
	if (es.n_samples > 0) {
		CMFormatDescriptionRef fmt = CMSampleBufferGetFormatDescription(es.samples[0]);
		VTDecompressionSessionRef dec = NULL;
		VTDecompressionOutputCallbackRecord dcb;
		CFDictionaryRef attrs = make_420v_attrs();
		if (!fmt) {
			probe_say(c, "dec_format", "none");
			pass = 0;
		} else {
			memset(&dcb, 0, sizeof dcb);
			dcb.decompressionOutputRefCon = &ds;
			dcb.decompressionOutputCallback = dec_psnr_cb;
			st = VTDecompressionSessionCreate(NULL, fmt, NULL, attrs, &dcb, &dec);
			probe_say(c, "dec_create", "%d", (int)st);
			if (st == 0 && dec) {
				int j;
				for (j = 0; j < es.n_samples; j++) {
					VTDecodeInfoFlags info = 0;
					st = VTDecompressionSessionDecodeFrame(dec, es.samples[j], 0, NULL, &info);
					if (st != 0) {
						probe_say(c, "dec_frame", "sample %d status %d", j, (int)st);
						pass = 0;
						break;
					}
				}
				st = VTDecompressionSessionWaitForAsynchronousFrames(dec);
				probe_say(c, "dec_wait", "%d", (int)st);
				probe_say(c, "dec_frames", "%d", ds.frames);
				if (ds.first_err)
					probe_say(c, "dec_err", "%d", (int)ds.first_err);
				if (ds.have_first) {
					probe_say(c, "psnr_first", "%.2f", ds.first);
					if (ds.first < 30.0)
						pass = 0;
				} else {
					probe_say(c, "psnr_first", "none");
					pass = 0;
				}
				if (ds.have_last) {
					probe_say(c, "psnr_last", "%.2f", ds.last);
					if (ds.last < 30.0)
						pass = 0;
				} else {
					probe_say(c, "psnr_last", "none");
					pass = 0;
				}
				if (ds.frames != 30)
					pass = 0;
				VTDecompressionSessionInvalidate(dec);
				CFRelease(dec);
			} else {
				pass = 0;
			}
		}
		if (attrs)
			CFRelease(attrs);
	} else {
		probe_say(c, "dec", "no samples");
		pass = 0;
	}
	for (i = 0; i < es.n_samples; i++)
		CFRelease(es.samples[i]);
	VTCompressionSessionInvalidate(session);
	CFRelease(session);
	return pass ? 0 : 1;
}

static int t_enc_avc1_640(struct probe_ctx *c)  { return encode_roundtrip(c, N2_CODEC_AVC1, 640, 480, 0); }
static int t_enc_avc1_720(struct probe_ctx *c)  { return encode_roundtrip(c, N2_CODEC_AVC1, 1280, 720, 0); }
static int t_enc_avc1_1080(struct probe_ctx *c) { return encode_roundtrip(c, N2_CODEC_AVC1, 1920, 1080, 0); }
static int t_enc_hvc1_640(struct probe_ctx *c)  { return encode_roundtrip(c, N2_CODEC_HVC1, 640, 480, 0); }
static int t_enc_hvc1_720(struct probe_ctx *c)  { return encode_roundtrip(c, N2_CODEC_HVC1, 1280, 720, 0); }
static int t_enc_hvc1_1080(struct probe_ctx *c) { return encode_roundtrip(c, N2_CODEC_HVC1, 1920, 1080, 0); }
static int t_enc_jpeg_640(struct probe_ctx *c)  { return encode_roundtrip(c, N2_CODEC_JPEG, 640, 480, 0); }
static int t_enc_hvc1_req(struct probe_ctx *c)  { return encode_roundtrip(c, N2_CODEC_HVC1, 640, 480, 1); }
static int t_enc_hvc1_en(struct probe_ctx *c)   { return encode_roundtrip(c, N2_CODEC_HVC1, 640, 480, 2); }
static int t_enc_avc1_req(struct probe_ctx *c)  { return encode_roundtrip(c, N2_CODEC_AVC1, 640, 480, 1); }
static int t_enc_avc1_en(struct probe_ctx *c)   { return encode_roundtrip(c, N2_CODEC_AVC1, 640, 480, 2); }
static int t_enc_avc1_n2(struct probe_ctx *c)   { return encode_roundtrip(c, N2_CODEC_AVC1, 640, 480, 3); }
static int t_enc_avc1_n2_720(struct probe_ctx *c) { return encode_roundtrip(c, N2_CODEC_AVC1, 1280, 720, 3); }

/* ------------------------------------------------------------------ T04 transfer ------------------------------------------- */

/* (lead) BGRA -> 420v at the same size, pixels checked: what CMPhoto does to a picture before the HEVC encoder (run 3: HEIC
 * written through ImageIO comes back wrong although the encoder and the HEIC decoder are right). sw = 1 asks VideoToolbox
 * not to use its Metal path. Expected luma for BT.601 and BT.709 video range; the smaller mean difference is reported. */
static int transfer_pixels(struct probe_ctx *c, int sw, uint32_t dstfmt)
{
	enum { TW = 1024, TH = 768 };
	CVPixelBufferRef src = NULL, dst = NULL;
	VTPixelTransferSessionRef session = NULL;
	OSStatus st;
	int pass = 0;
	CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(NULL, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFMutableDictionaryRef empty = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

	if (attrs && empty)
		CFDictionarySetValue(attrs, kCVPixelBufferIOSurfacePropertiesKey, empty);      /* IOSurface-backed, as CMPhoto's are */
	st = CVPixelBufferCreate(NULL, TW, TH, N2_FOURCC('B', 'G', 'R', 'A'), attrs, &src);
	probe_say(c, "src", "%d", (int)st);
	if (st == 0)
		st = CVPixelBufferCreate(NULL, TW, TH, dstfmt, attrs, &dst);
	probe_say(c, "dst", "%d", (int)st);
	if (st != 0 || !src || !dst)
		goto out;
	if (CVPixelBufferLockBaseAddress(src, 0) == 0) {
		uint8_t *b = CVPixelBufferGetBaseAddressOfPlane(src, 0);
		size_t stride = CVPixelBufferGetBytesPerRowOfPlane(src, 0);
		int x, y;
		if (!b) {       /* a non-planar buffer: plane 0 may not exist */
			probe_say(c, "src_base", "none");
			CVPixelBufferUnlockBaseAddress(src, 0);
			goto out;
		}
		for (y = 0; y < TH; y++)
			for (x = 0; x < TW; x++) {
				uint8_t *p = b + (size_t)y * stride + (size_t)x * 4;
				p[0] = (uint8_t)x; p[1] = (uint8_t)y; p[2] = (uint8_t)((x + y) / 2); p[3] = 255;      /* B, G, R, A */
			}
		CVPixelBufferUnlockBaseAddress(src, 0);
	}
	st = VTPixelTransferSessionCreate(NULL, &session);
	probe_say(c, "session", "%d", (int)st);
	if (st != 0 || !session)
		goto out;
	if (sw) {
		st = VTSessionSetProperty((VTSessionRef)session, N2STR("EnableHardwareAcceleratedTransfer"), kCFBooleanFalse);
		probe_say(c, "set_sw", "%d", (int)st);
	}
	st = VTPixelTransferSessionTransferImage(session, src, dst);
	probe_say(c, "status", "%d", (int)st);
	if (st == 0 && CVPixelBufferLockBaseAddress(dst, kCVPixelBufferLock_ReadOnly) == 0) {
		const uint8_t *yb = CVPixelBufferGetBaseAddressOfPlane(dst, 0), *cb = CVPixelBufferGetBaseAddressOfPlane(dst, 1);
		size_t ys = CVPixelBufferGetBytesPerRowOfPlane(dst, 0), cs = CVPixelBufferGetBytesPerRowOfPlane(dst, 1);
		double d601 = 0, d709 = 0, dc = 0;
		long n = 0, nc = 0;
		int x, y;
		if (yb && cb) {
			for (y = 0; y < TH; y += 2)
				for (x = 0; x < TW; x += 2) {
					double B = (uint8_t)x, G = (uint8_t)y, R = (uint8_t)((x + y) / 2);
					double full = dstfmt == N2_PIXFMT_420F;
					double y601 = 0.299 * R + 0.587 * G + 0.114 * B, y709 = 0.2126 * R + 0.7152 * G + 0.0722 * B;
					double e601 = full ? y601 : 16.0 + y601 * 219.0 / 255.0, e709 = full ? y709 : 16.0 + y709 * 219.0 / 255.0;
					double got = yb[(size_t)y * ys + (size_t)x];
					double cbe = (B - y601) / 1.772 * (full ? 1.0 : 224.0 / 255.0) + 128.0;
					double cbg = cb[(size_t)(y / 2) * cs + (size_t)(x / 2) * 2];
					d601 += got > e601 ? got - e601 : e601 - got;
					d709 += got > e709 ? got - e709 : e709 - got;
					if ((x & 255) > 8 && (x & 255) < 248 && (y & 255) > 8 && (y & 255) < 248) {  /* away from the wrap edges */
						dc += cbg > cbe ? cbg - cbe : cbe - cbg;
						nc++;
					}
					n++;
				}
			probe_say(c, "luma_absdiff_601", "%.2f", d601 / (double)n);
			probe_say(c, "luma_absdiff_709", "%.2f", d709 / (double)n);
			probe_say(c, "cb_absdiff_601", "%.2f", nc ? dc / (double)nc : -1.0);
			probe_say(c, "y_samples", "%u %u %u %u (at 0,0 100,50 500,300 1000,700)", yb[0], yb[50 * ys + 100], yb[300 * ys + 500], yb[700 * ys + 1000]);
			probe_say(c, "cbcr_samples", "%u,%u %u,%u", cb[25 * cs + 100], cb[25 * cs + 101], cb[150 * cs + 500], cb[150 * cs + 501]);
			pass = (d601 < d709 ? d601 : d709) / (double)n < 4.0;
		} else {
			probe_say(c, "dst_planes", "missing");
		}
		CVPixelBufferUnlockBaseAddress(dst, kCVPixelBufferLock_ReadOnly);
	}
out:
	if (session) {
		VTPixelTransferSessionInvalidate(session);
		CFRelease(session);
	}
	if (src) CFRelease(src);
	if (dst) CFRelease(dst);
	if (attrs) CFRelease(attrs);
	if (empty) CFRelease(empty);
	return pass ? 0 : 1;
}
static int t_transfer_px(struct probe_ctx *c)      { return transfer_pixels(c, 0, N2_PIXFMT_420V); }
static int t_transfer_px_sw(struct probe_ctx *c)   { return transfer_pixels(c, 1, N2_PIXFMT_420V); }
static int t_transfer_px_f(struct probe_ctx *c)    { return transfer_pixels(c, 0, N2_PIXFMT_420F); }
static int t_transfer_px_f_sw(struct probe_ctx *c) { return transfer_pixels(c, 1, N2_PIXFMT_420F); }

static int t_transfer(struct probe_ctx *c)
{
	CVPixelBufferRef src = NULL, dst = NULL;
	VTPixelTransferSessionRef session = NULL;
	OSStatus st;
	uint64_t t0, t1;
	int pass = 1;

	st = CVPixelBufferCreate(NULL, 1920, 1080, N2_PIXFMT_420V, NULL, &src);
	probe_say(c, "src", "%d", (int)st);
	if (st != 0 || !src)
		return 1;
	st = CVPixelBufferCreate(NULL, 640, 360, N2_FOURCC('B', 'G', 'R', 'A'), NULL, &dst);
	probe_say(c, "dst", "%d", (int)st);
	if (st != 0 || !dst) {
		CFRelease(src);
		return 1;
	}
	{
		uint8_t *y = malloc(1920 * 1080);
		uint8_t *uv = malloc(1920 * 1080 / 2);
		if (y && uv) {
			probe_fill_nv12(y, 1920, uv, 1920, 1920, 1080, 0);
			if (CVPixelBufferLockBaseAddress(src, 0) != 0) {
				probe_say(c, "src", "does not lock");
				pass = 0;
			} else {
				uint8_t *dy = CVPixelBufferGetBaseAddressOfPlane(src, 0);
				uint8_t *duv = CVPixelBufferGetBaseAddressOfPlane(src, 1);
				size_t sy = CVPixelBufferGetBytesPerRowOfPlane(src, 0);
				size_t suv = CVPixelBufferGetBytesPerRowOfPlane(src, 1);
				int yy;
				if (dy && duv && sy >= 1920 && suv >= 1920) {
					for (yy = 0; yy < 1080; yy++)
						memcpy(dy + (size_t)yy * sy, y + (size_t)yy * 1920, 1920);
					for (yy = 0; yy < 540; yy++)
						memcpy(duv + (size_t)yy * suv, uv + (size_t)yy * 1920, 1920);
				} else {
					probe_say(c, "src_planes", "missing or too narrow");
					pass = 0;
				}
				CVPixelBufferUnlockBaseAddress(src, 0);
			}
		} else {
			probe_say(c, "alloc", "failed");
			pass = 0;
		}
		free(y);
		free(uv);
	}
	st = VTPixelTransferSessionCreate(NULL, &session);
	probe_say(c, "session", "%d", (int)st);
	if (st != 0 || !session) {
		CFRelease(src);
		CFRelease(dst);
		return 1;
	}
	t0 = probe_now_ns();
	st = VTPixelTransferSessionTransferImage(session, src, dst);
	t1 = probe_now_ns();
	probe_say(c, "status", "%d", (int)st);
	probe_say(c, "ms", "%.2f", (double)(t1 - t0) / 1e6);
	if (st != 0)
		pass = 0;
	VTPixelTransferSessionInvalidate(session);
	CFRelease(session);
	CFRelease(src);
	CFRelease(dst);
	return pass ? 0 : 1;
}

/* ------------------------------------------------------------------ the table ---------------------------------------------- */

const struct probe_test probe_vt_tests[] = {
	{ "vt.encoders",            t_encoders,       20 },
	{ "vt.decode.avc1",         t_decode_avc1,    30 },
	{ "vt.decode.hvc1",         t_decode_hvc1,    30 },
	{ "vt.decode.av01",         t_decode_av01,    30 },
	{ "vt.decode.mp4v",         t_decode_mp4v,    30 },
	{ "vt.encode.avc1.640x480",   t_enc_avc1_640,   60 },
	{ "vt.encode.avc1.1280x720",  t_enc_avc1_720,  120 },
	{ "vt.encode.avc1.1920x1080", t_enc_avc1_1080, 180 },
	{ "vt.encode.hvc1.640x480",   t_enc_hvc1_640,   60 },
	{ "vt.encode.hvc1.1280x720",  t_enc_hvc1_720,  120 },
	{ "vt.encode.hvc1.1920x1080", t_enc_hvc1_1080, 180 },
	{ "vt.encode.jpeg.640x480",   t_enc_jpeg_640,   60 },
	{ "vt.encode.hvc1.req-hw",    t_enc_hvc1_req,   30 },
	{ "vt.encode.hvc1.en-hw",     t_enc_hvc1_en,    30 },
	{ "vt.encode.avc1.req-hw",    t_enc_avc1_req,   30 },
	{ "vt.encode.avc1.en-hw",     t_enc_avc1_en,    30 },
	{ "vt.encode.avc1.n2avc.640x480",  t_enc_avc1_n2,     60 },
	{ "vt.encode.avc1.n2avc.1280x720", t_enc_avc1_n2_720, 120 },
	{ "vt.transfer",              t_transfer,       30 },
	{ "vt.transfer.bgra-420v",    t_transfer_px,      30 },
	{ "vt.transfer.bgra-420v.sw", t_transfer_px_sw,   30 },
	{ "vt.transfer.bgra-420f",    t_transfer_px_f,    30 },
	{ "vt.transfer.bgra-420f.sw", t_transfer_px_f_sw, 30 },
};
const unsigned probe_vt_tests_count = sizeof probe_vt_tests / sizeof probe_vt_tests[0];
