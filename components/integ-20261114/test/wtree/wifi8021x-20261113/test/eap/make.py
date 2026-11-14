#!/usr/bin/env python3
"""0450 host test: one 802.1X join on the real owner loop with the UNCHANGED native binder: glue wrappers, e8_ instance of
the stack, the CI stack behind renamed entry points (as in the kernel archive), native transport, RX decode; synthetic
hardware of tools/fixtures/nord2-wifi-handover (not modified). The AP is test/engine/ap.h.
usage: make.py --candidate DIR --log-dir DIR --akm 1|5 [--case 1|2]   (case 2: the supplicant never sets a key)
       make.py --candidate DIR --log-dir DIR --psk    the fixture's own two WPA2-PSK joins, unmodified, through the wrappers"""
from pathlib import Path
import argparse,subprocess,sys
H=Path(__file__).resolve().parent
R=next(p for p in H.parents if (p/'tools/fixtures/nord2-wifi-handover').exists() and (p/'research/nord2/wifi/calibration').exists());F=R/'tools/fixtures/nord2-wifi-handover'
a=argparse.ArgumentParser();a.add_argument('--candidate',type=Path,required=True);a.add_argument('--log-dir',type=Path,required=True)
a.add_argument('--board',type=Path,default=R/'research/nord2/wifi/calibration/WIFI');a.add_argument('--bearssl',type=Path,default=R/'research/ci-39158169212/buildtree/xnu/libkern/crypto/sweet_bearssl')
a.add_argument('--akm',type=int,default=2,choices=[1,2,5]);a.add_argument('--case',type=int,default=1,choices=[1,2]);a.add_argument('--psk',action='store_true');x=a.parse_args();assert x.psk==(x.akm==2)
def sub(t,old,new):
    assert t.count(old)==1,(old[:70],t.count(old));return t.replace(old,new)
if x.psk:
    o=(F/'owner.c').read_text()+'\nint n8x_host_input(const uint8_t*p,size_t n){(void)p;(void)n;assert(0);return 0;} /* no 802.1X frame in a PSK run */\n'
else:
    o=(F/'owner.c').read_text();o=o[:o.index('static void join_completed(')]
    o=sub(o,'int nord2_wifi_ifnet_link(uint64_t g,int a){assert(g);if(a){assert(nst_native.state.controlled_port);link_up_calls++;}else{assert(!nst_native.state.controlled_port);link_down_calls++;}return 0;}\n','static void eap_ap(const struct nst*);static void eap_assoc(const uint8_t*,unsigned);\n')
    o=sub(o,'int nord2_wifi_ifnet_receive(uint64_t g,const uint8_t*p,size_t n){assert(g&&p&&n>=14);return 0;}\n','')
    o=sub(o,' if(s->wire_type==2&&njs_native.driver.owner.transaction.wpa.tx_kind==1&&!m3_sent){uint8_t packet[sizeof m3];make_m3(packet);station_eapol(packet,sizeof packet);m3_sent++;}',' if(s->wire_type==2)eap_ap(s);')
    o=sub(o,' unsigned fc=s->wire[32];if(fc==0xb0||fc==0)station_mgmt(fc==0xb0?0xb0:0x10);}',' unsigned fc=s->wire[32];if(fc==0)eap_assoc(s->wire+32,s->wire_bytes-32);if(fc==0xb0||fc==0)station_mgmt(fc==0xb0?0xb0:0x10);}')
    o='#define EAP_AKM %d\n#define EAP_CASE %d\n'%(x.akm,x.case)+o+(H/'tail.c').read_text()
W=x.log_dir.resolve();W.mkdir(parents=True,exist_ok=True);(W/'owner.c').write_text(o);(W/'ap.h').write_text((H.parent/'engine/ap.h').read_text())
r=(F/'run-owner.py').read_text()
# the synthetic beacon advertises the 802.1X AKM; ap.h sits beside the generated test
r=sub(r,"owner='static unsigned handover_park,radio_ssid=88,radio_ap;\\\\n'+owner","owner='static unsigned handover_park,radio_ssid=88,radio_ap,radio_akm=%d;\\\\n'+owner"%x.akm)
r=sub(r,"'if(!radio_open)memcpy(p+74,rsnbytes,22);deliver(k,p,radio_open?74:96,ddone);'","'if(!radio_open){memcpy(p+74,rsnbytes,22);p[93]=(unsigned char)radio_akm;}deliver(k,p,radio_open?74:96,ddone);'")
RENAME=''.join('#define nwn_%s nwc_%s\n'%(n,n) for n in ('begin','pump','event','peer_complete','cancel'))+'#include "wifi_wpa_nst.c"\n'
(W/'wifi_ci_nst.c').write_text('/* host stand-in of the archive symbol rename of wifi_wpa_nst.o */\n'+RENAME)
r=sub(r,"f=W/'runner.py';f.write_text(s)","s=s.replace(\" (d/'test.c').write_text(test)\",\" (d/'ap.h').write_bytes(Path(%r).read_bytes());(d/'wifi_ci_nst.c').write_bytes(Path(%r).read_bytes())\\n (d/'test.c').write_text(test)\",1);assert 'wifi_ci_nst.c' in s\nn0=s.count(\"'wifi_wpa_nst',\");assert n0==1;s=s.replace(\"'wifi_wpa_nst',\",\"'wifi_ci_nst','wifi_8021x_nst','wifi_8021x_owner','wifi_8021x_stc','wifi_8021x_wpa2','wifi_8021x_glue',\")\nf=W/'runner.py';f.write_text(s)"%(str(W/'ap.h'),str(W/'wifi_ci_nst.c')))
(W/'run-owner.py').write_text(r.replace('F=Path(__file__).resolve().parent;','F=Path(%r);'%str(W)).replace("R=next(p for p in Path(__file__).resolve().parents if (p/\"AGENTS.md\").exists());","R=Path(%r);"%str(R)))
c=subprocess.run(['python3',str(W/'run-owner.py'),'--candidate',str(x.candidate.resolve()),'--board',str(x.board),'--bearssl',str(x.bearssl),'--initial-open','0','--log-dir',str(W/'owning')],capture_output=True,text=True)
(W/'runner.log').write_text(c.stdout+c.stderr);print((c.stdout.strip().splitlines() or ['(no output)'])[-1]);sys.exit(c.returncode)
