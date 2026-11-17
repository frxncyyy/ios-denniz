#ifndef WIFI_WPA3_H
#define WIFI_WPA3_H
#include "wifi_wpa2.h"
/* 0433: engine for the negotiated profiles beside the CI engine. It shares
 * wifi_wpa2.h's states, actions, failures, struct nwp_result and RNG type and
 * the same call sequence; struct nwq repeats struct nwp's members (same
 * names, same meaning) and appends the profile state. struct nwp itself and
 * every structure embedding it keep their CI layout. */
struct nwq {
 uint64_t generation,serial,pending,install_ticket;unsigned state,tx_kind,installed,ready_mask;
 uint8_t pmk[32],own[6],peer[6],snonce[32],anonce[32],kck[16],kek[16],tk[16],gtk[16],rsc[8];
 unsigned group_phase,group_bytes,gtk_history_count;uint64_t group_ticket;
 uint8_t rx_replay[8],group[NWP_FRAME],gtk_history[32][16];
 unsigned gtk_index;uint8_t rsn[22],ap_rsn[257],m1_replay[8];unsigned ap_rsn_bytes;
 uint8_t ap_rsnxe[257];unsigned ap_rsnxe_bytes;
 uint8_t m3[NWP_FRAME];unsigned m3_bytes;
 /* version is the EAPOL-Key descriptor version: 2 HMAC-SHA1/PRF-SHA1, 3 and 0 AES-128-CMAC/KDF-SHA256. */
 unsigned akm,pmf,version,pmk_ready,own_rsn_bytes,own_rsnxe_bytes;
 unsigned igtk_ready,igtk_index,igtk_fresh;
 uint8_t own_rsn[28],own_rsnxe[3],igtk[16],ipn[6];
};
#define NWP_AKM_PSK 2u
#define NWP_AKM_PSK_SHA256 6u
#define NWP_AKM_SAE 8u
#define NWP_ALLOW_LEGACY 1u /* WPA2-PSK without PMF: served by the CI engine, byte for byte as before */
#define NWP_ALLOW_PMF 2u    /* AKM 2 with PMF required by the AP, AKM 6 */
#define NWP_ALLOW_SAE 4u    /* AKM 8 */
/* pmf: management frame protection negotiated (BIP-CMAC-128, IGTK in M3);
 * pmf_required: the AP sets MFPR and so does the station element;
 * h2e: SAE hash-to-element, the station sends RSNXE in association and M2. */
struct nwp_profile {unsigned akm,pmf,pmf_required,h2e;};
/* Pure choice from a full AP RSN element and the complete AP element list
 * (RSNXE). Order: SAE when allowed and offered; else the legacy profile when
 * the AP offers AKM 2 without requiring PMF; else AKM 6; else AKM 2 with PMF.
 * 0 chosen, -1 nothing acceptable (output zero). */
int nwq_negotiate(const uint8_t*ap_rsn,size_t,const uint8_t*ap_ies,size_t,unsigned allow,struct nwp_profile*);
/* Station RSN element for a profile: 22 octets, or 28 with PMF (capabilities + BIP group management cipher). */
int nwq_station_rsn(const struct nwp_profile*,uint8_t out[28],size_t*used);
/* pmk may be null for SAE: every frame is then refused until nwq_set_pmk supplies the SAE PMK. */
int nwq_begin(struct nwq*,uint64_t,const uint8_t*pmk,const uint8_t own[6],const uint8_t peer[6],const struct nwp_profile*,const uint8_t*ap_rsn,size_t ap_bytes,nwp_rng,void*);
int nwq_set_pmk(struct nwq*,uint64_t generation,const uint8_t pmk[32]);
int nwq_copy_ap_rsnxe(struct nwq*,const uint8_t*,size_t);
int nwq_receive(struct nwq*,const uint8_t*,size_t,struct nwp_result*);
int nwq_security_submitted(struct nwq*,uint64_t generation,uint64_t ticket,int success);
int nwq_keys_ready(struct nwq*,uint64_t generation,uint64_t install_ticket,unsigned mask,int success);
int nwq_group_ready(struct nwq*,uint64_t generation,uint64_t ticket,int success,struct nwp_result*);
void nwq_cancel(struct nwq*);
#endif
