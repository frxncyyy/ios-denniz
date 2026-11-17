/* Host test of the real active n2mb bridge (gpu/metal/nord2_metal_bridge.c):
 * TGP1 byte-compatibility, TGP2 manifest matching, table1/table2 assembly,
 * FAU buffers, BO spans, write masks and deterministic refusals. No GPU. */
#include "nord2_metal_bridge.h"
#include "panfrost/genxml/v9_pack.h"
#include "pipeline_format.h"
#include "nord2_texture_resources.h"
#include "nord2_gpu_transport.h"
#include "nord2_gpu_desc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Must match the bridge's internal command-memory partition. */
#define N2C_BUFFER_TABLE 40960u
#define N2C_BUFFER_TABLE_BYTES (TEGU_PIPELINE2_BUFFER_TABLE_RECORD*TEGU_PIPELINE2_MAX_BUFFERS)
#define FBO_MAX 64
#define FBO_SIZE 262144u /* root: holds 64B x 2048 threads x 1 core TLS */
struct fbo {unsigned handle;uint64_t va,size;unsigned char *host;int used,released;};
static struct fbo fbos[FBO_MAX];
static unsigned next_handle=1,submits,waits;static uint64_t next_va=0x100000000ull,job_id;
static struct nord2_gpu_submit_request last_req;static int have_req;
static struct fbo *find_handle(unsigned h){for(unsigned i=0;i<FBO_MAX;i++)if(fbos[i].used&&!fbos[i].released&&fbos[i].handle==h)return &fbos[i];return 0;}
static struct fbo *find_va(uint64_t va){for(unsigned i=0;i<FBO_MAX;i++)if(fbos[i].used&&va>=fbos[i].va&&va<fbos[i].va+FBO_SIZE)return &fbos[i];return 0;}
static unsigned live_fbos(void){unsigned n=0;for(unsigned i=0;i<FBO_MAX;i++)if(fbos[i].used&&!fbos[i].released)n++;return n;}
static int fake_call(void *o,unsigned sel,const uint64_t *in,unsigned ni,const void *bytes,size_t size,
 uint64_t *out,unsigned *no,void *os,size_t *osz)
{
 (void)o;(void)ni;
 switch(sel) {
 case NORD2_GPU_SEL_ALLOC: {
  size_t need=(size_t)in[0],rounded=(need+4095u)&~4095u;
  if(!need||need>FBO_SIZE)return 1;
  for(unsigned i=0;i<FBO_MAX;i++)if(!fbos[i].used) {
   fbos[i].host=calloc(1,FBO_SIZE);if(!fbos[i].host)return 1;
   fbos[i].used=1;fbos[i].handle=next_handle++;fbos[i].va=next_va;next_va+=FBO_SIZE;fbos[i].size=rounded;
   out[0]=fbos[i].handle;out[1]=fbos[i].va;out[2]=rounded;*no=3;return 0;
  }
  return 1;
 }
 case NORD2_GPU_SEL_RELEASE: {
  struct fbo *b=find_handle((unsigned)in[0]);if(!b)return 1;
  b->released=1;*no=0;return 0;
 }
 case NORD2_GPU_SEL_WAIT: out[0]=4;*no=1;waits++;return 0;
 case NORD2_GPU_SEL_SUBMIT_JM:
  if(size!=sizeof(struct nord2_gpu_submit_request))return 1;
  memcpy(&last_req,bytes,sizeof(last_req));have_req=1;submits++;out[0]=++job_id;*no=1;return 0;
 case NORD2_GPU_SEL_STATUS: {
  struct nord2_gpu_status *s=os;
  if(!s||!osz||*osz<sizeof(*s))return 1;
  memset(s,0,sizeof(*s));s->abi_version=2;s->enabled=s->powered=s->arch_ok=1;s->gpu_id=0x9000000;s->shader_present_lo=1;
  *osz=sizeof(*s);*no=0;return 0;
 }
 default:return 1;
 }
}
static int fake_map(void *o,unsigned h,uint64_t *addr,uint64_t *size)
{
 (void)o;struct fbo *b=find_handle(h);if(!b)return 1;*addr=(uint64_t)(uintptr_t)b->host;*size=FBO_SIZE;return 0;
}
static int fake_unmap(void *o,unsigned h,uint64_t a){(void)o;(void)h;(void)a;return 0;}
static int fake_close(void *o){(void)o;return 0;}
static void fake_lock(void *o){(void)o;}static void fake_unlock(void *o){(void)o;}

static uint32_t rd32(const unsigned char *p){return tegu_pipeline_word(p);}
static uint64_t rd64(const unsigned char *p){return (uint64_t)rd32(p)|((uint64_t)rd32(p+4)<<32);}
static void wr32(unsigned char *p,uint32_t v){tegu_pipeline_put(p,v);}
static void wr64(unsigned char *p,uint64_t v){wr32(p,(uint32_t)v);wr32(p+4,(uint32_t)(v>>32));}

static uint32_t g_tex_shape=1,g_extra_sampler=0,g_image_storage=37,g_ext_mask=0;
static void mflags(unsigned char *p,unsigned i,uint32_t f){wr32(p+TEGU_PIPELINE2_HEADER_BYTES+i*TEGU_PIPELINE2_MANIFEST_BYTES+28,f);}
static void manifest(unsigned char *p,unsigned i,uint32_t binding,uint32_t kind,uint32_t access,
 uint32_t table,uint32_t entry,uint32_t flags,uint32_t shape,uint32_t storage_format)
{
 unsigned char *r=p+TEGU_PIPELINE2_HEADER_BYTES+i*TEGU_PIPELINE2_MANIFEST_BYTES;
 wr32(r+0,0);wr32(r+4,binding);wr32(r+8,kind);wr32(r+12,1);
 wr32(r+16,access);wr32(r+20,table);wr32(r+24,entry);wr32(r+28,flags);
 wr32(r+32,storage_format);wr32(r+36,0);wr32(r+40,0);wr32(r+44,0);
 wr32(r+48,shape);wr32(r+52,0);wr32(r+56,0);wr32(r+60,0);
}
static size_t tgp2(unsigned char *out,size_t cap,int combined,int dummy,int image)
{
 memset(out,0,cap);
 unsigned ext_count=0;
 for(unsigned b=8;b<TEGU_PIPELINE2_MAX_BUFFERS;b++)if(g_ext_mask&(1u<<b))ext_count++;
 unsigned count=4+ext_count+(combined?2:0)+(dummy?1:0)+(image?1:0)+(g_extra_sampler?1:0);
 unsigned code_offset=TEGU_PIPELINE2_HEADER_BYTES+count*TEGU_PIPELINE2_MANIFEST_BYTES;
 const uint32_t code_bytes=8;
 assert(code_offset+code_bytes<=cap);
 wr32(out+0,TEGU_PIPELINE2_MAGIC);wr32(out+4,TEGU_PIPELINE2_VERSION);wr32(out+8,code_bytes);
 wr32(out+12,TEGU_PIPELINE_GPU);wr32(out+16,8);wr32(out+20,8);wr32(out+24,0);wr32(out+28,0);
 wr32(out+32,3); /* UBO bit0, SSBO bit1 */
 wr32(out+36,1);wr32(out+40,1);wr32(out+44,1);
 wr32(out+48,TEGU_PIPELINE2_HEADER_BYTES);wr32(out+52,count);wr32(out+56,TEGU_PIPELINE2_MANIFEST_BYTES);
 wr32(out+60,code_offset);wr32(out+64,(dummy?TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER:0)|
  (g_ext_mask?TEGU_PIPELINE2_FLAG_BUFFER_TABLE:0));
 wr32(out+TEGU_PIPELINE2_WORD_BUFFER_MASK*4,g_ext_mask);
 unsigned at=0;
 manifest(out,at++,0,TEGU_PIPELINE2_KIND_UBO,TEGU_PIPELINE2_ACCESS_READ,TEGU_PIPELINE2_TABLE_FAU,0,0,0,0);
 manifest(out,at++,1,TEGU_PIPELINE2_KIND_SSBO,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,
  TEGU_PIPELINE2_TABLE_FAU,1,0,0,0);
 manifest(out,at++,3,TEGU_PIPELINE2_KIND_TEXTURE,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
  TEGU_PIPELINE2_TABLE_TEXTURE,3,0,g_tex_shape,0);
 manifest(out,at++,6,TEGU_PIPELINE2_KIND_SAMPLER,0,TEGU_PIPELINE2_TABLE_SAMPLER,6,0,0,0);
 if(g_extra_sampler) manifest(out,at++,31,TEGU_PIPELINE2_KIND_SAMPLER,0,TEGU_PIPELINE2_TABLE_SAMPLER,31,0,0,0);
 if(combined) {
  unsigned c=at;
  manifest(out,at++,5,TEGU_PIPELINE2_KIND_TEXTURE,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   TEGU_PIPELINE2_TABLE_TEXTURE,5,0,g_tex_shape,0);
  manifest(out,at++,5,TEGU_PIPELINE2_KIND_SAMPLER,0,TEGU_PIPELINE2_TABLE_SAMPLER,5,0,0,0);
  mflags(out,c,TEGU_PIPELINE2_FLAG_DECLARED_COMBINED);
  mflags(out,c+1,TEGU_PIPELINE2_FLAG_DECLARED_COMBINED);
 }
 if(image) manifest(out,at++,4,TEGU_PIPELINE2_KIND_IMAGE,TEGU_PIPELINE2_ACCESS_STORAGE|TEGU_PIPELINE2_ACCESS_WRITE,
  TEGU_PIPELINE2_TABLE_TEXTURE,4,0,1u,g_image_storage);
 for(unsigned b=8;b<TEGU_PIPELINE2_MAX_BUFFERS;b++)if(g_ext_mask&(1u<<b))
  manifest(out,at++,b,TEGU_PIPELINE2_KIND_SSBO,
   TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,
   TEGU_PIPELINE2_TABLE_BUFFER,b,0,0,0);
 if(dummy) manifest(out,at++,TEGU_PIPELINE2_DUMMY_BINDING,TEGU_PIPELINE2_KIND_SAMPLER,0,
  TEGU_PIPELINE2_TABLE_SAMPLER,TEGU_PIPELINE2_DUMMY_SAMPLER_ENTRY,TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER,0,0);
 assert(at==count);
 out[code_offset]=0xA5;out[code_offset+1]=0x5A;
 return code_offset+code_bytes;
}
static size_t tgp1(unsigned char *out,size_t cap)
{
 memset(out,0,cap);
 wr32(out+0,TEGU_PIPELINE_MAGIC);wr32(out+4,1);wr32(out+8,8);wr32(out+12,TEGU_PIPELINE_GPU);
 wr32(out+16,8);wr32(out+20,8);wr32(out+24,0);wr32(out+28,0);
 wr32(out+32,1);wr32(out+36,1);wr32(out+40,1);wr32(out+44,1);
 wr32(out+48,0);wr32(out+52,0);wr32(out+56,1);wr32(out+60,0);
 out[TEGU_PIPELINE_HEADER_BYTES]=0x11;out[TEGU_PIPELINE_HEADER_BYTES+1]=0x22;
 return TEGU_PIPELINE_HEADER_BYTES+8;
}
static void desc(unsigned char *wire,unsigned i,uint32_t binding,uint32_t kind,uint32_t metal,uint32_t flags,
 uint64_t token,uint64_t offset,uint64_t span)
{
 unsigned char *d=wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+i*TEGU_PIPELINE_DISPATCH2_DESC_BYTES;
 wr32(d+TEGU_PIPELINE_D2_BINDING,binding);wr32(d+TEGU_PIPELINE_D2_KIND,kind);
 wr32(d+TEGU_PIPELINE_D2_METAL_INDEX,metal);wr32(d+TEGU_PIPELINE_D2_FLAGS,flags);
 wr64(d+TEGU_PIPELINE_D2_TOKEN,token);wr64(d+TEGU_PIPELINE_D2_OFFSET,offset);wr64(d+TEGU_PIPELINE_D2_SPAN,span);
}
static void texture_desc(unsigned char *wire,unsigned i,uint32_t binding,uint32_t kind,uint32_t metal,uint32_t flags,
 uint64_t token,uint64_t offset,uint64_t span,uint32_t format,uint32_t width,uint32_t height,uint32_t row,uint32_t levels)
{
 desc(wire,i,binding,kind,metal,flags,token,offset,span);
 unsigned char *d=wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+i*TEGU_PIPELINE_DISPATCH2_DESC_BYTES;
 wr32(d+TEGU_PIPELINE_D2_FORMAT,format);wr32(d+TEGU_PIPELINE_D2_VIEW_FORMAT,0);
 wr32(d+TEGU_PIPELINE_D2_DIM,2);wr32(d+TEGU_PIPELINE_D2_WIDTH,width);wr32(d+TEGU_PIPELINE_D2_HEIGHT,height);
 wr32(d+TEGU_PIPELINE_D2_DEPTH,1);wr32(d+TEGU_PIPELINE_D2_LAYERS,1);wr32(d+TEGU_PIPELINE_D2_LEVELS,levels);
 wr32(d+TEGU_PIPELINE_D2_SAMPLES,1);wr32(d+TEGU_PIPELINE_D2_USAGE,kind==TEGU_PIPELINE2_KIND_IMAGE?2:1);
 wr32(d+TEGU_PIPELINE_D2_ROW_STRIDE,row);wr32(d+TEGU_PIPELINE_D2_STORAGE_MODE,0);
 wr64(d+TEGU_PIPELINE_D2_LEVEL_OFFSET,0);
}
static void sampler_desc(unsigned char *wire,unsigned i,uint32_t binding,uint32_t metal)
{
 desc(wire,i,binding,TEGU_PIPELINE2_KIND_SAMPLER,metal,0,0,0,0);
 unsigned char *d=wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+i*TEGU_PIPELINE_DISPATCH2_DESC_BYTES;
 wr32(d+TEGU_PIPELINE_D2_MIN_FILTER,0);wr32(d+TEGU_PIPELINE_D2_MAG_FILTER,0);wr32(d+TEGU_PIPELINE_D2_MIP_FILTER,0);
 wr32(d+TEGU_PIPELINE_D2_ADDRESS_S,0);wr32(d+TEGU_PIPELINE_D2_ADDRESS_T,0);wr32(d+TEGU_PIPELINE_D2_ADDRESS_R,0);
 wr32(d+TEGU_PIPELINE_D2_BORDER_COLOR,0);wr32(d+TEGU_PIPELINE_D2_NORMALIZED,1);
 wr32(d+TEGU_PIPELINE_D2_COMPARE,0);wr32(d+TEGU_PIPELINE_D2_MAX_ANISOTROPY,1);
 wr32(d+TEGU_PIPELINE_D2_REDUCTION,0);wr32(d+TEGU_PIPELINE_D2_LOD_AVERAGE,0);
 wr32(d+TEGU_PIPELINE_D2_LOD_MIN,0);wr32(d+TEGU_PIPELINE_D2_LOD_MAX,0);
}
static void wire_header(unsigned char *wire,unsigned n,uint32_t push_word)
{
 memset(wire,0,TEGU_PIPELINE_DISPATCH2_BYTES);
 wr32(wire+0,TEGU_PIPELINE_DISPATCH2_MAGIC);wr32(wire+4,TEGU_PIPELINE_DISPATCH2_VERSION);
 wr32(wire+8,n);wr32(wire+12,2);wr32(wire+16,1);wr32(wire+20,1);wr32(wire+24,8);wr32(wire+28,0);
 wr32(wire+TEGU_PIPELINE_DISPATCH2_PUSH_OFFSET,push_word);
}
static struct bridge_ctx {struct n2mb_ops ops;uintptr_t dev;unsigned ubo,ssbo,tex,tex2,img;unsigned ext[8];} C;
static void open_bridge(void)
{
 memset(&C,0,sizeof(C));
 C.ops=(struct n2mb_ops){.call=fake_call,.map=fake_map,.unmap=fake_unmap,.close=fake_close,
  .lock=fake_lock,.unlock=fake_unlock};
 C.dev=n2mb_open(&C.ops);assert(C.dev);
 uint64_t out[3];unsigned n=3;
 assert(!n2mb_scalar(C.dev,NORD2_GPU_SEL_ALLOC,(uint64_t[]){4096},1,out,&n));
 C.ubo=(unsigned)out[0];
 assert(!n2mb_scalar(C.dev,NORD2_GPU_SEL_ALLOC,(uint64_t[]){4096},1,out,&n));C.ssbo=(unsigned)out[0];
 assert(!n2mb_scalar(C.dev,NORD2_GPU_SEL_ALLOC,(uint64_t[]){4096},1,out,&n));C.tex=(unsigned)out[0];
 assert(!n2mb_scalar(C.dev,NORD2_GPU_SEL_ALLOC,(uint64_t[]){4096},1,out,&n));C.tex2=(unsigned)out[0];
 assert(!n2mb_scalar(C.dev,NORD2_GPU_SEL_ALLOC,(uint64_t[]){4096},1,out,&n));C.img=(unsigned)out[0];
 for(unsigned i=0;i<8;i++) {
  assert(!n2mb_scalar(C.dev,NORD2_GPU_SEL_ALLOC,(uint64_t[]){4096},1,out,&n));
  C.ext[i]=(unsigned)out[0];
 }
}
static uint64_t import_package(const unsigned char *pkg,size_t n)
{
 uint64_t handle=0;unsigned count=1;
 int e=n2mb_method(C.dev,5,0,0,pkg,n,&handle,&count,0,0);
 assert(!e&&count==1&&handle);
 return handle;
}
static int dispatch(uint64_t pipeline,const unsigned char *wire,uint64_t *out)
{
 unsigned count=2;
 return n2mb_method(C.dev,7,&pipeline,1,wire,TEGU_PIPELINE_DISPATCH2_BYTES,out,&count,0,0);
}
static unsigned char *cmd_memory(uint64_t *va_out)
{
 assert(have_req&&last_req.nr_handles);
 struct fbo *b=find_va(last_req.heads[0]);assert(b);*va_out=b->va;
 return b->host+(last_req.heads[0]-b->va);
}
static void check(int condition,const char *what)
{
 if(!condition){fprintf(stderr,"FAIL %s\n",what);exit(1);}
}
static int run_real_package(const char *path)
{
 FILE *f=fopen(path,"rb");if(!f){fprintf(stderr,"open real package failed: %s\n",path);return 1;}
 if(fseek(f,0,SEEK_END)||ftell(f)<=0){fclose(f);return 1;}long n=ftell(f);rewind(f);
 unsigned char *pkg=malloc((size_t)n);if(!pkg||fread(pkg,1,(size_t)n,f)!=(size_t)n){fclose(f);free(pkg);return 1;}fclose(f);
 struct tegu_pipeline_info2 pi={0};int pr=tegu_pipeline_parse2(pkg,(size_t)n,&pi);
 fprintf(stderr,"REAL_PACKAGE parse=%d bytes=%ld manifests=%u push=%u flag=%u mask=%x\n",pr,n,pi.manifest_count,pi.push_words,pi.flags,pi.buffer_table_mask);
 if(pr||pi.manifest_count>TEGU_PIPELINE2_MAX_RESOURCES){free(pkg);return 1;}
 open_bridge();
 unsigned char wire[TEGU_PIPELINE_DISPATCH2_BYTES];
 wire_header(wire,pi.manifest_count,0);
 wr32(wire+24,pi.push_words);
 unsigned ext=0;
 for(unsigned i=0;i<pi.manifest_count;i++) {
  const struct tegu_pipeline_manifest *m=&pi.manifest[i];
  if(m->kind!=TEGU_PIPELINE2_KIND_UBO&&m->kind!=TEGU_PIPELINE2_KIND_SSBO){fprintf(stderr,"real package non-buffer kind=%u\n",m->kind);free(pkg);return 1;}
  uint64_t o[3];unsigned c=3;
  if(n2mb_scalar(C.dev,NORD2_GPU_SEL_ALLOC,(uint64_t[]){64},1,o,&c)||c!=2){free(pkg);return 1;}
  desc(wire,i,m->binding,m->kind,m->binding,m->access,o[0],0,64);
  if(m->table==TEGU_PIPELINE2_TABLE_BUFFER)ext|=1u<<m->binding;
 }
 uint64_t pipe=import_package(pkg,(size_t)n);
 uint64_t out[2];submits=0;have_req=0;
 int e=dispatch(pipe,wire,out);
 fprintf(stderr,"REAL_DISPATCH e=%d submits=%u handles=%u ext=%x\n",e,submits,have_req?last_req.nr_handles:0,ext);
 if(e||!submits){free(pkg);return 1;}
 uint64_t va;unsigned char *m=cmd_memory(&va);
 if(ext) {
  if(rd64(m+256+TEGU_PIPELINE2_BUFFER_TABLE_FAU)!=va+N2C_BUFFER_TABLE){free(pkg);return 1;}
  for(unsigned i=0;i<pi.manifest_count;i++) {
   const struct tegu_pipeline_manifest *me=&pi.manifest[i];
   if(me->table!=TEGU_PIPELINE2_TABLE_BUFFER)continue;
   unsigned char *rec=m+N2C_BUFFER_TABLE+16u*me->binding;
   if(rd32(rec+8)!=64||rd32(rec+12)){free(pkg);return 1;}
  }
 }
 free(pkg);
 printf("PASS real corpus buffer table: manifests=%u ext=%x\n",pi.manifest_count,ext);
 return 0;
}

int main(int argc,char **argv)
{
 if(argc>1)return run_real_package(argv[1]);
 unsigned char pkg[16384],wire[TEGU_PIPELINE_DISPATCH2_BYTES];uint64_t out[2];
 open_bridge();
 check(n2mb_draw_v2(C.dev,0,0,0,0,0)==(int)0xe00002c2u,"additive draw_v2 symbol/refusal");
 {uint64_t p=0;
  check(n2mb_compute_srt_pointer(0x100000000ull,0x100100000ull,4,&p)==0&&p==0x100000004ull,"SRT pointer count4");
  check(n2mb_compute_srt_pointer(0x100000000ull,0x100100000ull,8,&p)==0&&p==0x100000008ull,"SRT pointer count8");
  check(n2mb_compute_srt_pointer(0x100000000ull,0x100100000ull,3,&p)!=0,"SRT rejects count3");
  check(n2mb_compute_srt_pointer(0x100000000ull,0x100100000ull,64,&p)!=0,"SRT rejects count64");
  check(n2mb_compute_srt_pointer(0x100000020ull,0x100100000ull,4,&p)!=0,"SRT rejects misalignment");
  check(n2mb_compute_srt_pointer(0x100000000ull,0x100000030ull,4,&p)!=0,"SRT rejects undersized span");
 }
 /* --- TGP1 regression: byte-identical FAU path, chain.resources zero. --- */
 {
  size_t bytes=tgp1(pkg,sizeof(pkg));uint64_t pipe=import_package(pkg,bytes);
  memset(wire,0,TEGU_PIPELINE_DISPATCH_BYTES);
  wr32(wire+0,1);wr32(wire+4,2);wr32(wire+8,1);wr32(wire+12,1);
  wr32(wire+16,C.ubo);wr32(wire+20,0);wr32(wire+24,64);wr32(wire+28,0);
  wr32(wire+144,0x12345678);
  struct fbo *b=find_handle(C.ubo);assert(b);
  unsigned count=2;submits=0;
  int e=n2mb_method(C.dev,7,&pipe,1,wire,TEGU_PIPELINE_DISPATCH_BYTES,out,&count,0,0);
  check(!e&&submits==1,"TGP1 dispatch");
  uint64_t va;unsigned char *m=cmd_memory(&va);
  check(rd64(m+96)==0,"TGP1 chain.resources zero");
  check(rd64(m+256)==b->va&&rd32(m+264)==64,"TGP1 FAU descriptor");
  check(rd32(m+384)==0x12345678,"TGP1 push");
 }
 /* --- TGP2 full manifest: tables, FAU, sampler, combined, image, write mask. --- */
 {
  size_t bytes=tgp2(pkg,sizeof(pkg),1,0,1);struct tegu_pipeline_info2 pi;
  check(!tegu_pipeline_parse2(pkg,bytes,&pi),"TGP2 parse before import");
  unsigned before=next_handle;uint64_t pipe=import_package(pkg,bytes);
  {struct fbo *code=0;for(unsigned i=0;i<FBO_MAX;i++)if(fbos[i].used&&!fbos[i].released&&fbos[i].handle>=before){code=&fbos[i];break;}
   check(code&&code->host&&code->host[0]==0xA5&&code->host[1]==0x5A,"TGP2 imports code at code_offset, not header+128");}
  wire_header(wire,6,0x11223344);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,7,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,30,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,5,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,256,64*16,80,16,16,64,1);
  sampler_desc(wire,3,6,2);
  texture_desc(wire,4,5,TEGU_PIPELINE2_KIND_COMBINED,9,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE|
   TEGU_PIPELINE2_DESC_SWIZZLE_VALID,
   C.tex2,0,64*16,80,16,16,64,1);
  wr32(wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+4*TEGU_PIPELINE_DISPATCH2_DESC_BYTES+TEGU_PIPELINE_D2_SWIZZLE,0);
  wr32(wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+4*TEGU_PIPELINE_DISPATCH2_DESC_BYTES+TEGU_PIPELINE_D2_SAMPLER_INDEX,4);
  wr32(wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+4*TEGU_PIPELINE_DISPATCH2_DESC_BYTES+TEGU_PIPELINE_D2_SAMPLER_FLAGS,0);
  {
   unsigned char *d=wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+4*TEGU_PIPELINE_DISPATCH2_DESC_BYTES;
   wr32(d+TEGU_PIPELINE_D2_MIN_FILTER,0);wr32(d+TEGU_PIPELINE_D2_MAG_FILTER,0);wr32(d+TEGU_PIPELINE_D2_MIP_FILTER,0);
   wr32(d+TEGU_PIPELINE_D2_ADDRESS_S,0);wr32(d+TEGU_PIPELINE_D2_ADDRESS_T,0);wr32(d+TEGU_PIPELINE_D2_ADDRESS_R,0);
   wr32(d+TEGU_PIPELINE_D2_NORMALIZED,1);wr32(d+TEGU_PIPELINE_D2_MAX_ANISOTROPY,1);
  }
  texture_desc(wire,5,4,TEGU_PIPELINE2_KIND_IMAGE,6,TEGU_PIPELINE2_ACCESS_STORAGE|TEGU_PIPELINE2_ACCESS_WRITE,
   C.img,0,128*16,70,16,16,128,1);
  wr32(wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+5*TEGU_PIPELINE_DISPATCH2_DESC_BYTES+TEGU_PIPELINE_D2_VIEW_FORMAT_VK,37);
  wr32(wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+5*TEGU_PIPELINE_DISPATCH2_DESC_BYTES+TEGU_PIPELINE_D2_VIEW_FORMAT,70);
  submits=0;have_req=0;
  int e=dispatch(pipe,wire,out);
  check(!e&&submits==1,"TGP2 dispatch");
  struct fbo *ubo=find_handle(C.ubo),*ssbo=find_handle(C.ssbo),*tex=find_handle(C.tex);
  uint64_t va;unsigned char *m=cmd_memory(&va);
  check((rd64(m+96)&~0x3full)==va+768&&(rd64(m+96)&0x3fu)==4,"TGP2 chain.resources SRT address+count");
  check((rd32(m+128)&15u)!=0&&((rd32(m+128)>>4)&15u)==1&&rd64(m+136)!=0,"TGP2 shader program descriptor");
  check(rd32(m+196)==0x3f,"TGP2 local storage descriptor");
  {unsigned char expected[32];nord2_desc_local_storage(expected,0,0,0,0);check(!memcmp(m+192,expected,32),"TGP2 no-TLS/WLS LOCAL_STORAGE is byte-identical");}
  check((rd64(m+768+16)&((1ull<<56)-1))==va+832,"TGP2 table1 resource entry");
  check(rd64(m+768+24)==6*32,"TGP2 table1 size highest entry");
  check((rd64(m+768+32)&((1ull<<56)-1))==va+9024,"TGP2 table2 resource entry");
  check(rd64(m+768+40)==7*32,"TGP2 table2 size highest entry");
  check(rd64(m+256)==ubo->va&&rd32(m+264)==64,"TGP2 UBO FAU");
  check(rd64(m+272)==ssbo->va&&rd32(m+280)==64,"TGP2 SSBO FAU");
  check(rd32(m+384)==0x11223344,"TGP2 push");
  unsigned char *t1=m+832,*t2=m+9024;
  check((rd32(t1+3*32)&15u)==2,"texture descriptor type");
  check(rd32(t1+3*32+4)==(15u|(15u<<16)),"texture width/height");
  check(rd64(t1+3*32+16)==va+17248+2*16*32,"texture surface points at its plane");
  check((rd32(t1+4*32)&15u)==2,"image descriptor uses table1");
  check(rd64(t1+4*32+16)==va+17248+5*16*32,"image surface points at its plane");
  check((rd32(t2+6*32)&15u)==1,"sampler descriptor type");
  check((rd32(t2+5*32)&15u)==1,"combined sampler at native binding 5");
  check((rd32(t1+5*32+8)&0xfffu)==0,"explicit all-zero swizzle flag reaches descriptor word2");
  check(rd64(m+18272+8)==tex->va+256,"nonzero texture base reaches the plane pointer");
  check((rd32(m+18272)&15u)==10,"texture plane type");
  check((rd32(m+19808)&15u)==10,"image plane type");
  int ubo_write=0,ssbo_write=0,tex_write=0,img_write=0;
  for(unsigned i=0;i<last_req.nr_handles;i++) {
   if(last_req.handles[i]==C.ubo)ubo_write=(last_req.write_mask>>i)&1;
   if(last_req.handles[i]==C.ssbo)ssbo_write=(last_req.write_mask>>i)&1;
   if(last_req.handles[i]==C.tex)tex_write=(last_req.write_mask>>i)&1;
   if(last_req.handles[i]==C.img)img_write=(last_req.write_mask>>i)&1;
  }
  check(!ubo_write&&ssbo_write&&!tex_write&&img_write,"TGP2 write-mask classification");
 }
 /* --- Extended set0 buffer table: bindings 8..30 through real bridge. --- */
 {
  g_ext_mask=(1u<<8)|(1u<<9)|(1u<<10);
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,0);
   uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,7,0x51525354);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,
   TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,0,
   TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,C.tex,0,64*16,80,16,16,64,1);
  sampler_desc(wire,3,6,0);
  for(unsigned i=0;i<3;i++)desc(wire,4+i,8+i,TEGU_PIPELINE2_KIND_SSBO,8+i,
   TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ext[i],0,64);
  submits=0;have_req=0;
  int e=dispatch(pipe,wire,out);
  check(!e&&submits==1,"extended buffer table dispatch");
  uint64_t va;unsigned char *m=cmd_memory(&va);
  check(rd64(m+256+TEGU_PIPELINE2_BUFFER_TABLE_FAU)==va+N2C_BUFFER_TABLE,
        "extended buffer table FAU pointer");
  check(rd32(m+256+TEGU_PIPELINE2_BUFFER_TABLE_FAU+8)==N2C_BUFFER_TABLE_BYTES,
        "extended buffer table byte span");
  for(unsigned i=0;i<3;i++) {
   struct fbo *b=find_handle(C.ext[i]);
   unsigned char *rec=m+N2C_BUFFER_TABLE+16u*(8+i);
   check(rd64(rec)==b->va&&rd32(rec+8)==64&&rd32(rec+12)==0,
         "extended buffer table record address/length");
  }
  for(unsigned i=0;i<3;i++) {
   int seen=0,write=0;
   for(unsigned j=0;j<last_req.nr_handles;j++)if(last_req.handles[j]==C.ext[i]) {
    seen=1;write=(last_req.write_mask>>j)&1;
   }
   check(seen&&write,"extended buffer retained write");
  }
  /* A package that advertises table bindings but omits their descriptors is
   * refused before submit. */
  wire_header(wire,4,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,
   TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,0,
   TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,C.tex,0,64*16,80,16,16,64,1);
  sampler_desc(wire,3,6,0);
  submits=0;check(dispatch(pipe,wire,out)&&submits==0,
     "missing extended buffer descriptors refused");
  g_ext_mask=0;
 }

 /* --- TLS/WLS LOCAL_STORAGE allocation and packing. --- */
 {
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,0);
  wr32(pkg+4*TEGU_PIPELINE2_WORD_TLS,64);wr32(pkg+4*TEGU_PIPELINE2_WORD_WLS,256);
  struct tegu_pipeline_info2 pi;
  check(!tegu_pipeline_parse2(pkg,bytes,&pi)&&pi.tls_size==64&&pi.wls_size==256,"TLS/WLS package fields");
  uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,4,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,5,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,0,64*16,80,16,16,64,1);
  sampler_desc(wire,3,6,2);
  uint64_t before_va=next_va;unsigned live_before=live_fbos();submits=0;have_req=0;
  int e=dispatch(pipe,wire,out);
  check(!e&&submits==1,"TLS/WLS dispatch");
  check(live_fbos()==live_before,"TLS/WLS BOs released after job completion");
  uint64_t va;unsigned char *m=cmd_memory(&va);
  check(rd32(m+192)==2,"TLS shift from 64 bytes/thread");
  check((rd32(m+196)&31u)==1&&((rd32(m+196)>>8)&31u)==9,"WLS two instances and size-scale9");
  uint64_t tls=(rd64(m+200)&0xffffffffffffULL)<<8; /* word2+3 low48 */
  uint64_t wls=rd64(m+208);
  check(tls&&(tls&4095u)==0&&wls&&(wls&4095u)==0,"TLS/WLS bases 4K aligned");
  check((rd32(m+204)&(1u<<28))!=0,"TLS packed address mode");
  int seen_tls=0,seen_wls=0;
  for(unsigned i=0;i<FBO_MAX;i++)if(fbos[i].used&&fbos[i].va>=before_va) {
   if(fbos[i].va==tls&&fbos[i].size>=131072)seen_tls=1;
   if(fbos[i].va==wls&&fbos[i].size>=512)seen_wls=1;
  }
  check(seen_tls&&seen_wls,"TLS/WLS BOs are owned and sized to the Mesa formula");
  {MALI_LOCAL_STORAGE_PACKED_T lp;struct MALI_LOCAL_STORAGE ls;
   memcpy(lp.opaque,m+192,32);MALI_LOCAL_STORAGE_unpack(&lp,&ls);
   check(ls.tls_size==2&&ls.wls_instances==2&&ls.wls_size_base==0&&ls.wls_size_scale==9&&
         ls.tls_base_pointer==(tls>>8)&&ls.tls_address_mode==1&&ls.wls_base_pointer==wls,
         "v9_pack oracle agrees with emitted LOCAL_STORAGE words");}
 /* TLS-only package (the observed D8F18H private-memory shape). */
 {
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,0);wr32(pkg+4*TEGU_PIPELINE2_WORD_TLS,64);
  uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,4,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,5,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,0,64*16,80,16,16,64,1);
  sampler_desc(wire,3,6,2);
  unsigned live=live_fbos();submits=0;int e=dispatch(pipe,wire,out);
  uint64_t va;unsigned char *m=cmd_memory(&va);
  check(!e&&submits==1&&live_fbos()==live,"TLS-only dispatch");
  check(rd32(m+192)==2&&(rd32(m+196)&31u)==31&&((rd32(m+196)>>5)&1u)==0,"TLS-only field/shift and Mesa no-WLS marker");
  uint64_t tls=(rd64(m+200)&0xffffffffffffULL)<<8;
  check(tls&&(tls&4095u)==0&&(rd32(m+204)&(1u<<28))&&rd64(m+208)==0,"TLS-only packed base and zero WLS base");
  {MALI_LOCAL_STORAGE_PACKED_T lp;struct MALI_LOCAL_STORAGE ls;memcpy(lp.opaque,m+192,32);
   MALI_LOCAL_STORAGE_unpack(&lp,&ls);
   check(ls.tls_size==2&&ls.tls_base_pointer==(tls>>8)&&ls.tls_address_mode==1&&ls.wls_instances==(1u<<31),
         "v9_pack oracle TLS-only");}
 }
   /* WLS-only package: private storage absent, shared storage present. */
 {
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,0);wr32(pkg+4*TEGU_PIPELINE2_WORD_WLS,256);
  uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,4,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,5,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,0,64*16,80,16,16,64,1);
  sampler_desc(wire,3,6,2);
  unsigned live=live_fbos();submits=0;int e=dispatch(pipe,wire,out);
  uint64_t va;unsigned char *m=cmd_memory(&va);
  check(!e&&submits==1&&live_fbos()==live,"WLS-only dispatch");
  check(rd32(m+192)==0&&(rd32(m+196)&31u)==1&&((rd32(m+196)>>8)&31u)==9,"WLS-only shift zero and packed instances/scale");
  uint64_t wls=rd64(m+208);
  check(wls&&(wls&4095u)==0&&rd32(m+204)==0,"WLS-only base and flat TLS mode");
  {MALI_LOCAL_STORAGE_PACKED_T lp;struct MALI_LOCAL_STORAGE ls;memcpy(lp.opaque,m+192,32);
   MALI_LOCAL_STORAGE_unpack(&lp,&ls);
   check(ls.tls_size==0&&ls.wls_instances==2&&ls.wls_size_scale==9&&ls.wls_base_pointer==wls,
         "v9_pack oracle WLS-only");}
 }
  /* Allocation failure is an explicit refusal, never a zero LOCAL_STORAGE. */
  bytes=tgp2(pkg,sizeof(pkg),0,0,0);wr32(pkg+4*TEGU_PIPELINE2_WORD_TLS,32768);
  pipe=import_package(pkg,bytes);wire_header(wire,4,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,5,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,0,64*16,80,16,16,64,1);
  sampler_desc(wire,3,6,2);
  submits=0;check(dispatch(pipe,wire,out)&&submits==0,"oversized TLS allocation refused before submit");
  bytes=tgp2(pkg,sizeof(pkg),0,0,0);wr32(pkg+4*TEGU_PIPELINE2_WORD_TLS,64);wr32(pkg+4*TEGU_PIPELINE2_WORD_WLS,32768);
  pipe=import_package(pkg,bytes);wire_header(wire,4,0);wr32(wire+12,16); /* root: 32KiB x16 WGs exceeds fake FBO */
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,5,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,0,64*16,80,16,16,64,1);
  sampler_desc(wire,3,6,2);
  live_before=live_fbos();submits=0;
  check(dispatch(pipe,wire,out)&&submits==0&&live_fbos()==live_before,"partial TLS/WLS allocation released on refusal");
 }
 /* --- 2-layer texture array: both plane descriptors, nonzero base. --- */
 {
  g_tex_shape=17; /* 2D, arrayed */
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,0);uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,4,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,0,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,256,2*64*16,80,16,16,64,1);
  unsigned char *d=wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+2*TEGU_PIPELINE_DISPATCH2_DESC_BYTES;
  wr32(d+TEGU_PIPELINE_D2_DIM,3);wr32(d+TEGU_PIPELINE_D2_LAYERS,2);wr32(d+TEGU_PIPELINE_D2_LAYER_STRIDE,64*16);
  sampler_desc(wire,3,6,0);
  check(!dispatch(pipe,wire,out),"2-layer array dispatch");
  uint64_t va;unsigned char *m=cmd_memory(&va);struct fbo *t=find_handle(C.tex);
  check(rd64(m+832+3*32+16)==va+18272,"array table1 points at plane payload");
  check(rd64(m+18272+8)==t->va+256,"array plane0 pointer");
  check(rd64(m+18272+32+8)==t->va+256+64*16,"array plane1 pointer");
  g_tex_shape=1;
 }
 /* --- Formatless storage declaration still needs the actual Vk view. --- */
 {
  g_image_storage=0;
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,1);uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,5,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,0,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,0,64*16,80,16,16,64,1);
  sampler_desc(wire,3,6,0);
  texture_desc(wire,4,4,TEGU_PIPELINE2_KIND_IMAGE,0,TEGU_PIPELINE2_ACCESS_STORAGE|TEGU_PIPELINE2_ACCESS_WRITE,
   C.img,0,128*16,70,16,16,128,1);
  wr32(wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+4*TEGU_PIPELINE_DISPATCH2_DESC_BYTES+TEGU_PIPELINE_D2_VIEW_FORMAT_VK,37);
  wr32(wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+4*TEGU_PIPELINE_DISPATCH2_DESC_BYTES+TEGU_PIPELINE_D2_VIEW_FORMAT,70);
  check(!dispatch(pipe,wire,out),"formatless storage declaration with real Vk view");
  g_image_storage=37;
 }
 /* --- A correct compare sampler flag is accepted. --- */
 {
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,0);
  mflags(pkg,3,TEGU_PIPELINE2_FLAG_SAMPLER_COMPARE);
  uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,4,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,0,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,0,64*16,80,16,16,64,1);
  sampler_desc(wire,3,6,0);
  wr32(wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+3*TEGU_PIPELINE_DISPATCH2_DESC_BYTES+TEGU_PIPELINE_D2_SAMPLER_FLAGS,1);
  check(!dispatch(pipe,wire,out),"compare sampler flags accepted");
 }
 /* --- Atomic RMW is refused until the transport implements it. --- */
 {
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,0);
  wr32(pkg+TEGU_PIPELINE2_HEADER_BYTES+1*TEGU_PIPELINE2_MANIFEST_BYTES+16,
       TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE|TEGU_PIPELINE2_ACCESS_ATOMIC);
  uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,4,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,
       TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE|TEGU_PIPELINE2_ACCESS_ATOMIC,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,0,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,0,64*16,80,16,16,64,1);
  sampler_desc(wire,3,6,0);
  submits=0;check(dispatch(pipe,wire,out)&&submits==0,"atomic RMW refused until implemented");
 }
 /* --- Native sampler binding 31 is independent of Metal slot capacity. --- */
 {
  g_extra_sampler=1;
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,0);uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,5,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,0,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,0,64*16,80,16,16,64,1);
  sampler_desc(wire,3,6,0);
  sampler_desc(wire,4,31,0);
  check(!dispatch(pipe,wire,out),"native sampler31 dispatch");
  uint64_t va;unsigned char *m=cmd_memory(&va);
  check(rd64(m+768+40)==32*32,"sampler table sizes to highest native binding");
  check((rd32(m+9024+31*32)&15u)==1,"sampler descriptor at native binding31");
  g_extra_sampler=0;
 }
 /* --- Dummy sampler requires table2[256] and a 257-entry table. --- */
 {
  size_t bytes=tgp2(pkg,sizeof(pkg),0,1,1);uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,5,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,0,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,0,64*16,80,16,16,64,1);
  sampler_desc(wire,3,6,0);
  texture_desc(wire,4,4,TEGU_PIPELINE2_KIND_IMAGE,1,TEGU_PIPELINE2_ACCESS_STORAGE|TEGU_PIPELINE2_ACCESS_WRITE,
   C.img,0,128*16,70,16,16,128,1);
  wr32(wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+4*TEGU_PIPELINE_DISPATCH2_DESC_BYTES+TEGU_PIPELINE_D2_VIEW_FORMAT_VK,37);
  check(!dispatch(pipe,wire,out),"dummy sampler dispatch");
  uint64_t va;unsigned char *m=cmd_memory(&va);
  check(rd64(m+768+40)==257*32,"dummy sampler table2 count");
  check((rd32(m+9024+256*32)&15u)==1,"dummy sampler packed");
 }
 /* --- Refusals: each returns an error and does not submit. --- */
 {
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,1);uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,1,0);desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  texture_desc(wire,1,3,TEGU_PIPELINE2_KIND_TEXTURE,0,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,0,64*16,80,16,16,64,1);
  wr32(wire+8,2);submits=0;
  check(dispatch(pipe,wire,out)&&submits==0,"extra wire descriptor refused");
 }
 {
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,0);uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,1,0);desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  /* package has UBO/SSBO/texture/sampler; supply only UBO */
  submits=0;check(dispatch(pipe,wire,out)&&submits==0,"missing manifest resources refused");
 }
 {
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,0);uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,4,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,0,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,0,64*16,80,16,16,64,2);
  sampler_desc(wire,3,6,0);
  submits=0;check(dispatch(pipe,wire,out)&&submits==0,"mip level>0 refused");
 }
 {
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,1);uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,4,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,0,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,0,64*16,80,16,16,64,1);
  sampler_desc(wire,3,6,0);
  /* replace sampler with image at the same binding: manifest image is binding4 */
  texture_desc(wire,3,3,TEGU_PIPELINE2_KIND_IMAGE,0,TEGU_PIPELINE2_ACCESS_STORAGE|TEGU_PIPELINE2_ACCESS_WRITE,
   C.img,0,128*16,70,16,16,128,1);
  submits=0;check(dispatch(pipe,wire,out)&&submits==0,"image at wrong native binding refused");
 }
 {
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,1);uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,4,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,0,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,0,64*16,80,16,16,64,1);
  texture_desc(wire,3,4,TEGU_PIPELINE2_KIND_IMAGE,0,TEGU_PIPELINE2_ACCESS_STORAGE|TEGU_PIPELINE2_ACCESS_WRITE,
   C.img,0,128*16,999,16,16,128,1);
  submits=0;check(dispatch(pipe,wire,out)&&submits==0,"unsupported image format refused");
 }
 {
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,0);
  /* sampler6 is manifest record index3 in this package; set before import */
  mflags(pkg,3,TEGU_PIPELINE2_FLAG_SAMPLER_COMPARE);
  uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,4,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,0,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,0,64*16,80,16,16,64,1);
  sampler_desc(wire,3,6,0); /* wire compare flag 0, manifest requires compare */
  submits=0;check(dispatch(pipe,wire,out)&&submits==0,"manifest sampler compare mismatch refused");
 }
 {
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,1);uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,4,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,0,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,0,64*16,80,16,16,64,1);
  texture_desc(wire,3,4,TEGU_PIPELINE2_KIND_IMAGE,0,TEGU_PIPELINE2_ACCESS_STORAGE|TEGU_PIPELINE2_ACCESS_WRITE,
   C.img,0,128*16,70,16,16,128,1);
  wr32(wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+3*TEGU_PIPELINE_DISPATCH2_DESC_BYTES+TEGU_PIPELINE_D2_VIEW_FORMAT_VK,38);
  submits=0;check(dispatch(pipe,wire,out)&&submits==0,"manifest view-format mismatch refused");
 }
 {
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,0);uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,4,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,0,TEGU_PIPELINE2_ACCESS_READ, /* no SAMPLE */
   C.tex,0,64*16,80,16,16,64,1);
  sampler_desc(wire,3,6,0);
  submits=0;check(dispatch(pipe,wire,out)&&submits==0,"missing sample access refused");
 }
 {
  size_t bytes=tgp2(pkg,sizeof(pkg),0,0,0);uint64_t pipe=import_package(pkg,bytes);
  wire_header(wire,4,0);
  desc(wire,0,0,TEGU_PIPELINE2_KIND_UBO,0,TEGU_PIPELINE2_ACCESS_READ,C.ubo,0,64);
  desc(wire,1,1,TEGU_PIPELINE2_KIND_SSBO,1,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE,C.ssbo,0,64);
  texture_desc(wire,2,3,TEGU_PIPELINE2_KIND_TEXTURE,0,TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,
   C.tex,0,64*16,80,16,16,64,1);
  sampler_desc(wire,3,6,0);wr32(wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+3*TEGU_PIPELINE_DISPATCH2_DESC_BYTES+TEGU_PIPELINE_D2_REDUCTION,1);
  submits=0;check(dispatch(pipe,wire,out)&&submits==0,"unsupported sampler reduction refused");
 }
 printf("PASS compute v2 bridge: TGP1 regression, TGP2 manifest/tables, dummy sampler, BO/write masks, refusals\n");
 n2mb_close(C.dev);
 return 0;
}
