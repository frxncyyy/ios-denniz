#include <arm64/sweet_early.h>
#if defined(NORD2)
/* 0432: SAE for group 19. Original code: a small fixed-width Montgomery
 * field (eight 32-bit limbs) for the P-256 prime and group order, Jacobian
 * point arithmetic, the two password element derivations and the commit /
 * confirm computations of IEEE 802.11-2020 12.4. Hashing comes from the
 * wifi_wpa_crypto provider. Secret-dependent work uses fixed iteration
 * counts and masked selection; it is not hardened beyond that. */
#include "wifi_sae.h"
#include "wifi_wpa_crypto.h"
#if defined(KERNEL) || defined(_KERNEL)
#include <sys/systm.h>
#else
#include <string.h>
#endif
typedef uint32_t fe[8];
struct mod {fe m;uint32_t k;fe r2,one;};
static const struct mod FP={{0xffffffff,0xffffffff,0xffffffff,0x00000000,0x00000000,0x00000000,0x00000001,0xffffffff},0x00000001,
 {0x00000003,0x00000000,0xffffffff,0xfffffffb,0xfffffffe,0xffffffff,0xfffffffd,0x00000004},
 {0x00000001,0x00000000,0x00000000,0xffffffff,0xffffffff,0xffffffff,0xfffffffe,0x00000000}};
static const struct mod FN={{0xfc632551,0xf3b9cac2,0xa7179e84,0xbce6faad,0xffffffff,0xffffffff,0x00000000,0xffffffff},0xee00bc4f,
 {0xbe79eea2,0x83244c95,0x49bd6fa6,0x4699799c,0x2b6bec59,0x2845b239,0xf3d95620,0x66e12d94},
 {0x039cdaaf,0x0c46353d,0x58e8617b,0x43190552,0x00000000,0x00000000,0xffffffff,0x00000000}};
static const fe CURVE_B={0x27d2604b,0x3bce3c3e,0xcc53b0f6,0x651d06b0,0x769886bc,0xb3ebbd55,0xaa3a93e7,0x5ac635d8};
static const fe SSWU_C1={0x2375f224,0xbdba1313,0x20693090,0x146fe6a0,0xd94959d3,0x6c65b982,0x96bdaf99,0xa528bd86}; /* b/(z*a), z=-10 */
static const fe SSWU_C2={0x629b756e,0x6944bebf,0x441be5a7,0xcc5f023b,0x7cdd823e,0x3bf93f1c,0xe368dbf8,0x73976747}; /* -b/a */
static const fe EXP_SQRT={0x00000000,0x00000000,0x40000000,0x00000000,0x00000000,0x40000000,0xc0000000,0x3fffffff}; /* (p+1)/4 */
static const fe EXP_INV={0xfffffffd,0xffffffff,0xffffffff,0x00000000,0x00000000,0x00000000,0x00000001,0xffffffff}; /* p-2 */
static const fe RAW_ONE={1,0,0,0,0,0,0,0};
static void wipe(void*v,size_t n){volatile uint8_t*p=v;while(n--)*p++=0;}
static uint32_t add_raw(fe r,const fe a,const fe b){uint64_t c=0;for(unsigned i=0;i<8;i++){c+=(uint64_t)a[i]+b[i];r[i]=(uint32_t)c;c>>=32;}return (uint32_t)c;}
static uint32_t sub_raw(fe r,const fe a,const fe b){uint64_t c=0;for(unsigned i=0;i<8;i++){c=(uint64_t)a[i]-b[i]-c;r[i]=(uint32_t)c;c=(c>>32)&1;}return (uint32_t)c;}
static void cmov(fe r,const fe a,uint32_t mask){for(unsigned i=0;i<8;i++)r[i]^=(r[i]^a[i])&mask;}
static void copy(fe r,const fe a){for(unsigned i=0;i<8;i++)r[i]=a[i];}
static uint32_t is_zero(const fe a){uint32_t d=0;for(unsigned i=0;i<8;i++)d|=a[i];return 0u-(uint32_t)(((uint64_t)d-1)>>63);} /* all ones when zero */
static uint32_t is_equal(const fe a,const fe b){uint32_t d=0;for(unsigned i=0;i<8;i++)d|=a[i]^b[i];return 0u-(uint32_t)(((uint64_t)d-1)>>63);}
static void load(fe r,const uint8_t*p){for(unsigned i=0;i<8;i++)r[7-i]=(uint32_t)p[4*i]<<24|(uint32_t)p[4*i+1]<<16|(uint32_t)p[4*i+2]<<8|p[4*i+3];}
static void store(uint8_t*p,const fe a){for(unsigned i=0;i<8;i++){uint32_t v=a[7-i];p[4*i]=(uint8_t)(v>>24);p[4*i+1]=(uint8_t)(v>>16);p[4*i+2]=(uint8_t)(v>>8);p[4*i+3]=(uint8_t)v;}}
/* r = a-m when a >= m (a < 2m). */
static void reduce_once(fe r,const fe a,const fe m){fe d;uint32_t borrow=sub_raw(d,a,m);copy(r,a);cmov(r,d,borrow-1);}
static void madd(fe r,const fe a,const fe b,const struct mod*M){fe t,d;uint32_t carry=add_raw(t,a,b),borrow=sub_raw(d,t,M->m);cmov(t,d,0u-(carry|(1u-borrow)));copy(r,t);}
static void msub(fe r,const fe a,const fe b,const struct mod*M){fe t,d;uint32_t borrow=sub_raw(t,a,b);add_raw(d,t,M->m);cmov(t,d,0u-borrow);copy(r,t);}
/* Montgomery product a*b/2^256 mod m; r may alias the inputs. */
static void mmul(fe r,const fe a,const fe b,const struct mod*M){
 uint32_t t[10]={0};fe d;
 for(unsigned i=0;i<8;i++){
  uint64_t c=0;
  for(unsigned j=0;j<8;j++){c+=(uint64_t)a[j]*b[i]+t[j];t[j]=(uint32_t)c;c>>=32;}
  c+=t[8];t[8]=(uint32_t)c;t[9]=(uint32_t)(c>>32);
  uint32_t q=t[0]*M->k;
  c=((uint64_t)q*M->m[0]+t[0])>>32;
  for(unsigned j=1;j<8;j++){c+=(uint64_t)q*M->m[j]+t[j];t[j-1]=(uint32_t)c;c>>=32;}
  c+=t[8];t[7]=(uint32_t)c;t[8]=t[9]+(uint32_t)(c>>32);
 }
 uint32_t borrow=sub_raw(d,t,M->m);cmov(t,d,0u-(t[8]|(1u-borrow)));copy(r,t);
}
static void to_mont(fe r,const fe a,const struct mod*M){mmul(r,a,M->r2,M);}
static void from_mont(fe r,const fe a,const struct mod*M){mmul(r,a,RAW_ONE,M);}
/* r = a^e, a and r in Montgomery form, fixed 256 squarings and products. */
static void mpow(fe r,const fe a,const fe e,const struct mod*M){
 fe x,t;copy(x,M->one);
 for(int i=255;i>=0;i--){mmul(x,x,x,M);mmul(t,x,a,M);cmov(x,t,0u-((e[i>>5]>>(i&31))&1u));}
 copy(r,x);
}
/* ---- curve: y^2 = x^3 - 3x + b, Jacobian coordinates in Montgomery form, z = 0 is infinity ---- */
struct pt {fe x,y,z;};
static void curve_g(fe r,const fe x){
 fe t,u,b;mmul(t,x,x,&FP);mmul(t,t,x,&FP);madd(u,x,x,&FP);madd(u,u,x,&FP);msub(t,t,u,&FP);to_mont(b,CURVE_B,&FP);madd(r,t,b,&FP);
}
/* y = sqrt(v) when v is a square; returns all ones when it is. */
static uint32_t field_sqrt(fe y,const fe v){fe t;mpow(y,v,EXP_SQRT,&FP);mmul(t,y,y,&FP);return is_equal(t,v);}
static void pt_dbl(struct pt*r,const struct pt*p){
 fe delta,gamma,beta,alpha,t,u;
 mmul(delta,p->z,p->z,&FP);mmul(gamma,p->y,p->y,&FP);mmul(beta,p->x,gamma,&FP);
 msub(t,p->x,delta,&FP);madd(u,p->x,delta,&FP);mmul(t,t,u,&FP);madd(alpha,t,t,&FP);madd(alpha,alpha,t,&FP);
 madd(t,p->y,p->z,&FP);mmul(t,t,t,&FP);msub(t,t,gamma,&FP);msub(r->z,t,delta,&FP);
 madd(beta,beta,beta,&FP);madd(beta,beta,beta,&FP); /* 4 beta */
 mmul(t,alpha,alpha,&FP);msub(t,t,beta,&FP);msub(r->x,t,beta,&FP);
 mmul(gamma,gamma,gamma,&FP);madd(gamma,gamma,gamma,&FP);madd(gamma,gamma,gamma,&FP);madd(gamma,gamma,gamma,&FP); /* 8 gamma^2 */
 msub(t,beta,r->x,&FP);mmul(t,t,alpha,&FP);msub(r->y,t,gamma,&FP);
}
static void pt_add(struct pt*r,const struct pt*p,const struct pt*q){
 fe z1z1,z2z2,u1,u2,s1,s2,h,rr,hh,hhh,v,t;struct pt o;
 uint32_t inf1=is_zero(p->z),inf2=is_zero(q->z);
 mmul(z1z1,p->z,p->z,&FP);mmul(z2z2,q->z,q->z,&FP);mmul(u1,p->x,z2z2,&FP);mmul(u2,q->x,z1z1,&FP);
 mmul(s1,p->y,q->z,&FP);mmul(s1,s1,z2z2,&FP);mmul(s2,q->y,p->z,&FP);mmul(s2,s2,z1z1,&FP);
 msub(h,u2,u1,&FP);msub(rr,s2,s1,&FP);
 if(!inf1&&!inf2&&is_zero(h)){ /* same x: doubling or the inverse; never reached with a scalar in 1..order-1 */
  if(is_zero(rr)){pt_dbl(r,p);return;}
  wipe(r,sizeof *r);return;
 }
 mmul(hh,h,h,&FP);mmul(hhh,hh,h,&FP);mmul(v,u1,hh,&FP);
 mmul(t,rr,rr,&FP);msub(t,t,hhh,&FP);msub(t,t,v,&FP);msub(o.x,t,v,&FP);
 msub(t,v,o.x,&FP);mmul(t,t,rr,&FP);mmul(s1,s1,hhh,&FP);msub(o.y,t,s1,&FP);
 mmul(t,p->z,q->z,&FP);mmul(o.z,t,h,&FP);
 cmov(o.x,q->x,inf1);cmov(o.y,q->y,inf1);cmov(o.z,q->z,inf1);
 cmov(o.x,p->x,inf2);cmov(o.y,p->y,inf2);cmov(o.z,p->z,inf2);
 *r=o;
}
static void pt_mul(struct pt*r,const struct pt*p,const fe k){
 struct pt acc,t;wipe(&acc,sizeof acc);
 for(int i=255;i>=0;i--){
  pt_dbl(&acc,&acc);pt_add(&t,&acc,p);
  uint32_t mask=0u-((k[i>>5]>>(i&31))&1u);cmov(acc.x,t.x,mask);cmov(acc.y,t.y,mask);cmov(acc.z,t.z,mask);
 }
 *r=acc;wipe(&acc,sizeof acc);wipe(&t,sizeof t);
}
static int pt_store(const struct pt*p,uint8_t out[64]){
 fe zi,z2,x,y;if(is_zero(p->z))return -1;
 mpow(zi,p->z,EXP_INV,&FP);mmul(z2,zi,zi,&FP);mmul(x,p->x,z2,&FP);mmul(z2,z2,zi,&FP);mmul(y,p->y,z2,&FP);
 from_mont(x,x,&FP);from_mont(y,y,&FP);store(out,x);store(out+32,y);return 0;
}
static int pt_load(struct pt*p,const uint8_t in[64]){
 fe x,y,t,g;load(x,in);load(y,in+32);
 if(!sub_raw(t,x,FP.m)||!sub_raw(t,y,FP.m))return -1; /* coordinates below p */
 to_mont(p->x,x,&FP);to_mont(p->y,y,&FP);copy(p->z,FP.one);
 curve_g(g,p->x);mmul(t,p->y,p->y,&FP);return is_equal(t,g)?0:-1;
}
static int scalar_ok(const fe k){fe t;static const fe two={2,0,0,0,0,0,0,0};return !sub_raw(t,k,two)&&sub_raw(t,k,FN.m);} /* 1 < k < order */
int nsae_scalar_mult(const uint8_t point[64],const uint8_t k[32],uint8_t out[64]){
 struct pt p,r;fe s,t;if(!point||!k||!out)return -1;
 load(s,k);if(is_zero(s)||!sub_raw(t,s,FN.m)||pt_load(&p,point))return -1;
 pt_mul(&r,&p,s);int e=pt_store(&r,out);wipe(s,sizeof s);wipe(&r,sizeof r);return e;
}
static void order_addresses(const uint8_t a[6],const uint8_t b[6],uint8_t out[12]){
 int first=memcmp(a,b,6)>0;memcpy(out,first?a:b,6);memcpy(out+6,first?b:a,6); /* max | min */
}
/* 12.4.4.2.2: forty iterations, the first valid candidate is kept by masked selection. */
int nsae_pwe_loop(const uint8_t a[6],const uint8_t b[6],const uint8_t*password,size_t pn,uint8_t out[64]){
 if(!out)return -1;
 if(!a||!b||!password||!pn||pn>63){wipe(out,64);return -1;}
 uint8_t addresses[12],seed[32],value[32],prime[32];fe x,y,g,t,bx,by;uint32_t found=0,parity=0;
 order_addresses(a,b,addresses);store(prime,FP.m);wipe(bx,sizeof bx);wipe(by,sizeof by);
 for(unsigned counter=1;counter<=40;counter++){
  uint8_t c=(uint8_t)counter;const uint8_t*parts[2]={password,&c};size_t sizes[2]={pn,1};
  (void)nwpc_hmac256(addresses,12,parts,sizes,2,seed);
  (void)nwpc_kdf256(seed,32,"SAE Hunting and Pecking",prime,32,value,32);
  load(x,value);uint32_t below=0u-sub_raw(t,x,FP.m); /* value < p */
  reduce_once(x,x,FP.m);to_mont(x,x,&FP);curve_g(g,x);
  uint32_t take=field_sqrt(y,g)&below&~found;
  cmov(bx,x,take);cmov(by,y,take);parity^=(parity^(uint32_t)(seed[31]&1u))&take;found|=take;
 }
 from_mont(x,bx,&FP);from_mont(y,by,&FP);
 msub(t,FP.m,y,&FP); /* p - y (y is never zero on this curve) */
 cmov(y,t,0u-((y[0]&1u)^parity));
 store(out,x);store(out+32,y);
 wipe(seed,32);wipe(value,32);wipe(x,sizeof x);wipe(y,sizeof y);wipe(bx,sizeof bx);wipe(by,sizeof by);wipe(g,sizeof g);wipe(t,sizeof t);
 if(!found){wipe(out,64);return -1;}
 return 0;
}
/* 12.4.4.2.3 simplified SWU for a = -3, z = -10; u in Montgomery form. */
static void sswu(struct pt*r,const fe u){
 fe z,u2,m,t,x1,x2,gx1,gx2,y,c,plain,neg;static const fe ten={10,0,0,0,0,0,0,0};
 to_mont(t,ten,&FP);wipe(z,sizeof z);msub(z,z,t,&FP); /* -10 */
 mmul(u2,u,u,&FP);mmul(u2,u2,z,&FP); /* z u^2 */
 mmul(m,u2,u2,&FP);madd(m,m,u2,&FP); /* z^2 u^4 + z u^2 */
 uint32_t zero=is_zero(m);
 mpow(t,m,EXP_INV,&FP); /* 0 stays 0 */
 madd(t,t,FP.one,&FP);to_mont(c,SSWU_C2,&FP);mmul(x1,t,c,&FP);to_mont(c,SSWU_C1,&FP);cmov(x1,c,zero);
 curve_g(gx1,x1);mmul(x2,u2,x1,&FP);curve_g(gx2,x2);
 uint32_t square=field_sqrt(y,gx1);
 copy(r->x,x2);cmov(r->x,x1,square);
 cmov(gx2,gx1,square);(void)field_sqrt(y,gx2);
 from_mont(plain,y,&FP);from_mont(t,u,&FP);wipe(neg,sizeof neg);msub(neg,neg,y,&FP);
 cmov(y,neg,0u-((plain[0]^t[0])&1u)); /* LSB(y) = LSB(u) */
 copy(r->y,y);copy(r->z,FP.one);
 wipe(u2,sizeof u2);wipe(m,sizeof m);wipe(t,sizeof t);wipe(x1,sizeof x1);wipe(x2,sizeof x2);wipe(y,sizeof y);wipe(plain,sizeof plain);
}
/* HKDF-Expand(seed, label, 48) reduced modulo p, in Montgomery form. */
static void hash_to_field(fe u,const uint8_t seed[32],const char*label){
 uint8_t t1[32],t2[32],one=1,two=2;size_t ln=strlen(label);fe hi,lo;
 const uint8_t*p1[2]={(const uint8_t*)label,&one};size_t n1[2]={ln,1};(void)nwpc_hmac256(seed,32,p1,n1,2,t1);
 const uint8_t*p2[3]={t1,(const uint8_t*)label,&two};size_t n2[3]={32,ln,1};(void)nwpc_hmac256(seed,32,p2,n2,3,t2);
 /* value = t1 | t2[0..15]: 384 bits = hi(128) * 2^256 + lo(256) */
 uint8_t wide[32]={0};memcpy(wide+16,t1,16);load(hi,wide);memcpy(wide,t1+16,16);memcpy(wide+16,t2,16);load(lo,wide);
 reduce_once(lo,lo,FP.m);to_mont(lo,lo,&FP);to_mont(hi,hi,&FP);to_mont(hi,hi,&FP);madd(u,lo,hi,&FP);
 wipe(t1,32);wipe(t2,32);wipe(wide,32);wipe(hi,sizeof hi);wipe(lo,sizeof lo);
}
int nsae_pt(const uint8_t*ssid,size_t sn,const uint8_t*password,size_t pn,const uint8_t*identifier,size_t in,uint8_t out[64]){
 if(!out)return -1;
 if(!ssid||!sn||sn>32||!password||!pn||pn>63||(!identifier&&in)||in>255){wipe(out,64);return -1;}
 uint8_t seed[32];fe u;struct pt p1,p2;const uint8_t*parts[2]={password,identifier};size_t sizes[2]={pn,in};
 (void)nwpc_hmac256(ssid,sn,parts,sizes,in?2:1,seed);
 hash_to_field(u,seed,"SAE Hash to Element u1 P1");sswu(&p1,u);
 hash_to_field(u,seed,"SAE Hash to Element u2 P2");sswu(&p2,u);
 pt_add(&p1,&p1,&p2);int e=pt_store(&p1,out);
 wipe(seed,32);wipe(u,sizeof u);wipe(&p1,sizeof p1);wipe(&p2,sizeof p2);if(e)wipe(out,64);return e;
}
int nsae_pwe_pt(const uint8_t pt[64],const uint8_t a[6],const uint8_t b[6],uint8_t out[64]){
 if(!out)return -1;
 if(!pt||!a||!b){wipe(out,64);return -1;}
 uint8_t addresses[12],zero[32]={0},digest[32];fe val,limit,t;struct pt p,r;const uint8_t*parts[1]={addresses};size_t sizes[1]={12};
 order_addresses(a,b,addresses);(void)nwpc_hmac256(zero,32,parts,sizes,1,digest);
 load(val,digest);sub_raw(limit,FN.m,RAW_ONE);reduce_once(val,val,limit);add_raw(t,val,RAW_ONE); /* val mod (r-1) + 1 */
 int e=pt_load(&p,pt);if(!e){pt_mul(&r,&p,t);e=pt_store(&r,out);}
 wipe(digest,32);wipe(val,sizeof val);wipe(t,sizeof t);wipe(&p,sizeof p);wipe(&r,sizeof r);if(e)wipe(out,64);return e;
}
void nsae_cancel(struct nsae*s){if(s)wipe(s,sizeof *s);}
static int refuse(struct nsae*s){nsae_cancel(s);return -1;}
static int unicast(const uint8_t*p){unsigned any=0;for(unsigned i=0;i<6;i++)any|=p[i];return any&&!(p[0]&1);}
static int draw(nsae_rng rng,void*ctx,fe out){
 uint8_t raw[32];
 for(unsigned tries=0;tries<8;tries++){
  if(rng(ctx,raw,32)){wipe(raw,32);return -1;}
  load(out,raw);reduce_once(out,out,FN.m);if(scalar_ok(out)){wipe(raw,32);return 0;}
 }
 wipe(raw,32);return -1;
}
int nsae_begin(struct nsae*s,const uint8_t own[6],const uint8_t peer[6],const uint8_t*password,size_t pn,const uint8_t*ssid,size_t sn,unsigned h2e,nsae_rng rng,void*ctx){
 if(!s)return -1;
 nsae_cancel(s);
 if(!own||!peer||!password||!pn||pn>63||!ssid||!sn||sn>32||h2e>1||!rng||!unicast(own)||!unicast(peer)||!memcmp(own,peer,6))return -1;
 memcpy(s->own,own,6);memcpy(s->peer,peer,6);s->h2e=h2e;
 if(h2e){uint8_t pt[64];int e=nsae_pt(ssid,sn,password,pn,0,0,pt)||nsae_pwe_pt(pt,own,peer,s->pwe);wipe(pt,64);if(e)return refuse(s);}
 else if(nsae_pwe_loop(own,peer,password,pn,s->pwe))return refuse(s);
 struct pt pwe,e;fe r,m,sum;int bad=pt_load(&pwe,s->pwe);
 for(unsigned tries=0;!bad;tries++){
  if(tries==4||draw(rng,ctx,r)||draw(rng,ctx,m)){bad=1;break;}
  madd(sum,r,m,&FN);if(scalar_ok(sum))break;
 }
 if(!bad){
  pt_mul(&e,&pwe,m);fe zero={0};msub(e.y,zero,e.y,&FP); /* element = inverse(mask * PWE) */
  bad=pt_store(&e,s->element);store(s->rand,r);store(s->scalar,sum);
 }
 wipe(r,sizeof r);wipe(m,sizeof m);wipe(sum,sizeof sum);wipe(&pwe,sizeof pwe);wipe(&e,sizeof e);
 if(bad)return refuse(s);
 s->state=NSAE_READY;return 0;
}
static void put16(uint8_t*p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
int nsae_commit(struct nsae*s,uint8_t*out,size_t capacity,size_t*used){
 if(used)*used=0;
 if(!s||!out||!used||(s->state!=NSAE_READY&&s->state!=NSAE_COMMITTED))return -1;
 size_t n=NSAE_COMMIT_MIN+s->token_bytes+(s->h2e&&s->token_bytes?3u:0u),at=2;
 if(capacity<n)return -1;
 put16(out,NSAE_GROUP);
 if(!s->h2e&&s->token_bytes){memcpy(out+at,s->token,s->token_bytes);at+=s->token_bytes;}
 memcpy(out+at,s->scalar,32);memcpy(out+at+32,s->element,64);at+=96;
 if(s->h2e&&s->token_bytes){out[at++]=255;out[at++]=(uint8_t)(1+s->token_bytes);out[at++]=93;memcpy(out+at,s->token,s->token_bytes);at+=s->token_bytes;}
 s->state=NSAE_COMMITTED;*used=at;return 0;
}
int nsae_token(struct nsae*s,const uint8_t*p,size_t n){
 if(!s||s->state!=NSAE_COMMITTED||!p||n<3||(unsigned)(p[0]|p[1]<<8)!=NSAE_GROUP)return -1;
 p+=2;n-=2;
 if(s->h2e){if(n<4||p[0]!=255||p[1]<2||(size_t)p[1]+2>n||p[2]!=93)return -1;n=p[1]-1u;p+=3;}
 if(!n||n>(s->h2e?254u:255u))return -1;
 memcpy(s->token,p,n);s->token_bytes=(unsigned)n;return 0;
}
int nsae_peer_commit(struct nsae*s,const uint8_t*p,size_t n){
 if(!s||s->state!=NSAE_COMMITTED)return -1;
 if(!p||n<NSAE_COMMIT_MIN||n>NSAE_BODY_MAX||(unsigned)(p[0]|p[1]<<8)!=NSAE_GROUP)return refuse(s);
 fe ps,own,rnd,sum;struct pt pwe,element,k;uint8_t shared[64],seed[32],zero[32]={0},keys[64],val[32];
 load(ps,p+2);
 if(!scalar_ok(ps)||pt_load(&element,p+34)||(!memcmp(p+2,s->scalar,32)&&!memcmp(p+34,s->element,64))||pt_load(&pwe,s->pwe))return refuse(s);
 pt_mul(&k,&pwe,ps);pt_add(&k,&k,&element);load(rnd,s->rand);pt_mul(&k,&k,rnd);
 int bad=pt_store(&k,shared);
 if(!bad){
  const uint8_t*parts[1]={shared};size_t sizes[1]={32};(void)nwpc_hmac256(zero,32,parts,sizes,1,seed);
  load(own,s->scalar);madd(sum,own,ps,&FN);store(val,sum);
  bad=nwpc_kdf256(seed,32,"SAE KCK and PMK",val,32,keys,64);
  memcpy(s->kck,keys,32);memcpy(s->pmk,keys+32,32);memcpy(s->pmkid,val,16);
  memcpy(s->peer_scalar,p+2,32);memcpy(s->peer_element,p+34,64);
 }
 wipe(rnd,sizeof rnd);wipe(&pwe,sizeof pwe);wipe(&k,sizeof k);wipe(shared,64);wipe(seed,32);wipe(keys,64);wipe(val,32);
 if(bad)return refuse(s);
 wipe(s->rand,32);wipe(s->pwe,64);s->state=NSAE_CONFIRMED;return 0;
}
static void confirm_value(const struct nsae*s,unsigned counter,int mine,uint8_t out[32]){
 uint8_t sc[2];put16(sc,counter);
 const uint8_t*parts[5]={sc,mine?s->scalar:s->peer_scalar,mine?s->element:s->peer_element,mine?s->peer_scalar:s->scalar,mine?s->peer_element:s->element};
 size_t sizes[5]={2,32,64,32,64};(void)nwpc_hmac256(s->kck,32,parts,sizes,5,out);
}
int nsae_confirm(struct nsae*s,uint8_t*out,size_t capacity,size_t*used){
 if(used)*used=0;
 if(!s||!out||!used||capacity<34||(s->state!=NSAE_CONFIRMED&&s->state!=NSAE_ACCEPTED)||s->send_confirm>=65534)return -1;
 s->send_confirm++;put16(out,s->send_confirm);confirm_value(s,s->send_confirm,1,out+2);*used=34;return 0;
}
int nsae_peer_confirm(struct nsae*s,const uint8_t*p,size_t n){
 if(!s||s->state!=NSAE_CONFIRMED)return -1;
 if(!p||n<34)return refuse(s);
 uint8_t want[32];unsigned d=0;confirm_value(s,(unsigned)(p[0]|p[1]<<8),0,want);
 for(unsigned i=0;i<32;i++)d|=want[i]^p[2+i];
 wipe(want,32);if(d)return refuse(s);
 wipe(s->kck,32);s->state=NSAE_ACCEPTED;return 0;
}

#endif /* NORD2 */
