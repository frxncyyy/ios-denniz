#include "query_contract.h"
static uint32_t get32(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int niwq_caps_decode(const uint8_t*p,size_t n,struct niwq_caps*out){
 struct niwq_caps c={0};if(!p||!out||n!=13)return -1;
 for(unsigned i=0;i<13;i++)c.raw[i]=p[i];
 for(unsigned i=0;i<103;i++)if(p[i/8]&(1u<<(i%8)))c.set[c.count++]=(uint8_t)i;
 c.unconsumed_bit103=!!(p[12]&128);*out=c;return 0;
}
int niwq_power_decode(const uint8_t*p,size_t n,struct niwq_power*out){
 struct niwq_power r={0};if(!p||!out||n!=24||get32(p)!=1||get32(p+4)>4)return -1;
 r.version=get32(p);r.count=get32(p+4);
 for(unsigned i=0;i<4;i++)r.state[i]=get32(p+8+4*i);
 for(unsigned i=0;i<r.count;i++){r.any_state_one|=r.state[i]==1;r.unknown_states+=r.state[i]>1;}
 *out=r;return 0;
}
enum niwq_result_status niwq_result_status(int get_return,uint32_t captured_errno){
 if(!get_return)return NIWQ_ROW;
 if(captured_errno==5||captured_errno==UINT32_C(0xe0820446))return NIWQ_CACHE_END;
 if(captured_errno==16)return NIWQ_BUSY;
 return NIWQ_ERROR;
}
