#include "native_package_ui1.h"
#include "native_package_ui1_writer.h"
#include "generic_job_ui.h"
#include "compositor_v7_scratch_layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t x) {p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);p[2]=(uint8_t)(x>>16);p[3]=(uint8_t)(x>>24);}
static void inputs(struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,
 struct tegu_mali_graphics_pair2 *pair,struct tegu_mali_fixed_ui_plan *backend,
 TeguAIRFixedUiR80Plan *front) {
 memset(vs,0,sizeof(*vs));memset(fs,0,sizeof(*fs));memset(pair,0,sizeof(*pair));
 memset(backend,0,sizeof(*backend));memset(front,0,sizeof(*front));
 vs->size=4096;vs->code=(unsigned char *)malloc(vs->size);memset(vs->code,0x31,vs->size);
 *vs=(struct tegu_mali_graphics){vs->code,4096,1,32,0,33,3,4,UINT64_C(0x3800000000000000)};
 fs->size=1024;fs->code=(unsigned char *)malloc(fs->size);memset(fs->code,0x52,fs->size);
 *fs=(struct tegu_mali_graphics){fs->code,1024,2,32,0,50,8,72,UINT64_C(0x3000000000000000)};
 pair->version=2;pair->flags=1;pair->position_offset=1792;pair->position_size=1792;
 pair->point_position_size=1792;pair->secondary_offset=3584;pair->secondary_size=512;
 pair->secondary_registers=32;pair->generic_size_B=16;pair->secondary_preload=UINT64_C(0x3800000000000000);
 pair->vs_attribute_mask=3;pair->vs_varying_mask=pair->fs_varying_mask=1;pair->varying_count=1;
 pair->point_size_section=1;pair->point_size_alu_type=144;
 pair->varyings[0]=(struct tegu_mali_varying_slot){0,0,4,160};
 backend->version=1;backend->table=1;backend->resource_count=3;
 backend->constant_bytes=8;backend->constant_access_bytes=336;
 backend->ubos[0]=(struct tegu_mali_resource_ubo){1,0,0,1,0,320};
 backend->ubos[1]=(struct tegu_mali_resource_ubo){2,0,1,1,1,432};
 backend->ubos[2]=(struct tegu_mali_resource_ubo){2,0,2,1,2,0};
 front->version=1;front->binding_count=2;front->push_count=2;front->push_bytes_length=12;
 front->constant_stage=16;front->constant_length=8;
 front->bindings[0]=(TeguAIRFixedUiR80Binding){1,2,0,0,19,512,0,96};
 front->bindings[1]=(TeguAIRFixedUiR80Binding){2,1,0,1,277,512,256,192};
 front->pushes[0]=(TeguAIRFixedUiR80PushSpan){1,0,4,0};
 front->pushes[1]=(TeguAIRFixedUiR80PushSpan){16,64,8,4};
 put32(front->push_bytes,19);put32(front->push_bytes+4,21);put32(front->push_bytes+8,0x76543210);
 memcpy(front->constant_bytes,(const uint8_t[]){0,126,0,126,0,126,0,126},8);
}
static void parse_refuses(const uint8_t *package,size_t n,size_t offset) {
 uint8_t *bad=(uint8_t *)malloc(n);memcpy(bad,package,n);bad[offset]^=1;
 struct tegu_ui1_info out,before;memset(&out,0xa5,sizeof(out));before=out;
 int rc=tegu_ui1_parse(bad,n,&out);if(!rc)fprintf(stderr,"accepted mutated header offset %zu\n",offset);
 assert(rc!=0);assert(!memcmp(&out,&before,sizeof(out)));free(bad);
}
static struct tegu_ui1_bind_request request(void) {
 struct tegu_ui1_bind_request r;memset(&r,0,sizeof(r));r.version=1;r.width=1125;r.height=2436;
 r.topology=3;r.vertex_count=3;r.instance_count=1;
 uint64_t target_bytes=(uint64_t)(r.height-1)*4544+r.width*4;
 r.target=(struct tegu_ui1_target){{11,101,target_bytes,0},80,r.width,r.height,4544};
 r.vertex=(struct tegu_ui1_allocation){12,102,4096,0};r.vertex_slot=1;r.vertex_stride=48;
 r.resources[0]=(struct tegu_ui1_buffer){{13,103,512,19},1,2,0,96};
 r.resources[1]=(struct tegu_ui1_buffer){{14,103,512,277},2,1,256,192};
 put32(r.vertex_push,19);put32(r.fragment_push,21);put32(r.fragment_push+4,0x76543210);
 r.pass.render_width=r.pass.viewport_width=r.pass.scissor_width=r.width;
 r.pass.render_height=r.pass.viewport_height=r.pass.scissor_height=r.height;
 r.pass.load_action=TEGU_UI1_LOAD_CLEAR;r.pass.store_action=TEGU_UI1_STORE;
 for(unsigned i=0;i<4;i++)r.pass.clear_color[i]=0x11223344;
 return r;
}
static void bind_refuses(const struct tegu_ui1_info *info,struct tegu_ui1_bind_request r) {
 struct tegu_ui1_binding out,before;memset(&out,0xa5,sizeof(out));before=out;
 assert(tegu_ui1_bind(info,&r,&out)!=0);assert(!memcmp(&out,&before,sizeof(out)));
}
int main(void) {
 _Static_assert(sizeof(TeguAIRFixedUiR80Plan)==184,"AIR fixed UI ABI");
 _Static_assert(sizeof(struct tegu_ui1_attribute)==28,"UI1 attribute ABI");
 _Static_assert(sizeof(struct tegu_ui1_resource)==40,"UI1 resource ABI");
 struct tegu_mali_graphics vs,fs;struct tegu_mali_graphics_pair2 pair;
 struct tegu_mali_fixed_ui_plan backend;TeguAIRFixedUiR80Plan front;
 inputs(&vs,&fs,&pair,&backend,&front);uint8_t *package=NULL;size_t n=0;
 assert(!tegu_ui1_build(&vs,&fs,&pair,&backend,&front,0,&package,&n));
 assert(package&&n==TEGU_UI1_HEADER_BYTES+4096+1024&&get32(package)==0x31554654);
 struct tegu_ui1_info info;assert(!tegu_ui1_parse(package,n,&info));
 assert(info.version==1&&info.total_bytes==n&&info.target_format==80&&info.target_write_mask==15);
 assert(info.stages[0].offset==768&&info.stages[1].offset==4864&&info.stages[0].push_bytes==4&&info.stages[1].push_bytes==72);
 assert(info.attributes[0].format==109&&info.attributes[1].format==97&&info.attributes[1].offset==32);
 assert(info.resources[0].metal_slot==2&&info.resources[0].window_bytes==64&&info.resources[1].metal_slot==1&&info.resources[1].window_bytes==168);
 assert(info.resources[2].metal_stage==0&&info.resources[2].binding==2&&info.resources[2].index==2&&info.resources[2].window_bytes==8);
 assert(info.pushes[0].offset==0&&info.pushes[0].length==4&&info.pushes[1].offset==64&&info.pushes[1].length==8);
 assert(info.constant_stage==16&&!memcmp(info.constant,front.constant_bytes,8));
 uint8_t vcopy[4096],fcopy[1024];memset(vcopy,0,sizeof(vcopy));memset(fcopy,0,sizeof(fcopy));
 assert(!tegu_ui1_copy_code(&info,package,n,vcopy,sizeof(vcopy),fcopy,sizeof(fcopy)));
 assert(!memcmp(vcopy,vs.code,4096)&&!memcmp(fcopy,fs.code,1024));
 /* A specialization may change code length, register pressure and the linked
  * generic layout without changing the fixed resource contract. */
 {struct tegu_mali_graphics dvs=vs,dfs=fs;struct tegu_mali_graphics_pair2 dp=pair;
  dvs.size=4352;dvs.registers=48;dvs.code=malloc(dvs.size);memset(dvs.code,0x63,dvs.size);
  dfs.size=1280;dfs.code=malloc(dfs.size);memset(dfs.code,0x84,dfs.size);
  dp.secondary_size=dvs.size-dp.secondary_offset;dp.vs_varying_mask=dp.fs_varying_mask=5;
  dp.varying_count=2;dp.varyings[0]=(struct tegu_mali_varying_slot){0,0,2,160};
  dp.varyings[1]=(struct tegu_mali_varying_slot){2,8,2,160};
  uint8_t *dynamic=NULL;size_t dynamic_n=0;struct tegu_ui1_info dynamic_info;
  assert(!tegu_ui1_build(&dvs,&dfs,&dp,&backend,&front,0,&dynamic,&dynamic_n));
  assert(dynamic_n==TEGU_UI1_HEADER_BYTES+dvs.size+dfs.size&&
   !tegu_ui1_parse(dynamic,dynamic_n,&dynamic_info)&&dynamic_info.pair.varying_count==2&&
   dynamic_info.stages[0].bytes==dvs.size&&dynamic_info.stages[1].bytes==dfs.size);
  tegu_ui1_free(&dynamic,&dynamic_n);free(dvs.code);free(dfs.code);}
 static const size_t strict_offsets[]={0,4,8,12,16,20,24,28,32,36,40,48,56,60,64,
  72,76,80,88,96,100,104,112,116,120,124,136,140,148,160,164,168,172,
  176,180,184,188,192,196,200,216,264,272,300,328,384,424,464,504,520,
  536,540,552,556,564,568,572,576,580,584,588,592,624,656,767};
 for(unsigned k=0;k<sizeof(strict_offsets)/sizeof(strict_offsets[0]);k++)parse_refuses(package,n,strict_offsets[k]);
 struct tegu_ui1_bind_request r=request();struct tegu_ui1_binding bound;
 assert(!tegu_ui1_bind(&info,&r,&bound));
 assert(bound.width==1125&&bound.height==2436&&bound.target_format==80&&bound.store_action==1);
 assert(bound.resources[0].descriptor_offset==0&&bound.resources[0].descriptor_bytes==96&&bound.resources[0].push_residual==19);
 assert(bound.resources[1].descriptor_offset==256&&bound.resources[1].descriptor_bytes==192&&bound.resources[1].push_residual==21);
 assert(!memcmp(bound.constant,front.constant_bytes,8)&&bound.constant_stage==16);
 struct tegu_ui1_bind_request live=request();live.target.allocation=(struct tegu_ui1_allocation){1,1,0xab8000,0};live.target.row_bytes=4608;live.resources[0]=(struct tegu_ui1_buffer){{17,17,0x40000,176},1,2,0,240};live.resources[1]=(struct tegu_ui1_buffer){{17,17,0x40000,0},2,1,0,176};put32(live.vertex_push,176);put32(live.fragment_push,0);assert(!tegu_ui1_bind(&info,&live,&bound));assert(bound.resources[0].push_residual==176&&bound.resources[1].push_residual==0);
 struct tegu_ui1_bind_request bad=r;bad.resources[1].allocation.length=447;bind_refuses(&info,bad);
 bad=r;put32(bad.fragment_push,20);bind_refuses(&info,bad);
 bad=r;bad.vertex_stride=44;bind_refuses(&info,bad);
 bad=r;bad.target.row_bytes=4500;bind_refuses(&info,bad);
 bad=r;bad.pass.scissor_width=r.width+1;bind_refuses(&info,bad);
 bad=r;bad.pass.load_action=3;bind_refuses(&info,bad);
 bad=r;bad.target.allocation.object_id=bad.resources[0].allocation.object_id;
 bad.target.allocation.offset=bad.resources[0].descriptor_offset;bind_refuses(&info,bad);
 bad=r;bad.resources[1].allocation.offset=21;bad.resources[1].descriptor_offset=0;
 bad.resources[1].descriptor_length=192;put32(bad.fragment_push,21);
 assert(!tegu_ui1_bind(&info,&bad,&bound)); /* Read-only VS/FS ranges may alias. */
 bad=r;bad.pass.load_action=UINT32_MAX;bind_refuses(&info,bad);
 uint8_t *none=NULL;size_t zero=0;struct tegu_mali_fixed_ui_plan badbackend=backend;badbackend.ubos[1].declared_bytes=431;
 assert(tegu_ui1_build(&vs,&fs,&pair,&badbackend,&front,0,&none,&zero)!=0&&!none&&!zero);
 TeguAIRFixedUiR80Plan badfront=front;badfront.pushes[1].length=4;
 assert(tegu_ui1_build(&vs,&fs,&pair,&backend,&badfront,0,&none,&zero)!=0&&!none&&!zero);
 struct tegu_mali_graphics_pair2 badpair=pair;badpair.vs_attribute_mask=7;
 assert(tegu_ui1_build(&vs,&fs,&badpair,&backend,&front,0,&none,&zero)!=0&&!none&&!zero);
 struct tegu_mali_graphics badstage=fs;badstage.push_bytes=68;
 assert(tegu_ui1_build(&vs,&badstage,&pair,&backend,&front,0,&none,&zero)!=0&&!none&&!zero);
 /* R111 live lock-screen fill: a 128-byte fragment stage that reads no push
  * constants (14 FAU words) packages, parses and builds TGJ1 state. */
 {struct tegu_mali_graphics small=fs;small.size=128;small.push_bytes=0;small.fau_words=14;
  uint8_t *pkg=NULL;size_t bytes=0;struct tegu_ui1_info sinfo;
  assert(!tegu_ui1_build(&vs,&small,&pair,&backend,&front,1,&pkg,&bytes)&&!tegu_ui1_parse(pkg,bytes,&sinfo));
  assert(sinfo.stages[1].bytes==128&&sinfo.stages[1].push_bytes==0&&sinfo.stages[1].fau_words==14&&sinfo.target_blend_enabled==1);
  {uint8_t *scratch=calloc(1,TEGU_GENERIC_JOB_SCRATCH_BYTES),*load=calloc(1,TEGU_GENERIC_JOB_LOAD_BYTES);assert(scratch&&load);
   struct tegu_generic_job_images im={900,901,scratch,load};struct tegu_ui1_bind_request g=request();
   g.target.allocation=(struct tegu_ui1_allocation){11,11,UINT64_C(4544)*2436,0};g.vertex.object_id=g.vertex.token;
   g.resources[0].allocation.object_id=g.resources[1].allocation.object_id=13;g.resources[1].allocation.token=13;
   struct tegu_generic_job_request job;
   assert(!tegu_ui1_generic_job_build(&sinfo,pkg,bytes,&g,&im,&job));
   g.pass.load_action=TEGU_UI1_LOAD;memset(g.pass.clear_color,0,sizeof(g.pass.clear_color));
   assert(!tegu_ui1_generic_job_build(&sinfo,pkg,bytes,&g,&im,&job)&&job.span_count==3);
   free(scratch);free(load);}
  tegu_ui1_free(&pkg,&bytes);
  small.push_bytes=36;assert(tegu_ui1_build(&vs,&small,&pair,&backend,&front,1,&pkg,&bytes)!=0&&!pkg&&!bytes);}
 /* TGJ1: UI1 descriptor windows in one allocation share one span, as in XNU. */
 {uint8_t *scratch=calloc(1,TEGU_GENERIC_JOB_SCRATCH_BYTES),*load=calloc(1,TEGU_GENERIC_JOB_LOAD_BYTES);assert(scratch&&load);
  struct tegu_generic_job_images im={900,901,scratch,load};struct tegu_ui1_bind_request g=request();
  g.target.allocation=(struct tegu_ui1_allocation){11,11,UINT64_C(4544)*2436,0};g.vertex.object_id=g.vertex.token;
  g.resources[0].allocation.object_id=g.resources[1].allocation.object_id=13;g.resources[1].allocation.token=13;
  struct tegu_generic_job_request job;struct tegu_generic_job_plan plan;
  assert(!tegu_ui1_generic_job_build(&info,package,n,&g,&im,&job)&&!tegu_generic_job_plan(&job,&plan));
  assert(job.span_count==2&&job.spans[1].token==13&&job.spans[1].offset==0&&job.spans[1].length==256+192&&
   job.exec_mask==TEGU_V7_CLEAR_EXEC_MASK&&job.command_count>40);
  uint64_t rt=(uint64_t)get32(scratch+160)|(uint64_t)get32(scratch+164)<<32;assert(rt==plan.target_va);
  assert(!memcmp(scratch+0x2000,package+info.stages[0].offset,info.stages[0].bytes));
  g.pass.load_action=TEGU_UI1_LOAD;memset(g.pass.clear_color,0,sizeof(g.pass.clear_color));
  assert(!tegu_ui1_generic_job_build(&info,package,n,&g,&im,&job)&&!tegu_generic_job_plan(&job,&plan));
  assert(job.span_count==3&&job.spans[2].token==901&&job.exec_mask==TEGU_V7_LOAD_EXEC_MASK&&
   job.commands[0]==((3ull<<56)|(0xffull<<16)));
  g.resources[1].allocation=(struct tegu_ui1_allocation){14,14,512,277};
  g.pass.load_action=TEGU_UI1_LOAD_CLEAR;
  assert(!tegu_ui1_generic_job_build(&info,package,n,&g,&im,&job)&&job.span_count==3&&job.spans[2].token==14);
  free(scratch);free(load);}
 tegu_ui1_free(&package,&n);assert(!package&&!n);free(vs.code);free(fs.code);
 puts("PASS TFU1 exact fixed UI package/parser/binder: real R82 stages, two live stride48 attributes, two user UBOs plus the internal constant descriptor; HOST ONLY");
 return 0;
}
