#include "abi-layout.h"
#include "wifi_main_wfdma_native.h"
#include "wifi_main_fw_xnu.h"
void ncd_canonical_abi(size_t o[NCD_ABI_FIELDS]) {
 const size_t v[NCD_ABI_FIELDS]={
 sizeof(struct nwt_state),offsetof(struct nwt_state,tx_ring),offsetof(struct nwt_state,tx_index),
 offsetof(struct nwt_state,irq_status),offsetof(struct nwt_state,tx_descriptor_address),
 offsetof(struct nwt_state,tx_submitted),offsetof(struct nwt_state,tx_observed),
 offsetof(struct nwt_state,tx_didx),offsetof(struct nwt_state,tx_used),
 offsetof(struct nwt_state,tx_mismatch),offsetof(struct nwt_state,tx_descriptor_valid),
 offsetof(struct nwt_state,tx_ddone_waits),sizeof(struct nwi_state),
 offsetof(struct nwi_state,transport),sizeof(struct nwfn_native),
 offsetof(struct nwfn_native,init),offsetof(struct nwfn_native,finish_tx),
 sizeof(struct nfp_native),offsetof(struct nfp_native,state),
 offsetof(struct nfp_native,pre_span_reason),offsetof(struct nfp_native,pre_span_index),
 offsetof(struct nwt_state,tx_consumed_zero),offsetof(struct nwt_state,rx_descriptor_address),offsetof(struct nwt_state,rx_expected_buffer),offsetof(struct nwt_state,rx_descriptor_raw),offsetof(struct nwt_state,rx_didx),offsetof(struct nwt_state,rx_diag_valid),offsetof(struct nwt_state,rx_failure_reason),offsetof(struct nwt_state,rx_expected_eid),offsetof(struct nwt_state,rx_expected_sequence),offsetof(struct nwt_state,rx_packet_type),offsetof(struct nwt_state,rx_event_eid),offsetof(struct nwt_state,rx_event_sequence),offsetof(struct nwt_state,rx_status),offsetof(struct nwt_state,rx_cid)};
 for(unsigned i=0;i<NCD_ABI_FIELDS;i++)o[i]=v[i];
}
