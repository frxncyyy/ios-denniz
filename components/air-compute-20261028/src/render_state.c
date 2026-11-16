#include "render_state.h"
#include "blend_state.c"
#include "native_package_v6.c"
#include "render_pipeline_v6.c"
#include "native_package_v7.c"
#include "render_pipeline_v7.c"
#include "native_package_b3.c"
#include "render_pipeline_b3.c"
static void put64(unsigned char *p,uint64_t value) {
 tegu_render_put(p,(uint32_t)value);tegu_render_put(p+4,(uint32_t)(value>>32));
}
/* Exact IEEE-754 binary32 encoding of n/2 for the bounded target range
 * 16..256. Keep this integer-only: render-state construction runs in XNU.
 */
static uint32_t half_dimension_float(uint32_t n) {
 unsigned e=31u-(unsigned)__builtin_clz(n);
 return ((126u+e)<<23)|((n-(1u<<e))<<(23u-e));
}
static void move32(struct tegu_render_state *s,unsigned r,uint32_t v) {
 s->commands[s->command_count++]=(2ULL<<56)|((uint64_t)r<<48)|v;
}
static void move64(struct tegu_render_state *s,unsigned r,uint64_t v) {
 if(v<(1ULL<<48))s->commands[s->command_count++]=(1ULL<<56)|((uint64_t)r<<48)|v;
 else {move32(s,r,(uint32_t)v);move32(s,r+1,(uint32_t)(v>>32));}
}
int tegu_render_target_format(uint32_t metal_format,struct tegu_render_target_format *out) {
 /* Logical channels in target memory bytes: RGBA 0x688, BGRA 0x60a. */
 static const unsigned char rgba[4]={0,1,2,3},bgra[4]={2,1,0,3};
 const unsigned char *order=metal_format==70?rgba:metal_format==80?bgra:0;
 if(!order||!out)return -1;
 struct tegu_render_target_format f={metal_format,1,19,0,0,0,TEGU_RENDER_CLEAR_LOGICAL_RGBA8,{0}};
 for(unsigned i=0;i<4;i++) {f.swizzle|=(uint32_t)order[i]<<(3*i);f.storage_channel[i]=order[i];}
 f.word0=f.internal_format<<26;
 f.word1=1u|(f.writeback_format<<3)|(2u<<8)|(f.swizzle<<16)|(1u<<31);
 *out=f;return 0;
}
static void emit(struct tegu_render_state *s,uint64_t v) {s->commands[s->command_count++]=v;}
static void wait_all(struct tegu_render_state *s) {emit(s,(3ULL<<56)|(0xffULL<<16));}
int tegu_render_build(const void *package,size_t size,
 const struct tegu_render_addresses *a,struct tegu_render_state *out) {
 return tegu_render_build_vertex(package,size,a,0,out);
}
int tegu_render_build_vertex(const void *package,size_t size,
 const struct tegu_render_addresses *a,uint64_t vertex_resource_table,struct tegu_render_state *out) {
 return tegu_render_build_resources(package,size,a,vertex_resource_table,0,out);
}
int tegu_render_build_resources(const void *package,size_t size,
 const struct tegu_render_addresses *a,uint64_t vertex_resource_table,
 uint64_t fragment_resource_table,struct tegu_render_state *out) {
 const struct tegu_render_target_layout legacy={TEGU_RENDER_TARGET_LAYOUT_VERSION,16,16,64,1024};
 return tegu_render_build_target_resources(package,size,a,vertex_resource_table,
  fragment_resource_table,&legacy,out);
}
int tegu_render_target_layout_validate(const struct tegu_render_target_layout *l,uint64_t *surface_bytes) {
 if(!l||!surface_bytes||l->version!=TEGU_RENDER_TARGET_LAYOUT_VERSION||
    l->width<16||l->height<16||l->width>TEGU_RENDER_TARGET_MAX_DIMENSION||
    l->height>TEGU_RENDER_TARGET_MAX_DIMENSION||(l->row_stride&63)||
    l->row_stride<l->width*4u||l->row_stride>TEGU_RENDER_TARGET_MAX_DIMENSION*16u)return -1;
 uint64_t surface=(uint64_t)l->row_stride*l->height;
 if(surface>TEGU_RENDER_TARGET_MAX_BYTES||l->target_length<surface||
    l->target_length>TEGU_RENDER_TARGET_MAX_BYTES)return -1;
 *surface_bytes=surface;return 0;
}
int tegu_render_build_target_resources(const void *package,size_t size,
 const struct tegu_render_addresses *a,uint64_t vertex_resource_table,
 uint64_t fragment_resource_table,const struct tegu_render_target_layout *layout,
 struct tegu_render_state *out) {
 struct tegu_render_info info;
 struct tegu_render_target_format target;
 uint64_t surface_bytes=0;
 if(tegu_render_target_layout_validate(layout,&surface_bytes))return -1;
 if(!a||!out||tegu_render_parse(package,size,&info)||
    !tegu_render_legacy_target_supported(&info)||info.version==6||
    tegu_render_target_format(info.color_format,&target))return -1;
 if((info.texture.version!=0)!=(fragment_resource_table!=0)||
    (fragment_resource_table&&((fragment_resource_table&4095)!=4||fragment_resource_table>=(1ULL<<48)))||
    (info.vertex_mask!=0)!=(vertex_resource_table!=0)||
    (vertex_resource_table&&((vertex_resource_table&4095)!=4||vertex_resource_table>=(1ULL<<48))))return -1;
 uint64_t start[9]={a->descriptors,a->tiler,a->geometry,a->chunk,a->target,a->vertex_code,a->fragment_code};
 uint64_t length[9]={4096,4096,65536,131072,surface_bytes,info.stages[0].code_bytes,info.stages[1].code_bytes};
 unsigned ranges=7;
 if(vertex_resource_table){start[ranges]=vertex_resource_table&~4095ULL;length[ranges++]=4096;}
 if(fragment_resource_table){start[ranges]=fragment_resource_table&~4095ULL;length[ranges++]=4096;}
 for(unsigned i=0;i<ranges;i++) {
  if(!start[i]||(start[i]&((i<4)?4095u:63u))||start[i]>((1ULL<<48)-length[i]))return -2;
  for(unsigned j=0;j<i;j++)if(start[i]<start[j]+length[j]&&start[j]<start[i]+length[i])return -2;
 }
 struct tegu_blend_state blend;
 if(tegu_blend_build_format(info.color_format,&info.blend,&blend))return -3;
 const unsigned char *p=(const unsigned char *)package;
 unsigned char *raw=(unsigned char *)out;for(size_t i=0;i<sizeof(*out);i++)raw[i]=0;
 unsigned char *d=out->descriptors,*t=out->tiler;uint64_t va=a->descriptors;
 #define D32(off,v) tegu_render_put(d+(off),(v))
 #define D64(off,v) put64(d+(off),(v))
 uint32_t extent=(layout->width-1u)|((layout->height-1u)<<16);
 D64(16,va+0x100);D32(32,extent);D32(40,extent);
 D32(44,(1u<<24)|(8u<<9)|(2u<<6));D32(48,1u<<16);D64(56,a->tiler);
 D32(128,target.word0);D32(132,target.word1);
 D64(160,a->target);D32(168,layout->row_stride);D32(172,(uint32_t)surface_bytes);
 for(unsigned i=0;i<4;i++)D32(176+i*4,a->clear_rgba);
 D32(0x100,0x00800080);D32(0x180,0x00800080);
 D32(0x400,7u|(7u<<16));D32(0x410,(7u<<29)|(1u<<22)|(1u<<23)|(2u<<25));
 for(unsigned i=0;i<4;i++)D32(0x440+i*4,blend.words[i]);
 D32(0x4c4,31);
 for(unsigned i=0;i<2;i++) {
  unsigned spd=i?0x480:0x600,fau=i?0xb00:0xa00;
  D32(spd,(i?0x128u:0x38u)|0x80000000u);
  D32(spd+4,(uint32_t)(info.stages[i].preload>>48));
  D64(spd+8,i?a->fragment_code:a->vertex_code+info.position_offset);
  for(unsigned j=0;j<128;j++)d[fau+128+j]=p[info.push_offset+128*i+j];
 }
 // Version3 IDVS varying program: same SRT0/FAU0/TSD0 as the position program.
 if(info.secondary_offset!=0) {
  D32(0x700,0x80000038u);D32(0x704,(uint32_t)(info.secondary_preload>>48));
  D64(0x708,a->vertex_code+info.secondary_offset);
 }
 // Viewport scale/offset are half the target dimensions; Z remains unchanged.
 uint32_t half_w=half_dimension_float(layout->width),half_h=half_dimension_float(layout->height);
 D32(0xa00,half_w);D32(0xa04,half_h);D32(0xa08,0x3f800000);
 D32(0xa10,half_w);D32(0xa14,half_h);D32(0xa20,1);
 D64(0xb30,(uint64_t)blend.words[2]|((uint64_t)blend.words[3]<<32));
 tegu_render_put(t+8,1);tegu_render_put(t+12,extent);put64(t+24,a->tiler+0x80);
 put64(t+32,a->geometry|16u);
 tegu_render_put(t+0x80,0x29);tegu_render_put(t+0x84,131072);
 put64(t+0x88,a->chunk);put64(t+0x90,a->chunk+64);put64(t+0x98,a->chunk+131072);
 emit(out,23ULL<<56);move64(out,72,a->tiler+0xc0);
 emit(out,(48ULL<<56)|(72ULL<<40));emit(out,49ULL<<56);wait_all(out);emit(out,(34ULL<<56)|0xe);
 const unsigned cleared[]={0,4,12,18,28,46,54};
 for(unsigned i=0;i<7;i++) {
  if(cleared[i]==0&&vertex_resource_table) {
   // Match the hardware-qualified LD_ATTR stream's two-word SRT setup.
   move32(out,0,(uint32_t)vertex_resource_table);move32(out,1,(uint32_t)(vertex_resource_table>>32));
  } else move64(out,cleared[i],cleared[i]==4?fragment_resource_table:cleared[i]==12?
   (va+0xb00)|((uint64_t)((info.stages[1].fau_words+1)/2)<<56):
   cleared[i]==18&&info.secondary_offset!=0?va+0x700:0);
 }
 move64(out,8,(va+0xa00)|((uint64_t)((info.stages[0].fau_words+1)/2)<<56));
 move64(out,16,va+0x600);move64(out,20,va+0x480);move64(out,24,va+0x4c0);
 for(unsigned r=32;r<40;r++)move32(out,r,r==33?3:r==34?1:0);
 move64(out,40,a->tiler);move64(out,42,(uint64_t)extent<<32);
 move32(out,44,0);move32(out,45,0x3f800000);move32(out,48,info.varying_bytes);
 move64(out,50,(va+0x440)|1);move64(out,52,va+0x400);
 move32(out,56,(3u<<16)|(1u<<22));move32(out,57,blend.reads_destination?2:3);move32(out,58,0x1ffff);move64(out,60,0x3f800000);
 emit(out,(6ULL<<56)|(1ULL<<33)|(1ULL<<38)|8|(info.secondary_offset!=0?1ULL<<18:0));emit(out,9ULL<<56);wait_all(out);
 emit(out,(49ULL<<56)|(1ULL<<32));wait_all(out);
 move64(out,40,va|1);move32(out,42,0);move32(out,43,extent);move64(out,44,0);move32(out,46,0);
 emit(out,7ULL<<56);wait_all(out);emit(out,(49ULL<<56)|(3ULL<<32));wait_all(out);
 move32(out,74,0);emit(out,(36ULL<<56)|(74ULL<<40)|0x211);emit(out,(3ULL<<56)|(1ULL<<16));emit(out,34ULL<<56);
 #undef D32
 #undef D64
 return 0;
}

#include "render_load.c"
