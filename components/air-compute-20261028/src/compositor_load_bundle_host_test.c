#include "compositor_load_bundle_v6.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char package[10240];
static const char *fixture_dir;
static uint64_t addr[7]={0x100000,0x101000,0x110000,0x120000,0x200000,0x300000,0x310000};
static void read_exact(const char *p,void *out,size_t n) {
 FILE *f=fopen(p,"rb");assert(f);assert(fread(out,1,n,f)==n);assert(fgetc(f)==EOF);assert(!fclose(f));
}
static uint32_t rd(const unsigned char *p) {
 return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t rd64(const unsigned char *p) {return rd(p)|((uint64_t)rd(p+4)<<32);}
static void test(unsigned w,unsigned h,unsigned stride,const char *tag) {
 char path[512];unsigned char fbd[128],res[192],dcd[4096],shader[128],tex[64],rt[64],sync[16];
#define LOAD(s,n,d) do {snprintf(path,sizeof(path),"%s/%s-%s",fixture_dir,tag,s);read_exact(path,d,n);}while(0)
 LOAD("fbd.bin",128,fbd);LOAD("resource.bin",192,res);LOAD("dcd.bin",4096,dcd);
 LOAD("texture.bin",64,tex);LOAD("rt.bin",64,rt);LOAD("sync.bin",16,sync);
#undef LOAD
 snprintf(path,sizeof(path),"%s/preload_shader_v10.bin",fixture_dir);read_exact(path,shader,sizeof(shader));
 struct tegu_render_addresses a={addr[0],addr[1],addr[2],addr[3],0x10000000,addr[5],addr[6],0x44332211,0,0};
 struct tegu_render_target_layout l={TEGU_RENDER_TARGET_LAYOUT_VERSION,w,h,stride,(uint64_t)stride*h};
 struct tegu_render_state clear;uint8_t vp[128]={0},fp[128]={0};
 int clear_rc=tegu_compositor_render_build_v6(package,sizeof(package),&a,0x400004,&l,1,
  vp,fp,&clear);
 if(clear_rc)fprintf(stderr,"clear state %s refused rc=%d\n",tag,clear_rc);
 assert(!clear_rc);
 struct tegu_load_mapping_input mi={TEGU_LOAD_APERTURE_VA,(uint64_t)stride*h,64,4,8192};
 struct tegu_load_mapping m;assert(!tegu_load_mapping_build(&mi,&m));
 struct tegu_compositor_load_assets_v6 assets={fbd,sizeof(fbd),res,sizeof(res),dcd,sizeof(dcd),
  shader,sizeof(shader),tex,sizeof(tex),rt,sizeof(rt),sync,sizeof(sync)};
 struct tegu_compositor_load_bundle_v6 *bundle=malloc(sizeof(*bundle));assert(bundle);memset(bundle,0xa5,sizeof(*bundle));
 int load_rc=tegu_compositor_load_bundle_build_v6(&clear,&a,&l,&m,&assets,1152,7424,bundle);
 if(load_rc)fprintf(stderr,"LOAD bundle %s refused rc=%d\n",tag,load_rc);
 assert(!load_rc);
 assert(!memcmp(bundle->draw.descriptors+128,clear.descriptors+128,64));
 assert(!memcmp(bundle->draw.descriptors+64,clear.descriptors+64,64));
 assert(!memcmp(bundle->draw.descriptors+192,clear.descriptors+192,4096-192));
 assert((rd(bundle->draw.descriptors)&7)==2);
 assert(rd(bundle->draw.descriptors+32)==((w-1)|((h-1)<<16)));
 assert(rd64(bundle->draw.descriptors+24)==m.dcd_va);
 assert(rd64(bundle->resource_page+0x30)==m.resource_va+0x40);
 assert(rd64(bundle->resource_page+0x48)==a.target);
 assert(rd(bundle->resource_page+0x80)==(uint32_t)m.resource_va);
 assert(rd(bundle->resource_page+0x84)==(0x01000000u|(uint32_t)(m.resource_va>>32)));
 assert(rd64(bundle->resource_page+0x20+16)==m.resource_va+0x40);
 assert(rd64(bundle->resource_page+0x40+8)==a.target);
 unsigned char expected_texplane[64];memcpy(expected_texplane,tex,64);
 expected_texplane[16]=(unsigned char)(m.resource_va+0x40);
 expected_texplane[17]=(unsigned char)((m.resource_va+0x40)>>8);
 expected_texplane[18]=(unsigned char)((m.resource_va+0x40)>>16);
 expected_texplane[19]=(unsigned char)((m.resource_va+0x40)>>24);
 expected_texplane[20]=(unsigned char)((m.resource_va+0x40)>>32);
 expected_texplane[21]=(unsigned char)((m.resource_va+0x40)>>40);
 memset(expected_texplane+22,0,2);
 for(unsigned i=0;i<8;i++)expected_texplane[40+i]=(unsigned char)(a.target>>(8*i));
 assert(!memcmp(bundle->resource_page+0x20,expected_texplane,64));
 assert(rd64(bundle->dcd_page+0x28)==m.dcd_va+0x1e0);
 assert(rd64(bundle->dcd_page+0x30)==(m.dcd_va+0x1c0|1));
 assert(rd64(bundle->dcd_page+0x60)==(m.resource_va+0x80|4));
 assert(rd64(bundle->dcd_page+0x1a8)==m.shader_va);
 assert(!memcmp(bundle->shader_page,shader,128));
 for(unsigned q=128;q<4096;q++)assert(bundle->shader_page[q]==0);
 for(unsigned q=192;q<4096;q++)assert(bundle->resource_page[q]==0);
 assert(bundle->resource_readonly_nx&&bundle->dcd_readonly_nx&&bundle->shader_readonly_x);
 assert(bundle->draw.command_count==clear.command_count+1);
 assert(bundle->draw.commands[0]==(3ull<<56|0xffull<<16));
 assert(bundle->draw.commands[1]==rd64(sync)&&bundle->draw.commands[2]==rd64(sync+8));
 unsigned oi=3;
 for(unsigned ci=0;ci<clear.command_count;ci++) {
  if(clear.commands[ci]==(49ull<<56|1ull<<32)) {ci++;continue;}
  assert(bundle->draw.commands[oi++]==clear.commands[ci]);
 }
 assert(oi==bundle->draw.command_count);
 unsigned nclear=0,nstore=0;for(unsigned i=0;i<bundle->draw.command_count;i++) {
  nclear+=bundle->draw.commands[i]==(49ull<<56|1ull<<32);
  nstore+=bundle->draw.commands[i]==(49ull<<56|3ull<<32);
 }
 assert(nclear==0&&nstore==1);
 /* Failed late template checks preserve every byte of a caller-owned output. */
 memset(bundle,0xa5,sizeof(*bundle));struct tegu_compositor_load_bundle_v6 *before=malloc(sizeof(*before));assert(before);memcpy(before,bundle,sizeof(*before));
 assets.dcd_page=dcd;((unsigned char *)dcd)[0x28]^=1;
 assert(tegu_compositor_load_bundle_build_v6(&clear,&a,&l,&m,&assets,1152,7424,bundle));
 assert(!memcmp(bundle,before,sizeof(*before)));((unsigned char *)dcd)[0x28]^=1;
 ((unsigned char *)tex)[4]^=1;
 assert(tegu_compositor_load_bundle_build_v6(&clear,&a,&l,&m,&assets,1152,7424,bundle));
 assert(!memcmp(bundle,before,sizeof(*before)));((unsigned char *)tex)[4]^=1;
 struct tegu_load_mapping badmap=m;badmap.resource_va=a.target&~4095ull;
 assert(tegu_compositor_load_bundle_build_v6(&clear,&a,&l,&badmap,&assets,1152,7424,bundle));
 assert(!memcmp(bundle,before,sizeof(*before)));
 unsigned char resource_saved[192];memcpy(resource_saved,res,sizeof(res));
 assert(tegu_compositor_load_bundle_build_v6(&clear,&a,&l,&m,&assets,1152,7424,
  (struct tegu_compositor_load_bundle_v6 *)res));
 assert(!memcmp(res,resource_saved,sizeof(res)));
 free(before);free(bundle);
}
int main(int argc,char **argv) {
 assert(argc==3);fixture_dir=argv[1];read_exact(argv[2],package,sizeof(package));
 test(16,16,64,"16x16");test(32,32,128,"32x32");test(256,256,1024,"256x256");
 test(1080,2424,4352,"1080x2424");
 puts("LOAD bundle: actual Mesa BGRA texture/plane, FBD, RT and DCD relocated across four extents; TGR6 RT bytes retained; refusal atomic");
}
