struct CMD_BSS_ACTIVATE_CTRL {
	uint8_t ucBssIndex;
	uint8_t ucActive;
	uint8_t ucNetworkType;
	uint8_t ucOwnMacAddrIndex;
	uint8_t aucBssMacAddr[6];
	uint8_t ucBMCWlanIndex;
	uint8_t ucReserved;
};

