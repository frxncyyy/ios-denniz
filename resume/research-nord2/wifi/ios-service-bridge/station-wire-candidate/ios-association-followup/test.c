#include "event9.h"
#include "producer-fixture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
static unsigned g16(const unsigned char*p){return p[0]|(unsigned)p[1]<<8;}
static void zero(const unsigned char*p,size_t n){while(n--)assert(!*p++);}
/* Projection of mapped callback0x1d3ee5814: preserves the actual scratch extent.
 * This executes no Apple code and does not model CF allocation or callbacks. */
static void mapped_zero_primary(const unsigned char record[48]){
 unsigned char stack[4272];memset(stack,0xa5,sizeof stack);
 memset(stack+152,0,4116); /* original ___bzero, message+24 */
 memcpy(stack+152,record+32,16); /* interface */
 assert(!g16(record+24)&&!g16(record+26));
 /* primary0 copies no bytes to message+40/sp168. */
 zero(stack+168,152); /* helper metadata reads including+148u32 */
 assert(stack[151]==0xa5&&stack[4268]==0xa5);checks++;
}
int main(void){unsigned char record[64],copy[592];struct swia_join join;struct swia_metadata meta;
 size_t used;const uint8_t name[16]="en0";
 assert(swia_decode_pmk(pmk64_fixture,592,&join,sizeof join,&meta)==0);
 assert(!meta.native_flags&&join.auth_upper==8&&join.ssid_length==7);checks++;
 swia_clear(&join,sizeof join);zero((const unsigned char*)&join,sizeof join);checks++;
 memcpy(copy,pmk64_fixture,592);copy[504]=4;
 assert(swia_decode_pmk(copy,592,&join,sizeof join,&meta)==SWIA_UNSUPPORTED&&meta.reason==SWIA_R_NATIVE_OPTIONS);
 zero((const unsigned char*)&join,sizeof join);checks++;
 copy[507]=0x40;assert(swia_decode_pmk(copy,592,&join,sizeof join,&meta)==SWIA_UNSUPPORTED);checks++;
 for(unsigned code=1;code<=21;code++)if(code==1||code==2||code==9||code==21){
  memset(record,0xa5,sizeof record);assert(swif_event9(record,64,&used,name,7,7,code)==0&&used==48);
  assert(record[0]==87&&record[28]==9);mapped_zero_primary(record);checks++;
 }
 for(size_t cap=0;cap<48;cap++){
  memset(record,0xa5,sizeof record);assert(swif_event9(record,cap,&used,name,7,7,1)==SWIA_CAPACITY&&!used);
  for(unsigned i=0;i<64;i++)assert(record[i]==0xa5);checks++;
 }
 assert(swif_event9(record,64,&used,name,7,8,1)==SWIA_STALE&&!used);checks++;
 assert(swif_event9(record,64,&used,name,7,7,0)==SWIA_INVALID&&!used);checks++;
 swia_clear(copy,sizeof copy);zero(copy,sizeof copy);checks++;
 printf("PASS %u focused PMK64/event9 extent, atomicity and refusal checks; synthetic projection only\n",checks);
}
