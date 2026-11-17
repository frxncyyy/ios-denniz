#include <stdint.h>
#include <stddef.h>
#define SCAN_DONE_EVENT_MAX_CHANNEL_NUM 64
struct CHANNEL_INFO {
	uint8_t  ucBand;
	uint8_t  ucChannelNum;
};
struct EVENT_SCAN_DONE {
	uint8_t          ucSeqNum;
	uint8_t          ucSparseChannelValid;
	struct CHANNEL_INFO  rSparseChannel;
	uint8_t          ucCompleteChanCount;
	uint8_t          ucCurrentState;
	uint8_t          ucScanDoneVersion;
	uint8_t          ucReserved;
	uint32_t         u4ScanDurBcnCnt;
	uint8_t          fgIsPNOenabled;
	uint8_t          aucReserving[3];
	/*channel idle count # Mike */
	uint8_t          ucSparseChannelArrayValidNum;
	uint8_t          aucReserved[3];
	uint8_t          aucChannelNum[SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
	/* Idle format for au2ChannelIdleTime */
	/* 0: first bytes: idle time(ms) 2nd byte: dwell time(ms) */
	/* 1: first bytes: idle time(8ms) 2nd byte: dwell time(8ms) */
	/* 2: dwell time (16us) */
	uint16_t         au2ChannelIdleTime[SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
	/* Beacon and Probe Response Count in each Channel  */
	uint8_t          aucChannelBAndPCnt[SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
	/* Mdrdy Count in each Channel  */
	uint8_t          aucChannelMDRDYCnt[SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
	uint32_t         u4ScanDurBcnCnt2G4;
	uint32_t         u4ScanDurBcnCnt5G;
	uint16_t         au2ChannelScanTime[SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
};
struct HW_MAC_RX_STS_GROUP_1 {
	uint8_t aucPN[16];
};
struct HW_MAC_RX_STS_GROUP_2 {
	uint32_t u4Timestamp;	/* DW 12 */
	uint32_t u4CRC;		/* DW 13 */
};
struct HW_MAC_RX_STS_GROUP_4 {
	/* For HDR_TRAN */
	uint16_t u2FrameCtl;	/* DW 4 */
	uint8_t aucTA[6];	/* DW 4~5 */
	uint16_t u2SeqFrag;	/* DW 6 */
	uint16_t u2Qos;		/* DW 6 */
	uint32_t u4HTC;		/* DW 7 */
};
struct HW_MAC_RX_STS_GROUP_3_V2 {
	/*  PRXVector Info */
	uint32_t u4RxVector[2];	/* FALCON: DW 16~17 */
};
struct HW_MAC_RX_STS_GROUP_5 {
	/*  CRXVector Info */
	/* FALCON: DW 18~33 for harrier E1,  DW 18~35 for harrier E2
	 * Other project: give group5_size in chip info,
	 * e.g Soc3_0.c
,
	 * or modify prChipInfo->group5_size when doing wlanCheckAsicCap,
	 * e.g. Harrier E1
	 */
	uint32_t u4RxVector[18];
};
_Static_assert(sizeof(struct CHANNEL_INFO)==2,"CHANNEL_INFO");
_Static_assert(sizeof(struct EVENT_SCAN_DONE)==476,"EVENT_SCAN_DONE");
_Static_assert(sizeof(struct HW_MAC_RX_STS_GROUP_1)==16,"HW_MAC_RX_STS_GROUP_1");
_Static_assert(sizeof(struct HW_MAC_RX_STS_GROUP_2)==8,"HW_MAC_RX_STS_GROUP_2");
_Static_assert(sizeof(struct HW_MAC_RX_STS_GROUP_4)==16,"HW_MAC_RX_STS_GROUP_4");
_Static_assert(sizeof(struct HW_MAC_RX_STS_GROUP_3_V2)==8,"HW_MAC_RX_STS_GROUP_3_V2");
_Static_assert(sizeof(struct HW_MAC_RX_STS_GROUP_5)==72,"HW_MAC_RX_STS_GROUP_5");
_Static_assert(offsetof(struct EVENT_SCAN_DONE,ucSeqNum)==0,"ucSeqNum");
_Static_assert(offsetof(struct EVENT_SCAN_DONE,ucCurrentState)==5,"ucCurrentState");
_Static_assert(offsetof(struct EVENT_SCAN_DONE,ucScanDoneVersion)==6,"ucScanDoneVersion");
_Static_assert(offsetof(struct EVENT_SCAN_DONE,u4ScanDurBcnCnt)==8,"u4ScanDurBcnCnt");
_Static_assert(offsetof(struct EVENT_SCAN_DONE,ucSparseChannelArrayValidNum)==16,"ucSparseChannelArrayValidNum");
_Static_assert(offsetof(struct EVENT_SCAN_DONE,aucChannelNum)==20,"aucChannelNum");
_Static_assert(offsetof(struct EVENT_SCAN_DONE,au2ChannelIdleTime)==84,"au2ChannelIdleTime");
_Static_assert(offsetof(struct EVENT_SCAN_DONE,aucChannelBAndPCnt)==212,"aucChannelBAndPCnt");
_Static_assert(offsetof(struct EVENT_SCAN_DONE,aucChannelMDRDYCnt)==276,"aucChannelMDRDYCnt");
_Static_assert(offsetof(struct EVENT_SCAN_DONE,u4ScanDurBcnCnt2G4)==340,"u4ScanDurBcnCnt2G4");
_Static_assert(offsetof(struct EVENT_SCAN_DONE,u4ScanDurBcnCnt5G)==344,"u4ScanDurBcnCnt5G");
_Static_assert(offsetof(struct EVENT_SCAN_DONE,au2ChannelScanTime)==348,"au2ChannelScanTime");
