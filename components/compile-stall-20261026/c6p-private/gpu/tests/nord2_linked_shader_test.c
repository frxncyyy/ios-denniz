/* Executes the real reused compiler against the arch9 backend. No GPU. */
#include "tegu/mali_graphics_compiler.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *read_file(const char *path,size_t *n) {
 FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0&&size<1048576);rewind(f);
 void *p=malloc((size_t)size);assert(p);assert(fread(p,1,(size_t)size,f)==(size_t)size);fclose(f);*n=(size_t)size;return p;
}
int main(int argc,char **argv) {
 assert(argc==3);size_t vn,fn;void *vd=read_file(argv[1],&vn),*fd=read_file(argv[2],&fn);
 struct tegu_mali_graphics vs={0},fs={0};struct tegu_mali_graphics_pair2 pair={0};char error[512];
 int e=tegu_mali_compile_graphics_pair2(vd,vn,fd,fn,&vs,&fs,&pair,error,sizeof(error));
 if(e){fprintf(stderr,"arch9 pair: %d %s\n",e,error);free(vd);free(fd);return 1;}
 assert(vs.code&&fs.code&&pair.version==2&&pair.fs_uses_ld_var==0&&pair.varying_count);
 assert(pair.secondary_offset%128==0&&pair.secondary_offset<vs.size&&pair.secondary_offset==pair.position_offset+pair.position_size);
 assert(pair.secondary_size==vs.size-pair.secondary_offset&&pair.secondary_registers&&pair.secondary_registers<=64);
 assert(pair.vs_varying_mask==pair.fs_varying_mask&&!pair.needs_extended_fifo&&!pair.noperspective_mask);
 uint32_t seen=0;
 for(unsigned i=0;i<pair.varying_count;i++){
  const struct tegu_mali_varying_slot *v=&pair.varyings[i];assert(v->location<31&&v->alu_type==160&&v->components>=1&&v->components<=4);
  assert(v->offset+v->components*4<=pair.generic_size_B&&!(seen&(1u<<v->location)));seen|=1u<<v->location;
 }
 assert(seen==pair.vs_varying_mask);
 printf("PASS arch9 linked pair: VS%zu FS%zu, secondary%u, varyingBytes%u, locations0x%x; existing donor LD_VAR_BUF contract retained\n",
  vs.size,fs.size,pair.secondary_offset,pair.generic_size_B,seen);
 tegu_mali_graphics_free(&vs);tegu_mali_graphics_free(&fs);free(vd);free(fd);
}
