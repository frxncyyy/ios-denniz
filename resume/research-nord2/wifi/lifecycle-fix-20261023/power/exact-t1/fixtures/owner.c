static unsigned test_unknown_dma,previous_deauth_pid;
static unsigned hold_cancel_dma,held_cancel_next;
static unsigned block_down,down_entered,allow_down,on_started,on_done;
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
static unsigned lifecycle_rx,timeout_callbacks;
#include <openssl/evp.h>
#include "wifi_wpa_crypto.h"
#include "vectors.h"
static unsigned station_pubs,station_frames,station_commands,link_up_calls,link_down_calls,key_deletes,notices[5],m1_sent,m3_sent,stale_m3,clock_expire;
static __attribute__((unused)) void test_prepare_clock(void){if(clock_expire){drain_now=njs_native.deadline_us*1000;clock_expire=0;}}
static unsigned callbacks;
static __attribute__((unused)) void completed(void*c,uint64_t generation,uint64_t ticket,enum nss_status status){assert(c==&callbacks&&generation==99&&(status==NSS_COMPLETE||status==NSS_PARTIAL));callbacks++;assert(!nord2_wifi_scan_release(ticket));}
int nord2_wifi_ifnet_link(uint64_t g,int a){assert(g);if(a){assert(nst_native.state.controlled_port);link_up_calls++;}else{assert(!nst_native.state.controlled_port);link_down_calls++;if(__atomic_load_n(&block_down,__ATOMIC_SEQ_CST)){__atomic_store_n(&down_entered,1,__ATOMIC_SEQ_CST);while(!__atomic_load_n(&allow_down,__ATOMIC_SEQ_CST))sched_yield();}}return 0;}
int nord2_wifi_ifnet_receive(uint64_t g,const uint8_t*p,size_t n){assert(g&&p&&n>=14);lifecycle_rx++;return 0;}
static __attribute__((unused)) void drain(void){worker_thread=2;if(!setjmp(parked_exit))nss_owner_loop();}
static __attribute__((unused)) int checked_owner(void){int r=njs_owner_work();if(r!=1)fprintf(stderr,"OWNER r=%d native_error=%u halt=%u stc=%u ownerhalt=%u ledgerbad=%u scan=%u\n",r,nst_native.state.error,nst_native.state.halt,njs_native.driver.owner.transaction.phase,njs_native.driver.owner.halted,nsr_native.bad,!!nst_native.state.associated_scan);return r;}
static uint8_t*station_packet(unsigned k,unsigned type,unsigned bytes){
 struct ncr_queue*q=&nst_native.state.queue[k];unsigned at=q->next;
 uint8_t*d=pool_memory+(q->ring.descriptors-NDP_POOL_BASE)+16*at,*p=pool_memory+(q->ring.buffers-NDP_POOL_BASE)+4096*at;memset(p,0,bytes);w16(p,bytes);w16(p+2,type==7?0x3800:type<<11);
 put32(d,(uint32_t)(q->ring.buffers+4096*at));put32(d+4,0xc0000000u|(bytes<<16));put32(d+8,0);put32(d+12,0);hardware_register(q->ring.base+12,(at+1)%q->ring.count);return p;
}
static __attribute__((unused)) void station_mgmt(unsigned fc){
 struct nst*s=&nst_native.state;unsigned bytes=fc==0xb0?30:55;uint8_t*p=station_packet(2,7,24+bytes);w16(p+2,0x3801);put32(p+8,12u<<8);p+=24;w16(p,fc);memcpy(p+4,s->peer.self,6);memcpy(p+10,s->peer.ap,6);memcpy(p+16,s->peer.ap,6);
 if(fc==0xb0){w16(p+26,2);}else{w16(p+24,0x11);w16(p+28,1);p[30]=1;p[31]=1;p[32]=0x8c;memcpy(p+33,rsn,22);}
}
static __attribute__((unused)) void station_eapol(const uint8_t*f,size_t n){
 struct nst*s=&nst_native.state;uint8_t*p=station_packet(0,2,(unsigned)n+54);put32(p+4,(8u<<11)|s->peer.wlan);put32(p+8,(1u<<13)|(7u<<8));w16(p+24,0x0208);memcpy(p+26,s->peer.ap,6);memcpy(p+40,s->peer.self,6);memcpy(p+46,s->peer.ap,6);p[52]=0x88;p[53]=0x8e;memcpy(p+54,f,n);
}
static __attribute__((unused)) void make_m3(uint8_t*out){
 uint8_t context[76],derived[48],plain[80],mic[20];size_t used=0;struct nst*s=&nst_native.state;
 const uint8_t*a=s->peer.self,*b=s->peer.ap;if(memcmp(a,b,6)>0){const uint8_t*t=a;a=b;b=t;}memcpy(context,a,6);memcpy(context+6,b,6);
 uint8_t snonce[32];memset(snonce,(int)(0x40+random_calls-(stale_m3?1:0)),32);const uint8_t*x=snonce,*y=m1+17;if(memcmp(x,y,32)>0){const uint8_t*t=x;x=y;y=t;}memcpy(context+12,x,32);memcpy(context+44,y,32);
 assert(!nwpc_ptk(pmk_vector,context,derived));assert(!nwpc_unwrap(ptk+16,m3+99,sizeof m3-99,plain,sizeof plain,&used)&&used==48);
 memcpy(out,m3,sizeof m3);EVP_CIPHER_CTX*c=EVP_CIPHER_CTX_new();assert(c);EVP_CIPHER_CTX_set_flags(c,EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);int n=0,end=0;assert(EVP_EncryptInit_ex(c,EVP_aes_128_wrap(),0,derived+16,0)&&EVP_EncryptUpdate(c,out+99,&n,plain,(int)used)&&EVP_EncryptFinal_ex(c,out+99+n,&end)&&n+end==56);EVP_CIPHER_CTX_free(c);
 memset(out+81,0,16);const uint8_t*parts[]={out};size_t lengths[]={sizeof m3};assert(!nwpc_hmac(derived,16,parts,lengths,1,mic));memcpy(out+81,mic,16);niwj_clear(derived,sizeof derived);niwj_clear(plain,sizeof plain);
}
static __attribute__((unused)) void station_data_completion(unsigned next){
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
static unsigned associated_callbacks,associated_canceled,scan_hold,held_sequence,cancel_commands,hold_scan_dma,held_scan_next;
static __attribute__((unused)) void canceled_completed(void*c,uint64_t g,uint64_t ticket,enum nss_status status){assert(c==&associated_canceled&&g==89&&status==NSS_CANCELED);associated_canceled++;assert(!nord2_wifi_scan_release(ticket));}
static __attribute__((unused)) void timeout_completed(void*c,uint64_t g,uint64_t t,enum nss_status status){assert(c==&timeout_callbacks&&g==90&&status==NSS_REFUSED);struct nss_view v;assert(nord2_wifi_scan_view(t,&v)==1);timeout_callbacks++;assert(!nord2_wifi_scan_release(t));}
static unsigned off_nct_callbacks;static __attribute__((unused)) void off_nct_completed(void*c,uint64_t g,uint64_t t,enum nss_status status){assert(c==&off_nct_callbacks&&g==87&&status==NSS_CANCELED);off_nct_callbacks++;assert(!nord2_wifi_scan_release(t));}
static __attribute__((unused)) void associated_completed(void*c,uint64_t g,uint64_t ticket,enum nss_status status){
 assert(c==&associated_callbacks&&g==88);assert(status==NSS_COMPLETE);struct nss_view v={0};assert(!nord2_wifi_scan_view(ticket,&v)&&v.scan->done&&v.scan->count==1&&v.scan->completion.state==7);assert(njs_native.linked&&nst_native.state.controlled_port&&nst_native.state.ptk.installed&&nst_native.state.gtk.installed);associated_callbacks++;assert(!nord2_wifi_scan_release(ticket));}
static const unsigned char g1_0[]={2,3,0,127,2,19,130,0,0,0,0,0,0,0,0,0,3,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,8,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,224,147,157,252,35,82,26,178,84,9,35,85,87,27,81,139,0,32,15,103,196,24,219,127,5,23,75,230,197,76,148,57,26,53,31,204,57,251,212,46,123,148,149,89,187,150,144,202,95,7};

static __attribute__((unused)) void make_group(uint8_t*out){uint8_t plain[64],mic[20];size_t used=0;struct nwp*w=&njs_native.driver.owner.transaction.wpa;assert(!nwpc_unwrap(ptk+16,g1_0+99,sizeof g1_0-99,plain,sizeof plain,&used));memcpy(out,g1_0,sizeof g1_0);EVP_CIPHER_CTX*c=EVP_CIPHER_CTX_new();assert(c);EVP_CIPHER_CTX_set_flags(c,EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);int n=0,end=0;assert(EVP_EncryptInit_ex(c,EVP_aes_128_wrap(),0,w->kek,0)&&EVP_EncryptUpdate(c,out+99,&n,plain,(int)used)&&EVP_EncryptFinal_ex(c,out+99+n,&end)&&n+end==(int)sizeof g1_0-99);EVP_CIPHER_CTX_free(c);memset(out+81,0,16);const uint8_t*parts[]={out};size_t sizes[]={sizeof g1_0};assert(!nwpc_hmac(w->kck,16,parts,sizes,1,mic));memcpy(out+81,mic,16);niwj_clear(plain,sizeof plain);niwj_clear(mic,sizeof mic); }
static __attribute__((unused)) void associated_frames(unsigned seq){
 uint8_t*p=station_packet(2,7,96);w16(p+2,0x3801);put32(p+4,4u<<11);put32(p+8,12u<<8);put32(p+12,6u<<8);p[28]=100;p[29]=255;p[32]=0x80;p[42]=2;p[48]=2;w16(p+64,100);w16(p+66,0x11);p[68]=0;p[69]=1;p[70]='X';p[71]=1;p[72]=1;p[73]=0x82;memcpy(p+74,rsn,22);
 p=station_packet(4,7,512);p[28]=13;p[36]=(uint8_t)seq;p[40]=1;p[41]=7;p[42]=2;p[52]=1;p[56]=6;
}

static __attribute__((unused)) void station_completion(unsigned next){
 struct nwt_state*t=&nwfn_native.init.transport;struct nst*s=&nst_native.state;uint8_t*d=pool_memory+(t->tx_ring-NDP_POOL_BASE)+16*t->tx_index,*p=pool_memory+(t->command-NDP_POOL_BASE);
 assert(station_hardware&&s->wire_busy);station_pubs++;
 if(s->wire_type){station_frames++;assert(s->wire_type==1);unsigned suppress_deauth=s->wire[32]==0xc0;if(suppress_deauth)previous_deauth_pid=s->wire_pid;if(suppress_deauth&&test_unknown_dma)return;if(!suppress_deauth){uint8_t*r=station_packet(1,7,124);r[28]=15;r[36]=s->wire_pid;r[40]=s->wire_wlan;if(s->generation>1&&s->wire[32]==0xb0){assert(previous_deauth_pid&&s->wire_pid>previous_deauth_pid);uint8_t*late=station_packet(4,7,124);late[28]=15;late[36]=(uint8_t)previous_deauth_pid;late[40]=s->wire_wlan;late=station_packet(3,7,124);late[28]=15;late[36]=(uint8_t)previous_deauth_pid;late[37]=7;late[40]=s->wire_wlan;}}
 unsigned fc=s->wire[32];if(fc==0xb0||fc==0)station_mgmt(fc==0xb0?0xb0:0x10);}
 else{station_commands++;assert(p[37]==0xa0&&p[39]==t->next_sequence);
 if(p[36]==3){assert(njs_native.linked&&nst_native.state.controlled_port);assert(p[64+159]==13&&!p[64+826]);for(unsigned j=0;j<13;j++)assert(p[64+160+2*j]==1&&p[64+161+2*j]==j+1);if(scan_hold)held_sequence=p[64];else associated_frames(p[64]);if(hold_scan_dma){held_scan_next=next;return;}}
 if(p[36]==4){assert(p[64]==held_sequence&&!p[65]&&!p[66]&&!p[67]);cancel_commands++;if(hold_cancel_dma){held_cancel_next=next;return;}}
 if(p[36]==0x1c&&!p[66]){uint8_t*r=station_packet(1,7,60);r[28]=16;r[36]=s->peer.bss;r[37]=p[65];r[39]=s->peer.channel;r[41]=s->peer.band;put32(r+48,4000);}
 if(p[36]==0x13&&p[84]==SW_STA_CLASS_3&&p[115]){uint8_t*r=station_packet(1,7,44);r[28]=12;r[29]=p[39];memcpy(r+36,s->peer.ap,6);r[42]=s->peer.sta;r[43]=s->peer.bss;}
 if(p[36]==7&&p[64]&&p[66]){uint8_t*r=station_packet(1,7,44);r[28]=36;r[36]=s->peer.bss;memcpy(r+38,s->peer.ap,6);}
 if(p[36]==7&&!p[64])key_deletes++;
 }
 put32(d,0xffffffffu);put32(d+4,0xffffffffu);put32(d+8,0xdeadbeefu);put32(d+12,0x12345678u);hardware_register(0x1802541c,next);
}
static __attribute__((unused)) void join_completed(void*c,uint64_t epoch,uint64_t ticket,enum njs_notice notice){assert(c==notices&&epoch==7&&ticket&&notice<5);notices[notice]++;assert(!pthread_mutex_trylock(&nss_native.lock->m));pthread_mutex_unlock(&nss_native.lock->m);if(notice==NJS_RETIRED)assert(!njs_rejoin_ready_locked());}
/* Pending handover has its own cookie/lifetime; cancellation must not
 * replace or revive the closed native epoch while its fresh scan is pending. */
struct power_handover_notes {uint64_t ticket;unsigned failed,retired;};
static __attribute__((unused)) void handover_completed(void*cookie,uint64_t epoch,uint64_t ticket,enum njs_notice notice){
 struct power_handover_notes*n=cookie;assert(n&&epoch==91&&ticket==n->ticket);
 assert(!pthread_mutex_trylock(&nss_native.lock->m));pthread_mutex_unlock(&nss_native.lock->m);
 struct niwj_request zero={0};assert(!memcmp(&njs_handover.request,&zero,sizeof zero));
 if(notice==NJS_JOIN_FAILED){assert(!n->failed&&!n->retired);n->failed=1;}
 else{assert(notice==NJS_RETIRED&&n->failed&&!n->retired);n->retired=1;}
}
static struct niwj_request request(void){struct niwj_request q={0};q.security=1;q.ssid_length=1;q.ssid[0]='X';memcpy(q.pmk,pmk_vector,32);memcpy(q.station_rsn,rsn,22);return q;}
static __attribute__((unused)) void until_authorized(void){for(unsigned k=0;k<1000&&!njs_native.linked;k++){
 if(njs_native.driver.owner.transaction.phase==STC_EAPOL&&!m1_sent){station_eapol(m1,sizeof m1);uint8_t*early=station_packet(4,7,44);early[28]=36;early[36]=nst_native.state.peer.bss;memcpy(early+38,nst_native.state.peer.ap,6);assert(!nst_native.state.ptk_command&&!nst_native.state.ptk_ready);m1_sent++;}
 assert(checked_owner()==1&&!nst_native.state.halt);
 }assert(njs_native.linked&&nwo_authorized(&njs_native.driver.owner)&&m1_sent&&m3_sent&&nsr_native.ptk&&nsr_native.gtk);}
static __attribute__((unused)) void ledger_negatives(void){
 struct nsr_ledger l={0};struct nst*s=calloc(1,sizeof(*s));assert(s);s->wire_busy=1;s->wire_cid=7;s->ticket=1;
 struct nst_event e={.kind=NST_COMMAND_DMA,.cid=7,.ticket=1};
 s->wire[64]=1;s->wire[66]=1;nsr_key_capture(&l,s);assert(l.pending&&!l.ptk);assert(!nsr_key_complete(&l,&e)&&l.ptk&&!l.pending);
 for(unsigned k=0;k<4;k++){s->wire[66]=0;s->wire[76]=(uint8_t)k;s->ticket=++e.ticket;nsr_key_capture(&l,s);assert(!nsr_key_complete(&l,&e));}assert(l.gtk==15);
 for(unsigned k=0;k<4;k++){s->wire[64]=0;s->wire[76]=(uint8_t)k;s->ticket=++e.ticket;nsr_key_capture(&l,s);assert(l.gtk&(1u<<k));assert(!nsr_key_complete(&l,&e)&&!(l.gtk&(1u<<k)));}assert(!l.gtk&&l.ptk);
 s->wire[66]=1;s->wire[76]=0;s->ticket=++e.ticket;nsr_key_capture(&l,s);assert(!nsr_key_complete(&l,&e)&&!l.ptk);
 s->wire[64]=1;s->ticket=++e.ticket;nsr_key_capture(&l,s);e.ticket++;assert(nsr_key_complete(&l,&e)<0&&l.bad&&!l.ptk);free(s);
}
static __attribute__((unused)) void*power_off_thread(void*x){(void)x;assert(!nord2_wifi_power_set(0));return 0;}
static __attribute__((unused)) void*power_on_thread(void*x){(void)x;__atomic_store_n(&on_started,1,__ATOMIC_SEQ_CST);assert(!nord2_wifi_power_set(1));__atomic_store_n(&on_done,1,__ATOMIC_SEQ_CST);return 0;}

int main(int argc,char**argv){
 setvbuf(stdout,0,_IONBF,0);assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f&&fread(authenticated_board,1,sizeof authenticated_board,f)==sizeof authenticated_board);fclose(f);
 setup(0);run_worker();worker_thread=2;assert(nss_native.ready);struct niwj_request q=request();uint64_t ticket=0;
 assert(!nord2_wifi_join_enqueue(&q,notices,7,join_completed,&ticket));niwj_clear(&q,sizeof q);station_hardware=1;until_authorized();
 assert(nst_native.state.peer.band==2&&nst_native.state.peer.channel==52&&njs_native.linked&&nst_native.state.ptk.installed&&nst_native.state.gtk.installed);
 uint8_t data[60]={0};memcpy(data,nst_native.state.peer.ap,6);memcpy(data+6,nss_native.info.mac,6);data[12]=8;assert(!nord2_wifi_link_send(data,sizeof data,ticket));while(njs_native.tx_count||nst_native.state.wire_busy)assert(checked_owner()==1);
 struct nss_request rq={0};rq.count=13;for(unsigned j=0;j<13;j++)rq.channels[j]=(struct ncs_channel){1,j+1};uint64_t scan_ticket=0;
 scan_hold=1;assert(!nord2_wifi_scan_enqueue(&rq,&associated_canceled,89,canceled_completed,&scan_ticket));for(unsigned k=0;k<100&&!held_sequence;k++)assert(checked_owner()==1);
 assert(held_sequence&&nst_native.state.associated_scan&&nst_native.state.associated_scan->active&&njs_native.linked);unsigned sequence=held_sequence;
 /* No SCAN_DONE is supplied. CID4 still retires its actual descriptor. */
 unsigned deletes=key_deletes;assert(!nord2_wifi_power_set(0));for(unsigned k=0;k<100&&!associated_canceled;k++)assert(checked_owner()==1);
 assert(cancel_commands==1&&associated_canceled==1&&!nst_native.state.associated_scan&&key_deletes==deletes&&!nss_native.powered);
 /* Deauth receives real DDONE but no TXS. Pause3.5s while cleanup is pending. */
 for(unsigned k=0;k<100&&!previous_deauth_pid;k++)assert(checked_owner()==1);
 assert(previous_deauth_pid&&nst_native.state.dma_done&&!nst_native.state.wire_busy);unsigned deauth=previous_deauth_pid;
 drain_now+=UINT64_C(3500000000);assert(checked_owner()==1&&!nst_native.state.halt);
 /* Old SCAN_DONE plus both late air outcomes arrive only after cancel detached. */
 uint8_t*p=station_packet(4,7,512);p[28]=13;p[36]=(uint8_t)sequence;p[41]=7;p[42]=2;p[52]=13;
 p=station_packet(3,7,124);p[28]=15;p[36]=(uint8_t)deauth;p[40]=nst_native.state.peer.wlan;
 p=station_packet(0,7,124);p[28]=15;p[36]=(uint8_t)deauth;p[37]=7;p[40]=nst_native.state.peer.wlan;
 for(unsigned k=0;k<1000&&!njs_native.scan_returned;k++)assert(checked_owner()==1);
 assert(njs_native.scan_returned&&!nss_native.failed&&!nst_native.state.halt&&key_deletes==2&&notices[NJS_LEFT]==1&&notices[NJS_RETIRED]==1&&!nsr_native.ptk&&!nsr_native.gtk&&!nsr_native.pending&&!nsr_native.bad);
 assert(!nord2_wifi_power_set(1));station_hardware=0;normal_pubs+=station_commands;station_commands=0;delivered=0;drain();
 assert(nsr_native.scan_ready&&nss_return.consumed&&njs_rejoin_ready_locked());q=request();assert(!nord2_wifi_join_enqueue(&q,notices,7,join_completed,&ticket));niwj_clear(&q,sizeof q);m1_sent=m3_sent=0;station_hardware=1;until_authorized();
 assert(ticket==2&&njs_native.linked&&nst_native.state.peer.band==2&&nst_native.state.peer.channel==52&&nst_native.state.wire_pid>deauth&&!nss_native.failed&&!manual_halts);
 /* Close the second epoch too, preserving real key/peer cleanup. */
 assert(!nord2_wifi_power_set(0));for(unsigned k=0;k<1000&&!njs_native.scan_returned;k++)assert(checked_owner()==1);
 assert(njs_native.scan_returned&&notices[NJS_LEFT]==2&&notices[NJS_RETIRED]==2&&key_deletes==4&&!nss_native.failed&&!nst_native.state.halt);
 assert(delivered&&nrd_calls==nrd_restored&&nrd_checked==nrd_calls);
 assert(!held_scan_next&&!held_cancel_next&&!lifecycle_rx&&station_pubs>station_frames&&link_up_calls==2&&link_down_calls>=2&&!clock_expire&&random_calls==2);
 IOLock*notice=njs_native.notice_lock;assert(!pthread_mutex_destroy(&notice->m));free(notice);assert(!pthread_mutex_destroy(&nss_native.lock->m));free(nss_native.lock);
 puts("PASS exactT1 actual-owner synthetic5GHz52 +1..13 associatedscan, OFF/CID4DDONE withoutSCANDONE, deauthDDONE/noTXS,3.5s quiet thenlateSCAN/TXS, actualkey/peercleanup LEFT RETIRED scanreturn ON/freshscan/rejoin; no hardwarecrash reproduction");return 0;
}
