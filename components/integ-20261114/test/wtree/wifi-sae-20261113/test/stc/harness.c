/* 0433 host harness: the real station transaction (wifi_station_transaction.c with the CI engine wifi_wpa2.c and
 * the new wifi_wpa3.c, wifi_sae.c, wifi_sae_crypto.c, wifi_station_wire2.c) driven over a line protocol by
 * test/stc/run.py, which plays the access point with an independent implementation.
 * argv: allow pmf_flags password ssid pmk_hex ap_rsn_hex ap_ies_hex   (allow 0 = no secret record: legacy profile)
 * stdin:  R <0|1> receipt for the pending action | G grant | M <hex> management frame | E <hex> EAPOL
 *         K key event | T <ms> advance time and tick | C cancel | L link lost | N begin a new transaction
 * stdout: "A <kind> <hex>" for each newly pending action, "S <phase> <outcome> <authenticated> <associated>"
 *         after every command; engine printf lines pass through. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wifi_station_transaction.h"
static unsigned rng_calls;
static int rng(void*c,uint8_t*out,size_t n){(void)c;if(n!=32)return -1;rng_calls++;for(unsigned i=0;i<32;i++)out[i]=(uint8_t)(0x31*rng_calls+7*i+rng_calls*i);out[0]&=0x7f;return 0;}
static size_t unhex(const char*s,uint8_t*out,size_t cap){size_t n=0;while(s[0]&&s[1]&&s[0]!='\n'&&n<cap){unsigned v;if(sscanf(s,"%2x",&v)!=1)break;out[n++]=(uint8_t)v;s+=2;}return n;}
static uint64_t shown;
static void show(struct stc*s){
 const struct stc_action*a=stc_next(s);
 if(a&&a->ticket!=shown){shown=a->ticket;printf("A %u ",a->kind);for(unsigned i=0;i<a->bytes;i++)printf("%02x",a->payload[i]);printf("\n");}
 printf("S %u %u %u %u\n",s->phase,s->outcome,s->authenticated,s->associated);fflush(stdout);
}
static unsigned secret_allow,secret_flags;static const char*secret_password;
static int secret(const uint8_t*ssid,size_t n,struct stc_secret*out){(void)ssid;(void)n;if(!secret_allow)return 0;memset(out,0,sizeof *out);out->allow=secret_allow;out->pmf_flags=secret_flags;out->bytes=(unsigned)strlen(secret_password);memcpy(out->password,secret_password,out->bytes);return 1;}
int main(int argc,char**argv){
 if(argc!=8)return 2;
 static struct stc s;static struct stc_arbiter arbiter;struct stc_config c={0};
 static uint8_t pmk[32],ap_rsn[257],ap_ies[1024],line[4096],frame[2048];
 static const uint8_t self[6]={0x02,0x11,0x22,0x33,0x44,0x55},ap[6]={0x00,0xaa,0xbb,0xcc,0xdd,0xee},rates[4]={0x82,0x84,0x8b,0x96};
 static const uint8_t legacy[22]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
 secret_allow=(unsigned)strtoul(argv[1],0,0);secret_flags=(unsigned)strtoul(argv[2],0,0);secret_password=argv[3];stc_secret_hook=secret;
 uint64_t now=1000;
 c.generation=7;c.deadline_ms=now+8000;c.cleanup_timeout_ms=30000;c.security=SW_SECURITY_WPA2;
 memcpy(c.assoc.peer.self,self,6);memcpy(c.assoc.peer.ap,ap,6);c.assoc.ssid=(const uint8_t*)argv[4];c.assoc.ssid_len=strlen(argv[4]);c.assoc.rates=rates;c.assoc.rates_len=4;
 c.assoc.capability=0x11;c.assoc.listen_interval=10;c.assoc.security=SW_SECURITY_WPA2;
 c.channel=(struct sw_channel){.bss=0,.token=7,.band=1,.channel=6,.dbdc_band=4,.interval_ms=4000};
 c.group_spare[0]=6;c.group_spare[1]=7;c.group_spare[2]=8;c.sta_wlan=5;c.group_wlan=4;
 unhex(argv[5],pmk,32);c.ap_rsn_bytes=unhex(argv[6],ap_rsn,sizeof ap_rsn);c.ap_rsn=ap_rsn;c.ap_ies_bytes=unhex(argv[7],ap_ies,sizeof ap_ies);c.ap_ies=ap_ies;
 c.rng=rng;c.pmk=pmk;c.station_rsn=legacy;
 int r=stc_begin(&s,&arbiter,&c,now);printf("B %d\n",r);if(r!=1){fflush(stdout);return 0;}
 show(&s);
 while(fgets((char*)line,sizeof line,stdin)){
  const struct stc_action*a=stc_next(&s);int v=0;
  switch(line[0]){
  case 'R':if(a)v=stc_receipt(&s,a->generation,a->ticket,line[2]=='1',now);break;
  case 'G':v=stc_grant(&s,7,0,7,1,6,1,now);break;
  case 'M':{size_t n=unhex((char*)line+2,frame,sizeof frame);v=stc_management(&s,7,frame,n,now);break;}
  case 'E':{size_t n=unhex((char*)line+2,frame,sizeof frame);v=stc_eapol(&s,7,frame,n,now);break;}
  case 'K':{uint8_t k[8]={0};memcpy(k+2,ap,6);v=stc_key_event(&s,7,k,8,now);break;}
  case 'T':now+=(uint64_t)atoi((char*)line+2);v=stc_tick(&s,now);break;
  case 'C':v=stc_cancel(&s,7,now);break;
  case 'L':v=stc_link_lost(&s,7,now);break;
  case 'N':memset(&s,0,sizeof s);memset(&arbiter,0,sizeof arbiter);shown=0;c.deadline_ms=now+8000;v=stc_begin(&s,&arbiter,&c,now);printf("B %d\n",v);break;
  default:return 0;
  }
  printf("V %d\n",v);show(&s);
 }
 return 0;
}
