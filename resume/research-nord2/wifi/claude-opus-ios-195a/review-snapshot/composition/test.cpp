#define NIWL_HOST_TEST 1
#include "wifi_ios_client.cpp"
#include <cstdio>
static task ownerTask,otherTask;
static nss_completion callback;static void*cookie;static uint64_t ticket=1,gen;static bool active,canceled;static unsigned releases,enqueues;static nsb_scan scan;
/* Same22 channel numbers as retained owner/hardware source evidence. Hardware,
 * owner queue execution and RX delivery remain explicitly mocked here. */
extern "C" int nord2_wifi_scan_info(nss_info*out){*out={};out->mac[0]=2;out->count=22;out->band5=1;out->scanning=active;for(unsigned i=0;i<13;i++)out->channels[i]={1,(uint8_t)(i+1)};const uint8_t five[9]={36,40,44,48,149,153,157,161,165};for(unsigned i=0;i<9;i++)out->channels[13+i]={2,five[i]};return 0;}
extern "C" int nord2_wifi_scan_enqueue(const nss_request*r,void*c,uint64_t g,nss_completion f,uint64_t*t){assert(r->count==13&&!active);for(unsigned i=0;i<13;i++)assert(r->channels[i].band==1&&r->channels[i].channel==i+1);active=true;canceled=false;cookie=c;gen=g;callback=f;enqueues++;*t=++ticket;return 0;}
extern "C" int nord2_wifi_scan_cancel(uint64_t t){assert(t==ticket);canceled=true;return 0;}
extern "C" int nord2_wifi_scan_view(uint64_t t,nss_view*out){assert(t==ticket);if(canceled)return 1;*out={&scan,400,gen,NSS_COMPLETE};return 0;}
extern "C" int nord2_wifi_scan_release(uint64_t t){assert(t==ticket);releases++;return 0;}
static void put(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static IOReturn call(IOUserClient*c,unsigned s,IOExternalMethodArguments&a){a.selector=s;return c->externalMethod(s,&a,nullptr,nullptr,nullptr);}
static void envelope(uint8_t*p,uint32_t op,uint32_t req,uint32_t bytes){memset(p,0,NIW_SMALL_BYTES);put(p,op);put(p+4,bytes);memcpy(p+15368,"en0",4);put(p+15384,req);}
static void request(uint8_t*p){envelope(p,0x802869c8,10,5456);p+=8;put(p,1);put(p+4,3);put(p+52,1);put(p+56,1);put(p+68,13);put(p+4872,0x20);put(p+4880,0x80000000u);for(unsigned i=0;i<13;i++){put(p+72+12*i,1);put(p+76+12*i,i+1);put(p+80+12*i,10);}}
static size_t ie(uint8_t*p,unsigned id,const uint8_t*v,size_t n){p[0]=id;p[1]=(uint8_t)n;memcpy(p+2,v,n);return n+2;}
static void add(unsigned hw,unsigned ds,unsigned identity,bool rxv,unsigned rate,size_t extra){
 uint8_t p[3840]={0},ssid[3]={1,2,3},rates[5]={2,4,0xff,0xfe,(uint8_t)rate},v[250]={0,16,24},ch=(uint8_t)ds;size_t m=rxv?32:24,at=m+36;
 p[2]=1;p[3]=0x38;put(p+4,rxv?4u<<11:0);put(p+8,12u<<8);p[13]=hw;if(rxv){p[28]=100;p[29]=90;}p[m]=0x80;p[m+10]=2;p[m+15]=identity;p[m+16]=2;p[m+21]=identity;p[m+32]=100;p[m+34]=1;
 at+=ie(p+at,0,ssid,3);at+=ie(p+at,1,rates,5);at+=ie(p+at,3,&ch,1);
 while(extra){size_t n=extra>252?250:extra-2;if(extra==253)n=248;at+=ie(p+at,221,v,n);extra-=n+2;}
 p[0]=(uint8_t)at;p[1]=(uint8_t)(at>>8);assert(!nsb_add(&scan,p,at,110+scan.frames*10));
}
static void fill(){scan={};assert(!nsb_begin(&scan,1,100,1000000));add(1,1,1,true,0xfb,0);add(6,6,2,true,0xf9,0);add(5,6,3,true,0xfb,0);add(6,6,4,true,0xfb,685);add(11,11,5,true,0xfb,1085);add(3,3,6,false,0xfb,0);add(13,13,7,true,0xfb,0);uint8_t done[476]={1};done[4]=13;done[5]=7;done[6]=2;assert(!nsb_complete(&scan,done,sizeof done,200)&&scan.count==7&&scan.frames==7);}
int main(){test_current=&ownerTask;NiwlLifecycleService*p=new NiwlLifecycleService;IOUserClient*c=nullptr;assert(!p->newUserClient(&ownerTask,nullptr,0,nullptr,&c));p->release();IOExternalMethodArguments a={};uint8_t one=0;a.structureOutput=&one;a.structureOutputSize=1;assert(!call(c,1,a)&&one==1);a.structureOutputSize=1;assert(!call(c,5,a)&&one==1);a={};assert(!call(c,3,a));OSAsyncReference64 ref={1};rights++;a={};a.asyncReference=ref;a.asyncReferenceCount=8;a.asyncWakePort=1;assert(!call(c,2,a));
 IOOptionBits opt;IOMemoryDescriptor*d,*s;assert(!c->clientMemoryForType(1,&opt,&d));assert(!c->clientMemoryForType(2,&opt,&s));auto*ring=(uint8_t*)static_cast<IOBufferMemoryDescriptor*>(d)->getBytesNoCopy();auto*state=(uint32_t*)static_cast<IOBufferMemoryDescriptor*>(s)->getBytesNoCopy();
 static uint8_t carrier[NIW_SMALL_BYTES];request(carrier);a={};a.structureInput=carrier;a.structureInputSize=sizeof carrier;
 /* Bad member refuses without enqueue; restore the exact explicit13 profile. */
 put(carrier+8+76+12*12,165);assert(call(c,0,a)==kIOReturnUnsupported&&!enqueues);request(carrier);assert(!call(c,0,a)&&active&&enqueues==1);assert(call(c,0,a)==(IOReturn)16&&enqueues==1);
 fill();callback(cookie,gen,ticket,NSS_COMPLETE);active=false;assert(releases==1&&notifications==1&&state[1]==52&&ring[0]==87&&ring[28]==10&&ring[48]==0);
 uint8_t row[1164];const unsigned expected[3]={1,6,13},expected_ies[3]={15,700,15};
 envelope(carrier,NIW_GET,11,1164);a.structureOutput=row;
 for(unsigned i=0;i<3;i++){a.structureOutputSize=sizeof row;assert(!call(c,0,a));niws_result r;assert(!niws_decode_result(row,sizeof row,&r)&&r.channel==expected[i]&&r.rate_count==2&&r.rates[0]==1&&r.rates[1]==2&&r.rssi==-60&&r.ie_bytes==expected_ies[i]);unsigned found=0;for(unsigned j=0;j<r.ie_bytes;j++)if(r.ie[j]==0xff||r.ie[j]==0xfe||r.ie[j]==0xfb)found++;assert(found>=3);}
 a.structureOutputSize=sizeof row;assert(call(c,0,a)==(IOReturn)0xe0820446);a.structureOutputSize=sizeof row;assert(call(c,0,a)==(IOReturn)0xe0820446);
 assert(!c->clientClose());c->release();d->release();s->release();assert(!rights&&!locks&&!malloc_count&&!buffer_count&&ownerTask.refs==1);
 puts("PASS targeted actual IOUC composition: owner22/explicit13 flags10/LOW_LATENCY/defaultRSSI, queued subset exact, mixed7=>3 rows/4 omissions, event10, GET11 order+EOF+cleanup; backend/IOKit mocked, synthetic RXD only");}
