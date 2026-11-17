#include <stddef.h>
#include <stdio.h>
#include <assert.h>
void active_layout(size_t*);void candidate_layout(size_t*);
int main(void){size_t a[29],b[29];active_layout(a);candidate_layout(b);
assert(a[0]==b[0]);printf("nwt_size=%zu\n",a[0]);
assert(a[1]==b[1]);printf("tx_descriptor_address=%zu\n",a[1]);
assert(a[2]==b[2]);printf("tx_consumed_zero=%zu\n",a[2]);
assert(a[3]==b[3]);printf("rx_descriptor_address=%zu\n",a[3]);
assert(a[4]==b[4]);printf("rx_expected_buffer=%zu\n",a[4]);
assert(a[5]==b[5]);printf("rx_descriptor_raw=%zu\n",a[5]);
assert(a[6]==b[6]);printf("rx_didx=%zu\n",a[6]);
assert(a[7]==b[7]);printf("rx_diag_valid=%zu\n",a[7]);
assert(a[8]==b[8]);printf("rx_failure_reason=%zu\n",a[8]);
assert(a[9]==b[9]);printf("rx_expected_eid=%zu\n",a[9]);
assert(a[10]==b[10]);printf("rx_expected_sequence=%zu\n",a[10]);
assert(a[11]==b[11]);printf("rx_packet_type=%zu\n",a[11]);
assert(a[12]==b[12]);printf("rx_event_eid=%zu\n",a[12]);
assert(a[13]==b[13]);printf("rx_event_sequence=%zu\n",a[13]);
assert(a[14]==b[14]);printf("rx_status=%zu\n",a[14]);
assert(a[15]==b[15]);printf("rx_cid=%zu\n",a[15]);
assert(a[16]==b[16]);printf("nwi_size=%zu\n",a[16]);
assert(a[17]==b[17]);printf("nwi_transport=%zu\n",a[17]);
assert(a[18]==b[18]);printf("nwfn_size=%zu\n",a[18]);
assert(a[19]==b[19]);printf("nwfn_init=%zu\n",a[19]);
assert(a[20]==b[20]);printf("nwfn_rom=%zu\n",a[20]);
assert(a[21]==b[21]);printf("nwfn_pool=%zu\n",a[21]);
assert(a[22]==b[22]);printf("nwfn_halt=%zu\n",a[22]);
assert(a[23]==b[23]);printf("nwfn_stage=%zu\n",a[23]);
assert(a[24]==b[24]);printf("nwfn_ap_owned=%zu\n",a[24]);
assert(a[25]==b[25]);printf("nwfn_wf_owned=%zu\n",a[25]);
assert(a[26]==b[26]);printf("nwfn_finish_tx=%zu\n",a[26]);
assert(a[27]==b[27]);printf("nfp_size=%zu\n",a[27]);
assert(a[28]==b[28]);printf("nfp_pre_reason=%zu\n",a[28]);
return 0;}
