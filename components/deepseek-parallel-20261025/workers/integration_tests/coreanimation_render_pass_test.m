/*
 * Independent CoreAnimation-shaped render-pass/admission test.
 *
 * This is intentionally not the render worker's large frontend harness.  It
 * compiles the production nord2_render_pass.inc against a small fake ObjC
 * frontend and a fake JM job executor (status 4 = completed).  The fake JM
 * writes independently calculated target bytes, so the test checks both pass
 * admission and the target geometry/stride rather than only inspecting flags.
 *
 * The pipeline shape used for the two-attachment cases is a small independent
 * descriptor model: two colour attachments, slot 0 writeMask 15, slot 1
 * writeMask 0, formats 80/80 or 80/115.  The production render-pipeline
 * compiler is NOT exercised here; that distinction is reported in the test
 * output and requires a separate full-frontend harness.
 *
 * Host only: no phone, GPU, native library rebuild or production edit.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef unsigned long NSUInteger;
typedef unsigned long long U64;
typedef struct { double red, green, blue, alpha; } TeguClearColor;

#define TEGU_COMMAND_MAX_JOBS 64u
#define TEST_MAX_JOBS 64u
#define TEST_SENTINEL 0xa5u
#define TEST_DRAW_VALUE 0x5au

extern id class_createInstance(id, size_t);
extern id object_dispose(id);

__attribute__((objc_root_class))
@interface NSObject {
    void *_isa;
}
+ (id)alloc;
- (id)init;
- (id)retain;
- (oneway void)release;
- (id)autorelease;
- (void)dealloc;
@end

@implementation NSObject
+ (id)alloc { return class_createInstance(self, 0); }
- (id)init { return self; }
- (id)retain { return self; }
- (oneway void)release { }
- (id)autorelease { return self; }
- (void)dealloc { object_dispose(self); }
@end

@class TeguComputeDevice;
@class TeguRenderPipeline;
@class TeguRenderEncoder;

struct tegu_generic_draw_request {
    uint32_t target_format, clear_words[4];
    uint32_t width, height, row_bytes, load_action, clear_color, vertex_count;
    uint64_t target_token, target_offset, target_length;
};

/* Dispatch model: only fields read by nord2_render_pass.inc plus explicit
 * fields used by this test's independent draw/JM model. */
struct Dispatch {
    unsigned nord2ClearOnly;
    id renderTexture;
    struct tegu_generic_draw_request genericDrawRequest;

    unsigned testDraw;
    TeguRenderPipeline *testPipeline;
    TeguRenderEncoder *testEncoder;
    id testTargets[4];
    unsigned testMasks[4];
    unsigned testBytesLen;
    unsigned char testBytes[8];
};

@interface TeguComputeDevice : NSObject @end
@implementation TeguComputeDevice @end

@interface TeguBuffer : NSObject {
@public
    void *bytes;
    U64 logical, handle;
    TeguComputeDevice *owner;
}
@end
@implementation TeguBuffer @end

struct TeguMip { NSUInteger width, height, stride; U64 offset; };

@interface TeguTexture : NSObject {
@public
    TeguComputeDevice *deviceValue;
    TeguBuffer *buffer;
    unsigned textureKind, textureSamples, textureDepth, levelCount, storageMode;
    NSUInteger textureWidth, textureHeight, rowBytes;
    unsigned format, surfaceImported, surfaceTargetVersion;
    unsigned textureUsage, textureResourceOptions;
    U64 surfaceAllocSize, surfaceRowBytes, importedHandle;
    void *surfaceBytes; /* test storage for imported targets */
    struct TeguMip mip[1];
}
- (id)device;
- (NSUInteger)pixelFormat;
- (NSUInteger)width;
- (NSUInteger)height;
- (NSUInteger)storageMode;
- (NSUInteger)usage;
@end
@implementation TeguTexture
- (id)device { return deviceValue; }
- (NSUInteger)pixelFormat { return format; }
- (NSUInteger)width { return textureWidth; }
- (NSUInteger)height { return textureHeight; }
- (NSUInteger)storageMode { return storageMode; }
- (NSUInteger)usage { return textureUsage; }
@end

@interface TeguQueue : NSObject {
@public
    TeguComputeDevice *owner;
}
@end
@implementation TeguQueue @end

@interface TeguCommand : NSObject {
@public
    TeguQueue *queue;
    unsigned count, status, encoding, invalid;
    const char *invalidFunction;
    unsigned invalidLine;
    struct Dispatch jobs[TEST_MAX_JOBS];
}
- (NSUInteger)status;
@end
@implementation TeguCommand
- (NSUInteger)status { return status; }
@end

static struct Dispatch fallback_job;
static struct Dispatch *tegu_command_job(TeguCommand *c, unsigned index) {
    if (!c || index >= TEST_MAX_JOBS) {
        fprintf(stderr, "COREANIMATION_FAIL job index=%u beyond fake capacity\n",
                index);
        return &fallback_job;
    }
    return &c->jobs[index];
}
static void tegu_command_invalidate(TeguCommand *c, const char *fn, unsigned line) {
    if (!c) return;
    c->invalid = 1;
    c->invalidFunction = fn;
    c->invalidLine = line;
}
static int tegu_owned_class(id object, const char *name) {
    (void)name;
    return object != 0;
}

@interface TeguPassAttachment : NSObject {
@public
    id textureValue, resolveTextureValue;
    NSUInteger levelValue, sliceValue, depthPlaneValue;
    NSUInteger resolveLevelValue, resolveSliceValue, resolveDepthPlaneValue;
    NSUInteger loadActionValue, storeActionValue;
    TeguClearColor clearColorValue;
}
- (id)texture;
- (id)resolveTexture;
- (NSUInteger)level;
- (NSUInteger)slice;
- (NSUInteger)depthPlane;
- (NSUInteger)resolveLevel;
- (NSUInteger)resolveSlice;
- (NSUInteger)resolveDepthPlane;
- (NSUInteger)loadAction;
- (NSUInteger)storeAction;
- (TeguClearColor)clearColor;
@end
@implementation TeguPassAttachment
- (id)texture { return textureValue; }
- (id)resolveTexture { return resolveTextureValue; }
- (NSUInteger)level { return levelValue; }
- (NSUInteger)slice { return sliceValue; }
- (NSUInteger)depthPlane { return depthPlaneValue; }
- (NSUInteger)resolveLevel { return resolveLevelValue; }
- (NSUInteger)resolveSlice { return resolveSliceValue; }
- (NSUInteger)resolveDepthPlane { return resolveDepthPlaneValue; }
- (NSUInteger)loadAction { return loadActionValue; }
- (NSUInteger)storeAction { return storeActionValue; }
- (TeguClearColor)clearColor { return clearColorValue; }
@end

@interface TeguPassAttachments : NSObject {
@public
    id slots[8];
}
- (id)objectAtIndexedSubscript:(NSUInteger)index;
@end
@implementation TeguPassAttachments
- (id)objectAtIndexedSubscript:(NSUInteger)index {
    return index < 8 ? slots[index] : 0;
}
@end

@interface TeguPassDescriptor : NSObject {
@public
    TeguPassAttachments *colors;
    TeguPassAttachment *depthAttachmentValue, *stencilAttachmentValue;
    NSUInteger arrayLength, targetWidth, targetHeight, sampleCount;
    id visibilityValue;
}
- (id)colorAttachments;
- (TeguPassAttachment *)depthAttachment;
- (TeguPassAttachment *)stencilAttachment;
- (NSUInteger)renderTargetArrayLength;
- (NSUInteger)renderTargetWidth;
- (NSUInteger)renderTargetHeight;
- (NSUInteger)defaultRasterSampleCount;
- (id)visibilityResultBuffer;
@end
@implementation TeguPassDescriptor
- (id)colorAttachments { return colors; }
- (TeguPassAttachment *)depthAttachment { return depthAttachmentValue; }
- (TeguPassAttachment *)stencilAttachment { return stencilAttachmentValue; }
- (NSUInteger)renderTargetArrayLength { return arrayLength; }
- (NSUInteger)renderTargetWidth { return targetWidth; }
- (NSUInteger)renderTargetHeight { return targetHeight; }
- (NSUInteger)defaultRasterSampleCount { return sampleCount; }
- (id)visibilityResultBuffer { return visibilityValue; }
@end

/* Independent descriptor model for the CoreAnimation blur/MRT shape. */
@interface TeguRenderPipeline : NSObject {
@public
    unsigned attachmentCount;
    unsigned formats[4], masks[4];
}
@end
@implementation TeguRenderPipeline @end

@interface TeguRenderEncoder : NSObject {
@public
    TeguCommand *command;
    TeguTexture *target;
    TeguRenderPipeline *pipeline;
    unsigned inert, skipPass, ended, storeAction;
    unsigned renderTargetsValid;
    TeguTexture *renderTargets[4], *resolveTargets[4];
    unsigned rasterSampleCount;
    unsigned renderLoads[4], renderJobs[4], rt0Jobs;
    unsigned mrtDraws, mrtDraws2, mrtDraws3;
    uint32_t renderClears[4][4], nord2Clear[4][4];
    TeguTexture *mrtTarget, *mrtTarget2, *mrtTarget3;
    unsigned mrtLoad, mrtLoad2, mrtLoad3;
    uint32_t mrtClear, mrtClear2, mrtClear3;
    unsigned hasDepthAttachment, hasStencilAttachment;
    NSUInteger passDepthFormat, passStencilFormat;
    struct { NSUInteger x, y, width, height; } scissor;
}
- (id)initWithCommand:(TeguCommand *)c target:(TeguTexture *)t
                 clear:(uint32_t)value loadAction:(unsigned)action;
- (void)setRenderPipelineState:(TeguRenderPipeline *)p;
- (void)setColorStoreAction:(NSUInteger)action atIndex:(NSUInteger)index;
- (void)endEncoding;
@end
@implementation TeguRenderEncoder
- (id)initWithCommand:(TeguCommand *)c target:(TeguTexture *)t
                 clear:(uint32_t)value loadAction:(unsigned)action {
    (void)value; (void)action;
    command = c; target = t; scissor.width = 4096; scissor.height = 4096;
    return self;
}
- (void)setRenderPipelineState:(TeguRenderPipeline *)p { pipeline = p; }
- (void)setColorStoreAction:(NSUInteger)action atIndex:(NSUInteger)index {
    /* Mirrors the externally visible deferred-store contract: the pass always
     * stores, so a later Store/DontCare needs no state change. */
    if (ended) return;
    if (index > 3 || action > 1) tegu_command_invalidate(command, "test", __LINE__);
}
- (void)endEncoding { ended = 1; if (command) command->encoding = 0; }
@end

/* ------------------------------------------------------------------ */
/* Independent format oracle: format 80 BGRA8 and format 115 RGBA16F. */
static unsigned n2r_bpp(unsigned format) {
    if (format == 80) return 4;
    if (format == 115) return 8;
    return 0;
}
/* Both oracle formats are blendable, so a LOAD preload restores them exactly. */
static unsigned n2r_rt_load_exact(unsigned format) { return n2r_bpp(format) != 0; }
static uint16_t half_from_unit(double v) {
    if (v <= 0.0) return 0x0000;
    if (v >= 1.0) return 0x3c00;
    int exp = 0;
    double m = v;
    /* Normalize to m in [1,2), as required by the IEEE-754 half mantissa. */
    while (m < 1.0) { m *= 2.0; exp--; }
    while (m >= 2.0) { m *= 0.5; exp++; }
    unsigned he = (unsigned)(exp + 15);
    unsigned mant = (unsigned)((m - 1.0) * 1024.0 + 0.5);
    return (uint16_t)((he << 10) | (mant & 0x3ffu));
}
static int n2r_clear(unsigned format, const double rgba[4], uint32_t out[4]) {
    if (!rgba || !out || !n2r_bpp(format)) return -1;
    if (format == 80) {
        uint32_t packed = 0;
        for (unsigned i = 0; i < 4; i++) {
            double v = rgba[i];
            if (v != v) return -1;
            if (v < 0) v = 0;
            if (v > 1) v = 1;
            packed |= (uint32_t)(v * 255.0 + 0.5) << (8 * i);
        }
        out[0] = out[1] = out[2] = out[3] = packed;
        return 0;
    }
    uint16_t h[4];
    for (unsigned i = 0; i < 4; i++) {
        if (rgba[i] != rgba[i]) return -1;
        if (rgba[i] < 0) return -1;
        if (rgba[i] > 1) return -1;
        h[i] = half_from_unit(rgba[i]);
    }
    out[0] = (uint32_t)h[0] | ((uint32_t)h[1] << 16);
    out[1] = (uint32_t)h[2] | ((uint32_t)h[3] << 16);
    out[2] = out[3] = 0;
    return 0;
}

extern long write(int, const void *, size_t);
#define TEGU_INVALID(c) tegu_command_invalidate((c), "nord2_render_pass", 0)
#include "nord2_render_pass.inc"

/* ------------------------------------------------------------------ */
static unsigned checks, failures;
static void check(int ok, const char *what) {
    checks++;
    if (!ok) {
        fprintf(stderr, "FAIL [required] %s\n", what);
        failures++;
    }
}
static unsigned long long align4096(unsigned long long n) {
    return (n + 4095ull) & ~4095ull;
}
static TeguCommand *new_command(void) {
    TeguQueue *q = [[TeguQueue alloc] init];
    q->owner = [[TeguComputeDevice alloc] init];
    TeguCommand *c = [[TeguCommand alloc] init];
    c->queue = q;
    return c;
}
static TeguTexture *new_target(TeguComputeDevice *owner, NSUInteger w, NSUInteger h,
                               unsigned format, NSUInteger stride,
                               int imported, unsigned usage, unsigned char fill) {
    TeguTexture *t = [[TeguTexture alloc] init];
    t->deviceValue = owner;
    t->textureKind = 2;
    t->textureSamples = 1;
    t->textureDepth = 1;
    t->levelCount = 1;
    t->textureWidth = w;
    t->textureHeight = h;
    t->rowBytes = stride;
    t->format = format;
    t->textureUsage = usage;
    t->textureResourceOptions = imported ? 0x20u : 0;
    t->storageMode = 2;
    t->mip[0].width = w;
    t->mip[0].height = h;
    t->mip[0].stride = stride;
    t->mip[0].offset = 0;
    if (imported) {
        t->surfaceImported = 1;
        t->surfaceTargetVersion = 1;
        t->importedHandle = 0xa0000001u + (U64)(w + h + format);
        t->surfaceRowBytes = stride;
        t->surfaceAllocSize = align4096((unsigned long long)stride * h);
        t->surfaceBytes = calloc(1, (size_t)t->surfaceAllocSize);
        if (t->surfaceBytes) memset(t->surfaceBytes, fill, (size_t)t->surfaceAllocSize);
    } else {
        TeguBuffer *b = [[TeguBuffer alloc] init];
        b->owner = owner;
        b->logical = (U64)stride * h;
        b->handle = 0xb0000001u + (U64)(w + h + format);
        b->bytes = calloc(1, (size_t)b->logical + 256);
        if (b->bytes) memset(b->bytes, fill, (size_t)b->logical + 256);
        t->buffer = b;
    }
    return t;
}
static unsigned char *target_bytes(TeguTexture *t) {
    if (!t) return 0;
    return t->surfaceImported ? (unsigned char *)t->surfaceBytes
                              : (t->buffer ? (unsigned char *)t->buffer->bytes : 0);
}
static void expected_clear_pixel(unsigned format, const uint32_t words[4],
                                 unsigned char out[8]) {
    memset(out, 0, 8);
    if (format == 80) {
        uint32_t c = words[0];
        out[0] = (unsigned char)(c >> 16);
        out[1] = (unsigned char)(c >> 8);
        out[2] = (unsigned char)c;
        out[3] = (unsigned char)(c >> 24);
    } else if (format == 115) {
        memcpy(out, words, 8);
    }
}
static void check_pixels(TeguTexture *t, const unsigned char *pixel,
                         unsigned bpp, const char *label) {
    unsigned char *bytes = target_bytes(t);
    char what[240];
    if (!t || !bytes || !bpp) {
        snprintf(what, sizeof(what), "%s target storage", label);
        check(0, what);
        return;
    }
    NSUInteger xs[3] = {0, t->textureWidth / 2, t->textureWidth - 1};
    NSUInteger ys[4] = {0, 1, t->textureHeight / 2, t->textureHeight - 1};
    int ok = 1;
    for (unsigned yi = 0; yi < 4; yi++) {
        for (unsigned xi = 0; xi < 3; xi++) {
            NSUInteger off = t->mip[0].offset + ys[yi] * t->rowBytes +
                             xs[xi] * bpp;
            if (memcmp(bytes + off, pixel, bpp) != 0) ok = 0;
        }
    }
    snprintf(what, sizeof(what), "%s bytes at row-stride %lu", label,
             (unsigned long)t->rowBytes);
    check(ok, what);
    if (t->surfaceImported) {
        NSUInteger tail = t->mip[0].offset + t->rowBytes * t->textureHeight;
        snprintf(what, sizeof(what), "%s does not write IOSurface padding", label);
        check(tail < t->surfaceAllocSize && bytes[tail] == TEST_SENTINEL, what);
    } else if (t->buffer) {
        snprintf(what, sizeof(what), "%s does not write past target logical span", label);
        check(bytes[t->buffer->logical] == TEST_SENTINEL, what);
    }
}
static void check_unchanged(TeguTexture *t, const unsigned char *before,
                            size_t n, const char *label) {
    unsigned char *bytes = target_bytes(t);
    char what[220];
    snprintf(what, sizeof(what), "%s untouched by slot1 mask0 draw", label);
    check(bytes && before && memcmp(bytes, before, n) == 0, what);
}

/* Fake JM: execute clear jobs then the test draw, status 4, and write target
 * bytes using the independent format oracle above. */
static int fake_jm_execute(TeguCommand *c) {
    unsigned i;
    for (i = 0; i < c->count; i++) {
        struct Dispatch *j = &c->jobs[i];
        if (j->nord2ClearOnly) {
            TeguTexture *t = (TeguTexture *)j->renderTexture;
            struct tegu_generic_draw_request *q = &j->genericDrawRequest;
            unsigned bpp = n2r_bpp(q->target_format);
            unsigned char pixel[8];
            unsigned char *bytes = target_bytes(t);
            if (!t || !bytes || !bpp || q->row_bytes != t->rowBytes ||
                q->width != t->textureWidth || q->height != t->textureHeight) {
                return 5;
            }
            expected_clear_pixel(q->target_format, q->clear_words, pixel);
            for (NSUInteger y = 0; y < t->textureHeight; y++) {
                unsigned char *row = bytes + q->target_offset + y * q->row_bytes;
                for (NSUInteger x = 0; x < t->textureWidth; x++)
                    memcpy(row + x * bpp, pixel, bpp);
            }
        }
        if (j->testDraw) {
            unsigned bpp = n2r_bpp(j->testPipeline->formats[0]);
            for (unsigned rt = 0; rt < j->testPipeline->attachmentCount && rt < 4; rt++) {
                if (!j->testMasks[rt]) continue;
                TeguTexture *t = (TeguTexture *)j->testTargets[rt];
                unsigned char *bytes = target_bytes(t);
                if (!t || !bytes || !bpp || j->testBytesLen != bpp) return 5;
                for (NSUInteger y = 0; y < t->textureHeight; y++) {
                    unsigned char *row = bytes + t->mip[0].offset + y * t->rowBytes;
                    for (NSUInteger x = 0; x < t->textureWidth; x++)
                        memcpy(row + x * bpp, j->testBytes, bpp);
                }
            }
        }
    }
    c->status = 4;
    return 4;
}
static void add_test_draw(TeguRenderEncoder *e, TeguRenderPipeline *p,
                          const unsigned char *bytes, unsigned nbytes,
                          const unsigned masks[2], const char *label) {
    TeguCommand *c = e ? e->command : 0;
    char what[240];
    snprintf(what, sizeof(what), "%s pipeline has a supported attachment count", label);
    check(p && (p->attachmentCount == 1 || p->attachmentCount == 2), what);
    if (p && p->attachmentCount == 2) {
        snprintf(what, sizeof(what), "%s pipeline slot1 write mask is zero", label);
        check(p->masks[1] == 0, what);
    }
    snprintf(what, sizeof(what), "%s draw has room for a job", label);
    check(c && c->count < TEST_MAX_JOBS, what);
    if (!e || !c || c->count >= TEST_MAX_JOBS) return;
    [e setRenderPipelineState:p];
    struct Dispatch *j = &c->jobs[c->count++];
    memset(j, 0, sizeof(*j));
    j->testDraw = 1;
    j->testPipeline = p;
    j->testEncoder = e;
    j->testTargets[0] = e->renderTargets[0];
    j->testTargets[1] = e->renderTargets[1];
    j->testMasks[0] = masks[0];
    j->testMasks[1] = masks[1];
    j->testBytesLen = nbytes > sizeof(j->testBytes) ? sizeof(j->testBytes) : nbytes;
    memcpy(j->testBytes, bytes, j->testBytesLen);
}
static TeguPassAttachment *new_color(TeguTexture *t, NSUInteger load,
                                     NSUInteger store) {
    TeguPassAttachment *a = [[TeguPassAttachment alloc] init];
    a->textureValue = t;
    a->loadActionValue = load;
    a->storeActionValue = store;
    a->clearColorValue = (TeguClearColor){0.25, 0.5, 0.75, 1.0};
    return a;
}
static TeguPassDescriptor *new_pass(TeguPassAttachment *a0,
                                    TeguPassAttachment *a1) {
    TeguPassDescriptor *d = [[TeguPassDescriptor alloc] init];
    TeguPassAttachments *colors = [[TeguPassAttachments alloc] init];
    colors->slots[0] = a0;
    if (a1) colors->slots[1] = a1;
    d->colors = colors;
    d->arrayLength = 1;
    d->sampleCount = 1;
    return d;
}

/* Execute a pass with a single attachment. */
static void run_single(const char *name, NSUInteger w, NSUInteger h,
                       unsigned format, int imported, NSUInteger stride,
                       NSUInteger load, NSUInteger store,
                       int add_draw, int expect_clear_job) {
    TeguCommand *c = new_command();
    TeguComputeDevice *owner = c->queue->owner;
    TeguTexture *t = new_target(owner, w, h, format, stride, imported, 5, TEST_SENTINEL);
    TeguPassAttachment *a = new_color(t, load, store);
    TeguPassDescriptor *d = new_pass(a, 0);
    unsigned before = c->count;
    TeguRenderEncoder *e = nord2_render_pass(c, d);
    char label[240];
    snprintf(label, sizeof(label), "%s accepted", name);
    check(e != 0 && !e->inert, label);
    snprintf(label, sizeof(label), "%s command not poisoned", name);
    check(!c->invalid, label);
    if (e && !e->inert) {
        [e setColorStoreAction:1 atIndex:0];
        snprintf(label, sizeof(label), "%s deferred store accepted", name);
        check(!c->invalid, label);
        if (expect_clear_job) {
            snprintf(label, sizeof(label), "%s queued a clear job", name);
            check(c->count > before, label);
        } else {
            snprintf(label, sizeof(label), "%s Load queued no clear job", name);
            check(c->count == before, label);
        }
        if (add_draw) {
            if (load == 1) {
                unsigned char sentinel[8];
                memset(sentinel, TEST_SENTINEL, sizeof(sentinel));
                check_pixels(t, sentinel, n2r_bpp(format), name);
            }
            TeguRenderPipeline *p = [[TeguRenderPipeline alloc] init];
            p->attachmentCount = 1;
            p->formats[0] = format;
            p->masks[0] = 15;
            unsigned char draw[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
            unsigned masks[2] = {15, 0};
            add_test_draw(e, p, draw, n2r_bpp(format), masks, name);
            check(fake_jm_execute(c) == 4, "fake JM completed");
            check_pixels(t, draw, n2r_bpp(format), name);
        } else if (expect_clear_job) {
            uint32_t words[4] = {0, 0, 0, 0};
            unsigned char pixel[8];
            /* DontCare (0) is defined as clear-to-zero by the production pass;
             * Clear (2) uses the attachment's clearColor. */
            double rgba[4] = {load == 0 ? 0.0 : 0.25,
                              load == 0 ? 0.0 : 0.5,
                              load == 0 ? 0.0 : 0.75,
                              load == 0 ? 0.0 : 1.0};
            if (n2r_clear(format, rgba, words) == 0) {
                expected_clear_pixel(format, words, pixel);
                check(fake_jm_execute(c) == 4, "fake JM completed");
                check_pixels(t, pixel, n2r_bpp(format), name);
            } else {
                check(0, "test clear oracle supports format");
            }
        } else {
            /* Load with no draw: existing bytes must remain and no job runs. */
            check(fake_jm_execute(c) == 4, "fake JM completed");
            unsigned char sentinel[8];
            memset(sentinel, TEST_SENTINEL, sizeof(sentinel));
            check_pixels(t, sentinel, n2r_bpp(format), name);
        }
        [e endEncoding];
    }
}

/* Execute a two-attachment pass with slot1 write mask 0. */
static void run_pair(const char *name, NSUInteger w, NSUInteger h,
                     unsigned f0, unsigned f1, NSUInteger stride0,
                     NSUInteger stride1, NSUInteger load0, NSUInteger load1,
                     int imported1) {
    TeguCommand *c = new_command();
    TeguComputeDevice *owner = c->queue->owner;
    TeguTexture *t0 = new_target(owner, w, h, f0, stride0, 0, 5, TEST_SENTINEL);
    TeguTexture *t1 = new_target(owner, w, h, f1, stride1, imported1, 5, TEST_SENTINEL);
    TeguPassAttachment *a0 = new_color(t0, load0, 4);
    TeguPassAttachment *a1 = new_color(t1, load1, 4);
    TeguPassDescriptor *d = new_pass(a0, a1);
    TeguRenderEncoder *e = nord2_render_pass(c, d);
    char label[240];
    snprintf(label, sizeof(label), "%s accepted", name);
    check(e != 0 && !e->inert, label);
    snprintf(label, sizeof(label), "%s two targets retained", name);
    check(e && e->renderTargets[0] == t0 && e->renderTargets[1] == t1, label);
    snprintf(label, sizeof(label), "%s command not poisoned", name);
    check(!c->invalid, label);
    if (e && !e->inert) {
        [e setColorStoreAction:1 atIndex:0];
        [e setColorStoreAction:1 atIndex:1];
        snprintf(label, sizeof(label), "%s deferred store accepted", name);
        check(!c->invalid, label);
        size_t t1bytes = (size_t)t1->rowBytes * t1->textureHeight;
        unsigned char *t1before = malloc(t1bytes);
        if (t1before) memcpy(t1before, target_bytes(t1), t1bytes);
        TeguRenderPipeline *p = [[TeguRenderPipeline alloc] init];
        p->attachmentCount = 2;
        p->formats[0] = f0;
        p->formats[1] = f1;
        p->masks[0] = 15;
        p->masks[1] = 0;
        unsigned char draw[8] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22};
        unsigned masks[2] = {15, 0};
        add_test_draw(e, p, draw, n2r_bpp(f0), masks, name);
        unsigned before = c->count;
        check(fake_jm_execute(c) == 4, "fake JM completed");
        char what[240];
        snprintf(what, sizeof(what), "%s clear/draw jobs submitted", name);
        check(c->count >= before, what);
        check_pixels(t0, draw, n2r_bpp(f0), name);
        check_unchanged(t1, t1before, t1bytes, name);
        free(t1before);
        [e endEncoding];
    }
}

int main(void) {
    /* Pin the independent format oracle before using it as the byte judge. */
    check(half_from_unit(0.25) == 0x3400 && half_from_unit(0.5) == 0x3800 &&
          half_from_unit(0.75) == 0x3a00 && half_from_unit(1.0) == 0x3c00,
          "RGBA16F half oracle values");
    {
        uint32_t w80[4] = {0}, w115[4] = {0};
        double rgba[4] = {0.25, 0.5, 0.75, 1.0};
        check(n2r_clear(80, rgba, w80) == 0 && w80[0] == 0xffbf8040u,
              "BGRA8 clear-word oracle");
        check(n2r_clear(115, rgba, w115) == 0 && w115[0] == 0x38003400u &&
              w115[1] == 0x3c003a00u, "RGBA16F clear-word oracle");
    }

    /* Two display-sized BGRA8 shapes with natural, non-64-aligned strides.
     * 1080*4=4352 and 1170*4=4736. */
    run_single("CA 1080x2400 BGRA8 Clear/Unknown drawable", 1080, 2400,
               80, 0, 4352, 2, 4, 1, 1);
    run_single("CA 1170x2532 BGRA8 Load/Unknown drawable", 1170, 2532,
               80, 0, 4736, 1, 4, 1, 0);
    run_single("CA 1170x2532 BGRA8 IOSurface Clear/Unknown", 1170, 2532,
               80, 1, 4736, 2, 4, 0, 1);
    run_single("CA 1080x2400 BGRA8 DontCare/Unknown", 1080, 2400,
               80, 0, 4352, 0, 4, 0, 1);
    run_single("CA 540x1200 RGBA16F Clear/Unknown", 540, 1200,
               115, 0, 4352, 2, 4, 0, 1);
    run_single("CA 540x1200 RGBA16F Load/Unknown draw", 540, 1200,
               115, 0, 4352, 1, 4, 1, 0);
    run_single("CA aligned BGRA8 Unknown-store", 1088, 2400,
               80, 0, 4352, 2, 4, 1, 1);

    /* Two-attachment CoreAnimation compositor/blur shapes. */
    run_pair("CA pair 80/80 slot1-mask0 Clear/Load", 1080, 2400,
             80, 80, 4352, 4352, 2, 1, 0);
    run_pair("CA pair 80/115 slot1-mask0 Clear/Load", 540, 1200,
             80, 115, 4352, 4352, 2, 1, 0);
    run_pair("CA pair 80/80 IOSurface-slot1 Clear/Load", 1170, 2532,
             80, 80, 4736, 4736, 2, 1, 1);

    printf("COREANIMATION_PASS checks=%u failures=%u pipeline_builder=not-exercised "
           "jm_status=4\n", checks, failures);
    return failures ? 1 : 0;
}
