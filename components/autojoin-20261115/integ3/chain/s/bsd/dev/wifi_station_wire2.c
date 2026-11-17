#include <arm64/sweet_early.h>
#if defined(NORD2)
#include "wifi_station_wire2.h"
#include <string.h>
static void w2_le16(uint8_t *p,uint16_t v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static int w2_unicast(const uint8_t p[6]) {
    unsigned i; uint8_t v=0;
    if(p[0]&1)return 0;
    for(i=0;i<6;i++)v|=p[i];
    return v!=0;
}
static int w2_peer_ok(const struct sw_peer *p) {
    return p&&w2_unicast(p->self)&&w2_unicast(p->ap)&&memcmp(p->self,p->ap,6);
}
static void w2_header(uint8_t *b,uint16_t fc,const struct sw_peer *p) {
    w2_le16(b,fc);memcpy(b+4,p->ap,6);memcpy(b+10,p->self,6);memcpy(b+16,p->ap,6);
}
static int w2_finish(uint8_t *out,size_t cap,size_t *used,const uint8_t *b,size_t n) {
    if(!out)return SW_INVALID;
    if(cap<n)return SW_CAPACITY;
    memcpy(out,b,n);*used=n;return SW_OK;
}
int sw_auth_sae(uint8_t *out,size_t cap,size_t *used,const struct sw_peer *p,unsigned transaction,unsigned status,const uint8_t *body,size_t n) {
    uint8_t b[30+SW_SAE_BODY_MAX]={0};
    if(!used)return SW_INVALID;
    *used=0;
    if(!w2_peer_ok(p)||!body||n<34||n>SW_SAE_BODY_MAX||transaction<1||transaction>2||
       (status&&(status!=126||transaction!=1)))return SW_INVALID;
    w2_header(b,0xb0,p);w2_le16(b+24,3);w2_le16(b+26,(uint16_t)transaction);w2_le16(b+28,(uint16_t)status);memcpy(b+30,body,n);
    return w2_finish(out,cap,used,b,30+n);
}
static int w2_rate_ok(uint8_t r) {
    switch(r&127) {case 2:case 4:case 11:case 22:case 12:case 18:case 24:
    case 36:case 48:case 72:case 96:case 108:return 1;default:return 0;}
}
int sw_assoc_rsn(uint8_t *out,size_t cap,size_t *used,const struct sw_assoc *p,const uint8_t *rsn,size_t rsn_len,const uint8_t *rsnxe,size_t rsnxe_len) {
    uint8_t b[144]={0};size_t i,j,n=28,first;
    if(!used)return SW_INVALID;
    *used=0;
    if(!p||!w2_peer_ok(&p->peer)||!p->ssid||!p->ssid_len||
       p->ssid_len>32||!p->rates||!p->rates_len||p->rates_len>12||
       !p->listen_interval||p->security!=SW_SECURITY_WPA2||
       (p->capability&0x11)!=0x11u||(p->capability&~0x0431u)||
       !rsn||(rsn_len!=22&&rsn_len!=28)||rsn[0]!=48||rsn[1]!=rsn_len-2||
       (rsnxe_len&&(!rsnxe||rsnxe_len!=3||rsnxe[0]!=244||rsnxe[1]!=1)))return SW_INVALID;
    for(i=0;i<p->rates_len;i++) {
        if(!w2_rate_ok(p->rates[i]))return SW_INVALID;
        for(j=0;j<i;j++)if((p->rates[i]&127)==(p->rates[j]&127))return SW_INVALID;
    }
    w2_header(b,0,&p->peer);w2_le16(b+24,p->capability);w2_le16(b+26,p->listen_interval);
    b[n++]=0;b[n++]=(uint8_t)p->ssid_len;memcpy(b+n,p->ssid,p->ssid_len);n+=p->ssid_len;
    first=p->rates_len<8?p->rates_len:8;
    b[n++]=1;b[n++]=(uint8_t)first;memcpy(b+n,p->rates,first);n+=first;
    if(p->rates_len>8) {b[n++]=50;b[n++]=(uint8_t)(p->rates_len-8);
        memcpy(b+n,p->rates+8,p->rates_len-8);n+=p->rates_len-8;}
    memcpy(b+n,rsn,rsn_len);n+=rsn_len;
    if(rsnxe_len){memcpy(b+n,rsnxe,3);n+=3;}
    return w2_finish(out,cap,used,b,n);
}

#endif /* NORD2 */
