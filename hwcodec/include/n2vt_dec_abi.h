/*
 * n2vt_dec_abi.h -- the VideoToolbox video-DECODER plug-in ABI of iOS 16.7 (20H392, iPhone X cache), read out of the
 * dyld shared cache on 2026-11-11 by opus-dec3 (gpu/hwcodec/facts/vt_decoder_abi.py regenerates the evidence; the full
 * write-up with every address is gpu/hwcodec/facts/VT_DECODER_ABI.md). Companion of gpu/video/include/n2vt_abi.h (the
 * frozen encoder half, read-only here): the CMBase class record and CMDerivedObjectCreate are shared and come from there.
 *
 * [V] = read in the cache today (address given in VT_DECODER_ABI.md), [I] = inferred. Unslid cache addresses.
 *
 * Evidence summary:
 *   _kParavirtualizedVideoDecoderVTable @0x1d69e8d28 = { 0, &base @0x1d69e8d60, &class @0x1d69e8db8 }       [V]
 *     base  = { 1, 0x60, 0, Invalidate, Finalize, CopyDebugDescription, CopyProperty, SetProperty, 0, 0, 0 }
 *     class = { 3, StartSession, DecodeFrame, CopySupportedPropertyDictionary, SetProperties,
 *               CopySerializableProperties, CanAcceptFormatDescription, FinishDelayedFrames,
 *               StartTileSession, DecodeTile, FinishDelayedTiles }   (11 slots; the next vtable starts at +0x58)
 *   Every slot is named by its Paravirtualized symbol AND by its VideoToolbox caller (VT_DECODER_ABI.md section 2).
 *   Signatures: C++ mangled names in the cache (MPEG4VideoDecoder @0x1f53f4a40/0x1f53f5230 in VCPMP4V, WebKit
 *   VP8/VP9 decoders @0x1d5168274..) give CreateInstance, StartSession and DecodeFrame exactly                    [V]
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef N2VT_DEC_ABI_H
#define N2VT_DEC_ABI_H

#include "n2vt_abi.h"           /* n2_CMBaseClass, CMDerivedObjectCreate, CMBaseObjectGetDerivedStorage, N2VT_DECODER_* */

typedef struct OpaqueVTVideoDecoder        *VTVideoDecoderRef;      /* CMBaseObject of class VTVideoDecoderGetClassID() */
typedef struct OpaqueVTVideoDecoderSession *VTVideoDecoderSession;  /* VideoToolbox's side of one decompression session */
typedef struct OpaqueVTVideoDecoderFrame   *VTVideoDecoderFrame;    /* VideoToolbox's token for one submitted sample */
typedef struct __CVPixelBufferPool         *CVPixelBufferPoolRef;

/* The decoder class record: 11 pointer-sized slots (0x58 bytes). VideoToolbox reads the version word before the
 * version-2 slots it guards (FinishDelayedFrames, CanAcceptFormatDescription: "cmp version, #2; b.lo skip") [V].
 * SetProperties/CopySerializableProperties are called if non-NULL without a version test [V]. Use version 2. */
typedef struct n2_VTVideoDecoderClass {
	uint64_t        version;                                                                        /* 2 */
	/* +0x08 [V] caller VTDecompressionSessionCreateWithOptions (once, right after the instance is created and two
	 * SetProperty calls); a non-zero status FAILS the session create, VT does NOT try another decoder here. */
	OSStatus        (*startSession)(VTVideoDecoderRef dec, VTVideoDecoderSession session,
	                                CMVideoFormatDescriptionRef format);
	/* +0x10 [V] caller vtDecompressionDuctDecodeSingleFrame: x0..x4 = dec, frame, sample, decodeFlags, infoFlagsOut.
	 * Every frame token must be emitted exactly once with VTDecoderSessionEmitDecodedFrame (inside this call, or
	 * later when kVTDecodeFrame_EnableAsynchronousDecompression / EnableTemporalProcessing is set). A status of
	 * -17690 is passed through without FigSignalErrorAt [V]; its meaning (kVTVideoDecoderRemovedErr?) is [I]. */
	OSStatus        (*decodeFrame)(VTVideoDecoderRef dec, VTVideoDecoderFrame frame, CMSampleBufferRef sample,
	                               uint32_t decodeFlags, uint32_t *infoFlagsOut);
	/* +0x18 [V] callers VTDecompressionSessionCopySupportedPropertyDictionary, vtDecompressionSessionIsProperty-
	 * SupportedByVideoDecoder: (dec, &dict); -12782 or 0 accepted. Returned dictionary is owned by the caller. */
	OSStatus        (*copySupportedPropertyDictionary)(VTVideoDecoderRef dec, CFDictionaryRef *dictOut);
	/* +0x20 [V] caller VTDecompressionSessionSetProperties: (dec, dict); -12782 => VT falls back to one base-class
	 * SetProperty call per key. May be NULL (same fallback). */
	OSStatus        (*setProperties)(VTVideoDecoderRef dec, CFDictionaryRef properties);
	/* +0x28 [V] caller VTDecompressionSessionCopySerializableProperties: (dec, allocator, &dict); -12782 => 0. */
	OSStatus        (*copySerializableProperties)(VTVideoDecoderRef dec, CFAllocatorRef allocator, CFDictionaryRef *dictOut);
	/* +0x30 [V] callers VTVideoDecoderCanAcceptFormatDescription and vtDecompressionSessionDecodeFrameCommon (on a
	 * sample whose format description differs from the current one); version >= 2 only; NULL => "cannot accept"
	 * (return type Boolean in w0 [V]; what VT does on false - recreate the decoder - is [I]). */
	Boolean         (*canAcceptFormatDescription)(VTVideoDecoderRef dec, CMVideoFormatDescriptionRef format);
	/* +0x38 [V] caller VTDecompressionSessionFinishDelayedFrames: (dec); version >= 2 only; -12782 => 0. Must emit
	 * every frame still held (reorder queue / in flight in vcodecd) before returning. */
	OSStatus        (*finishDelayedFrames)(VTVideoDecoderRef dec);
	void            *startTileSession;      /* +0x40 version 3, VTTileDecompressionSessionCreate [V]; NULL for us */
	void            *decodeTile;            /* +0x48 version 3, VTTileDecompressionSessionDecodeTile [V]; NULL */
	void            *finishDelayedTiles;    /* +0x50 version 3 [V by Paravirtualized symbol]; NULL */
} n2_VTVideoDecoderClass;

typedef struct n2_VTVideoDecoderVTable {
	const void                      *reserved;      /* NULL [V] */
	const n2_CMBaseClass            *baseClass;     /* same record as the encoder's (n2vt_abi.h) [V] */
	const n2_VTVideoDecoderClass    *decoderClass;
} n2_VTVideoDecoderVTable;

_Static_assert(sizeof(n2_VTVideoDecoderClass) == 0x58, "VTVideoDecoderClass is 11 slots");
_Static_assert(__builtin_offsetof(n2_VTVideoDecoderClass, finishDelayedFrames) == 0x38, "FinishDelayedFrames slot");
_Static_assert(__builtin_offsetof(n2_VTVideoDecoderClass, startTileSession) == 0x40, "tile slots");
_Static_assert(sizeof(n2_VTVideoDecoderVTable) == 0x18, "vtable is 3 slots");

/* Factory [V] (mangled createWebKitVP8Decoder(unsigned int, __CFAllocator const*, OpaqueVTVideoDecoder**)). An error
 * or a NULL instance makes VTSelectAndCreateVideoDecoderInstanceInternal try the NEXT candidate [V @0x194cc7c44..
 * 0x194cc7ccc]: this is the only place where "hardware not available" can fall back to Apple's software decoder. */
typedef OSStatus (*n2_VTVideoDecoderCreateInstance)(uint32_t codecType, CFAllocatorRef allocator, VTVideoDecoderRef *instanceOut);

/* VideoToolbox exports (all [V] in the VideoToolbox export trie, research/macho.py). */
extern CMTypeID         VTVideoDecoderGetClassID(void);                                   /* @0x194cc9ad4 */
extern CMBaseObjectRef  VTVideoDecoderGetCMBaseObject(VTVideoDecoderRef dec);             /* @0x194cfc1e4: identity */
extern OSStatus VTRegisterVideoDecoder(uint32_t codecType, n2_VTVideoDecoderCreateInstance create);   /* @0x194cc80bc */
extern OSStatus VTRegisterVideoDecoderWithInfo(uint32_t codecType, CFDictionaryRef info,            /* @0x194cc80c8 */
                                               n2_VTVideoDecoderCreateInstance create);
/* @0x194ccbe7c: x0 session, x1 frame, w2 status, w3 infoFlags, x4 imageBuffer. imageBuffer is used only when status == 0
 * and !(infoFlags & kVTDecodeInfo_FrameDropped); VT retains it, copies the format description's colour attachments on to
 * it if it is untagged, and removes the token (an unknown or already-emitted token => -12902). Takes the session mutex. */
extern OSStatus VTDecoderSessionEmitDecodedFrame(VTVideoDecoderSession session, VTVideoDecoderFrame frame,
                                                 OSStatus status, uint32_t infoFlags, CVImageBufferRef imageBuffer);
/* @0x194cc9bac: retains attributes (the decoder's NATIVE output: format list, size, IOSurface); VT builds the pool and
 * inserts its own pixel transfer if the client asked for something else. Call in StartSession (Apple's do). */
extern OSStatus VTDecoderSessionSetPixelBufferAttributes(VTVideoDecoderSession session, CFDictionaryRef attributes);
/* @0x194cc9c44: NOT retained; brings the pool up to date with the last attributes; NULL on error. */
extern CVPixelBufferPoolRef VTDecoderSessionGetPixelBufferPool(VTVideoDecoderSession session);
/* @0x194cfbb88: = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, GetPixelBufferPool(session), out); the
 * second argument is ignored in this build [V]. */
extern OSStatus VTDecoderSessionCreatePixelBuffer(VTVideoDecoderSession session, CFDictionaryRef unused, CVPixelBufferRef *out);
/* @0x194cfc240: the client's destinationImageBufferAttributes, NOT retained, may be NULL. */
extern CFDictionaryRef VTDecoderSessionGetDestinationPixelBufferAttributes(VTVideoDecoderSession session);

/* Registration info keys ([V] exported; the CFString values read in the cache). */
extern const CFStringRef kVTVideoDecoder_IsHardwareAccelerated;         /* @0x1d69aac58 "VTIsHardwareAccelerated" */
extern const CFStringRef kVTVideoDecoderInfo_Rating;                    /* @0x1d69aa9d8 "VTRating" (CFNumber int32) */
/* "CMClassImplementationID" (plain CFSTR key, as for encoders). VideoToolbox APPENDS ".<codec 4cc>" to it [V @0x194cc8188],
 * so N2VT_DEC_ID_BASE below becomes "...mtkvdec.avc1" / "...mtkvdec.hvc1" in VTCopyVideoDecoderList / DecoderID. */
#define N2VT_DEC_ID_BASE        "com.nord2.videotoolbox.videodecoder.mtkvdec"
/* Rating: higher sorts first, ties broken by IsHardwareAccelerated=true first [V vtSortRegistryItemsByRating
 * @0x194ed1c58]. Apple's H264SW ('avc1') and VCPHEVC ('hvc1') register IsHardwareAccelerated=FALSE, no rating (=> 0);
 * AppleAVD registers with plain VTRegisterVideoDecoder (=> hardware=TRUE, rating 0). 100 puts us first. */
#define N2VT_DEC_RATING         100

/* Decoder switches (proposal, opus-dec3; same convention as n2vt_abi.h: presence of the file is the switch, read once in
 * SLIMRegister). N2VT_SWITCH_OFF ("nord2-video-off") is honoured too. The decoder registers ONLY while HWDEC exists. */
#define N2VT_SWITCH_HWDEC       "/private/var/db/nord2-video-hwdec"        /* register the hardware decoders */
#define N2VT_SWITCH_HWDEC_NOHEVC "/private/var/db/nord2-video-hwdec-no-hevc" /* register avc1 only */

/* Public VTDecompressionProperties/VTErrors values used by decoders (iPhoneOS SDK headers, not read from the cache). */
enum {
	N2_kVTDecodeFrame_EnableAsynchronousDecompression = 1u << 0,
	N2_kVTDecodeFrame_DoNotOutputFrame                = 1u << 1,
	N2_kVTDecodeFrame_1xRealTimePlayback              = 1u << 2,
	N2_kVTDecodeFrame_EnableTemporalProcessing        = 1u << 3,
	N2_kVTDecodeInfo_Asynchronous                     = 1u << 0,
	N2_kVTDecodeInfo_FrameDropped                     = 1u << 1,
	N2_kVTDecodeInfo_ImageBufferModifiable            = 1u << 2,
};
enum {
	N2_kVTVideoDecoderBadDataErr            = -12909,
	N2_kVTVideoDecoderUnsupportedDataFormatErr = -12910,
	N2_kVTVideoDecoderMalfunctionErr        = -12911,   /* also what VT signals for "CreateInstance gave NULL" [V] */
	N2_kVTVideoDecoderNotAvailableNowErr    = -12913,
	N2_kVTUnimplementedErr                  = -12782,   /* "slot not implemented": VT treats it as "use the default" [V] */
};

#endif
