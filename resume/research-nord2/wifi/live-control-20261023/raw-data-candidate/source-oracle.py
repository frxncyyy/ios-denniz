#!/usr/bin/env python3
"""Compile selected original TXD macros in scratch; never publish source artifacts."""
from pathlib import Path
import subprocess,re,argparse,json,hashlib,tempfile
R=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').exists())
G=R.parent/'nord2-current-kernel/drivers/misc/mediatek/connectivity/wlan/core/gen4m'
a=argparse.ArgumentParser();a.add_argument('--output',type=Path,required=True);x=a.parse_args();x.output.mkdir(parents=True,exist_ok=False)
assert subprocess.check_output(['git','-C',str(R.parent/'nord2-current-kernel'),'rev-parse','HEAD'],text=True).strip()=='04d00c2565764c3922c9b4826b5acff1106e533f'
h=G/'include/nic/nic_connac2x_tx.h';header=re.sub(r'^\s*#include[^\n]*','',h.read_text(),flags=re.M)
pre='''#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#define CFG_SUPPORT_CONNAC2X 1
#define BIT(n) (UINT32_C(1)<<(n))
#define BITS(a,b) ((UINT32_MAX>>(31-(b))) & (UINT32_MAX<<(a)))
#define TX_DESC_SET_FIELD(d,v,m,s) ((d)=((d)&~(m))|(((uint32_t)(v)<<(s))&(m)))
#define MSEC_TO_USEC(x) ((x)*1000)
#define USEC_PER_TU 1024
#define TX_DESC_TX_TIME_NO_LIMIT 0
'''
body=r'''
#include "wifi_station_tx.h"
int main(void){unsigned checks=0;uint8_t eth[1514]={0},out[4096];size_t used;
struct sw_peer peer={{2,0,0,0,0,1},{2,0,0,0,0,2}};memset(eth,255,6);memcpy(eth+6,peer.self,6);
for(unsigned rate=0;rate<2;rate++)for(unsigned special=0;special<2;special++)for(unsigned group=0;group<2;group++)for(unsigned pid=1;pid<128;pid+=7){
 struct sw_txd d={.wlan=5,.own_mac=rate,.queue=1,.pid=pid,.retry_limit=30,.fixed_rate=rate?11:0,.pse_header_len=8,.lifetime_ms=2000,.protected_frame=1};
 eth[0]=group?255:2;eth[12]=8;eth[13]=special?6:0;struct HW_MAC_CONNAC2X_TX_DESC o;memset(&o,0,sizeof o);
 HAL_MAC_CONNAC2X_TXD_SET_TX_BYTE_COUNT(&o,32+78);
 HAL_MAC_CONNAC2X_TXD_SET_ETHER_TYPE_OFFSET(&o,(8+24+6)/2);
 HAL_MAC_CONNAC2X_TXD_SET_QUEUE_INDEX(&o,1);HAL_MAC_CONNAC2X_TXD_SET_PKT_FORMAT(&o,0);
 HAL_MAC_CONNAC2X_TXD_SET_WLAN_INDEX(&o,5);HAL_MAC_CONNAC2X_TXD_SET_VTA(&o,1);
 HAL_MAC_CONNAC2X_TXD_SET_HEADER_FORMAT(&o,2);HAL_MAC_CONNAC2X_TXD_SET_802_11_HEADER_LENGTH(&o,12);
 HAL_MAC_CONNAC2X_TXD_SET_LONG_FORMAT(&o);HAL_MAC_CONNAC2X_TXD_SET_OWN_MAC_INDEX(&o,d.own_mac);
 HAL_MAC_CONNAC2X_TXD_SET_HEADER_PADDING(&o,0);HAL_MAC_CONNAC2X_TXD_SET_TID(&o,0);
 HAL_MAC_CONNAC2X_TXD_SET_TYPE(&o,2);HAL_MAC_CONNAC2X_TXD_SET_SUB_TYPE(&o,0);
 HAL_MAC_CONNAC2X_TXD7_SET_TYPE(&o,2);HAL_MAC_CONNAC2X_TXD7_SET_SUB_TYPE(&o,0);
 HAL_MAC_CONNAC2X_TXD_SET_REMAINING_LIFE_TIME_IN_MS(&o,2000);HAL_MAC_CONNAC2X_TXD_SET_REMAINING_TX_COUNT(&o,30);
 HAL_MAC_CONNAC2X_TXD_SET_PROTECTION(&o);HAL_MAC_CONNAC2X_TXD_SET_PID(&o,pid);HAL_MAC_CONNAC2X_TXD_SET_TXS_TO_MCU(&o);
 if(special){HAL_MAC_CONNAC2X_TXD_SET_FIXED_RATE_ENABLE(&o);HAL_MAC_CONNAC2X_TXD_SET_FR_RATE(&o,d.fixed_rate);HAL_MAC_CONNAC2X_TXD_SET_BA_DISABLE(&o);}
 assert(!nstx_build_profile(out,4096,&used,&d,eth,60,0,23,0x90855000,1,&peer)&&used==142);
 assert(!memcmp(out,&o,32));checks++;
}
printf("PASS %u byte-exact raw ToDS TXD source macro vectors\n",checks);return 0;}
'''
with tempfile.TemporaryDirectory() as z:
 p=Path(z);(p/'oracle.c').write_text(pre+header+body);cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(R/'shim/nord2-xnu'),str(p/'oracle.c'),str(R/'shim/nord2-xnu/wifi_station_tx.c'),str(R/'shim/nord2-xnu/wifi_station_wire.c'),'-o',str(p/'test')]
 c=subprocess.run(cmd,capture_output=True,text=True);(x.output/'compile.log').write_text(c.stdout+c.stderr);c.check_returncode();v=subprocess.run([str(p/'test')],capture_output=True,text=True);(x.output/'test.log').write_text(v.stdout+v.stderr);v.check_returncode();print(v.stdout,end='')
(x.output/'report.json').write_text(json.dumps({'passed':True,'hardware_claim':False,'source_commit':'04d00c2565764c3922c9b4826b5acff1106e533f','source_sha256':{str(h.relative_to(G)):hashlib.sha256(h.read_bytes()).hexdigest()},'test':v.stdout.strip()},indent=2)+'\n')
