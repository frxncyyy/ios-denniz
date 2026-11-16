/* Integration: actual core + MMU + GPU protocol, independently controlled
 * register acknowledgements. No hardware claims; payload is vendor pinned. */
#define main hardware_contract_main
#include "nord2_hw_test.c"
#undef main
#include "nord2_gpu_core.h"
#include "valhall_wa_firmware.h"

struct env {
        struct model m;
        struct n2hw hw;
        struct n2c_core c;
        void *pages[32];
        unsigned live, allocated, fail_alloc, syncs, jobs;
        uint32_t cfg;
        uint64_t affinity;
        bool job_timeout, job_failed, mmu_fault, off_stuck;
};
static struct env e;
static bool at(void *v, uint64_t *pa, void **cpu) {
        struct env *x=v;
        if (x->fail_alloc && ++x->allocated==x->fail_alloc) return false;
        for (unsigned i=0;i<32;i++) if (!x->pages[i]) {
                x->pages[i]=aligned_alloc(4096,4096);
                if (!x->pages[i]) return false;
                memset(x->pages[i],0,4096); *cpu=x->pages[i];
                *pa=0x80000000ull+i*4096; x->live++; return true;
        } return false;
}
static void ft(void*v,uint64_t pa,void*cpu) {
        struct env*x=v; unsigned i=(pa-0x80000000ull)/4096;
        CHECK(i<32 && x->pages[i]==cpu); free(cpu);x->pages[i]=NULL;x->live--;
}
static void flush(void*v,uint64_t pa,size_t n){(void)v;(void)pa;CHECK(n==4096);}
static bool ah(void*v,size_t n,void**p){(void)v;*p=calloc(1,n);return *p!=NULL;}
static void fh(void*v,void*p,size_t n){(void)v;(void)n;free(p);}
static void syncp(void*v,uint64_t pa,void*p,size_t n,bool to){
        struct env*x=v;(void)pa;CHECK(p && n==4096 && to);x->syncs++;
}
static uint64_t time_ms(void*v){(void)v;return 0;}
static uint32_t erd(void*v,uint32_t off){return rd(&((struct env*)v)->m,off);}
static void ewr(void*v,uint32_t off,uint32_t val){
        struct env*x=v;
        wr(&x->m,off,val);
        if(off==N2HW_SHADER_PWROFF_LO && x->off_stuck) x->m.shader_off_pending=0;
        if(off==N2HW_JS_REG(0,N2HW_JS_COMMAND_NEXT) && val==1) {
                x->jobs++; x->cfg=*R(&x->m,N2HW_JS_REG(0,N2HW_JS_CONFIG_NEXT));
                x->affinity=*R(&x->m,N2HW_JS_REG(0,N2HW_JS_AFFINITY_NEXT_LO));
                CHECK(*R(&x->m,N2HW_JS_REG(0,N2HW_JS_HEAD_NEXT_LO))==0);
                CHECK(*R(&x->m,N2HW_JS_REG(0,N2HW_JS_HEAD_NEXT_HI))==1);
                CHECK(x->c.wa_as_live && x->syncs);
                uint64_t pa,pte;
                CHECK(nord2_gpu_mmu_walk(&x->c.wa_mmu,0x100000000ull,&pa,&pte));
                CHECK(pa==x->c.wa_pa && (pte&(1ull<<54))); /* NX like Android */
                CHECK((pte&(3u<<6))==(3u<<6)); /* no GPU write permission */
                CHECK((pte & 0xfff) == 0x4c3); /* vendor flags=6: default, non-shareable */
                CHECK(!memcmp(x->c.wa_cpu,valhall_wa_payload,384));
                if(!x->job_timeout) {
                        *R(&x->m,N2HW_JOB_IRQ_RAWSTAT)|=x->job_failed?0x10000:1;
                        *R(&x->m,N2HW_JS_REG(0,N2HW_JS_COMMAND_NEXT))=0;
                        if(x->mmu_fault) *R(&x->m,N2HW_MMU_IRQ_RAWSTAT)=1;
                }
        }
}
static void delay(void*v,unsigned us){struct env*x=v;x->m.delays+=us;}
static void setup(void) {
        memset(&e,0,sizeof(e)); model_init(&e.m,0x90800011);
        const struct n2hw_ops ho={erd,ewr,delay,NULL};
        CHECK(n2hw_init(&e.hw,&ho,&e));CHECK(n2hw_reset(&e.hw)==N2HW_OK);
        CHECK(n2hw_identify(&e.hw)==N2HW_OK && e.hw.has_issue_ttrx_3485);
        CHECK(n2hw_select_slots(&e.hw)==N2HW_OK);CHECK(n2hw_apply_quirks(&e.hw)==N2HW_OK);
        CHECK(n2hw_power_on_l2(&e.hw)==N2HW_OK && !e.hw.shaders_on);
        const struct n2c_ops co={at,ft,flush,ah,fh,syncp,NULL,time_ms,NULL};
        CHECK(n2c_init(&e.c,&co,&e,&e.hw));
        CHECK(n2c_set_wa_payload(&e.c,valhall_wa_payload,384));
        *R(&e.m,N2HW_GPU_IRQ_MASK)=0x123;*R(&e.m,N2HW_JOB_IRQ_MASK)=0x321;
}
static void cleanup_power_cut(void) {
        /* Only a test process's external power cut permits this on failures. */
        nord2_gpu_mmu_fini(&e.c.wa_mmu);
        if(e.c.wa_cpu) ft(&e,e.c.wa_pa,e.c.wa_cpu);
        CHECK(e.live==0);
}
static void good(void) {
        setup();CHECK(n2c_start(&e.c)==N2C_OK);
        CHECK(e.hw.wa_complete && e.hw.wa_runs==1 && e.jobs==1);
        CHECK(e.cfg==(1u<<15) && e.affinity==N2HW_MT6893_SHADER_PRESENT_9);
        CHECK(e.hw.shaders_on==N2HW_MT6893_SHADER_PRESENT_9 && e.live==5);
        CHECK(*R(&e.m,N2HW_GPU_IRQ_MASK)==0x123 && *R(&e.m,N2HW_JOB_IRQ_MASK)==0x321);
        unsigned ctx=99;CHECK(n2c_ctx_open(&e.c,0,4096,true,&ctx)==N2C_E_STATE && ctx==99);
        CHECK(n2c_reset(&e.c)==N2C_OK);CHECK(e.hw.wa_runs==2 && e.jobs==2 && e.live==5);
        CHECK(n2c_stop(&e.c)==N2C_OK);CHECK(e.live==0 && !e.c.wa_as_live);
        CHECK(n2hw_reset(&e.hw)==N2HW_OK);CHECK(!e.hw.wa_complete);
        CHECK(n2hw_power_on_l2(&e.hw)==N2HW_OK);CHECK(n2c_start(&e.c)==N2C_OK);
        CHECK(e.hw.wa_runs==3 && e.jobs==3);CHECK(n2c_stop(&e.c)==N2C_OK);CHECK(!e.live);
}
int main(void) {
        good();
        setup();e.c.wa_payload=NULL;CHECK(n2c_start(&e.c)!=N2C_OK && !e.jobs && !e.live);
        for(unsigned i=1;i<=5;i++){
                setup();e.fail_alloc=i;CHECK(n2c_start(&e.c)!=N2C_OK);
                CHECK(!e.jobs && !e.live && !e.c.wa_as_live);
        }
        for(unsigned i=0;i<6;i++){
                setup();
                if(i==0)e.job_timeout=true;
                if(i==1)e.job_failed=true;
                if(i==2)e.mmu_fault=true;
                if(i==3)e.off_stuck=true;
                if(i==4)e.m.shader_never=true;
                if(i==5)e.m.as_active_reads=-1;
                CHECK(n2c_start(&e.c)!=N2C_OK && e.c.state==N2C_CORE_BROKEN);
                CHECK(e.live==5 && e.c.wa_as_live && !e.hw.wa_complete);
                CHECK(n2c_stop(&e.c)!=N2C_OK && e.live==5);
                CHECK(e.m.delays<=300000);cleanup_power_cut();
        }
        setup();CHECK(n2c_start(&e.c)==N2C_OK);e.m.as_active_reads=-1;
        CHECK(n2c_stop(&e.c)!=N2C_OK && e.live==5 && e.c.wa_as_live);cleanup_power_cut();
        printf("WA actual core/MMU/protocol: valid start, reset/restart, NX mapping, failures and pinning: %u failures\n",failures);
        return failures?1:0;
}
