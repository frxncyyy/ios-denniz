/* Sanitized version3 paired-IDVS builder checks: truncation, padding, metadata,
 * address ranges, output guards and refusal preservation. argv: v3.tgr v2.tgr table */
#include "render_state.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t table;
static struct guarded {uint64_t before[2];struct tegu_render_state state;uint64_t after[2];} *g;
static struct tegu_render_state *saved;
static unsigned refusals,accepts;
static unsigned char *load(const char *path,size_t *n) {
 FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long l=ftell(f);assert(l>0);rewind(f);
 /* One spare byte lets the oversize check read inside an allocation. */
 unsigned char *p=calloc((size_t)l+1,1);assert(p);assert(fread(p,1,(size_t)l,f)==(size_t)l);fclose(f);
 *n=(size_t)l;return p;
}
static int build(const void *p,size_t n,const struct tegu_render_addresses *a,struct tegu_render_state *s) {
 return tegu_render_build_vertex(p,n,a,table,s);
}
static void guards(void) {
 for(unsigned i=0;i<2;i++)assert(g->before[i]==0xa5a5a5a5a5a5a5a5ULL&&g->after[i]==0xa5a5a5a5a5a5a5a5ULL);
}
static void refused(const void *p,size_t n,const struct tegu_render_addresses *a,const char *what) {
 if(!build(p,n,a,&g->state)){fprintf(stderr,"FAIL accepted %s\n",what);abort();}
 assert(!memcmp(saved,&g->state,sizeof(*saved)));guards();refusals++;
}
static uint64_t q(const unsigned char *d,unsigned off) {
 return (uint64_t)tegu_render_word(d+off)|(uint64_t)tegu_render_word(d+off+4)<<32;
}
/* 64-bit pair r:r+1 of the 32-bit CSF register file just before RUN_IDVS. */
static uint64_t reg(const struct tegu_render_state *s,unsigned r) {
 uint32_t file[256]={0};
 for(uint32_t i=0;i<s->command_count;i++) {
  uint64_t c=s->commands[i];unsigned op=(unsigned)(c>>56),dst=(unsigned)(c>>48)&255;
  if(op==6)break;
  if(op==1){file[dst]=(uint32_t)c;file[(dst+1)&255]=(uint32_t)(c>>32)&0xffff;}
  if(op==2)file[dst]=(uint32_t)c;
 }
 return (uint64_t)file[r]|(uint64_t)file[(r+1)&255]<<32;
}
static uint64_t run_idvs(const struct tegu_render_state *s) {
 for(uint32_t i=0;i<s->command_count;i++)if(s->commands[i]>>56==6)return s->commands[i];
 abort();
}
/* Accept a valid variant; check fields derived from it, then restore the baseline. */
static void accepted(const unsigned char *p,size_t n,const struct tegu_render_addresses *a,
 uint64_t primary,uint64_t secondary,uint32_t varying,uint32_t preload) {
 struct tegu_render_state *s=malloc(sizeof(*s));assert(s);
 assert(!build(p,n,a,s));
 assert(q(s->descriptors,0x608)==primary&&q(s->descriptors,0x708)==secondary);
 assert(tegu_render_word(s->descriptors+0x700)==0x80000038u&&tegu_render_word(s->descriptors+0x704)==preload);
 assert((uint32_t)reg(s,48)==varying&&reg(s,18)==a->descriptors+0x700&&(run_idvs(s)>>18&1));
 assert(!(run_idvs(s)>>35&7));
 assert(s->command_count==58);free(s);accepts++;
}
int main(int argc,char **argv) {
 assert(argc==4);table=strtoull(argv[3],NULL,0);
 size_t n,n2;unsigned char *pkg=load(argv[1],&n),*v2=load(argv[2],&n2);
 unsigned char *m=malloc(n+1);assert(m);
 const struct tegu_render_addresses base={0x501c000,0x501d000,0x5020000,0x5030000,
  0x5003000,0x5019000,0x501b000,0xff3366cc,0,0};
 struct tegu_render_addresses a=base;
 g=malloc(sizeof(*g));saved=malloc(sizeof(*saved));assert(g&&saved);
 memset(g,0xa5,sizeof(*g));
 assert(!build(pkg,n,&a,&g->state));guards();
 memcpy(saved,&g->state,sizeof(*saved));
 assert(!build(pkg,n,&a,&g->state)&&!memcmp(saved,&g->state,sizeof(*saved)));
 accepted(pkg,n,&a,base.vertex_code+384,base.vertex_code+768,16,0x3800);
 /* Truncation and oversize. */
 for(size_t i=0;i<n;i++)refused(pkg,i,&a,"truncated");
 refused(pkg,n+1,&a,"oversize");
 /* Each nonzero padding or push tail byte. */
 static const unsigned pads[][2]={{128,256},{256+68,384},{384+2*28,1280}};
 for(unsigned r=0;r<3;r++)for(unsigned i=pads[r][0];i<pads[r][1];i++) {
  memcpy(m,pkg,n);m[i]=1;refused(m,n,&a,"padding");
 }
 /* Invalid version3 metadata words; also v3 without an attribute plan. */
 static const uint32_t bad[][2]={
  {1,4},{24,0},{25,0},{25,32},
  {26,1},{26,64},{26,768},{26,896},{26,0x80000000u},
  {27,0},{27,384},{27,832},{27,1024},{27,1152},{27,0xffffff80u},
  {28,0},{28,33},{28,0xffffffffu},
  {29,1},{29,0x80000000u},{30,0x80000000u},{30,0x00400000u},{30,0x00010000u},
  {31,0},{31,2},{31,65},{31,68},{31,0x40000000u},{31,0xfffffffcu}};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
  memcpy(m,pkg,n);tegu_render_put(m+4*bad[i][0],bad[i][1]);refused(m,n,&a,"metadata");
 }
 /* v3 header words on a v2 package remain reserved. */
 for(unsigned w=26;w<32;w++){memcpy(m,v2,n2);tegu_render_put(m+4*w,1);refused(m,n2,&a,"v2 reserved");}
 /* Valid edges: zero position offset, one register, max varying, both preload bits. */
 memcpy(m,pkg,n);tegu_render_put(m+4*26,0);
 accepted(m,n,&a,base.vertex_code,base.vertex_code+768,16,0x3800);
 memcpy(m,pkg,n);tegu_render_put(m+4*27,896);tegu_render_put(m+4*28,1);tegu_render_put(m+4*31,64);
 tegu_render_put(m+4*30,0x7f800000u);
 accepted(m,n,&a,base.vertex_code+384,base.vertex_code+896,64,0x7f80);
 memcpy(m,pkg,n);tegu_render_put(m+4*26,512);tegu_render_put(m+4*27,640);tegu_render_put(m+4*31,4);
 accepted(m,n,&a,base.vertex_code+512,base.vertex_code+640,4,0x3800);
 /* Address ranges cover the full VS binary, not only the selected program. */
 static const int64_t bad_vc[]={-512,-64,1600,1};
 for(unsigned i=0;i<sizeof(bad_vc)/sizeof(bad_vc[0]);i++) {
  a=base;a.vertex_code=(i<2?base.descriptors:i==2?base.fragment_code:base.vertex_code)+(uint64_t)bad_vc[i];
  refused(pkg,n,&a,"vertex range");
 }
 a=base;a.fragment_code=base.vertex_code+960;refused(pkg,n,&a,"fragment in VS tail");
 a=base;a.fragment_code=base.vertex_code+768;refused(pkg,n,&a,"fragment at secondary");
 a=base;a.vertex_code=table&~4095ULL;refused(pkg,n,&a,"VS over resource page");
 a=base;a.vertex_code=(table&~4095ULL)-512;refused(pkg,n,&a,"VS tail over resource page");
 a=base;a.vertex_code=(1ULL<<48)-512;refused(pkg,n,&a,"VS past 48 bits");
 a=base;a.vertex_code=0;refused(pkg,n,&a,"VS null");
 a=base;a.vertex_code=base.vertex_code+1024+0x200000;a.fragment_code=base.vertex_code+0x200000+1024+1024;
 accepted(pkg,n,&a,a.vertex_code+384,a.vertex_code+768,16,0x3800);
 a=base;a.vertex_code=(1ULL<<48)-1024;
 accepted(pkg,n,&a,a.vertex_code+384,a.vertex_code+768,16,0x3800);
 a=base;a.fragment_code=base.vertex_code+1024;accepted(pkg,n,&a,base.vertex_code+384,base.vertex_code+768,16,0x3800);
 /* Resource table must accompany v3 and stay qualified. */
 a=base;uint64_t t=table;table=0;refused(pkg,n,&a,"no table");
 refused(pkg,n,&a,"no table");table=t+8;refused(pkg,n,&a,"table offset");table=t;
 assert(tegu_render_build(pkg,n,&a,&g->state));assert(!memcmp(saved,&g->state,sizeof(*saved)));
 /* Output and input guards. */
 refused(NULL,n,&a,"null package");refused(pkg,n,NULL,"null addresses");
 assert(build(pkg,n,&a,NULL));
 assert(!memcmp(saved,&g->state,sizeof(*saved)));guards();
 free(m);free(saved);free(g);free(pkg);free(v2);
 printf("PASS sanitized TGR3 builder: %u refusals preserved output, %u valid variants decoded\n",refusals,accepts);
 return 0;
}
