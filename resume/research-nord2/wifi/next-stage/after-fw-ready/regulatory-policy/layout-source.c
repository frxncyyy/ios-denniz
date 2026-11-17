#include <stdint.h>
#include <stddef.h>
struct SINGLE_SKU_INFO {
	uint32_t u4EfuseCountryCode;
	uint8_t  isEfuseValid;
	uint8_t  ucReserved[7];
};
struct CAP_PHY_CAP {
	uint8_t ucHt; /* 1:support, 0:not*/
	uint8_t ucVht; /* 1:support, 0:not*/
	uint8_t uc5gBand; /* 1:support, 0:not*/
	/* 0: BW20, 1:BW40, 2:BW80, 3:BW160, 4:BW80+80 */
	uint8_t ucMaxBandwidth;
	uint8_t ucNss; /* 1:1x1, 2:2x2, ... */
	uint8_t ucDbdc; /* 1:support, 0:not*/
	uint8_t ucTxLdpc; /* 1:support, 0:not*/
	uint8_t ucRxLdpc; /* 1:support, 0:not*/
	uint8_t ucTxStbc; /* 1:support, 0:not*/
	uint8_t ucRxStbc; /* 1:support, 0:not*/
	/* BIT(0): WLAN_FLAG_2G4_WF0
	 * BIT(1): WLAN_FLAG_5G_WF0
	 * BIT(2): WLAN_FLAG_2G4_WF1
	 * BIT(3): WLAN_FLAG_5G_WF1
	 */
	uint8_t ucWifiPath;
	uint8_t ucHe; /* 1:support, 0:not*/
};
struct CMD_DOMAIN_CHANNEL {
	uint16_t u2ChNum;
	uint8_t  aucPadding[2];
	uint32_t eFlags; /*enum ieee80211_channel_flags*/
};
struct CMD_DOMAIN_ACTIVE_CHANNEL_LIST {
	uint8_t u1ActiveChNum2g;
	uint8_t u1ActiveChNum5g;
	uint8_t u1ActiveChNum6g;
	uint8_t aucPadding[1];
	struct CMD_DOMAIN_CHANNEL arChannels[0];
};
struct CMD_SET_DOMAIN_INFO_V2 {
	/* DWORD_0 - Country code*/
	uint32_t u4CountryCode;

	/* DWORD_1 - 2.4G & 5G BW info*/
	uint8_t  uc2G4Bandwidth; /* CONFIG_BW_20_40M or CONFIG_BW_20M */
	uint8_t  uc5GBandwidth;  /* CONFIG_BW_20_40M or CONFIG_BW_20M */
	uint8_t  uc6GBandwidth;  /* CONFIG_BW_20_40M or CONFIG_BW_20M */
	uint8_t  aucPadding[1];

	/* DWORD_2 ~ - 2.4G & 5G active channel info*/
	struct CMD_DOMAIN_ACTIVE_CHANNEL_LIST arActiveChannels;
};
struct WIFI_CFG_PREFIX {
	/* NVRAM offset[0] ~ offset[255] */
	uint16_t u2Part1OwnVersion;
	uint16_t u2Part1PeerVersion;
	uint8_t aucMacAddress[6];
	uint8_t aucCountryCode[2];
	uint8_t aucOldTxPwr0[185];

	uint8_t ucSupport5GBand;
	uint8_t aucOldTxPwr1[4];

	uint8_t ucRegChannelListMap;
	uint8_t ucRegChannelListIndex;
	uint8_t aucRegSubbandInfo[36];
	uint8_t ucEnable5GBand;
};
_Static_assert(sizeof(struct SINGLE_SKU_INFO)==12,"SKU body");
_Static_assert(offsetof(struct SINGLE_SKU_INFO,isEfuseValid)==4,"SKU valid");
_Static_assert(sizeof(struct CAP_PHY_CAP)==12,"PHY body");
_Static_assert(offsetof(struct CAP_PHY_CAP,uc5gBand)==2,"PHY band");
_Static_assert(offsetof(struct CAP_PHY_CAP,ucNss)==4,"PHY NSS");
_Static_assert(offsetof(struct CAP_PHY_CAP,ucWifiPath)==10,"PHY path");
_Static_assert(sizeof(struct CMD_DOMAIN_CHANNEL)==8,"domain channel");
_Static_assert(offsetof(struct CMD_DOMAIN_CHANNEL,eFlags)==4,"flags");
_Static_assert(sizeof(struct CMD_SET_DOMAIN_INFO_V2)==12,"domain prefix");
_Static_assert(offsetof(struct CMD_SET_DOMAIN_INFO_V2,arActiveChannels)==8,"counts");
_Static_assert(offsetof(struct WIFI_CFG_PREFIX,aucCountryCode)==10,"board country");
_Static_assert(offsetof(struct WIFI_CFG_PREFIX,ucSupport5GBand)==197,"board support");
_Static_assert(offsetof(struct WIFI_CFG_PREFIX,ucEnable5GBand)==240,"board enable");

#define MAX_SUBBAND_NUM 6
struct CMD_SUBBAND_INFO {
	uint8_t ucRegClass;
	uint8_t ucBand;
	uint8_t ucChannelSpan;
	uint8_t ucFirstChannelNum;
	uint8_t ucNumChannels;
	uint8_t aucReserved[3];
};
struct CMD_SET_DOMAIN_INFO {
	uint16_t u2CountryCode;
	uint16_t u2IsSetPassiveScan;
	struct CMD_SUBBAND_INFO rSubBand[MAX_SUBBAND_NUM];

	uint8_t uc2G4Bandwidth;	/* CONFIG_BW_20_40M or CONFIG_BW_20M */
	uint8_t uc5GBandwidth;	/* CONFIG_BW_20_40M or CONFIG_BW_20M */
	uint8_t aucReserved[2];
};
struct CMD_CHANNEL_POWER_LIMIT_V2 {
	uint8_t ucCentralCh;
	uint8_t ucReserved[3];

	uint8_t tx_pwr_dsss_cck;
	uint8_t tx_pwr_dsss_bpsk;

	uint8_t tx_pwr_ofdm_bpsk; /* 6M, 9M */
	uint8_t tx_pwr_ofdm_qpsk; /* 12M, 18M */
	uint8_t tx_pwr_ofdm_16qam; /* 24M, 36M */
	uint8_t tx_pwr_ofdm_48m;
	uint8_t tx_pwr_ofdm_54m;

	uint8_t tx_pwr_ht20_bpsk; /* MCS0*/
	uint8_t tx_pwr_ht20_qpsk; /* MCS1, MCS2*/
	uint8_t tx_pwr_ht20_16qam; /* MCS3, MCS4*/
	uint8_t tx_pwr_ht20_mcs5; /* MCS5*/
	uint8_t tx_pwr_ht20_mcs6; /* MCS6*/
	uint8_t tx_pwr_ht20_mcs7; /* MCS7*/

	uint8_t tx_pwr_ht40_bpsk; /* MCS0*/
	uint8_t tx_pwr_ht40_qpsk; /* MCS1, MCS2*/
	uint8_t tx_pwr_ht40_16qam; /* MCS3, MCS4*/
	uint8_t tx_pwr_ht40_mcs5; /* MCS5*/
	uint8_t tx_pwr_ht40_mcs6; /* MCS6*/
	uint8_t tx_pwr_ht40_mcs7; /* MCS7*/
	uint8_t tx_pwr_ht40_mcs32; /* MCS32*/

	uint8_t tx_pwr_vht20_bpsk; /* MCS0*/
	uint8_t tx_pwr_vht20_qpsk; /* MCS1, MCS2*/
	uint8_t tx_pwr_vht20_16qam; /* MCS3, MCS4*/
	uint8_t tx_pwr_vht20_64qam; /* MCS5, MCS6*/
	uint8_t tx_pwr_vht20_mcs7;
	uint8_t tx_pwr_vht20_mcs8;
	uint8_t tx_pwr_vht20_mcs9;

	uint8_t tx_pwr_vht_40;
	uint8_t tx_pwr_vht_80;
	uint8_t tx_pwr_vht_160c;
	uint8_t tx_pwr_vht_160nc;
	uint8_t tx_pwr_lg_40;
	uint8_t tx_pwr_lg_80;

	uint8_t tx_pwr_1ss_delta;
	uint8_t ucReserved_1[2];
};
struct CMD_SET_COUNTRY_CHANNEL_POWER_LIMIT_V2 {
	uint8_t ucNum;
	uint8_t eband; /*ENUM_BAND_T*/
	uint8_t usReserved[2];
	uint32_t countryCode;
	struct CMD_CHANNEL_POWER_LIMIT_V2 rChannelPowerLimit[0];
};
_Static_assert(sizeof(struct CMD_SET_DOMAIN_INFO)==56,"legacy domain body");
_Static_assert(sizeof(struct CMD_CHANNEL_POWER_LIMIT_V2)==40,"power V2 channel");
_Static_assert(sizeof(struct CMD_SET_COUNTRY_CHANNEL_POWER_LIMIT_V2)==8,"power V2 prefix");
