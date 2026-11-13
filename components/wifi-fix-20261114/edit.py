#!/usr/bin/env python3
"""wifi-fix-20261114/edit.py: writes b/ (= a/ + the 0491 edits). Every anchor must occur exactly once."""
from pathlib import Path
H=Path(__file__).resolve().parent
def ed(name,pairs):
    t=(H/'a/bsd/dev'/name).read_text()
    for old,new in pairs:
        assert t.count(old)==1,(name,old[:80],t.count(old)); t=t.replace(old,new)
    (H/'b/bsd/dev'/name).write_text(t)
# ---- decoder: default = the vendor sequence
ed('wifi_ios_join.c',[('unsigned niwj_privmac_mode=1;','unsigned niwj_privmac_mode=2;')])
ed('wifi_ios_join.h',[(
'''/* 0430/0436 join policy, kern.n2wifi_privmac (boot-arg n2wpm), default 1:
 * 0 legacy decoder: exact WPA2 profile, a private address must equal the
 *   current station address;
 * 1 relaxed decoder, the requested private (locally administered) address
 *   becomes the station address: firmware BSS re-activated with it before
 *   authentication (0436);
 * 2 as 1, with a deactivate command before the activate;
 * 3 relaxed decoder, private address accepted and ignored (board address).
 *   This is the kill switch: no address is ever changed in modes 0 and 3.''',
'''/* 0430/0436/0491 join policy, kern.n2wifi_privmac (boot-arg n2wpm), default 2:
 * 0 legacy decoder: exact WPA2 profile, a private address must equal the
 *   current station address;
 * 1 relaxed decoder, the requested private (locally administered) address
 *   becomes the station address, firmware told by a second BSS activate
 *   while the BSS is active. q10jia on hardware: the firmware keeps its old
 *   own address (authentication frames from the new one are never
 *   acknowledged), so this mode only remains for experiments;
 * 2 as 1, with the vendor driver's sequence: BSS deactivate, then activate
 *   with the new address (gen4m nicDeactivateNetwork/nicActivateNetwork, the
 *   only path by which that driver changes the own address). Default (0491);
 * 3 relaxed decoder, private address accepted and ignored (board address).
 *   This is the kill switch: no private address is used in modes 0 and 3.
 * 0491: a private address that fails before authentication is given up for
 * the board address (kern.n2wifi_privmac_state, wifi_station_service_bind.inc).''')])
# ---- scan binder: the deactivate command of the vendor driver carries no address
ed('wifi_scan_service_bind.inc',[(
''' /* Deactivate = the same command with ucActive 0 and the address the firmware has now. */
 if(pre==2){memcpy(nss_native.deactivate,nss_native.frames.activate,NCS_ACTIVATE_BYTES);nss_native.deactivate[65]=0;memcpy(nss_native.deactivate+68,nss_native.fw_mac,6);}''',
''' /* Deactivate = gen4m nicDeactivateNetworkEx: zeroed command with the BSS index, ucActive 0, network type, own-address index and
  * BMC WLAN index of the activate; no address (0491: was the address the firmware had). */
 if(pre==2){memcpy(nss_native.deactivate,nss_native.frames.activate,NCS_ACTIVATE_BYTES);nss_native.deactivate[65]=0;memset(nss_native.deactivate+68,0,6);}''')])
# ---- station binder
ed('wifi_station_service_bind.inc',[
('''extern unsigned niwi_scan_relax;
SYSCTL_UINT(_kern, OID_AUTO, n2wifi_privmac, CTLFLAG_RW | CTLFLAG_LOCKED, &niwj_privmac_mode, 0, "0436: private Wi-Fi address: 0 legacy refuse, 1 use it (default), 2 use it with a deactivate first, 3 accept and ignore (kill switch)");''',
'''extern unsigned niwi_scan_relax;
SYSCTL_UINT(_kern, OID_AUTO, n2wifi_privmac, CTLFLAG_RW | CTLFLAG_LOCKED, &niwj_privmac_mode, 0, "0436/0491: private Wi-Fi address: 0 legacy refuse, 1 use it with a second activate only (ignored by the firmware), 2 use it with deactivate + activate (default), 3 accept and ignore (kill switch)");'''),
(''' * the join then fails by its ordinary timeout. No address is ever logged. */
static int njs_ifnet_set_mac(''',
''' * the join then fails by its ordinary timeout. No address is ever logged.
 * 0491, from q10jia on hardware (mode 1): both joins on a private address
 * ended with the first authentication frame never acknowledged (TX status 3
 * after 30 transmissions) while the same AP acknowledged the board address at
 * once: the firmware had not taken the new own address. So (a) every change,
 * also the way back to the board address, is deactivate + activate unless
 * mode 1 is forced, and (b) njs_privmac_state remembers a private address
 * that does not get through authentication:
 * 0 private addresses are used; 1 suspected: an attempt on a private address
 * failed before any association request (no acknowledgement or no answer to
 * authentication), the next attempts use the board address; 2 confirmed: the
 * board address then passed authentication, board address until reboot (or
 * until kern.n2wifi_privmac_state is written 0). A suspected state is dropped
 * when the board address fails the same way (the AP is simply not there). */
static unsigned njs_privmac_state,njs_privmac_logs;
#ifndef NJS_HOST_TEST
SYSCTL_UINT(_kern, OID_AUTO, n2wifi_privmac_state, CTLFLAG_RW | CTLFLAG_LOCKED, &njs_privmac_state, 0, "0491: private Wi-Fi address fallback: 0 in use, 1 suspected after a failed authentication (board address next), 2 confirmed not working (board address); write 0 to try again");
#endif
/* Admission lock held, first terminal observation of an attempt. stc keeps
 * auth_attempts/assoc_attempts through its cleanup (authenticated/associated
 * are cleared there): an association request was sent = authentication passed. */
static inline __attribute__((always_inline)) unsigned njs_privmac_verdict_locked(void){
 const struct stc*t=&njs_native.driver.owner.transaction;
 unsigned on_private=memcmp(nss_native.info.mac,nss_native.factory_mac,6)!=0,passed=njs_native.joined||t->assoc_attempts!=0;
 unsigned early=!passed&&t->auth_attempts!=0&&(t->outcome==STC_FAILED||t->outcome==STC_TIMEOUT),old=njs_privmac_state,now=old;
 if(on_private){if(early&&!old)now=1;}
 else if(old==1)now=passed?2:early?0:1;
 njs_privmac_state=now;
 return (on_private||old!=now)?1u|on_private<<1|passed<<2|early<<3|old<<4|now<<6:0;
}
static int njs_ifnet_set_mac('''),
('''static void njs_mac_select_locked(void){
 unsigned mode=niwj_privmac_mode;
 const uint8_t*want=(mode==1||mode==2)&&njs_native.has_self?njs_native.want_self:nss_native.factory_mac;''',
'''static void njs_mac_select_locked(void){
 unsigned mode=niwj_privmac_mode;
 const uint8_t*want=(mode==1||mode==2)&&njs_native.has_self&&!njs_privmac_state?njs_native.want_self:nss_native.factory_mac;'''),
(''' nss_native.mac_switch=mode==2?2:1;nss_native.lladdr_pending=1;nss_native.mac_attempts=0;''',
''' nss_native.mac_switch=mode==1?1:2;nss_native.lladdr_pending=1;nss_native.mac_attempts=0; /* 0491: deactivate + activate also on the way back (modes 0/3, fallback) */'''),
(''' unsigned notify_join=0,notify_terminal=0,retire=0,detach=0,link_up=0,link_down=0;
 IOLockLock(nss_native.lock);
 if(njs_native.detach){njs_native.detach=0;detach=1;}''',
''' unsigned notify_join=0,notify_terminal=0,retire=0,detach=0,link_up=0,link_down=0,verdict=0;
 IOLockLock(nss_native.lock);
 if(njs_native.detach){njs_native.detach=0;detach=1;}'''),
(''' if(terminal){if(!njs_native.terminal){njs_native.terminal=1;notify_terminal=1;}''',
''' if(terminal){if(!njs_native.terminal){njs_native.terminal=1;notify_terminal=1;verdict=njs_privmac_verdict_locked();if(verdict&&njs_privmac_logs<64)njs_privmac_logs++;else verdict=0;}'''),
(''' if(scan_done)nss_deliver(scan_done,!scan_done->scan.done?NSS_REFUSED:''',
''' if(verdict)printf("NORD2 WIFI IOS MAC: verdict private=%u passed_auth=%u failed_before_assoc=%u state=%u->%u\\n",(verdict>>1)&1u,(verdict>>2)&1u,(verdict>>3)&1u,(verdict>>4)&3u,(verdict>>6)&3u);
 if(scan_done)nss_deliver(scan_done,!scan_done->scan.done?NSS_REFUSED:'''),
])
print('edited')
