/*
 * probe_apple.h -- hand-written declarations of the Apple frameworks the probe measures: VideoToolbox, ImageIO /
 * CoreGraphics, AudioToolbox, and the four libc calls those need.  No Apple SDK header is involved (same method as
 * gpu/build/camera-userland-20261109/src/rwcam_abi.h); every symbol listed here is in probe/symbols.txt and is
 * checked by the lead against the phone's dyld cache before an install.
 *
 * Signatures are public-API signatures written from documentation memory.  STATUS.json says which call has which
 * prototype so the lead can verify them one by one.
 *
 * Only this file (and probe_core.h) may be included by probe/probe_*.c; include/n2_apple.h is the lead's shared part.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PROBE_APPLE_H
#define PROBE_APPLE_H

#include "n2_apple.h"

#ifdef __cplusplus
extern "C" {
#endif

#define N2_FOURCC(a, b, c, d) \
	(((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

#define N2_CODEC_AVC1 N2_FOURCC('a', 'v', 'c', '1')
#define N2_CODEC_HVC1 N2_FOURCC('h', 'v', 'c', '1')
#define N2_CODEC_JPEG N2_FOURCC('j', 'p', 'e', 'g')
#define N2_CODEC_AV01 N2_FOURCC('a', 'v', '0', '1')
#define N2_CODEC_MP4V N2_FOURCC('m', 'p', '4', 'v')
#define N2_CODEC_AAC  N2_FOURCC('a', 'a', 'c', ' ')
#define N2_CODEC_LPCM N2_FOURCC('l', 'p', 'c', 'm')

/* ---- libSystem: the only four calls of it this header needs (no <dlfcn.h> in sdk/sysinc) ---- */
#define N2_RTLD_LAZY  1
#define N2_RTLD_NOW   2
#define N2_RTLD_LOCAL 4
#define N2_RTLD_DEFAULT ((void *)-2)
extern void *dlopen(const char *path, int mode);
extern void *dlsym(void *handle, const char *symbol);
extern char *dlerror(void);

/* ---- CoreFoundation: the few objects n2_apple.h does not declare ---- */
typedef struct __CFData *CFMutableDataRef;
extern CFMutableDataRef CFDataCreateMutable(CFAllocatorRef allocator, CFIndex capacity);
extern CFDataRef        CFDataCreate(CFAllocatorRef allocator, const uint8_t *bytes, CFIndex length);
extern CFIndex          CFDataGetLength(CFDataRef data);
extern const uint8_t   *CFDataGetBytePtr(CFDataRef data);

/* ---- CoreMedia: HEVC parameter sets and dimensions ---- */
typedef uint32_t CMVideoCodecType;
extern OSStatus CMVideoFormatDescriptionCreateFromHEVCParameterSets(CFAllocatorRef allocator, size_t parameterSetCount,
                                                                    const uint8_t *const *parameterSetPointers,
                                                                    const size_t *parameterSetSizes,
                                                                    int nalUnitHeaderLength, CFDictionaryRef extensions,
                                                                    CMFormatDescriptionRef *formatDescriptionOut);

/* ---- CoreVideo: the single-plane accessor for the BGRA destination of the transfer test ---- */

/* ---- VideoToolbox ---- */
typedef struct OpaqueVTCompressionSession   *VTCompressionSessionRef;
typedef struct OpaqueVTDecompressionSession *VTDecompressionSessionRef;
typedef struct OpaqueVTPixelTransferSession *VTPixelTransferSessionRef;
typedef struct OpaqueVTSession              *VTSessionRef;
typedef uint32_t VTEncodeInfoFlags;
typedef uint32_t VTDecodeInfoFlags;

typedef void (*VTCompressionOutputCallback)(void *outputCallbackRefCon, void *sourceFrameRefCon, OSStatus status,
                                            VTEncodeInfoFlags infoFlags, CMSampleBufferRef sampleBuffer);
typedef struct {
	void                        *compressionOutputRefCon;
	VTCompressionOutputCallback  compressionOutputCallback;
} VTCompressionOutputCallbackRecord;

typedef void (*VTDecompressionOutputCallback)(void *decompressionOutputRefCon, void *sourceFrameRefCon, OSStatus status,
                                              VTDecodeInfoFlags infoFlags, CVImageBufferRef imageBuffer,
                                              CMTime presentationTimeStamp, CMTime presentationDuration);
typedef struct {
	VTDecompressionOutputCallback  decompressionOutputCallback;     /* (lead) the callback is the FIRST member: run 1 on the */
	void                          *decompressionOutputRefCon;       /* phone jumped to the refcon (five crashes, pc on the stack) */
} VTDecompressionOutputCallbackRecord;

extern OSStatus VTCompressionSessionCreate(CFAllocatorRef allocator, int32_t width, int32_t height, CMVideoCodecType codecType,
                                           CFDictionaryRef encoderSpecification, CFDictionaryRef sourceImageBufferAttributes,
                                           CFAllocatorRef compressedDataAllocator,
                                           VTCompressionOutputCallback outputCallback, void *outputCallbackRefCon,
                                           VTCompressionSessionRef *compressionSessionOut);
                /* (lead, 11 Nov) the public signature: callback and refcon are two arguments (10 in all, the last two on the
                 * stack); only VTDecompressionSessionCreate takes a callback RECORD by pointer. */
extern OSStatus VTCompressionSessionEncodeFrame(VTCompressionSessionRef session, CVImageBufferRef imageBuffer,
                                                CMTime presentationTimeStamp, CMTime duration,
                                                CFDictionaryRef frameProperties, void *sourceFrameRefCon,
                                                VTEncodeInfoFlags *infoFlagsOut);
extern OSStatus VTCompressionSessionCompleteFrames(VTCompressionSessionRef session,
                                                   CMTime completeUntilPresentationTimeStamp);
extern void     VTCompressionSessionInvalidate(VTCompressionSessionRef session);

extern OSStatus VTDecompressionSessionCreate(CFAllocatorRef allocator, CMFormatDescriptionRef videoFormatDescription,
                                             CFDictionaryRef videoDecoderSpecification,
                                             CFDictionaryRef destinationImageBufferAttributes,
                                             const VTDecompressionOutputCallbackRecord *outputCallback,
                                             VTDecompressionSessionRef *decompressionSessionOut);
extern OSStatus VTDecompressionSessionDecodeFrame(VTDecompressionSessionRef session, CMSampleBufferRef sampleBuffer,
                                                  VTDecodeInfoFlags flags, void *sourceFrameRefCon,
                                                  VTDecodeInfoFlags *infoFlagsOut);
extern OSStatus VTDecompressionSessionWaitForAsynchronousFrames(VTDecompressionSessionRef session);
extern void     VTDecompressionSessionInvalidate(VTDecompressionSessionRef session);

extern OSStatus VTSessionSetProperty(VTSessionRef session, CFStringRef propertyKey, CFTypeRef propertyValue);
extern OSStatus VTSessionCopyProperty(VTSessionRef session, CFStringRef propertyKey, CFAllocatorRef allocator,
                                      void *propertyValueOut);
extern OSStatus VTCopyVideoEncoderList(CFDictionaryRef options, CFArrayRef *listOfVideoEncodersOut);

extern OSStatus VTPixelTransferSessionCreate(CFAllocatorRef allocator, VTPixelTransferSessionRef *sessionOut);
extern OSStatus VTPixelTransferSessionTransferImage(VTPixelTransferSessionRef session, CVImageBufferRef source,
                                                    CVImageBufferRef destination);
extern void     VTPixelTransferSessionInvalidate(VTPixelTransferSessionRef session);

/* The constants we need.  The four encoder-list keys are exported (checked by the lead); the optional ones are looked
 * up with dlsym so that a missing constant becomes a reported "no" instead of a crash at load. */
extern const CFStringRef kVTVideoEncoderList_CodecType;
extern const CFStringRef kVTVideoEncoderList_EncoderID;
extern const CFStringRef kVTVideoEncoderList_EncoderName;
extern const CFStringRef kVTVideoEncoderList_DisplayName;

/* ---- CoreGraphics ---- */
typedef struct __CGImage        *CGImageRef;
typedef struct __CGContext      *CGContextRef;
typedef struct __CGColorSpace   *CGColorSpaceRef;
typedef struct CGDataProvider   *CGDataProviderRef;
typedef double                   CGFloat;
typedef struct { CGFloat x, y; } CGPoint;
typedef struct { CGFloat width, height; } CGSize;
typedef struct { CGPoint origin; CGSize size; } CGRect;

extern CGColorSpaceRef CGColorSpaceCreateDeviceRGB(void);
extern void            CGColorSpaceRelease(CGColorSpaceRef space);
extern CGContextRef    CGBitmapContextCreate(void *data, size_t width, size_t height, size_t bitsPerComponent,
                                             size_t bytesPerRow, CGColorSpaceRef space, uint32_t bitmapInfo);
extern void            CGContextRelease(CGContextRef ctx);
extern void            CGContextDrawImage(CGContextRef ctx, CGRect rect, CGImageRef image);
extern CGImageRef      CGBitmapContextCreateImage(CGContextRef ctx);
extern size_t          CGImageGetWidth(CGImageRef image);
extern size_t          CGImageGetHeight(CGImageRef image);
extern void            CGImageRelease(CGImageRef image);

/* ---- ImageIO ---- */
typedef struct __CGImageSource      *CGImageSourceRef;
typedef struct __CGImageDestination *CGImageDestinationRef;

extern CGImageDestinationRef CGImageDestinationCreateWithData(CFMutableDataRef data, CFStringRef type, size_t count,
                                                              CFDictionaryRef options);
extern void                  CGImageDestinationAddImage(CGImageDestinationRef dest, CGImageRef image,
                                                        CFDictionaryRef properties);
extern Boolean               CGImageDestinationFinalize(CGImageDestinationRef dest);
extern CGImageSourceRef      CGImageSourceCreateWithData(CFDataRef data, CFDictionaryRef options);
extern CGImageRef            CGImageSourceCreateImageAtIndex(CGImageSourceRef source, size_t index,
                                                             CFDictionaryRef options);
extern size_t                CGImageSourceGetCount(CGImageSourceRef source);

/* ---- AudioToolbox ---- */
typedef struct OpaqueAudioConverter *AudioConverterRef;
typedef uint32_t AudioFormatID;
typedef uint32_t AudioFormatFlags;

typedef struct AudioStreamBasicDescription {
	double         mSampleRate;
	AudioFormatID  mFormatID;
	AudioFormatFlags mFormatFlags;
	uint32_t       mBytesPerPacket;
	uint32_t       mFramesPerPacket;
	uint32_t       mBytesPerFrame;
	uint32_t       mChannelsPerFrame;
	uint32_t       mBitsPerChannel;
	uint32_t       mReserved;
} AudioStreamBasicDescription;

typedef struct AudioBuffer {
	uint32_t mNumberChannels;
	uint32_t mDataByteSize;
	void    *mData;
} AudioBuffer;

typedef struct AudioBufferList {
	uint32_t     mNumberBuffers;
	AudioBuffer  mBuffers[1];
} AudioBufferList;

typedef struct AudioStreamPacketDescription {
	int64_t  mStartOffset;
	uint32_t mVariableFramesInPacket;
	uint32_t mDataByteSize;
} AudioStreamPacketDescription;

typedef OSStatus (*AudioConverterComplexInputDataProc)(AudioConverterRef converter, uint32_t *ioNumberDataPackets,
                                                       AudioBufferList *ioData,
                                                       AudioStreamPacketDescription **outDataPacketDescription,
                                                       void *inUserData);

extern OSStatus AudioConverterNew(const AudioStreamBasicDescription *sourceFormat,
                                  const AudioStreamBasicDescription *destinationFormat,
                                  AudioConverterRef *outAudioConverter);
extern OSStatus AudioConverterDispose(AudioConverterRef converter);
extern OSStatus AudioConverterFillComplexBuffer(AudioConverterRef converter,
                                                AudioConverterComplexInputDataProc inputProc, void *inUserData,
                                                uint32_t *ioOutputDataPacketSize, AudioBufferList *outOutputData,
                                                AudioStreamPacketDescription *outPacketDescription);
extern OSStatus AudioConverterSetProperty(AudioConverterRef converter, uint32_t propertyID, uint32_t propertyDataSize,
                                          const void *propertyData);
extern OSStatus AudioConverterGetProperty(AudioConverterRef converter, uint32_t propertyID,
                                          uint32_t *ioPropertyDataSize, void *outPropertyData);

enum {
	N2_kAudioFormatLinearPCM      = N2_FOURCC('l', 'p', 'c', 'm'),
	N2_kAudioFormatMPEG4AAC       = N2_FOURCC('a', 'a', 'c', ' '),
	N2_kAudioFormatFlagIsFloat    = 1u << 0,
	N2_kAudioFormatFlagIsSignedInteger = 1u << 2,
	N2_kAudioFormatFlagIsPacked   = 1u << 3,
	N2_kAudioConverterEncodeBitRate                 = N2_FOURCC('b', 'r', 'a', 't'),
	N2_kAudioConverterCurrentInputStreamDescription = N2_FOURCC('a', 'c', 'i', 'd'),
	N2_kAudioConverterCurrentOutputStreamDescription = N2_FOURCC('a', 'c', 'o', 'd'),
};

/* ---- CMPhoto: dlsym only, never called (T06) ---- */
#define N2_CMPHOTO_PATH "/System/Library/PrivateFrameworks/CMPhoto.framework/CMPhoto"

#ifdef __cplusplus
}
#endif
#endif
