/* 0630 register-sequence test of the two-chain helpers that live in nord2_gpu_core.c (nord2_gpu_hw.o is a CI member, not rebuilt):
 * n2hw_js_stop_chain, n2hw_js_evict_next, n2hw_job_irq_rb against a register log; the order is kbase_job_done's / kbase_gpu_irq_evict's. */
#include "../p10/b/bsd/dev/nord2_gpu_core.c"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d %s\n", __func__, __LINE__, #x); exit(1); } } while (0)
/* link stubs: the core's other hardware calls are not exercised here */
enum n2hw_error n2hw_as_configure(struct n2hw*h,unsigned a,uint64_t r){(void)h;(void)a;(void)r;return N2HW_OK;}
enum n2hw_error n2hw_as_flush(struct n2hw*h,unsigned a,uint64_t v,uint64_t n,bool m){(void)h;(void)a;(void)v;(void)n;(void)m;return N2HW_OK;}
enum n2hw_error n2hw_as_disable(struct n2hw*h,unsigned a){(void)h;(void)a;return N2HW_OK;}
enum n2hw_error n2hw_js_start(struct n2hw*h,unsigned s,unsigned a,uint64_t hd,uint64_t af){(void)h;(void)s;(void)a;(void)hd;(void)af;return N2HW_OK;}
void n2hw_js_stop(struct n2hw*h,unsigned s,bool hd){(void)h;(void)s;(void)hd;}
unsigned n2hw_job_irq(struct n2hw*h,struct n2hw_job_event*e,unsigned n){(void)h;(void)e;(void)n;return 0;}
unsigned n2hw_mmu_irq(struct n2hw*h,struct n2hw_mmu_fault*f,unsigned n){(void)h;(void)f;(void)n;return 0;}
enum n2hw_error n2hw_reset(struct n2hw*h){(void)h;return N2HW_OK;}
enum n2hw_error n2hw_dummy_job_wa(struct n2hw*h){(void)h;return N2HW_OK;}
enum n2hw_error n2hw_power_on_l2(struct n2hw*h){(void)h;return N2HW_OK;}
enum n2hw_error n2hw_apply_quirks(struct n2hw*h){(void)h;return N2HW_OK;}
enum n2hw_error n2hw_power_on_cores(struct n2hw*h,uint64_t m){(void)h;(void)m;return N2HW_OK;}

static uint32_t reg[0x4000/4];
struct acc { char rw; uint32_t off, val; } logv[256]; unsigned nlog;
static uint32_t raw_after_clear_or;    /* a failure that lands between the clear and the JS_STATE read */
static uint32_t r(void*c,uint32_t off){(void)c;uint32_t v=reg[off/4];if(off==N2HW_JOB_IRQ_RAWSTAT)v=reg[off/4];logv[nlog++]=(struct acc){'r',off,v};return v;}
static void w(void*c,uint32_t off,uint32_t v){(void)c;logv[nlog++]=(struct acc){'w',off,v};
 if(off==N2HW_JOB_IRQ_CLEAR){reg[N2HW_JOB_IRQ_RAWSTAT/4]&=~v;reg[N2HW_JOB_IRQ_RAWSTAT/4]|=raw_after_clear_or;return;}
 if(off==N2HW_JS_REG(0,N2HW_JS_COMMAND_NEXT)||off==N2HW_JS_REG(1,N2HW_JS_COMMAND_NEXT)){reg[off/4]=v;return;}
 reg[off/4]=v;}
static int find(char rw,uint32_t off){for(unsigned i=0;i<nlog;i++)if(logv[i].rw==rw&&logv[i].off==off)return (int)i;return -1;}
enum n2hw_error n2hw_power_off_cores(struct n2hw *hw){(void)hw;return N2HW_OK;}
int main(void){
 struct n2hw hw={0};hw.ops.read=r;hw.ops.write=w;hw.ctx=NULL;
 /* stop: flag-targeted commands */
 nlog=0;n2hw_js_stop_chain(&hw,1,true,true);CHECK(nlog==1&&logv[0].off==N2HW_JS_REG(1,N2HW_JS_COMMAND)&&logv[0].val==7);
 nlog=0;n2hw_js_stop_chain(&hw,0,true,false);CHECK(logv[0].off==N2HW_JS_REG(0,N2HW_JS_COMMAND)&&logv[0].val==5);
 /* evict: NOP first, then HEAD_NEXT */
 reg[N2HW_JS_REG(1,N2HW_JS_HEAD_NEXT_LO)/4]=0x1000;reg[N2HW_JS_REG(1,N2HW_JS_HEAD_NEXT_HI)/4]=0x10;
 nlog=0;uint64_t h=0;CHECK(n2hw_js_evict_next(&hw,1,&h)&&h==0x1000001000ull);
 CHECK(logv[0].rw=='w'&&logv[0].off==N2HW_JS_REG(1,N2HW_JS_COMMAND_NEXT)&&logv[0].val==0);
 CHECK(find('r',N2HW_JS_REG(1,N2HW_JS_HEAD_NEXT_LO))>0&&reg[N2HW_JS_REG(1,N2HW_JS_HEAD_NEXT_LO)/4]==0&&reg[N2HW_JS_REG(1,N2HW_JS_HEAD_NEXT_HI)/4]==0);
 nlog=0;CHECK(!n2hw_js_evict_next(&hw,1,&h)&&h==0);                 /* started (HEAD_NEXT 0) */
 /* 0668: a taken-back chain gives its JOB_CHAIN_FLAG back; a NEXT that already started does not */
 {bool f0=hw.job_chain_flag[1];reg[N2HW_JS_REG(1,N2HW_JS_HEAD_NEXT_LO)/4]=0x3000;CHECK(n2hw_js_evict_next(&hw,1,&h)&&hw.job_chain_flag[1]==!f0);
  CHECK(!n2hw_js_evict_next(&hw,1,&h)&&hw.job_chain_flag[1]==!f0);hw.job_chain_flag[1]=f0;}
 /* irq: slot 1 failed with a NEXT chain -> evict before the clear; JS_STATE after the clear */
 struct n2hw_job_event_rb ev[16];uint64_t nh[16]={0};nh[1]=0x2000;
 reg[N2HW_JOB_IRQ_RAWSTAT/4]=(1u<<17)|(1u<<0);reg[N2HW_JS_REG(1,N2HW_JS_STATUS)/4]=0x40;reg[N2HW_JS_REG(1,N2HW_JS_HEAD_LO)/4]=0x1040;
 reg[N2HW_JS_REG(1,N2HW_JS_HEAD_NEXT_LO)/4]=0x2000;reg[N2HW_JS_REG(1,N2HW_JS_HEAD_NEXT_HI)/4]=0;reg[N2HW_JOB_IRQ_JS_STATE/4]=0;
 nlog=0;unsigned n=n2hw_job_irq_rb(&hw,nh,ev,16);CHECK(n==2);
 CHECK(ev[0].slot==0&&ev[0].done&&!ev[0].failed&&!ev[0].evicted);
 CHECK(ev[1].slot==1&&ev[1].failed&&ev[1].status==0x40&&ev[1].head==0x1040&&ev[1].evicted&&ev[1].evicted_head==0x2000&&!ev[1].cur_active&&!ev[1].next_busy);
 int nop=-1,clr1=-1;for(unsigned i=0;i<nlog;i++){if(logv[i].rw=='w'&&logv[i].off==N2HW_JS_REG(1,N2HW_JS_COMMAND_NEXT))nop=(int)i;if(logv[i].rw=='w'&&logv[i].off==N2HW_JOB_IRQ_CLEAR&&logv[i].val==(1u<<17))clr1=(int)i;}
 CHECK(nop>=0&&clr1>nop);                                            /* evicted while the failed chain still halts the slot */
 CHECK(reg[N2HW_JOB_IRQ_RAWSTAT/4]==0);
 /* done with the NEXT chain still waiting and the current active: JS_STATE bits */
 reg[N2HW_JOB_IRQ_RAWSTAT/4]=1u<<1;reg[N2HW_JOB_IRQ_JS_STATE/4]=(1u<<1)|(1u<<17);
 n=n2hw_job_irq_rb(&hw,nh,ev,16);CHECK(n==1&&ev[0].done&&ev[0].cur_active&&ev[0].next_busy&&!ev[0].evicted);
 /* kbase race: done read, then the moved-up chain fails before the clear's effect is seen: idle state + new failure bit = still active */
 reg[N2HW_JOB_IRQ_RAWSTAT/4]=1u<<1;reg[N2HW_JOB_IRQ_JS_STATE/4]=0;raw_after_clear_or=1u<<17;
 n=n2hw_job_irq_rb(&hw,nh,ev,16);CHECK(n==1&&ev[0].done&&!ev[0].failed&&ev[0].cur_active);
 CHECK(reg[N2HW_JOB_IRQ_RAWSTAT/4]==(1u<<17));                     /* left for the next round */
 raw_after_clear_or=0;
 /* a failure without a NEXT chain: no NEXT register is touched */
 reg[N2HW_JOB_IRQ_RAWSTAT/4]=1u<<16;reg[N2HW_JS_REG(0,N2HW_JS_STATUS)/4]=0;nlog=0;uint64_t none[16]={0};
 n=n2hw_job_irq_rb(&hw,none,ev,16);CHECK(n==1&&ev[0].failed&&ev[0].status==0x04&&!ev[0].evicted&&find('w',N2HW_JS_REG(0,N2HW_JS_COMMAND_NEXT))<0);
 printf("HW_RB_REGISTER_TEST_OK\n");return 0;}
