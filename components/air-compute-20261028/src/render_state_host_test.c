/* Sanitized execution of the actual builder with an authored compiled package. */
#include "render_state.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t vertex_resource_table;
static int build(const void *p,size_t n,const struct tegu_render_addresses *a,struct tegu_render_state *s) {
 return tegu_render_build_vertex(p,n,a,vertex_resource_table,s);
}
int main(int argc,char **argv) {
 assert(argc==3);vertex_resource_table=strtoull(argv[2],NULL,0);FILE *f=fopen(argv[1],"rb");assert(f);
 assert(!fseek(f,0,SEEK_END));long length=ftell(f);assert(length>0);rewind(f);
 unsigned char *package=malloc((size_t)length);assert(package);
 assert(fread(package,1,(size_t)length,f)==(size_t)length);fclose(f);
 struct tegu_render_addresses a={0x501c000,0x501d000,0x5020000,0x5030000,
  0x5003000,0x501b400,0x501b000,0xff3366cc,0,0};
 struct guarded { uint64_t before[2];struct tegu_render_state state;uint64_t after[2]; } *g=malloc(sizeof(*g));
 assert(g);memset(g,0xa5,sizeof(*g));
 assert(!build(package,(size_t)length,&a,&g->state));
 assert(g->state.command_count>0&&g->state.command_count<=128);
 for(unsigned i=0;i<2;i++)assert(g->before[i]==0xa5a5a5a5a5a5a5a5ULL&&g->after[i]==0xa5a5a5a5a5a5a5a5ULL);
 struct tegu_render_state *saved=malloc(sizeof(*saved));assert(saved);memcpy(saved,&g->state,sizeof(*saved));
 for(size_t n=0;n<(size_t)length;n++) {
  assert(build(package,n,&a,&g->state));
  assert(!memcmp(saved,&g->state,sizeof(*saved)));
 }
 assert(build(NULL,(size_t)length,&a,&g->state));
 assert(build(package,(size_t)length,NULL,&g->state));
 assert(build(package,(size_t)length,&a,NULL));
 assert(!memcmp(saved,&g->state,sizeof(*saved)));
 free(saved);free(g);free(package);
 puts("PASS sanitized render builder: bounds, truncated input, output guards and refusal preservation");
 return 0;
}
