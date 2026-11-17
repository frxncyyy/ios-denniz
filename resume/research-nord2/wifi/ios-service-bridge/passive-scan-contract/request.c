#include "request.h"
static uint16_t g16(const uint8_t*p){return p[0]|(uint16_t)p[1]<<8;}
static uint32_t g32(const uint8_t*p){return g16(p)|(uint32_t)g16(p+2)<<16;}
int niwp_request_decode(const uint8_t*p,size_t n,struct niwp_request*out){
 struct niwp_request r={0};
 if(!p||!out||n!=NIWP_BYTES||g32(p)!=1||g32(p+16)>32||g32(p+68)>NIWP_CHANNELS)return -1;
 r.bss_type=g32(p+4);r.ssid_bytes=g32(p+16);
 for(unsigned i=0;i<6;i++)r.bssid[i]=p[8+i];
 for(unsigned i=0;i<r.ssid_bytes;i++)r.ssid[i]=p[20+i];
 r.scan_type=g32(p+52);r.phy_mode=g32(p+56);r.dwell_time=g16(p+60);
 r.rest_time=g32(p+64);r.channel_count=g32(p+68);
 for(unsigned i=0;i<r.channel_count;i++){
  const uint8_t*c=p+72+12*i;if(g32(c)!=1)return -1;
  r.channels[i].number=g32(c+4);r.channels[i].flags=g32(c+8);
 }
 r.flags=g32(p+4872)|(uint64_t)g32(p+4876)<<32;
 r.rssi_threshold=(int32_t)g32(p+4880);*out=r;return 0;
}
