#include "niw_rate_compat.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define C(x) do{assert(x);checks++;}while(0)
int main(void){
 const uint8_t source[12]={2,4,11,22,12,18,24,36,48,72,96,108};
 const uint32_t wanted[12]={1,2,5,11,6,9,12,18,24,36,48,54};
 size_t n=777;const struct niwp_rate_map*m=niwr_production_map(&n);C(m&&n==12);C(!niwr_production_map(0));
 for(unsigned i=0;i<12;i++){C(m[i].ie_rate==source[i]&&m[i].ios_rate==wanted[i]);}
 for(unsigned raw=0;raw<256;raw++){
  uint8_t p=(uint8_t)raw;uint32_t out[15];memset(out,0xa5,sizeof out);uint32_t saved[15];memcpy(saved,out,sizeof out);size_t count=99;unsigned i;
  for(i=0;i<12;i++)if((raw&127)==source[i])break;
  int result=niwr_rates(&p,1,out,15,&count);
  if(i<12){C(!result&&count==1&&out[0]==wanted[i]);C(!memcmp(out+1,saved+1,14*sizeof(*out)));}
  else C(result&&count==99&&!memcmp(out,saved,sizeof out));
 }
 uint8_t input[36];for(unsigned i=0;i<12;i++){input[i]=source[i];input[12+i]=source[i]|128;input[24+i]=source[i];}
 for(unsigned cap=0;cap<=15;cap++){
  uint32_t out[15],saved[15];memset(out,0x5a,sizeof out);memcpy(saved,out,sizeof out);size_t count=99;
  int result=niwr_rates(input,sizeof input,out,cap,&count);
  if(cap<12)C(result&&count==99&&!memcmp(out,saved,sizeof out));
  else{C(!result&&count==12&&!memcmp(out,wanted,sizeof wanted));C(!memcmp(out+12,saved+12,3*sizeof(*out)));}
 }
 uint32_t out[15],saved[15];memset(out,0x77,sizeof out);memcpy(saved,out,sizeof out);size_t count=99;
 uint8_t mixed[]={2,4,11,3};C(niwr_rates(mixed,sizeof mixed,out,15,&count)&&count==99&&!memcmp(out,saved,sizeof out));
 C(!niwr_rates(0,0,out,0,&count)&&count==0&&!memcmp(out,saved,sizeof out));
 C(niwr_rates(0,1,out,15,&count));C(niwr_rates(input,256,out,15,&count));C(niwr_rates(input,1,0,15,&count));C(niwr_rates(input,1,out,15,0));
 printf("PASS %u production-rate compatibility checks: all256selectors/basicbit/dedup/atomiccapacity/unknown/nullbounds\n",checks);
}
