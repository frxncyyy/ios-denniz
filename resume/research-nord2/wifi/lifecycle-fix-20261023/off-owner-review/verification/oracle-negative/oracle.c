#include <assert.h>
#include "wifi_station_transport.h"
enum ENUM_CMD_ID {
	CMD_ID_DUMMY_RSV            = 0x00, /* 0x00 (Set) */
	CMD_ID_TEST_CTRL            = 0x01, /* 0x01 (Set) */
	CMD_ID_BASIC_CONFIG,                /* 0x02 (Set) */
	CMD_ID_SCAN_REQ_V2,                 /* 0x03 (Set) */
	CMD_ID_NIC_POWER_CTRL,              /* 0x04 (Set) */
	CMD_ID_POWER_SAVE_MODE,             /* 0x05 (Set) */
	CMD_ID_LINK_ATTRIB,                 /* 0x06 (Set) */
	CMD_ID_ADD_REMOVE_KEY,              /* 0x07 (Set) */
	CMD_ID_DEFAULT_KEY_ID,              /* 0x08 (Set) */
	CMD_ID_INFRASTRUCTURE,              /* 0x09 (Set) */
	CMD_ID_SET_RX_FILTER,               /* 0x0a (Set) */
	CMD_ID_DOWNLOAD_BUF,                /* 0x0b (Set) */
	CMD_ID_WIFI_START,                  /* 0x0c (Set) */
	CMD_ID_CMD_BT_OVER_WIFI,            /* 0x0d (Set) */
	CMD_ID_SET_MEDIA_CHANGE_DELAY_TIME, /* 0x0e (Set) */
	CMD_ID_SET_DOMAIN_INFO,             /* 0x0f (Set) */
	CMD_ID_SET_IP_ADDRESS,              /* 0x10 (Set) */
	CMD_ID_BSS_ACTIVATE_CTRL,           /* 0x11 (Set) */
	CMD_ID_SET_BSS_INFO,                /* 0x12 (Set) */
	CMD_ID_UPDATE_STA_RECORD,           /* 0x13 (Set) */
	CMD_ID_REMOVE_STA_RECORD,           /* 0x14 (Set) */
	CMD_ID_INDICATE_PM_BSS_CREATED,     /* 0x15 (Set) */
	CMD_ID_INDICATE_PM_BSS_CONNECTED,   /* 0x16 (Set) */
	CMD_ID_INDICATE_PM_BSS_ABORT,       /* 0x17 (Set) */
	CMD_ID_UPDATE_BEACON_CONTENT,       /* 0x18 (Set) */
	CMD_ID_SET_BSS_RLM_PARAM,           /* 0x19 (Set) */
	CMD_ID_SCAN_REQ,                    /* 0x1a (Set) */
	CMD_ID_SCAN_CANCEL,
};
struct CMD_SCAN_CANCEL {
	uint8_t          ucSeqNum;
	uint8_t          ucIsExtChannel;     /* For P2P channel extension. */
	uint8_t          aucReserved[2];
};
static int cid_size(uint8_t cid,size_t n){switch(cid){case 3:return n==1188;case 4:return n==4;case 7:return n==64;case 0x0a:return n==68;case 0x11:return n==12;case 0x12:return n==116;case 0x13:return n==200;case 0x14:return n==4;case 0x16:return n==12;case 0x17:return n==4;case 0x19:return n==22;case 0x1c:return n==24;default:return 0;}}
_Static_assert(CMD_ID_SCAN_REQ_V2==3,"scan v2 ABI");
_Static_assert(CMD_ID_NIC_POWER_CTRL==4,"power ABI");
_Static_assert(CMD_ID_SCAN_CANCEL==0x1b,"cancel ABI");
_Static_assert(NST_CID_SCAN_CANCEL==CMD_ID_SCAN_CANCEL,"native cancellation must use source wire ID");
_Static_assert(sizeof(struct CMD_SCAN_CANCEL)==4,"cancel extent");
_Static_assert(offsetof(struct CMD_SCAN_CANCEL,ucSeqNum)==0,"sequence");
_Static_assert(offsetof(struct CMD_SCAN_CANCEL,ucIsExtChannel)==1,"extension");
_Static_assert(offsetof(struct CMD_SCAN_CANCEL,aucReserved)==2,"reserved");
int main(void){for(size_t n=0;n<65;n++){assert(cid_size(CMD_ID_SCAN_CANCEL,n)==(n==sizeof(struct CMD_SCAN_CANCEL)));assert(!cid_size(CMD_ID_NIC_POWER_CTRL,n));}return 0;}
