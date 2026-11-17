#include "capability.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void put16(uint8_t*p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static size_t tlv(uint8_t*f,size_t at,uint32_t tag,unsigned n){put32(f+at,tag);put32(f+at+4,n);return at+8;}
static size_t fixture(uint8_t*f,uint8_t seq){memset(f,0,3840);f[27]=0xe0;f[28]=0xec;f[29]=seq;put16(f+36,6);size_t at=40,b;
 b=tlv(f,at,0,28);put32(f+b,0x80000001u);for(unsigned i=0;i<5;i++)put32(f+b+4+4*i,100+i);f[b+24]=2;at=b+28;
 b=tlv(f,at,5,20);put16(f+b,0x6635);f[b+2]=3;put32(f+b+4,0x11223344);at=b+20;
 b=tlv(f,at,6,24);put16(f+b,0x1234);put16(f+b+2,7);memcpy(f+b+4,"TEST",4);at=b+24;
 b=tlv(f,at,7,8);for(unsigned i=0;i<6;i++)f[b+i]=(uint8_t)(i*2);at=b+8;
 b=tlv(f,at,8,12);f[b]=1;f[b+4]=2;at=b+12;
 b=tlv(f,at,0xfeed,3);f[b]=1;at=b+3;return at;}
int main(void){uint8_t f[3840],copy[3840],cmd[68];struct ncp_result r,old;size_t n=fixture(f,17);
 for(unsigned seq=0;seq<256;seq++){memset(cmd,0xcc,sizeof(cmd));assert(!ncp_query(cmd,64,(uint8_t)seq));for(unsigned i=0;i<64;i++){unsigned want=0;if(i==0)want=64;if(i==3)want=1;if(i==6)want=1;if(i==32)want=32;if(i==36)want=0x8a;if(i==37)want=0xa0;if(i==39)want=seq;assert(cmd[i]==want);}assert(cmd[64]==0xcc);}
 assert(ncp_query(cmd,63,0));assert(!ncp_parse(f,n,17,&r)&&r.decoded&&r.elements==6&&r.known==5&&r.unknown==1&&r.mac_valid&&r.product==0x6635&&r.resources_supported&&r.resources[5]==2&&r.trailing==0);
 old=r;for(size_t bytes=0;bytes<n;bytes++){r=old;assert(ncp_parse(f,bytes,17,&r)<0&&!memcmp(&r,&old,sizeof(r)));}
 assert(ncp_parse(f,n,18,&r));memcpy(copy,f,n);copy[28]=1;assert(ncp_parse(copy,n,17,&r));
 memcpy(copy,f,n);put32(copy+44,0xffffffffu);assert(ncp_parse(copy,n,17,&r));
 memcpy(copy,f,n);put16(copy+36,0xffff);assert(ncp_parse(copy,n,17,&r));
 memset(f,0,sizeof(f));f[27]=0xe0;f[28]=0xec;put16(f+36,2);size_t b=tlv(f,40,7,8);f[b]=2;b=tlv(f,b+8,7,8);assert(ncp_parse(f,b+8,0,&r));
 put16(f+36,1);b=tlv(f,40,7,8);memset(f+b,0,8);assert(!ncp_parse(f,b+8,0,&r)&&r.mac_present&&!r.mac_valid);f[b]=1;assert(!ncp_parse(f,b+8,0,&r)&&!r.mac_valid);
 b=tlv(f,40,0,16);put32(f+b,12);put32(f+b+4,128);put32(f+b+8,32);put32(f+b+12,64);assert(!ncp_parse(f,b+16,0,&r)&&r.resources_supported&&r.resource_version==0&&r.resources[3]==64);
 b=tlv(f,40,0,4);put32(f+b,0x80000009);assert(!ncp_parse(f,b+4,0,&r)&&!r.resources_supported&&r.resource_version==0x80000009);
 memset(f+b+4,0xaa,4);assert(!ncp_parse(f,b+8,0,&r)&&r.trailing==4);
 assert(ncp_parse(f,3841,0,&r));
 printf("PASS capability: 256 exact frames, all %zu event truncations, output atomicity, TLV overflow/count bounds, duplicate known tags, invalid MAC flags, legacy/V1/unknown resources, captured counts and bounded trailing bytes. Pure synthetic bytes only.\n",n);
}
