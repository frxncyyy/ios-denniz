#!/usr/bin/env python3
"""Compile the production probe's pixel/direct-surface checks with mock storage.

The expected buffers are built independently from explicit regions/colours.
No GPU execution or device qualification is claimed by this host regression.
"""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/probe-surface-test'
OUT.mkdir(parents=True, exist_ok=True)
source = (ROOT / 'metal/nord2_metal_probe.m').read_text()


def function(name):
    start = source.index('static ', source.index(name) - 20)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


prefix = r'''
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
'''
suffix = r'''
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
'''
fixture = prefix + function('pixelMismatches') + '\n' + function('surfacePixels') + suffix
(OUT / 'test.c').write_text(fixture)
subprocess.run(['clang', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                str(OUT / 'test.c'), '-o', str(OUT / 'test')], check=True)
result = subprocess.run([str(OUT / 'test')], capture_output=True, text=True, check=True)
(OUT / 'test.log').write_text(result.stdout + result.stderr)
receipt = dict(production_source_sha256=hashlib.sha256(source.encode()).hexdigest(),
               fixture_sha256=hashlib.sha256(fixture.encode()).hexdigest(),
               sanitizer_pass=True, hardware_executed=False, result=result.stdout.splitlines()[-1])
(OUT / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(receipt['result'])
