
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern id class_createInstance(Class,size_t);
extern Class objc_getClass(const char*);
extern id object_dispose(id);
#define NORD2_JM_TRANSPORT 1
#define TEGU_IOSURFACE_BGRA 0x42475241u
#define TEGU_IOSURFACE_RGBA 0x52474241u
#define MARK(s) do{(void)(s);}while(0)
typedef unsigned long NSUInteger;
typedef struct{NSUInteger x,y,z;} Origin;
typedef struct{NSUInteger width,height,depth;} Size;
typedef struct{Origin origin;Size size;} TeguRegion;
struct tegu_texture_level{NSUInteger width,height,stride,offset;};
struct Buffer{NSUInteger logical;uint8_t *data;};
static void *tegu_buffer_bytes(struct Buffer *b){return b->data;}
static NSUInteger tegu_texture_bytes_per_pixel(NSUInteger f){assert(f==80);return 4;}
static int tegu_texture_is_astc(NSUInteger f){(void)f;return 0;}
static void tegu_texture_block_shape(NSUInteger f,unsigned *w,unsigned *h,unsigned *b){(void)f;(void)w;(void)h;(void)b;abort();}
struct Surface{uint8_t data[16384];NSUInteger w,h,row,alloc;unsigned fourcc,planes,failLock,locks,unlocks;};
static size_t width(void *p){return ((struct Surface*)p)->w;}
static size_t height(void *p){return ((struct Surface*)p)->h;}
static size_t row(void *p){return ((struct Surface*)p)->row;}
static size_t alloc(void *p){return ((struct Surface*)p)->alloc;}
static unsigned fourcc(void *p){return ((struct Surface*)p)->fourcc;}
static size_t planes(void *p){return ((struct Surface*)p)->planes;}
static int lock(void *p,unsigned o,unsigned *seed){(void)o;(void)seed;struct Surface *s=p;s->locks++;return s->failLock?-1:0;}
static int unlock(void *p,unsigned o,unsigned *seed){(void)o;(void)seed;((struct Surface*)p)->unlocks++;return 0;}
static void *base(void *p){return ((struct Surface*)p)->data;}
struct TeguIOSurfaceAPI{size_t(*getWidth)(void*),(*getHeight)(void*),(*getBytesPerRow)(void*),(*getAllocSize)(void*);unsigned(*getPixelFormat)(void*);size_t(*getPlaneCount)(void*);int(*lock)(void*,unsigned,unsigned*),(*unlock)(void*,unsigned,unsigned*);void*(*getBaseAddress)(void*);void*(*getPlaneBase)(void*,size_t);size_t(*getPlaneRow)(void*,size_t),(*getPlaneWidth)(void*,size_t),(*getPlaneHeight)(void*,size_t);};
static const struct TeguIOSurfaceAPI *tegu_iosurface_api(void){static const struct TeguIOSurfaceAPI api={width,height,row,alloc,fourcc,planes,lock,unlock,base,0,0,0,0};return &api;}
__attribute__((objc_root_class)) @interface TeguTexture {
@public Class isa;NSUInteger textureResourceOptions,surfaceImported,surfaceTargetVersion,levelCount,textureKind,textureDepth,format,surfaceRowBytes,surfaceAllocSize,textureWidth,textureHeight;
struct tegu_texture_level mip[1];NSUInteger layers,levelLayerStride[1];struct Buffer *buffer;void *importedSurface;
void *shadowSurface;unsigned shadowConvert,shadowSeedValid;NSUInteger shadowPlane;
}
- (void)getBytes:(void*)b bytesPerRow:(NSUInteger)s bytesPerImage:(NSUInteger)i fromRegion:(TeguRegion)r mipmapLevel:(NSUInteger)l slice:(NSUInteger)x;
- (void)replaceRegion:(TeguRegion)r mipmapLevel:(NSUInteger)l slice:(NSUInteger)x withBytes:(const void*)b bytesPerRow:(NSUInteger)s bytesPerImage:(NSUInteger)i;
@end
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
/* CPU access to a decode shadow (shadowConvert) goes to its IOSurface, whose
 * bytes are the Metal format's; the owned RGBA16F backing is GPU-only data.
 * Writes invalidate the shadow so the next command re-decodes it. */
static unsigned tegu_xr_source_bpp_of(unsigned kind);
/* Base/row/width/height of the bytes a shadow mirrors: the whole surface, or
 * plane shadowPlane-1. Callers hold the surface lock for `base`. */
static int tegu_shadow_geometry(const struct TeguIOSurfaceAPI *api,void *surface,NSUInteger shadowPlane,
 unsigned char **base,size_t *row,size_t *width,size_t *height,size_t *alloc) {
 if(!api||!surface)return 0;
 if(!shadowPlane) {
  *row=api->getBytesPerRow(surface);*width=api->getWidth(surface);*height=api->getHeight(surface);
  *alloc=api->getAllocSize(surface);if(base)*base=(unsigned char *)api->getBaseAddress(surface);return 1;
 }
 size_t plane=shadowPlane-1;
 if(!api->getPlaneBase||!api->getPlaneRow||!api->getPlaneWidth||!api->getPlaneHeight||plane>=api->getPlaneCount(surface))return 0;
 *row=api->getPlaneRow(surface,plane);*width=api->getPlaneWidth(surface,plane);*height=api->getPlaneHeight(surface,plane);
 *alloc=*row*(*height);if(base)*base=(unsigned char *)api->getPlaneBase(surface,plane);return 1;
}
static int tegu_shadow_convert_cpu(TeguTexture *t,void *bytes,NSUInteger stride,TeguRegion r,
 NSUInteger level,NSUInteger slice,int write) {
 const struct TeguIOSurfaceAPI *api=tegu_iosurface_api();uint32_t seed=0;
 NSUInteger bpp=tegu_xr_source_bpp_of(t->shadowConvert);
 void *surface=(void *)t->shadowSurface;
 if(!api||!surface||!bytes||!bpp||level||slice||r.origin.z||r.size.depth!=1||
    r.origin.x>t->textureWidth||r.size.width>t->textureWidth-r.origin.x||
    r.origin.y>t->textureHeight||r.size.height>t->textureHeight-r.origin.y||
    stride<r.size.width*bpp)return 0;
 size_t row=0,sw=0,sh=0,salloc=0;unsigned char *base=0;
 if(!tegu_shadow_geometry(api,surface,t->shadowPlane,0,&row,&sw,&sh,&salloc)||!row||
    sw<t->textureWidth||sh<t->textureHeight||row<t->textureWidth*bpp||salloc/row<t->textureHeight)return 0;
 if(api->lock(surface,write?0:1,&seed))return 0;
 if(!tegu_shadow_geometry(api,surface,t->shadowPlane,&base,&row,&sw,&sh,&salloc))base=0;
 if(base)for(NSUInteger y=0;y<r.size.height;y++) {
  unsigned char *s=base+(r.origin.y+y)*row+r.origin.x*bpp,*c=(unsigned char *)bytes+y*stride;
  if(write)memcpy(s,c,r.size.width*bpp);else memcpy(c,s,r.size.width*bpp);
 }
 if(write)t->shadowSeedValid=0;
 return !api->unlock(surface,write?0:1,&seed)&&base;
}

static unsigned tegu_xr_source_bpp_of(unsigned k){return k==1||k==2?4:k==3||k==4?8:0;}
@implementation TeguTexture
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
 if(shadowConvert){if(!tegu_shadow_convert_cpu(self,bytes,stride,r,level,slice,0))MARK("FAIL texture_read_xr_surface");return;}
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
 if(shadowConvert){if(!tegu_shadow_convert_cpu(self,(void *)bytes,stride,r,level,slice,1))MARK("FAIL texture_write_xr_surface");return;}
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

@end
int main(void){
 struct Surface s={.w=64,.h=64,.row=256,.alloc=16384,.fourcc=TEGU_IOSURFACE_BGRA};
 for(unsigned i=0;i<sizeof(s.data);i++)s.data[i]=(unsigned char)(i*17+i/256);
 TeguTexture *t=class_createInstance(objc_getClass("TeguTexture"),0);assert(t);
 t->surfaceImported=1;t->surfaceTargetVersion=1;t->levelCount=1;t->textureKind=2;t->textureDepth=1;t->format=80;
 t->surfaceRowBytes=s.row;t->surfaceAllocSize=s.alloc;t->textureWidth=s.w;t->textureHeight=s.h;t->importedSurface=&s;
 t->mip[0]=(struct tegu_texture_level){64,64,256,0};
 uint8_t readback[66];memset(readback,0xa5,sizeof(readback));TeguRegion region={{3,2,0},{4,4,1}};
 [t getBytes:readback+1 bytesPerRow:16 fromRegion:region mipmapLevel:0];
 for(unsigned y=0;y<4;y++)assert(!memcmp(readback+1+y*16,s.data+(y+2)*256+12,16));
 assert(readback[0]==0xa5&&readback[65]==0xa5&&s.locks==1&&s.unlocks==1);
 uint8_t patch[16];memset(patch,0x51,16);TeguRegion wr={{7,8,0},{2,2,1}};
 [t replaceRegion:wr mipmapLevel:0 withBytes:patch bytesPerRow:8];
 for(unsigned y=0;y<2;y++)assert(!memcmp(s.data+(8+y)*256+28,patch,8));
 for(unsigned failure=0;failure<8;failure++){
  memset(readback,0xa5,sizeof(readback));unsigned oldUnlock=s.unlocks;
  if(failure==0)s.row=320;if(failure==1)s.alloc=4095;if(failure==2)s.fourcc=0;
  if(failure==3)s.planes=1;if(failure==4)s.w=63;if(failure==5)s.failLock=1;
  if(failure==6)t->textureResourceOptions=32;TeguRegion bad=region;if(failure==7)bad.origin.x=63;
  [t getBytes:readback+1 bytesPerRow:16 fromRegion:bad mipmapLevel:0];
  for(unsigned i=0;i<sizeof(readback);i++)assert(readback[i]==0xa5);
  assert(s.unlocks==oldUnlock);s.row=256;s.alloc=16384;s.fourcc=TEGU_IOSURFACE_BGRA;s.planes=0;s.w=64;s.failLock=0;t->textureResourceOptions=0;
 }
 object_dispose(t);
 /* XR decode shadow (BGR10_XR 'w30r'): CPU access goes to the surface's own
  * 4-byte texels, never to the RGBA16F backing; a write invalidates the
  * shadow so the next command re-decodes it. */
 struct Surface x={.w=16,.h=8,.row=64,.alloc=4096,.fourcc=0x77333072u};
 for(unsigned i=0;i<sizeof(x.data);i++)x.data[i]=(unsigned char)(i*29+7);
 uint8_t backing[16*8*8];memset(backing,0xee,sizeof(backing));struct Buffer xb={sizeof(backing),backing};
 TeguTexture *xt=class_createInstance(objc_getClass("TeguTexture"),0);assert(xt);
 xt->levelCount=1;xt->textureKind=2;xt->textureDepth=1;xt->format=115;xt->textureWidth=16;xt->textureHeight=8;
 xt->mip[0]=(struct tegu_texture_level){16,8,128,0};xt->buffer=&xb;xt->shadowSurface=&x;xt->shadowConvert=1;xt->shadowSeedValid=1;
 uint8_t xr[3*4*2+2];memset(xr,0xa5,sizeof(xr));TeguRegion xreg={{5,3,0},{3,2,1}};
 [xt getBytes:xr+1 bytesPerRow:12 fromRegion:xreg mipmapLevel:0];
 for(unsigned y=0;y<2;y++)assert(!memcmp(xr+1+y*12,x.data+(3+y)*64+5*4,12));
 assert(xr[0]==0xa5&&xr[sizeof(xr)-1]==0xa5&&x.locks==1&&x.unlocks==1&&xt->shadowSeedValid==1);
 uint8_t xpatch[8];memset(xpatch,0x3c,8);TeguRegion xw={{14,7,0},{2,1,1}};
 [xt replaceRegion:xw mipmapLevel:0 withBytes:xpatch bytesPerRow:8];
 assert(!memcmp(x.data+7*64+14*4,xpatch,8)&&xt->shadowSeedValid==0);
 for(unsigned i=0;i<sizeof(backing);i++)assert(backing[i]==0xee);
 TeguRegion xbad={{15,0,0},{2,1,1}};memset(xr,0xa5,sizeof(xr));
 [xt getBytes:xr bytesPerRow:12 fromRegion:xbad mipmapLevel:0];for(unsigned i=0;i<sizeof(xr);i++)assert(xr[i]==0xa5);
 object_dispose(xt);
 puts("PASS production Nord2 IOSurface CPU access: stride/subregion/canaries, eight refusal cases, XR decode-shadow surface access");
}
