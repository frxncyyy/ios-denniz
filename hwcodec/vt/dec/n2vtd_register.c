/* SPDX-License-Identifier: MIT */
/*
 * n2vtd_register.c -- register the hardware decoder objects with VideoToolbox, exactly as DEC08 read the ABI:
 *
 *   VTRegisterVideoDecoderWithInfo(codecType, info, createInstance)
 *   info = { CMClassImplementationID: N2VT_DEC_ID_BASE,
 *            kVTVideoDecoder_IsHardwareAccelerated: kCFBooleanTrue,
 *            kVTVideoDecoderInfo_Rating: CFNumber(100) }
 *
 * VideoToolbox appends ".<codec>" to the implementation ID itself, filters by codec type, sorts by rating descending
 * (hardware first on a tie) and calls each factory until one succeeds; Apple's H264SW/VCPHEVC are software, rating 0,
 * so an available daemon wins and an unavailable one falls through (the factory returns -12913).
 *
 * Switches (include/n2vt_dec_abi.h): N2VT_SWITCH_HWDEC must exist, N2VT_SWITCH_OFF must not, and
 * N2VT_SWITCH_HWDEC_NOHEVC suppresses 'hvc1'.  They are read when SLIMRegister() runs, once per process.
 */
#include "n2vtd_vt.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "n2_apple.h"
#include "n2vtd_nal.h"
#include "n2vtd_log.h"

#if defined(__APPLE__) && !defined(N2_HOST_FAKE)
#define N2VTD_WEAK __attribute__((weak_import))
#else
#define N2VTD_WEAK __attribute__((weak))
#endif

extern OSStatus VTRegisterVideoDecoderWithInfo(uint32_t codecType, CFDictionaryRef info,
                                               n2_VTVideoDecoderCreateInstance create) N2VTD_WEAK;
extern const CFAllocatorRef kCFAllocatorDefault N2VTD_WEAK;
extern const CFStringRef kVTVideoDecoder_IsHardwareAccelerated N2VTD_WEAK;
extern const CFStringRef kVTVideoDecoderInfo_Rating N2VTD_WEAK;
extern const CFDictionaryKeyCallBacks kCFTypeDictionaryKeyCallBacks N2VTD_WEAK;
extern const CFDictionaryValueCallBacks kCFTypeDictionaryValueCallBacks N2VTD_WEAK;
extern const CFBooleanRef kCFBooleanTrue N2VTD_WEAK;
extern CFMutableDictionaryRef CFDictionaryCreateMutable(CFAllocatorRef allocator, CFIndex capacity,
                                                        const CFDictionaryKeyCallBacks *keys,
                                                        const CFDictionaryValueCallBacks *values) N2VTD_WEAK;
extern void CFDictionarySetValue(CFMutableDictionaryRef dict, const void *key, const void *value) N2VTD_WEAK;
extern CFNumberRef CFNumberCreate(CFAllocatorRef allocator, CFIndex type, const void *valuePtr) N2VTD_WEAK;
extern void CFRelease(CFTypeRef object) N2VTD_WEAK;
extern int access(const char *path, int mode) N2VTD_WEAK;

#ifndef F_OK
#define F_OK 0
#endif

#define N2VTD_CODEC_AVC1_REG N2VTD_FOURCC_AVC1
#define N2VTD_CODEC_HVC1_REG N2VTD_FOURCC_HVC1

/* The switch directory; /private/var/db in the product, a /tmp path in the host tests. */
static const char *g_switch_dir = "/private/var/db";
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_registered = -1;

/* Test/staging hook: change the directory the switches are looked up in and forget the cached registration. */
void n2vtd_register_set_switch_dir(const char *dir)
{
	pthread_mutex_lock(&g_lock);
	g_switch_dir = dir;
	g_registered = -1;
	pthread_mutex_unlock(&g_lock);
}

static int file_exists(const char *dir, const char *name)
{
	char path[256];
	size_t dl, nl;

	if (!dir || access == NULL)
		return 0;
	dl = strlen(dir);
	nl = strlen(name);
	if (dl + 1 + nl + 1 > sizeof(path))
		return 0;
	memcpy(path, dir, dl);
	path[dl] = '/';
	memcpy(path + dl + 1, name, nl + 1);
	return access(path, F_OK) == 0;
}

/* The switch decision, separated so the host test can check it without registering anything. */
int n2vtd_register_enabled(int codec_hevc)
{
	if (file_exists(g_switch_dir, "nord2-video-off"))
		return 0;
	if (!file_exists(g_switch_dir, "nord2-video-hwdec"))
		return 0;
	if (codec_hevc && file_exists(g_switch_dir, "nord2-video-hwdec-no-hevc"))
		return 0;
	return 1;
}

static OSStatus register_one(uint32_t codecType, n2_VTVideoDecoderCreateInstance create)
{
	CFMutableDictionaryRef info;
	CFNumberRef rating;
	int32_t value = N2VT_DEC_RATING;
	OSStatus status;

	info = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
	                                 &kCFTypeDictionaryValueCallBacks);
	if (info == NULL)
		return N2_kVTAllocationFailedErr;
	/* The key is the plain string "CMClassImplementationID" (n2vt_abi.h); VT appends ".<codec>4cc" itself. */
	CFDictionarySetValue(info, N2VT_INFO_IMPLEMENTATION_ID, N2STR(N2VT_DEC_ID_BASE));
	/* Weak variables are checked by ADDRESS: reading an absent weak variable would dereference 0. */
	if (&kVTVideoDecoder_IsHardwareAccelerated != NULL && &kCFBooleanTrue != NULL)
		CFDictionarySetValue(info, kVTVideoDecoder_IsHardwareAccelerated, kCFBooleanTrue);
	if (&kVTVideoDecoderInfo_Rating != NULL) {
		rating = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
		if (rating != NULL) {
			CFDictionarySetValue(info, kVTVideoDecoderInfo_Rating, rating);
			CFRelease(rating);
		}
	}
	status = VTRegisterVideoDecoderWithInfo(codecType, info, create);
	CFRelease(info);
	return status;
}

int n2vtd_vt_register_all(void)
{
	int n;

	pthread_mutex_lock(&g_lock);
	if (g_registered >= 0) {
		n = g_registered;
		pthread_mutex_unlock(&g_lock);
		return n;
	}
	if (VTRegisterVideoDecoderWithInfo == NULL || CFDictionaryCreateMutable == NULL || CFDictionarySetValue == NULL ||
	    CFRelease == NULL || CFNumberCreate == NULL || &kCFAllocatorDefault == NULL ||
	    &kCFTypeDictionaryKeyCallBacks == NULL || &kCFTypeDictionaryValueCallBacks == NULL) {
		g_registered = 0;
		pthread_mutex_unlock(&g_lock);
		return 0;
	}
	n = 0;
	if (n2vtd_register_enabled(0) && register_one(N2VTD_CODEC_AVC1_REG, n2vtd_vt_create_avc) == 0)
		n++;
	if (n2vtd_register_enabled(1) && register_one(N2VTD_CODEC_HVC1_REG, n2vtd_vt_create_hevc) == 0)
		n++;
	g_registered = n;
	if (n > 0)
		n2vtd_log("registered %d hardware decoder(s) (avc1%s)", n,
		          n2vtd_register_enabled(1) ? "+hvc1" : "");
	else
		n2vtd_log("not registered: switch files in %s say off", g_switch_dir ? g_switch_dir : "?");
	pthread_mutex_unlock(&g_lock);
	return n;
}

/* The one symbol VideoToolbox resolves with dlsym() after dlopen; exported even under -fvisibility=hidden. */
__attribute__((visibility("default"))) void SLIMRegister(void)
{
	(void)n2vtd_vt_register_all();
}
