#include "render_load.h"
#include "load_mesa_templates.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *read_file(const char *p,size_t *n) {FILE *f=fopen(p,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long z=ftell(f);assert(z>=0);rewind(f);unsigned char *b=malloc((size_t)z);assert(b);assert(fread(b,1,(size_t)z,f)==(size_t)z);fclose(f);*n=(size_t)z;return b;}
static void eq(const void *a,const void *b,size_t n) {assert(!memcmp(a,b,n));}
static void put64(unsigned char *p,uint64_t x) {for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(x>>(8*i));}
int main(int argc,char **argv) {
 assert(argc==8);size_t n[4];unsigned char *fixture[4];for(unsigned i=0;i<4;i++)fixture[i]=read_file(argv[i+1],&n[i]);
 assert(n[0]==192&&n[1]==4096&&n[2]==192&&n[3]==128);eq(fixture[0],tegu_load_fbd,n[0]);eq(fixture[1],tegu_load_dcd,n[1]);eq(fixture[2],tegu_load_resources,n[2]);eq(fixture[3],tegu_load_shader,n[3]);
 size_t pkgBytes;unsigned char *pkg=read_file(argv[5],&pkgBytes);
 size_t rawBytes=0;unsigned char *raw=argv[6][0]?read_file(argv[6],&rawBytes):NULL;
 const unsigned char *r7resources=NULL,*r7dcd=NULL,*r7shader=NULL,*r7fbp=NULL;
 if(raw){assert(rawBytes>0x4000+89*4096+192);
  r7resources=raw+0x4000+86*4096;r7dcd=raw+0x4000+87*4096;
  r7shader=raw+0x4000+88*4096;r7fbp=raw+0x4000+89*4096;}
 struct tegu_render_addresses a={TEGU_RENDER_SCRATCH_VA,TEGU_RENDER_SCRATCH_VA+0x1000,TEGU_RENDER_SCRATCH_VA+0x10000,TEGU_RENDER_SCRATCH_VA+0x20000,TEGU_RENDER_TARGET_VA,TEGU_RENDER_SCRATCH_VA+0x2000,TEGU_RENDER_SCRATCH_VA+0x3000,0xff3366cc,0,0};
 struct tegu_render_state state,clear;assert(!tegu_render_build_resources(pkg,pkgBytes,&a,0,0,&state));clear=state;
 struct tegu_render_target_layout l={TEGU_RENDER_TARGET_LAYOUT_VERSION,16,16,64,1024};struct tegu_render_load_state load;memset(&load,0xa5,sizeof(load));
 assert(!tegu_render_build_load_pass(&state,pkg,pkgBytes,&a,&l,&load));
 unsigned char ex[4096];memcpy(ex,fixture[2],n[2]);put64(ex+0x30,TEGU_LOAD_RESOURCES_VA+0x40);put64(ex+0x48,a.target);eq(load.resources,ex,n[2]);for(size_t i=n[2];i<4096;i++)assert(load.resources[i]==0);
 if(raw){memcpy(ex,load.resources,4096);put64(ex+0x30,UINT64_C(0x05056040));put64(ex+0x48,UINT64_C(0x05054000));eq(ex,r7resources,4096);}
 memcpy(ex,fixture[1],n[1]);put64(ex+0x28,TEGU_LOAD_DCD_VA+0x1e0);put64(ex+0x30,(TEGU_LOAD_DCD_VA+0x1c0)|1);put64(ex+0x60,(TEGU_LOAD_RESOURCES_VA+0x80)|4);put64(ex+0x68,TEGU_LOAD_DCD_VA+0x1a0);put64(ex+0x70,TEGU_LOAD_DCD_VA+0x200);put64(ex+0x78,TEGU_LOAD_DCD_VA+0x180);put64(ex+0x1a8,TEGU_LOAD_SHADER_VA);eq(load.dcd,ex,n[1]);
 if(raw){memcpy(ex,load.dcd,4096);put64(ex+0x28,UINT64_C(0x050571e0));put64(ex+0x30,UINT64_C(0x050571c1));put64(ex+0x60,UINT64_C(0x05056084));put64(ex+0x68,UINT64_C(0x050571a0));put64(ex+0x70,UINT64_C(0x05057200));put64(ex+0x78,UINT64_C(0x05057180));put64(ex+0x1a8,UINT64_C(0x05058000));eq(ex,r7dcd,4096);}
 memset(ex,0,4096);memcpy(ex,fixture[3],n[3]);eq(load.shader_page,ex,4096);
 if(raw)eq(load.shader_page,r7shader,4096);
 memcpy(ex,fixture[0],n[0]);put64(ex+0x10,a.descriptors+0x100);put64(ex+0x18,TEGU_LOAD_DCD_VA);
 put64(ex+0x28,UINT64_C(0x01001080000f000f));put64(ex+0x30,UINT64_C(0x10000));
 put64(ex+0x38,a.tiler);put64(ex+0xa0,a.target);eq(state.descriptors,ex,64);eq(state.descriptors+128,ex+128,64);
 if(raw){memcpy(ex,r7fbp,192);put64(ex+0x10,a.descriptors+0x100);put64(ex+0x18,TEGU_LOAD_DCD_VA);
  put64(ex+0x38,a.tiler);put64(ex+0xa0,a.target);
  eq(state.descriptors,ex,64);eq(state.descriptors+128,ex+128,64);}
 eq(state.descriptors+64,clear.descriptors+64,64);eq(state.descriptors+192,clear.descriptors+192,4096-192);eq(state.tiler,clear.tiler,sizeof(state.tiler));eq(state.commands,clear.commands,sizeof(state.commands));
 assert(load.page_start==65&&load.page_count==3);
 assert(load.readonly_page_mask==3&&load.executable_page_mask==4);
 struct tegu_render_state prior=state;struct tegu_render_load_state priorLoad=load;
 l.width=32;assert(tegu_render_build_load_pass(&state,pkg,pkgBytes,&a,&l,&load));eq(&state,&prior,sizeof(state));eq(&load,&priorLoad,sizeof(load));l.width=16;
 struct tegu_render_addresses bad=a;
 uint64_t *addressFields[]={&bad.descriptors,&bad.tiler,&bad.geometry,&bad.chunk,&bad.target,&bad.vertex_code,&bad.fragment_code};
 for(unsigned i=0;i<sizeof(addressFields)/sizeof(addressFields[0]);i++) {
  bad=a;addressFields[i]=i==0?&bad.descriptors:i==1?&bad.tiler:i==2?&bad.geometry:i==3?&bad.chunk:
      i==4?&bad.target:i==5?&bad.vertex_code:&bad.fragment_code;
  *addressFields[i]=TEGU_LOAD_RESOURCES_VA;
  assert(tegu_render_build_load_pass(&state,pkg,pkgBytes,&bad,&l,&load));
  eq(&state,&prior,sizeof(state));eq(&load,&priorLoad,sizeof(load));
 }
 bad=a;bad.target=UINT64_C(0x0000fffffffffe00);assert(tegu_render_build_load_pass(&state,pkg,pkgBytes,&bad,&l,&load));eq(&state,&prior,sizeof(state));eq(&load,&priorLoad,sizeof(load));
 size_t bgraBytes;unsigned char *bgra=read_file(argv[7],&bgraBytes);bgra[12]=80;assert(tegu_render_build_load_pass(&state,bgra,bgraBytes,&a,&l,&load));eq(&state,&prior,sizeof(state));eq(&load,&priorLoad,sizeof(load));
 /* Output may not alias package bytes or the other output; failure leaves all
  * caller storage byte-identical. */
 unsigned char *alias=calloc(1,sizeof(load));assert(alias);memcpy(alias,pkg,pkgBytes);
 unsigned char aliasBefore[sizeof(load)];memcpy(aliasBefore,alias,sizeof(aliasBefore));
 assert(tegu_render_build_load_pass(&state,alias,pkgBytes,&a,&l,(struct tegu_render_load_state *)alias));
 eq(alias,aliasBefore,sizeof(aliasBefore));free(alias);
 unsigned char *overlap=calloc(1,sizeof(state)+sizeof(load));assert(overlap);
 struct tegu_render_state *overlapDraw=(struct tegu_render_state *)overlap;
 struct tegu_render_load_state *overlapOut=(struct tegu_render_load_state *)(overlap+sizeof(state)-32);
 unsigned char *overlapBefore=malloc(sizeof(state)+sizeof(load));assert(overlapBefore);
 memcpy(overlapBefore,overlap,sizeof(state)+sizeof(load));
 assert(tegu_render_build_load_pass(overlapDraw,pkg,pkgBytes,&a,&l,overlapOut));
 eq(overlap,overlapBefore,sizeof(state)+sizeof(load));free(overlapBefore);free(overlap);
 free(bgra);free(raw);free(pkg);for(unsigned i=0;i<4;i++)free(fixture[i]);puts(raw?"PASS load pre-frame builder: Mesa golden descriptors, optional r7 byte parity, legacy-state preservation, and refusal cases":"PASS load pre-frame builder: Mesa golden descriptors, legacy-state preservation, and refusal cases");return 0;
}
