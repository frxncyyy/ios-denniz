#include "render_state.h"
#include "tegu_memory.h"
static void tegu_comp_v6_put64(unsigned char *p,uint64_t value) {
 tegu_render_put(p,(uint32_t)value);tegu_render_put(p+4,(uint32_t)(value>>32));
}
/* Exact IEEE-754 binary32 encoding of n/2 for the bounded target range
 * 16..4096. Keep this integer-only: render-state construction runs in XNU.
 */
static uint32_t tegu_comp_v6_half_dimension_float(uint32_t n) {
 unsigned e=31u-(unsigned)__builtin_clz(n);
 return ((126u+e)<<23)|((n-(1u<<e))<<(23u-e));
}
static void tegu_comp_v6_move32(struct tegu_render_state *s,unsigned r,uint32_t v) {
 s->commands[s->command_count++]=(2ULL<<56)|((uint64_t)r<<48)|v;
}
static void tegu_comp_v6_move64(struct tegu_render_state *s,unsigned r,uint64_t v) {
 if(v<(1ULL<<48))s->commands[s->command_count++]=(1ULL<<56)|((uint64_t)r<<48)|v;
 else {tegu_comp_v6_move32(s,r,(uint32_t)v);tegu_comp_v6_move32(s,r+1,(uint32_t)(v>>32));}
}
int tegu_comp_v6_target_format(uint32_t metal_format,struct tegu_render_target_format *out) {
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
static void tegu_comp_v6_emit(struct tegu_render_state *s,uint64_t v) {s->commands[s->command_count++]=v;}
static void tegu_comp_v6_wait_all(struct tegu_render_state *s) {tegu_comp_v6_emit(s,(3ULL<<56)|(0xffULL<<16));}
int tegu_comp_v6_build_vertex(const void *,size_t,const struct tegu_render_addresses *,uint64_t,struct tegu_render_state *);
int tegu_comp_v6_build_resources(const void *,size_t,const struct tegu_render_addresses *,uint64_t,uint64_t,struct tegu_render_state *);
int tegu_comp_v6_build_target_resources(const void *,size_t,const struct tegu_render_addresses *,uint64_t,uint64_t,const struct tegu_render_target_layout *,struct tegu_render_state *);
int tegu_comp_v6_build(const void *package,size_t size,
 const struct tegu_render_addresses *a,struct tegu_render_state *out) {
 return tegu_comp_v6_build_vertex(package,size,a,0,out);
}
int tegu_comp_v6_build_vertex(const void *package,size_t size,
 const struct tegu_render_addresses *a,uint64_t vertex_resource_table,struct tegu_render_state *out) {
 return tegu_comp_v6_build_resources(package,size,a,vertex_resource_table,0,out);
}
int tegu_comp_v6_build_resources(const void *package,size_t size,
 const struct tegu_render_addresses *a,uint64_t vertex_resource_table,
 uint64_t fragment_resource_table,struct tegu_render_state *out) {
 const struct tegu_render_target_layout legacy={TEGU_RENDER_TARGET_LAYOUT_VERSION,16,16,64,1024};
 return tegu_comp_v6_build_target_resources(package,size,a,vertex_resource_table,
  fragment_resource_table,&legacy,out);
}
static int tegu_comp_target_layout_validate_class(const struct tegu_render_target_layout *l,
 uint64_t *surface_bytes,int b3) {
 if(!l||!surface_bytes||l->version!=TEGU_RENDER_TARGET_LAYOUT_VERSION||
    l->width<16||l->height<16||l->width>4096u||
    l->height>4096u||((l->row_stride&63)&&!(b3&&l->width==776u&&l->height==24u&&l->row_stride==3104u))||
    l->row_stride<l->width*4u||l->row_stride>4096u*16u)return -1;
 uint64_t surface=(uint64_t)l->row_stride*l->height;
 if(surface>(16u*1024u*1024u)||l->target_length<surface||
    l->target_length>(16u*1024u*1024u))return -1;
 *surface_bytes=surface;return 0;
}
int tegu_comp_v6_target_layout_validate(const struct tegu_render_target_layout *l,uint64_t *surface_bytes) {
 return tegu_comp_target_layout_validate_class(l,surface_bytes,0);
}
int tegu_comp_v6_build_target_resources(const void *package,size_t size,
 const struct tegu_render_addresses *a,uint64_t vertex_resource_table,
 uint64_t fragment_resource_table,const struct tegu_render_target_layout *layout,
 struct tegu_render_state *out) {
 struct tegu_render_info info;
 struct tegu_render_target_format target;
 uint64_t surface_bytes=0;
 if(!a||!out||tegu_render_parse(package,size,&info)||
    tegu_comp_target_layout_validate_class(layout,&surface_bytes,info.version==TEGU_RENDER_B3_VERSION)||
    tegu_comp_v6_target_format(info.color_format,&target))return -1;
 uint32_t chunk_bytes=a->chunk_bytes?a->chunk_bytes:131072u;
 uint32_t hierarchy_mask=a->hierarchy_mask?a->hierarchy_mask:1u;
 if((chunk_bytes!=131072u&&chunk_bytes!=0x200000u)||
    (hierarchy_mask!=1u&&hierarchy_mask!=0x82u)||
    ((chunk_bytes==0x200000u)!=(hierarchy_mask==0x82u)))return -2;
 const int compositor=info.version==6||info.version==7||info.version==TEGU_RENDER_B3_VERSION;
 if((info.texture.version!=0)!=(fragment_resource_table!=0)||
    (fragment_resource_table&&((fragment_resource_table&4095)!=4||fragment_resource_table>=(1ULL<<48)))||
    (info.vertex_mask!=0)!=(vertex_resource_table!=0)||
    (vertex_resource_table&&((vertex_resource_table&4095)!=4||vertex_resource_table>=(1ULL<<48)))||
    (compositor&&vertex_resource_table!=fragment_resource_table))return -1;
 uint64_t start[9]={a->descriptors,a->tiler,a->geometry,a->chunk,a->target,a->vertex_code,a->fragment_code};
 uint64_t length[9]={4096,4096,65536,chunk_bytes,surface_bytes,info.stages[0].code_bytes,info.stages[1].code_bytes};
 unsigned ranges=7;
 if(vertex_resource_table){start[ranges]=vertex_resource_table&~4095ULL;length[ranges++]=4096;}
 if(fragment_resource_table&&!compositor){start[ranges]=fragment_resource_table&~4095ULL;length[ranges++]=4096;}
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
 #define D64(off,v) tegu_comp_v6_put64(d+(off),(v))
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
  D32(spd,(i?0x128u:0x38u)|(info.stages[i].registers>32?0:0x80000000u));
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
 uint32_t half_w=tegu_comp_v6_half_dimension_float(layout->width),half_h=tegu_comp_v6_half_dimension_float(layout->height);
 D32(0xa00,half_w);D32(0xa04,half_h);D32(0xa08,0x3f800000);
 D32(0xa10,half_w);D32(0xa14,half_h);D32(0xa20,1);
 D64(0xb30,(uint64_t)blend.words[2]|((uint64_t)blend.words[3]<<32));
 tegu_render_put(t+8,hierarchy_mask);tegu_render_put(t+12,extent);tegu_comp_v6_put64(t+24,a->tiler+0x80);
 tegu_comp_v6_put64(t+32,a->geometry|16u);
 tegu_render_put(t+0x80,0x29);tegu_render_put(t+0x84,chunk_bytes);
 tegu_comp_v6_put64(t+0x88,a->chunk);tegu_comp_v6_put64(t+0x90,a->chunk+64);tegu_comp_v6_put64(t+0x98,a->chunk+chunk_bytes);
 tegu_comp_v6_emit(out,23ULL<<56);tegu_comp_v6_move64(out,72,a->tiler+0xc0);
 tegu_comp_v6_emit(out,(48ULL<<56)|(72ULL<<40));tegu_comp_v6_emit(out,49ULL<<56);tegu_comp_v6_wait_all(out);tegu_comp_v6_emit(out,(34ULL<<56)|0xe);
 const unsigned cleared[]={0,4,12,18,28,46,54};
 for(unsigned i=0;i<7;i++) {
  if(cleared[i]==0&&vertex_resource_table) {
   // Match the hardware-qualified LD_ATTR stream's two-word SRT setup.
   tegu_comp_v6_move32(out,0,(uint32_t)vertex_resource_table);tegu_comp_v6_move32(out,1,(uint32_t)(vertex_resource_table>>32));
  } else tegu_comp_v6_move64(out,cleared[i],cleared[i]==4?fragment_resource_table:cleared[i]==12?
   (va+0xb00)|((uint64_t)((info.stages[1].fau_words+1)/2)<<56):
   cleared[i]==18&&info.secondary_offset!=0?va+0x700:0);
 }
 tegu_comp_v6_move64(out,8,(va+0xa00)|((uint64_t)((info.stages[0].fau_words+1)/2)<<56));
 tegu_comp_v6_move64(out,16,va+0x600);tegu_comp_v6_move64(out,20,va+0x480);tegu_comp_v6_move64(out,24,va+0x4c0);
 for(unsigned r=32;r<40;r++)tegu_comp_v6_move32(out,r,r==33?3:r==34?1:0);
 tegu_comp_v6_move64(out,40,a->tiler);tegu_comp_v6_move64(out,42,(uint64_t)extent<<32);
 tegu_comp_v6_move32(out,44,0);tegu_comp_v6_move32(out,45,0x3f800000);tegu_comp_v6_move32(out,48,info.varying_bytes);
 tegu_comp_v6_move64(out,50,(va+0x440)|1);tegu_comp_v6_move64(out,52,va+0x400);
 tegu_comp_v6_move32(out,56,(3u<<16)|(1u<<22));tegu_comp_v6_move32(out,57,blend.reads_destination?2:3);tegu_comp_v6_move32(out,58,0x1ffff);tegu_comp_v6_move64(out,60,0x3f800000);
 tegu_comp_v6_emit(out,(6ULL<<56)|(1ULL<<33)|(1ULL<<38)|8|(info.secondary_offset!=0?1ULL<<18:0));tegu_comp_v6_emit(out,9ULL<<56);tegu_comp_v6_wait_all(out);
 tegu_comp_v6_emit(out,(49ULL<<56)|(1ULL<<32));tegu_comp_v6_wait_all(out);
 tegu_comp_v6_move64(out,40,va|1);tegu_comp_v6_move32(out,42,0);tegu_comp_v6_move32(out,43,extent);tegu_comp_v6_move64(out,44,0);tegu_comp_v6_move32(out,46,0);
 tegu_comp_v6_emit(out,7ULL<<56);tegu_comp_v6_wait_all(out);tegu_comp_v6_emit(out,(49ULL<<56)|(3ULL<<32));tegu_comp_v6_wait_all(out);
 tegu_comp_v6_move32(out,74,0);tegu_comp_v6_emit(out,(36ULL<<56)|(74ULL<<40)|0x211);tegu_comp_v6_emit(out,(3ULL<<56)|(1ULL<<16));tegu_comp_v6_emit(out,34ULL<<56);
 #undef D32
 #undef D64
 return 0;
}


static int tegu_compositor_render_build_native(const void *package,size_t size,
 const struct tegu_render_addresses *a,uint64_t resource_table,
 const struct tegu_render_target_layout *layout,uint32_t attachment_mask,
 const uint8_t vertex_push[128],const uint8_t fragment_push[128],
 struct tegu_render_state *out,unsigned expected_version)
{
 struct tegu_render_info info;
 if(!package||!a||!layout||!vertex_push||!fragment_push||!out||
    tegu_render_parse(package,size,&info)||info.version!=expected_version||
    !tegu_render_compositor_pass_supported(&info,attachment_mask)||
    info.compositor.version!=1||!resource_table||
    (resource_table&4095u)!=4||resource_table>=(1ull<<48))return -1;
 if(tegu_comp_v6_build_target_resources(package,size,a,resource_table,
      resource_table,layout,out))return -2;
 /* Native v6 builder emitted bounded package snapshots; override only the five
  * per-draw dynamic words after every validation has succeeded. */
 for(unsigned i=0;i<4;i++) {
  uint32_t field=info.compositor.push_residual_offset[i];
  uint8_t *dst=i<2?out->descriptors+0xa80+field:
      out->descriptors+0xb80+field;
  const uint8_t *src=i<2?vertex_push:fragment_push;
  uint32_t source=field;
  for(unsigned j=0;j<4;j++)dst[j]=src[source+j];
 }
 for(unsigned j=0;j<4;j++)out->descriptors[0xb80+72+j]=fragment_push[72+j];
 return 0;
}

int tegu_compositor_render_build_v6(const void *package,size_t size,
 const struct tegu_render_addresses *a,uint64_t resource_table,
 const struct tegu_render_target_layout *layout,uint32_t attachment_mask,
 const uint8_t vertex_push[128],const uint8_t fragment_push[128],struct tegu_render_state *out) {
 return tegu_compositor_render_build_native(package,size,a,resource_table,layout,attachment_mask,vertex_push,fragment_push,out,6);
}

int tegu_compositor_render_build_v7(const void *package,size_t size,
 const struct tegu_render_addresses *a,uint64_t resource_table,
 const struct tegu_render_target_layout *layout,uint32_t attachment_mask,
 const uint8_t vertex_push[128],const uint8_t fragment_push[128],struct tegu_render_state *out) {
 return tegu_compositor_render_build_native(package,size,a,resource_table,layout,attachment_mask,vertex_push,fragment_push,out,7);
}

int tegu_compositor_render_build_b3(const void *package,size_t size,
 const struct tegu_render_addresses *a,uint64_t resource_table,
 const struct tegu_render_target_layout *layout,uint32_t attachment_mask,
 const uint8_t vertex_push[128],const uint8_t fragment_push[128],struct tegu_render_state *out) {
 return tegu_compositor_render_build_native(package,size,a,resource_table,layout,attachment_mask,vertex_push,fragment_push,out,TEGU_RENDER_B3_VERSION);
}
