/* Apple1/Apple2-baseline Metal API surface that is not tied to one pipeline file.
 *
 * Included last by compute_frontend.m (after the blit encoder), so every class
 * below is complete.  New API goes here; the exact policy per selector is in
 * gpu/build/api-surface-20261027/API-SURFACE.md.  Rules this file follows:
 *  - implemented  : the selector does what Metal documents, and a host test
 *                   (metal/tegu/api_surface_host_test.inc) proves it;
 *  - refused      : the selector exists, the call marks the command buffer
 *                   invalid (it completes with an error) or returns nil with an
 *                   NSError, and logs "TEGU_FRONTEND UNSUPPORTED <name>".  A
 *                   refusal never pretends to have done the work and is never
 *                   counted as support;
 *  - never silent : nothing here drops observable state.
 * Devices advertise Apple1/Apple2 only, so tessellation, argument buffers,
 * indirect command buffers, mesh/tile shading and ray tracing are feature-gated
 * by supportsFamily:; their entry points are intentionally absent or refused. */

typedef void (^TeguPipelineCompletion)(id pipeline,id error);
typedef void (^TeguPipelineReflectionCompletion)(id pipeline,id reflection,id error);
typedef void (^TeguLibraryCompletion)(id library,id error);

extern int getpid(void);
extern int unlink(const char *);
extern void *realloc(void *,NSUInteger);
extern char *getenv(const char *);

@interface NSObject (TeguAPISurfaceBase)
/* _MTLDevice's own source compiler front end (MTLCompilerService): it returns
 * Apple's library object; we only take its serialized AIR. */
- (id)newLibraryWithSource:(id)source options:(id)options error:(id *)error;
- (signed char)serializeToURL:(id)url error:(id *)error;
- (signed char)respondsToSelector:(SEL)selector;
- (id)binaryArchives;
- (id)url;
- (NSUInteger)count;
- (NSUInteger)maxCommandBufferCount;
@end
@interface NSURL (TeguAPISurfaceFileURL)
+ (id)fileURLWithPath:(id)path;
@end

/* Minimal file helpers (the freestanding device build has no stdio.h). */
#ifndef TEGU_FRONTEND_HOST
extern void *fopen(const char *,const char *);extern int fclose(void *);
extern unsigned long fwrite(const void *,unsigned long,unsigned long,void *);
extern unsigned long fread(void *,unsigned long,unsigned long,void *);
#endif
static int tegu_api_file_write(const char *path,const void *bytes,NSUInteger length) {
 void *file=fopen(path,"wb");
 if(!file)return 0;
 int ok=fwrite(bytes,1,length,file)==length;
 return fclose(file)==0&&ok;
}
static NSUInteger tegu_api_file_read(const char *path,void *bytes,NSUInteger length) {
 void *file=fopen(path,"rb");
 if(!file)return 0;
 NSUInteger got=fread(bytes,1,length,file);
 fclose(file);return got;
}
static id tegu_api_error(NSUInteger code) {
 static const char *const text[]={"unknown","invalid argument","unsupported on this device","invalid archive URL",
  "archive could not be written","temporary library path too long","library could not be serialized to AIR",
  "serialized library is empty","missing Metal source","Metal source compiler unavailable",
  "pipeline not found in the binary archives","archive file missing or foreign","counter sampling is unsupported",
  "dynamic libraries are unsupported","function stitching is unsupported","tile pipelines are unsupported","MTLIO is unsupported"};
 const char *message=code<sizeof(text)/sizeof(text[0])?text[code]:text[0];
 id description=[NSString stringWithUTF8String:message];
 id info=description?[NSDictionary dictionaryWithObject:description forKey:@"NSLocalizedDescription"]:0;
 return [NSError errorWithDomain:@"TeguAPI" code:(long)code userInfo:info];
}
/* Bounded one-line log of a refused selector (first N per process). */
static void tegu_api_unsupported(const char *what) {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=64)return;
 char line[160];int n=snprintf(line,sizeof(line),"TEGU_FRONTEND UNSUPPORTED %s\n",what);
 if(n>0)write(2,line,(NSUInteger)n<sizeof(line)?(NSUInteger)n:sizeof(line)-1);
}
#define TEGU_API_REFUSE(command,what) do{tegu_api_unsupported(what);TEGU_INVALID(command);}while(0)

/* ------------------------------------------------------------------------- */
/* MTLBinaryArchive: a functional but non-caching archive.  Metal compiles and
 * stores pipeline binaries; this frontend keeps its own pipeline cache
 * (pipeline_cache*.c), so the archive records that functions were "added"
 * (descriptor validity is checked), serializes a small header that round trips,
 * and never claims a hit: MTLPipelineOptionFailOnBinaryArchiveMiss therefore
 * always fails and other pipeline creation compiles (or hits our own cache). */
#define TEGU_ARCHIVE_MAGIC 0x41424754u /* "TGBA" */
@interface TeguBinaryArchive : NSObject {
@public TeguComputeDevice *owner;id debugLabel;unsigned added;
}
- (id)device;
- (id)label;
- (void)setLabel:(id)value;
- (signed char)addComputePipelineFunctionsWithDescriptor:(id)descriptor error:(id *)error;
- (signed char)addRenderPipelineFunctionsWithDescriptor:(id)descriptor error:(id *)error;
- (signed char)addTileRenderPipelineFunctionsWithDescriptor:(id)descriptor error:(id *)error;
- (signed char)addFunctionWithDescriptor:(id)descriptor library:(id)library error:(id *)error;
- (signed char)serializeToURL:(id)url error:(id *)error;
@end
@implementation TeguBinaryArchive
- (id)device { return owner; }
- (id)label { return debugLabel; }
- (void)setLabel:(id)value { id copy=[value copy];[debugLabel release];debugLabel=copy; }
- (signed char)teguAdd:(id)descriptor error:(id *)error {
 if(error)*error=0;
 if(!descriptor){if(error)*error=tegu_api_error(1);return 0;}
 __atomic_fetch_add(&added,1u,__ATOMIC_RELAXED);return 1;
}
- (signed char)addComputePipelineFunctionsWithDescriptor:(id)descriptor error:(id *)error { return [self teguAdd:descriptor error:error]; }
- (signed char)addRenderPipelineFunctionsWithDescriptor:(id)descriptor error:(id *)error { return [self teguAdd:descriptor error:error]; }
- (signed char)addTileRenderPipelineFunctionsWithDescriptor:(id)descriptor error:(id *)error {
 (void)descriptor;tegu_api_unsupported("binary_archive_tile");
 if(error)*error=tegu_api_error(2);return 0;
}
- (signed char)addFunctionWithDescriptor:(id)descriptor library:(id)library error:(id *)error {
 if(!library){if(error)*error=tegu_api_error(1);return 0;}
 return [self teguAdd:descriptor error:error];
}
- (signed char)serializeToURL:(id)url error:(id *)error {
 if(error)*error=0;
 id path=url&&[url isFileURL]?[url path]:0;
 const char *name=path?[path UTF8String]:0;
 if(!name){if(error)*error=tegu_api_error(3);return 0;}
 unsigned header[2]={TEGU_ARCHIVE_MAGIC,added};
 if(!tegu_api_file_write(name,header,sizeof(header))){if(error)*error=tegu_api_error(4);return 0;}
 return 1;
}
- (void)dealloc { [debugLabel release];[owner release];[super dealloc]; }
@end

/* ------------------------------------------------------------------------- */
@interface TeguComputeDevice (APISurface)
- (id)newLibraryWithSource:(id)source options:(id)options error:(id *)error;
- (void)newLibraryWithSource:(id)source options:(id)options completionHandler:(TeguLibraryCompletion)handler;
- (id)newCommandQueueWithMaxCommandBufferCount:(NSUInteger)count;
- (id)newCommandQueueWithDescriptor:(id)descriptor;
- (id)newRenderPipelineStateWithDescriptor:(id)descriptor options:(NSUInteger)options reflection:(id *)reflection error:(id *)error;
- (void)newRenderPipelineStateWithDescriptor:(id)descriptor options:(NSUInteger)options completionHandler:(TeguPipelineReflectionCompletion)handler;
- (void)newComputePipelineStateWithFunction:(id)function completionHandler:(TeguPipelineCompletion)handler;
- (void)newComputePipelineStateWithFunction:(id)function options:(NSUInteger)options completionHandler:(TeguPipelineReflectionCompletion)handler;
- (void)newComputePipelineStateWithDescriptor:(id)descriptor options:(NSUInteger)options completionHandler:(TeguPipelineReflectionCompletion)handler;
- (void)sampleTimestamps:(U64 *)cpu gpuTimestamp:(U64 *)gpu;
- (id)newBinaryArchiveWithDescriptor:(id)descriptor error:(id *)error;
- (id)counterSets;
- (signed char)supportsCounterSampling:(NSUInteger)point;
- (id)newCounterSampleBufferWithDescriptor:(id)descriptor error:(id *)error;
- (id)newDynamicLibrary:(id)library error:(id *)error;
- (id)newDynamicLibraryWithURL:(id)url error:(id *)error;
- (id)newSharedTextureWithDescriptor:(id)descriptor;
- (id)newSharedTextureWithHandle:(id)handle;
- (signed char)supportsRasterizationRateMapWithLayerCount:(NSUInteger)count;
- (id)newRasterizationRateMapWithDescriptor:(id)descriptor;
- (void)getDefaultSamplePositions:(float *)positions count:(NSUInteger)count;
- (id)newLibraryWithStitchedDescriptor:(id)descriptor error:(id *)error;
- (void)newLibraryWithStitchedDescriptor:(id)descriptor completionHandler:(TeguLibraryCompletion)handler;
- (id)newArgumentEncoderWithArguments:(id)arguments;
- (id)newArgumentEncoderWithBufferBinding:(id)binding;
- (id)newIndirectCommandBufferWithDescriptor:(id)descriptor maxCommandCount:(NSUInteger)count options:(NSUInteger)options;
- (NSUInteger)minimumTextureBufferAlignmentForPixelFormat:(NSUInteger)pixelFormat;
- (signed char)supportsVertexAmplificationCount:(NSUInteger)count;
- (void)newRenderPipelineStateWithTileDescriptor:(id)descriptor options:(NSUInteger)options completionHandler:(TeguPipelineReflectionCompletion)handler;
- (id)newIOHandleWithURL:(id)url error:(id *)error;
- (id)newIOHandleWithURL:(id)url compressionMethod:(NSUInteger)compression error:(id *)error;
- (id)newIOCommandQueueWithDescriptor:(id)descriptor error:(id *)error;
@end

/* Apple's front end made `apple` from MSL; take its serialized metallib. */
static id tegu_api_library_from_apple(TeguComputeDevice *device,id apple,id *error) {
 const char *tmp=getenv("TMPDIR");
 if(!tmp||!tmp[0])tmp="/tmp";
 static unsigned sequence;
 char path[512];
 int n=snprintf(path,sizeof(path),"%s/tegu-msl-%d-%u.metallib",tmp,getpid(),__atomic_fetch_add(&sequence,1u,__ATOMIC_RELAXED));
 if(n<=0||(size_t)n>=sizeof(path)){if(error)*error=tegu_api_error(5);return 0;}
 id file=[NSString stringWithUTF8String:path];
 id url=file?[NSURL fileURLWithPath:file]:0;
 id serializeError=0;
 if(!url||![apple respondsToSelector:@selector(serializeToURL:error:)]||![apple serializeToURL:url error:&serializeError]) {
  tegu_api_unsupported("msl_library_serialize");
  if(error)*error=serializeError?serializeError:tegu_api_error(6);
  unlink(path);return 0;
 }
 NSData *data=[NSData dataWithContentsOfFile:file];
 unlink(path);
 if(!data||![data length]){if(error)*error=tegu_api_error(7);return 0;}
 return [device newTeguLibraryWithBytes:[data bytes] length:[data length] error:error];
}

static id tegu_make_argument_encoder(TeguComputeDevice *device,id arguments);
@implementation TeguComputeDevice (APISurface)
/* MSL source: Apple's compiler front end (the base class, reached through the
 * compiler service) makes the AIR; the result is rebuilt as OUR library so its
 * functions belong to this bridge.  Without a front end (no base) this fails
 * with an NSError, never with a foreign library object. */
- (id)newLibraryWithSource:(id)source options:(id)options error:(id *)error {
 if(error)*error=0;
 if(!source){if(error)*error=tegu_api_error(8);return 0;}
 id baseError=0;
 id apple=[super newLibraryWithSource:source options:options error:&baseError];
 if(!apple){
  tegu_api_unsupported("msl_source_compiler");
  if(error)*error=baseError?baseError:tegu_api_error(9);return 0;
 }
 id library=tegu_api_library_from_apple(self,apple,error);
 [apple release];
 return library;
}
- (void)newLibraryWithSource:(id)source options:(id)options completionHandler:(TeguLibraryCompletion)handler {
 id error=0;id library=[self newLibraryWithSource:source options:options error:&error];
 if(handler)handler(library,error);
 [library release];
}
- (id)newCommandQueueWithMaxCommandBufferCount:(NSUInteger)count {
 /* Command buffers are unbounded here; the count only has to be a sane request. */
 if(!count)return 0;
 return [self newCommandQueue];
}
/* Reflection objects (MTLRenderPipelineReflection) are not produced; the out
 * parameter is cleared so callers never read stale memory.  Binary-archive
 * misses cannot be satisfied (the archive stores nothing). */
static int tegu_api_archive_miss(id descriptor,NSUInteger options) {
 if(!(options&4u)||!descriptor||![descriptor respondsToSelector:@selector(binaryArchives)])return 0;
 id archives=[descriptor binaryArchives];
 return archives&&[archives count]>0;
}
- (id)newCommandQueueWithDescriptor:(id)descriptor {
 /* MTLCommandQueueDescriptor: only maxCommandBufferCount exists (0 = default 64). */
 NSUInteger count=64;
 if(descriptor&&[descriptor respondsToSelector:@selector(maxCommandBufferCount)])count=[descriptor maxCommandBufferCount];
 return [self newCommandQueueWithMaxCommandBufferCount:count?count:64];
}
- (id)newRenderPipelineStateWithDescriptor:(id)descriptor options:(NSUInteger)options reflection:(id *)reflection error:(id *)error {
 if(reflection)*reflection=0;
 if(tegu_api_archive_miss(descriptor,options)){
  tegu_api_unsupported("binary_archive_miss");
  if(error)*error=tegu_api_error(10);return 0;
 }
 return [self newRenderPipelineStateWithDescriptor:descriptor error:error];
}
- (void)newRenderPipelineStateWithDescriptor:(id)descriptor options:(NSUInteger)options completionHandler:(TeguPipelineReflectionCompletion)handler {
 id error=0,reflection=0;
 id pipeline=[self newRenderPipelineStateWithDescriptor:descriptor options:options reflection:&reflection error:&error];
 if(handler)handler(pipeline,reflection,error);
 [pipeline release];
}
- (void)newComputePipelineStateWithFunction:(id)function completionHandler:(TeguPipelineCompletion)handler {
 id error=0;id pipeline=[self newComputePipelineStateWithFunction:function error:&error];
 if(handler)handler(pipeline,error);
 [pipeline release];
}
- (void)newComputePipelineStateWithFunction:(id)function options:(NSUInteger)options completionHandler:(TeguPipelineReflectionCompletion)handler {
 id error=0,reflection=0;
 id pipeline=[self newComputePipelineStateWithFunction:function options:options reflection:&reflection error:&error];
 if(handler)handler(pipeline,reflection,error);
 [pipeline release];
}
- (void)newComputePipelineStateWithDescriptor:(id)descriptor options:(NSUInteger)options completionHandler:(TeguPipelineReflectionCompletion)handler {
 id error=0,reflection=0;
 id pipeline=[self newComputePipelineStateWithDescriptor:descriptor options:options reflection:&reflection error:&error];
 if(handler)handler(pipeline,reflection,error);
 [pipeline release];
}
/* CPU and GPU timestamps share the host monotonic clock: command times
 * (GPUStartTime) are host seconds, so GPU ticks ARE host nanoseconds. */
- (void)sampleTimestamps:(U64 *)cpu gpuTimestamp:(U64 *)gpu {
 U64 now=(U64)(tegu_host_seconds()*1e9);
 if(cpu)*cpu=now;
 if(gpu)*gpu=now;
}
- (id)newBinaryArchiveWithDescriptor:(id)descriptor error:(id *)error {
 if(error)*error=0;
 TeguBinaryArchive *archive=[[TeguBinaryArchive alloc] init];if(!archive)return 0;
 archive->owner=[self retain];
 /* A descriptor url names an archive to load: it must exist and carry our header
  * (a foreign or truncated file is rejected, not silently treated as empty). */
 id url=descriptor&&[descriptor respondsToSelector:@selector(url)]?[descriptor url]:0;
 if(url) {
  id path=[url isFileURL]?[url path]:0;const char *name=path?[path UTF8String]:0;
  unsigned header[2]={0,0};
  int ok=name&&tegu_api_file_read(name,header,sizeof(header))==sizeof(header)&&header[0]==TEGU_ARCHIVE_MAGIC;
  if(!ok){[archive release];if(error)*error=tegu_api_error(11);return 0;}
  archive->added=header[1];
 }
 return archive;
}
/* Counters: no sample sets exist.  Metal reports an empty/nil set list for a
 * device without them; creating a sample buffer is an error. */
- (id)counterSets { return 0; }
- (signed char)supportsCounterSampling:(NSUInteger)point { (void)point;return 0; }
- (id)newCounterSampleBufferWithDescriptor:(id)descriptor error:(id *)error {
 (void)descriptor;tegu_api_unsupported("counter_sample_buffer");
 if(error)*error=tegu_api_error(12);return 0;
}
- (id)newDynamicLibrary:(id)library error:(id *)error {
 (void)library;tegu_api_unsupported("dynamic_library");
 if(error)*error=tegu_api_error(13);return 0;
}
- (id)newDynamicLibraryWithURL:(id)url error:(id *)error {
 (void)url;tegu_api_unsupported("dynamic_library_url");
 if(error)*error=tegu_api_error(13);return 0;
}
/* Cross-process shared textures need a Mach-port handle. */
- (id)newSharedTextureWithDescriptor:(id)descriptor { (void)descriptor;tegu_api_unsupported("shared_texture");return 0; }
- (id)newSharedTextureWithHandle:(id)handle { (void)handle;tegu_api_unsupported("shared_texture_handle");return 0; }
- (signed char)supportsRasterizationRateMapWithLayerCount:(NSUInteger)count { (void)count;return 0; }
- (id)newRasterizationRateMapWithDescriptor:(id)descriptor { (void)descriptor;tegu_api_unsupported("rasterization_rate_map");return 0; }
/* Function stitching (CoreImage kernel graphs), argument buffers and indirect command
 * buffers need compiler lowering this bridge does not have: nil (+NSError), traced.
 * Callers that probe capability (supportsFamily:) never reach these. */
- (id)newLibraryWithStitchedDescriptor:(id)descriptor error:(id *)error {
 (void)descriptor;tegu_api_unsupported("stitched_library");
 if(error)*error=tegu_api_error(14);return 0;
}
- (void)newLibraryWithStitchedDescriptor:(id)descriptor completionHandler:(TeguLibraryCompletion)handler {
 id error=0;id library=[self newLibraryWithStitchedDescriptor:descriptor error:&error];
 if(handler)handler(library,error);
}
- (id)newArgumentEncoderWithArguments:(id)arguments { return tegu_make_argument_encoder(self,arguments); }
- (id)newArgumentEncoderWithBufferBinding:(id)binding { (void)binding;tegu_api_unsupported("argument_encoder");return 0; }
- (id)newIndirectCommandBufferWithDescriptor:(id)descriptor maxCommandCount:(NSUInteger)count options:(NSUInteger)options {
 (void)descriptor;(void)count;(void)options;tegu_api_unsupported("indirect_command_buffer");return 0;
}
/* Linear-texture-over-buffer alignment equals the linear texture alignment. */
- (NSUInteger)minimumTextureBufferAlignmentForPixelFormat:(NSUInteger)pixelFormat {
 return [self minimumLinearTextureAlignmentForPixelFormat:pixelFormat];
}
/* Vertex amplification is not implemented: only the trivial count of one. */
- (signed char)supportsVertexAmplificationCount:(NSUInteger)count { return count==1; }
- (void)newRenderPipelineStateWithTileDescriptor:(id)descriptor options:(NSUInteger)options completionHandler:(TeguPipelineReflectionCompletion)handler {
 id reflection=0,error=0;
 id pipeline=[self newRenderPipelineStateWithTileDescriptor:descriptor options:options reflection:&reflection error:&error];
 if(handler)handler(pipeline,reflection,error);
 [pipeline release];
}
/* MTLIO (iOS 16 file streaming): no I/O queue exists. */
- (id)newIOHandleWithURL:(id)url error:(id *)error { (void)url;tegu_api_unsupported("mtlio");if(error)*error=tegu_api_error(16);return 0; }
- (id)newIOHandleWithURL:(id)url compressionMethod:(NSUInteger)compression error:(id *)error { (void)url;(void)compression;tegu_api_unsupported("mtlio");if(error)*error=tegu_api_error(16);return 0; }
- (id)newIOCommandQueueWithDescriptor:(id)descriptor error:(id *)error { (void)descriptor;tegu_api_unsupported("mtlio");if(error)*error=tegu_api_error(16);return 0; }
/* Metal's fixed sample positions: 1x centre, 2x and 4x the documented patterns. */
- (void)getDefaultSamplePositions:(float *)positions count:(NSUInteger)count {
 static const float p1[2]={0.5f,0.5f};
 static const float p2[4]={0.75f,0.75f,0.25f,0.25f};
 static const float p4[8]={0.375f,0.125f,0.875f,0.375f,0.125f,0.625f,0.625f,0.875f};
 const float *source=count==1?p1:count==2?p2:count==4?p4:0;
 if(!positions||!source){if(positions&&count)tegu_api_unsupported("sample_positions");return;}
 __builtin_memcpy(positions,source,count*2*sizeof(float));
}
@end

/* ------------------------------------------------------------------------- */
/* Capability queries the Apple1/Apple2 baseline answers NO to.  The real _MTLDevice would
 * answer through its _featureQueries object, which this device never fills (an unqualified
 * profile); stating the answers here makes them explicit, traced (TEGU_DEVQ) and independent
 * of that ivar.  argumentBuffersSupport is left to the base class: Metal has no "none" tier,
 * and the entry points behind it are refused (newArgumentEncoder*). */
typedef struct { NSUInteger accelerationStructureSize,buildScratchBufferSize,refitScratchBufferSize; } TeguAccelerationStructureSizes;
@interface TeguComputeDevice (APIFeatureQueries)
- (NSUInteger)readWriteTextureSupport;
- (signed char)areRasterOrderGroupsSupported;
- (signed char)supports32BitFloatFiltering;
- (signed char)supports32BitMSAA;
- (signed char)supportsQueryTextureLOD;
- (signed char)supportsBCTextureCompression;
- (signed char)supportsPullModelInterpolation;
- (signed char)areBarycentricCoordsSupported;
- (signed char)supportsShaderBarycentricCoordinates;
- (signed char)areProgrammableSamplePositionsSupported;
- (signed char)supportsDynamicLibraries;
- (signed char)supportsRenderDynamicLibraries;
- (signed char)supportsRaytracing;
- (signed char)supportsRaytracingFromRender;
- (signed char)supportsFunctionPointers;
- (signed char)supportsFunctionPointersFromRender;
- (signed char)supportsPrimitiveMotionBlur;
- (NSUInteger)maxArgumentBufferSamplerCount;
- (NSUInteger)sparseTileSizeInBytes;
/* Private identity/memory SPI the reference plugin also answers (system tools and frameworks read them). */
- (id)productName;
- (id)familyName;
- (id)vendorName;
- (U64)sharedMemorySize;
- (U64)dedicatedMemorySize;
- (signed char)requiresIABEmulation;
- (NSUInteger)minLinearTextureAlignmentForPixelFormat:(NSUInteger)pixelFormat;
@end
#define TEGU_API_QUERY(name,type,value) - (type)name { tegu_device_query_trace(#name,0,(int)(value));return (type)(value); }
@implementation TeguComputeDevice (APIFeatureQueries)
TEGU_API_QUERY(readWriteTextureSupport,NSUInteger,0)
TEGU_API_QUERY(areRasterOrderGroupsSupported,signed char,0)
TEGU_API_QUERY(supports32BitFloatFiltering,signed char,0)
TEGU_API_QUERY(supports32BitMSAA,signed char,0)
TEGU_API_QUERY(supportsQueryTextureLOD,signed char,0)
TEGU_API_QUERY(supportsBCTextureCompression,signed char,0)
TEGU_API_QUERY(supportsPullModelInterpolation,signed char,0)
TEGU_API_QUERY(areBarycentricCoordsSupported,signed char,0)
TEGU_API_QUERY(supportsShaderBarycentricCoordinates,signed char,0)
TEGU_API_QUERY(areProgrammableSamplePositionsSupported,signed char,0)
TEGU_API_QUERY(supportsDynamicLibraries,signed char,0)
TEGU_API_QUERY(supportsRenderDynamicLibraries,signed char,0)
TEGU_API_QUERY(supportsRaytracing,signed char,0)
TEGU_API_QUERY(supportsRaytracingFromRender,signed char,0)
TEGU_API_QUERY(supportsFunctionPointers,signed char,0)
TEGU_API_QUERY(supportsFunctionPointersFromRender,signed char,0)
TEGU_API_QUERY(supportsPrimitiveMotionBlur,signed char,0)
TEGU_API_QUERY(maxArgumentBufferSamplerCount,NSUInteger,0)
TEGU_API_QUERY(sparseTileSizeInBytes,NSUInteger,0)
TEGU_API_QUERY(dedicatedMemorySize,U64,0)
TEGU_API_QUERY(requiresIABEmulation,signed char,0)
- (id)productName { return [self name]; }
- (id)familyName { return [NSString stringWithUTF8String:"Mali Valhall"]; }
- (id)vendorName { return [NSString stringWithUTF8String:"Arm"]; }
/* Unified memory: the whole kernel allocation budget the device reports as its working set. */
- (U64)sharedMemorySize { return (U64)[self recommendedMaxWorkingSetSize]; }
- (NSUInteger)minLinearTextureAlignmentForPixelFormat:(NSUInteger)pixelFormat { return [self minimumLinearTextureAlignmentForPixelFormat:pixelFormat]; }
/* Features of devices above Apple1/Apple2: sparse textures, ray tracing acceleration structures, mesh shading.  The
 * entry points exist so a client that probes without checking the family gets nil / zero sizes and a trace instead of
 * an unrecognized-selector exception; none of them pretends to have done any work. */
- (id)newRenderPipelineStateWithMeshDescriptor:(id)descriptor options:(NSUInteger)options reflection:(id *)reflection error:(id *)error {
 (void)descriptor;(void)options;if(reflection)*reflection=0;tegu_api_unsupported("mesh_render_pipeline");
 if(error)*error=tegu_api_error(2);return 0;
}
- (void)newRenderPipelineStateWithMeshDescriptor:(id)descriptor options:(NSUInteger)options completionHandler:(TeguPipelineReflectionCompletion)handler {
 id error=0,reflection=0;id state=[self newRenderPipelineStateWithMeshDescriptor:descriptor options:options reflection:&reflection error:&error];
 if(handler)handler(state,reflection,error);
}
- (MTLSize)sparseTileSizeWithTextureType:(NSUInteger)type pixelFormat:(NSUInteger)format sampleCount:(NSUInteger)samples {
 (void)type;(void)format;(void)samples;tegu_api_unsupported("sparse_textures");return (MTLSize){0,0,0};
}
- (MTLSize)sparseTileSizeWithTextureType:(NSUInteger)type pixelFormat:(NSUInteger)format sampleCount:(NSUInteger)samples sparsePageSize:(long)pageSize {
 (void)type;(void)format;(void)samples;(void)pageSize;tegu_api_unsupported("sparse_textures");return (MTLSize){0,0,0};
}
- (NSUInteger)sparseTileSizeInBytesForSparsePageSize:(long)pageSize { (void)pageSize;tegu_api_unsupported("sparse_textures");return 0; }
- (void)convertSparsePixelRegions:(const void *)pixelRegions toTileRegions:(void *)tileRegions withTileSize:(MTLSize)tileSize alignmentMode:(NSUInteger)mode numRegions:(NSUInteger)count {
 (void)pixelRegions;(void)tileRegions;(void)tileSize;(void)mode;(void)count;tegu_api_unsupported("sparse_textures");
}
- (void)convertSparseTileRegions:(const void *)tileRegions toPixelRegions:(void *)pixelRegions withTileSize:(MTLSize)tileSize numRegions:(NSUInteger)count {
 (void)tileRegions;(void)pixelRegions;(void)tileSize;(void)count;tegu_api_unsupported("sparse_textures");
}
- (TeguAccelerationStructureSizes)accelerationStructureSizesWithDescriptor:(id)descriptor {
 (void)descriptor;tegu_api_unsupported("acceleration_structure");return (TeguAccelerationStructureSizes){0,0,0};
}
- (id)newAccelerationStructureWithSize:(NSUInteger)size { (void)size;tegu_api_unsupported("acceleration_structure");return 0; }
- (id)newAccelerationStructureWithDescriptor:(id)descriptor { (void)descriptor;tegu_api_unsupported("acceleration_structure");return 0; }
- (TeguSizeAndAlign)heapAccelerationStructureSizeAndAlignWithSize:(NSUInteger)size {
 (void)size;tegu_api_unsupported("acceleration_structure");return (TeguSizeAndAlign){0,0};
}
- (TeguSizeAndAlign)heapAccelerationStructureSizeAndAlignWithDescriptor:(id)descriptor {
 (void)descriptor;tegu_api_unsupported("acceleration_structure");return (TeguSizeAndAlign){0,0};
}
@end
#undef TEGU_API_QUERY

/* ------------------------------------------------------------------------- */
@interface TeguQueue (APISurface)
- (id)commandBufferWithUnretainedReferences;
- (void)insertDebugCaptureBoundary;
@end
@implementation TeguQueue (APISurface)
/* Our command buffers always retain what they encode, which is the safe
 * superset of "unretained references". */
- (id)commandBufferWithUnretainedReferences { return [self commandBuffer]; }
- (void)insertDebugCaptureBoundary {}
@end

@interface NSObject (TeguPassDescriptorSelectors)
- (NSUInteger)dispatchType;
- (NSUInteger)maxCommandBufferCount;
@end
@interface TeguCommand (APISurface)
- (id)commandQueue;
- (signed char)retainedReferences;
- (NSUInteger)errorOptions;
- (id)logs;
- (id)computeCommandEncoderWithDescriptor:(id)descriptor;
- (id)resourceStateCommandEncoder;
- (id)resourceStateCommandEncoderWithDescriptor:(id)descriptor;
- (id)accelerationStructureCommandEncoder;
- (id)accelerationStructureCommandEncoderWithDescriptor:(id)descriptor;
@end
@implementation TeguCommand (APISurface)
- (id)commandQueue { return queue; }
- (signed char)retainedReferences { return 1; }
- (NSUInteger)errorOptions { return 0; }
/* No shader validation logs exist; Metal returns an empty container, nil is
 * the nil-safe equivalent for enumeration. */
- (id)logs { return 0; }
/* MTLComputePassDescriptor: serial and concurrent dispatch are both satisfied by
 * serial execution; counter sample attachments cannot exist (no counter sets). */
- (id)computeCommandEncoderWithDescriptor:(id)descriptor {
 if(descriptor&&[descriptor respondsToSelector:@selector(dispatchType)]&&[descriptor dispatchType]>1){TEGU_API_REFUSE(self,"compute_pass_dispatch_type");return 0;}
 return [self computeCommandEncoder];
}
- (id)resourceStateCommandEncoder { TEGU_API_REFUSE(self,"resource_state_encoder");return 0; }
- (id)resourceStateCommandEncoderWithDescriptor:(id)descriptor { (void)descriptor;TEGU_API_REFUSE(self,"resource_state_encoder");return 0; }
- (id)accelerationStructureCommandEncoder { TEGU_API_REFUSE(self,"acceleration_structure_encoder");return 0; }
- (id)accelerationStructureCommandEncoderWithDescriptor:(id)descriptor { (void)descriptor;TEGU_API_REFUSE(self,"acceleration_structure_encoder");return 0; }
@end

/* ------------------------------------------------------------------------- */
/* Compute encoder: bulk binding, residency hints, barriers, fences.  Resources
 * live in shared kernel memory and every job runs in encode order on one
 * serial stream, so residency hints and memory barriers have nothing left to
 * do after validation; fences are identity objects (api_events.inc). */
static int tegu_api_resource_ok(TeguCommand *command,id resource) {
 if(!resource)return 0;
 if(tegu_api_class_is(resource,"TeguBuffer",0))return [(TeguBuffer *)resource device]==command->queue->owner;
#ifdef TEGU_GRAPHICS_BRIDGE
 if(tegu_api_class_is(resource,"TeguTexture",0))return [(TeguTexture *)resource device]==command->queue->owner;
#endif
 return 0;
}
static int tegu_api_heap_ok(TeguCommand *command,id heap) {
#ifdef TEGU_GRAPHICS_BRIDGE
 return heap&&tegu_api_class_is(heap,"TeguHeap",0)&&[(TeguHeap *)heap device]==command->queue->owner;
#else
 (void)command;(void)heap;return 0;
#endif
}
#ifdef TEGU_GRAPHICS_BRIDGE
/* Sampler with a LOD clamp override: the exact Metal state with new lod_min/lod_max. */
static id tegu_api_sampler_with_lod(TeguSampler *source,float lodMin,float lodMax) {
 if(!source||!tegu_api_class_is((id)source,"TeguSampler",0)||!(lodMin<=lodMax)||lodMin<0)return 0;
 TeguSampler *clone=[[TeguSampler alloc] init];if(!clone)return 0;
 clone->owner=[source->owner retain];clone->linearFilters=source->linearFilters;
 clone->state=source->state;clone->state.lod_min=lodMin;clone->state.lod_max=lodMax;
 if(n2t_sampler_validate(&clone->state)!=N2T_OK){[clone release];return 0;}
 return clone;
}
#endif
/* Indirect compute dispatch (CoreImage's grid-from-GPU kernels).  The grid lives in a
 * buffer a previous job may write, and the v2 plan bakes the grid into the pipeline
 * variant and push constants, so nothing can be planned at encode time.  The encoder
 * therefore snapshots its bindings into a pseudo-job (api_events.inc kind 3); when the
 * job runs, earlier GPU work has been reaped, the three uint32 threadgroup counts are
 * read, and the dispatch is planned and submitted through the ordinary compute encoder
 * on a scratch command buffer (so every validation and the v2 planner are the exact
 * code of -dispatchThreadgroups:).  A zero count dispatches nothing, as in Metal. */
#ifdef TEGU_MALI_BRIDGE
@interface TeguIndirectDispatch : NSObject {
@public TeguQueue *queue;TeguPipeline *pipeline;TeguBuffer *buffers[31];NSUInteger offsets[31];
 id textures[31],samplers[16];NSUInteger lengths[31];TeguBuffer *indirect;NSUInteger indirectOffset;MTLSize local;
}
@end
@implementation TeguIndirectDispatch
- (void)dealloc {
 for(unsigned i=0;i<31;i++){[buffers[i] release];[textures[i] release];}
 for(unsigned i=0;i<16;i++)[samplers[i] release];
 [indirect release];[pipeline release];[queue release];[super dealloc];
}
@end
#endif
struct TeguComputeRegion {NSUInteger origin[3],size[3];};
@interface TeguEncoder (APISurface)
- (void)setBuffers:(TeguBuffer *const *)buffers offsets:(const NSUInteger *)offsets withRange:(struct TeguComputeRange)range;
- (void)setSamplerState:(id)sampler lodMinClamp:(float)lodMin lodMaxClamp:(float)lodMax atIndex:(NSUInteger)index;
- (void)setSamplerStates:(id const *)samplers lodMinClamps:(const float *)mins lodMaxClamps:(const float *)maxs withRange:(struct TeguComputeRange)range;
- (void)useResource:(id)resource usage:(NSUInteger)usage;
- (void)useResources:(id const *)resources count:(NSUInteger)count usage:(NSUInteger)usage;
- (void)useHeap:(id)heap;
- (void)useHeaps:(id const *)heaps count:(NSUInteger)count;
- (void)memoryBarrierWithScope:(NSUInteger)scope;
- (void)memoryBarrierWithResources:(id const *)resources count:(NSUInteger)count;
- (void)updateFence:(id)fence;
- (void)waitForFence:(id)fence;
- (NSUInteger)dispatchType;
- (void)dispatchThreadgroupsWithIndirectBuffer:(id)buffer indirectBufferOffset:(NSUInteger)offset threadsPerThreadgroup:(MTLSize)local;
- (void)setStageInRegion:(struct TeguComputeRegion)region;
- (void)setImageblockWidth:(NSUInteger)width height:(NSUInteger)height;
- (void)executeCommandsInBuffer:(id)buffer withRange:(struct TeguComputeRange)range;
- (void)sampleCountersInBuffer:(id)buffer atSampleIndex:(NSUInteger)index withBarrier:(signed char)barrier;
@end
@implementation TeguEncoder (APISurface)
- (void)setBuffers:(TeguBuffer *const *)list offsets:(const NSUInteger *)offs withRange:(struct TeguComputeRange)range {
 if(ended||[command teguCommitted]||range.location>31||range.length>31-range.location||(range.length&&(!list||!offs))){TEGU_INVALID(command);return;}
 for(NSUInteger i=0;i<range.length;i++)[self setBuffer:list[i] offset:offs[i] atIndex:range.location+i];
}
- (void)setSamplerState:(id)sampler lodMinClamp:(float)lodMin lodMaxClamp:(float)lodMax atIndex:(NSUInteger)index {
#ifdef TEGU_GRAPHICS_BRIDGE
 if(ended||[command teguCommitted]||index>=16){TEGU_INVALID(command);return;}
 if(!sampler){[self setSamplerState:0 atIndex:index];return;}
 id clone=tegu_api_sampler_with_lod((TeguSampler *)sampler,lodMin,lodMax);
 if(!clone||((TeguSampler *)clone)->owner!=command->queue->owner){[clone release];TEGU_INVALID(command);return;}
 [self setSamplerState:clone atIndex:index];[clone release];
#else
 (void)sampler;(void)lodMin;(void)lodMax;(void)index;TEGU_API_REFUSE(command,"compute_sampler_lod");
#endif
}
- (void)setSamplerStates:(id const *)list lodMinClamps:(const float *)mins lodMaxClamps:(const float *)maxs withRange:(struct TeguComputeRange)range {
 if(ended||[command teguCommitted]||range.location>16||range.length>16-range.location||(range.length&&(!list||!mins||!maxs))){TEGU_INVALID(command);return;}
 for(NSUInteger i=0;i<range.length;i++)[self setSamplerState:list[i] lodMinClamp:mins[i] lodMaxClamp:maxs[i] atIndex:range.location+i];
}
- (void)useResource:(id)resource usage:(NSUInteger)usage {
 if(ended||[command teguCommitted]||!usage||usage>7||!tegu_api_resource_ok(command,resource)||
    tegu_command_use_resource(command,resource))TEGU_INVALID(command);
}
- (void)useResources:(id const *)resources count:(NSUInteger)count usage:(NSUInteger)usage {
 if(ended||[command teguCommitted]||!usage||usage>7||(count&&!resources)){TEGU_INVALID(command);return;}
 for(NSUInteger i=0;i<count;i++)if(!tegu_api_resource_ok(command,resources[i])||tegu_command_use_resource(command,resources[i])){TEGU_INVALID(command);return;}
}
- (void)useHeap:(id)heap {
 if(ended||[command teguCommitted]||!tegu_api_heap_ok(command,heap)||tegu_command_use_resource(command,heap))TEGU_INVALID(command);
}
- (void)useHeaps:(id const *)heaps count:(NSUInteger)count {
 if(ended||[command teguCommitted]||(count&&!heaps)){TEGU_INVALID(command);return;}
 for(NSUInteger i=0;i<count;i++)if(!tegu_api_heap_ok(command,heaps[i])||tegu_command_use_resource(command,heaps[i])){TEGU_INVALID(command);return;}
}
/* Scope bits: buffers 1, textures 2, render targets 4. */
- (void)memoryBarrierWithScope:(NSUInteger)scope {
 if(ended||[command teguCommitted]||!scope||scope>7)TEGU_INVALID(command);
}
- (void)memoryBarrierWithResources:(id const *)resources count:(NSUInteger)count {
 if(ended||[command teguCommitted]||(count&&!resources)){TEGU_INVALID(command);return;}
 for(NSUInteger i=0;i<count;i++)if(!tegu_api_resource_ok(command,resources[i])){TEGU_INVALID(command);return;}
}
- (void)updateFence:(id)fence {
 if(ended||[command teguCommitted]){TEGU_INVALID(command);return;}
 tegu_fence_update(command,fence);
}
- (void)waitForFence:(id)fence {
 if(ended||[command teguCommitted]){TEGU_INVALID(command);return;}
 tegu_fence_wait(command,fence);
}
- (NSUInteger)dispatchType { return 0; }
/* Indirect dispatch reads its grid when the GPU runs it; the v2 plan bakes the
 * grid into the specialized shader at encode time, so a GPU-produced grid has no
 * honest lowering here.  Refused explicitly. */
- (void)dispatchThreadgroupsWithIndirectBuffer:(id)buffer indirectBufferOffset:(NSUInteger)offset threadsPerThreadgroup:(MTLSize)local {
#ifdef TEGU_MALI_BRIDGE
 if(ended||[command teguCommitted]||!pipeline||command->count>=TEGU_COMMAND_MAX_JOBS||!buffer||
    !tegu_api_class_is(buffer,"TeguBuffer",0)||((TeguBuffer *)buffer)->owner!=command->queue->owner||
    offset%4||offset>((TeguBuffer *)buffer)->logical||((TeguBuffer *)buffer)->logical-offset<12||
    !local.width||!local.height||!local.depth){TEGU_INVALID(command);return;}
 if(pipeline->inert){tegu_compute_inert_trace(0,(unsigned)local.width,tegu_threadgroup_lengths_present(threadgroupLengths));return;}
#ifdef TEGU_GRAPHICS_BRIDGE
 for(unsigned i=0;i<31;i++)if(textures[i]&&(((TeguTexture *)textures[i])->shadowSurface||((TeguTexture *)textures[i])->surfaceImported)){
  TEGU_API_REFUSE(command,"compute_indirect_dispatch_iosurface");return;
 }
#endif
 TeguIndirectDispatch *snap=[[TeguIndirectDispatch alloc] init];
 if(!snap){TEGU_INVALID(command);return;}
 snap->queue=[command->queue retain];snap->pipeline=[pipeline retain];
 for(unsigned i=0;i<31;i++){snap->buffers[i]=[buffers[i] retain];snap->offsets[i]=offsets[i];snap->textures[i]=[textures[i] retain];snap->lengths[i]=threadgroupLengths[i];}
 for(unsigned i=0;i<16;i++)snap->samplers[i]=[samplers[i] retain];
 snap->indirect=[(TeguBuffer *)buffer retain];snap->indirectOffset=offset;snap->local=local;
 struct Dispatch *job=tegu_command_job(command,command->count++);
 job->eventKind=TEGU_EVENT_INDIRECT;job->eventObject=snap;command->eventJobs++;
#else
 (void)buffer;(void)offset;(void)local;TEGU_API_REFUSE(command,"compute_indirect_dispatch");
#endif
}
- (void)setStageInRegion:(struct TeguComputeRegion)region { (void)region;TEGU_API_REFUSE(command,"compute_stage_in");}
- (void)setImageblockWidth:(NSUInteger)width height:(NSUInteger)height { (void)width;(void)height;TEGU_API_REFUSE(command,"compute_imageblock"); }
- (void)executeCommandsInBuffer:(id)buffer withRange:(struct TeguComputeRange)range { (void)buffer;(void)range;TEGU_API_REFUSE(command,"indirect_command_buffer"); }
- (void)sampleCountersInBuffer:(id)buffer atSampleIndex:(NSUInteger)index withBarrier:(signed char)barrier {
 (void)buffer;(void)index;(void)barrier;TEGU_API_REFUSE(command,"counter_sampling");
}
@end

#ifdef TEGU_MALI_BRIDGE
static int tegu_indirect_dispatch_run(struct Dispatch *job) {
 TeguIndirectDispatch *s=(TeguIndirectDispatch *)job->eventObject;
 const unsigned char *args=(const unsigned char *)tegu_buffer_bytes(s->indirect);
 if(!args||s->indirectOffset>s->indirect->logical||s->indirect->logical-s->indirectOffset<12)return -4;
 uint32_t groups[3];__builtin_memcpy(groups,args+s->indirectOffset,sizeof(groups));
 if(!groups[0]||!groups[1]||!groups[2])return 0;
 if(groups[0]>65536||groups[1]>65536||groups[2]>65536){MARK("FAIL indirect_dispatch_grid");return -4;}
 TeguComputeDevice *device=s->queue->owner;
 TeguCommand *scratch=[[TeguCommand alloc] init];if(!scratch)return -1;
 scratch->queue=[s->queue retain];
 TeguEncoder *encoder=[[TeguEncoder alloc] initWithCommand:scratch];
 if(!encoder){[scratch release];return -1;}
 scratch->encoding=1;
 [encoder setComputePipelineState:s->pipeline];
 for(unsigned i=0;i<31;i++){
  if(s->buffers[i])[encoder setBuffer:s->buffers[i] offset:s->offsets[i] atIndex:i];
#if defined(TEGU_GRAPHICS_BRIDGE)&&defined(TEGU_AIR_BRIDGE)
  if(s->textures[i])[encoder setTexture:(TeguTexture *)s->textures[i] atIndex:i];
#endif
  if(s->lengths[i])[encoder setThreadgroupMemoryLength:s->lengths[i] atIndex:i];
 }
 for(unsigned i=0;i<16;i++)if(s->samplers[i])[encoder setSamplerState:s->samplers[i] atIndex:i];
 [encoder dispatchThreadgroups:(MTLSize){groups[0],groups[1],groups[2]} threadsPerThreadgroup:s->local];
 [encoder endEncoding];
 int error=scratch->invalid?-1:0;
 for(unsigned i=0;!error&&i<scratch->count;i++) {
  struct Dispatch *j=&TEGU_JOB(scratch->jobChunks,i);U64 reply[2]={0};unsigned n=2;
  if(!j->pipeline){error=-1;break;}
  const void *wire=j->wire2?j->wire2:(const void *)j->wire;NSUInteger bytes=j->wire2?j->wire2Bytes:sizeof(j->wire);
  error=device->method(device->connection,7,&j->pipeline->handle,1,wire,bytes,reply,&n,0,0);
  if(!error&&(n!=2||!reply[0]))error=-2;
 }
 [encoder release];[scratch release];
 return error;
}
#else
static int tegu_indirect_dispatch_run(struct Dispatch *job) { (void)job;return -1; }
#endif

/* ------------------------------------------------------------------------- */
#ifdef TEGU_GRAPHICS_BRIDGE
@interface TeguRenderEncoder (APISurface)
- (void)setViewports:(const TeguViewport *)viewports count:(NSUInteger)count;
- (void)setScissorRects:(const TeguScissorRect *)rects count:(NSUInteger)count;
- (void)setVertexSamplerState:(id)sampler lodMinClamp:(float)lodMin lodMaxClamp:(float)lodMax atIndex:(NSUInteger)index;
- (void)setFragmentSamplerState:(id)sampler lodMinClamp:(float)lodMin lodMaxClamp:(float)lodMax atIndex:(NSUInteger)index;
- (void)setVertexSamplerStates:(id const *)samplers lodMinClamps:(const float *)mins lodMaxClamps:(const float *)maxs withRange:(TeguRange)range;
- (void)setFragmentSamplerStates:(id const *)samplers lodMinClamps:(const float *)mins lodMaxClamps:(const float *)maxs withRange:(TeguRange)range;
- (void)setVertexTexture:(TeguTexture *)texture atTextureIndex:(NSUInteger)textureIndex samplerState:(TeguSampler *)sampler atSamplerIndex:(NSUInteger)samplerIndex;
- (void)setDepthStoreAction:(NSUInteger)action;
- (void)setStencilStoreAction:(NSUInteger)action;
- (void)setColorStoreActionOptions:(NSUInteger)options atIndex:(NSUInteger)index;
- (void)setDepthStoreActionOptions:(NSUInteger)options;
- (void)setStencilStoreActionOptions:(NSUInteger)options;
- (void)useResources:(id const *)resources count:(NSUInteger)count usage:(NSUInteger)usage;
- (void)useResources:(id const *)resources count:(NSUInteger)count usage:(NSUInteger)usage stages:(NSUInteger)stages;
- (void)useHeaps:(id const *)heaps count:(NSUInteger)count;
- (void)useHeap:(id)heap stages:(NSUInteger)stages;
- (void)useHeaps:(id const *)heaps count:(NSUInteger)count stages:(NSUInteger)stages;
- (void)updateFence:(id)fence afterStages:(NSUInteger)stages;
- (void)waitForFence:(id)fence beforeStages:(NSUInteger)stages;
- (void)memoryBarrierWithScope:(NSUInteger)scope afterStages:(NSUInteger)after beforeStages:(NSUInteger)before;
- (void)memoryBarrierWithResources:(id const *)resources count:(NSUInteger)count afterStages:(NSUInteger)after beforeStages:(NSUInteger)before;
- (void)executeCommandsInBuffer:(id)buffer withRange:(TeguRange)range;
- (void)sampleCountersInBuffer:(id)buffer atSampleIndex:(NSUInteger)index withBarrier:(signed char)barrier;
- (void)textureBarrier;
- (NSUInteger)tileWidth;
- (NSUInteger)tileHeight;
@end
@implementation TeguRenderEncoder (APISurface)
/* Draws run strictly in order and each completes (and is cache-maintained) before the next, so a
 * texture barrier has nothing to wait for.  No tile shading: tile dimensions are 0, as for a device
 * without it. */
- (void)textureBarrier { if(!inert&&(ended||[command teguCommitted]))TEGU_INVALID(command); }
- (NSUInteger)tileWidth { return 0; }
- (NSUInteger)tileHeight { return 0; }
/* One viewport/scissor only: multiple viewports need the layered-rendering family. */
- (void)setViewports:(const TeguViewport *)viewports count:(NSUInteger)count {
 if(inert)return;
 if(ended||!viewports||count!=1){TEGU_API_REFUSE(command,"multiple_viewports");return;}
 [self setViewport:viewports[0]];
}
- (void)setScissorRects:(const TeguScissorRect *)rects count:(NSUInteger)count {
 if(inert)return;
 if(ended||!rects||count!=1){TEGU_API_REFUSE(command,"multiple_scissors");return;}
 [self setScissorRect:rects[0]];
}
- (void)setVertexSamplerState:(id)sampler lodMinClamp:(float)lodMin lodMaxClamp:(float)lodMax atIndex:(NSUInteger)index {
 if(inert)return;
 if(!sampler){[self setVertexSamplerState:0 atIndex:index];return;}
 id clone=tegu_api_sampler_with_lod((TeguSampler *)sampler,lodMin,lodMax);
 if(!clone||((TeguSampler *)clone)->owner!=command->queue->owner){[clone release];TEGU_INVALID(command);return;}
 [self setVertexSamplerState:(TeguSampler *)clone atIndex:index];[clone release];
}
- (void)setFragmentSamplerState:(id)sampler lodMinClamp:(float)lodMin lodMaxClamp:(float)lodMax atIndex:(NSUInteger)index {
 if(inert)return;
 if(!sampler){[self setFragmentSamplerState:0 atIndex:index];return;}
 id clone=tegu_api_sampler_with_lod((TeguSampler *)sampler,lodMin,lodMax);
 if(!clone||((TeguSampler *)clone)->owner!=command->queue->owner){[clone release];TEGU_INVALID(command);return;}
 [self setFragmentSamplerState:(TeguSampler *)clone atIndex:index];[clone release];
}
- (void)setVertexSamplerStates:(id const *)samplers lodMinClamps:(const float *)mins lodMaxClamps:(const float *)maxs withRange:(TeguRange)range {
 if(inert)return;
 if(ended||range.location>16||range.length>16-range.location||(range.length&&(!samplers||!mins||!maxs))){TEGU_INVALID(command);return;}
 for(NSUInteger i=0;i<range.length;i++)[self setVertexSamplerState:samplers[i] lodMinClamp:mins[i] lodMaxClamp:maxs[i] atIndex:range.location+i];
}
- (void)setFragmentSamplerStates:(id const *)samplers lodMinClamps:(const float *)mins lodMaxClamps:(const float *)maxs withRange:(TeguRange)range {
 if(inert)return;
 if(ended||range.location>16||range.length>16-range.location||(range.length&&(!samplers||!mins||!maxs))){TEGU_INVALID(command);return;}
 for(NSUInteger i=0;i<range.length;i++)[self setFragmentSamplerState:samplers[i] lodMinClamp:mins[i] lodMaxClamp:maxs[i] atIndex:range.location+i];
}
- (void)setVertexTexture:(TeguTexture *)texture atTextureIndex:(NSUInteger)textureIndex samplerState:(TeguSampler *)sampler atSamplerIndex:(NSUInteger)samplerIndex {
 if(inert)return;
 [self setVertexTexture:texture atIndex:textureIndex];
 if(!command->invalid)[self setVertexSamplerState:sampler atIndex:samplerIndex];
}
/* Depth/stencil attachments are admitted only with DontCare storage (the generic
 * pass has no depth/stencil execution), so DontCare is the one valid request. */
- (void)setDepthStoreAction:(NSUInteger)action {
 if(inert||ended)return;
 if(hasDepthAttachment&&action!=0)TEGU_API_REFUSE(command,"depth_store_action");
}
- (void)setStencilStoreAction:(NSUInteger)action {
 if(inert||ended)return;
 if(hasStencilAttachment&&action!=0)TEGU_API_REFUSE(command,"stencil_store_action");
}
- (void)setColorStoreActionOptions:(NSUInteger)options atIndex:(NSUInteger)index {
 if(inert||ended)return;
 if(index>3||options)TEGU_API_REFUSE(command,"color_store_action_options");
}
- (void)setDepthStoreActionOptions:(NSUInteger)options { if(!inert&&!ended&&options)TEGU_API_REFUSE(command,"depth_store_action_options"); }
- (void)setStencilStoreActionOptions:(NSUInteger)options { if(!inert&&!ended&&options)TEGU_API_REFUSE(command,"stencil_store_action_options"); }
- (void)useResources:(id const *)resources count:(NSUInteger)count usage:(NSUInteger)usage {
 if(inert)return;
 if(ended||!usage||usage>7||(count&&!resources)){TEGU_INVALID(command);return;}
 for(NSUInteger i=0;i<count;i++)if(!tegu_api_resource_ok(command,resources[i])||tegu_command_use_resource(command,resources[i])){TEGU_INVALID(command);return;}
}
- (void)useResource:(id)resource usage:(NSUInteger)usage {
 if(inert)return;
 if(ended||[command teguCommitted]||!usage||usage>7||!tegu_api_resource_ok(command,resource)||
    tegu_command_use_resource(command,resource))TEGU_INVALID(command);
}
- (void)useResource:(id)resource usage:(NSUInteger)usage stages:(NSUInteger)stages {
 if(inert)return;
 if(stages&~3u){TEGU_INVALID(command);return;}
 [self useResource:resource usage:usage];
}
- (void)useHeap:(id)heap {
 if(inert)return;
 if(ended||[command teguCommitted]||!tegu_api_heap_ok(command,heap)||tegu_command_use_resource(command,heap))TEGU_INVALID(command);
}
- (void)useResources:(id const *)resources count:(NSUInteger)count usage:(NSUInteger)usage stages:(NSUInteger)stages {
 if(stages&~3u){TEGU_INVALID(command);return;}
 [self useResources:resources count:count usage:usage];
}
- (void)useHeaps:(id const *)heaps count:(NSUInteger)count {
 if(inert)return;
 if(ended||(count&&!heaps)){TEGU_INVALID(command);return;}
 for(NSUInteger i=0;i<count;i++)if(!tegu_api_heap_ok(command,heaps[i])||tegu_command_use_resource(command,heaps[i])){TEGU_INVALID(command);return;}
}
- (void)useHeap:(id)heap stages:(NSUInteger)stages {
 if(inert)return;
 if(stages&~3u){TEGU_INVALID(command);return;}
 [self useHeap:heap];
}
- (void)useHeaps:(id const *)heaps count:(NSUInteger)count stages:(NSUInteger)stages {
 if(stages&~3u){TEGU_INVALID(command);return;}
 [self useHeaps:heaps count:count];
}
/* Stage masks: vertex 1, fragment 2.  Fences are identity objects (draws and
 * passes already execute in encode order). */
- (void)updateFence:(id)fence afterStages:(NSUInteger)stages {
 if(inert)return;
 if(ended||!stages||(stages&~3u)){TEGU_INVALID(command);return;}
 tegu_fence_update(command,fence);
}
- (void)waitForFence:(id)fence beforeStages:(NSUInteger)stages {
 if(inert)return;
 if(ended||!stages||(stages&~3u)){TEGU_INVALID(command);return;}
 tegu_fence_wait(command,fence);
}
- (void)memoryBarrierWithScope:(NSUInteger)scope afterStages:(NSUInteger)after beforeStages:(NSUInteger)before {
 if(inert)return;
 if(ended||!scope||scope>7||!after||(after&~3u)||!before||(before&~3u))TEGU_INVALID(command);
}
- (void)memoryBarrierWithResources:(id const *)resources count:(NSUInteger)count afterStages:(NSUInteger)after beforeStages:(NSUInteger)before {
 if(inert)return;
 if(ended||!after||(after&~3u)||!before||(before&~3u)||(count&&!resources)){TEGU_INVALID(command);return;}
 for(NSUInteger i=0;i<count;i++)if(!tegu_api_resource_ok(command,resources[i])){TEGU_INVALID(command);return;}
}
- (void)executeCommandsInBuffer:(id)buffer withRange:(TeguRange)range { (void)buffer;(void)range;if(!inert)TEGU_API_REFUSE(command,"indirect_command_buffer"); }
- (void)sampleCountersInBuffer:(id)buffer atSampleIndex:(NSUInteger)index withBarrier:(signed char)barrier {
 (void)buffer;(void)index;(void)barrier;if(!inert)TEGU_API_REFUSE(command,"counter_sampling");
}
@end

@interface TeguCommand (APISurfaceBlit)
- (id)blitCommandEncoderWithDescriptor:(id)descriptor;
@end
@implementation TeguCommand (APISurfaceBlit)
- (id)blitCommandEncoderWithDescriptor:(id)descriptor { (void)descriptor;return [self blitCommandEncoder]; }
@end
@interface TeguBlitEncoder (APISurface)
- (void)copyFromTexture:(TeguTexture *)source sourceSlice:(NSUInteger)sourceSlice sourceLevel:(NSUInteger)sourceLevel
 toTexture:(TeguTexture *)destination destinationSlice:(NSUInteger)destinationSlice destinationLevel:(NSUInteger)destinationLevel
 sliceCount:(NSUInteger)sliceCount levelCount:(NSUInteger)levelCount;
@end
@implementation TeguBlitEncoder (APISurface)
/* iOS 15 whole-subresource copy: every selected slice of every selected level at
 * that level's full extent (a level shrinks with its index, minimum 1). */
- (void)copyFromTexture:(TeguTexture *)source sourceSlice:(NSUInteger)ss sourceLevel:(NSUInteger)sl
 toTexture:(TeguTexture *)destination destinationSlice:(NSUInteger)ds destinationLevel:(NSUInteger)dl
 sliceCount:(NSUInteger)sliceCount levelCount:(NSUInteger)levelCount {
 if(ended||[command teguCommitted]||!source||!destination||!sliceCount||!levelCount||
    !tegu_api_class_is((id)source,"TeguTexture",0)||!tegu_api_class_is((id)destination,"TeguTexture",0)||
    sl>source->levelCount||levelCount>source->levelCount-sl||dl>destination->levelCount||levelCount>destination->levelCount-dl){
  TEGU_INVALID(command);return;
 }
 for(NSUInteger s=0;s<sliceCount&&!command->invalid;s++)for(NSUInteger l=0;l<levelCount&&!command->invalid;l++) {
  NSUInteger w=source->textureWidth>>(sl+l),h=source->textureHeight>>(sl+l),d=source->textureDepth>>(sl+l);
  [self copyFromTexture:source sourceSlice:ss+s sourceLevel:sl+l sourceOrigin:(TeguOrigin){0,0,0}
   sourceSize:(MTLSize){w?w:1,h?h:1,(source->textureKind==7&&d)?d:1}
   toTexture:destination destinationSlice:ds+s destinationLevel:dl+l destinationOrigin:(TeguOrigin){0,0,0}];
 }
}
@end

@interface TeguBlitEncoder (APISurfaceHints)
- (void)optimizeContentsForCPUAccess:(id)resource;
- (void)optimizeContentsForCPUAccess:(id)resource slice:(NSUInteger)slice level:(NSUInteger)level;
@end
@implementation TeguBlitEncoder (APISurfaceHints)
/* Performance hints; shared storage has one layout, so they validate and do nothing. */
- (void)optimizeContentsForCPUAccess:(id)resource { [self optimizeContentsForGPUAccess:resource]; }
- (void)optimizeContentsForCPUAccess:(id)resource slice:(NSUInteger)slice level:(NSUInteger)level {
 [self optimizeContentsForGPUAccess:resource slice:slice level:level];
}
@end
@interface TeguTexture (APISurface)
- (id)parentTexture;
- (id)rootResource;
- (NSUInteger)parentRelativeLevel;
- (NSUInteger)parentRelativeSlice;
- (signed char)isShareable;
- (signed char)allowGPUOptimizedContents;
- (NSUInteger)compressionType;
- (unsigned)swizzle;
- (id)newSharedTextureHandle;
- (signed char)isSparse;
- (NSUInteger)firstMipmapInTail;
- (NSUInteger)tailSizeInBytes;
- (id)newTextureViewWithPixelFormat:(NSUInteger)viewFormat textureType:(NSUInteger)viewType levels:(TeguTextureViewRange)levels
 slices:(TeguTextureViewRange)slices swizzle:(unsigned)swizzle;
@end
@implementation TeguTexture (APISurface)
- (id)parentTexture { return viewSource; }
- (NSUInteger)parentRelativeLevel { return viewSource?viewParentLevel:0; }
- (NSUInteger)parentRelativeSlice { return viewSource?viewParentSlice:0; }
- (id)rootResource { TeguTexture *t=self;while(t->viewSource)t=t->viewSource;return t==self?0:t; }
- (signed char)isShareable { return 0; }
- (signed char)allowGPUOptimizedContents { return 1; }
- (NSUInteger)compressionType { return 0; }
/* No component swizzle exists in the sampler path: identity RGBA
 * (MTLTextureSwizzle: Red 2, Green 3, Blue 4, Alpha 5, one byte each). */
- (unsigned)swizzle { return 2u|(3u<<8)|(4u<<16)|(5u<<24); }
- (id)newSharedTextureHandle { tegu_api_unsupported("shared_texture_handle");return 0; }
- (signed char)isSparse { return 0; }
- (NSUInteger)firstMipmapInTail { return 0; }
- (NSUInteger)tailSizeInBytes { return 0; }
- (id)newTextureViewWithPixelFormat:(NSUInteger)viewFormat textureType:(NSUInteger)viewType levels:(TeguTextureViewRange)levels
 slices:(TeguTextureViewRange)slices swizzle:(unsigned)swizzle {
 if(swizzle!=(2u|(3u<<8)|(4u<<16)|(5u<<24))){tegu_api_unsupported("texture_view_swizzle");return 0;}
 return [self newTextureViewWithPixelFormat:viewFormat textureType:viewType levels:levels slices:slices];
}
@end
@interface TeguBuffer (APISurfaceOwner)
- (int)setOwnerWithIdentity:(unsigned)identity;
@end
@implementation TeguBuffer (APISurfaceOwner)
/* Memory-attribution SPI (task identity token): accounting only, accepted. */
- (int)setOwnerWithIdentity:(unsigned)identity { (void)identity;return 0; }
@end
@interface TeguTexture (APISurfaceOwner)
- (int)setOwnerWithIdentity:(unsigned)identity;
@end
@implementation TeguTexture (APISurfaceOwner)
- (int)setOwnerWithIdentity:(unsigned)identity { (void)identity;return 0; }
@end
@interface TeguHeap (APISurfaceOwner)
- (int)setOwnerWithIdentity:(unsigned)identity;
@end
@implementation TeguHeap (APISurfaceOwner)
- (int)setOwnerWithIdentity:(unsigned)identity { (void)identity;return 0; }
@end
static U64 tegu_buffer_gpu_address(TeguBuffer *buffer);
@interface TeguBuffer (APISurface)
- (void)addDebugMarker:(id)marker range:(TeguRange)range;
- (void)removeAllDebugMarkers;
- (U64)gpuAddress;
@end
@implementation TeguBuffer (APISurface)
/* The BO's GPU virtual address (bridge-assigned, fixed for the buffer's life): what an argument buffer
 * stores and a shader dereferences.  0 when the bridge cannot name it (api_argbuf.inc). */
- (U64)gpuAddress { return tegu_buffer_gpu_address(self); }
- (void)addDebugMarker:(id)marker range:(TeguRange)range { (void)marker;(void)range; }
- (void)removeAllDebugMarkers {}
@end

/* Pipeline-state / texture / sampler members of feature families the device does not advertise (tile and mesh
 * shading, function tables, ray tracing, argument-buffer resource IDs).  Zero or nil with a trace: a client that
 * probes without checking the family does not take an unrecognized-selector exception.  MTLResourceID is one
 * 64-bit word; no resource ID model exists on this hardware path, so 0 (invalid) is the honest answer. */
typedef struct { unsigned long long impl; } TeguResourceID;
@interface TeguPipeline (APISurfaceFamilies)
- (NSUInteger)imageblockMemoryLengthForDimensions:(MTLSize)dimensions;
- (TeguResourceID)gpuResourceID;
- (id)functionHandleWithFunction:(id)function;
- (id)newComputePipelineStateWithAdditionalBinaryFunctions:(id)functions error:(id *)error;
- (id)newVisibleFunctionTableWithDescriptor:(id)descriptor;
- (id)newIntersectionFunctionTableWithDescriptor:(id)descriptor;
@end
@implementation TeguPipeline (APISurfaceFamilies)
- (NSUInteger)imageblockMemoryLengthForDimensions:(MTLSize)dimensions { (void)dimensions;tegu_api_unsupported("imageblock");return 0; }
- (TeguResourceID)gpuResourceID { tegu_api_unsupported("gpu_resource_id");return (TeguResourceID){0}; }
- (id)functionHandleWithFunction:(id)function { (void)function;tegu_api_unsupported("function_handle");return 0; }
- (id)newComputePipelineStateWithAdditionalBinaryFunctions:(id)functions error:(id *)error {
 (void)functions;tegu_api_unsupported("additional_binary_functions");if(error)*error=tegu_api_error(2);return 0;
}
- (id)newVisibleFunctionTableWithDescriptor:(id)descriptor { (void)descriptor;tegu_api_unsupported("visible_function_table");return 0; }
- (id)newIntersectionFunctionTableWithDescriptor:(id)descriptor { (void)descriptor;tegu_api_unsupported("intersection_function_table");return 0; }
@end
#ifdef TEGU_GRAPHICS_BRIDGE
@interface TeguRenderPipeline (APISurfaceFamilies)
- (signed char)threadgroupSizeMatchesTileSize;
- (NSUInteger)imageblockSampleLength;
- (NSUInteger)imageblockMemoryLengthForDimensions:(MTLSize)dimensions;
- (NSUInteger)maxTotalThreadsPerObjectThreadgroup;
- (NSUInteger)maxTotalThreadsPerMeshThreadgroup;
- (NSUInteger)objectThreadExecutionWidth;
- (NSUInteger)meshThreadExecutionWidth;
- (NSUInteger)maxTotalThreadgroupsPerMeshGrid;
- (TeguResourceID)gpuResourceID;
- (id)functionHandleWithFunction:(id)function stage:(NSUInteger)stage;
- (id)newVisibleFunctionTableWithDescriptor:(id)descriptor stage:(NSUInteger)stage;
- (id)newIntersectionFunctionTableWithDescriptor:(id)descriptor stage:(NSUInteger)stage;
- (id)newRenderPipelineStateWithAdditionalBinaryFunctions:(id)functions error:(id *)error;
@end
@implementation TeguRenderPipeline (APISurfaceFamilies)
- (signed char)threadgroupSizeMatchesTileSize { return 0; }
- (NSUInteger)imageblockSampleLength { tegu_api_unsupported("imageblock");return 0; }
- (NSUInteger)imageblockMemoryLengthForDimensions:(MTLSize)dimensions { (void)dimensions;tegu_api_unsupported("imageblock");return 0; }
- (NSUInteger)maxTotalThreadsPerObjectThreadgroup { return 0; }
- (NSUInteger)maxTotalThreadsPerMeshThreadgroup { return 0; }
- (NSUInteger)objectThreadExecutionWidth { return 0; }
- (NSUInteger)meshThreadExecutionWidth { return 0; }
- (NSUInteger)maxTotalThreadgroupsPerMeshGrid { return 0; }
- (TeguResourceID)gpuResourceID { tegu_api_unsupported("gpu_resource_id");return (TeguResourceID){0}; }
- (id)functionHandleWithFunction:(id)function stage:(NSUInteger)stage { (void)function;(void)stage;tegu_api_unsupported("function_handle");return 0; }
- (id)newVisibleFunctionTableWithDescriptor:(id)descriptor stage:(NSUInteger)stage { (void)descriptor;(void)stage;tegu_api_unsupported("visible_function_table");return 0; }
- (id)newIntersectionFunctionTableWithDescriptor:(id)descriptor stage:(NSUInteger)stage { (void)descriptor;(void)stage;tegu_api_unsupported("intersection_function_table");return 0; }
- (id)newRenderPipelineStateWithAdditionalBinaryFunctions:(id)functions error:(id *)error {
 (void)functions;tegu_api_unsupported("additional_binary_functions");if(error)*error=tegu_api_error(2);return 0;
}
@end
@interface TeguTexture (APISurfaceResourceID)
- (TeguResourceID)gpuResourceID;
@end
@implementation TeguTexture (APISurfaceResourceID)
- (TeguResourceID)gpuResourceID { tegu_api_unsupported("gpu_resource_id");return (TeguResourceID){0}; }
@end
@interface TeguSampler (APISurfaceResourceID)
- (TeguResourceID)gpuResourceID;
@end
@implementation TeguSampler (APISurfaceResourceID)
- (TeguResourceID)gpuResourceID { tegu_api_unsupported("gpu_resource_id");return (TeguResourceID){0}; }
@end
#endif

/* Function/library reflection that has nothing to report for a non-tessellated,
 * non-stitched AIR function. */
@interface TeguFunction (APISurface)
- (NSUInteger)patchType;
- (long)patchControlPointCount;
- (NSUInteger)options;
- (id)functionConstantsDictionary;
- (id)vertexAttributes;
- (id)stageInputAttributes;
- (id)newArgumentEncoderWithBufferIndex:(NSUInteger)index;
- (id)newArgumentEncoderWithBufferIndex:(NSUInteger)index reflection:(id *)reflection;
@end
@implementation TeguFunction (APISurface)
- (id)newArgumentEncoderWithBufferIndex:(NSUInteger)index { (void)index;tegu_api_unsupported("argument_encoder");return 0; }
- (id)newArgumentEncoderWithBufferIndex:(NSUInteger)index reflection:(id *)reflection {
 (void)index;if(reflection)*reflection=0;tegu_api_unsupported("argument_encoder");return 0;
}
- (NSUInteger)patchType { return 0; }
- (long)patchControlPointCount { return -1; }
- (NSUInteger)options { return 0; }
- (id)functionConstantsDictionary { return 0; }
- (id)vertexAttributes { return 0; }
- (id)stageInputAttributes { return 0; }
@end
@interface TeguLibrary (APISurface)
- (NSUInteger)type;
- (id)installName;
- (void)newFunctionWithDescriptor:(id)descriptor completionHandler:(void (^)(id,id))handler;
@end
@implementation TeguLibrary (APISurface)
- (NSUInteger)type { return 0; }
- (id)installName { return 0; }
- (void)newFunctionWithDescriptor:(id)descriptor completionHandler:(void (^)(id,id))handler {
 id error=0;id function=[self newFunctionWithDescriptor:descriptor error:&error];
 if(handler)handler(function,error);
 [function release];
}
@end
@interface TeguPipeline (APISurface)
- (signed char)supportIndirectCommandBuffers;
@end
@implementation TeguPipeline (APISurface)
- (signed char)supportIndirectCommandBuffers { return 0; }
@end
#endif

/* ------------------------------------------------------------------------- */
/* Indirect draws with CPU-written arguments.  A GPU/blit/compute-produced argument buffer has no
 * encode-time value and stays refused; a stable one is read now, drawn through the ordinary direct
 * draw, and verified again before the command runs (api_events.inc). */
#ifdef TEGU_GRAPHICS_BRIDGE
static int tegu_indirect_args_gpu_written(TeguCommand *command,TeguBuffer *buffer) {
 for(unsigned i=0;i<command->count;i++) {
  if(!command->jobChunks[i/TEGU_JOB_CHUNK])continue;
  const struct Dispatch *j=&TEGU_JOB(command->jobChunks,i);
  if(j->blit){if(j->buffers[1]==buffer)return 1;continue;}
#ifdef TEGU_MALI_BRIDGE
  if((j->eventKind&3u)==TEGU_EVENT_INDIRECT&&j->eventObject) {
   const TeguIndirectDispatch *s=(const TeguIndirectDispatch *)j->eventObject;
   for(unsigned k=0;k<31;k++)if(s->buffers[k]==buffer)return 1;
   continue;
  }
#endif
  if(j->pipeline&&!j->renderPipeline) {
   for(unsigned k=0;k<8;k++)if(j->buffers[k]==buffer)return 1;
   for(unsigned k=0;k<31;k++)if(j->v2Buffers[k]==buffer)return 1;
  }
 }
 return 0;
}
static void tegu_render_indirect_draw(TeguRenderEncoder *e,NSUInteger type,id buffer,NSUInteger offset,int indexed,
 NSUInteger indexType,TeguBuffer *indexBuffer,NSUInteger indexOffset) {
 if(e->inert)return;
 TeguCommand *command=e->command;unsigned words=indexed?5u:4u;
 if(e->ended||[command teguCommitted]||!buffer||!tegu_api_class_is(buffer,"TeguBuffer",0)||
    ((TeguBuffer *)buffer)->owner!=command->queue->owner||offset%4||offset>((TeguBuffer *)buffer)->logical||
    ((TeguBuffer *)buffer)->logical-offset<words*4u||(indexed&&(indexType>1||!indexBuffer))){TEGU_INVALID(command);return;}
 TeguBuffer *args=(TeguBuffer *)buffer;
 if(tegu_indirect_args_gpu_written(command,args)){TEGU_API_REFUSE(command,"indirect_draw_gpu_arguments");return;}
 const unsigned char *bytes=(const unsigned char *)tegu_buffer_bytes(args);
 struct TeguIndirectVerify *node=bytes?calloc(1,sizeof(*node)):0;
 if(!node){TEGU_INVALID(command);return;}
 __builtin_memcpy(node->expected,bytes+offset,words*4u);
 node->buffer=[args retain];node->offset=offset;node->words=words;
 node->next=(struct TeguIndirectVerify *)command->indirectVerifies;command->indirectVerifies=node;
 const uint32_t *a=node->expected;
 if(!a[0]||!a[1])return; /* zero vertices/indices or instances draw nothing */
 if(!indexed) {
  [e drawPrimitives:type vertexStart:a[2] vertexCount:a[0] instanceCount:a[1] baseInstance:a[3]];
 } else {
  NSUInteger size=indexType?4u:2u;
  [e drawIndexedPrimitives:type indexCount:a[0] indexType:indexType indexBuffer:indexBuffer
   indexBufferOffset:indexOffset+(NSUInteger)a[2]*size instanceCount:a[1] baseVertex:(long)(int32_t)a[3] baseInstance:a[4]];
 }
}
#endif

/* ------------------------------------------------------------------------- */
/* Textures over an MTLBuffer (-[MTLBuffer newTextureWithDescriptor:offset:bytesPerRow:]): no allocation;
 * the texture retains the app's buffer as its backing and describes level 0 at (offset, bytesPerRow), so
 * CPU access, sampling and rendering see the buffer's own memory, in both directions.  Admitted shape:
 * a 2D, single-level, single-sample, uncompressed colour texture whose offset and row pitch honour the
 * device's linear alignment, that fits the buffer, in the buffer's storage mode (Metal's own rules plus
 * what the n2t layout can address).  Anything else is nil with a trace. */
#ifdef TEGU_GRAPHICS_BRIDGE
static id tegu_buffer_make_texture(TeguBuffer *buffer,id descriptor,NSUInteger offset,NSUInteger bytesPerRow) {
 if(!buffer||!tegu_heap_texture_descriptor_ready(descriptor)){tegu_api_unsupported("buffer_texture_descriptor");return 0;}
 TeguTextureDescriptor *d=(TeguTextureDescriptor *)descriptor;
 NSUInteger format=[d pixelFormat],width=[d width],height=[d height];
 NSUInteger bpp=tegu_texture_bytes_per_pixel(format);
 if([d textureType]!=2||[d mipmapLevelCount]!=1||[d depth]!=1||[d arrayLength]!=1||[d sampleCount]!=1||
    !bpp||tegu_texture_is_astc(format)||!width||!height||width>16384||height>16384){
  tegu_api_unsupported("buffer_texture_shape");return 0;
 }
 NSUInteger align=[buffer->owner minimumLinearTextureAlignmentForPixelFormat:format];
 if(!align||offset%align||bytesPerRow%align||bytesPerRow<width*bpp||bytesPerRow>(~(NSUInteger)0)/height){
  tegu_api_unsupported("buffer_texture_alignment");return 0;
 }
 NSUInteger span=bytesPerRow*(height-1)+width*bpp;
 if(offset>buffer->logical||span>buffer->logical-offset){tegu_api_unsupported("buffer_texture_extent");return 0;}
 NSUInteger options=[d resourceOptions];
 if(((options>>4)&15)!=((buffer->bufferResourceOptions>>4)&15)){tegu_api_unsupported("buffer_texture_storage");return 0;}
 TeguTexture *t=[[TeguTexture alloc] init];if(!t)return 0;
 t->textureWidth=width;t->textureHeight=height;t->textureDepth=1;t->textureKind=2;t->textureSamples=1;t->format=format;
 t->textureUsage=[d usage];t->textureResourceOptions=options;t->rowBytes=bytesPerRow;t->levelCount=1;t->arrayLength=1;t->layers=1;
 t->mip[0]=(struct tegu_texture_level){width,height,bytesPerRow,offset};
 t->levelDepth[0]=1;t->levelSliceStride[0]=0;t->levelLayerStride[0]=0;
 t->buffer=[buffer retain];
 t->bufferBacked=1;t->bufferBackedOffset=offset;t->bufferBackedRow=bytesPerRow;
 return t;
}
@interface TeguTexture (APISurfaceBuffer)
- (id)buffer;
- (NSUInteger)bufferOffset;
- (NSUInteger)bufferBytesPerRow;
@end
@implementation TeguTexture (APISurfaceBuffer)
- (id)buffer { return bufferBacked?buffer:0; }
- (NSUInteger)bufferOffset { return bufferBacked?bufferBackedOffset:0; }
- (NSUInteger)bufferBytesPerRow { return bufferBacked?bufferBackedRow:0; }
@end
#else
static id tegu_buffer_make_texture(TeguBuffer *buffer,id descriptor,NSUInteger offset,NSUInteger bytesPerRow) {
 (void)buffer;(void)descriptor;(void)offset;(void)bytesPerRow;tegu_api_unsupported("buffer_texture");return 0;
}
#endif

/* ------------------------------------------------------------------------- */
/* newBufferWithBytesNoCopy (reference plugin semantics, ansuz CA-METAL 10.4).  The kernel cannot import
 * user pages, so the buffer has a GPU-side copy (the BO) next to the caller's memory:
 *  - -contents is the CALLER's pointer (Metal's guarantee), and the deallocator runs when the buffer dies;
 *  - before a command executes, the caller's bytes are copied into the BO of every no-copy buffer (and
 *    every texture over one) the command binds;
 *  - after the command completes, blit destinations are copied back to the caller's memory, only the range
 *    the blit wrote, so an app writing other parts of the same memory concurrently is not clobbered.
 * Not copied back: writes by compute shaders (no per-binding write information at this layer). */
static unsigned tegu_nocopy_live,tegu_nocopy_epochs;
static void tegu_nocopy_refresh(TeguBuffer *b,unsigned epoch) {
 if(!b||!b->external||b->externalEpoch==epoch)return;
 unsigned char *dst=tegu_buffer_bytes(b);
 if(dst)__builtin_memcpy(dst,b->external,b->logical<b->externalLength?b->logical:b->externalLength);
 b->externalEpoch=epoch;
}
#ifdef TEGU_GRAPHICS_BRIDGE
static void tegu_nocopy_refresh_texture(id texture,unsigned epoch) {
 if(texture&&tegu_api_class_is(texture,"TeguTexture",0))tegu_nocopy_refresh(((TeguTexture *)texture)->buffer,epoch);
}
#endif
static void tegu_nocopy_sync_in(TeguCommand *command) {
 if(!__atomic_load_n(&tegu_nocopy_live,__ATOMIC_RELAXED))return;
 unsigned epoch=__atomic_add_fetch(&tegu_nocopy_epochs,1u,__ATOMIC_RELAXED);
 for(unsigned i=0;i<command->count;i++) {
  if(!command->jobChunks[i/TEGU_JOB_CHUNK])continue;
  const struct Dispatch *j=&TEGU_JOB(command->jobChunks,i);
  for(unsigned k=0;k<8;k++)tegu_nocopy_refresh(j->buffers[k],epoch);
  for(unsigned k=0;k<31;k++)tegu_nocopy_refresh(j->v2Buffers[k],epoch);
#ifdef TEGU_GRAPHICS_BRIDGE
  for(unsigned k=0;k<31;k++){
   tegu_nocopy_refresh(j->vertexBuffers[k],epoch);tegu_nocopy_refresh(j->compositorVertexBuffers[k],epoch);
   tegu_nocopy_refresh(j->compositorFragmentBuffers[k],epoch);
   tegu_nocopy_refresh_texture(j->compositorTextures[k],epoch);tegu_nocopy_refresh_texture(j->v2Textures[k],epoch);
  }
  tegu_nocopy_refresh(j->expandIndex,epoch);tegu_nocopy_refresh(j->expandSource,epoch);tegu_nocopy_refresh(j->expandOut,epoch);
  tegu_nocopy_refresh(j->attachmentClearBuffers[0],epoch);tegu_nocopy_refresh(j->attachmentClearBuffers[1],epoch);
  tegu_nocopy_refresh_texture(j->sampledTexture,epoch);tegu_nocopy_refresh_texture(j->renderTexture,epoch);
#endif
#ifdef TEGU_MALI_BRIDGE
  if((j->eventKind&3u)==TEGU_EVENT_INDIRECT&&j->eventObject) {
   const TeguIndirectDispatch *s=(const TeguIndirectDispatch *)j->eventObject;
   for(unsigned k=0;k<31;k++)tegu_nocopy_refresh(s->buffers[k],epoch);
   tegu_nocopy_refresh(s->indirect,epoch);
  }
#endif
 }
}
/* Copy back the bytes a CPU blit wrote into a no-copy buffer. */
static void tegu_nocopy_sync_out(TeguCommand *command) {
 if(!__atomic_load_n(&tegu_nocopy_live,__ATOMIC_RELAXED))return;
 for(unsigned i=0;i<command->count;i++) {
  if(!command->jobChunks[i/TEGU_JOB_CHUNK])continue;
  const struct Dispatch *j=&TEGU_JOB(command->jobChunks,i);
  TeguBuffer *b=j->blit?j->buffers[1]:0;
  if(!b||!b->external)continue;
  NSUInteger start=j->destinationOffset,span;
  if(j->blit==1||j->blit==2)span=j->byteCount;
  else if(j->blit==3||j->blit==5)span=j->rows>1?(j->rows-1)*j->destinationStride+j->byteCount:j->byteCount;
  else {start=0;span=b->logical;} /* mip generation: the written levels are not tracked, refresh everything */
  NSUInteger limit=b->logical<b->externalLength?b->logical:b->externalLength;
  if(start>=limit)continue;
  if(span>limit-start)span=limit-start;
  const unsigned char *src=(const unsigned char *)tegu_buffer_bytes(b);
  if(src&&span)__builtin_memcpy(b->external+start,src+start,span);
 }
}
static void tegu_buffer_external_free(TeguBuffer *b) {
 if(!b->external)return;
 __atomic_fetch_sub(&tegu_nocopy_live,1u,__ATOMIC_RELAXED);
 if(b->externalDeallocator) {
  ((void (^)(void *,NSUInteger))b->externalDeallocator)(b->external,b->externalLength);
  if(tegu_event_ops.blockRelease)tegu_event_ops.blockRelease(b->externalDeallocator);
 }
 b->external=0;b->externalDeallocator=0;
}
static id tegu_device_new_nocopy(TeguComputeDevice *device,void *bytes,NSUInteger length,NSUInteger options,
 void (^deallocator)(void *,NSUInteger)) {
 /* Metal: page-aligned memory, shared storage (private/managed/memoryless cannot alias CPU memory). */
 if(!bytes||!length||((NSUInteger)bytes&16383u)||((options>>4)&15)!=0||!tegu_buffer_options_ok(options)){
  tegu_api_unsupported("buffer_nocopy_shape");return 0;
 }
 void *block=0;
 if(deallocator) {
  if(!tegu_event_ops_ready()||!tegu_event_ops.blockCopy||!tegu_event_ops.blockRelease){tegu_api_unsupported("buffer_nocopy_deallocator");return 0;}
  block=tegu_event_ops.blockCopy(deallocator);
  if(!block)return 0;
 }
 TeguBuffer *b=[device newBufferWithLength:length options:0];
 if(!b){if(block)tegu_event_ops.blockRelease(block);return 0;}
 unsigned char *bo=tegu_buffer_bytes(b);
 if(!bo){[b release];if(block)tegu_event_ops.blockRelease(block);return 0;}
 __builtin_memcpy(bo,bytes,length);
 b->bufferResourceOptions=options;b->external=bytes;b->externalLength=length;b->externalDeallocator=block;
 __atomic_fetch_add(&tegu_nocopy_live,1u,__ATOMIC_RELAXED);
 return b;
}

/* ------------------------------------------------------------------------- */
/* Field inventory of what clients really call: a selector none of our classes implements is about to
 * kill the process (unrecognized selector).  +resolveInstanceMethod: runs just before that (and for
 * respondsToSelector: probes); it only LOGS "TEGU_UNRECOGNIZED class sel" (bounded to 256 lines, repeats suppressed; respondsToSelector: probes appear too)
 * and answers NO exactly as NSObject does, so behaviour is unchanged. */
@interface NSObject (TeguResolver)
+ (signed char)resolveInstanceMethod:(SEL)selector;
@end
extern const char *sel_getName(SEL);
extern const char *class_getName(id);
static unsigned tegu_unrecognized_count;
static void tegu_api_note_unrecognized(id cls,SEL selector) {
 static SEL recent[16];static unsigned recentNext;
 for(unsigned i=0;i<16;i++)if(__atomic_load_n(&recent[i],__ATOMIC_RELAXED)==selector)return;
 __atomic_store_n(&recent[__atomic_fetch_add(&recentNext,1u,__ATOMIC_RELAXED)&15u],selector,__ATOMIC_RELAXED);
 if(__atomic_fetch_add(&tegu_unrecognized_count,1u,__ATOMIC_RELAXED)>=256)return;
 char line[240];int n=snprintf(line,sizeof(line),"TEGU_UNRECOGNIZED class=%s sel=%s\n",
  cls?class_getName(cls):"?",selector?sel_getName(selector):"?");
 if(n>0)write(2,line,(NSUInteger)n<sizeof(line)?(NSUInteger)n:sizeof(line)-1);
}
#ifdef TEGU_METAL_BASE
#define TEGU_API_RESOLVER_SUPER(c) return [super resolveInstanceMethod:selector]
#else
#define TEGU_API_RESOLVER_SUPER(c) return 0
#endif
#define TEGU_API_RESOLVER(c) @implementation c (APIUnrecognized) \
 + (signed char)resolveInstanceMethod:(SEL)selector { tegu_api_note_unrecognized((id)self,selector);TEGU_API_RESOLVER_SUPER(c); } \
 @end
TEGU_API_RESOLVER(TeguComputeDevice) TEGU_API_RESOLVER(TeguQueue) TEGU_API_RESOLVER(TeguCommand) TEGU_API_RESOLVER(TeguEncoder)
TEGU_API_RESOLVER(TeguBlitEncoder) TEGU_API_RESOLVER(TeguBuffer) TEGU_API_RESOLVER(TeguPipeline) TEGU_API_RESOLVER(TeguHeap)
TEGU_API_RESOLVER(TeguLibrary) TEGU_API_RESOLVER(TeguFunction) TEGU_API_RESOLVER(TeguSampler) TEGU_API_RESOLVER(TeguDepthStencilState)
TEGU_API_RESOLVER(TeguFence) TEGU_API_RESOLVER(TeguEvent) TEGU_API_RESOLVER(TeguSharedEvent) TEGU_API_RESOLVER(TeguBinaryArchive)
#ifdef TEGU_GRAPHICS_BRIDGE
TEGU_API_RESOLVER(TeguRenderEncoder) TEGU_API_RESOLVER(TeguTexture) TEGU_API_RESOLVER(TeguRenderPipeline)
#endif

#ifdef TEGU_GRAPHICS_BRIDGE
#include "api_encoder_refusals.inc"
#include "api_tess.inc"
#include "api_argbuf.inc"
#endif
#include "api_parallel.inc"
