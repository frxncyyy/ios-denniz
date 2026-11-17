#include <stdint.h>
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

_Static_assert(sizeof(struct CMD_SCAN_REQ_V2)==1188,"scan size");
_Static_assert(offsetof(struct CMD_SCAN_REQ_V2,arSSIDExtend)==896,"natural padding");
