/* A bounded automatic-heap facade for the resource classes this frontend
 * already owns.  Resources keep their normal independent kernel allocations;
 * the heap supplies Metal-compatible budget and lifetime semantics without
 * advertising placement, sparse backing, aliasing, or purgeability. */
typedef struct { NSUInteger size,align; } TeguSizeAndAlign;

@interface NSObject (TeguHeapDescriptorSelectors)
- (signed char)respondsToSelector:(SEL)selector;
- (NSUInteger)size;
- (NSUInteger)storageMode;
- (NSUInteger)cpuCacheMode;
- (NSUInteger)hazardTrackingMode;
- (NSUInteger)resourceOptions;
- (NSUInteger)type;
- (NSUInteger)sparsePageSize;
- (void)teguReleaseHeapCharge:(NSUInteger)bytes;
@end

@interface TeguHeap : NSObject {
@public TeguComputeDevice *owner; id debugLabel;
 NSUInteger heapSize,usedBytes,storage,cpuCache,hazard,options,heapType;
}
- (id)device;
- (id)label;
- (void)setLabel:(id)value;
- (NSUInteger)storageMode;
- (NSUInteger)cpuCacheMode;
- (NSUInteger)hazardTrackingMode;
- (NSUInteger)resourceOptions;
- (NSUInteger)size;
- (NSUInteger)usedSize;
- (NSUInteger)currentAllocatedSize;
- (NSUInteger)type;
- (NSUInteger)maxAvailableSizeWithAlignment:(NSUInteger)alignment;
- (id)newBufferWithLength:(NSUInteger)length options:(NSUInteger)resourceOptions;
- (id)newTextureWithDescriptor:(TeguTextureDescriptor *)descriptor;
- (id)newBufferWithLength:(NSUInteger)length options:(NSUInteger)resourceOptions offset:(NSUInteger)offset;
- (id)newTextureWithDescriptor:(TeguTextureDescriptor *)descriptor offset:(NSUInteger)offset;
- (NSUInteger)setPurgeableState:(NSUInteger)state;
- (void)teguReleaseHeapCharge:(NSUInteger)bytes;
@end

#ifdef TEGU_RENDER_TRACE
static void tegu_heap_trace(const char *phase,NSUInteger size,NSUInteger storage,
 NSUInteger cpu,NSUInteger hazard,NSUInteger options,NSUInteger type,NSUInteger sparse,int result) {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=24)return;
 char line[240];int n=snprintf(line,sizeof(line),
  "TEGU_HEAP phase=%s size=%llu storage=%llu cpu=%llu hazard=%llu options=0x%llx type=%llu sparse=%llu result=%d\n",
  phase,(U64)size,(U64)storage,(U64)cpu,(U64)hazard,(U64)options,(U64)type,(U64)sparse,result);
 if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
}
#else
#define tegu_heap_trace(phase,size,storage,cpu,hazard,options,type,sparse,result) ((void)0)
#endif

static int tegu_heap_has_selector(id object,SEL selector) {
 return object&&[object respondsToSelector:@selector(respondsToSelector:)]&&
        [object respondsToSelector:selector];
}

static TeguSizeAndAlign tegu_heap_buffer_size(NSUInteger length,NSUInteger options) {
 TeguSizeAndAlign result={0,0};
 if(!length||length>(16UL<<20)||(options!=0&&options!=0x20))return result;
 if(length>~(NSUInteger)0-16383)return result;
 result.size=(length+16383)&~(NSUInteger)16383;result.align=16384;return result;
}

static int tegu_heap_texture_descriptor_ready(id d) {
 SEL required[]={@selector(textureType),@selector(pixelFormat),@selector(width),@selector(height),
  @selector(depth),@selector(mipmapLevelCount),@selector(sampleCount),@selector(arrayLength),
  @selector(resourceOptions),@selector(usage)};
 for(unsigned i=0;i<sizeof(required)/sizeof(required[0]);i++)if(!tegu_heap_has_selector(d,required[i]))return 0;
 return 1;
}

static TeguSizeAndAlign tegu_heap_texture_size(TeguComputeDevice *device,TeguTextureDescriptor *d) {
 TeguSizeAndAlign result={0,0};
 if(!tegu_heap_texture_descriptor_ready(d))return result;
 struct tegu_texture_level layout[13];NSUInteger bytes=0;
 if(!tegu_texture_layout_for_descriptor(device,d,layout,&bytes,0))return result;
 if(bytes>~(NSUInteger)0-16383)return result;
 result.size=(bytes+16383)&~(NSUInteger)16383;result.align=16384;return result;
}

static int tegu_heap_reserve(TeguHeap *heap,NSUInteger bytes) {
 NSUInteger old=__atomic_load_n(&heap->usedBytes,__ATOMIC_ACQUIRE);
 for(;;) {
  if(bytes>heap->heapSize||old>heap->heapSize-bytes)return 0;
  if(__atomic_compare_exchange_n(&heap->usedBytes,&old,old+bytes,0,
      __ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 1;
 }
}

@interface TeguComputeDevice (Heaps)
- (TeguSizeAndAlign)heapBufferSizeAndAlignWithLength:(NSUInteger)length options:(NSUInteger)options;
- (TeguSizeAndAlign)heapTextureSizeAndAlignWithDescriptor:(TeguTextureDescriptor *)descriptor;
- (id)newHeapWithDescriptor:(id)descriptor;
@end

@implementation TeguComputeDevice (Heaps)
- (TeguSizeAndAlign)heapBufferSizeAndAlignWithLength:(NSUInteger)length options:(NSUInteger)options {
 TeguSizeAndAlign result=tegu_heap_buffer_size(length,options);
 tegu_heap_trace("buffer_query",length,(options>>4)&15,options&15,(options>>8)&3,options,0,0,result.size!=0);
 return result;
}
- (TeguSizeAndAlign)heapTextureSizeAndAlignWithDescriptor:(TeguTextureDescriptor *)descriptor {
 TeguSizeAndAlign result=tegu_heap_texture_size(self,descriptor);
 NSUInteger options=tegu_heap_texture_descriptor_ready(descriptor)?[descriptor resourceOptions]:0;
 tegu_heap_trace("texture_query",result.size,(options>>4)&15,options&15,(options>>8)&3,options,0,0,result.size!=0);
 (void)options;
 return result;
}
- (id)newHeapWithDescriptor:(id)d {
 SEL required[]={@selector(size),@selector(storageMode),@selector(cpuCacheMode),
  @selector(hazardTrackingMode),@selector(resourceOptions),@selector(type)};
 for(unsigned i=0;i<sizeof(required)/sizeof(required[0]);i++)if(!tegu_heap_has_selector(d,required[i])) {
  tegu_heap_trace("create",0,0,0,0,0,0,0,0);return 0;
 }
 NSUInteger size=[d size],storage=[d storageMode],cpu=[d cpuCacheMode];
 NSUInteger hazard=[d hazardTrackingMode],options=[d resourceOptions],type=[d type];
 NSUInteger sparse=tegu_heap_has_selector(d,@selector(sparsePageSize))?[d sparsePageSize]:0;
 NSUInteger packed=(storage<<4)|cpu|(hazard<<8);
 int valid=size&&size<=(128UL<<20)&&(storage==0||storage==2)&&cpu==0&&hazard==0&&
   type==0&&options==packed&&!(options&~(NSUInteger)0x33f)&&size<=~(NSUInteger)0-16383;
 if(!valid){tegu_heap_trace("create",size,storage,cpu,hazard,options,type,sparse,0);return 0;}
 (void)sparse;
 TeguHeap *heap=[[TeguHeap alloc] init];if(!heap)return 0;
 heap->owner=[self retain];heap->heapSize=(size+16383)&~(NSUInteger)16383;
 heap->storage=storage;heap->cpuCache=cpu;heap->hazard=hazard;heap->options=options;heap->heapType=type;
 tegu_heap_trace("create",heap->heapSize,storage,cpu,hazard,options,type,sparse,1);return heap;
}
@end

@implementation TeguHeap
- (id)device{return owner;}
- (id)label{return debugLabel;}
- (void)setLabel:(id)value{id copy=[value copy];[debugLabel release];debugLabel=copy;}
- (NSUInteger)storageMode{return storage;}
- (NSUInteger)cpuCacheMode{return cpuCache;}
- (NSUInteger)hazardTrackingMode{return hazard;}
- (NSUInteger)resourceOptions{return options;}
- (NSUInteger)size{return heapSize;}
- (NSUInteger)usedSize{return __atomic_load_n(&usedBytes,__ATOMIC_ACQUIRE);}
- (NSUInteger)currentAllocatedSize{return [self usedSize];}
- (NSUInteger)type{return heapType;}
- (NSUInteger)maxAvailableSizeWithAlignment:(NSUInteger)alignment {
 if(alignment&&(alignment&(alignment-1)))return 0;
 NSUInteger used=[self usedSize],available=used<=heapSize?heapSize-used:0;
 return alignment?available&~(alignment-1):available;
}
- (id)newBufferWithLength:(NSUInteger)length options:(NSUInteger)resourceOptions {
 if(resourceOptions!=options) {tegu_heap_trace("buffer_refused",length,storage,cpuCache,hazard,resourceOptions,heapType,0,0);return 0;}
 TeguSizeAndAlign required=tegu_heap_buffer_size(length,resourceOptions);if(!required.size||!tegu_heap_reserve(self,required.size))return 0;
 TeguBuffer *buffer=[owner newBufferWithLength:length options:0];
 if(!buffer||buffer->allocated!=required.size) {if(buffer)[buffer release];[self teguReleaseHeapCharge:required.size];return 0;}
 buffer->bufferResourceOptions=options;buffer->heapOwner=[self retain];buffer->heapCharge=required.size;return buffer;
}
- (id)newTextureWithDescriptor:(TeguTextureDescriptor *)descriptor {
 if(!tegu_heap_texture_descriptor_ready(descriptor)) {tegu_heap_trace("texture_refused",0,storage,cpuCache,hazard,0,heapType,0,0);return 0;}
 if([descriptor resourceOptions]!=options) {tegu_heap_trace("texture_refused",0,storage,cpuCache,hazard,[descriptor resourceOptions],heapType,0,0);return 0;}
 TeguSizeAndAlign required=tegu_heap_texture_size(owner,descriptor);if(!required.size||!tegu_heap_reserve(self,required.size))return 0;
 TeguTexture *texture=[owner newTextureWithDescriptor:descriptor];
 if(!texture||!texture->buffer||texture->buffer->allocated!=required.size) {
  if(texture)[texture release];[self teguReleaseHeapCharge:required.size];return 0;
 }
 texture->buffer->heapOwner=[self retain];texture->buffer->heapCharge=required.size;return texture;
}
- (id)newBufferWithLength:(NSUInteger)length options:(NSUInteger)resourceOptions offset:(NSUInteger)offset {
 (void)length;(void)resourceOptions;(void)offset;tegu_heap_trace("placement_buffer",heapSize,storage,cpuCache,hazard,options,heapType,0,0);return 0;
}
- (id)newTextureWithDescriptor:(TeguTextureDescriptor *)descriptor offset:(NSUInteger)offset {
 (void)descriptor;(void)offset;tegu_heap_trace("placement_texture",heapSize,storage,cpuCache,hazard,options,heapType,0,0);return 0;
}
- (NSUInteger)setPurgeableState:(NSUInteger)state {
 tegu_heap_trace("purge",heapSize,storage,cpuCache,hazard,options,heapType,0,state==1||state==2);return 2;
}
- (void)teguReleaseHeapCharge:(NSUInteger)bytes {
 NSUInteger old=__atomic_fetch_sub(&usedBytes,bytes,__ATOMIC_ACQ_REL);if(old<bytes)__builtin_trap();
}
- (void)dealloc{[debugLabel release];[owner release];[super dealloc];}
@end

@interface TeguBuffer (HeapResourceQueries)
- (NSUInteger)heapOffset;
- (NSUInteger)allocatedSize;
- (NSUInteger)resourceOptions;
- (NSUInteger)storageMode;
- (NSUInteger)cpuCacheMode;
- (NSUInteger)hazardTrackingMode;
- (signed char)isAliasable;
- (void)makeAliasable;
- (NSUInteger)setPurgeableState:(NSUInteger)state;
@end
@implementation TeguBuffer (HeapResourceQueries)
- (NSUInteger)heapOffset{return 0;}
- (NSUInteger)allocatedSize{return allocated;}
- (NSUInteger)resourceOptions{return bufferResourceOptions;}
- (NSUInteger)storageMode{return (bufferResourceOptions>>4)&15;}
- (NSUInteger)cpuCacheMode{return bufferResourceOptions&15;}
- (NSUInteger)hazardTrackingMode{return (bufferResourceOptions>>8)&3;}
- (signed char)isAliasable{return 0;}
- (void)makeAliasable{}
- (NSUInteger)setPurgeableState:(NSUInteger)state{(void)state;return 2;}
@end

@interface TeguTexture (HeapResourceQueries)
- (NSUInteger)heapOffset;
- (NSUInteger)allocatedSize;
- (NSUInteger)hazardTrackingMode;
- (signed char)isAliasable;
- (void)makeAliasable;
- (NSUInteger)setPurgeableState:(NSUInteger)state;
@end
@implementation TeguTexture (HeapResourceQueries)
- (NSUInteger)heapOffset{return 0;}
- (NSUInteger)allocatedSize{return surfaceImported?surfaceAllocSize:(buffer?buffer->allocated:0);}
- (NSUInteger)hazardTrackingMode{return (textureResourceOptions>>8)&3;}
- (signed char)isAliasable{return 0;}
- (void)makeAliasable{}
- (NSUInteger)setPurgeableState:(NSUInteger)state{(void)state;return 2;}
@end
