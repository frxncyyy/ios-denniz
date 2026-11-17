#ifndef WIFI_8021X_RENAME_H
#define WIFI_8021X_RENAME_H
/* 0450: the 802.1X instance of the station stack. The prebuilt CI members
 * wifi_wpa2.o, wifi_station_transaction.o, wifi_wpa_owner.o and wifi_wpa_nst.o
 * keep serving WPA2-PSK and OPEN unchanged; the 802.1X joins run through a
 * second copy of the same four sources, compiled as new objects whose global
 * symbols carry the prefix e8_ (wifi_8021x_wpa2.c, wifi_8021x_stc.c and
 * wifi_8021x_nst.c are generated modified copies, wifi_8021x_owner.c includes
 * the unmodified source). Same structures, same transport, same binder storage.
 * This header is the first include of those four files and of nothing else. */
#define nwp_begin_ap e8_nwp_begin_ap
#define nwp_copy_ap_rsnxe e8_nwp_copy_ap_rsnxe
#define nwp_ap_rsn e8_nwp_ap_rsn
#define nwp_ap_rsn_akm e8_nwp_ap_rsn_akm
#define nwp_begin e8_nwp_begin
#define nwp_receive e8_nwp_receive
#define nwp_security_submitted e8_nwp_security_submitted
#define nwp_keys_ready e8_nwp_keys_ready
#define nwp_group_ready e8_nwp_group_ready
#define nwp_cancel e8_nwp_cancel
#define nwp_pmk e8_nwp_pmk
#define stc_scan_acquire e8_stc_scan_acquire
#define stc_scan_release e8_stc_scan_release
#define stc_begin e8_stc_begin
#define stc_next e8_stc_next
#define stc_submitted e8_stc_submitted
#define stc_receipt e8_stc_receipt
#define stc_grant e8_stc_grant
#define stc_management e8_stc_management
#define stc_eapol e8_stc_eapol
#define stc_key_event e8_stc_key_event
#define stc_channel_released e8_stc_channel_released
#define stc_link_lost e8_stc_link_lost
#define stc_cancel e8_stc_cancel
#define stc_tick e8_stc_tick
#define stc_pmk e8_stc_pmk
#define stc_eap_waiting e8_stc_eap_waiting
#define nwo_begin e8_nwo_begin
#define nwo_pump e8_nwo_pump
#define nwo_complete e8_nwo_complete
#define nwo_eapol e8_nwo_eapol
#define nwo_management e8_nwo_management
#define nwo_grant e8_nwo_grant
#define nwo_key24 e8_nwo_key24
#define nwo_link_lost e8_nwo_link_lost
#define nwo_cancel e8_nwo_cancel
#define nwo_authorized e8_nwo_authorized
#define nwn_begin e8_nwn_begin
#define nwn_pump e8_nwn_pump
#define nwn_event e8_nwn_event
#define nwn_peer_complete e8_nwn_peer_complete
#define nwn_cancel e8_nwn_cancel
#define nwp_rekey_pmk e8_nwp_rekey_pmk
#define nwp_rekeying e8_nwp_rekeying
#define nwp_rekey_done e8_nwp_rekey_done
#define nwn_rekey_installs e8_nwn_rekey_installs
#include <stddef.h>
#include <stdint.h>
struct stc;struct nwp;
/* 0452 PTK rekey: new PMK of a re-authentication for the next message 1; a rekeyed PTK awaits its install; install done. */
void nwp_rekey_pmk(const uint8_t pmk[32]);
int nwp_rekeying(const struct nwp*);
void nwp_rekey_done(void);
extern unsigned nwn_rekey_installs;
/* Additions of the 802.1X instance (not in the CI headers). */
int nwp_ap_rsn_akm(const uint8_t*,size_t,unsigned akm);
/* The PMK of the EAP exchange starts the WPA engine: 1 started (a waiting
 * message 1 is answered), 0 not applicable now, <0 the transaction stopped. */
int stc_pmk(struct stc*,uint64_t generation,const uint8_t pmk[32],uint64_t now_ms);
/* The attempt associated without a PMK and still waits for it. */
int stc_eap_waiting(const struct stc*);
#endif
