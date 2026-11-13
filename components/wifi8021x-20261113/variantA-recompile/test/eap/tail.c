/* ---- 0451: 802.1X join on the real owner loop (synthetic hardware, synthetic AP = test/engine/ap.h) ---- */
#include "ap.h"
#if EAP_AKM==5
#define EAP_SECURITY NIWJ_WPA2_8021X_SHA256
#define EAP_UPPER 0x80
#else
#define EAP_SECURITY NIWJ_WPA2_8021X
#define EAP_UPPER 4
#endif
static struct ap eap_authenticator;static uint8_t eap_station_rsn[22],eap_msk[64],eap_tx_last[1514],eap_rx_last[1514];
static unsigned eap_links,eap_link_log[8],eap_up_rx,eap_rx_bytes,eap_tx_bytes,eap_ap_rx,eap_expect_protected,eap_m2,eap_m4,eap_assoc_seen;
int nord2_wifi_ifnet_link(uint64_t g,int a){assert(g&&g==njs_native.native_generation);if(eap_links<8)eap_link_log[eap_links]=(unsigned)a|(nst_native.state.controlled_port<<4);eap_links++;if(a)link_up_calls++;else{assert(!nst_native.state.controlled_port);link_down_calls++;}return 0;}
int nord2_wifi_ifnet_receive(uint64_t g,const uint8_t*p,size_t n){assert(g&&p&&n>=14);if(p[12]==0x88&&p[13]==0x8e){assert(n<=sizeof eap_rx_last&&p[15]!=3&&!memcmp(p,nss_native.info.mac,6)&&!memcmp(p+6,nst_native.state.peer.ap,6));memcpy(eap_rx_last,p,n);eap_rx_bytes=(unsigned)n;eap_up_rx++;}return 0;}
/* The association request carries the station RSN element with the 802.1X AKM. */
static void eap_assoc(const uint8_t*p,unsigned n){unsigned found=0;for(unsigned at=28;at+2<=n;at+=2u+p[at+1])if(p[at]==48){assert(p[at+1]==20&&!memcmp(p+at,eap_station_rsn,22));found++;}assert(found==1);eap_assoc_seen++;}
/* Every EAPOL frame the station publishes: EAP packets of "eapolclient" unchanged to the AP, EAPOL-Key from the engine. */
static void eap_ap(const struct nst*s){
 const uint8_t*f=s->wire+32;unsigned n=s->wire_bytes-32,protect=!!(get32(s->wire+12)&2);
 assert(n>=18&&!memcmp(f,s->peer.ap,6)&&!memcmp(f+6,s->peer.self,6)&&f[12]==0x88&&f[13]==0x8e);
 if(f[15]!=3){assert(n==eap_tx_bytes&&!memcmp(f+12,eap_tx_last+12,n-12)&&protect==eap_expect_protected);eap_ap_rx++;return;}
 unsigned info=(unsigned)f[19]<<8|f[20];assert(!protect);
 if(!(info&0x0200)){ap_m2(&eap_authenticator,f+14,n-14,eap_station_rsn);uint8_t m3x[256];unsigned z=ap_m3(&eap_authenticator,m3x);station_eapol(m3x,z);eap_m2++;}
 else{ap_m4(&eap_authenticator,f+14,n-14);eap_m4++;}
}
static void join_completed(void*c,uint64_t epoch,uint64_t ticket,enum njs_notice notice){assert(c==notices&&epoch==7&&ticket&&notice<5);notices[notice]++;assert(njs_native.snapshot.security==EAP_SECURITY);assert(!pthread_mutex_trylock(&nss_native.lock->m));pthread_mutex_unlock(&nss_native.lock->m);
 if(notice==NJS_JOINED){struct njs_snapshot v;assert(!nord2_wifi_join_copy_state(&v)&&v.phase==NJS_AUTHORIZED&&v.result==1&&v.associated&&!v.data_ready&&!v.ptk_ready&&!v.gtk_ready&&v.security==EAP_SECURITY&&!nst_native.state.controlled_port);}
 if(notice==NJS_RETIRED)assert(!njs_rejoin_ready_locked());}
/* The 592-byte ASSOCIATE of the log (auth_upper 4, no key, 22-byte RSN, flag 0x08 at 463), through the real decoder. */
static int eap_decode(struct niwj_request*q){uint8_t a[592]={0};struct niwj_diagnostic d;a[0]=1;a[4]=2;a[6]=1;a[8]=EAP_UPPER;a[12]=1;a[16]=(uint8_t)radio_ssid;a[204]=22;n8x_station_rsn(EAP_AKM,a+206);a[463]=0x08;return niwj_decode_identity(a,sizeof a,nss_native.info.mac,q,&d);}
static void eap_from_ap(unsigned code,unsigned bytes){uint8_t f[1504];assert(bytes>=4&&bytes<=1500);memset(f,0x5a,sizeof f);f[0]=2;f[1]=0;f[2]=(uint8_t)(bytes>>8);f[3]=(uint8_t)bytes;f[4]=(uint8_t)code;f[5]=(uint8_t)(eap_up_rx+1);f[6]=f[2];f[7]=f[3];station_eapol(f,bytes+4);}
static void eap_from_supplicant(unsigned type,unsigned bytes,int expect){
 static const uint8_t pae[6]={0x01,0x80,0xc2,0x00,0x00,0x03};uint8_t*f=eap_tx_last;memset(f,0xa5,sizeof eap_tx_last);memcpy(f,pae,6);memcpy(f+6,nss_native.info.mac,6);f[12]=0x88;f[13]=0x8e;f[14]=2;f[15]=(uint8_t)type;f[16]=(uint8_t)(bytes>>8);f[17]=(uint8_t)bytes;
 eap_tx_bytes=bytes+18;assert(nord2_wifi_link_send(f,eap_tx_bytes,njs_native.native_generation)==expect);
}
int main(int argc,char**argv){assert(!initial_open);assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f&&fread(authenticated_board,1,sizeof authenticated_board,f)==sizeof authenticated_board);fclose(f);
 (void)make_m3;(void)completed;(void)test_prepare_clock;{unsigned fixture_counters=delivered+nrd_calls+nrd_restored+nrd_checked+station_pubs+station_frames+station_commands;(void)fixture_counters;(void)m1_sent;(void)m3_sent;(void)drain;}
 setup(0);run_worker();assert(nss_native.ready);worker_thread=2;
 struct niwj_request q;uint64_t ticket=0;n8x_station_rsn(EAP_AKM,eap_station_rsn);for(unsigned i=0;i<64;i++)eap_msk[i]=(uint8_t)(0xc0+i);
 /* kern.n2wifi_8021x=0: the profile is refused as before 0450, at the decoder and at the native admission. */
 niwj_8021x=0;assert(eap_decode(&q)==NIWJ_PROFILE);{struct niwj_request raw={0};raw.security=EAP_SECURITY;raw.ssid_length=1;raw.ssid[0]=(uint8_t)radio_ssid;memcpy(raw.station_rsn,eap_station_rsn,22);assert(nord2_wifi_join_enqueue(&raw,notices,7,join_completed,&ticket)==-1&&!ticket);}
 niwj_8021x=1;assert(!eap_decode(&q)&&q.security==EAP_SECURITY&&!memcmp(q.station_rsn,eap_station_rsn,22));
 assert(nord2_wifi_8021x_key(eap_msk,64)==1&&nord2_wifi_8021x_key(eap_msk,48)==-1); /* nothing waits for a key yet */
 unsigned reads=mmio_reads,writes=mmio_writes;
 assert(!nord2_wifi_join_enqueue(&q,notices,7,join_completed,&ticket)&&ticket==1);niwj_clear(&q,sizeof q);assert(reads==mmio_reads&&writes==mmio_writes);
 assert(njs_native.deadline_us-njs_native.admitted_us<=UINT64_C(55000000)&&njs_native.deadline_us-njs_native.admitted_us>UINT64_C(54990000));
 station_hardware=1;unsigned step=0;uint8_t ip[60]={0};
 for(unsigned k=0;k<8000&&!njs_native.linked&&!njs_native.scan_returned;k++){
  struct stc*t=&njs_native.driver.owner.transaction;
  switch(step){
  case 0:if(n8xb.prelinked){
    /* Associated, STA configured: BSD link up for EAPOL only, JOINED delivered, no engine, port closed. */
    assert(t->phase==STC_EAPOL&&t->associated&&nst_native.state.sta_active&&!nst_native.state.controlled_port&&t->wpa.state==NWP_OFF&&stc_eap(t)&&stc_eap_waiting(t)&&eap_assoc_seen==1);
    assert(eap_links==1&&eap_link_log[0]==1&&k<8000);
    memcpy(ip,nst_native.state.peer.ap,6);memcpy(ip+6,nss_native.info.mac,6);ip[12]=8;assert(nord2_wifi_link_send(ip,sizeof ip,ticket)==NWIL_SEND_NOT_READY); /* controlled port */
    eap_from_ap(1,5);step=1;}break; /* EAP-Request/Identity */
  case 1:if(eap_up_rx==1){assert(notices[NJS_JOINED]==1&&eap_rx_bytes==14+4+5&&eap_rx_last[18]==1);eap_from_supplicant(0,5+9,NWIL_SEND_ACCEPTED);step=2;}break; /* EAP-Response/Identity, written to the PAE group address */
  case 2:if(eap_ap_rx==1){eap_from_ap(1,1400);step=3;}break; /* a TLS-sized request */
  case 3:if(eap_up_rx==2){assert(eap_rx_bytes==14+4+1400);eap_from_supplicant(0,1000,NWIL_SEND_ACCEPTED);step=4;}break;
  case 4:if(eap_ap_rx==2){eap_from_ap(3,4);step=5;}break; /* EAP-Success */
  case 5:if(eap_up_rx==3){ /* message 1 right behind EAP-Success, before eapolclient has set the key */
    struct ap*a=&eap_authenticator;memset(a,0,sizeof *a);a->akm=EAP_AKM;memcpy(a->pmk,eap_msk,32);memcpy(a->aa,nst_native.state.peer.ap,6);memcpy(a->spa,nss_native.info.mac,6);for(unsigned i=0;i<32;i++)a->anonce[i]=(uint8_t)(0x11*EAP_AKM+i);for(unsigned i=0;i<16;i++)a->gtk[i]=(uint8_t)(0x60+i);
    assert(njs_native.target.ap_rsn_bytes==22&&njs_native.target.ap_rsn[19]==EAP_AKM);memcpy(a->rsn,njs_native.target.ap_rsn,22);a->rsn_bytes=22;
    uint8_t m1x[128];unsigned z=ap_m1(a,m1x);station_eapol(m1x,z);step=6;}break;
  case 6:if(t->eapol_pending){
    assert(t->phase==STC_EAPOL&&t->wpa.state==NWP_OFF&&stc_eap_waiting(t)&&!eap_m2&&!nst_native.state.controlled_port&&eap_up_rx==3);
#if EAP_CASE==2
    /* eapolclient never delivers a key: the attempt ends at its 55 s budget with a clean teardown. */
    assert(nord2_wifi_8021x_key(eap_msk,16)==-1);drain_now=njs_native.deadline_us*1000+1000000;step=9;
#else
    eap_from_supplicant(3,95,NWIL_SEND_INVALID);                         /* EAPOL-Key is the kernel's */
    assert(!nord2_wifi_8021x_key(eap_msk,64)&&n8xb.key_pending);step=7; /* APPLE80211_IOC_CIPHER_KEY: MSK */
#endif
   }break;
  default:break;}
  assert(njs_owner_work()==1&&!nst_native.state.halt);
 }
 struct njs_snapshot v;assert(!nord2_wifi_join_copy_state(&v));
#if EAP_CASE==2
 for(unsigned k=0;k<4000&&!njs_native.scan_returned;k++)assert(njs_owner_work()==1);
 assert(step==9&&njs_native.scan_returned&&!njs_native.linked&&!n8xb.prelinked&&!n8xb.key_pending&&!eap_m2&&!eap_m4&&!nst_native.state.halt&&!manual_halts&&!nss_native.failed);
 assert(!nord2_wifi_join_copy_state(&v)&&v.result==9&&v.phase==NJS_FAILED&&!v.associated&&!v.data_ready);
 assert(notices[NJS_JOINED]==1&&notices[NJS_LEFT]==1&&notices[NJS_RETIRED]==1&&!notices[NJS_JOIN_FAILED]&&eap_links==2&&eap_link_log[1]==0&&link_up_calls==1&&link_down_calls==1&&!key_deletes&&!nsr_native.ptk&&!nsr_native.gtk);
 eap_from_supplicant(0,9,NWIL_SEND_NOT_READY);assert(nord2_wifi_8021x_key(eap_msk,64)==1);
 const char*what="no key from the supplicant: timeout at the 55 s budget, deauthentication and peer retirement, link down, LEFT + RETIRED, no halt";
#else
 /* PMK -> message 2 (checked by the authenticator) -> message 3 -> message 4 -> PTK/GTK install -> port open. */
 assert(step==7&&njs_native.linked&&nwo_authorized(&njs_native.driver.owner)&&nst_native.state.controlled_port&&eap_m2==1&&eap_m4==1&&nsr_native.ptk&&nsr_native.gtk&&!n8xb.key_pending&&n8xb.started);
 assert(!memcmp(njs_native.driver.owner.transaction.wpa.tk,eap_authenticator.ptk+32,16)&&!memcmp(njs_native.driver.owner.transaction.wpa.gtk,eap_authenticator.gtk,16));
 assert(v.phase==NJS_AUTHORIZED||!v.data_ready);assert(!nord2_wifi_join_copy_state(&v)&&v.phase==NJS_AUTHORIZED&&v.result==1&&v.associated&&v.data_ready&&v.ptk_ready&&v.gtk_ready&&v.security==EAP_SECURITY);
 assert(notices[NJS_JOINED]==1&&eap_links==2&&eap_link_log[0]==1&&eap_link_log[1]==0x11&&link_up_calls==2&&!link_down_calls&&!n8xb.prelinked&&eap_up_rx==3&&eap_ap_rx==2);
 /* ordinary data now passes; a reauthentication EAPOL-Start of the supplicant leaves encrypted */
 assert(!nord2_wifi_link_send(ip,sizeof ip,ticket));while(njs_native.tx_count||nst_native.state.wire_busy)assert(njs_owner_work()==1);
 eap_expect_protected=1;eap_from_supplicant(1,0,NWIL_SEND_ACCEPTED);for(unsigned k=0;k<200&&eap_ap_rx<3;k++)assert(njs_owner_work()==1);assert(eap_ap_rx==3&&n8xb.tx_sent==3&&!n8xb.tx_drop);
 /* a second key (reauthentication) is not taken: no PTK rekey in this engine */
 assert(nord2_wifi_8021x_key(eap_msk,64)==1&&!n8xb.key_pending);
 assert(!nord2_wifi_join_disconnect());for(unsigned k=0;k<2000&&!njs_native.scan_returned;k++)assert(njs_owner_work()==1);
 assert(njs_native.scan_returned&&notices[NJS_LEFT]==1&&notices[NJS_RETIRED]==1&&!notices[NJS_JOIN_FAILED]&&link_down_calls>=1&&key_deletes==2&&!nsr_native.ptk&&!nsr_native.gtk&&!nst_native.state.halt&&!manual_halts);
 const char*what="EAP packets up and down while the port is closed (5..1400 bytes, PAE group destination rewritten), message 1 kept until the MSK arrives, 4-way checked by an OpenSSL authenticator, keys, port open, protected EAPOL after it, leave";
#endif
 IOLock*notice=njs_native.notice_lock;assert(!pthread_mutex_destroy(&notice->m));free(notice);assert(!pthread_mutex_destroy(&nss_native.lock->m));free(nss_native.lock);
 printf("PASS 0451 owner 802.1X AKM %d: switch off refuses; association with the AKM in the RSN element, BSD link up before any key, JOINED; %s; synthetic hardware only\n",EAP_AKM,what);return 0;}
