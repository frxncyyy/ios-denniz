#include "native_package_b3_writer.h"
#include "native_package_b3.h"
#include "tegu_memory.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void *malloc(size_t);
void free(void *);
#ifdef __cplusplus
}
#endif

#define TEGU_NATIVE_B3_MAGIC 0x31524754u
#define TEGU_NATIVE_B3_GPU 0xb8a21030u
static int b3_writer_overlap(const void *a,size_t an,const void *b,size_t bn) {
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return an&&bn&&(x<=y?y-x<an:x-y<bn);
}
static void b3_writer_u32(uint8_t *p,uint32_t x) {
 p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);p[2]=(uint8_t)(x>>16);p[3]=(uint8_t)(x>>24);
}
int tegu_native_b3_build(const void *vs,size_t vn,const void *fs,size_t fn,
 uint8_t **out,size_t *outn) {
 if(!vs||!fs||!out||!outn||*out||*outn||vn!=TEGU_NATIVE_B3_VS_BYTES||
    fn!=TEGU_NATIVE_B3_FS_BYTES||
    b3_writer_overlap(out,sizeof(*out),outn,sizeof(*outn))||
    b3_writer_overlap(out,sizeof(*out),vs,vn)||b3_writer_overlap(out,sizeof(*out),fs,fn)||
    b3_writer_overlap(outn,sizeof(*outn),vs,vn)||b3_writer_overlap(outn,sizeof(*outn),fs,fn))return -1;
 size_t fso=TEGU_NATIVE_B3_CODE+((vn+63u)&~(size_t)63u),n=fso+fn;
 uint8_t *p=(uint8_t *)malloc(n);if(!p)return -2;memset(p,0,n);
 const uint32_t h[32]={TEGU_NATIVE_B3_MAGIC,TEGU_NATIVE_B3_VERSION,TEGU_NATIVE_B3_GPU,80,
  TEGU_NATIVE_B3_VS_BYTES,TEGU_NATIVE_B3_FS_BYTES,8,76,
  32,34,3,0,0,0x38000000,8,0, 64,51,8,0,0,0x30000000,76,0,
  3,2,512,1024,32,0,0x38000000,8};
 for(unsigned i=0;i<32;i++)b3_writer_u32(p+4*i,h[i]);
 const uint32_t blend[9]={1,0,0,1,0,0,1,0,15};
 for(unsigned i=0;i<9;i++)b3_writer_u32(p+128+4*i,blend[i]);
 const uint32_t texture[7]={1,16,3,0,0,4,1};
 for(unsigned i=0;i<7;i++)b3_writer_u32(p+192+4*i,texture[i]);
 const uint32_t resources[30]={1,1,0,4,1,1,4,4,1,1,2,3,2,1,6,3,0,
  64,16,82,2,0,4,64,68,72,4,80,32,112};
 for(unsigned i=0;i<30;i++)b3_writer_u32(p+256+4*i,resources[i]);
 b3_writer_u32(p+376,32);
 b3_writer_u32(p+384,80);b3_writer_u32(p+388,15);
 b3_writer_u32(p+392,80);b3_writer_u32(p+396,0);
 const uint32_t attrs[14]={0,1,109,0,48,0,1,1,1,103,16,48,0,1};
 for(unsigned i=0;i<14;i++)b3_writer_u32(p+TEGU_NATIVE_B3_ATTR+4*i,attrs[i]);
 memcpy(p+TEGU_NATIVE_B3_CODE,vs,vn);memcpy(p+fso,fs,fn);
 struct tegu_native_b3_info info;
 if(tegu_native_b3_parse(p,n,&info)){free(p);return -3;}
 *out=p;*outn=n;return 0;
}
void tegu_native_b3_free(uint8_t **p,size_t *n) {
 if(p&&*p){free(*p);*p=NULL;}if(n)*n=0;
}
