#include "render_load.h"
#include "compositor_request.h"
#include "../nord2_metal_bridge.h"
/* Render-owned sidecar attached to a generic Dispatch via compositorPlan.
 * The target record uses the public n2mb_render_target ABI. */
#define TEGU_RENDER_SIDECAR_MAGIC 0x4e325336u /* "N2S6" */
#define TEGU_RENDER_SIDECAR_VERSION 1u
struct tegu_render_generic_sidecar {
 uint32_t magic,version,tls,count,msaa;
 struct n2mb_render_target targets[4];
 TeguAIRRenderBlend blends[4];
 uint32_t sample_counts[4];
 struct n2mb_resolve_target resolves[4];
};
/* Shared 2D textures; selectors10-12 keep16x16 and selector15 records bounded
 * 16..256 targets. All passes remain one clear+triangle draw. Unsupported
 * pass state fails the command before GPU submission. */
typedef struct {NSUInteger x,y,z;} TeguOrigin;
typedef struct {TeguOrigin origin;MTLSize size;} TeguRegion;
typedef struct {double red,green,blue,alpha;} TeguClearColor;
@interface TeguTextureDescriptor : NSObject
- (NSUInteger)textureType;
- (NSUInteger)pixelFormat;
- (NSUInteger)width;
- (NSUInteger)height;
- (NSUInteger)depth;
- (NSUInteger)mipmapLevelCount;
- (NSUInteger)sampleCount;
- (NSUInteger)arrayLength;
- (NSUInteger)resourceOptions;
- (NSUInteger)usage;
@end
struct tegu_texture_level {NSUInteger width,height,stride,offset;};
struct TeguIOSurfaceAPI {
 uint32_t (*getID)(void *);size_t (*getWidth)(void *),(*getHeight)(void *),(*getBytesPerRow)(void *),(*getAllocSize)(void *);
 uint32_t (*getPixelFormat)(void *);size_t (*getPlaneCount)(void *);
 int (*lock)(void *,uint32_t,uint32_t *);int (*unlock)(void *,uint32_t,uint32_t *);
 void *(*getBaseAddress)(void *);
 void *(*cfRetain)(void *);void (*cfRelease)(void *);
};
#ifdef TEGU_IOSURFACE_TEST_LOADER
void *tegu_test_iosurface_dlopen(const char *,int);
void *tegu_test_iosurface_dlsym(void *,const char *);
#define TEGU_IOSURFACE_DLOPEN(p,m) tegu_test_iosurface_dlopen((p),(m))
#define TEGU_IOSURFACE_DLSYM(h,n) tegu_test_iosurface_dlsym((h),(n))
#else
#define TEGU_IOSURFACE_DLOPEN(p,m) dlopen((p),(m))
#define TEGU_IOSURFACE_DLSYM(h,n) dlsym((h),(n))
#endif
static const struct TeguIOSurfaceAPI *tegu_iosurface_api(void) {
 static struct TeguIOSurfaceAPI api;static unsigned state;
 unsigned expected=0;
 if(__atomic_compare_exchange_n(&state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
  struct TeguIOSurfaceAPI loaded={0};
  void *h=TEGU_IOSURFACE_DLOPEN("/System/Library/Frameworks/IOSurface.framework/IOSurface",2);
  void *cf=TEGU_IOSURFACE_DLOPEN("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation",2);
  if(h&&cf){loaded.getID=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceGetID");loaded.getWidth=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceGetWidth");loaded.getHeight=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceGetHeight");
   loaded.getBytesPerRow=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceGetBytesPerRow");loaded.getAllocSize=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceGetAllocSize");
   loaded.getPixelFormat=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceGetPixelFormat");loaded.getPlaneCount=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceGetPlaneCount");
   loaded.lock=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceLock");loaded.unlock=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceUnlock");loaded.getBaseAddress=TEGU_IOSURFACE_DLSYM(h,"IOSurfaceGetBaseAddress");
   loaded.cfRetain=TEGU_IOSURFACE_DLSYM(cf,"CFRetain");loaded.cfRelease=TEGU_IOSURFACE_DLSYM(cf,"CFRelease");}
  if(loaded.getID&&loaded.getWidth&&loaded.getHeight&&loaded.getBytesPerRow&&loaded.getAllocSize&&
     loaded.getPixelFormat&&loaded.getPlaneCount&&loaded.lock&&loaded.unlock&&loaded.getBaseAddress&&loaded.cfRetain&&loaded.cfRelease)
   api=loaded;
  /* Framework loading/dlsym executes no Tegu callback; the acquire/release
   * state publishes the complete table once and never exposes partial entries. */
  __atomic_store_n(&state,2,__ATOMIC_RELEASE);
 } else while(__atomic_load_n(&state,__ATOMIC_ACQUIRE)!=2) {}
 return api.getID?&api:0;
}
#undef TEGU_IOSURFACE_DLOPEN
#undef TEGU_IOSURFACE_DLSYM
#define TEGU_IOSURFACE_RGBA 0x52474241u
#define TEGU_IOSURFACE_BGRA 0x42475241u
/* Keep descriptor refusals visible while discovering actual compositor needs. */
static void tegu_texture_refused(TeguTextureDescriptor *d) {
 static unsigned reported;
 if(!d||__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)>=8)return;
 const char prefix[]="TEGU_TEXTURE_REFUSED width,height,depth,format,options,mips,usage,type,samples,array=";
 NSUInteger values[10]={[d width],[d height],[d depth],[d pixelFormat],[d resourceOptions],
  [d mipmapLevelCount],[d usage],[d textureType],[d sampleCount],[d arrayLength]};
 char line[sizeof(prefix)-1+10*17];
 memcpy(line,prefix,sizeof(prefix)-1);
 for(unsigned i=0;i<10;i++) {
  NSUInteger at=sizeof(prefix)-1+i*17;
  for(unsigned j=0;j<16;j++)line[at+j]="0123456789abcdef"[(values[i]>>(60-j*4))&15];
  line[at+16]=i==9?'\n':',';
 }
 /* One write keeps a descriptor intact when several UI processes log. */
 write(2,line,sizeof(line));
}
static void tegu_shadow_trace(NSUInteger width,NSUInteger height,NSUInteger stride) {
#ifdef TEGU_RENDER_TRACE
 static unsigned reported;
 if(__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED)>=16)return;
 char line[128];int n=snprintf(line,sizeof(line),"TEGU_IOSURFACE_SHADOW %lux%lu row=%lu\n",
  (unsigned long)width,(unsigned long)height,(unsigned long)stride);
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
#else
 (void)width;(void)height;(void)stride;
#endif
}
#ifdef TEGU_RENDER_TRACE
/* Observe the actual IOSurface contract before any import gate rejects it.
 * Fixed-width hexadecimal fields and one write keep records intact when
 * several UI processes reach the driver concurrently. */
static void tegu_iosurface_import_trace(TeguTextureDescriptor *d,id surface,NSUInteger plane) {
 static unsigned reported;
 unsigned record=__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED);
 if(record>=32)return;
 const struct TeguIOSurfaceAPI *api=surface?tegu_iosurface_api():0;
 const char prefix[]="TEGU_IOSURFACE_IMPORT call,plane,desc,width,height,depth,format,options,mips,usage,type,samples,array,api,sid,swidth,sheight,fourcc,row,alloc,planes=";
 NSUInteger values[21]={record,plane,d!=0,
  d?[d width]:0,d?[d height]:0,d?[d depth]:0,d?[d pixelFormat]:0,
  d?[d resourceOptions]:0,d?[d mipmapLevelCount]:0,d?[d usage]:0,
  d?[d textureType]:0,d?[d sampleCount]:0,d?[d arrayLength]:0,api!=0,
  api?api->getID((void *)surface):0,api?api->getWidth((void *)surface):0,
  api?api->getHeight((void *)surface):0,api?api->getPixelFormat((void *)surface):0,
  api?api->getBytesPerRow((void *)surface):0,api?api->getAllocSize((void *)surface):0,
  api?api->getPlaneCount((void *)surface):0};
 char line[sizeof(prefix)-1+21*17];
 memcpy(line,prefix,sizeof(prefix)-1);
 for(unsigned i=0;i<21;i++) {
  NSUInteger at=sizeof(prefix)-1+i*17;
  for(unsigned j=0;j<16;j++)line[at+j]="0123456789abcdef"[(values[i]>>(60-j*4))&15];
  line[at+16]=i==20?'\n':',';
 }
 write(2,line,sizeof(line));
}
#else
#define tegu_iosurface_import_trace(d,s,p) do{}while(0)
#endif
static NSUInteger tegu_texture_bytes_per_pixel(NSUInteger format) {
 switch(format) {
  case 1:case 10:return 1; /* A8Unorm, R8Unorm */
  case 30:return 2; /* RG8Unorm */
  case 23:case 25:case 43:return 2; /* R16Uint, R16Float, packed BGR5A1 */
  case 55:case 70:case 71:case 80:case 81:case 550:case 554:return 4; /* R32Float / four-byte color */
  case 60:case 62:case 63:case 64:case 65:return 4; /* RG16 Unorm/Snorm/Uint/Sint/Float */
  case 253:return 1;case 252:return 4;case 260:return 8; /* Stencil8, Depth32Float, Depth32Float_Stencil8 */
  case 105:case 115:return 8; /* RG32Float / RGBA16Float */
  default:return 0;
 }
}
/* ASTC 2D (Metal 186-200 sRGB, 204-218 LDR): 16-byte blocks; 0 otherwise. */
static int tegu_texture_block_shape(NSUInteger format,unsigned *bw,unsigned *bh,unsigned *bytes);
static int tegu_texture_is_astc(NSUInteger format) {return (format>=186&&format<=200)||(format>=204&&format<=218);}

/* BackBoard's linear display IOSurface is both shader-readable and a render
 * target (5). Keep the earlier render-target-only contract (4) as well. */
static int tegu_iosurface_render_target_usage(NSUInteger usage) {
 return usage==4||usage==5;
}
static int tegu_r58_memoryless_msaa_descriptor(TeguComputeDevice *device,TeguTextureDescriptor *d) {
 return d&&(device->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&
   [d width]==594&&[d height]==594&&[d depth]==1&&[d pixelFormat]==80&&
   [d resourceOptions]==0x30&&[d mipmapLevelCount]==1&&[d usage]==4&&
   [d textureType]==4&&[d sampleCount]==4&&[d arrayLength]==1;
}
/* NSRange-compatible by value; kept local so this freestanding file does not
 * require Foundation. */
typedef struct {NSUInteger location,length;} TeguTextureViewRange;
@interface TeguTexture : NSObject {
@public TeguBuffer *buffer;id debugLabel,heapOwner;NSUInteger heapCharge; TeguComputeDevice *transientOwner,*importedOwner; id importedSurface; U64 importedHandle; signed char surfaceImported;uint32_t surfaceTargetVersion;
 /* Views retain the source object so an imported kernel handle or heap charge
  * cannot be released before every view. importedHandleShared suppresses the
  * per-view kernel release; the source owns that handle. */
 TeguTexture *viewSource;signed char importedHandleShared;
 /* IOSurface the kernel refused to import: the texture owns a GPU-reachable
  * copy, synchronised around each command buffer that uses it. */
 id shadowSurface;
 void (*releaseImportedSurface)(void *);NSUInteger textureWidth,textureHeight,textureDepth,textureKind,textureSamples,format,textureUsage,textureResourceOptions,rowBytes,levelCount,surfaceAllocSize,surfaceRowBytes;
 struct tegu_texture_level mip[13];
 /* v2 sidecar facts. The legacy mip record stays byte-compatible; these
  * parallel arrays add the dimensions/strides a N2T layout needs without
  * changing existing aggregate initializers or private-class comparisons. */
 NSUInteger arrayLength,layers;
 NSUInteger levelDepth[13],levelSliceStride[13],levelLayerStride[13];
}
- (id)device;
- (id)heap;
- (NSUInteger)pixelFormat;
- (NSUInteger)width;
- (NSUInteger)height;
- (void)getBytes:(void *)bytes bytesPerRow:(NSUInteger)stride fromRegion:(TeguRegion)region mipmapLevel:(NSUInteger)level;
- (void)getBytes:(void *)bytes bytesPerRow:(NSUInteger)stride bytesPerImage:(NSUInteger)imageStride
 fromRegion:(TeguRegion)region mipmapLevel:(NSUInteger)level slice:(NSUInteger)slice;
- (id)newTextureViewWithPixelFormat:(NSUInteger)viewFormat;
- (id)newTextureViewWithPixelFormat:(NSUInteger)viewFormat textureType:(NSUInteger)viewType
 levels:(TeguTextureViewRange)levels slices:(TeguTextureViewRange)slices;
@end
/* Resolve the CPU layer base for a Metal slice. Arrays/cube/MS arrays are
 * layer-major: the level's offset is layer-relative and levelLayerStride is
 * the same for all levels. 3D uses its own slice_stride from origin.z and
 * only slice0. MS readback/write is sample0 of the selected layer. */
static int tegu_texture_cpu_layer(TeguTexture *t,NSUInteger level,NSUInteger slice,
 NSUInteger *base,NSUInteger *depth) {
 if(!t||!base||!depth||level>=t->levelCount)return 0;
 struct tegu_texture_level m=t->mip[level];
 NSUInteger kind=t->textureKind;
 if(kind==3||kind==5||kind==6||kind==4||kind==8) {
  NSUInteger layers=t->layers?t->layers:1;
  NSUInteger stride=t->levelLayerStride[level];
  if(slice>=layers||(layers>1&&!stride))return 0;
  if(layers>1&&slice>(~(NSUInteger)0-m.offset)/stride)return 0;
  *base=m.offset+slice*stride;*depth=1;return 1;
 }
 if(slice)return 0;
 *base=m.offset;*depth=kind==7?(t->textureDepth>>level?t->textureDepth>>level:1u):1u;
 return 1;
}
@implementation TeguTexture
- (id)device{return surfaceImported?importedOwner:(buffer?buffer->owner:transientOwner);}
/* Zero-copy view creation.  The format alias policy is the shared audited
 * n2t_view_format_check; no descriptor or backing allocation is performed. */
static TeguTexture *tegu_texture_view_alloc(TeguTexture *src,NSUInteger viewFormat) {
 if(!src||!viewFormat)return 0;
 const struct TeguIOSurfaceAPI *api=tegu_iosurface_api();
 TeguTexture *v=[[TeguTexture alloc] init];if(!v)return 0;
 v->viewSource=[src retain];
 v->textureWidth=src->textureWidth;v->textureHeight=src->textureHeight;v->textureDepth=src->textureDepth;
 v->textureKind=src->textureKind;v->textureSamples=src->textureSamples;v->format=viewFormat;
 v->textureUsage=src->textureUsage;v->textureResourceOptions=src->textureResourceOptions;
 v->rowBytes=src->rowBytes;v->levelCount=src->levelCount;v->arrayLength=src->arrayLength;v->layers=src->layers;
 v->surfaceAllocSize=src->surfaceAllocSize;v->surfaceRowBytes=src->surfaceRowBytes;
 memcpy(v->mip,src->mip,sizeof(v->mip));
 memcpy(v->levelDepth,src->levelDepth,sizeof(v->levelDepth));
 memcpy(v->levelSliceStride,src->levelSliceStride,sizeof(v->levelSliceStride));
 memcpy(v->levelLayerStride,src->levelLayerStride,sizeof(v->levelLayerStride));
 v->buffer=[src->buffer retain];
 v->transientOwner=[src->transientOwner retain];
 if(src->heapOwner){v->heapOwner=[src->heapOwner retain];v->heapCharge=0;}
 v->debugLabel=[src->debugLabel copy];
 if(src->surfaceImported) {
  v->surfaceImported=1;v->surfaceTargetVersion=src->surfaceTargetVersion;
  v->importedOwner=[src->importedOwner retain];v->importedHandle=src->importedHandle;
  v->importedHandleShared=1;v->releaseImportedSurface=src->releaseImportedSurface;
  if(src->importedSurface) {
   if(!api||!api->cfRetain)goto fail;
   v->importedSurface=api->cfRetain((void *)src->importedSurface);
   if(!v->importedSurface)goto fail;
  }
 }
 if(src->shadowSurface) {
  if(!api||!api->cfRetain)goto fail;
  v->shadowSurface=api->cfRetain((void *)src->shadowSurface);
  if(!v->shadowSurface)goto fail;
  v->releaseImportedSurface=src->releaseImportedSurface;
 }
 return v;
fail:
 [v release];return 0;
}
- (id)newTextureViewWithPixelFormat:(NSUInteger)viewFormat {
 struct n2t_format_info allocation,view;
 if(!viewFormat||n2t_view_format_check((uint32_t)format,(uint32_t)viewFormat,&allocation,&view)!=N2T_OK)return 0;
 return tegu_texture_view_alloc(self,viewFormat);
}
- (id)newTextureViewWithPixelFormat:(NSUInteger)viewFormat textureType:(NSUInteger)viewType
 levels:(TeguTextureViewRange)levels slices:(TeguTextureViewRange)slices {
 NSUInteger fullSlices=textureKind==7?textureDepth:(layers?layers:1);
 if(!viewFormat||viewType!=textureKind||!levels.length||!slices.length||
    levels.location||slices.location||levels.length!=levelCount||slices.length!=fullSlices)return 0;
 return [self newTextureViewWithPixelFormat:viewFormat];
}
- (id)heap{return heapOwner?heapOwner:(buffer?buffer->heapOwner:0);}
- (id)iosurface{return surfaceImported?importedSurface:shadowSurface;}
- (NSUInteger)iosurfacePlane{return 0;}
- (id)label{return debugLabel;}
- (void)setLabel:(id)value{id copy=[value copy];[debugLabel release];debugLabel=copy;}
- (NSUInteger)textureType{return textureKind;}
- (NSUInteger)pixelFormat{return format;}
- (NSUInteger)width{return textureWidth;}
- (NSUInteger)height{return textureHeight;}
- (NSUInteger)depth{return textureDepth;}
- (NSUInteger)mipmapLevelCount{return levelCount;}
- (NSUInteger)sampleCount{return textureSamples;}
- (NSUInteger)arrayLength{return arrayLength?arrayLength:1;}
- (NSUInteger)storageMode{return (textureResourceOptions>>4)&15;}
- (NSUInteger)cpuCacheMode{return 0;}
- (NSUInteger)resourceOptions{return textureResourceOptions;}
- (NSUInteger)usage{return textureUsage;}
- (signed char)isFramebufferOnly{return 0;}
- (void)getBytes:(void *)bytes bytesPerRow:(NSUInteger)stride fromRegion:(TeguRegion)r mipmapLevel:(NSUInteger)level {
 [self getBytes:bytes bytesPerRow:stride bytesPerImage:0 fromRegion:r mipmapLevel:level slice:0];
}
- (void)getBytes:(void *)bytes bytesPerRow:(NSUInteger)stride bytesPerImage:(NSUInteger)imageStride
 fromRegion:(TeguRegion)r mipmapLevel:(NSUInteger)level slice:(NSUInteger)slice {
 if(textureResourceOptions){MARK("UNSUPPORTED private_texture_cpu_readback");return;}
#ifndef NORD2_JM_TRANSPORT
 if(surfaceImported&&surfaceTargetVersion){MARK("UNSUPPORTED compositor_iosurface_cpu_readback");return;}
#endif
 if(level>=levelCount){MARK("FAIL texture_read_level");return;}
 struct tegu_texture_level m=mip[level];
 NSUInteger bytesPerPixel=tegu_texture_bytes_per_pixel(format);
 NSUInteger layerBase=0,lvlDepth=0;
 if(!tegu_texture_cpu_layer(self,level,slice,&layerBase,&lvlDepth)){MARK("FAIL texture_read_layer");return;}
 if(!bytesPerPixel||r.size.width>(~(NSUInteger)0)/bytesPerPixel){MARK("FAIL texture_read_bounds");return;}
 NSUInteger rowByteCount=r.size.width*bytesPerPixel;
 /* Metal callers use zero bytesPerRow for tightly packed 1D transfers.  No
  * other admitted texture kind receives that ABI exception. */
 NSUInteger effectiveRowStride=stride?stride:(textureKind==0?rowByteCount:0);
 if(r.size.height&&effectiveRowStride>(~(NSUInteger)0)/r.size.height){MARK("FAIL texture_read_bounds");return;}
 NSUInteger minImageStride=effectiveRowStride*r.size.height;
 NSUInteger effectiveImageStride=imageStride?imageStride:minImageStride;
 if(!bytes||r.origin.x>m.width||r.origin.y>m.height||r.origin.z>lvlDepth||
    r.size.width>m.width-r.origin.x||r.size.height>m.height-r.origin.y||
    r.size.depth>lvlDepth-r.origin.z||(!stride&&textureKind!=0)||effectiveRowStride<rowByteCount||
    (imageStride&&imageStride<minImageStride)||
    (r.size.depth>1&&(!imageStride||effectiveImageStride>
      ((~(NSUInteger)0)-minImageStride)/(r.size.depth-1)))) {
  MARK("FAIL texture_read_bounds");return;
 }
 if(surfaceImported){const struct TeguIOSurfaceAPI *api=tegu_iosurface_api();uint32_t seed=0;
  if(r.origin.z||r.size.depth!=1){MARK("FAIL texture_read_bounds");return;}
  uint32_t expectedFourcc=surfaceTargetVersion?TEGU_IOSURFACE_BGRA:(format==70?TEGU_IOSURFACE_RGBA:TEGU_IOSURFACE_BGRA);
  NSUInteger minRow=surfaceTargetVersion?textureWidth*4:64;
  NSUInteger maxAlloc=surfaceTargetVersion?((16u<<20)+4095):(256u<<20);
  if(!api||api->getWidth((void *)importedSurface)!=textureWidth||api->getHeight((void *)importedSurface)!=textureHeight||
     api->getBytesPerRow((void *)importedSurface)!=surfaceRowBytes||api->getAllocSize((void *)importedSurface)!=surfaceAllocSize||
     api->getPixelFormat((void *)importedSurface)!=expectedFourcc||api->getPlaneCount((void *)importedSurface)!=0||
     (surfaceTargetVersion?(surfaceRowBytes<minRow||(surfaceRowBytes&63)):(surfaceRowBytes!=64))||
     surfaceAllocSize<4096||surfaceAllocSize>maxAlloc||(surfaceAllocSize&4095)||
     textureHeight>SIZE_MAX/surfaceRowBytes||textureHeight*surfaceRowBytes>surfaceAllocSize){MARK("FAIL iosurface_read_snapshot");return;}
  if(api->lock((void *)importedSurface,1,&seed)){MARK("FAIL iosurface_lock_read");return;}
  unsigned char *base=api->getBaseAddress((void *)importedSurface);NSUInteger sourceStride=api->getBytesPerRow((void *)importedSurface);
  if(!base||sourceStride!=surfaceRowBytes){if(api->unlock((void *)importedSurface,1,&seed))MARK("FAIL iosurface_unlock_read");MARK("FAIL iosurface_read_layout");return;}
  for(NSUInteger y=0;y<r.size.height;y++)memcpy((unsigned char *)bytes+y*effectiveRowStride,base+(r.origin.y+y)*sourceStride+r.origin.x*4,r.size.width*4);
  if(api->unlock((void *)importedSurface,1,&seed))MARK("FAIL iosurface_unlock_read");return;}
 if(m.height&&m.stride>(~(NSUInteger)0)/m.height){MARK("FAIL texture_read_bounds");return;}
 NSUInteger sourceImageStride=m.stride*m.height;
 if(!buffer||layerBase>buffer->logical){MARK("FAIL texture_read_bounds");return;}
 NSUInteger imageTail=r.size.depth?(r.size.depth-1)*sourceImageStride:0;
 NSUInteger rowTail=r.size.height?(r.size.height-1)*m.stride:0;
 if(imageTail>~(NSUInteger)0-rowTail||layerBase>~(NSUInteger)0-imageTail-rowTail||
    r.origin.x>~(NSUInteger)0-rowByteCount||
    layerBase+imageTail+rowTail+r.origin.x+rowByteCount>buffer->logical){MARK("FAIL texture_read_bounds");return;}
 for(NSUInteger z=0;z<r.size.depth;z++)for(NSUInteger y=0;y<r.size.height;y++)memcpy(
  (unsigned char *)bytes+z*effectiveImageStride+y*effectiveRowStride,
  (unsigned char *)tegu_buffer_bytes(buffer)+layerBase+(r.origin.z+z)*sourceImageStride+
   (r.origin.y+y)*m.stride+r.origin.x*bytesPerPixel,rowByteCount);
}
- (void)replaceRegion:(TeguRegion)r mipmapLevel:(NSUInteger)level withBytes:(const void *)bytes bytesPerRow:(NSUInteger)stride {
 [self replaceRegion:r mipmapLevel:level slice:0 withBytes:bytes bytesPerRow:stride bytesPerImage:0];
}
- (void)replaceRegion:(TeguRegion)r mipmapLevel:(NSUInteger)level slice:(NSUInteger)slice
 withBytes:(const void *)bytes bytesPerRow:(NSUInteger)stride bytesPerImage:(NSUInteger)imageStride {
 if(textureResourceOptions){MARK("UNSUPPORTED private_texture_cpu_write");return;}
#ifndef NORD2_JM_TRANSPORT
 if(surfaceImported&&surfaceTargetVersion){MARK("UNSUPPORTED compositor_iosurface_cpu_write");return;}
#endif
 if(level>=levelCount){MARK("FAIL texture_write_level");return;}
 struct tegu_texture_level m=mip[level];
 if(tegu_texture_is_astc(format)) {
  /* Compressed upload: rows of blocks; bytesPerRow is per block row. */
  unsigned bw=4,bh=4,bb=16;tegu_texture_block_shape(format,&bw,&bh,&bb);
  NSUInteger bx=r.origin.x/bw,by=r.origin.y/bh,blocksWide=(r.size.width+bw-1)/bw,blocksTall=(r.size.height+bh-1)/bh;
  NSUInteger levelWide=(m.width+bw-1)/bw,levelTall=(m.height+bh-1)/bh,copy=blocksWide*bb;
  NSUInteger layerBase=0,lvlDepth=0;
  if(!tegu_texture_cpu_layer(self,level,slice,&layerBase,&lvlDepth)){MARK("FAIL texture_write_layer");return;}
  if(!bytes||!buffer||surfaceImported||r.origin.z||r.size.depth>1||r.origin.x%bw||r.origin.y%bh||
     bx>levelWide||blocksWide>levelWide-bx||by>levelTall||blocksTall>levelTall-by||stride<copy||
     m.stride<levelWide*bb||layerBase>buffer->logical||
     layerBase+(by+blocksTall)*m.stride>buffer->logical){MARK("FAIL texture_write_bounds");return;}
  for(NSUInteger y=0;y<blocksTall;y++)
   memcpy((unsigned char *)tegu_buffer_bytes(buffer)+layerBase+(by+y)*m.stride+bx*bb,(const unsigned char *)bytes+y*stride,copy);
#if defined(TEGU_RENDER_TRACE)||defined(TEGU_ASTC_TRACE)
  {static unsigned uploads;
   if(__atomic_fetch_add(&uploads,1,__ATOMIC_RELAXED)<16){char line[160];int n=snprintf(line,sizeof(line),
    "TEGU_ASTC_TEXTURE upload %lux%lu f%lu level=%lu region=%lu,%lu %lux%lu stride=%lu first=%02x%02x%02x%02x\n",
    (unsigned long)textureWidth,(unsigned long)textureHeight,(unsigned long)format,(unsigned long)level,
    (unsigned long)r.origin.x,(unsigned long)r.origin.y,(unsigned long)r.size.width,(unsigned long)r.size.height,(unsigned long)stride,
    ((const unsigned char *)bytes)[0],((const unsigned char *)bytes)[1],((const unsigned char *)bytes)[2],((const unsigned char *)bytes)[3]);
    if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);}}
#endif
  return;
 }
 NSUInteger bytesPerPixel=tegu_texture_bytes_per_pixel(format);
 if(!bytesPerPixel||r.size.width>(~(NSUInteger)0)/bytesPerPixel){MARK("FAIL texture_write_bounds");return;}
 NSUInteger rowByteCount=r.size.width*bytesPerPixel;
 NSUInteger layerBase=0,lvlDepth=0;
 if(!tegu_texture_cpu_layer(self,level,slice,&layerBase,&lvlDepth)){MARK("FAIL texture_write_layer");return;}
 NSUInteger effectiveRowStride=stride?stride:(textureKind==0?rowByteCount:0);
 if(r.size.height&&effectiveRowStride>(~(NSUInteger)0)/r.size.height){MARK("FAIL texture_write_bounds");return;}
 NSUInteger minImageStride=effectiveRowStride*r.size.height;
 NSUInteger effectiveImageStride=imageStride?imageStride:minImageStride;
 if(!bytes||r.origin.x>m.width||r.origin.y>m.height||r.origin.z>lvlDepth||
    r.size.width>m.width-r.origin.x||r.size.height>m.height-r.origin.y||
    r.size.depth>lvlDepth-r.origin.z||(!stride&&textureKind!=0)||effectiveRowStride<rowByteCount||
    (imageStride&&imageStride<minImageStride)||
    (r.size.depth>1&&(!imageStride||effectiveImageStride>
      ((~(NSUInteger)0)-minImageStride)/(r.size.depth-1)))) {
  MARK("FAIL texture_write_bounds");return;
 }
 if(surfaceImported){const struct TeguIOSurfaceAPI *api=tegu_iosurface_api();uint32_t seed=0;
  if(r.origin.z||r.size.depth!=1){MARK("FAIL texture_write_bounds");return;}
  uint32_t expectedFourcc=surfaceTargetVersion?TEGU_IOSURFACE_BGRA:(format==70?TEGU_IOSURFACE_RGBA:TEGU_IOSURFACE_BGRA);
  NSUInteger minRow=surfaceTargetVersion?textureWidth*4:64;
  NSUInteger maxAlloc=surfaceTargetVersion?((16u<<20)+4095):(256u<<20);
  if(!api||api->getWidth((void *)importedSurface)!=textureWidth||api->getHeight((void *)importedSurface)!=textureHeight||
     api->getBytesPerRow((void *)importedSurface)!=surfaceRowBytes||api->getAllocSize((void *)importedSurface)!=surfaceAllocSize||
     api->getPixelFormat((void *)importedSurface)!=expectedFourcc||api->getPlaneCount((void *)importedSurface)!=0||
     (surfaceTargetVersion?(surfaceRowBytes<minRow||(surfaceRowBytes&63)):(surfaceRowBytes!=64))||
     surfaceAllocSize<4096||surfaceAllocSize>maxAlloc||(surfaceAllocSize&4095)||
     textureHeight>SIZE_MAX/surfaceRowBytes||textureHeight*surfaceRowBytes>surfaceAllocSize){MARK("FAIL iosurface_write_snapshot");return;}
  if(api->lock((void *)importedSurface,0,&seed)){MARK("FAIL iosurface_lock_write");return;}
  unsigned char *base=api->getBaseAddress((void *)importedSurface);NSUInteger sourceStride=api->getBytesPerRow((void *)importedSurface);
  if(!base||sourceStride!=surfaceRowBytes){if(api->unlock((void *)importedSurface,0,&seed))MARK("FAIL iosurface_unlock_write");MARK("FAIL iosurface_write_layout");return;}
  for(NSUInteger y=0;y<r.size.height;y++)memcpy(base+(r.origin.y+y)*sourceStride+r.origin.x*bytesPerPixel,(const unsigned char *)bytes+y*effectiveRowStride,rowByteCount);
  if(api->unlock((void *)importedSurface,0,&seed))MARK("FAIL iosurface_unlock_write");return;}
 if(m.height&&m.stride>(~(NSUInteger)0)/m.height){MARK("FAIL texture_write_bounds");return;}
 NSUInteger destinationImageStride=m.stride*m.height;
 if(!buffer||layerBase>buffer->logical){MARK("FAIL texture_write_bounds");return;}
 NSUInteger imageTail=r.size.depth?(r.size.depth-1)*destinationImageStride:0;
 NSUInteger rowTail=r.size.height?(r.size.height-1)*m.stride:0;
 if(imageTail>~(NSUInteger)0-rowTail||layerBase>~(NSUInteger)0-imageTail-rowTail||
    r.origin.x>~(NSUInteger)0-rowByteCount||
    layerBase+imageTail+rowTail+r.origin.x+rowByteCount>buffer->logical){MARK("FAIL texture_write_bounds");return;}
 for(NSUInteger z=0;z<r.size.depth;z++)for(NSUInteger y=0;y<r.size.height;y++)memcpy(
  (unsigned char *)tegu_buffer_bytes(buffer)+layerBase+(r.origin.z+z)*destinationImageStride+
   (r.origin.y+y)*m.stride+r.origin.x*bytesPerPixel,
  (const unsigned char *)bytes+z*effectiveImageStride+y*effectiveRowStride,rowByteCount);
}
- (void)dealloc{if(heapOwner&&heapCharge)[heapOwner teguReleaseHeapCharge:heapCharge];[heapOwner release];[viewSource release];[debugLabel release];[buffer release];[transientOwner release];if(surfaceImported){if(importedHandle&&!importedHandleShared&&importedOwner&&importedOwner->scalar){unsigned n=0;importedOwner->scalar(importedOwner->connection,14,&importedHandle,1,0,&n);}
 if(importedSurface&&releaseImportedSurface)releaseImportedSurface((void *)importedSurface);[importedOwner release];}
 if(shadowSurface&&releaseImportedSurface)releaseImportedSurface((void *)shadowSurface);[super dealloc];}
@end
/* One logical-backing check for every CPU row walk over a texture level. */
static int tegu_texture_level_rows(TeguTexture *t,NSUInteger level,NSUInteger x,NSUInteger y,
 NSUInteger width,NSUInteger height,NSUInteger *offset) {
 NSUInteger bpp=t?tegu_texture_bytes_per_pixel(t->format):0;
 if(!t||!t->buffer||t->surfaceImported||level>=t->levelCount||t->textureSamples!=1||
    [t storageMode]==3||!bpp)return 0;
 struct tegu_texture_level m=t->mip[level];NSUInteger logical=t->buffer->logical;
 if(!m.stride||m.width>m.stride/bpp||x>m.width||y>m.height||width>m.width-x||height>m.height-y||
    m.offset>logical||y>(logical-m.offset)/m.stride)return 0;
 NSUInteger start=m.offset+y*m.stride;
 if(x>(logical-start)/bpp)return 0;start+=x*bpp;
 if(height) {
  NSUInteger tail=(height-1)*m.stride,rowBytes=width*bpp;
  if(height-1>(logical-start)/m.stride||tail>logical-start||rowBytes>logical-start-tail)return 0;
 }
 if(offset)*offset=start;return 1;
}
static int tegu_texture_layout_for_descriptor(TeguComputeDevice *device,TeguTextureDescriptor *d,
 struct tegu_texture_level layout[13],NSUInteger *totalBytes,int reportRefusal) {
 NSUInteger requestedFormat=d?[d pixelFormat]:0;
 int halfSource=requestedFormat==115;
 int r16Float=requestedFormat==43;
 int twoPlaneXR=requestedFormat==550,xrFormat=twoPlaneXR||requestedFormat==554;
 NSUInteger options=d?[d resourceOptions]:0,levels=d?[d mipmapLevelCount]:0;
 int r52Lookup1D=d&&(device->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&[d textureType]==0&&[d height]==1&&
   [d depth]==1&&[d sampleCount]==1&&[d arrayLength]==1&&options==0&&levels==1&&[d usage]==1&&
   ((requestedFormat==23&&[d width]==3072)||(requestedFormat==25&&[d width]==4096)||
    (requestedFormat==55&&([d width]==1024||[d width]==4096))||(requestedFormat==105&&[d width]==1024));
 NSUInteger requestedDepth=d?[d depth]:0;
 int r54R8Lookup=d&&(device->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&requestedFormat==10&&
   [d textureType]==0&&[d width]==16&&[d height]==1&&requestedDepth==1;
 /* R55 proves this is a real MTLTextureType3D request. Bound the unknown
  * depth to a 64-texel axis until the richer refusal record gives its exact
  * value; every admitted depth is represented in storage and region copies. */
 int r55R8Volume=d&&(device->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&requestedFormat==10&&
   [d textureType]==7&&[d width]==64&&[d height]==64&&requestedDepth&&requestedDepth<=64;
 int r54R8=(r54R8Lookup||r55R8Volume)&&[d sampleCount]==1&&[d arrayLength]==1&&
   options==0&&levels==1&&[d usage]==1;
 int capturedPrivate=d&&(device->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&[d width]==776&&[d height]==24&&
   (options==0x20||options==0x30)&&[d usage]==5&&
   ((requestedFormat==115&&levels==10)||((requestedFormat==550||requestedFormat==554)&&levels==1));
 /* R44 additionally requests two exact private-storage resources; R56 uses
  * the same R16Float geometry with the complete ten-level mip chain. Keep
  * memoryless 0x30 variants and every neighboring descriptor outside this
  * evidence-bounded allocation class. */
 int r44Private=d&&(device->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&[d width]==776&&[d height]==24&&
   options==0x20&&[d usage]==5&&
   ((requestedFormat==80&&levels==10)||(r16Float&&(levels==1||levels==10)));
 /* R47 observed these two memoryless BGRA attachment descriptors verbatim.
  * Keep them separate from sampled textures: the owned buffer is a transient
  * attachment stand-in and does not make storage mode 3 shader-bindable. */
 int r47MemorylessBGRA=d&&(device->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&
   [d width]==776&&[d height]==24&&requestedFormat==80&&options==0x30&&[d usage]==5&&
   (levels==1||levels==10);
 /* R58 records this complete MTLTextureType2DMultisample descriptor.  This
  * allocation-only class reports its Metal identity while pipeline creation,
  * encoder construction and kernel wire ABIs refuse multisample execution. */
 int r58MemorylessMSAABGRA=tegu_r58_memoryless_msaa_descriptor(device,d);
 /* Three further R58 descriptors are private single-sample intermediates.
  * Admit their exact storage shape only: no render, sampling or image-write
  * path consumes this allocation class until those contracts are observed. */
 int r58Private2D=d&&(device->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&
   requestedDepth==1&&options==0x20&&levels==1&&[d textureType]==2&&
   [d sampleCount]==1&&[d arrayLength]==1&&
   (([d width]==594&&[d height]==594&&
      (([d usage]==5&&(requestedFormat==10||requestedFormat==80))||
       ([d usage]==7&&requestedFormat==10)))||
    ([d width]==297&&[d height]==297&&[d usage]==7&&requestedFormat==80));
 /* R117: BackBoard's full-screen private BGRA8 render/sample targets and
  * small R8 2D lookup tables (luma boost).  Single level and sample only. */
 int displayPrivateBGRA=d&&(device->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&requestedFormat==80&&
   [d textureType]==2&&requestedDepth==1&&(options==0x20||options==0x30)&&levels==1&&[d sampleCount]==1&&[d arrayLength]==1&&
   ([d usage]==1||[d usage]==4||[d usage]==5);
 /* Memoryless (0x30) intermediates get an ordinary allocation: CoreAnimation
  * renders through them and treats a nil texture as fatal or drops the layer. */
 /* R139: A8 masks/text, RG8 and RGBA16F lookups, sampled single-level 2D. */
 int sampledSmall2D=d&&(device->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&(device->capabilities&TEGU_GPU_CAP_GENERIC_JOB)&&
   (requestedFormat==1||requestedFormat==30||requestedFormat==115)&&[d textureType]==2&&requestedDepth==1&&
   options==0&&levels==1&&[d sampleCount]==1&&[d arrayLength]==1&&[d usage]==1;
 int r8Lookup2D=d&&(device->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&requestedFormat==10&&
   [d textureType]==2&&requestedDepth==1&&options==0&&levels==1&&[d sampleCount]==1&&[d arrayLength]==1&&
   [d usage]==1&&[d height]<=64;
 /* R158: chronod (widgets) asks for memoryless RG16Float targets and R8
  * read/write intermediates; a nil texture makes CoreAnimation skip the whole
  * render. Any single-sample 2D texture of a sized format is an allocation;
  * render/sample paths still refuse formats they cannot execute. */
 int genericAlloc=d&&(device->capabilities&TEGU_GPU_CAP_GENERIC_JOB)&&[d textureType]==2&&requestedDepth==1&&
   [d sampleCount]==1&&[d arrayLength]==1&&(options==0||options==0x20||options==0x30)&&
   tegu_texture_bytes_per_pixel(requestedFormat)&&requestedFormat!=550&&requestedFormat!=554;
 /* Bounded 4x colour attachment class used by SpringBoard Siri. Unlike the
  * exact memoryless 594x594 capture above, this allocates ordinary linear
  * sample-major backing so the generic v3 target sidecar can address it. */
 int genericMSAA=d&&(device->capabilities&TEGU_GPU_CAP_GENERIC_JOB)&&[d textureType]==4&&
   requestedDepth==1&&[d sampleCount]==4&&[d arrayLength]==1&&levels==1&&
   (options==0x20||options==0x30)&&([d usage]==4||[d usage]==5)&&
   tegu_texture_bytes_per_pixel(requestedFormat)&&requestedFormat!=550&&requestedFormat!=554;
 /* ASTC icons (R165: ImageIO hands CoreAnimation ASTC texture data). */
 int astc2D=d&&(device->capabilities&TEGU_GPU_CAP_GENERIC_JOB)&&tegu_texture_is_astc(requestedFormat)&&
   [d textureType]==2&&requestedDepth==1&&[d sampleCount]==1&&[d arrayLength]==1&&(options==0||options==0x20);
 int uiPrivate=capturedPrivate||r44Private||r47MemorylessBGRA||r58MemorylessMSAABGRA||r58Private2D||displayPrivateBGRA||genericAlloc||genericMSAA||astc2D;
 if(!d||([d textureType]!=2&&!r52Lookup1D&&!r54R8&&!r58MemorylessMSAABGRA&&!genericMSAA)||
    ([d pixelFormat]!=70&&[d pixelFormat]!=80&&!halfSource&&!r16Float&&requestedFormat!=550&&requestedFormat!=554&&!r52Lookup1D&&!r54R8&&!r58Private2D&&!r8Lookup2D&&!sampledSmall2D&&!genericAlloc&&!genericMSAA&&!astc2D)||
    ![d width]||![d height]||[d width]>4096||[d height]>4096||(!r55R8Volume&&[d depth]!=1)||
    ![d mipmapLevelCount]||[d mipmapLevelCount]>13||(!r58MemorylessMSAABGRA&&!genericMSAA&&[d sampleCount]!=1)||[d arrayLength]!=1||
    (!uiPrivate&&([d resourceOptions]||([d usage]&~(NSUInteger)5)))||
    ((requestedFormat==550||requestedFormat==554)&&!uiPrivate)||
    (r16Float&&!r44Private&&!genericAlloc)||
    (halfSource&&!uiPrivate&&!sampledSmall2D&&(!(device->capabilities&2048)||[d width]!=16||[d height]!=16||levels!=1||[d usage]!=1))){if(reportRefusal)tegu_texture_refused(d);return 0;}
 NSUInteger bytes=0,w=[d width],h=[d height],depth=requestedDepth;
 if(astc2D) {
  unsigned bw=4,bh=4,bb=16;tegu_texture_block_shape(requestedFormat,&bw,&bh,&bb);
  for(NSUInteger level=0;level<levels;level++) {
   bytes=(bytes+63)&~(NSUInteger)63;
   NSUInteger stride=(((w+bw-1)/bw)*bb+63)&~(NSUInteger)63,rows=(h+bh-1)/bh;
   layout[level]=(struct tegu_texture_level){w,h,stride,bytes};bytes+=stride*rows;
   if(bytes>64*1024*1024)return 0;
   w=w>1?w/2:1;h=h>1?h/2:1;
  }
  *totalBytes=bytes;return 1;
 }
 NSUInteger bytesPerPixel=tegu_texture_bytes_per_pixel(requestedFormat);if(!bytesPerPixel)return 0;
 for(NSUInteger level=0;level<levels;level++) {
  if(level&&w==1&&h==1&&layout[level-1].width==1&&layout[level-1].height==1)return 0;
  if(bytes>SIZE_MAX-63||w>SIZE_MAX/bytesPerPixel)return 0;
  bytes=(bytes+63)&~(NSUInteger)63;
  NSUInteger stride=w*bytesPerPixel;
  if(xrFormat){if(stride>SIZE_MAX-15)return 0;stride=(stride+15)&~(NSUInteger)15;}
  /* Mali v7+ linear sampling also requires 64-byte rows (Mesa
   * pan_linear_or_tiled_row_align_req); pad sampled 2D RGBA8/BGRA8.
   * Every level of a render-target texture is a possible colour attachment
   * and the arch9 linear RT stride is 64-byte aligned (n2r, pass admission),
   * so mipmapped targets pad each level too (QuartzCore's mipmapped private
   * BGRA8/RGBA16F targets, e.g. 776x24x10, were unrenderable before). */
  else if(((levels==1&&![d usage])||([d usage]&4)||
    ([d usage]==1&&[d textureType]==2&&
    (requestedFormat==70||requestedFormat==80||requestedFormat==10||requestedFormat==1||requestedFormat==30||requestedFormat==115)))&&
    (device->capabilities&(TEGU_GPU_CAP_COMPOSITOR|TEGU_GPU_CAP_ATTACHMENT_CLEAR))){if(stride>SIZE_MAX-63)return 0;stride=(stride+63)&~(NSUInteger)63;}
  if(h&&stride>SIZE_MAX/h)return 0;
  NSUInteger imageBytes=stride*h;
  if(depth&&imageBytes>SIZE_MAX/depth)return 0;
  NSUInteger levelBytes=imageBytes*depth;
  if(r58MemorylessMSAABGRA||genericMSAA) {
   NSUInteger samples=[d sampleCount];
   if(samples&&levelBytes>SIZE_MAX/samples)return 0;
   levelBytes*=samples;
  }
  if(bytes>SIZE_MAX-levelBytes)return 0;
  layout[level]=(struct tegu_texture_level){w,h,stride,bytes};bytes+=levelBytes;
  /* 550 is Apple's two-plane RGB10+A8 allocation. Keep the alpha plane in
   * the lifetime-owned buffer while leaving sampling/rendering unsupported. */
  if(twoPlaneXR) {
   if(w>SIZE_MAX-15)return 0;NSUInteger alphaStride=(w+15)&~(NSUInteger)15;
   if(h&&alphaStride>SIZE_MAX/h)return 0;NSUInteger alphaBytes=alphaStride*h;
   if(bytes>SIZE_MAX-alphaBytes)return 0;bytes+=alphaBytes;
  }
  if(bytes>64*1024*1024)return 0;
  w=w>1?w/2:1;h=h>1?h/2:1;depth=depth>1?depth/2:1;
 }
 *totalBytes=bytes;return 1;
}
/* R164: SpringBoard aborted on -[MTLDevice newTextureLayoutWithDescriptor:
 * isHeapOrBufferBacked:] when unlocking. ImageIO's ASTCTwiddler asks the device
 * how compressed icon data is laid out in GPU memory (size, alignment) and
 * copies linear blocks into it. This driver keeps textures linear: block rows
 * (ASTC 16-byte blocks) or pixel rows per level, levels 64-byte aligned. */
static int tegu_texture_block_shape(NSUInteger format,unsigned *bw,unsigned *bh,unsigned *bytes) {
 static const unsigned char astc[15][2]={{4,4},{5,4},{5,5},{6,5},{6,6},{0,0},{8,5},{8,6},{8,8},
  {10,5},{10,6},{10,8},{10,10},{12,10},{12,12}};
 unsigned index=format>=186&&format<=200?(unsigned)(format-186):format>=204&&format<=218?(unsigned)(format-204):99;
 if(index<15&&astc[index][0]){*bw=astc[index][0];*bh=astc[index][1];*bytes=16;return 1;}
 NSUInteger pixel=tegu_texture_bytes_per_pixel(format);
 if(!pixel)return 0;
 *bw=1;*bh=1;*bytes=(unsigned)pixel;return 1;
}
/* 16 bytes, returned in x0/x1 (ImageIO ASTCTwiddler::getWatermark stores both). */
typedef struct {uint64_t low,high;} TeguTextureLayoutWatermark;
@interface TeguTextureLayout : NSObject {
@public unsigned blockWidth,blockHeight,blockBytes,levels;NSUInteger size;
 NSUInteger levelOffset[16],levelRowBytes[16],levelRows[16],levelImageBytes[16];
}
- (NSUInteger)size;
- (NSUInteger)alignment;
- (void)initializeTextureMemory:(void *)memory;
- (void)finalizeTextureMemory:(void *)memory;
- (TeguTextureLayoutWatermark)watermark;
- (void)copyFromLinearBytes:(const void *)bytes linearOffset:(NSUInteger)offset linearBytesPerRow:(NSUInteger)rowBytes
 linearBytesPerImage:(NSUInteger)imageBytes toTextureMemory:(void *)memory textureSlice:(NSUInteger)slice
 textureLevel:(NSUInteger)level textureRegion:(TeguRegion)region;
- (void)copyFromTextureMemory:(const void *)memory textureSlice:(NSUInteger)slice textureLevel:(NSUInteger)level
 textureRegion:(TeguRegion)region toLinearBytes:(void *)bytes linearOffset:(NSUInteger)offset
 linearBytesPerRow:(NSUInteger)rowBytes linearBytesPerImage:(NSUInteger)imageBytes;
@end
@implementation TeguTextureLayout
- (NSUInteger)size {return size;}
- (NSUInteger)alignment {return 64;}
- (void)initializeTextureMemory:(void *)memory {if(memory)memset(memory,0,size);}
/* Linear memory needs no post-processing (R167: CoreUI/ImageIO/QuartzCore call it). */
- (void)finalizeTextureMemory:(void *)memory {(void)memory;}
/* Identifies a GPU-specific pre-twiddled layout. This one is plain linear, so
 * no pre-twiddled data can match it and ImageIO takes its ordinary path
 * (R197: ~985 unrecognized-selector exceptions from PosterBoard/SpringBoard). */
- (TeguTextureLayoutWatermark)watermark {return (TeguTextureLayoutWatermark){0,0};}
/* Region in texels; level rows/columns in blocks. Returns 0 for out-of-range. */
static int tegu_layout_region(TeguTextureLayout *l,NSUInteger slice,NSUInteger level,TeguRegion r,
 NSUInteger *start,NSUInteger *rows,NSUInteger *rowBytes) {
 if(!l||slice||level>=l->levels||r.origin.z||r.size.depth>1||r.origin.x%l->blockWidth||r.origin.y%l->blockHeight)return 0;
 NSUInteger bx=r.origin.x/l->blockWidth,by=r.origin.y/l->blockHeight;
 NSUInteger bwide=(r.size.width+l->blockWidth-1)/l->blockWidth,btall=(r.size.height+l->blockHeight-1)/l->blockHeight;
 if(by>l->levelRows[level]||btall>l->levelRows[level]-by||bx*l->blockBytes>l->levelRowBytes[level]||
    bwide*l->blockBytes>l->levelRowBytes[level]-bx*l->blockBytes)return 0;
 *start=l->levelOffset[level]+by*l->levelRowBytes[level]+bx*l->blockBytes;*rows=btall;*rowBytes=bwide*l->blockBytes;
 return 1;
}
- (void)copyFromLinearBytes:(const void *)bytes linearOffset:(NSUInteger)offset linearBytesPerRow:(NSUInteger)rowBytes
 linearBytesPerImage:(NSUInteger)imageBytes toTextureMemory:(void *)memory textureSlice:(NSUInteger)slice
 textureLevel:(NSUInteger)level textureRegion:(TeguRegion)region {
 (void)imageBytes;NSUInteger start=0,rows=0,copy=0;
 if(!bytes||!memory||!tegu_layout_region(self,slice,level,region,&start,&rows,&copy)||(rows&&rowBytes<copy))return;
 for(NSUInteger y=0;y<rows;y++)
  memcpy((unsigned char *)memory+start+y*levelRowBytes[level],(const unsigned char *)bytes+offset+y*rowBytes,copy);
}
- (void)copyFromTextureMemory:(const void *)memory textureSlice:(NSUInteger)slice textureLevel:(NSUInteger)level
 textureRegion:(TeguRegion)region toLinearBytes:(void *)bytes linearOffset:(NSUInteger)offset
 linearBytesPerRow:(NSUInteger)rowBytes linearBytesPerImage:(NSUInteger)imageBytes {
 (void)imageBytes;NSUInteger start=0,rows=0,copy=0;
 if(!bytes||!memory||!tegu_layout_region(self,slice,level,region,&start,&rows,&copy)||(rows&&rowBytes<copy))return;
 for(NSUInteger y=0;y<rows;y++)
  memcpy((unsigned char *)bytes+offset+y*rowBytes,(const unsigned char *)memory+start+y*levelRowBytes[level],copy);
}
@end
@interface TeguComputeDevice (TextureLayouts)
- (id)newTextureLayoutWithDescriptor:(TeguTextureDescriptor *)d isHeapOrBufferBacked:(signed char)backed;
@end
@implementation TeguComputeDevice (TextureLayouts)
- (id)newTextureLayoutWithDescriptor:(TeguTextureDescriptor *)d isHeapOrBufferBacked:(signed char)backed {
 (void)backed;unsigned bw=0,bh=0,bb=0;
 if(!d||[d textureType]!=2||[d depth]>1||[d arrayLength]>1||[d sampleCount]>1||![d width]||![d height]||
    [d width]>16384||[d height]>16384||![d mipmapLevelCount]||[d mipmapLevelCount]>16||
    !tegu_texture_block_shape([d pixelFormat],&bw,&bh,&bb))return 0;
 TeguTextureLayout *l=[[TeguTextureLayout alloc] init];if(!l)return 0;
 l->blockWidth=bw;l->blockHeight=bh;l->blockBytes=bb;l->levels=(unsigned)[d mipmapLevelCount];
 NSUInteger w=[d width],h=[d height],bytes=0;
 for(unsigned level=0;level<l->levels;level++) {
  bytes=(bytes+63)&~(NSUInteger)63;
  l->levelOffset[level]=bytes;l->levelRowBytes[level]=((w+bw-1)/bw)*bb;l->levelRows[level]=(h+bh-1)/bh;
  l->levelImageBytes[level]=l->levelRowBytes[level]*l->levelRows[level];bytes+=l->levelImageBytes[level];
  w=w>1?w/2:1;h=h>1?h/2:1;
 }
 l->size=(bytes+63)&~(NSUInteger)63;
 return l;
}
@end
/* General sampled array/cube/MSAA allocation class for the v2 sidecar. The
 * legacy 2D/special classes stay byte-identical; this branch is limited to
 * shader-read usage=1, options=0 and bounded dimensions. CPU layer copies
 * remain outside this ABI (slice0 only), so allocation is not a claim of
 * complete array upload support. */
static int tegu_texture_v2_general_descriptor(TeguComputeDevice *device,TeguTextureDescriptor *d) {
 if(!device||!(device->capabilities&TEGU_GPU_CAP_GENERIC_JOB)||!d||[d usage]!=1||[d resourceOptions]||[d depth]!=1)return 0;
 NSUInteger type=[d textureType],levels=[d mipmapLevelCount],samples=[d sampleCount],arrays=[d arrayLength];
 NSUInteger width=[d width],height=[d height],format=[d pixelFormat];
 if(!width||!height||width>4096||height>4096||!levels||levels>13)return 0;
 if(!(format==1||format==10||format==30||format==70||format==80||format==115||
      tegu_texture_is_astc(format)))return 0;
 NSUInteger layers=type==5?6u:type==6?6u*arrays:type==4?1u:arrays;
 if(!layers||layers>2048||layers>256/levels)return 0;
 switch(type) {
 case 3: return arrays>=1&&arrays<=2048&&samples==1;
 case 5: return arrays==1&&samples==1&&width==height;
 case 6: return arrays>=1&&arrays<=(2048/6)&&samples==1&&width==height;
 case 4: return arrays==1&&levels==1&&(samples==2||samples==4||samples==8)&&!tegu_texture_is_astc(format);
 case 8: return arrays>=1&&arrays<=2048&&levels==1&&(samples==2||samples==4||samples==8)&&!tegu_texture_is_astc(format);
 default: return 0;
 }
}
static int tegu_texture_v2_general_layout(TeguTextureDescriptor *d,struct tegu_texture_level layout[13],
 NSUInteger *totalBytes,NSUInteger *layersOut,NSUInteger *layerStrideOut) {
 NSUInteger type=[d textureType],levels=[d mipmapLevelCount],samples=[d sampleCount],arrays=[d arrayLength];
 NSUInteger width=[d width],height=[d height],format=[d pixelFormat];
 NSUInteger layers=type==5?6u:type==6?6u*arrays:type==4?1u:arrays;
 NSUInteger w=width,h=height,bytes=0;
 unsigned bw=1,bh=1,bb=0;
 if(!layers||layers>2048||!tegu_texture_block_shape(format,&bw,&bh,&bb))return 0;
 for(NSUInteger level=0;level<levels;level++) {
  bytes=(bytes+63)&~(NSUInteger)63;
  NSUInteger rowBytes=((w+bw-1)/bw)*bb;
  if(rowBytes>SIZE_MAX-63)return 0;
  NSUInteger stride=(rowBytes+63)&~(NSUInteger)63;
  NSUInteger rows=(h+bh-1)/bh;
  if(!rows||stride>SIZE_MAX/rows)return 0;
  NSUInteger levelBytes=stride*rows;
  if((type==4||type==8)&&samples>1) {
   if(levelBytes>SIZE_MAX/samples)return 0;
   levelBytes*=samples;
  }
  if(bytes>SIZE_MAX-levelBytes)return 0;
  layout[level]=(struct tegu_texture_level){w,h,stride,bytes};bytes+=levelBytes;
  w=w>1?w/2:1;h=h>1?h/2:1;
 }
 NSUInteger layerStride=(bytes+63)&~(NSUInteger)63;
 if(layerStride>64*1024*1024||layers>64*1024*1024/layerStride)return 0;
 *totalBytes=layerStride*layers;*layersOut=layers;*layerStrideOut=layerStride;return 1;
}
@implementation TeguComputeDevice (RenderTextures)
- (id)newTextureWithDescriptor:(TeguTextureDescriptor *)d {
 struct tegu_texture_level layout[13];NSUInteger bytes=0,generalLayers=0,generalLayerStride=0;
 int general=tegu_texture_v2_general_descriptor(self,d);
 if(general) {
  if(!tegu_texture_v2_general_layout(d,layout,&bytes,&generalLayers,&generalLayerStride)) {
   tegu_texture_refused(d);return 0;
  }
 } else if(!tegu_texture_layout_for_descriptor(self,d,layout,&bytes,1))return 0;
 NSUInteger requestedDepth=[d depth],options=[d resourceOptions],levels=[d mipmapLevelCount];
 TeguTexture *t=[[TeguTexture alloc] init];if(!t)return 0;
 t->textureWidth=[d width];t->textureHeight=[d height];t->textureDepth=requestedDepth;t->textureKind=[d textureType];t->textureSamples=[d sampleCount];t->format=[d pixelFormat];
 t->textureUsage=[d usage];t->textureResourceOptions=options;t->rowBytes=layout[0].stride;t->levelCount=levels;
 t->arrayLength=[d arrayLength];
 t->layers=general?generalLayers:t->arrayLength*((t->textureKind==5||t->textureKind==6)?6u:1u);
 memcpy(t->mip,layout,levels*sizeof(layout[0]));
 for(NSUInteger level=0;level<levels;level++) {
  t->levelDepth[level]=general?1u:(t->textureKind==7?(requestedDepth>>level?requestedDepth>>level:1u):1u);
  t->levelSliceStride[level]=general?
   (((t->textureKind==4||t->textureKind==8))?layout[level].stride*layout[level].height:0):
   ((t->textureKind==7||t->textureSamples>1)?layout[level].stride*layout[level].height:0);
  t->levelLayerStride[level]=general&&generalLayers>1?generalLayerStride:0;
 }
 /* Memoryless attachments exist only for the render pass.  Retain the device
  * for resource identity, but do not manufacture addressable texture backing. */
 if(tegu_r58_memoryless_msaa_descriptor(self,d))t->transientOwner=[self retain];
 else {t->buffer=[self newBufferWithLength:bytes options:0];if(!t->buffer){[t release];return 0;}}
#if defined(TEGU_RENDER_TRACE)||defined(TEGU_ASTC_TRACE)
 if(tegu_texture_is_astc(t->format)) {
  static unsigned astc;
  if(__atomic_fetch_add(&astc,1,__ATOMIC_RELAXED)<16){char line[160];int n=snprintf(line,sizeof(line),
   "TEGU_ASTC_TEXTURE create %lux%lu f%lu levels=%lu usage=%lu options=0x%lx row=%lu bytes=%lu\n",
   (unsigned long)t->textureWidth,(unsigned long)t->textureHeight,(unsigned long)t->format,(unsigned long)levels,
   (unsigned long)t->textureUsage,(unsigned long)options,(unsigned long)t->rowBytes,(unsigned long)bytes);
   if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);}
 }
#endif
 return t;
}
/* backboardd (R176) wraps CPU images with this private selector. The bytes
 * are laid out as -newTextureLayoutWithDescriptor: describes, i.e. this
 * driver's linear layout, so a copy is the texture; the caller's memory is
 * returned at once because nothing keeps referring to it. */
- (id)newTextureWithBytesNoCopy:(void *)bytes length:(NSUInteger)length descriptor:(TeguTextureDescriptor *)d
 deallocator:(void (^)(void *pointer,NSUInteger length))deallocator {
 TeguTexture *t=[self newTextureWithDescriptor:d];
 if(t&&t->buffer&&bytes&&length)memcpy(tegu_buffer_bytes(t->buffer),bytes,length<t->buffer->logical?length:t->buffer->logical);
 if(deallocator)deallocator(bytes,length);
 return t;
}
/* Always-on, bounded: an IOSurface import that returns nil leaves the
 * caller's layer (or a whole display) undrawn without any other record
 * (c6ec: backboardd's 1125x2436 'b3a8' two-plane display surfaces). */
static void tegu_iosurface_refused(TeguTextureDescriptor *d,id surface,NSUInteger plane,const char *reason) {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=16)return;
 const struct TeguIOSurfaceAPI *api=surface?tegu_iosurface_api():0;
 char line[320];int n=snprintf(line,sizeof(line),
  "TEGU_IOSURFACE_REFUSED reason=%s plane=%lu desc=%lux%lu fmt=%lu type=%lu usage=0x%lx options=0x%lx mips=%lu samples=%lu surf=%lux%lu fourcc=0x%08x row=%lu alloc=%lu planes=%lu\n",
  reason?reason:"?",(unsigned long)plane,(unsigned long)(d?[d width]:0),(unsigned long)(d?[d height]:0),
  (unsigned long)(d?[d pixelFormat]:0),(unsigned long)(d?[d textureType]:0),(unsigned long)(d?[d usage]:0),
  (unsigned long)(d?[d resourceOptions]:0),(unsigned long)(d?[d mipmapLevelCount]:0),(unsigned long)(d?[d sampleCount]:0),
  (unsigned long)(api?api->getWidth((void *)surface):0),(unsigned long)(api?api->getHeight((void *)surface):0),
  (unsigned)(api?api->getPixelFormat((void *)surface):0),(unsigned long)(api?api->getBytesPerRow((void *)surface):0),
  (unsigned long)(api?api->getAllocSize((void *)surface):0),(unsigned long)(api?api->getPlaneCount((void *)surface):0));
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
}
/* Owned BGRA8 copy of an IOSurface: refreshed from the surface before every
 * command buffer that uses it and written back after one that renders into
 * it (tegu_shadow_sync_jobs), so stride/placement need not satisfy the
 * hardware's direct-import rules. */
static TeguTexture *tegu_iosurface_shadow(TeguComputeDevice *self,TeguTextureDescriptor *d,id surface,
 const struct TeguIOSurfaceAPI *api,NSUInteger width,NSUInteger height,NSUInteger stride) {
 TeguTexture *shadow=[self newTextureWithDescriptor:d];
 if(shadow&&shadow->buffer&&shadow->format==80&&shadow->levelCount==1&&!shadow->surfaceImported) {
  shadow->shadowSurface=api->cfRetain((void *)surface);shadow->releaseImportedSurface=api->cfRelease;
  if(shadow->shadowSurface){tegu_shadow_trace(width,height,stride);return shadow;}
 }
 [shadow release];return 0;
}
- (id)newTextureWithDescriptor:(TeguTextureDescriptor *)d iosurface:(id)surface plane:(NSUInteger)plane {
 tegu_iosurface_import_trace(d,surface,plane);
 /* Additive selector17 path is deliberately narrower than legacy selector13:
  * non-planar BGRA8 render targets only, with full per-surface bounds. */
 if((self->capabilities&TEGU_GPU_CAP_COMPOSITOR_IOSURFACE)&&d&&surface&&plane==0&&
    [d pixelFormat]==80&&tegu_iosurface_render_target_usage([d usage])&&[d textureType]==2&&[d width]>=16&&[d height]>=16&&
    [d width]<=4096&&[d height]<=4096&&[d depth]==1&&[d mipmapLevelCount]==1&&
    [d sampleCount]==1&&[d arrayLength]==1&&![d resourceOptions]) {
  const struct TeguIOSurfaceAPI *api=tegu_iosurface_api();
  if(!api){tegu_iosurface_refused(d,surface,plane,"iosurface-api");return 0;}
  uint32_t id=api->getID((void *)surface);
  NSUInteger width=[d width],height=[d height],stride=api->getBytesPerRow((void *)surface),allocSize=api->getAllocSize((void *)surface);
  size_t planeCount=api->getPlaneCount((void *)surface);
  if(!id||api->getPixelFormat((void *)surface)!=TEGU_IOSURFACE_BGRA||
     planeCount>1||api->getWidth((void *)surface)!=width||
     api->getHeight((void *)surface)!=height||stride<width*4||stride>65536||
     height>SIZE_MAX/stride||stride*height>(16u<<20)||allocSize<stride*height){
   tegu_iosurface_refused(d,surface,plane,"bgra8-surface-shape");return 0;
  }
  /* A BGRA8 surface whose row stride or allocation is not directly
   * importable (arch9 linear RT rows are 64-byte aligned) is rendered through
   * an exact owned shadow instead of being refused. */
  if((stride&63)||allocSize>((16u<<20)+4095)||(allocSize&4095)) {
   TeguTexture *shadow=(self->capabilities&TEGU_GPU_CAP_GENERIC_JOB)?
    tegu_iosurface_shadow(self,d,surface,api,width,height,stride):0;
   if(!shadow)tegu_iosurface_refused(d,surface,plane,"bgra8-unaligned-no-shadow");
   return shadow;
  }
  U64 args[2]={id,1},reply[4]={0};unsigned count=4;
#ifdef NORD2_JM_TRANSPORT
  (void)args;
  int status=n2mb_import_surface(self->connection,id,allocSize,(U64)(uintptr_t)api->getBaseAddress((void *)surface),&reply[0]);
  reply[1]=0;reply[2]=stride;reply[3]=allocSize;
#else
  int status=!self->method||!self->scalar?-1:self->method(self->connection,17,args,2,0,0,reply,&count,0,0);
#endif
  U64 handle=reply[0],surfaceOffset=reply[1],kernelRowBytes=reply[2],kernelAllocSize=reply[3];
#ifdef NORD2_JM_TRANSPORT
  int known=handle&&handle<=UINT32_MAX;
#else
  int known=handle>=0xa0000001u&&handle<=0xafffffffu;
#endif
  /* R154: the kernel refuses surfaces whose pages lie outside the GPU's
   * memory window (three 465x465 layers). Use an owned copy instead. */
  if(status&&!known&&(self->capabilities&TEGU_GPU_CAP_GENERIC_JOB)) {
   TeguTexture *shadow=tegu_iosurface_shadow(self,d,surface,api,width,height,stride);
   if(!shadow)tegu_iosurface_refused(d,surface,plane,"kernel-import-and-shadow");
   return shadow;
  }
  if(status||count!=4||!known||surfaceOffset>UINT32_MAX||(surfaceOffset&4095)||
     kernelRowBytes!=stride||kernelAllocSize!=allocSize||surfaceOffset>kernelAllocSize||
     stride*height>kernelAllocSize-surfaceOffset){if(known&&self->scalar){unsigned z=0;self->scalar(self->connection,14,&handle,1,0,&z);}return 0;}
  TeguTexture *t=[[TeguTexture alloc] init];if(!t){unsigned z=0;self->scalar(self->connection,14,&handle,1,0,&z);return 0;}
  t->surfaceImported=1;t->surfaceTargetVersion=1;t->importedHandle=handle;
  t->importedOwner=[self retain];t->importedSurface=api->cfRetain((void *)surface);t->releaseImportedSurface=api->cfRelease;
  if(!t->importedOwner||!t->importedSurface){unsigned z=0;self->scalar(self->connection,14,&handle,1,0,&z);t->importedHandle=0;[t release];return 0;}
  t->textureWidth=width;t->textureHeight=height;t->textureDepth=1;t->textureKind=2;t->textureSamples=1;t->format=80;t->textureUsage=[d usage];t->rowBytes=stride;t->levelCount=1;
  t->arrayLength=1;t->layers=1;t->levelDepth[0]=1;
  t->surfaceAllocSize=kernelAllocSize;t->surfaceRowBytes=kernelRowBytes;
  t->mip[0]=(struct tegu_texture_level){width,height,stride,surfaceOffset};return t;
 }
 /* Sampled-only single-plane surfaces (c6fc: SpringBoard-made 'LA08' layer
  * contents as RG8Unorm, 230x186 row 480). arch9 linear sampling needs
  * 64-byte rows, so the surface is read through an exact owned shadow that
  * tegu_shadow_sync_jobs refreshes before every command buffer using it; a
  * sampled-only texture is never written back. The fourcc must name exactly
  * the descriptor's byte layout. */
 if((self->capabilities&TEGU_GPU_CAP_GENERIC_JOB)&&d&&surface&&plane==0&&[d usage]==1&&[d textureType]==2&&
    [d depth]==1&&[d mipmapLevelCount]==1&&[d sampleCount]==1&&[d arrayLength]==1&&![d resourceOptions]&&
    [d width]&&[d height]&&[d width]<=4096&&[d height]<=4096) {
  const struct TeguIOSurfaceAPI *api=tegu_iosurface_api();
  uint32_t fourcc=api?api->getPixelFormat((void *)surface):0;NSUInteger format=[d pixelFormat];
  int layout=(fourcc==TEGU_IOSURFACE_BGRA&&format==80)||(fourcc==TEGU_IOSURFACE_RGBA&&format==70)||
   (fourcc==0x4c303038u&&format==10)||        /* 'L008' one 8-bit component */
   (fourcc==0x4c413038u&&format==30)||        /* 'LA08' luminance+alpha, 2 bytes */
   (fourcc==0x32433038u&&format==30);         /* '2C08' two 8-bit components */
  NSUInteger bpp=tegu_texture_bytes_per_pixel(format),width=[d width],height=[d height];
  NSUInteger row=api?api->getBytesPerRow((void *)surface):0,alloc=api?api->getAllocSize((void *)surface):0;
  if(layout&&api&&api->getPlaneCount((void *)surface)==0&&api->getWidth((void *)surface)==width&&
     api->getHeight((void *)surface)==height&&bpp&&row>=width*bpp&&row<=65536&&height<=SIZE_MAX/row&&alloc>=row*height) {
   TeguTexture *shadow=[self newTextureWithDescriptor:d];
   if(shadow&&shadow->buffer&&shadow->format==format&&shadow->levelCount==1&&!shadow->surfaceImported&&!(shadow->rowBytes&63)) {
    shadow->shadowSurface=api->cfRetain((void *)surface);shadow->releaseImportedSurface=api->cfRelease;
    if(shadow->shadowSurface){tegu_shadow_trace(width,height,row);return shadow;}
   }
   [shadow release];tegu_iosurface_refused(d,surface,plane,"sampled-shadow-allocation");return 0;
  }
 }
 if(!(self->capabilities&512)){MARK("UNSUPPORTED iosurface_target_capability");
  tegu_iosurface_refused(d,surface,plane,"outside-bgra8-target-class");return 0;}
 const struct TeguIOSurfaceAPI *api=tegu_iosurface_api();
 if(!api||!d||!surface||plane||[d textureType]!=2||([d pixelFormat]!=70&&[d pixelFormat]!=80)||
    [d width]!=16||[d height]!=16||[d depth]!=1||[d mipmapLevelCount]!=1||[d sampleCount]!=1||[d arrayLength]!=1||
    [d resourceOptions]||([d usage]!=0&&[d usage]!=4)){tegu_iosurface_refused(d,surface,plane,"outside-import-classes");return 0;}
 uint32_t fourcc=api->getPixelFormat((void *)surface),expected=[d pixelFormat]==70?TEGU_IOSURFACE_RGBA:TEGU_IOSURFACE_BGRA;
 uint32_t id=api->getID((void *)surface);NSUInteger stride=api->getBytesPerRow((void *)surface),allocSize=api->getAllocSize((void *)surface);
 if(!id||fourcc!=expected||api->getPlaneCount((void *)surface)!=0||api->getWidth((void *)surface)!=16||
    api->getHeight((void *)surface)!=16||stride!=64||allocSize<4096||allocSize>(256u<<20)||(allocSize&4095)){
  tegu_iosurface_refused(d,surface,plane,"16x16-surface-shape");return 0;}
 U64 sid=id,handle=0;unsigned n=1;
 int status=!self->method||!self->scalar?-1:self->method(self->connection,13,&sid,1,0,0,&handle,&n,0,0);
 int knownHandle=handle>=0xa0000001u&&handle<=0xafffffffu;
 if(status||n!=1||!knownHandle){if(knownHandle&&self->scalar){unsigned z=0;self->scalar(self->connection,14,&handle,1,0,&z);}return 0;}
 TeguTexture *t=[[TeguTexture alloc] init];if(!t){unsigned z=0;self->scalar(self->connection,14,&handle,1,0,&z);return 0;}
 t->surfaceImported=1;t->importedOwner=[self retain];t->importedSurface=api->cfRetain((void *)surface);t->releaseImportedSurface=api->cfRelease;t->importedHandle=handle;
 t->textureWidth=16;t->textureHeight=16;t->textureDepth=1;t->textureKind=2;t->textureSamples=1;t->format=[d pixelFormat];t->textureUsage=[d usage];t->rowBytes=64;t->levelCount=1;
 t->arrayLength=1;t->layers=1;t->levelDepth[0]=1;
 t->surfaceAllocSize=allocSize;t->surfaceRowBytes=stride;
 t->mip[0]=(struct tegu_texture_level){16,16,64,0};return t;
}
@end
static int tegu_r58_private_allocation_only(TeguTexture *t,TeguComputeDevice *owner) {
 if(!t||[t device]!=owner||t->surfaceImported||!t->buffer||t->buffer->owner!=owner||
    t->textureKind!=2||t->textureDepth!=1||t->textureSamples!=1||t->textureResourceOptions!=0x20||
    t->levelCount!=1||t->mip[0].width!=t->textureWidth||t->mip[0].height!=t->textureHeight||
    t->mip[0].offset)return 0;
 return (t->textureWidth==594&&t->textureHeight==594&&
          ((t->format==10&&(t->textureUsage==5||t->textureUsage==7)&&t->rowBytes==640&&t->buffer->logical==380160)||
           (t->format==80&&t->textureUsage==5&&t->rowBytes==2432&&t->buffer->logical==1444608)))||
        (t->textureWidth==297&&t->textureHeight==297&&t->textureUsage==7&&
          t->format==80&&t->rowBytes==1216&&t->buffer->logical==361152);
}
static int tegu_memoryless_msaa_resolve_texture_pair(TeguTexture *target,TeguTexture *resolve,TeguComputeDevice *owner) {
 if(!tegu_owned_class(target,"TeguTexture")||!tegu_owned_class(resolve,"TeguTexture")||target==resolve||
    target->surfaceImported||resolve->surfaceImported||[target device]!=owner||[resolve device]!=owner)return 0;
 if(target->buffer||target->transientOwner!=owner||[target storageMode]!=3||target->textureKind!=4||
    target->textureSamples!=4||target->format!=80||target->textureDepth!=1||target->levelCount!=1||
    target->textureUsage!=4)return 0;
 if(!resolve->buffer||resolve->buffer->owner!=owner||[resolve storageMode]!=2||resolve->textureKind!=2||
    resolve->textureSamples!=1||resolve->format!=target->format||resolve->textureDepth!=1||
    resolve->textureWidth!=target->textureWidth||resolve->textureHeight!=target->textureHeight||
    !(resolve->textureUsage&4)||resolve->mip[0].width!=resolve->textureWidth||
    resolve->mip[0].height!=resolve->textureHeight)return 0;
 return tegu_texture_level_rows(resolve,0,0,0,resolve->textureWidth,resolve->textureHeight,0);
}
@interface NSObject (TeguPassDiagnosticAvailability)
- (signed char)respondsToSelector:(SEL)selector;
@end
@interface TeguPassAttachment : NSObject
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
@interface TeguPassDescriptor : NSObject
- (id)colorAttachments;
- (TeguPassAttachment *)depthAttachment;
- (TeguPassAttachment *)stencilAttachment;
- (NSUInteger)renderTargetArrayLength;
- (NSUInteger)renderTargetWidth;
- (NSUInteger)renderTargetHeight;
- (NSUInteger)defaultRasterSampleCount;
- (id)visibilityResultBuffer;
@end
static int tegu_render_has_selector(id object,SEL selector) {
 return object&&[object respondsToSelector:@selector(respondsToSelector:)]&&
        [object respondsToSelector:selector];
}
static int tegu_render_resolve_subresources_zero(TeguPassAttachment *color) {
 if(!tegu_render_has_selector(color,@selector(level))||
    !tegu_render_has_selector(color,@selector(slice))||
    !tegu_render_has_selector(color,@selector(depthPlane))||
    !tegu_render_has_selector(color,@selector(resolveLevel))||
    !tegu_render_has_selector(color,@selector(resolveSlice))||
    !tegu_render_has_selector(color,@selector(resolveDepthPlane)))return 0;
 return ![color level]&&![color slice]&&![color depthPlane]&&
        ![color resolveLevel]&&![color resolveSlice]&&![color resolveDepthPlane];
}
#ifndef TEGU_RANGE_DEFINED
#define TEGU_RANGE_DEFINED 1
typedef struct {NSUInteger location,length;} TeguRange;
#endif
typedef struct {double originX,originY,width,height,znear,zfar;} TeguViewport;
typedef struct {NSUInteger x,y,width,height;} TeguScissorRect;
_Static_assert(sizeof(TeguViewport)==48,"Metal viewport ABI");
_Static_assert(sizeof(TeguScissorRect)==4*sizeof(NSUInteger),"Metal scissor ABI");
@interface TeguRenderEncoder : NSObject {
@public
 TeguCommand *command;TeguTexture *target;TeguRenderPipeline *pipeline;
 TeguTexture *vertexTextures[31],*fragmentTextures[31];TeguSampler *vertexSamplers[16],*fragmentSamplers[16];
 TeguBuffer *vertexBuffers[31],*fragmentBuffers[31];
 TeguBuffer *expandArena;NSUInteger expandArenaUsed;
 NSUInteger vertexOffsets[31],fragmentOffsets[31];id debugLabel;
 TeguScissorRect scissor;
 TeguViewport nord2Viewport;unsigned nord2HasViewport;
 uint32_t clear;unsigned draws,ended,loadAction,storeAction,compositorDrawClass,observerPassOnly,memorylessResolveOnly,r8ClearOnly,skipPass,pipelineRefused,viewportRefused;
 uint32_t preserveTargets;
 /* A pass rejected before any attachment is usable. It still returns a real
  * encoder (RenderBox caches IMPs without a nil check); every selector is a
  * no-op and endEncoding completes the already-invalid command. */
 unsigned inert,hasDepthAttachment,hasStencilAttachment;
 TeguDepthStencilState *depthState;
 NSUInteger passDepthFormat,passStencilFormat;
 TeguTexture *attachmentClearTargets[2],*resolveTarget;
 uint32_t attachmentClearColors[2];unsigned attachmentClearLoads[2],attachmentClearStores[2],twoTargetPass,attachmentClearPass,attachmentClearElided;
 /* MRT emulation: the RT1 a single-target generic pass ignores, rendered by
  * its own jobs (load action, clear, how many jobs have touched it). */
 TeguTexture *mrtTarget,*mrtTarget2,*mrtTarget3;unsigned mrtLoad,mrtLoad2,mrtLoad3,mrtDraws,mrtDraws2,mrtDraws3;uint32_t mrtClear,mrtClear2,mrtClear3;
 /* Generic MRT slots. Slot0 remains `target` for older paths; every generic
  * read/write uses these exact arrays so an unbound RT0 never aliases RT2. */
 TeguTexture *renderTargets[4],*resolveTargets[4];unsigned renderLoads[4],renderJobs[4],renderTargetsValid;uint32_t renderClears[4][4];unsigned rasterSampleCount;
 uint32_t nord2Clear[4][4];TeguBuffer *nord2MrtSnapshots[4];
 /* Primitive of the generic draw being snapshotted (TEGU_GENERIC_DRAW_*). */
 unsigned drawPrimitive;
 /* Jobs this pass queued (first index, RT0 generic jobs): a clear happens on
  * the first job that really renders, or at endEncoding if none does. */
 unsigned passJobStart,rt0Jobs;
 /* TEGU_PERF_BATCH: the pass's loadAction was DontCare (run as CLEAR). */
 unsigned perfDontCare;
 /* Index buffer/baseVertex of the native indexed generic draw being snapshotted. */
 TeguBuffer *drawIndexBuffer;NSUInteger drawIndexOffset;unsigned drawIndexSize;int32_t drawIndexBaseVertex;
#ifdef TEGU_RENDER_TRACE
 unsigned traceID,traceDrawAttempts;
#endif
}
- (id)initWithCommand:(TeguCommand *)c target:(TeguTexture *)t clear:(uint32_t)value loadAction:(unsigned)action;
- (void)setRenderPipelineState:(TeguRenderPipeline *)p;
- (void)setVertexBuffer:(TeguBuffer *)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index;
- (void)setVertexBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)index;
- (void)setVertexBytes:(const void *)bytes length:(NSUInteger)length atIndex:(NSUInteger)index;
- (void)setFragmentBuffer:(TeguBuffer *)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index;
- (void)setFragmentBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)index;
- (void)setFragmentBytes:(const void *)bytes length:(NSUInteger)length atIndex:(NSUInteger)index;
- (void)setVertexTexture:(TeguTexture *)texture atIndex:(NSUInteger)index;
- (void)setVertexSamplerState:(TeguSampler *)sampler atIndex:(NSUInteger)index;
- (void)setFragmentTexture:(TeguTexture *)texture atIndex:(NSUInteger)index;
- (void)setFragmentSamplerState:(TeguSampler *)sampler atIndex:(NSUInteger)index;
- (void)setVertexBuffers:(TeguBuffer *const *)buffers offsets:(const NSUInteger *)offsets withRange:(TeguRange)range;
- (void)setFragmentBuffers:(TeguBuffer *const *)buffers offsets:(const NSUInteger *)offsets withRange:(TeguRange)range;
- (void)setVertexTextures:(TeguTexture *const *)textures withRange:(TeguRange)range;
- (void)setFragmentTextures:(TeguTexture *const *)textures withRange:(TeguRange)range;
- (void)setVertexSamplerStates:(TeguSampler *const *)samplers withRange:(TeguRange)range;
- (void)setFragmentSamplerStates:(TeguSampler *const *)samplers withRange:(TeguRange)range;
- (void)setViewport:(TeguViewport)viewport;
- (void)setScissorRect:(TeguScissorRect)rect;
- (void)setColorStoreAction:(NSUInteger)action atIndex:(NSUInteger)index;
- (void)setCullMode:(NSUInteger)mode;
- (void)setFrontFacingWinding:(NSUInteger)winding;
- (void)setTriangleFillMode:(NSUInteger)mode;
- (void)drawPrimitives:(NSUInteger)type vertexStart:(NSUInteger)start vertexCount:(NSUInteger)count;
- (void)drawPrimitives:(NSUInteger)type vertexStart:(NSUInteger)start vertexCount:(NSUInteger)count
 instanceCount:(NSUInteger)instances;
- (void)drawPrimitives:(NSUInteger)type vertexStart:(NSUInteger)start vertexCount:(NSUInteger)count
 instanceCount:(NSUInteger)instances baseInstance:(NSUInteger)baseInstance;
- (void)drawIndexedPrimitives:(NSUInteger)type indexCount:(NSUInteger)count indexType:(NSUInteger)indexType
 indexBuffer:(TeguBuffer *)indexBuffer indexBufferOffset:(NSUInteger)offset;
- (void)drawIndexedPrimitives:(NSUInteger)type indexCount:(NSUInteger)count indexType:(NSUInteger)indexType
 indexBuffer:(TeguBuffer *)indexBuffer indexBufferOffset:(NSUInteger)offset instanceCount:(NSUInteger)instances;
- (void)drawIndexedPrimitives:(NSUInteger)type indexCount:(NSUInteger)count indexType:(NSUInteger)indexType
 indexBuffer:(TeguBuffer *)indexBuffer indexBufferOffset:(NSUInteger)offset instanceCount:(NSUInteger)instances
 baseVertex:(long)baseVertex baseInstance:(NSUInteger)baseInstance;
- (void)dispatchThreadsPerTile:(MTLSize)threadsPerTile;
- (void)setTileBytes:(const void *)bytes length:(NSUInteger)length atIndex:(NSUInteger)index;
- (void)setTileTexture:(TeguTexture *)texture atIndex:(NSUInteger)index;
- (void)endEncoding;
@end
static int tegu_b3_private_mip0_texture(TeguTexture *,TeguComputeDevice *);
#ifdef TEGU_RENDER_TRACE
#define TEGU_RENDER_TRACE_NO_ID 0xffffffffu
static unsigned tegu_render_trace_passes,tegu_render_trace_draws,tegu_render_trace_rt1_captures;
static unsigned tegu_render_trace_b3_encoder_events;
static unsigned tegu_render_trace_late_encoder_events;
static unsigned tegu_render_trace_sphere_events;
static unsigned tegu_render_trace_pipeline_bind_events;
static unsigned tegu_render_trace_draw_variant_events;
static unsigned tegu_render_trace_ui2_bind_events;
static unsigned tegu_render_trace_claim(unsigned *counter,unsigned limit) {
 unsigned next=__atomic_load_n(counter,__ATOMIC_RELAXED);
 while(next<limit) {
  if(__atomic_compare_exchange_n(counter,&next,next+1,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED))return next;
 }
 return TEGU_RENDER_TRACE_NO_ID;
}
static unsigned tegu_render_trace_pass(TeguTexture *target,NSUInteger load,NSUInteger store) {
 unsigned id=tegu_render_trace_claim(&tegu_render_trace_passes,64);
 if(id==TEGU_RENDER_TRACE_NO_ID)return id;
 unsigned kind=!target?0:target->surfaceImported?2:target->buffer?1:0;
 NSUInteger width=target?target->textureWidth:0,height=target?target->textureHeight:0;
 NSUInteger stride=target?target->rowBytes:0,format=target?target->format:0;
 char line[224];int n=snprintf(line,sizeof(line),
  "TEGU_UI_PASS id=%u target=%u width=%llu height=%llu stride=%llu format=%llu load=%llu store=%llu phase=descriptor\n",
  id,kind,(U64)width,(U64)height,(U64)stride,(U64)format,(U64)load,(U64)store);
 if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
 return id;
}
static unsigned tegu_render_trace_object_id(id objects[16],unsigned *count,id object) {
 if(!object)return 0;
 for(unsigned i=0;i<*count;i++)if(objects[i]==object)return i+1;
 if(*count>=16)return 0;
 objects[*count]=object;return ++*count;
}
static int tegu_render_trace_has_rt1(id colors) {
 if(!tegu_render_has_selector(colors,@selector(objectAtIndexedSubscript:)))return 0;
 for(unsigned slot=1;slot<8;slot++) {
  id attachment=[colors objectAtIndexedSubscript:slot];
  if(tegu_render_has_selector(attachment,@selector(texture))&&[attachment texture])return 1;
  if(tegu_render_has_selector(attachment,@selector(resolveTexture))&&[attachment resolveTexture])return 1;
 }
 return 0;
}
static void tegu_render_trace_texture_row(unsigned pass,unsigned capture,unsigned slot,const char *role,unsigned objectID,id texture) {
 unsigned klass=0,kind=0;U64 handle=0,surfaceID=0;NSUInteger width=0,height=0,stride=0,format=0,offset=0,allocation=0;
 if(texture) {
  klass=tegu_owned_class(texture,"TeguTexture")?1:2;
  if(klass==1) {
   TeguTexture *t=(TeguTexture *)texture;
   width=t->textureWidth;height=t->textureHeight;stride=t->rowBytes;format=t->format;
   offset=t->mip[0].offset;allocation=t->surfaceImported?t->surfaceAllocSize:(t->buffer?t->buffer->logical:0);
   if(t->surfaceImported) {
    kind=2;handle=t->importedHandle;
    const struct TeguIOSurfaceAPI *api=t->importedSurface?tegu_iosurface_api():0;
    if(api)surfaceID=api->getID(t->importedSurface);
   } else if(t->buffer) {kind=1;handle=t->buffer->handle;}
   else kind=3;
  }
 }
 char line[320];int n=snprintf(line,sizeof(line),
  "TEGU_UI_%s_TEXTURE id=%u slot=%u role=%s present=%u class=%u kind=%u object_id=%u handle=%016llx surface_id=%llu width=%llu height=%llu format=%llu stride=%llu offset=%llu alloc=%llu\n",
  capture?"RT1_CAPTURE":"ATTACHMENT",pass,slot,role,texture!=0,klass,kind,objectID,(unsigned long long)handle,
  (unsigned long long)surfaceID,(unsigned long long)width,(unsigned long long)height,
  (unsigned long long)format,(unsigned long long)stride,(unsigned long long)offset,
  (unsigned long long)allocation);
 if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
}
static void tegu_render_trace_pass_details(unsigned passID,unsigned capture,TeguPassDescriptor *descriptor,id colors) {
 if(passID==TEGU_RENDER_TRACE_NO_ID)return;
 unsigned widthKnown=tegu_render_has_selector(descriptor,@selector(renderTargetWidth));
 unsigned heightKnown=tegu_render_has_selector(descriptor,@selector(renderTargetHeight));
 unsigned arrayKnown=tegu_render_has_selector(descriptor,@selector(renderTargetArrayLength));
 unsigned samplesKnown=tegu_render_has_selector(descriptor,@selector(defaultRasterSampleCount));
 NSUInteger width=widthKnown?[descriptor renderTargetWidth]:0;
 NSUInteger height=heightKnown?[descriptor renderTargetHeight]:0;
 NSUInteger array=arrayKnown?[descriptor renderTargetArrayLength]:0;
 NSUInteger samples=samplesKnown?[descriptor defaultRasterSampleCount]:0;
 unsigned depthKnown=tegu_render_has_selector(descriptor,@selector(depthAttachment));
 unsigned stencilKnown=tegu_render_has_selector(descriptor,@selector(stencilAttachment));
 unsigned visibilityKnown=tegu_render_has_selector(descriptor,@selector(visibilityResultBuffer));
 id depthAttachment=depthKnown?[descriptor depthAttachment]:0;
 id stencilAttachment=stencilKnown?[descriptor stencilAttachment]:0;
 unsigned depthTextureKnown=tegu_render_has_selector(depthAttachment,@selector(texture));
 unsigned stencilTextureKnown=tegu_render_has_selector(stencilAttachment,@selector(texture));
 id depthTexture=depthTextureKnown?[depthAttachment texture]:0;
 id stencilTexture=stencilTextureKnown?[stencilAttachment texture]:0;
 id visibility=visibilityKnown?[descriptor visibilityResultBuffer]:0;
 char state[224];int n=snprintf(state,sizeof(state),
  "TEGU_UI_%s_META id=%u width=%llu width_known=%u height=%llu height_known=%u array=%llu array_known=%u samples=%llu samples_known=%u viewport=not_exposed scissor=not_exposed\n",
  capture?"RT1_CAPTURE":"PASS",passID,(unsigned long long)width,widthKnown,(unsigned long long)height,heightKnown,
  (unsigned long long)array,arrayKnown,(unsigned long long)samples,samplesKnown);
 if(n>0&&(NSUInteger)n<sizeof(state))write(2,state,(NSUInteger)n);
 char aux[160];n=snprintf(aux,sizeof(aux),
  "TEGU_UI_%s_AUX id=%u depth_known=%u depth_texture=%u stencil_known=%u stencil_texture=%u visibility_known=%u visibility=%u\n",
  capture?"RT1_CAPTURE":"PASS",passID,depthKnown,depthTexture!=0,stencilKnown,stencilTexture!=0,visibilityKnown,visibility!=0);
 if(n>0&&(NSUInteger)n<sizeof(aux))write(2,aux,(NSUInteger)n);
 unsigned subscriptKnown=tegu_render_has_selector(colors,@selector(objectAtIndexedSubscript:));
 id passObjects[16]={0};unsigned passObjectCount=0;
 for(unsigned slot=0;slot<8;slot++) {
  id attachment=subscriptKnown?[colors objectAtIndexedSubscript:slot]:0;
  unsigned textureKnown=tegu_render_has_selector(attachment,@selector(texture));
  unsigned resolveKnown=tegu_render_has_selector(attachment,@selector(resolveTexture));
  unsigned levelKnown=tegu_render_has_selector(attachment,@selector(level));
  unsigned sliceKnown=tegu_render_has_selector(attachment,@selector(slice));
  unsigned depthKnown=tegu_render_has_selector(attachment,@selector(depthPlane));
  unsigned loadKnown=tegu_render_has_selector(attachment,@selector(loadAction));
  unsigned storeKnown=tegu_render_has_selector(attachment,@selector(storeAction));
  unsigned clearKnown=tegu_render_has_selector(attachment,@selector(clearColor));
  id texture=textureKnown?[attachment texture]:0;
  id resolve=resolveKnown?[attachment resolveTexture]:0;
  NSUInteger level=levelKnown?[attachment level]:0,slice=sliceKnown?[attachment slice]:0;
  NSUInteger depth=depthKnown?[attachment depthPlane]:0,load=loadKnown?[attachment loadAction]:0;
  NSUInteger store=storeKnown?[attachment storeAction]:0;
  TeguClearColor c={0,0,0,0};if(clearKnown)c=[attachment clearColor];
  char row[288];n=snprintf(row,sizeof(row),
   "TEGU_UI_%s_ATTACHMENT id=%u slot=%u present=%u texture_known=%u resolve=%u resolve_known=%u level=%llu level_known=%u slice=%llu slice_known=%u depth_plane=%llu depth_known=%u load=%llu load_known=%u store=%llu store_known=%u clear_known=%u clear_rgba=%.7g,%.7g,%.7g,%.7g\n",
   capture?"RT1_CAPTURE":"PASS",passID,slot,attachment!=0,textureKnown,resolve!=0,resolveKnown,(unsigned long long)level,levelKnown,
   (unsigned long long)slice,sliceKnown,(unsigned long long)depth,depthKnown,(unsigned long long)load,
   loadKnown,(unsigned long long)store,storeKnown,clearKnown,c.red,c.green,c.blue,c.alpha);
  if(n>0&&(NSUInteger)n<sizeof(row))write(2,row,(NSUInteger)n);
  unsigned textureID=tegu_render_trace_object_id(passObjects,&passObjectCount,texture);
  unsigned resolveID=tegu_render_trace_object_id(passObjects,&passObjectCount,resolve);
  tegu_render_trace_texture_row(passID,capture,slot,"target",textureID,texture);
  if(resolve)tegu_render_trace_texture_row(passID,capture,slot,"resolve",resolveID,resolve);
 }
}
static void tegu_render_trace_pass_accepted(unsigned id) {
 if(id==TEGU_RENDER_TRACE_NO_ID)return;
 char line[64];int n=snprintf(line,sizeof(line),"TEGU_UI_PASS id=%u phase=accepted\n",id);
 if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
}
static void tegu_render_trace_extra_target(unsigned id,unsigned attachment) {
 if(id==TEGU_RENDER_TRACE_NO_ID)return;
 char line[112];int n=snprintf(line,sizeof(line),
  "TEGU_UI_PASS id=%u phase=refused reason=extra_target attachment=%u\n",id,attachment);
 if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
}
static void tegu_render_trace_rt1_refused(unsigned id,unsigned attachment) {
 if(id==TEGU_RENDER_TRACE_NO_ID)return;
 char line[128];int n=snprintf(line,sizeof(line),"TEGU_UI_RT1_CAPTURE id=%u phase=refused reason=extra_target attachment=%u\n",id,attachment);
 if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
}
static void tegu_render_trace_resolve_refused(unsigned id,unsigned attachment,int capture) {
 if(id==TEGU_RENDER_TRACE_NO_ID)return;
 char line[128];int n=snprintf(line,sizeof(line),
  "TEGU_UI_%s id=%u phase=refused reason=resolve_target attachment=%u\n",
  capture?"RT1_CAPTURE":"PASS",id,attachment);
 if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
}
static void tegu_render_trace_draw(unsigned pass,unsigned sequence,NSUInteger type,NSUInteger start,
 NSUInteger count,TeguRenderPipeline *pipeline) {
 if(pass==TEGU_RENDER_TRACE_NO_ID)return;
 unsigned id=tegu_render_trace_claim(&tegu_render_trace_draws,128);
 if(id==TEGU_RENDER_TRACE_NO_ID)return;
 U64 slot=pipeline&&pipeline->vertexFetch.count?pipeline->vertexFetch.attributes[0].buffer_index:UINT32_MAX;
 char line[224];int n=snprintf(line,sizeof(line),
  "TEGU_UI_DRAW id=%u pass=%u pass_draw=%u variant=drawPrimitives_3arg type=%llu start=%llu count=%llu vertex_slot=%llu\n",
  id,pass,sequence,(U64)type,(U64)start,(U64)count,slot);
 if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
}
static void tegu_render_trace_b3_encoder(const char *phase,unsigned pass,int accepted,
 unsigned exact,unsigned draws,unsigned jobs,unsigned invalid) {
 unsigned id=tegu_render_trace_claim(&tegu_render_trace_b3_encoder_events,16);
 if(id==TEGU_RENDER_TRACE_NO_ID)return;
 char line[192];int n=snprintf(line,sizeof(line),
  "TEGU_B3_ENCODER id=%u phase=%s pass=%u accepted=%d exact=%u draws=%u jobs=%u invalid=%u\n",
  id,phase,pass,accepted,exact,draws,jobs,invalid);
 if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
}
/* Descriptor detail is intentionally capped because one pass emits many rows.
 * Preserve a small independent lifecycle window after that cap so a late UI
 * client can still prove that it bound a pipeline, attempted a draw and ended
 * encoding. */
static void tegu_render_trace_late_encoder(const char *phase,unsigned pass,int accepted,
 TeguRenderPipeline *pipeline,unsigned draws,unsigned jobs,unsigned invalid) {
 if(pass!=TEGU_RENDER_TRACE_NO_ID)return;
 unsigned id=tegu_render_trace_claim(&tegu_render_trace_late_encoder_events,64);
 if(id==TEGU_RENDER_TRACE_NO_ID)return;
 char line[224];int n=snprintf(line,sizeof(line),
  "TEGU_UI_ENCODER id=%u phase=%s pass=untraced accepted=%d pipeline=%u compositor=%u b3=%u draws=%u jobs=%u invalid=%u\n",
  id,phase,accepted,pipeline!=0,pipeline?(unsigned)!!pipeline->compositorSnapshotOnly:0,
  pipeline?(unsigned)!!pipeline->compositorB3:0,draws,jobs,invalid);
 if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
}
static void tegu_render_trace_sphere_encoder(const char *phase,TeguRenderEncoder *e,int result,
 NSUInteger type,NSUInteger start,NSUInteger count,NSUInteger instances,NSUInteger baseInstance) {
 if(!e||!e->r8ClearOnly||!e->target||e->target->format!=10||
    e->target->textureWidth!=594||e->target->textureHeight!=594)return;
 unsigned id=tegu_render_trace_claim(&tegu_render_trace_sphere_events,32);
 if(id==TEGU_RENDER_TRACE_NO_ID)return;
 uint32_t vb=0,fb=0,vt=0,ft=0,vs=0,fs=0;
 for(unsigned i=0;i<31;i++) {if(e->vertexBuffers[i])vb|=1u<<i;if(e->fragmentBuffers[i])fb|=1u<<i;
  if(e->vertexTextures[i])vt|=1u<<i;if(e->fragmentTextures[i])ft|=1u<<i;}
 for(unsigned i=0;i<16;i++){if(e->vertexSamplers[i])vs|=1u<<i;if(e->fragmentSamplers[i])fs|=1u<<i;}
 char line[256];int n=snprintf(line,sizeof(line),
  "TEGU_SPHERE_ENCODER id=%u phase=%s result=%d pipeline=%u resources=v%08x/f%08x/vt%08x/ft%08x/vs%04x/fs%04x draw=%llu/%llu/%llu/%llu/%llu\n",
  id,phase,result,e->pipeline?(unsigned)!!e->pipeline->sphereObserverOnly:0,vb,fb,vt,ft,vs,fs,
  (U64)type,(U64)start,(U64)count,(U64)instances,(U64)baseInstance);
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
}
static void tegu_render_trace_pipeline_bind(TeguRenderEncoder *e,TeguRenderPipeline *p,int accepted) {
 unsigned id=tegu_render_trace_claim(&tegu_render_trace_pipeline_bind_events,64);
 if(id==TEGU_RENDER_TRACE_NO_ID)return;
 char line[256];int n=snprintf(line,sizeof(line),
  "TEGU_PIPELINE_BIND id=%u pass=%u accepted=%d target=%lux%lu/f%lu pipeline=f%lu/h%u/c%u/b3%u/home%u/sphere%u draws=%u jobs=%u invalid=%u\n",
  id,e?e->traceID:TEGU_RENDER_TRACE_NO_ID,accepted,
  (unsigned long)(e&&e->target?e->target->textureWidth:0),
  (unsigned long)(e&&e->target?e->target->textureHeight:0),
  (unsigned long)(e&&e->target?e->target->format:0),
  (unsigned long)(p?p->colorFormat:0),p?(unsigned)!!p->handle:0,
  p?(unsigned)!!p->compositorSnapshotOnly:0,p?(unsigned)!!p->compositorB3:0,
  p?p->homeAffordanceObserverOnly:0,p?(unsigned)!!p->sphereObserverOnly:0,
  e?e->draws:0,e&&e->command?e->command->count:0,e&&e->command?e->command->invalid:0);
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
}
static void tegu_render_trace_draw_variant(const char *variant,TeguRenderEncoder *e,int result,
 NSUInteger type,NSUInteger count,NSUInteger instances,long baseVertex,NSUInteger baseInstance) {
 unsigned id=tegu_render_trace_claim(&tegu_render_trace_draw_variant_events,64);
 if(id==TEGU_RENDER_TRACE_NO_ID)return;
 char line[224];int n=snprintf(line,sizeof(line),
  "TEGU_DRAW_VARIANT id=%u variant=%s result=%d pass=%u target=f%lu pipeline=c%u/b3%u/home%u/sphere%u type=%llu count=%llu instances=%llu baseVertex=%lld baseInstance=%llu\n",
  id,variant,result,e?e->traceID:TEGU_RENDER_TRACE_NO_ID,
  (unsigned long)(e&&e->target?e->target->format:0),
  e&&e->pipeline?(unsigned)!!e->pipeline->compositorSnapshotOnly:0,
  e&&e->pipeline?(unsigned)!!e->pipeline->compositorB3:0,
  e&&e->pipeline?e->pipeline->homeAffordanceObserverOnly:0,
  e&&e->pipeline?(unsigned)!!e->pipeline->sphereObserverOnly:0,
  (U64)type,(U64)count,(U64)instances,(long long)baseVertex,(U64)baseInstance);
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
}
static void tegu_render_trace_ui2_bind(int result,const struct tegu_ui2_info *info,
 const struct tegu_ui2_bind_request *request) {
 unsigned id=tegu_render_trace_claim(&tegu_render_trace_ui2_bind_events,8);
 if(id==TEGU_RENDER_TRACE_NO_ID||!info||!request)return;
 static const char digits[]="0123456789abcdef";
 char row[1408];const unsigned char *objects[2]={(const unsigned char *)info,(const unsigned char *)request};
 const size_t sizes[2]={sizeof(*info),sizeof(*request)};const char *kinds[2]={"info","request"};
 for(unsigned object=0;object<2;object++) {
  int n=snprintf(row,sizeof(row),"TEGU_UI2_BIND_REFUSED id=%u result=%d kind=%s bytes=%lu hex=",
   id,result,kinds[object],(unsigned long)sizes[object]);
  if(n<=0||(size_t)n+sizes[object]*2+1>sizeof(row))continue;
  size_t at=(size_t)n;for(size_t i=0;i<sizes[object];i++) {
   row[at++]=digits[objects[object][i]>>4];row[at++]=digits[objects[object][i]&15];
  }
  row[at++]='\n';write(2,row,at);
 }
}
#else
#define TEGU_RENDER_TRACE_NO_ID 0xffffffffu
#define tegu_render_trace_pass(target,load,store) TEGU_RENDER_TRACE_NO_ID
#define tegu_render_trace_has_rt1(colors) 0
#define tegu_render_trace_pass_details(id,capture,descriptor,colors) ((void)0)
#define tegu_render_trace_pass_accepted(id) ((void)0)
#define tegu_render_trace_extra_target(id,attachment) ((void)0)
#define tegu_render_trace_rt1_refused(id,attachment) ((void)0)
#define tegu_render_trace_resolve_refused(id,attachment,capture) ((void)0)
#define tegu_render_trace_draw(pass,sequence,type,start,count,pipeline) ((void)0)
#define tegu_render_trace_b3_encoder(phase,pass,accepted,exact,draws,jobs,invalid) ((void)0)
#define tegu_render_trace_late_encoder(phase,pass,accepted,pipeline,draws,jobs,invalid) ((void)0)
#define tegu_render_trace_sphere_encoder(phase,encoder,result,type,start,count,instances,base) ((void)0)
#define tegu_render_trace_pipeline_bind(encoder,pipeline,accepted) ((void)0)
#define tegu_render_trace_draw_variant(variant,encoder,result,type,count,instances,baseVertex,baseInstance) ((void)0)
#define tegu_render_trace_ui2_bind(result,info,request) ((void)0)
#endif
static void tegu_compositor_job_release(struct Dispatch *);
static void tegu_render_preserve_targets(TeguRenderEncoder *,uint32_t);
#ifdef TEGU_INERT_PIPELINES
static TeguRenderEncoder *tegu_render_inert_encoder(TeguCommand *command,const char *reason) {
 TeguRenderEncoder *e=[[TeguRenderEncoder alloc] initWithCommand:command target:0 clear:0 loadAction:1];
 if(!e)return 0;
 e->inert=1;e->skipPass=1;e->ended=1;
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)<32) {
  char line[240];int n=snprintf(line,sizeof(line),"TEGU_RENDER_INERT_ENCODER reason=%.180s\n",
   reason?reason:"command already closed");
  if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
 }
 return [e autorelease];
}
#endif
@implementation TeguRenderEncoder
- (id)initWithCommand:(TeguCommand *)c target:(TeguTexture *)t clear:(uint32_t)value loadAction:(unsigned)action {
 self=[super init];if(self){command=[c retain];target=[t retain];clear=value;loadAction=action;passJobStart=c->count;
  renderTargets[0]=target;renderLoads[0]=action;rasterSampleCount=t?t->textureSamples:1;
  scissor=(TeguScissorRect){0,0,t?t->textureWidth:0,t?t->textureHeight:0};}return self;
}
static TeguTexture *tegu_render_target_for_rt(TeguRenderEncoder *e,unsigned rt) {
 if(!e||rt>=4)return 0;
 if(e->renderTargetsValid)return e->renderTargets[rt];
 return rt==0?e->target:rt==1?e->mrtTarget:rt==2?e->mrtTarget2:e->mrtTarget3;
}
static unsigned tegu_render_load_for_rt(TeguRenderEncoder *e,unsigned rt) {
 if(!e||rt>=4)return 1;
 if(e->renderTargetsValid)return e->renderLoads[rt];
 return rt==0?e->loadAction:rt==1?e->mrtLoad:rt==2?e->mrtLoad2:e->mrtLoad3;
}
static unsigned tegu_render_jobs_for_rt(TeguRenderEncoder *e,unsigned rt) {
 if(!e||rt>=4)return 0;
 if(e->renderTargetsValid)return e->renderJobs[rt];
 return rt==0?e->rt0Jobs:rt==1?e->mrtDraws:rt==2?e->mrtDraws2:e->mrtDraws3;
}
static uint32_t tegu_render_clear_for_rt(TeguRenderEncoder *e,unsigned rt) {
 if(!e||rt>=4)return 0;
 if(e->renderTargetsValid)return e->renderClears[rt][0];
 return rt==0?e->clear:rt==1?e->mrtClear:rt==2?e->mrtClear2:e->mrtClear3;
}
- (id)device{return command->queue->owner;}
- (id)label{return debugLabel;}
- (void)setLabel:(id)value{if(inert)return;id copy=[value copy];[debugLabel release];debugLabel=copy;}
- (void)setViewport:(TeguViewport)v {
 if(inert)return;
#ifdef TEGU_RENDER_TRACE
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)<16) {
  U64 bits[6];memcpy(bits,&v,sizeof(bits));char line[256];int n=snprintf(line,sizeof(line),
   "TEGU_RENDER_VIEWPORT x=%016llx y=%016llx width=%016llx height=%016llx near=%016llx far=%016llx target=%llux%llu\n",
   bits[0],bits[1],bits[2],bits[3],bits[4],bits[5],
   (unsigned long long)target->textureWidth,(unsigned long long)target->textureHeight);
  if(n>0)write(2,line,(NSUInteger)n<sizeof(line)?(NSUInteger)n:sizeof(line)-1);
 }
#endif
 /* The current selector-16 backend emits this exact full-target viewport.
  * Any other viewport drops the draws made under it (until the request ABI
  * carries a dynamic viewport) instead of failing the command and its frame
  * (R180: a 127-job backboardd command). Misuse still invalidates. */
 if(ended||[command teguCommitted]){TEGU_INVALID(command);return;}
#ifdef NORD2_JM_TRANSPORT
 double values[6]={v.originX,v.originY,v.width,v.height,v.znear,v.zfar};
 for(unsigned i=0;i<6;i++)if(values[i]!=values[i]||values[i]>16384||values[i]<-16384){TEGU_INVALID(command);return;}
 if(v.width<=0||v.height<=0||v.znear<0||v.znear>1||v.zfar<0||v.zfar>1){TEGU_INVALID(command);return;}
 nord2Viewport=v;nord2HasViewport=1;viewportRefused=0;return;
#endif
 viewportRefused=v.originX!=0.0||v.originY!=0.0||
    v.width!=(double)target->textureWidth||v.height!=(double)target->textureHeight||
    v.znear!=0.0||v.zfar!=1.0;
}
- (void)setColorStoreAction:(NSUInteger)action atIndex:(NSUInteger)index {
 /* Passes admitted by nord2_render_pass always store their targets, so a
  * deferred Store or DontCare needs no change. Anything else (resolve
  * variants) is not representable for this pass and poisons the command. */
 if(ended)return;
 if(index>3||(action!=0&&action!=1))TEGU_INVALID(command);
}
- (void)setScissorRect:(TeguScissorRect)r {
 if(inert)return;
#ifdef TEGU_RENDER_TRACE
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)<32) {
  char line[224];int n=snprintf(line,sizeof(line),
   "TEGU_RENDER_SCISSOR x=%llu y=%llu width=%llu height=%llu target=%llux%llu draws=%u jobs=%u\n",
   (U64)r.x,(U64)r.y,(U64)r.width,(U64)r.height,(U64)target->textureWidth,
   (U64)target->textureHeight,draws,command->count);
  if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
 }
#endif
 /* Record every in-bounds Metal scissor. Observer-only pipelines encode no
  * work, so their state must not discard earlier supported jobs. A draw that
  * does create a backend job checks the currently supported full-target
  * contract below. */
 if(ended||[command teguCommitted]||!r.width||!r.height||r.x>target->textureWidth||
    r.y>target->textureHeight||r.width>target->textureWidth-r.x||
    r.height>target->textureHeight-r.y){TEGU_INVALID(command);return;}
 scissor=r;
}
static int tegu_render_full_scissor(TeguRenderEncoder *e) {
 return e&&e->scissor.x==0&&e->scissor.y==0&&e->scissor.width==e->target->textureWidth&&
  e->scissor.height==e->target->textureHeight;
}
- (void)setCullMode:(NSUInteger)mode {
 if(ended||[command teguCommitted]||mode)TEGU_INVALID(command);
}
- (void)setFrontFacingWinding:(NSUInteger)winding {
 /* Winding is unobservable while the only supported cull mode is None. */
 if(ended||[command teguCommitted]||winding>1)TEGU_INVALID(command);
}
- (void)setTriangleFillMode:(NSUInteger)mode {
 if(ended||[command teguCommitted]||mode)TEGU_INVALID(command);
}
- (void)setVertexBuffer:(TeguBuffer *)b offset:(NSUInteger)offset atIndex:(NSUInteger)index {
 if(ended||[command teguCommitted]||index>=31||(b&&(!tegu_owned_class(b,"TeguBuffer")||
    b->owner!=command->queue->owner||offset>b->logical))){TEGU_INVALID(command);return;}
 [b retain];[vertexBuffers[index] release];vertexBuffers[index]=b;vertexOffsets[index]=b?offset:0;
}
- (void)setVertexBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)index {
 if(ended||[command teguCommitted]||index>=31||!vertexBuffers[index]||offset>vertexBuffers[index]->logical){TEGU_INVALID(command);return;}
 vertexOffsets[index]=offset;
}
- (void)setVertexBytes:(const void *)bytes length:(NSUInteger)length atIndex:(NSUInteger)index {
 if(ended||[command teguCommitted]||index>=31||length>4096||(!bytes&&length)){TEGU_INVALID(command);return;}
 TeguBuffer *b=length?[command->queue->owner newBufferWithBytes:bytes length:length options:0]:0;
 if(length&&!b){TEGU_INVALID(command);return;}
 [self setVertexBuffer:b offset:0 atIndex:index];[b release];
}
/* Keep Metal slots separate from backend descriptor bindings. Bindings not
 * used by an accepted pipeline are legal; only its reflected resources become
 * immutable draw snapshots. Compositor resource execution is gated separately. */
- (void)setFragmentBuffer:(TeguBuffer *)b offset:(NSUInteger)offset atIndex:(NSUInteger)index {
 if(ended||[command teguCommitted]||index>=31||(b&&(!tegu_owned_class(b,"TeguBuffer")||
    b->owner!=command->queue->owner||offset>b->logical))){TEGU_INVALID(command);return;}
 [b retain];[fragmentBuffers[index] release];fragmentBuffers[index]=b;fragmentOffsets[index]=b?offset:0;
}
- (void)setFragmentBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)index {
 if(ended||[command teguCommitted]||index>=31||!fragmentBuffers[index]||offset>fragmentBuffers[index]->logical){TEGU_INVALID(command);return;}
 fragmentOffsets[index]=offset;
}
- (void)setFragmentBytes:(const void *)bytes length:(NSUInteger)length atIndex:(NSUInteger)index {
 if(ended||[command teguCommitted]||index>=31||length>4096||(!bytes&&length)){TEGU_INVALID(command);return;}
 TeguBuffer *b=length?[command->queue->owner newBufferWithBytes:bytes length:length options:0]:0;
 if(length&&!b){TEGU_INVALID(command);return;}
 [self setFragmentBuffer:b offset:0 atIndex:index];[b release];
}
- (void)setVertexTexture:(TeguTexture *)texture atIndex:(NSUInteger)index {
 if(ended||[command teguCommitted]||index>=31||
    (texture&&(!tegu_owned_class(texture,"TeguTexture")||[texture device]!=command->queue->owner))) {
  TEGU_INVALID(command);return;
 }
 [texture retain];[vertexTextures[index] release];vertexTextures[index]=texture;
}
- (void)setVertexSamplerState:(TeguSampler *)sampler atIndex:(NSUInteger)index {
 if(ended||[command teguCommitted]||index>=16||
    (sampler&&(!tegu_owned_class(sampler,"TeguSampler")||sampler->owner!=command->queue->owner))) {
  TEGU_INVALID(command);return;
 }
 [sampler retain];[vertexSamplers[index] release];vertexSamplers[index]=sampler;
}
- (void)setFragmentTexture:(TeguTexture *)texture atIndex:(NSUInteger)index {
 if(ended||[command teguCommitted]||index>=31||
    (texture&&(!tegu_owned_class(texture,"TeguTexture")||[texture device]!=command->queue->owner))) {
  TEGU_INVALID(command);return;
 }
 [texture retain];[fragmentTextures[index] release];fragmentTextures[index]=texture;
}
- (void)setFragmentSamplerState:(TeguSampler *)sampler atIndex:(NSUInteger)index {
 if(ended||[command teguCommitted]||index>=16||
    (sampler&&(!tegu_owned_class(sampler,"TeguSampler")||sampler->owner!=command->queue->owner))) {
  TEGU_INVALID(command);return;
 }
 [sampler retain];[fragmentSamplers[index] release];fragmentSamplers[index]=sampler;
}
static int tegu_render_range(TeguRange range,NSUInteger limit) {
 return range.location<=limit&&range.length<=limit-range.location;
}
static int tegu_render_bulk_buffers_valid(TeguRenderEncoder *e,TeguBuffer *const *buffers,
 const NSUInteger *offsets,TeguRange range) {
 if(!e||!tegu_render_range(range,31)||(!buffers&&range.length)||(!offsets&&range.length))return 0;
 for(NSUInteger j=0;j<range.length;j++) {
  TeguBuffer *b=buffers[j];NSUInteger offset=offsets[j];
  if(b&&(!tegu_owned_class(b,"TeguBuffer")||b->owner!=e->command->queue->owner||offset>b->logical))return 0;
  if(!b&&offset)return 0;
 }
 return 1;
}
static int tegu_render_bulk_textures_valid(TeguRenderEncoder *e,TeguTexture *const *textures,TeguRange range) {
 if(!e||!tegu_render_range(range,31)||(!textures&&range.length))return 0;
 for(NSUInteger j=0;j<range.length;j++)if(textures[j]&&
   (!tegu_owned_class(textures[j],"TeguTexture")||[textures[j] device]!=e->command->queue->owner))return 0;
 return 1;
}
static int tegu_render_bulk_samplers_valid(TeguRenderEncoder *e,TeguSampler *const *samplers,TeguRange range) {
 if(!e||!tegu_render_range(range,16)||(!samplers&&range.length))return 0;
 for(NSUInteger j=0;j<range.length;j++)if(samplers[j]&&
   (!tegu_owned_class(samplers[j],"TeguSampler")||samplers[j]->owner!=e->command->queue->owner))return 0;
 return 1;
}
- (void)setVertexBuffers:(TeguBuffer *const *)buffers offsets:(const NSUInteger *)offsets withRange:(TeguRange)range {
 if(ended||[command teguCommitted]||!tegu_render_bulk_buffers_valid(self,buffers,offsets,range)){TEGU_INVALID(command);return;}
 for(NSUInteger j=0;j<range.length;j++)[self setVertexBuffer:buffers[j] offset:offsets[j] atIndex:range.location+j];
}
- (void)setFragmentBuffers:(TeguBuffer *const *)buffers offsets:(const NSUInteger *)offsets withRange:(TeguRange)range {
 if(ended||[command teguCommitted]||!tegu_render_bulk_buffers_valid(self,buffers,offsets,range)){TEGU_INVALID(command);return;}
 for(NSUInteger j=0;j<range.length;j++)[self setFragmentBuffer:buffers[j] offset:offsets[j] atIndex:range.location+j];
}
- (void)setVertexTextures:(TeguTexture *const *)textures withRange:(TeguRange)range {
 if(ended||[command teguCommitted]||!tegu_render_bulk_textures_valid(self,textures,range)){TEGU_INVALID(command);return;}
 for(NSUInteger j=0;j<range.length;j++)[self setVertexTexture:textures[j] atIndex:range.location+j];
}
- (void)setFragmentTextures:(TeguTexture *const *)textures withRange:(TeguRange)range {
 if(ended||[command teguCommitted]||!tegu_render_bulk_textures_valid(self,textures,range)){TEGU_INVALID(command);return;}
 for(NSUInteger j=0;j<range.length;j++)[self setFragmentTexture:textures[j] atIndex:range.location+j];
}
- (void)setVertexSamplerStates:(TeguSampler *const *)samplers withRange:(TeguRange)range {
 if(ended||[command teguCommitted]||!tegu_render_bulk_samplers_valid(self,samplers,range)){TEGU_INVALID(command);return;}
 for(NSUInteger j=0;j<range.length;j++)[self setVertexSamplerState:samplers[j] atIndex:range.location+j];
}
- (void)setFragmentSamplerStates:(TeguSampler *const *)samplers withRange:(TeguRange)range {
 if(ended||[command teguCommitted]||!tegu_render_bulk_samplers_valid(self,samplers,range)){TEGU_INVALID(command);return;}
 for(NSUInteger j=0;j<range.length;j++)[self setFragmentSamplerState:samplers[j] atIndex:range.location+j];
}
- (void)setRenderPipelineState:(TeguRenderPipeline *)p {
#ifdef NORD2_JM_TRANSPORT
 if(ended||[command teguCommitted]||!p||p->owner!=command->queue->owner||(!p->genericDraw&&!p->inert)){TEGU_INVALID(command);return;}
 if(p->inert){[p retain];[pipeline release];pipeline=p;pipelineRefused=0;return;}
 if(p->genericRtPreserve)tegu_render_preserve_targets(self,p->genericRtPreserve);
 TeguTexture *mainTarget=tegu_render_target_for_rt(self,p->genericMainRt);
 if(!mainTarget||p->colorFormat!=mainTarget->format||
    (p->rasterSampleCount&&p->rasterSampleCount!=rasterSampleCount)){TEGU_INVALID(command);return;}
 [p retain];[pipeline release];pipeline=p;pipelineRefused=0;return;
#endif
 if(skipPass)return;
 TeguTexture *rt1=attachmentClearTargets[1];
 int exactB3Pair=p&&p->compositorB3&&twoTargetPass&&observerPassOnly&&rt1&&rt1!=target&&
  tegu_b3_private_mip0_texture(target,command->queue->owner)&&
  tegu_b3_private_mip0_texture(rt1,command->queue->owner)&&
  loadAction<=2&&storeAction==1&&attachmentClearLoads[1]==0&&attachmentClearStores[1]==0;
 int refused=ended||[command teguCommitted]||!p||p->owner!=command->queue->owner||
    (p->colorFormat!=70&&p->colorFormat!=80&&
     !(p->colorFormat==115&&p->homeAffordanceObserverOnly==2)&&
     !(p->colorFormat==10&&p->sphereObserverOnly))||p->colorFormat!=target->format||
    memorylessResolveOnly||(r8ClearOnly&&!p->sphereObserverOnly)||
    (observerPassOnly&&!p->homeAffordanceObserverOnly&&!p->fixedUiSnapshotOnly&&!p->fixedUiTextureSnapshotOnly&&!p->genericDraw&&!p->fixedUiObserverOnly&&!p->genericObserverOnly&&!p->compositorB3)||
    (p->compositorB3&&!exactB3Pair)||
    (p->attachmentClearSnapshotOnly&&!twoTargetPass)||
    (!p->attachmentClearSnapshotOnly&&twoTargetPass&&!exactB3Pair);
 if(p&&p->compositorB3)
  tegu_render_trace_b3_encoder("setPipeline",traceID,!refused,(unsigned)exactB3Pair,draws,command->count,command->invalid);
 tegu_render_trace_late_encoder("setPipeline",traceID,!refused,p,draws,command->count,command->invalid);
 tegu_render_trace_pipeline_bind(self,p,!refused);
 if(refused&&(command->queue->owner->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&
    (command->queue->owner->capabilities&TEGU_GPU_CAP_GENERIC_JOB)&&!ended&&![command teguCommitted]) {
  /* Drop this pipeline's draws instead of the whole frame. */
  [pipeline release];pipeline=0;pipelineRefused=1;return;
 }
 if(refused){TEGU_INVALID(command);return;}
 pipelineRefused=0;
 if(p->attachmentClearSnapshotOnly) {
  NSUInteger width=target->textureWidth,height=target->textureHeight;
  int exactActions=((attachmentClearLoads[0]==0&&attachmentClearStores[0]==0)||
    (attachmentClearLoads[0]==1&&attachmentClearStores[0]==1))&&
    (attachmentClearLoads[1]==0||attachmentClearLoads[1]==2)&&
    (attachmentClearStores[1]==0||attachmentClearStores[1]==1);
  if(!(command->queue->owner->capabilities&TEGU_GPU_CAP_ATTACHMENT_CLEAR)||!rt1||
     target->format!=80||rt1->format!=80||rt1->textureWidth!=width||rt1->textureHeight!=height||
     width<16||height<16||width>4096||height>4096||!exactActions){TEGU_INVALID(command);return;}
 }
 attachmentClearPass=(unsigned)!!p->attachmentClearSnapshotOnly;
 attachmentClearElided=(unsigned)(p->attachmentClearSnapshotOnly&&attachmentClearStores[1]==0);
 [p retain];[pipeline release];pipeline=p;
 tegu_render_trace_sphere_encoder("setPipeline",self,0,0,0,0,0,0);
}
static const TeguAIRRenderResourceUse *tegu_typed_use(const TeguAIRRenderTypedPlan *plan,
 unsigned kind,unsigned stage,unsigned index) {
 if(!plan||plan->version!=1||plan->count>186)return 0;
 for(unsigned i=0;i<plan->count;i++) {
  const TeguAIRRenderResourceUse *use=&plan->uses[i];
  if(use->kind==kind&&use->stage==stage&&use->index==index)return use;
 }
 return 0;
}
static void tegu_render_fixed_ui_observer(TeguRenderEncoder *e,const char *variant,NSUInteger type,
 NSUInteger start,NSUInteger count,NSUInteger instances,long baseVertex,NSUInteger baseInstance) {
#ifdef TEGU_RENDER_TRACE
 static unsigned observations;
 unsigned id=__atomic_fetch_add(&observations,1,__ATOMIC_RELAXED);if(id>=16)return;
 uint32_t vb=0,fb=0,vt=0,ft=0,vs=0,fs=0;
 for(unsigned i=0;i<31;i++){if(e->vertexBuffers[i])vb|=1u<<i;if(e->fragmentBuffers[i])fb|=1u<<i;
  if(e->vertexTextures[i])vt|=1u<<i;if(e->fragmentTextures[i])ft|=1u<<i;}
 for(unsigned i=0;i<16;i++){if(e->vertexSamplers[i])vs|=1u<<i;if(e->fragmentSamplers[i])fs|=1u<<i;}
 char line[320];int n=snprintf(line,sizeof(line),
  "TEGU_FIXED_UI_OBSERVER id=%u variant=%s type=%llu start=%llu count=%llu instances=%llu base_vertex=%ld base_instance=%llu vb=%08x fb=%08x vt=%08x ft=%08x vs=%04x fs=%04x target=%lux%lu/f%lu load=%lu store=%lu\n",
  id,variant,(U64)type,(U64)start,(U64)count,(U64)instances,baseVertex,(U64)baseInstance,
  vb,fb,vt,ft,vs,fs,(unsigned long)e->target->textureWidth,(unsigned long)e->target->textureHeight,
  (unsigned long)e->target->format,(unsigned long)e->loadAction,(unsigned long)e->storeAction);
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
 for(unsigned i=0;i<31;i++)if(e->fragmentTextures[i]) {
  TeguTexture *t=e->fragmentTextures[i];n=snprintf(line,sizeof(line),
   "TEGU_FIXED_UI_SOURCE id=%u stage=fragment slot=%u width=%lu height=%lu format=%lu levels=%lu usage=%lu options=%lx stride=%lu offset=%llu surface=%u\n",
   id,i,(unsigned long)t->textureWidth,(unsigned long)t->textureHeight,(unsigned long)t->format,
   (unsigned long)t->levelCount,(unsigned long)t->textureUsage,(unsigned long)t->textureResourceOptions,
   (unsigned long)t->rowBytes,(unsigned long long)t->mip[0].offset,t->surfaceImported);
  if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
 }
#else
 (void)e;(void)variant;(void)type;(void)start;(void)count;(void)instances;(void)baseVertex;(void)baseInstance;
#endif
}
static void tegu_render_generic_observer(TeguRenderEncoder *e,const char *variant,NSUInteger type,
 NSUInteger start,NSUInteger count,NSUInteger instances,long baseVertex,NSUInteger baseInstance) {
#ifdef TEGU_RENDER_TRACE
 static unsigned observations;
 unsigned id=__atomic_fetch_add(&observations,1,__ATOMIC_RELAXED);if(id>=64)return;
 uint32_t vb=0,fb=0,vt=0,ft=0,vs=0,fs=0;
 for(unsigned i=0;i<31;i++){if(e->vertexBuffers[i])vb|=1u<<i;if(e->fragmentBuffers[i])fb|=1u<<i;
  if(e->vertexTextures[i])vt|=1u<<i;if(e->fragmentTextures[i])ft|=1u<<i;}
 for(unsigned i=0;i<16;i++){if(e->vertexSamplers[i])vs|=1u<<i;if(e->fragmentSamplers[i])fs|=1u<<i;}
 char line[320];int n=snprintf(line,sizeof(line),
  "TEGU_RENDER_OBSERVER id=%u variant=%s type=%llu start=%llu count=%llu instances=%llu base_vertex=%ld base_instance=%llu vb=%08x fb=%08x vt=%08x ft=%08x vs=%04x fs=%04x target=%lux%lu/f%lu load=%lu store=%lu\n",
  id,variant,(U64)type,(U64)start,(U64)count,(U64)instances,baseVertex,(U64)baseInstance,
  vb,fb,vt,ft,vs,fs,(unsigned long)e->target->textureWidth,(unsigned long)e->target->textureHeight,
  (unsigned long)e->target->format,(unsigned long)e->loadAction,(unsigned long)e->storeAction);
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
 for(unsigned i=0;i<31;i++)if(e->fragmentTextures[i]) {
  TeguTexture *t=e->fragmentTextures[i];n=snprintf(line,sizeof(line),
   "TEGU_RENDER_SOURCE id=%u stage=fragment slot=%u width=%lu height=%lu format=%lu levels=%lu usage=%lu options=%lx stride=%lu offset=%llu surface=%u\n",
   id,i,(unsigned long)t->textureWidth,(unsigned long)t->textureHeight,(unsigned long)t->format,
   (unsigned long)t->levelCount,(unsigned long)t->textureUsage,(unsigned long)t->textureResourceOptions,
   (unsigned long)t->rowBytes,(unsigned long long)t->mip[0].offset,t->surfaceImported);
  if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
 }
#else
 (void)e;(void)variant;(void)type;(void)start;(void)count;(void)instances;(void)baseVertex;(void)baseInstance;
#endif
}
static int tegu_compositor_typed_uses_supported(const TeguAIRRenderTypedPlan *plan) {
 if(!plan||plan->version!=1||plan->count>186)return 0;
 for(unsigned i=0;i<plan->count;i++) {
  const TeguAIRRenderResourceUse *u=&plan->uses[i];if(u->reserved)return 0;
  if((u->kind==3&&(u->flags&~25u))||(u->kind!=3&&(u->flags&~39u)))return 0;
  if((u->flags&4)&&(u->flags&32))return 0;
  if((u->flags&32)&&(u->range_offset||u->range_length))return 0;
  if((u->flags&4)&&(__builtin_add_overflow(u->range_offset,u->range_length,&(uint64_t){0})||!u->range_length))return 0;
  if(u->flags&2)return 0; /* This prototype snapshots readonly resources only. */
  if(u->kind==1&&(u->stage==1||u->stage==2)) {
   /* VS storage is the captured position/vertex data path; fragment buffers
    * remain UBO-only in this initial request class. */
   uint32_t allowed=u->stage==1?(1u|2u|16u):2u;
   if(!(u->flags&1)||!u->class_mask||(u->class_mask&~allowed))return 0;
  } else if(u->kind==2&&u->stage==2) {
   if(!(u->flags&1)||u->class_mask!=4)return 0; /* sampled texture */
  } else if(u->kind==3&&u->stage==2) {
   if((u->class_mask&~4u)||!(u->flags&24))return 0;
  } else return 0; /* No vertex textures/samplers or unknown kinds/stages yet. */
 }
 return 1;
}
static int tegu_home_affordance_typed_plan_supported(const TeguAIRRenderTypedPlan *plan) {
 if(!plan||plan->version!=1||plan->count!=6||plan->attachment_count!=2||
    plan->diagnostic_count||plan->neutral_count||
    plan->attachments[0].format!=80||plan->attachments[0].write_mask!=15||
    plan->attachments[1].format!=80||plan->attachments[1].write_mask)return 0;
 const TeguAIRRenderResourceUse *v1=tegu_typed_use(plan,1,1,1),*v2=tegu_typed_use(plan,1,1,2);
 const TeguAIRRenderResourceUse *v3=tegu_typed_use(plan,1,1,3),*f1=tegu_typed_use(plan,1,2,1);
 const TeguAIRRenderResourceUse *t3=tegu_typed_use(plan,2,2,3),*t4=tegu_typed_use(plan,2,2,4);
 return v1&&v1->class_mask==16&&v1->flags==33&&!v1->range_offset&&!v1->range_length&&
  v2&&v2->class_mask==2&&v2->flags==5&&!v2->range_offset&&v2->range_length==64&&
  v3&&v3->class_mask==2&&v3->flags==5&&!v3->range_offset&&v3->range_length==16&&
  f1&&f1->class_mask==2&&f1->flags==5&&!f1->range_offset&&f1->range_length==12&&
  t3&&t3->class_mask==4&&t3->flags==33&&!t3->range_offset&&!t3->range_length&&
  t4&&t4->class_mask==4&&t4->flags==33&&!t4->range_offset&&!t4->range_length;
}
static int tegu_narrow_blur_11_typed_plan_supported(const TeguAIRRenderTypedPlan *plan) {
 if(!plan||plan->version!=1||plan->count!=7||plan->attachment_count!=2||
    plan->diagnostic_count||plan->neutral_count||
    (plan->attachments[0].format!=80&&plan->attachments[0].format!=115)||
    plan->attachments[0].write_mask!=15||
    plan->attachments[1].format!=plan->attachments[0].format||plan->attachments[1].write_mask)return 0;
 const TeguAIRRenderResourceUse *v1=tegu_typed_use(plan,1,1,1),*v2=tegu_typed_use(plan,1,1,2);
 const TeguAIRRenderResourceUse *v3=tegu_typed_use(plan,1,1,3),*f1=tegu_typed_use(plan,1,2,1);
 const TeguAIRRenderResourceUse *f6=tegu_typed_use(plan,1,2,6),*t3=tegu_typed_use(plan,2,2,3);
 const TeguAIRRenderResourceUse *s0=tegu_typed_use(plan,3,2,0);
 return v1&&v1->class_mask==16&&v1->flags==33&&!v1->range_offset&&!v1->range_length&&
  v2&&v2->class_mask==2&&v2->flags==5&&!v2->range_offset&&v2->range_length==64&&
  v3&&v3->class_mask==2&&v3->flags==5&&!v3->range_offset&&v3->range_length==16&&
  f1&&f1->class_mask==2&&f1->flags==5&&!f1->range_offset&&f1->range_length==82&&
  f6&&f6->class_mask==2&&f6->flags==5&&!f6->range_offset&&f6->range_length==2&&
  t3&&t3->class_mask==4&&t3->flags==33&&!t3->range_offset&&!t3->range_length&&
  s0&&!s0->class_mask&&s0->flags==24&&!s0->range_offset&&!s0->range_length;
}
static int tegu_rgba16float_private_observer_texture(TeguTexture *t,TeguComputeDevice *owner) {
 static const struct tegu_texture_level exact[10]={
  {776,24,6208,0},{388,12,3104,148992},{194,6,1552,186240},{97,3,776,195584},
  {48,1,384,197952},{24,1,192,198336},{12,1,96,198528},{6,1,48,198656},
  {3,1,24,198720},{1,1,8,198784}};
 if(!t||[t device]!=owner||t->surfaceImported||!t->buffer||t->buffer->owner!=owner||!t->buffer->handle||
    t->format!=115||t->textureWidth!=776||t->textureHeight!=24||t->rowBytes!=6208||
    t->textureResourceOptions!=0x20||t->textureUsage!=5||t->levelCount!=10||t->buffer->logical!=198792)return 0;
 for(unsigned i=0;i<10;i++)if(memcmp(&t->mip[i],&exact[i],sizeof(exact[i])))return 0;
 return 1;
}
/* Sampled client IOSurface (wallpaper, app and widget content) on kernels
 * whose TGJ1 spans resolve imported surfaces. */
static int tegu_imported_sample_ok(TeguTexture *t,TeguComputeDevice *owner) {
 return t&&tegu_owned_class(t,"TeguTexture")&&[t device]==owner&&t->surfaceImported&&t->importedHandle&&
  (owner->capabilities&TEGU_GPU_CAP_GENERIC_JOB_SURFACE_SPANS)&&(t->format==70||t->format==80)&&t->textureKind==2&&
  t->levelCount==1&&t->textureSamples==1&&t->textureWidth&&t->textureHeight&&t->textureWidth<=4096&&t->textureHeight<=4096&&
  t->rowBytes>=t->textureWidth*4u&&!(t->rowBytes&63)&&!(t->mip[0].offset&63)&&t->mip[0].offset<=t->surfaceAllocSize&&
  t->rowBytes*t->textureHeight<=t->surfaceAllocSize-t->mip[0].offset;
}
static int tegu_compositor_texture_resource(TeguTexture *t,TeguComputeDevice *owner,unsigned index,TeguAIRRenderResource *r) {
 if(tegu_imported_sample_ok(t,owner)) {
  memset(r,0,sizeof(*r));r->kind=2;r->stage=2;r->index=index;r->format=(uint32_t)t->format;r->texture_type=2;
  r->width=(uint32_t)t->textureWidth;r->height=(uint32_t)t->textureHeight;r->array_length=1;r->mipmap_levels=1;
  r->sample_count=1;r->usage=1;r->swizzle[0]=2;r->swizzle[1]=3;r->swizzle[2]=4;r->swizzle[3]=5;
  r->address_mode_identity=1;r->resource_id=0x100000ull+index;return 1;
 }
 if(!t||!tegu_owned_class(t,"TeguTexture")||[t device]!=owner||t->surfaceImported||!t->buffer||
    t->buffer->owner!=owner||(t->format!=1&&t->format!=10&&t->format!=30&&t->format!=70&&t->format!=80&&t->format!=115)||t->textureWidth==0||t->textureHeight==0||
    !t->levelCount||t->levelCount>13||t->textureResourceOptions==0x30||
    t->mip[0].width!=t->textureWidth||t->mip[0].height!=t->textureHeight)return 0;
 if(tegu_r58_private_allocation_only(t,owner))return 0;
 if(t->format==115&&t->levelCount!=1&&!tegu_rgba16float_private_observer_texture(t,owner))return 0;
 NSUInteger bpp=t->format==115?8:t->format==10||t->format==1?1:t->format==30?2:4;
 NSUInteger expectedWidth=t->textureWidth,expectedHeight=t->textureHeight;
 for(NSUInteger level=0;level<t->levelCount;level++) {
  struct tegu_texture_level *m=&t->mip[level];
  if(m->width!=expectedWidth||m->height!=expectedHeight||m->stride<expectedWidth*bpp||
     m->offset>t->buffer->logical||m->stride>SIZE_MAX/expectedHeight||
     m->stride*expectedHeight>t->buffer->logical-m->offset)return 0;
  expectedWidth=expectedWidth>1?expectedWidth/2:1;
  expectedHeight=expectedHeight>1?expectedHeight/2:1;
 }
 memset(r,0,sizeof(*r));r->kind=2;r->stage=2;r->index=index;r->format=(uint32_t)t->format;r->texture_type=2;
 r->width=(uint32_t)t->textureWidth;r->height=(uint32_t)t->textureHeight;r->array_length=1;r->mipmap_levels=(uint32_t)t->levelCount;
 r->sample_count=1;r->usage=(uint32_t)t->textureUsage;r->swizzle[0]=2;r->swizzle[1]=3;r->swizzle[2]=4;r->swizzle[3]=5;
 /* AIR accepts typed texture facts only; backing ownership/span stay in the
  * separate job snapshot fields and are never smuggled into planner input. */
 r->address_mode_identity=1;r->length=0;r->offset=0;r->gpu_address=0;
 /* Stable opaque token derived from the binding key; never expose object or GPU pointers. */
 r->resource_id=0x100000ull+index;r->stride=0;return 1;
}
/* Legacy fixed/compositor paths still synthesize a nearest/clamp LOD0 plan and
 * a two-bit hardware word. Only admit states that projection can represent;
 * objects created before the exact-state field existed have no hidden fields
 * to lose and remain compatible. */
static int tegu_sampler_legacy_ok(TeguSampler *s) {
 if(!s)return 0;
 if(s->state.version==N2T_TEXTURE_RESOURCES_VERSION)return tegu_sampler_legacy_word_ok(s);
 return s->linearFilters<=3u;
}
static void tegu_compositor_sampler_resource(TeguAIRRenderResource *r,unsigned index) {
 memset(r,0,sizeof(*r));r->kind=3;r->stage=2;r->index=index;
 /* TeguSampler construction admits only nearest, normalized, clamp,
  * non-compare/non-anisotropic weighted-average state. A descriptor's default
  * FLT_MAX upper clamp is equivalent to LOD0 for the admitted one-level UI
  * source and is projected to the compiler's exact LOD0 resource fact. */
 r->normalized_coordinates=1;r->max_anisotropy=1;r->lod_min_clamp=0.0f;r->lod_max_clamp=0.0f;
}
static int tegu_sphere_texture_resource(TeguTexture *t,TeguComputeDevice *owner,unsigned index,TeguAIRRenderResource *r) {
 if(!t||!r||!tegu_owned_class(t,"TeguTexture")||[t device]!=owner||t->surfaceImported||!t->buffer||
    t->buffer->owner!=owner||!t->buffer->handle||t->format!=10||t->textureKind!=7||
    t->textureWidth!=64||t->textureHeight!=64||!t->textureDepth||t->textureDepth>64||
    t->textureSamples!=1||t->levelCount!=1||t->textureUsage!=1||t->textureResourceOptions||
    t->mip[0].width!=64||t->mip[0].height!=64||t->mip[0].stride!=64||t->mip[0].offset||
    t->textureDepth>SIZE_MAX/(64u*64u)||64u*64u*t->textureDepth>t->buffer->logical)return 0;
 memset(r,0,sizeof(*r));r->kind=2;r->stage=1;r->index=index;r->format=10;r->texture_type=7;
 r->width=64;r->height=64;r->array_length=1;r->mipmap_levels=1;r->sample_count=1;r->usage=1;
 r->swizzle[0]=2;r->swizzle[1]=3;r->swizzle[2]=4;r->swizzle[3]=5;r->address_mode_identity=1;
 r->resource_id=0x200000ull+index;return 1;
}
static int tegu_render_sphere_observe(TeguRenderEncoder *e,const TeguAIRRenderDraw *draw) {
 if(!e||!draw||!e->pipeline||!e->pipeline->sphereObserverOnly||!e->pipeline->compiled||
    !e->r8ClearOnly||e->target->format!=10||e->target->textureWidth!=594||
    e->target->textureHeight!=594||e->target->rowBytes!=640||e->loadAction!=2||e->storeAction!=1)return -1;
 TeguComputeDevice *owner=e->command->queue->owner;TeguAIRRenderResource resources[186];size_t count=0;
 uint32_t vb=0,fb=0,vt=0,ft=0,vs=0,fs=0;
 for(unsigned stage=1;stage<=2;stage++)for(unsigned i=0;i<31;i++) {
  TeguBuffer *b=stage==1?e->vertexBuffers[i]:e->fragmentBuffers[i];if(!b)continue;
  NSUInteger offset=stage==1?e->vertexOffsets[i]:e->fragmentOffsets[i];
  if(!tegu_owned_class(b,"TeguBuffer")||b->owner!=owner||offset>b->logical||count>=186)return -2;
  TeguAIRRenderResource *r=&resources[count++];memset(r,0,sizeof(*r));
  r->kind=1;r->stage=stage;r->index=i;r->length=b->logical;r->offset=offset;
  if(stage==1)vb|=1u<<i;else fb|=1u<<i;
 }
 for(unsigned stage=1;stage<=2;stage++)for(unsigned i=0;i<31;i++) {
  TeguTexture *t=stage==1?e->vertexTextures[i]:e->fragmentTextures[i];if(!t)continue;
  if(count>=186)return -3;
  if(stage==1) {if(!tegu_sphere_texture_resource(t,owner,i,&resources[count]))return -2;vt|=1u<<i;}
  else {if(!tegu_compositor_texture_resource(t,owner,i,&resources[count]))return -2;ft|=1u<<i;}
  count++;
 }
 for(unsigned stage=1;stage<=2;stage++)for(unsigned i=0;i<16;i++) {
  TeguSampler *s=stage==1?e->vertexSamplers[i]:e->fragmentSamplers[i];if(!s)continue;
  if(!tegu_owned_class(s,"TeguSampler")||s->owner!=owner||!tegu_sampler_legacy_ok(s)||count>=186)return -2;
  tegu_compositor_sampler_resource(&resources[count],i);resources[count++].stage=stage;
  if(stage==1)vs|=1u<<i;else fs|=1u<<i;
 }
 TeguAIRBlob plan={0};TeguAIRRenderTypedPlan typed={0};char error[512]={0};
 int rc=tegu_air_render_pipeline_plan_draw_typed(e->pipeline->compiled,resources,count,draw,&plan,&typed,error,sizeof(error));
 if(rc) {
  char line[224];int n=snprintf(line,sizeof(line),"TEGU_SPHERE_PLAN_REFUSED resources=v%08x/f%08x/vt%08x/ft%08x/vs%04x/fs%04x error=%.96s\n",
   vb,fb,vt,ft,vs,fs,error);if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);return -40;
 }
 if(!plan.bytes||!plan.length||typed.version!=1||typed.count>186||typed.attachment_count!=1) {
  if(plan.bytes)tegu_air_blob_free(&plan);return -41;
 }
 char line[224];int n=snprintf(line,sizeof(line),
  "TEGU_SPHERE_PLAN resources=v%08x/f%08x/vt%08x/ft%08x/vs%04x/fs%04x uses=%u neutral=%u diagnostic=%u target=%lux%lu/%lu\n",
  vb,fb,vt,ft,vs,fs,typed.count,typed.neutral_count,typed.diagnostic_count,
  (unsigned long)e->target->textureWidth,(unsigned long)e->target->textureHeight,(unsigned long)e->target->rowBytes);
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
 for(unsigned i=0;i<typed.count;i++) {
  TeguAIRRenderResourceUse *u=&typed.uses[i];char row[192];int m=snprintf(row,sizeof(row),
   "TEGU_SPHERE_USE kind=%u stage=%u slot=%u class=%x flags=%x offset=%llu length=%llu\n",
   u->kind,u->stage,u->index,u->class_mask,u->flags,(unsigned long long)u->range_offset,
   (unsigned long long)u->range_length);if(m>0)write(2,row,(size_t)m<sizeof(row)?(size_t)m:sizeof(row)-1);
 }
 tegu_air_blob_free(&plan);return 0;
}
static void tegu_compositor_job_release(struct Dispatch *job) {
 if(!job)return;
 for(unsigned i=0;i<31;i++){[job->compositorVertexBuffers[i] release];[job->compositorFragmentBuffers[i] release];[job->compositorTextures[i] release];}
 for(unsigned i=0;i<16;i++)[job->compositorSamplers[i] release];
 [job->renderPipeline release];[job->renderTexture release];
 if(job->compositorPlan.bytes)tegu_air_blob_free(&job->compositorPlan);
 memset(job,0,sizeof(*job));
}
/* Path pipelines can leave descriptor-active attachments unwritten. Drop the
 * pass-initial clear jobs for those exact targets so their prior contents are
 * preserved; written targets keep their clear/draw order unchanged. */
static void tegu_render_preserve_targets(TeguRenderEncoder *e,uint32_t mask) {
 if(!e||!mask)return;
 e->preserveTargets|=mask;
 for(unsigned i=e->passJobStart;i<e->command->count;) {
  struct Dispatch *j=&TEGU_JOB(e->command->jobChunks,i);
  if(!j->nord2ClearOnly||!j->renderTexture){i++;continue;}
  unsigned rt=4;
  for(unsigned k=0;k<4;k++)if(tegu_render_target_for_rt(e,k)==j->renderTexture){rt=k;break;}
  if(rt>=4||!(mask&(1u<<rt))){i++;continue;}
  tegu_compositor_job_release(j);
  for(unsigned k=i;k+1<e->command->count;k++)
   TEGU_JOB(e->command->jobChunks,k)=TEGU_JOB(e->command->jobChunks,k+1);
  memset(&TEGU_JOB(e->command->jobChunks,e->command->count-1),0,sizeof(struct Dispatch));
  e->command->count--;
  if(e->renderJobs[rt])e->renderJobs[rt]--;
  if(rt==0){if(e->rt0Jobs)e->rt0Jobs--;}else if(rt==1){if(e->mrtDraws)e->mrtDraws--;}
  else if(rt==2){if(e->mrtDraws2)e->mrtDraws2--;}else if(e->mrtDraws3)e->mrtDraws3--;
 }
}
static void tegu_render_generic_refused(int,const char *);
struct tegu_generic_v2_sidecar {
 uint32_t count;
 struct n2r_texture_binding bindings[TEGU_GENERIC_DRAW_MAX_BINDINGS];
};
static int tegu_generic_v2_build_job(const struct Dispatch *,struct tegu_generic_v2_sidecar *,int,char *,size_t);
static int tegu_generic_v2_required(const struct Dispatch *);
#include "compositor_submit.inc"
#include "gpu_floor_bench.inc"

/* Retain the exact objects referenced by the execution-facing R80 plan. The
 * package is reusable; descriptor ranges and residual push bytes are resolved
 * again for every draw from the encoder's current binding offsets. */
/* Index-expanded vertices share one arena per encoder. A kernel buffer per
 * draw costs at least a 16 KiB page for a few hundred bytes; R176's home
 * screen exhausted GPU memory that way and every later expansion failed
 * (rc -15), wiping whole frames. Returns a retained buffer and the offset. */
#define TEGU_EXPAND_ARENA_BYTES ((NSUInteger)32*1024)
static TeguBuffer *tegu_render_expand_alloc(TeguRenderEncoder *e,NSUInteger bytes,NSUInteger *offset) {
 *offset=0;if(!bytes)return 0;
 if(bytes>TEGU_EXPAND_ARENA_BYTES/4)return [e->command->queue->owner newBufferWithLength:bytes options:0];
 NSUInteger at=(e->expandArenaUsed+255)&~(NSUInteger)255;
 if(!e->expandArena||at+bytes>TEGU_EXPAND_ARENA_BYTES) {
  TeguBuffer *arena=[e->command->queue->owner newBufferWithLength:TEGU_EXPAND_ARENA_BYTES options:0];if(!arena)return 0;
  [e->expandArena release];e->expandArena=arena;at=0;
 }
 e->expandArenaUsed=at+bytes;*offset=at;return [e->expandArena retain];
}
static int tegu_render_fixed_ui_snapshot(TeguRenderEncoder *e,const TeguAIRRenderDraw *draw,
 TeguBuffer *expandedVertex,NSUInteger expandedOffset,struct Dispatch *job) {
 if(!e||!draw||!job||!e->pipeline||!e->pipeline->compiled||!e->pipeline->fixedUiSnapshotOnly||
    draw->version!=1||draw->vertex_start||draw->vertex_count<3||draw->vertex_count>384||draw->vertex_count%3||
    draw->instance_count!=1||draw->base_instance||e->storeAction!=1||e->loadAction>2||
    !e->scissor.width||!e->scissor.height)return -1;
 TeguComputeDevice *owner=e->command->queue->owner;
 TeguBuffer *vertex=expandedVertex?expandedVertex:e->vertexBuffers[1],*vb=e->vertexBuffers[2],*fb=e->fragmentBuffers[1];
 NSUInteger selectedVertexOffset=expandedVertex?expandedOffset:e->vertexOffsets[1];
 if(!vertex||!vb||!fb||!vertex->handle||!vb->handle||!fb->handle||
    vertex->owner!=owner||vb->owner!=owner||fb->owner!=owner||
    selectedVertexOffset>vertex->logical||e->vertexOffsets[2]>vb->logical||
    e->fragmentOffsets[1]>fb->logical)return -2;
 TeguAIRRenderResource resources[2]={{0}};
 resources[0].kind=1;resources[0].stage=1;resources[0].index=2;
 resources[0].offset=e->vertexOffsets[2];resources[0].length=vb->logical;
 resources[1].kind=1;resources[1].stage=2;resources[1].index=1;
 resources[1].offset=e->fragmentOffsets[1];resources[1].length=fb->logical;
 TeguAIRFixedUiR80Plan plan={0};char error[384]={0};
 TeguAIRRenderDraw planningDraw={1,0,3,1,0};
 if(tegu_air_render_pipeline_plan_fixed_ui_r80(e->pipeline->compiled,resources,2,&planningDraw,
     &plan,error,sizeof(error)))return -3;
 TeguTexture *target=e->target;
 if(!target||[target device]!=owner||target->format!=80||!target->textureWidth||!target->textureHeight||
    target->textureWidth>8192||target->textureHeight>8192||target->textureWidth>SIZE_MAX/4u||
    target->rowBytes<target->textureWidth*4u||(target->rowBytes&63)||
    (target->mip[0].offset&63)||target->rowBytes>UINT64_MAX/(target->textureHeight-1u))return -4;
 U64 targetLength=target->surfaceImported?target->surfaceAllocSize:
    (target->buffer?target->buffer->logical:0);
 U64 targetHandle=target->surfaceImported?target->importedHandle:
    (target->buffer?target->buffer->handle:0);
 U64 targetTail=(target->textureHeight-1u)*target->rowBytes,targetRow=target->textureWidth*4u;
 if(targetTail>UINT64_MAX-targetRow)return -4;
 U64 targetNeed=targetTail+targetRow;
 if(!targetHandle||target->mip[0].offset>targetLength||targetNeed>targetLength-target->mip[0].offset)return -4;
 U64 vertexOffset=selectedVertexOffset,vertexNeed=(U64)draw->vertex_count*48u;
 if(vertexOffset>vertex->logical||vertexNeed>vertex->logical-vertexOffset)return -5;
 memset(job,0,sizeof(*job));job->fixedUiSnapshot=1;job->fixedUiPlan=plan;
 unsigned effectiveLoad=e->draws?1u:(unsigned)e->loadAction;
 job->compositorLoadAction=effectiveLoad;job->compositorStoreAction=(unsigned)e->storeAction;
 job->renderArgs[3]=effectiveLoad==2?e->clear:0;
 job->fixedUiScissor[0]=(uint32_t)e->scissor.x;job->fixedUiScissor[1]=(uint32_t)e->scissor.y;
 job->fixedUiScissor[2]=(uint32_t)e->scissor.width;job->fixedUiScissor[3]=(uint32_t)e->scissor.height;
 job->compositorVertexBufferMask=(1u<<1)|(1u<<2);job->compositorFragmentBufferMask=1u<<1;
 job->compositorVertexBuffers[1]=[vertex retain];job->compositorVertexBuffers[2]=[vb retain];
 job->compositorFragmentBuffers[1]=[fb retain];
 job->compositorVertexFacts[1]=(struct TeguCompositorBufferFact){vertex->handle,vertexOffset,vertex->logical-vertexOffset,0,0,vertexNeed,16,5};
 job->compositorVertexFacts[2]=(struct TeguCompositorBufferFact){vb->handle,plan.bindings[0].bind_offset,
  vb->logical-plan.bindings[0].bind_offset,plan.bindings[0].bind_offset-plan.bindings[0].descriptor_offset,0,64,2,5};
 job->compositorFragmentFacts[1]=(struct TeguCompositorBufferFact){fb->handle,plan.bindings[1].bind_offset,
  fb->logical-plan.bindings[1].bind_offset,plan.bindings[1].bind_offset-plan.bindings[1].descriptor_offset,0,168,2,5};
 job->renderPipeline=[e->pipeline retain];job->renderTexture=[target retain];return 0;
}

/* Snapshot the exact R85 sampled fixed-UI draw into the additive TFU2 ABI. */
static int tegu_render_fixed_ui_texture_snapshot(TeguRenderEncoder *e,const TeguAIRRenderDraw *draw,
 TeguBuffer *expandedVertex,NSUInteger expandedOffset,struct Dispatch *job) {
 if(!e||!draw||!job||!e->pipeline||!e->pipeline->compiled||!e->pipeline->fixedUiTextureSnapshotOnly||
    draw->version!=1||draw->vertex_start||draw->vertex_count<3||draw->vertex_count>384||draw->vertex_count%3||
    !e->scissor.width||!e->scissor.height||
    draw->instance_count!=1||draw->base_instance||e->storeAction!=1||e->loadAction>2)return -1;
 TeguComputeDevice *owner=e->command->queue->owner;
 TeguBuffer *vertex=expandedVertex?expandedVertex:e->vertexBuffers[1];
 TeguBuffer *buffers[4]={e->vertexBuffers[2],e->vertexBuffers[3],e->fragmentBuffers[1],e->fragmentBuffers[2]};
 NSUInteger offsets[4]={e->vertexOffsets[2],e->vertexOffsets[3],e->fragmentOffsets[1],e->fragmentOffsets[2]};
 TeguTexture *sample=e->fragmentTextures[3],*target=e->target;TeguSampler *sampler=e->fragmentSamplers[0];
 NSUInteger vertexOffset=expandedVertex?expandedOffset:e->vertexOffsets[1];
 if(!vertex||!vertex->handle||vertex->owner!=owner||!sample||!sampler||sampler->owner!=owner||
    !tegu_sampler_legacy_ok(sampler)||!target||[target device]!=owner)return -2;
 for(unsigned i=0;i<4;i++)if(!buffers[i]||!buffers[i]->handle||buffers[i]->owner!=owner||offsets[i]>buffers[i]->logical)return -2;
 if(target->format!=80||!target->textureWidth||!target->textureHeight||target->textureWidth>4096||
    target->textureHeight>4096||target->rowBytes<target->textureWidth*4u||(target->rowBytes&63))return -3;
 U64 targetHandle=target->surfaceImported?target->importedHandle:(target->buffer?target->buffer->handle:0);
 U64 targetLength=target->surfaceImported?target->surfaceAllocSize:(target->buffer?target->buffer->logical:0);
 U64 targetOffset=target->mip[0].offset,targetNeed=(target->textureHeight-1u)*target->rowBytes+target->textureWidth*4u;
 if(!targetHandle||(targetOffset&63)||targetOffset>targetLength||targetNeed>targetLength-targetOffset)return -3;
 if(!tegu_owned_class(sample,"TeguTexture")||[sample device]!=owner||sample->surfaceImported||!sample->buffer||
    sample->buffer->owner!=owner||!sample->buffer->handle||sample->format!=80||sample->textureKind!=2||
    !sample->textureWidth||!sample->textureHeight||sample->textureWidth>4096||sample->textureHeight>4096||
    sample->levelCount!=1||sample->textureSamples!=1||!(sample->textureUsage&1)||
    sample->textureResourceOptions==0x30||sample->rowBytes<sample->textureWidth*4u||(sample->rowBytes&63)||
    sample->mip[0].offset&63||sample->mip[0].stride!=sample->rowBytes||
    sample->mip[0].offset>sample->buffer->logical||
    sample->rowBytes*sample->textureHeight>sample->buffer->logical-sample->mip[0].offset) {
#ifdef TEGU_RENDER_TRACE
  /* Any single-level 2D BGRA8 sampled texture with 64-byte rows is encodable;
   * report the facts of anything else. */
  static unsigned refusedSources;
  if(sample&&__atomic_fetch_add(&refusedSources,1,__ATOMIC_RELAXED)<8){char line[256];int n=snprintf(line,sizeof(line),
   "TEGU_UI2_SOURCE_REFUSED %lux%lu f%lu kind=%lu levels=%lu samples=%lu usage=%lu options=0x%lx row=%lu imported=%u\n",
   (unsigned long)sample->textureWidth,(unsigned long)sample->textureHeight,(unsigned long)sample->format,
   (unsigned long)sample->textureKind,(unsigned long)sample->levelCount,(unsigned long)sample->textureSamples,
   (unsigned long)sample->textureUsage,(unsigned long)sample->textureResourceOptions,(unsigned long)sample->rowBytes,
   (unsigned)sample->surfaceImported);if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);}
#endif
  return -4;
 }
 U64 vertexNeed=(U64)draw->vertex_count*48u;
 if((vertexOffset&3)||vertexOffset>vertex->logical||vertexNeed>vertex->logical-vertexOffset)return -5;
 TeguAIRRenderResource resources[7]={0};
 for(unsigned i=0;i<4;i++)resources[i]=(TeguAIRRenderResource){.kind=1,.stage=i<2?1u:2u,
  .index=i<2?2u+i:1u+(i-2),.length=buffers[i]->logical,.offset=offsets[i]};
 resources[4]=(TeguAIRRenderResource){.kind=1,.stage=1,.index=1,.length=vertex->logical,.offset=vertexOffset};
 if(!tegu_compositor_texture_resource(sample,owner,3,&resources[5]))return -4;
 tegu_compositor_sampler_resource(&resources[6],0);
 TeguAIRFixedUiR85Plan plan={0};char error[384]={0};TeguAIRRenderDraw planning={1,0,3,1,0};
 if(tegu_air_render_pipeline_plan_fixed_ui_r85_texture(e->pipeline->compiled,resources,7,&planning,
    &plan,error,sizeof(error)))return -6;
 struct tegu_ui2_bind_request request={0};request.version=2;
 request.width=(uint32_t)target->textureWidth;request.height=(uint32_t)target->textureHeight;request.topology=3;
 request.vertex_count=draw->vertex_count;request.instance_count=1;
 request.target.allocation=(struct tegu_ui2_allocation){targetHandle,targetHandle,targetLength,targetOffset};
 request.target.format=80;request.target.width=request.width;request.target.height=request.height;request.target.row_bytes=(uint32_t)target->rowBytes;
 request.vertex=(struct tegu_ui2_allocation){vertex->handle,vertex->handle,vertex->logical,vertexOffset};
 request.vertex_slot=1;request.vertex_stride=48;
 for(unsigned i=0;i<4;i++) {
  request.resources[i].allocation=(struct tegu_ui2_allocation){buffers[i]->handle,buffers[i]->handle,buffers[i]->logical,plan.bindings[i].bind_offset};
  request.resources[i].metal_stage=plan.bindings[i].stage;request.resources[i].metal_slot=plan.bindings[i].index;
  request.resources[i].descriptor_offset=plan.bindings[i].descriptor_offset;
  request.resources[i].descriptor_length=plan.bindings[i].descriptor_length;
 }
 request.texture.allocation=(struct tegu_ui2_allocation){sample->buffer->handle,sample->buffer->handle,
  sample->buffer->logical,sample->mip[0].offset};
 request.texture.resource_id=plan.texture.resource_id;request.texture.metal_stage=2;request.texture.metal_slot=3;
 request.texture.format=80;request.texture.texture_type=2;
 request.texture.width=(uint32_t)sample->textureWidth;request.texture.height=(uint32_t)sample->textureHeight;
 request.texture.row_bytes=(uint32_t)sample->rowBytes;
 memcpy(request.vertex_push,plan.push_bytes,8);memcpy(request.fragment_push,plan.push_bytes+8,12);
 unsigned effectiveLoad=e->draws?1u:(unsigned)e->loadAction;
 request.pass.render_width=request.pass.viewport_width=request.width;
 request.pass.render_height=request.pass.viewport_height=request.height;
 request.pass.scissor_x=(uint32_t)e->scissor.x;request.pass.scissor_y=(uint32_t)e->scissor.y;
 request.pass.scissor_width=(uint32_t)e->scissor.width;request.pass.scissor_height=(uint32_t)e->scissor.height;
 request.pass.load_action=effectiveLoad;request.pass.store_action=e->storeAction;
 request.pass.clear_color[0]=effectiveLoad==2?e->clear:0;
 struct tegu_ui2_binding bound={0};int bindResult=tegu_ui2_bind(&e->pipeline->fixedUiTextureInfo,&request,&bound);
 if(bindResult){tegu_render_trace_ui2_bind(bindResult,&e->pipeline->fixedUiTextureInfo,&request);return -7;}
 memset(job,0,sizeof(*job));job->fixedUiTextureSnapshot=1;job->fixedUiTexturePlan=plan;job->fixedUiTextureRequest=request;
 job->fixedUiSamplerLinear=sampler->linearFilters;
 job->renderArgs[0]=e->pipeline->handle;job->renderArgs[1]=targetHandle;
 job->renderArgs[2]=targetOffset;job->renderArgs[3]=request.pass.clear_color[0];
 job->compositorLoadAction=effectiveLoad;job->compositorStoreAction=e->storeAction;
 job->compositorVertexBufferMask=(1u<<1)|(1u<<2)|(1u<<3);job->compositorFragmentBufferMask=(1u<<1)|(1u<<2);
 job->compositorTextureMask=1u<<3;job->compositorSamplerMask=1u;
 job->compositorVertexBuffers[1]=[vertex retain];job->compositorVertexBuffers[2]=[buffers[0] retain];job->compositorVertexBuffers[3]=[buffers[1] retain];
 job->compositorFragmentBuffers[1]=[buffers[2] retain];job->compositorFragmentBuffers[2]=[buffers[3] retain];
 job->compositorTextures[3]=[sample retain];job->compositorSamplers[0]=[sampler retain];
 job->renderPipeline=[e->pipeline retain];job->renderTexture=[target retain];return 0;
}

/* Generic draw refusals are traced and the draw is skipped, never failing
 * the command buffer, so the pass's other draws still render. */
static void tegu_render_generic_refused(int rc,const char *detail) {
 /* Always on (quiet builds too), capped: refusals are what we act on. */
 {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=16)return;
 char line[400];int n=snprintf(line,sizeof(line),"TEGU_GENERIC_DRAW_REFUSED rc=%d %s\n",rc,detail?detail:"");
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
 }
}
/* Vulkan sampler enums of a constexpr sampler to the Mali sampler word. */
static int tegu_generic_immutable_sampler(const TeguAIRGenericBinding *b,uint32_t *word) {
 static const uint32_t wrap[5]={8,12,9,11,13};
 if(b->min_filter>1||b->mag_filter>1)return -1;
 for(unsigned i=0;i<3;i++)if(b->address[i]>4)return -1;
 *word=TEGU_GENERIC_DRAW_SAMPLER_WORD(b->min_filter,b->mag_filter,wrap[b->address[0]],wrap[b->address[1]],wrap[b->address[2]]);
 return 0;
}
static int tegu_generic_compile_matches(const struct tegu_mali_generic_plan *c,const TeguAIRGenericPlan *plan) {
 for(uint32_t i=0;i<c->count;i++) {
  const struct tegu_mali_generic_binding *m=&c->bindings[i];const TeguAIRGenericBinding *b=0;
  for(uint32_t j=0;j<plan->binding_count;j++)if(plan->bindings[j].binding==m->binding)b=&plan->bindings[j];
  if(!b)return 0;
  int buffer=b->kind==TEGU_AIR_GENERIC_BUFFER||b->kind==TEGU_AIR_GENERIC_CONSTANT||b->kind==TEGU_AIR_GENERIC_NEUTRAL_BUFFER;
  int combined=b->kind==TEGU_AIR_GENERIC_COMBINED||b->kind==TEGU_AIR_GENERIC_COMBINED_IMMUTABLE||b->kind==TEGU_AIR_GENERIC_NEUTRAL_IMAGE;
  int ok=(m->kind==TEGU_MALI_GENERIC_UBO||m->kind==TEGU_MALI_GENERIC_READONLY_STORAGE)?buffer:
   m->kind==TEGU_MALI_GENERIC_COMBINED?combined:
   m->kind==TEGU_MALI_GENERIC_TEXTURE?combined||b->kind==TEGU_AIR_GENERIC_TEXTURE:
   m->kind==TEGU_MALI_GENERIC_SAMPLER?combined||b->kind==TEGU_AIR_GENERIC_SAMPLER:
   m->kind==TEGU_MALI_GENERIC_INPUT_ATTACHMENT?b->kind==TEGU_AIR_GENERIC_ATTACHMENT:0;
  if(!ok)return 0;
 }
 return 1;
}
static int tegu_generic_compiled_binding(const struct tegu_mali_generic_plan *c,uint32_t binding) {
 for(uint32_t i=0;i<c->count;i++)if(c->bindings[i].binding==binding)return 1;
 return 0;
}
/* Single-level BGRA8, RGBA8 or R8 2D texture with 64-byte rows, owned or
 * an imported client surface. */
/* Rows of texels, or of ASTC blocks, in one level. */
static NSUInteger tegu_texture_level_row_count(TeguTexture *t,NSUInteger level) {
 unsigned bw=1,bh=1,bb=0;NSUInteger height=t&&level<t->levelCount?t->mip[level].height:0;
 if(t&&tegu_texture_is_astc(t->format)&&tegu_texture_block_shape(t->format,&bw,&bh,&bb))return (height+bh-1)/bh;
 return height;
}
static NSUInteger tegu_texture_level0_rows(TeguTexture *t) {return tegu_texture_level_row_count(t,0);}
/* Full bytes from the level0 offset through the end of the last level. This
 * is the span the v2 sidecar may address; it is deliberately not reduced to
 * level0. Refuses a negative/overlapping chain or unaligned rows. */
static int tegu_generic_texture_span(TeguTexture *t,U64 *span) {
 if(!t||!span||!t->levelCount)return 0;
 U64 base=t->mip[0].offset,end=base;
 for(NSUInteger level=0;level<t->levelCount;level++) {
  struct tegu_texture_level *m=&t->mip[level];
  if((m->stride&63)||m->offset<base||!m->height||m->stride>SIZE_MAX/m->height)return 0;
  U64 tail=(U64)m->stride*tegu_texture_level_row_count(t,level);
  NSUInteger depth=t->levelDepth[level]?t->levelDepth[level]:1;
  NSUInteger samples=t->textureSamples?t->textureSamples:1;
  U64 slice=t->levelSliceStride[level]?t->levelSliceStride[level]:(U64)m->stride*m->height;
  if(t->textureKind==7&&depth>1) {
   if(slice>~(U64)0/(depth-1))return 0;
   tail+=slice*(depth-1);
  }
  if(samples>1&&slice>~(U64)0/(samples-1))return 0;
  if(samples>1)tail+=slice*(samples-1);
  if(m->offset>~(U64)0-tail||m->offset+tail>end)end=m->offset+tail;
 }
 U64 layerStride=t->layers>1?t->levelLayerStride[0]:0;
 U64 spanValue=end-base;
 if(t->layers>1) {
  if(!layerStride||layerStride&63)return 0;
  if(t->layers-1>~(U64)0/layerStride)return 0;
  if(spanValue>~(U64)0-(t->layers-1)*layerStride)return 0;
  spanValue+=(t->layers-1)*layerStride;
 }
 U64 length=t->surfaceImported?t->surfaceAllocSize:(t->buffer?t->buffer->logical:0);
 if(!length||base>length||spanValue>length-base)return 0;
 *span=spanValue;return 1;
}
static int tegu_generic_texture_ok(TeguTexture *t,TeguComputeDevice *owner) {
 if(tegu_imported_sample_ok(t,owner))return 1;
 if(t&&tegu_texture_is_astc(t->format)) {
  unsigned bw=4,bh=4,bb=16;tegu_texture_block_shape(t->format,&bw,&bh,&bb);
  return tegu_owned_class(t,"TeguTexture")&&[t device]==owner&&!t->surfaceImported&&t->buffer&&
   t->buffer->owner==owner&&t->buffer->handle&&t->textureKind==2&&t->textureWidth&&t->textureHeight&&
   t->textureWidth<=4096&&t->textureHeight<=4096&&t->textureSamples==1&&(t->textureUsage&1)&&
   t->rowBytes>=((t->textureWidth+bw-1)/bw)*bb&&!(t->rowBytes&63)&&!(t->mip[0].offset&63)&&t->mip[0].stride==t->rowBytes&&
   t->mip[0].offset<=t->buffer->logical&&t->rowBytes*tegu_texture_level0_rows(t)<=t->buffer->logical-t->mip[0].offset;
 }
 return t&&tegu_owned_class(t,"TeguTexture")&&[t device]==owner&&!t->surfaceImported&&t->buffer&&
  t->buffer->owner==owner&&t->buffer->handle&&(t->format==80||t->format==70||t->format==10||t->format==1||t->format==30||t->format==43||t->format==115)&&t->textureKind==2&&
  t->textureWidth&&t->textureHeight&&t->textureWidth<=4096&&t->textureHeight<=4096&&
  t->textureSamples==1&&(t->textureUsage&1)&&t->textureResourceOptions!=0x30&&
  /* 43 BGR5A1Unorm: RGB5_A1_UNORM/BGRA in gd_format and n2t (c6hc rc=-13). */
  t->rowBytes>=t->textureWidth*(t->format==10||t->format==1?1u:t->format==30||t->format==43?2u:t->format==115?8u:4u)&&!(t->rowBytes&63)&&!(t->mip[0].offset&63)&&t->mip[0].stride==t->rowBytes&&
  t->mip[0].offset<=t->buffer->logical&&t->rowBytes*t->textureHeight<=t->buffer->logical-t->mip[0].offset;
}
/* Generic TGJ1 currently stores one linear 2D plane (level0, layer0, one
 * sample). Keep that transport bound explicit: a texture outside it is never
 * offered as a partial descriptor and never silently becomes a neutral
 * resource. ASTC 2D block rows are the one compressed layout gd_texture and
 * gd_request_ok both implement; mip/3D/cube/array/MSAA need the textures
 * owner's n2r v2 sidecar. */
static int tegu_generic_texture_bindable(TeguTexture *t,TeguComputeDevice *owner) {
 U64 span=0;
 if(!t||!tegu_owned_class(t,"TeguTexture")||!t->levelCount)return 0;
 if(t->textureKind==3||t->textureKind==5||t->textureKind==6||t->textureKind==4||t->textureKind==8) {
  if(t->textureUsage!=1||t->textureResourceOptions||t->surfaceImported||!t->buffer||
     t->buffer->owner!=owner||!t->buffer->handle||!t->layers||(t->rowBytes&63))return 0;
  if((t->textureKind==5||t->textureKind==6)&&t->textureWidth!=t->textureHeight)return 0;
  if((t->textureKind==4||t->textureKind==8)&&
     !(t->textureSamples==2||t->textureSamples==4||t->textureSamples==8))return 0;
  return tegu_generic_texture_span(t,&span);
 }
 if(t->textureSamples!=1)return 0;
 if(t->textureKind==7) {
  /* The only 3D allocation currently reachable is the exact R8 64x64 R55
   * class; the translator's bounded plan accepts that same class only. */
  return t->format==10&&t->textureWidth==64&&t->textureHeight==64&&t->textureDepth&&
   t->textureDepth<=64&&t->levelCount==1&&t->textureUsage==1&&!t->textureResourceOptions&&
   !t->surfaceImported&&t->buffer&&t->buffer->owner==owner&&t->buffer->handle&&
   !(t->rowBytes&63)&&tegu_generic_texture_span(t,&span);
 }
 if(t->textureKind!=2||!tegu_generic_texture_ok(t,owner))return 0;
 return t->levelCount==1||tegu_generic_texture_span(t,&span);
}
/* Translator fact record. It carries the allocation's real kind, format,
 * dimensions, level and sample counts; do not reduce these to Metal slot
 * conventions. The native binder validates backing spans separately. */
static void tegu_generic_texture_facts(TeguTexture *t,unsigned stage,unsigned index,TeguAIRRenderResource *r) {
 memset(r,0,sizeof(*r));
 r->kind=2;r->stage=stage;r->index=index;r->format=(uint32_t)t->format;
 r->texture_type=(uint32_t)t->textureKind;r->width=(uint32_t)t->textureWidth;r->height=(uint32_t)t->textureHeight;
 r->array_length=(uint32_t)t->arrayLength;r->mipmap_levels=(uint32_t)t->levelCount;
 r->sample_count=(uint32_t)t->textureSamples;
 r->usage=(uint32_t)t->textureUsage;r->swizzle[0]=2;r->swizzle[1]=3;r->swizzle[2]=4;r->swizzle[3]=5;
 r->address_mode_identity=1;
 r->resource_id=0x60000000ull|((uint64_t)(stage&3u)<<16)|(index&0xffffu);
}
/* Feed the planner the exact retained Metal sampler state. The Rust bounded
 * class may refuse linear/mip/address variants until it and the graphics
 * specialization API are extended; that is an explicit command error, never a
 * nearest substitution. FLT_MAX is the legacy "unbounded" spelling of LOD0 for
 * the admitted one-level path. */
static int tegu_generic_sampler_facts(TeguSampler *s,unsigned stage,unsigned index,
 TeguAIRRenderResource *r,char *why,size_t why_size) {
 if(!s||s->state.version!=N2T_TEXTURE_RESOURCES_VERSION) {
  snprintf(why,why_size,"sampler has no exact N2T state");return -1;
 }
 memset(r,0,sizeof(*r));r->kind=3;r->stage=stage;r->index=index;
 r->min_filter=s->state.min_filter;r->mag_filter=s->state.mag_filter;
 r->mip_filter=s->state.mip_filter;r->address_s=s->state.address_s;
 r->address_t=s->state.address_t;r->address_r=s->state.address_r;
 r->border_color=s->state.border_color_kind;r->normalized_coordinates=s->state.normalized;
 r->lod_average=0;r->max_anisotropy=s->state.max_anisotropy;
 r->compare_function=s->state.compare_function;r->reduction_mode=s->state.reduction;
 r->lod_min_clamp=s->state.lod_min;
 r->lod_max_clamp=s->state.lod_max==__FLT_MAX__?0.0f:s->state.lod_max;
 return 0;
}
/* The generic job's descriptor tables are indexed by native binding, not by
 * Metal slot. Same Metal slot in VS and FS is legal and maps to two native
 * bindings; one native binding identifies one descriptor and therefore one
 * retained object. Collision with a different object is an error. */
static int tegu_generic_retain_texture(struct Dispatch *job,unsigned binding,TeguTexture *t) {
 if(!job||!t||binding>=31)return -1;
 if(job->compositorTextures[binding])return job->compositorTextures[binding]==(id)t?0:-2;
 job->compositorTextures[binding]=[t retain];return 0;
}
static int tegu_generic_retain_sampler(struct Dispatch *job,unsigned binding,TeguSampler *s) {
 if(!job||!s||binding>=16)return -1;
 if(job->compositorSamplers[binding])return job->compositorSamplers[binding]==(id)s?0:-2;
 job->compositorSamplers[binding]=[s retain];return 0;
}
struct tegu_generic_unsupported_texture {
 unsigned stage,index;NSUInteger kind,format,levels,samples,usage,options,surface;
};
/* Snapshot only the first few bound-but-unrepresentable textures for a bounded
 * refusal line. Encoder setters already proved ownership/class. */
static unsigned tegu_generic_unsupported_textures(TeguRenderEncoder *e,
 struct tegu_generic_unsupported_texture out[4]) {
 unsigned count=0;
 if(!e)return 0;
 for(unsigned stage=1;stage<=2;stage++)for(unsigned i=0;i<31;i++) {
  TeguTexture *t=stage==1?e->vertexTextures[i]:e->fragmentTextures[i];
  if(!t||tegu_generic_texture_bindable(t,e->command->queue->owner))continue;
  if(count<4)out[count++]=(struct tegu_generic_unsupported_texture){stage,i,t->textureKind,
   t->format,t->levelCount,t->textureSamples,t->textureUsage,t->textureResourceOptions,t->surfaceImported};
  else return count;
 }
 return count;
}
static int tegu_generic_plan_has_neutral_image(const TeguAIRGenericPlan *plan) {
 for(uint32_t i=0;plan&&i<plan->binding_count;i++)
  if(plan->bindings[i].kind==TEGU_AIR_GENERIC_NEUTRAL_IMAGE)return 1;
 return 0;
}
/* Compiler reflection is the authority on which resources the generated code
 * consumes (the compiler -> planner direction is a hard check above). A
 * planner-only extra descriptor is not submitted; record its identity instead
 * of silently dropping it. Neutral buffers/images and the pipeline-owned
 * constant blob have separate, documented roles and are not descriptors:
 * n2r_build emits a zero buffer/valid neutral texture for the former and
 * copies the latter as pipeline data. The call site also skips the extra
 * descriptor, so a later neutral substitution is still caught explicitly. */
static void tegu_generic_record_unreflected(const struct tegu_mali_generic_plan *c,
 const TeguAIRGenericPlan *plan) {
 if(!c||!plan)return;
 for(uint32_t i=0;i<plan->binding_count;i++) {
  uint32_t kind=plan->bindings[i].kind;
  if(kind==TEGU_AIR_GENERIC_NEUTRAL_IMAGE||kind==TEGU_AIR_GENERIC_NEUTRAL_BUFFER||
     kind==TEGU_AIR_GENERIC_CONSTANT)continue;
  if(tegu_generic_compiled_binding(c,plan->bindings[i].binding))continue;
  static unsigned emitted;
  if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=16)return;
  char line[128];int n=snprintf(line,sizeof(line),"TEGU_GENERIC_UNREFLECTED b=%u kind=%u\n",
   plan->bindings[i].binding,kind);
  if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
 }
}

/* v2 sidecar construction. Kept separate from submission so the same validated
 * facts can be tested and handed to n2mb_draw_v2 without another interpretation
 * of Metal slots. It covers the bounded 2D/mip/array/cube/3D/MS shape classes
 * allocated by this frontend; exact view aliases are applied from the selected
 * specialization binding, and every refusal is explicit before output
 * mutation. */
static int tegu_generic_v2_layout(TeguTexture *t,struct n2t_texture_layout *l,
 uint32_t view_format,uint32_t view_level,uint32_t view_layer,
 uint64_t *backing_length,char *why,size_t why_size) {
 if(!t||!l||!backing_length||!t->levelCount||t->levelCount>13||
    !t->layers||(!tegu_generic_texture_span(t,backing_length)))
  {snprintf(why,why_size,"v2 layout is not a bounded texture");return -1;}
 if(t->textureKind!=2&&t->textureKind!=7&&t->textureKind!=3&&t->textureKind!=5&&
    t->textureKind!=6&&t->textureKind!=4&&t->textureKind!=8) {
  snprintf(why,why_size,"v2 layout kind %lu is not reachable yet",(unsigned long)t->textureKind);return -1;
 }
 if(view_level||view_layer) {
  snprintf(why,why_size,"texture view level/layer is not transportable yet");return -1;
 }
 memset(l,0,sizeof(*l));l->version=N2T_TEXTURE_RESOURCES_VERSION;
 l->kind=(uint32_t)t->textureKind;
 l->format=(uint32_t)t->format;l->view_format=view_format;l->width=(uint32_t)t->textureWidth;
 l->height=(uint32_t)t->textureHeight;l->depth=t->textureKind==7?(uint32_t)t->textureDepth:1;
 l->levels=(uint32_t)t->levelCount;l->layers=(uint32_t)t->layers;
 l->samples=(uint32_t)t->textureSamples;l->array_length=
  (t->textureKind==3||t->textureKind==8)?(uint32_t)t->arrayLength:
  ((t->textureKind==5||t->textureKind==6)?0u:1u);
 l->first_level=0;l->first_layer=0;l->swizzle_valid=0;l->swizzle_word=0;
 l->usage=(uint32_t)t->textureUsage;
 l->storage_mode=(uint32_t)((t->textureResourceOptions>>4)&15u);l->access=N2T_ACCESS_SAMPLE;
 uint64_t base=(uint64_t)t->mip[0].offset;
 for(NSUInteger level=0;level<t->levelCount;level++) {
  if((uint64_t)t->mip[level].offset<base) {
   snprintf(why,why_size,"v2 mip chain offset is before level0");return -1;
  }
  l->level[level].offset=(uint64_t)t->mip[level].offset-base;
  l->level[level].row_stride=(uint32_t)t->mip[level].stride;
  l->level[level].slice_stride=(t->textureKind==7||t->textureSamples>1)?
   (uint32_t)t->levelSliceStride[level]:0;
  l->level[level].layer_stride=(uint32_t)t->levelLayerStride[level];
  l->level[level].width=(uint32_t)t->mip[level].width;
  l->level[level].height=(uint32_t)t->mip[level].height;
  l->level[level].depth=t->textureKind==7?(uint32_t)t->levelDepth[level]:1;
 }
 int rc=n2t_layout_validate(l,0,*backing_length);
 if(rc) {snprintf(why,why_size,"n2t layout validate %d: %s",rc,n2t_error_string(rc));return -1;}
 return 0;
}
static int tegu_generic_v2_sampler(const TeguAIRGenericBinding *b,const struct Dispatch *job,
 int compare_enabled,struct n2t_sampler_state *out,char *why,size_t why_size) {
 TeguSampler *s=0;
 if(b->kind==TEGU_AIR_GENERIC_COMBINED||b->kind==TEGU_AIR_GENERIC_SAMPLER)
  s=(TeguSampler *)job->compositorSamplers[b->binding];
 memset(out,0,sizeof(*out));
 if(s) {
  if(tegu_sampler_get_state(s,out)||out->version!=N2T_TEXTURE_RESOURCES_VERSION) {
   snprintf(why,why_size,"sampler has no exact N2T state");return -1;
  }
  if(compare_enabled) {
   if(out->compare_function==N2T_COMPARE_NEVER) {
    snprintf(why,why_size,"compare-enabled sampler has no compare function");return -1;
   }
   out->compare_enabled=1;
  } else out->compare_enabled=0;
 } else if(b->kind==TEGU_AIR_GENERIC_COMBINED_IMMUTABLE) {
  /* GenericBinding carries Vulkan filter/mip/address/border enums but no
   * normalized flag, LOD clamps, bias or per-use integer-border class. The
   * legacy word path is retained for one-level immutable samplers; the v2
   * sidecar refuses rather than guessing Metal semantics from Vulkan enums. */
  snprintf(why,why_size,"immutable sampler v2 lacks exact Metal state in the plan ABI");return -1;
 } else {
  snprintf(why,why_size,"combined sampler has no exact state");return -1;
 }
 int rc=n2t_sampler_validate(out);
 if(rc) {snprintf(why,why_size,"n2t sampler validate %d: %s",rc,n2t_error_string(rc));return -1;}
 return 0;
}
/* Submit-time projection that needs no extra plan field: the request already
 * carries binding/kind and the retained arrays carry the objects. This is the
 * function compositor_submit.inc will call once the bridge exposes
 * n2mb_draw_v2. Immutable combined samplers still refuse (no exact state). */
static const TeguAIRGraphicsSpecializationBinding *tegu_generic_v2_plan_binding(
 const struct Dispatch *job,uint32_t binding) {
 TeguRenderPipeline *p=job?(TeguRenderPipeline *)job->renderPipeline:0;
 uint64_t selected=job?job->renderArgs[0]:0;
 unsigned count=p?__atomic_load_n(&p->genericVariantCount,__ATOMIC_ACQUIRE):0;
 if(!p||!selected||selected>count)return 0;
 const struct tegu_render_generic_variant *v=&p->genericVariants[selected-1];
 for(uint32_t i=0;i<v->binding_count;i++)if(v->bindings[i].binding==binding)return &v->bindings[i];
 return 0;
}
static int tegu_generic_v2_build_job(const struct Dispatch *job,
 struct tegu_generic_v2_sidecar *out,int allow_legacy_immutable,char *why,size_t why_size) {
 struct tegu_generic_v2_sidecar tmp;if(!job||!out)return -1;
 memset(&tmp,0,sizeof(tmp));
 for(uint32_t i=0;i<job->genericDrawRequest.binding_count;i++) {
  const struct tegu_generic_draw_binding *request=&job->genericDrawRequest.bindings[i];
  if(request->kind!=TEGU_GENERIC_DRAW_COMBINED&&request->kind!=TEGU_GENERIC_DRAW_COMBINED_IMMUTABLE&&
     request->kind!=TEGU_GENERIC_DRAW_TEXTURE&&request->kind!=TEGU_GENERIC_DRAW_SAMPLER)continue;
  if(tmp.count>=TEGU_GENERIC_DRAW_MAX_BINDINGS||request->binding>=TEGU_GENERIC_DRAW_MAX_BINDINGS) {
   snprintf(why,why_size,"too many v2 bindings");return -1;
  }
  struct n2r_texture_binding *entry=&tmp.bindings[tmp.count];
  entry->binding=request->binding;entry->kind=request->kind;
  if(request->kind==TEGU_GENERIC_DRAW_COMBINED||request->kind==TEGU_GENERIC_DRAW_COMBINED_IMMUTABLE||
     request->kind==TEGU_GENERIC_DRAW_TEXTURE) {
   TeguTexture *t=(TeguTexture *)job->compositorTextures[request->binding];
   if(!t) {snprintf(why,why_size,"v2 texture binding %u has no retained object",request->binding);return -1;}
   const TeguAIRGraphicsSpecializationBinding *sb=tegu_generic_v2_plan_binding(job,request->binding);
   /* A v3-only draw can keep a one-level immutable sampler on the legacy
    * q->sampler word. Do not synthesize exact sampler state for it. */
   if(request->kind==TEGU_GENERIC_DRAW_COMBINED_IMMUTABLE&&allow_legacy_immutable&&
      t->textureKind==2&&t->levelCount==1&&t->textureSamples==1&&
      (!sb||(!sb->flags&&!sb->compare_enabled&&!sb->view_level&&!sb->view_layer&&
        (!sb->view_format_metal||sb->view_format_metal==t->format)))) continue;
   if(sb&&(sb->flags||sb->view_level||sb->view_layer)) {
    snprintf(why,why_size,"texture view flags/level/layer are not transportable yet");return -1;
   }
   if(tegu_generic_v2_layout(t,&entry->layout,sb?sb->view_format_metal:0,0,0,
      &entry->backing_length,why,why_size))return -1;
   if(!request->memory.token||request->memory.bytes<entry->backing_length||
      entry->layout.format!=request->format||entry->layout.width!=request->width||
      entry->layout.height!=request->height) {
    snprintf(why,why_size,"v2 texture facts differ from request");return -1;
   }
  }
  if(request->kind==TEGU_GENERIC_DRAW_COMBINED||request->kind==TEGU_GENERIC_DRAW_COMBINED_IMMUTABLE||
     request->kind==TEGU_GENERIC_DRAW_SAMPLER) {
   TeguAIRGenericBinding b={0};b.binding=request->binding;b.kind=request->kind;
   const TeguAIRGraphicsSpecializationBinding *sb=tegu_generic_v2_plan_binding(job,request->binding);
   if(tegu_generic_v2_sampler(&b,job,sb?sb->compare_enabled:0,
      &entry->sampler,why,why_size))return -1;
   if(request->kind!=TEGU_GENERIC_DRAW_SAMPLER) {
    int rc=n2t_sampler_layout_check(&entry->sampler,&entry->layout);
    if(rc) {snprintf(why,why_size,"n2t sampler/layout %d: %s",rc,n2t_error_string(rc));return -1;}
   }
  }
  tmp.count++;
 }
 *out=tmp;return 0;
}
/* True when the request needs fields the old sampler word/single-plane path
 * cannot express. The caller must use v2 or return a command error. */
/* Quiet-build TEGU_COMMAND_FAILED suffix: which job the transport refused. */
static void tegu_memory_trace(const char *,TeguComputeDevice *);
static __attribute__((unused)) void tegu_command_failed_job_detail(const struct Dispatch *j,unsigned index,char *out,size_t size) {
 const TeguTexture *t=(const TeguTexture *)j->renderTexture;
 TeguRenderPipeline *rp=(TeguRenderPipeline *)j->renderPipeline;TeguComputeDevice *d=rp?rp->owner:0;
 if(!d&&t)d=(TeguComputeDevice *)[(TeguTexture *)t device]; /* clear jobs carry no pipeline */
 int bridgeSite=0,n2rSite=0;
#ifdef NORD2_JM_TRANSPORT
 if(d)bridgeSite=n2mb_last_failure(d->connection,&n2rSite);
#else
 (void)d;
#endif
 snprintf(out,size," site=%d/%d job=%u kind=%s fs=%.48s copy=%u target=%lux%lu f%lu row=%lu imp=%u load=%u bindings=%u v2=%u",
  bridgeSite,n2rSite,index,
  j->blit?"blit":j->nord2ClearOnly?"clear":j->genericDrawSnapshot?"draw":"other",j->genericName,
  (unsigned)((j->nord2CopySources[0]!=0)+(j->nord2CopySources[1]!=0)),
  t?(unsigned long)t->textureWidth:0,t?(unsigned long)t->textureHeight:0,t?(unsigned long)t->format:0,
  t?(unsigned long)t->rowBytes:0,t?(unsigned)t->surfaceImported:0,(unsigned)j->genericDrawRequest.load_action,
  (unsigned)j->genericDrawRequest.binding_count,(unsigned)tegu_generic_v2_required(j));
 tegu_memory_trace("command_failed",d);
}
static int tegu_generic_v2_required(const struct Dispatch *job) {
 if(!job)return 0;
 TeguRenderPipeline *p=(TeguRenderPipeline *)job->renderPipeline;
 uint64_t selected=job->renderArgs[0];
 unsigned count=p?__atomic_load_n(&p->genericVariantCount,__ATOMIC_ACQUIRE):0;
 if(p&&selected&&selected<=count) {
  const struct tegu_render_generic_variant *v=&p->genericVariants[selected-1];
  for(uint32_t i=0;i<v->binding_count;i++) {
   const TeguAIRGraphicsSpecializationBinding *b=&v->bindings[i];
   if(b->compare_enabled||b->view_level||b->view_layer||b->flags)return 1;
   if(b->kind!=TEGU_AIR_GENERIC_COMBINED&&b->kind!=TEGU_AIR_GENERIC_COMBINED_IMMUTABLE&&
      b->kind!=TEGU_AIR_GENERIC_TEXTURE)continue;
   TeguTexture *t=(TeguTexture *)job->compositorTextures[b->binding];
   if(!t||(b->view_format_metal&&b->view_format_metal!=t->format))return 1;
  }
 }
 for(uint32_t i=0;i<job->genericDrawRequest.binding_count;i++) {
  const struct tegu_generic_draw_binding *b=&job->genericDrawRequest.bindings[i];
  if(b->kind==TEGU_GENERIC_DRAW_COMBINED||b->kind==TEGU_GENERIC_DRAW_COMBINED_IMMUTABLE||
     b->kind==TEGU_GENERIC_DRAW_TEXTURE) {
   TeguTexture *t=(TeguTexture *)job->compositorTextures[b->binding];
   if(t&&(t->levelCount>1||t->textureKind!=2||t->textureSamples>1||t->layers>1))return 1;
  }
  if(b->kind==TEGU_GENERIC_DRAW_COMBINED||b->kind==TEGU_GENERIC_DRAW_SAMPLER) {
   TeguSampler *s=(TeguSampler *)job->compositorSamplers[b->binding];
   if(!s||!tegu_sampler_legacy_word_ok(s))return 1;
  }
 }

 return 0;
}

static uint32_t tegu_render_generic_tls_for_job(const struct Dispatch *job) {
 TeguRenderPipeline *p=job?(TeguRenderPipeline *)job->renderPipeline:0;
 if(!p)return 0;
 uint64_t selected=job->renderArgs[0];
 unsigned count=__atomic_load_n(&p->genericVariantCount,__ATOMIC_ACQUIRE);
 if(selected&&selected<=count)return p->genericVariants[selected-1].tls>p->genericTls?
  p->genericVariants[selected-1].tls:p->genericTls;
 return p->genericTls;
}
/* Attach TLS and/or true-MRT target descriptors to this generic Dispatch.
 * compositorPlan is otherwise unused by the generic path and is released with
 * the command, so no Dispatch ABI change is needed. */
static int tegu_render_generic_sidecar_build(TeguRenderEncoder *e,struct Dispatch *job,
 int trueMrt,unsigned singleRt){
 if(!e||!job||job->compositorPlan.bytes)return -1;
 TeguRenderPipeline *p=e->pipeline;uint32_t tls=tegu_render_generic_tls_for_job(job);
 TeguTexture *single=singleRt<4?tegu_render_target_for_rt(e,singleRt):0;
 int msaa=!trueMrt&&single&&single->textureSamples==4;
 if(!tls&&!trueMrt&&!msaa)return 0;
 struct tegu_render_generic_sidecar *sc=calloc(1,sizeof(*sc));
 if(!sc)return -1;
 sc->magic=TEGU_RENDER_SIDECAR_MAGIC;sc->version=TEGU_RENDER_SIDECAR_VERSION;sc->tls=tls;
 if(trueMrt){
  if(!p||!p->genericTrueMrt||p->genericRtCount>4)goto bad;
  sc->count=p->genericRtCount;
  for(unsigned rt=0;rt<p->genericRtCount;rt++){
   TeguTexture *t=tegu_render_target_for_rt(e,rt);
   if(!t||t->format!=p->genericRtFormats[rt]||rt>=4)goto bad;
   U64 token=t->surfaceImported?t->importedHandle:(t->buffer?t->buffer->handle:0);
   U64 length=t->surfaceImported?t->surfaceAllocSize:(t->buffer?t->buffer->logical:0);
   if(!token||t->mip[0].offset>length)goto bad;
   struct n2mb_render_target *x=&sc->targets[rt];
   x->token=token;x->offset=t->mip[0].offset;x->length=length;
   x->format=(uint32_t)t->format;x->load_action=2;x->write_mask=p->genericRtMasks[rt];x->reserved=0;
   x->row_bytes=(uint32_t)t->rowBytes;x->width=(uint32_t)t->textureWidth;x->height=(uint32_t)t->textureHeight;
   memcpy(x->clear_words,e->renderClears[rt],sizeof(x->clear_words));
   sc->blends[rt]=p->genericRtBlends[rt];
   if(sc->blends[rt].write_mask!=p->genericRtMasks[rt])goto bad;
   sc->sample_counts[rt]=t->textureSamples;
   if(sc->sample_counts[rt]!=sc->sample_counts[0])goto bad;
   if(t->textureSamples==4){
    TeguTexture *r=e->resolveTargets[rt];
    U64 rtok=r?r->surfaceImported?r->importedHandle:(r->buffer?r->buffer->handle:0):0;
    U64 rlen=r?r->surfaceImported?r->surfaceAllocSize:(r->buffer?r->buffer->logical:0):0;
    if(!r||!rtok||r->textureWidth!=t->textureWidth||r->textureHeight!=t->textureHeight||
       r->format!=t->format||r->mip[0].offset>rlen)goto bad;
    sc->resolves[rt]=(struct n2mb_resolve_target){rtok,r->mip[0].offset,rlen,(uint32_t)r->format,
     (uint32_t)r->rowBytes,(uint32_t)r->textureWidth,(uint32_t)r->textureHeight,0};
    if(!job->compositorTextures[23+rt])job->compositorTextures[23+rt]=[r retain];
    sc->msaa=1;
   }
   if(!job->compositorTextures[27+rt])job->compositorTextures[27+rt]=[t retain];
  }
  job->compositorAttachmentCount=(unsigned)p->genericRtCount;
 } else if(msaa&&single){
  unsigned rt=singleRt;
  if(rt)goto bad;
  U64 token=single->surfaceImported?single->importedHandle:(single->buffer?single->buffer->handle:0);
  U64 length=single->surfaceImported?single->surfaceAllocSize:(single->buffer?single->buffer->logical:0);
  if(!token||single->mip[0].offset>length)goto bad;
  struct n2mb_render_target *x=&sc->targets[0];
  x->token=token;x->offset=single->mip[0].offset;x->length=length;
  x->format=(uint32_t)single->format;x->load_action=2;
  x->write_mask=p?p->genericRtMasks[rt]:15;x->reserved=0;
  x->row_bytes=(uint32_t)single->rowBytes;x->width=(uint32_t)single->textureWidth;
  x->height=(uint32_t)single->textureHeight;
  memcpy(x->clear_words,e->renderClears[rt],sizeof(x->clear_words));
  if(p)sc->blends[0]=p->genericRtBlends[rt];
  else sc->blends[0]=(TeguAIRRenderBlend){1,0,0,1,0,0,1,0,15};
  sc->sample_counts[0]=4;sc->count=1;sc->msaa=1;
  TeguTexture *r=e->resolveTargets[rt];
  U64 rtok=r?r->surfaceImported?r->importedHandle:(r->buffer?r->buffer->handle:0):0;
  U64 rlen=r?r->surfaceImported?r->surfaceAllocSize:(r->buffer?r->buffer->logical:0):0;
  if(!r||!rtok||r->textureWidth!=single->textureWidth||r->textureHeight!=single->textureHeight||
     r->format!=single->format||r->mip[0].offset>rlen)goto bad;
  sc->resolves[0]=(struct n2mb_resolve_target){rtok,r->mip[0].offset,rlen,(uint32_t)r->format,
   (uint32_t)r->rowBytes,(uint32_t)r->textureWidth,(uint32_t)r->textureHeight,0};
  if(!job->compositorTextures[23])job->compositorTextures[23]=[r retain];
  if(!job->compositorTextures[27])job->compositorTextures[27]=[single retain];
  job->compositorAttachmentCount=1;
 }
 job->compositorPlan.bytes=(uint8_t *)sc;job->compositorPlan.length=sizeof(*sc);return 0;
bad:
 free(sc);memset(&job->compositorPlan,0,sizeof(job->compositorPlan));return -1;
}
static int tegu_render_generic_snapshot_rt(TeguRenderEncoder *,const TeguAIRRenderDraw *,TeguBuffer *,NSUInteger,struct Dispatch *,unsigned);
static int tegu_render_generic_snapshot(TeguRenderEncoder *e,const TeguAIRRenderDraw *draw,
 TeguBuffer *expandedVertex,NSUInteger expandedOffset,struct Dispatch *job) {
 if(!e||!e->pipeline)return -1;
 if(e->pipeline->genericTrueMrt){
  if(!e->pipeline->genericMrt)return -11;
  for(unsigned rt=0;rt<e->pipeline->genericRtCount;rt++)
   if(tegu_render_load_for_rt(e,rt)==1)return -11;
  if(tegu_render_jobs_for_rt(e,e->pipeline->genericMainRt)>1)return -11;
  int trueRc=tegu_render_generic_snapshot_rt(e,draw,expandedVertex,expandedOffset,job,e->pipeline->genericMainRt);
  if(trueRc)return trueRc;
  for(unsigned rt=0;rt<e->pipeline->genericRtCount;rt++)if(rt!=e->pipeline->genericMainRt)e->renderJobs[rt]++;
  if(tegu_render_generic_sidecar_build(e,job,1,0))return -11;
  return 0;
 }
 /* Snapshots preserve the pre-draw value of a target that another per-RT
  * job of this same draw writes first. They are needed only when the draw
  * writes two or more targets and the fragment can fetch one of them; a
  * fetch of an unwritten attachment reads its memory directly and a fetch
  * of the job's own target is n2r's attachment snapshot. Copying two whole
  * display targets per draw otherwise (c6fc: luma_boost/fixed_frag_lph_cph,
  * 1125x2436) exhausted the transport allocator and dropped every frame. */
 uint32_t written=e->pipeline->genericRtActive&0xfu;
 int mrtSnapshots=e->pipeline->genericMrt&&__builtin_popcount(written)>1&&(e->pipeline->genericFsSubpass&written);
 if(mrtSnapshots){
  /* The bridge's CPU snapshot copy facility is bounded to RT0/RT1. RT2/RT3
   * cross-target reads use the target memory only when its job has not run
   * yet; same-target feedback is handled by n2r's attachment snapshot. */
  for(unsigned rt=0;rt<2;rt++){
   TeguTexture *t=tegu_render_target_for_rt(e,rt);
   if(!t)continue;
   if(!e->nord2MrtSnapshots[rt])
    e->nord2MrtSnapshots[rt]=[e->command->queue->owner newBufferWithLength:t->rowBytes*t->textureHeight options:0];
   if(!e->nord2MrtSnapshots[rt]||e->nord2MrtSnapshots[rt]->logical!=t->rowBytes*t->textureHeight){
    tegu_render_generic_refused(-16,"MRT cross-target snapshot allocation failed");return -16;}
  }
 }
 int rc=tegu_render_generic_snapshot_rt(e,draw,expandedVertex,expandedOffset,job,e->pipeline->genericMainRt);
 if(!rc&&mrtSnapshots)for(unsigned rt=0;rt<2;rt++){
  TeguTexture *t=tegu_render_target_for_rt(e,rt);
  if(!t)continue;
  job->nord2CopySources[rt]=t->surfaceImported?t->importedHandle:t->buffer->handle;
  job->nord2CopyOffsets[rt]=t->mip[0].offset;
  job->buffers[6+rt]=[e->nord2MrtSnapshots[rt] retain];
 }
 return rc;
}
/* After a draw's RT0 job: the same draw's RT1 job, when the pipeline writes
 * color(1) and the pass has an MRT target. A failure drops only that job. */
static void tegu_render_generic_mrt(TeguRenderEncoder *e,const TeguAIRRenderDraw *draw,
 TeguBuffer *expandedVertex,NSUInteger expandedOffset);
static int tegu_render_generic_snapshot_rt(TeguRenderEncoder *e,const TeguAIRRenderDraw *draw,
 TeguBuffer *expandedVertex,NSUInteger expandedOffset,struct Dispatch *job,unsigned rt) {
 TeguRenderPipeline *p=e?e->pipeline:0;
 unsigned primitive=e?e->drawPrimitive:TEGU_GENERIC_DRAW_TRIANGLES;
 unsigned perPrimitive=primitive==TEGU_GENERIC_DRAW_LINES?2u:primitive==TEGU_GENERIC_DRAW_TRIANGLE_STRIP?1u:3u;
 unsigned minimum=primitive==TEGU_GENERIC_DRAW_LINES?2u:3u;
 if(!p||!p->genericDraw||!p->compiled||!draw||!job||draw->version!=1||(draw->vertex_start&&expandedVertex)||
    (p->genericTrueMrt&&tegu_render_jobs_for_rt(e,p->genericMainRt)>1)||
    draw->vertex_start>(1u<<24)||draw->vertex_count<minimum||
    draw->vertex_count>TEGU_GENERIC_DRAW_MAX_VERTICES||draw->vertex_count%perPrimitive||draw->instance_count!=1||
    !e->scissor.width||!e->scissor.height||e->storeAction!=1||e->loadAction>2||rt>3||
    (rt!=p->genericMainRt&&!(p->genericRtActive&(1u<<rt)))||
    !tegu_render_target_for_rt(e,rt)||tegu_render_load_for_rt(e,rt)>2) {
  /* Name the clause: the quiet device build otherwise reports only rc=-1. */
  const char *why=!p?"no pipeline":!p->genericDraw?"pipeline has no generic draw":!p->compiled?"pipeline has no compiled AIR":
   !draw||!job||draw->version!=1?"bad draw record":(draw->vertex_start&&expandedVertex)?"vertexStart with CPU expansion":
   (p->genericTrueMrt&&tegu_render_jobs_for_rt(e,p->genericMainRt)>1)?"true-MRT second draw in pass":
   draw->vertex_start>(1u<<24)?"vertexStart out of range":draw->vertex_count<minimum?"too few vertices":
   draw->vertex_count>TEGU_GENERIC_DRAW_MAX_VERTICES?"too many vertices":draw->vertex_count%perPrimitive?"partial primitive":
   draw->instance_count!=1?"instanced":(!e->scissor.width||!e->scissor.height)?"empty scissor":
   e->storeAction!=1?"store action":e->loadAction>2?"load action":rt>3?"target index":
   (rt!=p->genericMainRt&&!(p->genericRtActive&(1u<<rt)))?"target not written by pipeline":
   !tegu_render_target_for_rt(e,rt)?"pipeline target not bound in pass":"target load action";
  char detail[160];snprintf(detail,sizeof(detail),"generic draw precondition: %s (rt=%u main=%u active=0x%x)",why,rt,
   p?p->genericMainRt:0,p?p->genericRtActive:0);
  tegu_render_generic_refused(-1,detail);return -1;
 }
 /* Native VertexIndex can now consume the v3 FAU vertex base. CPU index
  * expansion changes the hardware id sequence and cannot reconstruct arbitrary
  * original IDs, so that combination remains an explicit refusal. */
 if(p->genericVertexIndex&&expandedVertex) {
  tegu_render_generic_refused(-13,"VS VertexIndex with CPU index expansion cannot preserve index identity");
  return -13;
 }
 TeguComputeDevice *owner=e->command->queue->owner;TeguTexture *target=tegu_render_target_for_rt(e,rt);
 int primary=rt==p->genericMainRt;
 const struct tegu_generic_draw_shader *shader=primary?&p->genericShader:rt==1?&p->rt1Shader:rt==2?&p->rt2Shader:&p->rt3Shader;
 const struct tegu_mali_generic_plan *compile=primary?&p->genericCompile:rt==1?&p->rt1Compile:rt==2?&p->rt2Compile:&p->rt3Compile;
 unsigned vslot=shader->attributes[0].buffer_index;
 /* A vertex-pulling shader has no hardware fetch base to rebase. Its
  * VertexIndex must come from the v3 FAU vertex base; a shader that does not
  * read VertexIndex cannot express a nonzero start. */
 int pulls=!shader->attribute_count;
 if(vslot>=31||(pulls&&(expandedVertex||(draw->vertex_start&&!p->genericVertexIndex))))return -1;
 TeguBuffer *vertex=pulls?0:expandedVertex?expandedVertex:e->vertexBuffers[vslot];
 /* vertexStart: per-vertex attributes are pre-rebased by start*stride; n2r
  * writes the same signed base to FAU and does not add it again. */
 NSUInteger vertexOffset=expandedVertex?expandedOffset:e->vertexOffsets[vslot]+(NSUInteger)draw->vertex_start*shader->attributes[0].stride;
 if((!pulls&&(!vertex||!vertex->handle||vertex->owner!=owner||vertexOffset>vertex->logical))||!target||[target device]!=owner)return -2;
 if(target->format!=p->genericRtFormats[rt]||!target->textureWidth||!target->textureHeight||target->textureWidth>4096||
    target->textureHeight>4096||target->rowBytes<target->textureWidth*n2r_bpp(target->format)||(target->rowBytes&63))return -3;
 U64 targetHandle=target->surfaceImported?target->importedHandle:(target->buffer?target->buffer->handle:0);
 U64 targetLength=target->surfaceImported?target->surfaceAllocSize:(target->buffer?target->buffer->logical:0);
 U64 targetOffset=target->mip[0].offset,targetNeed=(target->textureHeight-1u)*target->rowBytes+target->textureWidth*n2r_bpp(target->format);
 if(!targetHandle||(targetOffset&4095)||targetOffset>targetLength||targetNeed>targetLength-targetOffset)return -3;
 /* Every owned binding is offered; the planner keeps what the shader reads.
  * Both stages are real inputs: a VS texture/sampler at the same Metal slot as
  * an FS one maps to distinct native bindings and must not overwrite it. */
 TeguAIRRenderResource resources[31+31+31+31+16+16];unsigned count=0;
 for(unsigned stage=1;stage<=2;stage++)for(unsigned i=0;i<31;i++) {
  TeguBuffer *b=stage==1?e->vertexBuffers[i]:e->fragmentBuffers[i];
  NSUInteger offset=stage==1?e->vertexOffsets[i]:e->fragmentOffsets[i];
  if(stage==1&&i==vslot&&!pulls){b=vertex;offset=vertexOffset;}
  if(!b||!b->handle||b->owner!=owner||!b->logical||offset>b->logical)continue;
  resources[count++]=(TeguAIRRenderResource){.kind=1,.stage=stage,.index=i,.length=b->logical,.offset=offset};
 }
 for(unsigned stage=1;stage<=2;stage++)for(unsigned i=0;i<31;i++) {
  TeguTexture *t=stage==1?e->vertexTextures[i]:e->fragmentTextures[i];
  if(!t||!tegu_generic_texture_bindable(t,owner))continue;
  tegu_generic_texture_facts(t,stage,i,&resources[count++]);
 }
 for(unsigned stage=1;stage<=2;stage++)for(unsigned i=0;i<16;i++) {
  TeguSampler *s=stage==1?e->vertexSamplers[i]:e->fragmentSamplers[i];
  if(!s||!tegu_owned_class(s,"TeguSampler")||s->owner!=owner)continue;
  char swhy[96]={0};
  if(tegu_generic_sampler_facts(s,stage,i,&resources[count],swhy,sizeof(swhy))) {
   tegu_render_generic_refused(-9,swhy);return -9;
  }
  count++;
 }
 struct tegu_generic_unsupported_texture unsupported[4];
 unsigned unsupportedCount=tegu_generic_unsupported_textures(e,unsupported);
 TeguAIRGenericPlan plan;TeguAIRGraphicsSpecializationPlan spec;char error[384]={0};
 TeguAIRRenderDraw planning={1,0,draw->vertex_count,1,0};
 if(tegu_air_render_pipeline_plan_generic_specialized(p->compiled,resources,count,
    p->genericLayouts,p->genericLayoutCount,&planning,&plan,&spec,error,sizeof(error))) {
  tegu_render_generic_refused(-6,error);return -6;
 }
 unsigned variantIndex=0;
 if(tegu_render_generic_variant_select(p,rt,&spec,resources,count,&shader,&compile,
    &variantIndex,error,sizeof(error))) {
  tegu_render_generic_refused(-7,error[0]?error:"specialized variant compile refused");return -7;
 }
 if(!tegu_generic_compile_matches(compile,&plan)){
  tegu_render_generic_refused(-7,"compiled bindings differ from the draw plan");return -7;
 }
 tegu_generic_record_unreflected(compile,&plan);
 /* A missing texture can resolve to a translator Neutral resource. That is
  * legal only when the caller did not bind a texture the generic transport
  * cannot represent. If one is bound, the neutral image may be its silent
  * substitute; report and drop the draw rather than submit altered semantics. */
 if(unsupportedCount&&tegu_generic_plan_has_neutral_image(&plan)) {
  const struct tegu_generic_unsupported_texture *u=&unsupported[0];
  char detail[300];snprintf(detail,sizeof(detail),
   "neutral image input with unsupported bound texture stage=%u slot=%u kind=%lu format=%lu levels=%lu samples=%lu usage=%lu options=0x%lx imported=%u",
   u->stage,u->index,(unsigned long)u->kind,(unsigned long)u->format,(unsigned long)u->levels,
   (unsigned long)u->samples,(unsigned long)u->usage,(unsigned long)u->options,(unsigned)u->surface);
  tegu_render_generic_refused(-13,detail);return -13;
 }
 struct tegu_generic_draw_request *q=&job->genericDrawRequest;
 int rc=0;
 memset(job,0,sizeof(*job));
 job->renderPipeline=[p retain];job->renderArgs[0]=variantIndex;
 job->genericVertexBase=(int32_t)(e->drawIndexSize?e->drawIndexBaseVertex:(int32_t)draw->vertex_start);
 job->genericBaseInstance=draw->base_instance;
 uint32_t pairFlags=variantIndex?p->genericVariants[variantIndex-1].pair_flags:
  (primary?p->genericPairFlags:rt==1?p->rt1PairFlags:rt==2?p->rt2PairFlags:p->rt3PairFlags);
 uint32_t pairSysvals=variantIndex?p->genericVariants[variantIndex-1].sysval_mask:
  (primary?p->genericSysvalMask:rt==1?p->rt1SysvalMask:rt==2?p->rt2SysvalMask:p->rt3SysvalMask);
 if(pairFlags&TEGU_MALI_PAIR2_FLAT_SHADING) {
  if(primitive==TEGU_GENERIC_DRAW_LINES) {
   tegu_render_generic_refused(-13,"flat line-list provoking vertex is not representable on arch9");
   rc=-13;goto fail;
  }
  job->genericDrawFlags|=N2R_DRAW_FLAT_SHADING;
 }
 if(((pairSysvals&TEGU_GRAPHICS_SYSVAL_VERTEX_BASE)||p->genericVertexIndex)&&job->genericVertexBase)
  job->genericDrawFlags|=N2R_DRAW_WRITE_VERTEX_BASE;
 if((pairSysvals&TEGU_GRAPHICS_SYSVAL_BASE_INSTANCE)&&job->genericBaseInstance)
  job->genericDrawFlags|=N2R_DRAW_WRITE_BASE_INSTANCE;
 q->width=(uint32_t)target->textureWidth;q->height=(uint32_t)target->textureHeight;q->row_bytes=(uint32_t)target->rowBytes;
 unsigned requestedLoad=tegu_render_load_for_rt(e,rt),jobs=tegu_render_jobs_for_rt(e,rt);
 unsigned effectiveLoad=requestedLoad;
 if(jobs)effectiveLoad=(requestedLoad==2&&jobs==1)?2u:1u; /* first draw after the pass CLEAR job */
 q->load_action=effectiveLoad;q->clear_color=effectiveLoad==2?tegu_render_clear_for_rt(e,rt):0;q->vertex_count=draw->vertex_count;
 q->viewport_valid=e->nord2HasViewport;
 if(q->viewport_valid){const TeguViewport v=e->nord2Viewport;const float view[6]={v.originX,v.originY,v.width,v.height,v.znear,v.zfar};memcpy(q->viewport,view,sizeof(view));}
 q->target_format=(uint32_t)target->format;memcpy(q->clear_words,e->renderClears[rt],sizeof(q->clear_words));
 q->primitive=primitive;
 q->scissor[0]=(uint32_t)e->scissor.x;q->scissor[1]=(uint32_t)e->scissor.y;
 q->scissor[2]=(uint32_t)e->scissor.width;q->scissor[3]=(uint32_t)e->scissor.height;
 q->target_token=targetHandle;q->target_offset=targetOffset;q->target_length=targetLength;
 uint32_t stride=shader->attributes[0].stride;
 U64 vertexBytes=(U64)draw->vertex_count*stride;
 if(!pulls&&draw->base_instance) {
  U64 extra=(U64)draw->base_instance*stride;
  if(extra>~(U64)0-vertexBytes){rc=-5;goto fail;}
  vertexBytes+=extra;
 }
 if(!pulls) {
  if(!stride||vertexBytes>vertex->logical-vertexOffset){rc=-5;goto fail;}
  q->vertex=(struct tegu_generic_draw_memory){vertex->handle,vertexOffset,vertexBytes};
  job->compositorVertexBuffers[vslot]=[vertex retain];
 } else if(e->drawIndexSize) {
  TeguBuffer *ib=e->drawIndexBuffer;U64 bytes=(U64)draw->vertex_count*e->drawIndexSize;
  if(!ib||!ib->handle||ib->owner!=owner||e->drawIndexOffset>ib->logical||bytes>ib->logical-e->drawIndexOffset){rc=-5;goto fail;}
  q->index=(struct tegu_generic_draw_memory){ib->handle,e->drawIndexOffset,bytes};q->index_size=e->drawIndexSize;
  job->expandIndex=[ib retain];
 }
 for(uint32_t i=0;i<plan.binding_count;i++) {
  const TeguAIRGenericBinding *b=&plan.bindings[i];
  /* Logged by tegu_generic_record_unreflected above. The shader does not
   * consume this planner-only descriptor; neutral entries are still forwarded
   * so n2r_build emits the valid zero/neutral descriptor, not a zero word. */
  if(!tegu_generic_compiled_binding(compile,b->binding)&&
     b->kind!=TEGU_AIR_GENERIC_NEUTRAL_IMAGE&&b->kind!=TEGU_AIR_GENERIC_NEUTRAL_BUFFER&&
     b->kind!=TEGU_AIR_GENERIC_CONSTANT)continue;
  struct tegu_generic_draw_binding *o=&q->bindings[q->binding_count];
  memset(o,0,sizeof(*o));o->binding=b->binding;o->kind=b->kind;
  if(p->genericTrueMrt&&b->kind==TEGU_AIR_GENERIC_ATTACHMENT){rc=-11;goto fail;}
  if(b->kind==TEGU_AIR_GENERIC_ATTACHMENT&&(shader->flags&TEGU_GENERIC_SHADER_TILE_FETCH)) {
   if(b->attachment!=rt){rc=-11;goto fail;}
   o->kind=TEGU_GENERIC_DRAW_TILE_ATTACHMENT;q->binding_count++;continue;
  }
  /* Framebuffer fetch of the other MRT target: a texel fetch of its texture.
   * An attachment this draw does not write is read from its memory as is;
   * one it writes needs the pre-draw snapshot (RT0/RT1) or a later job. */
  if(b->kind==TEGU_AIR_GENERIC_ATTACHMENT&&b->attachment!=rt) {
   int drawWrites=b->attachment<4&&(p->genericRtActive&(1u<<b->attachment));
   if(b->attachment>=4||(drawWrites&&b->attachment<rt)){rc=-11;goto fail;} /* already written or out of class */
   TeguTexture *t=tegu_render_target_for_rt(e,b->attachment);
   if(!t||t->format!=p->genericRtFormats[b->attachment]||
      !(t->surfaceImported?t->importedHandle:(t->buffer?t->buffer->handle:0))){rc=-11;goto fail;}
   o->kind=TEGU_AIR_GENERIC_TEXTURE;
   o->memory=(struct tegu_generic_draw_memory){t->surfaceImported?t->importedHandle:t->buffer->handle,
    t->mip[0].offset,(U64)t->rowBytes*t->textureHeight};
   o->width=(uint32_t)t->textureWidth;o->height=(uint32_t)t->textureHeight;o->row_bytes=(uint32_t)t->rowBytes;
   o->format=t->format;
   if(drawWrites&&b->attachment<2) {
    TeguBuffer *snapshot=e->nord2MrtSnapshots[b->attachment];
    if(!snapshot){rc=-11;goto fail;}
    o->memory=(struct tegu_generic_draw_memory){snapshot->handle,0,snapshot->logical};
    if(!job->buffers[5])job->buffers[5]=[snapshot retain];
   }
   if(!job->compositorTextures[30])job->compositorTextures[30]=[t retain];
   q->binding_count++;continue;
  }
  if(b->kind==TEGU_AIR_GENERIC_ATTACHMENT&&b->attachment==rt) {
   o->kind=TEGU_GENERIC_DRAW_ATTACHMENT;q->binding_count++;continue;
  }
  if(b->kind==TEGU_AIR_GENERIC_BUFFER) {
   if(b->stage<1||b->stage>2||b->index>=31){rc=-8;goto fail;}
   TeguBuffer *buffer=b->stage==1?e->vertexBuffers[b->index]:e->fragmentBuffers[b->index];
   if(b->stage==1&&b->index==vslot&&!pulls)buffer=vertex;
   if(!buffer||buffer->handle==0||b->descriptor_offset>buffer->logical||b->descriptor_length>buffer->logical-b->descriptor_offset){rc=-8;goto fail;}
   o->memory=(struct tegu_generic_draw_memory){buffer->handle,b->descriptor_offset,b->descriptor_length};
   TeguBuffer **slot=b->stage==1?&job->compositorVertexBuffers[b->index]:&job->compositorFragmentBuffers[b->index];
   if(!*slot)*slot=[buffer retain];
  } else if(b->kind==TEGU_AIR_GENERIC_COMBINED||b->kind==TEGU_AIR_GENERIC_COMBINED_IMMUTABLE||b->kind==TEGU_AIR_GENERIC_TEXTURE) {
   TeguTexture *t=0;
   if(b->stage==1&&b->index<31)t=e->vertexTextures[b->index];
   else if(b->stage==2&&b->index<31)t=e->fragmentTextures[b->index];
   if(!tegu_generic_texture_bindable(t,owner)) {
    char detail[180];snprintf(detail,sizeof(detail),
     "texture stage=%u slot=%u %lux%lu f%lu kind=%lu levels=%lu samples=%lu row=%lu options=0x%lx imported=%u",
     b->stage,b->index,t?(unsigned long)t->textureWidth:0,t?(unsigned long)t->textureHeight:0,t?(unsigned long)t->format:0,
     t?(unsigned long)t->textureKind:0,t?(unsigned long)t->levelCount:0,t?(unsigned long)t->textureSamples:0,
     t?(unsigned long)t->rowBytes:0,t?(unsigned long)t->textureResourceOptions:0,t?(unsigned)t->surfaceImported:0);
    tegu_render_generic_refused(-4,detail);rc=-4;goto fail;
   }
   U64 textureSpan=0;
   if(b->format!=t->format||b->width!=t->textureWidth||b->height!=t->textureHeight||
      !tegu_generic_texture_span(t,&textureSpan)||tegu_generic_retain_texture(job,b->binding,t)) {
    tegu_render_generic_refused(-12,"texture facts, mip span, or retained binding differ from the plan");rc=-12;goto fail;
   }
   o->memory=(struct tegu_generic_draw_memory){t->surfaceImported?t->importedHandle:t->buffer->handle,
    t->mip[0].offset,textureSpan};
#if defined(TEGU_RENDER_TRACE)||defined(TEGU_ASTC_TRACE)
   if(tegu_texture_is_astc(t->format)&&t->buffer) {
    static unsigned astcBinds;
    if(__atomic_fetch_add(&astcBinds,1,__ATOMIC_RELAXED)<16) {
     const unsigned char *tx=(const unsigned char *)[t->buffer contents]+t->mip[0].offset;unsigned long nz=0;
     for(U64 w=0;tx&&w<o->memory.bytes&&w<65536;w++)nz+=tx[w]!=0;
     char line[200];int n=snprintf(line,sizeof(line),"TEGU_ASTC_TEXTURE bind fs=%s b=%u kind=%u %lux%lu f%lu row=%lu bytes=%llu nonzero=%lu\n",
      p->genericName,b->binding,b->kind,(unsigned long)t->textureWidth,(unsigned long)t->textureHeight,(unsigned long)t->format,
      (unsigned long)t->rowBytes,(U64)o->memory.bytes,nz);
     if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
    }
   }
#endif
   o->width=(uint32_t)t->textureWidth;o->height=(uint32_t)t->textureHeight;o->row_bytes=(uint32_t)t->rowBytes;
   o->format=(uint32_t)t->format;
   if(b->kind==TEGU_AIR_GENERIC_COMBINED_IMMUTABLE&&tegu_generic_immutable_sampler(b,&o->sampler)){rc=-9;goto fail;}
  }
  if(b->kind==TEGU_AIR_GENERIC_COMBINED||b->kind==TEGU_AIR_GENERIC_SAMPLER) {
   TeguSampler *s=0;
   if(b->sampler_stage==1&&b->sampler_index<16)s=e->vertexSamplers[b->sampler_index];
   else if(b->sampler_stage==2&&b->sampler_index<16)s=e->fragmentSamplers[b->sampler_index];
   if(!s||s->owner!=owner||tegu_generic_retain_sampler(job,b->binding,s)){rc=-9;goto fail;}
   /* The legacy word carries only min/mag and clamp wrap. A state the word
    * cannot express must wait for the v2 sidecar transport, not be reduced. */
   if(tegu_sampler_legacy_word_ok(s))
    o->sampler=TEGU_GENERIC_DRAW_SAMPLER_WORD(s->linearFilters&1u,(s->linearFilters>>1)&1u,0,0,0);
   else {
#ifdef NORD2_JM_TRANSPORT
    o->sampler=0; /* exact retained state travels in the n2r v2 sidecar */
#else
    tegu_render_generic_refused(-9,"non-JM transport cannot carry exact sampler state");
    rc=-9;goto fail;
#endif
   }
  }
  q->binding_count++;
 }
 for(uint32_t i=0;i<plan.push_count;i++) {
  const TeguAIRFixedUiR85PushSpan *s=&plan.pushes[i];
  if(s->offset>TEGU_GENERIC_DRAW_PUSH_BYTES||s->length>TEGU_GENERIC_DRAW_PUSH_BYTES-s->offset||
     s->bytes_offset>plan.push_bytes_length||s->length>plan.push_bytes_length-s->bytes_offset||!(s->stage&17u)||(s->stage&~17u)){rc=-10;goto fail;}
  if(s->stage&1u){memcpy(q->vertex_push+s->offset,plan.push_bytes+s->bytes_offset,s->length);
   if(s->offset+s->length>q->vertex_push_bytes)q->vertex_push_bytes=s->offset+s->length;}
  if(s->stage&16u){memcpy(q->fragment_push+s->offset,plan.push_bytes+s->bytes_offset,s->length);
   if(s->offset+s->length>q->fragment_push_bytes)q->fragment_push_bytes=s->offset+s->length;}
 }
 if(plan.constant_length>TEGU_GENERIC_DRAW_CONSTANT_BYTES){rc=-10;goto fail;}
 memcpy(q->constant,plan.constant_bytes,plan.constant_length);q->constant_bytes=plan.constant_length;
 /* Multi-level/array textures and non-legacy sampler states must be
  * represented by validated N2T facts before submission. The transport still
  * needs n2mb_draw_v2; until that exists the full-span request makes the old
  * builder refuse instead of sampling LOD0. */
 if(tegu_generic_v2_required(job)) {
  struct tegu_generic_v2_sidecar sidecar;char vwhy[160]={0};
  if(tegu_generic_v2_build_job(job,&sidecar,0,vwhy,sizeof(vwhy))) {
   tegu_render_generic_refused(-13,vwhy);rc=-13;goto fail;
  }
 }
 job->genericDrawSnapshot=1;memcpy(job->genericName,p->genericName,sizeof(job->genericName));
#ifdef TEGU_RENDER_TRACE
 /* What each generic draw is fed: bindings, scissor, positions, uniforms. */
 static unsigned inputTraces;
 if(__atomic_fetch_add(&inputTraces,1,__ATOMIC_RELAXED)<6) {
  char line[400];int n=snprintf(line,sizeof(line),"TEGU_GENERIC_INPUT fs=%s target=%ux%u row=%u scissor=%u,%u,%u,%u vertices=%u vpush=%u fpush=%u const=%u\n",
   p->genericName,q->width,q->height,q->row_bytes,q->scissor[0],q->scissor[1],q->scissor[2],q->scissor[3],q->vertex_count,
   q->vertex_push_bytes,q->fragment_push_bytes,q->constant_bytes);
  if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
  const float *v=(const float *)((const unsigned char *)tegu_buffer_bytes(vertex)+vertexOffset);
  for(unsigned k=0;v&&k<q->vertex_count&&k<6;k++){const float *x=v+k*(stride/4);
   n=snprintf(line,sizeof(line),"TEGU_GENERIC_VERTEX fs=%s v=%u pos=%g,%g,%g,%g a1=%g,%g a2=%g,%g\n",p->genericName,k,x[0],x[1],x[2],x[3],x[4],x[5],x[6],x[7]);
   if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);}
  for(uint32_t k=0;k<q->binding_count;k++) {
   const struct tegu_generic_draw_binding *o=&q->bindings[k];
   n=snprintf(line,sizeof(line),"TEGU_GENERIC_BINDING fs=%s b=%u kind=%u offset=%llu bytes=%llu tex=%ux%u f%u row=%u sampler=0x%x\n",
    p->genericName,o->binding,o->kind,(U64)o->memory.offset,(U64)o->memory.bytes,o->width,o->height,o->format,o->row_bytes,o->sampler);
   if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
   if(o->kind==TEGU_AIR_GENERIC_BUFFER) {
    const TeguAIRGenericBinding *pb=0;for(uint32_t m=0;m<plan.binding_count;m++)if(plan.bindings[m].binding==o->binding)pb=&plan.bindings[m];
    TeguBuffer *bb=pb?(pb->stage==1?(pb->index==vslot?vertex:e->vertexBuffers[pb->index]):e->fragmentBuffers[pb->index]):0;
    const float *f=bb?(const float *)((const unsigned char *)[bb contents]+o->memory.offset):0;
    for(unsigned w=0;f&&w<o->memory.bytes/4&&w<48;w+=8){
     n=snprintf(line,sizeof(line),"TEGU_GENERIC_UNIFORM fs=%s b=%u at=%u %g %g %g %g %g %g %g %g\n",p->genericName,o->binding,w*4,
      f[w],w+1<o->memory.bytes/4?f[w+1]:0,w+2<o->memory.bytes/4?f[w+2]:0,w+3<o->memory.bytes/4?f[w+3]:0,
      w+4<o->memory.bytes/4?f[w+4]:0,w+5<o->memory.bytes/4?f[w+5]:0,w+6<o->memory.bytes/4?f[w+6]:0,w+7<o->memory.bytes/4?f[w+7]:0);
     if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);}
   }
   if(o->kind==TEGU_AIR_GENERIC_COMBINED_IMMUTABLE||o->kind==TEGU_AIR_GENERIC_COMBINED||o->kind==TEGU_AIR_GENERIC_TEXTURE) {
    TeguTexture *t=0;for(uint32_t m=0;m<plan.binding_count;m++)if(plan.bindings[m].binding==o->binding)
     t=plan.bindings[m].stage==1?e->vertexTextures[plan.bindings[m].index]:e->fragmentTextures[plan.bindings[m].index];
    const unsigned char *tx=t&&t->buffer?(const unsigned char *)[t->buffer contents]+t->mip[0].offset:0;
    unsigned long sum=0,nz=0;for(unsigned long w=0;tx&&w<o->memory.bytes&&w<65536;w++){sum+=tx[w];nz+=tx[w]!=0;}
    n=snprintf(line,sizeof(line),"TEGU_GENERIC_TEXEL fs=%s b=%u sum=%lu nonzero=%lu first=%u,%u,%u,%u\n",p->genericName,o->binding,sum,nz,
     tx?tx[0]:0,tx&&o->memory.bytes>1?tx[1]:0,tx&&o->memory.bytes>64?tx[64]:0,tx&&o->memory.bytes>127?tx[127]:0);
    if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
   }
  }
 }
#endif
 /* CLEAR jobs already precede both targets; framebuffer fetch sees LOAD. */
 job->renderArgs[1]=targetHandle;job->renderArgs[2]=targetOffset;job->renderArgs[3]=q->clear_color;
 job->compositorLoadAction=effectiveLoad;job->compositorStoreAction=e->storeAction;
 job->renderTexture=[target retain];
 e->renderJobs[rt]++;if(rt==0)e->rt0Jobs++;else if(rt==1)e->mrtDraws++;else if(rt==2)e->mrtDraws2++;else e->mrtDraws3++;
 if(!p->genericTrueMrt&&tegu_render_generic_sidecar_build(e,job,0,rt))return -11;
 return 0;
fail:
 for(unsigned i=0;i<8;i++)[job->buffers[i] release];
 for(unsigned i=0;i<31;i++){[job->compositorVertexBuffers[i] release];[job->compositorFragmentBuffers[i] release];[job->compositorTextures[i] release];}
 for(unsigned i=0;i<16;i++)[job->compositorSamplers[i] release];
 [job->expandIndex release];[job->expandSource release];[job->expandOut release];
 [job->renderPipeline release];[job->renderTexture release];
 memset(job,0,sizeof(*job));return rc;
}
static void tegu_render_generic_mrt(TeguRenderEncoder *e,const TeguAIRRenderDraw *draw,
 TeguBuffer *expandedVertex,NSUInteger expandedOffset) {
 TeguRenderPipeline *p=e?e->pipeline:0;
 if(!p||!p->genericMrt||p->genericTrueMrt)return;
 for(unsigned rt=0;rt<4;rt++) {
  if(rt==p->genericMainRt||!(p->genericRtActive&(1u<<rt)))continue;
  if(!tegu_render_target_for_rt(e,rt)||e->command->count>=TEGU_COMMAND_MAX_JOBS){TEGU_INVALID(e->command);return;}
  struct Dispatch *job=tegu_command_job(e->command,e->command->count);
  if(!tegu_render_generic_snapshot_rt(e,draw,expandedVertex,expandedOffset,job,rt))e->command->count++;
  else TEGU_INVALID(e->command);
 }
}

static int tegu_attachment_clear_target_binding(TeguTexture *t,TeguComputeDevice *owner,
 uint32_t width,uint32_t height,struct tegu_attachment_clear_target_request *out) {
 if(!t||!out||!tegu_owned_class(t,"TeguTexture")||[t device]!=owner||t->format!=80||
    t->textureWidth!=width||t->textureHeight!=height||t->levelCount!=1||!tegu_iosurface_render_target_usage(t->textureUsage)||
    t->mip[0].width!=width||t->mip[0].height!=height||t->rowBytes!=t->mip[0].stride||
    t->rowBytes<width*4u||(t->rowBytes&63)||t->rowBytes>65536||
    t->rowBytes>SIZE_MAX/height||t->rowBytes*height>(16u<<20))return -1;
 U64 handle=0,offset=t->mip[0].offset,span=t->rowBytes*height;
 U64 mapped=(span+4095u)&~UINT64_C(4095);
 if((offset&4095)||mapped<span||mapped>(16u<<20))return -1;
 if(t->surfaceImported) {
  if(t->surfaceTargetVersion!=1||!(owner->capabilities&TEGU_GPU_CAP_COMPOSITOR_IOSURFACE)||
     t->surfaceRowBytes!=t->rowBytes||offset>t->surfaceAllocSize||mapped>t->surfaceAllocSize-offset)return -1;
  handle=t->importedHandle;
 } else {
  if(!t->buffer||t->buffer->owner!=owner||offset>t->buffer->logical||span>t->buffer->logical-offset||
     offset>t->buffer->mapped||mapped>t->buffer->mapped-offset||
     offset>t->buffer->allocated||mapped>t->buffer->allocated-offset)return -1;
  handle=t->buffer->handle;
 }
 if(!handle||!mapped)return -1;
 out->memory=(struct tegu_render_vertex_binding){handle,offset,mapped};
 out->format=80;out->row_stride=(uint32_t)t->rowBytes;return 0;
}

static int tegu_render_attachment_clear_snapshot(TeguRenderEncoder *e,struct Dispatch *job) {
 if(!e||!job||!e->attachmentClearPass||e->draws||!e->pipeline||!e->pipeline->handle||
    !e->pipeline->attachmentClearSnapshotOnly||
    !(e->command->queue->owner->capabilities&TEGU_GPU_CAP_ATTACHMENT_CLEAR))return -1;
 TeguComputeDevice *owner=e->command->queue->owner;TeguTexture *rt0=e->attachmentClearTargets[0],*rt1=e->attachmentClearTargets[1];
 if(!rt0||!rt1||rt0==rt1)return -2;
 uint32_t width=(uint32_t)rt0->textureWidth,height=(uint32_t)rt0->textureHeight;
 if(width<16||height<16||width>4096||height>4096)return -2;
 TeguBuffer *rect=e->vertexBuffers[TEGU_ATTACHMENT_CLEAR_RECT_SLOT];
 TeguBuffer *matrix=e->vertexBuffers[TEGU_ATTACHMENT_CLEAR_MATRIX_SLOT];
 U64 ro=e->vertexOffsets[TEGU_ATTACHMENT_CLEAR_RECT_SLOT],mo=e->vertexOffsets[TEGU_ATTACHMENT_CLEAR_MATRIX_SLOT];
 if(!rect||!matrix||rect==matrix||rect->owner!=owner||matrix->owner!=owner||!rect->handle||!matrix->handle||
    (ro&3)||(mo&15)||ro>rect->logical||TEGU_ATTACHMENT_CLEAR_RECT_BYTES>rect->logical-ro||
    mo>matrix->logical||TEGU_ATTACHMENT_CLEAR_MATRIX_BYTES>matrix->logical-mo)return -3;
 struct tegu_attachment_clear_request r={0};
 r.version=TEGU_ATTACHMENT_CLEAR_REQUEST_VERSION;r.width=width;r.height=height;r.sample_count=1;
 r.attachment_count=2;r.target_mask=TEGU_ATTACHMENT_CLEAR_TARGET_MASK;
 r.draw_write_mask=TEGU_ATTACHMENT_CLEAR_TARGET_MASK;
 if(tegu_attachment_clear_target_binding(rt0,owner,width,height,&r.targets[0])||
    tegu_attachment_clear_target_binding(rt1,owner,width,height,&r.targets[1]))return -4;
 if(r.targets[0].memory.handle==r.targets[1].memory.handle)return -4;
 r.targets[0].write_mask=TEGU_ATTACHMENT_CLEAR_RT0_MASK;
 r.targets[1].write_mask=TEGU_ATTACHMENT_CLEAR_RT1_MASK;
 for(unsigned i=0;i<2;i++) {
  r.targets[i].load_action=e->attachmentClearLoads[i];r.targets[i].store_action=e->attachmentClearStores[i];
  r.targets[i].clear_rgba8=e->attachmentClearColors[i];
 }
 r.rect=(struct tegu_render_vertex_binding){rect->handle,ro,TEGU_ATTACHMENT_CLEAR_RECT_BYTES};
 r.matrix=(struct tegu_render_vertex_binding){matrix->handle,mo,TEGU_ATTACHMENT_CLEAR_MATRIX_BYTES};
 r.primitive_type=3;r.vertex_count=3;r.instance_count=1;
 memset(job,0,sizeof(*job));job->attachmentClearSnapshot=1;job->attachmentClearPipelineHandle=e->pipeline->handle;job->attachmentClearRequest=r;
 job->renderPipeline=[e->pipeline retain];
 job->attachmentClearTargets[0]=[rt0 retain];job->attachmentClearTargets[1]=[rt1 retain];
 job->attachmentClearBuffers[0]=[rect retain];job->attachmentClearBuffers[1]=[matrix retain];
 return 0;
}
/* Create a metadata-only draw snapshot from actual encoder-owned Metal objects.
 * The typed plan determines the retention masks; supplied but unreflected slots
 * never enter the returned snapshot. This is intentionally not a submission API. */
static int tegu_render_compositor_snapshot(TeguRenderEncoder *e,const TeguAIRRenderDraw *draw,struct Dispatch *job) {
 if(!e||!draw||!job||!e->pipeline||!e->pipeline->compiled||draw->version!=1||draw->vertex_count!=3||
    draw->instance_count!=1||draw->vertex_start||draw->base_instance||
    (!e->pipeline->compositorSnapshotOnly&&!e->pipeline->homeAffordanceObserverOnly&&!e->pipeline->fixedUiSnapshotOnly)||
    (e->loadAction!=2&&e->loadAction!=1&&!((e->pipeline->homeAffordanceObserverOnly||e->pipeline->compositorB3)&&e->loadAction==0))||e->storeAction!=1)return -1;
 TeguComputeDevice *owner=e->command->queue->owner;TeguAIRRenderResource resources[186];size_t count=0;
 if(e->pipeline->compositorB3) {
  TeguTexture *rt1=e->attachmentClearTargets[1];
  if(!e->twoTargetPass||!rt1||rt1==e->target||!tegu_b3_private_mip0_texture(e->target,owner)||
     !tegu_b3_private_mip0_texture(rt1,owner)||e->attachmentClearLoads[1]||
     e->attachmentClearStores[1])return -1;
 }
 TeguAIRRenderResource *r=resources;
 for(unsigned stage=1;stage<=2;stage++)for(unsigned i=0;i<31;i++) {
  TeguBuffer *b=stage==1?e->vertexBuffers[i]:e->fragmentBuffers[i];
  if(!b)continue;
  if(!tegu_owned_class(b,"TeguBuffer")||b->owner!=owner||
     (stage==1?e->vertexOffsets[i]:e->fragmentOffsets[i])>b->logical)return -2;
  memset(r,0,sizeof(*r));r->kind=1;r->stage=stage;r->index=i;r->length=b->logical;
  r->offset=stage==1?e->vertexOffsets[i]:e->fragmentOffsets[i];r->gpu_address=0;r++;
  if(++count>186)return -3;
 }
 for(unsigned i=0;i<31;i++)if(e->fragmentTextures[i]) {
  if(!tegu_compositor_texture_resource(e->fragmentTextures[i],owner,i,r))return -2;
  r++;if(++count>186)return -3;
 }
 for(unsigned i=0;i<16;i++)if(e->fragmentSamplers[i]) {
  if(!tegu_owned_class(e->fragmentSamplers[i],"TeguSampler")||((TeguSampler *)e->fragmentSamplers[i])->owner!=owner||!tegu_sampler_legacy_ok((TeguSampler *)e->fragmentSamplers[i]))return -2;
  tegu_compositor_sampler_resource(r,i);r++;if(++count>186)return -3;
 }
 TeguAIRBlob plan={0};TeguAIRRenderTypedPlan typed={0};char error[512]={0};
 if(tegu_air_render_pipeline_plan_draw_typed(e->pipeline->compiled,resources,count,draw,&plan,&typed,error,sizeof(error))) {
  if(e->pipeline->homeAffordanceObserverOnly) {
   static unsigned emitted;
   if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)<8) {
    char line[256];int n=snprintf(line,sizeof(line),"TEGU_RESOURCE_PLAN_REFUSED class=%u error=%.180s\n",
     e->pipeline->homeAffordanceObserverOnly,error);
    if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
   }
  }
  return -40;
 }
 if(!plan.bytes||!plan.length||typed.version!=1||typed.count>186||typed.attachment_count!=2||
    typed.diagnostic_count||typed.neutral_count) {if(plan.bytes)tegu_air_blob_free(&plan);return -41;}
 int typedSupported=e->pipeline->homeAffordanceObserverOnly==1?tegu_home_affordance_typed_plan_supported(&typed):
  (e->pipeline->homeAffordanceObserverOnly==2||e->pipeline->compositorB3)?tegu_narrow_blur_11_typed_plan_supported(&typed):
  tegu_compositor_typed_uses_supported(&typed);
 if(!typedSupported){
  tegu_air_blob_free(&plan);return -42;
 }
 if(typed.attachments[0].format!=e->pipeline->colorFormat||typed.attachments[0].write_mask!=15||
    typed.attachments[1].format!=e->pipeline->colorFormat||typed.attachments[1].write_mask!=0) {tegu_air_blob_free(&plan);return -5;}
 memset(job,0,sizeof(*job));job->compositorPlan=plan;job->compositorSnapshot=1;
 job->compositorAttachmentCount=typed.attachment_count;
 for(unsigned i=0;i<typed.attachment_count;i++) {
  job->compositorAttachments[i].format=typed.attachments[i].format;
  job->compositorAttachments[i].writeMask=typed.attachments[i].write_mask;
 }
 job->compositorLoadAction=e->draws?(unsigned)TEGU_COMPOSITOR_LOAD_LOAD:
  (e->pipeline->compositorB3&&e->loadAction==0?(unsigned)TEGU_COMPOSITOR_LOAD_CLEAR:(unsigned)e->loadAction);
 job->compositorStoreAction=(unsigned)e->storeAction;
 for(unsigned i=0;i<31;i++) {
  const TeguAIRRenderResourceUse *vu=tegu_typed_use(&typed,1,1,i);
  const TeguAIRRenderResourceUse *fu=tegu_typed_use(&typed,1,2,i);
  const TeguAIRRenderResourceUse *tu=tegu_typed_use(&typed,2,2,i);
  if(vu) {
   TeguBuffer *b=e->vertexBuffers[i];if(!b){tegu_compositor_job_release(job);return -6;}
   uint64_t bind=e->vertexOffsets[i],base=bind&~(uint64_t)15,residual=bind-base;
   uint64_t end=0;if((vu->flags&4)&&(__builtin_add_overflow(vu->range_offset,vu->range_length,&end)||
      __builtin_add_overflow(end,residual,&end))){tegu_compositor_job_release(job);return -8;}
   if((vu->flags&4)&&end>b->logical-base){tegu_compositor_job_release(job);return -8;}
   job->compositorVertexBufferMask|=1u<<i;job->compositorVertexBuffers[i]=[b retain];
   job->compositorVertexFacts[i].handle=b->handle;job->compositorVertexFacts[i].offset=base;
   job->compositorVertexFacts[i].available=b->logical-base;job->compositorVertexFacts[i].bindingResidual=residual;
   job->compositorVertexFacts[i].rangeOffset=vu->range_offset;job->compositorVertexFacts[i].rangeLength=vu->range_length;
   job->compositorVertexFacts[i].classMask=vu->class_mask;job->compositorVertexFacts[i].usageFlags=vu->flags;
  }
  if(fu) {
   TeguBuffer *b=e->fragmentBuffers[i];if(!b){tegu_compositor_job_release(job);return -6;}
   uint64_t bind=e->fragmentOffsets[i],base=bind&~(uint64_t)15,residual=bind-base;
   uint64_t end=0;if((fu->flags&4)&&(__builtin_add_overflow(fu->range_offset,fu->range_length,&end)||
      __builtin_add_overflow(end,residual,&end))){tegu_compositor_job_release(job);return -8;}
   if((fu->flags&4)&&end>b->logical-base){tegu_compositor_job_release(job);return -8;}
   job->compositorFragmentBufferMask|=1u<<i;job->compositorFragmentBuffers[i]=[b retain];
   job->compositorFragmentFacts[i].handle=b->handle;job->compositorFragmentFacts[i].offset=base;
   job->compositorFragmentFacts[i].available=b->logical-base;job->compositorFragmentFacts[i].bindingResidual=residual;
   job->compositorFragmentFacts[i].rangeOffset=fu->range_offset;job->compositorFragmentFacts[i].rangeLength=fu->range_length;
   job->compositorFragmentFacts[i].classMask=fu->class_mask;job->compositorFragmentFacts[i].usageFlags=fu->flags;
  }
  if(tu) {
   TeguTexture *t=e->fragmentTextures[i];if(!t||!t->buffer){tegu_compositor_job_release(job);return -6;}
   job->compositorTextureMask|=1u<<i;job->compositorTextures[i]=[t retain];
   job->compositorTextureFacts[i].handle=t->buffer->handle;job->compositorTextureFacts[i].offset=t->mip[0].offset;
   job->compositorTextureFacts[i].span=t->mip[0].stride*t->textureHeight;job->compositorTextureFacts[i].stride=t->mip[0].stride;
   job->compositorTextureFacts[i].width=t->textureWidth;job->compositorTextureFacts[i].height=t->textureHeight;job->compositorTextureFacts[i].format=t->format;
  }
 }
 for(unsigned i=0;i<16;i++) {
  const TeguAIRRenderResourceUse *su=tegu_typed_use(&typed,3,2,i);
  if(!su||!(su->flags&24))continue;
  TeguSampler *sampler=e->fragmentSamplers[i];if(!sampler){tegu_compositor_job_release(job);return -6;}
  job->compositorSamplerMask|=1u<<i;job->compositorSamplers[i]=[sampler retain];
 }
 job->renderPipeline=[e->pipeline retain];job->renderTexture=[e->target retain];
 if(e->pipeline->compositorB3&&e->twoTargetPass)
  job->attachmentClearTargets[1]=[e->attachmentClearTargets[1] retain];
 job->renderArgs[3]=job->compositorLoadAction==TEGU_COMPOSITOR_LOAD_CLEAR?e->clear:0;
 return 0;
}
/* Apple3 (reported so QuartzCore batches more than 128 draws per command
 * buffer) admits indirect draws.  They cannot be expressed yet: drop them on
 * TGJ1 compositor devices, invalidate elsewhere, never crash on a missing
 * selector. */
/* A draw the encoders cannot express: on TGJ1 compositor devices only the
 * draw is dropped (logged, capped); elsewhere the command buffer is
 * invalidated as before.  One unsupported draw used to discard whole frames
 * (R130-R137: the gesture bar vanished with them). */
/* A pass-level depth/stencil attachment can be ignored only while it is
 * unobservable.  The generic job ABI has no depth/stencil pipeline state, so
 * accept exactly the no-test/no-write case and refuse every observable draw
 * instead of producing different pixels. */
static int tegu_render_depth_stencil_exact(TeguRenderEncoder *e) {
 if(!e||(!e->hasDepthAttachment&&!e->hasStencilAttachment))return 1;
 TeguDepthStencilState *s=e->depthState;
 if(!s)return 1;
 if(e->hasDepthAttachment&&(s->state.compare!=7||s->state.write))return 0;
 if(e->hasStencilAttachment&&(s->state.front.enabled||s->state.back.enabled))return 0;
 return 1;
}
static void tegu_render_inert_refused(TeguRenderEncoder *e) {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=32)return;
 char line[280];int n=snprintf(line,sizeof(line),"TEGU_INERT_DRAW_REFUSED pipeline=%s reason=%.180s\n",
  e&&e->pipeline?e->pipeline->genericName:"",e&&e->pipeline?e->pipeline->inertReason:"inert pipeline");
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
}
static void tegu_render_draw_refused(TeguRenderEncoder *e,int line) {
#ifdef NORD2_JM_TRANSPORT
 TEGU_INVALID(e->command);
#endif
 TeguComputeDevice *owner=e->command->queue->owner;
 if(!e->ended&&(owner->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&(owner->capabilities&TEGU_GPU_CAP_GENERIC_JOB)) {
  static unsigned emitted;
  if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)<16){char text[160];int k=snprintf(text,sizeof(text),
   "TEGU_DRAW_DROPPED line=%d fs=%s\n",line,e->pipeline?e->pipeline->genericName:"");
   if(k>0)write(2,text,(size_t)k<sizeof(text)?(size_t)k:sizeof(text)-1);}
  e->draws++;return;
 }
 TEGU_INVALID(e->command);
}
/* R173: home-screen icons, labels and widget content vanish at rest while no
 * DRAW_DROPPED/GENERIC_DRAW_REFUSED line is ever logged; R172 shows display
 * passes whose nine draws queued no job.  These paths drop a draw without a
 * trace.  Always on (quiet builds too), capped: name each dropped draw. */
/* Always-on, bounded: where the per-context GPU budget went when an
 * allocation failed (private incl. bridge-internal and code cache, imported
 * IOSurfaces, BO count). */
static void tegu_memory_trace(const char *site,TeguComputeDevice *d) {
#ifdef NORD2_JM_TRANSPORT
 static unsigned emitted;
 if(!d||__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=16)return;
 uint64_t priv=0,imp=0,internal=0,code=0;unsigned bos=0;
 n2mb_memory_stats(d->connection,&priv,&imp,&internal,&code,&bos);
 char text[200];int k=snprintf(text,sizeof(text),
  "TEGU_GPU_MEMORY site=%s private_kib=%llu internal_kib=%llu code_kib=%llu imported_kib=%llu bos=%u\n",site,
  (unsigned long long)(priv>>10),(unsigned long long)(internal>>10),(unsigned long long)(code>>10),(unsigned long long)(imp>>10),bos);
 if(k>0)write(2,text,(size_t)k<sizeof(text)?(size_t)k:sizeof(text)-1);
#else
 (void)site;(void)d;
#endif
}
static void tegu_render_trace_silent(TeguRenderEncoder *e,const char *site,int rc,NSUInteger type,NSUInteger n) {
#ifdef NORD2_JM_TRANSPORT
 if(e)TEGU_INVALID(e->command);
#endif
 static unsigned emitted;
 if(!e||__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=48)return;
 TeguRenderPipeline *p=e->pipeline;TeguTexture *t=e->target,*s=e->fragmentTextures[3]?e->fragmentTextures[3]:e->fragmentTextures[0];
 char text[400];int k=snprintf(text,sizeof(text),
  "TEGU_DRAW_SILENT site=%s rc=%d fs=%s cls=g%u/u%u/t%u/mrt%u/rt1%u/mrtTarget%u type=%lu n=%lu target=%lux%lu imp=%u "
  "scissor=%lu,%lu,%lu,%lu load=%u draws=%u jobs=%u src=%lux%lu/f%lu/imp%u\n",site,rc,p?p->genericName:"",
  p?p->genericDraw:0,p?p->fixedUiSnapshotOnly:0,p?p->fixedUiTextureSnapshotOnly:0,p?p->genericMainRt:0,p?p->rt1Draw:0,
  e->mrtTarget!=0,(unsigned long)type,(unsigned long)n,
  t?(unsigned long)t->textureWidth:0,t?(unsigned long)t->textureHeight:0,t?(unsigned)t->surfaceImported:0,
  (unsigned long)e->scissor.x,(unsigned long)e->scissor.y,(unsigned long)e->scissor.width,(unsigned long)e->scissor.height,
  e->loadAction,e->draws,e->command->count,s?(unsigned long)s->textureWidth:0,s?(unsigned long)s->textureHeight:0,
  s?(unsigned long)s->format:0,s?(unsigned)s->surfaceImported:0);
 if(k>0)write(2,text,(size_t)k<sizeof(text)?(size_t)k:sizeof(text)-1);
 if(rc==-15)tegu_memory_trace("draw_silent",e->command->queue->owner);
}
- (void)drawPrimitives:(NSUInteger)type indirectBuffer:(TeguBuffer *)buffer indirectBufferOffset:(NSUInteger)offset {
 (void)type;(void)buffer;(void)offset;
#ifndef NORD2_JM_TRANSPORT
 if((command->queue->owner->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&(command->queue->owner->capabilities&TEGU_GPU_CAP_GENERIC_JOB)){draws++;return;}
#endif
 TEGU_INVALID(command);
}
- (void)drawIndexedPrimitives:(NSUInteger)type indexType:(NSUInteger)indexType indexBuffer:(TeguBuffer *)indexBuffer
 indexBufferOffset:(NSUInteger)indexOffset indirectBuffer:(TeguBuffer *)buffer indirectBufferOffset:(NSUInteger)offset {
 (void)type;(void)indexType;(void)indexBuffer;(void)indexOffset;(void)buffer;(void)offset;
 if(inert)return;
#ifndef NORD2_JM_TRANSPORT
 if((command->queue->owner->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&(command->queue->owner->capabilities&TEGU_GPU_CAP_GENERIC_JOB)){draws++;return;}
#endif
 TEGU_INVALID(command);
}
- (void)drawPrimitives:(NSUInteger)type vertexStart:(NSUInteger)start vertexCount:(NSUInteger)n {
 if(inert)return;
 if(skipPass||pipelineRefused||viewportRefused){draws++;return;}
 if(pipeline&&pipeline->inert){tegu_render_inert_refused(self);return;}
 if(!tegu_render_depth_stencil_exact(self)) {
  tegu_render_generic_refused(-14,"depth/stencil test or write has no arch9 generic job");
  return;
 }
 tegu_render_trace_draw(traceID,traceDrawAttempts++,type,start,n,pipeline);
 tegu_render_trace_late_encoder("draw",traceID,-1,pipeline,draws,command->count,command->invalid);
 if(command->count>=TEGU_COMMAND_MAX_JOBS)tegu_command_capacity_marker("render",command->count);
 int sphereDraw=pipeline&&pipeline->sphereObserverOnly&&type<=4&&n&&n<=(1u<<24)&&start<=(1u<<24)-n;
 int fixedObserverDraw=pipeline&&pipeline->fixedUiObserverOnly&&type<=4&&n&&n<=(1u<<24)&&start<=(1u<<24)-n;
 int genericObserverDraw=pipeline&&pipeline->genericObserverOnly&&type<=4&&n&&n<=(1u<<24)&&start<=(1u<<24)-n;
 int fixedUiDraw=pipeline&&(pipeline->fixedUiSnapshotOnly||pipeline->fixedUiTextureSnapshotOnly||pipeline->genericDraw)&&
  ((type==3&&n>=3&&n<=384&&n%3==0)||
   /* Line lists (R153: fixed_frag type=1, 64..502 vertices) take the generic draw. */
   (pipeline->genericDraw&&(type==1||type==3||type==4)&&n>=(type==1?2u:3u)&&n<=TEGU_GENERIC_DRAW_MAX_VERTICES&&
    (type==4||n%(type==1?2u:3u)==0)))&&
  (!start||(pipeline->genericDraw&&start<=(1u<<24)));
 if(ended||[command teguCommitted]||!pipeline||(!sphereDraw&&!fixedObserverDraw&&!genericObserverDraw&&!fixedUiDraw&&(type!=3||start||n!=3))||command->count>=TEGU_COMMAND_MAX_JOBS||
    /* Generic draws are one TGJ1 job each: a dropped earlier draw (no class)
     * must not refuse them (R150: 6 fixed_frag platter draws). */
    (draws&&!pipeline->genericDraw&&((unsigned)!!(pipeline->compositorSnapshotOnly||pipeline->homeAffordanceObserverOnly||pipeline->fixedUiSnapshotOnly||pipeline->fixedUiTextureSnapshotOnly||pipeline->genericDraw||pipeline->fixedUiObserverOnly||pipeline->genericObserverOnly||pipeline->sphereObserverOnly)!=compositorDrawClass))||
    (draws&&!pipeline->compositorSnapshotOnly&&!pipeline->homeAffordanceObserverOnly&&!pipeline->fixedUiSnapshotOnly&&!pipeline->fixedUiTextureSnapshotOnly&&!pipeline->genericDraw&&!pipeline->fixedUiObserverOnly&&!pipeline->genericObserverOnly&&!pipeline->sphereObserverOnly)){
  static unsigned shapes;
  if(__atomic_fetch_add(&shapes,1,__ATOMIC_RELAXED)<16){char text[200];int k=snprintf(text,sizeof(text),
   "TEGU_DRAW_SHAPE type=%lu start=%lu count=%lu generic=%u ui=%u/%u jobs=%u draws=%u class=%u\n",(unsigned long)type,(unsigned long)start,(unsigned long)n,
   pipeline?pipeline->genericDraw:0,pipeline?pipeline->fixedUiSnapshotOnly:0,pipeline?pipeline->fixedUiTextureSnapshotOnly:0,command->count,draws,compositorDrawClass);
   if(k>0)write(2,text,(size_t)k<sizeof(text)?(size_t)k:sizeof(text)-1);}
  {tegu_render_draw_refused(self,__LINE__);return;}}
 if(!sphereDraw&&!fixedObserverDraw&&!genericObserverDraw&&!fixedUiDraw&&
    !(pipeline&&pipeline->homeAffordanceObserverOnly)&&!tegu_render_full_scissor(self)){
  if((command->queue->owner->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&(command->queue->owner->capabilities&TEGU_GPU_CAP_GENERIC_JOB)){
   tegu_render_trace_silent(self,"scissor",0,type,n);draws++;return;}
  {tegu_render_draw_refused(self,__LINE__);return;}
 }
 if(target->surfaceImported&&target->surfaceTargetVersion&&
    (target->surfaceTargetVersion!=1||(!pipeline->compositorSnapshotOnly&&!pipeline->attachmentClearSnapshotOnly&&!pipeline->homeAffordanceObserverOnly&&!pipeline->fixedUiSnapshotOnly&&!pipeline->fixedUiTextureSnapshotOnly&&!pipeline->genericDraw&&!pipeline->fixedUiObserverOnly&&!pipeline->genericObserverOnly)||
     !(command->queue->owner->capabilities&TEGU_GPU_CAP_COMPOSITOR_IOSURFACE)||target->format!=80)){
  {tegu_render_draw_refused(self,__LINE__);return;}
 }
 if(pipeline->attachmentClearSnapshotOnly) {
  if(attachmentClearElided) {
   /* TAC1 has no observable output when RT0's shader mask is zero and RT1 is
    * discarded. Validate the frontend contract, then encode no GPU work. */
   struct Dispatch checked={0};
   if(tegu_render_attachment_clear_snapshot(self,&checked)){{tegu_render_draw_refused(self,__LINE__);return;}}
   [checked.renderPipeline release];
   for(unsigned i=0;i<2;i++){[checked.attachmentClearTargets[i] release];[checked.attachmentClearBuffers[i] release];}
   draws++;compositorDrawClass=1;return;
  }
  struct Dispatch *job=tegu_command_job(command,command->count);
  if(tegu_render_attachment_clear_snapshot(self,job)){{tegu_render_draw_refused(self,__LINE__);return;}}
  command->count++;draws++;compositorDrawClass=1;return;
 }
 if(pipeline->sphereObserverOnly) {
  TeguAIRRenderDraw draw={1,(uint32_t)start,(uint32_t)n,1,0};
  if(tegu_render_sphere_observe(self,&draw)){{tegu_render_draw_refused(self,__LINE__);return;}}
  draws++;compositorDrawClass=1;return;
 }
 if(pipeline->fixedUiObserverOnly) {
  tegu_render_fixed_ui_observer(self,"nonindexed",type,start,n,1,0,0);
  draws++;compositorDrawClass=1;return;
 }
 if(pipeline->genericObserverOnly) {
  tegu_render_generic_observer(self,"nonindexed",type,start,n,1,0,0);
  draws++;compositorDrawClass=1;return;
 }
 /* A non-zero vertexStart or a line list goes to the generic draw. */
 if(pipeline->fixedUiSnapshotOnly&&!((start||type!=3)&&pipeline->genericDraw)) {
  TeguAIRRenderDraw draw={1,(uint32_t)start,(uint32_t)n,1,0};struct Dispatch *job=tegu_command_job(command,command->count);
  if(tegu_render_fixed_ui_snapshot(self,&draw,0,0,job)){{tegu_render_draw_refused(self,__LINE__);return;}}
  command->count++;draws++;compositorDrawClass=1;return;
 }
 if(pipeline->fixedUiTextureSnapshotOnly&&!((start||type!=3)&&pipeline->genericDraw)) {
  TeguAIRRenderDraw draw={1,(uint32_t)start,(uint32_t)n,1,0};struct Dispatch *job=tegu_command_job(command,command->count);
  if(tegu_render_fixed_ui_texture_snapshot(self,&draw,0,0,job)) {
   if(pipeline->genericDraw&&!tegu_render_generic_snapshot(self,&draw,0,0,job)){command->count++;draws++;compositorDrawClass=1;return;}
   {tegu_render_draw_refused(self,__LINE__);return;}
  }
  command->count++;draws++;compositorDrawClass=1;return;
 }
 if(pipeline->genericDraw) {
  /* A refused generic draw is skipped (and traced); the pass still renders. */
  TeguAIRRenderDraw draw={1,(uint32_t)start,(uint32_t)n,1,0};struct Dispatch *job=tegu_command_job(command,command->count);
  drawPrimitive=type==1?TEGU_GENERIC_DRAW_LINES:type==4?TEGU_GENERIC_DRAW_TRIANGLE_STRIP:TEGU_GENERIC_DRAW_TRIANGLES;
  int rc=tegu_render_generic_snapshot(self,&draw,0,0,job);
  if(!rc)command->count++;else tegu_render_trace_silent(self,"generic",rc,type,n);
  tegu_render_generic_mrt(self,&draw,0,0);
  drawPrimitive=TEGU_GENERIC_DRAW_TRIANGLES;
  draws++;compositorDrawClass=1;return;
 }
 if(pipeline->homeAffordanceObserverOnly) {
  TeguAIRRenderDraw draw={1,0,3,1,0};struct Dispatch observed={0};
  int snapshotError=tegu_render_compositor_snapshot(self,&draw,&observed);
  if(snapshotError){
   static unsigned refused;
   if(__atomic_fetch_add(&refused,1,__ATOMIC_RELAXED)<8) {
    char line[96];int m=snprintf(line,sizeof(line),"TEGU_RESOURCE_OBSERVER_REFUSED class=%u status=%d\n",
     pipeline->homeAffordanceObserverOnly,snapshotError);
    if(m>0)write(2,line,(size_t)m<sizeof(line)?(size_t)m:sizeof(line)-1);
   }
   {tegu_render_draw_refused(self,__LINE__);return;}
  }
  static unsigned emitted;
  if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)<8) {
   TeguTexture *sample=observed.compositorTextures[3];
   char line[256];int m=snprintf(line,sizeof(line),
    "%s resources=v%08x/f%08x/t%08x/s%08x attachments=%u target=%lux%lu sample=%lux%lu/f%lu/m%lu/u%lu/o%lx load=%u store=%u\n",
    pipeline->homeAffordanceObserverOnly==1?"TEGU_HOME_OBSERVER":"TEGU_NARROW_BLUR_OBSERVER",
    observed.compositorVertexBufferMask,observed.compositorFragmentBufferMask,observed.compositorTextureMask,
    observed.compositorSamplerMask,observed.compositorAttachmentCount,
    (unsigned long)target->textureWidth,(unsigned long)target->textureHeight,
    (unsigned long)(sample?sample->textureWidth:0),(unsigned long)(sample?sample->textureHeight:0),
    (unsigned long)(sample?sample->format:0),(unsigned long)(sample?sample->levelCount:0),
    (unsigned long)(sample?sample->textureUsage:0),(unsigned long)(sample?sample->textureResourceOptions:0),
    loadAction,storeAction);
   if(m>0)write(2,line,(size_t)m<sizeof(line)?(size_t)m:sizeof(line)-1);
  }
  tegu_compositor_job_release(&observed);draws++;compositorDrawClass=1;return;
 }
 /* Reject an observed B3 shape before retaining a dispatch.  Selector16 also
  * revalidates these objects at commit, but an inexact capture must never be
  * represented as a queued compositor job. */
 if(pipeline->compositorB3&&
    (!tegu_b3_private_mip0_texture(target,command->queue->owner)||
     !tegu_b3_private_mip0_texture(fragmentTextures[3],command->queue->owner))) {
  {tegu_render_draw_refused(self,__LINE__);return;}
 }
 if(pipeline->compositorSnapshotOnly) {
  const TeguAIRRenderBlend *b=&pipeline->blend;
  int sourceover=b->version==1&&b->enabled&&b->rgb_op==0&&b->rgb_src==1&&b->rgb_dst==5&&
   b->alpha_op==0&&b->alpha_src==1&&b->alpha_dst==5&&b->write_mask==15;
  if((pipeline->compositorB3&&(draws||(loadAction==1&&
      !(command->queue->owner->capabilities&TEGU_GPU_CAP_COMPOSITOR_LOAD))))||
     (!pipeline->compositorB3&&(draws||loadAction==1)&&
      (!(command->queue->owner->capabilities&TEGU_GPU_CAP_COMPOSITOR_LOAD)||!sourceover))){
   {tegu_render_draw_refused(self,__LINE__);return;}
  }
  TeguAIRRenderDraw draw={1,0,3,1,0};struct Dispatch *job=tegu_command_job(command,command->count);
  int snapshotError=tegu_render_compositor_snapshot(self,&draw,job);
  if(snapshotError){{tegu_render_draw_refused(self,__LINE__);return;}}
  command->count++;draws++;compositorDrawClass=1;return;
 }
 if(loadAction==1&&(!(command->queue->owner->capabilities&TEGU_GPU_CAP_LOAD_PASS)||target->surfaceImported||
    target->format!=70||target->textureWidth!=16||target->textureHeight!=16||target->rowBytes!=64)){
  {tegu_render_draw_refused(self,__LINE__);return;}
 }
 struct tegu_render_vertex_request request={0};
 if(pipeline->vertexFetch.count) {
  request.version=1;request.draw=(struct tegu_vertex_draw){0,3,1,0};
  for(unsigned i=0;i<pipeline->vertexFetch.count;i++) {
   const struct tegu_vertex_attribute *a=&pipeline->vertexFetch.attributes[i];uint64_t required;
   if(a->buffer_index>=31||tegu_vertex_attribute_end(a,&request.draw,&required)){{tegu_render_draw_refused(self,__LINE__);return;}}
   unsigned slot=a->buffer_index;TeguBuffer *b=vertexBuffers[slot];
   if(!b||b->owner!=command->queue->owner||(vertexOffsets[slot]&3)||vertexOffsets[slot]>b->logical||
      required>b->logical-vertexOffsets[slot]){{tegu_render_draw_refused(self,__LINE__);return;}}
   request.buffer_mask|=1u<<slot;
   request.buffers[slot].handle=b->handle;request.buffers[slot].offset=vertexOffsets[slot];
   if(required>request.buffers[slot].length)request.buffers[slot].length=required;
  }
 }
 struct tegu_render_texture_request textureRequest={0};
 if(pipeline->texture.version) {
  TeguTexture *sample=fragmentTextures[0];TeguSampler *sampler=fragmentSamplers[0];
  if(pipeline->texture.version!=1||pipeline->texture.stage!=16||pipeline->texture.texture_index||
     pipeline->texture.sampler_index||pipeline->texture.descriptor_set||pipeline->texture.binding||
     pipeline->texture.normalized_coordinates!=1||!sample||!sampler||
     !tegu_owned_class(sample,"TeguTexture")||!tegu_owned_class(sampler,"TeguSampler")||
     !tegu_sampler_legacy_ok(sampler)||[sample device]!=command->queue->owner||sampler->owner!=command->queue->owner||
     (sample->format!=70&&sample->format!=115)||sample->textureWidth!=16||sample->textureHeight!=16||sample->levelCount!=1||
     (sample->format==115&&(!(command->queue->owner->capabilities&2048)||sample->textureUsage!=1||target->format!=70))||
     (sample->format==70&&(sample->textureUsage!=0&&sample->textureUsage!=1))||sample->surfaceImported||
     (target->surfaceImported==0&&sample->buffer==target->buffer)||sample->mip[0].stride!=(sample->format==115?128:64)||
     sample->mip[0].offset>sample->buffer->logical||
     (sample->format==115?2048u:1024u)>sample->buffer->logical-sample->mip[0].offset||
     (sample->mip[0].offset&4095)||!sample->buffer->handle) {{tegu_render_draw_refused(self,__LINE__);return;}}
  textureRequest.vertex=request;textureRequest.version=sample->format==115?2:1;
  if(sample->format==115){textureRequest.reserved[0]=115;textureRequest.reserved[1]=128;textureRequest.reserved[2]=2048;}
  textureRequest.texture.handle=sample->buffer->handle;
  textureRequest.texture.offset=sample->mip[0].offset;
  textureRequest.texture.length=sample->format==115?2048:1024;
 }
 struct tegu_render_target_request targetRequest={0};
 if(loadAction==1) {
  if(request.version||textureRequest.version){{tegu_render_draw_refused(self,__LINE__);return;}}
  targetRequest.version=TEGU_RENDER_TARGET_REQUEST_VERSION_LOAD;
  targetRequest.reserved[0]=TEGU_RENDER_PASS_LOAD;
  targetRequest.width=16;targetRequest.height=16;targetRequest.row_stride=64;
 } else if(target->textureWidth!=16||target->textureHeight!=16) {
  if(!(command->queue->owner->capabilities&1024)){{tegu_render_draw_refused(self,__LINE__);return;}}
  NSUInteger width=target->textureWidth,height=target->textureHeight,stride=target->rowBytes;
  if(target->surfaceImported||width<16||height<16||width>256||height>256||
     (stride&63)||stride<width*4||stride>4096||
     stride>SIZE_MAX/height||stride*height>target->buffer->logical||
     stride*height>(1u<<20)) {{tegu_render_draw_refused(self,__LINE__);return;}}
  targetRequest.version=TEGU_RENDER_TARGET_REQUEST_VERSION;
  targetRequest.width=(uint32_t)width;targetRequest.height=(uint32_t)height;
  targetRequest.row_stride=(uint32_t)stride;
  if(textureRequest.version) {
   targetRequest.kind=TEGU_RENDER_TARGET_REQUEST_TEXTURE;
   memcpy(targetRequest.payload,&textureRequest,sizeof(textureRequest));
  } else if(request.version) {
   targetRequest.kind=TEGU_RENDER_TARGET_REQUEST_VERTEX;
   memcpy(targetRequest.payload,&request,sizeof(request));
  }
 }
 struct Dispatch *job=tegu_command_job(command,command->count);
 job->renderPipeline=[pipeline retain];job->renderTexture=[target retain];
 job->renderArgs[0]=pipeline->handle;job->renderArgs[1]=target->surfaceImported?target->importedHandle:target->buffer->handle;
 job->renderArgs[2]=0;job->renderArgs[3]=clear;
 job->vertexRequest=request;
 if(targetRequest.version)job->targetRequest=targetRequest;
 for(unsigned i=0;i<31;i++)if(request.buffer_mask&(1u<<i))job->vertexBuffers[i]=[vertexBuffers[i] retain];
 if(textureRequest.version) {
  job->textureRequest=textureRequest;
  job->sampledTexture=[fragmentTextures[0] retain];job->fragmentSampler=[fragmentSamplers[0] retain];
 }
 command->count++;draws++;
}
- (void)drawPrimitives:(NSUInteger)type vertexStart:(NSUInteger)start vertexCount:(NSUInteger)n
 instanceCount:(NSUInteger)instances {
 [self drawPrimitives:type vertexStart:start vertexCount:n instanceCount:instances baseInstance:0];
}
- (void)drawPrimitives:(NSUInteger)type vertexStart:(NSUInteger)start vertexCount:(NSUInteger)n
 instanceCount:(NSUInteger)instances baseInstance:(NSUInteger)baseInstance {
 if(inert)return;
 if(instances==1&&!baseInstance){[self drawPrimitives:type vertexStart:start vertexCount:n];return;}
 if(pipeline&&pipeline->inert){tegu_render_inert_refused(self);return;}
 if(!tegu_render_depth_stencil_exact(self)) {
  tegu_render_generic_refused(-14,"depth/stencil test or write has no arch9 generic job");
  return;
 }
 int valid=pipeline&&pipeline->sphereObserverOnly&&instances&&instances<=(1u<<24)&&
  type<=4&&n&&n<=(1u<<24)&&start<=(1u<<24)-n&&baseInstance<=(1u<<24)-instances&&
  !ended&&![command teguCommitted]&&command->count<TEGU_COMMAND_MAX_JOBS&&
  (!draws||compositorDrawClass==1)&&!target->surfaceImported;
 if(!valid){tegu_render_trace_draw_variant("nonindexed",self,-1,type,n,instances,0,baseInstance);
  tegu_render_trace_sphere_encoder("drawN",self,-1,type,start,n,instances,baseInstance);{tegu_render_draw_refused(self,__LINE__);return;}}
 TeguAIRRenderDraw draw={1,(uint32_t)start,(uint32_t)n,(uint32_t)instances,(uint32_t)baseInstance};
 int rc=tegu_render_sphere_observe(self,&draw);
 tegu_render_trace_draw_variant("nonindexed",self,rc,type,n,instances,0,baseInstance);
 tegu_render_trace_sphere_encoder("drawN",self,rc,type,start,n,instances,baseInstance);
 if(rc){{tegu_render_draw_refused(self,__LINE__);return;}}
 draws++;compositorDrawClass=1;
}
- (void)drawIndexedPrimitives:(NSUInteger)type indexCount:(NSUInteger)n indexType:(NSUInteger)indexType
 indexBuffer:(TeguBuffer *)indexBuffer indexBufferOffset:(NSUInteger)offset {
 [self drawIndexedPrimitives:type indexCount:n indexType:indexType indexBuffer:indexBuffer
  indexBufferOffset:offset instanceCount:1 baseVertex:0 baseInstance:0];
}
- (void)drawIndexedPrimitives:(NSUInteger)type indexCount:(NSUInteger)n indexType:(NSUInteger)indexType
 indexBuffer:(TeguBuffer *)indexBuffer indexBufferOffset:(NSUInteger)offset instanceCount:(NSUInteger)instances {
 [self drawIndexedPrimitives:type indexCount:n indexType:indexType indexBuffer:indexBuffer
  indexBufferOffset:offset instanceCount:instances baseVertex:0 baseInstance:0];
}
- (void)drawIndexedPrimitives:(NSUInteger)type indexCount:(NSUInteger)n indexType:(NSUInteger)indexType
 indexBuffer:(TeguBuffer *)indexBuffer indexBufferOffset:(NSUInteger)offset instanceCount:(NSUInteger)instances
 baseVertex:(long)baseVertex baseInstance:(NSUInteger)baseInstance {
 if(inert)return;
 if(skipPass||pipelineRefused||viewportRefused){draws++;return;}
 if(pipeline&&pipeline->inert){tegu_render_inert_refused(self);return;}
 if(!tegu_render_depth_stencil_exact(self)) {
  tegu_render_generic_refused(-14,"depth/stencil test or write has no arch9 generic job");
  return;
 }
 NSUInteger element=indexType==0?2:indexType==1?4:0;
 int common=!ended&&![command teguCommitted]&&pipeline&&indexBuffer&&
  tegu_owned_class(indexBuffer,"TeguBuffer")&&indexBuffer->owner==command->queue->owner&&element&&
  !(offset&(element-1))&&offset<=indexBuffer->logical&&n<=(indexBuffer->logical-offset)/element&&
  type<=4&&n&&n<=(1u<<24)&&instances&&instances<=(1u<<24)&&baseInstance<=(1u<<24)-instances&&
  command->count<TEGU_COMMAND_MAX_JOBS;
 if(common&&pipeline->fixedUiObserverOnly) {
  tegu_render_fixed_ui_observer(self,"indexed",type,0,n,instances,baseVertex,baseInstance);
  draws++;compositorDrawClass=1;return;
 }
 if(common&&pipeline->genericObserverOnly) {
  tegu_render_generic_observer(self,"indexed",type,0,n,instances,baseVertex,baseInstance);
  draws++;compositorDrawClass=1;return;
 }
 /* Vertex-pulling generic shaders take the index buffer natively (R162:
  * chronod's 4-index widget strips). */
 if(common&&pipeline->genericDraw&&!pipeline->fixedUiTextureSnapshotOnly&&!pipeline->genericShader.attribute_count&&
    (type==1||type==3||type==4)&&instances==1&&baseVertex>=0&&baseVertex<=0x7fffffffLL&&
    baseInstance<=0xffffffffULL&&n>=(type==1?2u:3u)&&
    n<=TEGU_GENERIC_DRAW_MAX_VERTICES&&(type==4||n%(type==1?2u:3u)==0)) {
  TeguAIRRenderDraw draw={1,0,(uint32_t)n,1,(uint32_t)baseInstance};struct Dispatch *job=tegu_command_job(command,command->count);
  drawPrimitive=type==1?TEGU_GENERIC_DRAW_LINES:type==4?TEGU_GENERIC_DRAW_TRIANGLE_STRIP:TEGU_GENERIC_DRAW_TRIANGLES;
  drawIndexBuffer=indexBuffer;drawIndexOffset=offset;drawIndexSize=(unsigned)element;
  drawIndexBaseVertex=(int32_t)baseVertex;
  int rc=tegu_render_generic_snapshot(self,&draw,0,0,job);
  if(!rc)command->count++;else tegu_render_trace_silent(self,"indexed_native",rc,type,n);
  tegu_render_generic_mrt(self,&draw,0,0);
  drawPrimitive=TEGU_GENERIC_DRAW_TRIANGLES;drawIndexBuffer=0;drawIndexOffset=0;drawIndexSize=0;drawIndexBaseVertex=0;
  draws++;compositorDrawClass=1;return;
 }
 /* Indices 0..n-1 (from baseVertex) are a plain draw: vertex-pulling
  * shaders and strips need no CPU expansion (R160: chronod widget quads). */
 if(common&&pipeline->genericDraw&&!pipeline->fixedUiTextureSnapshotOnly&&(type==1||type==3||type==4)&&
    instances==1&&!baseInstance&&baseVertex>=0&&n<=TEGU_GENERIC_DRAW_MAX_VERTICES) {
  const unsigned char *ix=(const unsigned char *)tegu_buffer_bytes(indexBuffer)+offset;int identity=1;
  for(NSUInteger i=0;identity&&i<n;i++)
   identity=(element==2?((const uint16_t *)ix)[i]:((const uint32_t *)ix)[i])==i;
  if(identity&&tegu_buffer_bytes(indexBuffer)) {
   [self drawPrimitives:type vertexStart:(NSUInteger)baseVertex vertexCount:n];return;
  }
 }
 int fixedUiIndexed=common&&(pipeline->fixedUiSnapshotOnly||pipeline->fixedUiTextureSnapshotOnly||pipeline->genericDraw)&&type==3&&
  n>=3&&n<=384&&n%3==0&&instances==1&&!baseInstance&&baseVertex>=0&&
  /* Generic draws are one TGJ1 job each; an earlier dropped draw (counted
   * without a class) must not drop them too (R141 763x413 platter quads). */
  (!draws||compositorDrawClass==1||pipeline->genericDraw);
 if(common&&!fixedUiIndexed&&!tegu_render_full_scissor(self)) {
  tegu_render_trace_draw_variant("indexed_scissor",self,-1,type,n,instances,baseVertex,baseInstance);
  /* R137: a partial-scissor draw outside the fixed-UI class invalidated the
   * frame (and the gesture bar with it).  Drop only the draw. */
  if((command->queue->owner->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&(command->queue->owner->capabilities&TEGU_GPU_CAP_GENERIC_JOB)){
   tegu_render_trace_silent(self,"indexed_scissor",0,type,n);draws++;return;}
  {tegu_render_draw_refused(self,__LINE__);return;}
 }
 if(fixedUiIndexed) {
  /* Expand with the pipeline's own vertex layout (compositor meshes are
   * not the fixed-UI slot-1/48-byte layout). */
  unsigned vslot=pipeline->genericDraw?pipeline->genericShader.attributes[0].buffer_index:1u;
  NSUInteger vstride=pipeline->genericDraw?pipeline->genericShader.attributes[0].stride:48u;
  if(vslot>=31||!vstride||vstride>256){tegu_render_draw_refused(self,__LINE__);return;}
  TeguBuffer *source=vertexBuffers[vslot];const unsigned char *indices=(const unsigned char *)tegu_buffer_bytes(indexBuffer);
  const unsigned char *vertices=source?(const unsigned char *)tegu_buffer_bytes(source):0;
  TeguBuffer *expanded=0;
  /* Why a draw cannot be expanded, reported as its rc: -11 no vertex buffer,
   * -12 another device's, -13 no bytes, -14 offset past the end, -15 no memory. */
  int why=!source?-11:source->owner!=command->queue->owner?-12:!indices||!vertices?-13:
   vertexOffsets[vslot]>source->logical?-14:0;
  int valid=!why;
  NSUInteger expandedOffset=0;
  if(valid)expanded=tegu_render_expand_alloc(self,n*vstride,&expandedOffset);
  unsigned char *out=expanded&&tegu_buffer_bytes(expanded)?(unsigned char *)tegu_buffer_bytes(expanded)+expandedOffset:0;
  if(valid&&!out){valid=0;why=-15;} /* allocation failure must not become a NULL memcpy */
  /* An index out of range here may be one a blit earlier in this command
   * buffer has not written yet (CoreAnimation fills private quad buffers that
   * way): zero that vertex now; tegu_dispatch_reexpand copies the real bytes
   * at execution. */
  for(NSUInteger i=0;valid&&i<n;i++) {
   uint64_t index=element==2?((const uint16_t *)(indices+offset))[i]:((const uint32_t *)(indices+offset))[i];
   uint64_t vertex=(uint64_t)baseVertex+index,byte=(uint64_t)vertexOffsets[vslot]+vertex*vstride;
   if(byte>source->logical||vstride>source->logical-byte)memset(out+i*vstride,0,vstride);
   else memcpy(out+i*vstride,vertices+byte,vstride);
  }
  TeguAIRRenderDraw draw={1,0,(uint32_t)n,1,0};struct Dispatch *job=tegu_command_job(command,command->count);
  int rc=why?why:-1;
  if(valid&&expanded) {
   if(pipeline->fixedUiTextureSnapshotOnly) {
    rc=tegu_render_fixed_ui_texture_snapshot(self,&draw,expanded,expandedOffset,job);
    if(rc&&pipeline->genericDraw)rc=tegu_render_generic_snapshot(self,&draw,expanded,expandedOffset,job);
   }
   else if(pipeline->genericDraw)rc=tegu_render_generic_snapshot(self,&draw,expanded,expandedOffset,job);
   else rc=tegu_render_fixed_ui_snapshot(self,&draw,expanded,expandedOffset,job);
  }
  TeguBuffer *expandedMrt=!rc&&expanded?[expanded retain]:0;
  if(!rc&&expanded) {
   [job->expandIndex release];[job->expandSource release];[job->expandOut release];
   job->expandIndex=[indexBuffer retain];job->expandSource=[source retain];job->expandOut=[expanded retain];
   job->expandIndexOffset=offset;job->expandSourceOffset=vertexOffsets[vslot];job->expandOutOffset=expandedOffset;job->expandElement=(uint32_t)element;
   job->expandStride=(uint32_t)vstride;job->expandCount=(uint32_t)n;job->expandBaseVertex=baseVertex;
  }
  [expanded release];
  tegu_render_trace_draw_variant("indexed_expand",self,rc,type,n,instances,baseVertex,baseInstance);
  if(rc&&((pipeline->genericDraw&&valid&&expanded)||
     ((command->queue->owner->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&(command->queue->owner->capabilities&TEGU_GPU_CAP_GENERIC_JOB)))){
   tegu_render_trace_silent(self,valid&&expanded?"indexed_expand":"indexed_expand_vertices",rc,type,n);
   draws++;compositorDrawClass=1;return;
  }
  if(rc){{tegu_render_draw_refused(self,__LINE__);return;}}
  command->count++;
  if(pipeline->genericDraw&&!pipeline->fixedUiTextureSnapshotOnly)tegu_render_generic_mrt(self,&draw,expandedMrt,expandedOffset);
  [expandedMrt release];
  draws++;compositorDrawClass=1;return;
 }
 /* The current native ABI has no index-buffer descriptor.  Accept only the
  * identity three-index case, which is exactly equivalent to the existing
  * non-indexed triangle and therefore preserves vertex-ID semantics. */
 int identity=0;
 if(common&&n==3&&baseVertex>=0&&(NSUInteger)baseVertex<=(1u<<24)-3) {
  const unsigned char *contents=(const unsigned char *)tegu_buffer_bytes(indexBuffer);
  if(contents) {
   if(element==2) {uint16_t values[3];memcpy(values,contents+offset,sizeof(values));
    identity=values[0]==0&&values[1]==1&&values[2]==2;}
   else {uint32_t values[3];memcpy(values,contents+offset,sizeof(values));
    identity=values[0]==0&&values[1]==1&&values[2]==2;}
  }
 }
 if(!common||!identity||instances!=1||baseInstance) {
  tegu_render_trace_draw_variant("indexed",self,-1,type,n,instances,baseVertex,baseInstance);
  /* R135: unsupported indexed shapes invalidated whole frames.  Drop the
   * draw on TGJ1 compositor devices; the rest of the frame renders. */
  if(!ended&&(command->queue->owner->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&
     (command->queue->owner->capabilities&TEGU_GPU_CAP_GENERIC_JOB)) {
   static unsigned dropped;
   if(__atomic_fetch_add(&dropped,1,__ATOMIC_RELAXED)<8){char line[160];int k=snprintf(line,sizeof(line),
    "TEGU_DRAW_DROPPED indexed type=%lu count=%lu instances=%lu fs=%s\n",(unsigned long)type,(unsigned long)n,
    (unsigned long)instances,pipeline?pipeline->genericName:"");if(k>0)write(2,line,(size_t)k<sizeof(line)?(size_t)k:sizeof(line)-1);}
   draws++;return;
  }
  {tegu_render_draw_refused(self,__LINE__);return;}
 }
 tegu_render_trace_draw_variant("indexed_identity",self,0,type,n,instances,baseVertex,baseInstance);
 [self drawPrimitives:type vertexStart:(NSUInteger)baseVertex vertexCount:3];
}
static void tegu_render_tile_refused(TeguRenderEncoder *e,const char *selector) {
 if(!e||!e->command)return;
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)<32) {
  char line[180];int n=snprintf(line,sizeof(line),"TEGU_TILE_REFUSED selector=%s reason=no_tile_pipeline\n",
   selector?selector:"?");
  if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
 }
 TEGU_INVALID(e->command);
}
- (void)dispatchThreadsPerTile:(MTLSize)threadsPerTile {
 (void)threadsPerTile;
 tegu_render_tile_refused(self,"dispatchThreadsPerTile:");
}
- (void)setTileBytes:(const void *)bytes length:(NSUInteger)length atIndex:(NSUInteger)index {
 (void)bytes;(void)length;(void)index;
 tegu_render_tile_refused(self,"setTileBytes:length:atIndex:");
}
- (void)setTileTexture:(TeguTexture *)texture atIndex:(NSUInteger)index {
 (void)texture;(void)index;
 tegu_render_tile_refused(self,"setTileTexture:atIndex:");
}
- (void)endEncoding {
 if(inert){ended=1;command->encoding=0;return;}
 const unsigned perfJobs=command->count-passJobStart;
 if(pipeline&&pipeline->compositorB3)
  tegu_render_trace_b3_encoder("end",traceID,!(ended||[command teguCommitted]),
   (unsigned)twoTargetPass,draws,command->count,command->invalid);
 tegu_render_trace_late_encoder("end",traceID,!(ended||[command teguCommitted]),
  pipeline,draws,command->count,command->invalid);
 /* An idle sphere pass is still enough to validate the resources retained by
  * the real encoder.  Use a diagnostic draw only for planning; do not queue a
  * GPU job or suppress the existing clear/store behavior. */
 if(!ended&&![command teguCommitted]&&!draws&&pipeline&&pipeline->sphereObserverOnly) {
  TeguAIRRenderDraw diagnostic={1,0,3,1,0};int rc=tegu_render_sphere_observe(self,&diagnostic);
  tegu_render_trace_sphere_encoder("idlePlan",self,rc,3,0,3,1,0);
  (void)rc;
 }
 /* Metal permits an encoder to end without recording a draw.  BackBoard uses
  * this for load/store-only passes while assembling its compositor command
  * buffer, so poisoning the whole command here also discards later draws. */
 if(ended||[command teguCommitted])TEGU_INVALID(command);
 else if(r8ClearOnly&&!draws) {
  NSUInteger bytes=target->rowBytes*target->textureHeight;
  if(!target->buffer||target->mip[0].offset>target->buffer->logical||
     bytes>target->buffer->logical-target->mip[0].offset||command->count>=TEGU_COMMAND_MAX_JOBS) {
   if(command->count>=TEGU_COMMAND_MAX_JOBS)tegu_command_capacity_marker("r8-clear",command->count);
   TEGU_INVALID(command);
  } else {
   struct Dispatch *job=tegu_command_job(command,command->count++);
   job->blit=2;job->buffers[1]=[target->buffer retain];
   job->destinationOffset=target->mip[0].offset;job->byteCount=bytes;
   job->fillValue=(unsigned char)clear;
  }
 }
 else if(memorylessResolveOnly&&!draws&&loadAction==2) {
  NSUInteger offset=0;
  if(!resolveTarget||!tegu_texture_level_rows(resolveTarget,0,0,0,resolveTarget->mip[0].width,
     resolveTarget->mip[0].height,&offset)||command->count>=TEGU_COMMAND_MAX_JOBS) {
   if(command->count>=TEGU_COMMAND_MAX_JOBS)tegu_command_capacity_marker("resolve-clear",command->count);
   TEGU_INVALID(command);
  } else {
   struct Dispatch *job=tegu_command_job(command,command->count++);
   job->blit=5;job->buffers[1]=[resolveTarget->buffer retain];
   job->destinationOffset=offset;
   job->destinationStride=resolveTarget->mip[0].stride;
   job->bytesPerPixel=4;job->byteCount=resolveTarget->mip[0].width*4;job->rows=resolveTarget->mip[0].height;
   job->fillBytes[0]=(unsigned char)(clear>>16);job->fillBytes[1]=(unsigned char)(clear>>8);
   job->fillBytes[2]=(unsigned char)clear;job->fillBytes[3]=(unsigned char)(clear>>24);
  }
 }
 /* R173: a pass whose draws all vanished (always on, capped). */
 if(!ended&&draws&&!skipPass&&command->count==passJobStart) {
  static unsigned emitted;
  if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)<24){char text[200];int k=snprintf(text,sizeof(text),
   "TEGU_PASS_NO_JOBS draws=%u target=%lux%lu imp=%u load=%u fs=%s\n",draws,
   target?(unsigned long)target->textureWidth:0,target?(unsigned long)target->textureHeight:0,
   target?(unsigned)target->surfaceImported:0,loadAction,pipeline?pipeline->genericName:"");
   if(k>0)write(2,text,(size_t)k<sizeof(text)?(size_t)k:sizeof(text)-1);}
 }
#ifdef NORD2_JM_TRANSPORT
 if(!ended&&![command teguCommitted]&&!skipPass){
  for(unsigned rt=0;rt<4;rt++){
   TeguTexture *t=tegu_render_target_for_rt(self,rt);unsigned jobs=tegu_render_jobs_for_rt(self,rt),action=tegu_render_load_for_rt(self,rt);
   if(!t||t->textureSamples!=1||jobs||action!=2||(preserveTargets&(1u<<rt)))continue;
   if(command->count>=TEGU_COMMAND_MAX_JOBS){TEGU_INVALID(command);break;}
   struct Dispatch *j=tegu_command_job(command,command->count++);j->nord2ClearOnly=1;j->renderTexture=[t retain];
   struct tegu_generic_draw_request *q=&j->genericDrawRequest;
   q->target_format=t->format;q->width=t->textureWidth;q->height=t->textureHeight;q->row_bytes=t->rowBytes;
   q->target_token=t->surfaceImported?t->importedHandle:t->buffer->handle;
   q->target_length=t->surfaceImported?t->surfaceAllocSize:t->buffer->logical;q->target_offset=t->mip[0].offset;
   memcpy(q->clear_words,renderClears[rt],sizeof(q->clear_words));
  }
 }
#endif
 /* R159: a clear whose pass queued no job (no draws, or all dropped) still
  * clears; chronod's widget passes are clear-then-draw into owned targets. */
 if(!ended&&![command teguCommitted]&&!r8ClearOnly&&!memorylessResolveOnly&&!skipPass&&loadAction==2&&
    target&&target->textureSamples==1&&!(preserveTargets&1u)&&
    command->count==passJobStart&&command->count<TEGU_COMMAND_MAX_JOBS&&target&&!target->surfaceImported&&
    target->buffer&&(target->format==80||target->format==70)&&target->levelCount>=1&&
    target->mip[0].offset+(target->textureHeight-1)*target->rowBytes+target->textureWidth*4u<=target->buffer->logical&&
    (command->queue->owner->capabilities&TEGU_GPU_CAP_GENERIC_JOB)) {
  struct Dispatch *job=tegu_command_job(command,command->count++);
  job->blit=5;job->buffers[1]=[target->buffer retain];
  job->destinationOffset=target->mip[0].offset;job->destinationStride=target->rowBytes;
  job->bytesPerPixel=4;job->byteCount=target->textureWidth*4;job->rows=target->textureHeight;
  int bgra=target->format==80;
  job->fillBytes[0]=(unsigned char)(bgra?clear>>16:clear);job->fillBytes[1]=(unsigned char)(clear>>8);
  job->fillBytes[2]=(unsigned char)(bgra?clear:clear>>16);job->fillBytes[3]=(unsigned char)(clear>>24);
 }
 /* TEGU_PERF_BATCH: count the encoder and stamp its jobs with its number,
  * so execution can tell a split inside one encoder from a new encoder. */
 if(!ended&&![command teguCommitted]) {
  struct tegu_perf_batch *pb=&command->perfBatch;unsigned pass=++pb->encoders;
  for(unsigned k=passJobStart;k<command->count;k++)TEGU_JOB(command->jobChunks,k).perfPass=pass;
  if(target&&(U64)target->textureWidth*target->textureHeight>=TEGU_PERF_FULL_PIXELS)pb->encFull++;else pb->encOff++;
  if(skipPass)pb->encSkip++;else if(!perfJobs)pb->encEmpty++;
  if(perfDontCare)pb->encDontCare++;else if(loadAction==1)pb->encLoad++;else pb->encClear++;
 }
 ended=1;command->encoding=0;
}
- (void)dealloc{if(!ended&&!inert)TEGU_INVALID(command);[depthState release];[expandArena release];for(unsigned i=0;i<31;i++){[vertexBuffers[i] release];[vertexTextures[i] release];}
 for(unsigned i=0;i<31;i++){[fragmentBuffers[i] release];[fragmentTextures[i] release];}
 for(unsigned i=0;i<16;i++){[vertexSamplers[i] release];[fragmentSamplers[i] release];}
 [nord2MrtSnapshots[0] release];[nord2MrtSnapshots[1] release];[nord2MrtSnapshots[2] release];[nord2MrtSnapshots[3] release];
 [attachmentClearTargets[1] release];[resolveTarget release];[mrtTarget release];[mrtTarget2 release];[mrtTarget3 release];
 for(unsigned i=0;i<4;i++)[resolveTargets[i] release];
 [debugLabel release];[pipeline release];[target release];[command release];[super dealloc];}
@end
/* A pass the native encoders cannot express (partial render area, extra or
 * memoryless targets, ...) used to invalidate its whole command buffer, so
 * one odd offscreen pass dropped the entire frame (R125: 75 invalid frames).
 * On TGJ1 compositor devices it becomes a skip pass: draws are dropped and
 * the command buffer's other passes still execute. */
static TeguRenderEncoder *tegu_render_skip_pass(TeguCommand *c,TeguTexture *t,int line) {
#ifdef NORD2_JM_TRANSPORT
 TEGU_INVALID(c);return 0;
#endif
 TeguComputeDevice *owner=c->queue->owner;
 if(!t||!(owner->capabilities&TEGU_GPU_CAP_COMPOSITOR)||!(owner->capabilities&TEGU_GPU_CAP_GENERIC_JOB)) {
  TEGU_INVALID(c);return 0;
 }
 TeguRenderEncoder *e=[[TeguRenderEncoder alloc] initWithCommand:c target:t clear:0 loadAction:1];
 if(!e){TEGU_INVALID(c);return 0;}
 e->storeAction=1;e->skipPass=1;
 {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)<16) {
  char text[160];int n=snprintf(text,sizeof(text),"TEGU_PASS_SKIPPED line=%d target=%lux%lu f%lu\n",line,
   (unsigned long)t->textureWidth,(unsigned long)t->textureHeight,(unsigned long)t->format);
  if(n>0)write(2,text,(size_t)n<sizeof(text)?(size_t)n:sizeof(text)-1);
 }
 }
 c->encoding=1;return [e autorelease];
}
@implementation TeguCommand (RenderCommands)
static int tegu_render_pack_clear(TeguClearColor value,uint32_t *packed) {
 if(!packed)return -1;double components[4]={value.red,value.green,value.blue,value.alpha};uint32_t out=0;
 for(unsigned i=0;i<4;i++) {
  double c=components[i];if(c!=c)return -1;
  if(c<0)c=0;if(c>1)c=1;out|=(uint32_t)(c*255.0+0.5)<<(8*i);
 }
 *packed=out;return 0;
}
#ifdef NORD2_JM_TRANSPORT
#include "../nord2_render_pass.inc"
#endif
- (id)renderCommandEncoderWithDescriptor:(TeguPassDescriptor *)d {
 if([self teguCommitted]||encoding||!d){
  TEGU_INVALID(self);
#ifdef TEGU_INERT_PIPELINES
  if(d&&(encoding||[self teguCommitted])&&tegu_render_inert_pipeline_allowed())
   return tegu_render_inert_encoder(self,encoding?"command already encoding":"command already committed");
#endif
  return 0;
 }
#ifdef NORD2_JM_TRANSPORT
 return nord2_render_pass(self,d);
#endif
 id colors=d?[d colorAttachments]:0;TeguPassAttachment *color=[colors objectAtIndexedSubscript:0];
 TeguTexture *t=[color texture];
 TeguTexture *resolve=[color resolveTexture];
 NSUInteger passLoad=[color loadAction],passStore=[color storeAction];
 unsigned traceID=tegu_render_trace_pass(t,passLoad,passStore);
 unsigned captureID=TEGU_RENDER_TRACE_NO_ID;int capture=0;
#ifdef TEGU_RENDER_TRACE
 if(traceID==TEGU_RENDER_TRACE_NO_ID&&tegu_render_trace_has_rt1(colors)) {
  captureID=tegu_render_trace_claim(&tegu_render_trace_rt1_captures,16);
  if(captureID!=TEGU_RENDER_TRACE_NO_ID) {
   capture=1;char line[96];int n=snprintf(line,sizeof(line),"TEGU_UI_RT1_CAPTURE id=%u phase=descriptor\n",captureID);
   if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
  }
 }
#else
 (void)captureID;(void)capture;
#endif
 tegu_render_trace_pass_details(capture?captureID:traceID,capture,d,colors);
 int targetLayoutOK=0;
 if(t) {
  NSUInteger width=t->textureWidth,height=t->textureHeight,stride=t->rowBytes;
  targetLayoutOK=width>=16&&height>=16&&width<=256&&height<=256&&
   (stride&63)==0&&stride>=width*4&&stride<=4096&&stride<=SIZE_MAX/height&&
   stride*height<=(1u<<20);
  if(!t->surfaceImported&&(queue->owner->capabilities&TEGU_GPU_CAP_COMPOSITOR))
   targetLayoutOK=width>=16&&height>=16&&width<=4096&&height<=4096&&
    !(stride&63)&&stride>=width*4&&stride<=65536&&stride*height<=(16u<<20);
  if(t->surfaceImported) {
   if(t->surfaceTargetVersion==1)
    targetLayoutOK=width>=16&&height>=16&&width<=4096&&height<=4096&&
     !(stride&63)&&stride>=width*4&&stride<=65536&&stride<=SIZE_MAX/height&&
     stride*height<=(16u<<20);
   targetLayoutOK=targetLayoutOK&&((t->surfaceTargetVersion==1&&
    (queue->owner->capabilities&TEGU_GPU_CAP_COMPOSITOR_IOSURFACE)&&t->format==80&&
    tegu_iosurface_render_target_usage(t->textureUsage)&&width<=4096&&height<=4096&&stride<=65536&&
    t->mip[0].offset<=t->surfaceAllocSize&&
    stride*height<=t->surfaceAllocSize-t->mip[0].offset&&t->surfaceRowBytes==stride) ||
    (!t->surfaceTargetVersion&&width==16&&height==16));
  } else targetLayoutOK=targetLayoutOK&&t->buffer&&stride*height<=t->buffer->logical;
 }
 TeguPassAttachment *color1=[colors objectAtIndexedSubscript:1];TeguTexture *t1=[color1 texture];
 /* A memoryless second attachment cannot outlive the pass: ignore it so the
  * pass stays single-target (R130 regression once memoryless was admitted;
  * Snapintosh likewise leaves CoreAnimation's color(1) unwritten). */
 if(t1&&tegu_owned_class(t1,"TeguTexture")&&[color1 storeAction]!=1&&
    (queue->owner->capabilities&TEGU_GPU_CAP_GENERIC_JOB)&&
    (t1->textureResourceOptions==0x30||!tegu_b3_private_mip0_texture(t1,queue->owner)))t1=0;
 int ignoredRT1=[color1 texture]&&!t1;
 NSUInteger load1=t1?[color1 loadAction]:0,store1=t1?[color1 storeAction]:0;
 int twoTargetPass=t1!=0;
 NSUInteger defaultSamples=[d defaultRasterSampleCount];
 /* R159: chronod's widget passes carry a memoryless depth/stencil buffer.
  * Generic draws ignore it (no depth test, no stencil clip); a memoryless
  * attachment holds nothing that outlives the pass. */
 TeguTexture *depthTexture=[[d depthAttachment] texture],*stencilTexture=[[d stencilAttachment] texture];
 int ignorableAux=(queue->owner->capabilities&TEGU_GPU_CAP_GENERIC_JOB)&&
   (!depthTexture||(tegu_owned_class(depthTexture,"TeguTexture")&&depthTexture->textureResourceOptions==0x30))&&
   (!stencilTexture||(tegu_owned_class(stencilTexture,"TeguTexture")&&stencilTexture->textureResourceOptions==0x30));
 int auxiliaryInvalid=((depthTexture||stencilTexture)&&!ignorableAux)||[d renderTargetArrayLength]>1||
    /* A render area inside the texture renders with the scissor clamped to
     * it (R127: 57x57 icon passes in 64x64 textures). */
    ([d renderTargetWidth]&&([d renderTargetWidth]>[t width]||(twoTargetPass&&[d renderTargetWidth]!=[t width])))||
    ([d renderTargetHeight]&&([d renderTargetHeight]>[t height]||(twoTargetPass&&[d renderTargetHeight]!=[t height])))||
    (defaultSamples>1&&(!t||defaultSamples!=t->textureSamples))||[d visibilityResultBuffer];
 int memorylessResolve=!twoTargetPass&&!auxiliaryInvalid&&(passLoad==0||passLoad==2)&&passStore==2&&
    (defaultSamples==0||defaultSamples==4)&&tegu_render_resolve_subresources_zero(color)&&
    tegu_memoryless_msaa_resolve_texture_pair(t,resolve,queue->owner);
 int r8ClearOnly=!twoTargetPass&&!auxiliaryInvalid&&passLoad==2&&passStore==1&&
    tegu_r58_private_allocation_only(t,queue->owner)&&t->format==10&&
    !resolve&&![color level]&&![color slice]&&![color depthPlane];
 int commonInvalid=!t||[t device]!=queue->owner||
    tegu_r58_private_allocation_only(t,queue->owner)||auxiliaryInvalid;
 int exactObserverTarget=t&&(tegu_b3_private_mip0_texture(t,queue->owner)||
    tegu_rgba16float_private_observer_texture(t,queue->owner));
 int b3TwoTargetCandidate=twoTargetPass&&!commonInvalid&&t1!=t&&
    (queue->owner->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&
    tegu_b3_private_mip0_texture(t,queue->owner)&&tegu_b3_private_mip0_texture(t1,queue->owner)&&
    passLoad<=2&&passStore==1&&!load1&&!store1&&
    ![color resolveTexture]&&![color level]&&![color slice]&&![color depthPlane]&&
    ![color1 resolveTexture]&&![color1 level]&&![color1 slice]&&![color1 depthPlane];
 int observerPassCandidate=memorylessResolve||(!twoTargetPass&&!commonInvalid&&
    (queue->owner->capabilities&TEGU_GPU_CAP_COMPOSITOR)&&!t->surfaceImported&&t->buffer&&
    exactObserverTarget&&passLoad<=2&&passStore==1&&
    ![color resolveTexture]&&![color level]&&![color slice]&&![color depthPlane])||b3TwoTargetCandidate;
 if(twoTargetPass) {
  if(commonInvalid||!tegu_owned_class(t1,"TeguTexture")||[t1 device]!=queue->owner||
     [color resolveTexture]||[color level]||[color slice]||[color depthPlane]||
     [color1 resolveTexture]||[color1 level]||[color1 slice]||[color1 depthPlane]){
    if([color1 resolveTexture])tegu_render_trace_resolve_refused(capture?captureID:traceID,1,capture);
    return tegu_render_skip_pass(self,t,__LINE__);
   }
 }
 /* R168: home-screen platter passes use loadAction DontCare; the contents
  * are undefined, so on generic-job devices they start cleared to 0. */
 int dontCareClear=!twoTargetPass&&!r8ClearOnly&&!observerPassCandidate&&!memorylessResolve&&passLoad==0&&
   (queue->owner->capabilities&TEGU_GPU_CAP_GENERIC_JOB);
 if(dontCareClear)passLoad=2;
 if(twoTargetPass) {
 } else if(!r8ClearOnly&&!observerPassCandidate&&((passLoad!=1&&passLoad!=2)||(passLoad==1&&!(queue->owner->capabilities&(TEGU_GPU_CAP_LOAD_PASS|TEGU_GPU_CAP_COMPOSITOR_LOAD)))||
    commonInvalid||([t pixelFormat]!=70&&[t pixelFormat]!=80)||
    (passLoad==1&&!((queue->owner->capabilities&TEGU_GPU_CAP_LOAD_PASS)&&!t->surfaceImported&&t->format==70&&t->textureWidth==16&&t->textureHeight==16&&t->rowBytes==64)&&
     !((queue->owner->capabilities&TEGU_GPU_CAP_COMPOSITOR_LOAD)&&t->format==80&&targetLayoutOK))||
    !targetLayoutOK||([t usage]&&!([t usage]&4))||
    [color resolveTexture]||[color level]||[color slice]||[color depthPlane]||passStore!=1)){return tegu_render_skip_pass(self,t,__LINE__);}
 for(unsigned i=twoTargetPass?2:1;i<8;i++)if(!(i==1&&ignoredRT1)&&[(TeguPassAttachment *)[colors objectAtIndexedSubscript:i] texture]){
  if(capture)tegu_render_trace_rt1_refused(captureID,i);else tegu_render_trace_extra_target(traceID,i);
  return tegu_render_skip_pass(self,t,__LINE__);
 }
 uint32_t packed=0,packed1=0;
 if(passLoad==2&&!dontCareClear&&tegu_render_pack_clear([color clearColor],&packed)){return tegu_render_skip_pass(self,t,__LINE__);}
 if(twoTargetPass&&load1==2&&tegu_render_pack_clear([color1 clearColor],&packed1)){return tegu_render_skip_pass(self,t,__LINE__);}
 TeguRenderEncoder *e=[[TeguRenderEncoder alloc] initWithCommand:self target:t clear:packed loadAction:(unsigned)passLoad];
 if(!e){TEGU_INVALID(self);return 0;}e->storeAction=(unsigned)passStore;e->twoTargetPass=twoTargetPass;
 e->observerPassOnly=(unsigned)observerPassCandidate;e->perfDontCare=(unsigned)dontCareClear;
 if([d renderTargetWidth]&&[d renderTargetWidth]<e->scissor.width)e->scissor.width=[d renderTargetWidth];
 if([d renderTargetHeight]&&[d renderTargetHeight]<e->scissor.height)e->scissor.height=[d renderTargetHeight];
 e->memorylessResolveOnly=(unsigned)memorylessResolve;
 e->r8ClearOnly=(unsigned)r8ClearOnly;
 if(memorylessResolve)e->resolveTarget=[resolve retain];
 if(ignoredRT1&&(queue->owner->capabilities&TEGU_GPU_CAP_GENERIC_JOB)) {
  TeguTexture *m=[color1 texture];uint32_t mclear=0;
  if(tegu_owned_class(m,"TeguTexture")&&[m device]==queue->owner&&m!=t&&m->format==80&&m->buffer&&!m->surfaceImported&&
     m->textureWidth==t->textureWidth&&m->textureHeight==t->textureHeight&&m->levelCount==1&&
     m->rowBytes>=m->textureWidth*4u&&!(m->rowBytes&63)&&!(m->mip[0].offset&4095)&&
     ([color1 loadAction]!=2||!tegu_render_pack_clear([color1 clearColor],&mclear))) {
   e->mrtTarget=[m retain];e->mrtLoad=(unsigned)[color1 loadAction];e->mrtClear=mclear;
  }
 }
 if(twoTargetPass) {
  e->attachmentClearTargets[0]=t;e->attachmentClearTargets[1]=[t1 retain];
  e->attachmentClearLoads[0]=(unsigned)passLoad;e->attachmentClearLoads[1]=(unsigned)load1;
  e->attachmentClearStores[0]=(unsigned)passStore;e->attachmentClearStores[1]=(unsigned)store1;
  e->attachmentClearColors[0]=packed;e->attachmentClearColors[1]=packed1;
 }
#ifdef TEGU_RENDER_TRACE
 e->traceID=traceID;
#else
 (void)traceID;
#endif
 if(count>=TEGU_COMMAND_MAX_JOBS)tegu_command_capacity_marker("render-begin",count);
 tegu_render_trace_late_encoder("begin",traceID,!invalid,0,0,count,invalid);
 tegu_render_trace_pass_accepted(traceID);encoding=1;return [e autorelease];
}
@end
