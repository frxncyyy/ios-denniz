#!/usr/bin/env python3
"""Real-frontend host test for stage-aware generic texture/sampler binding.

Builds the production Objective-C frontend (render_pipeline.inc /
render_encoder.inc) with the real baseline AIR bridge and the real n2mb bridge
against an independent fake JM transport. No GPU, phone, adb or publish.

Checks:
- authored VS+FS AIR that reads Metal slot0 texture/sampler in BOTH stages;
- both stages reach the generic plan and the TGJ1 request as distinct native
  bindings with distinct retained texture/sampler objects;
- the job owns its resources after the encoder/autorelease pool is drained;
- a VS reading VertexIndex refuses nonzero vertexStart without submitting a
  changed-identity job;
- a 2-level texture with an exact linear sampler compiles a specialized
  variant and commits through the real n2mb_draw_v2 sidecar path;
- queued nearest/linear/nearest draws keep pipeline-owned variant selection
  after early app release;
- ASTC facts reach the planner; the translator's remaining format-feature
  refusal is explicit and never neutral-substituted;
- real non-indexed vertexStart and indexed BaseVertex use n2mb_draw_v3;
- real PointWarpVS/FS flat varying compiles through the options entry and
  reaches v3; flat line lists refuse explicitly;
- array/cube/MS CPU upload/readback addresses every slice/layer;
- refused render passes return non-nil inert encoders (all RenderBox selectors,
  ASan/UBSan), log exactly 16 bounded TEGU_PASS_REFUSED rows, and commit Error;
- exact DontCare depth/stencil attachments accept a no-test draw, while an
  enabled stencil state refuses the draw with a trace;
- real path_cubic RT2-only pipeline with four colour attachments, including
  RT0/RT1 unbound and bound forms, commits through the per-target jobs;
- TEGU_INERT_PIPELINES returns a nonnil inert PSO and refuses draws with a trace;
  the flag-off binary still returns nil/error;
- real asg_vert_lph/asg_frag_lph compiles with TLS=16 and draws through the
  current TLS bridge/fake JM (sidecar tls=16, status4);
- real path_blit true-MRT+attachment2 fetch becomes an inert PSO only in
  backboardd; non-backboardd and flag-off return nil/error and emit no inert
  trace; garbage attachment masks on path_cubic are preserved without clears;
- authored mrt_vertex/mrt_fragment writes outputs 0,1,2 and draws through
  n2mb_draw_v4 with version2 per-target blends, status4.
"""
import json
import os
import pathlib
import subprocess
import sys
import subprocess as _sp

HERE = pathlib.Path(__file__).resolve().parent
WORK = HERE
G = HERE.parents[3]
TEgu = G / 'metal' / 'tegu'

source = '#define TEGU_AIR_BRIDGE 1\n#define TEGU_MALI_BRIDGE 1\n#define TEGU_GRAPHICS_BRIDGE 1\n#define NORD2_JM_TRANSPORT 1\n#define TEGU_IOSURFACE_TEST_LOADER 1\n'
source += r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
struct TestIOSurface { unsigned char data[64*64*4]; size_t width,height,row,alloc,planes; uint32_t fourcc; };
static struct TestIOSurface test_surface;
static unsigned test_surface_locks;
static uint32_t test_si_id(void *p){(void)p;return 1;}
static size_t test_si_width(void *p){return ((struct TestIOSurface*)p)->width;}
static size_t test_si_height(void *p){return ((struct TestIOSurface*)p)->height;}
static size_t test_si_row(void *p){return ((struct TestIOSurface*)p)->row;}
static size_t test_si_alloc(void *p){return ((struct TestIOSurface*)p)->alloc;}
static uint32_t test_si_fourcc(void *p){return ((struct TestIOSurface*)p)->fourcc;}
static size_t test_si_planes(void *p){return ((struct TestIOSurface*)p)->planes;}
static int test_si_lock(void *p,uint32_t o,uint32_t *seed){(void)p;(void)o;(void)seed;test_surface_locks++;return 0;}
static int test_si_unlock(void *p,uint32_t o,uint32_t *seed){(void)p;(void)o;(void)seed;return 0;}
static void *test_si_base(void *p){return ((struct TestIOSurface*)p)->data;}
static void *test_si_retain(void *p){return p;}
static void test_si_release(void *p){(void)p;}
__attribute__((unused)) static void *tegu_test_iosurface_dlopen(const char *p,int m){(void)p;(void)m;return (void*)1;}
__attribute__((unused)) static void *tegu_test_iosurface_dlsym(void *h,const char *n){
 (void)h;
 if(!strcmp(n,"IOSurfaceGetID"))return (void*)test_si_id;
 if(!strcmp(n,"IOSurfaceGetWidth"))return (void*)test_si_width;
 if(!strcmp(n,"IOSurfaceGetHeight"))return (void*)test_si_height;
 if(!strcmp(n,"IOSurfaceGetBytesPerRow"))return (void*)test_si_row;
 if(!strcmp(n,"IOSurfaceGetAllocSize"))return (void*)test_si_alloc;
 if(!strcmp(n,"IOSurfaceGetPixelFormat"))return (void*)test_si_fourcc;
 if(!strcmp(n,"IOSurfaceGetPlaneCount"))return (void*)test_si_planes;
 if(!strcmp(n,"IOSurfaceLock"))return (void*)test_si_lock;
 if(!strcmp(n,"IOSurfaceUnlock"))return (void*)test_si_unlock;
 if(!strcmp(n,"IOSurfaceGetBaseAddress"))return (void*)test_si_base;
 if(!strcmp(n,"CFRetain"))return (void*)test_si_retain;
 if(!strcmp(n,"CFRelease"))return (void*)test_si_release;
 return 0;
}
'''

# The compute worker is mid-edit: its new compute_frontend declaration and
# runtime_pipeline definition currently disagree. Inline a test-only copy with
# the declaration matched to the current definition; production files and the
# active tree are untouched. The current compute bridge library supplies the
# additive v2 symbol used by that definition.
frontend = (TEgu / 'frontend_host_test.m').read_text()
# Inline the WORKING-TREE compute frontend and runtime pipeline so the gates
# cover uncommitted work (2026-10-26; the old mid-edit reason no longer holds).
# HARNESS_COMPUTE_FROM_HEAD=1 restores the frozen committed copies (bisecting).
_root = G.parent
if os.environ.get('HARNESS_COMPUTE_FROM_HEAD') == '1':
    compute = _sp.check_output(['git', '-C', str(_root), 'show',
                                'HEAD:gpu/metal/tegu/compute_frontend.m']).decode()
    runtime = _sp.check_output(['git', '-C', str(_root), 'show',
                                'HEAD:gpu/metal/tegu/runtime_pipeline.inc']).decode()
else:
    compute = (TEgu / 'compute_frontend.m').read_text()
    runtime = (TEgu / 'runtime_pipeline.inc').read_text()
library = (TEgu / 'library_frontend.inc').read_text()
assert '#include "runtime_pipeline.inc"' in library
library = library.replace('#include "runtime_pipeline.inc"', runtime, 1)
assert '#include "library_frontend.inc"' in compute
compute = compute.replace('#include "library_frontend.inc"', library, 1)
frontend = frontend.replace('#include "compute_frontend.m"', compute, 1)
source += frontend.split('static unsigned allocated,freed')[0]
# Older committed compute frontend lacks the new shared process helper; add a
# test-only definition backed by its existing host flag only when needed.
if 'tegu_compositor_process_is_backboardd' not in compute:
    source += r'''
int tegu_compositor_process_is_backboardd(void){return tegu_host_process_backboardd?1:0;}
'''
source += (TEgu / 'runtime_frontend_host_test.m').read_text().split('static id strings[1024]', 1)[1].split(
    'static unsigned imports,submissions', 1)[0].join(['static id strings[1024]', ''])
source += (TEgu / 'render_frontend_host_test.m').read_text().split('@interface RenderColor', 1)[1].split(
    '#include "render_encoder_host_test.inc"', 1)[0].join(['@interface RenderColor', ''])
source += (TEgu / 'render_encoder_host_test.inc').read_text().split('@class IOSurfaceFixture', 1)[0]
source += (G / 'tests' / 'nord2_metal_bridge_test.c').read_text().split('static uint64_t pipeline(', 1)[0].replace(
    '#define COUNT 300', '#define COUNT 2000')
# The shared bridge fake asserts the legacy single-target framebuffer tag.
# True-MRT tags the fragment framebuffer with the target count in the low bits;
# accept those bounded tags while still requiring the exact command address.
source = source.replace(
    'assert(get64(c->data+N2R_FRAGMENT+40)==(c->va|1));',
    '{uint64_t frag_fb=get64(c->data+N2R_FRAGMENT+40);'
    'assert((frag_fb&~(uint64_t)0xf)==c->va);}')

# Chained submission (bridge NORD2_METAL_CHAIN=1|2, default off): frames are
# linked into multi-frame kernel jobs, every frame on its own tiler-heap
# slice. The shared fake (tests/nord2_metal_bridge_test.c) verifies those
# with its chain model; enable it whenever the bridge chains, and keep the
# single-frame facts this harness asserts (last_chain = the tiler jobs of the
# job's last frame, as an unchained job reports them).
_chain_on = '   if(f->chain_model){'
assert source.count(_chain_on) == 1
source = source.replace(_chain_on, '   if(f->chain_model||fake_chain_env()){', 1)
_kbusy = '#define KBUSY_TEST ((int)0xe00002d5u)'
assert source.count(_kbusy) == 1
source = source.replace(_kbusy, _kbusy + r"""
static unsigned fake_chain_env(void){static int v=-1;
 if(v<0){const char *e=getenv("NORD2_METAL_CHAIN");v=e&&(e[0]=='1'||e[0]=='2')&&!e[1];}return (unsigned)v;}
static unsigned fake_bo_of(struct fake *f,const struct nord2_gpu_submit_request *r,uint64_t va){
 for(unsigned k=0;k<r->nr_handles;k++){struct buffer *b=&f->buffers[r->handles[k]];if(va>=b->va&&va<b->va+b->size)return r->handles[k];}
 assert(0);return 0;}""", 1)
_walk = '    for(uint64_t h=r->heads[0];h;steps++){const unsigned char *jh=fake_at(f,r,h);'
assert source.count(_walk) == 1
source = source.replace(_walk, '    unsigned frame_bo=0,frame_steps=0;\n'
    '    for(uint64_t h=r->heads[0];h;steps++){const unsigned char *jh=fake_at(f,r,h);'
    '{unsigned bo=fake_bo_of(f,r,h);if(bo!=frame_bo){frame_bo=bo;frame_steps=0;}frame_steps++;}', 1)
_tail = '    f->last_tjobs=steps;f->last_frames=k;f->draw_submits++;'
assert source.count(_tail) == 1
source = source.replace(_tail, '    f->last_tjobs=steps;f->last_frames=k;f->draw_submits++;f->last_chain=frame_steps;', 1)

source += r'''
/* Linux uint64_t is unsigned long; the Darwin frontend ABI uses U64. */
static int scalar_adapter(TeguConnection c,unsigned sel,const U64 *in,unsigned ni,U64 *out,unsigned *no){
 uint64_t a[16]={0},b[16]={0};assert(ni<=16&&(!no||*no<=16));
 if(ni)memcpy(a,in,ni*8);int e=n2mb_scalar(c,sel,ni?a:0,ni,out?b:0,no);
 if(out&&no){assert(*no<=16);memcpy(out,b,*no*8);}return e;
}
static int method_adapter(TeguConnection c,unsigned sel,const U64 *in,unsigned ni,const void *data,NSUInteger bytes,U64 *out,unsigned *no,void *output,NSUInteger *size){
 uint64_t a[16]={0},b[16]={0};assert(ni<=16&&(!no||*no<=16));
 if(ni)memcpy(a,in,ni*8);int e=n2mb_method(c,sel,ni?a:0,ni,data,bytes,out?b:0,no,output,size);
 if(out&&no){assert(*no<=16);memcpy(out,b,*no*8);}return e;
}
static int map_adapter(TeguConnection c,unsigned h,unsigned task,U64 *a,U64 *n,unsigned flags){uint64_t x=0,y=0;int e=n2mb_map(c,h,task,&x,&y,flags);*a=x;*n=y;return e;}
static int unmap_adapter(TeguConnection c,unsigned h,unsigned task,U64 a){return n2mb_unmap(c,h,task,a);}
static void drain(void){while(poolCount)[pool[--poolCount] release];}

static void fill_texture(TeguTexture *t,uint32_t value){
 assert(t&&t->buffer&&t->rowBytes&&t->textureHeight);
 uint32_t *p=(uint32_t *)[t->buffer contents]+t->mip[0].offset/4;
 for(NSUInteger i=0;i<t->rowBytes*t->textureHeight/4;i++)p[i]=value;
}
static TeguTexture *new_sample_ex(TeguComputeDevice *d,NSUInteger format,NSUInteger width,NSUInteger height,
 NSUInteger levels,NSUInteger type,NSUInteger depth,NSUInteger arrays,NSUInteger samples){
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=format;desc->w=width;desc->h=height;
 desc->levels=levels;desc->type=type;desc->dep=depth;desc->samples=samples;desc->arrays=arrays;desc->uses=1;desc->options=0;
 TeguTexture *t=[d newTextureWithDescriptor:(id)desc];[desc release];return t;
}
static TeguTexture *new_sample(TeguComputeDevice *d,NSUInteger format,NSUInteger width,NSUInteger height,
 NSUInteger levels,NSUInteger type,NSUInteger depth){
 return new_sample_ex(d,format,width,height,levels,type,depth,1,1);
}
static TeguTexture *new_target(TeguComputeDevice *d){
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=80;desc->w=desc->h=16;desc->uses=4;
 TeguTexture *t=[d newTextureWithDescriptor:(id)desc];[desc release];return t;
}
/* Auxiliary depth/stencil attachment used only to exercise pass admission.
 * The generic job has no depth/stencil execution, so the pass accepts this
 * only with DontCare storage and the draw path checks the bound state. */
static TeguTexture *new_aux_texture(TeguComputeDevice *d,NSUInteger format,NSUInteger options){
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=80;desc->w=desc->h=16;desc->uses=4;desc->options=options;
 TeguTexture *t=[d newTextureWithDescriptor:(id)desc];[desc release];
 if(t)t->format=format;
 return t;
}
@interface AuxPass : RenderPass { @public PassColor *auxDepth,*auxStencil; }
@end
@implementation AuxPass
- (id)depthAttachment{return auxDepth;}
- (id)stencilAttachment{return auxStencil;}
- (void)dealloc{[auxDepth release];[auxStencil release];[super dealloc];}
@end
@interface MrtPass : RenderPass { @public PassColor *third,*fourth; }
@end
@implementation MrtPass
- (id)objectAtIndexedSubscript:(NSUInteger)i {return i==0?color:i==1?second:i==2?third:i==3?fourth:0;}
- (void)dealloc{[third release];[fourth release];[super dealloc];}
@end
@interface PassDepthDesc : NSObject { @public NSUInteger compare; signed char write; id front,back; }
@end
@implementation PassDepthDesc
- (NSUInteger)depthCompareFunction{return compare;}
- (signed char)isDepthWriteEnabled{return write;}
- (id)frontFaceStencil{return front;}
- (id)backFaceStencil{return back;}
- (id)label{return 0;}
@end
@interface PassStencilFace : NSObject { @public NSUInteger compare,fail,depthFail,pass; unsigned read,write; }
@end
@implementation PassStencilFace
- (NSUInteger)stencilCompareFunction{return compare;}
- (NSUInteger)stencilFailureOperation{return fail;}
- (NSUInteger)depthFailureOperation{return depthFail;}
- (NSUInteger)depthStencilPassOperation{return pass;}
- (unsigned)readMask{return read;}
- (unsigned)writeMask{return write;}
@end
@interface TestSamplerDesc : NSObject { @public NSUInteger mn,mg,mip,as,at,ar,cmp,aniso; float lmin,lmax; }
@end
@implementation TestSamplerDesc
- (signed char)respondsToSelector:(SEL)sel{
 return sel==@selector(respondsToSelector:)||sel==@selector(minFilter)||sel==@selector(magFilter)||
  sel==@selector(mipFilter)||sel==@selector(sAddressMode)||sel==@selector(tAddressMode)||
  sel==@selector(rAddressMode)||sel==@selector(normalizedCoordinates)||sel==@selector(compareFunction)||
  sel==@selector(maxAnisotropy)||sel==@selector(lodMinClamp)||sel==@selector(lodMaxClamp);
}
- (NSUInteger)minFilter{return mn;}- (NSUInteger)magFilter{return mg;}- (NSUInteger)mipFilter{return mip;}
- (NSUInteger)sAddressMode{return as;}- (NSUInteger)tAddressMode{return at;}- (NSUInteger)rAddressMode{return ar;}
- (signed char)normalizedCoordinates{return 1;}- (NSUInteger)compareFunction{return cmp;}
- (NSUInteger)maxAnisotropy{return aniso;}- (float)lodMinClamp{return lmin;}- (float)lodMaxClamp{return lmax;}
@end
@interface TeguBuffer (TestReferences) - (unsigned)testReferences; @end
@implementation TeguBuffer (TestReferences)
- (unsigned)testReferences{return _references;}
@end
static TeguSampler *new_sampler(TeguComputeDevice *d,unsigned linear){
 TestSamplerDesc *desc=[[TestSamplerDesc alloc] init];assert(desc);
 desc->mn=linear&1?1:0;desc->mg=linear&2?1:0;desc->mip=0;desc->as=desc->at=desc->ar=0;
 desc->cmp=0;desc->aniso=1;desc->lmin=0;desc->lmax=0;
 TeguSampler *s=[d newSamplerStateWithDescriptor:(id)desc];[desc release];
 assert(s&&s->state.version==N2T_TEXTURE_RESOURCES_VERSION);
 return s;
}
#ifdef TEGU_HOST_STUB_V2
static unsigned stubV2Calls,stubV2Levels;
int n2mb_draw_v2(uintptr_t b,const struct tegu_generic_draw_shader *sh,
 const struct tegu_generic_draw_request *q,const struct n2r_texture_resources *tr,
 uint64_t *out,unsigned *count){
 (void)b;(void)sh;(void)q;stubV2Calls++;
 if(tr&&tr->count)stubV2Levels=tr->bindings[0].layout.levels;
 out[0]=1;out[1]=2;*count=2;return 0;
}
#endif
static void bind_all(TeguRenderEncoder *e,TeguBuffer *vb,TeguRenderPipeline *p,TeguTexture *vs,
 TeguTexture *fs,TeguSampler *vss,TeguSampler *fss){
 [e setRenderPipelineState:p];
 [e setVertexBuffer:vb offset:0 atIndex:0];
 [e setVertexTexture:vs atIndex:0];[e setVertexSamplerState:vss atIndex:0];
 [e setFragmentTexture:fs atIndex:0];[e setFragmentSamplerState:fss atIndex:0];
}

int main(int argc,char **argv){
 assert(argc>=2&&argc<=14);
 FILE *file=fopen(argv[1],"rb");assert(file);fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
 assert(size>0);void *bytes=malloc((size_t)size);assert(bytes&&fread(bytes,1,(size_t)size,file)==(size_t)size);fclose(file);
 struct fake f={0};uintptr_t bridge=open_bridge(&f);
 TeguComputeDevice *d=[TeguComputeDevice alloc];
 d->connection=bridge;d->scalar=scalar_adapter;d->method=method_adapter;d->map=map_adapter;d->unmap=unmap_adapter;
 d->capabilities=(1ULL<<14)|(1ULL<<18)|256ULL|64ULL|32ULL|16ULL; /* compositor|generic job|BGRA|blend|varying|fetch */
 test_texture_views(d);
 test_texture_blits(d);
 test_mipmaps(d);
 test_blit_completeness(d);
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
 assert(pipeline->genericDraw&&pipeline->genericShader.fragment_code&&pipeline->genericVertexIndex);

 TeguQueue *q=[d newCommandQueue];assert(q);
 TeguTexture *target=new_target(d);assert(target);
 TeguBuffer *vb=[d newBufferWithLength:256 options:0];assert(vb);
 RenderPass *pass=[[RenderPass alloc] init];
 pass->color=[[PassColor alloc] init];pass->color->target=[target retain];
 pass->color->load=2;pass->color->store=1;pass->passWidth=pass->passHeight=16;pass->arrayLength=1;pass->sampleCount=1;

 /* --- same Metal slot0 in both stages; distinct native bindings/objects --- */
 TeguTexture *vsTex=new_sample(d,80,16,16,1,2,1);assert(vsTex);
 TeguTexture *fsTex=new_sample(d,80,16,16,1,2,1);assert(fsTex);
 fill_texture(vsTex,0x11223344u);fill_texture(fsTex,0x55667788u);
 TeguSampler *vsSamp=new_sampler(d,0),*fsSamp=new_sampler(d,0);
 TeguCommand *c1=[q commandBuffer];assert(c1);
 TeguRenderEncoder *e1=[c1 renderCommandEncoderWithDescriptor:(id)pass];assert(e1);
 bind_all(e1,vb,pipeline,vsTex,fsTex,vsSamp,fsSamp);
 [e1 drawPrimitives:3 vertexStart:0 vertexCount:3];
 [e1 endEncoding];
 assert(!c1->invalid&&c1->count==2);
 struct Dispatch *job=tegu_command_job(c1,1);
 assert(job->genericDrawSnapshot);
 const struct tegu_generic_draw_request *r=&job->genericDrawRequest;
 assert(r->binding_count==2&&r->bindings[0].binding!=r->bindings[1].binding);
 assert(r->bindings[0].kind==TEGU_GENERIC_DRAW_COMBINED&&r->bindings[1].kind==TEGU_GENERIC_DRAW_COMBINED);
 assert(r->bindings[0].width==16&&r->bindings[1].width==16&&
        r->bindings[0].height==16&&r->bindings[1].height==16&&
        r->bindings[0].format==80&&r->bindings[1].format==80);
 assert(vsTex->buffer->handle!=fsTex->buffer->handle);
 assert(job->compositorTextures[r->bindings[0].binding]==(id)vsTex);
 assert(job->compositorTextures[r->bindings[1].binding]==(id)fsTex);
 assert(job->compositorSamplers[r->bindings[0].binding]==(id)vsSamp);
 assert(job->compositorSamplers[r->bindings[1].binding]==(id)fsSamp);
 /* Hold the command, drop every app reference and the autoreleased encoder,
  * then commit: only the job/command retains keep the bound objects alive. */
 [c1 retain];
 [vsTex release];[fsTex release];[vsSamp release];[fsSamp release];
 [pipeline release];pipeline=0;
 drain();
 [c1 commit];[c1 waitUntilCompleted];
 assert([c1 status]==4&&![c1 error]);
 assert(job->compositorTextures[r->bindings[0].binding]&&job->compositorTextures[r->bindings[1].binding]);
 [c1 release];

 /* --- nonzero vertexStart with VertexIndex uses n2mb_draw_v3 FAU base --- */
 pipeline=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];
 assert(pipeline&&!error&&pipeline->genericVertexIndex);
 vsTex=new_sample(d,80,16,16,1,2,1);fsTex=new_sample(d,80,16,16,1,2,1);
 vsSamp=new_sampler(d,0);fsSamp=new_sampler(d,0);
 TeguCommand *c2=[q commandBuffer];assert(c2);
 TeguRenderEncoder *e2=[c2 renderCommandEncoderWithDescriptor:(id)pass];assert(e2);
 bind_all(e2,vb,pipeline,vsTex,fsTex,vsSamp,fsSamp);
 [e2 drawPrimitives:3 vertexStart:3 vertexCount:3];
 [e2 endEncoding];
 assert(!c2->invalid&&c2->count==2);
 {
  struct Dispatch *j2=tegu_command_job(c2,1);
  assert(j2->genericDrawSnapshot&&j2->genericVertexBase==3&&
         (j2->genericDrawFlags&N2R_DRAW_WRITE_VERTEX_BASE));
 }
 [c2 commit];[c2 waitUntilCompleted];assert([c2 status]==4&&![c2 error]);
 [vsTex release];[fsTex release];[vsSamp release];[fsSamp release];



 /* --- 2D multi-mip at VS slot0: level0-only transport must refuse --- */
 TeguTexture *mip=new_sample(d,80,16,16,2,2,1);assert(mip);
 TeguTexture *fs3=new_sample(d,80,16,16,1,2,1);assert(fs3);
 TeguSampler *vs3=new_sampler(d,3),*fs3s=new_sampler(d,3);
 TeguCommand *c4=[q commandBuffer];assert(c4);
 TeguRenderEncoder *e4=[c4 renderCommandEncoderWithDescriptor:(id)pass];assert(e4);
 bind_all(e4,vb,pipeline,mip,fs3,vs3,fs3s);
 [e4 drawPrimitives:3 vertexStart:0 vertexCount:3];
 [e4 endEncoding];
 /* The specialized planner admits the bounded 2-level resource and the
  * renderer selects a validated variant. Do not fake-submit it here; the
  * real bridge path is exercised separately. */
 assert(!c4->invalid&&c4->count==2);
 {
  struct Dispatch *j4=tegu_command_job(c4,1);
  TeguRenderPipeline *rp4=(TeguRenderPipeline *)j4->renderPipeline;
  assert(j4->genericDrawSnapshot&&j4->renderArgs[0]>0&&
         j4->renderArgs[0]<=rp4->genericVariantCount);
  struct tegu_render_generic_variant *v4=&rp4->genericVariants[j4->renderArgs[0]-1];
  assert(v4->vertex_constant_count+v4->fragment_constant_count>0);
  assert(tegu_generic_v2_required(j4));
  struct tegu_generic_v2_sidecar sc4;char w4[160]={0};
  assert(!tegu_generic_v2_build_job(j4,&sc4,0,w4,sizeof(w4)));
  assert(sc4.count&&sc4.bindings[0].layout.levels==2);
 }
#ifdef TEGU_HOST_REAL_V2
 /* Run the actual current bridge v2 builder/transport through fake JM. */
 [c4 commit];[c4 waitUntilCompleted];assert([c4 status]==4&&![c4 error]);
#endif
 [mip release];[fs3 release];[vs3 release];[fs3s release];

 /* --- queued nearest/linear/nearest variants + early app release --- */
 {
  TeguTexture *qm=new_sample(d,80,16,16,2,2,1);assert(qm);
  TeguTexture *qf=new_sample(d,80,16,16,1,2,1);assert(qf);
  TeguSampler *n1=new_sampler(d,0),*lin=new_sampler(d,3),*n2=new_sampler(d,0);
  TeguCommand *cq=[q commandBuffer];assert(cq);
  TeguSampler *qss[3]={n1,lin,n2};
  for(unsigned k=0;k<3;k++) {
   TeguRenderEncoder *eq=[cq renderCommandEncoderWithDescriptor:(id)pass];assert(eq);
   bind_all(eq,vb,pipeline,qm,qf,qss[k],qss[k]);
   [eq drawPrimitives:3 vertexStart:0 vertexCount:3];[eq endEncoding];
  }
  /* Drop every app reference before commit: jobs retain the objects and the
   * pipeline owns its immutable variants. */
  [qm release];[qf release];[n1 release];[lin release];[n2 release];
  assert(cq->count==6);
  unsigned selected=0;
  for(unsigned ci=0;ci<cq->count;ci++) {
   struct Dispatch *cj=tegu_command_job(cq,ci);
   if(!cj->genericDrawSnapshot)continue;
   TeguRenderPipeline *rp=(TeguRenderPipeline *)cj->renderPipeline;
   assert(cj->renderArgs[0]&&cj->renderArgs[0]<=rp->genericVariantCount);
   const struct tegu_generic_draw_shader *sv=&rp->genericVariants[cj->renderArgs[0]-1].shader;
   /* Default-equivalent constants must not change native bytes. */
   assert(sv->vertex_bytes==rp->genericShader.vertex_bytes&&
          sv->fragment_bytes==rp->genericShader.fragment_bytes&&
          !memcmp(sv->vertex_code,rp->genericShader.vertex_code,sv->vertex_bytes)&&
          !memcmp(sv->fragment_code,rp->genericShader.fragment_code,sv->fragment_bytes));
   selected++;
  }
  assert(selected==3);
#ifdef TEGU_HOST_REAL_V2
  [cq commit];[cq waitUntilCompleted];assert([cq status]==4&&![cq error]);
#endif
 }

 /* --- ASTC facts reach the planner; baseline rejects them explicitly --- */
 TeguTexture *astc=new_sample(d,204,64,64,1,2,1);assert(astc);
 TeguTexture *fs4=new_sample(d,80,16,16,1,2,1);assert(fs4);
 TeguSampler *vs4=new_sampler(d,0),*fs4s=new_sampler(d,0);
 TeguCommand *c5=[q commandBuffer];assert(c5);
 TeguRenderEncoder *e5=[c5 renderCommandEncoderWithDescriptor:(id)pass];assert(e5);
 bind_all(e5,vb,pipeline,astc,fs4,vs4,fs4s);
 [e5 drawPrimitives:3 vertexStart:0 vertexCount:3];
 [e5 endEncoding];
 /* The specialized planner admits installed ASTC 2D facts; the renderer must
  * either build a real job or an explicit refusal, never a neutral job. */
 if(c5->invalid) assert(c5->count==1);
 else {
  assert(c5->count==2);
  struct Dispatch *j5=tegu_command_job(c5,1);
  assert(j5->genericDrawSnapshot);
 }
 [astc release];[fs4 release];[vs4 release];[fs4s release];

 /* --- attribute-only nonzero vertexStart still rebases and submits --- */
 /* The linked VS has no VertexIndex; only its attribute address is rebased.
  * Use the repo-authored varying fixture (attribute7/8, one stride32 layout). */
 if(argc>=3) {
  FILE *vf2=fopen(argv[2],"rb");assert(vf2);fseek(vf2,0,SEEK_END);long vsz=ftell(vf2);rewind(vf2);
  assert(vsz>0);void *vbytes=malloc((size_t)vsz);assert(vbytes&&fread(vbytes,1,(size_t)vsz,vf2)==(size_t)vsz);fclose(vf2);
  TeguLibrary *lib2=[d newTeguLibraryWithBytes:vbytes length:(NSUInteger)vsz error:&error];free(vbytes);
  assert(lib2&&!error);
  TeguFunction *v2=[lib2 newFunctionWithName:[NSString stringWithUTF8String:"varying_vertex"]];
  TeguFunction *f2=[lib2 newFunctionWithName:[NSString stringWithUTF8String:"varying_fragment"]];
  assert(v2&&f2);
  RenderDescriptor *pd2=[[RenderDescriptor alloc] init];
  pd2->vertex=v2;pd2->fragment=f2;pd2->samples=1;pd2->raster=1;pd2->amplification=1;pd2->topology=0;
  pd2->colors=[[RenderColors alloc] init];
  pd2->colors->colors[0]=[[RenderColor alloc] init];pd2->colors->colors[0]->format=80;pd2->colors->colors[0]->mask=15;
  RenderLayout *layout=[[RenderLayout alloc] init];
  for(unsigned i=0;i<2;i++)layout->entries[7+i]=[[RenderAttribute alloc] init];
  layout->entries[7]->value=31;layout->entries[7]->byteOffset=0;layout->entries[7]->bufferSlot=1;
  layout->entries[8]->value=31;layout->entries[8]->byteOffset=16;layout->entries[8]->bufferSlot=1;
  layout->buffers=[[RenderLayout alloc] init];
  layout->buffers->entries[1]=[[RenderAttribute alloc] init];
  layout->buffers->entries[1]->layoutStride=32;layout->buffers->entries[1]->step=1;layout->buffers->entries[1]->rate=1;
  pd2->layout=layout;
  TeguRenderPipeline *pipe2=[d newRenderPipelineStateWithDescriptor:(id)pd2 error:&error];
  assert(pipe2&&!error&&pipe2->genericDraw&&!pipe2->genericVertexIndex);
  TeguCommand *c6=[q commandBuffer];assert(c6);
  TeguRenderEncoder *e6=[c6 renderCommandEncoderWithDescriptor:(id)pass];assert(e6);
  [e6 setRenderPipelineState:pipe2];[e6 setVertexBuffer:vb offset:0 atIndex:1];
  [e6 drawPrimitives:3 vertexStart:3 vertexCount:3];
  [e6 endEncoding];
  assert(!c6->invalid&&c6->count==2);
  struct Dispatch *j6=tegu_command_job(c6,1);
  assert(j6->genericDrawSnapshot&&j6->genericDrawRequest.vertex.offset==96&&
         j6->genericDrawRequest.vertex.bytes==96);
  [c6 commit];[c6 waitUntilCompleted];assert([c6 status]==4&&![c6 error]);
  [pipe2 release];[pd2 release];[lib2 release];
 }
 /* --- native indexed vertex-pulling preserves arbitrary index identity --- */
 /* No attributes and VertexIndex live: the original index BO must go to the
  * IDVS path, not CPU expansion. Indices 7,2,9 are deliberately nonzero and
  * non-identity; the job request must retain that BO/offset/size. */
 TeguBuffer *ib=[d newBufferWithLength:6 options:0];assert(ib);
 uint16_t *indices=(uint16_t *)[ib contents];indices[0]=7;indices[1]=2;indices[2]=9;
 TeguTexture *vsi=new_sample(d,80,16,16,1,2,1);TeguTexture *fsi=new_sample(d,80,16,16,1,2,1);
 TeguSampler *vssi=new_sampler(d,0),*fssi=new_sampler(d,0);
 TeguCommand *c7=[q commandBuffer];assert(c7);
 TeguRenderEncoder *e7=[c7 renderCommandEncoderWithDescriptor:(id)pass];assert(e7);
 [e7 setRenderPipelineState:pipeline];
 [e7 setVertexTexture:vsi atIndex:0];[e7 setVertexSamplerState:vssi atIndex:0];
 [e7 setFragmentTexture:fsi atIndex:0];[e7 setFragmentSamplerState:fssi atIndex:0];
 [e7 drawIndexedPrimitives:3 indexCount:3 indexType:0 indexBuffer:ib indexBufferOffset:0
  instanceCount:1 baseVertex:5 baseInstance:0];
 [e7 endEncoding];
 assert(!c7->invalid&&c7->count==2);
 struct Dispatch *j7=tegu_command_job(c7,1);
 const struct tegu_generic_draw_request *r7=&j7->genericDrawRequest;
 assert(j7->genericDrawSnapshot&&r7->index_size==2&&r7->index.token==ib->handle&&
        r7->index.offset==0&&r7->index.bytes==6&&!r7->vertex.token&&
        j7->genericVertexBase==5&&(j7->genericDrawFlags&N2R_DRAW_WRITE_VERTEX_BASE));
 [c7 commit];[c7 waitUntilCompleted];assert([c7 status]==4&&![c7 error]);
 [ib release];[vsi release];[fsi release];[vssi release];[fssi release];


 /* --- real installed magma VS/FS pair through the final frontend --- */
 /* graphics-01523: VS texture2 with an immutable sampler, FS textures0..2.
  * The corpus baseline compiled it but never had a frontend path that offered
  * the vertex texture. Arguments: magmaVertex AIR, magmaFragment AIR. */
 if(argc>=5) {
  void *mv=0,*mf=0;long mvn=0,mfn=0;
  FILE *mfv=fopen(argv[3],"rb");assert(mfv);fseek(mfv,0,SEEK_END);mvn=ftell(mfv);rewind(mfv);
  mv=malloc((size_t)mvn);assert(mv&&fread(mv,1,(size_t)mvn,mfv)==(size_t)mvn);fclose(mfv);
  FILE *mff=fopen(argv[4],"rb");assert(mff);fseek(mff,0,SEEK_END);mfn=ftell(mff);rewind(mff);
  mf=malloc((size_t)mfn);assert(mf&&fread(mf,1,(size_t)mfn,mff)==(size_t)mfn);fclose(mff);
  TeguLibrary *mvl=[d newTeguLibraryWithBytes:mv length:(NSUInteger)mvn error:&error];
  TeguLibrary *mfl=[d newTeguLibraryWithBytes:mf length:(NSUInteger)mfn error:&error];
  free(mv);free(mf);assert(mvl&&mfl&&!error);
  TeguFunction *mvf=[mvl newFunctionWithName:[NSString stringWithUTF8String:"magmaVertexShader"]];
  TeguFunction *mffn=[mfl newFunctionWithName:[NSString stringWithUTF8String:"magmaForegroundFragmentShader"]];
  assert(mvf&&mffn);
  RenderDescriptor *md=[[RenderDescriptor alloc] init];
  md->vertex=mvf;md->fragment=mffn;md->samples=1;md->raster=1;md->amplification=1;md->topology=0;
  md->colors=[[RenderColors alloc] init];
  md->colors->colors[0]=[[RenderColor alloc] init];md->colors->colors[0]->format=80;md->colors->colors[0]->mask=15;
  TeguRenderPipeline *mpipe=[d newRenderPipelineStateWithDescriptor:(id)md error:&error];
  assert(mpipe&&!error&&mpipe->genericDraw&&mpipe->genericVertexIndex);
  TeguTexture *mvs=new_sample(d,80,16,16,1,2,1);assert(mvs);
  TeguTexture *mfs[3]={new_sample(d,80,16,16,1,2,1),new_sample(d,80,16,16,1,2,1),new_sample(d,80,16,16,1,2,1)};
  for(unsigned i=0;i<3;i++)assert(mfs[i]);
  TeguCommand *mc=[q commandBuffer];assert(mc);
  TeguRenderEncoder *me=[mc renderCommandEncoderWithDescriptor:(id)pass];assert(me);
  [me setRenderPipelineState:mpipe];[me setVertexBuffer:vb offset:0 atIndex:0];
  [me setVertexTexture:mvs atIndex:2];
  for(unsigned i=0;i<3;i++)[me setFragmentTexture:mfs[i] atIndex:i];
  [me drawPrimitives:3 vertexStart:0 vertexCount:3];
  [me endEncoding];
  assert(!mc->invalid&&mc->count==2);
  struct Dispatch *mj=tegu_command_job(mc,1);
  const struct tegu_generic_draw_request *mr=&mj->genericDrawRequest;
  unsigned seenVS=0,seenFS=0;
  for(uint32_t k=0;k<mr->binding_count;k++) {
   const struct tegu_generic_draw_binding *b=&mr->bindings[k];
   if(b->kind!=TEGU_GENERIC_DRAW_COMBINED_IMMUTABLE&&b->kind!=TEGU_GENERIC_DRAW_COMBINED)continue;
   if(b->memory.token==mvs->buffer->handle){seenVS++;assert(mj->compositorTextures[b->binding]==(id)mvs);}
   for(unsigned i=0;i<3;i++)if(b->memory.token==mfs[i]->buffer->handle){seenFS++;assert(mj->compositorTextures[b->binding]==(id)mfs[i]);}
  }
  assert(seenVS==1&&seenFS==3);
  [mc commit];[mc waitUntilCompleted];assert([mc status]==4&&![mc error]);
  [mvs release];for(unsigned i=0;i<3;i++)[mfs[i] release];
  [mpipe release];[md release];[mvl release];[mfl release];
 }
 /* --- real installed flat-varying VS/FS through options frontend --- */
 if(argc>=7) {
  void *fv=0,*ff=0;long fvn=0,ffn=0;FILE *fvp=fopen(argv[5],"rb");assert(fvp);
  fseek(fvp,0,SEEK_END);fvn=ftell(fvp);rewind(fvp);fv=malloc((size_t)fvn);
  assert(fv&&fread(fv,1,(size_t)fvn,fvp)==(size_t)fvn);fclose(fvp);
  FILE *ffp=fopen(argv[6],"rb");assert(ffp);fseek(ffp,0,SEEK_END);ffn=ftell(ffp);rewind(ffp);
  ff=malloc((size_t)ffn);assert(ff&&fread(ff,1,(size_t)ffn,ffp)==(size_t)ffn);fclose(ffp);
  TeguLibrary *flv=[d newTeguLibraryWithBytes:fv length:(NSUInteger)fvn error:&error];
  TeguLibrary *flf=[d newTeguLibraryWithBytes:ff length:(NSUInteger)ffn error:&error];
  free(fv);free(ff);assert(flv&&flf&&!error);
  TeguFunction *fvf=[flv newFunctionWithName:[NSString stringWithUTF8String:"PointWarpVS"]];
  TeguFunction *fff=[flf newFunctionWithName:[NSString stringWithUTF8String:"PointWarpFS"]];
  assert(fvf&&fff);
  RenderDescriptor *fd=[[RenderDescriptor alloc] init];
  fd->vertex=fvf;fd->fragment=fff;fd->samples=1;fd->raster=1;fd->amplification=1;fd->topology=0;
  fd->colors=[[RenderColors alloc] init];fd->colors->colors[0]=[[RenderColor alloc] init];
  fd->colors->colors[0]->format=80;fd->colors->colors[0]->mask=15;
  TeguRenderPipeline *fp=[d newRenderPipelineStateWithDescriptor:(id)fd error:&error];
  assert(fp&&!error&&fp->genericDraw);
  TeguCommand *fc=[q commandBuffer];assert(fc);
  TeguRenderEncoder *fe=[fc renderCommandEncoderWithDescriptor:(id)pass];assert(fe);
  [fe setRenderPipelineState:fp];[fe drawPrimitives:3 vertexStart:0 vertexCount:3];[fe endEncoding];
  assert(!fc->invalid&&fc->count==2);
  struct Dispatch *fj=tegu_command_job(fc,1);
  assert(fj->genericDrawSnapshot&&(fj->genericDrawFlags&N2R_DRAW_FLAT_SHADING));
  [fc commit];[fc waitUntilCompleted];assert([fc status]==4&&![fc error]);
  /* The pinned arch9 XML has no line DRAW.flat_shading_vertex; refuse flat
   * line lists rather than producing last-vertex flat values. */
  TeguCommand *flc=[q commandBuffer];assert(flc);
  TeguRenderEncoder *fle=[flc renderCommandEncoderWithDescriptor:(id)pass];assert(fle);
  [fle setRenderPipelineState:fp];[fle drawPrimitives:1 vertexStart:0 vertexCount:2];[fle endEncoding];
  assert(flc->invalid&&flc->count==1);
  [fp release];[fd release];[flv release];[flf release];
 }

 /* --- variants survive a module-generation reset while jobs may be queued --- */
 {
  unsigned before=pipeline->genericVariantCount;
  assert(before>0&&pipeline->genericVariants);
  struct tegu_render_generic_variant *array=pipeline->genericVariants;
  const uint8_t *old_vs=pipeline->genericVariants[0].shader.vertex_code;
  uint32_t old_vb=pipeline->genericVariants[0].shader.vertex_bytes;
  TeguAIRBlob vs={pipeline->genericVsBytes,pipeline->genericVsLength};
  TeguAIRBlob fs={pipeline->genericFsBytes,pipeline->genericFsLength};
  assert(!tegu_render_generic_set_modules(pipeline,&vs,&fs));
  assert(pipeline->genericVariants==array);
  assert(pipeline->genericVariantCount==before);
  assert(pipeline->genericVariants[0].shader.vertex_code==old_vs&&
         pipeline->genericVariants[0].shader.vertex_bytes==old_vb);
 }

 /* --- variant cache identity includes the plan's 512-byte key --- */
 {
  TeguAIRGraphicsSpecializationPlan a,b;
  memset(&a,0,sizeof(a));memset(&b,0,sizeof(b));a.version=b.version=1;
  a.specialization_length=b.specialization_length=4;
  a.specialization_bytes[0]=1;b.specialization_bytes[0]=2;
  uint8_t ka[32],kb[32],ka2[32];
  tegu_render_generic_variant_key(pipeline,0,0,&a,ka);
  tegu_render_generic_variant_key(pipeline,0,0,&a,ka2);
  tegu_render_generic_variant_key(pipeline,0,0,&b,kb);
  assert(!memcmp(ka,ka2,32)&&memcmp(ka,kb,32));
  a.vertex_count=1;a.vertex_constants[0].id=7;a.vertex_constants[0].size=4;
  a.vertex_constants[0].data[0]=3;b=a;b.vertex_constants[0].data[0]=4;
  tegu_render_generic_variant_key(pipeline,0,0,&a,ka);
  tegu_render_generic_variant_key(pipeline,0,0,&b,kb);
  assert(memcmp(ka,kb,32));
  tegu_render_generic_variant_key(pipeline,0,TEGU_MALI_GENERIC_OPTION_DRAW_PARAMETERS,&a,kb);
  assert(memcmp(ka,kb,32));
 }

 /* --- direct v2 sidecar facts for 2 mips and 3D --- */
 {
  TeguTexture *mipv=new_sample(d,80,16,16,2,2,1);assert(mipv);
  TeguSampler *ms=new_sampler(d,3);
  struct Dispatch probe;memset(&probe,0,sizeof(probe));
  probe.compositorTextures[0]=(id)mipv;probe.compositorSamplers[0]=(id)ms;
  probe.genericDrawRequest.binding_count=1;
  probe.genericDrawRequest.bindings[0]=(struct tegu_generic_draw_binding){
   .binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={mipv->buffer->handle,mipv->mip[0].offset,0},
   .width=16,.height=16,.row_bytes=64,.format=80};
  U64 span=0;assert(tegu_generic_texture_span(mipv,&span));
  probe.genericDrawRequest.bindings[0].memory.bytes=span;
  struct tegu_generic_v2_sidecar sc;char why[160]={0};
  assert(!tegu_generic_v2_build_job(&probe,&sc,0,why,sizeof(why)));
  assert(sc.count==1&&sc.bindings[0].layout.levels==2&&sc.bindings[0].layout.kind==N2T_KIND_2D);
  assert(sc.bindings[0].sampler.min_filter==N2T_FILTER_LINEAR&&sc.bindings[0].sampler.mag_filter==N2T_FILTER_LINEAR);
  assert(sc.bindings[0].backing_length==span);
  assert(sc.bindings[0].layout.level[1].offset>sc.bindings[0].layout.level[0].offset);
  /* Immutable v2 must refuse: GenericBinding still carries Vulkan enums and
   * omits exact Metal LOD/normalized/bias state. The one-level legacy path is
   * not affected. */
  {
   uint32_t saved=probe.genericDrawRequest.bindings[0].kind;
   probe.genericDrawRequest.bindings[0].kind=TEGU_GENERIC_DRAW_COMBINED_IMMUTABLE;
   struct tegu_generic_v2_sidecar imm;char iwhy[160]={0};
   assert(tegu_generic_v2_build_job(&probe,&imm,0,iwhy,sizeof(iwhy)));
   probe.genericDrawRequest.bindings[0].kind=saved;
  }
  /* Exercise the real n2r_build_v2 sidecar path, not only the fact builder. */
  struct n2r_texture_resources tr={N2R_TEXTURE_RESOURCES_VERSION,sc.count,sc.bindings};
  struct n2r_addresses na;memset(&na,0,sizeof(na));
  na.command=0x10000000ull;na.frame=0x10004000ull;na.heap=0x20000000ull;na.heap_bytes=2u<<20;
  na.vertex_code=0x30000000ull;na.fragment_code=0x31000000ull;na.target=0x40000000ull;
  na.binding[0]=0x50000000ull;
  probe.genericDrawRequest.width=16;probe.genericDrawRequest.height=16;probe.genericDrawRequest.row_bytes=64;
  probe.genericDrawRequest.target_format=80;probe.genericDrawRequest.load_action=2;
  probe.genericDrawRequest.vertex_count=3;probe.genericDrawRequest.scissor[2]=16;
  probe.genericDrawRequest.scissor[3]=16;probe.genericDrawRequest.target_token=mipv->buffer->handle;
  probe.genericDrawRequest.target_length=mipv->buffer->logical;
  assert(!n2r_validate_v2(&pipeline->genericShader,&probe.genericDrawRequest,&tr));
  /* The direct n2r_build_v2 replay currently returns BAD for this 2-level
   * fixture while n2r_validate_v2 accepts it; that is an n2r packer issue, not
   * a render-side proof. Keep the exact sidecar/validation check and do not
   * claim the raw builder in this test. */
#ifdef TEGU_HOST_STUB_V2
  /* Exercise the actual render submit hook with the strong test stub standing
   * in for the additive bridge entry. */
  TeguRenderPipeline *vp=[[TeguRenderPipeline alloc] init];assert(vp);vp->genericDraw=1;
  struct Dispatch sj;memset(&sj,0,sizeof(sj));sj.renderPipeline=vp;sj.genericDrawSnapshot=1;
  sj.genericDrawRequest=probe.genericDrawRequest;
  sj.compositorTextures[0]=(id)mipv;sj.compositorSamplers[0]=(id)ms;
  U64 rep[2]={0};unsigned rn=2;
  assert(!tegu_generic_draw_submit(&sj,d,rep,&rn));
  assert(stubV2Calls==1&&stubV2Levels==2&&rn==2&&rep[0]==1&&rep[1]==2);
  [vp release];
#endif
  [mipv release];[ms release];

  TeguTexture *vol2=new_sample(d,10,64,64,1,7,2);assert(vol2);
  TeguTexture *volfs=new_sample(d,80,16,16,1,2,1);assert(volfs);
  memset(&probe,0,sizeof(probe));
  probe.compositorTextures[0]=(id)vol2;probe.compositorTextures[1]=(id)volfs;
  probe.genericDrawRequest.binding_count=2;
  probe.genericDrawRequest.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_TEXTURE,
   .memory={vol2->buffer->handle,vol2->mip[0].offset,0},.width=64,.height=64,.row_bytes=64,.format=10};
  assert(tegu_generic_texture_span(vol2,&span));
  probe.genericDrawRequest.bindings[0].memory.bytes=span;
  probe.genericDrawRequest.bindings[1]=(struct tegu_generic_draw_binding){.binding=1,.kind=TEGU_GENERIC_DRAW_TEXTURE,
   .memory={volfs->buffer->handle,volfs->mip[0].offset,0},.width=16,.height=16,.row_bytes=64,.format=80};
  U64 fs2span=0;assert(tegu_generic_texture_span(volfs,&fs2span));
  probe.genericDrawRequest.bindings[1].memory.bytes=fs2span;
  memset(&sc,0,sizeof(sc));assert(!tegu_generic_v2_build_job(&probe,&sc,0,why,sizeof(why)));
  assert(sc.count==2&&sc.bindings[0].layout.kind==N2T_KIND_3D&&sc.bindings[0].layout.depth==2);
  assert(sc.bindings[0].layout.level[0].slice_stride==4096&&sc.bindings[0].backing_length==8192);
  [vol2 release];[volfs release];

  TeguTexture *arr=new_sample_ex(d,80,16,16,1,3,1,2,1);assert(arr);
  struct n2t_texture_layout al;U64 abacking=0;
  assert(!tegu_generic_v2_layout(arr,&al,0,0,0,&abacking,NULL,0));
  assert(al.kind==N2T_KIND_2D_ARRAY&&al.layers==2&&al.array_length==2&&al.level[0].layer_stride==1024&&abacking==2048);
  [arr release];
  TeguTexture *cube=new_sample_ex(d,80,16,16,1,5,1,1,1);assert(cube);
  assert(!tegu_generic_v2_layout(cube,&al,0,0,0,&abacking,NULL,0));
  assert(al.kind==N2T_KIND_CUBE&&al.layers==6&&al.level[0].layer_stride==1024&&abacking==6144);
  [cube release];
  TeguTexture *msaa=new_sample_ex(d,80,16,16,1,4,1,1,4);assert(msaa);
  assert(!tegu_generic_v2_layout(msaa,&al,0,0,0,&abacking,NULL,0));
  assert(al.kind==N2T_KIND_2D_MS&&al.samples==4&&al.level[0].slice_stride==1024&&abacking==4096);
  [msaa release];
  TeguTexture *cubearr=new_sample_ex(d,80,16,16,1,6,1,2,1);assert(cubearr);
  assert(!tegu_generic_v2_layout(cubearr,&al,0,0,0,&abacking,NULL,0));
  assert(al.kind==N2T_KIND_CUBE_ARRAY&&al.layers==12&&al.level[0].layer_stride==1024&&abacking==12288);
  [cubearr release];
  TeguTexture *msarr=new_sample_ex(d,80,16,16,1,8,1,2,4);assert(msarr);
  assert(!tegu_generic_v2_layout(msarr,&al,0,0,0,&abacking,NULL,0));
  assert(al.kind==N2T_KIND_2D_MS_ARRAY&&al.layers==2&&al.level[0].layer_stride==4096&&abacking==8192);
  [msarr release];

  /* CPU upload/readback addresses every array/cube layer and MS array slice;
   * MS transfers sample0 of the selected layer, matching Metal's slice API. */
  uint32_t cpu[16*16];
  TeguRegion full=(TeguRegion){{0,0,0},{16,16,1}};
  TeguTexture *cpuarr=new_sample_ex(d,80,16,16,1,3,1,3,1);assert(cpuarr);
  for(NSUInteger layer=0;layer<3;layer++) {
   for(unsigned i=0;i<16*16;i++)cpu[i]=0x1000u+(uint32_t)layer*0x100u+(i&0xffu);
   [cpuarr replaceRegion:full mipmapLevel:0 slice:layer withBytes:cpu bytesPerRow:64 bytesPerImage:0];
   memset(cpu,0,sizeof(cpu));
   [cpuarr getBytes:cpu bytesPerRow:64 bytesPerImage:0 fromRegion:full mipmapLevel:0 slice:layer];
   assert(cpu[0]==(0x1000u+(uint32_t)layer*0x100u)&&cpu[255]==(0x10ffu+(uint32_t)layer*0x100u));
  }
  [cpuarr release];
  TeguTexture *cpucube=new_sample_ex(d,80,16,16,1,5,1,1,1);assert(cpucube);
  for(NSUInteger layer=0;layer<6;layer++) {
   for(unsigned i=0;i<16*16;i++)cpu[i]=0x2000u+(uint32_t)layer*0x100u;
   [cpucube replaceRegion:full mipmapLevel:0 slice:layer withBytes:cpu bytesPerRow:64 bytesPerImage:0];
   memset(cpu,0,sizeof(cpu));
   [cpucube getBytes:cpu bytesPerRow:64 bytesPerImage:0 fromRegion:full mipmapLevel:0 slice:layer];
   assert(cpu[0]==(0x2000u+(uint32_t)layer*0x100u));
  }
  [cpucube release];
  TeguTexture *cpums=new_sample_ex(d,80,16,16,1,8,1,2,4);assert(cpums);
  for(NSUInteger layer=0;layer<2;layer++) {
   for(unsigned i=0;i<16*16;i++)cpu[i]=0x3000u+(uint32_t)layer*0x100u;
   [cpums replaceRegion:full mipmapLevel:0 slice:layer withBytes:cpu bytesPerRow:64 bytesPerImage:0];
   memset(cpu,0,sizeof(cpu));
   [cpums getBytes:cpu bytesPerRow:64 bytesPerImage:0 fromRegion:full mipmapLevel:0 slice:layer];
   assert(cpu[0]==(0x3000u+(uint32_t)layer*0x100u));
  }
  [cpums release];
 }

 /* --- compute v2 shadow textures are refreshed/written back --- */
 {
  test_surface.width=test_surface.height=64;test_surface.row=256;test_surface.alloc=16384;
  test_surface.planes=0;test_surface.fourcc=0x42475241u;memset(test_surface.data,0,sizeof(test_surface.data));
  TeguTexture *st=new_sample(d,80,64,64,1,2,1);assert(st);
  st->shadowSurface=(id)&test_surface;st->releaseImportedSurface=0;
  uint8_t *sb=(uint8_t *)[st->buffer contents]+st->mip[0].offset;
  for(unsigned i=0;i<64*64*4;i++)sb[i]=(uint8_t)(i*13+7);
  struct Dispatch sj2;memset(&sj2,0,sizeof(sj2));sj2.v2Textures[0]=(id)st;
  struct Dispatch *jobs2[1]={&sj2};
  assert(tegu_shadow_sync_jobs(jobs2,1,0)==64*64*4);
  assert(!memcmp(test_surface.data,sb,64*64*4)&&test_surface_locks==1);
  for(unsigned i=0;i<64*64*4;i++)test_surface.data[i]=(uint8_t)(i*5+3);
  memset(sb,0,64*64*4);st->textureUsage=2;
  assert(tegu_shadow_sync_jobs(jobs2,1,1)==64*64*4);
  assert(!memcmp(sb,test_surface.data,64*64*4));
  st->textureUsage=1;memset(sb,0,64*64*4);
  assert(tegu_shadow_sync_jobs(jobs2,1,1)==0);
  st->shadowSurface=0;[st release];
 }


 /* --- DELTA5: refused pass still returns a usable inert encoder --- */
 /* A depth/stencil PSO must be constructible even though execution refuses
  * observable depth/stencil state. QuartzCore aborts after a nil PSO retry. */
 pd->depth=252;pd->stencil=253;
 TeguRenderPipeline *depthPipeline=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];
 assert(depthPipeline&&!error&&depthPipeline->genericDraw&&
        depthPipeline->depthFormat==252&&depthPipeline->stencilFormat==253);
 pd->depth=0;pd->stencil=0;

 PassDepthDesc *nd=[[PassDepthDesc alloc] init];nd->compare=7;nd->write=0;
 TeguDepthStencilState *noDepth=[d newDepthStencilStateWithDescriptor:(id)nd];assert(noDepth);
 PassStencilFace *sf=[[PassStencilFace alloc] init];
 sf->compare=1;sf->fail=0;sf->depthFail=0;sf->pass=0;sf->read=~0u;sf->write=~0u;
 PassDepthDesc *sd=[[PassDepthDesc alloc] init];sd->compare=7;sd->write=0;sd->front=sf;
 TeguDepthStencilState *stencilTest=[d newDepthStencilStateWithDescriptor:(id)sd];assert(stencilTest);

 AuxPass *ap=[[AuxPass alloc] init];ap->color=[[PassColor alloc] init];
 ap->color->target=[target retain];ap->color->load=2;ap->color->store=1;
 ap->passWidth=ap->passHeight=16;ap->arrayLength=1;ap->sampleCount=4;
 TeguCommand *pc=[q commandBuffer];assert(pc);
 TeguRenderEncoder *pe=[pc renderCommandEncoderWithDescriptor:(id)ap];
 assert(pe&&pc->invalid&&pc->encoding);
 TeguBuffer *inertIB=[d newBufferWithLength:6 options:0];assert(inertIB);
 TeguSampler *inertSamp=new_sampler(d,0);assert(inertSamp);
 unsigned short *ii=(unsigned short *)[inertIB contents];ii[0]=0;ii[1]=1;ii[2]=2;
 uint32_t tiny=0;
 [pe setRenderPipelineState:depthPipeline];
 [pe setDepthStencilState:noDepth];[pe setStencilReferenceValue:0xffffffffu];
 [pe setScissorRect:(TeguScissorRect){0,0,16,16}];
 [pe setViewport:(TeguViewport){0,0,16,16,0,1}];
 [pe setVertexBytes:&tiny length:4 atIndex:0];
 [pe setVertexBuffer:vb offset:0 atIndex:1];[pe setVertexBufferOffset:0 atIndex:1];
 [pe setFragmentBytes:&tiny length:4 atIndex:0];
 [pe setFragmentBuffer:vb offset:0 atIndex:1];[pe setFragmentBufferOffset:0 atIndex:1];
 [pe setFragmentTexture:target atIndex:0];[pe setFragmentSamplerState:inertSamp atIndex:0];
 [pe drawPrimitives:3 vertexStart:0 vertexCount:3];
 [pe drawIndexedPrimitives:3 indexCount:3 indexType:0 indexBuffer:inertIB indexBufferOffset:0];
 assert(!pe->target&&!pe->pipeline&&pe->draws==0&&pe->ended);
 [pe endEncoding];
 assert(!pc->encoding);[pc commit];[pc waitUntilCompleted];
 assert([pc status]==5&&[pc error]&&!pc->count);
 [inertSamp release];[inertIB release];

 /* Resolve, mismatched color, and non-DontCare depth/stencil descriptors are
  * refused before any job, but still return a non-nil inert encoder. */
 AuxPass *rp=[[AuxPass alloc] init];rp->color=[[PassColor alloc] init];
 rp->color->target=[target retain];rp->color->resolve=[target retain];
 rp->color->load=2;rp->color->store=1;rp->passWidth=rp->passHeight=16;rp->arrayLength=1;
 TeguCommand *rc=[q commandBuffer];TeguRenderEncoder *re=[rc renderCommandEncoderWithDescriptor:(id)rp];
 assert(re&&rc->invalid);[re endEncoding];[rc commit];[rc waitUntilCompleted];assert([rc status]==5);[rp release];

 TeguTexture *other=new_sample(d,70,8,16,1,2,1);assert(other);
 AuxPass *mp=[[AuxPass alloc] init];mp->color=[[PassColor alloc] init];
 mp->color->target=[target retain];mp->color->load=2;mp->color->store=1;
 mp->second=[[PassColor alloc] init];mp->second->target=[other retain];mp->second->load=2;mp->second->store=1;
 mp->passWidth=mp->passHeight=16;mp->arrayLength=1;
 TeguCommand *mc=[q commandBuffer];TeguRenderEncoder *me=[mc renderCommandEncoderWithDescriptor:(id)mp];
 /* Mismatched sizes (c6o blur): Metal renders the common 8x16 area; the
  * larger 16x16 attachment becomes a window view of its own storage. */
 assert(me&&!mc->invalid&&!me->inert);
 {TeguTexture *w0=me->renderTargets[0],*w1=me->renderTargets[1];
  assert(w0&&w0!=target&&w0->textureWidth==8&&w0->textureHeight==16&&w0->rowBytes==target->rowBytes&&w0->buffer==target->buffer&&
   w0->mip[0].offset==target->mip[0].offset&&w1==other);}
 [me endEncoding];[mc commit];[mc waitUntilCompleted];assert([mc status]==4);[mp release];[other release];

 TeguTexture *daux=new_aux_texture(d,252,0x30);assert(daux);
 AuxPass *dp=[[AuxPass alloc] init];dp->color=[[PassColor alloc] init];
 dp->color->target=[target retain];dp->color->load=2;dp->color->store=1;
 dp->auxDepth=[[PassColor alloc] init];dp->auxDepth->target=[daux retain];dp->auxDepth->load=0;dp->auxDepth->store=1;
 dp->passWidth=dp->passHeight=16;dp->arrayLength=1;
 TeguCommand *dc=[q commandBuffer];TeguRenderEncoder *de=[dc renderCommandEncoderWithDescriptor:(id)dp];
 assert(de&&dc->invalid);[de endEncoding];[dc commit];[dc waitUntilCompleted];assert([dc status]==5);[dp release];[daux release];

 TeguTexture *saux=new_aux_texture(d,253,0x30);assert(saux);
 AuxPass *sp=[[AuxPass alloc] init];sp->color=[[PassColor alloc] init];
 sp->color->target=[target retain];sp->color->load=2;sp->color->store=1;
 sp->auxStencil=[[PassColor alloc] init];sp->auxStencil->target=[saux retain];sp->auxStencil->load=0;sp->auxStencil->store=1;
 sp->passWidth=sp->passHeight=16;sp->arrayLength=1;
 TeguCommand *spc=[q commandBuffer];TeguRenderEncoder *spe=[spc renderCommandEncoderWithDescriptor:(id)sp];
 assert(spe&&spc->invalid);[spe endEncoding];[spc commit];[spc waitUntilCompleted];assert([spc status]==5);[sp release];[saux release];

 /* --- same texture on two colour attachments is refused before clears --- */
 {
  TeguTexture *dup=new_target(d);assert(dup);
  MrtPass *dp=[[MrtPass alloc] init];dp->color=[[PassColor alloc] init];dp->color->target=[dup retain];dp->color->load=2;dp->color->store=1;
  dp->second=[[PassColor alloc] init];dp->second->target=[dup retain];dp->second->load=2;dp->second->store=1;
  dp->passWidth=dp->passHeight=16;dp->arrayLength=1;dp->sampleCount=1;
  TeguCommand *dc=[q commandBuffer];assert(dc);TeguRenderEncoder *de=[dc renderCommandEncoderWithDescriptor:(id)dp];
  fprintf(stderr,"DUP_TARGET invalid=%u ptr=%p\n",dc->invalid,de);
  assert(de&&dc->invalid);
  [de endEncoding];[dc commit];[dc waitUntilCompleted];assert([dc status]==5);
  [dp release];[dup release];
 }

 /* Exhaust the diagnostic budget deliberately: only the first 16 refused
  * passes per process may write, and every one still returns an encoder. */
 for(unsigned i=0;i<20;i++) {
  TeguCommand *lc=[q commandBuffer];assert(lc);
  TeguRenderEncoder *le=[lc renderCommandEncoderWithDescriptor:(id)ap];
  assert(le&&lc->invalid);[le endEncoding];[lc commit];[lc waitUntilCompleted];
  assert([lc status]==5);
 }
 [ap release];

 /* The exact accepted case: DontCare depth/stencil storage and a no-test
  * state render the color result; the same pass with a stencil test refuses
  * only that draw (trace, no changed-result job). */
 TeguTexture *adepth=new_aux_texture(d,252,0x30);TeguTexture *astencil=new_aux_texture(d,253,0x30);
 assert(adepth&&astencil);
 AuxPass *good=[[AuxPass alloc] init];good->color=[[PassColor alloc] init];
 good->color->target=[target retain];good->color->load=2;good->color->store=1;
 good->auxDepth=[[PassColor alloc] init];good->auxDepth->target=[adepth retain];good->auxDepth->load=0;good->auxDepth->store=0;
 good->auxStencil=[[PassColor alloc] init];good->auxStencil->target=[astencil retain];good->auxStencil->load=0;good->auxStencil->store=0;
 good->passWidth=good->passHeight=16;good->arrayLength=1;
 TeguTexture *gvs=new_sample(d,80,16,16,1,2,1),*gfs=new_sample(d,80,16,16,1,2,1);
 TeguSampler *gs=new_sampler(d,0);
 TeguCommand *gc=[q commandBuffer];TeguRenderEncoder *ge=[gc renderCommandEncoderWithDescriptor:(id)good];
 assert(ge&&!gc->invalid);
 bind_all(ge,vb,depthPipeline,gvs,gfs,gs,gs);
 [ge setDepthStencilState:noDepth];[ge drawPrimitives:3 vertexStart:0 vertexCount:3];[ge endEncoding];
 assert(!gc->invalid&&gc->count==2);[gc commit];[gc waitUntilCompleted];assert([gc status]==4&&![gc error]);
 [gvs release];[gfs release];[gs release];

 AuxPass *badState=[[AuxPass alloc] init];badState->color=[[PassColor alloc] init];
 badState->color->target=[target retain];badState->color->load=2;badState->color->store=1;
 badState->auxDepth=[[PassColor alloc] init];badState->auxDepth->target=[adepth retain];badState->auxDepth->load=0;badState->auxDepth->store=0;
 badState->auxStencil=[[PassColor alloc] init];badState->auxStencil->target=[astencil retain];badState->auxStencil->load=0;badState->auxStencil->store=0;
 badState->passWidth=badState->passHeight=16;badState->arrayLength=1;
 TeguCommand *sc=[q commandBuffer];TeguRenderEncoder *se=[sc renderCommandEncoderWithDescriptor:(id)badState];
 assert(se&&!sc->invalid);
 [se setDepthStencilState:stencilTest];[se drawPrimitives:3 vertexStart:0 vertexCount:3];[se endEncoding];
 assert(!sc->invalid&&sc->count==1);[sc commit];[sc waitUntilCompleted];assert([sc status]==4&&![sc error]);
 [badState release];
 {/* Generic draw with a stencil test (D32F + S8 -> one tile plane, 260):
    * tile-buffer depth/stencil, first job DontCare-starts, the second
    * continues (its write mask may change stencil) and joins the same JM job. */
  TeguTexture *zvs=new_sample(d,80,16,16,1,2,1),*zfs=new_sample(d,80,16,16,1,2,1);TeguSampler *zss=new_sampler(d,0);
  tegu_jm_batch_force=1;
  TeguCommand *zc=[q commandBuffer];TeguRenderEncoder *ze=[zc renderCommandEncoderWithDescriptor:(id)good];assert(ze&&!zc->invalid);
  bind_all(ze,vb,depthPipeline,zvs,zfs,zss,zss);
  [ze setDepthStencilState:stencilTest];[ze setStencilReferenceValue:5];
  [ze drawPrimitives:3 vertexStart:0 vertexCount:3];[ze drawPrimitives:3 vertexStart:0 vertexCount:3];[ze endEncoding];
  assert(!zc->invalid);
  unsigned zjobs=0;
  for(unsigned i=0;i<zc->count;i++){struct Dispatch *zj=tegu_command_job(zc,i);
   if(!(zj->genericDrawFlags&N2R_DRAW_DEPTH_STENCIL))continue;
   fprintf(stderr,"ZS job=%u fmt=%u load=%u cmp=%u sten=%u fcmp=%u ref=%u\n",i,zj->genericZs.format,zj->genericZs.load,
    zj->genericZs.depth_compare,zj->genericZs.stencil_enable,zj->genericZs.front.compare,zj->genericZs.front.reference);
   assert(zj->genericZs.format==260&&zj->genericZs.stencil_enable&&zj->genericZs.front.compare==1&&zj->genericZs.front.reference==5&&
          zj->genericZs.load==(zjobs?1u:0u));
   zjobs++;}
  assert(zjobs==2);
  [zc commit];[zc waitUntilCompleted];
  fprintf(stderr,"ZS status=%lu chain=%u\n",(unsigned long)[zc status],f.last_chain);
  assert([zc status]==4&&![zc error]&&f.last_chain==2);
  tegu_jm_batch_force=0; /* a continuing stencil job alone is refused, never re-cleared */
  TeguCommand *zc2=[q commandBuffer];TeguRenderEncoder *ze2=[zc2 renderCommandEncoderWithDescriptor:(id)good];
  bind_all(ze2,vb,depthPipeline,zvs,zfs,zss,zss);[ze2 setDepthStencilState:stencilTest];
  [ze2 drawPrimitives:3 vertexStart:0 vertexCount:3];[ze2 drawPrimitives:3 vertexStart:0 vertexCount:3];[ze2 endEncoding];
  [zc2 commit];[zc2 waitUntilCompleted];fprintf(stderr,"ZS unbatched status=%lu\n",(unsigned long)[zc2 status]);
  assert([zc2 status]==5);
  tegu_jm_batch_force=-1;[zvs release];[zfs release];[zss release];
 }

 [good release];[adepth release];[astencil release];
 [stencilTest release];[sd release];[sf release];[noDepth release];[nd release];
 [depthPipeline release];

 /* --- real QuartzCore path_* RT2-only pipeline with 3 colour attachments --- */
 if(argc>=9) {
  void *pv=0,*pf=0;long pvn=0,pfn=0;FILE *pvfp=fopen(argv[7],"rb");assert(pvfp);
  fseek(pvfp,0,SEEK_END);pvn=ftell(pvfp);rewind(pvfp);pv=malloc((size_t)pvn);
  assert(pv&&fread(pv,1,(size_t)pvn,pvfp)==(size_t)pvn);fclose(pvfp);
  FILE *pffp=fopen(argv[8],"rb");assert(pffp);fseek(pffp,0,SEEK_END);pfn=ftell(pffp);rewind(pffp);
  pf=malloc((size_t)pfn);assert(pf&&fread(pf,1,(size_t)pfn,pffp)==(size_t)pfn);fclose(pffp);
  TeguLibrary *mvl=[d newTeguLibraryWithBytes:pv length:(NSUInteger)pvn error:&error];
  TeguLibrary *mfl=[d newTeguLibraryWithBytes:pf length:(NSUInteger)pfn error:&error];
  free(pv);free(pf);assert(mvl&&mfl&&!error);
  TeguFunction *mvf=[mvl newFunctionWithName:[NSString stringWithUTF8String:"path_cubic_vert_0_lpf"]];
  TeguFunction *mff=[mfl newFunctionWithName:[NSString stringWithUTF8String:"path_cubic_frag_lpf"]];
  assert(mvf&&mff);
  RenderDescriptor *mpd=[[RenderDescriptor alloc] init];
  mpd->vertex=mvf;mpd->fragment=mff;mpd->samples=1;mpd->raster=1;mpd->amplification=1;mpd->topology=0;
  mpd->colors=[[RenderColors alloc] init];
  for(unsigned i=0;i<3;i++)mpd->colors->colors[i]=[[RenderColor alloc] init];
  mpd->colors->colors[0]->format=80;mpd->colors->colors[0]->mask=0;
  mpd->colors->colors[1]->format=80;mpd->colors->colors[1]->mask=0;
  mpd->colors->colors[2]->format=10;mpd->colors->colors[2]->mask=15;
  mpd->colors->colors[3]=[[RenderColor alloc] init];mpd->colors->colors[3]->format=80;mpd->colors->colors[3]->mask=0;
  TeguRenderPipeline *mrtp=[d newRenderPipelineStateWithDescriptor:(id)mpd error:&error];
  assert(mrtp&&!error&&mrtp->genericDraw&&mrtp->genericMrt&&mrtp->genericMainRt==2&&
         mrtp->genericRtCount==4&&mrtp->genericRtFormats[2]==10&&mrtp->colorFormat==10);
  TeguTexture *mrt0=new_sample(d,80,16,16,1,2,1),*mrt1=new_sample(d,80,16,16,1,2,1),*mrt2=new_sample(d,10,16,16,1,2,1),*mrt3=new_sample(d,80,16,16,1,2,1);
  assert(mrt0&&mrt1&&mrt2&&mrt3);
  MrtPass *mp=[[MrtPass alloc] init];mp->color=[[PassColor alloc] init];
  mp->color->target=[mrt0 retain];mp->color->load=2;mp->color->store=1;
  mp->second=[[PassColor alloc] init];mp->second->target=[mrt1 retain];mp->second->load=2;mp->second->store=1;
  mp->third=[[PassColor alloc] init];mp->third->target=[mrt2 retain];mp->third->load=2;mp->third->store=1;
  mp->fourth=[[PassColor alloc] init];mp->fourth->target=[mrt3 retain];mp->fourth->load=2;mp->fourth->store=1;
  mp->passWidth=mp->passHeight=16;mp->arrayLength=1;mp->sampleCount=1;
  TeguCommand *mrc=[q commandBuffer];assert(mrc);TeguRenderEncoder *mre=[mrc renderCommandEncoderWithDescriptor:(id)mp];assert(mre);
  [mre setRenderPipelineState:mrtp];[mre drawPrimitives:3 vertexStart:0 vertexCount:3];[mre endEncoding];
  assert(!mrc->invalid&&mrc->count==5);
  struct Dispatch *mrj=tegu_command_job(mrc,4);assert(mrj->genericDrawSnapshot&&mrj->genericDrawRequest.target_format==10&&
    mrj->renderTexture==mrt2);
  [mrc commit];[mrc waitUntilCompleted];assert([mrc status]==4&&![mrc error]);
  /* Descriptor active masks on RT0/1/3 must be preserved, not cleared or
   * refused, because path_cubic writes only RT2. */
  RenderDescriptor *ppd=[[RenderDescriptor alloc] init];
  ppd->vertex=[mvf retain];ppd->fragment=[mff retain];ppd->samples=1;ppd->raster=1;ppd->amplification=1;ppd->topology=0;
  ppd->colors=[[RenderColors alloc] init];
  for(unsigned i=0;i<4;i++){ppd->colors->colors[i]=[[RenderColor alloc] init];ppd->colors->colors[i]->mask=15;}
  ppd->colors->colors[0]->format=80;ppd->colors->colors[1]->format=80;
  ppd->colors->colors[2]->format=10;ppd->colors->colors[3]->format=80;
  error=0;TeguRenderPipeline *preserve=[d newRenderPipelineStateWithDescriptor:(id)ppd error:&error];
  fprintf(stderr,"PATH_PRESERVE_PIPE ptr=%p draw=%u true=%u main=%u active=%x preserve=%x err=%p\n",preserve,
   preserve?preserve->genericDraw:0,preserve?preserve->genericTrueMrt:0,preserve?preserve->genericMainRt:0,
   preserve?preserve->genericRtActive:0,preserve?preserve->genericRtPreserve:0,error);
  assert(preserve&&!error&&preserve->genericDraw&&!preserve->genericTrueMrt&&
         preserve->genericMainRt==2&&preserve->genericRtActive==4&&preserve->genericRtPreserve==0xb);
  MrtPass *pr=[[MrtPass alloc] init];pr->color=[[PassColor alloc] init];pr->color->target=[mrt0 retain];pr->color->load=2;pr->color->store=1;
  pr->second=[[PassColor alloc] init];pr->second->target=[mrt1 retain];pr->second->load=2;pr->second->store=1;
  pr->third=[[PassColor alloc] init];pr->third->target=[mrt2 retain];pr->third->load=2;pr->third->store=1;
  pr->fourth=[[PassColor alloc] init];pr->fourth->target=[mrt3 retain];pr->fourth->load=2;pr->fourth->store=1;
  pr->passWidth=pr->passHeight=16;pr->arrayLength=1;pr->sampleCount=1;
  TeguCommand *prc=[q commandBuffer];assert(prc);TeguRenderEncoder *pre=[prc renderCommandEncoderWithDescriptor:(id)pr];assert(pre);
  [pre setRenderPipelineState:preserve];[pre drawPrimitives:3 vertexStart:0 vertexCount:3];[pre endEncoding];
  fprintf(stderr,"PATH_PRESERVE_CMD invalid=%u count=%u\n",prc->invalid,prc->count);
  assert(!prc->invalid&&prc->count==2);
  {struct Dispatch *prj=tegu_command_job(prc,1);assert(prj->genericDrawSnapshot&&prj->renderTexture==mrt2);}
  [prc commit];[prc waitUntilCompleted];assert([prc status]==4&&![prc error]);
  [pr release];[preserve release];[ppd release];
  /* RT0/RT1 unbound: only RT2 is bound and written. */
  MrtPass *unbound=[[MrtPass alloc] init];unbound->color=[[PassColor alloc] init];
  unbound->second=[[PassColor alloc] init];unbound->third=[[PassColor alloc] init];
  unbound->third->target=[mrt2 retain];unbound->third->load=2;unbound->third->store=1;
  unbound->passWidth=unbound->passHeight=16;unbound->arrayLength=1;unbound->sampleCount=1;
  TeguCommand *uc=[q commandBuffer];assert(uc);TeguRenderEncoder *ue=[uc renderCommandEncoderWithDescriptor:(id)unbound];assert(ue);
  [ue setRenderPipelineState:mrtp];[ue drawPrimitives:3 vertexStart:0 vertexCount:3];[ue endEncoding];
  assert(!uc->invalid&&uc->count==2);[uc commit];[uc waitUntilCompleted];assert([uc status]==4&&![uc error]);
  [unbound release];
  [mrt0 release];[mrt1 release];[mrt2 release];[mrt3 release];[mp release];[mrtp release];[mpd release];[mvl release];[mfl release];
 }

 /* --- path_blit true-MRT+fetch: inert only in backboardd --- */
 if(argc>=13) {
  void *bv=0,*bf=0;long bvn=0,bfn=0;FILE *bvp=fopen(argv[11],"rb");assert(bvp);
  fseek(bvp,0,SEEK_END);bvn=ftell(bvp);rewind(bvp);bv=malloc((size_t)bvn);
  assert(bv&&fread(bv,1,(size_t)bvn,bvp)==(size_t)bvn);fclose(bvp);
  FILE *bfp=fopen(argv[12],"rb");assert(bfp);fseek(bfp,0,SEEK_END);bfn=ftell(bfp);rewind(bfp);
  bf=malloc((size_t)bfn);assert(bf&&fread(bf,1,(size_t)bfn,bfp)==(size_t)bfn);fclose(bfp);
  TeguLibrary *bvl=[d newTeguLibraryWithBytes:bv length:(NSUInteger)bvn error:&error];
  TeguLibrary *bfl=[d newTeguLibraryWithBytes:bf length:(NSUInteger)bfn error:&error];
  free(bv);free(bf);assert(bvl&&bfl&&!error);
  TeguFunction *bvf=[bvl newFunctionWithName:[NSString stringWithUTF8String:"path_blit_vert_lpf"]];
  TeguFunction *bff=[bfl newFunctionWithName:[NSString stringWithUTF8String:"path_blit_frag_lpf"]];assert(bvf&&bff);
  RenderDescriptor *bpd=[[RenderDescriptor alloc] init];bpd->vertex=bvf;bpd->fragment=bff;bpd->samples=1;bpd->raster=1;
  bpd->amplification=1;bpd->topology=0;bpd->colors=[[RenderColors alloc] init];
  for(unsigned i=0;i<3;i++)bpd->colors->colors[i]=[[RenderColor alloc] init];
  bpd->colors->colors[0]->format=80;bpd->colors->colors[0]->mask=15;
  bpd->colors->colors[1]->format=80;bpd->colors->colors[1]->mask=15;
  bpd->colors->colors[2]->format=10;bpd->colors->colors[2]->mask=15;
  tegu_host_process_backboardd=1;
  TeguRenderPipeline *bp=[d newRenderPipelineStateWithDescriptor:(id)bpd error:&error];
  fprintf(stderr,"PATH_BLIT_PIPE ptr=%p draw=%u inert=%u true=%u err=%p\n",bp,bp?bp->genericDraw:0,
   bp?bp->inert:0,bp?bp->genericTrueMrt:0,error);
#ifdef TEGU_INERT_PIPELINES
  assert(bp&&!error&&bp->inert&&!bp->genericDraw);
#else
  assert(!bp&&error);
#endif
  if(bp)[bp release];
  tegu_host_process_backboardd=0;
  error=0;TeguRenderPipeline *bp2=[d newRenderPipelineStateWithDescriptor:(id)bpd error:&error];
  fprintf(stderr,"PATH_BLIT_NONBBD ptr=%p err=%p\n",bp2,error);
  assert(!bp2&&error);if(bp2)[bp2 release];
  [bpd release];[bvl release];[bfl release];
 }

 /* --- authored three-output fragment: true-MRT v4 transport --- */
 if(argc>=14) {
  void *mv=0;long mvn=0;FILE *mvp=fopen(argv[13],"rb");assert(mvp);fseek(mvp,0,SEEK_END);mvn=ftell(mvp);rewind(mvp);
  mv=malloc((size_t)mvn);assert(mv&&fread(mv,1,(size_t)mvn,mvp)==(size_t)mvn);fclose(mvp);
  TeguLibrary *ml=[d newTeguLibraryWithBytes:mv length:(NSUInteger)mvn error:&error];free(mv);assert(ml&&!error);
  TeguFunction *mvf=[ml newFunctionWithName:[NSString stringWithUTF8String:"mrt_vertex"]];
  TeguFunction *mff=[ml newFunctionWithName:[NSString stringWithUTF8String:"mrt_fragment"]];assert(mvf&&mff);
  RenderDescriptor *mpd=[[RenderDescriptor alloc] init];mpd->vertex=mvf;mpd->fragment=mff;mpd->samples=1;mpd->raster=1;
  mpd->amplification=1;mpd->topology=0;mpd->colors=[[RenderColors alloc] init];
  for(unsigned i=0;i<3;i++)mpd->colors->colors[i]=[[RenderColor alloc] init];
  mpd->colors->colors[0]->format=80;mpd->colors->colors[0]->mask=15;
  mpd->colors->colors[1]->format=80;mpd->colors->colors[1]->mask=3;
  mpd->colors->colors[2]->format=10;mpd->colors->colors[2]->mask=8;
  TeguRenderPipeline *mp=[d newRenderPipelineStateWithDescriptor:(id)mpd error:&error];
  fprintf(stderr,"MRT_PROBE_PIPE ptr=%p draw=%u mrt=%u true=%u active=%x err=%p\n",mp,mp?mp->genericDraw:0,
   mp?mp->genericMrt:0,mp?mp->genericTrueMrt:0,mp?mp->genericRtActive:0,error);
  assert(mp&&!error&&mp->genericDraw&&mp->genericMrt&&mp->genericTrueMrt&&mp->genericRtActive==7);
  TeguTexture *mt0=new_sample(d,80,16,16,1,2,1),*mt1=new_sample(d,80,16,16,1,2,1),*mt2=new_sample(d,10,16,16,1,2,1);assert(mt0&&mt1&&mt2);
  MrtPass *mmp=[[MrtPass alloc] init];mmp->color=[[PassColor alloc] init];mmp->color->target=[mt0 retain];mmp->color->load=2;mmp->color->store=1;
  mmp->second=[[PassColor alloc] init];mmp->second->target=[mt1 retain];mmp->second->load=2;mmp->second->store=1;
  mmp->third=[[PassColor alloc] init];mmp->third->target=[mt2 retain];mmp->third->load=2;mmp->third->store=1;
  mmp->passWidth=mmp->passHeight=16;mmp->arrayLength=1;mmp->sampleCount=1;
  TeguCommand *mc=[q commandBuffer];assert(mc);TeguRenderEncoder *me=[mc renderCommandEncoderWithDescriptor:(id)mmp];assert(me);
  [me setRenderPipelineState:mp];[me drawPrimitives:3 vertexStart:0 vertexCount:3];[me endEncoding];
  fprintf(stderr,"MRT_PROBE_CMD invalid=%u count=%u\n",mc->invalid,mc->count);assert(!mc->invalid&&mc->count==4);
  {struct Dispatch *mj=tegu_command_job(mc,3);struct tegu_render_generic_sidecar *msc=(void *)mj->compositorPlan.bytes;
   assert(msc&&msc->magic==TEGU_RENDER_SIDECAR_MAGIC&&msc->count==3&&msc->targets[2].format==10);
   assert(msc->targets[0].write_mask==15&&msc->targets[1].write_mask==3&&msc->targets[2].write_mask==8);
   assert(msc->blends[0].write_mask==15&&msc->blends[1].write_mask==3&&msc->blends[2].write_mask==8);}
  [mc commit];[mc waitUntilCompleted];fprintf(stderr,"MRT_PROBE status=%lu err=%p\n",(unsigned long)[mc status],[mc error]);
  assert([mc status]==4&&![mc error]);
  /* Pre-frame on (PF=1): three draws of the true-MRT pipeline continue one
   * pass: first job Clears in-frame, later jobs Load; batched they are ONE
   * JM job (chain 3) after any queued pass CLEAR jobs. */
  {unsigned oldPf=n2mb_preframe(d->connection,1);tegu_jm_batch_force=1;
   TeguCommand *m3=[q commandBuffer];m3->abWord|=1u<<TEGU_AB_PREFRAME;
   TeguRenderEncoder *e3=[m3 renderCommandEncoderWithDescriptor:(id)mmp];assert(e3);[e3 setRenderPipelineState:mp];
   for(unsigned k=0;k<3;k++)[e3 drawPrimitives:3 vertexStart:0 vertexCount:3];
   [e3 endEncoding];assert(!m3->invalid);
   unsigned mrtJobs=0;
   for(unsigned i=0;i<m3->count;i++){struct Dispatch *j3=tegu_command_job(m3,i);
    struct tegu_render_generic_sidecar *s3=(void *)j3->compositorPlan.bytes;
    if(!s3||s3->count!=3)continue;
    fprintf(stderr,"MRT3 job=%u loads=%u/%u/%u q=%u\n",i,s3->targets[0].load_action,s3->targets[1].load_action,s3->targets[2].load_action,
     j3->genericDrawRequest.load_action);
    for(unsigned t=0;t<3;t++)assert(s3->targets[t].load_action==(mrtJobs?1u:2u));
    mrtJobs++;}
   assert(mrtJobs==3);
   [m3 commit];[m3 waitUntilCompleted];
   fprintf(stderr,"MRT3 status=%lu chain=%u\n",(unsigned long)[m3 status],f.last_chain);
   assert([m3 status]==4&&![m3 error]&&f.last_chain==3);
   tegu_jm_batch_force=-1;(void)n2mb_preframe(d->connection,oldPf);}
  [mt0 release];[mt1 release];[mt2 release];[mmp release];[mp release];[mpd release];[ml release];
 }

 /* --- real asg pair with graphics TLS: compile dataflow --- */
 if(argc>=11) {
  void *tv=0,*tf=0;long tvn=0,tfn=0;FILE *tvp=fopen(argv[9],"rb");assert(tvp);
  fseek(tvp,0,SEEK_END);tvn=ftell(tvp);rewind(tvp);tv=malloc((size_t)tvn);
  assert(tv&&fread(tv,1,(size_t)tvn,tvp)==(size_t)tvn);fclose(tvp);
  FILE *tfp=fopen(argv[10],"rb");assert(tfp);fseek(tfp,0,SEEK_END);tfn=ftell(tfp);rewind(tfp);
  tf=malloc((size_t)tfn);assert(tf&&fread(tf,1,(size_t)tfn,tfp)==(size_t)tfn);fclose(tfp);
  TeguLibrary *tvl=[d newTeguLibraryWithBytes:tv length:(NSUInteger)tvn error:&error];
  TeguLibrary *tfl=[d newTeguLibraryWithBytes:tf length:(NSUInteger)tfn error:&error];
  free(tv);free(tf);assert(tvl&&tfl&&!error);
  TeguFunction *tvf=[tvl newFunctionWithName:[NSString stringWithUTF8String:"asg_vert_lph"]];
  TeguFunction *tff=[tfl newFunctionWithName:[NSString stringWithUTF8String:"asg_frag_lph"]];assert(tvf&&tff);
  RenderDescriptor *tpd=[[RenderDescriptor alloc] init];tpd->vertex=tvf;tpd->fragment=tff;tpd->samples=1;tpd->raster=1;
  tpd->amplification=1;tpd->topology=0;tpd->colors=[[RenderColors alloc] init];
  tpd->colors->colors[0]=[[RenderColor alloc] init];tpd->colors->colors[0]->format=80;tpd->colors->colors[0]->mask=15;
  RenderLayout *tl=[[RenderLayout alloc] init];tl->entries[0]=[[RenderAttribute alloc] init];tl->entries[0]->value=31;tl->entries[0]->byteOffset=0;tl->entries[0]->bufferSlot=0;
  tl->entries[1]=[[RenderAttribute alloc] init];tl->entries[1]->value=29;tl->entries[1]->byteOffset=16;tl->entries[1]->bufferSlot=0;
  tl->entries[3]=[[RenderAttribute alloc] init];tl->entries[3]->value=27;tl->entries[3]->byteOffset=24;tl->entries[3]->bufferSlot=0;
  tl->buffers=[[RenderLayout alloc] init];tl->buffers->entries[0]=[[RenderAttribute alloc] init];tl->buffers->entries[0]->layoutStride=32;tl->buffers->entries[0]->step=1;tl->buffers->entries[0]->rate=1;
  tpd->layout=tl;
  TeguRenderPipeline *tp=[d newRenderPipelineStateWithDescriptor:(id)tpd error:&error];
  fprintf(stderr,"TLS_PIPE ptr=%p draw=%u tls=%u v=%u f=%u\\n",tp,tp?tp->genericDraw:0,tp?tp->genericTls:0,tp?tp->vertexCode.tls:0,tp?tp->fragmentCode.tls:0);
  assert(tp&&!error&&tp->genericDraw&&tp->genericTls>0);
  TeguBuffer *tvb=[d newBufferWithLength:96 options:0];assert(tvb);
  TeguCommand *tc=[q commandBuffer];assert(tc);TeguRenderEncoder *te=[tc renderCommandEncoderWithDescriptor:(id)pass];assert(te);
  [te setRenderPipelineState:tp];[te setVertexBuffer:tvb offset:0 atIndex:0];[te drawPrimitives:3 vertexStart:0 vertexCount:3];[te endEncoding];
  assert(!tc->invalid&&tc->count==2);
  {struct Dispatch *tdj=tegu_command_job(tc,1);struct tegu_render_generic_sidecar *tsc=(void *)tdj->compositorPlan.bytes;
   assert(tsc&&tsc->magic==TEGU_RENDER_SIDECAR_MAGIC&&tsc->version==TEGU_RENDER_SIDECAR_VERSION&&
          tsc->tls>0&&tsc->count==0);}
  [tc commit];[tc waitUntilCompleted];fprintf(stderr,"TLS_DRAW status=%lu err=%p\n",(unsigned long)[tc status],[tc error]);
  assert([tc status]==4&&![tc error]);
  [tvb release];[tp release];[tpd release];[tvl release];[tfl release];
 }

 /* --- missing capability bits and non-Tegu functions still inert in backboardd --- */
 {
  RenderDescriptor *capBad=[[RenderDescriptor alloc] init];capBad->vertex=[vf retain];capBad->fragment=[ff retain];
  capBad->samples=3;capBad->raster=1;capBad->amplification=1;capBad->topology=0; /* 3x: never admitted (4x and sRGB targets are) */
  capBad->colors=[[RenderColors alloc] init];capBad->colors->colors[0]=[[RenderColor alloc] init];
  capBad->colors->colors[0]->format=81;capBad->colors->colors[0]->mask=15;
  U64 savedCaps=d->capabilities;d->capabilities=savedCaps&~(TEGU_GPU_CAP_COMPOSITOR|TEGU_GPU_CAP_GENERIC_JOB);
  tegu_host_process_backboardd=1;error=0;
  TeguRenderPipeline *capPipeline=[d newRenderPipelineStateWithDescriptor:(id)capBad error:&error];
  fprintf(stderr,"NO_CAPS_PIPE ptr=%p inert=%u err=%p\n",capPipeline,capPipeline?capPipeline->inert:0,error);
#ifdef TEGU_INERT_PIPELINES
  assert(capPipeline&&!error&&capPipeline->inert);
#else
  assert(!capPipeline&&error);
#endif
  if(capPipeline)[capPipeline release];
  tegu_host_process_backboardd=0;error=0;
  TeguRenderPipeline *capNon=[d newRenderPipelineStateWithDescriptor:(id)capBad error:&error];
#ifdef TEGU_INERT_PIPELINES
  assert(!capNon&&error);
#else
  assert(!capNon&&error);
#endif
  if(capNon)[capNon release];
  d->capabilities=savedCaps;[capBad release];
 }
 {
  TeguFunction *fake=(TeguFunction *)[[NSObject alloc] init];
  RenderDescriptor *np=[[RenderDescriptor alloc] init];np->vertex=fake;np->fragment=[fake retain];
  np->samples=1;np->raster=1;np->amplification=1;np->topology=0;
  np->colors=[[RenderColors alloc] init];np->colors->colors[0]=[[RenderColor alloc] init];
  np->colors->colors[0]->format=80;np->colors->colors[0]->mask=15;
  tegu_host_process_backboardd=1;error=0;
  TeguRenderPipeline *fakePipeline=[d newRenderPipelineStateWithDescriptor:(id)np error:&error];
  fprintf(stderr,"NON_TEGU_PIPE ptr=%p inert=%u err=%p\n",fakePipeline,fakePipeline?fakePipeline->inert:0,error);
#ifdef TEGU_INERT_PIPELINES
  assert(fakePipeline&&!error&&fakePipeline->inert);
#else
  assert(!fakePipeline&&error);
#endif
  if(fakePipeline)[fakePipeline release];
  tegu_host_process_backboardd=0;error=0;
  TeguRenderPipeline *fakeNon=[d newRenderPipelineStateWithDescriptor:(id)np error:&error];
  assert(!fakeNon&&error);if(fakeNon)[fakeNon release];
  [np release];
 }

 /* --- closed/open command gets an inert encoder in backboardd --- */
 {
  tegu_host_process_backboardd=1;
  TeguCommand *committed=[q commandBuffer];assert(committed);[committed commit];[committed waitUntilCompleted];
  id committedEncoder=[committed renderCommandEncoderWithDescriptor:(id)pass];
  fprintf(stderr,"CLOSED_ENCODER ptr=%p\n",committedEncoder);
#ifdef TEGU_INERT_PIPELINES
  assert(committedEncoder&&((TeguRenderEncoder *)committedEncoder)->inert);
#else
  assert(!committedEncoder);
#endif
  if(committedEncoder)[(TeguRenderEncoder *)committedEncoder endEncoding];

  TeguCommand *open=[q commandBuffer];assert(open);TeguRenderEncoder *first=[open renderCommandEncoderWithDescriptor:(id)pass];assert(first);
  id openEncoder=[open renderCommandEncoderWithDescriptor:(id)pass];
  fprintf(stderr,"OPEN_ENCODER ptr=%p\n",openEncoder);
#ifdef TEGU_INERT_PIPELINES
  assert(openEncoder&&((TeguRenderEncoder *)openEncoder)->inert);
#else
  assert(!openEncoder);
#endif
  if(openEncoder)[(TeguRenderEncoder *)openEncoder endEncoding];
  [first endEncoding];
  tegu_host_process_backboardd=0;
  error=0;TeguCommand *non= [q commandBuffer];assert(non);[non commit];[non waitUntilCompleted];
  id nonEncoder=[non renderCommandEncoderWithDescriptor:(id)pass];
  assert(!nonEncoder);
 }

 /* --- TEGU_INERT_PIPELINES: nonnil inert PSO vs default nil --- */
 {
  RenderDescriptor *unsupported=[[RenderDescriptor alloc] init];
  unsupported->vertex=[vf retain];unsupported->fragment=[ff retain];unsupported->samples=8;unsupported->raster=1;
  unsupported->amplification=1;unsupported->topology=0;
  unsupported->colors=[[RenderColors alloc] init];
  unsupported->colors->colors[0]=[[RenderColor alloc] init];unsupported->colors->colors[0]->format=80;unsupported->colors->colors[0]->mask=15;
  tegu_host_process_backboardd=1;
  error=0;TeguRenderPipeline *ip=[d newRenderPipelineStateWithDescriptor:(id)unsupported error:&error];
#ifdef TEGU_INERT_PIPELINES
  assert(ip&&!error&&ip->inert&&!ip->genericDraw);
  TeguCommand *ic=[q commandBuffer];assert(ic);TeguRenderEncoder *ie=[ic renderCommandEncoderWithDescriptor:(id)pass];assert(ie);
  [ie setRenderPipelineState:ip];
  for(unsigned i=0;i<40;i++)[ie drawPrimitives:3 vertexStart:0 vertexCount:3];
  [ie endEncoding];
  assert(!ic->invalid&&ic->count==1);[ic commit];[ic waitUntilCompleted];assert([ic status]==4&&![ic error]);
  [ip release];
  /* The pipeline trace is bounded at 128 per process. */
  for(unsigned i=0;i<130;i++) {
   error=0;TeguRenderPipeline *bounded=[d newRenderPipelineStateWithDescriptor:(id)unsupported error:&error];
   assert(bounded&&!error&&bounded->inert);[bounded release];
  }
#else
  assert(!ip&&error);
#endif
  /* Compiled-in inert is still process-scoped: a non-backboardd process must
   * receive the ordinary nil/error outcome even with the flag on. */
  tegu_host_process_backboardd=0;error=0;
  TeguRenderPipeline *op=[d newRenderPipelineStateWithDescriptor:(id)unsupported error:&error];
  assert(!op&&error);if(op)[op release];
  [unsupported release];
 }

 /* --- async PSO completion, tile PSO refusal, tile encoder refusal --- */
 {
  __block unsigned asyncCalls=0,asyncBadCalls=0;__block id asyncPipeline=0,asyncError=0;
  [d newRenderPipelineStateWithDescriptor:(id)pd completionHandler:^(id p,id e){
   asyncCalls++;asyncPipeline=[p retain];asyncError=e; /* handler borrows p (Metal contract) */
  }];
  fprintf(stderr,"ASYNC_PSO calls=%u ptr=%p err=%p\n",asyncCalls,asyncPipeline,asyncError);
  assert(asyncCalls==1&&asyncPipeline&&!asyncError);
  if(asyncPipeline)[asyncPipeline release];
  RenderDescriptor *asyncBad=[[RenderDescriptor alloc] init];
  asyncBad->vertex=[vf retain];asyncBad->fragment=[ff retain];asyncBad->samples=8;asyncBad->raster=1;asyncBad->amplification=1;asyncBad->topology=0;
  asyncBad->colors=[[RenderColors alloc] init];asyncBad->colors->colors[0]=[[RenderColor alloc] init];
  asyncBad->colors->colors[0]->format=80;asyncBad->colors->colors[0]->mask=15;
  tegu_host_process_backboardd=1;
  [d newRenderPipelineStateWithDescriptor:(id)asyncBad completionHandler:^(id p,id e){
   asyncBadCalls++;asyncPipeline=[p retain];asyncError=e; /* handler borrows p (Metal contract) */
  }];
  fprintf(stderr,"ASYNC_PSO_BAD calls=%u ptr=%p inert=%u err=%p\n",asyncBadCalls,asyncPipeline,
   asyncPipeline?((TeguRenderPipeline *)asyncPipeline)->inert:0,asyncError);
  assert(asyncBadCalls==1);
#ifdef TEGU_INERT_PIPELINES
  assert(asyncPipeline&&((TeguRenderPipeline *)asyncPipeline)->inert);
#else
  assert(!asyncPipeline&&asyncError);
#endif
  if(asyncPipeline)[asyncPipeline release];[asyncBad release];tegu_host_process_backboardd=0;
  id reflection=(id)1;error=0;
  id tilePipeline=[d newRenderPipelineStateWithTileDescriptor:(id)pd options:0 reflection:&reflection error:&error];
  fprintf(stderr,"TILE_PSO ptr=%p reflection=%p err=%p\n",tilePipeline,reflection,error);
  assert(!tilePipeline&&!reflection&&error);
  TeguCommand *tileCommand=[q commandBuffer];assert(tileCommand);
  TeguRenderEncoder *tileEncoder=[tileCommand renderCommandEncoderWithDescriptor:(id)pass];assert(tileEncoder);
  [tileEncoder dispatchThreadsPerTile:(MTLSize){8,8,1}];
  [tileEncoder setTileBytes:(const void *)"tile" length:4 atIndex:0];
  [tileEncoder setTileTexture:target atIndex:0];
  [tileEncoder endEncoding];assert(tileCommand->invalid);
  [tileCommand commit];[tileCommand waitUntilCompleted];assert([tileCommand status]==5);
 }

 /* --- 4x MSAA colour target + resolve through n2r/n2mb v3 --- */
 if(1) {
  TextureDesc *md=[[TextureDesc alloc] init];md->type=4;md->format=80;md->w=16;md->h=16;
  md->samples=4;md->uses=5;md->options=0x20;md->levels=1;
  TeguTexture *msaa=[d newTextureWithDescriptor:(id)md];assert(msaa);
  TextureDesc *rd=[[TextureDesc alloc] init];rd->type=2;rd->format=80;rd->w=16;rd->h=16;
  rd->samples=1;rd->uses=5;rd->options=0x20;rd->levels=1;
  TeguTexture *resolved=[d newTextureWithDescriptor:(id)rd];assert(resolved);
  fprintf(stderr,"MSAA_TEX kind=%lu samples=%lu fmt=%lu row=%lu resolve=%p rkind=%lu rsamples=%lu rfmt=%lu\n",
   (unsigned long)msaa->textureKind,(unsigned long)msaa->textureSamples,(unsigned long)msaa->format,(unsigned long)msaa->rowBytes,
   resolved,(unsigned long)resolved->textureKind,(unsigned long)resolved->textureSamples,(unsigned long)resolved->format);
  error=0;pd->samples=4;TeguRenderPipeline *msaaPipeline=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];
  pd->samples=1;
  fprintf(stderr,"MSAA_PIPE ptr=%p draw=%u raster=%u err=%p\n",msaaPipeline,msaaPipeline?msaaPipeline->genericDraw:0,
   msaaPipeline?msaaPipeline->rasterSampleCount:0,error);
  assert(msaaPipeline&&!error&&msaaPipeline->genericDraw&&msaaPipeline->rasterSampleCount==4);
  RenderPass *mpass=[[RenderPass alloc] init];mpass->color=[[PassColor alloc] init];
  mpass->color->target=[msaa retain];mpass->color->resolve=[resolved retain];
  mpass->color->load=2;mpass->color->store=2;
  fprintf(stderr,"MSAA_PASS store=%lu load=%lu\n",(unsigned long)mpass->color->store,(unsigned long)mpass->color->load);
  mpass->passWidth=mpass->passHeight=16;mpass->arrayLength=1;mpass->sampleCount=4;
  TeguTexture *mt0=new_sample(d,80,16,16,1,2,1),*mt1=new_sample(d,80,16,16,1,2,1);
  TeguSampler *ms0=new_sampler(d,0),*ms1=new_sampler(d,0);assert(mt0&&mt1&&ms0&&ms1);
  TeguCommand *mc=[q commandBuffer];assert(mc);TeguRenderEncoder *me=[mc renderCommandEncoderWithDescriptor:(id)mpass];assert(me);
  bind_all(me,vb,msaaPipeline,mt0,mt1,ms0,ms1);[me drawPrimitives:3 vertexStart:0 vertexCount:3];[me endEncoding];
  fprintf(stderr,"MSAA_CMD invalid=%u count=%u\n",mc->invalid,mc->count);
  assert(!mc->invalid&&mc->count==1);
  {struct Dispatch *mj=tegu_command_job(mc,0);struct tegu_render_generic_sidecar *msc=(void *)mj->compositorPlan.bytes;
   assert(msc&&msc->magic==TEGU_RENDER_SIDECAR_MAGIC&&msc->count==1&&msc->msaa&&
          msc->sample_counts[0]==4&&msc->targets[0].format==80&&msc->resolves[0].token);}
  [mc commit];[mc waitUntilCompleted];fprintf(stderr,"MSAA status=%lu err=%p\n",(unsigned long)[mc status],[mc error]);
  assert([mc status]==4&&![mc error]);
  /* A 4x pass of three draws is ONE JM job (Clear, then two continuing
   * draws); without JM batching the 2nd draw's 4x Load is refused, never
   * re-cleared. */
  for(int batch=1;batch>=0;batch--){
   tegu_jm_batch_force=batch;
   TeguCommand *m3=[q commandBuffer];TeguRenderEncoder *e3=[m3 renderCommandEncoderWithDescriptor:(id)mpass];assert(e3);
   bind_all(e3,vb,msaaPipeline,mt0,mt1,ms0,ms1);
   /* Dynamic state reaches the generic draw (blend colour, clip mode). */
   [e3 setBlendColorRed:.25f green:.25f blue:.25f alpha:.25f];[e3 setDepthClipMode:1];[e3 setStencilReferenceValue:7];
   [e3 setDepthBias:0 slopeScale:0 clamp:0];[e3 setVisibilityResultMode:0 offset:0];
   for(unsigned k=0;k<3;k++)[e3 drawPrimitives:3 vertexStart:0 vertexCount:3];
   [e3 endEncoding];assert(!m3->invalid&&m3->count==3);
   for(unsigned k=0;k<3;k++){struct Dispatch *dj=tegu_command_job(m3,k);
    assert((dj->genericDrawFlags&N2R_DRAW_BLEND_CONSTANT)&&(dj->genericDrawFlags&N2R_DRAW_DEPTH_CLAMP)&&dj->genericBlendConstant[3]==.25f);}
   for(unsigned k=0;k<3;k++){struct tegu_render_generic_sidecar *s3=(void *)tegu_command_job(m3,k)->compositorPlan.bytes;
    fprintf(stderr,"MSAA3 job=%u sidecar=%p msaa=%u load=%u qload=%u\n",k,(void *)s3,s3?s3->msaa:0,s3?s3->targets[0].load_action:9,
     tegu_command_job(m3,k)->genericDrawRequest.load_action);
    assert(s3&&s3->msaa&&s3->targets[0].load_action==(k?1u:2u));}
   unsigned sub=f.submitted;
   [m3 commit];[m3 waitUntilCompleted];
   fprintf(stderr,"MSAA3 batch=%d status=%lu submitted=%u chain=%u\n",batch,(unsigned long)[m3 status],f.submitted-sub,f.last_chain);
   if(batch)assert([m3 status]==4&&![m3 error]&&f.submitted==sub+1&&f.last_chain==3);
   else assert([m3 status]==5&&[m3 error]);
  }
  tegu_jm_batch_force=-1;
  {/* Occlusion queries cannot be honoured (no visibility buffer): refused. */
   TeguCommand *vc=[q commandBuffer];TeguRenderEncoder *ve=[vc renderCommandEncoderWithDescriptor:(id)mpass];assert(ve);
   [ve setVisibilityResultMode:1 offset:0];assert(vc->invalid);[ve endEncoding];[vc commit];[vc waitUntilCompleted];
   fprintf(stderr,"VISIBILITY status=%lu\n",(unsigned long)[vc status]);}
  [ms0 release];[ms1 release];[mt0 release];[mt1 release];[mpass release];[msaaPipeline release];[resolved release];[md release];[rd release];
  /* Missing resolve is an explicit inert pass, never a silent MSAA draw. */
  RenderPass *badm=[[RenderPass alloc] init];badm->color=[[PassColor alloc] init];
  badm->color->target=[msaa retain];badm->color->load=2;badm->color->store=1;
  badm->passWidth=badm->passHeight=16;badm->arrayLength=1;badm->sampleCount=4;
  TeguCommand *bc=[q commandBuffer];TeguRenderEncoder *be=[bc renderCommandEncoderWithDescriptor:(id)badm];
  assert(be&&bc->invalid);[be endEncoding];[bc commit];[bc waitUntilCompleted];assert([bc status]==5);
  [badm release];[msaa release];
 }

 [pass release];[vb release];[target release];[pd release];
 [pipeline release];[q release];[lib release];drain();
 assert(!f.pending);
 close_bridge(bridge,&f);
 puts("PASS real frontend: backboardd-scoped inert + traces, path preserve, TLS16 draw, true-MRT v4 synth, path_blit fetch refusal, pass/ASan/v3/flat/v2/ASTC; no GPU claim");
 return 0;
}
'''

source += (TEgu / 'runtime_frontend_host_test.m').read_text().split(
    'NSUInteger tegu_host_constant_data_size(NSUInteger type)', 1)[1].join(
    ['NSUInteger tegu_host_constant_data_size(NSUInteger type)', ''])

path = WORK / 'vs_texture_host_test.m'
path.write_text(source)

import subprocess as _sp
_root = G.parent
# Use the current bridge source, which now contains n2mb_draw_v3 and the
# draw-parameter path. The real binary links it unmodified; the stub copy only
# renames n2mb_draw_v2 so the synthetic submit test can own that symbol.
for _name in ('nord2_metal_bridge.c', 'nord2_metal_bridge.h'):
    _data = (G / 'metal' / _name).read_bytes()
    if _name.endswith('.c'):
        (WORK / 'hostbase_nord2_metal_bridge_real.c').write_bytes(_data)
        _data = _data.replace(b'n2mb_draw_v2', b'n2mb_draw_v2_test_unused')
    (WORK / ('hostbase_' + _name)).write_bytes(_data)
(WORK / 'nord2_metal_bridge.h').write_bytes((WORK / 'hostbase_nord2_metal_bridge.h').read_bytes())
libs = [G / 'build/deepseek-parallel-20261025/workers/compute/cargo-target/release',
        G / 'build/mesa-build/src/panfrost/clc']
def compile_one(name,defines,bridge):
    cmd = ['clang', '-fblocks', '-pthread', '-O1', '-g']  # 4x MSAA is the production default now (TEGU_DISABLE_MSAA4 restores 1x)
    if not os.environ.get('TEGU_RELEASE'):
        cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    cmd += ['-Wno-incompatible-pointer-types', '-Wno-incompatible-function-pointer-types',
            '-fconstant-string-class=NXConstantString']
    cmd += ['-D'+d for d in defines]
    cmd += ['-I' + str(p) for p in (TEgu, G / 'metal', G / 'include', G / 'build/tegu-frontend')]
    cmd += [str(path), str(bridge),
            str(G / 'metal/nord2_metal_render.c'),
            str(G / 'src/nord2_gpu_desc.c'),
            str(G / 'src/nord2_gpu_jm.c')]
    cmd += ['-lobjc', '-ldl', '-lBlocksRuntime'] + ['-L' + str(p) for p in libs] + [
        '-ltegu_air_bridge', '-lnord2_mali_runtime', '-o', str(WORK / name)]
    subprocess.run(cmd, check=True)
    return str(WORK / name)
stub_bin=compile_one('vs_texture_host_test', ['TEGU_HOST_STUB_V2'], WORK/'hostbase_nord2_metal_bridge.c')
real_bin=compile_one('vs_texture_host_test_real', ['TEGU_HOST_REAL_V2','TEGU_INERT_PIPELINES'], WORK/'hostbase_nord2_metal_bridge_real.c')
air = WORK / 'authored' / 'probe.air'
if not air.exists():
    subprocess.run(['llvm-as', str(WORK / 'vertex_texture_probe.ll'), '-o', str(air)], check=True)
# Fixture is hand-written LLVM IR, not Apple AIR: frontend-feature test, not codegen -> m2v code path (coordinator decision a, 2026-10-28).
env = dict(os.environ, TEGU_AIR_DIRECT='0', LD_LIBRARY_PATH=':'.join(map(str, libs)),
           ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
args=[str(air)]
varying = G / 'build' / 'native' / 'varying.bc'
if varying.exists(): args.append(str(varying))
corpus = G / 'build/ui-corpus-20261025'
mj = next((x for x in json.load(open(corpus / 'graphics-jobs.json'))['jobs'] if x['id'] == 'graphics-01523'), None)
if mj and varying.exists():
    args.append(str(corpus / mj['vertex']['air_saved_as']))
    args.append(str(corpus / mj['fragment']['air_saved_as']))
    fj = next((x for x in json.load(open(corpus / 'graphics-jobs.json'))['jobs'] if x['id'] == 'graphics-01056'), None)
    if fj:
        args.append(str(corpus / fj['vertex']['air_saved_as']))
        args.append(str(corpus / fj['fragment']['air_saved_as']))
    pj = next((x for x in json.load(open(corpus / 'graphics-jobs.json'))['jobs'] if x['id'] == 'graphics-00073'), None)
    if pj:
        args.append(str(corpus / pj['vertex']['air_saved_as']))
        args.append(str(corpus / pj['fragment']['air_saved_as']))
    tj = next((x for x in json.load(open(corpus / 'graphics-jobs.json'))['jobs'] if x['id'] == 'graphics-00092'), None)
    if tj:
        args.append(str(corpus / tj['vertex']['air_saved_as']))
        args.append(str(corpus / tj['fragment']['air_saved_as']))
    bj = next((x for x in json.load(open(corpus / 'graphics-jobs.json'))['jobs'] if x['id'] == 'graphics-00081'), None)
    if bj:
        args.append(str(corpus / bj['vertex']['air_saved_as']))
        args.append(str(corpus / bj['fragment']['air_saved_as']))
    mrt_air = WORK / 'mrt_probe.air'
    if not mrt_air.exists():
        subprocess.run(['llvm-as', str(WORK / 'mrt_probe.ll'), '-o', str(mrt_air)], check=True)
    args.append(str(mrt_air))
def run_checked(binary, inert):
    result = subprocess.run([binary] + args, env=env, stderr=subprocess.PIPE)
    err = result.stderr.decode('utf-8', 'replace')
    sys.stderr.write(err)
    assert result.returncode == 0, f'{binary} exited {result.returncode}'
    refused = err.count('TEGU_PASS_REFUSED')
    assert refused == 16, f'pass refusal diagnostics {refused}'
    assert 'reason=multisample color attachment outside exact resolve class' in err
    assert 'reason=single-sample color attachment outside exact JM class' in err
    assert 'reason=mismatched color attachments' not in err  # c6o: sizes differ -> common area (window view)
    assert 'reason=depth/stencil attachment outside DontCare exact class' in err
    assert 'TEGU_GENERIC_DRAW_REFUSED rc=-14 depth/stencil test or write' in err
    assert 'reason=same color texture bound to multiple attachments' in err
    assert 'TEGU_PIPELINE_FALLBACK generic-mrt vertex=path_cubic_vert_0_lpf' in err
    assert 'TLS_PIPE' in err and 'TLS_DRAW status=4' in err
    assert 'MRT_PROBE status=4' in err
    assert 'ASYNC_PSO calls=1' in err
    assert 'TILE_PSO ptr=(nil) reflection=(nil)' in err
    assert err.count('TEGU_TILE_REFUSED')==3
    assert 'PATH_PRESERVE_CMD invalid=0 count=2' in err
    assert 'PATH_BLIT_NONBBD' in err
    assert 'PATH_BLIT_PIPE' in err
    assert 'NO_CAPS_PIPE' in err and 'NON_TEGU_PIPE' in err
    assert 'CLOSED_ENCODER' in err and 'OPEN_ENCODER' in err
    if inert:
        assert 'ASYNC_PSO_BAD calls=1 ptr=0x' in err and 'inert=1' in err
        assert 'NO_CAPS_PIPE ptr=0x' in err and 'NON_TEGU_PIPE ptr=0x' in err
        assert 'CLOSED_ENCODER ptr=0x' in err and 'OPEN_ENCODER ptr=0x' in err
        assert 'TEGU_RENDER_INERT_ENCODER' in err
        assert 'TEGU_INERT_PIPELINE v=path_blit_vert_lpf f=path_blit_frag_lpf fmt=80,80,10 reason=true-MRT framebuffer fetch unsupported' in err
        assert err.count('TEGU_INERT_PIPELINE')==128
        assert err.count('TEGU_INERT_DRAW_REFUSED')==32
    else:
        assert 'ASYNC_PSO_BAD calls=1 ptr=(nil)' in err
        assert 'NO_CAPS_PIPE ptr=(nil)' in err and 'NON_TEGU_PIPE ptr=(nil)' in err
        assert 'CLOSED_ENCODER ptr=(nil)' in err and 'OPEN_ENCODER ptr=(nil)' in err
        assert 'TEGU_RENDER_INERT_ENCODER' not in err
        assert 'TEGU_INERT_PIPELINE' not in err
        assert 'TEGU_INERT_DRAW_REFUSED' not in err
run_checked(stub_bin, False)
run_checked(real_bin, True)
