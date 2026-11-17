#include "snapshot.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static struct niwa_snapshot out,before;static struct nsb_scan scan;
static void fixture(void){memset(&out,0,sizeof out);memset(&scan,0,sizeof scan);scan.entered=1;scan.sequence=1;scan.started=1;scan.last=2;scan.deadline=100;scan.done=1;scan.completion.sequence=1;scan.completion.state=7;scan.completion.completed_channels=22;}
int main(void){uint8_t bytes[1164];uint32_t ages[16]={0};struct niwp_rate_map rates[]={{2,1},{12,6}};
 fixture();assert(!niwa_snapshot_prepare(&scan,8,0,0,0,0,&out));assert(out.status==NIWA_COMPLETE&&!out.count&&out.cursor.phase==NIWC_READY);assert(!niwc_take(&out.cursor,8,bytes,sizeof bytes));
 for(unsigned kind=0;kind<3;kind++){fixture();scan.done=0;scan.active=kind==0;scan.partial=kind!=0;scan.timed_out=kind==2;assert(niwa_snapshot_prepare(&scan,8,0,0,0,0,&out)==1);assert(out.status==(kind==0?NIWA_PENDING:kind==1?NIWA_PARTIAL:NIWA_TIMEOUT));assert(out.cursor.phase==NIWC_NEW&&niwc_take(&out.cursor,8,bytes,sizeof bytes)<0);}
 fixture();scan.count=scan.frames=1;struct nsb_record*r=&scan.records[0];r->bssid[0]=2;r->bssid[5]=1;r->subtype=8;r->channel=r->hw_channel=6;r->ssid_present=1;r->ssid_len=1;r->ssid[0]='x';r->interval=100;r->rxv_valid=1;r->group_valid=4;r->rxv[4]=100;r->rxv[5]=90;r->ie_total=r->ie_bytes=4;r->ies[0]=1;r->ies[1]=2;r->ies[2]=2;r->ies[3]=12;
 assert(!niwa_snapshot_prepare(&scan,9,ages,1,rates,2,&out));assert(niwc_take(&out.cursor,8,bytes,sizeof bytes)<0);assert(niwc_take(&out.cursor,9,bytes,sizeof bytes)==1);struct niws_result decoded;assert(!niws_decode_result(bytes,sizeof bytes,&decoded)&&decoded.channel_flags==8&&decoded.channel==6&&decoded.rssi==-60&&decoded.rate_count==2&&!decoded.noise_valid);assert(!niwc_take(&out.cursor,9,bytes,sizeof bytes));assert(niwa_snapshot_prepare(&scan,9,ages,1,rates,2,&out)<0);
 for(unsigned bad=0;bad<9;bad++){memset(&out,0,sizeof out);before=out;struct nsb_scan copy=scan;const struct niwp_rate_map*m=rates;size_t n=2,z=1;
  if(bad==0)copy.completion.state=0;if(bad==1)copy.completion.sequence=2;if(bad==2)copy.records[0].channel_mismatch=1;if(bad==3)copy.records[0].rxv_valid=0;if(bad==4)copy.records[0].ies_truncated=1;if(bad==5)m=0;if(bad==6)z=0;if(bad==7)copy.active=1;if(bad==8)copy.count=17;
  assert(niwa_snapshot_prepare(&copy,9,ages,z,m,n,&out)<0&&!memcmp(&out,&before,sizeof out));
 }
 memset(&out,0,sizeof out);scan.partial=1;assert(niwa_snapshot_prepare(&scan,9,ages,1,rates,2,&out)==1&&out.backend_count==1&&out.cursor.phase!=NIWC_READY);
 puts("PASS immutable completed snapshot: true empty EOF; bounded actual rows; pending/partial/timeout never READY; unknown metadata refuses; generation/reentry/cursor lifetime. Synthetic inputs only.");return 0;}
