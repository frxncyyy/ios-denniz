#include "native_package_v7_writer.h"
#include "tegu_memory.h"
#include <stdint.h>
/* The frontend also builds without an SDK. Import only the C allocation ABI,
 * rather than the build host's libc headers into an ARM64 iOS translation unit. */
#ifdef __cplusplus
extern "C" {
#endif
void *malloc(size_t);
void free(void *);
#ifdef __cplusplus
}
#endif
#define TEGU_NATIVE_V7_MAGIC 0x31524754u
#define TEGU_NATIVE_V7_GPU 0xb8a21030u
static int tegu_native_v7_writer_overlap(const void*a,size_t an,const void*b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return an&&bn&&(x<=y?y-x<an:x-y<bn);}
static void tegu_native_v7_wr(uint8_t*p,uint32_t x){p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);p[2]=(uint8_t)(x>>16);p[3]=(uint8_t)(x>>24);}
int tegu_native_v7_build_with_blend(const void *vs,size_t vn,const void *fs,size_t fn,uint32_t blend_class,uint8_t **out,size_t *outn){
 if(!vs||!fs||!out||!outn||*out||*outn||vn!=TEGU_NATIVE_V7_VS_BYTES||fn!=TEGU_NATIVE_V7_FS_BYTES||
    (blend_class!=TEGU_NATIVE_V7_BLEND_SOURCE_OVER&&blend_class!=TEGU_NATIVE_V7_BLEND_DISABLED)||
    tegu_native_v7_writer_overlap(out,sizeof(*out),outn,sizeof(*outn))||tegu_native_v7_writer_overlap(out,sizeof(*out),vs,vn)||
    tegu_native_v7_writer_overlap(out,sizeof(*out),fs,fn)||tegu_native_v7_writer_overlap(outn,sizeof(*outn),vs,vn)||
    tegu_native_v7_writer_overlap(outn,sizeof(*outn),fs,fn))return -1;
 size_t fso=TEGU_NATIVE_V7_CODE+((vn+63u)&~(size_t)63u),n=fso+fn;uint8_t*p=malloc(n);if(!p)return -2;memset(p,0,n);
 const uint32_t h[32]={TEGU_NATIVE_V7_MAGIC,7,TEGU_NATIVE_V7_GPU,80,1152,25600,8,76,
  32,34,3,0,0,0x38000000,8,0, 64,51,8,0,0,0x30000000,76,0,
  3,2,512,1024,32,0,0x38000000,8};
 for(unsigned i=0;i<32;i++)tegu_native_v7_wr(p+4*i,h[i]);
 const uint32_t sourceover[9]={1,1,0,1,5,0,1,5,15};
 const uint32_t disabled[9]={1,0,0,1,0,0,1,0,15};
 const uint32_t *blend=blend_class==TEGU_NATIVE_V7_BLEND_SOURCE_OVER?sourceover:disabled;
 for(unsigned i=0;i<9;i++)tegu_native_v7_wr(p+128+4*i,blend[i]);
 const uint32_t tex[7]={1,16,3,0,0,4,1};for(unsigned i=0;i<7;i++)tegu_native_v7_wr(p+192+4*i,tex[i]);
 const uint32_t ext[30]={1,1,0,4,1,1,4,4,1,1,2,3,2,1,6,3,0,64,16,32,2,0,4,64,68,72,16,80,32,48};
 for(unsigned i=0;i<30;i++)tegu_native_v7_wr(p+256+4*i,ext[i]);tegu_native_v7_wr(p+376,32);
 tegu_native_v7_wr(p+384,80);tegu_native_v7_wr(p+388,15);tegu_native_v7_wr(p+392,80);tegu_native_v7_wr(p+396,0);
 const uint32_t attrs[14]={0,1,109,0,48,0,1,1,1,103,16,48,0,1};for(unsigned i=0;i<14;i++)tegu_native_v7_wr(p+TEGU_NATIVE_V7_ATTR+4*i,attrs[i]);
 memcpy(p+TEGU_NATIVE_V7_CODE,vs,vn);memcpy(p+fso,fs,fn);
 struct tegu_native_v7_info info;if(tegu_native_v7_parse(p,n,&info)){free(p);return -3;}
 *out=p;*outn=n;return 0;
}
int tegu_native_v7_build(const void *vs,size_t vn,const void *fs,size_t fn,uint8_t **out,size_t *outn){
 return tegu_native_v7_build_with_blend(vs,vn,fs,fn,TEGU_NATIVE_V7_BLEND_SOURCE_OVER,out,outn);
}
void tegu_native_v7_free(uint8_t **p,size_t *n){if(p&&*p){free(*p);*p=NULL;}if(n)*n=0;}
