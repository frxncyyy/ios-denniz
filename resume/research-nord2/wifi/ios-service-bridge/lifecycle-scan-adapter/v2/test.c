#include "lifecycle_scan.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct niwa_snapshot output,other;
static struct nsb_scan scan;
static void completed(void){memset(&scan,0,sizeof scan);scan.entered=1;scan.done=1;scan.sequence=1;scan.completion.sequence=1;scan.completion.state=7;scan.started=100;scan.last=200;scan.deadline=1000;}
int main(void){
 struct nils_state s={0};struct nils_ticket t,old;uint8_t row[1164];
 assert(nils_open(&s,1)==0);assert(nils_open(&s,2)==-1);
 completed();assert(nils_begin(&s,&t,&output)==0);old=t;
 assert(nils_begin(&s,&old,&output)==-1);
 assert(nils_prepare(&t,&scan,200,300,0,0,&other)==-1&&!other.entered);
 assert(nils_finish(&s,&t,&other)==-1&&s.inflight&&s.pending==&output);
 struct nils_ticket forged=t;forged.output=&other;assert(nils_finish(&s,&forged,0)==-1&&s.inflight);assert(nils_take(&s,1,row,sizeof row)==-1);
 assert(nils_prepare(&t,&scan,200,300,0,0,&output)==0);
 assert(nils_finish(&s,&t,&output)==0&&s.cache==&output&&output.count==0);
 assert(nils_take(&s,1,row,sizeof row)==0);
 assert(nils_begin(&s,&old,&output)==-1&&!s.inflight); /* honest completed-empty EOF */
 assert(nils_take(&s,2,row,sizeof row)==-1);assert(nils_finish(&s,&t,&output)==-1);
 memset(&output,0,sizeof output);scan.done=0;scan.active=1;
 assert(!nils_begin(&s,&t,&output)&&!s.cache);assert(nils_prepare(&t,&scan,0,0,0,0,&output)==1);
 assert(nils_finish(&s,&t,&output)==1&&!s.cache);assert(nils_take(&s,1,row,sizeof row)==-1);
 memset(&output,0,sizeof output);scan.active=0;scan.partial=1;scan.timed_out=1;
 assert(!nils_begin(&s,&t,&output));assert(nils_prepare(&t,&scan,0,0,0,0,&output)==1);
 assert(output.status==NIWA_TIMEOUT&&nils_finish(&s,&t,&output)==1&&!s.cache);
 completed();memset(&output,0,sizeof output);assert(!nils_begin(&s,&t,&output));
 nils_close(&s);assert(!s.open&&!s.cache&&s.inflight);assert(nils_open(&s,2)==-1);
 assert(nils_prepare(&t,&scan,200,300,0,0,&output)==0);assert(nils_finish(&s,&t,&output)==2&&!s.inflight&&!s.cache);
 assert(nils_open(&s,1)==-1);assert(nils_open(&s,2)==0);old=t;
 memset(&output,0,sizeof output);assert(!nils_begin(&s,&t,&output));assert(nils_finish(&s,&old,&output)==-1&&s.inflight);
 assert(nils_finish(&s,&t,0)==-1&&!s.inflight); /* preparation failure consumes ticket */
 completed();memset(&output,0,sizeof output);assert(!nils_begin(&s,&t,&output));
 assert(nils_prepare(&t,&scan,199,300,0,0,&output)==-1&&!output.entered);
 assert(nils_finish(&s,&t,0)==-1&&!s.cache);
 /* Synthetic BSS fixture only: missing rate map must not create availability. */
 completed();memset(&output,0,sizeof output);scan.count=scan.frames=1;
 struct nsb_record*r=&scan.records[0];r->observed_us=150;r->bssid[0]=2;r->subtype=8;
 r->channel=r->hw_channel=6;r->ssid_present=1;r->interval=100;r->rxv_valid=1;
 r->group_valid=4;r->rxv[4]=100;r->rxv[5]=90;r->ie_total=r->ie_bytes=3;
 r->ies[0]=1;r->ies[1]=1;r->ies[2]=2;
 assert(!nils_begin(&s,&t,&output));assert(nils_prepare(&t,&scan,200,1150,0,0,&output)==-1);
 assert(nils_finish(&s,&t,0)==-1&&!s.cache);
 struct niwp_rate_map map={2,123};assert(!nils_begin(&s,&t,&output));
 assert(!nils_prepare(&t,&scan,200,1150,&map,1,&output));assert(!nils_finish(&s,&t,&output));
 assert(nils_take(&s,2,row,1163)==-1);assert(nils_take(&s,2,row,sizeof row)==1);
 struct niws_result decoded;assert(!niws_decode_result(row,sizeof row,&decoded));
 assert(decoded.age==1&&decoded.rates[0]==123);assert(nils_take(&s,2,row,sizeof row)==0);
 nils_close(&s);assert(nils_take(&s,2,row,sizeof row)==-1);
 s.serial=UINT64_MAX;assert(!nils_open(&s,3));assert(nils_begin(&s,&t,&output)==-1);
 puts("PASS lifecycle scan: completed empty EOF; pending/timeout refusal; close-inflight/stale/replay; atomic invalid age");
}
