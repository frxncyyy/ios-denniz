#!/usr/bin/env python3
"""Native-macro compute frontend host test.

Builds the real compute_frontend.m/runtime_pipeline.inc with
-DTEGU_AIR_BRIDGE -DTEGU_MALI_BRIDGE -DTEGU_GRAPHICS_BRIDGE -DNORD2_JM_TRANSPORT
against the real n2mb bridge and a fake JM kernel model.  A function-created
buffer-only pipeline must submit TDG1 (272-byte wire); a kernel with sampled
textures must submit TDG2 (6304 bytes) even when an extra texture is bound but
unused by the shader.  No GPU execution.
"""
from __future__ import annotations

import hashlib
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WORK = HERE.parents[4]
GPU = WORK / "gpu"
T = GPU / "metal/tegu"
OUT = HERE / "build/native_compute_frontend_test"

source = ("#include <pthread.h>\n#define TEGU_AIR_BRIDGE 1\n#define TEGU_MALI_BRIDGE 1\n"
          "#define TEGU_GRAPHICS_BRIDGE 1\n#define NORD2_JM_TRANSPORT 1\n"
          "#define TEGU_IOSURFACE_TEST_LOADER 1\n")
source += (T / "frontend_host_test.m").read_text().split(
    "static unsigned allocated,freed")[0]
runtime = (T / "runtime_frontend_host_test.m").read_text()
source += runtime.split("static id strings[1024]", 1)[1].split(
    "static unsigned imports,submissions", 1)[0].join(
    ["static id strings[1024]", ""])
source += (T / "render_encoder_host_test.inc").read_text().split(
    "@class IOSurfaceFixture", 1)[0]
source += "static unsigned iosurfaceImports,iosurfaceReleases,iosurfaceReplyMode;\n"
_inc = (T / "iosurface_frontend_host_test.inc").read_text()
source += _inc[_inc.index("@interface IOSurfaceFixture"):_inc.index("static void test_iosurface_texture_frontend")]

source += r'''
/* ---- independent fake transport for one native compute device ---- */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <pthread.h>
#include "nord2_gpu_transport.h"
extern int setenv(const char *,const char *,int);extern int unsetenv(const char *);
#define FB_COUNT 64
struct fb {unsigned handle;uint64_t va;size_t size;unsigned char *host;int active;};
struct fake {pthread_mutex_t mutex;struct fb f[FB_COUNT];uint32_t next;uint64_t nextva;
 unsigned live,maps,submitted,waited,closed;uint64_t job;struct nord2_gpu_submit_request req;int have_req;};
static struct fake F;
static struct fb *fb_by_handle(unsigned h){for(unsigned i=0;i<FB_COUNT;i++)if(F.f[i].active&&F.f[i].handle==h)return &F.f[i];return 0;}
static struct fb *fb_by_va(uint64_t va){for(unsigned i=0;i<FB_COUNT;i++)if(F.f[i].active&&va>=F.f[i].va&&va<F.f[i].va+F.f[i].size)return &F.f[i];return 0;}
static int fake_call(void *o,unsigned sel,const uint64_t *in,unsigned ni,const void *bytes,size_t size,uint64_t *out,unsigned *no,void *ob,size_t *osz){
 (void)o;(void)ob;(void)osz;
 switch(sel){
 case NORD2_GPU_SEL_ALLOC:{if(ni!=2||!no||*no<3)return 1;struct fb *b=0;for(unsigned i=0;i<FB_COUNT;i++)if(!F.f[i].active){b=&F.f[i];break;}if(!b)return 1;
  b->size=(in[0]+16383u)&~16383ull;b->host=calloc(1,b->size);if(!b->host)return 1;b->active=1;b->handle=++F.next;b->va=F.nextva;F.nextva+=0x4000000ull;F.live++;out[0]=b->handle;out[1]=b->va;out[2]=b->size;*no=3;return 0;}
 case NORD2_GPU_SEL_RELEASE:{if(ni!=1)return 1;struct fb *b=fb_by_handle((unsigned)in[0]);if(!b)return 1;b->active=0;free(b->host);b->host=0;F.live--;if(no)*no=0;return 0;}
 case NORD2_GPU_SEL_SUBMIT_JM:{if(size!=sizeof(struct nord2_gpu_submit_request)||!no||*no<1)return 1;memcpy(&F.req,bytes,sizeof(F.req));F.have_req=1;F.submitted++;out[0]=++F.job;*no=1;return 0;}
 case NORD2_GPU_SEL_WAIT:{if(ni!=2||!no||*no<1)return 1;F.waited++;out[0]=4;*no=1;return 0;}
 case NORD2_GPU_SEL_STATUS:{struct nord2_gpu_status *s=ob;if(!s||!osz||*osz<sizeof(*s))return 1;memset(s,0,sizeof(*s));s->abi_version=2;s->enabled=s->powered=s->arch_ok=1;s->gpu_id=0x90800011;*no=0;*osz=sizeof(*s);return 0;}
 default:return 1;}
}
static int fake_map(void *o,unsigned h,uint64_t *a,uint64_t *n){(void)o;struct fb *b=fb_by_handle(h);if(!b)return 1;F.maps++;*a=(uint64_t)(uintptr_t)b->host;*n=b->size;return 0;}
static int fake_unmap(void *o,unsigned h,uint64_t a){(void)o;(void)h;(void)a;return 0;}
static int fake_close(void *o){(void)o;F.closed=1;return 0;}
static void fake_lock(void *o){assert(!pthread_mutex_lock(&((struct fake *)o)->mutex));}
static void fake_unlock(void *o){assert(!pthread_mutex_unlock(&((struct fake *)o)->mutex));}
static uintptr_t open_fake(void){memset(&F,0,sizeof(F));F.nextva=0x1000000000ull;assert(!pthread_mutex_init(&F.mutex,0));
 struct n2mb_ops ops={.opaque=&F,.call=fake_call,.map=fake_map,.unmap=fake_unmap,.close=fake_close,.lock=fake_lock,.unlock=fake_unlock};
 uintptr_t b=n2mb_open(&ops);assert(b);return b;}
static void close_fake(uintptr_t b){assert(!n2mb_close(b));assert(F.closed);assert(!pthread_mutex_destroy(&F.mutex));}

static unsigned lastWireSize,lastWireMagic,wireEvents;
static int scalar_adapter(TeguConnection c,unsigned sel,const U64 *in,unsigned ni,U64 *out,unsigned *no){uint64_t a[16]={0},b[16]={0};assert(ni<=16&&(!no||*no<=16));if(ni)memcpy(a,in,ni*8);int e=n2mb_scalar(c,sel,ni?a:0,ni,out?b:0,no);if(out&&no){assert(*no<=16);memcpy(out,b,*no*8);}return e;}
static int method_adapter(TeguConnection c,unsigned sel,const U64 *in,unsigned ni,const void *data,NSUInteger bytes,U64 *out,unsigned *no,void *output,NSUInteger *size){uint64_t a[16]={0},b[16]={0};assert(ni<=16&&(!no||*no<=16));if(ni)memcpy(a,in,ni*8);
 if(sel==7&&bytes>=4){lastWireSize=(unsigned)bytes;lastWireMagic=*(const unsigned char *)data;wireEvents++;}
 int e=n2mb_method(c,sel,ni?a:0,ni,data,bytes,out?b:0,no,output,size);if(out&&no){assert(*no<=16);memcpy(out,b,*no*8);}return e;}
static int map_adapter(TeguConnection c,unsigned h,unsigned task,U64 *a,U64 *n,unsigned flags){uint64_t x=0,y=0;int e=n2mb_map(c,h,task,&x,&y,flags);*a=x;*n=y;return e;}
static int unmap_adapter(TeguConnection c,unsigned h,unsigned task,U64 a){return n2mb_unmap(c,h,task,a);}
static void drain(void){while(poolCount)[pool[--poolCount] release];}

@interface NativeSamplerDesc : NSObject { @public NSUInteger mn,mg,mip,as,at,ar,cmp,aniso; float lmin,lmax; }
@end
@implementation NativeSamplerDesc
- (signed char)respondsToSelector:(SEL)sel{return sel==@selector(respondsToSelector:)||sel==@selector(minFilter)||sel==@selector(magFilter)||sel==@selector(mipFilter)||sel==@selector(sAddressMode)||sel==@selector(tAddressMode)||sel==@selector(rAddressMode)||sel==@selector(normalizedCoordinates)||sel==@selector(compareFunction)||sel==@selector(maxAnisotropy)||sel==@selector(lodMinClamp)||sel==@selector(lodMaxClamp);}
- (NSUInteger)minFilter{return mn;}- (NSUInteger)magFilter{return mg;}- (NSUInteger)mipFilter{return mip;}
- (NSUInteger)sAddressMode{return as;}- (NSUInteger)tAddressMode{return at;}- (NSUInteger)rAddressMode{return ar;}
- (signed char)normalizedCoordinates{return 1;}- (NSUInteger)compareFunction{return cmp;}
- (NSUInteger)maxAnisotropy{return aniso;}- (float)lodMinClamp{return lmin;}- (float)lodMaxClamp{return lmax;}
@end
static TeguSampler *native_sampler(TeguComputeDevice *d){NativeSamplerDesc *q=[[NativeSamplerDesc alloc] init];q->mn=q->mg=0;q->mip=0;q->as=q->at=q->ar=0;q->cmp=0;q->aniso=1;q->lmin=0;q->lmax=0;
 TeguSampler *s=[d newSamplerStateWithDescriptor:(id)q];[q release];assert(s);return s;}
static TeguTexture *native_texture(TeguComputeDevice *d,unsigned w,unsigned h,unsigned uses,id *err){TextureDesc *t=[[TextureDesc alloc] init];t->format=80;t->w=w;t->h=h;t->levels=1;t->samples=1;t->arrays=1;t->type=2;t->uses=uses;t->options=0;TeguTexture *x=[d newTextureWithDescriptor:(id)t];[t release];assert(x);(void)err;return x;}

_Static_assert(__builtin_offsetof(struct Dispatch,v2Textures) >=
               __builtin_offsetof(struct Dispatch,v2Buffers)+31*sizeof(void*),
               "v2Buffers[31] must not alias v2Textures");
int main(void){
 uintptr_t b=open_fake();
 TeguComputeDevice *d=[TeguComputeDevice alloc];d->connection=b;d->scalar=scalar_adapter;d->method=method_adapter;d->map=map_adapter;d->unmap=unmap_adapter;
 uint64_t caps[4];unsigned n=4;assert(!n2mb_scalar(b,0,0,0,caps,&n));d->capabilities=caps[1];
 TeguQueue *q=[d newCommandQueue];assert(q);id error=0;

 /* buffer-only function-created pipeline: TDG1, one submit. probe-compute.bc is
  * hand-written LLVM IR assembled by the host llvm-as, not Apple AIR, so the
  * AIR-only compute path (runtime_pipeline.inc) cannot read it: this plumbing
  * check runs on the m2v SPIR-V compile (TEGU_AIR_COMPUTE=0, fixed per pipeline
  * at creation); the real AIR kernel below runs AIR-only. */
 setenv("TEGU_AIR_COMPUTE","0",1);
 NSData *bd=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:"gpu/build/native/probe-compute.bc"]];assert(bd);
 TeguLibrary *bl=[d newTeguLibraryWithBytes:[bd bytes] length:[bd length] error:&error];assert(bl&&!error);
 TeguFunction *bf=[bl newFunctionWithName:[NSString stringWithUTF8String:"nord2_compute_probe"]];assert(bf);
 TeguPipeline *bp=[d newComputePipelineStateWithFunction:bf error:&error];assert(bp&&!error);
 TeguBuffer *bo=[d newBufferWithLength:1024 options:0];assert(bo);
 unsigned beforeSub=F.submitted,beforeWire=wireEvents;lastWireSize=0;
 TeguCommand *c1=[q commandBuffer];assert(c1);TeguEncoder *e1=[c1 computeCommandEncoder];assert(e1);
 [e1 setComputePipelineState:bp];[e1 setBuffer:bo offset:0 atIndex:0];
 [e1 dispatchThreadgroups:(MTLSize){1,1,1} threadsPerThreadgroup:(MTLSize){1,1,1}];[e1 endEncoding];assert(!c1->invalid);
 [c1 commit];[c1 waitUntilCompleted];assert([c1 status]==4&&F.submitted==beforeSub+1&&wireEvents==beforeWire+1);
 assert(lastWireSize==272&&F.req.kind==1);
 [bo release];[bp release];[bf release];[bl release];
 unsetenv("TEGU_AIR_COMPUTE");

 /* texture kernel: TDG2, used texture + unused texture still submits */
 NSData *td=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:"gpu/build/deepseek-parallel-20261025/workers/compute/air-work/fixtures/test_fast_math.air"]];assert(td);
 TeguLibrary *tl=[d newTeguLibraryWithBytes:[td bytes] length:[td length] error:&error];assert(tl&&!error);
 TeguFunction *tf=[tl newFunctionWithName:[NSString stringWithUTF8String:"TestFastMath"]];assert(tf);
 TeguPipeline *tp=[d newComputePipelineStateWithFunction:tf error:&error];assert(tp&&!error);
 TeguTexture *used=native_texture(d,16,16,1,&error);TeguTexture *unused=native_texture(d,16,16,1,&error);
 TeguBuffer *tb=[d newBufferWithLength:4096 options:0];assert(tb);TeguSampler *smp=native_sampler(d);
 beforeSub=F.submitted;beforeWire=wireEvents;lastWireSize=0;lastWireMagic=0;
 TeguCommand *c2=[q commandBuffer];assert(c2);TeguEncoder *e2=[c2 computeCommandEncoder];assert(e2);
 [e2 setComputePipelineState:tp];[e2 setBuffer:tb offset:0 atIndex:0];
 [e2 setTexture:used atIndex:0];[e2 setTexture:unused atIndex:1];[e2 setSamplerState:smp atIndex:0];
 fprintf(stderr,"PRE comp=%p count=%u\n",tp->compiled,c2->count);
 [e2 dispatchThreadgroups:(MTLSize){1,1,1} threadsPerThreadgroup:(MTLSize){8,8,1}];[e2 endEncoding];
 assert(!c2->invalid);
 [c2 commit];[c2 waitUntilCompleted];assert([c2 status]==4&&F.submitted==beforeSub+1&&wireEvents==beforeWire+1);
 assert(lastWireSize==6304&&lastWireMagic==0x54u); /* TDG2 magic low byte 'T' */
 [smp release];[tb release];[unused release];[used release];[tp release];[tf release];[tl release];

 /* v2 buffer/texture retain arrays are distinct at Metal slots9/30 and1/22. */
 {
  unsigned live0=live;
  TeguBuffer *b9=[d newBufferWithLength:64 options:0];assert(b9);
  TeguBuffer *b30=[d newBufferWithLength:64 options:0];assert(b30);
  TeguTexture *t1=native_texture(d,16,16,1,&error);TeguTexture *t22=native_texture(d,16,16,1,&error);
  TeguCommand *c3=[q commandBuffer];assert(c3);struct Dispatch *j3=tegu_command_job(c3,0);
  j3->v2Buffers[9]=[b9 retain];j3->v2Buffers[30]=[b30 retain];
  j3->v2Textures[1]=[t1 retain];j3->v2Textures[22]=[t22 retain];
  assert((void*)j3->v2Buffers[9]!=(void*)j3->v2Textures[1]);
  assert((void*)j3->v2Buffers[30]!=(void*)j3->v2Textures[22]);
  [b9 release];[b30 release];[t1 release];[t22 release];
  c3->count=1;[c3 release];
  assert(live==live0);
 }

 /* Shadow-surface texture must be visible to tegu_shadow_sync_jobs via
  * Dispatch.v2Textures, not only compositorTextures. */
 {
  IOSurfaceFixture *surface=new_surface_fixture(701,TEGU_IOSURFACE_BGRA,0,16,16,64,16384);
  for(unsigned y=0;y<16;y++)for(unsigned x=0;x<64;x++)surface->bytes[y*64+x]=(unsigned char)(y*3+x);
  TeguTexture *shadow=native_texture(d,16,16,1,&error);assert(shadow&&shadow->buffer);
  shadow->shadowSurface=(id)surface;shadow->format=80;shadow->rowBytes=64;
  memset([shadow->buffer contents],0x5a,shadow->buffer->logical);
  TeguCommand *cs=[q commandBuffer];assert(cs);struct Dispatch *js=tegu_command_job(cs,0);
  js->v2Textures[0]=[shadow retain]; unsigned long long copied=tegu_shadow_sync_jobs(&js,1,0);
  assert(copied==16u*16u*4u);
  assert(!memcmp([shadow->buffer contents],surface->bytes,16u*16u*4u));
  [shadow release];[cs release];[surface release];
 }
 puts("PASS native compute frontend: buffer-only TDG1 submit; used+unused textures TDG2 submit; slots9/30 retain distinct; shadow v2 texture synced");
 return 0;
 return 0;
}
'''
source += runtime.split("NSUInteger tegu_host_constant_data_size(NSUInteger type)", 1)[1].join(
    ["NSUInteger tegu_host_constant_data_size(NSUInteger type)", ""])

audit = (HERE / "delta6_frontend_audit.inc").read_text()
audit += (HERE / "delta9_selectors.inc").read_text()
audit += (HERE / "delta9_frontend_audit.inc").read_text()
source = source.replace("int main(void){", audit + "\nint main(void){", 1)
quartz = (GPU / "build/native/QuartzCore.metallib").resolve()
if quartz.stat().st_size != 1014944:
    raise SystemExit(f"QuartzCore metallib size changed: {quartz.stat().st_size}")
quartz_sha = hashlib.sha256(quartz.read_bytes()).hexdigest()
if quartz_sha != "11c74cc42ce5137b73d44d427f75764f886656d4ddb374422d56b609ed2762d9":
    raise SystemExit(f"QuartzCore metallib content changed: {quartz_sha}")
call = r'''
 /* Delta6: real QuartzCore library, CA descriptor constants, selector audit. */
 audit_real_quartzcore_library(d,"__QUARTZ_PATH__");
 { TeguBuffer *audit_b=[d newBufferWithLength:64 options:0];
   TeguTexture *audit_t=native_texture(d,16,16,1,&error);
   TeguCommand *audit_c=[q commandBuffer]; TeguEncoder *audit_e=[audit_c computeCommandEncoder];
   RenderPass *audit_pass=[[RenderPass alloc] init];audit_pass->color=[[PassColor alloc] init];
   audit_pass->color->target=[audit_t retain];audit_pass->color->load=2;audit_pass->color->store=1;
   audit_pass->passWidth=audit_pass->passHeight=16;audit_pass->arrayLength=1;audit_pass->sampleCount=1;
   TeguCommand *audit_rc=[q commandBuffer];
   TeguRenderEncoder *audit_re=[audit_rc renderCommandEncoderWithDescriptor:(id)audit_pass];
   if(audit_e) audit_selectors(d,audit_b,audit_t,audit_c,audit_e,audit_re);
   else { fprintf(stderr,"SELECTOR_AUDIT no compute encoder\n"); audit_missing++; }
   if(audit_re)[audit_re endEncoding];
   [audit_pass release];[audit_t release];[audit_b release];
 }
 audit_extracted_selectors();
 audit_all_metallib_functions(d,"__QUARTZ_PATH__");
 audit_missing += extracted_missing;
 if(audit_missing){fprintf(stderr,"SELECTOR_AUDIT missing=%u checks=%u extracted=%u metal_functions=%u\n",audit_missing,audit_checks,extracted_missing,metallib_total);return 1;}
 fprintf(stderr,"SELECTOR_AUDIT checks=%u missing=0\n",audit_checks);
'''
source = source.replace(' puts("PASS native compute frontend:', call + ' puts("PASS native compute frontend:', 1)
source = source.replace("__QUARTZ_PATH__", str(quartz))

OUT.parent.mkdir(parents=True, exist_ok=True)
src = OUT.with_suffix(".m")
src.write_text(source)
libs = [GPU / "build/deepseek-parallel-20261025/workers/compute/cargo-target/release",
        GPU / "build/mesa-build/src/panfrost/clc"]
cmd = ["clang", "-fblocks", "-pthread", "-O1", "-g",
       "-Wno-incompatible-pointer-types", "-Wno-incompatible-function-pointer-types",
       "-fconstant-string-class=NXConstantString"]
for p in (T, GPU / "metal", GPU / "include", GPU / "build/tegu-frontend"):
    cmd.append("-I" + str(p))
cmd += [str(src),
        str(GPU / "metal/nord2_metal_bridge.c"),
        str(GPU / "metal/nord2_metal_render.c"),
        str(GPU / "src/nord2_gpu_desc.c"),
        str(GPU / "src/nord2_gpu_jm.c"),
        "-lobjc", "-ldl"]
for p in libs:
    cmd += ["-L" + str(p)]
cmd += ["-ltegu_air_bridge", "-lnord2_mali_runtime", "-o", str(OUT)]
built = subprocess.run(cmd, capture_output=True, text=True)
if built.returncode:
    print(built.stderr)
    raise SystemExit(1)
env = dict(os.environ, LD_LIBRARY_PATH=":".join(map(str, libs)),
           ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
ran = subprocess.run([str(OUT)], capture_output=True, text=True, env=env)
print(ran.stdout.strip())
if ran.stderr.strip():
    print(ran.stderr.strip(), file=sys.stderr)
raise SystemExit(ran.returncode)
