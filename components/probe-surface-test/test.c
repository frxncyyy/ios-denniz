
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
struct surface {
 size_t width,height,stride,allocation;
 uint32_t format;uint8_t *base;int lock,unlock,locks,unlocks,base_calls;
};
typedef struct surface *IOSurfaceRef;
#define kIOSurfaceLockReadOnly 1
static size_t IOSurfaceGetWidth(IOSurfaceRef s){return s->width;}
static size_t IOSurfaceGetHeight(IOSurfaceRef s){return s->height;}
static size_t IOSurfaceGetBytesPerRow(IOSurfaceRef s){return s->stride;}
static size_t IOSurfaceGetAllocSize(IOSurfaceRef s){return s->allocation;}
static uint32_t IOSurfaceGetPixelFormat(IOSurfaceRef s){return s->format;}
static void *IOSurfaceGetBaseAddress(IOSurfaceRef s){s->base_calls++;return s->base;}
static int IOSurfaceLock(IOSurfaceRef s,uint32_t flags,uint32_t *seed){assert(flags==1&&!seed);s->locks++;return s->lock;}
static int IOSurfaceUnlock(IOSurfaceRef s,uint32_t flags,uint32_t *seed){assert(flags==1&&!seed);s->unlocks++;return s->unlock;}
static unsigned pixelMismatches(const uint8_t *pixels,size_t stride,unsigned bpp,int bgra,unsigned pass) {
 unsigned mismatches=0;
 for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++){
  const float expected[4][4]={{.25,.5,.75,1},{1,0,0,1},{.125,.25,.5,1},{.5625,.25,.25,1}};
  int inside=x>=16&&x<48&&y>=16&&y<48;
  if(pass==5||pass==9)inside=0;
  if(pass==6)inside=x<16&&y>=16&&y<48;
  if(pass==7)inside=x>=24&&x<40&&y>=16&&y<48;
  unsigned mode=pass==4?3:pass==3?(inside?1:2):pass==2?2:pass==1&&inside?1:0;
  if(pass>=5)mode=pass==9?0:inside?1:2;
  for(unsigned k=0;k<(bpp==1?1u:4u);k++){
   unsigned channel=bgra&&k<3?2-k:k;float got;
   if(bpp==8){_Float16 half;memcpy(&half,pixels+y*stride+x*bpp+k*2,2);got=(float)half;}
   else got=pixels[y*stride+x*bpp+k]/255.0f;
   float delta=got-expected[mode][channel],tolerance=bpp==8?.001f:.0041f;
   if(!(delta>=-tolerance&&delta<=tolerance)){mismatches++;break;}
  }
 }
 return mismatches;
}
static int surfacePixels(IOSurfaceRef surface,unsigned pass,const char *name) {
 size_t width=IOSurfaceGetWidth(surface),height=IOSurfaceGetHeight(surface);
 size_t stride=IOSurfaceGetBytesPerRow(surface),allocation=IOSurfaceGetAllocSize(surface);
 int layout=width==64&&height==64&&stride>=256&&stride<=allocation/64&&IOSurfaceGetPixelFormat(surface)==0x42475241;
 int lock=IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL),unlock=-1;
 unsigned checked=0,bad=0;uint32_t center=0;
 if(!lock){
  const uint8_t *base=IOSurfaceGetBaseAddress(surface);
  if(layout&&base){checked=4096;bad=pixelMismatches(base,stride,4,1,pass);memcpy(&center,base+32*stride+32*4,4);}
  else layout=0;
  unlock=IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
 }
 printf("NORD2_METAL IOSurface pass=%s checked=%u mismatches=%u lock=%d unlock=%d layout=%d stride=%zu center=%08x\n",name,checked,bad,lock,unlock,layout,stride,center);
 return lock||unlock||!layout||checked!=4096||bad;
}
static void reference(uint8_t *out,size_t stride,unsigned bpp,int bgra,unsigned pass){
 memset(out,0xa5,stride*64);
 for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++){
  float rgba[4]={.25,.5,.75,1};int red=0,clear=0;
  switch(pass){
   case 0:case 9:break;
   case 1:red=x>=16&&x<48&&y>=16&&y<48;break;
   case 2:case 5:clear=1;break;
   case 3:case 8:clear=1;red=x>=16&&x<48&&y>=16&&y<48;break;
   case 4:rgba[0]=.5625;rgba[1]=.25;rgba[2]=.25;break;
   case 6:clear=1;red=x<16&&y>=16&&y<48;break;
   case 7:clear=1;red=x>=24&&x<40&&y>=16&&y<48;break;
   default:assert(0);
  }
  if(clear){rgba[0]=.125;rgba[1]=.25;rgba[2]=.5;}
  if(red){rgba[0]=1;rgba[1]=rgba[2]=0;}
  for(unsigned k=0;k<(bpp==1?1u:4u);k++){
   float value=rgba[bgra&&k<3?2-k:k];
   if(bpp==8){_Float16 h=value;memcpy(out+y*stride+x*bpp+k*2,&h,2);}
   else out[y*stride+x*bpp+k]=(uint8_t)(value*255+.5);
  }
 }
}
int main(void){
 uint8_t storage[64*(512+32)];unsigned cases=0;
 const unsigned widths[]={1,4,4,8};
 for(unsigned format=0;format<4;format++)for(unsigned padded=0;padded<2;padded++)
  for(unsigned pass=0;pass<10;pass++){
   unsigned bpp=widths[format];size_t stride=64*bpp+padded*32;
   reference(storage,stride,bpp,format==2,pass);
   assert(pixelMismatches(storage,stride,bpp,format==2,pass)==0);cases++;
  }
 for(unsigned padded=0;padded<2;padded++)for(unsigned pass=0;pass<10;pass++){
  size_t stride=256+padded*32;reference(storage,stride,4,1,pass);
  struct surface s={64,64,stride,stride*64,0x42475241,storage,0,0,0,0,0};
  assert(!surfacePixels(&s,pass,"golden"));assert(s.locks==1&&s.unlocks==1);
  /* A wrong single pixel must fail, even if the centre sample remains right. */
  storage[3]=0;assert(surfacePixels(&s,pass,"one-bad-alpha"));
  assert(pixelMismatches(storage,stride,4,1,pass)==1);cases+=2;
 }
 reference(storage,256,4,1,9);
 /* Reproduce the old v7 error: empty LOAD leaves background R=64, not the
  * old final blend's R=143/144. The old centre check falsely rejects it. */
 uint32_t center;memcpy(&center,storage+32*256+32*4,4);
 assert(center==0xff4080bf);assert(((center>>16)&255)<143);
 assert(pixelMismatches(storage,256,4,1,9)==0);
 reference(storage,256,4,1,4);
 assert(pixelMismatches(storage,256,4,1,9)==4096);
 reference(storage,256,4,1,9);
 for(unsigned fault=0;fault<9;fault++){
  struct surface s={64,64,256,16384,0x42475241,storage,0,0,0,0,0};
  switch(fault){
   case 0:s.lock=-1;break;
   case 1:s.unlock=-1;break;
   case 2:s.base=NULL;break;
   case 3:s.width=63;break;
   case 4:s.height=63;break;
   case 5:s.stride=128;break;
   case 6:s.allocation=16383;break;
   case 7:s.stride=SIZE_MAX;s.allocation=SIZE_MAX;break;
   case 8:s.format=0;break;
  }
  assert(surfacePixels(&s,9,"fault"));assert(s.locks==1);
  assert(s.unlocks==(fault?1:0));if(!fault)assert(!s.base_calls);cases++;
 }
 printf("PASS %u production pixel/surface checks; stale final colour, lock failures and malformed layouts rejected\n",cases);
 return 0;
}
