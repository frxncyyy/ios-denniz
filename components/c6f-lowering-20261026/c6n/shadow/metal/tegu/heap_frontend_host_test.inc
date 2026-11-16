@interface HeapTestDescriptor : NSObject {
@public NSUInteger bytes,storage,cpu,hazard,options,kind,sparse;
}
@end
@implementation HeapTestDescriptor
- (signed char)respondsToSelector:(SEL)s {
 return s==@selector(respondsToSelector:)||s==@selector(size)||s==@selector(storageMode)||
  s==@selector(cpuCacheMode)||s==@selector(hazardTrackingMode)||s==@selector(resourceOptions)||s==@selector(type)||s==@selector(sparsePageSize);
}
- (NSUInteger)size{return bytes;}
- (NSUInteger)storageMode{return storage;}
- (NSUInteger)cpuCacheMode{return cpu;}
- (NSUInteger)hazardTrackingMode{return hazard;}
- (NSUInteger)resourceOptions{return options;}
- (NSUInteger)type{return kind;}
- (NSUInteger)sparsePageSize{return sparse;}
@end

@interface HeapTestForeignDescriptor : NSObject
@end
@implementation HeapTestForeignDescriptor
- (signed char)respondsToSelector:(SEL)s{return s==@selector(respondsToSelector:)||s==@selector(size);}
- (NSUInteger)size{return 16384;}
@end

struct HeapStressContext {TeguHeap *heap;unsigned iterations;};
static void *heap_stress_worker(void *opaque) {
 struct HeapStressContext *context=opaque;
 for(unsigned i=0;i<context->iterations;i++) {
  while(!tegu_heap_reserve(context->heap,16384)) {}
  [context->heap teguReleaseHeapCharge:16384];
 }
 return 0;
}

static void recover_failed_heap_free(TeguComputeDevice *device,TeguHeap *heap,U64 handle,NSUInteger bytes) {
 unsigned count=0;assert(!device->scalar(device->connection,2,&handle,1,0,&count));
 __atomic_fetch_sub(&device->allocatedBytes,bytes,__ATOMIC_RELAXED);[heap teguReleaseHeapCharge:bytes];
}

static void test_heap_frontend(TeguComputeDevice *device) {
 NSUInteger baseline=[device currentAllocatedSize];unsigned liveBaseline=live;
 HeapTestForeignDescriptor *foreign=[[HeapTestForeignDescriptor alloc] init];
 assert(![device newHeapWithDescriptor:foreign]);[foreign release];
 HeapTestDescriptor *d=[[HeapTestDescriptor alloc] init];d->bytes=(128UL<<20)+1;
 assert(![device newHeapWithDescriptor:d]);
 d->bytes=32768;d->cpu=1;d->options=1;assert(![device newHeapWithDescriptor:d]);
 d->cpu=0;d->hazard=1;d->options=0x100;assert(![device newHeapWithDescriptor:d]);
 d->hazard=0;d->kind=1;d->options=0;assert(![device newHeapWithDescriptor:d]);
 d->kind=0;d->storage=1;d->options=0x10;assert(![device newHeapWithDescriptor:d]);
 d->storage=0;d->options=0x20;assert(![device newHeapWithDescriptor:d]);
 d->options=0;d->bytes=32769;d->sparse=101;
 TeguHeap *heap=[device newHeapWithDescriptor:d];assert(heap&&[heap device]==device&&[heap size]==49152);
 assert(![heap storageMode]&&![heap cpuCacheMode]&&![heap hazardTrackingMode]&&![heap resourceOptions]&&![heap type]);
 assert(![heap usedSize]&&![heap currentAllocatedSize]&&[heap maxAvailableSizeWithAlignment:16384]==49152);
 assert(![heap maxAvailableSizeWithAlignment:3]);
 TestLabel *label=[[TestLabel alloc] init];label->value=77;[heap setLabel:label];label->value=99;[label release];
 assert(((TestLabel *)[heap label])->value==77);
 TeguSizeAndAlign bsize=[device heapBufferSizeAndAlignWithLength:100 options:0];
 assert(bsize.size==16384&&bsize.align==16384);
 assert(![device heapBufferSizeAndAlignWithLength:~(NSUInteger)0 options:0].size);
 assert([device heapBufferSizeAndAlignWithLength:100 options:0x20].size==16384);
 TeguBuffer *buffer=[heap newBufferWithLength:100 options:0];assert(buffer&&[buffer heap]==heap&&[buffer device]==device);
 assert([heap usedSize]==16384&&[heap currentAllocatedSize]==16384&&[heap maxAvailableSizeWithAlignment:16384]==32768);
 TeguBuffer *large=[heap newBufferWithLength:16385 options:0];assert(large&&[heap usedSize]==49152);
 assert(![heap newBufferWithLength:1 options:0]);[large release];assert([heap usedSize]==16384);
 assert(![heap newBufferWithLength:1 options:0x20]);
 assert(![heap newBufferWithLength:1 options:0 offset:0]);
 assert([heap setPurgeableState:1]==2&&[heap setPurgeableState:2]==2);
 assert([heap setPurgeableState:3]==2&&[heap setPurgeableState:4]==2&&[heap setPurgeableState:99]==2);
 assert(![buffer heapOffset]&&[buffer allocatedSize]==16384&&![buffer resourceOptions]&&
   ![buffer storageMode]&&![buffer cpuCacheMode]&&![buffer hazardTrackingMode]&&![buffer isAliasable]);
 [buffer makeAliasable];assert(![buffer isAliasable]);
 assert([buffer setPurgeableState:1]==2&&[buffer setPurgeableState:3]==2);
 [heap release];TeguHeap *held=[[buffer heap] retain];assert(held&&[held device]==device); /* resource retained the externally released heap */
 [buffer release];heap=held;assert([device currentAllocatedSize]==baseline);

 /* A released resource returns its budget and permits another allocation. */
 TeguBuffer *one=[heap newBufferWithLength:1 options:0];assert(one&&[heap usedSize]==16384);
 [one release];assert(![heap usedSize]);
 failMap=1;assert(![heap newBufferWithLength:1 options:0]&&![heap usedSize]);failMap=0;
 allocationExtra=16384;assert(![heap newBufferWithLength:1 options:0]&&![heap usedSize]);allocationExtra=0;
 struct HeapStressContext stress={heap,2000};pthread_t threads[4];
 for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],0,heap_stress_worker,&stress));
 for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],0));assert(![heap usedSize]);
 TeguBuffer *failed=[heap newBufferWithLength:1 options:0];assert(failed);U64 failedHandle=failed->handle;NSUInteger failedBytes=failed->allocated;
 failFree=1;[failed release];failFree=0;assert([heap usedSize]==failedBytes&&[device currentAllocatedSize]==baseline+failedBytes);
 recover_failed_heap_free(device,heap,failedHandle,failedBytes);assert(![heap usedSize]&&[device currentAllocatedSize]==baseline);
 [heap setLabel:0];[heap release]; /* retained above only through the raw test hold */

 /* Private automatic heaps can own the exact private texture classes already
  * accepted by the device. Buffers remain unsupported because their public
  * CPU-visible contract cannot safely represent private storage. */
 d->bytes=1UL<<20;d->storage=2;d->options=0x20;
 TeguHeap *privateHeap=[device newHeapWithDescriptor:d];assert(privateHeap&&[privateHeap storageMode]==2);
 TeguBuffer *privateBuffer=[privateHeap newBufferWithLength:16 options:0x20];
 assert(privateBuffer&&![privateBuffer contents]&&[privateBuffer heap]==privateHeap&&[privateBuffer resourceOptions]==0x20&&
   [privateBuffer storageMode]==2&&![privateBuffer cpuCacheMode]&&![privateBuffer hazardTrackingMode]);
 [privateBuffer makeAliasable];assert(![privateBuffer isAliasable]);[privateBuffer release];assert(![privateHeap usedSize]);
 TextureDesc *td=[[TextureDesc alloc] init];td->w=776;td->h=24;td->dep=1;td->format=43;td->levels=10;td->uses=5;td->options=0x20;
 U64 capabilities=device->capabilities;device->capabilities|=TEGU_GPU_CAP_COMPOSITOR;
 foreign=[[HeapTestForeignDescriptor alloc] init];
 assert(![device heapTextureSizeAndAlignWithDescriptor:(id)foreign].size);
 assert(![privateHeap newTextureWithDescriptor:(id)foreign]);[foreign release];
 TeguSizeAndAlign tsize=[device heapTextureSizeAndAlignWithDescriptor:(id)td];
 assert(tsize.size&&tsize.align==16384);
 TeguTexture *texture=[privateHeap newTextureWithDescriptor:(id)td];assert(texture&&[texture heap]==privateHeap&&[texture device]==device);
 assert([privateHeap usedSize]==tsize.size&&[privateHeap currentAllocatedSize]==tsize.size);
 assert(![texture heapOffset]&&[texture allocatedSize]==tsize.size&&[texture resourceOptions]==0x20&&
   [texture storageMode]==2&&![texture cpuCacheMode]&&![texture hazardTrackingMode]&&![texture isAliasable]);
 [texture makeAliasable];assert(![texture isAliasable]);
 assert([texture setPurgeableState:2]==2&&[texture setPurgeableState:4]==2);
 assert(![privateHeap newTextureWithDescriptor:(id)td offset:0]);
 td->options=0;assert(![privateHeap newTextureWithDescriptor:(id)td]);td->options=0x20;
 failMap=1;NSUInteger used=[privateHeap usedSize];assert(![privateHeap newTextureWithDescriptor:(id)td]&&[privateHeap usedSize]==used);failMap=0;
 allocationExtra=16384;assert(![privateHeap newTextureWithDescriptor:(id)td]&&[privateHeap usedSize]==used);allocationExtra=0;
 TeguTexture *failedTexture=[privateHeap newTextureWithDescriptor:(id)td];assert(failedTexture);
 U64 failedTextureHandle=failedTexture->buffer->handle;NSUInteger failedTextureBytes=failedTexture->buffer->allocated;
 failFree=1;[failedTexture release];failFree=0;assert([privateHeap usedSize]==used+failedTextureBytes);
 recover_failed_heap_free(device,privateHeap,failedTextureHandle,failedTextureBytes);assert([privateHeap usedSize]==used);
 td->w=~(NSUInteger)0;assert(![device heapTextureSizeAndAlignWithDescriptor:(id)td].size);td->w=776;
 [privateHeap release];privateHeap=[[texture heap] retain];[texture release];assert(![privateHeap usedSize]);
 [privateHeap release];[td release];device->capabilities=capabilities;
 [d release];drain();
 assert([device currentAllocatedSize]==baseline&&live==liveBaseline);
}
