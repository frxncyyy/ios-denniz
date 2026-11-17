#ifndef WIFI_8021X_GLUE_H
#define WIFI_8021X_GLUE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* 0450: interface of the 802.1X glue for the supplicant user client
 * (iokit/Kernel/arm/Nord2WifiSupplicant.cpp, 0451). Any thread. */
/* Key of APPLE80211_IOC_CIPHER_KEY from eapolclient after EAP success: 64
 * bytes MSK (the PMK is its first half) or 32 bytes PMK. 0 taken, 1 no
 * 802.1X association waits for a key, -1 invalid. */
int nord2_wifi_8021x_key(const uint8_t*,size_t);
/* Apple80211 GET answers about the 802.1X association (from the moment the
 * link is up for EAPOL): request 1 SSID (payload 1..32), 9 BSSID (6),
 * 2 AUTH_TYPE (16: version 1, lower 1 = open, upper 4 = WPA2 802.1X or 0x80 =
 * SHA-256 802.1X), 4 CHANNEL (16), 13 STATE (raw scalar, payload 0).
 * 0 encoded, 1 no such association now, -1 shape, -2 unsupported request. */
int nord2_wifi_8021x_get(unsigned request,size_t payload_bytes,void*out,size_t capacity,size_t*used);
extern unsigned niwj_8021x;
#ifdef __cplusplus
}
#endif
#endif
