#!/usr/bin/env python3
"""0436 host test: the handover owner fixture (two different WPA2 networks on the real owner loop, synthetic
hardware) with the SECOND join carrying a private station address through the real IOUC decoder.
Generates owner.c / run-owner.py from tools/fixtures/nord2-wifi-handover (not modified) and runs them.
usage: make.py --candidate DIR --log-dir DIR [--board FILE]"""
from pathlib import Path
import argparse,subprocess,sys
H=Path(__file__).resolve().parent
R=next(p for p in H.parents if (p/'tools/fixtures/nord2-wifi-handover').exists() and (p/'research/nord2/wifi/calibration').exists());F=R/'tools/fixtures/nord2-wifi-handover'
a=argparse.ArgumentParser();a.add_argument('--candidate',type=Path,required=True);a.add_argument('--log-dir',type=Path,required=True)
a.add_argument('--board',type=Path,default=R/'research/nord2/wifi/calibration/WIFI');a.add_argument('--bearssl',type=Path,default=R/'research/ci-39158169212/buildtree/xnu/libkern/crypto/sweet_bearssl')
a.add_argument('--mode',type=int,default=1,choices=[1,2]);x=a.parse_args()
def sub(t,old,new):
    assert t.count(old)==1,(old[:70],t.count(old));return t.replace(old,new)
o=(F/'owner.c').read_text()
o=sub(o,'static unsigned callbacks;','''static unsigned callbacks,mac_park,activate_seen,activate_last,deactivate_seen;static uint8_t activate_mac[6],deactivate_mac[6];
static const uint8_t private_mac[6]={0x02,0x11,0x22,0x33,0x44,0x55};
/* The second network is requested through the real 592-byte ASSOCIATE decoder: WPA2 PMK profile,
 * association flag bit 0x08 at byte 464 and a locally administered address at 508. */
static struct niwj_request private_request(void){uint8_t a[592]={0};struct niwj_request q;struct niwj_diagnostic d;
 a[0]=1;a[4]=2;a[6]=1;a[8]=8;a[12]=1;a[16]='Y';a[56]=1;a[60]=32;a[64]=6;memcpy(a+72,pmk_vector,32);a[204]=22;memcpy(a+206,rsn,22);a[464]=0x08;memcpy(a+508,private_mac,6);
 niwj_privmac_mode=PRIVMAC_MODE;assert(!niwj_decode_identity(a,sizeof a,nss_native.info.mac,&q,&d)&&d.private_address_present&&d.owner_address_valid&&!d.private_address_matches&&!d.reason);return q;}''')
o=sub(o,"struct niwj_request next=request();next.ssid[0]='Y';","struct niwj_request next=private_request();assert(next.ssid[0]=='Y'&&next.security==NIWJ_WPA2_PSK_CCMP);")
o=sub(o,'niwj_clear(&q,sizeof q);handover_park=0;','''niwj_clear(&q,sizeof q);handover_park=0;
  if(cycle){
   /* Admission of the handover selects the private address; the join stays queued until one internal scan
    * whose recipe starts with BSS activate-control has run on the (scan-mode) synthetic hardware. */
   uint8_t factory[6];memcpy(factory,nss_native.factory_mac,6);assert(nct_native.session.count==1&&ncb_native.rx.scan_command==0&&ncb_native.rx.scan.done&&ncb_native.rx.scan.count>=1); /* last ordinary scan: plain path, BSS rows */unsigned act0=activate_seen,deact0=deactivate_seen;assert(!memcmp(nss_native.info.mac,factory,6)&&act0==1&&!deact0&&!memcmp(activate_mac,factory,6)); /* the boot recipe's own activate */
   unsigned pubs=normal_pubs;mac_park=1;station_hardware=0;delivered=0;drain();mac_park=0;
   assert(njs_native.queued&&!njs_native.active&&njs_native.has_self&&!nss_native.mac_switch&&!nss_native.lladdr_pending&&nss_native.mac_attempts==1);
   assert(!memcmp(nss_native.info.mac,private_mac,6)&&!memcmp(nss_native.fw_mac,private_mac,6)&&!memcmp(nss_native.lladdr_old,factory,6)&&!memcmp(nss_native.factory_mac,factory,6));
   assert(activate_seen==act0+1&&activate_last==1&&!memcmp(activate_mac,private_mac,6)&&normal_pubs==pubs+PRIVMAC_MODE+1&&deactivate_seen==deact0+PRIVMAC_MODE-1);
   if(PRIVMAC_MODE==2)assert(!memcmp(deactivate_mac,factory,6));
   assert(nct_native.session.count==PRIVMAC_MODE+1u&&nct_done_receipt(&nct_native.session)&&ncb_native.rx.scan_command==PRIVMAC_MODE&&delivered);
  }''')
o=sub(o,'  assert(nst_native_data_profile()==0&&nst_native_set_next_data_profile(1)<0&&nst_native_next_data_profile()==0);',
 '  assert(nst_native_data_profile()==0&&nst_native_set_next_data_profile(1)<0&&nst_native_next_data_profile()==0);\n  if(cycle)assert(!memcmp(nst_native.state.peer.self,private_mac,6)&&!memcmp(njs_native.profile.assoc.peer.self,private_mac,6)&&activate_seen==2);else assert(memcmp(nst_native.state.peer.self,private_mac,6));')
o=sub(o,' assert(delivered&&nrd_calls&&nrd_calls==nrd_restored','''/* ordinary scan after the change: plain path (one command, scan index 0), complete, BSS rows */
 assert(!memcmp(nss_native.info.mac,private_mac,6)&&!nss_native.mac_switch&&nct_native.session.count==1&&ncb_native.rx.scan_command==0&&ncb_native.rx.scan.done&&ncb_native.rx.scan.count>=1&&callbacks==1);
 /* A following request without a private address goes back to the board address. */
 IOLockLock(nss_native.lock);njs_native.has_self=0;njs_mac_select_locked();assert(!memcmp(nss_native.info.mac,nss_native.factory_mac,6)&&nss_native.mac_switch==(PRIVMAC_MODE==2?2u:1u)&&nss_native.lladdr_pending&&!memcmp(nss_native.lladdr_old,private_mac,6)&&!memcmp(nss_native.fw_mac,private_mac,6));
 niwj_privmac_mode=3;njs_native.has_self=1;memcpy(njs_native.want_self,private_mac,6);nss_native.mac_switch=0;njs_mac_select_locked();assert(!nss_native.mac_switch&&!memcmp(nss_native.info.mac,nss_native.factory_mac,6));IOLockUnlock(nss_native.lock);
 assert(delivered&&nrd_calls&&nrd_calls==nrd_restored''')
o=sub(o,'synthetic hardware only");','second join on a private address (mode " PRIVMAC_TEXT "): decoder, admission, activate-control before the scan, peer and keys; synthetic hardware only");')
o='#define PRIVMAC_MODE %d\n#define PRIVMAC_TEXT "%d"\n'%(x.mode,x.mode)+o
W=x.log_dir.resolve();W.mkdir(parents=True,exist_ok=True);(W/'owner.c').write_text(o)
r=(F/'run-owner.py').read_text()
r=sub(r,'F=Path(__file__).resolve().parent;P=R/"tools"','F=Path(__file__).resolve().parent;P=R/"tools"')
r=sub(r,"if(handover_park&&njs_handover.state&&nsr_native.scan_ready&&!njs_native.active)longjmp(parked_exit,1);",
        "if(handover_park&&njs_handover.state&&nsr_native.scan_ready&&!njs_native.active)longjmp(parked_exit,1);if(mac_park&&njs_native.queued&&!njs_native.active&&!nss_native.mac_switch&&!njs_handover.state)longjmp(parked_exit,1);")
r=sub(r,"owner='static unsigned handover_park,radio_ssid=88,radio_ap;\\\\n'+owner",
        "owner=owner.replace('normal_pubs++;assert(cmd[37]==0xa0','normal_pubs++;if(cmd[36]==0x11){if(cmd[65]){activate_seen++;activate_last=cmd[65];memcpy(activate_mac,cmd+68,6);}else{deactivate_seen++;memcpy(deactivate_mac,cmd+68,6);}}assert(cmd[37]==0xa0');assert 'activate_seen++' in owner\n owner='static unsigned handover_park,radio_ssid=88,radio_ap;static unsigned activate_seen,activate_last,deactivate_seen,mac_park;static uint8_t activate_mac[6],deactivate_mac[6];\\\\n'+owner")
# The fixture's firmware model emits scan results for the scan command only; in a continuation that is now the LAST command.
r=sub(r,"f=W/'runner.py';f.write_text(s)","s=s.replace('unsigned ix=nct_native.session.continuation?0:NCB_COMMANDS-1;','unsigned ix=nct_native.session.continuation?nct_native.session.count-1:NCB_COMMANDS-1;');assert 'session.count-1:NCB_COMMANDS-1' in s\nf=W/'runner.py';f.write_text(s)")
(W/'run-owner.py').write_text(r.replace('F=Path(__file__).resolve().parent;','F=Path(%r);'%str(W)).replace("R=next(p for p in Path(__file__).resolve().parents if (p/\"AGENTS.md\").exists());","R=Path(%r);"%str(R)))
c=subprocess.run(['python3',str(W/'run-owner.py'),'--candidate',str(x.candidate.resolve()),'--board',str(x.board),'--bearssl',str(x.bearssl),'--initial-open','0','--log-dir',str(W/'owning')],capture_output=True,text=True)
(W/'runner.log').write_text(c.stdout+c.stderr);print((c.stdout.strip().splitlines() or ['(no output)'])[-1]);sys.exit(c.returncode)
