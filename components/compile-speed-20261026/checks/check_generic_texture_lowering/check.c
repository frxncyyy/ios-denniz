
#include "mali_graphics_compiler.h"
#include <stdio.h>
#include <stdlib.h>
static void *read_file(const char *path,size_t *size) {
 FILE *f=fopen(path,"rb"); if(!f)exit(90); fseek(f,0,SEEK_END);
 long n=ftell(f); if(n<20)exit(90); *size=(size_t)n; rewind(f);
 void *data=malloc(*size); if(!data||fread(data,1,*size,f)!=*size)exit(90);
 fclose(f); return data;
}
int main(int argc,char **argv) {
 if(argc!=3)return 90;
 size_t vn,fn;void *v=read_file(argv[1],&vn),*f=read_file(argv[2],&fn);
 struct tegu_mali_graphics vs={0},fs={0};struct tegu_mali_graphics_pair2 pair={0};
 struct tegu_mali_generic_plan plan={0};char error[512]={0};
 int rc=tegu_mali_compile_generic_pair(v,vn,f,fn,&vs,&fs,&pair,&plan,error,sizeof(error));
 printf("RESULT rc=%d vs=%zu fs=%zu count=%u fetch=%u error=%s\n",rc,vs.size,fs.size,plan.count,plan.reads_framebuffer,error);
 if(!rc&&(!vs.code||!fs.code||vs.tls||fs.tls))return 91;
 if(rc&&(vs.code||fs.code||pair.version||plan.version))return 92;
 tegu_mali_graphics_free(&vs);tegu_mali_graphics_free(&fs);free(v);free(f);return rc;
}
