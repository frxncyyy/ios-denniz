#include <stdint.h>
#include <stddef.h>
struct CMD_CSUM_OFFLOAD {
	uint16_t u2RxChecksum;	/* bit0: IP, bit1: UDP, bit2: TCP */
	uint16_t u2TxChecksum;	/* bit0: IP, bit1: UDP, bit2: TCP */
};
struct CMD_BASIC_CONFIG {
	uint8_t ucNative80211;
	uint8_t ucCtrlFlagAssertPath;
	uint8_t ucCtrlFlagDebugLevel;
	uint8_t aucReserved[1];
	struct CMD_CSUM_OFFLOAD rCsumOffload;
	uint8_t	ucCrlFlagSegememt;
	uint8_t	aucReserved2[3];
};
struct CMD_NVRAM_FRAGMENT {
	/*restrict NVRAM TX CMD size to 1500 bytes*/
	/*because FW WFDMA MAX buf size is 1600 Byte*/
	uint8_t aucReverse[1500];
};
_Static_assert(sizeof(struct CMD_BASIC_CONFIG)==12,"basic size");
_Static_assert(offsetof(struct CMD_BASIC_CONFIG,ucNative80211)==0,"native");
_Static_assert(offsetof(struct CMD_BASIC_CONFIG,ucCtrlFlagAssertPath)==1,"assert path");
_Static_assert(offsetof(struct CMD_BASIC_CONFIG,ucCtrlFlagDebugLevel)==2,"assert level");
_Static_assert(offsetof(struct CMD_BASIC_CONFIG,rCsumOffload)==4,"csum");
_Static_assert(offsetof(struct CMD_BASIC_CONFIG,ucCrlFlagSegememt)==8,"segment");
_Static_assert(sizeof(struct CMD_NVRAM_FRAGMENT)==1500,"fragment size");
