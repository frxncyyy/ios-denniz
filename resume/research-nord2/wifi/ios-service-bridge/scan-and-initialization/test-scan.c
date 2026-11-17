#include "scan_contract.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void p32(uint8_t*p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=x>>(8*i);}
int main(void){
 struct niws_result r={0},q,old;uint8_t b[NIWS_SCAN_RESULT_BYTES],saved[sizeof b],req[NIWS_SCAN_REQUEST_BYTES]={0},event[44]={0};char name[16]="en0";
 r.channel=36;r.channel_flags=0x1234;r.rssi=-70;r.noise=-95;r.noise_valid=1;r.snr=25;r.beacon_interval=100;r.capabilities=0x411;r.rate_count=15;r.ssid_bytes=32;r.age=123;r.flags=0;
 for(unsigned i=0;i<32;i++)r.ssid[i]=(uint8_t)i;
 for(unsigned i=0;i<15;i++)r.rates[i]=i;
 r.ie_bytes=4;r.ie[0]=0;r.ie[1]=2;r.ie[2]=1;r.ie[3]=0;
 assert(!niws_encode_result(&r,b,sizeof b)&&!niws_decode_result(b,sizeof b,&q));assert(!memcmp(&r,&q,sizeof r));old=q;memcpy(saved,b,sizeof b);
 for(size_t n=0;n<sizeof b;n++)assert(niws_decode_result(b,n,&q)<0&&!memcmp(&q,&old,sizeof q));
 b[96]=33;assert(niws_decode_result(b,sizeof b,&q)<0);memcpy(b,saved,sizeof b);b[34]=16;assert(niws_decode_result(b,sizeof b,&q)<0);memcpy(b,saved,sizeof b);b[141]=5;assert(niws_decode_result(b,sizeof b,&q)<0);memcpy(b,saved,sizeof b);b[0]=2;assert(niws_decode_result(b,sizeof b,&q)<0);
 r.ie_bytes=1025;memcpy(b,saved,sizeof b);assert(niws_encode_result(&r,b,sizeof b)<0&&!memcmp(b,saved,sizeof b));r.ie_bytes=4;
 uint32_t type=0;p32(req,1);p32(req+4,3);assert(!niws_scan_request_header(req,sizeof req,&type)&&type==3);req[0]=2;assert(niws_scan_request_header(req,sizeof req,&type)<0);
 struct niws_scan_done d;p32(event,44);p32(event+20,10);memcpy(event+24,name,16);assert(!niws_decode_scan_done(event,44,name,&d)&&d.status==0);event[20]=9;assert(niws_decode_scan_done(event,44,name,&d)<0);event[20]=10;event[24]='x';assert(niws_decode_scan_done(event,44,name,&d)<0);
 uint8_t pipe[52]={0};p32(pipe,87);pipe[24]=4;p32(pipe+28,10);memcpy(pipe+32,name,16);
 assert(!niws_decode_pipe_scan_done(pipe,sizeof pipe,name,&d)&&d.status==0);pipe[26]=1;assert(niws_decode_pipe_scan_done(pipe,sizeof pipe,name,&d)<0);pipe[26]=0;pipe[24]=8;assert(niws_decode_pipe_scan_done(pipe,sizeof pipe,name,&d)<0);
 uint8_t envelope[15408]={0};p32(envelope,0x802869c8);p32(envelope+4,5456);p32(envelope+8,1);p32(envelope+12,3);memcpy(envelope+15368,name,16);p32(envelope+15384,10);
 assert(!niws_scan_envelope(0,envelope,sizeof envelope,name,&type)&&type==3);envelope[15384]=11;assert(niws_scan_envelope(0,envelope,sizeof envelope,name,&type)<0);
 puts("PASS pure version1 scan result roundtrip/bounds/truncation/IE validation; request subset and scan_done classification only");
}
