/* Independent C3 inert render-pass encoder safety test.
 *
 * This compiles the real gpu/metal/nord2_render_pass.inc against a minimal
 * fake Objective-C frontend.  It verifies the externally visible C3 contract:
 * a refused pass still returns a non-nil encoder, marks it inert/skip/ended,
 * poisons the command, and keeps diagnostic output bounded.  It deliberately
 * does not use render's host harness or its MRT/pipeline test objects.
 *
 * No GPU execution; no production edits.  The fake n2r_clear/n2r_bpp below
 * exist only to reach production admission checks and are not blanket claims.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long NSUInteger;
typedef unsigned long long U64;
typedef struct { double red, green, blue, alpha; } TeguClearColor;

#define TEGU_COMMAND_MAX_JOBS 1024u
#define TEGU_JOB_CHUNK 8u

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
@class TeguTexture;
@class TeguBuffer;
@class TeguQueue;
@class TeguRenderEncoder;

struct tegu_generic_draw_request {
    uint32_t target_format, clear_words[4];
    uint32_t width, height, row_bytes, load_action, clear_color, vertex_count;
    uint64_t target_token, target_offset, target_length;
};

struct Dispatch {
    unsigned nord2ClearOnly;
    id renderTexture;
    struct tegu_generic_draw_request genericDrawRequest;
};

@interface TeguComputeDevice : NSObject @end
@implementation TeguComputeDevice @end

@interface TeguBuffer : NSObject {
@public
    U64 handle, logical;
}
@end
@implementation TeguBuffer @end

struct TeguMip { U64 offset; };

@interface TeguTexture : NSObject {
@public
    TeguComputeDevice *device;
    TeguBuffer *buffer;
    unsigned textureKind, textureSamples, textureDepth, levelCount;
    NSUInteger textureWidth, textureHeight, rowBytes;
    unsigned format, surfaceImported, surfaceTargetVersion;
    U64 surfaceAllocSize, importedHandle;
    struct TeguMip mip[1];
}
- (id)device;
@end
@implementation TeguTexture
- (id)device { return device; }
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
}
@end
@implementation TeguCommand @end

static struct Dispatch fake_dispatch;
static struct Dispatch *tegu_command_job(TeguCommand *c, unsigned index) {
    (void)c; (void)index;
    return &fake_dispatch;
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

@interface TeguRenderEncoder : NSObject {
@public
    TeguCommand *command;
    TeguTexture *target;
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
- (void)endEncoding;
@end
@implementation TeguRenderEncoder
- (id)initWithCommand:(TeguCommand *)c target:(TeguTexture *)t
                 clear:(uint32_t)value loadAction:(unsigned)action {
    (void)value; (void)action;
    command = c; target = t; scissor.width = 4096; scissor.height = 4096;
    return self;
}
- (void)endEncoding { ended = 1; }
@end

/* Production admission code under test. */
static int n2r_clear(unsigned format, const double rgba[4], uint32_t out[4]);
static unsigned n2r_bpp(unsigned format) {
    return format == 10 ? 1 : (format == 115 ? 8 : (format == 70 || format == 80 ? 4 : 0));
}
/* nord2_metal_render.h: every format this stub admits (10/70/80/115) is a
 * float/unorm target with an exact LOAD round trip. */
static unsigned n2r_rt_load_exact(unsigned format) {
    return n2r_bpp(format) != 0;
}
static int n2r_clear(unsigned format, const double rgba[4], uint32_t out[4]) {
    (void)rgba;
    if (!n2r_bpp(format)) return -1;
    out[0] = out[1] = out[2] = out[3] = 0x01020304u;
    return 0;
}
extern long write(int, const void *, size_t);
#define TEGU_INVALID(c) tegu_command_invalidate((c), "nord2_render_pass", 0)
#include "nord2_render_pass.inc"

static unsigned failures, checks;
static void check(int ok, const char *what) {
    checks++;
    if (!ok) { fprintf(stderr, "FAIL [required] %s\n", what); failures++; }
}
static TeguCommand *new_command(void) {
    TeguQueue *q = [[TeguQueue alloc] init];
    q->owner = [[TeguComputeDevice alloc] init];
    TeguCommand *c = [[TeguCommand alloc] init];
    c->queue = q;
    return c;
}
static TeguTexture *new_texture(TeguComputeDevice *d) {
    TeguBuffer *b = [[TeguBuffer alloc] init];
    b->logical = 4096;
    TeguTexture *t = [[TeguTexture alloc] init];
    t->device = d; t->buffer = b; t->textureKind = 2; t->textureSamples = 1;
    t->textureDepth = 1; t->levelCount = 1; t->textureWidth = 16;
    t->textureHeight = 16; t->rowBytes = 64; t->format = 80;
    return t;
}
static TeguPassAttachment *new_color(TeguTexture *t, unsigned load) {
    TeguPassAttachment *a = [[TeguPassAttachment alloc] init];
    a->textureValue = t; a->loadActionValue = load;
    a->clearColorValue = (TeguClearColor){0, 0, 0, 1};
    return a;
}
static void assert_inert(TeguCommand *c, TeguPassDescriptor *d, const char *what) {
    TeguRenderEncoder *e = nord2_render_pass(c, d);
    check(e != 0 && e->inert && e->skipPass && e->ended, what);
    check(c->invalid && c->invalidFunction && !strcmp(c->invalidFunction, "nord2_render_pass"),
          "refused pass poisons the command");
    if (e) [e endEncoding];
}

int main(void) {
    TeguCommand *c = new_command();
    TeguComputeDevice *owner = c->queue->owner;

    /* Missing attachments, sample count, resolve, >4 attachments, and a
     * failed attachment class all return a usable inert encoder. */
    TeguPassDescriptor *d = [[TeguPassDescriptor alloc] init];
    assert_inert(c, d, "missing color attachments returns inert encoder");

    c = new_command(); owner = c->queue->owner;
    d = [[TeguPassDescriptor alloc] init];
    TeguTexture *t = new_texture(owner);
    TeguPassAttachments *colors = [[TeguPassAttachments alloc] init];
    colors->slots[0] = new_color(t, 2);
    d->colors = colors; d->sampleCount = 4;
    assert_inert(c, d, "multisample pass returns inert encoder");

    c = new_command(); owner = c->queue->owner;
    d = [[TeguPassDescriptor alloc] init];
    t = new_texture(owner);
    colors = [[TeguPassAttachments alloc] init];
    TeguPassAttachment *a = new_color(t, 2);
    a->resolveTextureValue = t;
    colors->slots[0] = a;
    d->colors = colors;
    assert_inert(c, d, "resolve pass returns inert encoder");

    c = new_command(); owner = c->queue->owner;
    d = [[TeguPassDescriptor alloc] init];
    colors = [[TeguPassAttachments alloc] init];
    for (unsigned i = 0; i < 5; i++) {
        TeguTexture *ti = new_texture(owner);
        colors->slots[i] = new_color(ti, 2);
    }
    d->colors = colors;
    assert_inert(c, d, "more than four color attachments returns inert encoder");

    /* The same texture in two color slots would silently write/clear it twice;
     * the C3 audit requires an explicit refusal, not a double clear. */
    c = new_command(); owner = c->queue->owner;
    d = [[TeguPassDescriptor alloc] init];
    colors = [[TeguPassAttachments alloc] init];
    t = new_texture(owner);
    colors->slots[0] = new_color(t, 2);
    colors->slots[1] = new_color(t, 2);
    d->colors = colors;
    assert_inert(c, d, "same texture in two color slots returns inert encoder");

    c = new_command(); owner = c->queue->owner;
    d = [[TeguPassDescriptor alloc] init];
    colors = [[TeguPassAttachments alloc] init];
    TeguTexture *bad = new_texture(owner);
    bad->format = 99; /* n2r_bpp refuses this without a format substitution. */
    colors->slots[0] = new_color(bad, 2);
    d->colors = colors;
    assert_inert(c, d, "unsupported color format returns inert encoder");

    /* The diagnostic cap is first-16-per-process; 20 more refusals must not
     * produce more than the bounded number of stderr records. */
    for (unsigned i = 0; i < 20; i++) {
        c = new_command();
        d = [[TeguPassDescriptor alloc] init];
        d->sampleCount = 4;
        TeguPassDescriptor *empty = d;
        TeguTexture *ti = new_texture(c->queue->owner);
        TeguPassAttachments *cc = [[TeguPassAttachments alloc] init];
        cc->slots[0] = new_color(ti, 2);
        empty->colors = cc;
        assert_inert(c, empty, "bounded inert pass iteration");
    }

    printf("inert render pass: %u checks, %u required failures\n", checks, failures);
    return failures ? 1 : 0;
}
