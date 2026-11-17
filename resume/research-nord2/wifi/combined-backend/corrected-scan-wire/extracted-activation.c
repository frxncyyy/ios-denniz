uint32_t nicActivateNetworkEx(IN struct ADAPTER *prAdapter,
			    IN uint8_t ucBssIndex,
			    IN uint8_t fgReset40mBw)
{
	struct CMD_BSS_ACTIVATE_CTRL rCmdActivateCtrl;
	struct BSS_INFO *prBssInfo;
	/*	const UINT_8 aucZeroMacAddr[] = NULL_MAC_ADDR; */

	ASSERT(prAdapter);
	ASSERT(ucBssIndex <= prAdapter->ucHwBssIdNum);

	prBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter, ucBssIndex);

	if (fgReset40mBw) {
		prBssInfo->fg40mBwAllowed = FALSE;
		prBssInfo->fgAssoc40mBwAllowed = FALSE;
	}

	rCmdActivateCtrl.ucBssIndex = ucBssIndex;
	rCmdActivateCtrl.ucActive = 1;
	rCmdActivateCtrl.ucNetworkType = (uint8_t)
					 prBssInfo->eNetworkType;
	rCmdActivateCtrl.ucOwnMacAddrIndex =
		prBssInfo->ucOwnMacIndex;
	COPY_MAC_ADDR(rCmdActivateCtrl.aucBssMacAddr,
		      prBssInfo->aucOwnMacAddr);

	prBssInfo->ucBMCWlanIndex =
		secPrivacySeekForBcEntry(prAdapter, prBssInfo->ucBssIndex,
					 prBssInfo->aucOwnMacAddr,
					 STA_REC_INDEX_NOT_FOUND,
					 CIPHER_SUITE_NONE, 0xFF);
#if CFG_SUPPORT_LIMITED_PKT_PID
	nicTxInitPktPID(prAdapter, prBssInfo->ucBMCWlanIndex);
#endif /* CFG_SUPPORT_LIMITED_PKT_PID */
	rCmdActivateCtrl.ucBMCWlanIndex = prBssInfo->ucBMCWlanIndex;

	kalMemZero(&rCmdActivateCtrl.ucReserved,
		   sizeof(rCmdActivateCtrl.ucReserved));

#if 1
	DBGLOG(RSN, INFO,
	       "[BSS index]=%d OwnMac%d=" MACSTR " BSSID=" MACSTR
	       " BMCIndex = %d NetType=%d\n",
	       ucBssIndex,
	       prBssInfo->ucOwnMacIndex,
	       MAC2STR(prBssInfo->aucOwnMacAddr),
	       MAC2STR(prBssInfo->aucBSSID),
	       prBssInfo->ucBMCWlanIndex, prBssInfo->eNetworkType);
#endif

	return wlanSendSetQueryCmd(prAdapter,
				   CMD_ID_BSS_ACTIVATE_CTRL,
				   TRUE,
				   FALSE,
				   FALSE,
				   NULL, NULL,
				   sizeof(struct CMD_BSS_ACTIVATE_CTRL),
				   (uint8_t *)&rCmdActivateCtrl, NULL, 0);
}

