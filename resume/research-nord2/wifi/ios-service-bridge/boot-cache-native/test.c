#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include "wifi_normal_backend.h"
#include "wifi_main_wfdma_native.h"
#include "../lifecycle-scan-adapter/v2/lifecycle_scan.h"
static unsigned opt=1;
static void PE_parse_boot_argn(const char*k,void*v,size_t n){assert(!strcmp(k,"nord2wifiscanexport")&&n==sizeof opt);memcpy(v,&opt,n);}
static unsigned thread=1;static unsigned current_thread(void){return thread;}
static struct {unsigned active,parked,halt,worker;struct ncd_state diag;} ncd_native;
static struct {struct nbc_state state;} nbc_native;
static struct {struct nct_session session;unsigned attempted,active;} nct_native;
struct nwfn_native nwfn_native;struct nfp_native nfp_native;
static struct {unsigned attempted,enabled,prepared,refusal;struct ncb_plan plan;struct ncr_state rx;}ncb_native;
int ncb_build(void*a,unsigned n,uint8_t*b,size_t c,size_t*d){(void)a;(void)n;(void)b;(void)c;(void)d;assert(0);return -1;}
static uint64_t time_us=400;static unsigned clock_calls;
static int ncd_native_now(void*c,uint64_t*out){assert(c==&ncd_native);clock_calls++;*out=time_us;return 0;}
#include "proposed/wifi_scan_export_bind.inc"
static void fixture(void){
 memset(&nbc_native,0,sizeof nbc_native);memset(&nse_native,0,sizeof nse_native);memset(&ncd_native,0,sizeof ncd_native);memset(&nct_native,0,sizeof nct_native);memset(&ncb_native,0,sizeof ncb_native);memset(&nwfn_native,0,sizeof nwfn_native);memset(&nfp_native,0,sizeof nfp_native);opt=thread=1;time_us=400;clock_calls=0;
 ncd_native.active=ncd_native.worker=1;nbc_native.state.completed=1;ncb_native.attempted=ncb_native.enabled=ncb_native.prepared=ncb_native.plan.prepared=1;nct_native.attempted=1;nfp_native.ready=1;
 struct nct_session*s=&nct_native.session;s->phase=NCT_DONE;s->prepared=s->entered=1;s->basic=&nbc_native.state;s->cap=&ncd_native.diag;s->init=&nwfn_native.init;s->firmware=&nfp_native;s->build_context=&ncb_native.plan;s->build=ncb_build;s->count=s->completed=s->cursor=NCB_COMMANDS;s->base_completed=5;s->first_sequence=6;s->acquired=s->released=s->count;s->available=s->capacity=100;
 nwfn_native.init.transport.completed=5+s->count;struct ncr_state*r=&ncb_native.rx;r->init=&nwfn_native.init;r->session=s;r->prepared=r->entered=r->scan_requested=1;struct nsb_scan*scan=&r->scan;scan->entered=scan->done=1;scan->sequence=1;scan->started=100;scan->last=200;scan->deadline=300;scan->completion.sequence=1;scan->completion.state=7;scan->completion.completed_channels=22;s->scan_complete=1;
 for(unsigned i=0;i<s->count;i++){ncb_native.plan.entry[i].cid=s->entry[i].cid=(uint8_t)i;ncb_native.plan.entry[i].bytes=s->entry[i].bytes=100;s->record[i].cid=(uint8_t)i;s->record[i].bytes=100;s->record[i].sequence=6+i;s->record[i].publication_attempted=s->record[i].published=s->record[i].credit_released=s->record[i].tx_done=1;}
}
static void refused(void){struct nse_view v,old;memset(&v,0xa5,sizeof v);old=v;assert(nse_after_normal(0)==1&&nse_native.publication==0);assert(nord2_wifi_scan_boot_view(&v)==1&&!memcmp(&v,&old,sizeof v));}
static atomic_int ready,reads,stop;
static void*reader(void*unused){(void)unused;atomic_store(&ready,1);do{struct nse_view v;if(!nord2_wifi_scan_boot_view(&v)){assert(v.scan==&nse_native.scan&&v.generation==1&&v.captured_us==400&&v.scan->completion.completed_channels==22&&!v.scan->count);atomic_fetch_add(&reads,1);}}while(!atomic_load(&stop));struct nse_view final;assert(!nord2_wifi_scan_boot_view(&final)&&final.scan->completion.state==7);return 0;}
int main(void){
 struct nse_view view;fixture();assert(nord2_wifi_scan_boot_view(0)==-1);assert(!nse_after_normal(0)&&clock_calls==1);assert(!nord2_wifi_scan_boot_view(&view)&&view.generation==1&&view.scan->done&&!view.scan->count);assert(nse_after_normal(0)==1);ncb_native.rx.scan.completion.state=0;assert(view.scan->completion.state==7); /* copied, not alias */
 fixture();ncb_native.rx.scan.done=0;ncb_native.rx.scan.partial=1;ncb_native.rx.scan.timed_out=1;nct_native.session.scan_complete=0;assert(!nse_after_normal(0));assert(!nord2_wifi_scan_boot_view(&view)&&view.scan->timed_out&&!view.scan->done);
 for(unsigned bad=0;bad<45;bad++){fixture();switch(bad){case 0:opt=0;break;case 1:thread=2;break;case 2:ncd_native.active=0;break;case 3:ncd_native.parked=1;break;case 4:ncd_native.halt=1;break;case 5:nct_native.active=1;break;case 6:ncb_native.enabled=0;break;case 7:ncb_native.prepared=0;break;case 8:ncb_native.refusal=1;break;case 9:nct_native.session.phase=NCT_HALTED;break;case 10:nct_native.session.halt=1;break;case 11:nct_native.session.basic=0;break;case 12:nct_native.session.cap=0;break;case 13:nct_native.session.init=0;break;case 14:nct_native.session.firmware=0;break;case 15:nct_native.session.build_context=0;break;case 16:nct_native.session.build=0;break;case 17:nct_native.session.completed--;break;case 18:nct_native.session.reserved=1;break;case 19:nct_native.session.released--;break;case 20:nwfn_native.halt=1;break;case 21:nwfn_native.init.transport.completed--;break;case 22:ncb_native.rx.session=0;break;case 23:ncb_native.rx.active=1;break;case 24:ncb_native.rx.halt=1;break;case 25:ncb_native.rx.scan_requested=0;break;case 26:ncb_native.rx.scan.active=1;break;case 27:ncb_native.rx.scan.sequence=2;break;case 28:ncb_native.rx.scan.count=17;break;case 29:ncb_native.rx.scan.completion.sequence=2;break;case 30:ncb_native.rx.scan.completion.state=0;break;case 31:nct_native.session.record[25].tx_done=0;break;case 32:nct_native.session.record[0].credit_released=0;break;case 33:nct_native.session.record[13].bytes++;break;case 34:time_us=199;break;case 35:ncb_native.attempted=0;break;case 36:nbc_native.state.completed=0;break;case 37:nbc_native.state.halt=1;break;case 38:ncd_native.diag.halt=1;break;case 39:nfp_native.state.error=1;break;case 40:nwfn_native.init.halt=1;break;case 41:nwfn_native.init.transport.halt=1;break;case 42:ncb_native.plan.entry[0].cid++;break;case 43:nct_native.session.record[0].sequence++;break;case 44:nct_native.session.cursor--;break;}refused();}
 fixture();assert(nse_after_normal(-1)==1&&!clock_calls);
 fixture();pthread_t t;atomic_store(&ready,0);atomic_store(&reads,0);atomic_store(&stop,0);assert(!pthread_create(&t,0,reader,0));while(!atomic_load(&ready)){}assert(!nse_after_normal(0));atomic_store(&stop,1);assert(!pthread_join(t,0));/* The final acquired read is guaranteed even if publication wins scheduling. */
 struct nils_state life={0};struct nils_ticket ticket;static struct niwa_snapshot output;
 assert(!nord2_wifi_scan_boot_view(&view));assert(!nils_open(&life,view.generation));
 assert(!nils_begin(&life,&ticket,&output));assert(!nils_prepare(&ticket,view.scan,view.captured_us,view.captured_us,0,0,&output));
 assert(!nils_finish(&life,&ticket,&output));uint8_t row[1164];assert(nils_take(&life,view.generation,row,sizeof row)==0);
 nils_close(&life);assert(nils_take(&life,view.generation,row,sizeof row)==-1);
 puts("PASS actual private export binder:45 admission failures, complete-empty/partial immutable copy, one-shot, acquire/release concurrent readers; no MMIO or native service");
}
