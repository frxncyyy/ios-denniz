#include "backend_bss.h"
#include <assert.h>
#include <string.h>
int main(void){struct nsb_record s={0};struct niwb_bss r,old;s.hw_channel=s.channel=1;s.subtype=8;s.rxv_valid=1;s.group_valid=4;s.rxv[4]=100;s.rxv[5]=120;s.ie_bytes=s.ie_total=6;s.ies[0]=1;s.ies[1]=4;s.ies[2]=0x82;s.ies[3]=11;s.ies[4]=22;s.ies[5]=2;
 assert(!niwb_bss_metadata(&s,&r)&&r.rssi_valid&&r.rssi_dbm==-50&&r.rate_count==3&&r.ie_rates[1]==11&&r.band==1);
 for(unsigned a=0;a<256;a++)for(unsigned b=0;b<256;b++){s.rxv[4]=a;s.rxv[5]=b;assert(!niwb_bss_metadata(&s,&r));unsigned x=a==255?0:a,y=b==255?0:b,z=x>y?x:y;assert(r.rcpi==z&&r.rssi_dbm==(int)((z>220?220:z)/2)-110&&r.rssi_valid==(a!=255||b!=255));}
 s.hw_channel=s.channel=36;assert(!niwb_bss_metadata(&s,&r)&&r.band==2);old=r;s.ies_truncated=1;assert(niwb_bss_metadata(&s,&r)==-1&&!memcmp(&r,&old,sizeof r));s.ies_truncated=0;s.channel_mismatch=1;assert(niwb_bss_metadata(&s,&r)==-1);s.channel_mismatch=0;s.ie_bytes=5;assert(niwb_bss_metadata(&s,&r)==-1);return 0;}
