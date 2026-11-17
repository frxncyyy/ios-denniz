void wlanFeatureToFw(IN struct ADAPTER *prAdapter, uint32_t u4Flag)
{

	struct WLAN_CFG_ENTRY *prWlanCfgEntry;
	uint32_t i;
	struct CMD_HEADER rCmdV1Header;
	uint32_t rStatus;
	struct CMD_FORMAT_V1 rCmd_v1;
	uint8_t  ucTimes = 0;



	rCmdV1Header.cmdType = CMD_TYPE_SET;
	rCmdV1Header.cmdVersion = CMD_VER_1;
	rCmdV1Header.cmdBufferLen = 0;
	rCmdV1Header.itemNum = 0;

	kalMemSet(rCmdV1Header.buffer, 0, MAX_CMD_BUFFER_LENGTH);
	kalMemSet(&rCmd_v1, 0, sizeof(struct CMD_FORMAT_V1));


	prWlanCfgEntry = NULL;

	for (i = 0; i < WLAN_CFG_ENTRY_NUM_MAX; i++) {

		prWlanCfgEntry = wlanCfgGetEntryByIndex(prAdapter, i, u4Flag);

		if (prWlanCfgEntry) {

			rCmd_v1.itemType = ITEM_TYPE_STR;


			/*send string format to firmware */
			rCmd_v1.itemStringLength = kalStrLen(
							prWlanCfgEntry->aucKey);
			if (rCmd_v1.itemStringLength > MAX_CMD_NAME_MAX_LENGTH)
				continue;
			kalMemZero(rCmd_v1.itemString, MAX_CMD_NAME_MAX_LENGTH);
			kalMemCopy(rCmd_v1.itemString, prWlanCfgEntry->aucKey,
				   rCmd_v1.itemStringLength);


			rCmd_v1.itemValueLength = kalStrLen(
						  prWlanCfgEntry->aucValue);
			if (rCmd_v1.itemValueLength > MAX_CMD_VALUE_MAX_LENGTH)
				continue;
			kalMemZero(rCmd_v1.itemValue, MAX_CMD_VALUE_MAX_LENGTH);
			kalMemCopy(rCmd_v1.itemValue, prWlanCfgEntry->aucValue,
				   rCmd_v1.itemValueLength);



			DBGLOG(INIT, TRACE,
			       "Send key word (%s) WITH (%s) to firmware\n",
			       rCmd_v1.itemString, rCmd_v1.itemValue);

			kalMemCopy(((struct CMD_FORMAT_V1 *)rCmdV1Header.buffer)
				   + ucTimes,
				   &rCmd_v1, sizeof(struct CMD_FORMAT_V1));


			ucTimes++;
			rCmdV1Header.cmdBufferLen +=
						sizeof(struct CMD_FORMAT_V1);
			rCmdV1Header.itemNum += ucTimes;

			if (ucTimes == MAX_CMD_ITEM_MAX) {
				/* Send to FW */
				rCmdV1Header.itemNum = ucTimes;

				rStatus = wlanSendSetQueryCmd(
						/* prAdapter */
						prAdapter,
						/* 0x70 */
						CMD_ID_GET_SET_CUSTOMER_CFG,
						/* fgSetQuery */
						TRUE,
						/* fgNeedResp */
						FALSE,
						/* fgIsOid */
						FALSE,
						/* pfCmdDoneHandler*/
						NULL,
						/* pfCmdTimeoutHandler */
						NULL,
						/* u4SetQueryInfoLen */
						sizeof(struct CMD_HEADER),
						/* pucInfoBuffer */
						(uint8_t *)&rCmdV1Header,
						/* pvSetQueryBuffer */
						NULL,
						/* u4SetQueryBufferLen */
						0);

				if (rStatus == WLAN_STATUS_FAILURE)
					DBGLOG(INIT, INFO,
					       "[Fail]kalIoctl wifiSefCFG fail 0x%x\n",
					       rStatus);

				DBGLOG(INIT, TRACE,
				       "kalIoctl wifiSefCFG num:%d\n", ucTimes);
				kalMemSet(rCmdV1Header.buffer, 0,
					  MAX_CMD_BUFFER_LENGTH);
				rCmdV1Header.cmdBufferLen = 0;
				ucTimes = 0;
			}


		} else {
			break;
		}
	}


	if (ucTimes != 0) {
		/* Send to FW */
		rCmdV1Header.itemNum = ucTimes;

		rStatus = wlanSendSetQueryCmd(
			  prAdapter,	/* prAdapter */
			  CMD_ID_GET_SET_CUSTOMER_CFG,	/* 0x70 */
			  TRUE,		/* fgSetQuery */
			  FALSE,	/* fgNeedResp */
			  FALSE,	/* fgIsOid */
			  NULL,		/* pfCmdDoneHandler*/
			  NULL,		/* pfCmdTimeoutHandler */
			  sizeof(struct CMD_HEADER),	/* u4SetQueryInfoLen */
			  (uint8_t *)&rCmdV1Header,/* pucInfoBuffer */
			  NULL,	/* pvSetQueryBuffer */
			  0);	/* u4SetQueryBufferLen */

		if (rStatus == WLAN_STATUS_FAILURE)
			DBGLOG(INIT, INFO,
			       "[Fail]kalIoctl wifiSefCFG fail 0x%x\n",
			       rStatus);

		DBGLOG(INIT, TRACE,
			"cmdV1Header.itemNum:%d, kalIoctl wifiSefCFG num:%d\n",
			rCmdV1Header.itemNum,
			ucTimes);
		kalMemSet(rCmdV1Header.buffer, 0, MAX_CMD_BUFFER_LENGTH);
		rCmdV1Header.cmdBufferLen = 0;
		ucTimes = 0;
	}

#if CFG_SUPPORT_SMART_GEAR
	/*Send Event, Notify Fwks*/
	#if CFG_SUPPORT_DATA_STALL
	if (prAdapter->rWifiVar.ucSGCfg == 0x00) {
		enum ENUM_VENDOR_DRIVER_EVENT eEvent = EVENT_SG_DISABLE;

		KAL_REPORT_ERROR_EVENT(prAdapter,
			eEvent, (uint16_t)sizeof(u_int8_t),
			0,
			TRUE);
	}
	#endif /* CFG_SUPPORT_DATA_STALL */
#endif

}
