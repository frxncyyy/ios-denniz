/* Compile an authored LOAD-pass shader with the actual arch9 compiler. */
#include "tegu/mali_graphics_compiler.h"
#include "nord2_metal_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
static void *read_file(const char *path,size_t *size){
 FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);assert(n>0&&n<1048576);
 void *p=malloc(n);assert(p&&fread(p,1,n,f)==(size_t)n);fclose(f);*size=n;return p;
}
/* Optional argv[3]: the pre-frame preload fragment shader (gl_FragCoord
 * texelFetch, no varyings), emitted as n2_frame_preload_shader reusing the
 * LOAD-pass vertex code; only its fragment program is used on the GPU. */
static int frame_preload(const void *v,size_t vn,const char *path){
 size_t fn;void *f=read_file(path,&fn);
 struct tegu_mali_graphics vs={0},fs={0};struct tegu_mali_graphics_pair2 p={0};struct tegu_mali_generic_plan plan={0};char err[512]={0};
 int e=tegu_mali_compile_generic_pair(v,vn,f,fn,&vs,&fs,&p,&plan,err,sizeof(err));
 if(e){fprintf(stderr,"frame preload compile %d: %s\n",e,err);return 1;}
 assert(!fs.tls&&plan.count==1&&plan.bindings[0].binding==0&&!p.fs_uses_ld_var);
 printf("static const uint8_t n2_frame_preload_fs[]={");for(size_t j=0;j<fs.size;j++)printf("%u,",fs.code[j]);puts("};");
 printf("static const struct tegu_generic_draw_shader n2_frame_preload_shader={.vertex_code=n2_preload_vs,.fragment_code=n2_frame_preload_fs,.vertex_bytes=sizeof(n2_preload_vs),.fragment_bytes=%zu,.vertex_registers=%u,.fragment_registers=%u,.secondary_registers=%u,.vertex_fau_words=%u,.fragment_fau_words=%u,.position_offset=%u,.secondary_offset=%u,.varying_bytes=%u,.vertex_preload=%lluull,.fragment_preload=%lluull,.secondary_preload=%lluull,.blend={.version=1,.write_mask=15}};\n",fs.size,vs.registers,fs.registers,p.secondary_registers,vs.fau_words,fs.fau_words,p.position_offset,p.secondary_offset,p.generic_size_B,(unsigned long long)vs.preload,(unsigned long long)fs.preload,(unsigned long long)p.secondary_preload);
 free(f);tegu_mali_graphics_free(&vs);tegu_mali_graphics_free(&fs);return 0;
}
/* Optional argv[4]/argv[5]: the raw-target (unsigned-integer twin view)
 * LOAD fragment and pre-frame fragment, emitted as n2_preload_uint_shader and
 * n2_frame_preload_uint_shader reusing the LOAD-pass vertex code. */
static int extra_fragment(const void *v,size_t vn,const char *path,const char *name,const char *code){
 size_t fn;void *f=read_file(path,&fn);
 struct tegu_mali_graphics vs={0},fs={0};struct tegu_mali_graphics_pair2 p={0};struct tegu_mali_generic_plan plan={0};char err[512]={0};
 int e=tegu_mali_compile_generic_pair(v,vn,f,fn,&vs,&fs,&p,&plan,err,sizeof(err));
 if(e){fprintf(stderr,"%s compile %d: %s\n",name,e,err);return 1;}
 assert(!fs.tls&&plan.count==1&&plan.bindings[0].binding==0&&!p.fs_uses_ld_var);
 printf("static const uint8_t %s[]={",code);for(size_t j=0;j<fs.size;j++)printf("%u,",fs.code[j]);puts("};");
 printf("static const struct tegu_generic_draw_shader %s={.vertex_code=n2_preload_vs,.fragment_code=%s,.vertex_bytes=sizeof(n2_preload_vs),.fragment_bytes=%zu,.vertex_registers=%u,.fragment_registers=%u,.secondary_registers=%u,.vertex_fau_words=%u,.fragment_fau_words=%u,.position_offset=%u,.secondary_offset=%u,.varying_bytes=%u,.vertex_preload=%lluull,.fragment_preload=%lluull,.secondary_preload=%lluull,.blend={.version=1,.write_mask=15}};\n",name,code,fs.size,vs.registers,fs.registers,p.secondary_registers,vs.fau_words,fs.fau_words,p.position_offset,p.secondary_offset,p.generic_size_B,(unsigned long long)vs.preload,(unsigned long long)fs.preload,(unsigned long long)p.secondary_preload);
 free(f);tegu_mali_graphics_free(&vs);tegu_mali_graphics_free(&fs);return 0;
}
int main(int argc,char **argv){assert(argc==3||argc==4||argc==6);size_t vn,fn;void *v=read_file(argv[1],&vn),*f=read_file(argv[2],&fn);
 struct tegu_mali_graphics vs={0},fs={0};struct tegu_mali_graphics_pair2 p={0};struct tegu_mali_generic_plan plan={0};char err[512]={0};
 int e=tegu_mali_compile_generic_pair(v,vn,f,fn,&vs,&fs,&p,&plan,err,sizeof(err));
 if(e){fprintf(stderr,"preload compile %d: %s\n",e,err);return 1;}
 assert(!vs.tls&&!fs.tls&&plan.count==1&&plan.bindings[0].binding==0&&!p.fs_uses_ld_var);
 puts("/* Generated from authored GLSL with pinned Mesa81daea6, arch9. */");
 const struct tegu_mali_graphics *shaders[2]={&vs,&fs};const char *names[2]={"vs","fs"};
 for(unsigned i=0;i<2;i++){printf("static const uint8_t n2_preload_%s[]={",names[i]);for(size_t j=0;j<shaders[i]->size;j++)printf("%u,",shaders[i]->code[j]);puts("};");}
 printf("static const struct tegu_generic_draw_shader n2_preload_shader={.vertex_code=n2_preload_vs,.fragment_code=n2_preload_fs,.vertex_bytes=%zu,.fragment_bytes=%zu,.vertex_registers=%u,.fragment_registers=%u,.secondary_registers=%u,.vertex_fau_words=%u,.fragment_fau_words=%u,.position_offset=%u,.secondary_offset=%u,.varying_bytes=%u,.vertex_preload=%lluull,.fragment_preload=%lluull,.secondary_preload=%lluull,.blend={.version=1,.write_mask=15}};\n",vs.size,fs.size,vs.registers,fs.registers,p.secondary_registers,vs.fau_words,fs.fau_words,p.position_offset,p.secondary_offset,p.generic_size_B,(unsigned long long)vs.preload,(unsigned long long)fs.preload,(unsigned long long)p.secondary_preload);
 free(f);tegu_mali_graphics_free(&vs);tegu_mali_graphics_free(&fs);
 int r=argc>=4?frame_preload(v,vn,argv[3]):0;
 if(!r&&argc==6)r=extra_fragment(v,vn,argv[4],"n2_preload_uint_shader","n2_preload_uint_fs")||
  extra_fragment(v,vn,argv[5],"n2_frame_preload_uint_shader","n2_frame_preload_uint_fs");
 free(v);return r;
}
