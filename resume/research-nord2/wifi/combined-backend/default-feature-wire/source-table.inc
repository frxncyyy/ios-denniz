struct PARAM_CUSTOM_KEY_CFG_STRUCT g_rDefaulteSetting[] = {
	/*format :
	*: {
	*	"firmware config parameter",
	*	"firmware config value",
	*	"Operation:default 0"
	*   }
	*/
#ifdef OPLUS_WLAN_BUG_STABILITY
	//CONNECTIVITY.WIFI.CONNECTION.DISCONNECT,2002137, 2019/05/11,
	//Add for add beacon to 10+10 before disconnect to avoid multiple disconnect
	{"ScreenOnBeaconTimeoutCount", "10"},
	//Add for check if any data at the last 2s,if has then not disconnect
	{"BeaconTimoutFilterDurationMs","2000"},
#endif /* OPLUS_WLAN_BUG_STABILITY */

	{"AdapScan", "0x0", WLAN_CFG_DEFAULT},
#if CFG_SUPPORT_IOT_AP_BLACKLIST
	/*Fill Iot AP blacklist here*/
#endif
#if CFG_TC3_FEATURE
	{"ScreenOnBeaconTimeoutCount", "20"},
	{"ScreenOffBeaconTimeoutCount", "10"},
	{"AgingPeriod", "0x19"},
	{"DropPacketsIPV4Low", "0x1"},
	{"DropPacketsIPV6Low", "0x1"},
	{"Sta2gBw", "1"},
#endif
#ifdef OPLUS_WLAN_BUG_STABILITY
	//CONNECTIVITY.WIFI.BASIC.POWER.917292, 2017/02/17,
	//Add for: filter SSDP packets
	{"DropPacketsIPV4Low", "0x12DE"},
	{"DropPacketsIPV4High", "0x0"},

	//Add for: filter IPV6 multicast packets
	{"DropPacketsIPV6Low", "0x2"},
	{"DropPacketsIPV6High", "0x0"},

	//Add for: 2.4G mask invalid issue
	{"2GTxMaskDPDOn", "1"},
	{"2GTxPMinus1dbAtHighTemp", "1"},
	{"TssiGroupBackupRestore", "1"},
#endif /* OPLUS_WLAN_BUG_STABILITY */

#ifdef OPLUS_WLAN_BUG_STABILITY
	//CONNECTIVITY.WIFI.BMISS, 2020/03/27,
	//Add for: adjust beacon miss report time as 3 seconds, 5*100ms is a round
	{"LdtBTONullHwLifeTime", "5"},
#endif /* OPLUS_WLAN_BUG_STABILITY */
};
