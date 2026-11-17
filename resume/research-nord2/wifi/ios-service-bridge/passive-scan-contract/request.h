#ifndef PRIVATE_NIW_PASSIVE_REQUEST_H
#define PRIVATE_NIW_PASSIVE_REQUEST_H
#include <stdint.h>
#include <stddef.h>
#define NIWP_BYTES 5456u
#define NIWP_CHANNELS 400u
struct niwp_channel { uint32_t number, flags; };
struct niwp_request {
 uint32_t bss_type,scan_type,phy_mode,rest_time,channel_count;
 uint16_t dwell_time; uint64_t flags;int32_t rssi_threshold;
 uint8_t bssid[6],ssid[32];uint32_t ssid_bytes;
 struct niwp_channel channels[NIWP_CHANNELS];
};
/* Copied request only. Does not identify passive numeric scan type, authorize
 * channels, ignore unsupported options, or manufacture readiness. Atomic out. */
int niwp_request_decode(const uint8_t *,size_t,struct niwp_request *);
#endif
