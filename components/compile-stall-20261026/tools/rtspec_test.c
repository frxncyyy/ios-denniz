/* Host check of RUNTIME_SPEC: compile a VS/FS pair specialized (tables from
 * argv values) and runtime (no tables); print status, sizes, push bytes. */
#include "mali_compute_compiler.h"
#include "mali_graphics_compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *slurp(const char *p,size_t *n){FILE *f=fopen(p,"rb");if(!f)return 0;fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);void *b=malloc(*n);fread(b,1,*n,f);fclose(f);return b;}
int main(int argc,char **argv){
 if(argc<4){fprintf(stderr,"usage: vs.spv fs.spv flags [id=value ...]\n");return 2;}
 size_t vn,fn;void *vd=slurp(argv[1],&vn),*fd=slurp(argv[2],&fn);if(!vd||!fd)return 2;
 unsigned flags=(unsigned)strtoul(argv[3],0,0);
 struct tegu_mali_specialization fc[16];size_t fcn=0;
 for(int i=4;i<argc&&fcn<16;i++){unsigned id;unsigned long long v;if(sscanf(argv[i],"%u=%llx",&id,&v)==2){memset(&fc[fcn],0,sizeof(fc[0]));fc[fcn].id=id;fc[fcn].size=4;memcpy(fc[fcn].data,&v,4);fcn++;}}
 for(int mode=0;mode<2;mode++){
  struct tegu_mali_graphics vs,fs;struct tegu_mali_graphics_pair2 pair;struct tegu_mali_generic_plan plan;struct tegu_mali_varying_info var;
  memset(&vs,0,sizeof vs);memset(&fs,0,sizeof fs);memset(&pair,0,sizeof pair);memset(&plan,0,sizeof plan);memset(&var,0,sizeof var);
  char err[256]={0};
  struct tegu_mali_generic_options o={TEGU_MALI_GENERIC_OPTIONS_VERSION,flags|TEGU_MALI_GENERIC_OPTION_DRAW_PARAMETERS|TEGU_MALI_GENERIC_OPTION_EXTENDED_VARYINGS|TEGU_MALI_GENERIC_OPTION_THREAD_STORAGE|(mode?TEGU_MALI_GENERIC_OPTION_RUNTIME_SPEC:0),0,0};
  int rc=tegu_mali_compile_generic_pair_options(vd,vn,fd,fn,NULL,0,mode?NULL:fc,mode?0:fcn,&o,&vs,&fs,&pair,&plan,&var,err,sizeof err);
  printf("%s rc=%d vs=%zu fs=%zu fs_push=%u vs_push=%u fs_regs=%u err=%s\n",mode?"runtime":"special",rc,vs.size,fs.size,fs.push_bytes,vs.push_bytes,fs.registers,err);
  if(!rc){tegu_mali_graphics_free(&vs);tegu_mali_graphics_free(&fs);}
 }
 return 0;
}
