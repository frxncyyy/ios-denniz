/* Production core lifetime tests. Hardware acknowledgements are controlled
 * independently here; register formats are tested by nord2_hw_test.c. */
#include "nord2_gpu_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__func__,__LINE__,#x); exit(1); } } while(0)
struct model {
 uint64_t time; unsigned owners, table_live, table_allocs, table_limit;
 unsigned flushes, disables, resets, starts, stops, sync_in, sync_out;
 bool fail_flush, fail_disable, fail_config, fail_reset, fault_ready;
 struct n2hw_mmu_fault fault;
 struct n2hw_job_event event; bool event_ready;
 void *tables[2048];
};
static struct model m;
static struct n2c_core core;
static struct n2hw hw;
static bool alloc_table(void *c,uint64_t *pa,void **cpu) {
 (void)c; if(m.table_limit && m.table_allocs>=m.table_limit)return false;
 for(unsigned i=0;i<2048;i++)if(!m.tables[i]) {
  m.tables[i]=aligned_alloc(4096,4096);CHECK(m.tables[i]);memset(m.tables[i],0,4096);
  *pa=0x80000000ull+i*4096ull;*cpu=m.tables[i];m.table_live++;m.table_allocs++;return true;
 }return false;
}
static void free_table(void*c,uint64_t pa,void*cpu) {
 (void)c;unsigned i=(unsigned)((pa-0x80000000ull)/4096);CHECK(i<2048 && m.tables[i]==cpu);
 free(cpu);m.tables[i]=NULL;m.table_live--;
}
static void flush_table(void*c,uint64_t pa,size_t n){(void)c;(void)pa;(void)n;}
static bool alloc_host(void*c,size_t n,void**p){(void)c;*p=calloc(1,n);return *p!=NULL;}
static void free_host(void*c,void*p,size_t n){(void)c;(void)n;free(p);}
static void sync_page(void*c,uint64_t pa,void*p,size_t n,bool to){(void)c;(void)pa;CHECK(p && n==4096);if(to)m.sync_in++;else m.sync_out++;}
static void free_owner(void*c,void*p){(void)c;free(p);m.owners++;}
static uint64_t now(void*c){(void)c;return m.time;}
enum n2hw_error n2hw_as_configure(struct n2hw*h,unsigned as,uint64_t root){(void)h;(void)as;CHECK(root);return m.fail_config?N2HW_E_AS_TIMEOUT:N2HW_OK;}
enum n2hw_error n2hw_as_flush(struct n2hw*h,unsigned as,uint64_t va,uint64_t n,bool mem){(void)h;(void)as;(void)va;(void)mem;CHECK(n);m.flushes++;return m.fail_flush?N2HW_E_AS_TIMEOUT:N2HW_OK;}
enum n2hw_error n2hw_as_disable(struct n2hw*h,unsigned as){(void)h;(void)as;m.disables++;return m.fail_disable?N2HW_E_AS_TIMEOUT:N2HW_OK;}
enum n2hw_error n2hw_js_start(struct n2hw*h,unsigned s,unsigned as,uint64_t head,uint64_t aff){(void)h;(void)s;(void)as;CHECK(head && aff);m.starts++;return N2HW_OK;}
void n2hw_js_stop(struct n2hw*h,unsigned s,bool hard){(void)h;(void)s;CHECK(hard);m.stops++;}
unsigned n2hw_job_irq(struct n2hw*h,struct n2hw_job_event*e,unsigned cap){(void)h;CHECK(cap);if(!m.event_ready)return 0;*e=m.event;m.event_ready=false;return 1;}
unsigned n2hw_mmu_irq(struct n2hw*h,struct n2hw_mmu_fault*f,unsigned cap){(void)h;CHECK(cap);if(!m.fault_ready)return 0;*f=m.fault;m.fault_ready=false;return 1;}
enum n2hw_error n2hw_reset(struct n2hw*h){(void)h;m.resets++;return m.fail_reset?N2HW_E_RESET_TIMEOUT:N2HW_OK;}
enum n2hw_error n2hw_dummy_job_wa(struct n2hw*h){h->wa_complete=true;h->wa_runs++;return N2HW_OK;}
enum n2hw_error n2hw_power_on_l2(struct n2hw*h){h->l2_on=true;return N2HW_OK;}
enum n2hw_error n2hw_apply_quirks(struct n2hw*h){(void)h;return N2HW_OK;}
enum n2hw_error n2hw_power_on_cores(struct n2hw*h,uint64_t mask){h->shaders_on=mask;h->l2_on=true;return N2HW_OK;}
static void init(void) {
 memset(&m,0,sizeof(m));memset(&hw,0,sizeof(hw));hw.identified=hw.slots_valid=hw.l2_on=true;
 hw.id.as_present=255;hw.id.shader_present=hw.shaders_on=1;hw.slot_fragment=0;hw.slot_compute=hw.slot_vertex_tiler=1;
 struct n2c_ops ops={alloc_table,free_table,flush_table,alloc_host,free_host,sync_page,free_owner,now,NULL};
 CHECK(n2c_init(&core,&ops,&m,&hw));CHECK(n2c_start(&core)==N2C_OK);
}
static unsigned context(void){unsigned c;CHECK(n2c_ctx_open(&core,123,64ull<<20,false,&c)==N2C_OK);return c;}
static enum n2c_error map_pages(unsigned c,unsigned n,uint32_t*h,uint64_t*va) {
 struct n2c_page*p=calloc(n,sizeof(*p));CHECK(p);void*owner=calloc(n,4096);CHECK(owner);
 for(unsigned i=0;i<n;i++){p[i].pa=0x90000000ull+4096ull*i;p[i].cpu=(char*)owner+4096ull*i;}
 bool adopted=false;enum n2c_error e=n2c_bo_map(&core,c,p,n,N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_GPU_EXEC,owner,n*4096ull,h,va,&adopted);
 free(p);if(!adopted)free(owner);return e;
}
static uint64_t submit(unsigned c,uint32_t h,uint64_t va,unsigned kind) {
 struct n2c_submit s={0};s.kind=kind;s.nr_handles=1;s.handles[0]=h;s.write_mask=1;
 s.heads[0]=kind==N2C_JOB_FRAGMENT?0:va;s.heads[1]=kind==N2C_JOB_COMPUTE?0:va+128;
 uint64_t id;CHECK(n2c_submit(&core,c,&s,&id)==N2C_OK);return id;
}
static void complete(unsigned slot){m.event=(struct n2hw_job_event){.slot=slot};m.event_ready=true;n2c_poll(&core);}
static void done(unsigned c,uint64_t id){uint32_t state,status;CHECK(n2c_job_state(&core,c,id,true,&state,&status)==N2C_OK);CHECK(state==N2C_JOB_DONE);}
static void test_reuse(void) {
 init();unsigned c=context(),other=context();core.next_job_id=0x100000000ull;
 for(unsigned i=0;i<1024;i++) {
  uint32_t h;uint64_t va;CHECK(map_pages(c,1,&h,&va)==N2C_OK);
  CHECK(!n2c_bo_lookup(&core,other,h));uint64_t id=submit(c,h,va,N2C_JOB_COMPUTE);CHECK(id>=0x100000000ull);
  CHECK(n2c_job_state(&core,other,id,false,NULL,NULL)==N2C_E_NO_JOB);
  unsigned freed=m.owners;CHECK(n2c_bo_release(&core,c,h)==N2C_E_BUSY);CHECK(m.owners==freed);
  complete(1);done(c,id);CHECK(m.owners==freed+1);CHECK(core.stats.bytes_live==0);
 }
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);CHECK(n2c_ctx_close(&core,other)==N2C_OK);
 CHECK(!m.table_live && !core.stats.bos_live);CHECK(m.sync_out==1024);
}
static void test_render_order(void) {
 init();unsigned c=context();uint32_t h;uint64_t va;CHECK(map_pages(c,1,&h,&va)==N2C_OK);
 /* Independent raw table walk: ordinary BOs must be GPU-inner-shareable,
  * while preserving default memory index, RW and executable permissions. */
 const uint64_t *table=core.ctxs[c].mmu.root_cpu;
 for(unsigned level=0;level<3;level++) {
  uint64_t pte=table[(va>>(39-level*9))&511];CHECK((pte&3)==3);
  uint64_t pa=pte&0xfffffff000ull;CHECK(pa>=0x80000000ull);
  unsigned index=(unsigned)((pa-0x80000000ull)/4096);CHECK(index<2048 && m.tables[index]);table=m.tables[index];
 }
 CHECK((table[(va>>12)&511]&((1ull<<54)|0xfffull))==0x743);
 uint64_t a=submit(c,h,va,N2C_JOB_RENDER),b=submit(c,h,va,N2C_JOB_COMPUTE);CHECK(m.starts==1);
 complete(1);CHECK(core.running[0]==a && !core.running[1]);complete(0);done(c,a);CHECK(core.running[1]==b);
 complete(1);done(c,b);CHECK(n2c_bo_release(&core,c,h)==N2C_OK);CHECK(n2c_ctx_close(&core,c)==N2C_OK);CHECK(!m.table_live);
}
static void test_quarantine_close(void) {
 init();unsigned c=context();uint32_t h;uint64_t va;CHECK(map_pages(c,1,&h,&va)==N2C_OK);
 m.fail_flush=true;CHECK(n2c_bo_release(&core,c,h)==N2C_OK);CHECK(core.nr_quarantine==1 && !m.owners);
 m.fail_flush=false;CHECK(n2c_ctx_close(&core,c)==N2C_OK);CHECK(!m.table_live && m.owners==1 && !core.nr_quarantine);
}
static void test_partial_map_failure(void) {
 init();unsigned c=context();m.table_limit=4; /* root + L1/L2/L3, then fail */
 m.fail_flush=true;uint32_t h=0;uint64_t va=0;CHECK(map_pages(c,513,&h,&va)!=N2C_OK);
 CHECK(core.reset_pending);CHECK(core.stats.bos_live==1);CHECK(core.ctxs[c].va.allocated>0);
 m.fail_flush=false;m.table_limit=0;CHECK(n2c_reset(&core)==N2C_OK);CHECK(core.stats.bos_live==0);
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);CHECK(!m.table_live);
}
static void test_close_running(void) {
 init();unsigned c=context();uint32_t h;uint64_t va;CHECK(map_pages(c,1,&h,&va)==N2C_OK);
 (void)submit(c,h,va,N2C_JOB_COMPUTE);CHECK(n2c_ctx_close(&core,c)==N2C_E_BUSY);CHECK(!m.owners);
 m.time+=N2C_STOP_TIMEOUT_MS+1;CHECK(n2c_ctx_close(&core,c)==N2C_OK);CHECK(m.resets==1 && m.owners==1 && !m.table_live);
}
static void test_open_failure(void) {
 init();m.fail_config=true;unsigned c=999;CHECK(n2c_ctx_open(&core,1,65536,false,&c)==N2C_E_HW);
 CHECK(m.disables>0);CHECK(!m.table_live);
}
static void test_allocation_failure_matrix(void) {
 for(unsigned limit=1;limit<=5;limit++) {
  init();unsigned c=context();m.table_limit=limit;uint32_t h;uint64_t va;
  enum n2c_error e=map_pages(c,513,&h,&va);
  if(e==N2C_OK)CHECK(n2c_bo_release(&core,c,h)==N2C_OK);
  else CHECK(!h && !va);
  CHECK(core.stats.bos_live==0 && core.stats.bytes_live==0);
  CHECK(!core.ctxs[c].va.allocated && !core.ctxs[c].wired);
  CHECK(m.table_live==1); /* all empty intermediate tables reclaimed */
  CHECK(n2c_ctx_close(&core,c)==N2C_OK);CHECK(!m.table_live);
 }
}
static void test_full_map_flush_failure(void) {
 init();unsigned c=context();m.fail_flush=true;uint32_t h;uint64_t va;
 CHECK(map_pages(c,1,&h,&va)==N2C_E_HW);CHECK(!h && !va && !m.owners);
 CHECK(core.ctxs[c].mmu.mapped_pages==1);
 m.fail_flush=false;CHECK(n2c_reset(&core)==N2C_OK);CHECK(m.owners==1);
 CHECK(!core.ctxs[c].mmu.mapped_pages && m.table_live==1);
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);CHECK(!m.table_live);
}
static void test_quarantine_overflow(void) {
 init();unsigned c=context();uint32_t hs[100];uint64_t va;
 for(unsigned i=0;i<100;i++)CHECK(map_pages(c,1,&hs[i],&va)==N2C_OK);
 m.fail_flush=true;
 for(unsigned i=0;i<100;i++)CHECK(n2c_bo_release(&core,c,hs[i])==N2C_OK);
 CHECK(!m.owners && core.stats.bos_live==100);
 m.fail_flush=false;CHECK(n2c_reset(&core)==N2C_OK);
 CHECK(m.owners==100 && !core.stats.bos_live && !core.nr_quarantine);
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);CHECK(!m.table_live);
}
static void test_wide_reclaim(void) {
 init();unsigned c=context();uint32_t h;uint64_t va;
 CHECK(map_pages(c,5000,&h,&va)==N2C_OK);CHECK(m.table_live>8);
 CHECK(n2c_bo_release(&core,c,h)==N2C_OK);CHECK(m.table_live==1);
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);CHECK(!m.table_live);
}
static void test_unlisted_buffer(void) {
 init();unsigned c=context();uint32_t h,unlisted;uint64_t va,uv;
 CHECK(map_pages(c,1,&h,&va)==N2C_OK);CHECK(map_pages(c,1,&unlisted,&uv)==N2C_OK);
 uint64_t job=submit(c,h,va,N2C_JOB_COMPUTE);
 CHECK(n2c_bo_sync(&core,c,h,true)==N2C_E_BUSY);
 CHECK(n2c_bo_release(&core,c,unlisted)==N2C_E_BUSY);CHECK(!m.owners);
 complete(1);done(c,job);CHECK(m.owners==1);
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);CHECK(!m.table_live && m.owners==2);
}
static void test_failed_open_pins(void) {
 init();m.fail_config=m.fail_disable=true;unsigned c=999;
 CHECK(n2c_ctx_open(&core,1,65536,false,&c)==N2C_E_HW);
 CHECK(c==999 && m.table_live==1 && core.reset_pending);
 CHECK(core.ctxs[1].state==N2C_CTX_ZOMBIE && core.ctxs[1].as_live);
 m.fail_config=m.fail_disable=false;CHECK(n2c_reset(&core)==N2C_OK);
 CHECK(!m.table_live && core.ctxs[1].state==N2C_CTX_FREE);
}
static void test_stop_ack(void) {
 init();unsigned c=context();uint32_t h;uint64_t va;CHECK(map_pages(c,1,&h,&va)==N2C_OK);
 m.fail_disable=true;CHECK(n2c_stop(&core)==N2C_E_HW);
 CHECK(core.gpu_powered && core.ctxs[c].as_live && !m.owners);
 m.fail_disable=false;CHECK(n2c_reset(&core)==N2C_OK);CHECK(n2c_stop(&core)==N2C_OK);
 CHECK(!core.ctxs[c].as_live);CHECK(n2c_ctx_close(&core,c)==N2C_OK);CHECK(!m.table_live);
}
static void model_destroy_after_power_cut(void) {
 /* Test-only cleanup AFTER observing an external power cut. Production has
  * no equivalent escape hatch when reset is unacknowledged. */
 for(unsigned c=0;c<N2C_MAX_CTX;c++) {
  struct n2c_ctx*x=&core.ctxs[c];
  for(unsigned i=0;i<N2C_MAX_BOS;i++)if(x->bos[i]) {
   struct n2c_bo*b=x->bos[i];free(b->cookie);free(b->pages);free(b);x->bos[i]=NULL;
  }
  if(x->mmu.valid)nord2_gpu_mmu_fini(&x->mmu);
 }
 CHECK(!m.table_live);
}
static void test_fault_failed_reset_pins(void) {
 init();unsigned c=context();uint32_t h;uint64_t va;CHECK(map_pages(c,1,&h,&va)==N2C_OK);
 (void)submit(c,h,va,N2C_JOB_COMPUTE);CHECK(n2c_bo_release(&core,c,h)==N2C_E_BUSY);
 m.fail_disable=m.fail_reset=true;m.fault=(struct n2hw_mmu_fault){.as=c,.status=0x42,.address=va};m.fault_ready=true;
 n2c_poll(&core);CHECK(core.state==N2C_CORE_BROKEN && core.ctxs[c].as_live);
 CHECK(!m.owners && core.stats.bos_live==1 && m.table_live==4);
 CHECK(n2c_ctx_close(&core,c)==N2C_E_BROKEN);CHECK(!m.owners);
 CHECK(n2c_stop(&core)!=N2C_OK);model_destroy_after_power_cut();
}
int main(int argc,char**argv) {
 if(argc>1 && !strcmp(argv[1],"quarantine")){test_quarantine_close();return 0;}
 if(argc>1 && !strcmp(argv[1],"partial")){test_partial_map_failure();return 0;}
 test_reuse();test_render_order();test_quarantine_close();test_partial_map_failure();test_close_running();test_open_failure();
 test_allocation_failure_matrix();test_full_map_flush_failure();test_quarantine_overflow();test_wide_reclaim();
 test_unlisted_buffer();test_failed_open_pins();test_stop_ack();test_fault_failed_reset_pins();
 puts("core: 14 lifetime/ordering/failure scenarios incl. 1024 reuse cycles PASS (hardware model)");return 0;
}
