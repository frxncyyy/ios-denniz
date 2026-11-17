#define NCD_NATIVE_HOST_TEST 1
#define main core_fixture_main
#include "test-dispatcher.c"
#undef main
#include "../../main-fw-transport/wfdma/native-candidate/wfdma_native.h"
#include <setjmp.h>
struct nwfn_native nwfn_native;
struct nfp_native nfp_native;
static struct {unsigned ready,halt;}nwfn_main;
static struct {uintptr_t host;}npa_native={0x18060000};
typedef uintptr_t thread_t;
typedef int wait_result_t;
typedef int kern_return_t;
typedef void *event_t;
#define THREAD_NULL 0
#define KERN_SUCCESS 0
#define THREAD_UNINT 0
#define THREAD_CONTINUE_NULL 0
static thread_t running_thread=1;
static unsigned flag=1,spawn_error,spawns,deallocations,panic_count,parked;
static void(*worker_entry)(void*,wait_result_t);
static jmp_buf terminal;
static struct mock model;
static thread_t current_thread(void){return running_thread;}
static int nwfn_owner_model(void*c){assert(c==&nwfn_native);if(nwfn_native.halt||nwfn_native.init.halt)return -1;return owner(&model);}
int nwfn_owner(void*c){return nwfn_owner_model(c);}
static int model_read(void*c,uint32_t p,uint32_t*v){assert(c==&nwfn_native);return rr(&model,p,v);}
static int model_write(void*c,uint32_t p,uint32_t v){assert(c==&nwfn_native);return rw(&model,p,v);}
static int model_mread(void*c,uint64_t p,void*v,uint32_t n){assert(c==&nwfn_native);return mr(&model,p,v,n);}
static int model_mwrite(void*c,uint64_t p,const void*v,uint32_t n){assert(c==&nwfn_native);return mw(&model,p,v,n);}
static int model_sync(void*c,uint64_t p,uint32_t n,unsigned w){assert(c==&nwfn_native);return sy(&model,p,n,w);}
static int model_delay(void*c,unsigned us){assert(c==&nwfn_native);return delay(&model,us);}
const struct nwt_io nwfn_io={nwfn_owner,model_read,model_write,model_mread,model_mwrite,model_sync,model_delay};
static uint32_t ni_rd(uintptr_t p,unsigned off){assert(p==npa_native.host&&off==0xf0&&running_thread==2);uint32_t v;assert(!rr(&model,0x180600f0,&v));return v;}
static void clock_get_uptime(uint64_t*t){*t=model.now*1000;}
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t*out){*out=t;}
static void PE_parse_boot_argn(const char*n,unsigned*out,unsigned size){assert(size==4);*out=!strcmp(n,"nord2wificap")?flag:1;}
static kern_return_t kernel_thread_start(void(*entry)(void*,wait_result_t),void*c,thread_t*out){assert(!c);spawns++;if(spawn_error)return 5;worker_entry=entry;*out=2;return 0;}
static void thread_deallocate(thread_t t){assert(t==2);deallocations++;}
static void assert_wait(event_t e,unsigned mode){assert(e&&mode==THREAD_UNINT);parked++;}
static void thread_block(unsigned x){assert(!x);longjmp(terminal,1);}
static void panic(const char*s){assert(strstr(s,"manual reset"));panic_count++;longjmp(terminal,2);}
#include "capability_bind.inc"
static void native_fixture(void){fixture();memset(&ncd_native,0,sizeof(ncd_native));memset(&nwfn_native,0,sizeof(nwfn_native));nwfn_native.init=init;nwfn_native.stage=NWFN_RETAINED;nfp_native=firmware;nfp_native.host_io=npa_native.host;nwfn_main.ready=1;nwfn_main.halt=0;flag=1;spawns=deallocations=spawn_error=panic_count=parked=0;running_thread=1;worker_entry=0;model=(struct mock){.txidx=3,.rxidx=3};}
int main(void){native_fixture();flag=0;ncd_schedule();assert(!spawns&&!model.calls);
 native_fixture();nfp_native.ready=0;ncd_schedule();assert(!spawns&&!model.calls);
 native_fixture();spawn_error=1;ncd_schedule();assert(spawns==1&&!model.calls&&ncd_native.schedule_error==5);ncd_schedule();assert(spawns==1);
 native_fixture();ncd_schedule();assert(spawns==1&&deallocations==1&&worker_entry&&!model.calls);assert(ncd_native_owner(&ncd_native)<0&&!model.calls);running_thread=2;
 int end=setjmp(terminal);if(!end)worker_entry(0,0);assert(end==1&&parked==1&&!panic_count&&ncd_native.parked&&ncd_native.diag.completed&&nwfn_native.init.transport.completed==4);unsigned before_calls=model.calls;running_thread=1;assert(ncd_native_owner(&ncd_native)<0&&model.calls==before_calls);ncd_schedule();assert(spawns==1);
 native_fixture();ncd_schedule();model.fail=12;running_thread=2;end=setjmp(terminal);if(!end)worker_entry(0,0);assert(end==2&&panic_count==1&&ncd_native.halt&&nwfn_native.halt&&nwfn_native.init.halt&&nwfn_native.init.transport.halt&&nfp_native.halt&&!model.after);
 puts("PASS native capability adapter: default off, real receipt gating, deferred dedicated worker, exact worker/pointer ownership, actual clock conversion boundary, static buffers, one-use schedule, wrong-thread refusal, parked retained success and shared-halt/panic on uncertainty. Synthetic model only.");
}
