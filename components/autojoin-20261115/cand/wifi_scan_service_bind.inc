/* Include at end of normal backend binder, before actual CAP worker. */
#ifdef NSS_HOST_TEST
#include "nss_host.h"
#else
#include <IOKit/IOLocks.h>
extern void nord2_wifi_ios_backend_ready(void);
#endif
#include "wifi_scan_service.h"
#include "wifi_ios_join.h"
enum {NSS_FREE,NSS_QUEUED,NSS_RUNNING,NSS_DONE};
struct nss_slot {unsigned state,canceled,callback_busy,retire;uint64_t ticket,generation,captured;void*cookie;nss_completion completion;enum nss_status status;struct nss_request request;struct nsb_scan scan;};
static struct {IOLock*lock;unsigned ready,failed,powered;uint64_t serial,power_epoch;uint8_t scan_sequence;struct nss_info info;struct nss_slot slot[NSS_SLOTS];struct nct_session previous;struct ncr_state previous_rx;struct ncs_frames frames;
 /* 0436: station address. info.mac = address in use (802.11 peer, PTK, output source check, ifnet); factory_mac = board address;
  * fw_mac = what the firmware was last activated with; mac_switch = BSS activate-control commands the next scan sends first
  * (nss_execute_mac). New fields only AFTER the CI-era ones: no existing offset moves. */
 uint8_t factory_mac[6],fw_mac[6],lladdr_old[6],deactivate[NCS_ACTIVATE_BYTES];unsigned mac_switch,build_pre,lladdr_pending,mac_attempts;}nss_native;
#if defined(NSS_HOST_TEST) && !defined(NJS_HOST_TEST)
#define NSS_STATION_TEST_DISABLED 1
static int njs_scan_blocked_locked(void){return 0;}
static int njs_pending_locked(void){return 0;}
static void njs_cache_scan_locked(const struct nsb_scan*s){(void)s;}
static void njs_initialize_locked(void){}
static int njs_owner_work(void){return 0;}
#else
static int nss_return_scans(void);
static int njs_scan_blocked_locked(void);
static int njs_pending_locked(void);
static void njs_cache_scan_locked(const struct nsb_scan*);
static void njs_initialize_locked(void);
#endif
static int nss_available(void){return __atomic_load_n(&nss_native.ready,__ATOMIC_ACQUIRE)==1;}
static struct nss_slot*nss_find(uint64_t ticket){for(unsigned k=0;k<NSS_SLOTS;k++)if(nss_native.slot[k].state&&nss_native.slot[k].ticket==ticket)return &nss_native.slot[k];return 0;}
int nord2_wifi_scan_info(struct nss_info*out){if(!out)return -1;if(!nss_available())return 1;IOLockLock(nss_native.lock);int r=nss_native.failed?1:0;if(!r){*out=nss_native.info;out->powered=nss_native.powered;out->power_epoch=nss_native.power_epoch;out->scanning=0;for(unsigned k=0;k<NSS_SLOTS;k++)if(nss_native.slot[k].state==NSS_RUNNING&&!nss_native.slot[k].canceled)out->scanning=1;}IOLockUnlock(nss_native.lock);return r;}
int nord2_wifi_scan_enqueue(const struct nss_request*q,void*cookie,uint64_t generation,nss_completion completion,uint64_t*ticket){
 if(!q||!cookie||!generation||!completion||!ticket||q->count>NSS_CHANNELS||(q->dwell_tu!=0&&q->dwell_tu!=108))return -1;
 if(!nss_available())return 1;IOLockLock(nss_native.lock);
 if(!nss_native.powered||njs_scan_blocked_locked()){IOLockUnlock(nss_native.lock);return 2;}
 if(nss_native.failed||nss_native.serial==UINT64_MAX){IOLockUnlock(nss_native.lock);return 1;}
 for(unsigned i=0;i<q->count;i++){unsigned found=0;for(unsigned j=0;j<nss_native.info.count;j++)if(q->channels[i].band==nss_native.info.channels[j].band&&q->channels[i].channel==nss_native.info.channels[j].channel)found=1;
  for(unsigned j=0;j<i;j++)if(q->channels[i].band==q->channels[j].band&&q->channels[i].channel==q->channels[j].channel)found=0;
  if(!found){IOLockUnlock(nss_native.lock);return -1;}}
 struct nss_slot*s=0;for(unsigned k=0;k<NSS_SLOTS;k++)if(!nss_native.slot[k].state){s=&nss_native.slot[k];break;}
 if(!s){IOLockUnlock(nss_native.lock);return 2;}
 memset(s,0,sizeof(*s));s->request=*q;s->cookie=cookie;s->generation=generation;s->completion=completion;s->ticket=++nss_native.serial;s->state=NSS_QUEUED;*ticket=s->ticket;
 IOLockUnlock(nss_native.lock);thread_wakeup((event_t)&nss_native);return 0;
}
int nord2_wifi_scan_cancel(uint64_t ticket){if(!ticket||!nss_available())return 1;IOLockLock(nss_native.lock);struct nss_slot*s=nss_find(ticket);int r=s?0:1;if(s)s->canceled=1;IOLockUnlock(nss_native.lock);if(!r)thread_wakeup((event_t)&nss_native);return r;}
int nord2_wifi_scan_view(uint64_t ticket,struct nss_view*out){if(!out)return -1;if(!nss_available())return 1;IOLockLock(nss_native.lock);struct nss_slot*s=nss_find(ticket);int r=1;if(s&&s->state==NSS_DONE&&!s->canceled&&(s->status==NSS_COMPLETE||s->status==NSS_PARTIAL)){*out=(struct nss_view){&s->scan,s->captured,s->generation,s->status};r=0;}IOLockUnlock(nss_native.lock);return r;}
int nord2_wifi_scan_release(uint64_t ticket){if(!ticket||!nss_available())return 1;IOLockLock(nss_native.lock);struct nss_slot*s=nss_find(ticket);int r=1;if(s&&s->state==NSS_DONE){if(s->callback_busy)s->retire=1;else memset(s,0,sizeof(*s));r=0;}IOLockUnlock(nss_native.lock);return r;}
static int nss_build(void*c,unsigned index,uint8_t*out,size_t capacity,size_t*used){if(c!=&nss_native.frames||index||!out||!used||capacity<NCS_SCAN_BYTES)return -1;memcpy(out,nss_native.frames.scan,NCS_SCAN_BYTES);*used=NCS_SCAN_BYTES;return 0;}
static int nss_start(void){
 unsigned opt=0;PE_parse_boot_argn("nord2wifiios",&opt,sizeof(opt));if(opt!=1)return 1;
 const struct nct_session*s=&nct_native.session;struct nni_address address;struct ncfg_channel channels[39];size_t count=0;
 if(nss_native.lock||ncd_native.worker!=current_thread()||!ncd_native.active||ncd_native.parked||ncd_native.halt||
 !ncb_native.prepared||ncb_native.refusal||!ncb_native.plan.prepared||nct_native.active||!nct_native.attempted||
 !nct_done_receipt(s)||s->phase!=NCT_DONE||s->halt||s->error||s->refusal||s->count!=NCB_COMMANDS||s->completed!=s->count||s->cursor!=s->count||
 s->basic!=&nbc_native.state||s->cap!=&ncd_native.diag||s->init!=&nwfn_native.init||s->firmware!=&nfp_native||
 nbc_native.state.halt||nbc_native.state.error||ncd_native.diag.halt||ncd_native.diag.error||nfp_native.state.halt||nfp_native.state.error||
 nwfn_native.halt||nwfn_native.stage!=NWFN_RETAINED||!nwfn_native.ap_owned||!nwfn_native.wf_owned||nwfn_native.asleep||!nwfn_native.init.initialized||nwfn_native.init.halt||nwfn_native.init.transport.halt||nfp_native.halt||!nfp_native.ready||!nfp_native.finish_done||
 ncb_native.rx.halt||ncb_native.rx.error||ncb_native.rx.active||!niwj_scan_usable(&ncb_native.rx.scan)||
 nwfn_native.init.transport.completed!=5u+NCB_COMMANDS||
 ncfg_address(&ncb_native.plan.input,&ncb_native.plan.config,&address)||address.source!=NNI_ADDRESS_BOARD||
 ncfg_channels(&ncb_native.plan.input,&ncb_native.plan.config,channels,39,&count))return 1;
 for(unsigned k=0;k<6;k++)nss_native.info.mac[k]=address.mac[k];
 memcpy(nss_native.factory_mac,address.mac,6);memcpy(nss_native.fw_mac,address.mac,6);
 const struct nrp_caps*c=&ncb_native.plan.input.caps;nss_native.info.band5=c->band5;nss_native.info.nss=c->nss;nss_native.info.max_bw=c->max_bw;nss_native.info.path=c->path;
 /* Android fgSkipDFS defaults0; DFS exclusion is not a station passive-scan rule.
  * Keep the authenticated regulatory intersection; no probe/DFS AP operation. */
 for(unsigned k=0;k<count;k++)if(channels[k].allowed){
  unsigned index=nss_native.info.count++;
  nss_native.info.channels[index]=(struct ncs_channel){channels[k].band,channels[k].channel};
  if(channels[k].dfs)nss_native.info.dfs_mask|=UINT64_C(1)<<index;
 }
 if(!nss_native.info.count||nss_native.info.count!=ncb_native.plan.channel_count)return 1;
 nss_native.scan_sequence=ncb_native.rx.scan.sequence;
 nss_native.lock=IOLockAlloc();if(!nss_native.lock)return 1;
 nss_native.powered=1;nss_native.power_epoch=1;njs_initialize_locked();
 __atomic_store_n(&nss_native.ready,1,__ATOMIC_RELEASE);
 printf("NORD2 WIFI IOS OWNER: active=1 retained=1 boot_complete=1 ready=1\n");
 nord2_wifi_ios_backend_ready();return 0;
}
static struct {struct nct_scan_return origin;struct ncr_queue queue[5];unsigned attempted,ready,consumed;}nss_return;
/* 0436 SCANDIAG: always on, first 24 lines of a boot. Why a scan is refused before/around its firmware command.
 * reason 1 precondition (a = bits worker,!cap_active,cap_halt,session_active,phase!=DONE,session_halt,rx_active,rx_halt; b = phase)
 * 2 ncs_build (b = channels, c = dwell)   3 session prepare (a = rc, b = refusal code, c = returning)   4 RX prepare
 * 5 drain refused (a = quiescent, b = events, c = pumps).   11..15 = the same steps of the address-change scan (d = leading commands).
 * Reasons 3/4/13/14 add the T (transport counters) and R (RX state) rows. Numbers only. */
static unsigned nss_diag_logs;
static void __attribute__((noinline)) nss_diag(unsigned reason,unsigned a,unsigned b,unsigned c,unsigned d){
 if(nss_diag_logs<24){nss_diag_logs++;printf("NORD2 WIFI IOS SCANDIAG: reason=%u a=%#x b=%u c=%u d=%u\n",reason,a,b,c,d);}
}
static void __attribute__((noinline)) nss_diag_state(const struct nct_session*s){
 const struct nwt_state*t=&nwfn_native.init.transport;const struct nct_session*p=&nss_native.previous;const struct ncr_state*x=&nss_native.previous_rx;
 if(nss_diag_logs>=24)return;
 nss_diag_logs+=2;
 printf("NORD2 WIFI IOS SCANDIAG T: completed=%u want=%u tx=%u want=%u seq=%u want=%u flags=%u rx_index=%u receipt=%d refusal=%u\n",t->completed,p->base_completed+p->count,t->tx_index,(p->first_tx+p->count)%1024,(unsigned)t->next_sequence,(unsigned)(uint8_t)(p->first_sequence+p->count),(unsigned)(t->submit_attempted!=0)|(unsigned)(t->tx_done!=0)<<1|(unsigned)(t->rx_done!=0)<<2,t->rx_index,nct_done_receipt(p),s->refusal);
 printf("NORD2 WIFI IOS SCANDIAG R: entered=%u active=%u halt=%u error=%u scan_active=%u command=%u count=%u next1=%u prev_count=%u prev_phase=%u\n",x->entered,x->active,x->halt,x->error,x->scan.active,x->scan_command,s->count,x->queue[1].next,p->count,p->phase);
}
static int nss_build_mac(void*c,unsigned index,uint8_t*out,size_t capacity,size_t*used){
 unsigned pre=nss_native.build_pre;
 if(c!=&nss_native.frames||index>pre||pre>2||!out||!used)return -1;
 if(index<pre){if(capacity<NCS_ACTIVATE_BYTES)return -1;memcpy(out,pre==2&&!index?nss_native.deactivate:nss_native.frames.activate,NCS_ACTIVATE_BYTES);*used=NCS_ACTIVATE_BYTES;return 0;}
 if(capacity<NCS_SCAN_BYTES)return -1;memcpy(out,nss_native.frames.scan,NCS_SCAN_BYTES);*used=NCS_SCAN_BYTES;return 0;
}
/* 0436: the scan that carries a station-address change. Same sequence as nss_execute; the recipe is
 * [BSS activate-control 0x11 (, a second one: deactivate first)] + scan 0x03, exactly the tail of the boot recipe. The prebuilt
 * transport accepts it because its "one scan command" shape branches are removed in the archive copy (build patch 0436);
 * the prebuilt RX prepare records scan index 0, corrected here to the last command. */
static int __attribute__((noinline)) nss_execute_mac(struct nss_slot*slot,const struct ncs_channel*channels,size_t count,uint8_t sequence){
 struct nct_session*s=&nct_native.session;struct ncr_state*r=&ncb_native.rx;
 if(ncd_native.worker!=current_thread()||!ncd_native.active||ncd_native.halt||nct_native.active||s->phase!=NCT_DONE||s->halt||r->active||r->halt){nss_diag(11,0,s->phase,0,0);return 1;}
 uint8_t mac[6];IOLockLock(nss_native.lock);unsigned pre=nss_native.mac_switch>2?0:nss_native.mac_switch;memcpy(mac,nss_native.info.mac,6);IOLockUnlock(nss_native.lock);
 if(ncs_build(ncd_native.diag.frame,ncd_native.diag.frame_bytes,ncd_native.diag.history[ncd_native.diag.events-1].sequence,mac,channels,count,1,2,sequence,1,slot->request.dwell_tu,0,&nss_native.frames)){nss_diag(12,0,(unsigned)count,slot->request.dwell_tu,pre);return 1;}
 /* Deactivate = gen4m nicDeactivateNetworkEx: zeroed command with the BSS index, ucActive 0, network type, own-address index and
  * BMC WLAN index of the activate; no address (0491: was the address the firmware had). */
 if(pre==2){memcpy(nss_native.deactivate,nss_native.frames.activate,NCS_ACTIVATE_BYTES);nss_native.deactivate[65]=0;memset(nss_native.deactivate+68,0,6);}
 nss_native.build_pre=pre;
 nss_native.previous=*s;nss_native.previous_rx=*r;nss_native.previous_rx.session=&nss_native.previous;
 memset(s,0,sizeof(*s));memset(r,0,sizeof(*r));
 static const struct nct_entry entry[3]={{NCS_ACTIVATE_BYTES,0x11,1},{NCS_ACTIVATE_BYTES,0x11,1},{NCS_SCAN_BYTES,3,1}};const struct nct_recipe recipe={entry+(2-pre),1+pre,nss_build_mac,&nss_native.frames,UINT64_C(30000000),UINT64_C(15000000)};
 unsigned returning=nss_return.ready&&!nss_return.consumed;
 int preparation=returning?nct_scan_return_prepare(&nbc_native.state,&nss_return.origin,&recipe,s):nct_continue_prepare(&nbc_native.state,&nss_native.previous,&recipe,s);
 if(!preparation)preparation=returning?ncr_scan_return_prepare(r,&nwfn_native.init,s,nss_return.queue,sequence,UINT64_C(14000000)):ncr_continue_prepare(r,&nwfn_native.init,s,&nss_native.previous_rx,sequence,UINT64_C(14000000));
 if(preparation){nss_diag(s->refusal?13:14,(unsigned)preparation,s->refusal,returning,pre);nss_diag_state(s);nss_native.build_pre=0;*s=nss_native.previous;*r=nss_native.previous_rx;r->session=s;return 1;}
 r->scan_command=pre;
 nct_native.active=1;NVC_PHASE(5);
 const struct nct_hooks hooks={ncr_pump,r,ncb_sleep,&ncb_native};
 int result=nct_run(&ncd_io,&ncd_native,&hooks,s);nct_native.active=0;nss_native.build_pre=0;
 if(result==1&&s->drain_refused){nss_diag(15,s->drain_quiescent,s->events,s->pump_calls,pre);/* RX may already have rearmed stale frames: preserve its current cursors. */
  if(returning)for(unsigned k=0;k<5;k++)nss_return.queue[k]=r->queue[k];
  *s=nss_native.previous;r->session=s;r->phase=NCR_IDLE;r->active=0;return 1;
 }
 if(returning)nss_return.consumed=1;
 nss_native.scan_sequence=sequence;
 if(result>=0){
  IOLockLock(nss_native.lock);memcpy(nss_native.fw_mac,mac,6);if(!memcmp(mac,nss_native.info.mac,6))nss_native.mac_switch=0;IOLockUnlock(nss_native.lock);
  printf("NORD2 WIFI IOS MAC: activated=1 commands=%u private=%u result=%d\n",pre,(unsigned)((mac[0]&2u)!=0),result);
 }
 if(result<0){slot->scan=r->scan;slot->captured=r->scan.last;ncd_native.halt=nwfn_native.halt=nwfn_native.init.halt=nwfn_native.init.transport.halt=nfp_native.halt=1;return -1;}
 if(!r->scan.done&&r->scan_requested){r->scan.active=0;r->scan.partial=1;if(s->service_timed_out)r->scan.timed_out=1;}
 uint64_t now;if(ncd_native_now(&ncd_native,&now)||now<r->scan.last)return -1;
 slot->scan=r->scan;slot->captured=now;return 0;
}
static int nss_execute(struct nss_slot*slot){
 struct nct_session*s=&nct_native.session;struct ncr_state*r=&ncb_native.rx;
 if(ncd_native.worker!=current_thread()||!ncd_native.active||ncd_native.halt||nct_native.active||s->phase!=NCT_DONE||s->halt||r->active||r->halt){
  nss_diag(1,(unsigned)(ncd_native.worker!=current_thread())|(unsigned)(!ncd_native.active)<<1|(unsigned)(ncd_native.halt!=0)<<2|(unsigned)(nct_native.active!=0)<<3|(unsigned)(s->phase!=NCT_DONE)<<4|(unsigned)(s->halt!=0)<<5|(unsigned)(r->active!=0)<<6|(unsigned)(r->halt!=0)<<7,s->phase,0,0);return 1;}
 const struct ncs_channel*channels=slot->request.count?slot->request.channels:nss_native.info.channels;
 size_t count=slot->request.count?slot->request.count:nss_native.info.count;
 uint8_t sequence=(uint8_t)(nss_native.scan_sequence+1u);
 if(nss_native.mac_switch)return nss_execute_mac(slot,channels,count,sequence); /* 0436: only while a station-address change is pending */
 if(ncs_build(ncd_native.diag.frame,ncd_native.diag.frame_bytes,ncd_native.diag.history[ncd_native.diag.events-1].sequence,nss_native.info.mac,channels,count,1,2,sequence,1,slot->request.dwell_tu,0,&nss_native.frames)){nss_diag(2,0,(unsigned)count,slot->request.dwell_tu,0);return 1;}
 nss_native.previous=*s;nss_native.previous_rx=*r;nss_native.previous_rx.session=&nss_native.previous;
 memset(s,0,sizeof(*s));memset(r,0,sizeof(*r));
 const struct nct_entry entry={NCS_SCAN_BYTES,3,1};const struct nct_recipe recipe={&entry,1,nss_build,&nss_native.frames,UINT64_C(30000000),UINT64_C(15000000)};
 unsigned returning=nss_return.ready&&!nss_return.consumed;
 int preparation=returning?nct_scan_return_prepare(&nbc_native.state,&nss_return.origin,&recipe,s):nct_continue_prepare(&nbc_native.state,&nss_native.previous,&recipe,s);
 if(!preparation)preparation=returning?ncr_scan_return_prepare(r,&nwfn_native.init,s,nss_return.queue,sequence,UINT64_C(14000000)):ncr_continue_prepare(r,&nwfn_native.init,s,&nss_native.previous_rx,sequence,UINT64_C(14000000));
 if(preparation){nss_diag(s->refusal?3:4,(unsigned)preparation,s->refusal,returning,0);nss_diag_state(s);*s=nss_native.previous;*r=nss_native.previous_rx;r->session=s;return 1;}
 nct_native.active=1;NVC_PHASE(5);
 const struct nct_hooks hooks={ncr_pump,r,ncb_sleep,&ncb_native};
 int result=nct_run(&ncd_io,&ncd_native,&hooks,s);nct_native.active=0;
 if(result==1&&s->drain_refused){nss_diag(5,s->drain_quiescent,s->events,s->pump_calls,0);/* RX may already have rearmed stale frames: preserve its current cursors. */
  if(returning)for(unsigned k=0;k<5;k++)nss_return.queue[k]=r->queue[k];
  *s=nss_native.previous;r->session=s;r->phase=NCR_IDLE;r->active=0;return 1;
 }
 if(returning)nss_return.consumed=1;
 nss_native.scan_sequence=sequence;
 if(result<0){slot->scan=r->scan;slot->captured=r->scan.last;ncd_native.halt=nwfn_native.halt=nwfn_native.init.halt=nwfn_native.init.transport.halt=nfp_native.halt=1;return -1;}
 if(!r->scan.done&&r->scan_requested){r->scan.active=0;r->scan.partial=1;if(s->service_timed_out)r->scan.timed_out=1;}
 uint64_t now;if(ncd_native_now(&ncd_native,&now)||now<r->scan.last)return -1;
 slot->scan=r->scan;slot->captured=now;return 0;
}
static void nss_deliver(struct nss_slot*s,enum nss_status status){
 IOLockLock(nss_native.lock);if(!s->canceled&&(status==NSS_COMPLETE||status==NSS_PARTIAL))njs_cache_scan_locked(&s->scan);s->status=s->canceled?NSS_CANCELED:status;s->state=NSS_DONE;s->callback_busy=1;
 printf("NORD2 WIFI IOS SCAN: ticket=%llu generation=%llu status=%u sequence=%u frames=%u bss=%u done=%u partial=%u canceled=%u error=%u rx_error=%u\n",(unsigned long long)s->ticket,(unsigned long long)s->generation,(unsigned)s->status,s->scan.sequence,s->scan.frames,s->scan.count,s->scan.done,s->scan.partial,s->canceled,nct_native.session.error,ncb_native.rx.error);
 if(s->status==NSS_FATAL)printf("NORD2 WIFI IOS GUARD: reason=%u source=%u stage=%u error=%u valid=%u addr=%#x raw=%#x mask=%#x expected=%#x\n",nwfn_native.guard_reason,nwfn_native.guard_source,nwfn_native.failure_stage,nwfn_native.error,nwfn_native.guard_valid,nwfn_native.guard_address,nwfn_native.guard_raw,nwfn_native.guard_mask,nwfn_native.guard_expected);
 nss_completion fn=s->completion;void*cookie=s->cookie;uint64_t gen=s->generation,ticket=s->ticket;status=s->status;IOLockUnlock(nss_native.lock);
 fn(cookie,gen,ticket,status); /* no lock held; cookie not accessed afterward */
 IOLockLock(nss_native.lock);s->callback_busy=0;if(s->retire)memset(s,0,sizeof(*s));IOLockUnlock(nss_native.lock);
}
#ifndef NSS_STATION_TEST_DISABLED
#if defined(NORD2) || defined(NWL_HOST_TEST)
static void nwl_configure_station(void);
#define NJS_LIVE_CONFIGURE() nwl_configure_station()
#endif
#include "wifi_station_transport_bind.inc"
#define NJS_RETURN_SCANS() nss_return_scans()
#include "wifi_station_service_bind.inc"
#include "wifi_scan_return_bind.inc"
#if defined(NORD2) || defined(NWL_HOST_TEST)
#include "wifi_live_control_bind.inc"
#define NSS_LIVE_START() nwl_native_start()
#define NSS_LIVE_WORK() nwl_native_work()
#define NSS_LIVE_ENABLED() nwl_enabled()
#endif
#else
static int nss_scan_return_origin_valid(const struct nct_session*s){return !s->scan_return;}
static int nst_native_owner_window(const struct nwfn_native*n){(void)n;return 0;}
static int nst_native_probe_window(const struct nwfn_native*n){(void)n;return 0;}
static int nst_native_rx_active(const struct nwfn_native*n){(void)n;return 0;}
static int nst_native_rx_memory(const struct nwfn_native*n,uint64_t a,uint32_t b,unsigned w){(void)n;(void)a;(void)b;(void)w;return 0;}
static int nst_native_rx_write(const struct nwfn_native*n,uint32_t a,uint32_t v){(void)n;(void)a;(void)v;return 0;}
#endif
#ifndef NSS_LIVE_START
#define NSS_LIVE_START() ((void)0)
#define NSS_LIVE_WORK() ((void)0)
#define NSS_LIVE_ENABLED() 0
#endif
static void nss_owner_loop(void){
 NSS_LIVE_START();
 for(;;){NSS_LIVE_WORK();int join_work=njs_owner_work();if(join_work<0){ncd_native.halt=nwfn_native.halt=nwfn_native.init.halt=nwfn_native.init.transport.halt=nfp_native.halt=1;IOLockLock(nss_native.lock);nss_native.failed=1;IOLockUnlock(nss_native.lock);nwm_failure_drain();panic("NORD2 WIFI STATION: uncertain retained DMA; manual reset required");}if(join_work)continue;
 IOLockLock(nss_native.lock);struct nss_slot*s=0;for(unsigned k=0;k<NSS_SLOTS;k++)if(nss_native.slot[k].state==NSS_QUEUED&&(!s||nss_native.slot[k].ticket<s->ticket))s=&nss_native.slot[k];
  if(!s){if(njs_pending_locked()){IOLockUnlock(nss_native.lock);continue;}NVC_PHASE(6);
   /* EP0 cannot acquire locks to wake this owner. Development mode polls
    * its one-slot mailbox; normal builds retain the original idle sleep. */
   if(NSS_LIVE_ENABLED()){uint64_t deadline;clock_interval_to_deadline(100,NSEC_PER_MSEC,&deadline);assert_wait_deadline((event_t)&nss_native,THREAD_UNINT,deadline);}
   else assert_wait((event_t)&nss_native,THREAD_UNINT);
   IOLockUnlock(nss_native.lock);thread_block(THREAD_CONTINUE_NULL);continue;}
  s->state=NSS_RUNNING;unsigned canceled=s->canceled;IOLockUnlock(nss_native.lock);
  int result=canceled?1:nss_execute(s);enum nss_status status=canceled?NSS_CANCELED:result<0?NSS_FATAL:result?NSS_REFUSED:(s->scan.done&&!s->scan.partial&&!s->scan.timed_out&&!s->scan.dropped)?NSS_COMPLETE:NSS_PARTIAL;
  if(result<0){ncd_native.halt=nwfn_native.halt=nwfn_native.init.halt=nwfn_native.init.transport.halt=nfp_native.halt=1;IOLockLock(nss_native.lock);nss_native.failed=1;IOLockUnlock(nss_native.lock);}
  nss_deliver(s,status);
  if(result<0){IOLockLock(nss_native.lock);nss_native.failed=1;IOLockUnlock(nss_native.lock);
   for(unsigned k=0;k<NSS_SLOTS;k++){IOLockLock(nss_native.lock);unsigned pending=nss_native.slot[k].state==NSS_QUEUED;IOLockUnlock(nss_native.lock);if(pending)nss_deliver(&nss_native.slot[k],NSS_FATAL);}
   nwm_failure_drain();panic("NORD2 WIFI SERVICE: uncertain retained DMA; manual reset required");}
 }
}
