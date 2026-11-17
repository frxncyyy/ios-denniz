#include <arm64/sweet_early.h>
#if defined(NORD2)
/* New private transaction model. No kernel IO, waits, publication or secrets log. */
#include "wifi_station_transaction.h"
#include "wifi_wpa_diag.h"
#if defined(KERNEL) || defined(_KERNEL)
#include <sys/systm.h>
#else
#include <string.h>
#endif
static void erase(void*v,size_t n){volatile uint8_t*p=v;while(n--)*p++=0;}
static unsigned le16(const uint8_t*p){return p[0]|(unsigned)p[1]<<8;}
/* 0433: one numeric line per SAE/PMF step so that a hang is attributable. No
 * address, key, nonce or password octet is ever printed. */
int printf(const char*,...) __attribute__((format(printf,1,2)));
int stc_scan_acquire(struct stc_arbiter*a,uint64_t g){if(!a||!g||a->mode)return -1;a->mode=1;a->generation=g;return 0;}
int stc_scan_release(struct stc_arbiter*a,uint64_t g){if(!a||a->mode!=1||a->generation!=g)return -1;a->mode=0;a->generation=0;return 0;}
static int emit(struct stc*s,unsigned phase,unsigned kind){
 erase(&s->action,sizeof s->action);s->phase=phase;
 if(s->serial==UINT64_MAX){s->phase=STC_HALTED;nwp_cancel(&s->wpa);return -1;}
 s->action.generation=s->generation;s->action.ticket=++s->serial;s->action.kind=kind;return 1;
}
static int halt(struct stc*s){nsae_cancel(&s->sae);if(s->outcome==STC_AUTHORIZED)s->outcome=STC_FAILED;erase(s->queued_eapol,sizeof s->queued_eapol);nwp_cancel(&s->wpa);erase(&s->action,sizeof s->action);erase(s->response,sizeof s->response);s->phase=STC_HALTED;return -1;}
static int release_done(struct stc*s){
 if(!s->arbiter||s->arbiter->mode!=2||s->arbiter->generation!=s->generation)return halt(s);
 s->arbiter->mode=0;s->arbiter->generation=0;s->authenticated=s->associated=0;s->phase=STC_DONE;erase(&s->action,sizeof s->action);erase(&s->peer,sizeof s->peer);return 1;
}
static int group_slot(struct stc*s,unsigned index){
 if(index>3)return -1;if(s->gtk_mapped_mask&(1u<<index))return s->gtk_wlan[index];
 unsigned slot=s->group_wlan;
 if(s->gtk_mapped_mask){slot=0;for(unsigned k=0;k<3;k++){unsigned x=s->group_spare[k],used=0;if(!x)continue;for(unsigned j=0;j<4;j++)if((s->gtk_mapped_mask&(1u<<j))&&s->gtk_wlan[j]==x)used=1;if(!used){slot=x;break;}}}
 if(!slot)return -1;s->gtk_wlan[index]=(uint8_t)slot;s->gtk_mapped_mask|=1u<<index;return (int)slot;
}
static int stop(struct stc*,unsigned);
static int command_key(struct stc*s,int group,int remove){
 struct sw_key k={0};size_t used=0;int slot=group?group_slot(s,s->gtk_index):s->sta_wlan;if(slot<0)return stop(s,STC_FAILED);k.bss=s->channel.bss;k.wlan=(uint8_t)slot;k.pairwise=!group;k.key_id=group?s->gtk_index:0;memcpy(k.ap,s->peer.ap,6);
 if(!remove){k.key=group?s->wpa.gtk:s->wpa.tk;k.key_len=16;if(!group){s->ptk_attempted=1;k.mgmt_protection=(uint8_t)(s->pmf&&(s->pmf_flags&STC_PMF_PTK_PROTECTION));}}
 if(emit(s,remove?(group?STC_REMOVE_GTK:STC_REMOVE_PTK):(group?STC_GTK:STC_PTK),remove?(group?STC_DELETE_GTK:STC_DELETE_PTK):(group?(s->group_active?STC_REKEY_GTK:STC_CID_GTK):STC_CID_PTK))<0)return -1;
 int r=remove?sw_key_remove(s->action.payload,sizeof s->action.payload,&used,&k):sw_key_add(s->action.payload,sizeof s->action.payload,&used,&k);
 if(r)return halt(s);s->action.bytes=(unsigned)used;return 1;
}
static int command_igtk(struct stc*s,unsigned step){
 struct sw_key k={0};size_t used=0;k.bss=s->channel.bss;k.wlan=s->channel.bss;k.key_id=3;k.bip=1;memcpy(k.ap,s->peer.ap,6);k.key=s->wpa.igtk;k.key_len=16;
 if(emit(s,STC_GTK,STC_CID_IGTK)<0)return -1;
 if(sw_key_add(s->action.payload,sizeof s->action.payload,&used,&k))return halt(s);
 s->action.bytes=(unsigned)used;s->igtk_step=step;s->wpa.igtk_fresh=0;
 printf("NORD2 WIFI PMF: IGTK install command (BIP key id 3, wtbl %u, ap key id %u)\n",(unsigned)k.wlan,s->wpa.igtk_index);return 1;
}
static int abort_channel(struct stc*s){s->channel_held=0;struct sw_channel c=s->channel;c.abort=1;size_t used=0;if(emit(s,STC_RELEASE,STC_ABORT_CHANNEL)<0)return -1;if(sw_channel(s->action.payload,sizeof s->action.payload,&used,&c))return halt(s);s->action.bytes=(unsigned)used;return 1;}
static int cleanup_next(struct stc*s){
 if(s->cleanup_abort_first&&s->channel_held){s->cleanup_abort_first=0;return abort_channel(s);}
 if(s->authenticated){s->authenticated=0;if(emit(s,STC_DEAUTH,STC_TX_DEAUTH)<0)return -1;
  size_t used=0;unsigned protect=s->pmf&&s->pmf_keys&&(s->pmf_flags&STC_PMF_TX_DEAUTH);
  if(sw_deauth(s->action.payload,sizeof s->action.payload,&used,&s->peer,protect))return halt(s);
  s->action.bytes=(unsigned)used;if(s->pmf)printf("NORD2 WIFI PMF: deauthentication protected=%u\n",protect);return 1;
 }
 if(s->ptk_attempted){s->ptk_attempted=0;return command_key(s,0,1);}
 if(s->gtk_attempted_mask){for(unsigned k=0;k<4;k++)if(s->gtk_attempted_mask&(1u<<k)){s->gtk_attempted_mask&=~(1u<<k);s->gtk_index=(uint8_t)k;s->gtk_attempted=!!s->gtk_attempted_mask;return command_key(s,1,1);}}
 if(s->gtk_attempted){s->gtk_attempted=0;return command_key(s,1,1);}
 if(s->peer_prepared){s->peer_prepared=0;return emit(s,STC_RETIRE,STC_RETIRE_PEER);}
 if(s->channel_held)return abort_channel(s);
 return release_done(s);
}
static int stop(struct stc*s,unsigned outcome){
 if(s->phase>=STC_DEAUTH)return 0;
 s->outcome=outcome;if(s->last_ms>UINT64_MAX-s->cleanup_timeout_ms)return halt(s);s->cleanup_deadline=s->last_ms+s->cleanup_timeout_ms;nwp_cancel(&s->wpa);nsae_cancel(&s->sae);s->saq_pending=s->mgmt_tx=0;erase(s->ssid,sizeof s->ssid);erase(s->rates,sizeof s->rates);erase(s->response,sizeof s->response);erase(s->queued_eapol,sizeof s->queued_eapol);s->eapol_pending=s->eapol_bytes=0;s->response_bytes=s->response_seen=0;
 if(s->phase<STC_EAPOL){s->cleanup_abort_first=s->channel_held;if(!s->associated)s->authenticated=0;}

 erase(&s->action,sizeof s->action);return cleanup_next(s);
}
static int send_auth(struct stc*);
static int send_assoc(struct stc*);
int stc_tick(struct stc*s,uint64_t now){
 if(!s||!s->attempted||s->phase==STC_NEW||s->phase==STC_DONE||s->phase==STC_HALTED)return 0;
 if(!s->arbiter||s->arbiter->mode!=2||s->arbiter->generation!=s->generation)return halt(s);
 if(now<s->last_ms)return halt(s);s->last_ms=now;
 if(s->phase>=STC_DEAUTH){if(now>=s->cleanup_deadline)return halt(s);return 0;}
 if(s->group_active&&now>=s->group_deadline)return stop(s,STC_TIMEOUT);
 if(s->outcome!=STC_AUTHORIZED&&now>=s->deadline){if(s->sae_step)printf("NORD2 WIFI SAE: join deadline in step %u (1 commit, 2 confirm), phase %u\n",s->sae_step,s->phase);return stop(s,STC_TIMEOUT);}
 if(s->saq_pending&&now>=s->saq_deadline){printf("NORD2 WIFI PMF: SA Query timeout, link treated as lost\n");s->authenticated=0;return stop(s,STC_DISCONNECTED);}
 /* Start response timer only after authoritative air completion. Two total
  * requests per stage, always within the actual remaining grant deadline. */
 if((s->phase==STC_AUTH_RX||s->phase==STC_ASSOC_RX)&&!s->response_seen&&s->response_deadline&&now>=s->response_deadline){
  unsigned attempts=s->phase==STC_AUTH_RX?s->auth_attempts:s->assoc_attempts;
  if(attempts>=(s->sae_step||s->pmf?3u:2u)){if(s->sae_step)printf("NORD2 WIFI SAE: no answer to %s after %u transmissions\n",s->sae_step==1?"commit":"confirm",attempts);return stop(s,STC_TIMEOUT);}
  return s->phase==STC_AUTH_RX?send_auth(s):send_assoc(s);
 }
 return 0;
}
static int current(struct stc*s,uint64_t gen,uint64_t now){
 if(!s||gen!=s->generation||!s->attempted||s->phase==STC_DONE||s->phase==STC_HALTED)return 0;
 unsigned before=s->phase;if(stc_tick(s,now)<0||s->phase!=before)return 0;return 1;
}
int stc_begin(struct stc*s,struct stc_arbiter*a,const struct stc_config*c,uint64_t now){
 if(!s||!a||!c||s->attempted||a->mode||a->connection_used||!c->generation||c->deadline_ms<=now||!c->cleanup_timeout_ms||c->security>SW_SECURITY_OPEN||c->assoc.security!=c->security||c->sta_wlan==255||c->group_wlan==255||c->sta_wlan==c->group_wlan)return -1;
 uint8_t preflight[640];size_t used;
 if(sw_auth(preflight,sizeof preflight,&used,&c->assoc.peer)||sw_assoc(preflight,sizeof preflight,&used,&c->assoc)||sw_channel(preflight,sizeof preflight,&used,&c->channel)||c->channel.abort)return -1;
 memset(s,0,sizeof *s);s->attempted=1;s->generation=c->generation;s->last_ms=now;s->deadline=c->deadline_ms;s->cleanup_timeout_ms=c->cleanup_timeout_ms;
 s->security=c->security;
 if(c->security==SW_SECURITY_OPEN){if(c->pmk||c->station_rsn||c->ap_rsn||c->ap_rsn_bytes||c->rng){erase(s,sizeof *s);s->attempted=1;return -1;}}
 else if(c->profile&&!(c->profile->akm==NWP_AKM_PSK&&!c->profile->pmf)){
  unsigned sae=c->profile->akm==NWP_AKM_SAE;
  if((sae?(!c->password||!c->password_bytes||c->password_bytes>63):!c->pmk)||!c->ap_rsn||!c->rng||
     nwp_begin_profile(&s->wpa,c->generation,sae?0:c->pmk,c->assoc.peer.self,c->assoc.peer.ap,c->profile,c->ap_rsn,c->ap_rsn_bytes,c->rng,c->rng_context)||nwp_copy_ap_rsnxe(&s->wpa,c->ap_ies,c->ap_ies_bytes)){erase(s,sizeof *s);s->attempted=1;return -1;}
  if(sae){
   printf("NORD2 WIFI SAE: deriving password element (%s), group 19\n",c->profile->h2e?"hash-to-element":"hunting-and-pecking");
   if(nsae_begin(&s->sae,c->assoc.peer.self,c->assoc.peer.ap,c->password,c->password_bytes,c->assoc.ssid,c->assoc.ssid_len,c->profile->h2e,c->rng,c->rng_context)){printf("NORD2 WIFI SAE: password element/commit preparation failed\n");erase(s,sizeof *s);s->attempted=1;return -1;}
   s->sae_step=1;printf("NORD2 WIFI SAE: commit prepared (pmf=%u required=%u)\n",c->profile->pmf,c->profile->pmf_required);
  }
  s->pmf=c->profile->pmf;s->pmf_flags=c->pmf_flags;
 }
 else if(nwp_begin_ap(&s->wpa,c->generation,c->pmk,c->assoc.peer.self,c->assoc.peer.ap,c->station_rsn,c->ap_rsn,c->ap_rsn_bytes?c->ap_rsn_bytes:22,c->rng,c->rng_context)||nwp_copy_ap_rsnxe(&s->wpa,c->ap_ies,c->ap_ies_bytes)){erase(s,sizeof *s);s->attempted=1;return -1;}
 for(unsigned k=0;k<3;k++){unsigned x=c->group_spare[k];if(x&&(x<6||x==c->sta_wlan||x==c->group_wlan)){erase(s,sizeof *s);s->attempted=1;return -1;}for(unsigned j=0;j<k;j++)if(x&&x==c->group_spare[j]){erase(s,sizeof *s);s->attempted=1;return -1;}s->group_spare[k]=(uint8_t)x;}
 s->arbiter=a;a->mode=2;a->connection_used=1;a->generation=c->generation;s->peer=c->assoc.peer;s->channel=c->channel;s->sta_wlan=c->sta_wlan;s->group_wlan=c->group_wlan;s->assoc=c->assoc;
 memcpy(s->ssid,c->assoc.ssid,c->assoc.ssid_len);memcpy(s->rates,c->assoc.rates,c->assoc.rates_len);s->assoc.ssid=s->ssid;s->assoc.rates=s->rates;s->peer_prepared=1;
 s->assoc.rsn=s->assoc.rsnxe=0;s->assoc.rsn_len=s->assoc.rsnxe_len=0;
 if(s->wpa.akm_profile){s->assoc.rsn=s->wpa.own_rsn;s->assoc.rsn_len=s->wpa.own_rsn_bytes;if(s->wpa.own_rsnxe_bytes){s->assoc.rsnxe=s->wpa.own_rsnxe;s->assoc.rsnxe_len=s->wpa.own_rsnxe_bytes;}}
 return emit(s,STC_PREPARE,STC_PREPARE_PEER);
}
const struct stc_action*stc_next(const struct stc*s){return s&&s->action.kind?&s->action:0;}
static int send_auth(struct stc*s){size_t n=0;s->response_seen=0;s->response_deadline=0;s->auth_attempts++;if(emit(s,STC_AUTH_TX,STC_TX_AUTH)<0)return -1;
 if(s->sae_step){
  uint8_t body[SW_SAE_BODY_MAX];size_t used=0;
  int e=s->sae_step==1?nsae_commit(&s->sae,body,sizeof body,&used):nsae_confirm(&s->sae,body,sizeof body,&used);
  if(!e)e=sw_auth_sae(s->action.payload,sizeof s->action.payload,&n,&s->peer,s->sae_step,s->sae_step==1&&s->sae.h2e?NSAE_STATUS_H2E:0,body,used);
  erase(body,sizeof body);
  if(e){printf("NORD2 WIFI SAE: could not build %s frame\n",s->sae_step==1?"commit":"confirm");return stop(s,STC_FAILED);}
  printf("NORD2 WIFI SAE: %s sent to the radio (transmission %u, %u bytes, h2e=%u, token=%u)\n",s->sae_step==1?"commit":"confirm",s->auth_attempts,(unsigned)n,s->sae.h2e,s->sae.token_bytes);
  s->action.bytes=(unsigned)n;return 1;
 }
 if(sw_auth(s->action.payload,sizeof s->action.payload,&n,&s->peer))return stop(s,STC_FAILED);s->action.bytes=(unsigned)n;return 1;}
static int send_assoc(struct stc*s){size_t n=0;s->response_seen=0;s->response_deadline=0;s->assoc_attempts++;if(emit(s,STC_ASSOC_TX,STC_TX_ASSOC)<0)return -1;if(sw_assoc(s->action.payload,sizeof s->action.payload,&n,&s->assoc))return stop(s,STC_FAILED);s->action.bytes=(unsigned)n;return 1;}
static int response_done(struct stc*s){
 if(!s->response_seen)return 0;
 if(s->phase==STC_AUTH_RX&&s->sae_step==1){
  unsigned status=le16(s->response+28),bytes=s->response_bytes-30;const uint8_t*body=s->response+30;
  if(status==NSAE_STATUS_TOKEN){
   if(s->sae_tokens++>=2||nsae_token(&s->sae,body,bytes)){printf("NORD2 WIFI SAE: anti-clogging token request refused (round %u, %u bytes)\n",s->sae_tokens,bytes);return stop(s,STC_FAILED);}
   printf("NORD2 WIFI SAE: anti-clogging token received (%u bytes), commit repeated\n",s->sae.token_bytes);s->auth_attempts=0;return send_auth(s);
  }
  if(nsae_peer_commit(&s->sae,body,bytes)){printf("NORD2 WIFI SAE: peer commit invalid (group/scalar/element, %u bytes)\n",bytes);return stop(s,STC_FAILED);}
  printf("NORD2 WIFI SAE: peer commit ok, keys derived\n");s->sae_step=2;s->auth_attempts=0;erase(s->response,sizeof s->response);return send_auth(s);
 }
 if(s->phase==STC_AUTH_RX&&s->sae_step==2){
  if(nsae_peer_confirm(&s->sae,s->response+30,s->response_bytes-30)){printf("NORD2 WIFI SAE: confirm mismatch (wrong password?)\n");return stop(s,STC_FAILED);}
  printf("NORD2 WIFI SAE: confirm ok\n");
  if(nwp_set_pmk(&s->wpa,s->generation,s->sae.pmk)){nsae_cancel(&s->sae);return stop(s,STC_FAILED);}
  nsae_cancel(&s->sae);s->sae_step=0;erase(s->response,sizeof s->response);
  printf("NORD2 WIFI SAE: PMK done, associating with AKM 8 (pmf=%u)\n",s->pmf);
 }
 if(s->phase==STC_AUTH_RX){s->authenticated=1;return send_assoc(s);}
 if(s->phase==STC_ASSOC_RX){s->associated=1;if(emit(s,STC_CONFIGURE,STC_CONFIGURE_PEER)<0)return -1;memcpy(s->action.payload,s->response,s->response_bytes);s->action.bytes=s->response_bytes;s->response_seen=0;erase(s->response,sizeof s->response);return 1;}
 return 0;
}
static int consume_eapol(struct stc*s,uint64_t now){
 if(!s->eapol_pending)return 1;
 if(s->phase!=STC_EAPOL&&s->phase!=STC_PTK_READY&&s->phase!=STC_CONNECTED)return 1;
 uint8_t p[640];unsigned n=s->eapol_bytes;memcpy(p,s->queued_eapol,n);erase(s->queued_eapol,sizeof s->queued_eapol);s->eapol_pending=s->eapol_bytes=0;
 int r=stc_eapol(s,s->generation,p,n,now);erase(p,sizeof p);return r;
}
static int group_tx(struct stc*s,struct nwp_result*r){
 if(emit(s,STC_EAPOL_TX,STC_TX_GROUP)<0){erase(r,sizeof *r);return -1;}
 s->resume_phase=STC_CONNECTED;s->wpa_tx_ticket=r->ticket;memcpy(s->action.payload,r->frame,r->bytes);s->action.bytes=r->bytes;erase(r,sizeof *r);return 1;
}
/* Only accepted native submissions acquire cleanup ownership. A queued
 * GTK action which never reached the adapter must not delete an unowned slot.
 * Successful direct-model receipts also establish this same fact below. */
int stc_submitted(struct stc*s,uint64_t gen,uint64_t ticket){
 if(!s||gen!=s->generation||!ticket||ticket!=s->action.ticket)return 0;
 if(s->action.kind!=STC_CID_GTK&&s->action.kind!=STC_REKEY_GTK)return 0;
 if(s->gtk_index>3||!(s->gtk_mapped_mask&(1u<<s->gtk_index)))return -1;
 s->gtk_attempted=1;s->gtk_attempted_mask|=1u<<s->gtk_index;return 1;
}
int stc_receipt(struct stc*s,uint64_t gen,uint64_t ticket,int success,uint64_t now){
 if(!current(s,gen,now)||!ticket||ticket!=s->action.ticket||!s->action.kind)return 0;
 unsigned phase=s->phase;if(success==1&&stc_submitted(s,gen,ticket)<0)return halt(s);erase(&s->action,sizeof s->action);
 /* Deauthentication is best-effort over air, not a firmware retirement
  * barrier. A negative air result must still run actual key/peer cleanup.
  * Failed command deletion/retirement remains fatal. Native unknown-DMA
  * failures never deliver an air receipt and keep their retained halt. */
 if(phase==STC_EAPOL_TX&&s->mgmt_tx){ /* SA Query frame: best effort, never a reason to drop the link by itself */
  s->mgmt_tx=0;s->phase=s->resume_phase;s->resume_phase=0;printf("NORD2 WIFI PMF: SA Query frame tx result=%d\n",success);return consume_eapol(s,now);}
 if(success!=1){if(phase==STC_DEAUTH)return cleanup_next(s);if(phase>=STC_DEAUTH)return halt(s);if(s->sae_step)printf("NORD2 WIFI SAE: %s transmission failed\n",s->sae_step==1?"commit":"confirm");return stop(s,STC_FAILED);}
 size_t n=0;
 switch(phase){
 case STC_PREPARE:s->channel_held=1;if(emit(s,STC_CHANNEL,STC_CID_CHANNEL)<0)return -1;if(sw_channel(s->action.payload,sizeof s->action.payload,&n,&s->channel))return stop(s,STC_FAILED);s->action.bytes=(unsigned)n;return 1;
 case STC_CHANNEL:s->phase=STC_GRANT;if(s->grant_seen)return stc_grant(s,gen,s->channel.bss,s->channel.token,s->channel.band,s->channel.channel,1,now);return 1;
 case STC_AUTH_TX:s->phase=STC_AUTH_RX;s->response_deadline=now>UINT64_MAX-524?UINT64_MAX:now+524;return response_done(s);
 case STC_ASSOC_TX:s->phase=STC_ASSOC_RX;s->response_deadline=now>UINT64_MAX-524?UINT64_MAX:now+524;return response_done(s);
 case STC_CONFIGURE:/* Actual peer/BSS/RLM and station-activation receipt, no invented grant release. */if(s->security==SW_SECURITY_OPEN)return emit(s,STC_DATA,STC_INSTALL_DATA_PATH);s->phase=STC_EAPOL;return consume_eapol(s,now);
 case STC_EAPOL_TX:{int r=nwp_security_submitted(&s->wpa,gen,s->wpa_tx_ticket,1);s->wpa_tx_ticket=0;if(r==NWP_INSTALL){s->gtk_index=s->wpa.gtk_index;return command_key(s,0,0);}if(r<0)return stop(s,STC_FAILED);if(s->group_active){s->group_active=0;s->group_deadline=0;}s->phase=s->resume_phase;s->resume_phase=0;if(s->phase==STC_PTK_READY&&s->key_event_seen){if(nwp_keys_ready(&s->wpa,gen,s->wpa.install_ticket,1,1)<0)return stop(s,STC_FAILED);return command_key(s,1,0);}return consume_eapol(s,now);}
 case STC_PTK:s->phase=STC_PTK_READY;s->pmf_keys=1;if(s->key_event_seen){if(nwp_keys_ready(&s->wpa,gen,s->wpa.install_ticket,1,1)<0)return stop(s,STC_FAILED);return command_key(s,1,0);}return consume_eapol(s,now); /* submission, not key-ready */
 case STC_GTK:
  if(s->group_active){
   if(s->igtk_step==3)s->igtk_step=2;
   else if(s->pmf&&s->wpa.igtk_fresh&&(s->pmf_flags&STC_PMF_IGTK))return command_igtk(s,3);
   struct nwp_result r;int v=nwp_group_ready(&s->wpa,gen,s->wpa.group_ticket,1,&r);if(v!=NWP_TX){erase(&r,sizeof r);return stop(s,STC_FAILED);}return group_tx(s,&r);}
  if(s->igtk_step==1){s->igtk_step=2;printf("NORD2 WIFI PMF: IGTK command submitted\n");return emit(s,STC_DATA,STC_INSTALL_DATA_PATH);}
  /* Source no-response host completion/replay initialization, NOT firmware ACK. */if(nwp_keys_ready(&s->wpa,gen,s->wpa.install_ticket,2,1)<0)return stop(s,STC_FAILED);
  if(s->pmf&&s->wpa.igtk_ready&&(s->pmf_flags&STC_PMF_IGTK))return command_igtk(s,1);
  return emit(s,STC_DATA,STC_INSTALL_DATA_PATH);
 case STC_DATA:if(s->security==SW_SECURITY_OPEN){s->phase=STC_CONNECTED;s->outcome=STC_AUTHORIZED;return 1;}if(nwp_keys_ready(&s->wpa,gen,s->wpa.install_ticket,4,1)!=NWP_AUTH)return stop(s,STC_FAILED);s->phase=STC_CONNECTED;s->outcome=STC_AUTHORIZED;return consume_eapol(s,now);
 case STC_DEAUTH:case STC_REMOVE_PTK:case STC_REMOVE_GTK:case STC_RETIRE:case STC_RELEASE:return cleanup_next(s);
 default:return halt(s);
 }
}
int stc_grant(struct stc*s,uint64_t gen,uint8_t bss,uint8_t token,uint8_t band,uint8_t ch,int granted,uint64_t now){
 if(!current(s,gen,now)||(s->phase!=STC_GRANT&&s->phase!=STC_CHANNEL)||bss!=s->channel.bss||token!=s->channel.token||band!=s->channel.band||ch!=s->channel.channel)return 0;
 if(granted!=1)return stop(s,STC_FAILED);s->grant_seen=1;if(s->phase==STC_CHANNEL)return 1;return send_auth(s);
}
int stc_management(struct stc*s,uint64_t gen,const uint8_t*p,size_t n,uint64_t now){
 if(!current(s,gen,now)||!p||n<24)return 0;
 unsigned fc=le16(p);if((fc&0x0703)||memcmp(p+4,s->peer.self,6)||memcmp(p+10,s->peer.ap,6)||memcmp(p+16,s->peer.ap,6))return 0;
 /* 0433 PMF: once the pairwise key is installed, robust management frames
  * count only when the hardware decrypted them (Protected set, delivered by
  * the transport under STC_PMF_RX). Unprotected deauth/disassoc is ignored,
  * optionally answered by one SA Query whose timeout ends the link. */
 unsigned live=s->pmf&&s->pmf_keys&&s->phase<STC_DEAUTH,protected_frame=(fc&0x4000)!=0;
 if(protected_frame&&!(live&&(s->pmf_flags&STC_PMF_RX)))return 0;
 if((fc&0xfc)==0xd0){
  if(!protected_frame||n<28||p[24]!=8)return 0;
  if(p[25]==1){if(s->saq_pending&&p[26]==s->saq_id[0]&&p[27]==s->saq_id[1]){s->saq_pending=0;printf("NORD2 WIFI PMF: SA Query answered, link kept\n");}return 0;}
  if(p[25]||s->phase!=STC_CONNECTED||s->action.kind||s->saq_answered>=4)return 0;
  size_t used=0;if(emit(s,STC_EAPOL_TX,STC_TX_ACTION)<0)return -1;
  if(sw_sa_query(s->action.payload,sizeof s->action.payload,&used,&s->peer,1,p+26))return halt(s);
  s->action.bytes=(unsigned)used;s->resume_phase=STC_CONNECTED;s->mgmt_tx=1;s->saq_answered++;printf("NORD2 WIFI PMF: SA Query request answered (%u)\n",s->saq_answered);return 1;
 }
 if((fc&0xfc)==0xc0||(fc&0xfc)==0xa0){if(n<26)return 0;
  if(live&&!protected_frame){
   if(s->unprotected_seen<4){s->unprotected_seen++;printf("NORD2 WIFI PMF: unprotected %s reason %u ignored (%u)\n",(fc&0xfc)==0xc0?"deauth":"disassoc",le16(p+24),s->unprotected_seen);}
   if(!(s->pmf_flags&STC_PMF_SA_QUERY)||s->saq_pending||s->saq_sent>=2||s->phase!=STC_CONNECTED||s->action.kind)return 0;
   size_t used=0;if(now>UINT64_MAX-1000||emit(s,STC_EAPOL_TX,STC_TX_ACTION)<0)return -1;
   s->saq_id[0]=(uint8_t)(now^s->serial);s->saq_id[1]=(uint8_t)((now>>8)^0xa5u);
   if(sw_sa_query(s->action.payload,sizeof s->action.payload,&used,&s->peer,0,s->saq_id))return halt(s);
   s->action.bytes=(unsigned)used;s->resume_phase=STC_CONNECTED;s->mgmt_tx=1;s->saq_pending=1;s->saq_sent++;s->saq_deadline=now+1000;
   printf("NORD2 WIFI PMF: SA Query sent (%u), 1000 ms\n",s->saq_sent);return 1;
  }
  if(protected_frame)printf("NORD2 WIFI PMF: protected %s reason %u\n",(fc&0xfc)==0xc0?"deauth":"disassoc",le16(p+24));
  s->authenticated=0;return stop(s,STC_DISCONNECTED);}
 if(protected_frame)return 0;
 int auth=s->phase==STC_AUTH_TX||s->phase==STC_AUTH_RX,assoc=s->phase==STC_ASSOC_TX||s->phase==STC_ASSOC_RX;
 if((!auth&&!assoc)||s->response_seen)return 0;
 if(n<30||n>sizeof s->response||(fc&0xfc)!=(auth?0xb0u:0x10u))return 0;
 if(auth&&s->sae_step){
  unsigned status=le16(p+28);
  if(le16(p+24)!=3||le16(p+26)!=s->sae_step)return 0; /* other algorithm or the other transaction: not for this step */
  if(s->sae_step==1&&status==NSAE_STATUS_TOKEN){if(n<33)return 0;}
  else if(s->sae_step==1&&status==NSAE_STATUS_GROUP){printf("NORD2 WIFI SAE: AP refuses group 19 (status 77, unsupported group)\n");return stop(s,STC_FAILED);}
  else if(status!=(s->sae_step==1&&s->sae.h2e?NSAE_STATUS_H2E:0u)){printf("NORD2 WIFI SAE: %s rejected by the AP, status %u\n",s->sae_step==1?"commit":"confirm",status);return stop(s,STC_FAILED);}
  else if(n<(s->sae_step==1?30u+NSAE_COMMIT_MIN:64u))return 0;
  printf("NORD2 WIFI SAE: %s received (status %u, %u bytes)\n",s->sae_step==1?(status==NSAE_STATUS_TOKEN?"token request":"commit"):"confirm",status,(unsigned)n);
 }
 else if(auth){if(le16(p+24)!=0||le16(p+26)!=2)return 0;for(size_t i=30;i<n;){if(n-i<2||p[i+1]>n-i-2)return 0;i+=2+p[i+1];}if(le16(p+28))return stop(s,STC_FAILED);}
 else{
  if(le16(p+26)==30&&s->pmf&&s->assoc_attempts<3){ /* association comeback (SA Query running at the AP for an older association) */
   unsigned tu=1000;for(size_t i=30;i+2<=n&&(size_t)p[i+1]+2<=n-i;i+=2u+p[i+1])if(p[i]==56&&p[i+1]==5&&p[i+2]==3)tu=le16(p+i+3)|le16(p+i+5)<<16;
   if(tu<100)tu=100;if(tu>3000)tu=3000;
   printf("NORD2 WIFI PMF: association comeback, retry in %u TU (attempt %u)\n",tu,s->assoc_attempts);
   s->response_deadline=now>UINT64_MAX-tu-24?UINT64_MAX:now+tu+24;return 0;
  }
  if(le16(p+26)||(s->security==SW_SECURITY_OPEN&&(!(le16(p+24)&1)||(le16(p+24)&16))))return stop(s,STC_FAILED);unsigned aid=le16(p+28)&0x3fff;if(!aid||aid>2007)return stop(s,STC_FAILED);for(size_t i=30;i<n;){if(n-i<2||p[i+1]>n-i-2)return stop(s,STC_FAILED);i+=2+p[i+1];}s->aid=(uint16_t)aid;}
 memcpy(s->response,p,n);s->response_bytes=(unsigned)n;s->response_seen=1;return response_done(s);
}
int stc_eapol(struct stc*s,uint64_t gen,const uint8_t*p,size_t n,uint64_t now){
 if(!current(s,gen,now)||s->security==SW_SECURITY_OPEN||s->phase<STC_CONFIGURE||s->phase>=STC_DEAUTH)return 0;
 if(!p||!n||n>640){nwpd_gate_observe(gen,s->action.ticket,s->wpa.state,p!=0,n);return stop(s,STC_FAILED);}
 if(s->phase!=STC_EAPOL&&s->phase!=STC_PTK_READY&&s->phase!=STC_CONNECTED){
  if(s->eapol_pending)return s->eapol_bytes==n&&!memcmp(s->queued_eapol,p,n)?0:2;
  memcpy(s->queued_eapol,p,n);s->eapol_bytes=(unsigned)n;s->eapol_pending=1;return 1;
 }
 unsigned resume=s->phase,security_state=s->wpa.state;struct nwp_result r;int result=nwp_receive(&s->wpa,p,n,&r);
 nwpd_observe(gen,s->action.ticket,security_state,p,n,result,r.failure);
 if(result<0){erase(&r,sizeof r);return stop(s,STC_FAILED);}
 if((result==NWP_INSTALL||result==NWP_TX)&&s->wpa.group_phase){
  if(!s->group_active){if(now>UINT64_MAX-10000){erase(&r,sizeof r);return stop(s,STC_FAILED);}s->group_active=1;s->group_deadline=now+10000;}
  if(result==NWP_INSTALL){s->gtk_index=(uint8_t)s->wpa.gtk_index;erase(&r,sizeof r);return command_key(s,1,0);}
  return group_tx(s,&r);
 }
 if(result!=NWP_TX){erase(&r,sizeof r);return 0;}
 if(emit(s,STC_EAPOL_TX,STC_TX_EAPOL)<0){erase(&r,sizeof r);return -1;}s->resume_phase=resume;s->wpa_tx_ticket=r.ticket;memcpy(s->action.payload,r.frame,r.bytes);s->action.bytes=r.bytes;erase(&r,sizeof r);return 1;
}
int stc_key_event(struct stc*s,uint64_t gen,const uint8_t*p,size_t n,uint64_t now){
 if(!current(s,gen,now)||s->security==SW_SECURITY_OPEN||(s->phase!=STC_PTK_READY&&s->phase!=STC_PTK&&!(s->phase==STC_EAPOL_TX&&s->resume_phase==STC_PTK_READY))||!p||n!=8||p[0]!=s->channel.bss||memcmp(p+2,s->peer.ap,6))return 0;
 s->key_event_seen=1;if(s->phase!=STC_PTK_READY)return 1;
 if(nwp_keys_ready(&s->wpa,gen,s->wpa.install_ticket,1,1)<0)return stop(s,STC_FAILED);return command_key(s,1,0);
}
int stc_link_lost(struct stc*s,uint64_t gen,uint64_t now){if(!current(s,gen,now))return 0;s->authenticated=0;return stop(s,STC_DISCONNECTED);}
int stc_cancel(struct stc*s,uint64_t gen,uint64_t now){if(!current(s,gen,now))return 0;return stop(s,STC_CANCELED);}

int stc_channel_released(struct stc*s,uint64_t gen,uint8_t token,uint64_t now){if(!current(s,gen,now)||token!=s->channel.token||s->phase>=STC_DEAUTH)return 0;s->channel_held=0;return 1;}

#endif /* NORD2 */
