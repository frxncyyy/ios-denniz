#include "mali_graphics_compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *read_file(const char *p,size_t *n){FILE *f=fopen(p,"rb");if(!f)exit(90);fseek(f,0,SEEK_END);long l=ftell(f);*n=(size_t)l;rewind(f);void *b=malloc(*n);if(fread(b,1,*n,f)!=*n)exit(90);fclose(f);return b;}
/* argv: vs.spv fs.spv tile(0/1) target_count */
int main(int argc,char **argv){
 if(argc!=5)return 90;size_t vn,fn;void *v=read_file(argv[1],&vn),*f=read_file(argv[2],&fn);
 struct tegu_mali_graphics vs={0},fs={0};struct tegu_mali_graphics_pair2 pair={0};struct tegu_mali_generic_plan plan={0};
 struct tegu_mali_varying_info var={0};char error[512]={0};
 struct tegu_mali_generic_options2 o;memset(&o,0,sizeof(o));
 o.version=TEGU_MALI_GENERIC_OPTIONS2_VERSION;o.flags=TEGU_MALI_GENERIC_OPTION_MULTI_TARGET|TEGU_MALI_GENERIC_OPTION_EXTENDED_VARYINGS|(atoi(argv[3])?TEGU_MALI_GENERIC_OPTION_TILE_FETCH:0);
 o.target_count=(uint32_t)atoi(argv[4]);o.target_formats[0]=80;o.target_formats[1]=80;
 int rc=tegu_mali_compile_generic_pair_options(v,vn,f,fn,NULL,0,NULL,0,(const struct tegu_mali_generic_options *)&o,&vs,&fs,&pair,&plan,&var,error,sizeof(error));
 printf("RESULT rc=%d fetch=%u reserved=%x fau=%u error=%s\n",rc,plan.reads_framebuffer,plan.reserved,fs.fau_words,error);
 return rc?5:0;
}
