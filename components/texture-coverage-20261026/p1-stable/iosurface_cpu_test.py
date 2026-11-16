#!/usr/bin/env python3
"""Execute the production texture CPU methods with a simulated IOSurface ABI.
No GPU emulation: this verifies v1 surface row copies and rejection guards.
"""
from pathlib import Path
import argparse, subprocess, os
r=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--source',type=Path,default=r/'metal/tegu/render_encoder.inc');a=ap.parse_args()
source=a.source.read_text()
helper=source[source.index('static int tegu_texture_cpu_layer'):source.index('@implementation TeguTexture')]
s=source.split('@implementation TeguTexture',1)[1]
methods=s[s.index('- (void)getBytes:'):s.index('- (void)dealloc')]
out=r/'build/render-diagnostic-review';out.mkdir(parents=True,exist_ok=True)
head=r'''
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
struct TeguIOSurfaceAPI{size_t(*getWidth)(void*),(*getHeight)(void*),(*getBytesPerRow)(void*),(*getAllocSize)(void*);unsigned(*getPixelFormat)(void*);size_t(*getPlaneCount)(void*);int(*lock)(void*,unsigned,unsigned*),(*unlock)(void*,unsigned,unsigned*);void*(*getBaseAddress)(void*);};
static const struct TeguIOSurfaceAPI *tegu_iosurface_api(void){static const struct TeguIOSurfaceAPI api={width,height,row,alloc,fourcc,planes,lock,unlock,base};return &api;}
__attribute__((objc_root_class)) @interface TeguTexture {
@public Class isa;NSUInteger textureResourceOptions,surfaceImported,surfaceTargetVersion,levelCount,textureKind,textureDepth,format,surfaceRowBytes,surfaceAllocSize,textureWidth,textureHeight;
struct tegu_texture_level mip[1];NSUInteger layers,levelLayerStride[1];struct Buffer *buffer;void *importedSurface;
void *shadowSurface;unsigned shadowConvert,shadowSeedValid;
}
- (void)getBytes:(void*)b bytesPerRow:(NSUInteger)s bytesPerImage:(NSUInteger)i fromRegion:(TeguRegion)r mipmapLevel:(NSUInteger)l slice:(NSUInteger)x;
- (void)replaceRegion:(TeguRegion)r mipmapLevel:(NSUInteger)l slice:(NSUInteger)x withBytes:(const void*)b bytesPerRow:(NSUInteger)s bytesPerImage:(NSUInteger)i;
@end
'''
head += helper + '\nstatic unsigned tegu_xr_source_bpp_of(unsigned k){return k==1||k==2?4:k==3||k==4?8:0;}\n@implementation TeguTexture\n'
foot=r'''
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
'''
f=out/'iosurface_cpu.m';f.write_text(head+methods+foot)
subprocess.run(['clang','-x','objective-c','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined',str(f),'-lobjc','-o',str(out/'iosurface_cpu')],check=True)
subprocess.run([str(out/'iosurface_cpu')],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
