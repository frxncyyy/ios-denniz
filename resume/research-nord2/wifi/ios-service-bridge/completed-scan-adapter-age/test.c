#include "age.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static struct nsb_scan scan;static struct niwa_snapshot out,before;
static void fixture(void){memset(&scan,0,sizeof scan);memset(&out,0,sizeof out);scan.entered=scan.done=1;scan.sequence=scan.completion.sequence=1;scan.completion.state=7;scan.started=100;scan.last=200;scan.deadline=300;scan.count=scan.frames=1;scan.records[0].observed_us=150;}
static void refuse(uint64_t captured,uint64_t reference){struct niwaa_ages a,saved;memset(&a,0xa5,sizeof a);saved=a;assert(niwaa_age_plan(&scan,captured,reference,&a)<0&&!memcmp(&a,&saved,sizeof a));before=out;assert(niwaa_snapshot_prepare(&scan,9,captured,reference,0,0,&out)<0&&!memcmp(&out,&before,sizeof out));}
int main(void){struct niwaa_ages a;unsigned checks=0;uint64_t deltas[]={50,999,1000,1001,(uint64_t)INT32_MAX*1000+999};
 for(unsigned i=0;i<5;i++){fixture();assert(!niwaa_age_plan(&scan,200,150+deltas[i],&a)&&a.milliseconds[0]==deltas[i]/1000&&a.captured_us==200&&a.reference_us==150+deltas[i]);checks++;}
 fixture();refuse(200,150+((uint64_t)INT32_MAX+1)*1000);refuse(199,200);refuse(200,199);scan.records[0].observed_us=201;refuse(200,200);scan.records[0].observed_us=99;refuse(200,200);checks+=5;
 for(unsigned bad=0;bad<9;bad++){fixture();if(bad==0)scan.active=1;if(bad==1)scan.partial=1;if(bad==2)scan.timed_out=1;if(bad==3)scan.dropped=1;if(bad==4)scan.completion.sequence=2;if(bad==5)scan.completion.state=0;if(bad==6)scan.count=17;if(bad==7)scan.last=300;if(bad==8)scan.frames=0;refuse(300,300);checks++;}
 fixture();scan.started=UINT64_MAX-200;scan.last=UINT64_MAX-100;scan.deadline=UINT64_MAX;scan.records[0].observed_us=UINT64_MAX-150;assert(!niwaa_age_plan(&scan,UINT64_MAX-50,UINT64_MAX,&a)&&a.milliseconds[0]==0);checks++;
 fixture();scan.count=scan.frames=0;assert(!niwaa_snapshot_prepare(&scan,9,200,200,0,0,&out)&&out.count==0&&out.cursor.phase==NIWC_READY);checks++;
 fixture();struct nsb_record*r=&scan.records[0];r->bssid[0]=2;r->subtype=8;r->channel=r->hw_channel=6;r->ssid_present=1;r->interval=100;r->rxv_valid=1;r->group_valid=4;r->rxv[4]=100;r->rxv[5]=90;r->ie_total=r->ie_bytes=3;r->ies[0]=1;r->ies[1]=1;r->ies[2]=2;
 /* Explicit synthetic rate fixture; no production mapping asserted. */
 struct niwp_rate_map map={2,123};before=out;assert(niwaa_snapshot_prepare(&scan,9,200,1150,0,0,&out)<0&&!memcmp(&out,&before,sizeof out));assert(!niwaa_snapshot_prepare(&scan,9,200,1150,&map,1,&out));uint8_t wire[1164];assert(niwc_take(&out.cursor,9,wire,sizeof wire)==1);struct niws_result decoded;assert(!niws_decode_result(wire,sizeof wire,&decoded)&&decoded.age==1&&decoded.rates[0]==123);checks+=2;
 fixture();scan.count=scan.frames=16;for(unsigned i=0;i<16;i++)scan.records[i].observed_us=100+i;assert(!niwaa_age_plan(&scan,200,1200,&a)&&a.count==16);for(unsigned i=0;i<16;i++)assert(a.milliseconds[i]==1);scan.records[15].observed_us=201;refuse(200,1200);assert(niwaa_age_plan(0,200,1200,&a)<0&&niwaa_age_plan(&scan,200,1200,0)<0);checks+=3;
 printf("PASS %u age checks: signed32 milliseconds/floor/atomic refusal/same-clock boundaries + completed adapter integration; synthetic rate only\n",checks);return 0;}
