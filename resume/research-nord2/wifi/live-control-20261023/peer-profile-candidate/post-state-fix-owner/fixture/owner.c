#include <openssl/evp.h>
#include "wifi_wpa_crypto.h"
#include "vectors.h"
static unsigned station_pubs,station_frames,station_commands,link_up_calls,link_down_calls,key_deletes,notices[5],m1_sent,m3_sent,stale_m3;
static unsigned callbacks;
static void completed(void*c,uint64_t generation,uint64_t ticket,enum nss_status status){assert(c==&callbacks&&generation==99&&(status==NSS_COMPLETE||status==NSS_PARTIAL));callbacks++;assert(!nord2_wifi_scan_release(ticket));}
int nord2_wifi_ifnet_link(uint64_t g,int a){assert(g);if(a){assert(nst_native.state.controlled_port);link_up_calls++;}else{assert(!nst_native.state.controlled_port);link_down_calls++;}return 0;}
int nord2_wifi_ifnet_receive(uint64_t g,const uint8_t*p,size_t n){assert(g&&p&&n>=14);return 0;}
static void drain(void){worker_thread=2;if(!setjmp(parked_exit))nss_owner_loop();}
/* Actual retained NSS/IO guard composition. Only the MMIO model supplies fixed
 * public WTBL words; the real owner/probe/lease functions execute unchanged. */
static unsigned peer_token;
static struct nwl_snapshot peer_command(unsigned op,unsigned generation){
 struct nwl_snapshot out;assert(nwl_submit((op<<12)|++peer_token,generation));
 nwl_native_work();uint8_t b[256];assert(nwl_reply(b,sizeof b)==256);
 for(unsigned k=0;k<64;k++)out.word[k]=get32(b+4*k);
 assert(out.word[NWL_F_TOKEN]==peer_token&&!nst_native.probe&&!nst_native.state.active);return out;
}
static void peer_probe_actual(void){
 struct nst*s=&nst_native.state;assert(nwl_enabled()&&s->controlled_port&&s->ptk.installed&&s->gtk.installed&&!s->active);
 unsigned writes=mmio_writes,reads=peer_hw_reads;struct nst saved=*s;
 struct nwl_snapshot out=peer_command(NWL_PEER_PROBE,(unsigned)s->generation);
 assert(out.word[NWL_F_RESULT]==NWL_OK&&out.word[NWL_F_PEER_VALID]&&out.word[NWL_F_PEER_META]==0x502&&peer_hw_reads==reads+4);
 assert(mmio_writes==writes&&!memcmp(s,&saved,sizeof saved));
 /* A normal checked IO lease still refuses: probe is not an active pump. */
 assert(!nst_native_owner_window(&nwfn_native)&&!nst_native_probe_window(&nwfn_native));
 for(unsigned k=0;k<8;k++){
  unsigned active=nct_native.active;uint64_t generation=s->generation;
  if(k==0)s->wire_busy=1;if(k==1)s->air_pending=1;if(k==2)s->normal.published=1;if(k==3)s->callback_active=1;
  if(k==4)s->credit_available--;if(k==5)s->queue[0].next=s->queue[0].ring.count;if(k==6)nct_native.active=1;if(k==7)s->generation++;
  reads=peer_hw_reads;out=peer_command(NWL_PEER_PROBE,(unsigned)njs_native.native_generation);
  assert(out.word[NWL_F_RESULT]==NWL_REFUSED&&!out.word[NWL_F_PEER_VALID]&&peer_hw_reads==reads&&!nst_native.probe);
  *s=saved;nct_native.active=active;s->generation=generation;
 }
 peer_selector=1;peer_selector_reads=0;reads=peer_hw_reads;out=peer_command(NWL_PEER_PROBE,(unsigned)s->generation);
 assert(out.word[NWL_F_PEER_ERROR]==NWL_PEER_SELECTOR&&peer_hw_reads==reads+1);peer_selector=0;
 peer_changed=1;peer_selector_reads=0;reads=peer_hw_reads;out=peer_command(NWL_PEER_PROBE,(unsigned)s->generation);
 assert(out.word[NWL_F_PEER_ERROR]==NWL_PEER_CHANGED&&peer_hw_reads==reads+4);peer_changed=0;
 /* Unknown/stale commands cannot map-read; invalid slots cannot expose any
  * arbitrary word. Wrong physical owner must reach unchanged common guard. */
 reads=peer_hw_reads;out=peer_command(NWL_PEER_PROBE,(unsigned)s->generation+1);assert(out.word[NWL_F_RESULT]==NWL_STALE&&peer_hw_reads==reads);
 assert(!nwl_submit((11u<<12)|(peer_token+1),(unsigned)s->generation)&&peer_hw_reads==reads);
 out=peer_command(NWL_DATA_TODS,(unsigned)s->generation);assert(out.word[NWL_F_RESULT]==NWL_REFUSED&&nst_native_next_data_profile()==s->data_profile);
 unsigned old_halt=nwfn_native.halt,old_error=nwfn_native.error,old_init=nwfn_native.init.halt,old_transport=nwfn_native.init.transport.halt;
 gic_active=1;reads=peer_hw_reads;out=peer_command(NWL_PEER_PROBE,(unsigned)s->generation);
 assert(out.word[NWL_F_RESULT]==NWL_REFUSED&&out.word[NWL_F_PEER_ERROR]==NWL_PEER_OWNER&&peer_hw_reads==reads&&!nst_native.probe&&nwfn_native.halt);
 /* Explicit fault-fixture reset only; production never clears fatal state. */
 gic_active=0;nwfn_native.halt=old_halt;nwfn_native.error=old_error;nwfn_native.init.halt=old_init;nwfn_native.init.transport.halt=old_transport;
 assert(mmio_writes==writes&&!memcmp(s,&saved,sizeof saved));
}

static uint8_t*station_packet(unsigned k,unsigned type,unsigned bytes){
 struct ncr_queue*q=&nst_native.state.queue[k];unsigned at=q->next;
 uint8_t*d=pool_memory+(q->ring.descriptors-NDP_POOL_BASE)+16*at,*p=pool_memory+(q->ring.buffers-NDP_POOL_BASE)+4096*at;memset(p,0,bytes);w16(p,bytes);w16(p+2,type==7?0x3800:type<<11);
 put32(d,(uint32_t)(q->ring.buffers+4096*at));put32(d+4,0xc0000000u|(bytes<<16));put32(d+8,0);put32(d+12,0);hardware_register(q->ring.base+12,(at+1)%q->ring.count);return p;
}
static void station_mgmt(unsigned fc){
 struct nst*s=&nst_native.state;unsigned bytes=fc==0xb0?30:55;uint8_t*p=station_packet(2,7,24+bytes);w16(p+2,0x3801);put32(p+8,12u<<8);p+=24;w16(p,fc);memcpy(p+4,s->peer.self,6);memcpy(p+10,s->peer.ap,6);memcpy(p+16,s->peer.ap,6);
 if(fc==0xb0){w16(p+26,2);}else{w16(p+24,0x11);w16(p+28,1);p[30]=1;p[31]=1;p[32]=0x82;memcpy(p+33,rsn,22);}
}
static void station_eapol(const uint8_t*f,size_t n){
 struct nst*s=&nst_native.state;uint8_t*p=station_packet(0,2,(unsigned)n+54);put32(p+4,(8u<<11)|s->peer.wlan);put32(p+8,(1u<<13)|(7u<<8));w16(p+24,0x0208);memcpy(p+26,s->peer.ap,6);memcpy(p+40,s->peer.self,6);memcpy(p+46,s->peer.ap,6);p[52]=0x88;p[53]=0x8e;memcpy(p+54,f,n);
}
static void make_m3(uint8_t*out){
 uint8_t context[76],derived[48],plain[80],mic[20];size_t used=0;struct nst*s=&nst_native.state;
 const uint8_t*a=s->peer.self,*b=s->peer.ap;if(memcmp(a,b,6)>0){const uint8_t*t=a;a=b;b=t;}memcpy(context,a,6);memcpy(context+6,b,6);
 uint8_t snonce[32];memset(snonce,(int)(0x40+random_calls-(stale_m3?1:0)),32);const uint8_t*x=snonce,*y=m1+17;if(memcmp(x,y,32)>0){const uint8_t*t=x;x=y;y=t;}memcpy(context+12,x,32);memcpy(context+44,y,32);
 assert(!nwpc_ptk(pmk_vector,context,derived));assert(!nwpc_unwrap(ptk+16,m3+99,sizeof m3-99,plain,sizeof plain,&used)&&used==48);
 memcpy(out,m3,sizeof m3);EVP_CIPHER_CTX*c=EVP_CIPHER_CTX_new();assert(c);EVP_CIPHER_CTX_set_flags(c,EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);int n=0,end=0;assert(EVP_EncryptInit_ex(c,EVP_aes_128_wrap(),0,derived+16,0)&&EVP_EncryptUpdate(c,out+99,&n,plain,(int)used)&&EVP_EncryptFinal_ex(c,out+99+n,&end)&&n+end==56);EVP_CIPHER_CTX_free(c);
 memset(out+81,0,16);const uint8_t*parts[]={out};size_t lengths[]={sizeof m3};assert(!nwpc_hmac(derived,16,parts,lengths,1,mic));memcpy(out+81,mic,16);niwj_clear(derived,sizeof derived);niwj_clear(plain,sizeof plain);
}
static void station_data_completion(unsigned next){
 struct nst*s=&nst_native.state;uint8_t*d=pool_memory+(s->normal.ring-NDP_POOL_BASE)+16*s->normal.next;
 assert(station_hardware&&s->wire_busy&&(s->wire_type==2||s->wire_type==3));station_pubs++;station_frames++;
 uint8_t*page=pool_memory+(s->normal.buffer-NDP_POOL_BASE);unsigned wirelen=(page[44]|(unsigned)page[45]<<8)&32767;
 if(s->wire_type==3&&s->data_profile==NST_DATA_TODS){const uint8_t*raw=page+64;
  assert(wirelen==s->wire_bytes-14&&raw[0]==8&&raw[1]==65&&!memcmp(raw+4,s->peer.ap,6)&&!memcmp(raw+10,s->peer.self,6)&&!memcmp(raw+16,s->wire+32,6)&&!memcmp(raw+30,s->wire+44,s->wire_bytes-44));
  assert(((get32(page+4)>>16)&3)==2&&((get32(page+8)&63)==32)&&((get32(page+28)>>20)&3)==2);
 }else{assert(wirelen==s->wire_bytes-32&&!memcmp(page+64,s->wire+32,wirelen)&&!((get32(page+4)>>16)&3));}
 uint8_t*r=station_packet(3,6,12);w16(r+2,(6u<<11)|1);put32(r+4,2u<<16);put32(r+8,s->normal.token);
 /* Inject valid M3 before local M2 receipt: actual bounded pending queue. */
 if(s->wire_type==2&&njs_native.driver.owner.transaction.wpa.tx_kind==1&&!m3_sent){uint8_t packet[sizeof m3];make_m3(packet);station_eapol(packet,sizeof packet);m3_sent++;}
 put32(d+4,get32(d+4)|0x80000000u);hardware_register(0x1802530c,next);
}
static void station_completion(unsigned next){
 struct nwt_state*t=&nwfn_native.init.transport;struct nst*s=&nst_native.state;uint8_t*d=pool_memory+(t->tx_ring-NDP_POOL_BASE)+16*t->tx_index,*p=pool_memory+(t->command-NDP_POOL_BASE);
 assert(station_hardware&&s->wire_busy);station_pubs++;
 if(s->wire_type){station_frames++;assert(s->wire_type==1);uint8_t*r=station_packet(1,7,124);r[28]=15;r[36]=s->wire_pid;r[40]=s->wire_wlan;if(s->generation>1){uint8_t*late=station_packet(4,7,124);late[28]=15;late[36]=1;late[40]=s->wire_wlan;}
 unsigned fc=s->wire[32];if(fc==0xb0||fc==0)station_mgmt(fc==0xb0?0xb0:0x10);}
 else{station_commands++;assert(p[37]==0xa0&&p[39]==t->next_sequence);
 if(p[36]==0x1c&&!p[66]){uint8_t*r=station_packet(1,7,60);r[28]=16;r[36]=s->peer.bss;r[37]=p[65];r[39]=s->peer.channel;r[41]=s->peer.band;put32(r+48,4000);}
 if(p[36]==0x13&&p[84]==SW_STA_CLASS_3&&p[115]){uint8_t*r=station_packet(1,7,44);r[28]=12;r[29]=p[39];memcpy(r+36,s->peer.ap,6);r[42]=s->peer.sta;r[43]=s->peer.bss;}
 if(p[36]==7&&p[64]&&p[66]){uint8_t*r=station_packet(1,7,44);r[28]=36;r[36]=s->peer.bss;memcpy(r+38,s->peer.ap,6);}
 if(p[36]==7&&!p[64])key_deletes++;
 }
 put32(d,0xffffffffu);put32(d+4,0xffffffffu);put32(d+8,0xdeadbeefu);put32(d+12,0x12345678u);hardware_register(0x1802541c,next);
}
static void join_completed(void*c,uint64_t epoch,uint64_t ticket,enum njs_notice notice){assert(c==notices&&epoch==7&&ticket&&notice<5);notices[notice]++;assert(!pthread_mutex_trylock(&nss_native.lock->m));pthread_mutex_unlock(&nss_native.lock->m);if(notice==NJS_RETIRED)assert(!njs_rejoin_ready_locked());}
static struct niwj_request request(void){struct niwj_request q={0};q.security=1;q.ssid_length=1;q.ssid[0]='X';memcpy(q.pmk,pmk_vector,32);memcpy(q.station_rsn,rsn,22);return q;}
static void until_authorized(void){for(unsigned k=0;k<1000&&!njs_native.linked;k++){
 if(njs_native.driver.owner.transaction.phase==STC_EAPOL&&!m1_sent){station_eapol(m1,sizeof m1);uint8_t*early=station_packet(4,7,44);early[28]=36;early[36]=nst_native.state.peer.bss;memcpy(early+38,nst_native.state.peer.ap,6);assert(!nst_native.state.ptk_command&&!nst_native.state.ptk_ready);m1_sent++;}
 assert(njs_owner_work()==1&&!nst_native.state.halt);
 }assert(njs_native.linked&&nwo_authorized(&njs_native.driver.owner)&&m1_sent&&m3_sent&&nsr_native.ptk&&nsr_native.gtk);}
static void ledger_negatives(void){
 struct nsr_ledger l={0};struct nst*s=calloc(1,sizeof(*s));assert(s);s->wire_busy=1;s->wire_cid=7;s->ticket=1;
 struct nst_event e={.kind=NST_COMMAND_DMA,.cid=7,.ticket=1};
 s->wire[64]=1;s->wire[66]=1;nsr_key_capture(&l,s);assert(l.pending&&!l.ptk);assert(!nsr_key_complete(&l,&e)&&l.ptk&&!l.pending);
 for(unsigned k=0;k<4;k++){s->wire[66]=0;s->wire[76]=(uint8_t)k;s->ticket=++e.ticket;nsr_key_capture(&l,s);assert(!nsr_key_complete(&l,&e));}assert(l.gtk==15);
 for(unsigned k=0;k<4;k++){s->wire[64]=0;s->wire[76]=(uint8_t)k;s->ticket=++e.ticket;nsr_key_capture(&l,s);assert(l.gtk&(1u<<k));assert(!nsr_key_complete(&l,&e)&&!(l.gtk&(1u<<k)));}assert(!l.gtk&&l.ptk);
 s->wire[66]=1;s->wire[76]=0;s->ticket=++e.ticket;nsr_key_capture(&l,s);assert(!nsr_key_complete(&l,&e)&&!l.ptk);
 s->wire[64]=1;s->ticket=++e.ticket;nsr_key_capture(&l,s);e.ticket++;assert(nsr_key_complete(&l,&e)<0&&l.bad&&!l.ptk);free(s);
}
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f&&fread(authenticated_board,1,sizeof authenticated_board,f)==sizeof authenticated_board);fclose(f);
 ledger_negatives();setup(0);run_worker();assert(nss_native.ready);worker_thread=2;
 /* Actual received synthetic beacon exposes real WPA2 selection inputs. */
 struct niwj_request q=request();uint64_t ticket=0;unsigned saved_pid=0,saved_token=0,saved_hw=0;
 assert(nst_native_next_data_profile()==NST_DATA_ETHERNET&&nst_native_data_profile()==NST_DATA_ETHERNET);
 assert(nst_native_set_next_data_profile(2)<0);
 for(unsigned cycle=0;cycle<2;cycle++){
  assert(!nst_native_set_next_data_profile(cycle)&&nst_native_next_data_profile()==cycle);
  assert(!nord2_wifi_join_enqueue(&q,notices,7,join_completed,&ticket)&&ticket==cycle+1);niwj_clear(&q,sizeof q);
  station_hardware=1;until_authorized();assert(random_calls==cycle+1&&nst_native.state.generation==ticket&&nst_native.state.channel_token==ticket);
  assert(nst_native_data_profile()==cycle&&nst_native_set_next_data_profile(1-cycle)<0&&nst_native_next_data_profile()==cycle);
  assert(!nst_native_probe_window(&nwfn_native));assert(!nst_native_probe_begin()&&nst_native_probe_window(&nwfn_native));assert(nst_native_probe_begin()<0&&nst_native_set_next_data_profile(cycle)<0);
  assert(!nst_native_owner_window(&nwfn_native)&&!nst_native_rx_memory(&nwfn_native,nst_native.state.normal.buffer,4096,0));nst_native_probe_end();assert(!nst_native_probe_window(&nwfn_native));
  if(cycle){assert(nst_native.state.next_pid>saved_pid&&nst_native.state.normal.token_next>saved_token&&nst_native.state.normal.next>saved_hw);}
  peer_probe_actual();
  assert(nst_native.state.data_profile==cycle);
  uint8_t data[60]={0};memcpy(data,nst_native.state.peer.ap,6);memcpy(data+6,nss_native.info.mac,6);data[12]=8;assert(!nord2_wifi_link_send(data,sizeof data,ticket));while(njs_native.tx_count||nst_native.state.wire_busy)assert(njs_owner_work()==1);
  /* Pending DMA/keys/port/cookie cannot become a reusable session. */
  assert(!njs_rejoin_ready_locked());assert(!nord2_wifi_join_disconnect());for(unsigned k=0;k<1000&&!njs_native.scan_returned;k++)assert(njs_owner_work()==1);
  assert(njs_native.scan_returned&&!nsr_native.ptk&&!nsr_native.gtk&&!nsr_native.pending&&!nsr_native.bad&&key_deletes==(cycle+1)*2&&notices[NJS_RETIRED]==cycle+1);
  assert(njs_native.used&&nst_native.attempted&&njs_native.arbiter.connection_used&&!njs_rejoin_ready_locked());saved_pid=nst_native.state.next_pid;saved_token=nst_native.state.normal.token_next;saved_hw=nst_native.state.normal.next;
  q=request();uint64_t untouched=123;assert(nord2_wifi_join_enqueue(&q,notices,7,join_completed,&untouched)==1&&untouched==123);
  /* Late TXS is copied into old queue then genuinely drained by return scan. */
  uint8_t*r=station_packet(1,7,124);r[28]=15;r[36]=1;r[40]=5;
  if(cycle==1)assert(!nord2_wifi_scan_cancel(nsr_native.scan_ticket));
  station_hardware=0;normal_pubs+=station_commands;station_commands=0;delivered=0;drain();
  if(cycle==1){assert(!njs_rejoin_ready_locked());struct nss_request refresh={0};uint64_t scan_ticket=0;assert(!nord2_wifi_scan_enqueue(&refresh,&callbacks,99,completed,&scan_ticket));delivered=0;drain();assert(callbacks==1);}
  assert(nsr_native.scan_ready&&nss_return.consumed&&njs_rejoin_ready_locked()&&!nss_native.failed&&!manual_halts);
  struct nwl_snapshot changed=peer_command(NWL_DATA_TODS,(unsigned)njs_native.native_generation);assert(changed.word[NWL_F_RESULT]==NWL_OK&&nst_native_next_data_profile()==1);
  /* Foreign origin/callback/key/DMA/halts refuse reuse without any IO. */
  unsigned reads=mmio_reads,writes=mmio_writes;
  nsr_native.ptk=1;assert(!njs_rejoin_ready_locked());nsr_native.ptk=0;
  nsr_native.gtk=8;assert(!njs_rejoin_ready_locked());nsr_native.gtk=0;
  nsr_native.pending=1;assert(!njs_rejoin_ready_locked());nsr_native.pending=0;
  nss_native.slot[0].callback_busy=1;assert(!njs_rejoin_ready_locked());nss_native.slot[0].callback_busy=0;
  ncd_native.halt=1;assert(!njs_rejoin_ready_locked());ncd_native.halt=0;
  assert(reads==mmio_reads&&writes==mmio_writes&&njs_rejoin_ready_locked());m1_sent=m3_sent=0;
 }
 assert(delivered&&nrd_calls&&nrd_calls==nrd_restored&&nrd_checked==nrd_calls&&station_pubs>station_frames&&link_up_calls==2&&link_down_calls>=2);assert(notices[NJS_JOINED]==2&&notices[NJS_LEFT]==2&&notices[NJS_RETIRED]==2&&!manual_halts);IOLock*notice=njs_native.notice_lock;assert(!pthread_mutex_destroy(&notice->m));free(notice);assert(!pthread_mutex_destroy(&nss_native.lock->m));free(nss_native.lock);puts("PASS actual retained owner peer: two WPA2 epochs, fixed WTBL reads, physical-owner failure, stale/busy/lease/selector refusal, zero writes and active profile immutable; synthetic hardware only");return 0;}
