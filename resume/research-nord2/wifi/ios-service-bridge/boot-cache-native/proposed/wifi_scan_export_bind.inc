/* Private include immediately after real ncb_native, before its functions.
 * Actual CAP/BASIC worker publishes once; consumers only acquire immutable
 * static RAM. No radio callback, user task, event, MMIO or service registration. */
#include "wifi_scan_export.h"
static struct {unsigned publication;uint64_t captured_us;struct nsb_scan scan;}nse_native;
int nord2_wifi_scan_boot_view(struct nse_view*out)
{
 if(!out)return -1;
 if(__atomic_load_n(&nse_native.publication,__ATOMIC_ACQUIRE)!=2)return 1;
 struct nse_view v={&nse_native.scan,1,nse_native.captured_us};*out=v;return 0;
}
static int nse_after_normal(int result)
{
 unsigned opt=0;uint64_t now=0;
 PE_parse_boot_argn("nord2wifiscanexport",&opt,sizeof(opt));if(opt!=1)return 1;
 const struct nct_session*s=&nct_native.session;const struct ncr_state*r=&ncb_native.rx;
 const struct nsb_scan*scan=&r->scan;
 /* Captured software only after native lease closed. No post-handoff owner
  * callback or off-domain read is added. The actual owning worker is required. */
 if(result||!ncd_native.active||ncd_native.parked||ncd_native.halt||
    ncd_native.worker!=current_thread()||nct_native.active||
    ncb_native.attempted!=1||!ncb_native.enabled||!ncb_native.prepared||ncb_native.refusal||
    !ncb_native.plan.prepared||!nct_native.attempted||
    s->phase!=NCT_DONE||!s->prepared||!s->entered||s->halt||s->error||s->refusal||
    s->basic!=&nbc_native.state||s->cap!=&ncd_native.diag||s->init!=&nwfn_native.init||s->firmware!=&nfp_native||
    s->build_context!=&ncb_native.plan||s->build!=ncb_build||
    s->count!=NCB_COMMANDS||s->completed!=s->count||s->cursor!=s->count||
    s->base_completed!=5||s->first_sequence!=6||s->reserved||
    s->acquired!=s->count||s->released!=s->count||s->available!=s->capacity||
    !nbc_native.state.completed||nbc_native.state.halt||nbc_native.state.error||
    ncd_native.diag.halt||ncd_native.diag.error||nfp_native.state.halt||nfp_native.state.error||
    nwfn_native.halt||nwfn_native.init.halt||nwfn_native.init.transport.halt||nfp_native.halt||
    !nfp_native.ready||nwfn_native.init.transport.completed!=5u+s->count||
    r->init!=&nwfn_native.init||r->session!=s||!r->prepared||!r->entered||r->active||r->halt||r->error||
    !r->scan_requested||!scan->entered||scan->active||scan->sequence!=1||
    scan->count>NSB_RESULTS||scan->count>scan->frames||scan->last<scan->started||
    scan->deadline<=scan->started||!!s->scan_complete!=!!scan->done||
    (scan->timed_out&&!scan->partial)||(!scan->done&&!scan->partial)||
    (scan->done&&(scan->completion.sequence!=scan->sequence||scan->completion.state!=7)))return 1;
 for(unsigned k=0;k<s->count;k++){
  const struct nct_record*q=&s->record[k];
  if(s->entry[k].cid!=ncb_native.plan.entry[k].cid||s->entry[k].bytes!=ncb_native.plan.entry[k].bytes||q->cid!=s->entry[k].cid||q->bytes!=s->entry[k].bytes||q->sequence!=6u+k||
     !q->publication_attempted||!q->published||!q->credit_released||!q->tx_done)return 1;
 }
 if(ncd_native_now(&ncd_native,&now)||now<scan->last)return 1;
 unsigned expected=0;
 if(!__atomic_compare_exchange_n(&nse_native.publication,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 1;
 memcpy(&nse_native.scan,scan,sizeof(*scan));nse_native.captured_us=now;
 __atomic_store_n(&nse_native.publication,2,__ATOMIC_RELEASE);return 0;
}
