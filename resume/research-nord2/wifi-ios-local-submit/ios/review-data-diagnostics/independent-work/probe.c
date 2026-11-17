#include "actual-helper.inc"
static unsigned extra,rejected;
static int reject_delivery(void*c,const struct nst_event*e){(void)c;assert(e->kind==NST_ETHERNET);rejected++;return -1;}
int main(void){
 uint8_t packet[1518],orig[1518],tx[88]={0};struct nsdd_frame f;struct nsdd_txs t;size_t n=dhcp_packet(orig,1,0,0);
 /* IP options shift UDP; classifier must honor IHL and bounded total length. */
 for(unsigned ihl=5;ihl<=15;ihl++){memcpy(packet,orig,34);unsigned off=ihl*4;memset(packet+34,0,off-20);memcpy(packet+14+off,orig+34,n-34);packet[14]=(uint8_t)(0x40|ihl);dput16(packet+16,(unsigned)n-14+off-20);assert(!nsdd_classify(&f,packet,n+off-20)&&f.dhcp==1);extra++;}
 memcpy(packet,orig,n);packet[283]=0;dput16(packet+16,270);dput16(packet+38,250);assert(!nsdd_classify(&f,packet,284)&&f.parse==NSDD_TYPE&&!f.dhcp);extra++;
 for(unsigned flags=0;flags<256;flags++){tx[8]=(uint8_t)flags;tx[2]=1;tx[3]=2;tx[5]=3;tx[6]=4;tx[7]=5;tx[9]=6;tx[14]=7;assert(!nsdd_decode_txs(&t,tx,88));assert(t.advanced==(flags&1));assert(t.sequence==((flags&1)?513u:0u)&&t.rate==((flags&1)?1284u:0u));extra++;}
 /* Diagnostic RX is native accepted/rearmed, not success of a BSD consumer. */
 struct test*h=calloc(1,sizeof *h);assert(h);struct composed x;struct stc_arbiter a={0};init_join(h,&x,&a);m2_retired(&x);eapol_rx(&x,m3,sizeof m3);advance(&x);finish_keys(&x);n=dhcp_packet(packet,2,1,0);memcpy(packet,h->s.peer.self,6);actual_ether(h,packet,n,0,1,0);
 unsigned before=h->s.queue[0].rearmed;assert(nst_step(&h->s,&io,h,16,reject_delivery,0)<0);assert(rejected==1&&h->s.queue[0].rearmed==before+1&&h->s.data_diag.rx_unicast==1&&h->s.data_diag.rx_type[2]==1&&h->s.halt&&!h->s.controlled_port);extra++;cleanup(h);free(h);
 printf("PASS %u independent IHL/optionextent/advancedflag/native-vs-consumer failure probes\n",extra);return 0;}
