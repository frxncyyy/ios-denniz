#ifndef WIFI_SAE_H
#define WIFI_SAE_H
#include <stddef.h>
#include <stdint.h>
/* 0432: WPA3-Personal SAE (IEEE 802.11-2020 12.4), group 19 (NIST P-256)
 * only, hunting-and-pecking and hash-to-element. Pure serialized engine in
 * the style of wifi_wpa2: no IO, timers, logging or allocation; one owner
 * serializes all calls; every buffer is a kernel-owned copy. A negative
 * return leaves no usable key material (the state is wiped).
 * Bodies are the octets that follow the six fixed octets (algorithm 3,
 * transaction sequence, status) of an Authentication frame. */
#define NSAE_GROUP 19u
#define NSAE_COMMIT_MIN 98u          /* group + scalar + element */
#define NSAE_BODY_MAX 360u           /* commit with a 255 octet token container */
#define NSAE_STATUS_H2E 126u         /* SAE_HASH_TO_ELEMENT */
#define NSAE_STATUS_TOKEN 76u        /* ANTI_CLOGGING_TOKEN_REQUIRED */
#define NSAE_STATUS_GROUP 77u        /* UNSUPPORTED_FINITE_CYCLIC_GROUP */
enum nsae_state {NSAE_OFF,NSAE_READY,NSAE_COMMITTED,NSAE_CONFIRMED,NSAE_ACCEPTED};
/* Same contract as nwp_rng: fill exactly 32 octets from a real CSPRNG, 0 on success. */
typedef int (*nsae_rng)(void*,uint8_t*,size_t);
struct nsae {
 unsigned state,h2e,send_confirm,token_bytes;
 uint8_t own[6],peer[6],pwe[64],rand[32],scalar[32],element[64];
 uint8_t peer_scalar[32],peer_element[64],kck[32],pmk[32],pmkid[16],token[255];
};
/* Derives the PWE (h2e: PT from SSID/password, then PWE; otherwise the 40
 * iteration loop), draws rand and mask and computes the commit scalar and
 * element. Password 1..63 octets, SSID 1..32, unicast distinct addresses. */
int nsae_begin(struct nsae*,const uint8_t own[6],const uint8_t peer[6],const uint8_t*password,size_t,
 const uint8_t*ssid,size_t,unsigned h2e,nsae_rng,void*);
/* group | [token] | scalar | element | [token container] ; READY or COMMITTED (retransmission). */
int nsae_commit(struct nsae*,uint8_t*out,size_t capacity,size_t*used);
/* Body of a status 76 frame: remembers the token for the next nsae_commit. */
int nsae_token(struct nsae*,const uint8_t*,size_t);
/* Peer commit body (status 0 or 126 already checked by the caller): validates
 * group, scalar range, element on curve, reflection; derives KCK, PMK, PMKID. */
int nsae_peer_commit(struct nsae*,const uint8_t*,size_t);
/* send-confirm | confirm (34 octets); each call uses the next send-confirm. */
int nsae_confirm(struct nsae*,uint8_t*out,size_t capacity,size_t*used);
/* Verifies the peer confirm; on success the state is ACCEPTED and pmk/pmkid are final. */
int nsae_peer_confirm(struct nsae*,const uint8_t*,size_t);
void nsae_cancel(struct nsae*);
/* Building blocks, exposed for the known-answer tests. Affine x||y, big endian. */
int nsae_pwe_loop(const uint8_t a[6],const uint8_t b[6],const uint8_t*password,size_t,uint8_t out[64]);
int nsae_pt(const uint8_t*ssid,size_t,const uint8_t*password,size_t,const uint8_t*identifier,size_t,uint8_t out[64]);
int nsae_pwe_pt(const uint8_t pt[64],const uint8_t a[6],const uint8_t b[6],uint8_t out[64]);
/* out = k*P (affine), k big endian 32 octets, 0 < k < order; test support. */
int nsae_scalar_mult(const uint8_t point[64],const uint8_t k[32],uint8_t out[64]);
#endif
