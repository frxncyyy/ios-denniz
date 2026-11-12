/* SPDX-License-Identifier: MIT */
/*
 * n2hwenc_register.c -- registers the two hardware encoder objects with VideoToolbox.
 *
 * The registration info is what makes VideoToolbox prefer them over the software objects for the same codec type:
 * IsHardwareAccelerated = true (truthful once vcodecd drives the VENC) and Rating = N2HWENC_RATING, against the
 * software objects that register without a rating.  The ImplementationID is ours, so a client that asks for a specific
 * encoder ID still selects by name.
 *
 * This file is self-contained because gpu/video/vt/n2vt_shim.c (the process's SLIMEncoderRegister) is read-only for this
 * package.  The proposal on the board is one weak extern and one call there; until it is applied, nothing calls this
 * function and nothing is registered.
 */
#include "n2hwenc_vt.h"

#if defined(__APPLE__) && !defined(N2_HOST_FAKE)
#define N2_WEAK __attribute__((weak_import))
#else
#define N2_WEAK __attribute__((weak))
#endif

/* VideoToolbox and CoreFoundation as weak imports (same pattern as gpu/video/vt/n2vt_shim.c). */
extern OSStatus VTRegisterVideoEncoderWithInfo(uint32_t codecType, CFDictionaryRef info,
                                               n2_VTVideoEncoderCreateInstance create) N2_WEAK;
extern const CFAllocatorRef kCFAllocatorDefault N2_WEAK;
extern const CFStringRef kVTVideoEncoder_IsHardwareAccelerated N2_WEAK;
extern const CFStringRef kVTVideoEncoder_Rating N2_WEAK;
extern const CFDictionaryKeyCallBacks kCFTypeDictionaryKeyCallBacks N2_WEAK;
extern const CFDictionaryValueCallBacks kCFTypeDictionaryValueCallBacks N2_WEAK;
extern const CFBooleanRef kCFBooleanTrue N2_WEAK;
extern CFMutableDictionaryRef CFDictionaryCreateMutable(CFAllocatorRef allocator, CFIndex capacity,
                                                        const CFDictionaryKeyCallBacks *keys,
                                                        const CFDictionaryValueCallBacks *values) N2_WEAK;
extern void CFDictionarySetValue(CFMutableDictionaryRef dict, const void *key, const void *value) N2_WEAK;
extern CFNumberRef CFNumberCreate(CFAllocatorRef allocator, CFIndex type, const void *valuePtr) N2_WEAK;
extern void CFRelease(CFTypeRef object) N2_WEAK;

#define N2_CODEC_AVC1 0x61766331u   /* 'avc1' */
#define N2_CODEC_HVC1 0x68766331u   /* 'hvc1' */
#define N2_CODEC_HEV1 0x68657631u   /* 'hev1' */

static OSStatus register_one(uint32_t codecType, CFStringRef implementationID, n2_VTVideoEncoderCreateInstance create)
{
	CFMutableDictionaryRef info;
	CFNumberRef rating;
	int32_t value = N2HWENC_RATING;
	OSStatus status;

	info = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
	                                 &kCFTypeDictionaryValueCallBacks);
	if (info == NULL)
		return N2_kVTAllocationFailedErr;
	CFDictionarySetValue(info, N2VT_INFO_IMPLEMENTATION_ID, implementationID);
	if (kVTVideoEncoder_IsHardwareAccelerated != NULL && kCFBooleanTrue != NULL)
		CFDictionarySetValue(info, kVTVideoEncoder_IsHardwareAccelerated, kCFBooleanTrue);
	if (kVTVideoEncoder_Rating != NULL) {
		rating = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
		if (rating != NULL) {
			CFDictionarySetValue(info, kVTVideoEncoder_Rating, rating);
			CFRelease(rating);
		}
	}
	status = VTRegisterVideoEncoderWithInfo(codecType, info, create);
	CFRelease(info);
	return status;
}

int n2hwenc_register_all(void)
{
	int registered = 0;

	if (VTRegisterVideoEncoderWithInfo == NULL || CFDictionaryCreateMutable == NULL || CFDictionarySetValue == NULL ||
	    CFRelease == NULL || CFNumberCreate == NULL || &kCFTypeDictionaryKeyCallBacks == NULL ||
	    &kCFTypeDictionaryValueCallBacks == NULL)
		return 0;

	if (register_one(N2_CODEC_AVC1, N2STR(N2HWENC_ID_AVC), n2hwavc_vt_create) == 0)
		registered++;
	if (register_one(N2_CODEC_HVC1, N2STR(N2HWENC_ID_HEVC), n2hwhevc_vt_create) == 0) {
		registered++;
		if (register_one(N2_CODEC_HEV1, N2STR(N2HWENC_ID_HEVC), n2hwhevc_vt_create) == 0)
			registered++;
	}
	return registered;
}
