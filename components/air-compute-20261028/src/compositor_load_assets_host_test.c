#include "compositor_load_assets_v6.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void same_file(const char *path,const void *p,size_t n)
{
 FILE *f=fopen(path,"rb");assert(f);unsigned char *b=malloc(n);assert(b);
 assert(fread(b,1,n,f)==n);assert(fgetc(f)==EOF);fclose(f);if(memcmp(b,p,n)){for(size_t i=0;i<n;i++)if(b[i]!=((const unsigned char*)p)[i]){fprintf(stderr,"%s mismatch at %zu expected %02x got %02x\n",path,i,b[i],((const unsigned char*)p)[i]);break;}abort();}free(b);
}
static void one(const char *dir,unsigned w,unsigned h,unsigned stride)
{
 char path[512];struct tegu_render_target_layout l={TEGU_RENDER_TARGET_LAYOUT_VERSION,w,h,stride,stride*h};
 unsigned char *r=calloc(1,4096),*d=calloc(1,4096),*sh=calloc(1,4096);assert(r&&d&&sh);
 struct tegu_compositor_load_asset_scratch_v6 *s=calloc(1,sizeof(*s));
 struct tegu_compositor_load_assets_v6 *x=calloc(1,sizeof(*x));assert(s&&x);
 int rc=tegu_compositor_load_assets_build_v6(&l,0x05054000,r,d,sh,s,x);if(rc){fprintf(stderr,"asset build %ux%u rc=%d version=%u target=%llu\n",w,h,rc,l.version,(unsigned long long)l.target_length);abort();}
#define CHECK(tag,ptr,n) do {snprintf(path,sizeof(path),"%s/%ux%u-%s.bin",dir,w,h,tag);same_file(path,ptr,n);} while(0)
 CHECK("fbd",s->fbd,128);CHECK("resource",r,192);CHECK("dcd",d,4096);
 CHECK("texture",s->texture_plane,64);CHECK("rt",s->rt,64);CHECK("sync",s->visibility,16);
 snprintf(path,sizeof(path),"%s/preload_shader_v10.bin",dir);same_file(path,sh,128);
#undef CHECK
 memset(r,0xa5,4096);memset(d,0x5a,4096);memset(sh,0x3c,4096);
 memset(s,0x69,sizeof(*s));memset(x,0x96,sizeof(*x));l.row_stride=w*4;
 if((l.row_stride&63u)==0)l.row_stride+=4;
 assert(tegu_compositor_load_assets_build_v6(&l,0x05054000,r,d,sh,s,x)<0);
 for(unsigned i=0;i<4096;i++)assert(r[i]==0xa5&&d[i]==0x5a&&sh[i]==0x3c);
 const unsigned char *q=(const unsigned char*)s;for(size_t i=0;i<sizeof(*s);i++)assert(q[i]==0x69);
 q=(const unsigned char*)x;for(size_t i=0;i<sizeof(*x);i++)assert(q[i]==0x96);
 free(x);free(s);free(sh);free(d);free(r);
}
int main(int argc,char **argv)
{
 assert(argc==2);one(argv[1],16,16,64);one(argv[1],32,32,128);
 one(argv[1],256,256,1024);one(argv[1],1080,2424,4352);puts("Mesa LOAD assets: 4 full-byte oracles + invalid-output preservation passed");return 0;
}
