/* 0451: WPA2-Enterprise (802.1X) glue of the native station service. Included
 * by wifi_station_service_bind.inc after njs_native / njs_handover; same
 * retained owner thread, same admission lock (nss_native.lock).
 *
 * An 802.1X join associates like WPA2-PSK (station RSN with AKM 1 or 5) but
 * without a PMK. When the station is configured (STC_EAPOL, STA active):
 *  - the BSD link goes up ("prelink") so that eapolclient starts; the native
 *    controlled port stays closed: only ethertype 0x888e passes, both ways;
 *  - EAPOL frames from the AP that are not EAPOL-Key (EAP packets) go up to
 *    the interface input (PF_NDRV / BPF of eapolclient); EAPOL-Key frames go
 *    to the host WPA2 engine, which keeps the newest one until the PMK exists;
 *  - EAPOL frames written by eapolclient are sent to the AP unencrypted
 *    (encrypted with the PTK after the keys exist: reauthentication);
 *  - APPLE80211_IOC_CIPHER_KEY (MSK 64 bytes, or PMK 32) from eapolclient
 *    starts the engine: 4-way handshake, key install, port open as for PSK.
 * The whole exchange has N8XB_BUDGET_MS from admission (the native transport
 * refuses unauthorized periods over 60 s). No frame bytes, addresses or key
 * bytes are logged. */
#define N8XB_SLOTS 4u
#define N8XB_BUDGET_MS 55000u
#define N8XB_TICKET (UINT64_MAX-7)
#define N8XB_FRAME_LOGS 96u
static void njs_snapshot_locked(void);
static struct {
 unsigned prelinked,tried,key_pending,started,port_logged,rx_head,rx_count,tx_head,tx_count;
 unsigned rx_seen,rx_up,rx_drop,tx_seen,tx_sent,tx_drop,tx_done,keys_set,keys_ignored,frame_logs;
 uint64_t generation; /* attempt that owns the prelink */
 uint8_t key[32];
 uint8_t rx[N8XB_SLOTS][NWIL_ETHERNET_MAX],tx[N8XB_SLOTS][NWIL_ETHERNET_MAX],scratch[NWIL_ETHERNET_MAX];
 unsigned rx_bytes[N8XB_SLOTS],tx_bytes[N8XB_SLOTS];
} n8xb;
#ifndef NJS_HOST_TEST
SYSCTL_UINT(_kern, OID_AUTO, n2wifi_8021x, CTLFLAG_RW | CTLFLAG_LOCKED, &niwj_8021x, 0, "0450: WPA2-Enterprise (802.1X) joins: 0 refuse the profile, 1 associate, pass EAPOL to eapolclient, 4-way handshake with its PMK");
#endif
static unsigned n8xb_akm(void){return njs_native.target.security==NIWJ_WPA2_8021X?N8X_AKM_8021X:N8X_AKM_8021X_SHA256;}
static int n8xb_active_locked(void){return njs_native.active&&niwj_eap(njs_native.target.security);}
static int n8xb_request_ok(const struct niwj_request*q){
 static const uint8_t zero[32]={0};
 return niwj_8021x&&niwj_eap(q->security)&&(q->native_flags==0||q->native_flags==4)&&
  n8x_rsn_akm(q->station_rsn)==(q->security==NIWJ_WPA2_8021X?N8X_AKM_8021X:N8X_AKM_8021X_SHA256)&&!memcmp(q->pmk,zero,32);
}
static uint64_t n8xb_budget_ms(const struct niwj_request*q){return niwj_eap(q->security)?N8XB_BUDGET_MS:8000u;}
/* New admitted attempt: nothing of an older exchange survives. The prelink
 * flag is kept; the owner loop revokes a stale one before anything else. */
static void n8xb_reset_locked(void){
 unsigned prelinked=n8xb.prelinked;uint64_t generation=n8xb.generation;
 niwj_clear(&n8xb,sizeof n8xb);n8xb.prelinked=prelinked;n8xb.generation=generation;
}
/* nst_step callback, lock held. 1 = consumed (EAP packet for userland). */
static int n8xb_rx_locked(const struct nst_event*e){
 if(e->kind==NST_SECURITY_SUBMITTED&&e->ticket==N8XB_TICKET){n8xb.tx_done++;return 1;}
 if(e->kind!=NST_EAPOL||!n8xb_active_locked()||!e->body||e->bytes<18||e->bytes>NWIL_ETHERNET_MAX||e->body[15]==3)return 0;
 n8xb.rx_seen++;
 if(njs_native.canceled||njs_native.leave||n8xb.rx_count==N8XB_SLOTS){n8xb.rx_drop++;return 1;}
 unsigned k=(n8xb.rx_head+n8xb.rx_count++)%N8XB_SLOTS;memcpy(n8xb.rx[k],e->body,e->bytes);n8xb.rx_bytes[k]=(unsigned)e->bytes;return 1;
}
/* Interface output of an EAPOL frame (eapolclient). The kernel owns the
 * 4-way handshake: EAPOL-Key from userland is refused. Destination becomes
 * the AP (the PAE group address is not usable on an infrastructure BSS). */
static int n8xb_send(const uint8_t*p,size_t n,uint64_t generation){
 if(n<18||p[15]==3)return NWIL_SEND_INVALID;
 if(!nss_available())return NWIL_SEND_NOT_READY;
 IOLockLock(nss_native.lock);int r=NWIL_SEND_NOT_READY;const struct stc*t=&njs_native.driver.owner.transaction;
 if(memcmp(p+6,nss_native.info.mac,6)||!niwj_8021x)r=NWIL_SEND_INVALID;
 else if(nss_native.powered&&!nss_native.failed&&n8xb_active_locked()&&!njs_native.canceled&&!njs_native.leave&&njs_native.native_generation==generation&&
  (n8xb.prelinked||njs_native.linked)&&t->phase>=STC_EAPOL&&t->phase<STC_DEAUTH){
  if(n8xb.tx_count==N8XB_SLOTS)r=NWIL_SEND_BUSY;
  else{unsigned k=(n8xb.tx_head+n8xb.tx_count++)%N8XB_SLOTS;memcpy(n8xb.tx[k],p,n);memcpy(n8xb.tx[k],nst_native.state.peer.ap,6);n8xb.tx_bytes[k]=(unsigned)n;n8xb.tx_seen++;r=NWIL_SEND_ACCEPTED;}
 }
 IOLockUnlock(nss_native.lock);if(!r)thread_wakeup((event_t)&nss_native);return r;
}
/* IOUC thread: key of APPLE80211_IOC_CIPHER_KEY. 64 bytes = MSK (PMK is its
 * first half), 32 = PMK. 0 taken, 1 no 802.1X association waits for one,
 * -1 invalid. The caller's copy is the caller's to wipe. */
int nord2_wifi_8021x_key(const uint8_t*key,size_t bytes){
 if(!key||(bytes!=32&&bytes!=64))return -1;
 if(!nss_available())return 1;
 IOLockLock(nss_native.lock);int r=1;
 if(n8xb_active_locked()&&!njs_native.canceled&&!njs_native.leave&&stc_eap_waiting(&njs_native.driver.owner.transaction)){memcpy(n8xb.key,key,32);n8xb.key_pending=1;n8xb.keys_set++;r=0;}
 else n8xb.keys_ignored++;
 unsigned waiting=(unsigned)!r,eap=(unsigned)n8xb_active_locked();IOLockUnlock(nss_native.lock);
 printf("NORD2 WIFI 8021X: key from supplicant bytes=%u taken=%u eap_join=%u\n",(unsigned)bytes,waiting,eap);
 if(!r)thread_wakeup((event_t)&nss_native);return r;
}
/* Owner, lock held, before the transaction pump: hand the PMK to the engine. */
static void n8xb_key_locked(uint64_t now_us){
 struct stc*t=&njs_native.driver.owner.transaction;
 if(!n8xb.key_pending)return;
 if(!n8xb_active_locked()||njs_native.canceled||njs_native.leave||!stc_eap_waiting(t)){niwj_clear(n8xb.key,sizeof n8xb.key);n8xb.key_pending=0;return;}
 if(njs_native.driver.owner.halted||njs_native.driver.owner.issuing||t->phase!=STC_EAPOL)return; /* association still completing */
 unsigned waiting=t->eapol_pending;int r=stc_pmk(t,njs_native.native_generation,n8xb.key,now_us/1000);
 if(!r)return;
 niwj_clear(n8xb.key,sizeof n8xb.key);n8xb.key_pending=0;n8xb.started=r>0;
 printf("NORD2 WIFI 8021X: pmk received generation=%llu t_ms=%llu\n",(unsigned long long)njs_native.native_generation,(unsigned long long)(now_us/1000));
 printf("NORD2 WIFI 8021X: 4way start akm=%u version=%u message1_waiting=%u result=%d\n",n8xb_akm(),n8xb_akm()==N8X_AKM_8021X_SHA256?3u:2u,waiting,r);
}
/* Owner, lock held, after the pump: one userland EAPOL frame when the
 * transport and the handshake engine are idle. */
static void n8xb_tx_locked(void){
 const struct stc*t=&njs_native.driver.owner.transaction;struct nst*n=&nst_native.state;
 if(!n8xb.tx_count)return;
 unsigned k=n8xb.tx_head,drop=!n8xb_active_locked()||njs_native.canceled||njs_native.leave||t->phase>=STC_DEAUTH;int sent=-1;
 if(!drop){
  if(n->wire_busy||n->air_pending||n->peer_kind||njs_native.driver.owner.in_ticket||t->action.kind||t->phase<STC_EAPOL)return;
  sent=nst_eapol(n,N8XB_TICKET,n8xb.tx[k],n8xb.tx_bytes[k],n->ptk_ready&&n->ptk.installed&&n->controlled_port);
  if(sent>0)return;
 }
 if(sent<0)n8xb.tx_drop++;else n8xb.tx_sent++;
 if(n8xb.frame_logs<N8XB_FRAME_LOGS){n8xb.frame_logs++;printf("NORD2 WIFI 8021X: eapol tx type=%u len=%u protected=%u status=%d\n",(unsigned)n8xb.tx[k][15],n8xb.tx_bytes[k]-14,(unsigned)(n->ptk_ready&&n->ptk.installed&&n->controlled_port),sent);}
 niwj_clear(n8xb.tx[k],sizeof n8xb.tx[k]);n8xb.tx_bytes[k]=0;n8xb.tx_head=(k+1)%N8XB_SLOTS;n8xb.tx_count--;
}
/* End of njs_snapshot_locked: the clients see an associated 802.1X link. */
static void n8xb_snapshot_locked(struct njs_snapshot*v,const struct stc*s){
 if(n8xb.prelinked&&n8xb.generation==njs_native.native_generation&&n8xb_active_locked()&&!njs_native.canceled&&!njs_native.leave&&s->phase>=STC_EAPOL&&s->phase<STC_DEAUTH&&s->associated&&nst_native.state.sta_active){v->phase=NJS_AUTHORIZED;v->result=1;}
}
static int n8xb_live_locked(void){return n8xb.prelinked&&n8xb.generation==njs_native.native_generation&&n8xb_active_locked();}
/* Owner, no lock held, after the ordinary link transitions. */
static void n8xb_link_work(unsigned*notify_join){
 IOLockLock(nss_native.lock);
 const struct stc*t=&njs_native.driver.owner.transaction;unsigned up=0,down=0,port=0,outcome=t->outcome,phase=t->phase;uint64_t generation=njs_native.native_generation,old=n8xb.generation;
 if(n8xb.prelinked){
  if(njs_native.linked&&old==generation)n8xb.prelinked=0; /* the authorized link owns the interface now */
  else if(old!=generation||!njs_native.active||njs_native.canceled||njs_native.leave||phase>=STC_DEAUTH){n8xb.prelinked=0;down=1;}
 }else if(n8xb_active_locked()&&!njs_native.canceled&&!njs_native.leave&&!njs_native.linked&&phase==STC_EAPOL&&t->associated&&nst_native.state.sta_active&&!n8xb.tried){n8xb.tried=1;up=1;} /* once per attempt */
 if(n8xb_active_locked()&&njs_native.linked&&nst_native.state.controlled_port&&!n8xb.port_logged){n8xb.port_logged=1;port=1;}
 unsigned key=n8xb.keys_set,rx=n8xb.rx_up,tx=n8xb.tx_sent;
 IOLockUnlock(nss_native.lock);
 if(port)printf("NORD2 WIFI 8021X: keys installed generation=%llu port=open eapol_rx=%u eapol_tx=%u\n",(unsigned long long)generation,rx,tx);
 if(down){
  IOLockLock(njs_native.notice_lock);int error=nord2_wifi_ifnet_link(old,0);IOLockUnlock(njs_native.notice_lock);
  printf("NORD2 WIFI 8021X: link down generation=%llu outcome=%u phase=%u keys=%u eapol_rx=%u eapol_tx=%u error=%d\n",(unsigned long long)old,outcome,phase,key,rx,tx,error);
 }
 if(!up)return;
 IOLockLock(njs_native.notice_lock);
 /* The clients must see the association before the link event reaches them. */
 IOLockLock(nss_native.lock);
 unsigned eligible=n8xb_active_locked()&&!njs_native.canceled&&!njs_native.leave&&njs_native.native_generation==generation&&njs_native.driver.owner.transaction.phase==STC_EAPOL;
 if(eligible){n8xb.prelinked=1;n8xb.generation=generation;njs_snapshot_locked();}
 IOLockUnlock(nss_native.lock);
 int error=eligible?nord2_wifi_ifnet_link(generation,1):0;
 IOLockLock(nss_native.lock);
 unsigned ok=eligible&&!error&&n8xb_live_locked()&&!njs_native.canceled&&!njs_native.leave;
 if(!ok&&n8xb.prelinked&&n8xb.generation==generation){n8xb.prelinked=0;njs_snapshot_locked();}
 if(ok&&!njs_native.joined){njs_native.joined=1;*notify_join=1;}
 IOLockUnlock(nss_native.lock);
 if(eligible&&!ok)(void)nord2_wifi_ifnet_link(generation,0);
 IOLockUnlock(njs_native.notice_lock);
 printf("NORD2 WIFI 8021X: assoc ok generation=%llu akm=%u link=%u error=%d (port closed, EAPOL only, waiting for pmk up to %u ms from admission)\n",(unsigned long long)generation,n8xb_akm(),ok,error,N8XB_BUDGET_MS);
}
/* Owner, no lock held: received EAP packets to the interface input. */
static void n8xb_deliver(void){
 for(unsigned round=0;round<N8XB_SLOTS;round++){
  unsigned bytes=0,log=0;uint64_t generation=0;
  IOLockLock(nss_native.lock);
  if(n8xb.rx_count){
   unsigned k=n8xb.rx_head,gone=!n8xb_active_locked()||njs_native.canceled||njs_native.leave;
   if(gone||n8xb_live_locked()||njs_native.linked){
    if(!gone){bytes=n8xb.rx_bytes[k];memcpy(n8xb.scratch,n8xb.rx[k],bytes);generation=njs_native.native_generation;}else n8xb.rx_drop++;
    niwj_clear(n8xb.rx[k],sizeof n8xb.rx[k]);n8xb.rx_bytes[k]=0;n8xb.rx_head=(k+1)%N8XB_SLOTS;n8xb.rx_count--;
    if(bytes&&n8xb.frame_logs<N8XB_FRAME_LOGS){n8xb.frame_logs++;log=1;}
   }
  }
  IOLockUnlock(nss_native.lock);
  if(!bytes)break;
  int error=nord2_wifi_ifnet_receive(generation,n8xb.scratch,bytes);
  IOLockLock(nss_native.lock);if(error)n8xb.rx_drop++;else n8xb.rx_up++;IOLockUnlock(nss_native.lock);
  if(log)printf("NORD2 WIFI 8021X: eapol rx type=%u len=%u error=%d\n",(unsigned)n8xb.scratch[15],bytes-14,error);
  niwj_clear(n8xb.scratch,sizeof n8xb.scratch);
 }
}
