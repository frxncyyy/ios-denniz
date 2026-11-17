#!/usr/bin/env python3
"""CoreAnimation texture-path host test through the real frontend.

This is the DELTA12 companion to the small pass/refusal harness.  It builds the
real Nord2 Metal frontend from the render worker's host prelude (read-only),
then independently checks:

  * newTextureWithDescriptor() allocates every mip level with 64-byte rows;
  * an unaligned BGRA8 IOSurface import becomes an exact owned shadow with
    64-byte rows, and its pass is accepted;
  * a hand-mutated unaligned target is explicitly refused by the real pass;
  * a two-attachment 80/80 pass (slot1 writeMask 0) is accepted and gets a
    fake-JM draw job; an independently modelled JM writes target bytes only to
    slot0 and leaves slot1 untouched;
  * Load/Clear/DontCare and deferred Unknown-store behavior;
  * a 115/115 clear pass is accepted at the pass level (the Rust generic
    planner's 115 two-target draw refusal is recorded separately, not waived).

The fake JM returns status 4 and no real GPU memory is touched by it.  The
target-byte writer below is a test oracle, not a production renderer.
"""
from __future__ import annotations

import json
import os
import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
WORK = HERE.parents[4]
GPU = WORK / "gpu"
RENDER = GPU / "build/deepseek-parallel-20261025/workers/render"
TEgu = GPU / "metal/tegu"
CORPUS = GPU / "build/ui-corpus-20261025"
HARNESS = RENDER / "host_test_vs_texture.py"
BUILD = HERE / "build"
RUNS = HERE / "runs"
CLANG = os.environ.get("CLANG", shutil.which("clang") or "clang")

# Reuse the render host prelude only to construct the real frontend; the main
# below is this worker's own independent test oracle.
text = HARNESS.read_text()
prelude = text.split("path = WORK / 'vs_texture_host_test.m'", 1)[0]
ns = {"__file__": str(HARNESS), "__name__": "coreanimation_frontend_prelude"}
exec(compile(prelude, str(HARNESS), "exec"), ns)
source = ns["source"]
prefix = source.split("int main(int argc,char **argv){", 1)[0]
tail = (TEgu / "runtime_frontend_host_test.m").read_text().split(
    "NSUInteger tegu_host_constant_data_size(NSUInteger type)", 1)[1].join(
    ["NSUInteger tegu_host_constant_data_size(NSUInteger type)", ""])

main = r'''
static unsigned ca_checks, ca_failures, ca_pending;
static void ca_check(int ok,const char *what){
 ca_checks++;if(!ok){fprintf(stderr,"FAIL [required] %s\n",what);ca_failures++;}
}
static void ca_pending_note(const char *what){
 ca_pending++;fprintf(stderr,"COREANIMATION_PENDING %s\n",what);
}
static void *ca_read(const char *path,long *length){
 FILE *f=fopen(path,"rb");if(!f)return 0;fseek(f,0,SEEK_END);*length=ftell(f);rewind(f);
 void *bytes=malloc((size_t)*length);if(!bytes||fread(bytes,1,(size_t)*length,f)!=(size_t)*length){fclose(f);free(bytes);return 0;}
 fclose(f);return bytes;
}
static TeguFunction *ca_function(TeguComputeDevice *d,const char *path,const char *name){
 long n=0;void *bytes=ca_read(path,&n);if(!bytes){fprintf(stderr,"COREANIMATION_FAIL read %s\n",path);return 0;}
 id error=0;TeguLibrary *lib=[d newTeguLibraryWithBytes:bytes length:(NSUInteger)n error:&error];free(bytes);
 if(!lib||error)return 0;
 TeguFunction *fn=[lib newFunctionWithName:[NSString stringWithUTF8String:name]];
 [lib release];return fn;
}
/* Exact CA-shaped descriptor: two targets, slot1 mask0, unused 2..7 have
 * Metal's default mask15 but format0 (the C6f unbound-slot case). */
static RenderDescriptor *ca_pipeline_descriptor(TeguFunction *v,TeguFunction *f,
 NSUInteger fmt0,NSUInteger mask0,NSUInteger fmt1,NSUInteger mask1,int blend){
 RenderDescriptor *pd=[[RenderDescriptor alloc] init];
 pd->vertex=[v retain];pd->fragment=[f retain];pd->samples=1;pd->raster=1;pd->amplification=1;pd->topology=0;
 pd->colors=[[RenderColors alloc] init];
 for(unsigned i=0;i<8;i++){pd->colors->colors[i]=[[RenderColor alloc] init];pd->colors->colors[i]->mask=15;}
 pd->colors->colors[0]->format=fmt0;pd->colors->colors[0]->mask=mask0;
 pd->colors->colors[1]->format=fmt1;pd->colors->colors[1]->mask=mask1;
 if(blend){RenderColor *c=pd->colors->colors[0];c->blend=1;c->rgbSrc=1;c->rgbDst=5;c->alphaSrc=1;c->alphaDst=5;}
 RenderLayout *l=[[RenderLayout alloc] init];l->buffers=[[RenderLayout alloc] init];
 const NSUInteger fmts[4]={31,29,29,27},offs[4]={0,16,24,32};
 for(unsigned i=0;i<4;i++){RenderAttribute *a=[[RenderAttribute alloc] init];a->value=fmts[i];a->byteOffset=offs[i];a->bufferSlot=1;l->entries[i]=a;}
 RenderAttribute *b=[[RenderAttribute alloc] init];b->layoutStride=48;b->step=1;b->rate=1;l->buffers->entries[1]=b;
 pd->layout=l;return pd;
}
static TeguTexture *ca_texture(TeguComputeDevice *d,NSUInteger fmt,NSUInteger w,NSUInteger h,
 NSUInteger levels,NSUInteger usage,NSUInteger options){
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=fmt;desc->w=w;desc->h=h;desc->levels=levels;desc->uses=usage;desc->options=options;
 TeguTexture *t=[d newTextureWithDescriptor:(id)desc];[desc release];return t;
}
static void ca_fill_texture(TeguTexture *t,unsigned char value){
 if(!t||!t->buffer)return;memset(tegu_buffer_bytes(t->buffer),value,(size_t)t->buffer->logical);
}
static unsigned char *ca_bytes(TeguTexture *t){return t&&t->buffer?tegu_buffer_bytes(t->buffer):0;}

/* Independent format oracle: only the formats exercised here. */
static unsigned ca_bpp(unsigned fmt){return fmt==80?4:fmt==115?8:0;}
static uint16_t ca_half(double v){
 if(v<=0.0)return 0; if(v>=1.0)return 0x3c00;
 int exp=0;double m=v;while(m<1.0){m*=2;exp--;}while(m>=2.0){m*=0.5;exp++;}
 return (uint16_t)((((unsigned)(exp+15))<<10)|((unsigned)((m-1.0)*1024.0+0.5)&0x3ffu));
}
static void ca_clear_words(unsigned fmt,const double rgba[4],uint32_t out[4]){
 if(fmt==80){uint32_t p=0;for(unsigned i=0;i<4;i++){double v=rgba[i];if(v<0)v=0;if(v>1)v=1;p|=(uint32_t)(v*255.0+0.5)<<(8*i);}out[0]=out[1]=out[2]=out[3]=p;return;}
 uint16_t h[4];for(unsigned i=0;i<4;i++)h[i]=ca_half(rgba[i]);
 out[0]=(uint32_t)h[0]|((uint32_t)h[1]<<16);out[1]=(uint32_t)h[2]|((uint32_t)h[3]<<16);out[2]=out[3]=0;
}
static void ca_clear_pixel(unsigned fmt,const uint32_t w[4],unsigned char out[8]){
 memset(out,0,8);
 if(fmt==80){uint32_t c=w[0];out[0]=(unsigned char)(c>>16);out[1]=(unsigned char)(c>>8);out[2]=(unsigned char)c;out[3]=(unsigned char)(c>>24);}
 else if(fmt==115)memcpy(out,w,8);
}
static void ca_write_pixel(TeguTexture *t,unsigned fmt,const unsigned char *pixel){
 unsigned bpp=ca_bpp(fmt);unsigned char *bytes=ca_bytes(t);if(!t||!bytes||!bpp)return;
 for(NSUInteger y=0;y<t->textureHeight;y++){unsigned char *row=bytes+t->mip[0].offset+y*t->rowBytes;
  for(NSUInteger x=0;x<t->textureWidth;x++)memcpy(row+x*bpp,pixel,bpp);}
}
static void ca_check_pixels(TeguTexture *t,unsigned fmt,const unsigned char *pixel,const char *label){
 unsigned bpp=ca_bpp(fmt);unsigned char *bytes=ca_bytes(t);char what[240];
 if(!t||!bytes||!bpp){snprintf(what,sizeof(what),"%s target bytes",label);ca_check(0,what);return;}
 NSUInteger xs[3]={0,t->textureWidth/2,t->textureWidth-1};
 NSUInteger ys[3]={0,t->textureHeight/2,t->textureHeight-1};
 int ok=1;for(unsigned yi=0;yi<3;yi++)for(unsigned xi=0;xi<3;xi++){
  NSUInteger off=t->mip[0].offset+ys[yi]*t->rowBytes+xs[xi]*bpp;
  if(memcmp(bytes+off,pixel,bpp))ok=0;}
 snprintf(what,sizeof(what),"%s last/mid rows at allocator rowBytes %lu",label,(unsigned long)t->rowBytes);ca_check(ok,what);
 ca_check(!(t->rowBytes&63),label);
}
/* Independent fake-JM target writer. Clear jobs carry clear_words; the draw
 * model writes slot0 only because the pipeline's slot1 mask is zero. */
static void ca_apply_jobs(TeguCommand *c,TeguTexture *t0,TeguTexture *t1,
 const double *rgba0,const double *rgba1,const unsigned char *draw,unsigned drawbpp,
 unsigned *clears,unsigned *draws){
 *clears=*draws=0;
 for(unsigned i=0;i<c->count;i++){
  struct Dispatch *j=tegu_command_job(c,i);
  if(j->nord2ClearOnly){
   TeguTexture *t=(TeguTexture *)j->renderTexture;struct tegu_generic_draw_request *q=&j->genericDrawRequest;
   unsigned fmt=q->target_format;uint32_t expected[4]={0};unsigned char pixel[8];
   const double *rgba=t==t0?rgba0:t==t1?rgba1:0;
   if(!t||!rgba||!ca_bpp(fmt)){ca_check(0,"clear job has a known target/format");continue;}
   ca_clear_words(fmt,rgba,expected);
   ca_check(!memcmp(expected,q->clear_words,sizeof(expected)),"clear words match independent oracle");
   ca_clear_pixel(fmt,expected,pixel);ca_write_pixel(t,fmt,pixel);(*clears)++;
  }
  if(j->genericDrawSnapshot){
   if(!draw||drawbpp!=ca_bpp(t0->format)){ca_check(0,"draw job has a known slot0 format");continue;}
   ca_write_pixel(t0,t0->format,draw);(*draws)++;
  }
 }
}
static TeguTexture *ca_new_shadow(TeguComputeDevice *d,NSUInteger w,NSUInteger h,NSUInteger row,
 unsigned long long alloc){
 test_surface.width=w;test_surface.height=h;test_surface.row=row;test_surface.alloc=(size_t)alloc;
 test_surface.planes=0;test_surface.fourcc=TEGU_IOSURFACE_BGRA;
 TextureDesc *sd=[[TextureDesc alloc] init];sd->format=80;sd->w=w;sd->h=h;sd->uses=5;sd->levels=1;
 TeguTexture *s=[d newTextureWithDescriptor:(id)sd iosurface:(id)&test_surface plane:0];[sd release];return s;
}
static void ca_run_pair(TeguComputeDevice *d,TeguTexture *t0,TeguTexture *t1,
 TeguRenderPipeline *p,NSUInteger load0,NSUInteger load1,int do_draw,const char *label){
 const double clear_rgba[4]={17.0/255,34.0/255,51.0/255,68.0/255};
 const double zero_rgba[4]={0,0,0,0};
 const unsigned char draw[8]={0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
 RenderPass *pass=[[RenderPass alloc] init];
 pass->color=[[PassColor alloc] init];pass->color->target=[t0 retain];pass->color->load=load0;pass->color->store=4;
 pass->second=[[PassColor alloc] init];pass->second->target=[t1 retain];pass->second->load=load1;pass->second->store=4;
 pass->passWidth=t0->textureWidth;pass->passHeight=t0->textureHeight;pass->arrayLength=1;pass->sampleCount=1;
 TeguQueue *q=[d newCommandQueue];TeguCommand *c=[q commandBuffer];
 ca_fill_texture(t0,0xa5);ca_fill_texture(t1,0xa5);
 TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
 char what[240];snprintf(what,sizeof(what),"%s pass accepted",label);ca_check(e&&!e->inert&&!c->invalid,what);
 if(!e||e->inert||c->invalid){ca_check(0,"pass did not return a usable encoder");[pass release];[c release];[q release];return;}
 [e setColorStoreAction:1 atIndex:0];[e setColorStoreAction:0 atIndex:1];
 if(do_draw){
  TeguBuffer *vb=[d newBufferWithLength:4096 options:0];TeguTexture *src=new_sample(d,80,64,64,1,2,1);TeguSampler *samp=new_sampler(d,3);
  ca_check(vb&&src&&samp,"draw resources allocated");
  if(vb&&src&&samp){
   [e setRenderPipelineState:p];[e setVertexBuffer:vb offset:0 atIndex:1];
   [e setFragmentTexture:src atIndex:0];[e setFragmentSamplerState:samp atIndex:0];
   [e drawPrimitives:3 vertexStart:0 vertexCount:3];
  }
  [samp release];[src release];[vb release];
 }
 [e endEncoding];
 snprintf(what,sizeof(what),"%s command remains valid",label);ca_check(!c->invalid&&c->status==0,what);
 unsigned clears=0,draws=0;ca_apply_jobs(c,t0,t1,clear_rgba,zero_rgba,do_draw?draw:0,do_draw?ca_bpp(t0->format):0,&clears,&draws);
 snprintf(what,sizeof(what),"%s expected clear count",label);ca_check(clears==(unsigned)((load0!=1)+(load1!=1)),what);
 if(do_draw){snprintf(what,sizeof(what),"%s one fake draw job",label);ca_check(draws==1,what);
  ca_check_pixels(t0,t0->format,draw,label);
 } else if(load0!=1){unsigned char pixel[8];uint32_t w[4]={0};ca_clear_words(t0->format,clear_rgba,w);ca_clear_pixel(t0->format,w,pixel);
  ca_check_pixels(t0,t0->format,pixel,label);}
 if(load1==1){unsigned char sentinel[8];memset(sentinel,0xa5,sizeof(sentinel));ca_check_pixels(t1,t1->format,sentinel,label);}
 else {unsigned char pixel[8];uint32_t w[4]={0};ca_clear_words(t1->format,zero_rgba,w);ca_clear_pixel(t1->format,w,pixel);
  ca_check_pixels(t1,t1->format,pixel,label);}
 [c commit];[c waitUntilCompleted];
 snprintf(what,sizeof(what),"%s fake JM completed",label);ca_check([c status]==4&&![c error],what);
 [pass release];[c release];[q release];
}

int main(int argc,char **argv){
 assert(argc==4);
 tegu_host_process_backboardd=1;
 struct fake f={0};uintptr_t bridge=open_bridge(&f);
 TeguComputeDevice *d=[TeguComputeDevice alloc];
 d->connection=bridge;d->scalar=scalar_adapter;d->method=method_adapter;d->map=map_adapter;d->unmap=unmap_adapter;
 d->capabilities=(1ULL<<14)|(1ULL<<18)|256ULL|64ULL|32ULL|16ULL;
 TeguFunction *dsv=ca_function(d,argv[1],"downsample_blur_vert_lph");
 TeguFunction *nb=ca_function(d,argv[2],"narrow_blur_11_frag_lph");
 TeguFunction *ds4=ca_function(d,argv[3],"downsample_4_frag_lph");
 ca_check(dsv&&nb&&ds4,"real QuartzCore functions loaded");
 if(!dsv||!nb||!ds4){printf("COREANIMATION_FRONTEND checks=%u failures=%u pending=%u\n",ca_checks,ca_failures,ca_pending);return 1;}

 /* (1) allocator shape/row oracle, including every mip level. */
 struct {NSUInteger fmt,w,h,levels,usage,options;} shapes[]={
  {80,1080,2400,1,5,0x20},{80,1170,2532,1,5,0x20},{115,540,1200,1,5,0x20},
  {80,1125,2436,1,4,0},{115,1125,2436,1,5,0x20},{80,776,24,10,5,0x20},
  {115,776,24,10,5,0x20},{80,563,1218,3,5,0},
 };
 for(unsigned i=0;i<sizeof(shapes)/sizeof(shapes[0]);i++){
  TeguTexture *t=ca_texture(d,shapes[i].fmt,shapes[i].w,shapes[i].h,shapes[i].levels,shapes[i].usage,shapes[i].options);
  char what[220];snprintf(what,sizeof(what),"allocator returned CA shape %u",i);ca_check(t&&t->buffer,what);
  if(!t){continue;}
  ca_check(!(t->rowBytes&63)&&t->rowBytes>=t->textureWidth*ca_bpp((unsigned)t->format),"allocated rowBytes is 64-byte aligned");
  for(unsigned l=0;l<t->levelCount;l++){
   snprintf(what,sizeof(what),"allocator mip level %u row/offset aligned",l);
   ca_check(!(t->mip[l].stride&63)&&!(t->mip[l].offset&63)&&
    t->mip[l].stride>=t->mip[l].width*n2r_bpp((unsigned)t->format),what);
   snprintf(what,sizeof(what),"allocator mip level %u fits backing",l);
   ca_check(t->mip[l].offset+t->mip[l].stride*t->mip[l].height<=t->buffer->logical,what);
  }
  fprintf(stderr,"COREANIMATION_TEX fmt=%lu %lux%lu levels=%lu row=%lu\n",(unsigned long)t->format,
   (unsigned long)t->textureWidth,(unsigned long)t->textureHeight,(unsigned long)t->levelCount,(unsigned long)t->rowBytes);
  [t release];
 }

 /* Real 80/80 pipeline with slot1 mask0, and a 115 single-target descriptor.
  * The latter is allocation/pass coverage; [115,115] draws remain pending on
  * the Rust generic planner (lowering board item A). */
 id error=0;RenderDescriptor *pd80=ca_pipeline_descriptor(dsv,nb,80,15,80,0,0);
 TeguRenderPipeline *p80=[d newRenderPipelineStateWithDescriptor:(id)pd80 error:&error];
 RenderDescriptor *pd115=ca_pipeline_descriptor(dsv,nb,115,15,0,0,0);
 TeguRenderPipeline *p115=[d newRenderPipelineStateWithDescriptor:(id)pd115 error:&error];
 if(!p80||p80->inert||!p80->genericDraw){ca_check(0,"80/80 slot1-mask0 pipeline accepted");}
 else ca_check(1,"80/80 slot1-mask0 pipeline accepted");

 /* (2) 80/80 Load+Load draw: no clear, fake draw writes slot0 only. */
 {
  TeguTexture *t0=ca_texture(d,80,1080,2400,1,5,0x20),*t1=ca_texture(d,80,1080,2400,1,5,0x20);
  ca_check(t0&&t1,"80/80 target allocation");
  if(t0&&t1)ca_run_pair(d,t0,t1,p80,1,1,1,"CA 80/80 Load+Load draw");
  [t0 release];[t1 release];
 }
 /* (3) 80/80 Clear+Load draw: clear job and draw, RT1 untouched. */
 {
  TeguTexture *t0=ca_texture(d,80,1170,2532,1,5,0x20),*t1=ca_texture(d,80,1170,2532,1,5,0x20);
  ca_check(t0&&t1,"80/80 Clear+Load target allocation");
  if(t0&&t1)ca_run_pair(d,t0,t1,p80,2,1,1,"CA 80/80 Clear+Load draw");
  [t0 release];[t1 release];
 }
 /* (4) 115/115 clear/DontCare pass: pass acceptance and clear bytes; no
  * pipeline draw claim while the Rust generic planner still refuses 115 MRT. */
 {
  TeguTexture *t0=ca_texture(d,115,540,1200,1,5,0x20),*t1=ca_texture(d,115,540,1200,1,5,0x20);
  ca_check(t0&&t1,"115/115 target allocation");
  if(t0&&t1)ca_run_pair(d,t0,t1,0,2,0,0,"CA 115/115 Clear+DontCare pass");
  [t0 release];[t1 release];
  fprintf(stderr,"COREANIMATION_PENDING 115 two-target draw is not claimed (Rust generic planner item A)\n");
 }

 /* (5) IOSurface unaligned BGRA8 -> owned shadow, 64B rows, accepted/drawn. */
 {
  d->capabilities|=(1ULL<<15);
  TeguTexture *t0=ca_new_shadow(d,1080,2400,4320,(unsigned long long)1080*2400*4+4096);
  ca_check(t0&&t0->shadowSurface&&!t0->surfaceImported,"unaligned IOSurface becomes owned shadow");
  if(t0){ca_check(!(t0->rowBytes&63)&&t0->rowBytes>=1080*4,"IOSurface shadow row is 64-byte aligned");
   TeguTexture *t1=ca_texture(d,80,1080,2400,1,5,0x20);ca_check(t1!=0,"IOSurface companion target allocation");
   if(t1)ca_run_pair(d,t0,t1,p80,2,1,1,"CA IOSurface-shadow Clear+Load draw");
   [t1 release];
  }
  [t0 release];
 }

 /* (6) Explicit refusal: a hand-mutated, unaligned target must not enter. */
 {
  TeguTexture *t=ca_texture(d,80,1080,2400,1,5,0x20);ca_check(t&&t->buffer,"unaligned refusal target allocated");
  if(t){
   t->rowBytes=4320;t->mip[0].stride=4320;
   RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];
   pass->color->target=[t retain];pass->color->load=2;pass->color->store=4;
   pass->passWidth=1080;pass->passHeight=2400;pass->arrayLength=1;pass->sampleCount=1;
   TeguQueue *q=[d newCommandQueue];TeguCommand *c=[q commandBuffer];
   TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
   ca_check(e&&e->inert&&c->invalid,"hand-made unaligned target explicitly refused");
   [pass release];[c release];[q release];[t release];
  }
 }

 /* (7) 115 single-target allocation/pass acceptance without a draw claim. */
 if(p115&&!p115->inert&&p115->genericDraw){
  TeguTexture *t=ca_texture(d,115,540,1200,1,5,0x20);ca_check(t!=0,"115 single target allocation");
  [t release];ca_check(1,"115 single pipeline accepted");
 } else ca_pending_note("115 single pipeline not claimed");

 printf("COREANIMATION_FRONTEND checks=%u failures=%u pending=%u pipeline_builder=real "
        "jm_status=4\n",ca_checks,ca_failures,ca_pending);
 [ds4 release];[nb release];[dsv release];[p80 release];[p115 release];[pd80 release];[pd115 release];
 [d release];close_bridge(bridge,&f);drain();
 return ca_failures?1:0;
}
'''
out = BUILD / "coreanimation_frontend_test.m"
BUILD.mkdir(parents=True, exist_ok=True)
RUNS.mkdir(parents=True, exist_ok=True)
out.write_text(prefix + main + tail)
# Use the current production bridge implementation, copied under the worker.
bridge_src = BUILD / "hostbase_coreanimation_nord2_metal_bridge.c"
bridge_hdr = BUILD / "nord2_metal_bridge.h"
bridge_src.write_bytes((GPU / "metal/nord2_metal_bridge.c").read_bytes())
bridge_hdr.write_bytes((GPU / "metal/nord2_metal_bridge.h").read_bytes())

jobs = {x["id"]: x for x in json.load(open(CORPUS / "graphics-jobs.json"))["jobs"]}
def air(job: str, stage: str) -> str:
    return str(CORPUS / jobs[job][stage]["air_saved_as"])
args = [air("graphics-00139", "vertex"), air("graphics-01847", "fragment"),
        air("graphics-00139", "fragment")]
libs = [GPU / "build/deepseek-parallel-20261025/workers/compute/cargo-target/release",
        GPU / "build/mesa-build/src/panfrost/clc"]

def build(name: str, asan: bool) -> pathlib.Path:
    exe = BUILD / name
    cmd = [CLANG, "-fblocks", "-pthread", "-O1", "-g"]
    if asan:
        cmd += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    cmd += ["-Wno-incompatible-pointer-types", "-Wno-incompatible-function-pointer-types",
            "-fconstant-string-class=NXConstantString",
            "-DTEGU_HOST_REAL_V2", "-DTEGU_INERT_PIPELINES"]
    cmd += ["-I" + str(HERE), "-I" + str(TEgu), "-I" + str(GPU / "metal"),
            "-I" + str(GPU / "include"), "-I" + str(GPU / "build/tegu-frontend")]
    cmd += [str(out), str(bridge_src), str(GPU / "metal/nord2_metal_render.c"),
            str(GPU / "src/nord2_gpu_desc.c"), str(GPU / "src/nord2_gpu_jm.c")]
    cmd += ["-lobjc", "-ldl", "-lBlocksRuntime"] + ["-L" + str(p) for p in libs]
    cmd += ["-ltegu_air_bridge", "-lnord2_mali_runtime", "-o", str(exe)]
    subprocess.run(cmd, check=True)
    return exe

def main_run() -> int:
    env = dict(os.environ, LD_LIBRARY_PATH=":".join(map(str, libs)),
               ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
    failed = False
    all_checks = all_failures = all_pending = 0
    for tag, asan in (("release", False), ("asan", True)):
        try:
            exe = build(f"coreanimation_frontend_test_{tag}", asan)
        except subprocess.CalledProcessError as exc:
            print(f"FAIL {tag} build rc={exc.returncode}", file=sys.stderr)
            failed = True
            continue
        proc = subprocess.run([str(exe)] + args, capture_output=True, text=True, env=env)
        textout = proc.stdout + proc.stderr
        log = RUNS / f"delta12_coreanimation_frontend_{tag}.log"
        log.write_text(textout)
        print(f"== CoreAnimation frontend {tag} ==")
        print(textout.strip())
        match = __import__("re").search(r"COREANIMATION_FRONTEND checks=(\d+) failures=(\d+) pending=(\d+)", textout)
        if not match:
            print(f"FAIL {tag}: summary missing", file=sys.stderr)
            failed = True
            continue
        checks, failures, pending = map(int, match.groups())
        all_checks += checks; all_failures += failures; all_pending += pending
        if proc.returncode != 0 or failures:
            print(f"FAIL {tag}: checks={checks} failures={failures} pending={pending} rc={proc.returncode}", file=sys.stderr)
            failed = True
        else:
            print(f"PASS {tag}: checks={checks} failures=0 pending={pending}")
    print(f"COREANIMATION_FRONTEND_RESULT checks={all_checks} failures={all_failures} pending={all_pending}")
    return 1 if failed else 0

if __name__ == "__main__":
    raise SystemExit(main_run())
