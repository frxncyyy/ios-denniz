void scnSendScanReqV2(IN struct ADAPTER *prAdapter)
{
	struct SCAN_INFO *prScanInfo;
	struct SCAN_PARAM *prScanParam;
	/* CMD_SCAN_REQ_V2 rCmdScanReq; */
	struct CMD_SCAN_REQ_V2 *prCmdScanReq;
	uint32_t i, j;
	uint8_t ucChannel;
	uint8_t ucBand;
	struct CHANNEL_INFO *prCnlInfo;

	ASSERT(prAdapter);

	prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
	prScanParam = &(prScanInfo->rScanParam);

	prCmdScanReq = kalMemAlloc(
		sizeof(struct CMD_SCAN_REQ_V2), VIR_MEM_TYPE);
	if (!prCmdScanReq) {
		log_dbg(SCN, ERROR, "alloc CmdScanReq V2 fail\n");
		return;
	}
	/* send command packet for scan */
	kalMemZero(prCmdScanReq, sizeof(struct CMD_SCAN_REQ_V2));
	/* Modify channelList number from 32 to 54 */
	if (prScanParam->ucScnFuncMask & ENUM_SCN_USE_PADDING_AS_BSSID) {
		kalMemCopy(prCmdScanReq->aucExtBSSID,
			&prScanParam->aucBSSID[0][0],
			CFG_SCAN_OOB_MAX_NUM * MAC_ADDR_LEN);
	} else {
		COPY_MAC_ADDR(prCmdScanReq->aucBSSID,
		&prScanParam->aucBSSID[0][0]);
	}
	if (!EQUAL_MAC_ADDR(prCmdScanReq->aucBSSID, "\x00\x00\x00\x00\x00\x00"))
		DBGLOG(SCN, INFO, "Include BSSID "MACSTR" in probe request\n",
			MAC2STR(prCmdScanReq->aucBSSID));

	prCmdScanReq->ucSeqNum = prScanParam->ucSeqNum;
	prCmdScanReq->ucBssIndex = prScanParam->ucBssIndex;
	prCmdScanReq->ucScanType = (uint8_t) prScanParam->eScanType;
	prCmdScanReq->ucSSIDType = prScanParam->ucSSIDType;
	prCmdScanReq->auVersion[0] = 1;
	prCmdScanReq->ucScnFuncMask |= prScanParam->ucScnFuncMask;
	/* for 6G OOB scan */
	kalMemCopy(prCmdScanReq->ucBssidMatchCh, prScanParam->ucBssidMatchCh,
			CFG_SCAN_OOB_MAX_NUM);
	kalMemCopy(prCmdScanReq->ucBssidMatchSsidInd,
		prScanParam->ucBssidMatchSsidInd, CFG_SCAN_OOB_MAX_NUM);

	if (kalIsValidMacAddr(prScanParam->aucRandomMac)) {
		prCmdScanReq->ucScnFuncMask |= (ENUM_SCN_RANDOM_MAC_EN |
						ENUM_SCN_RANDOM_SN_EN);
		kalMemCopy(prCmdScanReq->aucRandomMac,
			prScanParam->aucRandomMac, MAC_ADDR_LEN);
	}
	if (prAdapter->rWifiVar.eDbdcMode == ENUM_DBDC_MODE_DISABLED
#if (CFG_SUPPORT_POWER_THROTTLING == 1 && CFG_SUPPORT_CNM_POWER_CTRL == 1)
		|| prAdapter->fgPowerForceOneNss
#endif
		)
		prCmdScanReq->ucScnFuncMask |= ENUM_SCN_DBDC_SCAN_DIS;

	/* Set SSID to scan request */
	if (prScanParam->ucSSIDNum <= SCAN_CMD_SSID_NUM) {
		prCmdScanReq->ucSSIDNum = prScanParam->ucSSIDNum;
		prCmdScanReq->ucSSIDExtNum = 0;
	} else if (prScanParam->ucSSIDNum <= CFG_SCAN_SSID_MAX_NUM) {
		prCmdScanReq->ucSSIDNum = SCAN_CMD_SSID_NUM;
		prCmdScanReq->ucSSIDExtNum = prScanParam->ucSSIDNum
			- SCAN_CMD_SSID_NUM;
	} else {
		log_dbg(SCN, WARN, "Too many SSID %u\n",
					prScanParam->ucSSIDNum);
		prCmdScanReq->ucSSIDNum = SCAN_CMD_SSID_NUM;
		prCmdScanReq->ucSSIDExtNum = SCAN_CMD_EXT_SSID_NUM;
	}

	for (i = 0; i < prCmdScanReq->ucSSIDNum; i++) {
		COPY_SSID(prCmdScanReq->arSSID[i].aucSsid,
			prCmdScanReq->arSSID[i].u4SsidLen,
			prScanParam->aucSpecifiedSSID[i],
			prScanParam->ucSpecifiedSSIDLen[i]);
		log_dbg(SCN, TRACE,
			"Ssid=%s, SsidLen=%d\n",
			HIDE(prCmdScanReq->arSSID[i].aucSsid),
			prCmdScanReq->arSSID[i].u4SsidLen);
	}
	for (i = 0; i < prCmdScanReq->ucSSIDExtNum; i++) {
		COPY_SSID(prCmdScanReq->arSSIDExtend[i].aucSsid,
			prCmdScanReq->arSSIDExtend[i].u4SsidLen,
			prScanParam->aucSpecifiedSSID
			[prCmdScanReq->ucSSIDNum+i],
			prScanParam->ucSpecifiedSSIDLen
			[prCmdScanReq->ucSSIDNum+i]);
		log_dbg(SCN, TRACE,
			"Ssid=%s, SsidLen=%d\n",
			prCmdScanReq->arSSIDExtend[i].aucSsid,
			prCmdScanReq->arSSIDExtend[i].u4SsidLen);
	}

	prCmdScanReq->u2ProbeDelayTime
		= (uint8_t) prScanParam->u2ProbeDelayTime;
	prCmdScanReq->ucChannelType
		= (uint8_t) prScanParam->eScanChannel;

	/* Set channel info to scan request */
	if (prScanParam->eScanChannel == SCAN_CHANNEL_SPECIFIED) {
		for (i = 0, j = 0; i < prScanParam->ucChannelListNum; i++) {
			ucBand = (uint8_t) prScanParam->arChnlInfoList[i].eBand;
			ucChannel = prScanParam->arChnlInfoList[i].ucChannelNum;
			/* remove DFS channel */
			if (prAdapter->rWifiVar.rScanInfo.fgSkipDFS &&
				rlmDomainIsDfsChnls(prAdapter, ucChannel))
				continue;

			if (j < SCAN_CMD_CHNL_NUM) {
				prCnlInfo = &(prCmdScanReq->arChannelList[j]);
				prCnlInfo->ucBand = ucBand;
				prCnlInfo->ucChannelNum = ucChannel;
				prCmdScanReq->ucChannelListNum = ++j;
			} else if (j < MAXIMUM_OPERATION_CHANNEL_LIST) {
				prCnlInfo = &(prCmdScanReq->
				   arChannelListExtend[j - SCAN_CMD_CHNL_NUM]);
				prCnlInfo->ucBand = ucBand;
				prCnlInfo->ucChannelNum = ucChannel;
				prCmdScanReq->ucChannelListExtNum =
					(++j - SCAN_CMD_CHNL_NUM);
			} else {
				log_dbg(SCN, WARN, "Too many Channel %u\n",
					prScanParam->ucChannelListNum);
				prCmdScanReq->ucChannelListNum = 0;
				prCmdScanReq->ucChannelListExtNum = 0;
				prCmdScanReq->ucChannelType = SCAN_CHANNEL_FULL;
				break;
			}
		}
	}

	prCmdScanReq->u2ChannelDwellTime = prScanParam->u2ChannelDwellTime;
	prCmdScanReq->u2ChannelMinDwellTime =
		prScanParam->u2ChannelMinDwellTime;
	prCmdScanReq->u2TimeoutValue = prScanParam->u2TimeoutValue;

	/* If ProbeDelayTime bigger than MinDwellTime,
	 * reset ProbeDelayTime to 0
	 */
	if (prCmdScanReq->u2ProbeDelayTime >
	    prCmdScanReq->u2ChannelMinDwellTime)
		prCmdScanReq->u2ProbeDelayTime = 0;

	/* OCE certification handling */
	if (prAdapter->rWifiVar.u4SwTestMode == ENUM_SW_TEST_MODE_SIGMA_OCE) {
		scanHandleOceIE(prScanParam, prCmdScanReq);
		prCmdScanReq->ucScnFuncMask |= ENUM_SCN_OCE_SCAN_EN;
	}

	/* enable split scan when (not in roam) && (WFD || 1s TRX pkt > 30) */
	if (scnEnableSplitScan(prAdapter, prScanParam->ucBssIndex)) {
		prCmdScanReq->ucScnFuncMask |= ENUM_SCN_SPLIT_SCAN_EN;
		/* if WFD enable, not do dbdc scan and reduce dwell time to
		 * enhance latency
		 */
		if (wlanWfdEnabled(prAdapter)) {
			prCmdScanReq->ucScnFuncMask |= ENUM_SCN_DBDC_SCAN_DIS;
			prCmdScanReq->u2ChannelDwellTime =
				SCAN_CHANNEL_DWELL_TIME_MIN_MSEC;
			prCmdScanReq->u2ChannelMinDwellTime =
				SCAN_CHANNEL_DWELL_TIME_MIN_MSEC;
		}
	}

	if (prScanParam->u2IELen <= MAX_IE_LENGTH)
		prCmdScanReq->u2IELen = prScanParam->u2IELen;
	else
		prCmdScanReq->u2IELen = MAX_IE_LENGTH;

	if (prScanParam->u2IELen)
		kalMemCopy(prCmdScanReq->aucIE, prScanParam->aucIE,
			sizeof(uint8_t) * prCmdScanReq->u2IELen);

	log_dbg(SCN, TRACE, "ScanReqV2: ScanType=%d,BSS=%u,SSIDType=%d,Num=%u,Ext=%u,ChannelType=%d,Num=%d,Ext=%u,Seq=%u,Ver=%u,Dw=%u,Min=%u,Func=0x%X,Mac="
		MACSTR ",BSSID:"MACSTR"\n",
		prCmdScanReq->ucScanType,
		prCmdScanReq->ucBssIndex,
		prCmdScanReq->ucSSIDType,
		prCmdScanReq->ucSSIDNum,
		prCmdScanReq->ucSSIDExtNum,
		prCmdScanReq->ucChannelType,
		prCmdScanReq->ucChannelListNum,
		prCmdScanReq->ucChannelListExtNum,
		prCmdScanReq->ucSeqNum, prCmdScanReq->auVersion[0],
		prCmdScanReq->u2ChannelDwellTime,
		prCmdScanReq->u2ChannelMinDwellTime,
		prCmdScanReq->ucScnFuncMask,
		MAC2STR(prCmdScanReq->aucRandomMac),
		MAC2STR(prCmdScanReq->aucBSSID));

	scanLogCacheFlushAll(prAdapter, &(prScanInfo->rScanLogCache),
		LOG_SCAN_REQ_D2F);
	scanReqLog(prCmdScanReq);
	if (prCmdScanReq->ucBssIndex == KAL_NETWORK_TYPE_AIS_INDEX)
		scanInitEssResult(prAdapter);

	wlanSendSetQueryCmd(prAdapter,
		CMD_ID_SCAN_REQ_V2,
		TRUE,
		FALSE,
		FALSE,
		NULL,
		NULL,
		sizeof(struct CMD_SCAN_REQ_V2),
		(uint8_t *)prCmdScanReq, NULL, 0);
	log_dbg(SCN, TRACE, "Send %zu bytes\n", sizeof(struct CMD_SCAN_REQ_V2));

	GET_CURRENT_SYSTIME(&prScanInfo->rLastScanStartTime);

	kalMemFree(prCmdScanReq, VIR_MEM_TYPE, sizeof(struct CMD_SCAN_REQ_V2));

}

