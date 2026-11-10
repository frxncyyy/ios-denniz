/* SPDX-License-Identifier: MIT */
/*
 * n2avc_vt.h -- the create function of the n2avc VideoToolbox encoder object (WP05).
 *
 * The shim (vt/n2vt_shim.c) refers to this symbol weakly so that the plug-in also links and runs when the encoder core is
 * absent. On the host test build (-DN2_HOST_FAKE) the weak attribute is omitted: the test links the object directly.
 */
#ifndef N2AVC_VT_H
#define N2AVC_VT_H

#include "n2vt_abi.h"

#ifdef N2_HOST_FAKE
extern OSStatus n2avc_vt_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut);
#else
extern OSStatus n2avc_vt_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut)
	__attribute__((weak_import));
#endif

#endif
