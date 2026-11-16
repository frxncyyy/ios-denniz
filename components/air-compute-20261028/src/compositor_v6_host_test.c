#include "native_package_v6.h"
#ifdef TEGU_COMPOSITOR_TEST_STATE
#include "compositor_render_state_v6.h"
#include "compositor_sourceover_mesa_oracle.h"
int tegu_comp_v6_build_target_resources(const void *,size_t,const struct tegu_render_addresses *,uint64_t,uint64_t,const struct tegu_render_target_layout *,struct tegu_render_state *);
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *readall(const char *p,size_t *n){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);*n=ftell(f);rewind(f);unsigned char*b=malloc(*n);assert(b&&fread(b,1,*n,f)==*n);fclose(f);return b;}
static void put(unsigned char*p,size_t o,uint32_t v){p[o]=v;p[o+1]=v>>8;p[o+2]=v>>16;p[o+3]=v>>24;}

#ifdef TEGU_COMPOSITOR_TEST_STATE
static void assert_r57(const struct tegu_render_state *s,uint32_t value) {
 unsigned found=0;
 for(unsigned i=0;i<s->command_count;i++)if((s->commands[i]>>56)==2&&((s->commands[i]>>48)&255)==57){assert((uint32_t)s->commands[i]==value);found++;}
 assert(found==1);
}
static void test_heap2m_descriptor(const unsigned char *package,size_t n) {
 struct tegu_render_addresses legacy={0x880100000,0x880110000,0x880120000,0x880130000,0x880150000,0x880170000,0x880180000,0xff403020,0,0};
 struct tegu_render_target_layout l={TEGU_RENDER_TARGET_LAYOUT_VERSION,16,16,64,1024};
 struct tegu_render_state a,b,large,sentinel;
 assert(!tegu_comp_v6_build_target_resources(package,n,&legacy,0x880190004,0x880190004,&l,&a));
 legacy.chunk_bytes=131072;legacy.hierarchy_mask=1;
 assert(!tegu_comp_v6_build_target_resources(package,n,&legacy,0x880190004,0x880190004,&l,&b));
 assert(!memcmp(&a,&b,sizeof(a))); /* 16x16 remains byte-identical */
 legacy.chunk=0x06077000;legacy.chunk_bytes=0x200000;legacy.hierarchy_mask=0x82;
 assert(!tegu_comp_v6_build_target_resources(package,n,&legacy,0x880190004,0x880190004,&l,&large));
 assert(tegu_render_word(large.tiler+8)==0x82);
 assert(tegu_render_word(large.tiler+0x80)==0x29);
 assert(tegu_render_word(large.tiler+0x84)==0x200000);
 uint64_t base=(uint64_t)tegu_render_word(large.tiler+0x88)|((uint64_t)tegu_render_word(large.tiler+0x8c)<<32);
 uint64_t bottom=(uint64_t)tegu_render_word(large.tiler+0x90)|((uint64_t)tegu_render_word(large.tiler+0x94)<<32);
 uint64_t top=(uint64_t)tegu_render_word(large.tiler+0x98)|((uint64_t)tegu_render_word(large.tiler+0x9c)<<32);
 assert(base==0x06077000&&bottom==base+64&&top==base+0x200000);
 const uint32_t bad[][2]={{0x200000,1},{131072,0x82}};
 for(unsigned i=0;i<2;i++){
   legacy.chunk_bytes=bad[i][0];legacy.hierarchy_mask=bad[i][1];
   memset(&sentinel,0xa5,sizeof(sentinel));struct tegu_render_state before=sentinel;
   assert(tegu_comp_v6_build_target_resources(package,n,&legacy,0x880190004,0x880190004,&l,&sentinel)!=0);
   assert(!memcmp(&sentinel,&before,sizeof(sentinel)));
 }
}
static void test_sourceover_state(const unsigned char *clear,size_t n) {
 unsigned char *over=malloc(n);assert(over);memcpy(over,clear,n);
 const uint32_t words[9]={1,1,0,1,5,0,1,5,15};
 for(unsigned i=0;i<9;i++)put(over,128+4*i,words[i]);
 struct tegu_native_v6_info clear_info,over_info;assert(!tegu_native_v6_parse(clear,n,&clear_info));assert(!tegu_native_v6_parse(over,n,&over_info));
 assert(!clear_info.blend.enabled&&clear_info.blend.rgb_op==1&&clear_info.blend.rgb_dst==0);
 assert(over_info.blend.enabled&&over_info.blend.rgb_op==0&&over_info.blend.rgb_src==1&&over_info.blend.rgb_dst==5&&over_info.blend.alpha_src==1&&over_info.blend.alpha_dst==5);
 struct tegu_render_addresses a={0x880100000,0x880110000,0x880120000,0x880130000,0x880150000,0x880170000,0x880180000,0xff403020,0,0};
 struct tegu_render_target_layout l={TEGU_RENDER_TARGET_LAYOUT_VERSION,16,16,64,1024};
 struct tegu_render_state c,b;assert(!tegu_comp_v6_build_target_resources(clear,n,&a,0x880190004,0x880190004,&l,&c));
 assert(!tegu_comp_v6_build_target_resources(over,n,&a,0x880190004,0x880190004,&l,&b));
 assert_r57(&c,3);assert_r57(&b,2);
 const uint32_t expected[4]={TEGU_MESA_BGRA_SRCOVER_W0,TEGU_MESA_BGRA_SRCOVER_W1,TEGU_MESA_BGRA_SRCOVER_W2,TEGU_MESA_BGRA_SRCOVER_W3};
 for(unsigned i=0;i<4;i++)assert(tegu_render_word(b.descriptors+0x440+4*i)==expected[i]);
 assert(tegu_render_word(c.descriptors+0x440)==0x00000a00&&tegu_render_word(c.descriptors+0x444)==0xf0122122&&tegu_render_word(c.descriptors+0xb30)==0x19&&tegu_render_word(c.descriptors+0xb34)==0x000ed000);
 assert(tegu_render_word(b.descriptors+0xb30)==expected[2]&&tegu_render_word(b.descriptors+0xb34)==expected[3]);
 for(unsigned i=0;i<4096;i++)if(i<0x440||(i>=0x450&&i<0xb30)||i>=0xb38)assert(c.descriptors[i]==b.descriptors[i]);
 assert(!memcmp(c.tiler,b.tiler,sizeof(c.tiler))&&c.command_count==b.command_count);
 for(unsigned i=0;i<c.command_count;i++)if(c.commands[i]!=b.commands[i]){
  assert((c.commands[i]>>56)==2&&((c.commands[i]>>48)&255)==57);
  assert((b.commands[i]>>56)==2&&((b.commands[i]>>48)&255)==57);
  assert((uint32_t)c.commands[i]==3&&(uint32_t)b.commands[i]==2);
 }
 for(unsigned k=0;k<3;k++){
  unsigned char *bad=malloc(n);assert(bad);memcpy(bad,over,n);
  const uint32_t invalid[3][9]={{1,1,0,1,3,0,1,5,15},{1,1,1,1,5,1,1,5,15},{1,1,0,1,5,0,1,5,0}};
  for(unsigned i=0;i<9;i++)put(bad,128+4*i,invalid[k][i]);
  struct tegu_native_v6_info sent;memset(&sent,0x55,sizeof(sent));struct tegu_native_v6_info sent_copy=sent;
  assert(tegu_native_v6_parse(bad,n,&sent)!=0&&!memcmp(&sent,&sent_copy,sizeof(sent)));
  struct tegu_render_state unchanged;memset(&unchanged,0xa5,sizeof(unchanged));struct tegu_render_state saved=unchanged;
  assert(tegu_comp_v6_build_target_resources(bad,n,&a,0x880190004,0x880190004,&l,&unchanged)!=0&&!memcmp(&unchanged,&saved,sizeof(saved)));
  free(bad);
 }
 free(over);
}
#endif
int main(int argc,char**argv){assert(argc==2);size_t n;unsigned char*p=readall(argv[1],&n);put(p,768+4,1);put(p,768+16,48);put(p,768+28+4,1);put(p,768+28+16,48);struct tegu_native_v6_info info;
 assert(tegu_native_v6_parse(p,n,&info)==0);assert(info.attributes[0][1]==1&&info.attributes[1][1]==1&&info.attributes[0][4]==48&&info.attributes[1][4]==48);assert(info.version==6&&info.format==80&&info.stage[0].bytes==1152&&info.stage[1].bytes==7424&&info.stage[1].registers==64&&info.varying_bytes==8&&info.secondary_preload==0x3800000000000000ull);assert(info.attachment_count==2&&info.attachment[0].format==80&&info.attachment[0].write_mask==15&&info.attachment[1].format==80&&info.attachment[1].write_mask==0);assert(info.ubo_metal_slot[0]==2&&info.ubo_metal_slot[1]==3&&info.ubo_metal_slot[2]==1&&info.ubo_metal_slot[3]==6&&info.texture_metal_slot==3&&info.sampler_metal_slot==0);assert(info.ubo_window[0]==64&&info.ubo_window[1]==16&&info.ubo_window[2]==72&&info.ubo_window[3]==2);
#ifdef TEGU_COMPOSITOR_TEST_STATE
 test_sourceover_state(p,n);test_heap2m_descriptor(p,n);
 puts("PASS TGR6 clear/source-over bytes, 16x16 heap-preserving parity, 2MiB hierarchy descriptor, and refusal preservation (host only)");
#endif
 struct tegu_native_v6_binding b={0};b.table1_handle=0x12345004;
 b.ubo_offset[0]=16;b.ubo_offset[1]=16;b.ubo_offset[2]=24;b.ubo_offset[3]=30;
 b.ubo_length[0]=80;b.ubo_length[1]=32;b.ubo_length[2]=96;b.ubo_length[3]=32;
 put(b.vertex_push,0,0);put(b.vertex_push,4,0);put(b.fragment_push,64,8);put(b.fragment_push,68,14);put(b.fragment_push,72,0xd241f2);
 struct tegu_native_v6_checked out;memset(&out,0xa5,sizeof(out));int bindrc=tegu_native_v6_bind(p,n,&b,&out);if(bindrc)fprintf(stderr,"bindrc=%d offsets=%u,%u,%u,%u lengths=%u,%u,%u,%u push=%u,%u,%u,%u tex=%02x%02x%02x%02x\n",bindrc,b.ubo_offset[0],b.ubo_offset[1],b.ubo_offset[2],b.ubo_offset[3],b.ubo_length[0],b.ubo_length[1],b.ubo_length[2],b.ubo_length[3],b.vertex_push[0],b.vertex_push[4],b.fragment_push[64],b.fragment_push[68],b.fragment_push[72],b.fragment_push[73],b.fragment_push[74],b.fragment_push[75]);assert(bindrc==0);assert(out.table1_handle==b.table1_handle&&!memcmp(out.vertex_push,b.vertex_push,128)&&!memcmp(out.fragment_push,b.fragment_push,128));
 assert(out.ubo_base_offset[0]==16&&out.ubo_base_offset[1]==16&&out.ubo_base_offset[2]==16&&out.ubo_base_offset[3]==16);
 assert(out.ubo_window[0]==64&&out.ubo_window[1]==16&&out.ubo_window[2]==72&&out.ubo_window[3]==2);
 struct tegu_native_v6_layout l;assert(tegu_native_v6_layout_build(p,n,0x40000,&l)==0);assert(l.vs_offset==0x2000&&l.fs_offset==0xe000&&l.vs_bytes==1152&&l.fs_bytes==7424&&l.vs_rx&&l.fs_rx);
 unsigned char *scratch=malloc(0x40000);assert(scratch);memset(scratch,0xa7,0x40000);struct tegu_native_v6_layout copied;
 assert(tegu_native_v6_stage_copy(p,n,scratch,0x40000,&copied)==0);assert(!memcmp(&copied,&l,sizeof(l)));
 assert(!memcmp(scratch+0x2000,p+1664,1152)&&!memcmp(scratch+0xe000,p+2816,7424));
 for(unsigned i=1152;i<8192;i++)assert(scratch[0x2000+i]==0);
 for(unsigned i=7424;i<8192;i++)assert(scratch[0xe000+i]==0);
 for(unsigned i=0x4000;i<0x6000;i++)assert(scratch[i]==0xa7);
 for(unsigned i=0xc000;i<0xe000;i++)assert(scratch[i]==0xa7);
 for(unsigned k=0;k<12;k++){unsigned char*q=malloc(n);memcpy(q,p,n);struct tegu_native_v6_info before;memset(&before,0x5a,sizeof(before));struct tegu_native_v6_info save=before;switch(k){case 0:put(q,256+15*4,4);break;case 1:put(q,388+4,15);break;case 2:put(q,256+10*4,4);break;case 3:put(q,256+17*4,0x1f2);break;case 4:put(q,64,65);break;case 5:put(q,108,1);break;case 6:q[768+28+8]=100;break;case 7:q[510]=1;break;case 8:put(q,64,1);break;case 9:put(q,TEGU_NATIVE_V6_PUSH,1);break;case 10:put(q,TEGU_NATIVE_V6_PUSH+64,8);break;case 11:put(q,TEGU_NATIVE_V6_PUSH+72,0xd241f2);break;}int prc=tegu_native_v6_parse(q,n,&before);if(!prc)fprintf(stderr,"malformed case %u accepted\n",k);assert(prc!=0);assert(!memcmp(&before,&save,sizeof(save)));free(q);}
 /* The dynamic FS UBO residuals and texture format are at FS push base +
  * 64/68/72, not at VS push base + those offsets. Keep every such field
  * zero in a reusable package; values are populated per draw by bind(). */
 for(unsigned k=0;k<3;k++){unsigned char*q=malloc(n);assert(q);memcpy(q,p,n);
  size_t off=TEGU_NATIVE_V6_PUSH+128u+64u+4u*k;put(q,off,k==2?0xd241f2u:1u);
  struct tegu_native_v6_info before;memset(&before,0x3c,sizeof(before));
  struct tegu_native_v6_info save=before;
  int prc=tegu_native_v6_parse(q,n,&before);
  if(!prc)fprintf(stderr,"nonzero FS dynamic field %u accepted\n",k);
  assert(prc==-5);assert(!memcmp(&before,&save,sizeof(save)));free(q);
 }
 struct tegu_native_v6_checked unchanged;memset(&unchanged,0x6b,sizeof(unchanged));struct tegu_native_v6_checked sent=unchanged;b.table1_handle=0x12345000;assert(tegu_native_v6_bind(p,n,&b,&unchanged)!=0);assert(!memcmp(&unchanged,&sent,sizeof(sent)));
 b.table1_handle=0x12345004;put(b.fragment_push,72,0x1f2);assert(tegu_native_v6_bind(p,n,&b,&unchanged)!=0);assert(!memcmp(&unchanged,&sent,sizeof(sent)));
 struct tegu_native_v6_binding good=b;
 put(b.vertex_push,0,2);assert(tegu_native_v6_bind(p,n,&b,&unchanged)!=0);assert(!memcmp(&unchanged,&sent,sizeof(sent)));b=good;
 b.ubo_length[2]=95;assert(tegu_native_v6_bind(p,n,&b,&unchanged)!=0);assert(!memcmp(&unchanged,&sent,sizeof(sent)));b=good;
 b.ubo_offset[3]=31;assert(tegu_native_v6_bind(p,n,&b,&unchanged)!=0);assert(!memcmp(&unchanged,&sent,sizeof(sent)));b=good;
 b.vertex_push[8]=1;assert(tegu_native_v6_bind(p,n,&b,&unchanged)!=0);assert(!memcmp(&unchanged,&sent,sizeof(sent)));b=good;
 b.fragment_push[0]=1;assert(tegu_native_v6_bind(p,n,&b,&unchanged)!=0);assert(!memcmp(&unchanged,&sent,sizeof(sent)));b=good;
 struct tegu_native_v6_layout layout_sentinel;memset(&layout_sentinel,0x71,sizeof(layout_sentinel));struct tegu_native_v6_layout layout_before=layout_sentinel;
 assert(tegu_native_v6_layout_build(p,n,0x8000,&layout_sentinel)!=0);assert(!memcmp(&layout_sentinel,&layout_before,sizeof(layout_before)));
 memset(scratch,0x6d,0x40000);assert(tegu_native_v6_stage_copy(p,n,scratch,0x8000,&layout_sentinel)!=0);assert(!memcmp(&layout_sentinel,&layout_before,sizeof(layout_before)));for(unsigned i=0;i<0x40000;i++)assert(scratch[i]==0x6d);free(scratch);
 assert(tegu_native_v6_parse(p,n,(struct tegu_native_v6_info *)(void *)(p+512))!=0);
 /* Preserve every qualified Metal slot/stride combination in package metadata. */
 struct tegu_native_v6_info live;
 for(unsigned slot=0;slot<=1;slot++)for(unsigned stride=32;stride<=48;stride+=8){
  put(p,768+4,slot);put(p,768+28+4,slot);put(p,768+16,stride);put(p,768+28+16,stride);
  assert(tegu_native_v6_parse(p,n,&live)==0&&live.attributes[0][1]==slot&&live.attributes[1][1]==slot&&live.attributes[0][4]==stride&&live.attributes[1][4]==stride);
 }
 /* Reject out-of-class and split slots without modifying the parser output. */
 struct tegu_native_v6_info slot_sentinel;memset(&slot_sentinel,0x39,sizeof(slot_sentinel));
 struct tegu_native_v6_info slot_before=slot_sentinel;
 put(p,768+4,2);put(p,768+28+4,2);assert(tegu_native_v6_parse(p,n,&slot_sentinel)!=0);assert(!memcmp(&slot_sentinel,&slot_before,sizeof(slot_before)));
 put(p,768+4,1);put(p,768+28+4,0);assert(tegu_native_v6_parse(p,n,&slot_sentinel)!=0);assert(!memcmp(&slot_sentinel,&slot_before,sizeof(slot_before)));
 put(p,768+28+4,1);put(p,768+16,48);put(p,768+28+16,40);assert(tegu_native_v6_parse(p,n,&live)!=0);
 puts("PASS TGR6 host parser/binder/layout; full requested MRT metadata retained; per-draw offsets/residuals; 12 malformed package refusals plus all 3 FS dynamic-field refusals preserve output; HOST ONLY, no frontend/kernel/GPU claim");free(p);return 0;}
