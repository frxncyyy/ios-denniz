#ifndef STATION_WIRE2_H
#define STATION_WIRE2_H
#include "wifi_station_wire.h"
/* 0433: frame builders for the negotiated profiles, in a new object. The CI
 * codec wifi_station_wire.o changes only in its management TXD check (it now
 * admits the SAE authentication frames built here); every structure of
 * wifi_station_wire.h keeps its layout. Same rules: pure, disjoint buffers,
 * on failure used=0 and the output is unchanged. */
#define SW_SAE_BODY_MAX 360u
/* SAE authentication frame (algorithm 3): transaction 1 commit with status 0
 * or 126 (hash-to-element), transaction 2 confirm with status 0; body is the
 * SAE payload after the three fixed fields (34..360 octets). */
int sw_auth_sae(uint8_t *, size_t, size_t *, const struct sw_peer *, unsigned transaction,
                unsigned status, const uint8_t *body, size_t);
/* sw_assoc with the negotiated station RSN element (22 or 28 octets) and an
 * optional RSNXE (0 or 3 octets) in place of the fixed WPA2-PSK element. */
int sw_assoc_rsn(uint8_t *, size_t, size_t *, const struct sw_assoc *, const uint8_t *rsn, size_t,
                 const uint8_t *rsnxe, size_t);
#endif
