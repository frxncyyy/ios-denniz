
#include "mali_graphics_compiler.h"
#include <stdio.h>
#include <stdlib.h>
static void *read_file(const char *p,size_t *n) {
 FILE *f=fopen(p,"rb");if(!f)exit(90);fseek(f,0,SEEK_END);long length=ftell(f);
 if(length<20)exit(90);*n=(size_t)length;rewind(f);void *b=malloc(*n);
 if(!b||fread(b,1,*n,f)!=*n)exit(90);fclose(f);return b;
}
int main(int argc,char **argv) {
 if(argc!=5)return 90;size_t vn,fn;void *v=read_file(argv[1],&vn),*f=read_file(argv[2],&fn);
 struct tegu_mali_graphics vs={0},fs={0};struct tegu_mali_graphics_pair2 pair={0};
 struct tegu_mali_generic_plan plan={0};char error[512]={0};
 int tile=atoi(argv[3]);
 int rc=tile==1?tegu_mali_compile_generic_pair_tile_rt(v,vn,f,fn,&vs,&fs,&pair,&plan,0,error,sizeof(error)):
  tegu_mali_compile_generic_pair_rt(v,vn,f,fn,&vs,&fs,&pair,&plan,0,error,sizeof(error));
 printf("RESULT rc=%d fetch=%u bindings=%u vs=%zu fs=%zu sysvals=%u error=%s\n",
  rc,plan.reads_framebuffer,plan.count,vs.size,fs.size,fs.sysval_mask,error);
 if(!rc) {
  if(tile==2) { if(plan.reads_framebuffer||plan.count)return 91; }
  else if(plan.reads_framebuffer!=(tile?TEGU_MALI_GENERIC_FETCH_TILE:TEGU_MALI_GENERIC_FETCH_MEMORY)||
     plan.count!=1||plan.bindings[0].kind!=TEGU_MALI_GENERIC_INPUT_ATTACHMENT||plan.reserved!=1||
     !(fs.sysval_mask&8))return 91;
  printf("ASSET {\"vr\":%u,\"fr\":%u,\"sr\":%u,\"vfau\":%u,\"ffau\":%u,\"pos\":%u,\"secondary\":%u,\"varying\":%u,\"vp\":%llu,\"fp\":%llu,\"sp\":%llu}\n",
   vs.registers,fs.registers,pair.secondary_registers,vs.fau_words,fs.fau_words,pair.position_offset,
   pair.secondary_offset,pair.generic_size_B,(unsigned long long)vs.preload,(unsigned long long)fs.preload,(unsigned long long)pair.secondary_preload);
  if(pair.vs_attribute_mask!=1)return 94;
  char path[4096];snprintf(path,sizeof(path),"%s.vs",argv[4]);
  FILE *vertex=fopen(path,"wb");if(!vertex)return 92;
  if(fwrite(vs.code,1,vs.size,vertex)!=vs.size)return 92;fclose(vertex);
  FILE *fp=fopen(argv[4],"wb");if(!fp)return 92;
  if(fwrite(fs.code,1,fs.size,fp)!=fs.size)return 92;fclose(fp);
 } else if(vs.code||fs.code||pair.version||plan.version)return 93;
 tegu_mali_graphics_free(&vs);tegu_mali_graphics_free(&fs);free(v);free(f);return rc;
}
