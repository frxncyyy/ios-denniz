#define NST_TEST_NO_MAIN 1
#include "transport-hardware.inc"
int main(void){struct test*t=calloc(1,sizeof*t);assert(t);uint8_t p[132];for(unsigned mode=0;mode<4;mode++){
 setup(t);t->s.controlled_port=1;t->s.ptk.installed=1;t->s.ptk.key_id=0;t->s.ofld_drop=mode==1||mode==2;
 memset(p,0,sizeof p);put16(p,sizeof p);put16(p+2,2u<<11);put32(p+4,5u|(15u<<11)|(4u<<16)|(mode==3?1u<<25:0));put32(p+8,1u<<13);put32(p+12,6u<<8);put32(p+16,mode==2?0:3u<<11);put16(p+24,0x4208);memcpy(p+26,t->s.peer.ap,6);p[40]=1;memcpy(p+72,t->s.peer.self,6);p[78]=2;p[83]=7;p[84]=8;
 unsigned before=t->events[NST_ETHERNET];deliver(t,0,p,sizeof p);step(t);unsigned accept=mode==0||mode==2;C(t->events[NST_ETHERNET]==before+accept);C(t->s.ptk.pn[16]==accept);C(t->s.data_diag.rejected[NSDD_RX_ERROR]==!accept);cleanup(t);
 }free(t);printf("PASS %u actual OFLD profile assertions: additional-drop only, nonOFLD unchanged, ICV/replay gates retained\n",checks);return 0;}
