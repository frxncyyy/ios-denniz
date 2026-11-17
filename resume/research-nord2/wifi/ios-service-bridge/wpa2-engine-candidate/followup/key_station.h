#ifndef NWPK_STATION_H
#define NWPK_STATION_H
#include <stdint.h>
#include <stddef.h>
/* Source-equivalent station predicate only; no GTK ACK and no installation API.
 * The native retained owner supplies current generation and validated RX bytes.
 * Generation is LOCAL lifetime bookkeeping, not a field recovered from EID24.
 * This helper cannot distinguish a delayed old event after same BSS/peer reuse. */
struct nwpk_station {uint64_t generation;uint8_t bss,peer[6],valid,protected_bss,wep,tx_key_ready;};
int nwpk_event24(struct nwpk_station*,uint64_t rx_owner_generation,const uint8_t*,size_t);
int nwpk_tx_allowed(const struct nwpk_station*);
#endif
