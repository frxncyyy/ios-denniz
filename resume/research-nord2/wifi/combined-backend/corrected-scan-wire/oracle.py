#!/usr/bin/env python3
import pathlib, subprocess, tempfile, hashlib, json
P=pathlib.Path(__file__).resolve().parent; R=next(x for x in P.parents if x.name=='ios-sweet'); S=R.parent/'nord2-current-kernel/drivers/misc/mediatek/connectivity/wlan/core/gen4m'
def extract(path,start,end):
 t=(S/path).read_text(); return t[t.index(start):t.index(end,t.index(start))]
activation=extract('nic/nic.c','uint32_t nicActivateNetworkEx(IN struct ADAPTER *prAdapter,','uint32_t nicDeactivateNetwork(IN struct ADAPTER *prAdapter,')
actstruct=extract('include/nic_cmd_event.h','struct CMD_BSS_ACTIVATE_CTRL {','enum ENUM_RTS_POLICY {')
cmd=extract('include/wsys_cmd_handler_fw.h','struct CMD_SCAN_REQ_V2 {','/* TLV for CMD_ID_SCAN_REQ_V2*/')
f=extract('mgmt/scan_fsm.c','void scnSendScanReqV2(IN struct ADAPTER *prAdapter)\n{','/*----------------------------------------------------------------------------*/\n/*!\n * \\brief\n *\n * \\param[in]\n *\n * \\return none\n */\n/*----------------------------------------------------------------------------*/\nvoid scnFsmMsgStart')
shim=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "wifi_scan_plan.h"
#include "wifi_normal_config.h"
#define IN
#define CFG_SCAN_OOB_MAX_NUM 4
#define MAC_ADDR_LEN 6
#define SCAN_CMD_SSID_NUM 4
#define CFG_SCAN_SSID_MAX_NUM 10
#define SCAN_CMD_EXT_SSID_NUM 6
#define SCAN_CMD_CHNL_NUM 32
#define MAXIMUM_OPERATION_CHANNEL_LIST 64
#define MAX_IE_LENGTH 600
#define SCAN_CHANNEL_SPECIFIED 4
#define SCAN_CHANNEL_FULL 0
#define ENUM_SCN_USE_PADDING_AS_BSSID 8
#define ENUM_SCN_RANDOM_MAC_EN 1
#define ENUM_SCN_RANDOM_SN_EN 16
#define ENUM_SCN_DBDC_SCAN_DIS 2
#define ENUM_SCN_OCE_SCAN_EN 128
#define ENUM_SCN_SPLIT_SCAN_EN 32
#define ENUM_DBDC_MODE_DISABLED 0
#define ENUM_SW_TEST_MODE_SIGMA_OCE 99
#define SCAN_CHANNEL_DWELL_TIME_MIN_MSEC 42
#define VIR_MEM_TYPE 0
#define CMD_ID_SCAN_REQ_V2 3
#define KAL_NETWORK_TYPE_AIS_INDEX 0
#define TRUE 1
#define FALSE 0
#define ASSERT assert
#define DBGLOG(...) ((void)0)
#define log_dbg(...) ((void)0)
#define kalMemAlloc(n,t) malloc(n)
#define kalMemFree(p,t,n) free(p)
#define kalMemZero memset_zero
#define kalMemCopy memcpy
#define COPY_MAC_ADDR(d,s) memcpy(d,s,6)
#define EQUAL_MAC_ADDR(a,b) (!memcmp(a,b,6))
#define COPY_SSID(d,n,s,z) do{n=z;memcpy(d,s,z);}while(0)
#define GET_CURRENT_SYSTIME(p) (*(p)=0)
#define scanLogCacheFlushAll(...) ((void)0)
#define scanReqLog(...) ((void)0)
#define scanInitEssResult(...) ((void)0)
#define rlmDomainIsDfsChnls(a,c) 0
#define scanHandleOceIE(...) abort()
#define scnEnableSplitScan(a,b) 0
#define wlanWfdEnabled(a) 0
static void memset_zero(void*p,size_t n){memset(p,0,n);}
static int kalIsValidMacAddr(const uint8_t*p){unsigned v=0;for(int i=0;i<6;i++)v|=p[i];return v&&!(p[0]&1);}
struct PARAM_SSID {uint32_t u4SsidLen;uint8_t aucSsid[32];};
struct CHANNEL_INFO {uint8_t ucBand,ucChannelNum;};
struct RF_CHANNEL_INFO {int eBand;uint8_t ucChannelNum;};
struct SCAN_PARAM {
 uint8_t aucBSSID[4][6],ucSeqNum,ucBssIndex,ucSSIDType,ucScnFuncMask,ucBssidMatchCh[4],ucBssidMatchSsidInd[4],aucRandomMac[6],ucSSIDNum,aucSpecifiedSSID[10][32],ucSpecifiedSSIDLen[10];
 int eScanType,eScanChannel;uint16_t u2ProbeDelayTime,u2ChannelDwellTime,u2ChannelMinDwellTime,u2TimeoutValue,u2IELen;uint8_t ucChannelListNum,aucIE[600];struct RF_CHANNEL_INFO arChnlInfoList[64];
};
struct SCAN_INFO {struct SCAN_PARAM rScanParam;int fgSkipDFS,rLastScanStartTime;};
struct BSS_INFO {uint8_t ucBssIndex,ucOwnMacIndex,ucBMCWlanIndex,aucOwnMacAddr[6];int eNetworkType,fg40mBwAllowed,fgAssoc40mBwAllowed;};
#define GET_BSS_INFO_BY_INDEX(a,i) (&(a)->bss)
#define STA_REC_INDEX_NOT_FOUND 255
#define CIPHER_SUITE_NONE 0
#define CMD_ID_BSS_ACTIVATE_CTRL 0x11
#define secPrivacySeekForBcEntry(...) 4
struct ADAPTER {struct BSS_INFO bss;uint8_t ucHwBssIdNum;struct {struct SCAN_INFO rScanInfo;int eDbdcMode,u4SwTestMode;}rWifiVar;};
static uint8_t captured[1188],activated[12];
static uint32_t wlanSendSetQueryCmd(struct ADAPTER*a,int cid,int set,int resp,int oid,void*d,void*t,size_t n,uint8_t*p,void*i,int z){(void)a;(void)set;(void)resp;(void)oid;(void)d;(void)t;(void)i;(void)z;assert((cid==3&&n==1188)||(cid==0x11&&n==12));if(cid==3)memcpy(captured,p,n);else memcpy(activated,p,n);return 0;}
'''
main=r'''
int main(int argc,char **argv){
 assert(argc==2);uint8_t board[6146];FILE*bf=fopen(argv[1],"rb");assert(bf&&fread(board,1,sizeof(board),bf)==sizeof(board));fclose(bf);
 struct ncfg_input in={board,sizeof(board),{.phy_present=1,.band5=1,.nss=2,.max_bw=2,.path=15},20827};struct ncfg_plan plan;struct ncfg_channel actual[39];size_t actual_count=0;assert(!ncfg_prepare(&in,&plan)&&!ncfg_channels(&in,&plan,actual,39,&actual_count));
 _Static_assert(sizeof(struct CMD_SCAN_REQ_V2)==1188,"body");
 _Static_assert(offsetof(struct CMD_SCAN_REQ_V2,aucBSSID)==1112,"BSSID");
 _Static_assert(offsetof(struct CMD_SCAN_REQ_V2,arChannelListExtend)==830,"extended channels");
 uint8_t cap[40]={0},mac[6]={2,0,0,0,0,1};cap[3]=0x38;cap[28]=0xec;cap[29]=4;
 struct ADAPTER activate={0};activate.bss.ucOwnMacIndex=1;memcpy(activate.bss.aucOwnMacAddr,mac,6);assert(!nicActivateNetworkEx(&activate,0,1));
 unsigned cases=0;for(unsigned count=1;count<=64;count++)for(unsigned disabled=0;disabled<=1;disabled++)for(unsigned timing=0;timing<=1;timing++){
  struct ADAPTER a={0};struct SCAN_PARAM*p=&a.rWifiVar.rScanInfo.rScanParam;struct ncs_channel channels[64];struct ncs_frames out;
  a.rWifiVar.eDbdcMode=disabled?0:1;p->ucSeqNum=1;p->eScanChannel=4;p->ucChannelListNum=count;
  p->u2ChannelDwellTime=timing?40:0;p->u2ChannelMinDwellTime=timing?20:0;
  for(unsigned i=0;i<count;i++){channels[i]=(struct ncs_channel){i<14?1:2,i<14?i+1:i+22};p->arChnlInfoList[i]=(struct RF_CHANNEL_INFO){channels[i].band,channels[i].channel};}
  scnSendScanReqV2(&a);assert(!ncs_build(cap,40,4,mac,channels,count,26,27,1,disabled,p->u2ChannelDwellTime,p->u2ChannelMinDwellTime,&out));
  assert(!memcmp(activated,out.activate+64,12));unsigned differences=0;for(unsigned j=0;j<1188;j++)if(captured[j]!=out.scan[64+j]){assert(j>=1112&&j<1118);assert(captured[j]==0&&out.scan[64+j]==255);differences++;}assert(differences==0);cases++;
 }
 {struct ADAPTER a={0};struct SCAN_PARAM*p=&a.rWifiVar.rScanInfo.rScanParam;struct ncs_channel channels[64];struct ncs_frames out;unsigned count=0;
 for(size_t i=0;i<actual_count;i++)if(actual[i].allowed&&!actual[i].dfs){channels[count]=(struct ncs_channel){actual[i].band,actual[i].channel};p->arChnlInfoList[count]=(struct RF_CHANNEL_INFO){actual[i].band,actual[i].channel};count++;}
 assert(count==22);p->ucSeqNum=1;p->eScanChannel=4;p->ucChannelListNum=count;scnSendScanReqV2(&a);assert(!ncs_build(cap,40,4,mac,channels,count,26,27,1,1,0,0,&out));assert(!memcmp(activated,out.activate+64,12));unsigned differences=0;for(unsigned j=0;j<1188;j++)if(captured[j]!=out.scan[64+j]){assert(j>=1112&&j<1118);differences++;}assert(differences==0);cases++;}
 printf("{\"body_bytes\":1188,\"cases\":%u,\"difference_offsets\":[],\"other_bytes_equal\":1188,\"synthetic\":true}\n",cases);
}
'''
with tempfile.TemporaryDirectory() as td:
 c=pathlib.Path(td)/'oracle.c';c.write_text(shim+cmd+actstruct+f+activation+main);exe=pathlib.Path(td)/'oracle'
 subprocess.run(['clang','-std=c11','-fsanitize=address,undefined','-fno-sanitize-recover=all','-g','-I'+str(R/'shim/nord2-xnu'),str(c),str(R/'shim/nord2-xnu/wifi_scan_plan.c'),str(R/'shim/nord2-xnu/wifi_capability.c'),'-DNCFG_HOST_TEST',*[str(R/'shim/nord2-xnu'/name) for name in ['wifi_normal_config.c','wifi_normal_init.c','wifi_regulatory_policy.c','wifi_basic_primitives.c']],'-lcrypto','-o',str(exe)],check=True)
 result=subprocess.run([str(exe),str(R/'research/nord2/wifi/calibration/WIFI')],check=True,capture_output=True,text=True);print(result.stdout,end='')
 (P/'oracle-result.json').write_text(result.stdout)
(P/'extracted-builder.c').write_text(f)
(P/'extracted-wire-struct.h').write_text(cmd)

(P/'extracted-activation.c').write_text(activation)
(P/'extracted-activation-struct.h').write_text(actstruct)
