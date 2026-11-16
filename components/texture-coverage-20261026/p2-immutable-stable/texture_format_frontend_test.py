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
 if(!strcmp(n,"IOSurfaceGetBytesPerElementOfPlane"))return (void*)test_si_pe;''')
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
 puts("XR_IMPORTS ok=4");
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
 if(argc>=4)test_variable_blur(d,q,argv[2],argv[3]);
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
result = subprocess.run([str(OUT / 'texture_format_frontend_test'), str(air), str(blur_vs), str(blur_fs)], env=env,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
out = result.stdout.decode('utf-8', 'replace'); err = result.stderr.decode('utf-8', 'replace')
sys.stdout.write(out)
(OUT / 'run.stderr.log').write_text(err)
if result.returncode:
    sys.stderr.write(err[-6000:])
    sys.exit(result.returncode)
assert 'XR_IMPORTS ok=4' in out and 'PLANE_IMPORTS ok=4' in out and 'XR_DRAW status=4' in err, err[-3000:]
assert 'VBLUR_DRAW status=4' in err, err[-3000:]
assert 'reason=sampled-fourcc-size-mismatch' in err and 'fmt=554' in err, err[-3000:]
print('texture_format_frontend_test: PASS')
