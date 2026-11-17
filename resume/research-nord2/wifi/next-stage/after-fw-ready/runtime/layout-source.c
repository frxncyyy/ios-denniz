#include <stdint.h>
#include <stddef.h>
struct WIFI_EVENT {
	uint16_t u2PacketLength;
	uint16_t u2PacketType;	/* Must be filled with 0xE000 (EVENT Packet) */
	uint8_t ucEID;
	uint8_t ucSeqNum;
	uint8_t ucEventVersion;
	uint8_t aucReserved[1];

	uint8_t ucExtenEID;
	uint8_t aucReserved2[2];
	uint8_t ucS2DIndex;

	uint8_t aucBuffer[0];
};
struct EVENT_DEBUG_MSG {
	uint16_t u2DebugMsgId;
	uint8_t ucMsgType;
	uint8_t ucFlags;		/* unused */
	uint32_t u4Value;	/* memory addre or ... */
	uint16_t u2MsgSize;
	uint8_t aucReserved0[2];
	uint8_t aucMsg[1];
};
struct EVENT_NIC_CAPABILITY_V2 {
	uint16_t u2TotalElementNum;
	uint8_t aucReserved[2];
	uint8_t aucBuffer[0];
};
struct NIC_CAPABILITY_V2_ELEMENT {
	uint32_t tag_type; /* NIC_CAPABILITY_V2_TAG_T */
	uint32_t body_len;
	uint8_t aucbody[0];
};
_Static_assert(offsetof(struct WIFI_EVENT,aucBuffer)==12,"normal event header");
_Static_assert(offsetof(struct EVENT_DEBUG_MSG,aucMsg)==12,"debug body prefix");
_Static_assert(offsetof(struct EVENT_DEBUG_MSG,u2MsgSize)==8,"debug length field");
_Static_assert(offsetof(struct EVENT_NIC_CAPABILITY_V2,aucBuffer)==4,"capability prefix");
_Static_assert(offsetof(struct NIC_CAPABILITY_V2_ELEMENT,aucbody)==8,"TLV prefix");
