/* MMIO/firmware doubles for submission control flow; not GPU emulation. */
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "fullscreen_job_contract.h"
static int PE_parse_boot_argn(const char *name,void *value,int size) {
 (void)name;(void)value;(void)size;return 0;
}
static uint64_t full_l2[512];
static const struct tegu_full_render_job *full_job_under_test;
static unsigned full_mode;
static uint64_t last_full_lock;
static unsigned pt_syncs;
typedef uintptr_t vm_offset_t;
void FlushPoC_DcacheRegion(vm_offset_t address,size_t bytes) {
 assert(address&&bytes==4096);
 for(unsigned i=0;i<512;i++)assert(!full_l2[i]);
 pt_syncs++;
}
#define TEGU_GPU_STAGE_BASE 0x880000000ULL
static uint64_t tg_memory_size=0x154000;
#define TEGU_GPU_QUEUE_ROOT 0x880100000ULL
#define TEGU_GPU_QUEUE_RING_PA 0x1000
#define TEGU_GPU_QUEUE_INPUT_PA 0x2000
#define TEGU_GPU_QUEUE_OUTPUT_PA 0x3000
#define TEGU_GPU_QUEUE_RESULT_PA 0x4000
#define TEGU_GPU_SHARED_VA 0x5000
#define TEGU_GPU_QUEUE_VA 0x5000000ULL
static uint32_t code[32],desc[36],table[1024],pipeline_desc[160];
static uint32_t *pte=table+256;
static unsigned mmuWaits,mmuFail,mmuCommands;
struct diag_log_record { const char *name; uint64_t value; };
static struct diag_log_record diag_log[48];
static unsigned diag_log_count,completion_wait_calls,scratch_completion_waits,output_completion_waits;
static unsigned require_snapshot_before_stop,stop_snapshot_checks,shared_mapping_lookups;
static uint64_t bounded_wait_ticks; static unsigned bounded_wait_calls;
static unsigned status_refresh_ack,status_refresh_waits,status_refresh_rings,status_refresh_ring_calls;
static uint32_t expected_completion_token;
static void capture_hex(const char *name,uint64_t value) {
 assert(diag_log_count<48);diag_log[diag_log_count++]=(struct diag_log_record){name,value};
}
static uint32_t ring[1024],input[2],output[2],scratch;
static uint32_t global[8],group[40],gin[8],gout[4],ci[22],co[8],si[16],so[54];
static bool tg_queue_live;
static uint32_t tg_saved_pll=0x10000001;
static unsigned tg_changed_pll=1,mode,stops,restores,doorbells,halts;
static uintptr_t tg_cmu;
#define GPU_IF_BARRIER() ((void)0)
#define hex(s,v) capture_hex((s),(uint64_t)(v))
#define text(s) ((void)0)
#define LCK_GRP_DECLARE(n,s) int n
#define LCK_MTX_DECLARE(n,g) int *n=g
#define lck_mtx_lock(p) ((void)(p))
#define lck_mtx_unlock(p) ((void)(p))
static volatile uint32_t *memory(uint64_t pa,unsigned size){
 (void)size;
 switch(pa){
 case TEGU_GPU_QUEUE_ROOT+0x3000:return table;
 case TEGU_GPU_QUEUE_ROOT+0x2000:return (volatile uint32_t *)full_l2;
 case TEGU_GPU_QUEUE_ROOT+0x1c000:return pipeline_desc;
 case TEGU_GPU_QUEUE_ROOT+0x1f400:return code;
 case TEGU_GPU_QUEUE_ROOT+0x20a00:return desc;
 case TEGU_GPU_QUEUE_ROOT+0x20a80:return desc+32;
 case TEGU_GPU_QUEUE_ROOT+0x3400:return pte;
 case 0x1000:return ring;case 0x2000:return input;case 0x3000:return output;case 0x4800:return &scratch;}
 return 0;
}
#define GPU_CPU_POINTER(pa,n) memory(pa,n)
static volatile uint32_t *gpu_iface_pointer(uint32_t va,unsigned size){
 (void)size;
 if(va==0x5000 && mode==9){
  if(shared_mapping_lookups++ && require_snapshot_before_stop){assert(diag_log_count==31);stop_snapshot_checks++;}
  return 0;
 }
 switch(va){case 0x5000:return global;case 0x6000:return group;
 case 1:return gin;case 2:return gout;case 3:return ci;case 4:return co;case 5:return so;case 6:return si;}
 return 0;
}
static uint32_t gpu_rd(unsigned offset){
 if(offset==0x2000)return mode==3?1:mode==5?0x20000:0;
 return offset==0x3c && (mode==6 || (full_mode==21 && doorbells))?0x80:0;
}
static uint32_t s2_rd(unsigned offset){(void)offset;return 0;}
static void tg_write(uintptr_t base,unsigned offset,uint32_t value){
 (void)base;assert(offset==0x104 && value==tg_saved_pll);assert(stops);restores++;
}
static int gpu_firmware_stop(void){assert((co[0]&7)==0);if(require_snapshot_before_stop){assert(diag_log_count==31);stop_snapshot_checks++;}stops++;return mode==2 ? -100:0;}
static int gpu_queue_wait(volatile uint32_t *p,uint32_t mask,uint32_t want){
 if(p==co && mask==0x20u){status_refresh_waits++;if(!status_refresh_ack)return -1;}
 if(p==&scratch){completion_wait_calls++;scratch_completion_waits++;if(mode==7)return -1;}
 if(p==output){completion_wait_calls++;output_completion_waits++;if(mode==8)return -1;}
 return (*p&mask)==want ? 0:-1;
}
static int gpu_queue_wait_ticks(volatile uint32_t *p,uint32_t mask,uint32_t want,uint64_t ticks){
 assert(ticks);bounded_wait_ticks=ticks;bounded_wait_calls++;return gpu_queue_wait(p,mask,want);
}
static uint64_t position(uint32_t *p){return ((uint64_t)p[1]<<32)|p[0];}
static uint64_t instruction(uint64_t at){unsigned slot=(at&4095)/4;return ((uint64_t)ring[slot+1]<<32)|ring[slot];}
static void gpu_queue_ring(volatile uint32_t *in,volatile uint32_t *out){
 (void)in;(void)out;
 if(require_snapshot_before_stop && !tg_queue_live){assert(diag_log_count==31);stop_snapshot_checks++;}
 // Request doorbells acknowledge state, but do NOT consume new ring commands.
 if((ci[0]&7)==0){halts++;if(mode!=4)co[0]&=~7u;}
 if(status_refresh_rings && ((ci[0]^co[0])&0x20u)){
  status_refresh_ring_calls++;
  if(tg_queue_live){
   assert(diag_log_count==24);
   if(status_refresh_ack){
    co[0]=(co[0]&~0x20u)|(ci[0]&0x20u);
    so[0x40/4]=0x1234;so[0x44/4]=0x5678;so[0x48/4]=0xa5;
    so[0x4c/4]=0x11;so[0x5c/4]=0x33;so[0x60/4]=0x22;
   }
  }
 }
 co[2]=ci[2];gout[2]=gin[2];
}
static int gpu_boot_wait(unsigned offset,uint32_t mask,uint32_t want,unsigned ms){
 assert(offset==0x2468 && mask==1 && want==0 && ms==100);
 return ++mmuWaits==mmuFail ? -1:0;
}
static void gpu_wr64(unsigned offset,uint64_t value){if(value>=0x06000000)last_full_lock=value;assert(offset==0x2450 && (value==(0x05080000ULL|14) || value==(0x05080000ULL|16) || value==(0x05100000ULL|19) || (value>=0x06000013ULL && value<=0x06000018ULL)));}
static void gpu_wr(unsigned offset,uint32_t value){
 if(offset==0x2458){assert(value==2 || value==4);mmuCommands++;return;}
 assert(offset==0x90000 && value==1);doorbells++;
 if(mode==1 || mode==2 || mode==4 || mode==7)return;
 if(mode==8){scratch=expected_completion_token;return;}
 uint64_t old=position(output),next=position(input);
 uint64_t start=(old+63)&~63ULL;
 // Compute commands finish with the same trusted STORE completion marker.
 uint64_t payload=instruction(start)==(23ULL<<56) ?
  (instruction(start+8)==((2ULL<<56)|(74ULL<<48)) ? (instruction(start+32)==(23ULL<<56)?64*8:27*8):
   instruction(start+8)==((1ULL<<56)|(72ULL<<48)) ? 60*8:24*8):24;
 if(full_job_under_test) {
  assert(full_job_under_test->mapping->active);
  for(size_t i=0;i<full_job_under_test->request->l3_count;i++)
   assert(full_l2[FS_L2_FIRST+i]==(full_job_under_test->request->l3[i].page.pa|FS_PTE_L3));
 } else if(payload==64*8) {
  for(unsigned i=0;i<65;i++) {
   uint64_t entry=(uint64_t)table[(256+i)*2]|(uint64_t)table[(256+i)*2+1]<<32;
   assert(entry==((0x882000000ULL+i*4096)|((i==2||i==3)?0x4d7ULL:0x40000000000457ULL)));
  }
 }
 assert(next==((start+payload+63)&~63ULL));
 for(uint64_t pos=old;pos<start;pos+=8)assert(instruction(pos)==0);
 for(uint64_t pos=start+payload;pos<next;pos+=8)assert(instruction(pos)==0);
 start+=payload-24;
 assert(instruction(start)==((1ULL<<56)|(TEGU_GPU_QUEUE_VA+0x3800)));
 uint64_t move=instruction(start+8);
 assert((move>>32)==((2ULL<<24)|(2ULL<<16)));
 assert(instruction(start+16)==((21ULL<<56)|(2ULL<<48)|(1ULL<<16)));
 if(full_mode==22)full_l2[FS_L2_FIRST]^=4096;
 scratch=(uint32_t)move;
 output[0]=input[0];output[1]=input[1];co[2]=ci[2];gout[2]=gin[2];
}
static void __attribute__((unused)) gpu_repeat_trace(void) {}
#include "tegu_gpu_submit.inc"
static void setup(unsigned m){
 diag_log_count=completion_wait_calls=scratch_completion_waits=output_completion_waits=0;
 stop_snapshot_checks=shared_mapping_lookups=0;expected_completion_token=0;
 require_snapshot_before_stop=0;status_refresh_ack=1;status_refresh_waits=status_refresh_rings=status_refresh_ring_calls=0;
 memset(code,0,sizeof(code));memset(desc,0,sizeof(desc));memset(table,0,sizeof(table));memset(tg_pipeline_ptes,0,sizeof(tg_pipeline_ptes));
 memset(tg_render_ptes,0,sizeof(tg_render_ptes));
 tg_compute_prepared=false;tg_compute_mapped=0;mmuWaits=mmuFail=mmuCommands=0;
 memset(ring,0,sizeof(ring));memset(input,0,sizeof(input));memset(output,0,sizeof(output));
 memset(global,0,sizeof(global));memset(group,0,sizeof(group));
 memset(gin,0,sizeof(gin));memset(gout,0,sizeof(gout));
 memset(ci,0,sizeof(ci));memset(co,0,sizeof(co));memset(si,0,sizeof(si));memset(so,0,sizeof(so));
 bounded_wait_ticks=bounded_wait_calls=0;
 global[2]=1;global[3]=2;group[1]=3;group[2]=4;group[0x44/4]=6;group[0x48/4]=5;
 ci[0]=co[0]=si[0]=so[0]=1;so[0x40/4]=0x1111;so[0x44/4]=0x2222;so[0x48/4]=0xbb;so[0x4c/4]=0xcc;so[0x5c/4]=0xdd;so[0x60/4]=0xee;status_refresh_rings=1;tg_queue_live=true;mode=m;stops=restores=doorbells=halts=0;
}
static void test_fullscreen(void);
int main(void){
 test_fullscreen();
 uint32_t value;
 // Trusted command-shaped snapshot: tests publication/fence/control flow only,
 // not GPU interpretation. Real render-state bytes have a separate oracle test.
 uint64_t render_commands[57]={23ULL<<56,(1ULL<<56)|(72ULL<<48)};
 struct tegu_render_job render={.commands=render_commands,.command_count=57,.page_count=65,
  .target_page_start=64,.target_page_count=1};
 for(unsigned i=0;i<65;i++)render.pages[i]=0x882000000ULL+i*4096;
 setup(0);input[0]=output[0]=4088;
 assert(tegu_gpu_render(&render,234,&value)==0&&value==234&&mmuCommands==4);
 for(unsigned i=0;i<65;i++)assert(!table[(256+i)*2]&&!table[(256+i)*2+1]&&!tg_render_ptes[i]);
 assert(tg_queue_live&&!stops);
 // Maximum-sized mapping, read-only duplicate aliases, NX and full cleanup.
 render.page_count=256;
 for(unsigned i=65;i<256;i++)render.pages[i]=0x882000000ULL+i*4096;
 render.pages[66]=render.pages[65];
 setup(0);assert(!tg_render_map(&render));
 for(unsigned i=0;i<256;i++) {
  uint64_t entry=(uint64_t)table[(256+i)*2]|(uint64_t)table[(256+i)*2+1]<<32;
  uint64_t flags=(i==2||i==3)?0x4d7ULL:i>=65?0x400000000004d7ULL:0x40000000000457ULL;
  assert(entry==(render.pages[i]|flags));
 }
 assert(!tg_render_map(0));
 for(unsigned i=0;i<256;i++)assert(!table[(256+i)*2]&&!table[(256+i)*2+1]&&!tg_render_ptes[i]);
 setup(0);render.pages[65]=render.pages[64];
 assert(tg_render_map(&render)==-82&&!mmuCommands);
 assert(tegu_gpu_render(&render,234,&value)==-40&&!mmuCommands);
 render.pages[65]=0x882041000ULL;render.page_count=64;
 assert(tegu_gpu_render(&render,234,&value)==-40&&!mmuCommands);
 render.page_count=257;assert(tegu_gpu_render(&render,234,&value)==-40&&!mmuCommands);
 render.page_count=66;
 assert(!tegu_gpu_render(&render,234,&value)&&value==234);
 for(unsigned i=0;i<256;i++)assert(!tg_render_ptes[i]);
 render.page_count=65;
 for(unsigned fail=1;fail<=6;fail++) {
  setup(0);mmuFail=fail;assert(tegu_gpu_render(&render,234,&value)!=0);
  assert(!tg_queue_live&&stops==1&&restores==1);
 }
 setup(2);assert(tegu_gpu_render(&render,234,&value)==-100&&!restores&&!tg_queue_live);
 assert(tg_render_ptes[0]&&tg_render_ptes[64]);
 setup(4);assert(tegu_gpu_render(&render,234,&value)==-100&&!restores&&!tg_queue_live);
 setup(0);render.pages[64]=render.pages[0];
 assert(tegu_gpu_render(&render,234,&value)==-40&&!mmuCommands&&!doorbells);
 render.pages[64]=0x882040001ULL;
 assert(tegu_gpu_render(&render,234,&value)==-40&&!mmuCommands);
 render.pages[64]=0x882040000ULL;render.command_count=129;
 assert(tegu_gpu_render(&render,234,&value)==-40&&!mmuCommands);
 render.command_count=57;render.commands=0;
 assert(tegu_gpu_render(&render,234,&value)==-40&&!mmuCommands);

 struct tegu_pipeline_job job={.info={.registers=32,.push_words=43,.bindings=7,.local={1,1,1},.preload=1ULL<<57},
 .code_pa=0x882000000ULL,.pages={0x882004000ULL,0x882008000ULL,0x88200c000ULL},
 .offsets={128,128,0},.lengths={1024,1024,4},.groups={256,1,1}};
 setup(0);assert(tegu_gpu_dispatch(&job,123,&value)==0 && value==123);
 assert(!table[144*2] && !table[160*2] && !table[167*2] && mmuCommands==4);
 assert(pipeline_desc[0]==0x80000018u && pipeline_desc[1]==512 && pipeline_desc[2]==0x05090000);
 assert(pipeline_desc[32]==0x050a0080 && pipeline_desc[36]==0x050a1080 && pipeline_desc[38]==1024);
 assert(pipeline_desc[96]==256);
 for(unsigned fail=1;fail<=6;fail++) {
  setup(0);mmuFail=fail;assert(tegu_gpu_dispatch(&job,123,&value)!=0);
  assert(!tg_queue_live && stops==1);
 }
 setup(2);assert(tegu_gpu_dispatch(&job,123,&value)==-100 && !restores);
 assert(tg_pipeline_ptes[0] && tg_pipeline_ptes[3]);
 setup(0);job.groups[0]=65537;assert(tegu_gpu_dispatch(&job,123,&value)==-40 && !mmuCommands);
 job.groups[0]=256;job.lengths[0]=4096;assert(tegu_gpu_dispatch(&job,123,&value)==-40 && !mmuCommands);
 setup(0);assert(tg_compute_prepare()==0);
 assert(tg_compute_prepare()==-61);
 assert(tegu_gpu_compute_ready());
 assert(tegu_gpu_compute(0x881fff000ULL,0,123,&value)==-40);
 assert(tegu_gpu_compute(0x882000001ULL,0,123,&value)==-40);
 assert(tegu_gpu_compute(0x882000000ULL,3076,123,&value)==-40);
 assert(mmuCommands==0);
 assert(tegu_gpu_compute(0x882000000ULL,128,123,&value)==0 && value==123);
 assert(!pte[0] && !pte[1] && !tg_compute_mapped && mmuCommands==4);
 assert(desc[32]==123 && desc[33]==4 && desc[34]==0x05080080 && !desc[35]);
 for(unsigned fail=1;fail<=6;fail++){
  setup(0);assert(!tg_compute_prepare());mmuFail=fail;
  assert(tegu_gpu_compute(0x882000000ULL,0,123,&value)!=0);
  assert(!tg_queue_live && stops==1 && restores==1);
 }
 setup(2);assert(!tg_compute_prepare());
 assert(tegu_gpu_compute(0x882000000ULL,0,123,&value)==-100);
 assert(!tg_queue_live && !restores && tg_compute_mapped);

 setup(0);assert(tegu_gpu_store(1,0)==-40 && doorbells==0);
 // Several full ring wraps and a 32-bit producer-position carry.
 input[0]=output[0]=0xffffffe8u;
 for(unsigned i=0;i<1024;i++){
  uint32_t token=0x12340000u+i;
  assert(tegu_gpu_store(token,&value)==0 && value==token);
 }
 assert(doorbells==1024 && stops==0 && tg_queue_live);
 setup(1);assert(tegu_gpu_store(1,&value)==-44 && value==0);
 assert(stops==1 && restores==1 && halts==1 && !tg_queue_live);
 assert(tegu_gpu_store(1,&value)==-41 && stops==1 && doorbells==1);
 setup(2);assert(tegu_gpu_store(1,&value)==-100);
 assert(stops==1 && restores==0 && !tg_queue_live);
 setup(4);assert(tegu_gpu_store(1,&value)==-100);
 assert(halts==1 && stops==0 && restores==0 && !tg_queue_live);
 setup(3);assert(tegu_gpu_store(1,&value)==-42 && doorbells==0);
 assert(stops==1 && restores==1 && halts==1 && !tg_queue_live);
 setup(5);assert(tegu_gpu_store(0x5678,&value)==0 && value==0x5678);
 assert(doorbells==1 && stops==0 && tg_queue_live);
 setup(6);assert(tegu_gpu_store(1,&value)==-42 && doorbells==0);
 assert(stops==1 && !tg_queue_live);
 // Real tg_submit failure-label path: scratch timeout must skip output wait.
 setup(7);require_snapshot_before_stop=1;expected_completion_token=0x1234;
 assert(tegu_gpu_store(expected_completion_token,&value)==-44 && value==0);
 assert(completion_wait_calls==1 && scratch_completion_waits==1 && output_completion_waits==0);
 assert(diag_log_count==32 && stop_snapshot_checks>=2);
 assert(diag_log[0].value==(uint64_t)(int64_t)-44);
 assert(diag_log[9].value==0x222200001111ULL && diag_log[10].value==0xbb && diag_log[11].value==0xcc && diag_log[12].value==0xee);
 assert(diag_log[1].value==(uint64_t)(int64_t)-1 && diag_log[2].value==(uint64_t)(int64_t)INT32_MIN);
 assert(diag_log[24].value==0 && diag_log[26].value==0x567800001234ULL && diag_log[27].value==0xa5 && diag_log[28].value==0x11 && diag_log[29].value==0x22 && diag_log[30].value==0x33);
 assert(status_refresh_waits==1 && status_refresh_ring_calls==1);
 assert(!tg_queue_live && stops==1 && restores==1);
 // Scratch token succeeds; output extract timeout is recorded separately.
 setup(8);require_snapshot_before_stop=1;expected_completion_token=0x5678;status_refresh_ack=0;
 assert(tegu_gpu_store(expected_completion_token,&value)==-44 && value==0);
 assert(completion_wait_calls==2 && scratch_completion_waits==1 && output_completion_waits==1);
 assert(diag_log_count==32 && stop_snapshot_checks>=2);
 assert(diag_log[0].value==(uint64_t)(int64_t)-44);
 assert(diag_log[9].value==0x222200001111ULL && diag_log[10].value==0xbb && diag_log[11].value==0xcc && diag_log[12].value==0xee);
 assert(diag_log[1].value==0 && diag_log[2].value==(uint64_t)(int64_t)-1);
 assert(diag_log[24].value==(uint64_t)(int64_t)-1 && diag_log[26].value==UINT64_MAX && diag_log[27].value==UINT64_MAX);
 assert(status_refresh_waits==1 && status_refresh_ring_calls==2); /* refresh request, then ordinary stop ring */
 assert(!tg_queue_live && stops==1 && restores==1);
 // Missing parent mapping fails early; nested pointers remain NULL/sentinel.
 setup(9);require_snapshot_before_stop=1;
 assert(tegu_gpu_store(0x9abc,&value)==-100 && value==0);
 assert(diag_log_count==32 && stop_snapshot_checks==1 && !tg_queue_live);
 assert(diag_log[0].value==(uint64_t)(int64_t)-42);
 assert(diag_log[7].value==UINT64_MAX && diag_log[8].value==UINT32_MAX);
 assert(diag_log[24].value==(uint64_t)(int64_t)INT32_MIN && diag_log[25].value==UINT64_MAX && diag_log[26].value==UINT64_MAX && status_refresh_waits==0 && status_refresh_ring_calls==0);
 // Successful execution keeps the old result and emits no failure snapshot.
 setup(0);require_snapshot_before_stop=1;
 assert(tegu_gpu_store(0x4321,&value)==0 && value==0x4321);
 assert(diag_log_count==0 && tg_queue_live && stops==0 && doorbells==1);
 assert(status_refresh_waits==0 && status_refresh_ring_calls==0);
 setup(0);input[0]=8;assert(tegu_gpu_store(1,&value)==-43 && doorbells==0);
 assert(stops==1 && !tg_queue_live);
 puts("Queue diagnostics: split completion waits, acknowledged and timed-out status refresh, null-pointer suppression, pre-stop capture and success silence passed");
 puts("Persistent submission producer, wrap, timeout and cleanup checks passed");
}

static int reserved(void *ctx,uint64_t pa){(void)ctx;return pa>=0x880000000ULL&&pa<0x882000000ULL;}
struct full_fixture {
 struct fs_mapping map;
 struct fs_request request;
 struct tegu_full_render_job job;
 struct fs_page scratch[FS_V7_SCRATCH_PAGES],target[FS_MAX_TARGET_PAGES],resources[8],heap[FS_HEAP_PAGES];
 struct fs_pt_page l3[FS_MAX_L3_PAGES];
 uint64_t tables[FS_MAX_L3_PAGES][FS_L3_ENTRIES],hash[FS_HASH_CAPACITY];
 uint8_t roles[FS_HASH_CAPACITY];
 uint64_t commands[57];
};
static struct full_fixture *fixture_heap(unsigned width,unsigned height,unsigned stride,unsigned scratch_pages,unsigned heap_pages)
{
 struct full_fixture *f=calloc(1,sizeof(*f));assert(f);
 struct tegu_fs_surface_layout layout;
 assert(!tegu_fs_surface_layout_with_heap(width,height,stride,0,8,scratch_pages,heap_pages,&layout));
 for(unsigned i=0;i<scratch_pages;i++)f->scratch[i]=(struct fs_page){0x882000000ULL+i*4096,99};
 for(unsigned i=0;i<layout.target_pages;i++)f->target[i]=(struct fs_page){0x883000000ULL+i*4096,99};
 for(unsigned i=0;i<8;i++)f->resources[i]=(struct fs_page){0x885000000ULL+i*4096,99};
 for(unsigned i=0;i<heap_pages;i++)f->heap[i]=(struct fs_page){0x887000000ULL+i*4096,99};
 for(unsigned i=0;i<layout.l3_pages;i++)f->l3[i]=(struct fs_pt_page){{0x886000000ULL+i*4096,99},f->tables[i]};
 f->request=(struct fs_request){.owner=99,.scratch_exec_mask=scratch_pages==FS_V7_SCRATCH_PAGES?FS_V7_SCRATCH_EXEC_ALLOWED:0xc00c,.pa_min=0x882000000ULL,.pa_limit=0x8c0000000ULL,
  .l2_pa_min=TEGU_GPU_QUEUE_ROOT+0x2000,.l2_pa_limit=TEGU_GPU_QUEUE_ROOT+0x3000,
  .target_bytes=(uint32_t)layout.target_bytes,.scratch=f->scratch,.scratch_count=scratch_pages,
  .target=f->target,.target_count=layout.target_pages,.resources=f->resources,.resource_count=8,
  .heap=f->heap,.heap_count=heap_pages,.l3=f->l3,.l3_count=layout.l3_pages,.l2_entries=full_l2,.l2_page_pa=TEGU_GPU_QUEUE_ROOT+0x2000,
  .validation_hash=f->hash,.validation_hash_count=FS_HASH_CAPACITY,
  .validation_roles=f->roles,.validation_roles_count=FS_HASH_CAPACITY,.is_reserved_pa=reserved};
 assert(fs_mapping_init(&f->map));
 f->commands[0]=23ULL<<56;f->commands[1]=(1ULL<<56)|(72ULL<<48);
 f->job=(struct tegu_full_render_job){.mapping=&f->map,.request=&f->request,.commands=f->commands,.command_count=57,
  .owner=99,.target_gpu_va=FS_APERTURE_VA+(uint64_t)scratch_pages*4096,
  .heap_gpu_va=heap_pages?FS_APERTURE_VA+(uint64_t)(scratch_pages+layout.target_pages+8)*4096:0,
  .heap_bytes=heap_pages?0x200000u:0,.timeout_ticks=heap_pages?0x12345u:0,
  .width=width,.height=height,.row_stride=stride,
  .target_page_start=scratch_pages,.target_page_count=layout.target_pages,.page_count=layout.total_pages,.kind=TEGU_FS_TRIANGLE};
 return f;
}
static struct full_fixture *fixture(unsigned w,unsigned h,unsigned stride,unsigned scratch_pages)
{return fixture_heap(w,h,stride,scratch_pages,0);}

static void full_setup(unsigned mode_value)
{
 setup(0);pt_syncs=0;full_mode=mode_value;full_job_under_test=0;memset(full_l2,0,sizeof(full_l2));
}
static void test_fullscreen(void)
{
 uint32_t value;
 full_setup(0);struct tegu_fs_queue_context context={0};
 assert(!tegu_gpu_fullscreen_context(&context));
 assert(context.l2_entries==full_l2&&context.l2_page_pa==0x880102000ULL);
 assert(context.protected_begin==0x880000000ULL&&context.protected_end==0x880154000ULL);
 tg_queue_live=false;memset(&context,0xa5,sizeof(context));struct tegu_fs_queue_context before=context;
 assert(tegu_gpu_fullscreen_context(&context)==-41&&!memcmp(&context,&before,sizeof(context)));
 assert(tegu_gpu_fullscreen_context(0)==-40);
 const unsigned shapes[][3]={{16,16,64},{256,256,1024},{1080,2424,4352},{2048,2048,8192}};
 for(unsigned shape=0;shape<sizeof(shapes)/sizeof(shapes[0]);shape++){
  full_setup(0);struct full_fixture *f=fixture(shapes[shape][0],shapes[shape][1],shapes[shape][2],FS_SCRATCH_PAGES);
  full_job_under_test=&f->job;input[0]=output[0]=0xffffffe8u;
  int result=tegu_gpu_render_fullscreen(&f->job,0x12345678,&value);
  if(result)fprintf(stderr,"fullscreen result=%d shape=%u disposition=%d waits=%u cmds=%u\n",result,shape,f->map.disposition,mmuWaits,mmuCommands);
  assert(!result);
  assert(value==0x12345678&&tg_queue_live&&!stops&&doorbells==1&&mmuCommands==4&&mmuWaits==6);
  assert(f->map.disposition==FS_RELEASED&&!f->map.active);
  assert(pt_syncs==f->request.l3_count);
  unsigned lock_log=20;uint64_t covered=(uint64_t)f->job.page_count*4096;
  while((UINT64_C(1)<<lock_log)<covered)lock_log++;
  assert(last_full_lock==(FS_APERTURE_VA|(lock_log-1)));
  for(unsigned i=0;i<512;i++)assert(!full_l2[i]);
  for(unsigned i=0;i<FS_MAX_L3_PAGES;i++)for(unsigned j=0;j<512;j++)assert(!f->tables[i][j]);
  full_job_under_test=0;free(f);
 }
 /* The v7 profile places the target after all112 scratch pages. Exercise the
  * production BSD submit adapter and mapper together, not a mock-only client. */
 full_setup(0);struct full_fixture *v7=fixture(16,16,64,FS_V7_SCRATCH_PAGES);
 assert(v7->job.target_gpu_va==FS_APERTURE_VA+0x70000&&v7->job.target_page_start==112);
 assert(validate_pages(&v7->request));
 full_job_under_test=&v7->job;input[0]=output[0]=0xffffffe8u;
 int v7result=tegu_gpu_render_fullscreen(&v7->job,0x12345678,&value);
 if(v7result)fprintf(stderr,"v7 submit result=%d target=%llx start=%u scratch=%zu pages=%zu total=%u l3=%zu disposition=%d\n",v7result,(unsigned long long)v7->job.target_gpu_va,v7->job.target_page_start,v7->request.scratch_count,v7->request.target_count,v7->job.page_count,v7->request.l3_count,v7->map.disposition);
 assert(!v7result);
 assert(value==0x12345678&&doorbells==1&&mmuCommands==4&&v7->map.disposition==FS_RELEASED&&!v7->map.active);
 assert(pt_syncs==v7->request.l3_count);
 for(unsigned i=0;i<512;i++)assert(!full_l2[i]);
 full_job_under_test=0;free(v7);
 /* Reject unknown profiles and mismatched scratch/target aliases before MMU
  * programming or queue publication. */
 for(unsigned bad_profile=0;bad_profile<3;bad_profile++) {
  full_setup(0);struct full_fixture *bad=fixture(16,16,64,FS_V7_SCRATCH_PAGES);
  if(bad_profile==0)bad->request.scratch_count=111;
  if(bad_profile==1){bad->job.target_page_start=FS_SCRATCH_PAGES;bad->job.target_gpu_va=FS_APERTURE_VA+(uint64_t)FS_SCRATCH_PAGES*FS_PAGE_SIZE;}
  if(bad_profile==2)bad->job.target_gpu_va+=FS_PAGE_SIZE;
  full_job_under_test=&bad->job;
  assert(tegu_gpu_render_fullscreen(&bad->job,17,&value)!=0);
  assert(!doorbells&&!mmuCommands&&!full_l2[FS_L2_FIRST]);
  full_job_under_test=0;free(bad);
 }
 for(unsigned failure=1;failure<=6;failure++){
  full_setup(0);struct full_fixture *f=fixture(1080,2424,4352,FS_SCRATCH_PAGES);full_job_under_test=&f->job;mmuFail=failure;
  assert(tegu_gpu_render_fullscreen(&f->job,17,&value)!=0);
  assert(!tg_queue_live&&stops==1&&f->map.disposition==FS_QUARANTINE);
  /* Harness frees only simulated memory, never production backing. */
  full_job_under_test=0;free(f);
 }
 for(unsigned fault=21;fault<=22;fault++){
  full_setup(fault);struct full_fixture *f=fixture(1080,2424,4352,FS_SCRATCH_PAGES);full_job_under_test=&f->job;
  assert(tegu_gpu_render_fullscreen(&f->job,17,&value)!=0);
  assert(!tg_queue_live&&stops==1&&f->map.disposition==FS_QUARANTINE&&doorbells==1);
  full_job_under_test=0;free(f);
 }
 full_setup(0);struct full_fixture *f=fixture(1080,2424,4352,FS_SCRATCH_PAGES);full_job_under_test=&f->job;
 input[0]=8;assert(tegu_gpu_render_fullscreen(&f->job,17,&value)!=0);
 assert(!doorbells&&!mmuCommands&&f->map.disposition==FS_QUARANTINE);
 full_job_under_test=0;free(f);
 full_setup(0);f=fixture(1080,2424,4352,FS_SCRATCH_PAGES);full_job_under_test=&f->job;
 input[0]=input[1]=output[0]=output[1]=0xffffffffu;
 assert(tegu_gpu_render_fullscreen(&f->job,17,&value)!=0&&!doorbells&&!mmuCommands);
 full_job_under_test=0;free(f);
 full_setup(0);f=fixture(1080,2424,4352,FS_SCRATCH_PAGES);f->job.owner++;
 assert(tegu_gpu_render_fullscreen(&f->job,17,&value)!=0&&!mmuCommands&&!doorbells);
 free(f);full_setup(0);
 /* Real submit adapter heap profile: 512 validated heap leaves, extended bounded
  * waits for both completion words, post-refresh OOM classification, then the
  * same quarantine/reset path as an ordinary -44 timeout. */
 f=fixture_heap(32,16,128,FS_V7_SCRATCH_PAGES,FS_HEAP_PAGES);
 assert(validate_pages(&f->request));assert(f->job.page_count==633);
 f->job.commands=f->commands;f->job.command_count=57;full_job_under_test=&f->job;
 mode=7;so[0]=1u|(1u<<26);si[0]=1u;input[0]=output[0]=0xffffffe8u;
 assert(tegu_gpu_render_fullscreen(&f->job,0x12345678,&value)==-44);
 assert(bounded_wait_calls==1&&bounded_wait_ticks==0x12345u);
 assert(f->map.disposition==FS_QUARANTINE&&f->map.active&&!tg_queue_live);
 unsigned oom_seen=0;
 for(unsigned i=0;i<diag_log_count;i++)if(!strcmp(diag_log[i].name,"TEGU GPU 2MiB heap TILER_OOM (UINT64_MAX=not classified)")){
   assert(diag_log[i].value==1);oom_seen++;
 }
 assert(oom_seen==1);full_job_under_test=0;free(f);
 /* Token success followed by EXTRACT timeout uses the same bounded budget for
  * the second wait and the same uncertain-completion retention behavior. */
 full_setup(0);f=fixture_heap(32,16,128,FS_V7_SCRATCH_PAGES,FS_HEAP_PAGES);
 full_job_under_test=&f->job;mode=8;expected_completion_token=0x12345678;input[0]=output[0]=0xffffffe8u;
 assert(tegu_gpu_render_fullscreen(&f->job,0x12345678,&value)==-44);
 assert(bounded_wait_calls==2&&bounded_wait_ticks==0x12345u&&
        scratch_completion_waits==1&&output_completion_waits==1);
 assert(f->map.disposition==FS_QUARANTINE&&f->map.active&&!tg_queue_live);
 full_job_under_test=0;free(f);
 puts("PASS production fullscreen submit/MMU callbacks: 64/112 scratch profiles and target aliases, invalid profiles rejected before map, 4 target sizes, ring carry, exact unmap, six MMU timeouts, fault/ownership mismatch quarantine; MMIO doubles only");
}
