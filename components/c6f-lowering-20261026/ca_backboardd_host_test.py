#!/usr/bin/env python3
"""C6f host test: backboardd (CoreAnimation) shapes from the c6ec device log.

Reuses the real-frontend prelude of the render worker's harness
(workers/render/host_test_vs_texture.py, read-only) and adds its own main:

(2) real QuartzCore blur/home/attachment-clear AIR with the exact descriptor
    shape logged by backboardd: slot0 bound, slot1 bound with writeMask 0,
    slots 2..7 pixelFormat 0 with Metal's DEFAULT writeMask 15, vertex layout
    slot1 stride 48 (attributes 31@0,29@16,29@24,27@32). Before C6f every one
    became an inert/observer PSO ("Nord2 general attachment path").
(1) render targets allocated through the real allocator with CA-shaped
    descriptors (full-screen private BGRA8/RGBA16F, mipmapped 776x24x10
    BGRA8) must have 64-byte rows at every level and be admitted by
    nord2_render_pass (store Unknown + setColorStoreAction), and a
    two-attachment blur pass draws with RT1 untouched (clear kept, no
    snapshot copies). IOSurface: an unaligned BGRA8 stride becomes an exact
    owned shadow; a two-plane 'b3a8' surface returns nil with
    TEGU_IOSURFACE_REFUSED.
No GPU, phone, adb or publish. Host-only.
"""
import json, os, pathlib, subprocess, sys

HERE = pathlib.Path(__file__).resolve().parent
G = HERE.parents[1]
RENDER = G / 'build/deepseek-parallel-20261025/workers/render'
HARNESS = RENDER / 'host_test_vs_texture.py'
TEgu = G / 'metal' / 'tegu'
CORPUS = G / 'build/ui-corpus-20261025'

text = HARNESS.read_text()
prelude = text.split("path = WORK / 'vs_texture_host_test.m'", 1)[0]
ns = {'__file__': str(HARNESS), '__name__': 'c6f_prelude'}
exec(compile(prelude, str(HARNESS), 'exec'), ns)
source = ns['source']
prefix = source.split('int main(int argc,char **argv){', 1)[0]
tail = (TEgu / 'runtime_frontend_host_test.m').read_text().split(
    'NSUInteger tegu_host_constant_data_size(NSUInteger type)', 1)[1].join(
    ['NSUInteger tegu_host_constant_data_size(NSUInteger type)', ''])

# Test-only: the shared fake kernel has no IOSurface import selector; model it
# as an allocation-shaped handle so imported display targets can be exercised.
_imp = """ case NORD2_GPU_SEL_IMPORT_SURFACE: {
  assert(ni==4&&*no==3&&in[0]&&in[2]);
  if(c6f_budget){uint64_t live=0;for(unsigned q=1;q<=f->next&&q<COUNT;q++)if(f->buffers[q].active)live+=f->buffers[q].size;
   uint64_t need=(in[2]+16383)&~16383ull;if(live+need>c6f_budget){c6f_refused++;return (int)0xe00002c2u;}if(live+need>c6f_peak)c6f_peak=live+need;}
  unsigned h=++f->next;assert(h<COUNT);struct buffer *b=&f->buffers[h];
  b->size=(in[2]+16383)&~16383ull;b->va=0x1000000000ull+h*0x2000000ull;
  b->data=calloc(1,b->size);assert(b->data);b->active=1;b->exec=0;f->live++;
  out[0]=h;out[1]=b->va;out[2]=b->size;*no=3;return 0;
 }
 case NORD2_GPU_SEL_SYNC:"""
assert prefix.count(" case NORD2_GPU_SEL_SYNC:") == 1
prefix = prefix.replace(" case NORD2_GPU_SEL_SYNC:", _imp, 1)
_alloc = "  assert(ni==2&&*no==3&&!size);if(f->fail_alloc){f->fail_alloc--;return -10;}"
assert prefix.count(_alloc) == 1
# c6hc: the device context budget (64 MiB incl. imported surfaces) refuses a
# further 11 MB allocation with kIOReturnBadArgument; model it on demand.
_budget = "  if(c6f_budget){uint64_t live=0;for(unsigned q=1;q<=f->next&&q<COUNT;q++)if(f->buffers[q].active)live+=f->buffers[q].size;\\\n   uint64_t need=(in[IDX]+16383)&~16383ull;if(live+need>c6f_budget){c6f_refused++;return (int)0xe00002c2u;}if(live+need>c6f_peak)c6f_peak=live+need;}"
prefix = prefix.replace(_alloc, "  assert(ni==2&&*no==3&&!size);if(f->fail_alloc){f->fail_alloc--;return -10;}\n  if(c6f_alloc_cap&&in[0]>c6f_alloc_cap)return (int)0xe00002c2u;\n" + _budget.replace('IDX','0').replace('\\\n','\n'), 1)
import re as _re
assert len(_re.findall(r'#define COUNT \d+', prefix)) == 1
prefix = _re.sub(r'#define COUNT \d+', '#define COUNT 20000', prefix, count=1)
assert prefix.count("static int call(void *p,unsigned s,") == 1
prefix = prefix.replace("static int call(void *p,unsigned s,", "static uint64_t c6f_alloc_cap,c6f_budget,c6f_peak;static unsigned c6f_refused;\nstatic int call(void *p,unsigned s,", 1)
_base = "static void *test_si_base(void *p){return ((struct TestIOSurface*)p)->data;}"
assert prefix.count(_base) == 1
prefix = prefix.replace(_base, "static unsigned char *c6f_big_surface;\nstatic void *test_si_base(void *p){return c6f_big_surface&&((struct TestIOSurface*)p)->width>64?c6f_big_surface:((struct TestIOSurface*)p)->data;}", 1)
fixed_ui = (TEgu / 'fixed_ui_frontend_host_test.inc').read_text().split('static void test_fixed_ui_native_frontend', 1)[0]
main = r'''
static void *c6f_read(const char *p,long *n){FILE *f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);*n=ftell(f);rewind(f);
 void *b=malloc((size_t)*n);assert(b&&fread(b,1,(size_t)*n,f)==(size_t)*n);fclose(f);return b;}
static TeguFunction *c6f_function(TeguComputeDevice *d,const char *path,const char *name){
 long n=0;void *b=c6f_read(path,&n);id error=0;
 TeguLibrary *l=[d newTeguLibraryWithBytes:b length:(NSUInteger)n error:&error];free(b);assert(l&&!error);
 TeguFunction *f=[l newFunctionWithName:[NSString stringWithUTF8String:name]];assert(f);return f;
}
/* Exact backboardd descriptor (c6ec TEGU_RENDER_* captures). */
static RenderDescriptor *c6f_ca_descriptor(TeguFunction *v,TeguFunction *f,NSUInteger fmt,NSUInteger mask0,NSUInteger mask1,int blend){
 RenderDescriptor *pd=[[RenderDescriptor alloc] init];
 pd->vertex=[v retain];pd->fragment=[f retain];pd->samples=1;pd->raster=1;pd->amplification=1;pd->topology=0;
 pd->colors=[[RenderColors alloc] init];
 for(unsigned i=0;i<8;i++){pd->colors->colors[i]=[[RenderColor alloc] init];pd->colors->colors[i]->mask=15;}
 pd->colors->colors[0]->format=fmt;pd->colors->colors[0]->mask=mask0;
 pd->colors->colors[1]->format=fmt;pd->colors->colors[1]->mask=mask1;
 if(blend){RenderColor *c=pd->colors->colors[0];c->blend=1;c->rgbSrc=1;c->rgbDst=5;c->alphaSrc=1;c->alphaDst=5;}
 RenderLayout *l=[[RenderLayout alloc] init];l->buffers=[[RenderLayout alloc] init];
 const NSUInteger fmts[4]={31,29,29,27},offs[4]={0,16,24,32};
 for(unsigned i=0;i<4;i++){RenderAttribute *a=[[RenderAttribute alloc] init];a->value=fmts[i];a->byteOffset=offs[i];a->bufferSlot=1;l->entries[i]=a;}
 RenderAttribute *b=[[RenderAttribute alloc] init];b->layoutStride=48;b->step=1;b->rate=1;l->buffers->entries[1]=b;
 pd->layout=l;return pd;
}
static TeguTexture *c6f_texture(TeguComputeDevice *d,NSUInteger fmt,NSUInteger w,NSUInteger h,NSUInteger levels,NSUInteger usage,NSUInteger options){
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=fmt;desc->w=w;desc->h=h;desc->levels=levels;desc->uses=usage;desc->options=options;
 TeguTexture *t=[d newTextureWithDescriptor:(id)desc];[desc release];return t;
}
int main(int argc,char **argv){
 assert(argc==11);
 tegu_host_process_backboardd=1;
 struct fake f={0};uintptr_t bridge=open_bridge(&f);
 TeguComputeDevice *d=[TeguComputeDevice alloc];
 d->connection=bridge;d->scalar=scalar_adapter;d->method=method_adapter;d->map=map_adapter;d->unmap=unmap_adapter;
 d->capabilities=(1ULL<<14)|(1ULL<<18)|256ULL|64ULL|32ULL|16ULL;
 unsigned checks=0;
 TeguFunction *dsv=c6f_function(d,argv[1],"downsample_blur_vert_lph");
 TeguFunction *ds4=c6f_function(d,argv[2],"downsample_4_frag_lph");
 TeguFunction *nb11=c6f_function(d,argv[3],"narrow_blur_11_frag_lph");
 TeguFunction *hav=c6f_function(d,argv[4],"std_vert1_lph");
 TeguFunction *haf=c6f_function(d,argv[5],"home_affordance_base_lph");
 TeguFunction *pbv=c6f_function(d,argv[6],"path_blit_vert_lph");
 TeguFunction *acf=c6f_function(d,argv[7],"attachment_clear_frag_lph");
 struct {TeguFunction *v,*f;NSUInteger fmt,m0,m1;int blend;unsigned main;const char *name;} cases[]={
  {dsv,ds4,80,15,0,1,0,"downsample_4 80,80 blend"},
  {dsv,ds4,80,15,0,0,0,"downsample_4 80,80"},
  {dsv,nb11,80,15,0,0,0,"narrow_blur_11 80,80"},
  {dsv,nb11,115,15,0,0,0,"narrow_blur_11 115,115"},
  {hav,haf,80,15,0,0,0,"home_affordance_base 80,80"},
  {pbv,acf,80,0,15,0,1,"attachment_clear 80,80 RT1"},
 };
 TeguRenderPipeline *blur=0,*blur16=0;
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
  id error=0;RenderDescriptor *pd=c6f_ca_descriptor(cases[i].v,cases[i].f,cases[i].fmt,cases[i].m0,cases[i].m1,cases[i].blend);
  TeguRenderPipeline *p=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];
  fprintf(stderr,"C6F_PIPE %s ptr=%p inert=%u draw=%u mrt=%u true=%u main=%u count=%u active=%x preserve=%x subpass=%u err=%p\n",cases[i].name,p,
   p?p->inert:0,p?p->genericDraw:0,p?p->genericMrt:0,p?p->genericTrueMrt:0,p?p->genericMainRt:0,p?p->genericRtCount:0,
   p?p->genericRtActive:0,p?p->genericRtPreserve:0,p?p->genericFsSubpass:0,error);
  assert(p&&!error&&!p->inert&&p->genericDraw&&p->genericMrt&&!p->genericTrueMrt&&p->genericRtCount==2&&
         p->genericMainRt==cases[i].main&&p->genericRtActive==(1u<<cases[i].main)&&!p->genericRtPreserve&&
         p->colorFormat==cases[i].fmt&&p->genericRtFormats[1-cases[i].main]==cases[i].fmt);
  checks++;
  if(i==0)blur=[p retain];
  if(i==3)blur16=[p retain];
  [p release];[pd release];
 }
 /* (1) CA-shaped allocations: every level of a render target is a legal
  * arch9 linear colour target (64-byte rows). */
 struct {NSUInteger fmt,w,h,levels,usage,options;} shapes[]={
  {80,1125,2436,1,5,0x20},{115,1125,2436,1,5,0x20},{80,776,24,10,5,0x20},{115,776,24,10,5,0x20},
  {80,1080,2400,1,4,0},{80,281,609,1,7,0x20},{80,563,1218,3,5,0},
 };
 for(unsigned i=0;i<sizeof(shapes)/sizeof(shapes[0]);i++){
  TeguTexture *t=c6f_texture(d,shapes[i].fmt,shapes[i].w,shapes[i].h,shapes[i].levels,shapes[i].usage,shapes[i].options);
  assert(t&&t->levelCount==shapes[i].levels);
  for(unsigned l=0;l<t->levelCount;l++){
   assert(!(t->mip[l].stride&63)&&t->mip[l].stride>=t->mip[l].width*n2r_bpp((unsigned)t->format)&&!(t->mip[l].offset&63));
   assert(t->mip[l].offset+t->mip[l].stride*t->mip[l].height<=t->buffer->logical);
  }
  fprintf(stderr,"C6F_TEX fmt=%lu %lux%lu levels=%lu row=%lu\n",(unsigned long)t->format,(unsigned long)t->textureWidth,
   (unsigned long)t->textureHeight,(unsigned long)t->levelCount,(unsigned long)t->rowBytes);
  checks++;[t release];
 }
 TeguQueue *q=[d newCommandQueue];assert(q);
 TeguBuffer *vb=[d newBufferWithLength:4096 options:0];assert(vb);
 TeguTexture *src=new_sample(d,80,64,64,1,2,1);assert(src);fill_texture(src,0x80402010u);
 TeguSampler *samp=new_sampler(d,3);
 /* Two-attachment blur passes on full-screen and mipmapped level-0 targets:
  * store Unknown (4) resolved by setColorStoreAction, RT1 mask0 untouched. */
 struct {NSUInteger fmt,w,h,levels,load0,load1;TeguRenderPipeline *p;} passes[]={
  {80,1125,2436,1,2,1,0},{80,776,24,10,2,2,0},{80,1125,2436,1,1,0,0},{115,562,1218,1,2,1,0},
 };
 passes[0].p=passes[1].p=passes[2].p=blur;passes[3].p=blur16;
 for(unsigned i=0;i<sizeof(passes)/sizeof(passes[0]);i++){
  TeguTexture *t0=c6f_texture(d,passes[i].fmt,passes[i].w,passes[i].h,passes[i].levels,5,0x20);
  TeguTexture *t1=c6f_texture(d,passes[i].fmt,passes[i].w,passes[i].h,passes[i].levels,5,0x20);
  assert(t0&&t1);
  RenderPass *pass=[[RenderPass alloc] init];
  pass->color=[[PassColor alloc] init];pass->color->target=[t0 retain];pass->color->load=passes[i].load0;pass->color->store=4;
  pass->second=[[PassColor alloc] init];pass->second->target=[t1 retain];pass->second->load=passes[i].load1;pass->second->store=4;
  pass->passWidth=passes[i].w;pass->passHeight=passes[i].h;pass->arrayLength=1;pass->sampleCount=1;
  TeguCommand *c=[q commandBuffer];assert(c);
  TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
  assert(e&&!e->inert&&!c->invalid);
  [e setColorStoreAction:1 atIndex:0];[e setColorStoreAction:0 atIndex:1];
  [e setRenderPipelineState:passes[i].p];
  [e setVertexBuffer:vb offset:0 atIndex:1];
  [e setFragmentTexture:src atIndex:0];[e setFragmentSamplerState:samp atIndex:0];
  [e drawPrimitives:3 vertexStart:0 vertexCount:3];
  [e endEncoding];
  unsigned clears=(passes[i].load0!=1)+(passes[i].load1!=1); /* DontCare is realised as a clear */
  fprintf(stderr,"C6F_PASS %u fmt=%lu %lux%lu row=%lu invalid=%u at=%s:%u count=%u clears=%u\n",i,(unsigned long)passes[i].fmt,
   (unsigned long)passes[i].w,(unsigned long)passes[i].h,(unsigned long)t0->rowBytes,c->invalid,
   c->invalidFunction?c->invalidFunction:"-",c->invalidLine,c->count,clears);
  if(passes[i].fmt==115&&c->invalid&&c->invalidFunction&&!strcmp(c->invalidFunction,"tegu_render_trace_silent")) {
   /* PENDING (not support): the Rust generic planner (air_bridge.rs
    * generic_target_formats) still admits only [80,80] two-target sets. */
   fprintf(stderr,"C6F_PENDING two-target RGBA16F refused by generic planner\n");
   [c commit];[c waitUntilCompleted];assert([c status]==5);[pass release];[t0 release];[t1 release];continue;
  }
  /* Clear jobs (and a Load preload job, if the transport emits one) precede
   * the single draw; RT1 (mask 0) gets no draw job. */
  assert(!c->invalid&&c->count>=clears+1&&c->count<=clears+2);
  struct Dispatch *j=tegu_command_job(c,c->count-1);
  for(unsigned k=0;k+1<c->count;k++){struct Dispatch *o=tegu_command_job(c,k);assert(!o->genericDrawSnapshot);}
  assert(j->genericDrawSnapshot&&j->renderTexture==t0&&!j->nord2CopySources[0]&&!j->nord2CopySources[1]);
  {unsigned seen=0;for(unsigned k=0;k+1<c->count;k++)if(tegu_command_job(c,k)->nord2ClearOnly)seen++;assert(seen==clears);}
  [c commit];[c waitUntilCompleted];assert([c status]==4&&![c error]);
  checks++;[pass release];[t0 release];[t1 release];
 }
 /* IOSurface: unaligned BGRA8 stride -> exact owned shadow. */
 d->capabilities|=(1ULL<<15);
 test_surface.width=60;test_surface.height=60;test_surface.row=240;test_surface.alloc=16384;test_surface.planes=0;
 test_surface.fourcc=0x42475241u;
 {TextureDesc *sd=[[TextureDesc alloc] init];sd->format=80;sd->w=60;sd->h=60;sd->uses=5;
  TeguTexture *s=[d newTextureWithDescriptor:(id)sd iosurface:(id)&test_surface plane:0];
  fprintf(stderr,"C6F_SHADOW ptr=%p shadow=%d imported=%d row=%lu\n",s,s?s->shadowSurface!=0:0,s?(int)s->surfaceImported:0,
   (unsigned long)(s?s->rowBytes:0));
  assert(s&&s->shadowSurface&&!s->surfaceImported&&!(s->rowBytes&63)&&s->rowBytes>=240);
  checks++;[s release];
  /* backboardd display surface: 1125x2436 'b3a8' two-plane, row 4544. */
  test_surface.width=1125;test_surface.height=2436;test_surface.row=4544;test_surface.alloc=13893632;test_surface.planes=2;
  test_surface.fourcc=0x62336138u;sd->format=550;sd->w=1125;sd->h=2436;
  TeguTexture *x=[d newTextureWithDescriptor:(id)sd iosurface:(id)&test_surface plane:0];
  fprintf(stderr,"C6F_B3A8 ptr=%p\n",x);assert(!x);checks++;
  [sd release];}
 /* c6fc: fixed_frag_lph_cph / luma_boost_lph indexed (type 3, n=6, uint16)
  * draws into an imported BGRA8 display IOSurface with a second (mask 0)
  * attachment; every one became TEGU_DRAW_SILENT indexed_expand rc=-1. */
 {
  TeguLibrary *fxvl=0,*fxfl=0;
  {long n=0;void *b=c6f_read(argv[8],&n);id err=0;fxvl=[d newTeguLibraryWithBytes:b length:(NSUInteger)n error:&err];free(b);assert(fxvl);}
  {long n=0;void *b=c6f_read(argv[9],&n);id err=0;fxfl=[d newTeguLibraryWithBytes:b length:(NSUInteger)n error:&err];free(b);assert(fxfl);}
  /* Captured fixed-UI constants (fixed_ui_frontend_host_test.inc) with the
   * c6fc two-attachment count. */
  struct TeguFixedUIConstantRow fsrows[sizeof(tegu_fixed_ui_fs_constants)/sizeof(tegu_fixed_ui_fs_constants[0])];
  memcpy(fsrows,tegu_fixed_ui_fs_constants,sizeof(fsrows));
  for(unsigned r=0;r<sizeof(fsrows)/sizeof(fsrows[0]);r++)if(!strcmp(fsrows[r].name,"fc_attachment_count"))fsrows[r].data[0]=(uint8_t)C6F_ATTACHMENTS;
  TeguFunction *fxv=tegu_fixed_ui_test_function(fxvl,"fixed_vert_lph_spc",tegu_fixed_ui_vs_constants,
   sizeof(tegu_fixed_ui_vs_constants)/sizeof(tegu_fixed_ui_vs_constants[0]),1);
  TeguFunction *fxf=tegu_fixed_ui_test_function(fxfl,"fixed_frag_lph_cph",fsrows,sizeof(fsrows)/sizeof(fsrows[0]),2);
  TeguFunction *luv=c6f_function(d,argv[4],"std_vert1_lph");
  TeguFunction *luf=c6f_function(d,argv[10],"luma_boost_lph");
  struct {TeguFunction *v,*f;const char *name;NSUInteger srcFmt,sw,sh,dw,dh,drow;} ix[]={
   {fxv,fxf,"fixed_frag_lph_cph",80,64,64,64,64,256},{fxv,fxf,"fixed_frag_lph_cph/f115",115,256,1,64,64,256},
   {luv,luf,"luma_boost_lph",10,128,1,64,64,256},
   {fxv,fxf,"fixed_frag_lph_cph display",80,765,415,1125,2436,4608},{fxv,fxf,"fixed_frag_lph_cph/f115 display",115,256,1,1125,2436,4608},
   {luv,luf,"luma_boost_lph display",10,128,1,1125,2436,4608}};
  c6f_big_surface=calloc(1,4608u*2436u+4096u);assert(c6f_big_surface);
  for(unsigned i=0;i<6;i++){
   id error=0;RenderDescriptor *pd=c6f_ca_descriptor(ix[i].v,ix[i].f,80,15,0,0);
   TeguRenderPipeline *p=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];
   fprintf(stderr,"C6F_IXPIPE %s ptr=%p inert=%u draw=%u mrt=%u main=%u subpass=%u attrs=%u slot=%u stride=%u\n",ix[i].name,p,p?p->inert:0,
    p?p->genericDraw:0,p?p->genericMrt:0,p?p->genericMainRt:0,p?p->genericFsSubpass:0,p?p->genericShader.attribute_count:0,
    p?p->genericShader.attributes[0].buffer_index:0,p?p->genericShader.attributes[0].stride:0);
   assert(p&&!p->inert&&p->genericDraw);
   NSUInteger dw=ix[i].dw,dh=ix[i].dh;
   test_surface.width=dw;test_surface.height=dh;test_surface.row=ix[i].drow;test_surface.alloc=(ix[i].drow*dh+4095)&~(size_t)4095;test_surface.planes=0;
   test_surface.fourcc=0x42475241u;
   TextureDesc *sd=[[TextureDesc alloc] init];sd->format=80;sd->w=dw;sd->h=dh;sd->uses=5;
   TeguTexture *disp=[d newTextureWithDescriptor:(id)sd iosurface:(id)&test_surface plane:0];[sd release];
   assert(disp&&disp->surfaceImported);
   TeguTexture *t1=c6f_texture(d,80,dw,dh,1,5,0x20);
   TeguTexture *src=new_sample(d,ix[i].srcFmt,ix[i].sw,ix[i].sh,1,2,1);assert(src);
   RenderPass *pass=[[RenderPass alloc] init];
   pass->color=[[PassColor alloc] init];pass->color->target=[disp retain];pass->color->load=1;pass->color->store=1;
   pass->second=[[PassColor alloc] init];pass->second->target=[t1 retain];pass->second->load=0;pass->second->store=0;
   pass->passWidth=dw;pass->passHeight=dh;pass->arrayLength=1;pass->sampleCount=1;
   TeguBuffer *ib=[d newBufferWithLength:64 options:0];uint16_t idx[6]={0,1,2,2,1,3};memcpy([ib contents],idx,sizeof(idx));
   TeguBuffer *fb=[d newBufferWithLength:1024 options:0];memset([fb contents],0,1024);
   TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
   assert(e&&!e->inert&&!c->invalid);
   [e setRenderPipelineState:p];[e setVertexBuffer:vb offset:0 atIndex:1];
   for(unsigned b=0;b<8;b++){[e setFragmentBuffer:fb offset:0 atIndex:b];[e setVertexBuffer:fb offset:0 atIndex:b==1?30:b];}
   for(unsigned k=0;k<4;k++){[e setFragmentTexture:src atIndex:k];[e setFragmentSamplerState:samp atIndex:k];}
   [e drawIndexedPrimitives:3 indexCount:6 indexType:0 indexBuffer:ib indexBufferOffset:0];
   [e endEncoding];
   fprintf(stderr,"C6F_INDEXED %s invalid=%u at=%s:%u count=%u subpass=0x%x\n",ix[i].name,c->invalid,
    c->invalidFunction?c->invalidFunction:"-",c->invalidLine,c->count,p->genericFsSubpass);
   assert(!c->invalid);
   {struct Dispatch *dj=tegu_command_job(c,c->count-1);
    /* One written target: no cross-target snapshot copies (c6fc rc=-1). */
    assert(dj->genericDrawSnapshot&&!dj->nord2CopySources[0]&&!dj->nord2CopySources[1]&&!dj->buffers[6]&&!dj->buffers[7]);}
   [c commit];[c waitUntilCompleted];assert([c status]==4&&![c error]);
   checks++;[pass release];[disp release];[t1 release];[src release];[ib release];[fb release];[p release];[pd release];
  }
 }
 /* c6gc: textured fixed_frag_lph_cph (captured textured constants), blend
  * One/OneMinusSrcAlpha, Load into the imported 1125x2436 display target with
  * a mask-0 RT1; device: TEGU_COMMAND_FAILED bridge BAD job kind=draw load=1
  * bindings=3. */
 {
  long n=0;void *b=c6f_read(argv[8],&n);id err=0;TeguLibrary *ql=[d newTeguLibraryWithBytes:b length:(NSUInteger)n error:&err];free(b);assert(ql);
  struct TeguFixedUIConstantRow fs[sizeof(tegu_fixed_ui_fs_constants)/sizeof(tegu_fixed_ui_fs_constants[0])];
  struct TeguFixedUIConstantRow vs[sizeof(tegu_fixed_ui_vs_constants)/sizeof(tegu_fixed_ui_vs_constants[0])];
  memcpy(fs,tegu_fixed_ui_fs_constants,sizeof(fs));memcpy(vs,tegu_fixed_ui_vs_constants,sizeof(vs));
  const char *enabled[]={"fc_has_image_0A","fc_has_lod_bias","fc_image_count","fc_plane_count0","fc_texcoord_count0","fc_texture_function"};
  for(unsigned i=0;i<sizeof(fs)/sizeof(fs[0]);i++){for(unsigned j=0;j<6;j++)if(!strcmp(fs[i].name,enabled[j]))fs[i].data[0]=1;
   if(!strcmp(fs[i].name,"fc_attachment_count"))fs[i].data[0]=2;}
  for(unsigned i=0;i<sizeof(vs)/sizeof(vs[0]);i++)if(!strcmp(vs[i].name,"fc_texcoord_count0"))vs[i].data[0]=1;
  TeguFunction *tv=tegu_fixed_ui_test_function(ql,"fixed_vert_lph_spc",vs,sizeof(vs)/sizeof(vs[0]),1);
  TeguFunction *tf=tegu_fixed_ui_test_function(ql,"fixed_frag_lph_cph",fs,sizeof(fs)/sizeof(fs[0]),2);
  RenderDescriptor *pd=c6f_ca_descriptor(tv,tf,80,15,0,1);id error=0;
  TeguRenderPipeline *p=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];
  fprintf(stderr,"C6F_TXPIPE ptr=%p inert=%u draw=%u mrt=%u subpass=0x%x\n",p,p?p->inert:0,p?p->genericDraw:0,p?p->genericMrt:0,p?p->genericFsSubpass:0);
  assert(p&&!p->inert&&p->genericDraw);
  NSUInteger dw=1125,dh=2436;
  test_surface.width=dw;test_surface.height=dh;test_surface.row=4608;test_surface.alloc=(4608u*2436u+4095u)&~4095u;test_surface.planes=0;
  test_surface.fourcc=0x42475241u;
  TextureDesc *sd=[[TextureDesc alloc] init];sd->format=80;sd->w=dw;sd->h=dh;sd->uses=5;
  TeguTexture *disp=[d newTextureWithDescriptor:(id)sd iosurface:(id)&test_surface plane:0];[sd release];assert(disp&&disp->surfaceImported);
  TeguTexture *t1=c6f_texture(d,80,dw,dh,1,5,0x20);
  TeguTexture *img=new_sample(d,80,194,237,1,2,1);assert(img);
  TeguBuffer *v2=[d newBufferWithLength:512 options:0],*v3=[d newBufferWithLength:64 options:0];
  TeguBuffer *f1=[d newBufferWithLength:512 options:0],*f2=[d newBufferWithLength:64 options:0];
  memset([f1 contents],0,512);memset([f2 contents],0,64);memset([v2 contents],0,512);memset([v3 contents],0,64);
  TeguBuffer *ib=[d newBufferWithLength:64 options:0];uint16_t idx[6]={0,1,2,2,1,3};memcpy([ib contents],idx,sizeof(idx));
  for(unsigned load=1;load<=2;load++){
   RenderPass *pass=[[RenderPass alloc] init];
   pass->color=[[PassColor alloc] init];pass->color->target=[disp retain];pass->color->load=load;pass->color->store=1;
   pass->second=[[PassColor alloc] init];pass->second->target=[t1 retain];pass->second->load=0;pass->second->store=0;
   pass->passWidth=dw;pass->passHeight=dh;pass->arrayLength=1;pass->sampleCount=1;
   TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
   [e setRenderPipelineState:p];[e setVertexBuffer:vb offset:0 atIndex:1];
   [e setVertexBuffer:v2 offset:16 atIndex:2];[e setVertexBuffer:v3 offset:0 atIndex:3];
   [e setFragmentBuffer:f1 offset:256 atIndex:1];[e setFragmentBuffer:f2 offset:0 atIndex:2];
   [e setFragmentTexture:img atIndex:3];[e setFragmentSamplerState:samp atIndex:0];
   [e drawIndexedPrimitives:3 indexCount:6 indexType:0 indexBuffer:ib indexBufferOffset:0];
   [e setScissorRect:(TeguScissorRect){181,261,763,413}];
   [e drawIndexedPrimitives:3 indexCount:6 indexType:0 indexBuffer:ib indexBufferOffset:0];
   [e endEncoding];
   fprintf(stderr,"C6F_TXDRAW load=%u invalid=%u at=%s:%u count=%u bindings=%u\n",load,c->invalid,c->invalidFunction?c->invalidFunction:"-",
    c->invalidLine,c->count,c->count?(unsigned)tegu_command_job(c,c->count-1)->genericDrawRequest.binding_count:0);
   assert(!c->invalid);
   [c commit];[c waitUntilCompleted];
   fprintf(stderr,"C6F_TXDONE load=%u status=%lu error=%p\n",load,(unsigned long)[c status],[c error]);
   assert([c status]==4&&![c error]);checks++;
   [pass release];
  }
  [disp release];[t1 release];[img release];[v2 release];[v3 release];[f1 release];[f2 release];[ib release];[p release];[pd release];
 }
 /* c6hc: 61-job command buffers on the imported display under an exhausted
  * GPU budget (allocations > 8 MB refused like the device's 64 MiB cap):
  * framebuffer-fetch (luma_boost) and textured fixed_frag LOAD draws must not
  * need a display-sized snapshot. */
 {
  long n=0;void *b=c6f_read(argv[8],&n);id err=0;TeguLibrary *ql=[d newTeguLibraryWithBytes:b length:(NSUInteger)n error:&err];free(b);assert(ql);
  struct TeguFixedUIConstantRow fs[sizeof(tegu_fixed_ui_fs_constants)/sizeof(tegu_fixed_ui_fs_constants[0])];
  struct TeguFixedUIConstantRow vs[sizeof(tegu_fixed_ui_vs_constants)/sizeof(tegu_fixed_ui_vs_constants[0])];
  memcpy(fs,tegu_fixed_ui_fs_constants,sizeof(fs));memcpy(vs,tegu_fixed_ui_vs_constants,sizeof(vs));
  const char *enabled[]={"fc_has_image_0A","fc_has_lod_bias","fc_image_count","fc_plane_count0","fc_texcoord_count0","fc_texture_function"};
  for(unsigned i=0;i<sizeof(fs)/sizeof(fs[0]);i++){for(unsigned j=0;j<6;j++)if(!strcmp(fs[i].name,enabled[j]))fs[i].data[0]=1;
   if(!strcmp(fs[i].name,"fc_attachment_count"))fs[i].data[0]=2;}
  for(unsigned i=0;i<sizeof(vs)/sizeof(vs[0]);i++)if(!strcmp(vs[i].name,"fc_texcoord_count0"))vs[i].data[0]=1;
  TeguFunction *tv=tegu_fixed_ui_test_function(ql,"fixed_vert_lph_spc",vs,sizeof(vs)/sizeof(vs[0]),1);
  TeguFunction *tf=tegu_fixed_ui_test_function(ql,"fixed_frag_lph_cph",fs,sizeof(fs)/sizeof(fs[0]),2);
  RenderDescriptor *pdf=c6f_ca_descriptor(tv,tf,80,15,0,1),*pdl=c6f_ca_descriptor(c6f_function(d,argv[4],"std_vert1_lph"),c6f_function(d,argv[10],"luma_boost_lph"),80,15,0,0);
  id error=0;TeguRenderPipeline *pf=[d newRenderPipelineStateWithDescriptor:(id)pdf error:&error];
  TeguRenderPipeline *pl=[d newRenderPipelineStateWithDescriptor:(id)pdl error:&error];
  assert(pf&&pf->genericDraw&&pl&&pl->genericDraw&&pl->genericFsSubpass);
  NSUInteger dw=1125,dh=2436;
  test_surface.width=dw;test_surface.height=dh;test_surface.row=4608;test_surface.alloc=(4608u*2436u+4095u)&~4095u;test_surface.planes=0;
  test_surface.fourcc=0x42475241u;
  TextureDesc *sd=[[TextureDesc alloc] init];sd->format=80;sd->w=dw;sd->h=dh;sd->uses=5;
  TeguTexture *disp=[d newTextureWithDescriptor:(id)sd iosurface:(id)&test_surface plane:0];[sd release];assert(disp&&disp->surfaceImported);
  TeguTexture *t1=c6f_texture(d,80,dw,dh,1,5,0x20),*img=new_sample(d,80,194,237,1,2,1),*lut=new_sample(d,10,128,1,1,2,1);
  TeguBuffer *ib=[d newBufferWithLength:64 options:0];uint16_t idx[6]={0,1,2,2,1,3};memcpy([ib contents],idx,sizeof(idx));
  TeguBuffer *fb=[d newBufferWithLength:1024 options:0];memset([fb contents],0,1024);
  c6f_alloc_cap=8u<<20;
  RenderPass *pass=[[RenderPass alloc] init];
  pass->color=[[PassColor alloc] init];pass->color->target=[disp retain];pass->color->load=1;pass->color->store=1;
  pass->second=[[PassColor alloc] init];pass->second->target=[t1 retain];pass->second->load=0;pass->second->store=0;
  pass->passWidth=dw;pass->passHeight=dh;pass->arrayLength=1;pass->sampleCount=1;
  TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
  for(unsigned k=0;k<60;k++){
   TeguRenderPipeline *p=(k%3==2)?pl:pf;
   [e setRenderPipelineState:p];[e setVertexBuffer:vb offset:0 atIndex:1];
   for(unsigned bi=0;bi<4;bi++){[e setFragmentBuffer:fb offset:0 atIndex:bi];if(bi!=1)[e setVertexBuffer:fb offset:0 atIndex:bi];}
   [e setFragmentTexture:(p==pl?lut:img) atIndex:(p==pl?0:3)];[e setFragmentSamplerState:samp atIndex:0];
   [e drawIndexedPrimitives:3 indexCount:6 indexType:0 indexBuffer:ib indexBufferOffset:0];
  }
  [e endEncoding];
  fprintf(stderr,"C6F_JOBS61 invalid=%u at=%s:%u count=%u\n",c->invalid,c->invalidFunction?c->invalidFunction:"-",c->invalidLine,c->count);
  assert(!c->invalid&&c->count==61);
  [c commit];[c waitUntilCompleted];
  fprintf(stderr,"C6F_JOBS61_DONE status=%lu error=%p\n",(unsigned long)[c status],[c error]);
  assert([c status]==4&&![c error]);checks++;
  c6f_alloc_cap=0;
  [pass release];[disp release];[t1 release];[img release];[lut release];[ib release];[fb release];[pf release];[pl release];[pdf release];[pdl release];
 }
 /* c6ic: a ~200-draw backboardd command buffer (display pass with textured
  * fixed_frag draws and framebuffer-fetch luma_boost draws, BGRA8/BGR5A1 blur
  * passes, a second display pass) under the device's 64 MiB per-context
  * budget, which also holds three imported 1125x2436 display surfaces and a
  * full-screen private target. Run per-draw and render-pass batched; report
  * GPU jobs and the peak of live GPU memory. */
 {
  long nn=0;void *bb=c6f_read(argv[8],&nn);id err=0;TeguLibrary *ql=[d newTeguLibraryWithBytes:bb length:(NSUInteger)nn error:&err];free(bb);assert(ql);
  struct TeguFixedUIConstantRow fs[sizeof(tegu_fixed_ui_fs_constants)/sizeof(tegu_fixed_ui_fs_constants[0])];
  struct TeguFixedUIConstantRow vs[sizeof(tegu_fixed_ui_vs_constants)/sizeof(tegu_fixed_ui_vs_constants[0])];
  memcpy(fs,tegu_fixed_ui_fs_constants,sizeof(fs));memcpy(vs,tegu_fixed_ui_vs_constants,sizeof(vs));
  const char *enabled[]={"fc_has_image_0A","fc_has_lod_bias","fc_image_count","fc_plane_count0","fc_texcoord_count0","fc_texture_function"};
  for(unsigned i=0;i<sizeof(fs)/sizeof(fs[0]);i++){for(unsigned j=0;j<6;j++)if(!strcmp(fs[i].name,enabled[j]))fs[i].data[0]=1;
   if(!strcmp(fs[i].name,"fc_attachment_count"))fs[i].data[0]=2;}
  for(unsigned i=0;i<sizeof(vs)/sizeof(vs[0]);i++)if(!strcmp(vs[i].name,"fc_texcoord_count0"))vs[i].data[0]=1;
  RenderDescriptor *pdf=c6f_ca_descriptor(tegu_fixed_ui_test_function(ql,"fixed_vert_lph_spc",vs,sizeof(vs)/sizeof(vs[0]),1),
   tegu_fixed_ui_test_function(ql,"fixed_frag_lph_cph",fs,sizeof(fs)/sizeof(fs[0]),2),80,15,0,1);
  RenderDescriptor *pdl=c6f_ca_descriptor(c6f_function(d,argv[4],"std_vert1_lph"),c6f_function(d,argv[10],"luma_boost_lph"),80,15,0,0);
  RenderDescriptor *pdd=c6f_ca_descriptor(dsv,ds4,80,15,0,1),*pdn=c6f_ca_descriptor(dsv,nb11,43,15,0,0);
  id error=0;
  TeguRenderPipeline *pf=[d newRenderPipelineStateWithDescriptor:(id)pdf error:&error],*pl=[d newRenderPipelineStateWithDescriptor:(id)pdl error:&error];
  TeguRenderPipeline *pd=[d newRenderPipelineStateWithDescriptor:(id)pdd error:&error],*pn=[d newRenderPipelineStateWithDescriptor:(id)pdn error:&error];
  assert(pf&&pf->genericDraw&&pl&&pl->genericDraw&&pd&&pd->genericDraw&&pn&&pn->genericDraw);
  NSUInteger dw=1125,dh=2436;
  test_surface.width=dw;test_surface.height=dh;test_surface.row=4608;test_surface.alloc=(4608u*2436u+4095u)&~4095u;test_surface.planes=0;
  test_surface.fourcc=0x42475241u;
  /* Earlier sections' objects stay alive in this single process (no pool
   * drain); the modelled 64 MiB is charged on top of them. */
  uint64_t live0=0;{unsigned nlive=0;for(unsigned qq=1;qq<=f.next&&qq<COUNT;qq++)if(f.buffers[qq].active){live0+=f.buffers[qq].size;nlive++;}
   fprintf(stderr,"C6F_BUDGET200 live_before_mib=%.1f buffers=%u code_cache=%u\n",(double)live0/1048576.0,nlive,n2mb_code_cache_count(bridge));}
  c6f_budget=live0+(64ull<<20);
  TeguTexture *disp[3];
  for(unsigned k=0;k<3;k++){TextureDesc *sd=[[TextureDesc alloc] init];sd->format=80;sd->w=dw;sd->h=dh;sd->uses=5;
   disp[k]=[d newTextureWithDescriptor:(id)sd iosurface:(id)&test_surface plane:0];[sd release];assert(disp[k]&&disp[k]->surfaceImported);}
  TeguTexture *t1=c6f_texture(d,80,dw,dh,1,5,0x20);assert(t1);
  TeguTexture *b43=c6f_texture(d,43,565,1220,1,5,0x20),*c43=c6f_texture(d,43,565,1220,1,5,0x20);
  TeguTexture *s0=c6f_texture(d,80,144,213,1,5,0x20);
  TeguTexture *img=new_sample(d,80,194,237,1,2,1),*lut=new_sample(d,10,128,1,1,2,1);
  assert(b43&&c43&&s0&&img&&lut);
  TeguBuffer *ib=[d newBufferWithLength:64 options:0];uint16_t idx[6]={0,1,2,2,1,3};memcpy([ib contents],idx,sizeof(idx));
  TeguBuffer *fbuf=[d newBufferWithLength:1024 options:0];memset([fbuf contents],0,1024);
  unsigned perDrawJobs=0;
  for(int mode=0;mode<2;mode++){
   tegu_jm_batch_force=mode;c6f_peak=0;unsigned refused0=c6f_refused,sub0=f.submitted,draws=0;
   U64 enc0[TEGU_ENC_SLOTS];for(unsigned z=0;z<TEGU_ENC_SLOTS;z++)enc0[z]=tegu_enc_ns[z];double encStart=tegu_enc_clock();
   TeguCommand *c=[q commandBuffer];
   for(unsigned pass=0;pass<2;pass++){
    RenderPass *dp=[[RenderPass alloc] init];
    dp->color=[[PassColor alloc] init];dp->color->target=[disp[0] retain];dp->color->load=1;dp->color->store=1;
    dp->second=[[PassColor alloc] init];dp->second->target=[t1 retain];dp->second->load=0;dp->second->store=0;
    dp->passWidth=dw;dp->passHeight=dh;dp->arrayLength=1;dp->sampleCount=1;
    TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)dp];
    for(unsigned k=0;k<(pass?40u:120u);k++){
     TeguRenderPipeline *p=(k%30==29)?pl:pf;
     [e setRenderPipelineState:p];[e setVertexBuffer:vb offset:0 atIndex:1];
     for(unsigned bi=0;bi<4;bi++){[e setFragmentBuffer:fbuf offset:0 atIndex:bi];if(bi!=1)[e setVertexBuffer:fbuf offset:0 atIndex:bi];}
     [e setFragmentTexture:(p==pl?lut:img) atIndex:(p==pl?0:3)];[e setFragmentSamplerState:samp atIndex:0];
     [e setScissorRect:(TeguScissorRect){(k*37)%900,(k*53)%2200,150,150}];
     [e drawIndexedPrimitives:3 indexCount:6 indexType:0 indexBuffer:ib indexBufferOffset:0];draws++;
    }
    [e endEncoding];[dp release];
    if(pass)break;
    /* blur: downsample into BGRA8, then BGR5A1 narrow-blur ping-pong passes */
    for(unsigned bp=0;bp<10;bp++){
     TeguTexture *dst=bp==0?s0:(bp&1)?b43:c43,*src=bp==0?img:bp==1?s0:(bp&1)?c43:b43;
     RenderPass *bpz=[[RenderPass alloc] init];
     bpz->color=[[PassColor alloc] init];bpz->color->target=[dst retain];bpz->color->load=2;bpz->color->store=1;
     bpz->passWidth=dst->textureWidth;bpz->passHeight=dst->textureHeight;bpz->arrayLength=1;bpz->sampleCount=1;
     TeguRenderEncoder *be=[c renderCommandEncoderWithDescriptor:(id)bpz];
     TeguRenderPipeline *p=bp==0?pd:pn;
     [be setRenderPipelineState:p];[be setVertexBuffer:vb offset:0 atIndex:1];
     for(unsigned t=0;t<4;t++){[be setFragmentTexture:src atIndex:t];[be setFragmentSamplerState:samp atIndex:t];}
     for(unsigned k=0;k<4;k++){[be drawIndexedPrimitives:3 indexCount:6 indexType:0 indexBuffer:ib indexBufferOffset:0];draws++;}
     [be endEncoding];[bpz release];
    }
   }
   fprintf(stderr,"C6F_BUDGET200 mode=%s draws=%u jobs_encoded=%u invalid=%u at=%s:%u\n",mode?"batched":"per-draw",draws,c->count,c->invalid,
    c->invalidFunction?c->invalidFunction:"-",c->invalidLine);
   fprintf(stderr,"C6F_ENCODE200 mode=%s wall_ms=%.1f draw_ms=%.1f plan_ms=%.1f variant_ms=%.1f expand_ms=%.2f pass_ms=%.2f (host CPU)\n",
    mode?"batched":"per-draw",(tegu_enc_clock()-encStart)*1e3,(tegu_enc_ns[0]-enc0[0])/1e6,(tegu_enc_ns[1]-enc0[1])/1e6,
    (tegu_enc_ns[2]-enc0[2])/1e6,(tegu_enc_ns[3]-enc0[3])/1e6,(tegu_enc_ns[4]-enc0[4])/1e6);
   double execStart=tegu_enc_clock();
   assert(!c->invalid);
   [c commit];[c waitUntilCompleted];
   unsigned gpuJobs=f.submitted-sub0;
   fprintf(stderr,"C6F_EXEC200 mode=%s exec_ms=%.1f (host fake GPU)\n",mode?"batched":"per-draw",(tegu_enc_clock()-execStart)*1e3);
   fprintf(stderr,"C6F_BUDGET200 mode=%s status=%lu gpu_jobs=%u peak_scenario_mib=%.1f/64 refused_allocs=%u\n",mode?"batched":"per-draw",
    (unsigned long)[c status],gpuJobs,(double)(c6f_peak>live0?c6f_peak-live0:0)/1048576.0,c6f_refused-refused0);
   assert([c status]==4&&![c error]&&c6f_refused==refused0&&c6f_peak<=c6f_budget);
   if(!mode)perDrawJobs=gpuJobs;else assert(gpuJobs*4<perDrawJobs); /* batching cuts jobs several-fold */
   checks++;
  }
  fprintf(stderr,"C6F_VARIANTS fixed=%u luma=%u down=%u narrow=%u fixed_fs_spirv=%lu\n",pf->genericVariantCount,pl->genericVariantCount,
   pd->genericVariantCount,pn->genericVariantCount,(unsigned long)pf->genericFsLength);
  {double t0=tegu_enc_clock();uint8_t key[32];TeguAIRGraphicsSpecializationPlan sp;memset(&sp,0,sizeof(sp));sp.version=1;
   for(unsigned r=0;r<100;r++)tegu_render_generic_variant_key(pf,0,0,&sp,key);
   fprintf(stderr,"C6F_VARIANT_KEY us_per_key=%.1f\n",(tegu_enc_clock()-t0)*1e6/100);}
  tegu_jm_batch_force=-1;c6f_budget=0;
  for(unsigned k=0;k<3;k++)[disp[k] release];
  [t1 release];[b43 release];[c43 release];[s0 release];[img release];[lut release];[ib release];[fbuf release];
  [pf release];[pl release];[pd release];[pn release];[pdf release];[pdl release];[pdd release];[pdn release];
 }
 /* Lazy pass clears (gated with JM batching): Clear and DontCare targets get
  * no separate CLEAR job; the first draw clears in its own frame, later draws
  * load; a Clear target with no draw gets its job at endEncoding; DontCare
  * with no draw gets none. */
 {
  tegu_jm_batch_force=1;
  id error=0;RenderDescriptor *pdl=c6f_ca_descriptor(dsv,ds4,80,15,0,1);
  TeguRenderPipeline *p=[d newRenderPipelineStateWithDescriptor:(id)pdl error:&error];assert(p&&p->genericDraw);
  TeguTexture *t0=c6f_texture(d,80,144,213,1,5,0x20),*u1=c6f_texture(d,80,144,213,1,5,0x20),*src=new_sample(d,80,64,64,1,2,1);
  TeguBuffer *ib=[d newBufferWithLength:64 options:0];uint16_t idx[6]={0,1,2,2,1,3};memcpy([ib contents],idx,sizeof(idx));
  for(unsigned scenario=0;scenario<2;scenario++){
   RenderPass *pass=[[RenderPass alloc] init];
   pass->color=[[PassColor alloc] init];pass->color->target=[t0 retain];pass->color->load=2;pass->color->store=1;
   pass->second=[[PassColor alloc] init];pass->second->target=[u1 retain];pass->second->load=0;pass->second->store=0;
   pass->passWidth=144;pass->passHeight=213;pass->arrayLength=1;pass->sampleCount=1;
   TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
   assert(e&&!e->inert&&c->count==0&&e->nord2PendingClear==3u&&e->nord2DontCareMask==2u);
   unsigned draws=scenario?3u:0u;
   for(unsigned k=0;k<draws;k++){
    [e setRenderPipelineState:p];[e setVertexBuffer:vb offset:0 atIndex:1];
    for(unsigned t=0;t<4;t++){[e setFragmentTexture:src atIndex:t];[e setFragmentSamplerState:samp atIndex:t];}
    [e drawIndexedPrimitives:3 indexCount:6 indexType:0 indexBuffer:ib indexBufferOffset:0];
   }
   [e endEncoding];
   fprintf(stderr,"C6F_LAZY draws=%u jobs=%u pending=%x\n",draws,c->count,e->nord2PendingClear);
   if(!scenario){
    /* no draw: one CLEAR job for RT0, none for the DontCare RT1 */
    assert(!c->invalid&&c->count==1&&tegu_command_job(c,0)->nord2ClearOnly&&tegu_command_job(c,0)->renderTexture==t0);
   } else {
    assert(!c->invalid&&c->count==3);
    for(unsigned k=0;k<3;k++){struct Dispatch *j=tegu_command_job(c,k);
     assert(!j->nord2ClearOnly&&j->genericDrawSnapshot&&j->genericDrawRequest.load_action==(k?1u:2u));}
   }
   [c commit];[c waitUntilCompleted];assert([c status]==4&&![c error]);checks++;
   [pass release];
  }
  tegu_jm_batch_force=-1;
  [t0 release];[u1 release];[src release];[ib release];[p release];[pdl release];
 }
 /* c6hc rc=-13: a blur downsample sampling a BGR5A1 (43) 565x1220 source
  * into a 144x213 BGRA8 target. */
 {
  id error=0;RenderDescriptor *pd=c6f_ca_descriptor(dsv,ds4,80,15,0,1);
  TeguRenderPipeline *p=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];assert(p&&p->genericDraw);
  TeguTexture *s43=c6f_texture(d,43,565,1220,1,5,0x20),*t0=c6f_texture(d,80,144,213,1,5,0x20),*u1=c6f_texture(d,80,144,213,1,5,0x20);
  assert(s43&&t0&&u1);
  RenderPass *pass=[[RenderPass alloc] init];
  pass->color=[[PassColor alloc] init];pass->color->target=[t0 retain];pass->color->load=2;pass->color->store=1;
  pass->second=[[PassColor alloc] init];pass->second->target=[u1 retain];pass->second->load=0;pass->second->store=0;
  pass->passWidth=144;pass->passHeight=213;pass->arrayLength=1;pass->sampleCount=1;
  TeguBuffer *ib=[d newBufferWithLength:64 options:0];uint16_t idx[6]={0,1,2,2,1,3};memcpy([ib contents],idx,sizeof(idx));
  TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
  [e setRenderPipelineState:p];[e setVertexBuffer:vb offset:0 atIndex:1];
  for(unsigned k=0;k<4;k++){[e setFragmentTexture:s43 atIndex:k];[e setFragmentSamplerState:samp atIndex:k];}
  [e drawIndexedPrimitives:3 indexCount:6 indexType:0 indexBuffer:ib indexBufferOffset:0];[e endEncoding];
  fprintf(stderr,"C6F_F43_SAMPLE invalid=%u at=%s:%u count=%u\n",c->invalid,c->invalidFunction?c->invalidFunction:"-",c->invalidLine,c->count);
  assert(!c->invalid);
  {struct Dispatch *dj=tegu_command_job(c,c->count-1);int f43=0;
   for(unsigned bi=0;bi<dj->genericDrawRequest.binding_count;bi++)f43|=dj->genericDrawRequest.bindings[bi].format==43;
   assert(dj->genericDrawSnapshot&&f43);} /* the BGR5A1 source is a real binding, not a neutral image */
  [c commit];[c waitUntilCompleted];assert([c status]==4&&![c error]);checks++;
  [pass release];[s43 release];[t0 release];[u1 release];[ib release];[p release];[pd release];
 }
 /* (2) c6fc: BGR5A1Unorm (43) 776x24x10 blur pyramid passes (line 70). */
 {
  TeguTexture *p43=c6f_texture(d,43,776,24,10,5,0x20),*q43=c6f_texture(d,43,776,24,10,5,0x20);
  assert(p43&&q43&&!(p43->rowBytes&63)&&p43->rowBytes>=776*2);
  for(unsigned l=0;l<p43->levelCount;l++)assert(!(p43->mip[l].stride&63));
  NSUInteger loads[3]={1,2,0};
  for(unsigned k=0;k<3;k++){
   RenderPass *pass=[[RenderPass alloc] init];
   pass->color=[[PassColor alloc] init];pass->color->target=[p43 retain];pass->color->load=loads[k];pass->color->store=4;
   pass->second=[[PassColor alloc] init];pass->second->target=[q43 retain];pass->second->load=0;pass->second->store=0;
   pass->passWidth=776;pass->passHeight=24;pass->arrayLength=1;pass->sampleCount=1;
   TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
   fprintf(stderr,"C6F_F43_PASS load=%lu inert=%u invalid=%u count=%u\n",(unsigned long)loads[k],e?e->inert:9,c->invalid,c->count);
   assert(e&&!e->inert&&!c->invalid);
   [e setColorStoreAction:1 atIndex:0];[e endEncoding];[c commit];[c waitUntilCompleted];
   fprintf(stderr,"C6F_F43_DONE status=%lu error=%p\n",(unsigned long)[c status],[c error]);assert([c status]==4&&![c error]);
   checks++;[pass release];
  }
  id error=0;RenderDescriptor *pd=c6f_ca_descriptor(dsv,ds4,43,15,0,1);
  TeguRenderPipeline *p=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];
  fprintf(stderr,"C6F_F43_PIPE ptr=%p inert=%u draw=%u mrt=%u fmt=%lu\n",p,p?p->inert:0,p?p->genericDraw:0,p?p->genericMrt:0,
   (unsigned long)(p?p->colorFormat:0));
  assert(p&&p->colorFormat==43);
  if(p->inert)fprintf(stderr,"C6F_PENDING format-43 attachment refused by Rust translator admission (air_bridge.rs bounded class)\n");
  else {assert(p->genericDraw);checks++;}
  /* Clear words: Mesa pan_pack_color R5G5B5A1 tile layout, not dithered. */
  uint32_t w[4];double rgba[4]={1.0,0.5,0.25,1.0};assert(!n2r_clear(43,rgba,w));
  assert(w[0]==((31u<<5)|(16u<<15)|(8u<<25)|(1u<<31))&&w[1]==w[0]&&w[3]==w[0]);
  assert(n2r_tib_bytes(43)==4&&n2r_tib_bytes(10)==4&&n2r_tib_bytes(80)==4&&n2r_tib_bytes(115)==8&&n2r_bpp(43)==2);checks++;
  RenderPass *dp=[[RenderPass alloc] init];
  dp->color=[[PassColor alloc] init];dp->color->target=[p43 retain];dp->color->load=2;dp->color->store=1;
  dp->second=[[PassColor alloc] init];dp->second->target=[q43 retain];dp->second->load=0;dp->second->store=0;
  dp->passWidth=776;dp->passHeight=24;dp->arrayLength=1;dp->sampleCount=1;
  if(!p->inert){
  TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)dp];
  [e setRenderPipelineState:p];[e setVertexBuffer:vb offset:0 atIndex:1];
  [e setFragmentTexture:src atIndex:0];[e setFragmentSamplerState:samp atIndex:0];
  [e drawPrimitives:3 vertexStart:0 vertexCount:3];[e endEncoding];
  fprintf(stderr,"C6F_F43_DRAW invalid=%u at=%s:%u count=%u\n",c->invalid,c->invalidFunction?c->invalidFunction:"-",c->invalidLine,c->count);
  if(c->invalid)fprintf(stderr,"C6F_PENDING format-43 draw refused (Rust generic planner target formats)\n");
  else {struct Dispatch *dj=tegu_command_job(c,c->count-1);assert(dj->genericDrawSnapshot&&dj->genericDrawRequest.target_format==43);checks++;}
  [c commit];[c waitUntilCompleted];
  }
  [dp release];[p release];[pd release];[p43 release];[q43 release];
 }
 /* c6o device (app switcher blur): BGRA8 704x96 pyramid levels 2..4 at
  * offsets 337920/354816/359424 (176x24 row 704, 88x12 row 384, 44x6 row
  * 192) were refused 'storage outside target span' (page alignment), and a
  * level next to a larger second attachment 'mismatched color attachments';
  * every blur command was dropped. Each level must render: a level view at its
  * own offset/stride, the larger attachment through a window of the pass
  * area, sampling level L-1 of the same texture without a CPU snapshot. */
 {
  id error=0;RenderDescriptor *pd=c6f_ca_descriptor(dsv,ds4,80,15,0,0);
  TeguRenderPipeline *p=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];assert(p&&p->genericDraw);
  TeguTexture *pyr=c6f_texture(d,80,704,96,5,5,0x20),*big=c6f_texture(d,80,704,96,1,5,0x20);
  assert(pyr&&big&&pyr->levelCount==5);
  fprintf(stderr,"C6F_PYR_PYRAMID len=%llu offs=%llu/%llu/%llu rows=%lu/%lu/%lu\n",(unsigned long long)pyr->buffer->logical,
   (unsigned long long)pyr->mip[2].offset,(unsigned long long)pyr->mip[3].offset,(unsigned long long)pyr->mip[4].offset,
   (unsigned long)pyr->mip[2].stride,(unsigned long)pyr->mip[3].stride,(unsigned long)pyr->mip[4].stride);
  assert(pyr->mip[2].offset==337920&&pyr->mip[3].offset==354816&&pyr->mip[4].offset==359424);
  assert(pyr->mip[2].stride==704&&pyr->mip[3].stride==384&&pyr->mip[4].stride==192);
  TeguBuffer *ib=[d newBufferWithLength:64 options:0];uint16_t idx[6]={0,1,2,2,1,3};memcpy([ib contents],idx,sizeof(idx));
  static const unsigned W[5]={704,352,176,88,44},H[5]={96,48,24,12,6};
  for(unsigned withSecond=0;withSecond<2;withSecond++)for(unsigned level=2;level<5;level++){
   RenderPass *pass=[[RenderPass alloc] init];
   pass->color=[[PassColor alloc] init];pass->color->target=[pyr retain];pass->color->lvl=level;pass->color->load=0;pass->color->store=1;
   if(withSecond){pass->second=[[PassColor alloc] init];pass->second->target=[big retain];pass->second->load=0;pass->second->store=0;}
   pass->passWidth=0;pass->passHeight=0;pass->arrayLength=1;pass->sampleCount=1;
   TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
   fprintf(stderr,"C6F_PYR_LEVEL level=%u second=%u inert=%u invalid=%u at=%s:%u\n",level,withSecond,e?e->inert:9,c->invalid,
    c->invalidFunction?c->invalidFunction:"-",c->invalidLine);
   assert(e&&!e->inert&&!c->invalid);
   TeguTexture *rt0=e->renderTargets[0];
   assert(rt0->textureWidth==W[level]&&rt0->textureHeight==H[level]&&rt0->mip[0].offset==pyr->mip[level].offset&&rt0->rowBytes==pyr->mip[level].stride);
   if(withSecond){TeguTexture *rt1=e->renderTargets[1];
    assert(rt1&&rt1!=big&&rt1->textureWidth==W[level]&&rt1->textureHeight==H[level]&&rt1->rowBytes==big->rowBytes&&
     rt1->mip[0].offset==big->mip[0].offset&&rt1->buffer==big->buffer);}
   TeguTexture *prev=[pyr newTextureViewWithPixelFormat:80 textureType:2 levels:(TeguTextureViewRange){level-1,1} slices:(TeguTextureViewRange){0,1}];
   assert(prev&&prev->textureWidth==W[level-1]);
   [e setRenderPipelineState:p];[e setVertexBuffer:vb offset:0 atIndex:1];
   for(unsigned k=0;k<4;k++){[e setFragmentTexture:prev atIndex:k];[e setFragmentSamplerState:samp atIndex:k];}
   [e drawIndexedPrimitives:3 indexCount:6 indexType:0 indexBuffer:ib indexBufferOffset:0];
   [e endEncoding];
   fprintf(stderr,"C6F_PYR_LEVEL_DRAW level=%u second=%u invalid=%u at=%s:%u count=%u\n",level,withSecond,c->invalid,
    c->invalidFunction?c->invalidFunction:"-",c->invalidLine,c->count);
   assert(!c->invalid&&c->count>=1);
   {struct Dispatch *dj=tegu_command_job(c,c->count-1);const struct tegu_generic_draw_request *rq=&dj->genericDrawRequest;
    fprintf(stderr,"C6F_PYR_LEVEL_REQ level=%u %ux%u off=%llu row=%u bindings=%u\n",level,rq->width,rq->height,
     (unsigned long long)rq->target_offset,rq->row_bytes,rq->binding_count);
    for(unsigned bi=0;bi<rq->binding_count;bi++)fprintf(stderr,"C6F_PYR_LEVEL_BIND kind=%u tok=%d off=%llu bytes=%llu %ux%u\n",
     rq->bindings[bi].kind,rq->bindings[bi].memory.token==rq->target_token,(unsigned long long)rq->bindings[bi].memory.offset,
     (unsigned long long)rq->bindings[bi].memory.bytes,rq->bindings[bi].width,rq->bindings[bi].height);
    assert(dj->genericDrawSnapshot&&rq->width==W[level]&&rq->height==H[level]&&rq->target_offset==pyr->mip[level].offset&&
     rq->row_bytes==pyr->mip[level].stride);}
   [c commit];[c waitUntilCompleted];
   fprintf(stderr,"C6F_PYR_LEVEL_DONE level=%u second=%u status=%lu error=%p\n",level,withSecond,(unsigned long)[c status],[c error]);
   assert([c status]==4&&![c error]);checks++;
   [prev release];[pass release];
  }
  [ib release];[pyr release];[big release];[p release];[pd release];
 }
 /* (3) c6fc: sampled-only 'LA08' IOSurface as RG8Unorm 230x186 row 480. */
 {
  test_surface.width=230;test_surface.height=186;test_surface.row=480;test_surface.alloc=98304;test_surface.planes=0;
  test_surface.fourcc=0x4c413038u;
  for(unsigned y=0;y<186;y++)for(unsigned x=0;x<480;x++)c6f_big_surface[y*480+x]=(unsigned char)(x<460?(x*7+y*13):0xEE);
  TextureDesc *sd=[[TextureDesc alloc] init];sd->format=30;sd->w=230;sd->h=186;sd->uses=1;
  TeguTexture *la=[d newTextureWithDescriptor:(id)sd iosurface:(id)&test_surface plane:0];
  fprintf(stderr,"C6F_LA08 ptr=%p shadow=%d row=%lu fmt=%lu\n",la,la?la->shadowSurface!=0:0,(unsigned long)(la?la->rowBytes:0),(unsigned long)(la?la->format:0));
  assert(la&&la->shadowSurface&&la->format==30&&!(la->rowBytes&63)&&la->rowBytes>=460);
  assert(tegu_shadow_copy(la,0)==460u*186u);
  const unsigned char *b=(const unsigned char *)tegu_buffer_bytes(la->buffer)+la->mip[0].offset;
  for(unsigned y=0;y<186;y++)for(unsigned x=0;x<460;x++)assert(b[y*la->rowBytes+x]==(unsigned char)(x*7+y*13));
  checks++;[la release];
  /* A fourcc that does not name the descriptor layout stays refused. */
  sd->format=80;sd->w=230;test_surface.fourcc=0x4c413038u;
  TeguTexture *bad=[d newTextureWithDescriptor:(id)sd iosurface:(id)&test_surface plane:0];assert(!bad);checks++;
  [sd release];
 }
 free(c6f_big_surface);c6f_big_surface=0;
 [blur release];[blur16 release];[src release];[samp release];[vb release];
 printf("PASS C6f backboardd host test: %u checks (pipelines, CA allocations, two-attachment passes, IOSurface shadow/b3a8 refusal); no GPU claim\n",checks);
 return 0;
}
'''

out = HERE / 'ca_backboardd_host_test.m'
out.write_text(prefix + fixed_ui + main + tail)
for name in ('nord2_metal_bridge.c',):
    (HERE / ('hostbase_real_' + name)).write_bytes(pathlib.Path(os.environ.get('C6F_BRIDGE_SRC', str(G / 'metal' / name))).read_bytes())
(HERE / 'nord2_metal_bridge.h').write_bytes((G / 'metal' / 'nord2_metal_bridge.h').read_bytes())
libs = [G / 'build/deepseek-parallel-20261025/workers/compute/cargo-target/release',
        G / 'build/mesa-build/src/panfrost/clc']

def build(name, defines, asan):
    cmd = ['clang', '-fblocks', '-pthread', '-O1', '-g']
    if asan:
        cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    cmd += ['-Wno-incompatible-pointer-types', '-Wno-incompatible-function-pointer-types',
            '-fconstant-string-class=NXConstantString']
    cmd += ['-D' + x for x in defines] + ['-DC6F_ATTACHMENTS=' + os.environ.get('C6F_ATTACHMENTS', '2')]
    cmd += ['-I' + str(p) for p in (HERE, TEgu, G / 'metal', G / 'include', G / 'build/tegu-frontend')]
    cmd += [str(out), str(HERE / 'hostbase_real_nord2_metal_bridge.c'),
            str(G / 'metal/nord2_metal_render.c'), str(G / 'src/nord2_gpu_desc.c'), str(G / 'src/nord2_gpu_jm.c')]
    cmd += ['-lobjc', '-ldl', '-lBlocksRuntime'] + ['-L' + str(p) for p in libs] + [
        '-ltegu_air_bridge', '-lnord2_mali_runtime', '-o', str(HERE / name)]
    subprocess.run(cmd, check=True)
    return HERE / name

jobs = {x['id']: x for x in json.load(open(CORPUS / 'graphics-jobs.json'))['jobs']}
air = lambda job, stage: str(CORPUS / jobs[job][stage]['air_saved_as'])
args = [air('graphics-00139', 'vertex'), air('graphics-00139', 'fragment'), air('graphics-01847', 'fragment'),
        air('graphics-00089', 'vertex'), air('graphics-00160', 'fragment'),
        air('graphics-00168', 'vertex'), air('graphics-00168', 'fragment'),
        str(G / 'build/native/QuartzCore.metallib'), str(G / 'build/native/QuartzCore.metallib'), air('graphics-00165', 'fragment')]
env = dict(os.environ, LD_LIBRARY_PATH=':'.join(map(str, libs)),
           ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
ok = True
for name, defines, asan in (('ca_backboardd_host_test_asan', ['TEGU_HOST_REAL_V2', 'TEGU_INERT_PIPELINES'], True),
                            ('ca_backboardd_host_test_release', ['TEGU_HOST_REAL_V2', 'TEGU_INERT_PIPELINES'], False)):
    binary = build(name, defines, asan)
    r = subprocess.run([str(binary)] + args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    err = r.stderr.decode('utf-8', 'replace')
    for line in err.splitlines():
        if line.startswith(('C6F_', 'TEGU_COMMAND_FAILED', 'TEGU_DRAW_SILENT', 'TEGU_GENERIC_DRAW_REFUSED', 'TEGU_PASS_REFUSED', 'TEGU_INERT', 'TEGU_IOSURFACE_REFUSED', 'TEGU_PIPELINE_FALLBACK')) or 'Assertion' in line or 'ERROR' in line:
            print(line)
    print(r.stdout.decode().strip())
    inert = [l for l in err.splitlines() if l.startswith('TEGU_INERT_PIPELINE') and 'fmt=43,43 ' not in l]
    good = (r.returncode == 0 and not inert and 'TEGU_PASS_REFUSED' not in err and
            'TEGU_IOSURFACE_REFUSED reason=outside-bgra8-target-class' in err and 'fourcc=0x62336138' in err)
    pending = err.count('C6F_PENDING')
    print(('PASS' if good else 'FAIL'), name, 'rc=%d' % r.returncode, 'pending=%d' % pending)
    ok = ok and good
sys.exit(0 if ok else 1)
