/* SPDX-License-Identifier: MIT */
/*
 * n2hwenc_vt.h -- the VideoToolbox hardware H.264/HEVC encoder objects backed by vcodecd (package P04).
 *
 * Two CMBaseObject subclasses with the frozen VTVideoEncoder class record of gpu/video/include/n2vt_abi.h: one for
 * 'avc1' (n2hwavc_vt_create) and one for 'hvc1'/'hev1' (n2hwhevc_vt_create).  Everything they do goes through
 * struct n2hwenc_client (n2hwenc_client.h), which the default table maps on to P03's vcodecd client by weak symbol.
 *
 * Registration: n2hwenc_register_all() registers both types with IsHardwareAccelerated = true and Rating =
 * N2HWENC_RATING (above the software objects, which register without a rating).  gpu/video/vt/n2vt_shim.c is the
 * process's SLIMEncoderRegister() implementation and is read-only for this package, so the proposal on the board is a
 * two-line weak call from there; nothing here depends on it being applied.
 *
 * Host tests replace the client with a function-pointer table and use gpu/video/fake as the Apple runtime.
 */
#ifndef N2HWENC_VT_H
#define N2HWENC_VT_H

#include "n2_apple.h"
#include "n2vt_abi.h"
#include "n2hwenc_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Public VideoToolbox status (VTErrors.h) for "this encoder cannot run now": VideoToolbox's encoder selection loop
 * treats a candidate it cannot start as one to skip, which is what keeps n2avc / VCPHEVC (registered for the same
 * codec type) as the fallback.  Not in the frozen n2vt_abi.h yet; proposed to hw-lead. */
#define N2_kVTVideoEncoderNotAvailableNowErr (-12915)

/* CoreMedia parameter-set constructors that n2_apple.h does not declare yet (proposed to hw-lead; the host tests
 * provide test-local implementations in vt/test/t_support.c, iOS uses the real symbols). */
extern OSStatus CMVideoFormatDescriptionCreateFromHEVCParameterSets(CFAllocatorRef allocator,
                                                                    size_t parameterSetCount,
                                                                    const uint8_t *const *parameterSetPointers,
                                                                    const size_t *parameterSetSizes,
                                                                    int NALUnitHeaderLength,
                                                                    CFDictionaryRef extensions,
                                                                    CMFormatDescriptionRef *formatDescriptionOut);
extern OSStatus CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(CMFormatDescriptionRef fmt, size_t index,
                                                                   const uint8_t **ps, size_t *size, size_t *count,
                                                                   int *nalUnitHeaderLength);

/* Our encoder IDs (the value VideoToolbox shows and kVTVideoEncoderSpecification_EncoderID selects). */
#define N2HWENC_ID_AVC  "com.nord2.videotoolbox.videoencoder.avc1.n2hw"
#define N2HWENC_ID_HEVC "com.nord2.videotoolbox.videoencoder.hvc1.n2hw"

/* Rating handed to VTRegisterVideoEncoderWithInfo; software objects register without a rating (0). */
#define N2HWENC_RATING 100

/* Trace hook: the library never prints.  Every event carries two numbers whose meaning is in the enum below; the test
 * counts them, and on the phone a console-bearing caller can print them. */
enum n2hwenc_trace_event {
	N2HWENC_TRACE_CONNECTED = 1,        /* a = 0, b = 0 */
	N2HWENC_TRACE_CONNECT_FAILED,       /* a = client error (negative) */
	N2HWENC_TRACE_OPENED,               /* a = in_bytes, b = out_bytes */
	N2HWENC_TRACE_OPEN_FAILED,          /* a = client error */
	N2HWENC_TRACE_REOPENED,             /* a = bitrate, b = gop */
	N2HWENC_TRACE_FRAME,                /* a = annexb bytes, b = key frame */
	N2HWENC_TRACE_FRAME_FAILED,         /* a = client error */
	N2HWENC_TRACE_FORMAT,               /* a = parameter sets, b = avcC/hvcC bytes */
	N2HWENC_TRACE_FORMAT_FAILED,        /* a = CoreMedia status */
	N2HWENC_TRACE_ENC_SET,              /* a = bitrate, b = fps_num | fps_den << 16 */
	N2HWENC_TRACE_ENC_SET_FAILED,       /* a = client error */
	N2HWENC_TRACE_INVALIDATE           /* a = frames, b = bytes */
};
typedef void (*n2hwenc_trace_fn)(void *ctx, int event, uint32_t a, uint32_t b);
void n2hwenc_set_trace(n2hwenc_trace_fn fn, void *ctx);

/* The two encoder factories, with the frozen n2_VTVideoEncoderCreateInstance signature. */
OSStatus n2hwavc_vt_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut);
OSStatus n2hwhevc_vt_create(uint32_t codecType, CFAllocatorRef allocator, VTVideoEncoderRef *instanceOut);

/* Register both codec types with VideoToolbox (IsHardwareAccelerated true, N2HWENC_RATING).  Returns the number of
 * successful registrations (0 when VideoToolbox is not there), never a positive error.  'hev1' is registered with the
 * same factory when 'hvc1' succeeded. */
int n2hwenc_register_all(void);

/* Probe: 1 when the configured client can connect and disconnect, 0 when not.  This performs a HELLO, so it can block;
 * it is offered to the registration hook as an option, not called by the encoder object. */
int n2hwenc_vt_available(void);

/* Pipeline depth (opus-cam): the most pictures in flight per session, 1..3.  1 = synchronous (every picture is emitted
 * before encodeFrame returns).  0 restores the default: 1 when /private/var/db/nord2-video-hwenc-sync exists, else 2.
 * Read at each session's first picture; a client that sets MaxFrameDelayCount 0 always gets 1. */
void n2hwenc_set_max_in_flight(int frames);

#ifdef __cplusplus
}
#endif
#endif /* N2HWENC_VT_H */
