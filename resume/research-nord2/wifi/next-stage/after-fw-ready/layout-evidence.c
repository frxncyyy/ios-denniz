#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#define ELEM_MAX_LEN_SSID 32
#define MAC_ADDR_LEN 6
#define CFG_SCAN_OOB_MAX_NUM 4

struct CHANNEL_INFO {
	uint8_t  ucBand;
	uint8_t  ucChannelNum;
};
struct PARAM_SSID {
	uint32_t  u4SsidLen;
	/*!< SSID length in bytes. Zero length is broadcast(any) SSID */
	uint8_t   aucSsid[ELEM_MAX_LEN_SSID];
};
struct CMD_SCAN_REQ_V2 {
	uint8_t          ucSeqNum;
	uint8_t          ucBssIndex;
	uint8_t          ucScanType;
	uint8_t          ucSSIDType;
	uint8_t          ucSSIDNum;
	uint8_t          ucNumProbeReq;
	uint8_t          ucScnFuncMask;
	uint8_t          auVersion[1];
	struct PARAM_SSID    arSSID[4];
	uint16_t         u2ProbeDelayTime;
	uint16_t         u2ChannelDwellTime;
	uint16_t         u2TimeoutValue;
	uint8_t          ucChannelType;
	uint8_t          ucChannelListNum;
	struct CHANNEL_INFO  arChannelList[32];
	uint16_t         u2IELen;
	uint8_t          aucIE[600];  /*depends on u2IELen*/
	uint8_t          ucChannelListExtNum;
	uint8_t          ucSSIDExtNum;
	uint16_t         u2ChannelMinDwellTime;
	struct CHANNEL_INFO  arChannelListExtend[32];
	struct PARAM_SSID    arSSIDExtend[6];
	uint8_t          aucBSSID[MAC_ADDR_LEN];
	uint8_t          aucRandomMac[MAC_ADDR_LEN];
	uint8_t          aucExtBSSID[CFG_SCAN_OOB_MAX_NUM][MAC_ADDR_LEN];
	uint8_t          ucShortSSIDNum;
	uint8_t		 ucBssidMatchCh[CFG_SCAN_OOB_MAX_NUM];
	uint8_t		 ucBssidMatchSsidInd[CFG_SCAN_OOB_MAX_NUM];
	uint8_t          aucPadding_3[31];

};
int main(void){printf("CMD_SCAN_REQ_V2 size=%zu\n",sizeof(struct CMD_SCAN_REQ_V2));
printf("arSSID=%zu\n",offsetof(struct CMD_SCAN_REQ_V2,arSSID));
printf("u2ProbeDelayTime=%zu\n",offsetof(struct CMD_SCAN_REQ_V2,u2ProbeDelayTime));
printf("u2ChannelDwellTime=%zu\n",offsetof(struct CMD_SCAN_REQ_V2,u2ChannelDwellTime));
printf("u2TimeoutValue=%zu\n",offsetof(struct CMD_SCAN_REQ_V2,u2TimeoutValue));
printf("ucChannelType=%zu\n",offsetof(struct CMD_SCAN_REQ_V2,ucChannelType));
printf("arChannelList=%zu\n",offsetof(struct CMD_SCAN_REQ_V2,arChannelList));
printf("u2IELen=%zu\n",offsetof(struct CMD_SCAN_REQ_V2,u2IELen));
printf("aucIE=%zu\n",offsetof(struct CMD_SCAN_REQ_V2,aucIE));
printf("arChannelListExtend=%zu\n",offsetof(struct CMD_SCAN_REQ_V2,arChannelListExtend));
printf("arSSIDExtend=%zu\n",offsetof(struct CMD_SCAN_REQ_V2,arSSIDExtend));
printf("aucBSSID=%zu\n",offsetof(struct CMD_SCAN_REQ_V2,aucBSSID));
printf("aucRandomMac=%zu\n",offsetof(struct CMD_SCAN_REQ_V2,aucRandomMac));
printf("aucPadding_3=%zu\n",offsetof(struct CMD_SCAN_REQ_V2,aucPadding_3));
return 0;}