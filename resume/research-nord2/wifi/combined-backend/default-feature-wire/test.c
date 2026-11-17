#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "proposed/shim/nord2-xnu/wifi_default_feature.h"
#define IN
#define OPLUS_WLAN_BUG_STABILITY 1
#define CFG_TC3_FEATURE 0
#define CFG_SUPPORT_IOT_AP_BLACKLIST 1
#define CFG_SUPPORT_SMART_GEAR 0
#define WLAN_CFG_DEFAULT 0
#define WLAN_CFG_EM 2
#define WLAN_CFG_ENTRY_NUM_MAX 200
#define WLAN_CFG_KEY_LEN_MAX 32
#define WLAN_CFG_VALUE_LEN_MAX 32
#define MAX_CMD_NAME_MAX_LENGTH 32
#define MAX_CMD_VALUE_MAX_LENGTH 32
#define MAX_CMD_BUFFER_LENGTH 272
#define MAX_CMD_ITEM_MAX 4
#define ITEM_TYPE_STR 3
#define TRUE 1
#define FALSE 0
#define WLAN_STATUS_FAILURE 1
#define CMD_ID_GET_SET_CUSTOMER_CFG 0x70
#define DBGLOG(...) ((void)0)
#define kalMemSet memset
#define kalMemCopy memcpy
#define kalMemZero(p,n) memset(p,0,n)
#define kalStrLen strlen
struct ADAPTER {int unused;};
struct PARAM_CUSTOM_KEY_CFG_STRUCT {uint8_t aucKey[32],aucValue[32];uint32_t u4Flag;};
struct WLAN_CFG_ENTRY {uint8_t aucKey[32],aucValue[32];};
enum CMD_VER {CMD_VER_1,CMD_VER_1_EXT};
enum CMD_TYPE {CMD_TYPE_QUERY,CMD_TYPE_SET};
struct CMD_FORMAT_V1 {uint8_t itemType,itemStringLength,itemValueLength,Reserved;uint8_t itemString[32],itemValue[32];};
struct CMD_HEADER {enum CMD_VER cmdVersion;enum CMD_TYPE cmdType;uint8_t itemNum;uint16_t cmdBufferLen;uint8_t buffer[272];};
#include "source-table.inc"
static struct WLAN_CFG_ENTRY entries[11];
static unsigned sent;
static struct WLAN_CFG_ENTRY *wlanCfgGetEntryByIndex(struct ADAPTER *a,unsigned i,unsigned flag) {(void)a;return flag==0&&i<11?&entries[i]:NULL;}
static unsigned wlanSendSetQueryCmd(struct ADAPTER*a,unsigned cid,unsigned set,unsigned resp,unsigned oid,void*done,void*timeout,unsigned n,uint8_t*p,void*q,unsigned qn) {
 uint8_t expected[284],observed[284]; (void)a;(void)q; assert(!qn&&!done&&!timeout);assert(cid==0x70&&set==1&&!resp&&!oid&&n==284&&sent<3);assert(ndf_body(sent,expected)==0); memcpy(observed,p,284);observed[9]=0;assert(memcmp(observed,expected,284)==0);++sent;return 0;
}
#include "source-function.inc"
int main(void) {
 struct ADAPTER a={0};uint8_t b[284],old[284];unsigned i;
 _Static_assert(sizeof(struct CMD_HEADER)==284,"header");_Static_assert(offsetof(struct CMD_HEADER,cmdBufferLen)==10,"padding");_Static_assert(sizeof(struct CMD_FORMAT_V1)==68,"item");
 assert(sizeof(g_rDefaulteSetting)/sizeof(g_rDefaulteSetting[0])==11);
 for(i=0;i<11;i++){memcpy(entries[i].aucKey,g_rDefaulteSetting[i].aucKey,32);memcpy(entries[i].aucValue,g_rDefaulteSetting[i].aucValue,32);assert(!g_rDefaulteSetting[i].u4Flag);}
 wlanFeatureToFw(&a,0);assert(sent==3);wlanFeatureToFw(&a,2);assert(sent==3);
 memset(b,0xa5,sizeof b);memcpy(old,b,sizeof b);assert(ndf_body(3,b)==-1&&!memcmp(old,b,sizeof b));assert(ndf_body(~0u,b)==-1&&!memcmp(old,b,sizeof b));assert(ndf_body(0,NULL)==-1);
 for(i=0;i<3;i++){assert(!ndf_body(i,b));assert(b[9]==0&&b[8]==(i==2?3:4));}
 puts("PASS: original source three bodies (padding9 normalized), empty EM, atomic invalid inputs");
}
