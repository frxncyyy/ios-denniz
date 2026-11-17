/* Exact pinned original reference struct and original-cache RSN suites. */
struct apple80211_assoc {
	uint32_t version;
	uint16_t mode, auth_lower, auth_upper, unused_0;
	uint32_t ssid_length;
	uint8_t ssid[32];
	uint8_t bssid[6], unused_1[2];
	uint32_t key_version, key_length, key_cipher;   /* cipher 10: a passphrase as typed; 6: the key (from 64 hex digits) */
	uint16_t key_flags, key_index;
	uint8_t key[64];
	uint8_t rest[0x250 - 0x88];
};
static const unsigned char oracle_rsn[22]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
