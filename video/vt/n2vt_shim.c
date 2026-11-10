/* SPDX-License-Identifier: MIT */
/*
 * n2vt_shim.c -- the VideoToolbox encoder plug-in that the stock VideoToolbox dlopens from
 * /System/Library/VideoEncoders/Slim.videoencoder and whose entry SLIMEncoderRegister() it calls once per process.
 *
 * It registers three encoders, each only if its factory is really there:
 *
 *   'hvc1' / 'hev1'  Apple's own software HEVC encoder, already in the root file system: the exported factory
 *                    HEVCVideoEncoder_CreateInstance of /System/Library/VideoCodecs/VCPHEVC.videocodec. Apple's
 *                    registration function VCPHEVCRegisterEncoder is an empty ret, so nothing else registers it.
 *   'avc1'           our H.264 encoder, the factory n2avc_vt_create() of vt/n2avc_vt.c (weak: the plug-in links and runs
 *                    without that package).
 *   'jpeg'           the reserve JPEG encoder, n2jpeg_vt_create() (weak, same reason).
 *
 * Everything can be switched off with files in /private/var/db (n2vt_abi.h). No threads are started, no allocation is kept
 * and the only work done outside the registration is a handful of console lines.
 *
 * Two kinds of symbols are weak imports here: the encoder factories of the other packages (so this file links before they
 * exist and registers nothing when they are absent) and VideoToolbox/CoreFoundation (so a process where one of them is
 * missing gets a console line, not a load failure). n2vt_abi.h declares those without the attribute, hence the repeated
 * declarations below with the identical types.
 */
#include "n2vt_abi.h"
#include "n2vt_log.h"

#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>

/* Darwin's numbers: what the phone's dlopen()/access() receive. Checked against the real headers on the phone build; the
 * host test renames the calls and asserts these exact values, so the test checks the ABI the phone uses. */
#define N2_RTLD_NOW   0x2
#define N2_RTLD_LOCAL 0x4
#define N2_F_OK       0

#if defined(__APPLE__) && !defined(N2_HOST_FAKE)
_Static_assert(RTLD_NOW == N2_RTLD_NOW, "Darwin RTLD_NOW");
_Static_assert(RTLD_LOCAL == N2_RTLD_LOCAL, "Darwin RTLD_LOCAL");
_Static_assert(F_OK == N2_F_OK, "Darwin F_OK");
_Static_assert(sizeof(pthread_once_t) == 16, "Darwin pthread_once_t is 16 bytes");   /* sysinc's layout vs the real header */
#endif

#if defined(__APPLE__) && !defined(N2_HOST_FAKE)
#define N2_WEAK __attribute__((weak_import))
#else
#define N2_WEAK __attribute__((weak))
#endif

/* The H.264 and JPEG factories of the other packages; vt/n2avc_vt.h declares the first one with this exact signature.
 * Both are absent from the link until their package is there (build_ios.sh allows the two undefined names while it is not). */
extern OSStatus n2avc_vt_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut) N2_WEAK;
extern OSStatus n2jpeg_vt_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut) N2_WEAK;
/* P04's hardware encoders (gpu/hwcodec/vt/n2hwenc_register.c): avc1/hvc1/hev1, IsHardwareAccelerated, Rating 100. */
extern int n2hwenc_register_all(void) N2_WEAK;
/* opus-jpeg (12 Nov 2026), measurement only: gpu/hwcodec/heicm/n2vt_trace.c, linked only into the HEIC-measurement build.
 * Returns the factory unchanged unless /private/var/db/nord2-video-trace exists; then a logging proxy factory. */
extern n2_VTVideoEncoderCreateInstance n2vt_trace_wrap(uint32_t codecType, n2_VTVideoEncoderCreateInstance inner) N2_WEAK;

/* VideoToolbox and the CoreFoundation calls used below, as weak imports (see the file comment). */
extern OSStatus VTRegisterVideoEncoderWithInfo(uint32_t codecType, CFDictionaryRef info,
                                               n2_VTVideoEncoderCreateInstance create) N2_WEAK;
extern const CFAllocatorRef kCFAllocatorDefault N2_WEAK;
extern const CFStringRef kVTVideoEncoder_IsHardwareAccelerated N2_WEAK;
extern const CFDictionaryKeyCallBacks kCFTypeDictionaryKeyCallBacks N2_WEAK;
extern const CFDictionaryValueCallBacks kCFTypeDictionaryValueCallBacks N2_WEAK;
extern const CFBooleanRef kCFBooleanTrue N2_WEAK;
extern CFMutableDictionaryRef CFDictionaryCreateMutable(CFAllocatorRef allocator, CFIndex capacity,
                                                        const CFDictionaryKeyCallBacks *keys,
                                                        const CFDictionaryValueCallBacks *values) N2_WEAK;
extern void CFDictionarySetValue(CFMutableDictionaryRef dict, const void *key, const void *value) N2_WEAK;
extern void CFRelease(CFTypeRef object) N2_WEAK;

#define N2_HEVC_IMAGE  "/System/Library/VideoCodecs/VCPHEVC.videocodec"
#define N2_HEVC_SYMBOL "HEVCVideoEncoder_CreateInstance"

#define N2_CODEC_HVC1 0x68766331u   /* 'hvc1' */
#define N2_CODEC_HEV1 0x68657631u   /* 'hev1' */
#define N2_CODEC_AVC1 0x61766331u   /* 'avc1' */
#define N2_CODEC_JPEG 0x6a706567u   /* 'jpeg' */

enum {
	N2_HEVC_HVC1 = 0,
	N2_HEVC_HEV1,
	N2_AVC1,
	N2_JPEG,
	N2_CODEC_COUNT
};

static pthread_once_t n2_once = PTHREAD_ONCE_INIT;

static void n2_register_all(void);

/* Registers one codec type; the info dictionary is made here so that each ID goes with its own types. The returned status is
 * also what the caller says. */
static OSStatus
n2_register_one(uint32_t codecType, CFStringRef implementationID, n2_VTVideoEncoderCreateInstance create, int claimHW)
{
	CFMutableDictionaryRef info;
	OSStatus status;

	info = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
	                                 &kCFTypeDictionaryValueCallBacks);
	if (info == NULL) {
		return N2_kVTAllocationFailedErr;
	}
	CFDictionarySetValue(info, N2VT_INFO_IMPLEMENTATION_ID, implementationID);
	if (claimHW && kVTVideoEncoder_IsHardwareAccelerated != NULL && kCFBooleanTrue != NULL) {
		CFDictionarySetValue(info, kVTVideoEncoder_IsHardwareAccelerated, kCFBooleanTrue);
	}
	status = VTRegisterVideoEncoderWithInfo(codecType, info, create);
	CFRelease(info);
	return status;
}

static void
n2_register_all(void)
{
	const char *state[N2_CODEC_COUNT] = { "absent", "absent", "absent", "absent" };
	int claim_hw;

	if (access(N2VT_SWITCH_OFF, N2_F_OK) == 0) {
		n2vt_say("off: %s exists, nothing is registered", N2VT_SWITCH_OFF);
		return;
	}
	if (VTRegisterVideoEncoderWithInfo == NULL) {
		n2vt_say("VTRegisterVideoEncoderWithInfo is NULL: nothing is registered");
		return;
	}
	if (CFDictionaryCreateMutable == NULL || CFDictionarySetValue == NULL || CFRelease == NULL ||
	    &kCFTypeDictionaryKeyCallBacks == NULL || &kCFTypeDictionaryValueCallBacks == NULL) {
		n2vt_say("CoreFoundation is incomplete: nothing is registered");
		return;
	}
	claim_hw = access(N2VT_SWITCH_CLAIM_HW, N2_F_OK) == 0;

	/* HEVC: Apple's own software encoder, one factory, two codec types. */
	if (access(N2VT_SWITCH_NO_HEVC, N2_F_OK) == 0) {
		n2vt_say("HEVC: off (%s exists)", N2VT_SWITCH_NO_HEVC);
		state[N2_HEVC_HVC1] = "off";
		state[N2_HEVC_HEV1] = "off";
	} else {
		void *image = dlopen(N2_HEVC_IMAGE, N2_RTLD_NOW | N2_RTLD_LOCAL);

		state[N2_HEVC_HVC1] = "failed";
		state[N2_HEVC_HEV1] = "failed";
		if (image == NULL) {
			n2vt_say("HEVC: dlopen(%s) failed", N2_HEVC_IMAGE);
		} else {
			union {
				void                            *object;
				n2_VTVideoEncoderCreateInstance  function;
			} factory;

			_Static_assert(sizeof(factory.object) == sizeof(factory.function), "a data pointer holds a function pointer");
			factory.object = dlsym(image, N2_HEVC_SYMBOL);
			if (factory.object == NULL) {
				n2vt_say("HEVC: dlsym(%s) failed", N2_HEVC_SYMBOL);
			} else {
				if (n2vt_trace_wrap != NULL)
					factory.function = n2vt_trace_wrap(N2_CODEC_HVC1, factory.function);
				OSStatus hvc1 = n2_register_one(N2_CODEC_HVC1, N2STR(N2VT_ID_HEVC), factory.function, claim_hw);
				OSStatus hev1 = n2_register_one(N2_CODEC_HEV1, N2STR(N2VT_ID_HEVC), factory.function, claim_hw);

				n2vt_say("HEVC: VCPHEVC.videocodec hvc1=%ld hev1=%ld%s", (long)hvc1, (long)hev1,
				         claim_hw ? " (claim-hw)" : "");
				if (hvc1 == 0) {
					state[N2_HEVC_HVC1] = "ok";
				}
				if (hev1 == 0) {
					state[N2_HEVC_HEV1] = "ok";
				}
			}
		}
	}

	/* H.264: our encoder, if its package is in this link. */
	if (access(N2VT_SWITCH_NO_AVC, N2_F_OK) == 0) {
		n2vt_say("H.264: off (%s exists)", N2VT_SWITCH_NO_AVC);
		state[N2_AVC1] = "off";
	} else if (n2avc_vt_create == NULL) {
		n2vt_say("H.264: n2avc_vt_create is absent");
	} else {
		OSStatus avc = n2_register_one(N2_CODEC_AVC1, N2STR(N2VT_ID_AVC), n2avc_vt_create, claim_hw);

		n2vt_say("H.264: n2avc avc1=%ld%s", (long)avc, claim_hw ? " (claim-hw)" : "");
		state[N2_AVC1] = avc == 0 ? "ok" : "failed";
	}

	/* JPEG: the reserve encoder, if its package is in this link. */
	if (access(N2VT_SWITCH_NO_JPEG, N2_F_OK) == 0) {
		n2vt_say("JPEG: off (%s exists)", N2VT_SWITCH_NO_JPEG);
		state[N2_JPEG] = "off";
	} else if (n2jpeg_vt_create == NULL) {
		n2vt_say("JPEG: n2jpeg_vt_create is absent");
	} else {
		OSStatus jpeg = n2_register_one(N2_CODEC_JPEG, N2STR(N2VT_ID_JPEG), n2jpeg_vt_create, claim_hw);

		n2vt_say("JPEG: n2jpeg jpeg=%ld%s", (long)jpeg, claim_hw ? " (claim-hw)" : "");
		state[N2_JPEG] = jpeg == 0 ? "ok" : "failed";
	}

	n2vt_say("registered: hvc1=%s hev1=%s avc1=%s jpeg=%s",
	         state[N2_HEVC_HVC1], state[N2_HEVC_HEV1], state[N2_AVC1], state[N2_JPEG]);

	/* Hardware encoders (P04), only together with claim-hw: then the software encoders above are registered as hardware
	 * too, so a session that requires a hardware encoder (Camera/CMPhoto) still finds n2avc / VCPHEVC when the VENC path
	 * fails. Order is decided by VideoToolbox, not by registration order: Rating 100 sorts first, and a StartSession error
	 * (vcodecd unreachable: -12915) makes VTCompressionSessionCreate retry the next candidate [V 0x194cd5b20..0x194cd5c70,
	 * gpu/hwcodec/facts/VT_DECODER_ABI.md section 5]. */
	if (n2hwenc_register_all != NULL && claim_hw)
		n2vt_say("hardware encoders registered: %d", n2hwenc_register_all());
	else
		n2vt_say("hardware encoders: %s", n2hwenc_register_all == NULL ? "absent" : "off (no claim-hw)");
}

__attribute__((visibility("default"))) void
SLIMEncoderRegister(void)
{
	(void)pthread_once(&n2_once, n2_register_all);
}

__attribute__((visibility("default"))) unsigned
n2vt_version(void)
{
	return 1u;
}
