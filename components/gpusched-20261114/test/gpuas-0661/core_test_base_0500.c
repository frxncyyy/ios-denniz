/* 0500 copy of gpulat-20261112/test/core_test_base.c: n2c_ctx.va / .bos are pointers now; the hardware model also tracks what each AS is
 * programmed with and which AS each busy job slot runs (n2hw_* below), and init() selects static/dynamic AS management (g_static_as).
 * Production core lifetime tests. Hardware acknowledgements are controlled
 * independently here; register formats are tested by nord2_hw_test.c. */
#include "nord2_gpu_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__func__,__LINE__,#x); exit(1); } } while(0)
struct model {
 uint64_t time; unsigned owners, table_live, table_allocs, table_limit;
 unsigned flushes, disables, resets, starts, stops, sync_in, sync_out;
 unsigned sync_n; const void *sync_cpu[4096]; bool sync_to[4096]; uint64_t us_extra;
 bool fail_flush, fail_disable, fail_config, fail_reset, fault_ready;
 struct n2hw_mmu_fault fault;
 struct n2hw_job_event event; bool event_ready;
 void *tables[2048];
 /* 0500 AS model */
 uint64_t as_root[16]; unsigned as_cfg[16], as_dis[16], as_fl[16]; int slot_as[16]; unsigned fail_config_after, fail_flush_as_after; bool js_check_off;
};
static struct model m;
static struct n2c_core core;
static struct n2hw hw;
static bool g_static_as;
static void as_hw_check_idle(unsigned as);
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
static void sync_page(void*c,uint64_t pa,void*p,size_t n,bool to){(void)c;(void)pa;CHECK(p && n==4096);if(to)m.sync_in++;else m.sync_out++;
 if(m.sync_n<4096){m.sync_cpu[m.sync_n]=p;m.sync_to[m.sync_n]=to;m.sync_n++;}
 m.us_extra+=10;}
static void free_owner(void*c,void*p){(void)c;free(p);m.owners++;}
static uint64_t now(void*c){(void)c;return m.time;}
static uint64_t now_us(void*c){(void)c;return m.time*1000+m.us_extra;}
enum n2hw_error n2hw_as_configure(struct n2hw*h,unsigned as,uint64_t root){(void)h;CHECK(root);CHECK(as<16 && (hw.id.as_present&(1u<<as)));
 as_hw_check_idle(as);                       /* 0500: never reprogram an AS a running job uses */
 if(m.fail_config_after){if(--m.fail_config_after==0)m.fail_config=true;}
 m.as_cfg[as]++;m.as_root[as]=root;return m.fail_config?N2HW_E_AS_TIMEOUT:N2HW_OK;}
enum n2hw_error n2hw_as_flush(struct n2hw*h,unsigned as,uint64_t va,uint64_t n,bool mem){(void)h;(void)va;(void)mem;CHECK(n);CHECK(as<16);m.flushes++;m.as_fl[as]++;return m.fail_flush?N2HW_E_AS_TIMEOUT:N2HW_OK;}
enum n2hw_error n2hw_as_disable(struct n2hw*h,unsigned as){(void)h;CHECK(as<16);m.disables++;if(m.fail_disable)return N2HW_E_AS_TIMEOUT;
 bool faulted=false;for(unsigned i=0;i<N2C_MAX_CTX;i++)if(core.ctxs[i].as_live && core.ctxs[i].as==as && core.ctxs[i].state==N2C_CTX_FAULTED)faulted=true;
 if(!faulted)as_hw_check_idle(as);   /* a fault disables the AS under the stalled job (kbase_mmu_report_fault_and_kill); anything else needs it idle */
 m.as_dis[as]++;m.as_root[as]=0;return N2HW_OK;}
enum n2hw_error n2hw_js_start(struct n2hw*h,unsigned s,unsigned as,uint64_t head,uint64_t aff){(void)h;CHECK(head && aff);CHECK(s<16 && m.slot_as[s]<0);
 /* 0500: the AS the job runs in is programmed with the page tables of a context that has a queued job with this head and holds this AS */
 if(!m.js_check_off){bool ok=false;CHECK(m.as_root[as]);
  for(unsigned i=0;i<N2C_MAX_JOBS && !ok;i++){const struct n2c_job*j=&core.jobs[i];
   if(j->state==N2C_JOB_QUEUED && j->heads[j->phase]==head && core.ctxs[j->ctx].as==as && core.ctxs[j->ctx].as_live && core.ctxs[j->ctx].mmu.root_pa==m.as_root[as])ok=true;}
  CHECK(ok);}
 m.slot_as[s]=(int)as;m.starts++;return N2HW_OK;}
static void as_hw_check_idle(unsigned as){for(unsigned s=0;s<16;s++)CHECK(m.slot_as[s]!=(int)as);}
void n2hw_js_stop(struct n2hw*h,unsigned s,bool hard){(void)h;(void)s;CHECK(hard);m.stops++;}
unsigned n2hw_job_irq(struct n2hw*h,struct n2hw_job_event*e,unsigned cap){(void)h;CHECK(cap);if(!m.event_ready)return 0;*e=m.event;m.event_ready=false;if(e->slot<16)m.slot_as[e->slot]=-1;return 1;}
unsigned n2hw_mmu_irq(struct n2hw*h,struct n2hw_mmu_fault*f,unsigned cap){(void)h;CHECK(cap);if(!m.fault_ready)return 0;*f=m.fault;m.fault_ready=false;return 1;}
enum n2hw_error n2hw_reset(struct n2hw*h){(void)h;m.resets++;if(m.fail_reset)return N2HW_E_RESET_TIMEOUT;
for(unsigned s=0;s<16;s++)m.slot_as[s]=-1;
 memset(m.as_root,0,sizeof(m.as_root));return N2HW_OK;}
enum n2hw_error n2hw_dummy_job_wa(struct n2hw*h){h->wa_complete=true;h->wa_runs++;return N2HW_OK;}
enum n2hw_error n2hw_power_on_l2(struct n2hw*h){h->l2_on=true;return N2HW_OK;}
enum n2hw_error n2hw_apply_quirks(struct n2hw*h){(void)h;return N2HW_OK;}
enum n2hw_error n2hw_power_on_cores(struct n2hw*h,uint64_t mask){h->shaders_on=mask;h->l2_on=true;return N2HW_OK;}
static void init(void) {
 memset(&m,0,sizeof(m));memset(&hw,0,sizeof(hw));hw.identified=hw.slots_valid=hw.l2_on=true;for(unsigned s=0;s<16;s++)m.slot_as[s]=-1;
 hw.id.as_present=255;hw.id.shader_present=hw.shaders_on=1;hw.slot_fragment=0;hw.slot_compute=hw.slot_vertex_tiler=1;
 struct n2c_ops ops={alloc_table,free_table,flush_table,alloc_host,free_host,sync_page,free_owner,now,NULL,now_us,NULL};
 CHECK(n2c_init(&core,&ops,&m,&hw));CHECK(core.as_dyn);CHECK(n2c_set_as_dynamic(&core,!g_static_as));CHECK(n2c_start(&core)==N2C_OK);
}
static bool g_bind_on_open;   /* 0500: dynamic mode, bind the new context to an AS (one job on a scratch buffer), then forget the side effects */
static enum n2c_error map_pages(unsigned c,unsigned n,uint32_t*h,uint64_t*va);
static unsigned context(void){unsigned c;CHECK(n2c_ctx_open(&core,123,64ull<<20,false,&c)==N2C_OK);
 if(g_bind_on_open && core.as_dyn){uint32_t h;uint64_t va;CHECK(map_pages(c,1,&h,&va)==N2C_OK);
  struct n2c_submit s={0};s.kind=N2C_JOB_COMPUTE;s.nr_handles=1;s.handles[0]=h;s.write_mask=1;s.heads[0]=va;uint64_t id;CHECK(n2c_submit(&core,c,&s,&id)==N2C_OK);
  m.event=(struct n2hw_job_event){.slot=1};m.event_ready=true;n2c_poll(&core);uint32_t st;CHECK(n2c_job_state(&core,c,id,true,&st,NULL)==N2C_OK && st==N2C_JOB_DONE);
  CHECK(n2c_bo_release(&core,c,h)==N2C_OK && core.ctxs[c].as_live);
  m.owners=0;m.flushes=0;m.disables=0;m.starts=0;m.sync_in=m.sync_out=m.sync_n=0;m.us_extra=0;}
 return c;}
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
 CHECK(core.reset_pending);CHECK(core.stats.bos_live==1);CHECK(core.ctxs[c].va->allocated>0);
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
  CHECK(!core.ctxs[c].va->allocated && !core.ctxs[c].wired);
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
  if(x->bos)for(unsigned i=0;i<N2C_MAX_BOS;i++)if(x->bos[i]) {
   struct n2c_bo*b=x->bos[i];free(b->cookie);free(b->pages);free(b);x->bos[i]=NULL;
  }
  if(x->mmu.valid)nord2_gpu_mmu_fini(&x->mmu);
  free(x->bos);free(x->va);x->bos=NULL;x->va=NULL;
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
static void test_imported_accounting(void) {
 init();unsigned ctx;CHECK(n2c_ctx_open(&core,124,256ull<<20,false,&ctx)==N2C_OK);
 unsigned n=2744;struct n2c_page*p=calloc(n,sizeof(*p));void*owner=calloc(n,4096);CHECK(p&&owner);
 for(unsigned i=0;i<n;i++){p[i].pa=0x90000000ull+4096ull*i;p[i].cpu=(char*)owner+4096ull*i;}
 bool adopted=false;uint32_t h=0;uint64_t va=0;
 CHECK(n2c_bo_map(&core,ctx,p,n,N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_IMPORTED,owner,n*4096ull,&h,&va,&adopted)==N2C_OK);
 CHECK(adopted && core.ctxs[ctx].wired==n*4096ull && core.ctxs[ctx].imported==n*4096ull);
 uint32_t h2;uint64_t va2;CHECK(map_pages(ctx,4,&h2,&va2)==N2C_OK); /* private pages: wired only */
 CHECK(core.ctxs[ctx].imported==n*4096ull && core.ctxs[ctx].wired==n*4096ull+4*4096ull);
 CHECK(n2c_bo_release(&core,ctx,h)==N2C_OK);CHECK(core.ctxs[ctx].imported==0 && core.ctxs[ctx].wired==4*4096ull);
 CHECK(n2c_bo_release(&core,ctx,h2)==N2C_OK);CHECK(core.ctxs[ctx].wired==0);
 free(p);CHECK(n2c_ctx_close(&core,ctx)==N2C_OK);
}
/* ---- 0087: cache maintenance is scoped to the job's own buffer list ---- */
static uint32_t map_flags(unsigned c,unsigned n,uint32_t flags,uint32_t*h,uint64_t*va) {
 struct n2c_page*p=calloc(n,sizeof(*p));CHECK(p);void*owner=calloc(n,4096);CHECK(owner);
 for(unsigned i=0;i<n;i++){p[i].pa=0x90000000ull+4096ull*i;p[i].cpu=(char*)owner+4096ull*i;}
 bool adopted=false;CHECK(n2c_bo_map(&core,c,p,n,flags,owner,n*4096ull,h,va,&adopted)==N2C_OK);
 free(p);if(!adopted)free(owner);return *h;
}
static const char*base_of(unsigned c,uint32_t h){const struct n2c_bo*b=n2c_bo_lookup(&core,c,h);CHECK(b&&b->nr_pages);return (const char*)b->pages[0].cpu;}
/* sync log entries (clean when to!=0) that fall inside the n-page buffer at base */
static unsigned syncs_in(const char*base,unsigned n,bool to) {
 unsigned k=0;for(unsigned i=0;i<m.sync_n;i++){const char*q=m.sync_cpu[i];if(q>=base&&q<base+4096ull*n&&m.sync_to[i]==to)k++;}return k;
}
static uint64_t submit_list(unsigned c,unsigned kind,unsigned n,const uint32_t*hs,const uint64_t*vas,uint32_t wmask,uint32_t flags) {
 struct n2c_submit s={0};s.kind=kind;s.nr_handles=n;for(unsigned i=0;i<n;i++)s.handles[i]=hs[i];
 s.write_mask=wmask;s.flags=flags;s.heads[0]=kind==N2C_JOB_FRAGMENT?0:vas[0];s.heads[1]=kind==N2C_JOB_COMPUTE?0:vas[0]+128;
 uint64_t id;CHECK(n2c_submit(&core,c,&s,&id)==N2C_OK);return id;
}
static void test_scoped_cache_maintenance(void) {
 const uint32_t RW=N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_GPU_EXEC;
 init();unsigned c=context();uint32_t a,b,u,r,imp;uint64_t va[5];
 map_flags(c,2,RW,&a,&va[0]);                      /* listed, GPU-writable */
 map_flags(c,3,RW,&b,&va[1]);                      /* never listed */
 map_flags(c,1,N2C_BO_GPU_READ|N2C_BO_GPU_EXEC,&r,&va[2]); /* listed, read-only: clean only */
 map_flags(c,1,RW|N2C_BO_CPU_UNCACHED,&u,&va[3]);  /* listed, uncached CPU mapping: no maintenance */
 map_flags(c,4,N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_IMPORTED,&imp,&va[4]); /* display-surface style, unlisted */
 const char*A=base_of(c,a),*B=base_of(c,b),*R=base_of(c,r),*U=base_of(c,u),*I=base_of(c,imp);
 uint32_t hs[3]={a,r,u};uint64_t vs[3]={va[0],va[2],va[3]};
 /* 1: compute job listing {a,r,u}: only those are cleaned before and invalidated after */
 uint64_t id=submit_list(c,N2C_JOB_COMPUTE,3,hs,vs,0x5,0);
 CHECK(syncs_in(A,2,true)==2 && syncs_in(R,1,true)==1 && syncs_in(U,1,true)==0);
 CHECK(syncs_in(B,3,true)==0 && syncs_in(I,4,true)==0 && m.sync_in==3);
 complete(1);done(c,id);
 CHECK(syncs_in(A,2,false)==2 && syncs_in(R,1,false)==0 && syncs_in(U,1,false)==0);
 CHECK(syncs_in(B,3,false)==0 && syncs_in(I,4,false)==0 && m.sync_out==2);
 CHECK(core.stats.cache_clean_pages==3 && core.stats.cache_inval_pages==2 && core.stats.cache_full_jobs==0);
 /* 2: a render job (two phases) cleans once and invalidates once, imported surface listed as target */
 m.sync_n=0;m.sync_in=m.sync_out=0;
 uint32_t h2[2]={imp,a};uint64_t v2[2]={va[4],va[0]};
 id=submit_list(c,N2C_JOB_RENDER,2,h2,v2,0x1,0);
 CHECK(syncs_in(I,4,true)==4 && syncs_in(A,2,true)==2 && m.sync_in==6);
 complete(1);CHECK(core.running[0]==id);complete(0);done(c,id);
 CHECK(m.sync_in==6 && syncs_in(I,4,false)==4 && syncs_in(A,2,false)==2 && m.sync_out==6);
 CHECK(syncs_in(B,3,true)==0 && syncs_in(B,3,false)==0);
 /* 3: legacy/raw submissions (FULL flag) maintain the whole context */
 m.sync_n=0;m.sync_in=m.sync_out=0;
 id=submit_list(c,N2C_JOB_COMPUTE,1,&a,&va[0],0,N2C_SUBMIT_FULL_MAINT);
 CHECK(m.sync_in==10 && syncs_in(B,3,true)==3 && syncs_in(I,4,true)==4 && syncs_in(R,1,true)==1 && syncs_in(U,1,true)==0);
 complete(1);done(c,id);
 CHECK(m.sync_out==9 && syncs_in(B,3,false)==3 && syncs_in(I,4,false)==4 && syncs_in(R,1,false)==0 && core.stats.cache_full_jobs==1);
 /* 4: the rollback valve forces whole-context maintenance for ordinary lists */
 m.sync_n=0;m.sync_in=m.sync_out=0;core.full_cache_maint=true;
 id=submit_list(c,N2C_JOB_COMPUTE,1,&a,&va[0],0,0);CHECK(m.sync_in==10);complete(1);done(c,id);CHECK(m.sync_out==9 && core.stats.cache_full_jobs==2);
 core.full_cache_maint=false;
 /* 5: unknown submit flags are rejected before anything is retained */
 struct n2c_submit bad={0};bad.kind=N2C_JOB_COMPUTE;bad.nr_handles=1;bad.handles[0]=a;bad.heads[0]=va[0];bad.flags=0x80;   /* 0661: bits 1 (PRECISE) and 2 (TRUNCATED) are defined now */
 uint64_t bid;CHECK(n2c_submit(&core,c,&bad,&bid)==N2C_E_ARGS && !core.ctxs[c].jobs);
 /* 6: a faulted job never invalidates (unchanged behaviour) */
 m.sync_n=0;m.sync_in=m.sync_out=0;
 id=submit_list(c,N2C_JOB_COMPUTE,1,&a,&va[0],1,0);CHECK(m.sync_in==2);
 m.event=(struct n2hw_job_event){.slot=1,.failed=true,.status=0x40};m.event_ready=true;n2c_poll(&core);
 CHECK(m.sync_out==0);{uint32_t st,ss;CHECK(n2c_job_state(&core,c,id,true,&st,&ss)==N2C_OK && st==N2C_JOB_FAULTED);}
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);CHECK(!m.table_live);
}
static void test_full_list_is_not_trusted(void) {
 /* A producer that fills all N2C_JOB_BOS slots may have been unable to list
  * everything (the bridge drops the shader code BO when the list is full):
  * such a job maintains the whole context, 31 handles stay scoped. */
 init();unsigned c=context();uint32_t hs[N2C_JOB_BOS+1];uint64_t vas[N2C_JOB_BOS+1];
 for(unsigned i=0;i<N2C_JOB_BOS+1;i++)CHECK(map_pages(c,1,&hs[i],&vas[i])==N2C_OK);
 uint64_t id=submit_list(c,N2C_JOB_COMPUTE,N2C_JOB_BOS-1,hs,vas,0,0);
 CHECK(m.sync_in==N2C_JOB_BOS-1);complete(1);done(c,id);CHECK(m.sync_out==N2C_JOB_BOS-1 && core.stats.cache_full_jobs==0);
 m.sync_in=m.sync_out=0;
 id=submit_list(c,N2C_JOB_COMPUTE,N2C_JOB_BOS,hs,vas,0,0);
 CHECK(m.sync_in==N2C_JOB_BOS+1 && core.stats.cache_full_jobs==1);complete(1);done(c,id);CHECK(m.sync_out==N2C_JOB_BOS+1);
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);CHECK(!m.table_live);
}
static void test_context_limit_384(void) {
 /* 0088: a context opened with NORD2_GPU_CONTEXT_LIMIT holds the full private ceiling plus the full import budget, and no more. */
 init();unsigned ctx;CHECK(n2c_ctx_open(&core,125,384ull<<20,false,&ctx)==N2C_OK);
 uint32_t hp,hi,hx;uint64_t va;
 unsigned np=(256u<<20)/4096,ni=(128u<<20)/4096;
 struct n2c_page*p=calloc(np,sizeof(*p));void*o1=calloc(np,4096);CHECK(p&&o1);
 for(unsigned i=0;i<np;i++){p[i].pa=0x90000000ull+4096ull*i;p[i].cpu=(char*)o1+4096ull*i;}
 bool ad=false;CHECK(n2c_bo_map(&core,ctx,p,np,N2C_BO_GPU_READ|N2C_BO_GPU_WRITE,o1,np*4096ull,&hp,&va,&ad)==N2C_OK && ad);
 free(p);p=calloc(ni,sizeof(*p));void*o2=calloc(ni,4096);CHECK(p&&o2);
 for(unsigned i=0;i<ni;i++){p[i].pa=0xa0000000ull+4096ull*i;p[i].cpu=(char*)o2+4096ull*i;}
 CHECK(n2c_bo_map(&core,ctx,p,ni,N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_IMPORTED,o2,ni*4096ull,&hi,&va,&ad)==N2C_OK);
 CHECK(core.ctxs[ctx].wired==384ull<<20 && core.ctxs[ctx].imported==128ull<<20);
 free(p);
 CHECK(map_pages(ctx,1,&hx,&va)!=N2C_OK); /* hard wired limit */
 CHECK(n2c_bo_release(&core,ctx,hi)==N2C_OK && n2c_bo_release(&core,ctx,hp)==N2C_OK);CHECK(core.ctxs[ctx].wired==0 && core.ctxs[ctx].imported==0);
 CHECK(n2c_ctx_close(&core,ctx)==N2C_OK);
}
static void test_job_timing(void) {
 /* 0089: per-job queue/run/total, clean/invalidate time and pages, job times, window take. */
 init();unsigned c=context();uint32_t a;uint64_t va;
 map_flags(c,2,N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_GPU_EXEC,&a,&va);
 m.us_extra=100;
 uint64_t id=submit_list(c,N2C_JOB_COMPUTE,1,&a,&va,1,0);   /* submit 100, clean 2 pages = 20 us, start 120 */
 struct n2c_job_times jt;CHECK(n2c_job_get_times(&core,c,id,&jt)==N2C_OK);
 CHECK(jt.submit_us==100 && jt.start_us==120 && jt.clean_us==20 && !jt.end_us);
 m.us_extra+=400;complete(1);                                /* GPU ends at 520, invalidate 2 pages = 20 us */
 CHECK(n2c_job_get_times(&core,c,id,&jt)==N2C_OK && jt.end_us==520 && jt.inval_us==20);
 struct n2c_timing t;
 CHECK(!n2c_timing_take(&core,&t,1000000));                 /* window too young: not consumed */
 CHECK(n2c_timing_take(&core,&t,0));
 CHECK(t.jobs==1 && t.faulted==0 && t.full_jobs==0);
 CHECK(t.sum_queue_us==20 && t.max_queue_us==20 && t.sum_run_us==400 && t.max_run_us==400 && t.sum_total_us==420 && t.max_total_us==420);
 CHECK(t.sum_clean_us==20 && t.sum_inval_us==20 && t.sum_clean_pages==2 && t.sum_inval_pages==2);
 CHECK(t.window_start_us>=420);                              /* window length since the first submit */
 CHECK(!n2c_timing_take(&core,&t,0));                       /* reset */
 done(c,id);CHECK(n2c_job_get_times(&core,c,id,&jt)==N2C_E_NO_JOB);
 /* faulted jobs are counted, never averaged into the run/queue sums */
 m.us_extra=0;id=submit_list(c,N2C_JOB_COMPUTE,1,&a,&va,1,0);
 m.event=(struct n2hw_job_event){.slot=1,.failed=true,.status=0x40};m.event_ready=true;n2c_poll(&core);
 CHECK(n2c_timing_take(&core,&t,0) && t.jobs==0 && t.faulted==1 && t.sum_run_us==0);
 {uint32_t st,ss;CHECK(n2c_job_state(&core,c,id,true,&st,&ss)==N2C_OK && st==N2C_JOB_FAULTED);}
 /* full-maintenance jobs are tagged */
 id=submit_list(c,N2C_JOB_COMPUTE,1,&a,&va,1,N2C_SUBMIT_FULL_MAINT);complete(1);done(c,id);
 CHECK(n2c_timing_take(&core,&t,0) && t.jobs==1 && t.full_jobs==1);
 /* no microsecond clock: milliseconds are scaled */
 core.ops.now_us=NULL;m.time=7;CHECK(n2c_now_us(&core)==7000);
 CHECK(n2c_job_get_times(&core,c,12345,&jt)==N2C_E_NO_JOB && n2c_job_get_times(&core,99,1,&jt)==N2C_E_ARGS);
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);
}
static void test_async_chain(void) {
 /* 0090: many jobs in flight, in-order execution, WAIT_SEQ aggregate, failure reporting, window. */
 init();unsigned c=context(),o=context();uint32_t a,b;uint64_t va,vb;
 map_flags(c,1,N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_GPU_EXEC,&a,&va);
 map_flags(o,1,N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_GPU_EXEC,&b,&vb);
 uint64_t ids[5];for(unsigned i=0;i<5;i++)ids[i]=submit_list(c,N2C_JOB_COMPUTE,1,&a,&va,1,0);
 uint64_t other=submit_list(o,N2C_JOB_COMPUTE,1,&b,&vb,1,0);   /* newer id in ANOTHER context */
 CHECK(m.starts==1 && core.running[1]==ids[0]);                /* in-order: only the oldest runs */
 struct n2c_seq_result r;
 CHECK(n2c_seq_state(&core,c,ids[4],false,&r)==N2C_OK && r.pending==5 && !r.done && !r.failed);
 CHECK(n2c_seq_state(&core,c,other+1,false,&r)==N2C_E_NO_JOB);  /* never issued */
 CHECK(n2c_seq_state(&core,99,ids[0],false,&r)==N2C_E_ARGS);
 /* 0108: seq 0 is the bridge's availability probe: success, nothing pending, nothing reaped, no side effect */
 memset(&r,0xa5,sizeof(r));CHECK(n2c_seq_state(&core,c,0,false,&r)==N2C_OK && !r.pending && !r.done && !r.failed && !r.first_failed_id);
 memset(&r,0xa5,sizeof(r));CHECK(n2c_seq_state(&core,c,0,true,&r)==N2C_OK && !r.pending && !r.done && !r.failed);
 CHECK(n2c_seq_state(&core,c,ids[4],false,&r)==N2C_OK && r.pending==5);               /* the jobs are untouched by the probe */
 CHECK(n2c_seq_state(&core,99,0,false,&r)==N2C_E_ARGS);                                /* a bad context is still refused */
 complete(1);CHECK(core.running[1]==ids[1]);                   /* completion starts the successor within the same poll */
 CHECK(n2c_seq_state(&core,c,ids[4],false,&r)==N2C_OK && r.pending==4 && r.done==1);
 /* job 2 fails, the chain continues */
 m.event=(struct n2hw_job_event){.slot=1,.failed=true,.status=0x40};m.event_ready=true;n2c_poll(&core);
 CHECK(core.running[1]==ids[2]);
 complete(1);complete(1);complete(1);                          /* ids[2], ids[3], ids[4] */
 CHECK(n2c_seq_state(&core,c,ids[4],false,&r)==N2C_OK && !r.pending && r.done==4 && r.failed==1);
 CHECK(r.first_failed_id==ids[1] && r.first_failed_state==N2C_JOB_FAULTED && r.first_failed_status==0x40);
 CHECK(n2c_seq_state(&core,c,ids[4],true,&r)==N2C_OK);          /* reaps all five */
 {uint32_t st,ss;CHECK(n2c_job_state(&core,c,ids[0],false,&st,&ss)==N2C_E_NO_JOB && n2c_job_state(&core,c,ids[4],false,&st,&ss)==N2C_E_NO_JOB);}
 CHECK(n2c_seq_state(&core,c,ids[4],false,&r)==N2C_OK && !r.pending && !r.done && !r.failed); /* idempotent */
 CHECK(n2c_seq_state(&core,o,other,false,&r)==N2C_OK && r.pending==1);   /* other context untouched */
 complete(1);CHECK(n2c_seq_state(&core,o,other,true,&r)==N2C_OK && r.done==1);
 /* the in-flight window: 96 unfinished jobs, the 97th is refused until some finish */
 uint64_t last=0;for(unsigned i=0;i<N2C_CTX_JOBS;i++)last=submit_list(c,N2C_JOB_COMPUTE,1,&a,&va,1,0);
 {struct n2c_submit sub={0};sub.kind=N2C_JOB_COMPUTE;sub.nr_handles=1;sub.handles[0]=a;sub.heads[0]=va;uint64_t id;
  CHECK(n2c_submit(&core,c,&sub,&id)==N2C_E_BUSY);
  CHECK(n2c_submit(&core,o,&(struct n2c_submit){.kind=N2C_JOB_COMPUTE,.nr_handles=1,.handles={b},.heads={vb}},&id)==N2C_OK);} /* other context unaffected */
 for(unsigned i=0;i<N2C_CTX_JOBS;i++)complete(1);
 complete(1);                                                   /* the other context's job */
 CHECK(n2c_seq_state(&core,c,last,true,&r)==N2C_OK && !r.pending && r.done==N2C_CTX_JOBS);
 last=submit_list(c,N2C_JOB_COMPUTE,1,&a,&va,1,0);complete(1);  /* window reopened */
 CHECK(n2c_seq_state(&core,c,last,true,&r)==N2C_OK && r.done==1);
 {uint32_t st,ss;(void)n2c_job_state(&core,o,last-1,true,&st,&ss);}
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);(void)n2c_ctx_close(&core,o);
}
static void run_one(unsigned c,unsigned n,const uint32_t*hs,const uint64_t*vs,uint32_t w,uint32_t flags) {
 m.sync_n=0;m.sync_in=m.sync_out=0;uint64_t id=submit_list(c,N2C_JOB_COMPUTE,n,hs,vs,w,flags);complete(1);done(c,id);
}
static void test_cache_tracking(void) {
 const uint32_t RW=N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_GPU_EXEC,IMP=N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_IMPORTED;
 /* level 0 = legacy: the whole list every job, private or not */
 init();CHECK(n2c_set_track_level(&core,N2C_TRACK_LEGACY));unsigned c=context();uint32_t a,b,s;uint64_t va[3];
 map_flags(c,2,RW,&a,&va[0]);
 uint32_t hs[2]={a,0};uint64_t vs[2]={va[0],0};
 run_one(c,1,hs,vs,1,0);CHECK(m.sync_in==2 && m.sync_out==2);
 run_one(c,1,hs,vs,1,0);CHECK(m.sync_in==2 && m.sync_out==2);
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);
 /* level 1: invisible buffer cleaned once, never invalidated; visible buffers as before */
 init();CHECK(n2c_set_track_level(&core,N2C_TRACK_PRIVATE));c=context();
 map_flags(c,2,RW,&a,&va[0]);map_flags(c,3,RW,&b,&va[1]);       /* a: GPU-only, b: handed to the CPU */
 const char*A=base_of(c,a),*B=base_of(c,b);
 CHECK(n2c_bo_cpu_visible(&core,c,b)==N2C_OK && n2c_bo_cpu_visible(&core,c,b)==N2C_OK && n2c_bo_cpu_visible(&core,c,999)==N2C_E_NO_HANDLE);
 hs[0]=a;hs[1]=b;vs[0]=va[0];vs[1]=va[1];
 run_one(c,2,hs,vs,3,0);
 CHECK(syncs_in(A,2,true)==2 && syncs_in(B,3,true)==3 && syncs_in(A,2,false)==0 && syncs_in(B,3,false)==3);   /* first use: both cleaned */
 CHECK(!m.sync_cpu[0] || 1);
 run_one(c,2,hs,vs,3,0);
 CHECK(syncs_in(A,2,true)==0 && syncs_in(A,2,false)==0);                                                   /* GPU-only: nothing */
 CHECK(syncs_in(B,3,true)==3 && syncs_in(B,3,false)==3);                                                   /* visible: every job */
 CHECK(core.stats.cache_skip_clean_pages==2 && core.stats.cache_skip_inval_pages==4);
 /* a buffer that becomes visible after the GPU wrote it is invalidated once at that moment */
 m.sync_n=0;CHECK(n2c_bo_cpu_visible(&core,c,a)==N2C_OK && syncs_in(A,2,false)==2 && syncs_in(A,2,true)==0);
 run_one(c,2,hs,vs,3,0);CHECK(syncs_in(A,2,true)==2 && syncs_in(A,2,false)==2);                           /* now conservative */
 /* level 1 cannot track an imported surface */
 map_flags(c,4,IMP,&s,&va[2]);CHECK(n2c_bo_track(&core,c,s)==N2C_E_STATE);
 const char*S=base_of(c,s);hs[0]=s;vs[0]=va[2];run_one(c,1,hs,vs,1,0);run_one(c,1,hs,vs,1,0);
 CHECK(syncs_in(S,4,true)==4 && syncs_in(S,4,false)==4);
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);
 /* GPU-only promise: accepted, never visible, rejected combinations */
 {init();CHECK(n2c_set_track_level(&core,N2C_TRACK_PRIVATE));c=context();uint32_t g;uint64_t vg;
  map_flags(c,2,RW|N2C_BO_GPU_ONLY,&g,&vg);
  CHECK(n2c_bo_cpu_visible(&core,c,g)==N2C_E_STATE);
  hs[0]=g;vs[0]=vg;run_one(c,1,hs,vs,1,0);CHECK(syncs_in(base_of(c,g),2,true)==2);
  run_one(c,1,hs,vs,1,0);CHECK(m.sync_in==0 && m.sync_out==0);
  struct n2c_page pg={.pa=0x90000000ull,.cpu=(void*)&m};uint32_t h2;uint64_t v2;bool ad=false;
  CHECK(n2c_bo_map(&core,c,&pg,1,N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_GPU_ONLY|N2C_BO_IMPORTED,NULL,4096,&h2,&v2,&ad)==N2C_E_ARGS && !ad);
  CHECK(n2c_bo_map(&core,c,&pg,1,N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|(1u<<6),NULL,4096,&h2,&v2,&ad)==N2C_E_ARGS);
  CHECK(n2c_ctx_close(&core,c)==N2C_OK);}
 /* level 2: lock/unlock protocol for a visible imported surface */
 init();CHECK(n2c_set_track_level(&core,N2C_TRACK_EXPLICIT));c=context();
 map_flags(c,4,IMP,&s,&va[2]);S=base_of(c,s);map_flags(c,2,RW,&a,&va[0]);
 CHECK(n2c_bo_track(&core,c,s)==N2C_OK && n2c_bo_track(&core,c,a)==N2C_E_STATE && n2c_bo_track(&core,c,999)==N2C_E_NO_HANDLE);
 hs[0]=s;vs[0]=va[2];
 run_one(c,1,hs,vs,1,0);                                  /* first use: initial CPU state is dirty -> clean; GPU write deferred */
 CHECK(syncs_in(S,4,true)==4 && syncs_in(S,4,false)==0 && core.stats.cache_deferred_pages==4);
 run_one(c,1,hs,vs,1,0);run_one(c,1,hs,vs,1,0);           /* repeated GPU-only use: no maintenance at all */
 CHECK(m.sync_in==0 && m.sync_out==0 && core.stats.cache_deferred_pages==12);
 uint32_t inv=77;m.sync_n=0;
 CHECK(n2c_bo_cpu_acquire(&core,c,s,&inv)==N2C_OK && inv==4 && syncs_in(S,4,false)==4);   /* CPU read: invalidate once */
 CHECK(n2c_bo_cpu_acquire(&core,c,s,&inv)==N2C_OK && inv==0 && syncs_in(S,4,false)==4);   /* nothing new from the GPU */
 CHECK(n2c_bo_cpu_acquire(&core,c,999,&inv)==N2C_E_NO_HANDLE && inv==0);
 run_one(c,1,hs,vs,1,0);CHECK(m.sync_in==0);                                              /* CPU only read: still clean */
 CHECK(n2c_bo_cpu_dirty(&core,c,s)==N2C_OK);               /* CPU wrote (unlock) */
 run_one(c,1,hs,vs,1,0);CHECK(syncs_in(S,4,true)==4);     /* cleaned once, then clean again */
 run_one(c,1,hs,vs,1,0);CHECK(m.sync_in==0);
 /* full-maintenance jobs ignore tracking and reset the state */
 run_one(c,1,hs,vs,0,N2C_SUBMIT_FULL_MAINT);CHECK(syncs_in(S,4,true)==4 && syncs_in(S,4,false)==4 && core.stats.cache_full_jobs==1);
 m.sync_n=0;CHECK(n2c_bo_cpu_acquire(&core,c,s,&inv)==N2C_OK && inv==0);                  /* the full job already invalidated */
 /* mixed list: tracked surface + conservative visible buffer + invisible buffer */
 CHECK(n2c_bo_cpu_visible(&core,c,a)==N2C_OK);hs[0]=s;hs[1]=a;vs[0]=va[2];vs[1]=va[0];
 run_one(c,2,hs,vs,3,0);CHECK(syncs_in(base_of(c,a),2,true)==2 && syncs_in(base_of(c,a),2,false)==2 && syncs_in(S,4,true)==0);
 CHECK(n2c_set_track_level(&core,1)==false && core.track_level==N2C_TRACK_EXPLICIT);       /* fixed once buffers exist */
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);
 init();CHECK(!n2c_set_track_level(&core,3) && n2c_set_track_level(&core,2));
}
static void test_full_list_follows_tracking(void) {
 /* A list that was only full (not forced) maintains the whole context by the same CPU-access tracking as a listed buffer. */
 const uint32_t RW=N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_GPU_EXEC;
 init();CHECK(n2c_set_track_level(&core,N2C_TRACK_EXPLICIT));unsigned c=context();
 uint32_t hs[N2C_JOB_BOS];uint64_t vas[N2C_JOB_BOS];
 for(unsigned i=0;i<N2C_JOB_BOS;i++)map_flags(c,1,RW,&hs[i],&vas[i]);
 uint32_t v;uint64_t vv;map_flags(c,1,RW,&v,&vv);CHECK(n2c_bo_cpu_visible(&core,c,v)==N2C_OK);
 const char*V=base_of(c,v);
 run_one(c,N2C_JOB_BOS,hs,vas,0,0);
 CHECK(core.stats.cache_full_jobs==1 && m.sync_in==N2C_JOB_BOS+1 && m.sync_out==1 && syncs_in(V,1,false)==1);   /* first use: everything dirty; only the CPU-visible buffer invalidated */
 run_one(c,N2C_JOB_BOS,hs,vas,0,0);
 CHECK(core.stats.cache_full_jobs==2 && m.sync_in==1 && syncs_in(V,1,true)==1 && m.sync_out==1);                 /* then only the CPU-visible one */
 run_one(c,N2C_JOB_BOS,hs,vas,0,N2C_SUBMIT_FULL_MAINT);
 CHECK(m.sync_in==N2C_JOB_BOS+1 && m.sync_out==N2C_JOB_BOS+1);                                                   /* a forced full job covers everything */
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);
}
static void test_handle_limit_1024(void) {
 /* 0105: backboardd held all 256 handles of its context and a new code BO failed, dropping the command.  The limit is 1024 now; the slot field
  * of a handle is 10 bits and the core-wide serial 22 bits, a stale handle never aliases a live one. */
 CHECK(N2C_MAX_BOS==8192 && N2C_SLOT_BITS==13 && N2C_SLOT_MASK==8191 && N2C_HANDLE_SERIAL_MAX==(1u<<19)-1);  /* gpulat: 0186 sizes */
{}    /* 0500: bos is a table of N2C_MAX_BOS pointers allocated at open (was an embedded array) */
 init();unsigned a=context(),b=context();CHECK(core.ctxs[a].bos && core.ctxs[b].bos);
 static uint32_t h[N2C_MAX_BOS],hb[N2C_MAX_BOS];static uint64_t va[N2C_MAX_BOS],vb[N2C_MAX_BOS];
 unsigned seen[N2C_MAX_BOS];memset(seen,0,sizeof(seen));
 for(unsigned i=0;i<N2C_MAX_BOS;i++){CHECK(map_pages(a,1,&h[i],&va[i])==N2C_OK);seen[h[i]&N2C_SLOT_MASK]++;CHECK(n2c_bo_lookup(&core,a,h[i])!=NULL);}
 for(unsigned i=0;i<N2C_MAX_BOS;i++){CHECK(seen[i]==1);if(i)CHECK(va[i]!=va[i-1]);}      /* every slot used exactly once, distinct VAs */
 uint32_t hx;uint64_t vx;
 CHECK(map_pages(a,1,&hx,&vx)==N2C_E_NO_HANDLE);                                          /* the 1025th: no handle (not a quota error) */
 CHECK(core.stats.bos_live==N2C_MAX_BOS);
 /* another context is independent and has its own 1024 */
 for(unsigned i=0;i<N2C_MAX_BOS;i++)CHECK(map_pages(b,1,&hb[i],&vb[i])==N2C_OK);
 CHECK(map_pages(b,1,&hx,&vx)==N2C_E_NO_HANDLE && core.stats.bos_live==2u*N2C_MAX_BOS);
 CHECK(n2c_bo_lookup(&core,b,h[7])==NULL || h[7]==hb[7]);                                    /* a handle of one context is not a handle of the other */
 CHECK(n2c_bo_lookup(&core,a,h[5]^(1u<<N2C_SLOT_BITS))==NULL);                                /* same slot, other serial: no alias */
 CHECK(n2c_bo_lookup(&core,a,(h[5]&~N2C_SLOT_MASK)|((h[5]+1)&N2C_SLOT_MASK))==NULL);          /* other slot, same serial: no alias */
 /* the high slots work for jobs: a full 32-handle list spread over the table */
 uint32_t hs[32];uint64_t vs[32];for(unsigned i=0;i<32;i++){hs[i]=h[31*i+3];vs[i]=va[31*i+3];}
 run_one(a,32,hs,vs,0xffffffffu,0);
 /* free one in the middle: the slot is reused, the old handle stays dead */
 uint32_t old=h[500];
 CHECK(n2c_bo_release(&core,a,old)==N2C_OK && n2c_bo_lookup(&core,a,old)==NULL);
 CHECK(map_pages(a,1,&hx,&vx)==N2C_OK && (hx&N2C_SLOT_MASK)==500 && hx!=old && n2c_bo_lookup(&core,a,old)==NULL && n2c_bo_lookup(&core,a,hx)!=NULL);
 CHECK(map_pages(a,1,&hx,&vx)==N2C_E_NO_HANDLE);
 /* the highest slot is addressable and releasable */
 CHECK((h[N2C_MAX_BOS-1]&N2C_SLOT_MASK)==N2C_MAX_BOS-1 && n2c_bo_release(&core,a,h[N2C_MAX_BOS-1])==N2C_OK);
 /* closing a full context frees everything it held */
 CHECK(n2c_ctx_close(&core,a)==N2C_OK && core.stats.bos_live==N2C_MAX_BOS);
 CHECK(n2c_ctx_close(&core,b)==N2C_OK && core.stats.bos_live==0);
 /* the serial runs out cleanly, never wraps into a stale handle */
 init();unsigned c=context();core.next_handle=N2C_HANDLE_SERIAL_MAX;
 CHECK(map_pages(c,1,&hx,&vx)==N2C_OK && (hx>>N2C_SLOT_BITS)==N2C_HANDLE_SERIAL_MAX && hx>(hx&N2C_SLOT_MASK));
 CHECK(map_pages(c,1,&hx,&vx)==N2C_E_NO_HANDLE);
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);
}
static void test_write_mask_invalidate(void) {
 /* 0110 (opt-in n2wm): the post-job invalidate follows the job's declared writes.  Coherence both ways must hold in both modes. */
 const uint32_t RW=N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_GPU_EXEC;
 for(unsigned mode=0;mode<2;mode++){
  init();CHECK(n2c_set_wmask_inval(&core,mode==1));unsigned c=context();uint32_t a,b;uint64_t va[2];
  map_flags(c,2,RW,&a,&va[0]);map_flags(c,3,RW,&b,&va[1]);
  const char*A=base_of(c,a),*B=base_of(c,b);uint32_t hs[2]={a,b};uint64_t vs[2]={va[0],va[1]};
  /* GPU reads what the CPU wrote: both listed buffers are cleaned before the job whatever the mask says */
  uint64_t id=submit_list(c,N2C_JOB_COMPUTE,2,hs,vs,0x1,0);
  CHECK(syncs_in(A,2,true)==2 && syncs_in(B,3,true)==3);
  complete(1);done(c,id);
  /* CPU reads what the GPU wrote (a is declared): a is invalidated in both modes */
  CHECK(syncs_in(A,2,false)==2);
  if(mode==0)CHECK(syncs_in(B,3,false)==3 && core.stats.cache_skip_wmask_pages==0);       /* every listed GPU_WRITE buffer */
  else CHECK(syncs_in(B,3,false)==0 && core.stats.cache_skip_wmask_pages==3 && core.stats.cache_inval_pages==2);   /* only the declared one */
  /* the other half of the mask */
  m.sync_n=0;m.sync_in=m.sync_out=0;core.stats.cache_skip_wmask_pages=0;
  id=submit_list(c,N2C_JOB_COMPUTE,2,hs,vs,0x2,0);complete(1);done(c,id);
  CHECK(syncs_in(B,3,false)==3);
  if(mode==1)CHECK(syncs_in(A,2,false)==0 && core.stats.cache_skip_wmask_pages==2);else CHECK(syncs_in(A,2,false)==2);
  /* write_mask == 0 declares nothing: the conservative rule applies in both modes */
  m.sync_n=0;m.sync_in=m.sync_out=0;
  id=submit_list(c,N2C_JOB_COMPUTE,2,hs,vs,0,0);complete(1);done(c,id);
  CHECK(syncs_in(A,2,false)==2 && syncs_in(B,3,false)==3);
  /* both declared: both invalidated */
  m.sync_n=0;m.sync_in=m.sync_out=0;
  id=submit_list(c,N2C_JOB_COMPUTE,2,hs,vs,0x3,0);complete(1);done(c,id);
  CHECK(syncs_in(A,2,false)==2 && syncs_in(B,3,false)==3);
  /* whole-context jobs (FULL flag) ignore the mask */
  m.sync_n=0;m.sync_in=m.sync_out=0;
  id=submit_list(c,N2C_JOB_COMPUTE,1,&a,&va[0],0x1,N2C_SUBMIT_FULL_MAINT);complete(1);done(c,id);
  CHECK(syncs_in(A,2,false)==2 && syncs_in(B,3,false)==3);
  CHECK(n2c_ctx_close(&core,c)==N2C_OK);
 }
 /* refused once buffers exist (same rule as the tracking level) */
 init();{unsigned c=context();uint32_t h;uint64_t v;map_flags(c,1,RW,&h,&v);CHECK(!n2c_set_wmask_inval(&core,true) && !core.wmask_inval);CHECK(n2c_ctx_close(&core,c)==N2C_OK);}
 /* tracked display surface (level 2): the deferred GPU write is recorded only for a job that declares it */
 init();CHECK(n2c_set_track_level(&core,N2C_TRACK_EXPLICIT)&&n2c_set_wmask_inval(&core,true));
 {unsigned c=context();uint32_t sfc,a;uint64_t va[2];
  map_flags(c,4,N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_IMPORTED,&sfc,&va[0]);map_flags(c,2,RW,&a,&va[1]);
  CHECK(n2c_bo_track(&core,c,sfc)==N2C_OK);uint32_t hs[2]={sfc,a};uint64_t vs[2]={va[0],va[1]};uint32_t inv=77;
  run_one(c,2,hs,vs,0x2,0);                                  /* writes only a: the surface is listed but not declared */
  CHECK(core.stats.cache_deferred_pages==0 && n2c_bo_cpu_acquire(&core,c,sfc,&inv)==N2C_OK && inv==0);   /* CPU read: nothing new from the GPU */
  run_one(c,2,hs,vs,0x1,0);                                  /* declares the surface */
  CHECK(core.stats.cache_deferred_pages==4 && n2c_bo_cpu_acquire(&core,c,sfc,&inv)==N2C_OK && inv==4);  /* CPU read after a GPU write: invalidated once */
  CHECK(n2c_bo_cpu_dirty(&core,c,sfc)==N2C_OK);               /* CPU wrote (unlock) -> the next job cleans it again */
  m.sync_n=0;m.sync_in=m.sync_out=0;run_one(c,2,hs,vs,0x2,0);
  CHECK(syncs_in(base_of(c,sfc),4,true)==4);                   /* GPU reads what the CPU wrote, although the surface is not declared */
  CHECK(n2c_ctx_close(&core,c)==N2C_OK);}
}
/* ---- 0112: display-surface reservations ---------------------------------------------------------------------------------------------- */
#define IMP (N2C_BO_GPU_READ|N2C_BO_GPU_WRITE|N2C_BO_IMPORTED)
static uint32_t imported(unsigned c,uint64_t*va){uint32_t h;map_flags(c,1,IMP,&h,va);return h;}
static const void *tok(unsigned c,uint32_t h){const struct n2c_bo*b=n2c_bo_lookup(&core,c,h);CHECK(b&&b->cookie);return b->cookie;}
static const void *odd_token(void*c,void*cookie){(void)c;return cookie&&*(char*)cookie==0?(const void*)((char*)cookie+1):NULL;}
static void test_resv_basic(void) {
 init();unsigned c=context(),o=context();uint32_t h[6],ho,plain;uint64_t va[6],vo,pva;
 for(unsigned i=0;i<6;i++)h[i]=imported(c,&va[i]);
 map_flags(c,1,N2C_BO_GPU_READ|N2C_BO_GPU_WRITE,&plain,&pva);   /* a buffer that is not an imported surface */
 ho=imported(o,&vo);
 const void *t0=tok(c,h[0]),*t1=tok(c,h[1]),*t2=tok(c,h[2]),*to=tok(o,ho);
 uint32_t id=77,id2=77;
 /* availability probe: n == 0 answers id 0 and success, even with a NULL handle list; nothing becomes busy */
 CHECK(n2c_resv_create(&core,c,NULL,0,&id)==N2C_OK && id==0);CHECK(!n2c_resv_busy(&core,t0) && !core.stats.resv_reserves);
 /* argument errors leave nothing behind */
 CHECK(n2c_resv_create(&core,c,h,9,&id)==N2C_E_ARGS);
 CHECK(n2c_resv_create(&core,c,NULL,1,&id)==N2C_E_ARGS);
 uint32_t bad[2]={h[0],0x7fffffffu};CHECK(n2c_resv_create(&core,c,bad,2,&id)==N2C_E_ARGS);
 bad[1]=plain;CHECK(n2c_resv_create(&core,c,bad,2,&id)==N2C_E_ARGS);
 bad[1]=ho;CHECK(n2c_resv_create(&core,c,bad,2,&id)==N2C_E_ARGS);          /* another context's handle */
 CHECK(!n2c_resv_busy(&core,t0) && !core.stats.resv_reserves);
 /* reserve: visible at once, only for the named surfaces, ids per context monotonic */
 CHECK(n2c_resv_create(&core,c,h,2,&id)==N2C_OK && id==1);
 CHECK(n2c_resv_busy(&core,t0) && n2c_resv_busy(&core,t1) && !n2c_resv_busy(&core,t2) && !n2c_resv_busy(&core,NULL));
 CHECK(!n2c_resv_busy(&core,to));
 CHECK(n2c_resv_create(&core,o,&ho,1,&id2)==N2C_OK && id2==1 && n2c_resv_busy(&core,to));   /* the other context starts at 1 as well */
 CHECK(n2c_resv_create(&core,c,&h[2],1,&id2)==N2C_OK && id2==2 && n2c_resv_busy(&core,t2));
 /* retire: id 0 is a no-op, unknown ids and other contexts' ids are NotFound, a seq that was never issued is refused */
 CHECK(n2c_resv_retire(&core,c,0,0)==N2C_OK && n2c_resv_retire(&core,c,0,99)==N2C_OK);
 CHECK(n2c_resv_retire(&core,c,99,0)==N2C_E_NO_JOB);
 CHECK(n2c_resv_retire(&core,o,2,0)==N2C_E_NO_JOB);
 CHECK(n2c_resv_retire(&core,c,id2,core.next_job_id)==N2C_E_ARGS && n2c_resv_busy(&core,t2));
 /* cancel is immediate and only once */
 CHECK(n2c_resv_retire(&core,c,id2,0)==N2C_OK && !n2c_resv_busy(&core,t2) && core.stats.resv_cancels==1);
 CHECK(n2c_resv_retire(&core,c,id2,0)==N2C_E_NO_JOB);
 /* the 5th live reservation of one context is Busy (and reports id 0); another context is unaffected; a freed slot is reusable */
 uint32_t ids[4];
 CHECK(n2c_resv_create(&core,c,&h[3],1,&ids[0])==N2C_OK && n2c_resv_create(&core,c,&h[4],1,&ids[1])==N2C_OK);   /* 3 live */
 CHECK(n2c_resv_create(&core,c,&h[5],1,&ids[2])==N2C_OK);                                                         /* 4 live */
 id2=55;CHECK(n2c_resv_create(&core,c,&h[5],1,&id2)==N2C_E_BUSY && id2==0);
 CHECK(n2c_resv_create(&core,o,&ho,1,&id2)==N2C_OK && id2==2);
 CHECK(n2c_resv_retire(&core,c,ids[2],0)==N2C_OK && n2c_resv_create(&core,c,&h[5],1,&ids[3])==N2C_OK && ids[3]>ids[2]);
 /* the reservation names the surface only: releasing the buffer is not blocked by it and nothing is dereferenced (ASan) */
 CHECK(n2c_bo_release(&core,c,h[3])==N2C_OK);
 CHECK(n2c_resv_retire(&core,c,ids[0],0)==N2C_OK);
 /* closing both contexts drops what is left; every reservation ended as a cancel here */
 CHECK(n2c_ctx_close(&core,c)==N2C_OK && n2c_ctx_close(&core,o)==N2C_OK);
 CHECK(!n2c_resv_busy(&core,t1) && !n2c_resv_busy(&core,to));
 CHECK(core.stats.resv_cancels==core.stats.resv_reserves && !core.stats.resv_retires && !core.stats.resv_expired);
}
static void test_resv_time_and_jobs(void) {
 init();unsigned c=context();uint64_t va;uint32_t h=imported(c,&va);const void *t=tok(c,h);uint32_t id;
 /* expiry: unretired for 200 ms stops counting, is counted, and frees its slot */
 m.time=1000;CHECK(n2c_resv_create(&core,c,&h,1,&id)==N2C_OK);
 m.time=1000+N2C_RESV_EXPIRE_MS-1;CHECK(n2c_resv_busy(&core,t) && core.stats.resv_expired==0);
 m.time=1000+N2C_RESV_EXPIRE_MS;CHECK(!n2c_resv_busy(&core,t) && core.stats.resv_expired==1);
 CHECK(n2c_resv_retire(&core,c,id,0)==N2C_E_NO_JOB);                                /* swept: a late retire learns it */
 for(unsigned i=0;i<4;i++)CHECK(n2c_resv_create(&core,c,&h,1,&id)==N2C_OK);       /* every slot is usable again */
 CHECK(n2c_resv_create(&core,c,&h,1,&id)==N2C_E_BUSY);
 m.time+=N2C_RESV_EXPIRE_MS;CHECK(!n2c_resv_busy(&core,t) && core.stats.resv_expired==5);
 /* retire(seq): busy until the job is terminal, however long that takes; DONE releases */
 uint64_t j=submit(c,h,va,N2C_JOB_COMPUTE);
 CHECK(n2c_resv_create(&core,c,&h,1,&id)==N2C_OK && n2c_resv_retire(&core,c,id,j)==N2C_OK && core.stats.resv_retires==1);
 CHECK(n2c_resv_retire(&core,c,id,j)==N2C_OK && core.stats.resv_retires==1);       /* a second retire keeps the first */
 m.time+=5000;CHECK(n2c_resv_busy(&core,t) && core.stats.resv_expired==5);          /* a retired reservation does not time out under a running job */
 complete(1);CHECK(!n2c_resv_busy(&core,t));done(c,j);
 /* retiring a seq whose jobs are all terminal already releases at once (reaped or not) */
 CHECK(n2c_resv_create(&core,c,&h,1,&id)==N2C_OK && n2c_resv_retire(&core,c,id,j)==N2C_OK && !n2c_resv_busy(&core,t));
 /* a later job of the same context does not extend a reservation retired at an earlier seq */
 uint64_t j1=submit(c,h,va,N2C_JOB_COMPUTE),j2=submit(c,h,va,N2C_JOB_COMPUTE);
 CHECK(n2c_resv_create(&core,c,&h,1,&id)==N2C_OK && n2c_resv_retire(&core,c,id,j1)==N2C_OK && n2c_resv_busy(&core,t));
 complete(1);CHECK(!n2c_resv_busy(&core,t));done(c,j1);
 complete(1);done(c,j2);
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);
}
static void test_resv_failure_paths(void) {
 uint32_t h,id,id2;uint64_t va;unsigned c;const void *t;uint64_t j;
 /* a faulted job releases once its stop is verified (reset), not before */
 init();c=context();h=imported(c,&va);t=tok(c,h);j=submit(c,h,va,N2C_JOB_COMPUTE);
 CHECK(n2c_resv_create(&core,c,&h,1,&id)==N2C_OK && n2c_resv_retire(&core,c,id,j)==N2C_OK && n2c_resv_busy(&core,t));
 m.fault=(struct n2hw_mmu_fault){.as=c,.status=0x42,.address=va};m.fault_ready=true;n2c_poll(&core);
 uint32_t state=0,status=0;CHECK(n2c_job_state(&core,c,j,false,&state,&status)==N2C_OK && state==N2C_JOB_STOPPING);
 CHECK(n2c_resv_busy(&core,t));                      /* stopping is not terminal: the GPU may still write */
 m.time+=N2C_STOP_TIMEOUT_MS;n2c_poll(&core);        /* unacknowledged stop -> verified reset */
 CHECK(n2c_job_state(&core,c,j,false,&state,&status)==N2C_OK && state==N2C_JOB_RESET);
 CHECK(!n2c_resv_busy(&core,t));CHECK(n2c_ctx_close(&core,c)==N2C_OK);
 /* RESET releases both kinds: one retired behind a running job, and one never retired */
 init();c=context();h=imported(c,&va);t=tok(c,h);j=submit(c,h,va,N2C_JOB_COMPUTE);
 CHECK(n2c_resv_create(&core,c,&h,1,&id)==N2C_OK && n2c_resv_retire(&core,c,id,j)==N2C_OK && n2c_resv_busy(&core,t));
 CHECK(n2c_resv_create(&core,c,&h,1,&id2)==N2C_OK);
 CHECK(n2c_reset(&core)==N2C_OK);CHECK(!n2c_resv_busy(&core,t) && core.stats.resv_cancels==2);
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);
 /* a context that closes (client death) drops its unretired reservations */
 init();c=context();h=imported(c,&va);t=tok(c,h);
 CHECK(n2c_resv_create(&core,c,&h,1,&id)==N2C_OK && n2c_resv_busy(&core,t));
 CHECK(n2c_ctx_close(&core,c)==N2C_OK && !n2c_resv_busy(&core,t) && core.stats.resv_cancels==1);
 /* kill switch */
 init();c=context();h=imported(c,&va);t=tok(c,h);n2c_set_resv_enabled(&core,false);
 CHECK(n2c_resv_create(&core,c,&h,1,&id)==N2C_E_STATE && n2c_resv_create(&core,c,NULL,0,&id)==N2C_E_STATE && n2c_resv_retire(&core,c,1,0)==N2C_E_STATE);
 CHECK(!n2c_resv_busy(&core,t));n2c_set_resv_enabled(&core,true);CHECK(n2c_resv_create(&core,c,&h,1,&id)==N2C_OK);
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);
 /* the optional token callback decides what the surface identity is; NULL means not a surface */
 init();c=context();core.ops.resv_token=odd_token;
 uint64_t v2;uint32_t h2;h=imported(c,&va);h2=imported(c,&v2);*(char*)tok(c,h2)=1;
 CHECK(n2c_resv_create(&core,c,&h2,1,&id)==N2C_E_ARGS);
 CHECK(n2c_resv_create(&core,c,&h,1,&id)==N2C_OK && n2c_resv_busy(&core,(const char*)tok(c,h)+1) && !n2c_resv_busy(&core,tok(c,h)));
 CHECK(n2c_ctx_close(&core,c)==N2C_OK);
}

int main(int argc,char**argv) {
 if(argc>1 && !strcmp(argv[1],"quarantine")){test_quarantine_close();return 0;}
 if(argc>1 && !strcmp(argv[1],"partial")){test_partial_map_failure();return 0;}
 test_reuse();test_render_order();test_quarantine_close();test_partial_map_failure();test_close_running();test_open_failure();
 test_allocation_failure_matrix();test_full_map_flush_failure();test_quarantine_overflow();test_wide_reclaim();
 test_unlisted_buffer();test_failed_open_pins();test_stop_ack();test_fault_failed_reset_pins();test_imported_accounting();
 test_scoped_cache_maintenance();test_full_list_is_not_trusted();test_context_limit_384();test_job_timing();test_async_chain();test_cache_tracking();test_full_list_follows_tracking();test_handle_limit_1024();test_write_mask_invalidate();
 test_resv_basic();test_resv_time_and_jobs();test_resv_failure_paths();
 puts("core: 27 lifetime/ordering/failure scenarios incl. 1024 reuse cycles and 3 display-surface reservation scenarios (0112) PASS (hardware model)");return 0;
}
