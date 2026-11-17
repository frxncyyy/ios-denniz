#include "association.h"
#include <string.h>
static uint16_t g16(const uint8_t*p){return p[0]|(uint16_t)p[1]<<8;}
static uint32_t g32(const uint8_t*p){return g16(p)|(uint32_t)g16(p+2)<<16;}
static void p32(uint8_t*p,uint32_t x){p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);p[2]=(uint8_t)(x>>16);p[3]=(uint8_t)(x>>24);}
void swia_clear(void*p,size_t n){volatile uint8_t*q=p;if(q)while(n--)*q++=0;}
static int fail(struct swia_metadata*m,unsigned r,int e){if(m)m->reason=r;return e;}
static int nonzero(const uint8_t*p,size_t n){uint8_t v=0;while(n--)v|=*p++;return v!=0;}
static int target_ok(const uint8_t*p){return !nonzero(p,6)||!(p[0]&1);}
int swia_decode_pmk(const void*v,size_t n,struct swia_join*out,size_t cap,struct swia_metadata*m){
 const uint8_t*p=v;
 if(m)memset(m,0,sizeof(*m));
 if(out)swia_clear(out,cap<sizeof(*out)?cap:sizeof(*out));
 if(!out||cap<sizeof(*out))return fail(m,SWIA_R_SHAPE,SWIA_CAPACITY);
 if(!p||n!=SWIA_BYTES)return fail(m,SWIA_R_SHAPE,SWIA_INVALID);
 if(m){m->key_kind=g32(p+64);m->key_length=g32(p+60);m->native_flags=g32(p+504);
  m->association_flags=g16(p+463);m->association_reason=p[465];}
 /* Exact profile from selected producer: infrastructure mode2/open auth1,
  * RSN PSK upper8. It is cross-checked again by the explicit RSN suites. */
 if(g32(p)!=1||!g32(p+12)||g32(p+12)>32||!target_ok(p+48))return fail(m,SWIA_R_SHAPE,SWIA_INVALID);
 if(g16(p+4)!=2||g16(p+6)!=1||g16(p+8)!=8)return fail(m,SWIA_R_AUTH,SWIA_UNSUPPORTED);
 if(g32(p+56)!=1||g32(p+60)!=32||g32(p+64)!=6||g16(p+68)||g16(p+70))
  return fail(m,SWIA_R_CREDENTIAL,SWIA_UNSUPPORTED);
 /* RSN version1; group/pairwise CCMP128; one pairwise; one AKM PSK;
  * capabilities0, no PMF/PMKID/group-management cipher. */
 {static const uint8_t rsn[22]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
  if(g16(p+204)!=sizeof(rsn)||memcmp(p+206,rsn,sizeof(rsn)))return fail(m,SWIA_R_RSN,SWIA_UNSUPPORTED);}
 /* flags463/464 are source-populated active bits. Only FORCE_BSSID0x20
  * is interpreted. A target is never silently discarded or substituted. */
 if((p[463]&~0x20u)||p[464]||p[465]||((p[463]&0x20u)&&!nonzero(p+48,6)))
  return fail(m,SWIA_R_ASSOC_OPTIONS,SWIA_UNSUPPORTED);
 /* This may be4/0x40000004 in the real WPA2 builder. Its downstream native
  * contract is unresolved; do not treat it as unused or whitelist blindly. */
 if(g32(p+504))return fail(m,SWIA_R_NATIVE_OPTIONS,SWIA_UNSUPPORTED);
 /* PMK input path uses no secondary cached PMK. Cipher10 passphrase instead
  * stores length32 at468 and derived PMK at472; that profile is not admitted. */
 if(g32(p+468))return fail(m,SWIA_R_CACHE,SWIA_UNSUPPORTED);
 /* Private address/blacklist/scope storage is inactive only when its enable
  * flags/counts are absent. The active flags464 already refuse all enables.
  * Count514 is independently checked; scope-id536 is a source-written value. */
 if(p[514]||g32(p+536))return fail(m,SWIA_R_PRIVATE_OPTIONS,SWIA_UNSUPPORTED);
 if(g32(p+580)||g32(p+584)||g32(p+588))return fail(m,SWIA_R_TRAILING_OPTIONS,SWIA_UNSUPPORTED);
 out->ssid_length=(uint8_t)g32(p+12);memcpy(out->ssid,p+16,out->ssid_length);
 out->has_target=(uint8_t)nonzero(p+48,6);memcpy(out->target,p+48,6);memcpy(out->pmk,p+72,32);
 out->auth_lower=1;out->auth_upper=8;return SWIA_OK;
}
static int result_code(uint32_t c){return c==0||c==1||c==2||c==9||c==21;}
static int generation(uint64_t a,uint64_t b){return a&&a==b;}
int swia_result4(void*v,size_t cap,size_t*used,uint64_t gen,uint64_t expected,uint32_t code){
 if(!used)return SWIA_INVALID;*used=0;
 if(!generation(gen,expected))return SWIA_STALE;
 if(!v||!result_code(code))return SWIA_INVALID;
 if(cap<4)return SWIA_CAPACITY;
 p32(v,code);*used=4;return SWIA_OK;
}
int swia_event9(void*v,size_t cap,size_t*used,const uint8_t name[16],uint64_t gen,uint64_t expected,uint32_t code){
 uint8_t b[48]={0};unsigned terminated=0;
 if(!used)return SWIA_INVALID;*used=0;
 if(!generation(gen,expected))return SWIA_STALE;
 if(!v||!name||!name[0]||!code||!result_code(code))return SWIA_INVALID;
 for(unsigned i=0;i<16;i++)if(!name[i])terminated=1;
 if(!terminated)return SWIA_INVALID;
 if(cap<sizeof(b))return SWIA_CAPACITY;
 p32(b,87);p32(b+28,9);
 for(unsigned i=0;i<16&&name[i];i++)b[32+i]=name[i];
 memcpy(v,b,sizeof(b));*used=sizeof(b);return SWIA_OK;
}
int swia_disconnect22(uint32_t request,uint32_t value,size_t payload){return request==22&&!value&&!payload?SWIA_OK:SWIA_UNSUPPORTED;}
