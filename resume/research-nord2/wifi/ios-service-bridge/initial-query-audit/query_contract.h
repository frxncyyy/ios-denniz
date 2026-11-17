#ifndef PRIVATE_IOS_WIFI_QUERY_CONTRACT_H
#define PRIVATE_IOS_WIFI_QUERY_CONTRACT_H
#include <stdint.h>
#include <stddef.h>
#define NIWQ_CAP_BYTES 13u
#define NIWQ_CAP_ENUMS 103u
struct niwq_caps {uint8_t raw[13],set[103];unsigned count,unconsumed_bit103;};
/* Actual copied iOS capability bytes only, not a mapping from MTK features. */
int niwq_caps_decode(const uint8_t *,size_t,struct niwq_caps *);
struct niwq_power {uint32_t version,count,state[4];unsigned any_state_one,unknown_states;};
int niwq_power_decode(const uint8_t *,size_t,struct niwq_power *);
/* Source async result-cache end observations; no caller may use these to turn
 * an incomplete/failed backend scan into an empty successful snapshot. */
enum niwq_result_status {NIWQ_ROW=1,NIWQ_CACHE_END=2,NIWQ_BUSY=3,NIWQ_ERROR=4};
enum niwq_result_status niwq_result_status(int,uint32_t);
#endif
