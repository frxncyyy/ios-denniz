#include "query_contract.h"
#include "cache_cursor.h"
#include "../scan-and-initialization/scan_contract.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void put(uint8_t*p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=x>>(8*i);}
int main(void){
 uint8_t caps[13]={0};struct niwq_caps c,old;
 for(unsigned i=0;i<104;i++){memset(caps,0,13);caps[i/8]=1u<<(i%8);assert(!niwq_caps_decode(caps,13,&c));assert(c.count==(i<103)&&c.unconsumed_bit103==(i==103));if(i<103)assert(c.set[0]==i);}
 memset(caps,255,13);assert(!niwq_caps_decode(caps,13,&c)&&c.count==103&&c.unconsumed_bit103);old=c;
 for(unsigned n=0;n<13;n++)assert(niwq_caps_decode(caps,n,&c)<0&&!memcmp(&c,&old,sizeof c));
 uint8_t power[24]={0};struct niwq_power p;put(power,1);put(power+4,4);put(power+8,0);put(power+12,1);put(power+16,2);assert(!niwq_power_decode(power,24,&p)&&p.any_state_one&&p.unknown_states==1);put(power+4,5);assert(niwq_power_decode(power,24,&p)<0);
 assert(niwq_result_status(0,5)==NIWQ_ROW&&niwq_result_status(-1,5)==NIWQ_CACHE_END&&niwq_result_status(-1,0xe0820446)==NIWQ_CACHE_END&&niwq_result_status(-1,16)==NIWQ_BUSY&&niwq_result_status(-1,0)==NIWQ_ERROR);
 uint8_t records[2*1164]={0},row[1164],saved[1164];struct niws_result r={0};r.ssid_bytes=1;r.ssid[0]='x';assert(!niws_encode_result(&r,records,1164));r.ssid[0]='y';assert(!niws_encode_result(&r,records+1164,1164));
 struct niwc_cursor cursor={0};assert(!niwc_begin(&cursor,7));memset(row,0xa5,1164);memcpy(saved,row,1164);assert(niwc_take(&cursor,7,row,1164)<0&&!memcmp(row,saved,1164));assert(niwc_complete(&cursor,8,records,sizeof records,2)<0);records[0]=2;assert(niwc_complete(&cursor,7,records,sizeof records,2)<0&&cursor.phase==NIWC_WAITING);records[0]=1;
 assert(!niwc_complete(&cursor,7,records,sizeof records,2));assert(niwc_take(&cursor,8,row,1164)<0&&!cursor.next);assert(niwc_take(&cursor,7,row,1164)==1&&row[97]=='x');assert(niwc_take(&cursor,7,row,1164)==1&&row[97]=='y');assert(!niwc_take(&cursor,7,row,1164));assert(!niwc_cancel(&cursor,7)&&niwc_take(&cursor,7,row,1164)<0);assert(niwc_begin(&cursor,8)<0);
 struct niwc_cursor empty={0};assert(!niwc_begin(&empty,9)&&!niwc_complete(&empty,9,0,0,0)&&!niwc_take(&empty,9,row,1164));
 puts("PASS103-bit capabilities/no fake flags, power scalar bounds, exact end classification, generation/cancel/snapshot cursor and no row while waiting");
}
