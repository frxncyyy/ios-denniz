/* 0450: WPA2-Enterprise (802.1X) glue of the native station service, as a NEW
 * object. Nothing prebuilt is recompiled for it and sweet_touch.o (native
 * binder, scan path) is unchanged:
 *  - the five entry points of the CI member wifi_wpa_nst.o are renamed in the
 *    archive copy (nwn_* -> nwc_*, symbol names only, same code); the binder's
 *    unchanged calls to nwn_begin/pump/event/peer_complete/cancel land in the
 *    wrappers below, which forward WPA2-PSK and OPEN attempts to nwc_* as they
 *    are and run 802.1X attempts through the e8_ instance of the stack
 *    (wifi_8021x_rename.h);
 *  - an 802.1X attempt is recognised by the marker that wifi_ios_join.c puts
 *    in place of the PMK (n8x_marker). The wrapper starts it without a PMK,
 *    with the AKM in the station RSN element and with N8X_BUDGET_MS instead
 *    of the binder's 8 s (the native transport allows 60 s unauthorized);
 *  - when the station is configured (STC_EAPOL, STA active) the BSD link goes
 *    up so that eapolclient starts; the controlled port stays closed. EAPOL
 *    frames from the AP that are not EAPOL-Key go to the interface input
 *    (PF_NDRV socket of eapolclient), EAPOL-Key goes to the WPA engine, which
 *    keeps the newest one until the PMK exists. EAPOL frames written by
 *    eapolclient are taken by an interface output filter (the BSD endpoint of
 *    wifi_ios_registry.cpo refuses ethertype 0x888e and is not rebuilt) and
 *    sent to the AP, unencrypted before the keys, encrypted after;
 *  - APPLE80211_IOC_CIPHER_KEY of eapolclient (MSK 64 bytes or PMK 32, through
 *    the supplicant user client of 0451) starts the engine: 4-way handshake,
 *    key install, port open, link/JOINED by the unchanged binder as for PSK.
 * The wrappers run on the retained owner thread under the binder's admission
 * lock; the interface work runs later on a thread call without that lock.
 * No frame bytes, addresses or key bytes are logged. */
#include "wifi_8021x_glue.h"
#include "wifi_8021x.h"
#include "wifi_wpa_nst.h"
#include "wifi_ifnet_link.h"
#if defined(KERNEL) || defined(_KERNEL)
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <sys/kpi_mbuf.h>
#include <net/if.h>
#include <net/kpi_interface.h>
#include <net/kpi_interfacefilter.h>
#include <net/bpf.h>
#include <kern/thread_call.h>
#include <IOKit/IOLocks.h>
#define N8X_KERNEL 1
#else
#include <string.h>
#include <stdio.h>
/* Host harness: one thread. The test calls n8x_host_work() where the kernel
 * runs the thread call, supplies n8x_host_input() for the interface input and
 * calls n8x_output_frame() where the interface output filter does. */
int n8x_host_input(const uint8_t*,size_t);
void n8x_host_work(void);
#endif
#define N8X_SLOTS 4u
#define N8X_BUDGET_MS 55000u
#define N8X_TICKET (UINT64_MAX-7)
#define N8X_FRAME_LOGS 96u
/* The CI entry points (renamed members of wifi_wpa_nst.o) and the 802.1X instance. */
int nwc_begin(struct nwn*,struct nst*,struct stc_arbiter*,const struct stc_config*,nwn_peer_submit,void*,uint64_t);
int nwc_pump(struct nwn*,uint64_t);
int nwc_event(struct nwn*,const struct nst_event*,uint64_t);
int nwc_peer_complete(struct nwn*,uint64_t,uint64_t,unsigned,int,uint64_t);
int nwc_cancel(struct nwn*,uint64_t,uint64_t);
int e8_nwn_begin(struct nwn*,struct nst*,struct stc_arbiter*,const struct stc_config*,nwn_peer_submit,void*,uint64_t);
int e8_nwn_pump(struct nwn*,uint64_t);
int e8_nwn_event(struct nwn*,const struct nst_event*,uint64_t);
int e8_nwn_peer_complete(struct nwn*,uint64_t,uint64_t,unsigned,int,uint64_t);
int e8_nwn_cancel(struct nwn*,uint64_t,uint64_t);
int e8_stc_pmk(struct stc*,uint64_t,const uint8_t pmk[32],uint64_t);
int e8_stc_eap_waiting(const struct stc*);
void e8_nwp_rekey_pmk(const uint8_t pmk[32]);
extern unsigned e8_nwn_rekey_installs;
/* 0453: set by the supplicant user client; called on the thread call when the keys of an 802.1X link are in place. */
void (*n8x_port_open_hook)(void);
int n8x_output_frame(const uint8_t*,size_t);
static struct {
#ifdef N8X_KERNEL
 IOLock*lock;thread_call_t call;interface_filter_t filter;
#endif
 unsigned ready,working,again;
 /* the attempt: written by the wrappers only (owner thread) */
 unsigned eap,ended,akm,tried,started,port_seen,ssid_len,band,channel;
 uint64_t generation;const struct nwn*owner;
 uint8_t ssid[32],bssid[6],self[6];
 /* shared with the thread call, the output filter and the supplicant client */
 unsigned linked,want_up,want_down,want_notify,key_pending,reauth,rekey_seen,rx_head,rx_count,tx_head,tx_count;
 uint64_t rekey_reset_ms;
 uint64_t down_generation;
 unsigned rx_seen,rx_up,rx_drop,tx_seen,tx_sent,tx_drop,tx_done,keys_set,keys_ignored,frame_logs;
 uint8_t key[32];
 uint8_t rx[N8X_SLOTS][NWIL_ETHERNET_MAX],tx[N8X_SLOTS][NWIL_ETHERNET_MAX],scratch[NWIL_ETHERNET_MAX];
 unsigned rx_bytes[N8X_SLOTS],tx_bytes[N8X_SLOTS];
} g;
#ifdef N8X_KERNEL
SYSCTL_UINT(_kern, OID_AUTO, n2wifi_8021x, CTLFLAG_RW | CTLFLAG_LOCKED, &niwj_8021x, 0, "0450/0452 WPA2-Enterprise bits: 1 802.1X joins (clear: profile refused), 2 PTK rekey + EAP re-authentication on the link, 0x10 handshake-complete message for eapolclient");
static void n8x_call(thread_call_param_t,thread_call_param_t);
#define N8X_LOCK() IOLockLock(g.lock)
#define N8X_UNLOCK() IOLockUnlock(g.lock)
static void n8x_init(void){ /* first join of the boot, owner thread */
 if(g.ready)return;
 if(!g.lock)g.lock=IOLockAlloc();
 if(!g.call)g.call=thread_call_allocate(n8x_call,NULL);
 g.ready=g.lock&&g.call;
}
static void n8x_defer(void){if(g.ready)(void)thread_call_enter(g.call);}
#else
#define N8X_LOCK() ((void)0)
#define N8X_UNLOCK() ((void)0)
static void n8x_init(void){g.ready=1;}
static void n8x_defer(void){}
#endif
static void n8x_clear(void*v,size_t n){volatile uint8_t*p=v;while(n--)*p++=0;}
static void p32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static int mine(const struct nwn*s){return g.eap&&s&&g.owner==s&&s->owner.transaction.generation==g.generation;}
static int live_locked(void){return g.eap&&!g.ended&&(g.linked||g.port_seen);}
/* Lock held. Frames and the key of an attempt never outlive it; a link this
 * glue raised and the binder did not take over is revoked. */
static void n8x_end_locked(void){
 if(g.linked&&!g.port_seen){g.want_down=1;g.down_generation=g.generation;}
 g.linked=0;g.want_up=0;g.key_pending=0;g.reauth=0;g.rekey_reset_ms=0;g.rx_head=g.rx_count=g.tx_head=g.tx_count=0;
 n8x_clear(g.key,sizeof g.key);n8x_clear(g.rx,sizeof g.rx);n8x_clear(g.tx,sizeof g.tx);n8x_clear(g.rx_bytes,sizeof g.rx_bytes);n8x_clear(g.tx_bytes,sizeof g.tx_bytes);
}
/* ---- wrappers: owner thread, binder admission lock held ---- */
int nwn_begin(struct nwn*s,struct nst*t,struct stc_arbiter*a,const struct stc_config*c,nwn_peer_submit peer,void*ctx,uint64_t now){
 unsigned akm=c&&c->security==SW_SECURITY_WPA2&&c->pmk?n8x_marker_akm(c->pmk):0,down;
 n8x_init();
 if(g.ready){N8X_LOCK();n8x_end_locked();g.eap=0;g.ended=1;down=g.want_down;N8X_UNLOCK();if(down)n8x_defer();}
 if(!akm)return nwc_begin(s,t,a,c,peer,ctx,now);
 if(!(niwj_8021x&1u)||!g.ready||!s||!t||now>UINT64_MAX/1000-N8X_BUDGET_MS){printf("NORD2 WIFI 8021X: join refused enabled=%u ready=%u\n",niwj_8021x,g.ready);return -1;}
 struct stc_config e=*c;uint8_t rsn[22];uint64_t transport_deadline=t->deadline;
 n8x_station_rsn(akm,rsn);e.pmk=0;e.station_rsn=rsn;e.deadline_ms=now+N8X_BUDGET_MS;
 t->deadline=e.deadline_ms*1000; /* unauthorized period of the native transport, same clock in us */
 int r=e8_nwn_begin(s,t,a,&e,peer,ctx,now);
 n8x_clear(&e,sizeof e);
 if(r!=1){t->deadline=transport_deadline;printf("NORD2 WIFI 8021X: join start refused akm=%u result=%d\n",akm,r);return r;}
 N8X_LOCK();
 g.eap=1;g.ended=0;g.akm=akm;g.tried=g.started=g.port_seen=0;g.generation=c->generation;g.owner=s;g.rekey_seen=e8_nwn_rekey_installs;
 g.rx_seen=g.rx_up=g.rx_drop=g.tx_seen=g.tx_sent=g.tx_drop=g.tx_done=g.keys_set=g.keys_ignored=g.frame_logs=0;
 g.ssid_len=c->assoc.ssid_len<=32?c->assoc.ssid_len:0;memcpy(g.ssid,c->assoc.ssid,g.ssid_len);memcpy(g.bssid,c->assoc.peer.ap,6);memcpy(g.self,c->assoc.peer.self,6);
 g.band=c->channel.band;g.channel=c->channel.channel;
 N8X_UNLOCK();
 printf("NORD2 WIFI 8021X: join start generation=%llu akm=%u budget_ms=%u\n",(unsigned long long)c->generation,akm,N8X_BUDGET_MS);
 return r;
}
/* Lock held, before the transaction pump: hand the PMK to the engine. */
static void n8x_key_locked(struct nwn*s,uint64_t now){
 struct stc*t=&s->owner.transaction;
 if(!g.key_pending)return;
 if(!g.ended&&g.port_seen){ /* 0452: PMK of a re-authentication, used from the AP's next message 1 */
  e8_nwp_rekey_pmk(g.key);n8x_clear(g.key,sizeof g.key);g.key_pending=0;g.reauth=0;
  printf("NORD2 WIFI 8021X: pmk received (reauthentication) generation=%llu\n",(unsigned long long)g.generation);return;
 }
 if(g.ended||!e8_stc_eap_waiting(t)){n8x_clear(g.key,sizeof g.key);g.key_pending=0;return;}
 if(s->owner.halted||s->owner.issuing||t->phase!=STC_EAPOL)return; /* association still completing */
 unsigned waiting=t->eapol_pending;int r=e8_stc_pmk(t,g.generation,g.key,now);
 if(!r)return;
 n8x_clear(g.key,sizeof g.key);g.key_pending=0;g.started=r>0;
 printf("NORD2 WIFI 8021X: pmk received generation=%llu t_ms=%llu\n",(unsigned long long)g.generation,(unsigned long long)now);
 printf("NORD2 WIFI 8021X: 4way start akm=%u version=%u message1_waiting=%u result=%d\n",g.akm,g.akm==N8X_AKM_8021X_SHA256?3u:2u,waiting,r);
}
/* Lock held, after the pump: one userland EAPOL frame when the transport and
 * the handshake engine are idle. */
static void n8x_tx_locked(struct nwn*s){
 const struct stc*t=&s->owner.transaction;struct nst*n=s->transport;
 if(!g.tx_count)return;
 unsigned k=g.tx_head,protect=n->ptk_ready&&n->ptk.installed&&n->controlled_port;int sent=-1;
 if(!g.ended&&t->phase<STC_DEAUTH){
  if(n->wire_busy||n->air_pending||n->peer_kind||s->owner.in_ticket||t->action.kind||t->phase<STC_EAPOL)return;
  sent=nst_eapol(n,N8X_TICKET,g.tx[k],g.tx_bytes[k],protect);
  if(sent>0)return;
 }
 if(sent<0)g.tx_drop++;else g.tx_sent++;
 if(g.frame_logs<N8X_FRAME_LOGS){g.frame_logs++;printf("NORD2 WIFI 8021X: eapol tx type=%u len=%u protected=%u status=%d\n",(unsigned)g.tx[k][15],g.tx_bytes[k]-14,protect,sent);}
 n8x_clear(g.tx[k],sizeof g.tx[k]);g.tx_bytes[k]=0;g.tx_head=(k+1)%N8X_SLOTS;g.tx_count--;
}
/* Lock held: link decisions of the attempt. 1 = the thread call has work. */
static unsigned n8x_state_locked(const struct nwn*s){
 const struct stc*t=&s->owner.transaction;const struct nst*n=s->transport;unsigned work=0;
 if(!g.ended&&t->phase>=STC_DEAUTH){
  printf("NORD2 WIFI 8021X: attempt over generation=%llu outcome=%u phase=%u keys=%u started=%u port=%u eapol_rx=%u eapol_tx=%u\n",(unsigned long long)g.generation,t->outcome,t->phase,g.keys_set,g.started,g.port_seen,g.rx_up,g.tx_sent);
  g.ended=1;n8x_end_locked();work=g.want_down;
 }
 if(!g.ended&&!g.tried&&t->phase==STC_EAPOL&&t->associated&&n->sta_active){g.tried=1;g.want_up=1;work=1;} /* once per attempt */
 if(!g.ended&&!g.port_seen&&n->controlled_port&&t->outcome==STC_AUTHORIZED){
  g.port_seen=1;g.want_notify=1;work=1; /* the binder announces its own link now; the interface stays up */
  printf("NORD2 WIFI 8021X: keys installed generation=%llu port=open eapol_rx=%u eapol_tx=%u\n",(unsigned long long)g.generation,g.rx_up,g.tx_sent);
 }
 if(g.rx_count&&live_locked())work=1;
 return work;
}
int nwn_pump(struct nwn*s,uint64_t now){
 if(!mine(s))return nwc_pump(s,now);
 N8X_LOCK();n8x_key_locked(s,now);N8X_UNLOCK();
 int r=e8_nwn_pump(s,now);
 N8X_LOCK();if(r>=0)n8x_tx_locked(s);unsigned work=n8x_state_locked(s);
 /* 0452: a rekeyed PTK went to the firmware; restart the pairwise replay counters once frames under the old key are gone */
 if(e8_nwn_rekey_installs!=g.rekey_seen){g.rekey_seen=e8_nwn_rekey_installs;g.rekey_reset_ms=now+100;}
 if(g.rekey_reset_ms&&now>=g.rekey_reset_ms&&!g.ended){
  static const uint8_t zero[8];struct nst*n=s->transport;n->ptk.installed=0;int e=nst_replay_install(n,1,0,zero);g.rekey_reset_ms=0;g.want_notify=1;work=1;
  printf("NORD2 WIFI 8021X: ptk rekey installed generation=%llu replay_restart=%d\n",(unsigned long long)g.generation,e);
 }
 N8X_UNLOCK();
 if(work)n8x_defer();
 return r;
}
int nwn_event(struct nwn*s,const struct nst_event*e,uint64_t now){
 if(!mine(s))return nwc_event(s,e,now);
 if(e&&e->generation==g.generation){
  if(e->kind==NST_SECURITY_SUBMITTED&&e->ticket==N8X_TICKET){N8X_LOCK();g.tx_done++;N8X_UNLOCK();return 0;} /* receipt of a userland frame */
  if(e->kind==NST_EAPOL&&e->body&&e->bytes>=18&&e->bytes<=NWIL_ETHERNET_MAX&&e->body[12]==0x88&&e->body[13]==0x8e&&e->body[15]!=3&&
     !memcmp(e->body,s->transport->peer.self,6)&&!memcmp(e->body+6,s->transport->peer.ap,6)){
   /* EAP packet (or any EAPOL that is not EAPOL-Key): for eapolclient, never for the WPA engine. */
   N8X_LOCK();g.rx_seen++;
   if(g.port_seen&&e->bytes>=19&&e->body[15]==0&&e->body[18]==1)g.reauth=1; /* 0452: EAP-Request on an authorized link: a new PMK is coming */
   if(g.ended||g.rx_count==N8X_SLOTS)g.rx_drop++;
   else{unsigned k=(g.rx_head+g.rx_count++)%N8X_SLOTS;memcpy(g.rx[k],e->body,e->bytes);g.rx_bytes[k]=(unsigned)e->bytes;}
   N8X_UNLOCK();return 0;
  }
 }
 /* 0452: message 1 of the rekey after a re-authentication waits for the new PMK (the AP repeats it) */
 if(e&&e->kind==NST_EAPOL&&e->body&&e->bytes>=21&&e->body[15]==3&&!(e->body[19]&1)&&g.port_seen&&g.reauth)return 0;
 return e8_nwn_event(s,e,now);
}
int nwn_peer_complete(struct nwn*s,uint64_t generation,uint64_t ticket,unsigned type,int success,uint64_t now){
 return mine(s)?e8_nwn_peer_complete(s,generation,ticket,type,success,now):nwc_peer_complete(s,generation,ticket,type,success,now);
}
int nwn_cancel(struct nwn*s,uint64_t generation,uint64_t now){
 if(!mine(s))return nwc_cancel(s,generation,now);
 int r=e8_nwn_cancel(s,generation,now);
 N8X_LOCK();unsigned work=n8x_state_locked(s);N8X_UNLOCK();
 if(work)n8x_defer();
 return r;
}
/* ---- other threads ---- */
/* EAPOL frame written to the interface (eapolclient). 0 = not ours (no live
 * 802.1X association: the driver refuses it as before), 1 = taken. The kernel
 * owns the 4-way handshake: EAPOL-Key from userland is dropped. Destination
 * becomes the AP (the PAE group address means nothing on an infrastructure BSS). */
int n8x_output_frame(const uint8_t*p,size_t n){
 if(!g.ready||!p||n<14||n>NWIL_ETHERNET_MAX||p[12]!=0x88||p[13]!=0x8e)return 0;
 N8X_LOCK();int taken=(niwj_8021x&1u)&&live_locked();
 if(taken){
  if(n<18||p[15]==3||memcmp(p+6,g.self,6)||g.tx_count==N8X_SLOTS)g.tx_drop++;
  else{unsigned k=(g.tx_head+g.tx_count++)%N8X_SLOTS;memcpy(g.tx[k],p,n);memcpy(g.tx[k],g.bssid,6);g.tx_bytes[k]=(unsigned)n;g.tx_seen++;}
 }
 N8X_UNLOCK();return taken;
}
int nord2_wifi_8021x_key(const uint8_t*key,size_t bytes){
 if(!key||(bytes!=32&&bytes!=64))return -1;
 if(!g.ready)return 1;
 N8X_LOCK();int r=1;unsigned eap=g.eap&&!g.ended;
 /* Before the engine started, or (0452, bit 2) on the authorized link after a re-authentication. */
 if(eap&&((!g.started&&!g.port_seen)||(g.port_seen&&(niwj_8021x&2u)))){memcpy(g.key,key,32);g.key_pending=1;g.keys_set++;r=0;}else g.keys_ignored++;
 N8X_UNLOCK();
 printf("NORD2 WIFI 8021X: key from supplicant bytes=%u taken=%u eap_join=%u\n",(unsigned)bytes,(unsigned)!r,eap);
 return r;
}
int nord2_wifi_8021x_get(unsigned request,size_t payload,void*out,size_t capacity,size_t*used){
 uint8_t*p=out,ssid[32],bssid[6];size_t bytes=0;unsigned associated,akm,band,channel,ssid_len;
 if(!used)return -1;*used=0;if(!p)return -1;
 if(request!=1&&request!=9&&request!=2&&request!=4&&request!=13)return -2;
 if(request==13){if(payload||capacity<4)return -1;}
 else if(request==1){if(!payload||payload>32||!capacity)return -1;}
 else{bytes=request==9?6:16;if(payload!=bytes||capacity<bytes)return -1;}
 if(!g.ready){if(request==13){p32(p,0);*used=4;return 0;}return 1;}
 N8X_LOCK();associated=(unsigned)live_locked();akm=g.akm;band=g.band;channel=g.channel;ssid_len=g.ssid_len;memcpy(ssid,g.ssid,32);memcpy(bssid,g.bssid,6);N8X_UNLOCK();
 if(request==13){p32(p,associated?4:0);*used=4;return 0;}
 if(!associated)return 1;
 if(request==1){bytes=ssid_len;if(!bytes||capacity<bytes||payload<bytes)return -1;memcpy(p,ssid,bytes);}
 else if(request==9)memcpy(p,bssid,6);
 else if(request==2){n8x_clear(p,16);p32(p,1);p32(p+4,1);p32(p+8,akm==N8X_AKM_8021X_SHA256?0x80:4);}
 else{n8x_clear(p,16);p32(p,1);p32(p+4,1);p32(p+8,channel);p32(p+12,band==1?8:16);}
 *used=bytes;return 0;
}
/* ---- interface work: thread call, no binder lock ---- */
#ifdef N8X_KERNEL
static ifnet_t n8x_wifi(void){ /* retained, or NULL */
 for(unsigned unit=0;unit<8;unit++){
  char name[8];ifnet_t net=NULL;snprintf(name,sizeof name,"en%u",unit);
  if(ifnet_find_by_name(name,&net)||!net)continue;
  if(ifnet_subfamily(net)==IFNET_SUBFAMILY_WIFI)return net;
  ifnet_release(net);
 }
 return NULL;
}
static errno_t n8x_filter_output(void*cookie,ifnet_t net,protocol_family_t protocol,mbuf_t*data){
 (void)cookie;(void)net;(void)protocol;
 if(!data||!*data||!(mbuf_flags(*data)&MBUF_PKTHDR))return 0;
 size_t n=mbuf_pkthdr_len(*data);uint8_t head[14],frame[NWIL_ETHERNET_MAX];
 if(n<14||n>sizeof frame||mbuf_copydata(*data,0,14,head)||head[12]!=0x88||head[13]!=0x8e)return 0; /* everything else passes untouched */
 if(mbuf_copydata(*data,0,n,frame)||!n8x_output_frame(frame,n))return 0;
 mbuf_freem(*data);return EJUSTRETURN;
}
static void n8x_filter_detached(void*cookie,ifnet_t net){(void)cookie;(void)net;}
/* Once: EAPOL written to the interface never reaches the BSD endpoint of the
 * CI registry (it refuses 0x888e); this filter takes it during an 802.1X join. */
static int n8x_filter(ifnet_t net){
 static const struct iff_filter filter={.iff_cookie=NULL,.iff_name="nord2.wifi.8021x",.iff_protocol=0,.iff_input=NULL,.iff_output=n8x_filter_output,.iff_event=NULL,.iff_ioctl=NULL,.iff_detached=n8x_filter_detached};
 if(g.filter)return 0;
 int error=(int)iflt_attach(net,&filter,&g.filter);if(error)g.filter=NULL;
 printf("NORD2 WIFI 8021X: eapol output filter attached error=%d\n",error);return error;
}
static int n8x_input(ifnet_t net,const uint8_t*frame,size_t length){
 mbuf_t packet=NULL;int error=(int)mbuf_getpacket(MBUF_DONTWAIT,&packet);
 if(!error&&mbuf_maxlen(packet)<length)error=ENOBUFS;
 if(error){if(packet)mbuf_freem(packet);return error;}
 memcpy(mbuf_data(packet),frame,length);mbuf_setlen(packet,length);mbuf_pkthdr_setlen(packet,length);
 error=(int)mbuf_pkthdr_setrcvif(packet,net);
 if(!error){
  void*header=mbuf_data(packet);mbuf_pkthdr_setheader(packet,header);mbuf_pkthdr_setlen(packet,length-14);
  error=(int)mbuf_setdata(packet,(uint8_t*)header+14,length-14);
  if(!error){
   struct ifnet_stat_increment_param stats;bzero(&stats,sizeof stats);stats.packets_in=1;stats.bytes_in=(uint32_t)length;
   bpf_tap_in(net,DLT_EN10MB,packet,header,14);
   error=(int)ifnet_input(net,packet,&stats);packet=NULL; /* consumed even on failure */
  }
 }
 if(packet)mbuf_freem(packet);
 return error;
}
#endif
static void n8x_work(void){
 for(unsigned round=0;round<4u*N8X_SLOTS;round++){
  unsigned up,down,notify,bytes=0,log=0,akm;uint64_t generation,down_generation;
  N8X_LOCK();notify=g.want_notify&&(niwj_8021x&0x10u)&&g.eap&&!g.ended;g.want_notify=0;
  down=g.want_down;down_generation=g.down_generation;g.want_down=0;
  up=g.want_up&&g.eap&&!g.ended;g.want_up=0;generation=g.generation;akm=g.akm;
  if(!up&&!down&&g.rx_count){
   unsigned k=g.rx_head;
   if(live_locked()){bytes=g.rx_bytes[k];memcpy(g.scratch,g.rx[k],bytes);if(g.frame_logs<N8X_FRAME_LOGS){g.frame_logs++;log=1;}}
   if(bytes||g.ended){n8x_clear(g.rx[k],sizeof g.rx[k]);g.rx_bytes[k]=0;g.rx_head=(k+1)%N8X_SLOTS;g.rx_count--;if(!bytes)g.rx_drop++;}
  }
  N8X_UNLOCK();
  if(notify&&n8x_port_open_hook)n8x_port_open_hook(); /* 0453: eapolclient's "4-way handshake complete" */
  if(down){int error=nord2_wifi_ifnet_link(down_generation,0);printf("NORD2 WIFI 8021X: link down generation=%llu error=%d\n",(unsigned long long)down_generation,error);}
  if(up){
   int error;
#ifdef N8X_KERNEL
   ifnet_t net=n8x_wifi();error=net?n8x_filter(net):ENXIO;if(net)ifnet_release(net);
   if(!error)
#endif
   error=nord2_wifi_ifnet_link(generation,1);
   N8X_LOCK();unsigned ok=!error&&g.eap&&!g.ended&&g.generation==generation;if(ok)g.linked=1;N8X_UNLOCK();
   if(!error&&!ok)(void)nord2_wifi_ifnet_link(generation,0);
   printf("NORD2 WIFI 8021X: assoc ok generation=%llu akm=%u link=%u error=%d (port closed, EAPOL only, waiting for pmk up to %u ms from join start)\n",(unsigned long long)generation,akm,ok,error,N8X_BUDGET_MS);
  }
  if(bytes){
   int error;
#ifdef N8X_KERNEL
   ifnet_t net=n8x_wifi();error=net?n8x_input(net,g.scratch,bytes):ENXIO;if(net)ifnet_release(net);
#else
   error=n8x_host_input(g.scratch,bytes);
#endif
   N8X_LOCK();if(error)g.rx_drop++;else g.rx_up++;N8X_UNLOCK();
   if(log)printf("NORD2 WIFI 8021X: eapol rx type=%u len=%u error=%d\n",(unsigned)g.scratch[15],bytes-14,error);
   n8x_clear(g.scratch,sizeof g.scratch);
  }
  if(!up&&!down&&!bytes&&!notify)break;
 }
}
/* One worker at a time: a thread call may be entered again while it runs. */
static void n8x_serial(void){
 N8X_LOCK();if(g.working){g.again=1;N8X_UNLOCK();return;}g.working=1;N8X_UNLOCK();
 for(;;){
  N8X_LOCK();g.again=0;N8X_UNLOCK();
  n8x_work();
  N8X_LOCK();unsigned more=g.again;if(!more)g.working=0;N8X_UNLOCK();
  if(!more)break;
 }
}
#ifdef N8X_KERNEL
static void n8x_call(thread_call_param_t p0,thread_call_param_t p1){(void)p0;(void)p1;n8x_serial();}
#else
void n8x_host_work(void){if(g.ready)n8x_serial();}
#endif

