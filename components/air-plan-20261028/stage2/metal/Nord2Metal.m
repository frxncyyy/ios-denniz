/*
 * Nord 2 private Metal compute frontend (source; host syntax-checked only).
 *
 * This is a thin Objective-C shim over the tested transport-independent core
 * in nord2_metal.c.  It is compute-only and refuses render/texture/IOSurface
 * claims.  It is not an iOS bundle and has not been run through
 * Metal.framework or on the phone.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "nord2_metal.h"

typedef unsigned long NSUInteger;
typedef unsigned long long U64;
typedef struct { NSUInteger width, height, depth; } MTLSize;

@interface NSObject {
@public
        unsigned _references;
}
+ (id)alloc;
- (id)init;
- (id)retain;
- (oneway void)release;
- (id)autorelease;
- (void)dealloc;
@end

@interface NSString : NSObject
@end
@interface NSDictionary : NSObject
@end
@interface NSError : NSObject
+ (id)errorWithDomain:(id)domain code:(long)code userInfo:(id)info;
@end

/* Metal.framework supplies this class; no Apple accelerator ABI is claimed. */
@interface _MTLDevice : NSObject
@end

@class Nord2MetalDevice, Nord2MetalBuffer, Nord2MetalPipeline,
        Nord2MetalCommandQueue, Nord2MetalCommandBuffer,
        Nord2MetalComputeCommandEncoder;

@interface Nord2MetalDevice : _MTLDevice {
@public
        struct n2m_device core;
        unsigned connection, task;
        unsigned (*scalar)(unsigned, unsigned, const U64 *, unsigned, U64 *,
                           unsigned *);
        int (*method)(unsigned, unsigned, const U64 *, unsigned, const void *,
                      NSUInteger, U64 *, unsigned *, void *, unsigned *);
        int (*map)(unsigned, unsigned, unsigned, U64 *, U64 *, unsigned);
        int (*unmap)(unsigned, unsigned, unsigned, U64);
        void (*closeService)(unsigned);
}
- (id)initWithAcceleratorPort:(unsigned)port;
- (id)newBufferWithLength:(NSUInteger)length options:(NSUInteger)options;
- (id)newBufferInternal:(NSUInteger)length executable:(int)executable;
- (id)newBufferWithBytes:(const void *)bytes length:(NSUInteger)length
                 options:(NSUInteger)options;
- (id)newCommandQueue;
- (id)newComputePipelineStateWithBinary:(const void *)binary
                                 length:(NSUInteger)length
                                  error:(id *)error;
- (NSUInteger)maxBufferLength;
- (MTLSize)maxThreadsPerThreadgroup;
- (id)name;
- (signed char)hasUnifiedMemory;
- (signed char)isHeadless;
- (signed char)supportsFamily:(long long)family;
- (NSUInteger)currentAllocatedSize;
@end

@interface Nord2MetalBuffer : NSObject {
@public
        Nord2MetalDevice *owner;
        struct n2m_buffer *buffer;
}
- (void *)contents;
- (NSUInteger)length;
- (id)device;
@end

@interface Nord2MetalPipeline : NSObject {
@public
        Nord2MetalDevice *owner;
        struct n2m_pipeline *pipeline;
        Nord2MetalBuffer *binary;
}
- (id)device;
@end

@interface Nord2MetalCommandQueue : NSObject {
@public
        Nord2MetalDevice *owner;
}
- (id)commandBuffer;
- (id)device;
@end

@interface Nord2MetalCommandBuffer : NSObject {
@public
        Nord2MetalDevice *owner;
        struct n2m_command *command;
        int submitted, finished, failed;
        void (^completionBlock)(id);
}
- (id)computeCommandEncoder;
- (void)commit;
- (void)waitUntilCompleted;
- (signed char)status;
- (id)error;
- (void)setLabel:(id)label;
@end

@interface Nord2MetalComputeCommandEncoder : NSObject {
@public
        Nord2MetalCommandBuffer *commandBuffer;
        struct n2m_encoder *encoder;
        Nord2MetalPipeline *pipeline;
}
- (void)setComputePipelineState:(id)state;
- (void)setBuffer:(id)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index;
- (void)dispatchThreadgroups:(MTLSize)groups
        threadsPerThreadgroup:(MTLSize)threads;
- (void)endEncoding;
@end

#ifdef __linux__
#include <dlfcn.h>
#else
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
#endif
extern id class_createInstance(id, size_t);
extern id object_dispose(id);
extern unsigned mach_task_self(void);
extern void *calloc(NSUInteger, NSUInteger);
extern void free(void *);
extern void *memcpy(void *, const void *, NSUInteger);

#ifdef NORD2_METAL_HOST
@implementation _MTLDevice @end
#endif

@implementation NSObject
+ (id)alloc { return class_createInstance(self, 0); }
- (id)init { return self; }
- (id)retain { _references++; return self; }
- (oneway void)release { if (!--_references) [self dealloc]; }
- (id)autorelease { return self; }
- (void)dealloc { object_dispose(self); }
@end
@implementation NSString @end
@implementation NSDictionary @end
@implementation NSError
+ (id)errorWithDomain:(id)domain code:(long)code userInfo:(id)info
{
        (void)domain; (void)code; (void)info;
        return [[[self alloc] init] autorelease];
}
@end

/* ---- n2m_ops adapters -------------------------------------------------- */
static unsigned m_scalar(void *ctx, unsigned sel, const uint64_t *args,
                         unsigned argc, uint64_t *out, unsigned *n)
{
        Nord2MetalDevice *d = (Nord2MetalDevice *)ctx;
        return d->scalar(d->connection, sel, (const U64 *)args, argc,
                         (U64 *)out, n);
}
static int m_method(void *ctx, unsigned sel, const uint64_t *args,
                    unsigned argc, const void *in, size_t insz, uint64_t *out,
                    unsigned *n, void *ostruct, unsigned *osize)
{
        Nord2MetalDevice *d = (Nord2MetalDevice *)ctx;
        return d->method(d->connection, sel, (const U64 *)args, argc, in, insz,
                         (U64 *)out, n, ostruct, osize);
}
static int m_map(void *ctx, unsigned handle, uint64_t *addr, uint64_t *size)
{
        Nord2MetalDevice *d = (Nord2MetalDevice *)ctx;
        return d->map(d->connection, handle, d->task, (U64 *)addr,
                      (U64 *)size, 1);
}
static int m_unmap(void *ctx, unsigned handle, uint64_t addr)
{
        Nord2MetalDevice *d = (Nord2MetalDevice *)ctx;
        return d->unmap(d->connection, handle, d->task, addr);
}

@implementation Nord2MetalBuffer
- (void *)contents { return (void *)(NSUInteger)(buffer ? buffer->address : 0); }
- (NSUInteger)length { return buffer ? (NSUInteger)buffer->length : 0; }
- (id)device { return owner; }
- (void)dealloc
{
        if (owner && buffer) {
                n2m_buffer_free(&owner->core, buffer);
                free(buffer);
        }
        [owner release];
        [super dealloc];
}
@end

@implementation Nord2MetalPipeline
- (id)device { return owner; }
- (void)dealloc
{
        if (pipeline) {
                n2m_pipeline_free(pipeline);
                free(pipeline);
        }
        [binary release];
        [owner release];
        [super dealloc];
}
@end

@implementation Nord2MetalCommandQueue
- (id)commandBuffer
{
        Nord2MetalCommandBuffer *c = [[Nord2MetalCommandBuffer alloc] init];
        c->owner = [owner retain];
        c->command = calloc(1, sizeof(*c->command));
        if (!c->command || !n2m_command_begin(&owner->core, c->command)) {
                [c release];
                return 0;
        }
        return [c autorelease];
}
- (id)device { return owner; }
- (void)dealloc { [owner release]; [super dealloc]; }
@end

@implementation Nord2MetalCommandBuffer
- (id)computeCommandEncoder
{
        Nord2MetalComputeCommandEncoder *e =
                [[Nord2MetalComputeCommandEncoder alloc] init];
        e->commandBuffer = [self retain];
        e->encoder = calloc(1, sizeof(*e->encoder));
        if (!e->encoder || !n2m_command_encoder(command, e->encoder)) {
                [e release];
                return 0;
        }
        return [e autorelease];
}
- (void)commit
{
        if (submitted || !command)
                return;
        submitted = 1;
        if (!n2m_command_commit(command))
                failed = 1;
}
- (void)waitUntilCompleted
{
        if (!submitted || finished)
                return;
        if (!n2m_command_wait(command, 5000))
                failed = 1;
        finished = 1;
        if (completionBlock)
                completionBlock(self);
}
- (signed char)status { return (failed || !command ||
                                n2m_command_state(command) != 3) ? 1 : 0; }
- (id)error { return 0; }
- (void)setLabel:(id)label { (void)label; }
- (void)dealloc
{
        if (command) {
                free(command);
                command = 0;
        }
        [owner release];
        [super dealloc];
}
@end

@implementation Nord2MetalComputeCommandEncoder
- (void)setComputePipelineState:(id)state
{
        Nord2MetalPipeline *p = (Nord2MetalPipeline *)state;
        if (n2m_encoder_set_pipeline(encoder, p ? p->pipeline : 0)) {
                [p retain];
                [pipeline release];
                pipeline = p;
        }
}
- (void)setBuffer:(id)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index
{
        Nord2MetalBuffer *b = (Nord2MetalBuffer *)buffer;
        (void)n2m_encoder_set_buffer(encoder, (unsigned)index,
                                     b ? b->buffer : 0, (uint32_t)offset);
}
- (void)dispatchThreadgroups:(MTLSize)groups
        threadsPerThreadgroup:(MTLSize)threads
{
        (void)n2m_encoder_dispatch(encoder, (uint32_t)groups.width,
                                   (uint32_t)groups.height,
                                   (uint32_t)groups.depth,
                                   (uint32_t)threads.width,
                                   (uint32_t)threads.height,
                                   (uint32_t)threads.depth);
}
- (void)endEncoding { (void)n2m_encoder_end(encoder); }
- (void)dealloc
{
        if (encoder) {
                free(encoder);
                encoder = 0;
        }
        [pipeline release];
        [commandBuffer release];
        [super dealloc];
}
@end

@implementation Nord2MetalDevice
- (id)initWithAcceleratorPort:(unsigned)port
{
        (void)port;
        self = [super init];
        if (!self)
                return 0;
        void *io = dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",
                          2);
        void *sys = dlopen("/usr/lib/libSystem.B.dylib", 2);
        if (!io || !sys)
                return 0;
        void *(*matching)(const char *) = dlsym(io, "IOServiceMatching");
        unsigned (*get)(unsigned, void *) = dlsym(io, "IOServiceGetMatchingService");
        int (*open)(unsigned, unsigned, unsigned, unsigned *) =
                dlsym(io, "IOServiceOpen");
        unsigned (*release)(unsigned) = dlsym(io, "IOObjectRelease");
        unsigned *taskPtr = dlsym(sys, "mach_task_self_");
        scalar = dlsym(io, "IOConnectCallScalarMethod");
        method = dlsym(io, "IOConnectCallMethod");
        map = dlsym(io, "IOConnectMapMemory64");
        unmap = dlsym(io, "IOConnectUnmapMemory64");
        closeService = dlsym(io, "IOServiceClose");
        if (!matching || !get || !open || !release || !taskPtr || !scalar ||
            !method || !map || !unmap || !closeService)
                return 0;
        task = *taskPtr;
        void *dict = matching("Nord2GPU");
        if (!dict)
                return 0;
        unsigned service = get(0, dict);
        if (!service)
                return 0;
        int result = open(service, task, 0, &connection);
        release(service);
        if (result)
                return 0;
        struct n2m_ops ops = {m_scalar, m_method, m_map, m_unmap};
        if (!n2m_device_init(&core, &ops, self))
                return 0;
        return self;
}
- (id)init { return [self initWithAcceleratorPort:0]; }
- (id)name { return @"Nord 2 Mali-G77 (arch9)"; }
- (NSUInteger)maxBufferLength { return 16UL << 20; }
- (MTLSize)maxThreadsPerThreadgroup { return (MTLSize){256, 256, 256}; }
- (signed char)hasUnifiedMemory { return 1; }
- (signed char)isHeadless { return 1; }
- (signed char)supportsFamily:(long long)family { (void)family; return 0; }
- (NSUInteger)currentAllocatedSize { return (NSUInteger)core.allocated_bytes; }
- (id)newBufferInternal:(NSUInteger)length executable:(int)executable
{
        Nord2MetalBuffer *b = [[Nord2MetalBuffer alloc] init];
        b->owner = [self retain];
        b->buffer = calloc(1, sizeof(*b->buffer));
        if (!b->buffer ||
            !n2m_buffer_new(&core, length, executable, b->buffer)) {
                [b release];
                return 0;
        }
        return [b autorelease];
}
- (id)newBufferWithLength:(NSUInteger)length options:(NSUInteger)options
{
        (void)options; /* shared storage only; no private cache modes */
        return [self newBufferInternal:length executable:0];
}
- (id)newBufferWithBytes:(const void *)bytes length:(NSUInteger)length
                 options:(NSUInteger)options
{
        Nord2MetalBuffer *b = [self newBufferWithLength:length options:options];
        if (b && bytes)
                memcpy([b contents], bytes, length);
        return b;
}
- (id)newCommandQueue
{
        Nord2MetalCommandQueue *q = [[Nord2MetalCommandQueue alloc] init];
        q->owner = [self retain];
        return [q autorelease];
}
- (id)newComputePipelineStateWithBinary:(const void *)binary
                                 length:(NSUInteger)length
                                  error:(id *)error
{
        if (!binary || !length || length > 4096) {
                if (error)
                        *error = [NSError errorWithDomain:@"Nord2Metal"
                                                     code:1
                                                 userInfo:0];
                return 0;
        }
        Nord2MetalBuffer *code = [self newBufferInternal:4096 executable:1];
        if (!code) {
                if (error)
                        *error = [NSError errorWithDomain:@"Nord2Metal"
                                                     code:2
                                                 userInfo:0];
                return 0;
        }
        memcpy([code contents], binary, length);
        Nord2MetalPipeline *p = [[Nord2MetalPipeline alloc] init];
        p->owner = [self retain];
        p->binary = [code retain];
        p->pipeline = calloc(1, sizeof(*p->pipeline));
        if (!p->pipeline ||
            !n2m_pipeline_new(&core, code->buffer, 0, p->pipeline)) {
                [p release];
                return 0;
        }
        return [p autorelease];
}
- (void)dealloc
{
        if (connection && closeService)
                closeService(connection);
        [super dealloc];
}
@end
