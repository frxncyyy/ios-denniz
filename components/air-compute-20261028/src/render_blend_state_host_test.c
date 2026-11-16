/* Sanitized version4 bounds and refusal preservation. argv: package.tgr */
#include "render_state.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
 assert(!fseek(f,0,SEEK_END));long length=ftell(f);assert(length>0);rewind(f);
 size_t n=(size_t)length;unsigned char *p=calloc(n+1,1);assert(p);
 assert(fread(p,1,n,f)==n);fclose(f);
 struct tegu_render_addresses a={0x501c000,0x501d000,0x5020000,0x5030000,
  0x5003000,0x5052000,0x501b000,0xff3366cc,0,0};
 struct guarded {uint64_t before;struct tegu_render_state state;uint64_t after;} *g=malloc(sizeof(*g));
 struct tegu_render_state *saved=malloc(sizeof(*saved));assert(g&&saved);
 memset(g,0xa5,sizeof(*g));assert(!tegu_render_build_vertex(p,n,&a,0x5050004,&g->state));
 memcpy(saved,&g->state,sizeof(*saved));unsigned refused=0;
 for(size_t size=0;size<=n+1;size++)if(size!=n) {
  assert(tegu_render_build_vertex(p,size,&a,0x5050004,&g->state));
  assert(!memcmp(saved,&g->state,sizeof(*saved)));refused++;
 }
 for(unsigned offset=128;offset<192;offset+=4) {
  uint32_t word=tegu_render_word(p+offset);tegu_render_put(p+offset,0xffffffffu);
  assert(tegu_render_build_vertex(p,n,&a,0x5050004,&g->state));
  assert(!memcmp(saved,&g->state,sizeof(*saved)));refused++;tegu_render_put(p+offset,word);
 }
 assert(g->before==0xa5a5a5a5a5a5a5a5ULL&&g->after==0xa5a5a5a5a5a5a5a5ULL);
 free(saved);free(g);free(p);printf("PASS sanitized TGR4: %u bounded refusals, output and guards intact\n",refused);
}
