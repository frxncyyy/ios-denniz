/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PRIVATE_NORD2_REGULATORY_EVALUATOR_H
#define PRIVATE_NORD2_REGULATORY_EVALUATOR_H
#include "../regulatory-policy/regulatory.h"
#define NRE_CHANNELS 39u
#define NRE_MAX_RULES 7u
/* These are observed source branches, never a country or RF authorization. */
enum nre_origin { NRE_RESET_WORLD=1, NRE_TAG3_EFUSE=2 };
struct nre_channel {
 uint16_t channel,frequency_mhz;
 uint8_t band,rule_index,allowed20,passive20;
 uint32_t flags,hardware_flags,rule_flags,max_bandwidth_khz;
 uint32_t antenna_gain_mbi,max_eirp_mbm,dfs_cac_ms;
};
struct nre_result {
 uint32_t country,table_key;
 unsigned origin,local_match,world_fallback,dfs_region,rule_count;
 unsigned channels,allowed_count,passive_count;
 unsigned tx_authorized,power_path_resolved; /* Always zero in this pure stage. */
 uint8_t allowed[NRE_CHANNELS],passive[NRE_CHANNELS]; /* indices into channel[] */
 struct nre_channel channel[NRE_CHANNELS];
};
/* Caller owns actual retained TAG3/PHY and authenticated full-board metadata.
 * No caller boolean authenticates provenance. Board country does not override
 * TAG3 efuse/default country. Only the selected initial local-DB source branch
 * is modeled; later user/driver/country-IE state transitions are not invented.
 * All 39 source hardware channels are evaluated, never a handpicked subset.
 * Failure is atomic; success is an observation, not a command or RF lease. */
int nre_evaluate(const struct nrp_caps *,const struct nrp_board *,struct nre_result *);
#endif
