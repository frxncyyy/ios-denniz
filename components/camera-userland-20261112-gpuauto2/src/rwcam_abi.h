/*
 * Hand-written declarations of the few public CoreFoundation / CoreMedia / CoreVideo / Objective-C runtime / libSystem
 * entry points the camera code uses. There is no Apple SDK on the build machine and none is redistributed: everything
 * here is the documented (public) shape of the API, and the TBD stubs in ../sdk/lib list exactly these symbols by
 * install name (tools/check_tbd.py verifies each against the iOS 16.7 cache's export tables).
 */
#ifndef RWCAM_ABI_H
#define RWCAM_ABI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- libc / libSystem ---------------------------------------------------------------------------------- */
typedef long ssize_t;
int open(const char *, int, ...);
int close(int);
long read(int, void *, unsigned long);
long write(int, const void *, unsigned long);
int access(const char *, int);
int snprintf(char *, unsigned long, const char *, ...);
int vsnprintf(char *, unsigned long, const char *, __builtin_va_list);
void *malloc(unsigned long);
void *calloc(unsigned long, unsigned long);
void free(void *);
void *memset(void *, int, unsigned long);
void *memcpy(void *, const void *, unsigned long);
int memcmp(const void *, const void *, unsigned long);
int strcmp(const char *, const char *);
unsigned long strlen(const char *);
unsigned long strlcpy(char *, const char *, unsigned long);
char *strerror(int);
int *__error(void);
#define errno (*__error())
#define EAGAIN 35
#define EIO 5
#define ETIMEDOUT 60
#define EINTR 4
#define EBUSY 16
#define ENOENT 2
#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR 0x0002
#define O_NONBLOCK 0x0004
#define O_NOCTTY 0x20000
#define F_OK 0
const char *getprogname(void);
void *dlsym(void *, const char *);
#define RTLD_DEFAULT ((void *)-2)
int usleep(unsigned int);
double pow(double, double);
double log(double);
double exp(double);

typedef struct _opaque_pthread_t *pthread_t;
typedef struct { long sig; char opaque[56]; } pthread_mutex_t;
typedef struct { long sig; char opaque[8]; } pthread_attr_t_unused;
#define PTHREAD_MUTEX_INITIALIZER { 0x32AAABA7, { 0 } }
int pthread_mutex_lock(pthread_mutex_t *);
int pthread_mutex_unlock(pthread_mutex_t *);
int pthread_mutex_init(pthread_mutex_t *, const void *);
int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
int pthread_join(pthread_t, void **);
int pthread_detach(pthread_t);
pthread_t pthread_self(void);
int pthread_equal(pthread_t, pthread_t);
int pthread_setname_np(const char *);
/* (camera-userland-20261112-pipe: the pictures thread; arm64 Darwin layouts, _pthread_types.h / pthread_impl.h) */
typedef struct { long sig; char opaque[40]; } pthread_cond_t;
typedef struct { long sig; char opaque[8]; } pthread_once_t;
#define PTHREAD_COND_INITIALIZER { 0x3CB0B1BB, { 0 } }
#define PTHREAD_ONCE_INIT { 0x30B1BCBA, { 0 } }
int pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int pthread_cond_broadcast(pthread_cond_t *);
int pthread_once(pthread_once_t *, void (*)(void));

/* dyld (in libSystem) */
const void *_dyld_get_shared_cache_range(size_t *length);
struct mach_header;
void _dyld_register_func_for_add_image(void (*)(const struct mach_header *, intptr_t));

/* libdispatch (in libSystem) */
typedef struct dispatch_queue_s *dispatch_queue_t;
dispatch_queue_t dispatch_get_global_queue(intptr_t identifier, uintptr_t flags);
void dispatch_apply_f(size_t iterations, dispatch_queue_t queue, void *context, void (*work)(void *, size_t));
#define RW_QOS_USER_INTERACTIVE 0x21

/* ---- Objective-C runtime (libobjc.A.dylib) ------------------------------------------------------------------ */
typedef struct objc_object *id;
typedef struct objc_class *Class;
typedef struct objc_selector *SEL;
typedef struct objc_method *Method;
typedef void (*IMP)(void);
typedef signed char BOOL;
Class objc_getClass(const char *);
SEL sel_registerName(const char *);
Method class_getClassMethod(Class, SEL);
Method class_getInstanceMethod(Class, SEL);
IMP method_getImplementation(Method);
IMP method_setImplementation(Method, IMP);
const char *method_getTypeEncoding(Method);
void objc_msgSend(void);

/* ---- CoreFoundation ------------------------------------------------------------------------------------ */
typedef unsigned char Boolean;
typedef long CFIndex;
typedef unsigned long CFTypeID;
typedef int32_t OSStatus;
typedef const void *CFTypeRef;
typedef const struct __CFAllocator *CFAllocatorRef;
typedef const struct __CFString *CFStringRef;
typedef const struct __CFNumber *CFNumberRef;
typedef const struct __CFBoolean *CFBooleanRef;
typedef const struct __CFArray *CFArrayRef;
typedef const struct __CFDictionary *CFDictionaryRef;
typedef struct __CFDictionary *CFMutableDictionaryRef;
typedef const struct __CFSet *CFSetRef;
typedef struct __CFSet *CFMutableSetRef;
typedef uint32_t CFStringEncoding;
#define kCFStringEncodingUTF8 0x08000100u
enum { kCFNumberSInt32Type = 3, kCFNumberSInt64Type = 4, kCFNumberFloatType = 5, kCFNumberDoubleType = 6 };
struct CFDictionaryKeyCallBacks;
struct CFDictionaryValueCallBacks;
struct CFArrayCallBacks;
struct CFSetCallBacks;
extern const struct CFDictionaryKeyCallBacks kCFTypeDictionaryKeyCallBacks;
extern const struct CFDictionaryValueCallBacks kCFTypeDictionaryValueCallBacks;
extern const struct CFArrayCallBacks kCFTypeArrayCallBacks;
extern const struct CFSetCallBacks kCFTypeSetCallBacks;
extern const CFBooleanRef kCFBooleanTrue, kCFBooleanFalse;
CFTypeRef CFRetain(CFTypeRef);
void CFRelease(CFTypeRef);
CFTypeID CFGetTypeID(CFTypeRef);
CFTypeID CFNumberGetTypeID(void);
CFTypeID CFBooleanGetTypeID(void);
CFTypeID CFDictionaryGetTypeID(void);
CFTypeID CFStringGetTypeID(void);
CFNumberRef CFNumberCreate(CFAllocatorRef, int, const void *);
Boolean CFNumberGetValue(CFNumberRef, int, void *);
Boolean CFBooleanGetValue(CFBooleanRef);
CFStringRef CFStringCreateWithCString(CFAllocatorRef, const char *, CFStringEncoding);
Boolean CFStringGetCString(CFStringRef, char *, CFIndex, CFStringEncoding);
CFStringRef CFCopyDescription(CFTypeRef);
CFMutableDictionaryRef CFDictionaryCreateMutable(CFAllocatorRef, CFIndex, const struct CFDictionaryKeyCallBacks *,
    const struct CFDictionaryValueCallBacks *);
CFDictionaryRef CFDictionaryCreate(CFAllocatorRef, const void **, const void **, CFIndex, const struct CFDictionaryKeyCallBacks *,
    const struct CFDictionaryValueCallBacks *);
void CFDictionarySetValue(CFMutableDictionaryRef, const void *, const void *);
const void *CFDictionaryGetValue(CFDictionaryRef, const void *);
CFIndex CFDictionaryGetCount(CFDictionaryRef);
void CFDictionaryGetKeysAndValues(CFDictionaryRef, const void **, const void **);
CFArrayRef CFArrayCreate(CFAllocatorRef, const void **, CFIndex, const struct CFArrayCallBacks *);
CFIndex CFArrayGetCount(CFArrayRef);
const void *CFArrayGetValueAtIndex(CFArrayRef, CFIndex);

/* ---- CoreMedia ----------------------------------------------------------------------------------------- */
typedef struct { int64_t value; int32_t timescale; uint32_t flags; int64_t epoch; } CMTime;
typedef struct OpaqueCMBaseObject *CMBaseObjectRef;
typedef const void *CMBaseClassID;
typedef struct opaqueCMSampleBuffer *CMSampleBufferRef;
typedef const struct opaqueCMFormatDescription *CMVideoFormatDescriptionRef;
typedef struct OpaqueCMClock *CMClockRef;
struct vtable;
OSStatus CMDerivedObjectCreate(CFAllocatorRef, const struct vtable *, CMBaseClassID, CMBaseObjectRef *);
void *CMBaseObjectGetDerivedStorage(CMBaseObjectRef);
CMTime CMClockMakeHostTimeFromSystemUnits(uint64_t);
CMClockRef CMClockGetHostTimeClock(void);
OSStatus CMVideoFormatDescriptionCreate(CFAllocatorRef, uint32_t codecType, int32_t w, int32_t h, CFDictionaryRef ext,
    CMVideoFormatDescriptionRef *out);

/* ---- CoreVideo ----------------------------------------------------------------------------------------- */
typedef struct __CVBuffer *CVPixelBufferRef;
typedef struct __CVPixelBufferPool *CVPixelBufferPoolRef;
typedef int32_t CVReturn;
#define kCVReturnSuccess 0
#define kCVAttachmentMode_ShouldPropagate 1
extern const CFStringRef kCVPixelBufferWidthKey, kCVPixelBufferHeightKey, kCVPixelBufferPixelFormatTypeKey,
    kCVPixelBufferIOSurfacePropertiesKey;
extern const CFStringRef kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_601_4, kCVImageBufferColorPrimariesKey,
    kCVImageBufferColorPrimaries_ITU_R_709_2, kCVImageBufferTransferFunctionKey, kCVImageBufferTransferFunction_ITU_R_709_2;
CVReturn CVPixelBufferPoolCreate(CFAllocatorRef, CFDictionaryRef, CFDictionaryRef, CVPixelBufferPoolRef *);
CVReturn CVPixelBufferPoolCreatePixelBuffer(CFAllocatorRef, CVPixelBufferPoolRef, CVPixelBufferRef *);
void CVPixelBufferPoolRelease(CVPixelBufferPoolRef);
CFDictionaryRef CVPixelBufferPoolGetPixelBufferAttributes(CVPixelBufferPoolRef);
CVReturn CVPixelBufferLockBaseAddress(CVPixelBufferRef, uint64_t);
CVReturn CVPixelBufferUnlockBaseAddress(CVPixelBufferRef, uint64_t);
void *CVPixelBufferGetBaseAddress(CVPixelBufferRef);
void *CVPixelBufferGetBaseAddressOfPlane(CVPixelBufferRef, size_t);
size_t CVPixelBufferGetBytesPerRow(CVPixelBufferRef);
size_t CVPixelBufferGetBytesPerRowOfPlane(CVPixelBufferRef, size_t);
uint32_t CVPixelBufferGetPixelFormatType(CVPixelBufferRef);     /* (q10jn: the buffer's own format, whatever the pool's attributes say) */
void CVPixelBufferRelease(CVPixelBufferRef);
void CVBufferSetAttachment(CVPixelBufferRef, CFStringRef, CFTypeRef, uint32_t mode);

/* a block (clang's ABI, `^(CVPixelBufferRef, CMTime)`) called without the blocks runtime: layout isa, flags, reserved, invoke */
struct rw_block { void *isa; int32_t flags, reserved; void *invoke; };
static inline void
rw_call_pixel_handler(const void *block, CVPixelBufferRef pb, CMTime t)
{
	((void (*)(const void *, CVPixelBufferRef, CMTime))((const struct rw_block *)block)->invoke)(block, pb, t);
}

#endif
