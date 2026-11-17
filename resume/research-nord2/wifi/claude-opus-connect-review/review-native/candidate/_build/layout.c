#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define MAC_ADDR_LEN 6
#define HE_OP_BYTE_NUM 3
#define HE_MAC_CAP_BYTE_NUM 6
#define HE_PHY_CAP_BYTE_NUM 11
#define CFG_SUPPORT_802_11AX 1
#define CFG_SUPPORT_802_11BE 0
struct CMD_802_11_KEY {
	uint8_t      ucAddRemove;
	uint8_t      ucTxKey;   
	uint8_t      ucKeyType; 
	uint8_t      ucIsAuthenticator; 
	uint8_t      aucPeerAddr[6];
	uint8_t      ucBssIdx; 
	uint8_t      ucAlgorithmId;
	uint8_t      ucKeyId;
	uint8_t      ucKeyLen;
	uint8_t      ucWlanIndex;
	uint8_t      ucMgmtProtection;
	uint8_t      aucKeyMaterial[32];
	uint8_t      aucKeyRsc[16];
};
struct CMD_SET_BSS_RLM_PARAM {
	uint8_t      ucBssIndex;
	uint8_t      ucRfBand;
	uint8_t      ucPrimaryChannel;
	uint8_t      ucRfSco;
	uint8_t      ucErpProtectMode;
	uint8_t      ucHtProtectMode;
	uint8_t      ucGfOperationMode;
	uint8_t      ucTxRifsMode;
	uint16_t     u2HtOpInfo3;
	uint16_t     u2HtOpInfo2;
	uint8_t      ucHtOpInfo1;
	uint8_t      ucUseShortPreamble;
	uint8_t      ucUseShortSlotTime;
	uint8_t      ucVhtChannelWidth;
	uint8_t      ucVhtChannelFrequencyS1;
	uint8_t      ucVhtChannelFrequencyS2;
	uint16_t     u2VhtBasicMcsSet;
	uint8_t      ucTxNss;
	uint8_t      ucRxNss;
};
struct CMD_SET_BSS_INFO {
	uint8_t  ucBssIndex;
	uint8_t  ucConnectionState;
	uint8_t  ucCurrentOPMode;
	uint8_t  ucSSIDLen;
	uint8_t  aucSSID[32];
	uint8_t  aucBSSID[6];
	uint8_t  ucIsQBSS;
	uint8_t  ucVersion;
	uint16_t u2OperationalRateSet;
	uint16_t u2BSSBasicRateSet;
	uint8_t  ucStaRecIdxOfAP;
	uint8_t  aucPadding0[1];
	uint16_t u2HwDefaultFixedRateCode;
	uint8_t  ucNonHTBasicPhyType; 
	uint8_t  ucAuthMode;
	uint8_t  ucEncStatus;
	uint8_t  ucPhyTypeSet;
	uint8_t  ucWapiMode;
	uint8_t  ucIsApMode;
	uint8_t  ucBMCWlanIndex;
	uint8_t  ucHiddenSsidMode;
	uint8_t  ucDisconnectDetectThreshold;
	uint8_t  ucIotApAct;
	uint8_t  aucPadding1[2];
	uint32_t u4PrivateData;
	struct CMD_SET_BSS_RLM_PARAM rBssRlmParam; 
	uint8_t  ucDBDCBand;  
	uint8_t  ucWmmSet;
	uint8_t  ucDBDCAction;
	uint8_t  ucNss;
	uint8_t  aucPadding2[2];
	uint8_t  ucHeOpParams[HE_OP_BYTE_NUM];
	uint8_t  ucBssColorInfo;
	uint16_t u2HeBasicMcsSet;
	uint8_t  ucMaxBSSIDIndicator;
	uint8_t  ucMBSSIDIndex;
	uint8_t  aucPadding[12];
};
struct CMD_HTVHT_BA_SIZE {
	uint8_t ucTxBaSize;
	uint8_t ucRxBaSize;
	uint8_t aucReserved[2];
};
struct CMD_HE_BA_SIZE {
	uint16_t u2TxBaSize;
	uint16_t u2RxBaSize;
};
struct CMD_EHT_BA_SIZE {
	uint16_t u2TxBaSize;
	uint16_t u2RxBaSize;
};
struct CMD_UPDATE_STA_RECORD {
	uint8_t  ucStaIndex;
	uint8_t  ucStaType;
	uint8_t  aucMacAddr[MAC_ADDR_LEN];
	

	uint16_t u2AssocId;
	uint16_t u2ListenInterval;
	uint8_t  ucBssIndex;
	
	uint8_t  ucDesiredPhyTypeSet;
	uint16_t u2DesiredNonHTRateSet;

	uint16_t u2BSSBasicRateSet;
	uint8_t  ucIsQoS;
	uint8_t  ucIsUapsdSupported;
	uint8_t  ucStaState;
	uint8_t  ucMcsSet;
	uint8_t  ucSupMcs32;
	uint8_t  ucVersion;
	

	uint8_t  aucRxMcsBitmask[10];
	uint16_t u2RxHighestSupportedRate;
	uint32_t u4TxRateInfo;

	uint16_t u2HtCapInfo;
	uint16_t u2HtExtendedCap;
	uint32_t u4TxBeamformingCap;

	uint8_t  ucAmpduParam;
	uint8_t  ucAselCap;
	uint8_t  ucRCPI;
	uint8_t  ucNeedResp;
	uint8_t  ucUapsdAc;
	
	uint8_t  ucUapsdSp;
	
	uint8_t  ucWlanIndex;
	
	uint8_t  ucBMCWlanIndex;
	

	uint32_t u4VhtCapInfo;
	uint16_t u2VhtRxMcsMap;
	uint16_t u2VhtRxHighestSupportedDataRate;
	uint16_t u2VhtTxMcsMap;
	uint16_t u2VhtTxHighestSupportedDataRate;
	uint8_t  ucRtsPolicy;
	
	uint8_t  ucVhtOpMode;
	

	uint8_t  ucTrafficDataType;
	uint8_t  ucTxGfMode;               
	uint8_t  ucTxSgiMode;              
	uint8_t  ucTxStbcMode;             
	uint16_t u2HwDefaultFixedRateCode;
	uint8_t  ucTxAmpdu;
	uint8_t  ucRxAmpdu;
	uint32_t u4FixedPhyRate;
	
	uint16_t u2MaxLinkSpeed;
	
	uint16_t u2MinLinkSpeed;

	uint32_t  u4Flags;

	union BA_SIZE {
		struct CMD_HTVHT_BA_SIZE rHtVhtBaSize;
		struct CMD_HE_BA_SIZE rHeBaSize;
		struct CMD_EHT_BA_SIZE rEhtBaSize;
	} rBaSize;

	uint16_t   u2PfmuId;   
	uint8_t   fgSU_MU;    
	uint8_t   fgETxBfCap; 
	uint8_t    ucSoundingPhy;
	uint8_t    ucNdpaRate;
	uint8_t    ucNdpRate;
	uint8_t    ucReptPollRate;
	uint8_t    ucTxMode;
	uint8_t    ucNc;
	uint8_t    ucNr;
	uint8_t    ucCBW;      
	uint8_t    ucTotMemRequire;
	uint8_t    ucMemRequire20M;
	uint8_t    ucMemRow0;
	uint8_t    ucMemCol0;
	uint8_t    ucMemRow1;
	uint8_t    ucMemCol1;
	uint8_t    ucMemRow2;
	uint8_t    ucMemCol2;
	uint8_t    ucMemRow3;
	uint8_t    ucMemCol3;
	uint16_t   u2SmartAnt;
	uint8_t    ucSEIdx;
	uint8_t    uciBfTimeOut;
	uint8_t    uciBfDBW;
	uint8_t    uciBfNcol;
	uint8_t    uciBfNrow;
	uint8_t    aucPadding1[3];

	uint8_t ucTxAmsduInAmpdu;
	uint8_t ucRxAmsduInAmpdu;
	uint8_t aucPadding2[2];
	uint32_t u4TxMaxAmsduInAmpduLen;

#if CFG_SUPPORT_802_11AX
	
	uint8_t  ucHeMacCapInfo[HE_MAC_CAP_BYTE_NUM];
	uint8_t  ucHePhyCapInfo[HE_PHY_CAP_BYTE_NUM];
	uint8_t  aucPadding3[2];
	uint16_t u2HeRxMcsMapBW80;
	uint16_t u2HeTxMcsMapBW80;
	uint16_t u2HeRxMcsMapBW160;
	uint16_t u2HeTxMcsMapBW160;
	uint16_t u2HeRxMcsMapBW80P80;
	uint16_t u2HeTxMcsMapBW80P80;
	uint16_t u2He6gBandCapInfo;
	uint8_t  aucPadding4[2];
	uint8_t  aucPadding5[28];
#if (CFG_SUPPORT_802_11BE == 1)
	uint8_t ucEhtMacCapInfo[EHT_MAC_CAP_BYTE_NUM];
	uint8_t ucEhtPhyCapInfo[EHT_PHY_CAP_BYTE_NUM];
#endif
#else
	uint8_t  aucPadding4[32];
#endif
};
struct CMD_CH_PRIVILEGE {
	uint8_t ucBssIndex;
	uint8_t ucTokenID;
	uint8_t ucAction;
	uint8_t ucPrimaryChannel;
	uint8_t ucRfSco;
	uint8_t ucRfBand;
	uint8_t ucRfChannelWidth;	
	uint8_t ucRfCenterFreqSeg1;	
	uint8_t ucRfCenterFreqSeg2;	
	uint8_t ucReqType;
	uint8_t ucDBDCBand;
	uint8_t aucReserved;
	uint32_t u4MaxInterval;	
	uint8_t aucReserved2[8];
};
int main(void) {
printf("#define SW_SIZE_CMD_802_11_KEY %zu\n",sizeof(struct CMD_802_11_KEY));
printf("#define SW_OFF_CMD_802_11_KEY_ucAddRemove %zu\n",offsetof(struct CMD_802_11_KEY,ucAddRemove));
printf("#define SW_OFF_CMD_802_11_KEY_ucTxKey %zu\n",offsetof(struct CMD_802_11_KEY,ucTxKey));
printf("#define SW_OFF_CMD_802_11_KEY_ucKeyType %zu\n",offsetof(struct CMD_802_11_KEY,ucKeyType));
printf("#define SW_OFF_CMD_802_11_KEY_ucIsAuthenticator %zu\n",offsetof(struct CMD_802_11_KEY,ucIsAuthenticator));
printf("#define SW_OFF_CMD_802_11_KEY_aucPeerAddr %zu\n",offsetof(struct CMD_802_11_KEY,aucPeerAddr));
printf("#define SW_OFF_CMD_802_11_KEY_ucBssIdx %zu\n",offsetof(struct CMD_802_11_KEY,ucBssIdx));
printf("#define SW_OFF_CMD_802_11_KEY_ucAlgorithmId %zu\n",offsetof(struct CMD_802_11_KEY,ucAlgorithmId));
printf("#define SW_OFF_CMD_802_11_KEY_ucKeyId %zu\n",offsetof(struct CMD_802_11_KEY,ucKeyId));
printf("#define SW_OFF_CMD_802_11_KEY_ucKeyLen %zu\n",offsetof(struct CMD_802_11_KEY,ucKeyLen));
printf("#define SW_OFF_CMD_802_11_KEY_ucWlanIndex %zu\n",offsetof(struct CMD_802_11_KEY,ucWlanIndex));
printf("#define SW_OFF_CMD_802_11_KEY_ucMgmtProtection %zu\n",offsetof(struct CMD_802_11_KEY,ucMgmtProtection));
printf("#define SW_OFF_CMD_802_11_KEY_aucKeyMaterial %zu\n",offsetof(struct CMD_802_11_KEY,aucKeyMaterial));
printf("#define SW_OFF_CMD_802_11_KEY_aucKeyRsc %zu\n",offsetof(struct CMD_802_11_KEY,aucKeyRsc));
printf("#define SW_SIZE_CMD_SET_BSS_RLM_PARAM %zu\n",sizeof(struct CMD_SET_BSS_RLM_PARAM));
printf("#define SW_OFF_CMD_SET_BSS_RLM_PARAM_ucBssIndex %zu\n",offsetof(struct CMD_SET_BSS_RLM_PARAM,ucBssIndex));
printf("#define SW_OFF_CMD_SET_BSS_RLM_PARAM_ucRfBand %zu\n",offsetof(struct CMD_SET_BSS_RLM_PARAM,ucRfBand));
printf("#define SW_OFF_CMD_SET_BSS_RLM_PARAM_ucPrimaryChannel %zu\n",offsetof(struct CMD_SET_BSS_RLM_PARAM,ucPrimaryChannel));
printf("#define SW_OFF_CMD_SET_BSS_RLM_PARAM_ucErpProtectMode %zu\n",offsetof(struct CMD_SET_BSS_RLM_PARAM,ucErpProtectMode));
printf("#define SW_OFF_CMD_SET_BSS_RLM_PARAM_ucUseShortPreamble %zu\n",offsetof(struct CMD_SET_BSS_RLM_PARAM,ucUseShortPreamble));
printf("#define SW_OFF_CMD_SET_BSS_RLM_PARAM_ucUseShortSlotTime %zu\n",offsetof(struct CMD_SET_BSS_RLM_PARAM,ucUseShortSlotTime));
printf("#define SW_OFF_CMD_SET_BSS_RLM_PARAM_ucTxNss %zu\n",offsetof(struct CMD_SET_BSS_RLM_PARAM,ucTxNss));
printf("#define SW_OFF_CMD_SET_BSS_RLM_PARAM_ucRxNss %zu\n",offsetof(struct CMD_SET_BSS_RLM_PARAM,ucRxNss));
printf("#define SW_SIZE_CMD_SET_BSS_INFO %zu\n",sizeof(struct CMD_SET_BSS_INFO));
printf("#define SW_OFF_CMD_SET_BSS_INFO_ucBssIndex %zu\n",offsetof(struct CMD_SET_BSS_INFO,ucBssIndex));
printf("#define SW_OFF_CMD_SET_BSS_INFO_ucConnectionState %zu\n",offsetof(struct CMD_SET_BSS_INFO,ucConnectionState));
printf("#define SW_OFF_CMD_SET_BSS_INFO_ucCurrentOPMode %zu\n",offsetof(struct CMD_SET_BSS_INFO,ucCurrentOPMode));
printf("#define SW_OFF_CMD_SET_BSS_INFO_ucSSIDLen %zu\n",offsetof(struct CMD_SET_BSS_INFO,ucSSIDLen));
printf("#define SW_OFF_CMD_SET_BSS_INFO_aucSSID %zu\n",offsetof(struct CMD_SET_BSS_INFO,aucSSID));
printf("#define SW_OFF_CMD_SET_BSS_INFO_aucBSSID %zu\n",offsetof(struct CMD_SET_BSS_INFO,aucBSSID));
printf("#define SW_OFF_CMD_SET_BSS_INFO_ucIsQBSS %zu\n",offsetof(struct CMD_SET_BSS_INFO,ucIsQBSS));
printf("#define SW_OFF_CMD_SET_BSS_INFO_ucVersion %zu\n",offsetof(struct CMD_SET_BSS_INFO,ucVersion));
printf("#define SW_OFF_CMD_SET_BSS_INFO_u2OperationalRateSet %zu\n",offsetof(struct CMD_SET_BSS_INFO,u2OperationalRateSet));
printf("#define SW_OFF_CMD_SET_BSS_INFO_u2BSSBasicRateSet %zu\n",offsetof(struct CMD_SET_BSS_INFO,u2BSSBasicRateSet));
printf("#define SW_OFF_CMD_SET_BSS_INFO_ucStaRecIdxOfAP %zu\n",offsetof(struct CMD_SET_BSS_INFO,ucStaRecIdxOfAP));
printf("#define SW_OFF_CMD_SET_BSS_INFO_u2HwDefaultFixedRateCode %zu\n",offsetof(struct CMD_SET_BSS_INFO,u2HwDefaultFixedRateCode));
printf("#define SW_OFF_CMD_SET_BSS_INFO_ucNonHTBasicPhyType %zu\n",offsetof(struct CMD_SET_BSS_INFO,ucNonHTBasicPhyType));
printf("#define SW_OFF_CMD_SET_BSS_INFO_ucAuthMode %zu\n",offsetof(struct CMD_SET_BSS_INFO,ucAuthMode));
printf("#define SW_OFF_CMD_SET_BSS_INFO_ucEncStatus %zu\n",offsetof(struct CMD_SET_BSS_INFO,ucEncStatus));
printf("#define SW_OFF_CMD_SET_BSS_INFO_ucPhyTypeSet %zu\n",offsetof(struct CMD_SET_BSS_INFO,ucPhyTypeSet));
printf("#define SW_OFF_CMD_SET_BSS_INFO_ucBMCWlanIndex %zu\n",offsetof(struct CMD_SET_BSS_INFO,ucBMCWlanIndex));
printf("#define SW_OFF_CMD_SET_BSS_INFO_ucDisconnectDetectThreshold %zu\n",offsetof(struct CMD_SET_BSS_INFO,ucDisconnectDetectThreshold));
printf("#define SW_OFF_CMD_SET_BSS_INFO_u4PrivateData %zu\n",offsetof(struct CMD_SET_BSS_INFO,u4PrivateData));
printf("#define SW_OFF_CMD_SET_BSS_INFO_rBssRlmParam %zu\n",offsetof(struct CMD_SET_BSS_INFO,rBssRlmParam));
printf("#define SW_OFF_CMD_SET_BSS_INFO_ucDBDCBand %zu\n",offsetof(struct CMD_SET_BSS_INFO,ucDBDCBand));
printf("#define SW_OFF_CMD_SET_BSS_INFO_ucWmmSet %zu\n",offsetof(struct CMD_SET_BSS_INFO,ucWmmSet));
printf("#define SW_OFF_CMD_SET_BSS_INFO_ucNss %zu\n",offsetof(struct CMD_SET_BSS_INFO,ucNss));
printf("#define SW_SIZE_CMD_UPDATE_STA_RECORD %zu\n",sizeof(struct CMD_UPDATE_STA_RECORD));
printf("#define SW_OFF_CMD_UPDATE_STA_RECORD_ucStaIndex %zu\n",offsetof(struct CMD_UPDATE_STA_RECORD,ucStaIndex));
printf("#define SW_OFF_CMD_UPDATE_STA_RECORD_ucStaType %zu\n",offsetof(struct CMD_UPDATE_STA_RECORD,ucStaType));
printf("#define SW_OFF_CMD_UPDATE_STA_RECORD_aucMacAddr %zu\n",offsetof(struct CMD_UPDATE_STA_RECORD,aucMacAddr));
printf("#define SW_OFF_CMD_UPDATE_STA_RECORD_u2AssocId %zu\n",offsetof(struct CMD_UPDATE_STA_RECORD,u2AssocId));
printf("#define SW_OFF_CMD_UPDATE_STA_RECORD_u2ListenInterval %zu\n",offsetof(struct CMD_UPDATE_STA_RECORD,u2ListenInterval));
printf("#define SW_OFF_CMD_UPDATE_STA_RECORD_ucBssIndex %zu\n",offsetof(struct CMD_UPDATE_STA_RECORD,ucBssIndex));
printf("#define SW_OFF_CMD_UPDATE_STA_RECORD_ucDesiredPhyTypeSet %zu\n",offsetof(struct CMD_UPDATE_STA_RECORD,ucDesiredPhyTypeSet));
printf("#define SW_OFF_CMD_UPDATE_STA_RECORD_u2DesiredNonHTRateSet %zu\n",offsetof(struct CMD_UPDATE_STA_RECORD,u2DesiredNonHTRateSet));
printf("#define SW_OFF_CMD_UPDATE_STA_RECORD_u2BSSBasicRateSet %zu\n",offsetof(struct CMD_UPDATE_STA_RECORD,u2BSSBasicRateSet));
printf("#define SW_OFF_CMD_UPDATE_STA_RECORD_ucStaState %zu\n",offsetof(struct CMD_UPDATE_STA_RECORD,ucStaState));
printf("#define SW_OFF_CMD_UPDATE_STA_RECORD_ucVersion %zu\n",offsetof(struct CMD_UPDATE_STA_RECORD,ucVersion));
printf("#define SW_OFF_CMD_UPDATE_STA_RECORD_ucRCPI %zu\n",offsetof(struct CMD_UPDATE_STA_RECORD,ucRCPI));
printf("#define SW_OFF_CMD_UPDATE_STA_RECORD_ucNeedResp %zu\n",offsetof(struct CMD_UPDATE_STA_RECORD,ucNeedResp));
printf("#define SW_OFF_CMD_UPDATE_STA_RECORD_ucWlanIndex %zu\n",offsetof(struct CMD_UPDATE_STA_RECORD,ucWlanIndex));
printf("#define SW_OFF_CMD_UPDATE_STA_RECORD_ucBMCWlanIndex %zu\n",offsetof(struct CMD_UPDATE_STA_RECORD,ucBMCWlanIndex));
printf("#define SW_OFF_CMD_UPDATE_STA_RECORD_ucRtsPolicy %zu\n",offsetof(struct CMD_UPDATE_STA_RECORD,ucRtsPolicy));
printf("#define SW_OFF_CMD_UPDATE_STA_RECORD_u2HwDefaultFixedRateCode %zu\n",offsetof(struct CMD_UPDATE_STA_RECORD,u2HwDefaultFixedRateCode));
printf("#define SW_SIZE_CMD_CH_PRIVILEGE %zu\n",sizeof(struct CMD_CH_PRIVILEGE));
printf("#define SW_OFF_CMD_CH_PRIVILEGE_ucBssIndex %zu\n",offsetof(struct CMD_CH_PRIVILEGE,ucBssIndex));
printf("#define SW_OFF_CMD_CH_PRIVILEGE_ucTokenID %zu\n",offsetof(struct CMD_CH_PRIVILEGE,ucTokenID));
printf("#define SW_OFF_CMD_CH_PRIVILEGE_ucAction %zu\n",offsetof(struct CMD_CH_PRIVILEGE,ucAction));
printf("#define SW_OFF_CMD_CH_PRIVILEGE_ucPrimaryChannel %zu\n",offsetof(struct CMD_CH_PRIVILEGE,ucPrimaryChannel));
printf("#define SW_OFF_CMD_CH_PRIVILEGE_ucRfSco %zu\n",offsetof(struct CMD_CH_PRIVILEGE,ucRfSco));
printf("#define SW_OFF_CMD_CH_PRIVILEGE_ucRfBand %zu\n",offsetof(struct CMD_CH_PRIVILEGE,ucRfBand));
printf("#define SW_OFF_CMD_CH_PRIVILEGE_ucRfChannelWidth %zu\n",offsetof(struct CMD_CH_PRIVILEGE,ucRfChannelWidth));
printf("#define SW_OFF_CMD_CH_PRIVILEGE_ucReqType %zu\n",offsetof(struct CMD_CH_PRIVILEGE,ucReqType));
printf("#define SW_OFF_CMD_CH_PRIVILEGE_ucDBDCBand %zu\n",offsetof(struct CMD_CH_PRIVILEGE,ucDBDCBand));
printf("#define SW_OFF_CMD_CH_PRIVILEGE_u4MaxInterval %zu\n",offsetof(struct CMD_CH_PRIVILEGE,u4MaxInterval));
return 0;}
