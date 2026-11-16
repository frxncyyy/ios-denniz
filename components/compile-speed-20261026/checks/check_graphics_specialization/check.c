
#include "mali_graphics_compiler.h"
#include "mali_compute_compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *load(const char *path,size_t *n) {
 if(path[0]=='!'){*n=0;return NULL;}
 FILE *f=fopen(path,"rb");if(!f)exit(90);fseek(f,0,SEEK_END);
 long size=ftell(f);if(size<0)exit(90);rewind(f);*n=size;
 unsigned char *b=malloc(*n+1);if(!b||fread(b+1,1,*n,f)!=*n)exit(90);
 fclose(f);return b+1; /* Exercise unaligned SPIR-V and table input. */
}
static void save(const char *prefix,const char *suffix,const void *p,size_t n) {
 char path[4096];snprintf(path,sizeof(path),"%s.%s",prefix,suffix);
 FILE *f=fopen(path,"wb");if(!f||fwrite(p,1,n,f)!=n)exit(91);fclose(f);
}
int main(int argc,char **argv) {
 if(argc!=9)return 90;size_t vn,fn,vc_bytes,fc_bytes;
 void *v=load(argv[1],&vn),*f=load(argv[2],&fn);
 void *vc_data=load(argv[3],&vc_bytes),*fc_data=load(argv[4],&fc_bytes);
 const struct tegu_mali_specialization *vc=vc_data,*fc=fc_data;
 size_t nc=vc_bytes/16,nf=fc_bytes/16;
 struct tegu_mali_graphics vs={0},fs={0};struct tegu_mali_graphics_pair2 pair={0};
 struct tegu_mali_generic_plan plan={0};char error[512]={0};
 if(!strcmp(argv[3],"!null"))nc=1;
 if(!strcmp(argv[3],"!many"))nc=65;
 if(!strcmp(argv[3],"!wrap")){vc=(const void *)(UINTPTR_MAX-4);nc=1;}
 if(!strcmp(argv[3],"!alias")){vc=(const void *)&vs;nc=1;}
 int mode=atoi(argv[5]);unsigned rt=strtoul(argv[6],NULL,0);int rc;
 if(mode==0)rc=tegu_mali_compile_generic_pair(v,vn,f,fn,&vs,&fs,&pair,&plan,error,sizeof(error));
 else if(mode==1)rc=tegu_mali_compile_generic_pair_specialized(v,vn,f,fn,vc,nc,fc,nf,&vs,&fs,&pair,&plan,error,sizeof(error));
 else if(mode==2)rc=tegu_mali_compile_generic_pair_rt(v,vn,f,fn,&vs,&fs,&pair,&plan,rt,error,sizeof(error));
 else if(mode==3)rc=tegu_mali_compile_generic_pair_specialized_rt(v,vn,f,fn,vc,nc,fc,nf,&vs,&fs,&pair,&plan,rt,error,sizeof(error));
 else if(mode==4)rc=tegu_mali_compile_generic_pair_tile_rt(v,vn,f,fn,&vs,&fs,&pair,&plan,rt,error,sizeof(error));
 else rc=tegu_mali_compile_generic_pair_specialized_tile_rt(v,vn,f,fn,vc,nc,fc,nf,&vs,&fs,&pair,&plan,rt,error,sizeof(error));
 printf("RESULT {\"rc\":%d,\"bindings\":%u,\"fetch\":%u,\"vs\":%zu,\"fs\":%zu,\"error\":\"%s\"}\n",rc,plan.count,plan.reads_framebuffer,vs.size,fs.size,error);
 if(rc) {if(vs.code||fs.code||vs.size||fs.size||pair.version||plan.version)return 92;}
 else {
  save(argv[7],"vs",vs.code,vs.size);save(argv[7],"fs",fs.code,fs.size);
  struct tegu_mali_graphics vm=vs,fm=fs;vm.code=NULL;fm.code=NULL;
  save(argv[7],"vm",&vm,sizeof(vm));save(argv[7],"fm",&fm,sizeof(fm));
  save(argv[7],"pair",&pair,sizeof(pair));save(argv[7],"plan",&plan,sizeof(plan));
 }
 tegu_mali_graphics_free(&vs);tegu_mali_graphics_free(&fs);
 free((unsigned char *)v-1);free((unsigned char *)f-1);
 if(vc_data)free((unsigned char *)vc_data-1);if(fc_data)free((unsigned char *)fc_data-1);
 return 0;
}
