#include "generic_job_ui.h"
#include "compositor_v7_scratch_layout.h"
#include "fullscreen_map.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t get64(const uint8_t *p){return (uint64_t)get32(p)|(uint64_t)get32(p+4)<<32;}
static uint64_t reg(const struct tegu_generic_job_request *r,unsigned n){
 uint64_t v=0;for(unsigned k=0;k<r->command_count;k++){uint64_t c=r->commands[k];
  if((c>>56)==1&&((c>>48)&255)==n)v=c&((1ull<<48)-1);
  if((c>>56)==2&&((c>>48)&255)==n)v=(uint32_t)c;}
 return v;}

/* Reconstructed lock-screen serial 5: five UBOs, the constant blob, the
 * framebuffer-fetch input, a sampled texture and a constexpr-sampled noise
 * texture, with a 6.5 KiB fragment shader using 64 registers. */
static void fixture(struct tegu_generic_draw_shader *sh,struct tegu_generic_draw_request *q){
 static uint8_t vc[4864],fc[6528];memset(vc,0x31,sizeof(vc));memset(fc,0x72,sizeof(fc));
 memset(sh,0,sizeof(*sh));sh->vertex_code=vc;sh->fragment_code=fc;sh->vertex_bytes=sizeof(vc);sh->fragment_bytes=sizeof(fc);
 sh->vertex_registers=32;sh->fragment_registers=64;sh->secondary_registers=32;sh->vertex_fau_words=34;sh->fragment_fau_words=52;
 sh->position_offset=0;sh->secondary_offset=2432;sh->varying_bytes=24;
 sh->vertex_preload=sh->secondary_preload=UINT64_C(0x3800000000000000);sh->fragment_preload=UINT64_C(0x3000000000000000);
 sh->attribute_count=4;const uint32_t fmt[4]={109,103,103,97},off[4]={0,16,24,32};
 for(unsigned i=0;i<4;i++)sh->attributes[i]=(struct tegu_vertex_attribute){i,1,fmt[i],off[i],48,0,1};
 sh->blend=(struct tegu_blend_inputs){1,1,0,1,5,0,1,5,15};
 memset(q,0,sizeof(*q));q->width=1080;q->height=2424;q->row_bytes=4352;q->load_action=1;q->vertex_count=6;
 q->scissor[2]=1080;q->scissor[3]=2424;q->target_token=1;q->target_offset=0;q->target_length=UINT64_C(4352)*2424;
 q->vertex=(struct tegu_generic_draw_memory){2,0x100,288};
 /* Four UBO windows share one allocation; one is separate. */
 const uint64_t boff[5]={0x40,0x100,0x1000,0x1200,0x80};const uint64_t blen[5]={64,32,176,16,16};const uint64_t btok[5]={17,17,17,17,18};
 for(unsigned i=0;i<5;i++)q->bindings[i]=(struct tegu_generic_draw_binding){i,TEGU_GENERIC_DRAW_BUFFER,{btok[i],boff[i],blen[i]},0,0,0,0,0,0};
 q->bindings[5]=(struct tegu_generic_draw_binding){5,TEGU_GENERIC_DRAW_CONSTANT,{0,0,0},0,0,0,0,0,0};
 q->bindings[6]=(struct tegu_generic_draw_binding){6,TEGU_GENERIC_DRAW_ATTACHMENT,{0,0,0},0,0,0,0,0,0};
 q->bindings[7]=(struct tegu_generic_draw_binding){7,TEGU_GENERIC_DRAW_COMBINED,{30,0,UINT64_C(832)*237},194,237,832,
  TEGU_GENERIC_DRAW_SAMPLER_WORD(1,1,0,0,0),80,0};
 q->bindings[8]=(struct tegu_generic_draw_binding){8,TEGU_GENERIC_DRAW_COMBINED_IMMUTABLE,{31,0,128*32},32,32,128,
  TEGU_GENERIC_DRAW_SAMPLER_WORD(0,0,8,8,8),80,0};
 q->binding_count=9;q->vertex_push_bytes=8;q->fragment_push_bytes=80;
 q->vertex_push[0]=0x40;q->fragment_push[76]=0x22;q->fragment_push[77]=0x22;
 const uint8_t constant[8]={0,126,0,126,0,126,0,126};memcpy(q->constant,constant,8);q->constant_bytes=8;
}

/* Register file at each RUN_IDVS of a stream; counts RUN_FRAGMENT and
 * FINISH_TILING. */
static unsigned replay(const struct tegu_generic_job_request *r,uint32_t snap[][96],unsigned max,unsigned *fragments,unsigned *tilings){
 uint32_t R[96]={0};unsigned k=0;*fragments=*tilings=0;
 for(unsigned i=0;i<r->command_count;i++){uint64_t c=r->commands[i];unsigned op=(unsigned)(c>>56),n=(unsigned)((c>>48)&255);
  if(op==1&&n<95){R[n]=(uint32_t)c;R[n+1]=(uint32_t)((c>>32)&0xffff);}
  if(op==2&&n<96)R[n]=(uint32_t)c;
  if(op==6&&k<max)memcpy(snap[k++],R,sizeof(R));
  if(op==7)(*fragments)++;
  if(op==9)(*tilings)++;}
 return k;}
static unsigned replay2(const struct tegu_generic_job2_request *r,uint32_t snap[][96],unsigned max,unsigned *fragments){
 uint32_t R[96]={0};unsigned k=0;*fragments=0;
 for(unsigned i=0;i<r->command_count;i++){uint64_t c=r->commands[i];unsigned op=(unsigned)(c>>56),n=(unsigned)((c>>48)&255);
  if(op==1&&n<95){R[n]=(uint32_t)c;R[n+1]=(uint32_t)((c>>32)&0xffff);}
  if(op==2&&n<96)R[n]=(uint32_t)c;
  if(op==6){if(k<max)memcpy(snap[k],R,sizeof(R));k++;}
  if(op==7)(*fragments)++;}
 return k;}
static uint32_t gd_spd_word(uint32_t registers){return ((registers<=32u?2u:0u)<<30)|0x128u;}
static uint64_t r64(const uint32_t *R,unsigned n){return (uint64_t)R[n]|(uint64_t)R[n+1]<<32;}

int main(void){
 uint8_t *scratch=calloc(1,TEGU_GENERIC_JOB_SCRATCH_BYTES),*load=calloc(1,TEGU_GENERIC_JOB_LOAD_BYTES);assert(scratch&&load);
 struct tegu_generic_job_images im={900,901,scratch,load,0};
 struct tegu_generic_draw_shader sh;struct tegu_generic_draw_request q;fixture(&sh,&q);
 struct tegu_generic_job_request job;struct tegu_generic_job_plan plan;
 assert(!tegu_generic_draw_job_build(&sh,&q,&im,&job)&&!tegu_generic_job_plan(&job,&plan));
 /* vertex, the shared UBO allocation, the separate UBO, two textures, LOAD. */
 assert(job.span_count==6&&job.spans[1].token==17&&job.spans[1].offset==0&&job.spans[1].length==0x1210&&
  job.spans[5].token==901&&job.exec_mask==TEGU_V7_LOAD_EXEC_MASK);
 for(unsigned k=0;k<job.command_count;k++)assert(!tegu_generic_job_command_allowed(job.commands[k]));
 /* R170: a LOAD job writes back only the tiles it draws. The preload runs
  * in INTERSECT mode, so the RT must not write clean tiles (word 1 bit 31),
  * or every untouched tile is stored as the clear value 0. */
 assert((get32(scratch)&7)==2&&!(get32(scratch+TEGU_GENERIC_JOB_RT_OFFSET+4)>>31));
 /* Code: both stages copied whole, FS spanning several pages. */
 assert(!memcmp(scratch+TEGU_V7_VS_OFFSET,sh.vertex_code,sh.vertex_bytes)&&
  !memcmp(scratch+TEGU_V7_FS_OFFSET,sh.fragment_code,sh.fragment_bytes));
 /* SRT: three tables. */
 assert(reg(&job,0)==((FS_APERTURE_VA+0x7000)|4u));
 assert(get64(scratch+0x7000)==((FS_APERTURE_VA+0x6000)|(1ull<<56))&&get64(scratch+0x7010)==((FS_APERTURE_VA+0x7100)|(1ull<<56))&&
  get64(scratch+0x7020)==((FS_APERTURE_VA+0x7300)|(1ull<<56))&&get64(scratch+0x7028)==512);
 /* Table 0: four attributes, buffer at 17. */
 for(unsigned i=0;i<4;i++)assert((get32(scratch+0x6000+i*32)&15)==5&&get32(scratch+0x6000+i*32+12)==17);
 assert(get32(scratch+0x6000+17*32)==0x19&&get64(scratch+0x6000+17*32+8)==plan.span_va[0]+0x100);
 /* Buffers: allocation VA + window offset; sizes are the windows. */
 const uint64_t boff[5]={0x40,0x100,0x1000,0x1200,0x80};const uint32_t blen[5]={64,32,176,16,16};
 for(unsigned i=0;i<5;i++){const uint8_t *e=scratch+0x7100+i*32;unsigned span=i<4?1:2;
  assert(get32(e)==25&&get32(e+4)==blen[i]&&get64(e+8)==plan.span_va[span]+boff[i]);}
 /* Constant blob padded to 16 bytes in scratch. */
 assert(get32(scratch+0x7100+5*32+4)==16&&get64(scratch+0x7100+5*32+8)==FS_APERTURE_VA+0x7700&&scratch[0x7701]==126);
 /* Framebuffer fetch: the render target as a texture, with a sampler. */
 const uint8_t *fb=scratch+0x7100+6*32,*fbplane=scratch+0x7500+6*32;
 assert((get32(fb)&15)==2&&get32(fb+4)==(1079u|(2423u<<16))&&get64(fb+16)==FS_APERTURE_VA+0x7500+6*32);
 assert(get64(fbplane+8)==plan.target_va&&get32(fbplane+16)==4352&&get32(scratch+0x7300+6*32)!=0);
 /* Sampled texture with a linear runtime sampler; noise with repeat/nearest. */
 assert(get64(scratch+0x7500+7*32+8)==plan.span_va[3]&&get32(scratch+0x7500+7*32+16)==832);
 uint32_t s7=get32(scratch+0x7300+7*32),s8=get32(scratch+0x7300+8*32);
 assert(!(s7&(3u<<27))&&((s7>>16)&15)==9&&(s8&(3u<<27))==(3u<<27)&&((s8>>8)&15)==8&&((s8>>12)&15)==8&&((s8>>16)&15)==8);
 /* R8 lookup (luma boost): R8_UNORM with R001 order, one byte per texel. */
 {struct tegu_generic_draw_request r8=q;r8.bindings[8].format=10;r8.bindings[8].width=128;r8.bindings[8].height=1;
  r8.bindings[8].row_bytes=128;r8.bindings[8].memory.bytes=128;
  assert(!tegu_generic_draw_job_build(&sh,&r8,&im,&job)&&get32(scratch+0x7100+8*32)==(2u|(2u<<4)|(((163u<<12)|16u)<<10))&&
   get32(scratch+0x7500+8*32+16)==128&&(get32(scratch+0x7500+8*32)>>24)==0);
  r8.bindings[8].format=11;assert(tegu_generic_draw_job_build(&sh,&r8,&im,&job)<0);
  assert(!tegu_generic_draw_job_build(&sh,&q,&im,&job)&&get32(scratch+0x7100+7*32)==(2u|(2u<<4)|(((187u<<12)|4u)<<10)));}
 /* A8 (R8 with 000R swizzle), RG8 and RGBA16F lookups. */
 {struct tegu_generic_draw_request fq=q;struct tegu_generic_draw_binding *t=&fq.bindings[8];
  t->format=1;t->width=128;t->height=1;t->row_bytes=128;t->memory.bytes=128;
  assert(!tegu_generic_draw_job_build(&sh,&fq,&im,&job)&&get32(scratch+0x7100+8*32+8)==0x124u&&(get32(scratch+0x7500+8*32)>>24)==0);
  t->format=30;t->row_bytes=256;t->memory.bytes=256;
  assert(!tegu_generic_draw_job_build(&sh,&fq,&im,&job)&&get32(scratch+0x7100+8*32)==(2u|(2u<<4)|(((171u<<12)|16u)<<10))&&(get32(scratch+0x7500+8*32)>>24)==1);
  t->format=115;t->row_bytes=1024;t->memory.bytes=1024;
  assert(!tegu_generic_draw_job_build(&sh,&fq,&im,&job)&&get32(scratch+0x7100+8*32)==(2u|(2u<<4)|((223u<<12)<<10))&&(get32(scratch+0x7500+8*32)>>24)==3);
  t->row_bytes=512;t->memory.bytes=512;assert(tegu_generic_draw_job_build(&sh,&fq,&im,&job)<0); /* rows too short for 8-byte texels */
  assert(!tegu_generic_draw_job_build(&sh,&q,&im,&job));}
 /* Unbound textures read zero: a 1x1 zero texture with a nearest sampler. */
 {struct tegu_generic_draw_request nq=q;nq.bindings[7]=(struct tegu_generic_draw_binding){7,TEGU_GENERIC_DRAW_NEUTRAL_IMAGE,{0,0,0},0,0,0,0,0,0};
  assert(!tegu_generic_draw_job_build(&sh,&nq,&im,&job)&&get64(scratch+0x7500+7*32+8)==FS_APERTURE_VA+0x7900&&
   get32(scratch+0x7300+7*32)!=0&&!scratch[0x7900]&&!scratch[0x79ff]);
  nq.bindings[3]=(struct tegu_generic_draw_binding){3,TEGU_GENERIC_DRAW_NEUTRAL_BUFFER,{0,0,0},0,0,0,0,0,0};
  assert(!tegu_generic_draw_job_build(&sh,&nq,&im,&job)&&get64(scratch+0x7100+3*32+8)==FS_APERTURE_VA+0x7900);
  assert(!tegu_generic_draw_job_build(&sh,&q,&im,&job));}
 /* Unused entries stay null. */
 assert(!get32(scratch+0x7100+9*32)&&!get32(scratch+0x7300+0*32));
 /* Push images at FAU word 32; FAU sizes from the shader. */
 assert(scratch[0xa80]==0x40&&scratch[0xb80+76]==0x22&&reg(&job,9)>>24==17&&reg(&job,13)>>24==26);
 /* Register allocation per stage: VS 32, FS 64. */
 assert(get32(scratch+0x600)==((2u<<30)|0x38u)&&get32(scratch+0x480)==0x128u&&get32(scratch+0x700)==((2u<<30)|0x38u));
 assert(reg(&job,33)==6&&reg(&job,48)==24);
 /* CLEAR pass: no LOAD span, clear exec mask. */
 struct tegu_generic_draw_request c=q;c.load_action=2;c.clear_color=0xff000000u;
 assert(!tegu_generic_draw_job_build(&sh,&c,&im,&job)&&job.span_count==5&&job.exec_mask==TEGU_V7_CLEAR_EXEC_MASK);
 /* A CLEAR job writes every tile, drawn or not. */
 assert(get32(scratch+TEGU_GENERIC_JOB_RT_OFFSET+4)>>31);
 /* Refusals leave *out untouched. */
 struct tegu_generic_job_request keep;memset(&keep,0xa5,sizeof(keep));
 struct tegu_generic_draw_request bad=q;bad.bindings[1].binding=0;job=keep;
 assert(tegu_generic_draw_job_build(&sh,&bad,&im,&job)<0&&!memcmp(&job,&keep,sizeof(job)));
 bad=q;bad.bindings[7].row_bytes=776;bad.bindings[7].memory.bytes=UINT64_C(776)*237;assert(tegu_generic_draw_job_build(&sh,&bad,&im,&job)<0);
 bad=q;bad.bindings[8].sampler=TEGU_GENERIC_DRAW_SAMPLER_WORD(0,0,3,0,0);assert(tegu_generic_draw_job_build(&sh,&bad,&im,&job)<0);
 bad=q;bad.fragment_push_bytes=129;assert(tegu_generic_draw_job_build(&sh,&bad,&im,&job)<0);
 /* R117: a fragment stage reading no push constants has 14 FAU words. */
 {struct tegu_generic_draw_shader few=sh;few.fragment_fau_words=14;struct tegu_generic_draw_request fq=q;fq.fragment_push_bytes=0;
  assert(!tegu_generic_draw_job_build(&few,&fq,&im,&job)&&reg(&job,13)>>24==7);}
 bad=q;bad.vertex_count=7;assert(tegu_generic_draw_job_build(&sh,&bad,&im,&job)<0);
 /* Line lists: pairs of vertices, draw mode Lines in RUN_IDVS. */
 {struct tegu_generic_draw_request lines=q;lines.primitive=TEGU_GENERIC_DRAW_LINES;lines.vertex_count=4;
  assert(tegu_generic_draw_job_build(&sh,&lines,&im,&job)==0);
  lines.vertex_count=5;assert(tegu_generic_draw_job_build(&sh,&lines,&im,&job)<0);
  lines.vertex_count=6;lines.primitive=3;assert(tegu_generic_draw_job_build(&sh,&lines,&im,&job)<0);
  bad=q;bad.vertex_count=TEGU_GENERIC_DRAW_MAX_VERTICES+3;assert(tegu_generic_draw_job_build(&sh,&bad,&im,&job)<0);}
 /* Triangle strips: any count from 3, draw mode 10. */
 {struct tegu_generic_draw_request strip=q;strip.primitive=TEGU_GENERIC_DRAW_TRIANGLE_STRIP;strip.vertex_count=4;
  assert(tegu_generic_draw_job_build(&sh,&strip,&im,&job)==0);
  strip.vertex_count=2;assert(tegu_generic_draw_job_build(&sh,&strip,&im,&job)<0);}
 /* Vertex-pulling shaders: no attributes and no fetched vertex span. */
 {struct tegu_generic_draw_shader pull=sh;pull.attribute_count=0;memset(pull.attributes,0,sizeof(pull.attributes));
  struct tegu_generic_draw_request none=q;none.vertex=(struct tegu_generic_draw_memory){0,0,0};
  assert(tegu_generic_draw_job_build(&pull,&none,&im,&job)==0);
  assert(tegu_generic_draw_job_build(&pull,&q,&im,&job)<0);
  /* Native 16-bit indices for the vertex-pulling shader. */
  struct tegu_generic_draw_request indexed=none;indexed.primitive=TEGU_GENERIC_DRAW_TRIANGLE_STRIP;indexed.vertex_count=4;
  indexed.index=(struct tegu_generic_draw_memory){q.vertex.token,0,8};indexed.index_size=2;
  assert(tegu_generic_draw_job_build(&pull,&indexed,&im,&job)==0);
  indexed.index_size=3;assert(tegu_generic_draw_job_build(&pull,&indexed,&im,&job)<0);
  indexed.index_size=2;indexed.index.bytes=6;assert(tegu_generic_draw_job_build(&pull,&indexed,&im,&job)<0);
  indexed.index.bytes=8;assert(tegu_generic_draw_job_build(&sh,&indexed,&im,&job)<0);}
 /* ASTC 8x8 sRGB texture binding: rows of 16-byte blocks, ASTC 2D plane. */
 {struct tegu_generic_draw_request astc=q;int found=0;
  for(uint32_t k=0;k<astc.binding_count;k++)if(astc.bindings[k].kind==TEGU_GENERIC_DRAW_COMBINED&&!found) {
   struct tegu_generic_draw_binding *b=&astc.bindings[k];found=1;
   b->width=60;b->height=60;b->format=194;b->row_bytes=128;b->memory.bytes=128u*8u;
  }
  if(found) {
   assert(tegu_generic_draw_job_build(&sh,&astc,&im,&job)==0);
   for(uint32_t k=0;k<astc.binding_count;k++)if(astc.bindings[k].format==194)astc.bindings[k].memory.bytes=128u*60u;
   assert(tegu_generic_draw_job_build(&sh,&astc,&im,&job)<0);
  }}
 struct tegu_generic_draw_shader big=sh;big.fragment_bytes=7*4096+128;assert(tegu_generic_draw_job_build(&big,&q,&im,&job)<0);
 big=sh;big.attributes[1].buffer_index=2;assert(tegu_generic_draw_job_build(&big,&q,&im,&job)<0);
 /* Nine distinct allocations exceed the kernel's eight spans. */
 bad=q;for(unsigned i=0;i<5;i++)bad.bindings[i].memory.token=40+i;assert(tegu_generic_draw_job_build(&sh,&bad,&im,&job)<0);
 struct tegu_generic_job_images noload={900,0,scratch,0,0};assert(tegu_generic_draw_job_build(&sh,&q,&noload,&job)<0);
 /* Batched pass: draw 0 (LOAD, framebuffer fetch) and three later draws,
  * one with another pipeline, in ONE job: four RUN_IDVS, one tiler finish,
  * one fragment pass.  Each draw keeps its own tables, programs and FAU. */
 {static uint8_t vc2[1152],fc2[1408];memset(vc2,0x44,sizeof(vc2));memset(fc2,0x55,sizeof(fc2));
  struct tegu_generic_draw_shader other=sh;other.vertex_code=vc2;other.vertex_bytes=sizeof(vc2);other.secondary_offset=640;
  other.fragment_code=fc2;other.fragment_bytes=sizeof(fc2);other.fragment_registers=32;other.varying_bytes=16;
  struct tegu_generic_draw_request d[4];const struct tegu_generic_draw_request *dq[4];const struct tegu_generic_draw_shader *ds[4];
  for(unsigned k=0;k<4;k++){d[k]=q;dq[k]=&d[k];ds[k]=&sh;
   if(k){d[k].bindings[6]=(struct tegu_generic_draw_binding){6,TEGU_GENERIC_DRAW_NEUTRAL_IMAGE,{0,0,0},0,0,0,0,0,0};
    d[k].scissor[0]=10*k;d[k].scissor[1]=20*k;d[k].scissor[2]=100;d[k].scissor[3]=50;d[k].vertex.offset=0x100+288*k;}}
  d[2].fragment_push[76]=0x33;d[3].vertex_count=12;d[3].vertex.bytes=576;ds[2]=&other;
  memset(&job,0,sizeof(job));
  assert(tegu_generic_draw_batch_build(ds,dq,4,&im,&job)==4&&!tegu_generic_job_plan(&job,&plan));
  uint32_t snap[8][96];unsigned fragments,tilings;
  assert(replay(&job,snap,8,&fragments,&tilings)==4&&fragments==1&&tilings==1);
  assert(job.command_count<=125&&job.command_count<4*58&&job.exec_mask==TEGU_V7_LOAD_EXEC_MASK);
  for(unsigned k=0;k<job.command_count;k++)assert(!tegu_generic_job_command_allowed(job.commands[k]));
  /* Spans merge across draws: vertex, shared UBOs, UBO, two textures, LOAD. */
  assert(job.span_count==6&&job.spans[0].token==2&&job.spans[0].length==0x100+288*3+576&&job.spans[5].token==901);
  /* LOAD job: no clean tile writes. */
  assert((get32(scratch)&7)==2&&!(get32(scratch+TEGU_GENERIC_JOB_RT_OFFSET+4)>>31));
  for(unsigned k=0;k<4;k++){const uint32_t *R=snap[k];const struct tegu_generic_draw_shader *s=ds[k];
   uint64_t srt=r64(R,0)&~(uint64_t)63,block=srt-FS_APERTURE_VA;
   assert((r64(R,0)&63)==4&&r64(R,4)==r64(R,0)&&block==(k<16?0x20000u+k*0x1000u:0));
   assert(get64(scratch+block)==((FS_APERTURE_VA+block+0x400)|(1ull<<56)));
   /* Table 0 names this draw's vertex window. */
   assert(get64(scratch+block+0x400+17*32+8)==plan.span_va[0]+d[k].vertex.offset);
   /* UBO 4 in table 1; binding 6: the target (draw 0) or the zero texture. */
   assert(get64(scratch+block+0x700+4*32+8)==plan.span_va[2]+0x80);
   assert(get64(scratch+block+0xb00+6*32+8)==(k?FS_APERTURE_VA+block+0xf00:plan.target_va));
   /* Programs point at a copy of this draw's code. */
   uint64_t pos=get64(scratch+(r64(R,16)-FS_APERTURE_VA)+8),var=get64(scratch+(r64(R,18)-FS_APERTURE_VA)+8),
    frag=get64(scratch+(r64(R,20)-FS_APERTURE_VA)+8);
   assert(!memcmp(scratch+(pos-FS_APERTURE_VA-s->position_offset),s->vertex_code,s->vertex_bytes)&&
    var-pos==s->secondary_offset-s->position_offset&&!memcmp(scratch+(frag-FS_APERTURE_VA),s->fragment_code,s->fragment_bytes));
   assert(get32(scratch+(r64(R,20)-FS_APERTURE_VA))==gd_spd_word(s->fragment_registers));
   /* FAU images with this draw's push constants and word counts. */
   uint64_t vf=r64(R,8),ff=r64(R,12);
   assert(vf>>56==17&&ff>>56==26&&scratch[(vf&((1ull<<48)-1))-FS_APERTURE_VA+0x80]==0x40&&
    scratch[(ff&((1ull<<48)-1))-FS_APERTURE_VA+0x80+76]==(k==2?0x33:0x22));
   assert(R[33]==d[k].vertex_count&&R[48]==s->varying_bytes&&R[42]==((d[k].scissor[1]<<16)|d[k].scissor[0])&&
    R[43]==(((d[k].scissor[1]+d[k].scissor[3]-1)<<16)|(d[k].scissor[0]+d[k].scissor[2]-1)));}
  /* Identical descriptors are shared: draws 1 and 3 reuse draw 0's programs and vertex FAU. */
  assert(r64(snap[1],16)==r64(snap[0],16)&&r64(snap[3],20)==r64(snap[0],20)&&r64(snap[3],8)==r64(snap[0],8)&&r64(snap[2],20)!=r64(snap[0],20));
  /* Both pipelines' code fits the executable regions. */
  assert(!memcmp(scratch+TEGU_V7_VS_OFFSET,sh.vertex_code,sh.vertex_bytes));
  /* A later draw that reads the target, CLEARs, or draws elsewhere ends the batch. */
  struct tegu_generic_job_request keep;memset(&keep,0xa5,sizeof(keep));job=keep;
  d[1]=q;assert(tegu_generic_draw_batch_build(ds,dq,4,&im,&job)==0&&!memcmp(&job,&keep,sizeof(job)));
  d[1]=d[3];d[2].load_action=2;assert(tegu_generic_draw_batch_build(ds,dq,4,&im,&job)==2);
  d[2].load_action=1;d[2].target_offset=4096;assert(tegu_generic_draw_batch_build(ds,dq,4,&im,&job)==2);
  d[2].target_offset=0;
  /* Resource sharing keeps resolved table bytes, draw order and distinct
   * vertex bindings. Identical requests share one SRT; a changed buffer
   * offset must retain a distinct table0 even if other tables are shared. */
  {
   struct tegu_generic_draw_request rq[3];const struct tegu_generic_draw_request *rp[3];
   const struct tegu_generic_draw_shader *rs[3]={&sh,&sh,&sh};
   for(unsigned k=0;k<3;k++){rq[k]=d[3];rq[k].binding_count=1;rp[k]=&rq[k];}
   rq[2].vertex.offset+=144;rq[2].bindings[0].memory.offset+=16;
   uint8_t *before=malloc(TEGU_GENERIC_JOB_SCRATCH_BYTES);assert(before);
   uint32_t old[3][96],now[3][96];
   assert(!tegu_generic_resource_share());
   assert(tegu_generic_draw_batch_build(rs,rp,3,&im,&job)==3);
   unsigned commands=job.command_count;
   assert(replay(&job,old,3,&fragments,&tilings)==3);
   memcpy(before,scratch,TEGU_GENERIC_JOB_SCRATCH_BYTES);
   tegu_generic_set_resource_share(1);
   assert(tegu_generic_draw_batch_build(rs,rp,3,&im,&job)==3&&job.command_count<commands);
   assert(replay(&job,now,3,&fragments,&tilings)==3&&fragments==1&&tilings==1);
   assert(now[0][0]==now[1][0]&&now[1][0]!=now[2][0]);
   for(unsigned k=0;k<3;k++) {
    uint64_t a=((uint64_t)old[k][1]<<32|old[k][0])&~UINT64_C(7);
    uint64_t b=((uint64_t)now[k][1]<<32|now[k][0])&~UINT64_C(7);
    for(unsigned t=0;t<3;t++) {
     const uint8_t *oa=before+a-FS_APERTURE_VA+t*16,*ob=scratch+b-FS_APERTURE_VA+t*16;
     uint64_t av=get64(oa)&UINT64_C(0x00ffffffffffffff),bv=get64(ob)&UINT64_C(0x00ffffffffffffff);
     assert(get64(oa+8)==get64(ob+8));
     assert(!memcmp(before+av-FS_APERTURE_VA,scratch+bv-FS_APERTURE_VA,(size_t)get64(oa+8)));
    }
   }
   tegu_generic_set_resource_share(0);free(before);
  }
  /* A CLEAR pass batches too: clear exec mask, clean tiles written. */
  d[0]=d[3];d[0].load_action=2;d[0].clear_color=0xff000000u;
  assert(tegu_generic_draw_batch_build(ds,dq,4,&im,&job)==4&&job.exec_mask==TEGU_V7_CLEAR_EXEC_MASK&&job.span_count==5&&
   (get32(scratch+TEGU_GENERIC_JOB_RT_OFFSET+4)>>31)&&get32(scratch+176)==0xff000000u);
  /* Thirty-two draws: the 128-command submit limit caps the batch. */
  struct tegu_generic_draw_request many[32];const struct tegu_generic_draw_request *mq[32];const struct tegu_generic_draw_shader *ms[32];
  for(unsigned k=0;k<32;k++){many[k]=d[3];many[k].vertex_count=3;many[k].vertex.offset=0x100+144*k;many[k].fragment_push[0]=(uint8_t)k;mq[k]=&many[k];ms[k]=&sh;}
  int got=tegu_generic_draw_batch_build(ms,mq,32,&im,&job);
  assert(got>8&&got<32&&job.command_count<=TEGU_GENERIC_JOB_SUBMIT_COMMANDS&&replay(&job,snap,8,&fragments,&tilings)==8&&fragments==1);
  {uint32_t all[32][96];assert(replay(&job,all,32,&fragments,&tilings)==(unsigned)got);}
  printf("batch: %d of 32 same-pipeline draws in %u commands\n",got,job.command_count);
  /* Distinct allocations per draw: eight spans cap it. */
  for(unsigned k=0;k<32;k++)many[k].vertex.token=100+k;
  got=tegu_generic_draw_batch_build(ms,mq,32,&im,&job);
  assert(got==3&&job.span_count==8);
  /* Shaders that do not share code: the 28 KiB region holds four 6.4 KiB
   * fragment stages (the VS region keeps 3.3 KiB after the shared VS). */
  static uint8_t codes[8][6528];const struct tegu_generic_draw_shader *bs[8];struct tegu_generic_draw_shader bigs[8];
  for(unsigned k=0;k<8;k++){bigs[k]=sh;memset(codes[k],k,sizeof(codes[k]));bigs[k].fragment_code=codes[k];bs[k]=&bigs[k];many[k].vertex.token=2;}
  got=tegu_generic_draw_batch_build(bs,mq,8,&im,&job);
  assert(got==4);
  /* Byte-identical code in distinct pipeline objects (QuartzCore variants
   * sharing a vertex shader): only shared by pointer without the gate; with
   * tegu_generic_set_code_share the batch takes all eight draws and its
   * request and scratch equal the batch of one shared code pointer. */
  {static uint8_t same[8][6528];struct tegu_generic_draw_shader copies[8],one=sh;const struct tegu_generic_draw_shader *cs[8],*os[8];
   for(unsigned k=0;k<8;k++){memset(same[k],0x5c,sizeof(same[k]));copies[k]=sh;copies[k].fragment_code=same[k];cs[k]=&copies[k];os[k]=&one;}
   one.fragment_code=same[0];
   static struct tegu_generic_job_request ref,shared;static uint8_t refScratch[TEGU_GENERIC_JOB_SCRATCH_BYTES];
   assert(!tegu_generic_code_share()&&tegu_generic_draw_batch_build(cs,mq,8,&im,&job)==4);
   assert(tegu_generic_draw_batch_build(os,mq,8,&im,&ref)==8);memcpy(refScratch,im.scratch,sizeof(refScratch));
   tegu_generic_set_code_share(1);
   assert(tegu_generic_draw_batch_build(cs,mq,8,&im,&shared)==8&&!memcmp(&shared,&ref,sizeof(ref))&&
    !memcmp(im.scratch,refScratch,sizeof(refScratch)));
   /* Different bytes are never shared. */
   assert(tegu_generic_draw_batch_build(bs,mq,8,&im,&job)==4);
   tegu_generic_set_code_share(0);
   printf("code share: 4 -> 8 draws of byte-identical code in distinct pipelines, stream equal to one shared pointer\n");}}
 /* 0204 TGJ2: one job per pass, 32 spans, 400 commands, 64 draws. */
 {struct tegu_generic_job2_request *j2=malloc(sizeof(*j2)),*keep2=malloc(sizeof(*keep2));assert(j2&&keep2);
  static struct tegu_generic_draw_request d2[64];const struct tegu_generic_draw_request *q2[64];const struct tegu_generic_draw_shader *s2[64];
  static uint32_t all[64][96];unsigned fragments,tilings;struct tegu_generic_batch_info info;
  const struct tegu_generic_batch_limits l2={TEGU_GENERIC_JOB2_VERSION,TEGU_GENERIC_JOB2_MAX_COMMANDS,TEGU_GENERIC_BATCH_MAX_DRAWS,
   TEGU_GENERIC_BATCH_HEAP_BUDGET,0};
  struct tegu_generic_draw_request base=q;
  base.bindings[6]=(struct tegu_generic_draw_binding){6,TEGU_GENERIC_DRAW_NEUTRAL_IMAGE,{0,0,0},0,0,0,0,0,0};
  base.vertex_count=3;base.vertex.bytes=144;base.scissor[2]=64;base.scissor[3]=32;
  for(unsigned k=0;k<64;k++){d2[k]=base;d2[k].scissor[0]=(16u*k)%1000u;d2[k].scissor[1]=(32u*k)%2000u;
   d2[k].vertex.offset=0x100+144*k;q2[k]=&d2[k];s2[k]=&sh;}
  /* 64 draws of one pipeline: all in one job, blocks 32-63 in the scratch
   * chunk region (the heap descriptor names the 2 MiB heap instead). */
  int got=tegu_generic_draw_batch_build2(s2,q2,64,&im,&l2,j2,&info);
  struct tegu_generic_job_plan p2;
  printf("tgj2: %d of 64 same-pipeline draws in %u commands, %u spans, heap estimate %llu KiB\n",got,j2->command_count,j2->span_count,
   (unsigned long long)(info.heap_estimate>>10));
  assert(got==64&&j2->version==TEGU_GENERIC_JOB2_VERSION&&!tegu_generic_job2_plan(j2,&p2)&&j2->command_count>TEGU_GENERIC_JOB_MAX_COMMANDS&&
   j2->command_count<=TEGU_GENERIC_JOB2_MAX_COMMANDS&&info.commands==j2->command_count&&replay2(j2,all,64,&fragments)==64&&fragments==1);
  /* info.stop names the limit that ended a batch (TEGU_PERF_BREAK). */
  assert(info.stop==TEGU_BATCH_STOP_NONE);
  for(unsigned k=0;k<j2->command_count;k++)assert(!tegu_generic_job_command_allowed(j2->commands[k]));
  assert(get64(scratch+0x1000+0x88)==p2.heap_va);
  for(unsigned k=0;k<64;k++){uint64_t block=(r64(all[k],0)&~(uint64_t)63)-FS_APERTURE_VA;
   assert(block==(k<16?0x20000u+k*0x1000u:k<32?0x37000u+(k-16)*0x1000u:TEGU_V7_CHUNK_OFFSET+(k-32)*0x1000u));
   assert(get64(scratch+block)==((FS_APERTURE_VA+block+0x400)|(1ull<<56)));
   assert(get64(scratch+block+0x400+17*32+8)==p2.span_va[0]+d2[k].vertex.offset);
   assert(all[k][42]==((d2[k].scissor[1]<<16)|d2[k].scissor[0]));}
  /* The same pass as TGJ1: 160 or 128 commands cap it; the result converts. */
  const struct tegu_generic_batch_limits l160={TEGU_GENERIC_JOB_VERSION,TEGU_GENERIC_JOB_MAX_COMMANDS,TEGU_GENERIC_BATCH_TGJ1_DRAWS,
   TEGU_GENERIC_BATCH_HEAP_BUDGET,0};
  int got160=tegu_generic_draw_batch_build2(s2,q2,64,&im,&l160,j2,&info);
  assert(got160>2&&got160<32&&j2->command_count<=TEGU_GENERIC_JOB_MAX_COMMANDS&&!tegu_generic_job_to_v1(j2,&job)&&
   !tegu_generic_job_plan(&job,&plan)&&replay(&job,all,64,&fragments,&tilings)==(unsigned)got160);
  int got128=tegu_generic_draw_batch_build(s2,q2,64,&im,&job);
  assert(got128>=2&&got128<got160&&job.command_count<=TEGU_GENERIC_JOB_SUBMIT_COMMANDS);
  printf("tgj1: %d draws in 160 commands, %d in 128 (TGJ2: %d)\n",got160,got128,got);
  /* Distinct push constants per draw: the command stream caps the batch
   * below 64 draws and the prefix that fits is taken. */
  for(unsigned k=0;k<64;k++)d2[k].fragment_push[0]=(uint8_t)k;
  got=tegu_generic_draw_batch_build2(s2,q2,64,&im,&l2,j2,&info);
  assert(got>32&&got<64&&j2->command_count<=TEGU_GENERIC_JOB2_MAX_COMMANDS&&replay2(j2,all,64,&fragments)==(unsigned)got);
  assert(info.stop==TEGU_BATCH_STOP_COMMANDS);
  printf("tgj2: %d of 64 draws with distinct push constants in %u commands\n",got,j2->command_count);
  /* Distinct vertex allocations: 32 spans (vertex per draw, the UBO
   * allocations, two textures, LOAD) cap it at 27 draws. */
  for(unsigned k=0;k<64;k++){d2[k].fragment_push[0]=0;d2[k].vertex.token=100+k;d2[k].vertex.offset=0x100;}
  got=tegu_generic_draw_batch_build2(s2,q2,64,&im,&l2,j2,&info);
  assert(got==27&&j2->span_count==TEGU_GENERIC_JOB2_MAX_SPANS&&!tegu_generic_job2_plan(j2,&p2)&&info.stop==TEGU_BATCH_STOP_SPANS);
  /* Tiler heap: full-target scissors and ten triangles per draw.  The batch
   * stops before its estimate passes the budget; the estimate is the frame
   * part plus every draw's part. */
  for(unsigned k=0;k<64;k++){d2[k]=base;d2[k].vertex_count=30;d2[k].vertex.bytes=1440;d2[k].vertex.offset=0x100+1440*k;
   d2[k].scissor[0]=d2[k].scissor[1]=0;d2[k].scissor[2]=q.width;d2[k].scissor[3]=q.height;}
  uint64_t frame=tegu_generic_batch_heap_frame(q.width,q.height),per=tegu_generic_batch_heap_draw(&sh,&d2[0]);
  unsigned expect=(unsigned)((TEGU_GENERIC_BATCH_HEAP_BUDGET-frame)/per);
  assert(frame==144u*34u*76u+64u&&per==16u*(4u*34u*76u+4u*6u)+40u*30u&&expect>=2&&expect<64);
  got=tegu_generic_draw_batch_build2(s2,q2,64,&im,&l2,j2,&info);
  assert(got==(int)expect&&info.heap_estimate==frame+expect*per&&info.heap_estimate<=TEGU_GENERIC_BATCH_HEAP_BUDGET&&
   info.stop==TEGU_BATCH_STOP_HEAP);
  printf("heap: %d full-target draws of 10 triangles per job (estimate %llu KiB)\n",got,(unsigned long long)(info.heap_estimate>>10));
  /* A smaller budget (the frontend's raised estimate scale) splits sooner;
   * below two draws nothing is built and *out stays untouched. */
  struct tegu_generic_batch_limits small=l2;small.heap_budget=frame+3*per;
  assert(tegu_generic_draw_batch_build2(s2,q2,64,&im,&small,j2,&info)==3);
  small.heap_budget=frame+per;memset(keep2,0xa5,sizeof(*keep2));*j2=*keep2;
  assert(tegu_generic_draw_batch_build2(s2,q2,64,&im,&small,j2,&info)==0&&!memcmp(j2,keep2,sizeof(*j2))&&info.stop==TEGU_BATCH_STOP_HEAP);
  /* A draw that samples the target, or a CLEAR draw, does not join. */
  {struct tegu_generic_draw_request reads=d2[1],clears=d2[1];reads.vertex.token=reads.target_token;clears.load_action=2;
   const struct tegu_generic_draw_request *rq[2]={&d2[0],&reads},*cq[2]={&d2[0],&clears};
   assert(tegu_generic_draw_batch_build2(s2,rq,2,&im,&l2,j2,&info)==0&&info.stop==TEGU_BATCH_STOP_READS_TARGET);
   assert(tegu_generic_draw_batch_build2(s2,cq,2,&im,&l2,j2,&info)==0&&info.stop==TEGU_BATCH_STOP_SHAPE);}
  /* A 16x16 target has no 2 MiB heap: its tiler chunk is live, so only
   * descriptor blocks 0-31 exist. */
  for(unsigned k=0;k<64;k++){d2[k]=base;d2[k].width=d2[k].height=16;d2[k].row_bytes=64;d2[k].target_length=64*16;
   d2[k].scissor[0]=d2[k].scissor[1]=0;d2[k].scissor[2]=d2[k].scissor[3]=16;d2[k].vertex.offset=0x100+144*k;}
  got=tegu_generic_draw_batch_build2(s2,q2,64,&im,&l2,j2,&info);
  assert(got==32&&!(j2->flags&TEGU_GENERIC_JOB_HEAP2M)&&!tegu_generic_job2_plan(j2,&p2)&&replay2(j2,all,64,&fragments)==32&&
   info.stop==TEGU_BATCH_STOP_BLOCKS);
  free(j2);free(keep2);}
 /* Opt-in fragment bbox (default off): a LOAD pass's RUN_FRAGMENT covers the
  * union of its scissors; CLEAR passes and the default keep the whole target. */
 {assert(!tegu_generic_fragment_bbox());
  const uint32_t full=(1080u-1)|((2424u-1)<<16);
  struct tegu_generic_draw_request b=q;b.scissor[0]=10;b.scissor[1]=20;b.scissor[2]=30;b.scissor[3]=40;
  assert(!tegu_generic_draw_job_build(&sh,&b,&im,&job)&&reg(&job,42)==0&&reg(&job,43)==full);
  struct tegu_generic_job_request off=job;
  tegu_generic_set_fragment_bbox(1);
  assert(!tegu_generic_draw_job_build(&sh,&b,&im,&job)&&job.command_count==off.command_count&&
   reg(&job,42)==(10u|20u<<16)&&reg(&job,43)==(39u|59u<<16));
  unsigned diff=0;for(unsigned k=0;k<job.command_count;k++)diff+=job.commands[k]!=off.commands[k];
  assert(diff==2); /* only the tail's two bbox moves */
  b.scissor[0]=0;b.scissor[1]=0;assert(!tegu_generic_draw_job_build(&sh,&b,&im,&job)&&reg(&job,42)==0&&reg(&job,43)==(29u|39u<<16));
  b.load_action=2;assert(!tegu_generic_draw_job_build(&sh,&b,&im,&job)&&reg(&job,42)==0&&reg(&job,43)==full);
  b.load_action=0;assert(!tegu_generic_draw_job_build(&sh,&b,&im,&job)&&reg(&job,42)==0&&reg(&job,43)==full);
  struct tegu_generic_draw_request d[3];const struct tegu_generic_draw_request *dq[3];const struct tegu_generic_draw_shader *ds[3];
  const uint32_t sc[3][4]={{10,20,100,50},{200,300,16,16},{5,1000,10,10}};
  for(unsigned k=0;k<3;k++){d[k]=q;dq[k]=&d[k];ds[k]=&sh;memcpy(d[k].scissor,sc[k],sizeof(sc[k]));d[k].vertex.offset=0x100+288*k;
   if(k)d[k].bindings[6]=(struct tegu_generic_draw_binding){6,TEGU_GENERIC_DRAW_NEUTRAL_IMAGE,{0,0,0},0,0,0,0,0,0};}
  assert(tegu_generic_draw_batch_build(ds,dq,3,&im,&job)==3&&reg(&job,42)==(5u|20u<<16)&&reg(&job,43)==(215u|1009u<<16));
  d[0].load_action=2;assert(tegu_generic_draw_batch_build(ds,dq,3,&im,&job)==3&&reg(&job,42)==0&&reg(&job,43)==full);
  tegu_generic_set_fragment_bbox(0);d[0].load_action=1;
  assert(tegu_generic_draw_batch_build(ds,dq,3,&im,&job)==3&&reg(&job,42)==0&&reg(&job,43)==full);}
 /* GPU floor bench variants: allowlisted opcodes only, the intended edit. */
 {struct tegu_generic_job_request base;assert(!tegu_generic_draw_job_build(&sh,&q,&im,&base));
  unsigned ops[64];
  #define OPS(c,n) do{memset(ops,0,sizeof(ops));for(unsigned k_=0;k_<(n);k_++)ops[(c)[k_]>>56]++;}while(0)
  OPS(base.commands,base.command_count);
  assert(ops[7]==1&&ops[9]==1&&ops[6]==1&&ops[36]==2&&ops[34]==2);
  const uint32_t bb[4]={0,0,15,15};
  for(unsigned v=TEGU_BENCH_AS_BUILT;v<=TEGU_BENCH_FRAG2;v++){
   uint64_t c[TEGU_GENERIC_JOB_MAX_COMMANDS];uint32_t n=base.command_count;memcpy(c,base.commands,n*sizeof(c[0]));
   assert(!tegu_generic_bench_transform(c,&n,TEGU_GENERIC_JOB_SUBMIT_COMMANDS,v,bb));
   for(unsigned k=0;k<n;k++)assert(!tegu_generic_job_command_allowed(c[k]));
   OPS(c,n);
   switch(v){
   case TEGU_BENCH_AS_BUILT:assert(n==base.command_count&&!memcmp(c,base.commands,n*sizeof(c[0])));break;
   case TEGU_BENCH_NOP:assert(n==1&&c[0]==0);break;
   case TEGU_BENCH_FLUSH1:assert(n==3&&ops[36]==1&&c[1]==(36ull<<56|74ull<<40|0x211));break;
   case TEGU_BENCH_FLUSH8:assert(n==24&&ops[36]==8&&ops[3]==8);break;
   case TEGU_BENCH_REQRES:assert(n==2&&c[0]==(34ull<<56|0xe)&&c[1]==34ull<<56);break;
   case TEGU_BENCH_NOFLUSH:assert(n==base.command_count-2&&ops[36]==1);break; /* the LOAD bundle's 0x200 invalidate stays */
   case TEGU_BENCH_BBOX:{struct tegu_generic_job_request t=base;memcpy(t.commands,c,n*sizeof(c[0]));
    assert(n==base.command_count&&reg(&t,42)==0&&reg(&t,43)==(15u|15u<<16));break;}
   case TEGU_BENCH_KEEPRES:assert(n==base.command_count-1&&ops[34]==1&&c[n-1]>>56!=34);break;
   case TEGU_BENCH_NODRAW:assert(n==base.command_count-1&&!ops[6]&&ops[7]==1);break;
   case TEGU_BENCH_NOFRAG:assert(n==base.command_count-2&&!ops[7]&&ops[9]==1);break;
   case TEGU_BENCH_FRAG2:{assert(n==base.command_count+2&&ops[7]==2);
    unsigned f=0;while(c[f]>>56!=7)f++;assert(c[f+2]==c[f]&&c[f+3]==c[f+1]&&c[f+1]>>56==3&&!memcmp(c+f+4,base.commands+f+2,(n-f-4)*sizeof(c[0])));break;}
   }}
  uint64_t c[4]={0,0,0,0};uint32_t n=1;
  assert(tegu_generic_bench_transform(c,&n,4,TEGU_BENCH_BBOX,bb)==-1&&tegu_generic_bench_transform(c,&n,4,TEGU_BENCH_FLUSH8,bb)==-1);
  #undef OPS
 }
 /* Compiler-certified tile fetches can stay in one render pass. Ordinary
  * memory fetches retain their split; malformed tile contracts are refused. */
 {
  struct tegu_generic_draw_shader tile;struct tegu_generic_draw_request td[2];fixture(&tile,&td[0]);
  tile.flags=TEGU_GENERIC_SHADER_TILE_FETCH;td[0].bindings[6].kind=TEGU_GENERIC_DRAW_TILE_ATTACHMENT;
  td[0].load_action=2;td[0].clear_color=0xff102030;td[1]=td[0];td[1].load_action=1;
  const struct tegu_generic_draw_shader *ss[2]={&tile,&tile};
  const struct tegu_generic_draw_request *rr[2]={&td[0],&td[1]};
  struct tegu_generic_job_request built;
  assert(tegu_generic_draw_batch_build(ss,rr,2,&im,&built)==2);
  uint32_t states[2][96];unsigned fragments,tilings;
  assert(replay(&built,states,2,&fragments,&tilings)==2&&fragments==1&&tilings==1);
  for(unsigned k=0;k<2;k++) {
   uint64_t block=(r64(states[k],0)&~(uint64_t)63)-FS_APERTURE_VA;
   assert(states[k][57]==0); /* conservative forward-kill disabled */
   assert(get32(scratch+(r64(states[k],50)&~(uint64_t)15)-FS_APERTURE_VA)&1);
   assert(!get64(scratch+block+0x700+6*32)&&!get64(scratch+block+0x900+6*32));
  }
  struct tegu_generic_job_request preserved;memset(&preserved,0xa5,sizeof(preserved));built=preserved;
  tile.flags=0;
  assert(tegu_generic_draw_batch_build(ss,rr,2,&im,&built)==0&&!memcmp(&built,&preserved,sizeof(built)));
  tile.flags=TEGU_GENERIC_SHADER_TILE_FETCH;td[1].bindings[6].memory.token=td[1].target_token;
  assert(tegu_generic_draw_batch_build(ss,rr,2,&im,&built)==0);
  td[1].bindings[6].memory.token=0;td[1].bindings[6].kind=TEGU_GENERIC_DRAW_ATTACHMENT;
  assert(tegu_generic_draw_batch_build(ss,rr,2,&im,&built)==0);
 }
 /* 32x32 tiles require a matching4KiB color allocation. Test the final
  * serialized FBD for both LOAD/CLEAR and single/batched paths: LOAD copies
  * prebuilt parameters, so changing only the initial frame is insufficient. */
 {
  struct tegu_generic_draw_shader ts;struct tegu_generic_draw_request td[2];fixture(&ts,&td[0]);
  ts.flags=TEGU_GENERIC_SHADER_TILE_FETCH;td[0].bindings[6].kind=TEGU_GENERIC_DRAW_TILE_ATTACHMENT;
  const struct tegu_generic_draw_shader *ss[2]={&ts,&ts};
  const struct tegu_generic_draw_request *rr[2]={&td[0],&td[1]};
  uint8_t *saved=malloc(TEGU_GENERIC_JOB_SCRATCH_BYTES),*saved_load=malloc(TEGU_GENERIC_JOB_LOAD_BYTES);
  assert(saved&&saved_load&&tegu_generic_tile_pixels()==256);
  assert(tegu_generic_set_tile_pixels(512)==-1&&tegu_generic_tile_pixels()==256);
  for(unsigned action=1;action<=2;action++)for(unsigned batch=0;batch<2;batch++) {
   td[0].load_action=action;td[1]=td[0];td[1].load_action=1;
   struct tegu_generic_job_request original,changed;
   assert(batch?tegu_generic_draw_batch_build(ss,rr,2,&im,&original)==2:
    tegu_generic_draw_job_build(&ts,&td[0],&im,&original)==0);
   memcpy(saved,scratch,TEGU_GENERIC_JOB_SCRATCH_BYTES);memcpy(saved_load,load,TEGU_GENERIC_JOB_LOAD_BYTES);
   assert(!tegu_generic_set_tile_pixels(1024));
   assert(batch?tegu_generic_draw_batch_build(ss,rr,2,&im,&changed)==2:
    tegu_generic_draw_job_build(&ts,&td[0],&im,&changed)==0);
   uint32_t old=get32(saved+44),now=get32(scratch+44),mask=(15u<<9)|(255u<<24);
   assert((now&mask)==((10u<<9)|(4u<<24))&&(now&~mask)==(old&~mask));
   assert(!memcmp(&original,&changed,sizeof(original))&&!memcmp(load,saved_load,TEGU_GENERIC_JOB_LOAD_BYTES));
   memcpy(scratch+44,saved+44,4);assert(!memcmp(scratch,saved,TEGU_GENERIC_JOB_SCRATCH_BYTES));
   assert(!tegu_generic_set_tile_pixels(256));
  }
  free(saved);free(saved_load);
 }
 /* Measured small-triangle bound only extends runs the legacy estimate
  * would already split. Unsupported meshes retain exactly the old stream. */
 {
  assert(!tegu_generic_set_tile_pixels(1024));
  struct tegu_generic_draw_shader hs;struct tegu_generic_draw_request hq[13];fixture(&hs,&hq[0]);
  hs.varying_bytes=32;hq[0].binding_count=0;hq[0].width=1125;hq[0].height=2436;hq[0].row_bytes=4608;
  hq[0].target_length=4608u*2436u;hq[0].scissor[0]=hq[0].scissor[1]=0;hq[0].scissor[2]=1125;hq[0].scissor[3]=2436;
  const struct tegu_generic_draw_shader *ss[13];const struct tegu_generic_draw_request *rr[13];
  for(unsigned k=0;k<13;k++){hq[k]=hq[0];ss[k]=&hs;rr[k]=&hq[k];}
  struct tegu_generic_batch_limits lim={TEGU_GENERIC_JOB2_VERSION,TEGU_GENERIC_JOB2_MAX_COMMANDS,64,
   (TEGU_GENERIC_BATCH_HEAP_BUDGET-65536u)*100u/226u,0};
  struct tegu_generic_job2_request old,now;struct tegu_generic_batch_info hi={0};
  assert(tegu_generic_draw_batch_build2(ss,rr,13,&im,&lim,&old,&hi)==4&&!hi.small_heap);
  lim.small_heap_budget=TEGU_GENERIC_SMALL_HEAP_BUDGET;
  assert(tegu_generic_draw_batch_build2(ss,rr,13,&im,&lim,&now,&hi)==12&&hi.small_heap);
  assert(hi.heap_estimate>=1856896u+12u*4096u&&hi.heap_estimate<=lim.small_heap_budget);
  /* R230: a48-vertex mesh followed by four quads is24 primitives.
   * Cover all qualified varying sizes, mixed scissors and budget rejection. */
  for(unsigned varying=16;varying<=32;varying+=8) {
   hs.varying_bytes=varying;
   for(unsigned k=0;k<13;k++){hq[k]=hq[0];hq[k].vertex_count=k?6:48;hq[k].vertex.bytes=hq[k].vertex_count*48u;
    hq[k].scissor[2]=k&1?562:1125;}
   lim.small_heap_budget=0;int legacy=tegu_generic_draw_batch_build2(ss,rr,13,&im,&lim,&old,&hi);
   assert(legacy>=1&&legacy<5);
   lim.small_heap_budget=TEGU_GENERIC_SMALL_HEAP_BUDGET;
   assert(tegu_generic_draw_batch_build2(ss,rr,13,&im,&lim,&now,&hi)==5&&hi.small_heap);
   uint64_t expected=656u*36u*77u+64u+5u*(64u*(16u+varying)+128u+4096u);
   assert(hi.heap_estimate==expected&&hi.stop==TEGU_BATCH_STOP_HEAP);
   /* Tight experimental budget must split before the fifth draw. */
   lim.small_heap_budget=expected-1;
   assert(tegu_generic_draw_batch_build2(ss,rr,13,&im,&lim,&now,&hi)<5);
  }
  /* Wider varyings, larger meshes and indexed draws use the old path. */
  for(unsigned kind=0;kind<3;kind++) {
   hs.varying_bytes=kind==0?64:32;
   if(kind==2)hs.attribute_count=0;
   for(unsigned k=0;k<13;k++) {
    hq[k].vertex_count=kind==1?12:6;hq[k].vertex.bytes=kind==1?576:288;
    if(kind==2)hq[k].vertex=(struct tegu_generic_draw_memory){0};
    hq[k].index_size=kind==2?2:0;hq[k].index=(struct tegu_generic_draw_memory){kind==2?502:0,0,kind==2?12:0};
   }
   lim.small_heap_budget=0;int count=tegu_generic_draw_batch_build2(ss,rr,13,&im,&lim,&old,&hi);assert(count>=2);
   lim.small_heap_budget=TEGU_GENERIC_SMALL_HEAP_BUDGET;
   assert(tegu_generic_draw_batch_build2(ss,rr,13,&im,&lim,&now,&hi)==count&&!hi.small_heap&&!memcmp(&old,&now,sizeof(old)));
  }
  assert(!tegu_generic_set_tile_pixels(256));
  lim.small_heap_budget=TEGU_GENERIC_SMALL_HEAP_BUDGET+1u;
  assert(tegu_generic_draw_batch_build2(ss,rr,13,&im,&lim,&now,&hi)==0&&hi.stop==TEGU_BATCH_STOP_INVALID);
 }
 free(scratch);free(load);
 puts("PASS generic reflection-driven TGJ1 draw: merged spans, three resource tables, framebuffer fetch, per-stage registers, batched passes, TGJ2 64-draw passes with span/command/heap/block caps");
 return 0;
}
