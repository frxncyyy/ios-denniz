#ifndef WIFI_IOS_JOIN_H
#define WIFI_IOS_JOIN_H
#include <stddef.h>
#include <stdint.h>
#include "wifi_scan_bss.h"
#include "wifi_scan_service.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NIWJ_ASSOC_BYTES 592u
#define NIWJ_AP_RSN_BYTES 257u
#define NIWJ_WPA2_PSK_CCMP 1u
#define NIWJ_OPEN 2u
/* Copied kernel-only secret input; never a user pointer or authority receipt.
 * Framework supplies PMK32 for both ordinary passphrase and ready-PMK input.
 * OPEN carries no PMK or RSN; these output arrays stay zero without starting
 * WPA. Caller/backend explicitly clear all copies on every retirement. */
struct niwj_request {
 uint8_t ssid_length,ssid[32],has_target,target[6],pmk[32],station_rsn[22],association_reason;
 uint32_t security,native_flags;
};
enum niwj_refusal {NIWJ_OK=0,NIWJ_SHAPE=-1,NIWJ_PROFILE=-2,
 NIWJ_CREDENTIAL=-3,NIWJ_SECURITY=-4,NIWJ_OPTIONS=-5,NIWJ_TARGET=-6};
struct niwj_diagnostic {
 uint32_t mode,auth_lower,auth_upper,rsn_bytes;
 uint32_t reason,key_kind,key_bytes,native_flags,association_flags,association_reason;
 uint32_t cached_pmk_bytes,blacklist_count,scope_id,conditional_option,transition_disable,band_preference; /* scope_id is wire byte length, not a numeric ID. */
 uint32_t private_address_present,owner_address_valid,private_address_matches;
};
/* Secret output cleared before refusal; no full-record zero sweep. Disjoint
 * buffers required. Diagnostic contains numeric metadata only, never secrets. */
int niwj_decode(const void*,size_t,struct niwj_request*,struct niwj_diagnostic*);
/* actual_owner_mac must come from the retained native info accessor, not from
 * this user request. An address-bearing request may only confirm that exact
 * identity; changing station identity/private rotation remains unsupported.
 * COLOCATED_NETWORK_SCOPE_ID is bounded opaque caller metadata for this first
 * full association, not a channel, RF permission or scoped-roaming grant. */
int niwj_decode_identity(const void*,size_t,const uint8_t actual_owner_mac[6],
 struct niwj_request*,struct niwj_diagnostic*);
void niwj_clear(void*,size_t);
/* 0430/0436/0491 join policy, kern.n2wifi_privmac (boot-arg n2wpm), default 2:
 * 0 legacy decoder: exact WPA2 profile, a private address must equal the
 *   current station address;
 * 1 relaxed decoder, the requested private (locally administered) address
 *   becomes the station address, firmware told by a second BSS activate
 *   while the BSS is active. q10jia on hardware: the firmware keeps its old
 *   own address (authentication frames from the new one are never
 *   acknowledged), so this mode only remains for experiments;
 * 2 as 1, with the vendor driver's sequence: BSS deactivate, then activate
 *   with the new address (gen4m nicDeactivateNetwork/nicActivateNetwork, the
 *   only path by which that driver changes the own address). Default (0491);
 * 3 relaxed decoder, private address accepted and ignored (board address).
 *   This is the kill switch: no private address is used in modes 0 and 3.
 * 0491: a private address that fails before authentication is given up for
 * the board address (kern.n2wifi_privmac_state, wifi_station_service_bind.inc).
 * struct niwj_request/niwj_diagnostic keep the CI layout (the IOUC object is
 * not rebuilt): the requested address travels through niwj_take_self,
 * recorded by a successful decode, taken once by the native enqueue for the
 * same SSID, cleared by any decode. */
extern unsigned niwj_privmac_mode;
/* 0450/0452, kern.n2wifi_8021x, bits (default 0x11): 1 802.1X joins, 2 PTK rekey
 * and EAP re-authentication on the link (0452, off: the link drops and rejoins),
 * 0x10 "4-way handshake complete" message for eapolclient (0453). Bit 1 clear refuses 802.1X
 * profiles as before (NIWJ_PROFILE). 1 decodes auth_upper WPA2 (4) / SHA-256
 * 802.1X (0x80) without a key into a request that the unchanged native binder
 * and IOUC client accept: security NIWJ_WPA2_PSK_CCMP, the WPA2-PSK station
 * element, and the marker of wifi_8021x.h (n8x_marker, AKM in its last byte)
 * in place of the PMK. niwj_select then requires an AP that offers this AKM;
 * the nwn_begin wrapper of wifi_8021x_glue.c turns the request into the real
 * 802.1X attempt. niwj_ap_mfpr counts APs of the wanted SSID refused in the
 * last niwj_select only because they require management frame protection. */
extern unsigned niwj_8021x,niwj_ap_mfpr;
extern int (*niwj_pmk_hook)(const uint8_t*,size_t,const uint8_t*,size_t,uint8_t out[32]);
/* 0434 WPA3-Personal. kern.n2wifi_sae / boot-arg n2sae (niwj_sae_mode):
 * 0 = the 0430 decoder and selection (an SAE request is served as WPA2-PSK
 *     where the request allows that, else refused; no capability bits),
 * 1 = SAE when the request asks for it (auth_upper 0x1000) or the AP offers
 *     no WPA2-PSK alternative, plus PSK-SHA256 and PMF-required APs (default),
 * 2 = SAE whenever a passphrase is present and the AP offers it.
 * kern.n2wifi_pmf / boot-arg n2pmf (niwj_pmf_flags): STC_PMF_* bits, default 0.
 * struct niwj_request keeps the CI layout and the binders are not touched:
 * the passphrase and the allowed AKM families of a decoded request wait in a
 * two-entry store keyed by SSID (replaced by a later decode of that SSID)
 * until the station transaction of that SSID takes them through
 * stc_secret_hook = niwj_secret_take. Values equal NWP_ALLOW_* / STC_*. */
#define NIWJ_ALLOW_LEGACY 1u
#define NIWJ_ALLOW_PMF 2u
#define NIWJ_ALLOW_SAE 4u
#define NIWJ_SAE_REQUESTED 8u
extern unsigned niwj_sae_mode,niwj_pmf_flags;
/* Test override of the allowed families used by niwj_select (0 = from the store). */
extern unsigned niwj_select_allow;
/* Boot-args applied; nonzero when WPA3-Personal is offered (capability bits 75 and 52). */
unsigned niwj_sae_enabled(void);
struct stc_secret;
int niwj_secret_take(const uint8_t*ssid,size_t,struct stc_secret*);
int niwj_take_self(const struct niwj_request*,uint8_t out[6]);
struct niwj_target {
 struct nsb_record record;
 uint8_t band,channel,rate_count,rates[12]; /*500kbps; basic flags retained*/
 uint16_t ap_rsn_bytes;uint8_t ap_rsn[NIWJ_AP_RSN_BYTES];
 int16_t rssi_dbm;
 uint32_t security; /* copied validated request profile, never scan authority */
};
/* Pure retained-record eligibility, never native ownership/readiness. Source
 * nsb_add's full-cache overflow preserves all16 actual records. With matched
 * state7 completion and monotonic complete lifetime, overflow is the only
 * partial cause remaining; timeout/backward-clock/incomplete scan refuse. */
static inline int niwj_scan_usable(const struct nsb_scan*s){
 return s&&s->entered&&s->done&&!s->active&&!s->timed_out&&
  s->completion.state==7&&s->completion.sequence==s->sequence&&
  s->count<=NSB_RESULTS&&s->count<=s->frames&&s->dropped<=s->frames-s->count&&
  s->last>=s->started&&s->deadline>s->started&&s->last<s->deadline&&
  (s->partial?(s->dropped&&s->count==NSB_RESULTS):!s->dropped);
}
/* Owner-thread pure selection from its immutable actual complete scan and
 * allowed-channel snapshot. Does not establish freshness/airtime/ownership.
 * Cached AP must be authenticated/associated and its RSN verified by native
 * transaction; a beacon never establishes connection or permits ordinary TX.
 * Atomic output; no target/country/rate/index is invented. */
int niwj_select(const struct niwj_request*,const struct nsb_scan*,
 const struct nss_info*,uint64_t now_us,struct niwj_target*);
#ifdef __cplusplus
}
#endif
#endif
