#!/usr/bin/env python3
"""GPL-2.0 MediaTek extracted RX-filter source oracle. Metadata only."""
from pathlib import Path
import argparse,hashlib,subprocess,tempfile
R=Path(__file__).resolve().parents[5]
a=argparse.ArgumentParser();a.add_argument('--source',type=Path,default=R.parent/'nord2-current-kernel');x=a.parse_args();w=x.source/'drivers/misc/mediatek/connectivity/wlan/core/gen4m'
def extract(text,start):
 i=text.index(start);b=text.index('{',i);n=1;j=b+1
 while n:
  n+=(text[j]=='{')-(text[j]=='}');j+=1
 return text[i:j]
h=(w/'include/nic_cmd_event.h').read_text();c=(w/'common/wlan_oid.c').read_text()
expected={'include/nic_cmd_event.h':'8fa31ca08567b73f4a796350f77e5c813274b9c068ffcb6098dc30e178a7da47','common/wlan_oid.c':'0c065fa546b9eb91d9fdd04cbd04fa4ebad50c4fd71259336efb07b6342623cc'}
for name,pin in expected.items():
 if hashlib.sha256((w/name).read_bytes()).hexdigest()!=pin:raise SystemExit('selected source hash mismatch: '+name)
s=extract(h,'struct CMD_RX_PACKET_FILTER {')+';\n'+extract(c,'uint32_t wlanoidSetPacketFilter(struct ADAPTER *prAdapter,')
prefix=r'''/* SPDX-License-Identifier: GPL-2.0 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#define TRUE 1
#define FALSE 0
#define PARAM_PACKET_FILTER_MULTICAST 2
#define PARAM_PACKET_FILTER_ALL_MULTICAST 4
#define CMD_ID_SET_RX_FILTER 10
#define DBGLOG(...) ((void)0)
typedef unsigned char u_int8_t;
struct GLUE {int fgIsInSuspendMode;}; struct ADAPTER {struct GLUE *prGlueInfo;};
static unsigned char captured[68];
#define nicCmdEventSetCommon 0
#define nicOidCmdTimeoutCommon 0
static uint32_t wlanSendSetQueryCmd(struct ADAPTER*a,unsigned cid,unsigned set,unsigned resp,unsigned oid,int cb,int to,size_t n,uint8_t*p,void*out,uint32_t cap){(void)a;(void)oid;(void)cb;(void)to;(void)out;(void)cap;assert(cid==10&&set==1&&!resp&&n==68);memcpy(captured,p,n);return 0;}
'''
suffix=r'''
int main(void){struct GLUE g={0};struct ADAPTER a={&g};struct CMD_RX_PACKET_FILTER f={0};f.u4RxPacketFilter=12;assert(!wlanoidSetPacketFilter(&a,&f,1,0,0));assert(captured[0]==12);for(unsigned i=1;i<68;i++)assert(!captured[i]);return 0;}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'x.c').write_text(prefix+s+suffix);subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p/'x.c'),'-o',str(p/'x')],check=True);subprocess.run([str(p/'x')],check=True)
print('PASS extracted source CID0a SET/noresponse body68 mask0xc reserved64zero')
