#include "normal_init.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
static uint8_t out[NNI_FRAGMENT_BYTES],prior[NNI_FRAGMENT_BYTES],board[7000];
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);size_t bytes=fread(board,1,sizeof(board),f);assert(feof(f));assert(!fclose(f));
 struct nni_nvram plan;assert(!nni_nvram_plan(board,bytes,&plan));assert(bytes==6146&&plan.fragments==12&&plan.used_bytes==4297&&plan.ignored_bytes==1846);
 const unsigned sizes[12]={262,113,145,824,824,902,902,77,89,59,67,33};
 for(unsigned seq=0;seq<256;seq++){
  memset(out,0xa5,sizeof(out));assert(!nni_basic(out,sizeof(out),(uint8_t)seq,0,0));
  assert(out[0]==76&&out[2]==0&&out[3]==1&&out[6]==1&&out[32]==44&&out[36]==2&&out[37]==0xa0&&out[38]==1&&out[39]==seq);
  for(unsigned i=64;i<76;i++)assert(!out[i]);
  assert(out[76]==0xa5);
 }
 assert(!nni_basic(out,sizeof(out),1,1,2)&&out[65]==1&&out[66]==2);
 memset(out,0xa5,sizeof(out));memcpy(prior,out,sizeof(out));
 assert(nni_basic(out,75,1,0,0)==-1&&nni_basic(out,sizeof(out),1,2,0)==-1&&nni_basic(out,sizeof(out),1,0,3)==-1);assert(!memcmp(out,prior,sizeof(out)));
 for(unsigned i=0;i<plan.fragments;i++){
  assert(plan.span[i].bytes==sizes[i]&&plan.span[i].tag==i);
  assert(!nni_nvram_fragment(board,bytes,&plan,i,(uint8_t)i,out,sizeof(out)));
  assert(out[0]==0x1c&&out[1]==6&&out[32]==0xfc&&out[33]==5&&out[36]==0x48&&out[37]==0xa0&&out[38]==1&&out[39]==i);
  assert(!memcmp(out+64,board+plan.span[i].offset,sizes[i]));
  for(unsigned k=64+sizes[i];k<sizeof(out);k++)assert(out[k]==0);
 }
 struct nni_nvram unchanged;memset(&unchanged,0xa5,sizeof(unchanged));
 for(size_t n=0;n<4300;n++){struct nni_nvram x=unchanged;assert(nni_nvram_plan(board,n,&x)==-1);assert(!memcmp(&x,&unchanged,sizeof(x)));}
 /* Every builder rejection leaves output intact, including a tampered plan. */
 memset(out,0xa5,sizeof(out));memcpy(prior,out,sizeof(out));struct nni_nvram bad=plan;bad.span[0].offset=UINT32_MAX;
 assert(nni_nvram_fragment(board,bytes,&bad,0,1,out,sizeof(out))==-1);
 assert(nni_nvram_fragment(board,bytes,&plan,12,1,out,sizeof(out))==-1);assert(!memcmp(out,prior,sizeof(out)));
 uint8_t old=board[257];board[257]=0xff;board[258]=0xff;assert(nni_nvram_fragment(board,bytes,&plan,0,1,out,sizeof(out))==-1);assert(!memcmp(out,prior,sizeof(out)));board[257]=old;board[258]=0;
 /* Source's unchecked memcpy is narrowed to the 1500-byte owned payload. */
 uint8_t synthetic[7000]={0};synthetic[257]=0xda;synthetic[258]=4;/* first 256+3+1242=1501 */
 struct nni_nvram x=unchanged;assert(nni_nvram_plan(synthetic,sizeof(synthetic),&x)==-1&&!memcmp(&x,&unchanged,sizeof(x)));
 memset(synthetic,0,sizeof(synthetic));synthetic[257]=1;size_t off=260;
 for(unsigned i=1;i<33;i++){synthetic[off]=(uint8_t)i;synthetic[off+1]=1;off+=4;}
 x=unchanged;assert(nni_nvram_plan(synthetic,sizeof(synthetic),&x)==-1&&!memcmp(&x,&unchanged,sizeof(x)));
 struct ncp_result cap={0};struct nni_address addr,keep;memset(&addr,0xa5,sizeof(addr));keep=addr;
 assert(nni_address_select(0,0,&cap,&addr)==-1&&!memcmp(&addr,&keep,sizeof(addr)));
 cap.decoded=cap.mac_present=cap.mac_valid=1;cap.mac[0]=2;cap.mac[5]=3;
 assert(!nni_address_select(0,0,&cap,&addr)&&addr.source==NNI_ADDRESS_FIRMWARE&&!memcmp(addr.mac,cap.mac,6));
 uint8_t factory[10]={0};factory[4]=2;factory[9]=4;
 assert(!nni_address_select(factory,sizeof(factory),&cap,&addr)&&addr.source==NNI_ADDRESS_BOARD&&!memcmp(addr.mac,factory+4,6));
 factory[4]=1;assert(!nni_address_select(factory,sizeof(factory),&cap,&addr)&&addr.source==NNI_ADDRESS_FIRMWARE);
 cap.mac[0]=1;assert(nni_address_select(factory,sizeof(factory),&cap,&addr)==-1);
 puts("PASS pure normal init: 256 CID2 sequences, exact source 12-fragment saved NVRAM plan and padded CID48 frames, 4300 truncations, mutation/size bounds/output atomicity, real-address precedence without random fallback. No payload logged; no hardware.");
}
