from pathlib import Path
import tempfile,subprocess,json,argparse
a=argparse.ArgumentParser();a.add_argument("--log-dir",type=Path);args=a.parse_args()
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if p.name=='ios-sweet');S=R/'shim/nord2-xnu';F=R/'tools/fixtures/nord2-wifi-normal'
names=('wifi_default_feature','wifi_rx_readback','wifi_normal_backend','wifi_normal_config','wifi_normal_init','wifi_normal_rx','wifi_normal_transport','wifi_regulatory_policy','wifi_scan_bss','wifi_scan_plan','wifi_basic_config','wifi_basic_resources','wifi_basic_metadata','wifi_basic_primitives','wifi_capability','wifi_capability_dispatcher','wifi_main_initializer','wifi_main_dmashdl','wifi_main_init_plan','wifi_main_transport','wifi_main_layout','wifi_dma_preflight','wifi_main_command','dma_pool_contract')
with tempfile.TemporaryDirectory(prefix='nss-native-') as td:
 d=Path(td)
 for f in S.iterdir():
  if f.suffix in ('.h','.inc','.c'):(d/f.name).write_bytes(f.read_bytes())
 for f in (P/'proposed').iterdir():(d/f.name).write_bytes(f.read_bytes())
 (d/'nss_host.h').write_bytes((P/'nss_host.h').read_bytes())
 for f in F.iterdir():
  if f.suffix=='.inc':(d/f.name).write_bytes(f.read_bytes())
 owner=(d/'owner-fixture.inc').read_text().replace('static unsigned cap_flag=1,','static unsigned ios_flag=1;\nstatic unsigned cap_flag=1,').replace('*v=!strcmp(s,"nord2wificap")?', '*v=!strcmp(s,"nord2wifiios")?ios_flag:!strcmp(s,"nord2wificap")?')
 (d/'owner-fixture.inc').write_text(owner)
 # Existing full native fixture's probes and source-owned board/auth init remain.
 test=(F/'test-full.c').read_text();test=test[:test.index('int main(')]
 test=test.replace('assert(!delivered++&&nct_native.session.cursor==(NCB_COMMANDS-1)&&nct_native.session.record[NCB_COMMANDS-1].publication_attempted&&!nct_native.session.record[NCB_COMMANDS-1].published);','assert(!delivered++);unsigned ix=nct_native.session.continuation?0:NCB_COMMANDS-1;assert(nct_native.session.cursor==ix&&nct_native.session.record[ix].publication_attempted&&!nct_native.session.record[ix].published);')
 test=test.replace('p[36]=1;p[40]', 'p[36]=cmd[64];p[40]')
 test=test.replace('static unsigned mode,delivered,probe_memory,probe_write,delayed_trigger;', 'static unsigned refill_drain;\nstatic uint64_t cancel_inflight;\nstatic unsigned mode,delivered,probe_memory,probe_write,delayed_trigger;').replace('if(cmd[36]!=3)return;', 'if(cmd[36]!=3)return; if(cancel_inflight){assert(!nord2_wifi_scan_cancel(cancel_inflight));cancel_inflight=0;}')
 test=test.replace('probe_write|=1u<<r->current;', 'probe_write|=1u<<r->current;if(refill_drain&&nct_native.session.phase==NCT_DRAIN&&r->current==0){unsigned future=(q->next+1)%q->ring.count;uint8_t*dd=pool_memory+(q->ring.descriptors-NDP_POOL_BASE)+16*future;put32(dd+4,0xc0000000u);hardware_register(q->ring.base+12,(future+1)%q->ring.count);}')
 test+='''
static unsigned callbacks,last_status,defer;static uint64_t last_ticket;
static void completed(void*c,uint64_t gen,uint64_t ticket,enum nss_status status){assert(c==&callbacks&&gen==99);callbacks++;last_status=status;last_ticket=ticket;if(defer)assert(!nord2_wifi_scan_release(ticket));}
static void drain(void){worker_thread=2;if(!setjmp(parked_exit))nss_owner_loop();}
int main(int argc,char**argv){assert(argc==2||argc==3);FILE*f=fopen(argv[1],"rb");assert(f&&fread(authenticated_board,1,sizeof authenticated_board,f)==sizeof authenticated_board);fclose(f);
 if(argc==3){ios_flag=0;setup(0);run_worker();assert(!nss_native.ready&&!service_ready_hooks&&ncd_native.parked&&normal_pubs==26);puts("PASS default off: original boot26/park, no service-ready hook or queue");return 0;}
 setup(0);run_worker();assert(nss_native.ready&&!ncd_native.parked&&service_ready_hooks==1&&nct_native.session.completed==26);unsigned base_pubs=normal_pubs;struct nss_info info;assert(!nord2_wifi_scan_info(&info)&&info.count==22);struct nss_request q={0};uint64_t ticket;struct nss_view view;
 /* Pending DDONE is not quiescence. A stale completed frame may already
  * have been rearmed: preserve that advanced cursor on clean refusal. */
 struct ncr_queue *pending=&ncb_native.rx.queue[2];unsigned before0=ncb_native.rx.queue[0].next;
 beacon(0,'Z',1);hardware_register(pending->ring.base+12,(pending->next+1)%pending->ring.count);
 unsigned pending_pubs=normal_pubs;assert(!nord2_wifi_scan_enqueue(&q,&callbacks,99,completed,&ticket));drain();
 assert(last_status==NSS_REFUSED&&normal_pubs==pending_pubs&&!manual_halts&&nct_native.session.phase==NCT_DONE);
 assert(ncb_native.rx.queue[0].next==(before0+1)%1024&&ncb_native.rx.queue[2].wait_ddone>=256);
 assert(!nord2_wifi_scan_release(ticket));hardware_register(pending->ring.base+12,pending->next);
 callbacks=0;
 unsigned old_data=ncb_native.rx.queue[0].next,old_event=ncb_native.rx.queue[1].next;
 assert(!nord2_wifi_scan_enqueue(&q,&callbacks,99,completed,&ticket));assert(nord2_wifi_scan_view(ticket,&view)==1);delivered=0;drain();assert(callbacks==1&&last_status==NSS_COMPLETE&&normal_pubs==base_pubs+1);assert(nwfn_native.init.transport.completed==32&&nwfn_native.init.transport.next_sequence==33);assert(ncb_native.rx.queue[0].next==(old_data+1)%1024&&ncb_native.rx.queue[1].next==(old_event+1)%16);assert(!nord2_wifi_scan_view(ticket,&view)&&view.scan->sequence==2&&view.scan->count==2);assert(!nord2_wifi_scan_release(ticket));
 for(unsigned n=0;n<3;n++){delivered=0;assert(!nord2_wifi_scan_enqueue(&q,&callbacks,99,completed,&ticket));drain();assert(last_status==NSS_COMPLETE&&!nord2_wifi_scan_view(ticket,&view)&&view.scan->sequence==3+n);assert(!nord2_wifi_scan_release(ticket));}

 /* Persist cross-descriptor segmented state: last fragment is not a fresh BSS. */
 ncb_native.rx.queue[0].segmented=1;delivered=0;assert(!nord2_wifi_scan_enqueue(&q,&callbacks,99,completed,&ticket));drain();assert(!nord2_wifi_scan_view(ticket,&view)&&view.scan->count==1&&!ncb_native.rx.queue[0].segmented);assert(!nord2_wifi_scan_release(ticket));
 /* Exhaust all 4096 stale-frame budget without publishing a scan. Preserve
  * every already-rearmed cursor; a following request drains the remainder. */
 unsigned queued=0;for(unsigned k=0;k<5;k++){struct ncr_queue*z=&ncb_native.rx.queue[k];for(unsigned j=0;j<z->ring.count-1;j++){unsigned at=(z->next+j)%z->ring.count;uint8_t*dd=pool_memory+(z->ring.descriptors-NDP_POOL_BASE)+16*at;put32(dd+4,0xc0000000u);}hardware_register(z->ring.base+12,(z->next+z->ring.count-1)%z->ring.count);queued+=z->ring.count-1;}assert(queued>0);refill_drain=1;
 unsigned budget_pubs=normal_pubs;assert(!nord2_wifi_scan_enqueue(&q,&callbacks,99,completed,&ticket));drain();assert(last_status==NSS_REFUSED&&normal_pubs==budget_pubs&&!manual_halts&&ncb_native.rx.rearmed==4096);refill_drain=0;assert(!nord2_wifi_scan_release(ticket));
 delivered=0;assert(!nord2_wifi_scan_enqueue(&q,&callbacks,99,completed,&ticket));drain();assert(last_status==NSS_COMPLETE&&normal_pubs==budget_pubs+1);assert(!nord2_wifi_scan_release(ticket));
 /* Packet and scan sequence wrap; all five actual ring cursors continue. */
 while(nwfn_native.init.transport.next_sequence!=0){delivered=0;assert(!nord2_wifi_scan_enqueue(&q,&callbacks,99,completed,&ticket));drain();assert(last_status==NSS_COMPLETE);assert(!nord2_wifi_scan_release(ticket));}
 struct nct_session*closed=&nct_native.session;struct nwt_state*tx=&nwfn_native.init.transport;
 tx->tx_index=1023;closed->first_tx=1022;closed->record[0].from=1022;closed->record[0].to=1023;hardware_register(0x1802541c,1023);hardware_register(0x18025418,1023);
 delivered=0;assert(!nord2_wifi_scan_enqueue(&q,&callbacks,99,completed,&ticket));drain();assert(tx->tx_index==0&&tx->next_sequence==1&&nct_native.session.record[0].sequence==0);assert(!nord2_wifi_scan_release(ticket));
 /* Inflight cancel never aborts or replays the already published radio command. */
 delivered=0;assert(!nord2_wifi_scan_enqueue(&q,&callbacks,99,completed,&ticket));cancel_inflight=ticket;defer=1;unsigned completed_before=tx->completed;drain();assert(last_status==NSS_CANCELED&&tx->completed==completed_before+1&&!manual_halts&&nord2_wifi_scan_view(ticket,&view)==1);assert(nord2_wifi_scan_release(ticket)==1);defer=0;
 /* Exhaustion refuses before IO and restores the exact closed session. */
 unsigned saved_base=closed->base_completed,saved_completed=tx->completed;closed->base_completed=UINT32_MAX-1;tx->completed=UINT32_MAX;unsigned exhausted_reads=mmio_reads,exhausted_writes=mmio_writes;
 assert(!nord2_wifi_scan_enqueue(&q,&callbacks,99,completed,&ticket));drain();assert(last_status==NSS_REFUSED&&mmio_reads==exhausted_reads&&mmio_writes==exhausted_writes&&tx->completed==UINT32_MAX);assert(!nord2_wifi_scan_release(ticket));closed->base_completed=saved_base;tx->completed=saved_completed;

 unsigned saved_cost=nbc_native.state.plan.pse_cost;nbc_native.state.plan.pse_cost=0;unsigned refuse_reads=mmio_reads,refuse_writes=mmio_writes;
 assert(!nord2_wifi_scan_enqueue(&q,&callbacks,99,completed,&ticket));drain();assert(last_status==NSS_REFUSED&&mmio_reads==refuse_reads&&mmio_writes==refuse_writes&&nct_native.session.phase==NCT_DONE);assert(!nord2_wifi_scan_release(ticket));nbc_native.state.plan.pse_cost=saved_cost;
 delivered=0;assert(!nord2_wifi_scan_enqueue(&q,&callbacks,99,completed,&ticket));drain();assert(last_status==NSS_COMPLETE);assert(!nord2_wifi_scan_release(ticket));
 unsigned reads=mmio_reads,writes=mmio_writes;assert(!nord2_wifi_scan_enqueue(&q,&callbacks,99,completed,&ticket));assert(!nord2_wifi_scan_cancel(ticket));drain();assert(last_status==NSS_CANCELED&&reads==mmio_reads&&writes==mmio_writes&&nord2_wifi_scan_view(ticket,&view)==1);assert(!nord2_wifi_scan_release(ticket));
 struct nss_request bad={.count=1,.channels={{1,255}}};uint64_t saved=123;assert(nord2_wifi_scan_enqueue(&bad,&callbacks,99,completed,&saved)==-1&&saved==123);
 uint64_t tickets[4];for(unsigned i=0;i<4;i++)assert(!nord2_wifi_scan_enqueue(&q,&callbacks,99,completed,&tickets[i]));assert(nord2_wifi_scan_enqueue(&q,&callbacks,99,completed,&saved)==2&&saved==123);for(unsigned i=0;i<4;i++)assert(!nord2_wifi_scan_cancel(tickets[i]));drain();for(unsigned i=0;i<4;i++)assert(!nord2_wifi_scan_release(tickets[i]));
 mode=1;delivered=0;defer=1;assert(!nord2_wifi_scan_enqueue(&q,&callbacks,99,completed,&ticket));drain();assert(last_status==NSS_PARTIAL&&nord2_wifi_scan_view(ticket,&view)==1&&!manual_halts);defer=0;
 /* A real next command wrong DIDX keeps shared halt/drain, never retries. */
 mode=0;delivered=0;normal_wrong_didx=1;defer=1;assert(!nord2_wifi_scan_enqueue(&q,&callbacks,99,completed,&ticket));drain();assert(last_status==NSS_FATAL&&manual_halts==1&&nss_native.failed&&nwfn_native.halt&&nwfn_native.init.transport.halt&&nwm_drain_entered&&drain_blocks==1);normal_wrong_didx=0;
 assert(nrd_calls&&nrd_calls==nrd_restored&&nrd_checked==nrd_calls&&last_ticket);
 assert(!pthread_mutex_destroy(&nss_native.lock->m));free(nss_native.lock);
 puts("PASS actual owner repeated CID3: boot26 once then250+ scans/packet+TX wrap/segmentedcontinuity/inflightcancel/refusalrestore/fataldrain, sequence/credit/5ring continuity, sourceCID3 realdone/BSS, queuedcancel zeroIO, invalidchannels/fullqueue, deferredrelease, honest timeout; no hardware claim");return 0;}
'''
 (d/'test.c').write_text(test)
 # Same existing readback fixture seam; owner/checked IO are not replaced.
 rx=(d/'wifi_normal_rx.c').read_text().replace('#include "wifi_normal_rx.h"','#include "wifi_normal_rx.h"\nextern int nrd_host_probe(const struct ncd_io*,void*,struct ncr_state*,struct nrd_snapshot*,unsigned);\n#define nrd_capture nrd_host_probe');(d/'wifi_normal_rx.c').write_text(rx)
 basic=(d/'wifi_basic_config_bind.inc').read_text().replace(' int result=nbc_prepare',' sleepy_pre_basic();\n int result=nbc_prepare');(d/'wifi_basic_config_bind.inc').write_text(basic)
 cap=(d/'wifi_capability_bind.inc').read_text().replace('static int ncd_native_mread(void*c,uint64_t p,void*v,uint32_t n)\n{','static int ncd_native_mread(void*c,uint64_t p,void*v,uint32_t n)\n{full_test_probe(0);').replace('static int ncd_native_write(void*c,uint32_t p,uint32_t v)\n{','static int ncd_native_write(void*c,uint32_t p,uint32_t v)\n{nrd_forbid_write();full_test_probe(1);').replace('static int ncd_native_mwrite(void*c,uint64_t p,const void*v,uint32_t n)\n{','static int ncd_native_mwrite(void*c,uint64_t p,const void*v,uint32_t n)\n{nrd_forbid_write();');(d/'wifi_capability_bind.inc').write_text(cap)
 cmd=['clang','-std=c11','-DNSS_HOST_TEST','-DNCFG_HOST_TEST','-O1','-g','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(d),str(d/'test.c'),*[str(d/(name+'.c')) for name in names],'-lcrypto','-o',str(d/'test')];subprocess.run(cmd,check=True);result=subprocess.run([str(d/'test'),str(R/'research/nord2/wifi/calibration/WIFI')],capture_output=True,text=True);
 if args.log_dir:args.log_dir.mkdir(parents=True,exist_ok=True);(args.log_dir/'test-native.log').write_text(result.stdout+result.stderr)
 print(result.stdout.splitlines()[-1:] if result.stdout else result.stderr);result.check_returncode()
 off=subprocess.run([str(d/'test'),str(R/'research/nord2/wifi/calibration/WIFI'),'off'],capture_output=True,text=True);print(off.stdout.splitlines()[-1:] if off.stdout else off.stderr);off.check_returncode()
 if args.log_dir:(args.log_dir/'test-default-off.log').write_text(off.stdout+off.stderr)
