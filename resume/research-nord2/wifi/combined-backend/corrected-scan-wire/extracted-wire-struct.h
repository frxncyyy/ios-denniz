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

