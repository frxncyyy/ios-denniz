/* SPDX-License-Identifier: MIT */
/*
 * n2jpeg_vt.h -- the create function of the n2jpeg VideoToolbox encoder object (R1).
 *
 * The shim (vt/n2vt_shim.c) refers to this symbol weakly so that the plug-in also links and runs when the JPEG encoder is
 * absent.  On the host test build (-DN2_HOST_FAKE) the weak attribute is omitted: the test links the object directly, and
 * the fake runtime supplies CoreFoundation/CoreMedia/CoreVideo.
 */
#ifndef N2JPEG_VT_H
#define N2JPEG_VT_H

#include "n2vt_abi.h"

/* CMVideoFormatDescriptionCreate() is a public CoreMedia function (declared here because include/n2_apple.h, which is
 * lead-owned and frozen, does not declare it).  It is imported weakly for the same reason the plug-in links CoreMedia
 * weakly: a process without it gets a NULL check, not a load failure.  On the host build the fake runtime supplies it
 * when WP02 has it; the weak reference keeps the suite linkable while it does not. */
#ifdef N2_HOST_FAKE
extern OSStatus CMVideoFormatDescriptionCreate(CFAllocatorRef allocator, uint32_t codecType, int32_t width, int32_t height,
                                               CFDictionaryRef extensions, CMFormatDescriptionRef *formatDescriptionOut)
	__attribute__((weak));
#else
extern OSStatus CMVideoFormatDescriptionCreate(CFAllocatorRef allocator, uint32_t codecType, int32_t width, int32_t height,
                                               CFDictionaryRef extensions, CMFormatDescriptionRef *formatDescriptionOut)
	__attribute__((weak_import));
#endif

#ifdef N2_HOST_FAKE
extern OSStatus n2jpeg_vt_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut);
#else
extern OSStatus n2jpeg_vt_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut)
	__attribute__((weak_import));
#endif

#endif
