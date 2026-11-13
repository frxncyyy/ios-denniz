/* 0432 known-answer tests: IEEE Std 802.11-2020 Annex J.10 (as carried by hostap common_module_tests.c),
 * FIPS 180-4 / RFC 4231 / RFC 4493 / RFC 3394 primitives. */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "wifi_sae.h"
#include "wifi_sae_crypto.h"
static const uint8_t *feed[4];static unsigned fed;
static int rng(void*c,uint8_t*out,size_t n){(void)c;assert(n==32&&fed<4&&feed[fed]);memcpy(out,feed[fed++],32);return 0;}
static void hex(const char*s,uint8_t*out,size_t n){for(size_t i=0;i<n;i++){unsigned v;assert(sscanf(s+2*i,"%2x",&v)==1);out[i]=(uint8_t)v;}}
#define CHECK(c) do{if(!(c)){printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);return 1;}}while(0)
int main(void){
 /* HMAC-SHA256 RFC 4231 cases 2 and 3 and a two-block message; the KDF through Annex J below; CMAC RFC 4493 examples 1-4 */
 {uint8_t d[32],w[32],key[20],data[50];
  const uint8_t*p[1]={(const uint8_t*)"what do ya want for nothing?"};size_t n[1]={28};CHECK(!nsc_hmac256((const uint8_t*)"Jefe",4,p,n,1,d));hex("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",w,32);CHECK(!memcmp(d,w,32));
  memset(key,0xaa,20);memset(data,0xdd,50);const uint8_t*q[2]={data,data+17};size_t m[2]={17,33};CHECK(!nsc_hmac256(key,20,q,m,2,d));hex("773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe",w,32);CHECK(!memcmp(d,w,32));
  static const char*two="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopqabcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";const uint8_t*r[1]={(const uint8_t*)two};size_t z[1]={112};
  CHECK(!nsc_hmac256((const uint8_t*)"k",1,r,z,1,d));hex("0103948347a8f5bca73e18f04b5f12d00a4aa75608b3eb6f5c51ca19c26b2a75",w,32);CHECK(!memcmp(d,w,32));} /* value from Python hmac/hashlib */
 {uint8_t k[16],m[64],t[16],w[16];hex("2b7e151628aed2a6abf7158809cf4f3c",k,16);hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710",m,64);
  static const char*want[4]={"bb1d6929e95937287fa37d129b756746","070a16b46b4d4144f79bdd9dd04a287c","dfa66747de9ae63030ca32611497c827","51f0bebf7e3b9d92fc49741779363cfe"};static const size_t len[4]={0,16,40,64};
  for(unsigned i=0;i<4;i++){const uint8_t*p[2]={m,m+len[i]/2};size_t n[2]={len[i]/2,len[i]-len[i]/2};CHECK(!nsc_cmac(k,p,n,2,t));hex(want[i],w,16);CHECK(!memcmp(t,w,16));}}
 /* Annex J.10: hunting and pecking, commit, KCK, PMK, PMKID */
 static const uint8_t addr1[6]={0x4d,0x3f,0x2f,0xff,0xe3,0x87},addr2[6]={0xa5,0xd8,0xaa,0x95,0x8e,0x3c};
 uint8_t rand[32],mask[32],local[98],peer[98],kck[32],pmk[32],pmkid[16],out[400];size_t used=0;
 hex("992465fd3daa3c60aa6565b7f62a2a7f2e12dd12f198faf4fbed89d7ff1ace94",rand,32);hex("9507a90f777a044d6a0830b91ea3d5dd70bece44e1acffb86983b5e1bf9fb322",mask,32);
 hex("13002e2c0f0db52440ad146d967114ce005ce1eab0aa2c2e5c2871b774f6c2575c65d5ad9e00829707aa36ba8b859738fc961d08243505f47c035376d7ac4bc8d7b95083bf43827d0fc31ed778dd3671fd21a46d1091d64b6f9a1e127262"
     "1325dbe1",local,98);
 hex("1300591b96f3397fb945100848e7b550543b6720d88337ee93fc49fd6df7e08b5223e71b9bb048d3873f20556953a96c91536fd8ee6ca9b4a68a148b056a909be03e83ae208f60f8ef5537858074db0668703239986299"
     "9b511e0a1552a5fea317c2",peer,98);
 hex("1e733f6d9bd53256287304338831b09a39406d121017073a5c30db36f36cb81a",kck,32);hex("4e4dfab1a2dd8ac1a91790f953faaa452ae5c6873ab75b63605ba663f8a7fe59",pmk,32);hex("8747a600eea3f9f22475df58ca1e5498",pmkid,16);
 struct nsae s;feed[0]=rand;feed[1]=mask;fed=0;
 /* The engine refuses multicast addresses (addr2 a5:.. has the group bit, addr1 4d:.. too): the vector is run on the building blocks. */
 {uint8_t pwe[64];CHECK(!nsae_pwe_loop(addr1,addr2,(const uint8_t*)"mekmitasdigoat",14,pwe));
  memset(&s,0,sizeof s);memcpy(s.pwe,pwe,64);memcpy(s.rand,rand,32);
  /* scalar = (rand+mask) mod r ; element = -(mask*PWE) */
  uint8_t e[64];CHECK(!nsae_scalar_mult(pwe,mask,e));
  static const uint8_t p256[32]={0xff,0xff,0xff,0xff,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff};
  unsigned borrow=0;for(int i=31;i>=0;i--){int v=p256[i]-e[32+i]-(int)borrow;borrow=v<0;e[32+i]=(uint8_t)v;}
  CHECK(!memcmp(e,local+34,64));
  static const uint8_t order[32]={0xff,0xff,0xff,0xff,0,0,0,0,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51};
  uint8_t sum[33]={0};unsigned c=0;for(int i=31;i>=0;i--){unsigned v=rand[i]+mask[i]+c;sum[i+1]=(uint8_t)v;c=v>>8;}sum[0]=(uint8_t)c;
  int ge=sum[0]||memcmp(sum+1,order,32)>=0;if(ge){borrow=0;for(int i=31;i>=0;i--){int v=sum[i+1]-order[i]-(int)borrow;borrow=v<0;sum[i+1]=(uint8_t)v;}}
  CHECK(!memcmp(sum+1,local+2,32));
  memcpy(s.scalar,sum+1,32);memcpy(s.element,e,64);memcpy(s.own,addr1,6);memcpy(s.peer,addr2,6);s.state=NSAE_READY;
  CHECK(!nsae_commit(&s,out,sizeof out,&used)&&used==98&&!memcmp(out,local,98));
  CHECK(!nsae_peer_commit(&s,peer,98));CHECK(!memcmp(s.kck,kck,32));CHECK(!memcmp(s.pmk,pmk,32));CHECK(!memcmp(s.pmkid,pmkid,16));
  printf("PASS Annex J.10 hunting-and-pecking: commit scalar/element, KCK, PMK, PMKID\n");}
 /* Annex J.10 hash-to-element: PT(ssid byteme, password, identifier psk4internet) -> PWE for the pair 00:09:5b:66:ec:1e / 00:0b:6b:d9:02:46 */
 {static const uint8_t a[6]={0x00,0x09,0x5b,0x66,0xec,0x1e},b[6]={0x00,0x0b,0x6b,0xd9,0x02,0x46};uint8_t pt[64],pwe[64],want[64];
  hex("c93049b9e64000f848201649e999f2b5c22dea69b5632c9df4d633b8aa1f6c1e73634e94b53d82e7383a8d258199d9dc1a5ee8269d060382ccbf33e614ff59a0",want,64);
  CHECK(!nsae_pt((const uint8_t*)"byteme",6,(const uint8_t*)"mekmitasdigoat",14,(const uint8_t*)"psk4internet",12,pt));
  CHECK(!nsae_pwe_pt(pt,a,b,pwe));CHECK(!memcmp(pwe,want,64));
  printf("PASS Annex J.10 hash-to-element: PT -> PWE (group 19)\n");}
 /* two engines against each other, both methods, with token round and reflection/garbage refusals */
 for(unsigned h2e=0;h2e<2;h2e++){
  static const uint8_t sta[6]={0x02,0x11,0x22,0x33,0x44,0x55},ap[6]={0x00,0xaa,0xbb,0xcc,0xdd,0xee};
  uint8_t r1[32],r2[32],r3[32],r4[32];memset(r1,0x11,32);memset(r2,0x22,32);memset(r3,0x33,32);memset(r4,0x44,32);
  struct nsae x,y;uint8_t cx[400],cy[400],fx[40],fy[40];size_t nx,ny,u;
  feed[0]=r1;feed[1]=r2;fed=0;CHECK(!nsae_begin(&x,sta,ap,(const uint8_t*)"correct horse",13,(const uint8_t*)"net",3,h2e,rng,0));
  feed[0]=r3;feed[1]=r4;fed=0;CHECK(!nsae_begin(&y,ap,sta,(const uint8_t*)"correct horse",13,(const uint8_t*)"net",3,h2e,rng,0));
  CHECK(!nsae_commit(&x,cx,sizeof cx,&nx)&&nx==98);
  uint8_t req[40]={19,0};size_t rn=2;if(h2e){req[2]=255;req[3]=33;req[4]=93;rn=5;}memset(req+rn,0x5a,32);rn+=32;
  CHECK(!nsae_token(&x,req,rn));CHECK(!nsae_commit(&x,cx,sizeof cx,&nx)&&nx==(h2e?98u+35:98u+32));
  if(h2e){CHECK(cx[98]==255&&cx[99]==33&&cx[100]==93&&cx[101]==0x5a);}else{CHECK(cx[2]==0x5a&&cx[33]==0x5a);memmove(cx+2,cx+34,96);}
  CHECK(!nsae_commit(&y,cy,sizeof cy,&ny)&&ny==98);
  {struct nsae z=x;CHECK(nsae_peer_commit(&z,cx,98)<0&&z.state==NSAE_OFF);} /* reflection */
  {struct nsae z=x;uint8_t bad[98];memcpy(bad,cy,98);bad[97]^=1;CHECK(nsae_peer_commit(&z,bad,98)<0);} /* off curve */
  {struct nsae z=x;uint8_t bad[98];memcpy(bad,cy,98);bad[0]=20;CHECK(nsae_peer_commit(&z,bad,98)<0);} /* group */
  {struct nsae z=x;uint8_t bad[98];memcpy(bad,cy,98);memset(bad+2,0,32);bad[33]=1;CHECK(nsae_peer_commit(&z,bad,98)<0);} /* scalar 1 */
  CHECK(!nsae_peer_commit(&x,cy,ny));CHECK(!nsae_peer_commit(&y,cx,98));
  CHECK(!memcmp(x.pmk,y.pmk,32)&&!memcmp(x.pmkid,y.pmkid,16)&&!memcmp(x.kck,y.kck,32));
  CHECK(!nsae_confirm(&x,fx,sizeof fx,&u)&&u==34&&fx[0]==1);CHECK(!nsae_confirm(&y,fy,sizeof fy,&u)&&u==34);
  {struct nsae z=x;uint8_t bad[34];memcpy(bad,fy,34);bad[10]^=4;CHECK(nsae_peer_confirm(&z,bad,34)<0&&z.state==NSAE_OFF);}
  CHECK(!nsae_peer_confirm(&x,fy,34)&&x.state==NSAE_ACCEPTED);CHECK(!nsae_peer_confirm(&y,fx,34)&&y.state==NSAE_ACCEPTED);
  /* wrong password: confirm must fail */
  struct nsae w;feed[0]=r3;feed[1]=r4;fed=0;CHECK(!nsae_begin(&w,ap,sta,(const uint8_t*)"wrong horse",11,(const uint8_t*)"net",3,h2e,rng,0));
  feed[0]=r1;feed[1]=r2;fed=0;CHECK(!nsae_begin(&x,sta,ap,(const uint8_t*)"correct horse",13,(const uint8_t*)"net",3,h2e,rng,0));
  CHECK(!nsae_commit(&x,cx,sizeof cx,&nx)&&!nsae_commit(&w,cy,sizeof cy,&ny)&&!nsae_peer_commit(&x,cy,ny)&&!nsae_peer_commit(&w,cx,nx));
  CHECK(!nsae_confirm(&w,fy,sizeof fy,&u));CHECK(nsae_peer_confirm(&x,fy,34)<0);
  printf("PASS engine pair h2e=%u: token round, refusals, PMK/PMKID agreement, confirm, wrong password\n",h2e);
 }
 printf("PASS wifi_sae known-answer tests\n");return 0;
}
