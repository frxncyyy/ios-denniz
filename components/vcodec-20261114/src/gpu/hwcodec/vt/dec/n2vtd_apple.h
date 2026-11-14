/* SPDX-License-Identifier: MIT */
/*
 * n2vtd_apple.h -- the few CoreMedia/CoreVideo/VideoToolbox declarations the decoder object needs that
 * gpu/video/include/n2_apple.h (frozen, lead-owned) does not have yet.  Per that header's own rule a package declares
 * them in its own header, lists them in its own symbols.txt and posts the addition to the owner; the text proposed for
 * n2_apple.h is in vt/dec/STATUS.md.
 *
 * All of these are real iOS 16 CoreMedia/CoreVideo/VideoToolbox exports (checked against the gpu/video/sdk/lib TBD stubs
 * and the dyld cache symbol dump); the host build gets them from vt/dec/test/t_apple_dec.c (fake_apple.c has no CFData
 * and no decoder session driver).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef N2VTD_APPLE_H
#define N2VTD_APPLE_H

#include "n2_apple.h"
#include "n2vt_dec_abi.h"   /* VTVideoDecoderRef (the software-fallback declarations below) */

#ifdef __cplusplus
extern "C" {
#endif

/* The presentation timestamp of a sample buffer: the decoder pairs output pictures with the frame tokens VideoToolbox
 * handed it, and the only key both sides share is the pts. */
extern CMTime CMSampleBufferGetPresentationTimeStamp(CMSampleBufferRef sb);

/* Sample-description extension atoms: a CFDictionary of CFData, keyed "avcC"/"hvcC".  This is how a decoder reads the
 * parameter sets out of a CMFormatDescription without the H.264/HEVC specific accessors (the task's route). */
extern const CFStringRef kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms;
extern CFTypeRef CMFormatDescriptionGetExtension(CMFormatDescriptionRef fmt, CFStringRef key);

/* CFData, for the atoms above (n2_apple.h has no CFData at all). */
extern CFTypeID CFDataGetTypeID(void);
extern const uint8_t *CFDataGetBytePtr(CFDataRef data);
extern CFIndex CFDataGetLength(CFDataRef data);

/* HEVC parameter sets; the H.264 twin is in n2_apple.h.  Used as the fallback when there is no hvcC atom. */
extern OSStatus CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(CMFormatDescriptionRef fmt, size_t index,
                                                                  const uint8_t **parameterSetPointerOut,
                                                                  size_t *parameterSetSizeOut,
                                                                  size_t *parameterSetCountOut,
                                                                  int *NALUnitHeaderLengthOut);

/* opus-robust (12 Nov 2026): the in-object software fallback (n2vtd_obj.c).  Both are iOS 16 exports:
 *   CMBaseObjectGetVTable              CoreMedia; returns the object's {reserved, baseClass, derivedClass} vtable pointer
 *                                      (vt/dec/ABI.md section 2; heicm/n2vt_trace.c uses it on the encoder side).
 *   VTSelectAndCreateVideoDecoderInstance  VideoToolbox @0x194cc73d0 (ABI.md export table): a 14-instruction wrapper
 *                                      that calls the selector VTDecompressionSessionCreateWithOptions uses
 *                                      (@0x194cc7408) as (codecType, allocator, start index 0, specification, 0, out, 0,
 *                                      0); the selector calls each candidate's factory as (codecType, allocator, out)
 *                                      and moves to the next one on any error [V, disassembled 12 Nov 2026]. */
extern const void *CMBaseObjectGetVTable(CMBaseObjectRef obj);
extern OSStatus VTSelectAndCreateVideoDecoderInstance(uint32_t codecType, CFAllocatorRef allocator,
                                                      CFDictionaryRef decoderSpecification,
                                                      VTVideoDecoderRef *decoderOut);

#ifdef __cplusplus
}
#endif
#endif /* N2VTD_APPLE_H */
