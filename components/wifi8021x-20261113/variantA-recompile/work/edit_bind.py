#!/usr/bin/env python3
"""0451 edits on work/b: native station binder hooks, transport binder, BSD endpoint, IOUC client."""
from pathlib import Path
W=Path(__file__).resolve().parent/'b'
def sub(name,old,new,count=1):
    f=W/name;t=f.read_text();assert t.count(old)==count,(name,old[:80],t.count(old));f.write_text(t.replace(old,new))
S='bsd/dev/wifi_station_service_bind.inc'
sub('bsd/dev/wifi_station_service.h','int nord2_wifi_join_card_inventory(struct niwc_manifest*);','''int nord2_wifi_join_card_inventory(struct niwc_manifest*);
/* 0451: key of APPLE80211_IOC_CIPHER_KEY from eapolclient after EAP success:
 * 64 bytes MSK (PMK = first 32) or 32 bytes PMK. Copied under the native
 * lock; the 4-way handshake then runs in the native owner. 0 taken, 1 no
 * 802.1X association is waiting for a key, -1 invalid. */
int nord2_wifi_8021x_key(const uint8_t*,size_t);''')
sub(S,'#include "wifi_wpa2.h"\n','#include "wifi_wpa2.h"\n#include "wifi_8021x.h"\n')
sub(S,''' void*cookie;njs_completion completion;struct niwj_request request;struct njs_snapshot snapshot;uint8_t want_self[6];unsigned has_self;}njs_handover;
''',''' void*cookie;njs_completion completion;struct niwj_request request;struct njs_snapshot snapshot;uint8_t want_self[6];unsigned has_self;}njs_handover;
#include "wifi_8021x_bind.inc"
''')
sub(S,'''&&!memcmp(q->station_rsn,rsn,22))||
 (q->security==NIWJ_OPEN''','''&&!memcmp(q->station_rsn,rsn,22))||n8xb_request_ok(q)||
 (q->security==NIWJ_OPEN''')
sub(S,' njs_native.admitted_us=now_us;njs_native.deadline_us=(now_us/1000+8000)*1000;',' n8xb_reset_locked();\n njs_native.admitted_us=now_us;njs_native.deadline_us=(now_us/1000+n8xb_budget_ms(q))*1000;')
sub(S,'''int nord2_wifi_link_send(const uint8_t*p,size_t n,uint64_t generation){
''','''int nord2_wifi_link_send(const uint8_t*p,size_t n,uint64_t generation){
 if(p&&n>=14&&n<=NWIL_ETHERNET_MAX&&generation&&p[12]==0x88&&p[13]==0x8e)return n8xb_send(p,n,generation); /* 0451: EAPOL of eapolclient */
''')
sub(S,''' v->result=v->data_ready?1:(s->outcome==STC_PENDING||s->outcome==STC_AUTHORIZED)?0:s->outcome==STC_TIMEOUT?9:2;
''',''' v->result=v->data_ready?1:(s->outcome==STC_PENDING||s->outcome==STC_AUTHORIZED)?0:s->outcome==STC_TIMEOUT?9:2;
 n8xb_snapshot_locked(v,s);
''')
sub(S,''' if(!nss_native.powered&&e->kind==NST_EAPOL)return 0;
''',''' if(!nss_native.powered&&e->kind==NST_EAPOL)return 0;
 if(n8xb_rx_locked(e))return 0; /* 0451: EAP packet for eapolclient, or the receipt of one of its frames */
''')
sub(S,''' for(unsigned k=0;k<NJS_CACHE_SLOTS;k++)if(!niwj_select(&njs_native.request,&njs_native.cache[k],&nss_native.info,now_us,&njs_native.candidate)&&(!found||njs_native.candidate.rssi_dbm>njs_native.target.rssi_dbm)){njs_native.target=njs_native.candidate;found=1;}''',''' unsigned mfpr=0;
 for(unsigned k=0;k<NJS_CACHE_SLOTS;k++){niwj_ap_mfpr=0;int miss=niwj_select(&njs_native.request,&njs_native.cache[k],&nss_native.info,now_us,&njs_native.candidate);mfpr+=niwj_ap_mfpr;if(miss)continue;
  if(!found||njs_native.candidate.rssi_dbm>njs_native.target.rssi_dbm){njs_native.target=njs_native.candidate;found=1;}}
 if(niwj_eap(njs_native.request.security))printf("NORD2 WIFI 8021X: join akm=%u ap_found=%u ap_requires_pmf=%u%s\\n",njs_native.request.security==NIWJ_WPA2_8021X?1u:5u,(unsigned)found,mfpr,!found&&mfpr?" (PMF/MFPR is not supported)":"");
''')
sub(S,'''config.ap_rsn=njs_native.target.ap_rsn;config.ap_rsn_bytes=njs_native.target.ap_rsn_bytes;config.rng=njs_rng;}
''','''config.ap_rsn=njs_native.target.ap_rsn;config.ap_rsn_bytes=njs_native.target.ap_rsn_bytes;config.rng=njs_rng;}
 if(niwj_eap(njs_native.target.security))config.pmk=0; /* 0451: 802.1X, the PMK arrives after EAP (stc_pmk) */
''')
sub(S,''' int result=(njs_native.canceled||njs_native.leave)&&nst_native.state.associated_scan?0:nwn_pump(&njs_native.driver,now_us/1000);
 if(result<0){IOLockUnlock(nss_native.lock);return njs_fatal_owner(4);}
''',''' n8xb_key_locked(now_us);
 int result=(njs_native.canceled||njs_native.leave)&&nst_native.state.associated_scan?0:nwn_pump(&njs_native.driver,now_us/1000);
 if(result<0){IOLockUnlock(nss_native.lock);return njs_fatal_owner(4);}
 n8xb_tx_locked();
''')
sub(S,''' for(unsigned k=0;k<njs_native.rx_count;k++){IOLockLock(nss_native.lock);unsigned deliver=njs_native.linked''',''' n8xb_link_work(&notify_join);n8xb_deliver();
 for(unsigned k=0;k<njs_native.rx_count;k++){IOLockLock(nss_native.lock);unsigned deliver=njs_native.linked''')
sub(S,'unsigned live=njs_native.linked&&!njs_native.canceled&&!njs_native.leave;IOLockUnlock(nss_native.lock);if(live)njs_notice(NJS_JOINED);}','unsigned live=(njs_native.linked||n8xb_live_locked())&&!njs_native.canceled&&!njs_native.leave;IOLockUnlock(nss_native.lock);if(live)njs_notice(NJS_JOINED);}')
# transport binder: the 802.1X profiles are WPA2 on the wire
sub('bsd/dev/wifi_station_transport_bind.inc','(t->security!=NIWJ_WPA2_PSK_CCMP&&t->security!=NIWJ_OPEN)','(t->security!=NIWJ_WPA2_PSK_CCMP&&t->security!=NIWJ_OPEN&&!niwj_eap(t->security))')
# BSD endpoint: EAPOL is admitted by the native owner (n8xb_send / n8xb_deliver), not refused here
sub('bsd/dev/wifi_ifnet_link.inc',''' /* EAPOL belongs exclusively to the owner's authenticator. No VLAN or
  * 802.3-length/SNAP emulation is advertised by this first data path. */
 return type>=0x600 && type!=0x888e && type!=0x8100 && type!=0x88a8;''',''' /* No VLAN or 802.3-length/SNAP emulation is advertised by this data path.
  * 0451: EAPOL (0x888e) is no longer refused here. The native owner decides:
  * nord2_wifi_link_send accepts it only during an 802.1X join (EAP packets
  * of eapolclient, never EAPOL-Key), and only the owner's 802.1X delivery
  * calls nord2_wifi_ifnet_receive with it; for PSK both stay closed. */
 return type>=0x600 && type!=0x8100 && type!=0x88a8;''')
# IOUC client: eapolclient may open the client; CIPHER_KEY set carries the MSK/PMK
C='iokit/Kernel/arm/wifi_ios_client.cpp'
sub(C,' task_t owner;IOLock *gate;\n',' task_t owner;IOLock *gate;bool supplicant;\n')
sub(C,''' if(entitlement)entitlement->release();
 if(!authorized||!IOUserClient::initWithTask(task,security,type,props))return false;''',''' if(entitlement)entitlement->release();
 supplicant=false;
 if(!authorized&&niwj_8021x){
  /* 0451: the 802.1X supplicant (eapolclient) has com.apple.wlan.authentication, not manager access. It may query
   * the association (SSID, BSSID, AUTH_TYPE), monitor events, scan and set the EAP key; no join, leave or power. */
  entitlement=IOUserClient::copyClientEntitlement(task,"com.apple.wlan.authentication");allowed=OSDynamicCast(OSBoolean,entitlement);
  supplicant=authorized=allowed&&allowed->isTrue();if(entitlement)entitlement->release();
 }
 if(!authorized||!IOUserClient::initWithTask(task,security,type,props))return false;''')
sub(C,'''   const uint8_t*power=(const uint8_t*)carrier+8;size_t tail=selector==0?15368:245768;
''','''   const uint8_t*power=(const uint8_t*)carrier+8;size_t tail=selector==0?15368:245768;
   if(unsupportedSet==3||unsupportedSet==89){
    /* 0451: APPLE80211_IOC_CIPHER_KEY of eapolclient after EAP success. struct apple80211_key, 148 bytes: version 1 at 0,
     * key length at 4, cipher type at 8, key at 16. Cipher 9 = MSK, 64 bytes (the PMK is its first half); 6 = PMK, 32.
     * An empty MSK and request 89 clear a PMK cache this driver does not keep. Key bytes are never logged. */
    uint32_t extent=niw_u32((const uint8_t*)carrier+4),value=niw_u32((const uint8_t*)carrier+tail+20),cipher=0,length=0;const uint8_t*key=power;
    if(a->structureOutputSize||value)result=kIOReturnBadArgument;
    else if(unsupportedSet==89)result=extent?kIOReturnUnsupported:kIOReturnSuccess;
    else if(extent!=148||niw_u32(key)!=1)result=kIOReturnBadArgument;
    else{
     cipher=niw_u32(key+8);length=niw_u32(key+4);
     if((cipher==9&&length==64)||(cipher==6&&length==32)){int x=nord2_wifi_8021x_key(key+16,length);result=!x?kIOReturnSuccess:x<0?kIOReturnBadArgument:kIOReturnNotReady;}
     else if(cipher==9&&!length)result=kIOReturnSuccess;
     else result=kIOReturnUnsupported;
    }
    if(traceControl(unsupportedSet,0x802869c8))printf("NORD2 WIFI 8021X: set request=%u bytes=%u cipher=%u key_bytes=%u status=%#x supplicant=%u t_ms=%llu\\n",unsupportedSet,extent,cipher,length,(unsigned)result,(unsigned)supplicant,niw_ms());
    if(copy)niwj_free(copy,bytes);return result;
   }
   if(supplicant&&(unsupportedSet==19||unsupportedSet==20||unsupportedSet==22)){if(copy)niwj_free(copy,bytes);return kIOReturnNotPrivileged;}
''')
sub(C,''' *handler=c;printf("NORD2 WIFI IOS CLIENT: opened=1 type=0 root_busy=%d t_ms=%llu\\n",niw_root_busy(),niw_ms());return kIOReturnSuccess;''',''' *handler=c;printf("NORD2 WIFI IOS CLIENT: opened=1 type=0 root_busy=%d t_ms=%llu\\n",niw_root_busy(),niw_ms());if(c->isSupplicant())printf("NORD2 WIFI 8021X: supplicant client opened t_ms=%llu\\n",niw_ms());return kIOReturnSuccess;''')
sub(C,''' void bindInterface(const char*);
''',''' void bindInterface(const char*);
 bool isSupplicant(void) const {return supplicant;}
''')
