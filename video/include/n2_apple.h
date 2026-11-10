/*
 * n2_apple.h -- hand-written declarations of the few CoreFoundation / CoreMedia / CoreVideo calls the gpu/video code uses.
 * No Apple SDK header is involved (same method as gpu/build/camera-userland-20261109/src/rwcam_abi.h). LEAD-OWNED, FROZEN.
 *
 * Two builds see this file:
 *   - the iOS arm64 build (clang --target=arm64-apple-ios16.0): the symbols come from the TBD stubs in gpu/video/sdk/lib;
 *   - the host test build (-DN2_HOST_FAKE): the symbols come from gpu/video/fake/fake_apple.c (package WP02).
 * A package that needs a call that is not here declares it in its OWN header (e.g. probe/probe_apple.h), lists the symbol in its
 * own <package>/symbols.txt, and posts the addition on the board. Nobody but the lead edits this file.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef N2_APPLE_H
#define N2_APPLE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t         OSStatus;
typedef unsigned char   Boolean;
typedef long            CFIndex;
typedef unsigned long   CFTypeID;
typedef unsigned long   CMTypeID;
typedef const void      *CFTypeRef;
typedef const struct __CFAllocator      *CFAllocatorRef;
typedef const struct __CFString         *CFStringRef;
typedef const struct __CFDictionary     *CFDictionaryRef;
typedef struct __CFDictionary           *CFMutableDictionaryRef;
typedef const struct __CFArray          *CFArrayRef;
typedef struct __CFArray                *CFMutableArrayRef;
typedef const struct __CFNumber         *CFNumberRef;
typedef const struct __CFBoolean        *CFBooleanRef;
typedef const struct __CFData           *CFDataRef;
typedef struct OpaqueCMBaseObject       *CMBaseObjectRef;
typedef struct OpaqueCMBlockBuffer      *CMBlockBufferRef;
typedef struct opaqueCMSampleBuffer     *CMSampleBufferRef;
typedef const struct opaqueCMFormatDescription *CMFormatDescriptionRef;
typedef CMFormatDescriptionRef          CMVideoFormatDescriptionRef;
typedef struct __CVBuffer               *CVBufferRef;
typedef CVBufferRef                     CVImageBufferRef;
typedef CVBufferRef                     CVPixelBufferRef;

typedef struct { int64_t value; int32_t timescale; uint32_t flags; int64_t epoch; } CMTime;        /* 24 bytes */
typedef struct { CMTime duration, presentationTimeStamp, decodeTimeStamp; } CMSampleTimingInfo;     /* 72 bytes */
typedef struct { int32_t width, height; } CMVideoDimensions;
_Static_assert(sizeof(CMTime) == 24, "CMTime");
#define N2_CMTIME_VALID 1u

#ifdef N2_HOST_FAKE
CFStringRef n2fake_cfstr(const char *s);                /* interned: the same text gives the same pointer */
#define N2STR(s) n2fake_cfstr(s)
#else
#define N2STR(s) ((CFStringRef)__builtin___CFStringMakeConstantString(s))
#endif

/* ---- CoreFoundation ---- */
extern const CFAllocatorRef kCFAllocatorDefault;
extern const CFAllocatorRef kCFAllocatorNull;
extern const CFBooleanRef   kCFBooleanTrue, kCFBooleanFalse;
typedef struct { CFIndex version; void *retain, *release, *copyDescription, *equal, *hash; } CFDictionaryKeyCallBacks;
typedef struct { CFIndex version; void *retain, *release, *copyDescription, *equal; } CFDictionaryValueCallBacks;
typedef struct { CFIndex version; void *retain, *release, *copyDescription, *equal; } CFArrayCallBacks;
extern const CFDictionaryKeyCallBacks   kCFTypeDictionaryKeyCallBacks;
extern const CFDictionaryValueCallBacks kCFTypeDictionaryValueCallBacks;
extern const CFArrayCallBacks           kCFTypeArrayCallBacks;
CFTypeRef       CFRetain(CFTypeRef cf);
void            CFRelease(CFTypeRef cf);
CFTypeID        CFGetTypeID(CFTypeRef cf);
Boolean         CFEqual(CFTypeRef a, CFTypeRef b);
CFTypeID        CFNumberGetTypeID(void);
CFTypeID        CFBooleanGetTypeID(void);
CFTypeID        CFStringGetTypeID(void);
CFTypeID        CFDictionaryGetTypeID(void);
CFMutableDictionaryRef CFDictionaryCreateMutable(CFAllocatorRef a, CFIndex capacity, const CFDictionaryKeyCallBacks *k,
                                                 const CFDictionaryValueCallBacks *v);
void            CFDictionarySetValue(CFMutableDictionaryRef d, const void *key, const void *value);
const void     *CFDictionaryGetValue(CFDictionaryRef d, const void *key);
CFMutableArrayRef CFArrayCreateMutable(CFAllocatorRef a, CFIndex capacity, const CFArrayCallBacks *cb);
void            CFArrayAppendValue(CFMutableArrayRef arr, const void *value);
CFIndex         CFArrayGetCount(CFArrayRef arr);
const void     *CFArrayGetValueAtIndex(CFArrayRef arr, CFIndex i);
enum { kCFNumberSInt32Type = 3, kCFNumberSInt64Type = 4, kCFNumberFloat64Type = 6 };
CFNumberRef     CFNumberCreate(CFAllocatorRef a, CFIndex type, const void *valuePtr);
Boolean         CFNumberGetValue(CFNumberRef n, CFIndex type, void *valuePtr);
Boolean         CFBooleanGetValue(CFBooleanRef b);
enum { kCFStringEncodingUTF8 = 0x08000100 };
CFStringRef     CFStringCreateWithCString(CFAllocatorRef a, const char *s, uint32_t encoding);
Boolean         CFStringGetCString(CFStringRef s, char *buf, CFIndex size, uint32_t encoding);

/* ---- CoreMedia ---- */
OSStatus        CMBlockBufferCreateWithMemoryBlock(CFAllocatorRef structureAllocator, void *memoryBlock, size_t blockLength,
                                                   CFAllocatorRef blockAllocator, const void *customBlockSource, size_t offsetToData,
                                                   size_t dataLength, uint32_t flags, CMBlockBufferRef *blockBufferOut);
                /* memoryBlock NULL + blockAllocator kCFAllocatorDefault + flags N2_kCMBlockBufferAssureMemoryNowFlag: CoreMedia allocates */
#define N2_kCMBlockBufferAssureMemoryNowFlag 1u
OSStatus        CMBlockBufferReplaceDataBytes(const void *sourceBytes, CMBlockBufferRef dst, size_t offsetIntoDestination, size_t dataLength);
size_t          CMBlockBufferGetDataLength(CMBlockBufferRef b);
OSStatus        CMBlockBufferCopyDataBytes(CMBlockBufferRef b, size_t offsetToData, size_t dataLength, void *destination);
OSStatus        CMSampleBufferCreateReady(CFAllocatorRef a, CMBlockBufferRef dataBuffer, CMFormatDescriptionRef fmt, CFIndex numSamples,
                                          CFIndex numSampleTimingEntries, const CMSampleTimingInfo *timing,
                                          CFIndex numSampleSizeEntries, const size_t *sampleSizeArray, CMSampleBufferRef *out);
CFArrayRef      CMSampleBufferGetSampleAttachmentsArray(CMSampleBufferRef s, Boolean createIfNecessary);    /* array of mutable dictionaries */
CMBlockBufferRef CMSampleBufferGetDataBuffer(CMSampleBufferRef s);
CMFormatDescriptionRef CMSampleBufferGetFormatDescription(CMSampleBufferRef s);
OSStatus        CMVideoFormatDescriptionCreateFromH264ParameterSets(CFAllocatorRef a, size_t parameterSetCount,
                                                                    const uint8_t *const *parameterSetPointers,
                                                                    const size_t *parameterSetSizes, int NALUnitHeaderLength,
                                                                    CMFormatDescriptionRef *formatDescriptionOut);
OSStatus        CMVideoFormatDescriptionGetH264ParameterSetAtIndex(CMFormatDescriptionRef fmt, size_t index, const uint8_t **ps,
                                                                   size_t *size, size_t *count, int *nalUnitHeaderLength);
extern const CFStringRef kCMSampleAttachmentKey_NotSync;                /* kCFBooleanTrue on every non-IDR frame */
extern const CFStringRef kCMSampleAttachmentKey_DependsOnOthers;        /* kCFBooleanTrue on P frames, kCFBooleanFalse on IDR */

/* ---- CoreVideo ---- */
#define N2_PIXFMT_420V 0x34323076u      /* '420v' NV12 video range  */
#define N2_PIXFMT_420F 0x34323066u      /* '420f' NV12 full range   */
#define N2_PIXFMT_I420 0x79343230u      /* 'y420' three planes      */
enum { kCVPixelBufferLock_ReadOnly = 1 };
int32_t         CVPixelBufferLockBaseAddress(CVPixelBufferRef pb, uint64_t lockFlags);
int32_t         CVPixelBufferUnlockBaseAddress(CVPixelBufferRef pb, uint64_t lockFlags);
void           *CVPixelBufferGetBaseAddressOfPlane(CVPixelBufferRef pb, size_t plane);
size_t          CVPixelBufferGetBytesPerRowOfPlane(CVPixelBufferRef pb, size_t plane);
size_t          CVPixelBufferGetWidth(CVPixelBufferRef pb);
size_t          CVPixelBufferGetHeight(CVPixelBufferRef pb);
size_t          CVPixelBufferGetPlaneCount(CVPixelBufferRef pb);
uint32_t        CVPixelBufferGetPixelFormatType(CVPixelBufferRef pb);
int32_t         CVPixelBufferCreate(CFAllocatorRef a, size_t width, size_t height, uint32_t pixelFormat, CFDictionaryRef attrs,
                                    CVPixelBufferRef *out);
extern const CFStringRef kCVPixelBufferPixelFormatTypeKey;
extern const CFStringRef kCVPixelBufferWidthKey;
extern const CFStringRef kCVPixelBufferHeightKey;
extern const CFStringRef kCVPixelBufferIOSurfacePropertiesKey;

#ifdef __cplusplus
}
#endif
#endif
