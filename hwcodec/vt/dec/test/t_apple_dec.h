/* SPDX-License-Identifier: MIT */
/*
 * t_apple_dec.h -- the host-test Apple runtime for the DEC06 decoder object.
 *
 * gpu/video/fake (read-only) cannot host this package: fake_vt.c implements only the ENCODER half of the VideoToolbox
 * ABI (its CMDerivedObjectCreate rejects any class ID but VTVideoEncoderGetClassID), and it has no CFData, which the
 * avcC/hvcC extension-atom route needs.  This file is the decoder counterpart: CoreFoundation, the CoreMedia sample /
 * block buffer / format description / pixel buffer subset, and a VideoToolbox decoder session that calls the object the
 * way VTDecompressionSession does (startSession, decodeFrame, emit-once bookkeeping, finishDelayedFrames, flush).
 *
 * Host only; never compiled into the dylib.
 */
#ifndef N2VTD_T_APPLE_DEC_H
#define N2VTD_T_APPLE_DEC_H

#ifndef N2_HOST_FAKE
#error "t_apple_dec.h is a host-test header: compile with -DN2_HOST_FAKE"
#endif

#include <stddef.h>
#include <stdint.h>

#include "n2_apple.h"
#include "n2vt_dec_abi.h"
#include "n2vtd_apple.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Console capture: the test defines the strong n2vt_say() that n2vtd_log.c calls weakly. */
void        n2vtdt_log_reset(void);
int         n2vtdt_log_count(void);
const char *n2vtdt_log_line(int index);
int         n2vtdt_log_contains(const char *needle);

/* ---- CoreFoundation inspection ---------------------------------------------------------------------------------- */
long        n2vtdt_live_objects(void);
CFStringRef n2vtdt_cfstr(const char *s);                 /* interned */
CFIndex     n2vtdt_dictionary_count(CFDictionaryRef d);
const void *n2vtdt_dictionary_key_at(CFDictionaryRef d, CFIndex i);
const void *n2vtdt_dictionary_value_at(CFDictionaryRef d, CFIndex i);
const void *n2vtdt_dictionary_get(CFDictionaryRef d, const char *key);
CFDataRef   n2vtdt_data_create(const void *bytes, size_t n);
int32_t     n2vtdt_number_int32(CFTypeRef n, int32_t *out);

/* ---- CoreMedia construction ------------------------------------------------------------------------------------- */
/* A format description whose sample-description extension atoms contain `avcc`/`hvcc` under "avcC"/"hvcC". */
int  n2vtdt_format_with_atoms(uint32_t codec, const uint8_t *record, size_t record_bytes,
                              CMFormatDescriptionRef *fmtOut);
/* A format description with parameter sets only (the CMVideoFormatDescriptionGet*ParameterSetAtIndex fallback). */
int  n2vtdt_format_with_params(uint32_t codec, const uint8_t *const *ps, const size_t *sizes, size_t count,
                               size_t nal_length_size, CMFormatDescriptionRef *fmtOut);
/* One CMSampleBuffer holding `bytes` (already length-prefixed or Annex B) with presentation time `pts`. */
int  n2vtdt_sample_create(CMFormatDescriptionRef fmt, const void *bytes, size_t n, int64_t pts, CMSampleBufferRef *out);
const void *n2vtdt_sample_data(CMSampleBufferRef sb, size_t *bytes);
CMTime     n2vtdt_sample_pts(CMSampleBufferRef sb);

/* ---- CoreVideo inspection --------------------------------------------------------------------------------------- */
size_t n2vtdt_pixel_plane_height(CVPixelBufferRef pb, size_t plane);
size_t n2vtdt_pixel_plane_stride(CVPixelBufferRef pb, size_t plane);
const uint8_t *n2vtdt_pixel_plane(CVPixelBufferRef pb, size_t plane);

/* ---- VideoToolbox decoder session driver ------------------------------------------------------------------------ */
struct n2vtdt_session;

/* Forget every registration. */
void n2vtdt_vt_reset(void);
/* Number of registrations and one of them (borrowed info). */
int  n2vtdt_vt_registered(int index, uint32_t *codec, CFDictionaryRef *info, n2_VTVideoDecoderCreateInstance *fn);

/* Select the first registration for `codec`, call its factory (as VTSelectAndCreateVideoDecoderInstance does) and
 * then startSession(fmt).  Returns NULL on failure and sets *statusOut. */
struct n2vtdt_session *n2vtdt_session_create(uint32_t codec, CMFormatDescriptionRef fmt, OSStatus *statusOut);
/* opus-robust: the same with base-class SetProperty(keys[i], values[i]) calls before StartSession (VT makes two). */
struct n2vtdt_session *n2vtdt_session_create_props(uint32_t codec, CMFormatDescriptionRef fmt, const char *const *keys,
                                                   const CFTypeRef *values, int nprops, OSStatus *statusOut);
/* How many times VTSelectAndCreateVideoDecoderInstance was called (the object's software fallback calls it). */
int  n2vtdt_select_calls(void);
OSStatus n2vtdt_session_decode(struct n2vtdt_session *s, CMSampleBufferRef sb, uint32_t flags, uint32_t *infoOut);
OSStatus n2vtdt_session_finish(struct n2vtdt_session *s);
/* Call the decoder's base class copyProperty; returns a +1 reference in *valueOut. */
OSStatus n2vtdt_session_copy_property(struct n2vtdt_session *s, CFStringRef key, CFTypeRef *valueOut);
OSStatus n2vtdt_session_can_accept(struct n2vtdt_session *s, CMFormatDescriptionRef fmt);
OSStatus n2vtdt_session_set_properties(struct n2vtdt_session *s, CFDictionaryRef props);
OSStatus n2vtdt_session_copy_supported(struct n2vtdt_session *s, CFDictionaryRef *out);
const n2_VTVideoDecoderClass *n2vtdt_session_class(struct n2vtdt_session *s);
CMBaseObjectRef n2vtdt_session_object(struct n2vtdt_session *s);
const n2_CMBaseClass *n2vtdt_session_base(struct n2vtdt_session *s);
CFDictionaryRef n2vtdt_session_pixel_attributes(struct n2vtdt_session *s);
int  n2vtdt_session_emitted(struct n2vtdt_session *s, int index, OSStatus *status, uint32_t *flags,
                            CVImageBufferRef *image, int *tokenIndex);
int  n2vtdt_session_protocol_errors(struct n2vtdt_session *s);
int  n2vtdt_session_emit_count(struct n2vtdt_session *s);
/* 1 when the token with this index has been emitted exactly once. */
int  n2vtdt_session_token_emitted(struct n2vtdt_session *s, int tokenIndex);
void n2vtdt_session_destroy(struct n2vtdt_session *s);

#ifdef __cplusplus
}
#endif
#endif /* N2VTD_T_APPLE_DEC_H */
