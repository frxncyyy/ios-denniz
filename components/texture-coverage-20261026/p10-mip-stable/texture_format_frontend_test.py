#!/usr/bin/env python3
"""Real-frontend texture format coverage test (host only, no GPU claim).

Builds the production Objective-C frontend (render_encoder.inc /
render_pipeline.inc / compositor_submit.inc / compute_frontend.m) exactly as
the render harness gpu/build/deepseek-parallel-20261025/workers/render/
host_test_vs_texture.py does (its prelude is executed, not copied), with the
real AIR bridge, the real Mali compiler and the real n2mb bridge over the fake
JM kernel, and runs texture-format cases with independent CPU oracles:

- Apple XR IOSurfaces (BGR10_XR 'w30r', BGRA10_XR 'w40a'/'w40m', and the
  _sRGB twins) imported sampled-only: owned RGBA16F decode shadow, decoded
  halves against a libm/_Float16 oracle (independent of the generated
  tables), CPU getBytes/replaceRegion reach the surface, view rules, a fourcc
  size mismatch refuses, and a draw sampling the shadow commits (status 4).
- Multi-plane IOSurfaces ('420v' luma R8 / chroma RG8, 'b3a8' XR colour +
  A8): per-plane sampled shadows, plane/element-size refusals, compressed
  '&...' surfaces refused.
- Two-plane YCbCr textures (CA app-launch snapshots: '420v' as format 500,
  '420f' + BT.709 attachment as 520, codes 540): decode shadows equal the
  decoder on the surface planes, matrix/format/plane refusals, draw status 4.
- Texture types through authored AIR (tests/shaders/texture_types_probe.ll):
  cube (1 and 5 levels), 3D (1 and 3 levels), 1D (1 and 4 levels), 2D array
  (1 and 2 levels) sampled by the real planner/Mali/n2r: draw status 4.
- Sub-range/retyped views, rendering into a mip level through a level view.
- QuartzCore variable_blur_frag_lph (c6l device rc=-13) with its constexpr
  samplers over a 7-level BGRA8 pyramid: exact translator VkSampler desc
  through tegu_air_render_pipeline_immutable_sampler, draw status 4.

Usage: python3 gpu/tests/texture_format_frontend_test.py
Environment: TEGU_RELEASE=1 builds without ASan/UBSan.
"""
import os
import pathlib
import subprocess
import sys

G = pathlib.Path(__file__).resolve().parents[1]
HARNESS = G / 'build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py'
OUT = G / 'build/texture-coverage-20261026/host'
OUT.mkdir(parents=True, exist_ok=True)

# The planner's specialized sampled set (air_bridge.rs n2t_sampled_format)
# must equal the arch9 descriptor table minus depth (n2t_format_query).
import re as _re
_dump = subprocess.run(['cc', '-std=c11', '-I' + str(G / 'metal'), '-x', 'c', '-', '-o', str(OUT / 'n2t_dump')],
                       input=b'#include <stdio.h>\n#include "nord2_texture_resources.h"\nint main(void){for(unsigned f=0;f<700;f++){'
                       b'struct n2t_format_info i;if(!n2t_format_query(f,&i)&&!(i.flags&N2T_FMT_DEPTH))printf("%u ",f);}return 0;}\n',
                       check=True)
N2T_SAMPLED = {int(x) for x in subprocess.run([str(OUT / 'n2t_dump')], check=True, stdout=subprocess.PIPE).stdout.split()}
_rs = (G / 'metal/tegu/air_bridge.rs').read_text()
_body = _rs[_rs.index('fn n2t_sampled_format(format:u32)->bool {'):]
_body = _body[:_body.index('\n}\n')]
_astc = _rs[_rs.index('fn generic_astc_2d_format(format:u32)->bool {'):]
_astc = _astc[:_astc.index('\n}\n')]
def _expand(text):
    out = set()
    for a, b in _re.findall(r'(\d+)\.\.=(\d+)', text):
        out |= set(range(int(a), int(b) + 1))
    text = _re.sub(r'\d+\.\.=\d+', '', text)
    out |= {int(x) for x in _re.findall(r'\b(\d+)\b', text.split('matches!', 1)[1])} if 'matches!' in text else set()
    return out
RUST_SAMPLED = set()
for part in _body.split('matches!(format,')[1:]:
    RUST_SAMPLED |= _expand('matches!' + part.split(')', 1)[0])
RUST_SAMPLED |= _expand(_astc)
assert RUST_SAMPLED == N2T_SAMPLED, (sorted(RUST_SAMPLED ^ N2T_SAMPLED))
print('n2t sampled set == planner set:', len(N2T_SAMPLED))

text = HARNESS.read_text()
marker = "source += r'''\n/* Linux uint64_t"
pre = text[:text.index(marker)]
ns = {'__file__': str(HARNESS), '__name__': 'texture_format_prelude'}
exec(compile(pre, str(HARNESS), 'exec'), ns)
source = ns['source']
# Per-plane IOSurface fakes (IOSurfaceGet*OfPlane) for the plane shadow cases.
_plane_fakes = r'''
static size_t tp_off[3],tp_row[3],tp_w[3],tp_h[3],tp_e[3];
static void *test_si_pbase(void *p,size_t i){return ((struct TestIOSurface*)p)->data+tp_off[i];}
static size_t test_si_prow(void *p,size_t i){(void)p;return tp_row[i];}
static size_t test_si_pw(void *p,size_t i){(void)p;return tp_w[i];}
static size_t test_si_ph(void *p,size_t i){(void)p;return tp_h[i];}
static size_t test_si_pe(void *p,size_t i){(void)p;return tp_e[i];}
#include <stdio.h>
#include <stdlib.h>
/* CoreFoundation string fakes for IOSurfaceCopyValue(kIOSurfaceYCbCrMatrix). */
struct test_cfstr{unsigned magic;char text[40];};
static const char *test_matrix_text;
static void *test_cf_create(void *a,const char *t,uint32_t e){(void)a;(void)e;struct test_cfstr *s=calloc(1,sizeof(*s));
 s->magic=0xcf57;snprintf(s->text,sizeof(s->text),"%s",t);return s;}
static void *test_si_copyvalue(void *surface,void *key){(void)surface;
 if(!test_matrix_text||strcmp(((struct test_cfstr*)key)->text,"IOSurfaceYCbCrMatrix"))return 0;
 return test_cf_create(0,test_matrix_text,0);}
static unsigned char test_cf_cstring(void *s,char *buf,long n,uint32_t e){(void)e;
 if(((struct test_cfstr*)s)->magic!=0xcf57)return 0;snprintf(buf,(size_t)n,"%s",((struct test_cfstr*)s)->text);return 1;}
static unsigned long test_cf_typeid(void *s){(void)s;return 7;}
static unsigned long test_cf_string_typeid(void){return 7;}
'''
_anchor = 'static void test_si_release(void *p){(void)p;}'
assert source.count(_anchor) == 1
source = source.replace(_anchor, _anchor + _plane_fakes)
_dl = ' if(!strcmp(n,"CFRelease"))return (void*)test_si_release;'
assert source.count(_dl) == 1
source = source.replace(_dl, _dl + '''
 if(!strcmp(n,"IOSurfaceGetBaseAddressOfPlane"))return (void*)test_si_pbase;
 if(!strcmp(n,"IOSurfaceGetBytesPerRowOfPlane"))return (void*)test_si_prow;
 if(!strcmp(n,"IOSurfaceGetWidthOfPlane"))return (void*)test_si_pw;
 if(!strcmp(n,"IOSurfaceGetHeightOfPlane"))return (void*)test_si_ph;
 if(!strcmp(n,"IOSurfaceGetBytesPerElementOfPlane"))return (void*)test_si_pe;
 if(!strcmp(n,"IOSurfaceCopyValue"))return (void*)test_si_copyvalue;
 if(!strcmp(n,"CFStringCreateWithCString"))return (void*)test_cf_create;
 if(!strcmp(n,"CFStringGetCString"))return (void*)test_cf_cstring;
 if(!strcmp(n,"CFGetTypeID"))return (void*)test_cf_typeid;
 if(!strcmp(n,"CFStringGetTypeID"))return (void*)test_cf_string_typeid;''')
helpers = text.split(marker, 1)[1].split('int main(int argc,char **argv){', 1)[0]
source += '/* Linux uint64_t' + helpers

source += r'''
#include <math.h>
static uint16_t ref_half(double v){_Float16 h=(_Float16)v;uint16_t u;memcpy(&u,&h,2);return u;}
static double ref_srgb(double v){double a=fabs(v),l=a<=0.04045?a/12.92:pow((a+0.055)/1.055,2.4);return v<0?-l:l;}
static uint16_t ref_xr(unsigned code,int srgb){double v=((double)code-384.0)/510.0;return ref_half(srgb?ref_srgb(v):v);}
static uint16_t ref_xr_alpha(unsigned code){double v=((double)code-384.0)/510.0;return ref_half(v<0?0:v>1?1:v);}
static int half_close(uint16_t a,uint16_t b,int tolerance){int d=(int)a-(int)b;return d>=-tolerance&&d<=tolerance;}
static unsigned code_at(unsigned i){static const unsigned special[]={0,383,384,385,639,894,895,1023};return i<8?special[i]:(i*389u+17u)%1024u;}
static TeguTexture *import_surface(TeguComputeDevice *d,NSUInteger format,NSUInteger usage){
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=format;desc->w=test_surface.width;desc->h=test_surface.height;
 desc->levels=1;desc->type=2;desc->dep=1;desc->samples=1;desc->arrays=1;desc->uses=usage;desc->options=0;
 TeguTexture *t=[d newTextureWithDescriptor:(id)desc iosurface:(id)&test_surface plane:0];[desc release];return t;
}
static void test_xr_imports(TeguComputeDevice *d){
 static const struct {NSUInteger format;uint32_t fourcc;unsigned bpp,kind,srgb;} xr[]={
  {554,0x77333072u,4,1,0},{555,0x77333072u,4,2,1},{552,0x77343061u,8,3,0},{553,0x7734306du,8,4,1}};
 for(unsigned k=0;k<4;k++){
  const unsigned w=16,h=8;
  test_surface.width=w;test_surface.height=h;test_surface.row=w*xr[k].bpp+(k&1?64:0);
  test_surface.alloc=test_surface.row*h;test_surface.planes=0;test_surface.fourcc=xr[k].fourcc;
  memset(test_surface.data,0x5a,sizeof(test_surface.data));
  unsigned codes[16*8][4];
  for(unsigned i=0;i<w*h;i++){
   unsigned char *px=test_surface.data+(i/w)*test_surface.row+(i%w)*xr[k].bpp;
   for(unsigned c=0;c<4;c++)codes[i][c]=code_at(i*4+c);
   if(xr[k].bpp==4){uint32_t word=0xc0000000u|codes[i][0]<<20|codes[i][1]<<10|codes[i][2];memcpy(px,&word,4);}
   else{uint16_t q[4]={(uint16_t)(codes[i][2]<<6|(i&63)),(uint16_t)(codes[i][1]<<6),(uint16_t)(codes[i][0]<<6),(uint16_t)(codes[i][3]<<6)};memcpy(px,q,8);}
  }
  TeguTexture *t=import_surface(d,xr[k].format,1);
  assert(t&&[t pixelFormat]==xr[k].format&&t->format==115&&t->shadowConvert==xr[k].kind);
  assert(t->buffer&&!(t->rowBytes&63)&&t->rowBytes>=w*8&&!t->surfaceImported);
  struct Dispatch sj;memset(&sj,0,sizeof(sj));sj.compositorTextures[3]=(id)t;struct Dispatch *jobs[1]={&sj};
  unsigned before=test_surface_locks;
  assert(tegu_shadow_sync_jobs(jobs,1,0)==w*h*8);
  assert(test_surface_locks==before+1);
  /* A decode shadow is never written back, even when a job lists it as output. */
  assert(tegu_shadow_sync_jobs(jobs,1,1)==0);
  const uint16_t *halves=(const uint16_t *)((const unsigned char *)[t->buffer contents]+t->mip[0].offset);
  for(unsigned i=0;i<w*h;i++){
   const uint16_t *p=halves+(i/w)*(t->rowBytes/2)+(i%w)*4;
   for(unsigned c=0;c<3;c++)assert(half_close(p[c],ref_xr(codes[i][c],xr[k].srgb),xr[k].srgb?1:0));
   assert(p[3]==(xr[k].bpp==4?0x3c00u:ref_xr_alpha(codes[i][3])));
  }
  /* CPU readback is the surface's own XR bytes. */
  unsigned char back[16*8];TeguRegion region={{2,1,0},{3,2,1}};
  [t getBytes:back bytesPerRow:3*xr[k].bpp fromRegion:region mipmapLevel:0];
  for(unsigned y=0;y<2;y++)assert(!memcmp(back+y*3*xr[k].bpp,test_surface.data+(1+y)*test_surface.row+2*xr[k].bpp,3*xr[k].bpp));
  /* Views: itself only. */
  TeguTexture *same=[t newTextureViewWithPixelFormat:xr[k].format];
  assert(same&&same->format==115&&[same pixelFormat]==xr[k].format&&same->shadowConvert==xr[k].kind);[same release];
  assert(![t newTextureViewWithPixelFormat:115]&&![t newTextureViewWithPixelFormat:80]);
  [t release];
 }
 /* c6l: XR sampled + render-target surfaces (usage 5). A job rendering into
  * the RGBA16F shadow is encoded back to XR codes: every code's own decoded
  * half encodes to that code (decode/encode identity), BGR10_XR bits 30/31 set. */
 for(unsigned k=0;k<4;k++){
  const unsigned w=64,h=16;
  test_surface.width=w;test_surface.height=h;test_surface.row=w*xr[k].bpp;test_surface.alloc=test_surface.row*h;
  test_surface.planes=0;test_surface.fourcc=xr[k].fourcc;memset(test_surface.data,0,test_surface.alloc);
  TeguTexture *t=import_surface(d,xr[k].format,5);
  assert(t&&t->shadowConvert==xr[k].kind&&t->textureUsage==5&&t->format==115);
  const uint16_t *colour=xr[k].srgb?tegu_xr_half_srgb:tegu_xr_half_linear;
  uint16_t *halves=(uint16_t *)((unsigned char *)[t->buffer contents]+t->mip[0].offset);
  for(unsigned i=0;i<w*h;i++){uint16_t *px=halves+(i/w)*(t->rowBytes/2)+(i%w)*4;
   px[0]=colour[i%1024];px[1]=colour[(i*7+3)%1024];px[2]=colour[(i*13+5)%1024];px[3]=tegu_xr_half_alpha[384+(i%511)];}
  struct Dispatch wj;memset(&wj,0,sizeof(wj));wj.renderTexture=(id)t;struct Dispatch *wjobs[1]={&wj};
  assert(tegu_shadow_sync_jobs(wjobs,1,1)>0);
  for(unsigned i=0;i<w*h;i++){const unsigned char *px=test_surface.data+(i/w)*test_surface.row+(i%w)*xr[k].bpp;
   unsigned r,g,b,a=0;
   if(xr[k].bpp==4){uint32_t word;memcpy(&word,px,4);assert((word>>30)==3);r=(word>>20)&1023;g=(word>>10)&1023;b=word&1023;}
   else{uint16_t q[4];memcpy(q,px,8);b=q[0]>>6;g=q[1]>>6;r=q[2]>>6;a=q[3]>>6;}
   assert(r==i%1024&&g==(i*7+3)%1024&&b==(i*13+5)%1024);
   if(xr[k].bpp==8)assert(a==384+(i%511));
  }
  [t release];
 }
 /* Bytes-per-element mismatch: an XR texture over a one-byte surface. */
 test_surface.width=16;test_surface.height=8;test_surface.row=64;test_surface.alloc=512;test_surface.fourcc=0x4c303038u;
 assert(!import_surface(d,554,1));
 /* Unknown fourcc refuses rather than guessing. */
 test_surface.fourcc=0x3f3f3f3fu;assert(!import_surface(d,80,1));
 /* Plain byte-exact shadows: RGBA16F over 'RGhA' and BGRA8 over 'BGRA'. */
 test_surface.row=128;test_surface.alloc=1024;test_surface.fourcc=0x52476841u;
 for(unsigned i=0;i<1024;i++)test_surface.data[i]=(unsigned char)(i*7+1);
 TeguTexture *rgha=import_surface(d,115,1);assert(rgha&&!rgha->shadowConvert&&[rgha pixelFormat]==115);
 struct Dispatch pj;memset(&pj,0,sizeof(pj));pj.compositorTextures[0]=(id)rgha;struct Dispatch *pjobs[1]={&pj};
 assert(tegu_shadow_sync_jobs(pjobs,1,0)==16*8*8);
 for(unsigned y=0;y<8;y++)assert(!memcmp((unsigned char *)[rgha->buffer contents]+rgha->mip[0].offset+y*rgha->rowBytes,test_surface.data+y*128,128));
 [rgha release];
 puts("XR_IMPORTS ok=4");puts("XR_RENDER_WRITEBACK ok=4");
}
/* Two-plane '420v' (Y 8-bit full res, CbCr 8-bit pairs half res) and 'b3a8'
 * (XR colour + A8): each plane imports as its own sampled shadow. */
static void test_plane_imports(TeguComputeDevice *d){
 test_surface.width=16;test_surface.height=8;test_surface.planes=2;test_surface.fourcc=0x34323076u;
 tp_off[0]=0;tp_row[0]=64;tp_w[0]=16;tp_h[0]=8;tp_e[0]=1;
 tp_off[1]=1024;tp_row[1]=64;tp_w[1]=8;tp_h[1]=4;tp_e[1]=2;
 test_surface.row=64;test_surface.alloc=2048;
 for(unsigned i=0;i<2048;i++)test_surface.data[i]=(unsigned char)(i*11+3);
 TeguTexture *y=import_surface(d,10,1);assert(y&&y->shadowPlane==1&&[y iosurfacePlane]==0);
 test_surface.width=8;test_surface.height=4; /* the CbCr texture's own size */
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=30;desc->w=8;desc->h=4;desc->levels=1;desc->type=2;desc->dep=1;
 desc->samples=1;desc->arrays=1;desc->uses=1;desc->options=0;
 TeguTexture *uv=[d newTextureWithDescriptor:(id)desc iosurface:(id)&test_surface plane:1];
 assert(uv&&uv->shadowPlane==2&&[uv iosurfacePlane]==1&&uv->format==30);
 struct Dispatch sj;memset(&sj,0,sizeof(sj));sj.compositorTextures[0]=(id)y;sj.compositorTextures[1]=(id)uv;
 struct Dispatch *jobs[1]={&sj};
 assert(tegu_shadow_sync_jobs(jobs,1,0)==16*8+8*4*2);
 for(unsigned r=0;r<8;r++)assert(!memcmp((unsigned char *)[y->buffer contents]+y->mip[0].offset+r*y->rowBytes,test_surface.data+r*64,16));
 for(unsigned r=0;r<4;r++)assert(!memcmp((unsigned char *)[uv->buffer contents]+uv->mip[0].offset+r*uv->rowBytes,test_surface.data+1024+r*64,16));
 /* Wrong element size for the plane (RG8 over the 1-byte luma plane) and an
  * out-of-range plane refuse. */
 desc->format=30;desc->w=16;desc->h=8;
 assert(![d newTextureWithDescriptor:(id)desc iosurface:(id)&test_surface plane:0]);
 assert(![d newTextureWithDescriptor:(id)desc iosurface:(id)&test_surface plane:2]);
 /* 'b3a8': plane0 BGR10_XR decode shadow, plane1 A8. */
 test_surface.fourcc=0x62336138u;tp_e[0]=4;tp_row[0]=64;tp_w[0]=16;tp_h[0]=8;tp_off[0]=0;
 tp_e[1]=1;tp_row[1]=64;tp_w[1]=16;tp_h[1]=8;tp_off[1]=1024;
 for(unsigned i=0;i<128;i++){uint32_t word=(code_at(i)<<20)|(code_at(i+3)<<10)|code_at(i+5);
  memcpy(test_surface.data+(i/16)*64+(i%16)*4,&word,4);}
 desc->format=554;desc->w=16;desc->h=8;
 TeguTexture *xr=[d newTextureWithDescriptor:(id)desc iosurface:(id)&test_surface plane:0];
 assert(xr&&xr->shadowConvert==1&&xr->shadowPlane==1);
 desc->format=1;TeguTexture *alpha=[d newTextureWithDescriptor:(id)desc iosurface:(id)&test_surface plane:1];
 assert(alpha&&alpha->shadowPlane==2);
 struct Dispatch xj;memset(&xj,0,sizeof(xj));xj.compositorTextures[0]=(id)xr;xj.compositorTextures[1]=(id)alpha;
 struct Dispatch *xjobs[1]={&xj};
 assert(tegu_shadow_sync_jobs(xjobs,1,0)==16*8*8+16*8);
 const uint16_t *h=(const uint16_t *)((const unsigned char *)[xr->buffer contents]+xr->mip[0].offset);
 for(unsigned i=0;i<128;i++){const uint16_t *px=h+(i/16)*(xr->rowBytes/2)+(i%16)*4;
  assert(px[0]==ref_xr(code_at(i),0)&&px[1]==ref_xr(code_at(i+3),0)&&px[2]==ref_xr(code_at(i+5),0)&&px[3]==0x3c00u);}
 for(unsigned r=0;r<8;r++)assert(!memcmp((unsigned char *)[alpha->buffer contents]+alpha->mip[0].offset+r*alpha->rowBytes,test_surface.data+1024+r*64,16));
 /* Lossless-compressed surfaces ('&8v0') have no linear bytes: refused. */
 test_surface.fourcc=0x26387630u;desc->format=10;
 assert(![d newTextureWithDescriptor:(id)desc iosurface:(id)&test_surface plane:0]);
 [desc release];[y release];[uv release];[xr release];[alpha release];
 test_surface.planes=0;
 puts("PLANE_IMPORTS ok=4");
}
/* CA's app-launch zoom samples the app snapshot, a '420v' surface made a
 * texture of format 500 (YCBCR8_420_2P): RGBA16F decode shadow, Metal's
 * conversion (decoder bit-exactness: tests/ycbcr_decode_test.py). */
static void test_ycbcr_imports(TeguComputeDevice *d){
 test_surface.width=16;test_surface.height=8;test_surface.planes=2;test_surface.fourcc=0x34323076u;
 test_surface.row=64;test_surface.alloc=2048;
 tp_off[0]=0;tp_row[0]=64;tp_w[0]=16;tp_h[0]=8;tp_e[0]=1;
 tp_off[1]=1024;tp_row[1]=64;tp_w[1]=8;tp_h[1]=4;tp_e[1]=2;
 memset(test_surface.data,235,1024);memset(test_surface.data+1024,128,1024); /* video-range white */
 test_matrix_text=0;
 TeguTexture *t=import_surface(d,500,1);
 assert(t&&t->format==90&&[t pixelFormat]==500&&t->shadowConvert==TEGU_SHADOW_CONVERT_YCBCR);
 struct tegu_ycbcr_layout l;tegu_ycbcr_unpack(t->shadowYcbcr,&l);
 assert(l.bits==8&&!l.packed&&!l.full_range&&l.sub==TEGU_YCBCR_SUB_420&&l.output==TEGU_YCBCR_OUT_CONVERTED&&l.matrix==TEGU_YCBCR_BT601);
 struct Dispatch sj;memset(&sj,0,sizeof(sj));sj.compositorTextures[2]=(id)t;struct Dispatch *jobs[1]={&sj};
 assert(tegu_shadow_sync_jobs(jobs,1,0)==16*8*4);
 for(unsigned y=0;y<8;y++){const uint32_t *w=(const uint32_t *)((const unsigned char *)[t->buffer contents]+t->mip[0].offset+y*t->rowBytes);
  for(unsigned i=0;i<16;i++)assert(w[i]==0xffffffffu);}
 /* Random planes: the shadow equals the decoder on the surface's planes. */
 for(unsigned i=0;i<2048;i++)test_surface.data[i]=(unsigned char)(i*37+11);
 assert(tegu_shadow_sync_jobs(jobs,1,0)==16*8*4);
 uint16_t want[16*4];uint32_t packed[16];
 for(unsigned y=0;y<8;y++){tegu_ycbcr_decode_row(&l,test_surface.data,64,test_surface.data+1024,64,16,8,4,y,want,tegu_srgb_knots_8192);
  tegu_ycbcr_pack10_row(want,packed,16);
  assert(!memcmp((const unsigned char *)[t->buffer contents]+t->mip[0].offset+y*t->rowBytes,packed,sizeof(packed)));}
 [t release];
 /* Full range BT.709 attachment, '420f'; sRGB twin 520; codes 540. */
 test_surface.fourcc=0x34323066u;test_matrix_text="ITU_R_709_2";
 t=import_surface(d,520,1);assert(t);tegu_ycbcr_unpack(t->shadowYcbcr,&l);
 assert(l.full_range&&l.matrix==TEGU_YCBCR_BT709&&l.output==TEGU_YCBCR_OUT_SRGB&&t->format==115);[t release];
 t=import_surface(d,540,1);assert(t&&t->format==70&&[t pixelFormat]==540);
 sj.compositorTextures[2]=(id)t;assert(tegu_shadow_sync_jobs(jobs,1,0)==16*8*4);[t release];
 /* Refusals: unknown matrix value, 10-bit format over an 8-bit surface,
  * a 4:4:4 format over 4:2:0 chroma, plane 1, single-plane surface. */
 test_matrix_text="ITU_R_FAKE";assert(!import_surface(d,500,1));test_matrix_text=0;
 assert(!import_surface(d,505,1));assert(!import_surface(d,503,1));
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=500;desc->w=16;desc->h=8;desc->levels=1;desc->type=2;desc->dep=1;
 desc->samples=1;desc->arrays=1;desc->uses=1;
 assert(![d newTextureWithDescriptor:(id)desc iosurface:(id)&test_surface plane:1]);[desc release];
 test_surface.planes=0;assert(!import_surface(d,500,1));
 puts("YCBCR_IMPORTS ok=3");
}
'''

source += r'''
static TeguFunction *tf_function(TeguComputeDevice *d,const char *path,const char *name){
 FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
 void *b=malloc((size_t)n);assert(b&&fread(b,1,(size_t)n,f)==(size_t)n);fclose(f);id error=0;
 TeguLibrary *l=[d newTeguLibraryWithBytes:b length:(NSUInteger)n error:&error];free(b);assert(l&&!error);
 TeguFunction *fn=[l newFunctionWithName:[NSString stringWithUTF8String:name]];assert(fn);return fn;
}
static TeguTexture *tf_texture(TeguComputeDevice *d,NSUInteger fmt,NSUInteger w,NSUInteger h,NSUInteger levels,NSUInteger usage,NSUInteger options){
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=fmt;desc->w=w;desc->h=h;desc->levels=levels;desc->type=2;desc->dep=1;
 desc->samples=1;desc->arrays=1;desc->uses=usage;desc->options=options;
 TeguTexture *t=[d newTextureWithDescriptor:(id)desc];[desc release];return t;
}
/* c6l device: 9x TEGU_DRAW_SILENT variable_blur_frag_lph rc=-13 "immutable
 * sampler v2 lacks exact Metal state": QuartzCore's variable blur samples a
 * mip pyramid (bicubic_upsample at an explicit level) through constexpr
 * samplers. The translator's exact VkSampler desc now feeds the v2 sidecar. */
static void test_variable_blur(TeguComputeDevice *d,TeguQueue *q,const char *vs,const char *fs){
 TeguFunction *vf=tf_function(d,vs,"variable_blur_vert_lph"),*ff=tf_function(d,fs,"variable_blur_frag_lph");
 RenderDescriptor *pd=[[RenderDescriptor alloc] init];
 pd->vertex=vf;pd->fragment=ff;pd->samples=1;pd->raster=1;pd->amplification=1;pd->topology=0;
 pd->colors=[[RenderColors alloc] init];
 for(unsigned i=0;i<8;i++){pd->colors->colors[i]=[[RenderColor alloc] init];pd->colors->colors[i]->mask=15;}
 pd->colors->colors[0]->format=80;
 RenderLayout *l=[[RenderLayout alloc] init];l->buffers=[[RenderLayout alloc] init];
 const NSUInteger fmts[3]={31,29,29},offs[3]={0,16,24};
 for(unsigned i=0;i<3;i++){RenderAttribute *a=[[RenderAttribute alloc] init];a->value=fmts[i];a->byteOffset=offs[i];a->bufferSlot=0;l->entries[i]=a;}
 RenderAttribute *b=[[RenderAttribute alloc] init];b->layoutStride=32;b->step=1;b->rate=1;l->buffers->entries[0]=b;
 pd->layout=l;
 id error=0;TeguRenderPipeline *p=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];
 fprintf(stderr,"VBLUR_PIPE ptr=%p draw=%u err=%p\n",p,p?p->genericDraw:0,error);
 assert(p&&!error&&p->genericDraw);
 for(unsigned binding=0;binding<16;binding++){TeguAIRImmutableSampler v;char e[160];
  if(!tegu_air_render_pipeline_immutable_sampler(p->compiled,binding,&v,e,sizeof(e)))
   fprintf(stderr,"VBLUR_IMMUTABLE b=%u min=%u mag=%u mip=%u addr=%u,%u,%u border=%u cmp=%u aniso=%u lod=%g..%g bias=%g\n",
    binding,v.min_filter,v.mag_filter,v.mipmap_mode,v.address[0],v.address[1],v.address[2],v.border_color,v.compare_enabled,
    v.anisotropy_enabled,v.min_lod,v.max_lod,v.mip_lod_bias);}
 TeguTexture *target=tf_texture(d,80,64,64,1,5,0x20),*source=tf_texture(d,80,64,64,7,5,0x20),
  *mask=tf_texture(d,80,64,64,1,1,0),*noise=tf_texture(d,10,64,64,1,1,0);
 assert(target&&source&&mask&&noise);
 TeguBuffer *vb=[d newBufferWithLength:4096 options:0],*u=[d newBufferWithLength:4096 options:0];assert(vb&&u);
 memset([vb contents],0,4096);memset([u contents],0,4096);
 RenderPass *pass=[[RenderPass alloc] init];
 pass->color=[[PassColor alloc] init];pass->color->target=[target retain];
 pass->color->load=2;pass->color->store=1;pass->passWidth=pass->passHeight=64;pass->arrayLength=1;pass->sampleCount=1;
 TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];assert(e);
 [e setRenderPipelineState:p];[e setVertexBuffer:vb offset:0 atIndex:0];
 [e setVertexBuffer:u offset:0 atIndex:2];[e setVertexBuffer:u offset:256 atIndex:3];
 [e setFragmentBuffer:u offset:512 atIndex:1];[e setFragmentBuffer:u offset:1024 atIndex:5];[e setFragmentBuffer:u offset:1280 atIndex:6];
 [e setFragmentTexture:source atIndex:3];[e setFragmentTexture:mask atIndex:4];[e setFragmentTexture:noise atIndex:9];
 [e drawPrimitives:3 vertexStart:0 vertexCount:6];[e endEncoding];
 fprintf(stderr,"VBLUR_ENCODE invalid=%u at=%s:%u count=%u\n",c->invalid,c->invalidFunction?c->invalidFunction:"-",c->invalidLine,c->count);
 assert(!c->invalid);
 [c commit];[c waitUntilCompleted];
 fprintf(stderr,"VBLUR_DRAW status=%lu error=%p\n",(unsigned long)[c status],[c error]);
 assert([c status]==4&&![c error]);
 [pass release];[target release];[source release];[mask release];[noise release];[vb release];[u release];[p release];[pd release];
}

/* Texture types through authored AIR (tests/shaders/texture_types_probe.ll):
 * cube, 3D, 1D and 2D array sampled in a fragment function, each a fresh
 * pipeline, real planner + Mali + n2r sidecar. */
static void test_texture_types(TeguComputeDevice *d,TeguQueue *q,const char *path){
 static const struct {const char *fn;NSUInteger type,w,h,depth,arrays,levels;} cases[]={
  {"tt_cube",5,16,16,1,1,1},{"tt_cube",5,16,16,1,1,5},{"tt_3d",7,16,16,8,1,1},{"tt_3d",7,16,16,8,1,3},
  {"tt_1d",0,64,1,1,1,1},{"tt_1d",0,64,1,1,1,4},{"tt_2darray",3,16,16,1,3,1},{"tt_2darray",3,16,16,1,3,2}};
 TeguFunction *vf=tf_function(d,path,"tt_vertex");
 TeguTexture *target=new_target(d);TeguBuffer *vb=[d newBufferWithLength:256 options:0];
 RenderPass *pass=[[RenderPass alloc] init];
 pass->color=[[PassColor alloc] init];pass->color->target=[target retain];
 pass->color->load=2;pass->color->store=1;pass->passWidth=pass->passHeight=16;pass->arrayLength=1;pass->sampleCount=1;
 unsigned drawn=0;
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
  TeguFunction *ff=tf_function(d,path,cases[i].fn);
  RenderDescriptor *pd=[[RenderDescriptor alloc] init];
  pd->vertex=[vf retain];pd->fragment=ff;pd->samples=1;pd->raster=1;pd->amplification=1;pd->topology=0;
  pd->colors=[[RenderColors alloc] init];
  pd->colors->colors[0]=[[RenderColor alloc] init];pd->colors->colors[0]->format=80;pd->colors->colors[0]->mask=15;
  id error=0;TeguRenderPipeline *p=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];
  TextureDesc *td=[[TextureDesc alloc] init];td->format=70;td->w=cases[i].w;td->h=cases[i].h;td->levels=cases[i].levels;
  td->type=cases[i].type;td->dep=cases[i].depth;td->samples=1;td->arrays=cases[i].arrays;td->uses=1;td->options=0;
  TeguTexture *t=[d newTextureWithDescriptor:(id)td];[td release];
  TeguSampler *s=new_sampler(d,3);
  unsigned long status=0;unsigned invalid=9;
  if(p&&t){
   TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
   [e setRenderPipelineState:p];[e setFragmentTexture:t atIndex:0];[e setFragmentSamplerState:s atIndex:0];
   [e drawPrimitives:3 vertexStart:0 vertexCount:3];[e endEncoding];invalid=c->invalid;
   [c commit];[c waitUntilCompleted];status=[c status];
  }
  fprintf(stderr,"TYPE %s type=%lu levels=%lu pipe=%p tex=%p invalid=%u status=%lu\n",cases[i].fn,(unsigned long)cases[i].type,
   (unsigned long)cases[i].levels,(void *)p,(void *)t,invalid,status);
  if(status==4&&!invalid)drawn++;
  [t release];[s release];[p release];[pd release];drain();
 }
 [pass release];[target release];[vb release];[vf release];drain();
 printf("TEXTURE_TYPES drawn=%u of %u\n",drawn,(unsigned)(sizeof(cases)/sizeof(cases[0])));
}

int main(int argc,char **argv){
 assert(argc>=2);
 FILE *file=fopen(argv[1],"rb");assert(file);fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
 assert(size>0);void *bytes=malloc((size_t)size);assert(bytes&&fread(bytes,1,(size_t)size,file)==(size_t)size);fclose(file);
 struct fake f={0};uintptr_t bridge=open_bridge(&f);
 TeguComputeDevice *d=[TeguComputeDevice alloc];
 d->connection=bridge;d->scalar=scalar_adapter;d->method=method_adapter;d->map=map_adapter;d->unmap=unmap_adapter;
 d->capabilities=(1ULL<<14)|(1ULL<<18)|256ULL|64ULL|32ULL|16ULL;
 test_xr_imports(d);
 test_plane_imports(d);
 test_ycbcr_imports(d);
 id error=0;
 TeguLibrary *lib=[d newTeguLibraryWithBytes:bytes length:(NSUInteger)size error:&error];free(bytes);
 assert(lib&&!error);
 TeguFunction *vf=[lib newFunctionWithName:[NSString stringWithUTF8String:"vt_vertex"]];
 TeguFunction *ff=[lib newFunctionWithName:[NSString stringWithUTF8String:"vt_fragment"]];
 assert(vf&&ff);
 RenderDescriptor *pd=[[RenderDescriptor alloc] init];
 pd->vertex=vf;pd->fragment=ff;pd->samples=1;pd->raster=1;pd->amplification=1;pd->topology=0;
 pd->colors=[[RenderColors alloc] init];
 pd->colors->colors[0]=[[RenderColor alloc] init];pd->colors->colors[0]->format=80;pd->colors->colors[0]->mask=15;
 TeguRenderPipeline *pipeline=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];
 assert(pipeline&&!error);
 TeguQueue *q=[d newCommandQueue];assert(q);
 TeguTexture *target=new_target(d);assert(target);
 TeguBuffer *vb=[d newBufferWithLength:256 options:0];assert(vb);
 RenderPass *pass=[[RenderPass alloc] init];
 pass->color=[[PassColor alloc] init];pass->color->target=[target retain];
 pass->color->load=2;pass->color->store=1;pass->passWidth=pass->passHeight=16;pass->arrayLength=1;pass->sampleCount=1;

 /* --- a draw sampling an XR decode shadow commits through the ordinary
  * half-float class (no translator variant, no neutral substitution) --- */
 {
  test_surface.width=16;test_surface.height=16;test_surface.row=64;test_surface.alloc=1024;
  test_surface.planes=0;test_surface.fourcc=0x77333072u;
  for(unsigned i=0;i<256;i++){uint32_t word=(code_at(i)<<20)|(code_at(i+1)<<10)|code_at(i+2);memcpy(test_surface.data+i*4,&word,4);}
  TeguTexture *xt=import_surface(d,554,1);assert(xt);
  TeguSampler *s0=new_sampler(d,0),*s1=new_sampler(d,3);
  TeguCommand *c=[q commandBuffer];assert(c);
  TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];assert(e);
  bind_all(e,vb,pipeline,xt,xt,s0,s1);
  [e drawPrimitives:3 vertexStart:0 vertexCount:3];[e endEncoding];
  assert(!c->invalid);
  struct Dispatch *job=tegu_command_job(c,1);
  const struct tegu_generic_draw_request *r=&job->genericDrawRequest;
  unsigned textures=0;
  for(unsigned i=0;i<r->binding_count;i++)if(r->bindings[i].kind==TEGU_GENERIC_DRAW_COMBINED||
     r->bindings[i].kind==TEGU_GENERIC_DRAW_TEXTURE){assert(r->bindings[i].format==115);textures++;}
  assert(textures==2);
  [c commit];[c waitUntilCompleted];
  fprintf(stderr,"XR_DRAW status=%lu\n",(unsigned long)[c status]);
  assert([c status]==4&&![c error]);
  [xt release];[s0 release];[s1 release];drain();
 }
 /* --- a draw sampling a 420v snapshot (format 500) --- */
 {
  test_surface.width=16;test_surface.height=16;test_surface.planes=2;test_surface.fourcc=0x34323076u;test_surface.row=64;test_surface.alloc=4096;
  tp_off[0]=0;tp_row[0]=64;tp_w[0]=16;tp_h[0]=16;tp_e[0]=1;tp_off[1]=1024;tp_row[1]=64;tp_w[1]=8;tp_h[1]=8;tp_e[1]=2;
  for(unsigned i=0;i<2048;i++)test_surface.data[i]=(unsigned char)(i*13+5);
  TeguTexture *yt=import_surface(d,500,1);assert(yt);
  TeguSampler *s0=new_sampler(d,3),*s1=new_sampler(d,3);
  TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];assert(e);
  bind_all(e,vb,pipeline,yt,yt,s0,s1);[e drawPrimitives:3 vertexStart:0 vertexCount:3];[e endEncoding];
  assert(!c->invalid);
  const struct tegu_generic_draw_request *r=&tegu_command_job(c,1)->genericDrawRequest;
  for(unsigned i=0;i<r->binding_count;i++)if(r->bindings[i].kind==TEGU_GENERIC_DRAW_COMBINED)assert(r->bindings[i].format==90);
  [c commit];[c waitUntilCompleted];
  fprintf(stderr,"YCBCR_DRAW status=%lu\n",(unsigned long)[c status]);assert([c status]==4&&![c error]);
  [yt release];[s0 release];[s1 release];drain();test_surface.planes=0;
 }
 /* --- XR render targets: a pipeline with a BGR10_XR attachment compiles an
  * RGBA16F target; it draws into a private XR texture (RGBA16F internally)
  * and into a 'w30r' IOSurface shadow (usage 5), whose clear+draw result is
  * encoded back to XR codes after the command. --- */
 {
  RenderDescriptor *xd=[[RenderDescriptor alloc] init];
  xd->vertex=[vf retain];xd->fragment=[ff retain];xd->samples=1;xd->raster=1;xd->amplification=1;xd->topology=0;
  xd->colors=[[RenderColors alloc] init];
  xd->colors->colors[0]=[[RenderColor alloc] init];xd->colors->colors[0]->format=554;xd->colors->colors[0]->mask=15;
  id xerr=0;TeguRenderPipeline *xp=[d newRenderPipelineStateWithDescriptor:(id)xd error:&xerr];
  assert(xp&&!xerr&&xp->genericDraw&&xp->colorFormat==115&&xp->genericRtFormats[0]==115);
  TeguTexture *priv=tf_texture(d,554,16,16,1,5,0x20);
  assert(priv&&[priv pixelFormat]==554&&priv->format==115&&!(priv->rowBytes&63));
  test_surface.width=16;test_surface.height=16;test_surface.row=64;test_surface.alloc=1024;test_surface.planes=0;
  test_surface.fourcc=0x77333072u;memset(test_surface.data,0,1024);
  TeguTexture *surf=import_surface(d,554,5);assert(surf&&surf->textureUsage==5);
  TeguTexture *src=new_sample(d,80,16,16,1,2,1);fill_texture(src,0xff808080u);
  TeguSampler *s0=new_sampler(d,0);
  for(unsigned k=0;k<2;k++){
   RenderPass *xpass=[[RenderPass alloc] init];
   xpass->color=[[PassColor alloc] init];xpass->color->target=[(k?surf:priv) retain];
   xpass->color->load=2;xpass->color->store=1;
   xpass->passWidth=xpass->passHeight=16;xpass->arrayLength=1;xpass->sampleCount=1;
   TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)xpass];assert(e);
   bind_all(e,vb,xp,src,src,s0,s0);[e drawPrimitives:3 vertexStart:0 vertexCount:3];[e endEncoding];
   fprintf(stderr,"XR_TARGET k=%u invalid=%u at=%s:%u count=%u\n",k,c->invalid,c->invalidFunction?c->invalidFunction:"-",c->invalidLine,c->count);
   assert(!c->invalid);
   [c commit];[c waitUntilCompleted];
   fprintf(stderr,"XR_TARGET_DRAW k=%u status=%lu\n",k,(unsigned long)[c status]);assert([c status]==4&&![c error]);
   [xpass release];
  }
  /* The fake JM executes no GPU work: the shadow keeps what the CPU put
   * there, so prove the write-back ran by its XR encoding of the shadow. */
  const uint16_t *hs=(const uint16_t *)((const unsigned char *)[surf->buffer contents]+surf->mip[0].offset);
  uint32_t word;memcpy(&word,test_surface.data,4);
  assert((word>>30)==3&&((word>>20)&1023)==tegu_xr_encode_range(tegu_xr_half_linear,hs[0],0,1023));
  [priv release];[surf release];[src release];[s0 release];[xp release];[xd release];drain();
  puts("XR_TARGETS ok=2");
 }
 /* --- every n2t sampled format through the real planner + Mali compiler +
  * n2r: 16x16 2D usage1 texture sampled in both stages (the probe samples
  * texture2d<float>). Outcome per format is printed for the matrix. --- */
 {
  static const unsigned formats[]={TF_FORMATS};
  for(unsigned i=0;i<sizeof(formats)/sizeof(formats[0]);i++){
   TeguTexture *ft=new_sample(d,formats[i],16,16,1,2,1);
   if(!ft){fprintf(stderr,"FMT %u alloc=0\n",formats[i]);continue;}
   TeguSampler *s0=new_sampler(d,3);
   /* A fresh pipeline per format: each sampled view format is its own
    * specialized variant and a pipeline holds a bounded variant set. */
   id perr=0;TeguRenderPipeline *fp=[d newRenderPipelineStateWithDescriptor:(id)pd error:&perr];assert(fp&&!perr);
   TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];assert(e);
   bind_all(e,vb,fp,ft,ft,s0,s0);[e drawPrimitives:3 vertexStart:0 vertexCount:3];[e endEncoding];
   unsigned invalid=c->invalid;
   [c commit];[c waitUntilCompleted];
   fprintf(stderr,"FMT %u alloc=1 invalid=%u status=%lu\n",formats[i],invalid,(unsigned long)[c status]);
   [ft release];[s0 release];[fp release];drain();
  }
 }
 /* --- sub-range texture views (levels/slices/type casts): CPU access,
  * sampling, and a render pass into a mip level through a level view --- */
 {
  TeguTexture *m=new_sample_ex(d,80,16,16,4,2,1,1,1);assert(m&&m->levelCount==4);
  TeguTextureViewRange lv={2,2},one={0,1};
  TeguTexture *v=[m newTextureViewWithPixelFormat:80 textureType:2 levels:lv slices:one];
  assert(v&&v->levelCount==2&&v->textureWidth==4&&v->textureHeight==4&&v->mip[0].offset==m->mip[2].offset);
  uint32_t px[16];for(unsigned i=0;i<16;i++)px[i]=0x01020304u*(i+1);
  TeguRegion r4={{0,0,0},{4,4,1}};[v replaceRegion:r4 mipmapLevel:0 withBytes:px bytesPerRow:16];
  uint32_t back[16]={0};[m getBytes:back bytesPerRow:16 fromRegion:r4 mipmapLevel:2];assert(!memcmp(back,px,sizeof(px)));
  TeguTextureViewRange bad={3,2};assert(![m newTextureViewWithPixelFormat:80 textureType:2 levels:bad slices:one]);
  TeguTextureViewRange two={0,2};assert(![m newTextureViewWithPixelFormat:80 textureType:2 levels:lv slices:two]);
  assert(![m newTextureViewWithPixelFormat:115 textureType:2 levels:lv slices:one]); /* size class differs */
  TeguTexture *va=[m newTextureViewWithPixelFormat:81 textureType:3 levels:lv slices:one];assert(va&&va->textureKind==3&&va->format==81);
  [va release];
  /* 2DArray of 4 layers: view slices 1..2 as an array, slice 3 as a 2D. */
  TeguTexture *arr=new_sample_ex(d,70,16,16,1,3,1,4,1);assert(arr&&arr->layers==4&&arr->levelLayerStride[0]);
  TeguTextureViewRange l0={0,1},s12={1,2},s3={3,1};
  TeguTexture *av=[arr newTextureViewWithPixelFormat:70 textureType:3 levels:l0 slices:s12];
  assert(av&&av->layers==2&&av->arrayLength==2&&av->mip[0].offset==arr->mip[0].offset+arr->levelLayerStride[0]);
  TeguTexture *a2=[arr newTextureViewWithPixelFormat:70 textureType:2 levels:l0 slices:s3];
  assert(a2&&a2->layers==1&&a2->textureKind==2&&a2->mip[0].offset==arr->mip[0].offset+3*arr->levelLayerStride[0]);
  /* Sample the level view in a draw. */
  TeguSampler *s0=new_sampler(d,3);
  TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];assert(e);
  bind_all(e,vb,pipeline,v,a2,s0,s0);[e drawPrimitives:3 vertexStart:0 vertexCount:3];[e endEncoding];
  assert(!c->invalid);[c commit];[c waitUntilCompleted];
  fprintf(stderr,"VIEW_DRAW status=%lu\n",(unsigned long)[c status]);assert([c status]==4);
  /* Render into mip level 1 of a render-target texture through a level view. */
  TeguTexture *rtm=tf_texture(d,80,32,32,3,5,0x20);assert(rtm&&rtm->levelCount==3);
  TeguTextureViewRange l1={1,1};
  TeguTexture *rv=[rtm newTextureViewWithPixelFormat:80 textureType:2 levels:l1 slices:one];
  assert(rv&&rv->textureWidth==16&&!(rv->rowBytes&63));
  RenderPass *mp=[[RenderPass alloc] init];mp->color=[[PassColor alloc] init];mp->color->target=[rv retain];
  mp->color->load=2;mp->color->store=1;mp->passWidth=mp->passHeight=16;mp->arrayLength=1;mp->sampleCount=1;
  c=[q commandBuffer];e=[c renderCommandEncoderWithDescriptor:(id)mp];assert(e);
  bind_all(e,vb,pipeline,v,v,s0,s0);[e drawPrimitives:3 vertexStart:0 vertexCount:3];[e endEncoding];
  fprintf(stderr,"VIEW_TARGET invalid=%u at=%s:%u\n",c->invalid,c->invalidFunction?c->invalidFunction:"-",c->invalidLine);
  assert(!c->invalid);[c commit];[c waitUntilCompleted];
  fprintf(stderr,"VIEW_TARGET_DRAW status=%lu\n",(unsigned long)[c status]);assert([c status]==4);
  [mp release];[rv release];[rtm release];[s0 release];[v release];[av release];[a2 release];[arr release];[m release];drain();
  puts("VIEWS ok=1");
 }
 /* --- render targets: every blendable n2r format through a pipeline of that
  * colour format, opaque and with source-over blending, status 4 --- */
 {
  static const NSUInteger rts[]={10,25,43,65,70,80,90,94,115};unsigned ok=0;
  for(unsigned i=0;i<sizeof(rts)/sizeof(rts[0]);i++)for(unsigned blend=0;blend<2;blend++){
   RenderDescriptor *rd=[[RenderDescriptor alloc] init];
   rd->vertex=[vf retain];rd->fragment=[ff retain];rd->samples=1;rd->raster=1;rd->amplification=1;rd->topology=0;
   rd->colors=[[RenderColors alloc] init];
   RenderColor *col=rd->colors->colors[0]=[[RenderColor alloc] init];col->format=rts[i];col->mask=15;
   if(blend){col->blend=1;col->rgbSrc=1;col->rgbDst=5;col->alphaSrc=1;col->alphaDst=5;}
   id rerr=0;TeguRenderPipeline *rp=[d newRenderPipelineStateWithDescriptor:(id)rd error:&rerr];
   TeguTexture *rt=tf_texture(d,rts[i],16,16,1,5,0x20);
   TeguTexture *src=new_sample(d,80,16,16,1,2,1);TeguSampler *s0=new_sampler(d,3);
   unsigned long status=0;unsigned inv=9;
   if(rp&&rt){
    RenderPass *rpass=[[RenderPass alloc] init];rpass->color=[[PassColor alloc] init];rpass->color->target=[rt retain];
    rpass->color->load=2;rpass->color->store=1;rpass->passWidth=rpass->passHeight=16;rpass->arrayLength=1;rpass->sampleCount=1;
    TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)rpass];
    if(e){bind_all(e,vb,rp,src,src,s0,s0);[e drawPrimitives:3 vertexStart:0 vertexCount:3];[e endEncoding];}
    inv=c->invalid;[c commit];[c waitUntilCompleted];status=[c status];[rpass release];
   }
   fprintf(stderr,"RT %lu blend=%u pipe=%p tex=%p invalid=%u status=%lu\n",(unsigned long)rts[i],blend,(void *)rp,(void *)rt,inv,status);
   if(status==4&&!inv)ok++;
   [rp release];[rt release];[src release];[s0 release];[rd release];drain();
  }
  printf("RENDER_TARGETS ok=%u of %u\n",ok,(unsigned)(2*sizeof(rts)/sizeof(rts[0])));
 }
 /* --- generateMipmaps per format: BGR5A1 is a 5551 average (was decoded
  * as half floats), RGB10A2/R16F/RGBA16Unorm now generate too --- */
 {
  static const NSUInteger mf[]={43,90,25,110,81,70};unsigned ok=0;
  for(unsigned i=0;i<sizeof(mf)/sizeof(mf[0]);i++){
   TeguTexture *t=tf_texture(d,mf[i],4,4,3,5,0);assert(t&&t->levelCount==3);
   NSUInteger bpp=tegu_texture_bytes_per_pixel(mf[i]);unsigned char *base=(unsigned char *)[t->buffer contents];
   for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)memset(base+t->mip[0].offset+y*t->mip[0].stride+x*bpp,((x+y)&1)?0xff:0x00,bpp);
   TeguCommand *c=[q commandBuffer];TeguBlitEncoder *b=[c blitCommandEncoder];[b generateMipmapsForTexture:t];[b endEncoding];
   unsigned jobs=c->count;[c commit];[c waitUntilCompleted];
   struct tegu_mip_format m;assert(tegu_mip_codec((uint32_t)mf[i],&m));
   unsigned char want[16],white[16],black[16];float a[4],bk[4],avg[4];memset(white,0xff,16);memset(black,0,16);
   tegu_mip_decode(&m,white,a);tegu_mip_decode(&m,black,bk);for(unsigned k=0;k<4;k++)avg[k]=(a[k]+bk[k])/2;
   tegu_mip_encode(&m,avg,want);
   const unsigned char *l1=base+t->mip[1].offset;
   int same=!memcmp(l1,want,bpp)&&!memcmp(l1+bpp,want,bpp)&&jobs==2&&[c status]==4;
   fprintf(stderr,"MIP %lu jobs=%u status=%lu ok=%d\n",(unsigned long)mf[i],jobs,(unsigned long)[c status],same);
   ok+=same;[t release];drain();
  }
  printf("MIPMAPS ok=%u of %u\n",ok,(unsigned)(sizeof(mf)/sizeof(mf[0])));
 }
 if(argc>=4)test_variable_blur(d,q,argv[2],argv[3]);
 if(argc>=5)test_texture_types(d,q,argv[4]);
 [pass release];[target release];[vb release];[pipeline release];[q release];[lib release];drain();
 assert(!f.pending);
 close_bridge(bridge,&f);
 puts("PASS texture formats: XR decode shadows (4 formats, oracle), byte shadows, refusals, XR draw status4; no GPU claim");
 return 0;
}
'''
source += (ns['TEgu'] / 'runtime_frontend_host_test.m').read_text().split(
    'NSUInteger tegu_host_constant_data_size(NSUInteger type)', 1)[1].join(
    ['NSUInteger tegu_host_constant_data_size(NSUInteger type)', ''])

_only = os.environ.get('TF_FORMATS_ONLY')
TEST_FORMATS = sorted(int(x) for x in _only.split(',')) if _only else sorted(N2T_SAMPLED)
source = source.replace('TF_FORMATS', ','.join(str(f) for f in TEST_FORMATS))
path = OUT / 'texture_format_frontend_test.m'
path.write_text(source)
R = HARNESS.parent
bridge_c = OUT / 'bridge_real.c'
bridge_c.write_bytes((G / 'metal/nord2_metal_bridge.c').read_bytes())
libs = [os.environ.get('TEGU_AIR_LIB_DIR', str(G / 'build/deepseek-parallel-20261025/workers/compute/cargo-target/release')),
        str(G / 'build/mesa-build/src/panfrost/clc')]
cmd = ['clang', '-fblocks', '-pthread', '-O1', '-g', '-DTEGU_ENABLE_MSAA4', '-DTEGU_HOST_REAL_V2']
if not os.environ.get('TEGU_RELEASE'):
    cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
cmd += ['-Wno-incompatible-pointer-types', '-Wno-incompatible-function-pointer-types',
        '-fconstant-string-class=NXConstantString']
cmd += ['-I' + str(p) for p in (ns['TEgu'], G / 'metal', G / 'include', G / 'build/tegu-frontend')]
cmd += [str(path), str(bridge_c), str(G / 'metal/nord2_metal_render.c'),
        str(G / 'src/nord2_gpu_desc.c'), str(G / 'src/nord2_gpu_jm.c')]
cmd += ['-lobjc', '-ldl', '-lBlocksRuntime', '-lm'] + ['-L' + p for p in libs] + [
    '-ltegu_air_bridge', '-lnord2_mali_runtime', '-o', str(OUT / 'texture_format_frontend_test')]
subprocess.run(cmd, check=True)
air = R / 'authored' / 'probe.air'
if not air.exists():
    subprocess.run(['llvm-as', str(R / 'vertex_texture_probe.ll'), '-o', str(air)], check=True)
env = dict(os.environ, LD_LIBRARY_PATH=':'.join(libs), ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
corpus = G / 'build/ui-corpus-20261025'
blur_vs = corpus / 'air/5397b32688a4885f8209044bede3737b29d71ee66aff9fd9b515178d6314e28a.air'
blur_fs = corpus / 'air/b462ee23bea2364eb75f6a29cad591a35963d6567fc9e9ab65522defa5fb3805.air'
types_air = OUT / 'texture_types_probe.air'
subprocess.run(['llvm-as', str(G / 'tests/shaders/texture_types_probe.ll'), '-o', str(types_air)], check=True)
result = subprocess.run([str(OUT / 'texture_format_frontend_test'), str(air), str(blur_vs), str(blur_fs), str(types_air)], env=env,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
out = result.stdout.decode('utf-8', 'replace'); err = result.stderr.decode('utf-8', 'replace')
sys.stdout.write(out)
(OUT / 'run.stderr.log').write_text(err)
if result.returncode:
    sys.stderr.write(err[-6000:])
    sys.exit(result.returncode)
assert 'TEXTURE_TYPES drawn=8 of 8' in out, out
assert 'RENDER_TARGETS ok=18 of 18' in out, out
assert 'MIPMAPS ok=6 of 6' in out, out
assert 'XR_IMPORTS ok=4' in out and 'XR_RENDER_WRITEBACK ok=4' in out and 'PLANE_IMPORTS ok=4' in out and 'YCBCR_IMPORTS ok=3' in out and 'XR_TARGETS ok=2' in out and 'VIEWS ok=1' in out and 'XR_DRAW status=4' in err, err[-3000:]
assert 'VBLUR_DRAW status=4' in err and 'YCBCR_DRAW status=4' in err, err[-3000:]
assert 'reason=sampled-fourcc-size-mismatch' in err and 'fmt=554' in err, err[-3000:]
outcomes = {}
for line in err.splitlines():
    m = _re.match(r'FMT (\d+) alloc=(\d)(?: invalid=(\d+) status=(\d+))?', line)
    if m:
        outcomes[int(m.group(1))] = (int(m.group(2)), m.group(3), m.group(4))
(OUT / 'format-draw-outcomes.json').write_text(__import__('json').dumps(outcomes, indent=1, sort_keys=True))
SRGB8 = {11, 31, 71, 81}
INTEGER = {13, 14, 23, 24, 33, 34, 53, 54, 63, 64, 73, 74, 91, 103, 104, 113, 114, 123, 124}
for f in TEST_FORMATS:
    alloc, invalid, status = outcomes.get(f, (0, None, None))
    assert alloc, f'format {f} not allocated'
    if f in SRGB8 or f in INTEGER:
        continue  # translator variant / sampled-type class: recorded, explicit
    assert status == '4' and invalid == '0', f'format {f} did not draw: {outcomes.get(f)}'
drawn = sorted(f for f, o in outcomes.items() if o[2] == '4' and o[1] == '0')
print('formats drawn status4:', len(drawn), 'recorded:', len(outcomes))
print('texture_format_frontend_test: PASS')
