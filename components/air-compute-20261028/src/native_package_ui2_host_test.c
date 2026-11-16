#include "native_package_ui2.h"
#include "native_package_ui2_writer.h"
#include "fixed_ui_texture_resources.h"
#include "fixed_ui_texture_render_state.h"
#include "fullscreen_job_contract.h"
#include "generic_job_ui.h"
#include "compositor_v7_scratch_layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t dcd0(const struct tegu_render_state*s){uint32_t v=0;for(unsigned n=0;n<s->command_count;n++)if((s->commands[n]>>56)==2&&((s->commands[n]>>48)&255)==57)v=(uint32_t)s->commands[n];return v;}
static void put32(uint8_t*p,uint32_t x){p[0]=x;p[1]=x>>8;p[2]=x>>16;p[3]=x>>24;}static uint32_t get32(const uint8_t*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void fixture(struct tegu_mali_graphics*vs,struct tegu_mali_graphics*fs,struct tegu_mali_graphics_pair2*q,struct tegu_mali_fixed_ui_texture_r85_plan*be,TeguAIRFixedUiR85Plan*fe){
 static uint8_t vc[4096],fc[1152];memset(vc,0x31,sizeof(vc));memset(fc,0x72,sizeof(fc));*vs=(struct tegu_mali_graphics){vc,sizeof(vc),1,32,0,34,3,8,UINT64_C(0x3800000000000000)};*fs=(struct tegu_mali_graphics){fc,sizeof(fc),2,32,0,51,8,76,UINT64_C(0x3000000000000000)};memset(q,0,sizeof(*q));q->version=2;q->flags=1;q->position_offset=1792;q->position_size=1792;q->point_position_size=1792;q->secondary_offset=3584;q->secondary_size=512;q->secondary_registers=32;q->generic_size_B=8;q->secondary_preload=UINT64_C(0x3800000000000000);q->vs_attribute_mask=7;q->vs_varying_mask=q->fs_varying_mask=1;q->varying_count=1;q->point_size_section=1;q->point_size_alu_type=144;q->varyings[0]=(struct tegu_mali_varying_slot){0,0,2,160};
 memset(be,0,sizeof(*be));be->version=1;be->table=1;be->resource_count=5;be->texture_index=5;be->sampler_index=6;be->normalized_coordinates=be->sample_count=be->implicit_lod=be->has_bias=1;be->constant_bytes=8;be->constant_access_bytes=0;uint32_t decl[5]={320,272,432,272,0};for(unsigned n=0;n<5;n++)be->buffers[n]=(struct tegu_mali_resource_ubo){n<2?1u:2u,0,n,1,n,decl[n]};
 memset(fe,0,sizeof(*fe));fe->version=1;fe->binding_count=4;fe->push_count=2;fe->push_bytes_length=20;fe->constant_stage=16;fe->constant_length=8;fe->specialization_length=4;uint32_t st[4]={1,1,2,2},slot[4]={2,3,1,2};uint64_t bo[4]={19,0,277,0},len[4]={512,64,512,64},dof[4]={0,0,256,0},dlen[4]={96,16,192,16};for(unsigned n=0;n<4;n++)fe->bindings[n]=(TeguAIRFixedUiR85Binding){st[n],slot[n],0,n,bo[n],len[n],dof[n],dlen[n]};fe->texture=(TeguAIRFixedUiR85TextureBinding){2,3,2,0,0,5,80,2,194,237,0x100003};fe->pushes[0]=(TeguAIRFixedUiR85PushSpan){1,0,8,0};fe->pushes[1]=(TeguAIRFixedUiR85PushSpan){16,64,12,8};put32(fe->push_bytes,19);put32(fe->push_bytes+4,0);put32(fe->push_bytes+8,21);put32(fe->push_bytes+12,0);put32(fe->push_bytes+16,0x2222);uint8_t c[8]={0,126,0,126,0,126,0,126};memcpy(fe->constant_bytes,c,8);
}
static struct tegu_ui2_bind_request request(void){struct tegu_ui2_bind_request r;memset(&r,0,sizeof(r));r.version=2;r.width=1125;r.height=2436;r.topology=3;r.vertex_count=6;r.instance_count=1;r.target=(struct tegu_ui2_target){{1,100,12u*1024u*1024u,0},80,1125,2436,4544};r.vertex=(struct tegu_ui2_allocation){2,200,288,0};r.vertex_slot=1;r.vertex_stride=48;uint64_t bo[4]={19,0,277,0},len[4]={512,64,512,64},dof[4]={0,0,256,0},dlen[4]={96,16,192,16};uint32_t st[4]={1,1,2,2},slot[4]={2,3,1,2};for(unsigned n=0;n<4;n++)r.resources[n]=(struct tegu_ui2_buffer){{10+n,300+n,len[n],bo[n]},st[n],slot[n],dof[n],dlen[n]};r.texture=(struct tegu_ui2_texture){{30,400,200000,0},0x100003,2,3,80,2,194,237,832};put32(r.vertex_push,19);put32(r.fragment_push,21);put32(r.fragment_push+8,0x2222);r.pass=(struct tegu_ui2_pass){0,0,1125,2436,0,0,1125,2436,0,0,1125,2436,2,1,{1,2,3,4}};return r;}
int main(void){struct tegu_mali_graphics vs,fs;struct tegu_mali_graphics_pair2 pair;struct tegu_mali_fixed_ui_texture_r85_plan be;TeguAIRFixedUiR85Plan fe;fixture(&vs,&fs,&pair,&be,&fe);uint8_t*pkg=0;size_t n=0;assert(!tegu_ui2_build(&vs,&fs,&pair,&be,&fe,&pkg,&n));assert(pkg&&n==1024+4096+1152&&get32(pkg)==0x32554654);struct tegu_ui2_info info;assert(!tegu_ui2_parse(pkg,n,&info));assert(info.version==2&&info.target_blend_enabled==1&&info.attributes[2].format==97&&info.resources[3].window_bytes==8&&info.texture.texture_index==5&&info.texture.sampler_index==6);uint8_t*v=malloc(4096),*f=malloc(1152);assert(v&&f&&!tegu_ui2_copy_code(&info,pkg,n,v,4096,f,1152));assert(!memcmp(v,vs.code,4096)&&!memcmp(f,fs.code,1152));assert(tegu_ui2_copy_code(&info,pkg,n,v,4095,f,1152)==TEGU_UI2_ERANGE);
 {struct tegu_mali_graphics dvs=vs,dfs=fs;struct tegu_mali_graphics_pair2 dq=pair;
  dvs.size=4352;dvs.registers=48;dvs.code=malloc(dvs.size);memset(dvs.code,0x45,dvs.size);
  dfs.size=1280;dfs.code=malloc(dfs.size);memset(dfs.code,0x67,dfs.size);
  dq.secondary_size=dvs.size-dq.secondary_offset;dq.vs_varying_mask=dq.fs_varying_mask=5;
  dq.varying_count=2;dq.varyings[0]=(struct tegu_mali_varying_slot){0,0,2,160};
  dq.varyings[1]=(struct tegu_mali_varying_slot){2,8,2,160};dq.generic_size_B=16;
  uint8_t *dynamic=0;size_t dynamic_n=0;struct tegu_ui2_info dynamic_info;
  assert(!tegu_ui2_build(&dvs,&dfs,&dq,&be,&fe,&dynamic,&dynamic_n));
  assert(dynamic_n==TEGU_UI2_HEADER_BYTES+dvs.size+dfs.size&&
   !tegu_ui2_parse(dynamic,dynamic_n,&dynamic_info)&&dynamic_info.pair.varying_count==2&&
   dynamic_info.stages[0].bytes==dvs.size&&dynamic_info.stages[1].bytes==dfs.size);
  tegu_ui2_free(&dynamic,&dynamic_n);free(dvs.code);free(dfs.code);}
 struct tegu_ui2_bind_request req=request();struct tegu_ui2_binding bound;assert(!tegu_ui2_bind(&info,&req,&bound));assert(bound.vertex_count==6&&bound.resources[2].push_residual==21&&bound.texture.width==194&&bound.texture.row_bytes==832&&bound.texture.bytes==197184);
 /* Mali v10 linear textures need 64-byte rows: the tight 776-byte R102
  * stride, unaligned, undersized and allocation-overrunning rows are refused. */
 {const uint32_t badrows[3]={776,840,768};for(unsigned k=0;k<3;k++){struct tegu_ui2_bind_request bad=request();bad.texture.row_bytes=badrows[k];struct tegu_ui2_binding o;memset(&o,0xa5,sizeof(o));struct tegu_ui2_binding keep=o;assert(tegu_ui2_bind(&info,&bad,&o)<0&&!memcmp(&o,&keep,sizeof(o)));}
  struct tegu_ui2_bind_request bad=request();bad.texture.allocation.length=197183;assert(tegu_ui2_bind(&info,&bad,&bound)<0);
  bad=request();bad.texture.allocation.offset=16;bad.texture.allocation.length=300000;assert(tegu_ui2_bind(&info,&bad,&bound)<0);
  bad=request();bad.texture.row_bytes=896;bad.texture.allocation.length=212352;assert(!tegu_ui2_bind(&info,&bad,&bound)&&bound.texture.bytes==UINT64_C(896)*237);}
 req=request();assert(!tegu_ui2_bind(&info,&req,&bound));req.vertex_count=3;assert(!tegu_ui2_bind(&info,&req,&bound)&&bound.vertex_count==3);req.vertex.length=384u*48u;req.vertex_count=12;assert(!tegu_ui2_bind(&info,&req,&bound)&&bound.vertex_count==12);req.vertex_count=384;assert(!tegu_ui2_bind(&info,&req,&bound)&&bound.vertex_bytes==384u*48u);req.vertex_count=387;assert(tegu_ui2_bind(&info,&req,&bound)<0);req.vertex.length=288;req.vertex_count=12;assert(tegu_ui2_bind(&info,&req,&bound)<0);req.vertex_count=4;struct tegu_ui2_binding before;memset(&before,0xa5,sizeof(before));struct tegu_ui2_binding badout=before;assert(tegu_ui2_bind(&info,&req,&badout)<0&&!memcmp(&badout,&before,sizeof(before)));req=request();req.fragment_push[8]=0;badout=before;assert(tegu_ui2_bind(&info,&req,&badout)<0&&!memcmp(&badout,&before,sizeof(before)));req=request();assert(!tegu_ui2_bind(&info,&req,&bound));
 /* R93's real BackBoard buffers share one 256 KiB allocation. Their aligned
  * descriptor ranges contain the requested windows, while the AIR push
  * residuals are larger than one 32-byte descriptor-alignment unit. */
 req=request();req.target.allocation=(struct tegu_ui2_allocation){1,1,0xab8000,0};req.target.row_bytes=4608;req.vertex=(struct tegu_ui2_allocation){20,20,288,0};
 const uint64_t live_offset[4]={176,464,0,448},live_descriptor[4]={0,256,0,256},live_length[4]={240,224,176,208};
 for(unsigned k=0;k<4;k++){req.resources[k].allocation.token=req.resources[k].allocation.object_id=17;req.resources[k].allocation.length=0x40000;req.resources[k].allocation.offset=live_offset[k];req.resources[k].descriptor_offset=live_descriptor[k];req.resources[k].descriptor_length=live_length[k];}
 req.texture.allocation=(struct tegu_ui2_allocation){19,19,197184,0};put32(req.vertex_push,176);put32(req.vertex_push+4,208);put32(req.fragment_push,0);put32(req.fragment_push+4,192);req.pass.load_action=1;memset(req.pass.clear_color,0,sizeof(req.pass.clear_color));
 assert(!tegu_ui2_bind(&info,&req,&bound));assert(bound.resources[0].push_residual==176&&bound.resources[1].push_residual==208&&bound.resources[2].push_residual==0&&bound.resources[3].push_residual==192);
 struct tegu_fs_surface_layout liveLayout={};assert(!tegu_fs_surface_layout_with_heap(bound.width,bound.height,bound.target_row_bytes,bound.target_offset,9,FS_V7_SCRATCH_PAGES,FS_HEAP_PAGES,&liveLayout));assert(bound.target_bytes==11224980&&liveLayout.target_bytes==11225088);assert(tegu_fs_surface_visible_span_fits(&liveLayout,bound.target_bytes,req.target.allocation.length-bound.target_offset));assert(!tegu_fs_surface_visible_span_fits(&liveLayout,bound.target_bytes,liveLayout.target_bytes-1));
 req=request();assert(!tegu_ui2_bind(&info,&req,&bound));
 struct tegu_fixed_ui_texture_resource_input ri;memset(&ri,0,sizeof(ri));ri.version=1;ri.binding=&bound;ri.table0=(struct tegu_fixed_ui_texture_span){0x2000000,64};ri.resource_table=(struct tegu_fixed_ui_texture_span){0x2010000,64};ri.descriptors=(struct tegu_fixed_ui_texture_span){0x2020000,256};ri.plane=(struct tegu_fixed_ui_texture_span){0x2030000,32};uint64_t ba[4]={0x2100000,0x2110000,0x2120000,0x2130000};for(unsigned k=0;k<4;k++)ri.buffers[k]=(struct tegu_fixed_ui_texture_allocation_span){10+k,300+k,ba[k],req.resources[k].allocation.length};ri.texture=(struct tegu_fixed_ui_texture_allocation_span){30,400,0x2200000,200000};struct tegu_fixed_ui_texture_resource_state rs;assert(!tegu_fixed_ui_texture_resource_state_build(&ri,&rs));assert(rs.resource_table_handle==0x2010004&&get32(rs.descriptors)==25&&get32(rs.descriptors+128)==25&&get32(rs.descriptors+160)!=0&&get32(rs.descriptors+192)!=0&&get32(rs.push_bytes+16)==0x2222);struct tegu_fixed_ui_texture_resource_state rssave;memset(&rssave,0xa5,sizeof(rssave));struct tegu_fixed_ui_texture_resource_state rsbad=rssave;ri.texture.object_id++;assert(tegu_fixed_ui_texture_resource_state_build(&ri,&rsbad)<0&&!memcmp(&rsbad,&rssave,sizeof(rssave)));ri.texture.object_id--;
 assert(get32(rs.plane+4)==197184&&get32(rs.plane+16)==832&&get32(rs.plane+28)==(193u|(236u<<16)));
 {struct tegu_ui2_binding pad=bound;ri.binding=&pad;pad.texture.row_bytes=776;pad.texture.bytes=UINT64_C(776)*237;rsbad=rssave;assert(tegu_fixed_ui_texture_resource_state_build(&ri,&rsbad)<0&&!memcmp(&rsbad,&rssave,sizeof(rssave)));
  pad=bound;pad.texture.offset=16;rsbad=rssave;assert(tegu_fixed_ui_texture_resource_state_build(&ri,&rsbad)<0&&!memcmp(&rsbad,&rssave,sizeof(rssave)));ri.binding=&bound;}
 struct tegu_render_addresses a={.descriptors=0x100000,.tiler=0x110000,.geometry=0x120000,.chunk=0x200000,.target=0x500000,.vertex_code=0x1100000,.fragment_code=0x1120000,.chunk_bytes=0x200000,.hierarchy_mask=0x82};struct tegu_render_target_layout l={TEGU_RENDER_TARGET_LAYOUT_VERSION,1125,2436,4544,UINT64_C(4544)*2436};struct tegu_render_state state;assert(!tegu_fixed_ui_texture_render_build(&info,&bound,&a,rs.resource_table_handle,&l,&state));assert(get32(state.descriptors+0xa04)==(get32(state.descriptors+0xa14)|0x80000000u));assert(state.command_count>40&&get32(state.descriptors+0xa80)==19&&get32(state.descriptors+0xb80+64)==21);assert(get32(state.descriptors+128)==(1u<<26)&&get32(state.descriptors+132)==(1u|(19u<<3)|(2u<<8)|(0x60au<<16)|(1u<<31)));struct tegu_render_state clear_state=state;bound.load_action=1;assert(!tegu_fixed_ui_texture_render_build(&info,&bound,&a,rs.resource_table_handle,&l,&state)&&!memcmp(&state,&clear_state,sizeof(state)));struct tegu_render_state prior;memset(&prior,0x5a,sizeof(prior));struct tegu_render_state refused=prior;struct tegu_ui2_info bad=info;bad.target_blend_enabled=0;assert(!tegu_fixed_ui_texture_render_build(&bad,&bound,&a,rs.resource_table_handle,&l,&refused)&&dcd0(&refused)==3);refused=prior;bad.target_blend_enabled=2;assert(tegu_fixed_ui_texture_render_build(&bad,&bound,&a,rs.resource_table_handle,&l,&refused)<0&&!memcmp(&prior,&refused,sizeof(prior)));
 /* TGJ1: the userspace-built job carries the XNU client's spans, VAs and state. */
 {uint8_t *scratch=calloc(1,TEGU_GENERIC_JOB_SCRATCH_BYTES),*load=calloc(1,TEGU_GENERIC_JOB_LOAD_BYTES);assert(scratch&&load);
  struct tegu_generic_job_images im={900,901,scratch,load};
  struct tegu_ui2_bind_request g=request();g.target.allocation.object_id=g.target.allocation.token;
  for(unsigned k=0;k<4;k++)g.resources[k].allocation.object_id=g.resources[k].allocation.token;
  g.texture.allocation.object_id=g.texture.allocation.token;g.vertex.object_id=g.vertex.token;
  g.texture.allocation.length=200704;g.target.allocation.length=UINT64_C(4544)*2436;
  struct tegu_generic_job_request job;struct tegu_generic_job_plan plan;
  assert(!tegu_ui2_generic_job_build(&info,pkg,n,&g,&im,&job)&&!tegu_generic_job_plan(&job,&plan));
  assert(job.span_count==6&&job.flags==TEGU_GENERIC_JOB_HEAP2M&&job.scratch_token==900&&job.target_token==1&&
   job.spans[0].token==2&&job.spans[5].token==30&&job.spans[5].length==197184&&job.command_count>40);
  for(unsigned k=0;k<job.command_count;k++)assert(!tegu_generic_job_command_allowed(job.commands[k]));
  uint64_t rt=(uint64_t)get32(scratch+160)|(uint64_t)get32(scratch+164)<<32;assert(rt==plan.target_va);
  uint64_t texel=(uint64_t)get32(scratch+0x7200+8)|(uint64_t)get32(scratch+0x7200+12)<<32;assert(texel==plan.span_va[5]);
  assert(get32(scratch+0x7200+16)==832);
  for(unsigned k=0;k<4;k++){uint64_t ubo=(uint64_t)get32(scratch+0x7100+k*32+8)|(uint64_t)get32(scratch+0x7100+k*32+12)<<32;
   uint64_t off=g.resources[k].descriptor_offset;assert(ubo==plan.span_va[1+k]+(off&4095u));}
  assert(!memcmp(scratch+0x2000,pkg+info.stages[0].offset,info.stages[0].bytes)&&
   !memcmp(scratch+0x30000,pkg+info.stages[1].offset,info.stages[1].bytes));
  assert(get32(scratch+0xa04)==(get32(scratch+0xa14)|0x80000000u));
  assert((get32(scratch+0x7100+192)&(3u<<27))==(3u<<27));            /* nearest */
  im.sampler_linear=3;assert(!tegu_ui2_generic_job_build(&info,pkg,n,&g,&im,&job));
  assert(!(get32(scratch+0x7100+192)&(3u<<27)));                         /* linear min+mag */
  im.sampler_linear=2;assert(!tegu_ui2_generic_job_build(&info,pkg,n,&g,&im,&job));
  assert((get32(scratch+0x7100+192)&(3u<<27))==(1u<<27));
  im.sampler_linear=4;assert(tegu_ui2_generic_job_build(&info,pkg,n,&g,&im,&job)<0);im.sampler_linear=0;
  /* LOAD pass adds the 8 KiB preload pages as the last span and a preload stream. */
  g.pass.load_action=1;memset(g.pass.clear_color,0,sizeof(g.pass.clear_color));
  assert(!tegu_ui2_generic_job_build(&info,pkg,n,&g,&im,&job)&&!tegu_generic_job_plan(&job,&plan));
  assert(job.span_count==7&&job.spans[6].token==901&&job.spans[6].length==8192&&job.exec_mask==TEGU_V7_LOAD_EXEC_MASK&&
   job.commands[0]==((3ull<<56)|(0xffull<<16)));
  int nonzero=0;for(unsigned k=0;k<8192;k++)nonzero|=load[k];assert(nonzero);
  /* R170: INTERSECT preload without clean tile writes keeps untouched tiles. */
  assert((scratch[0]&7)==2&&!(scratch[TEGU_GENERIC_JOB_RT_OFFSET+7]&0x80));
  /* Refusals: missing LOAD image, unequal target identity. */
  struct tegu_generic_job_images noload={900,0,scratch,0};assert(tegu_ui2_generic_job_build(&info,pkg,n,&g,&noload,&job)<0);
  g=request();assert(tegu_ui2_generic_job_build(&info,pkg,n,&g,&im,&job)<0);
  free(scratch);free(load);}
 pkg[640]^=1;struct tegu_ui2_info untouched;memset(&untouched,0xcc,sizeof(untouched));struct tegu_ui2_info save=untouched;assert(tegu_ui2_parse(pkg,n,&untouched)<0&&!memcmp(&untouched,&save,sizeof(save)));free(v);free(f);tegu_ui2_free(&pkg,&n);assert(!pkg&&!n);puts("PASS TFU2/UI2 sampled fixed-UI package, four UBOs, texture/sampler SRT and source-over render state");return 0;}
