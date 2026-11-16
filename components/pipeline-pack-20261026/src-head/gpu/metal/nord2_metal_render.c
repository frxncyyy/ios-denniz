#include "nord2_metal_render.h"
#include "nord2_mesa_pack.h"
#include <string.h>
#define PACK(at,T,v) MALI_##T##_pack((MALI_##T##_PACKED_T *)(void *)(at),&(v))
static void gd_put32(uint8_t *p,uint32_t x){memcpy(p,&x,4);}
static void gd_put64(uint8_t *p,uint64_t x){memcpy(p,&x,8);}
static const struct n2r_texture_binding *n2r_texture_lookup(
 const struct n2r_texture_resources *r,uint32_t binding);
/* Last refusal site (source line) of n2r validation/build, for the quiet
 * device build's TEGU_COMMAND_FAILED record. Diagnostic only. */
static volatile int n2r_last_reject;
/* nord2_render_resources.inc sites are reported as 100000+line. */
#define N2R_SITE_BASE 0
#define N2R_FAIL(value) do{n2r_last_reject=N2R_SITE_BASE+__LINE__;return (value);}while(0)
int n2r_last_reject_line(void){return n2r_last_reject;}
#undef N2R_SITE_BASE
#define N2R_SITE_BASE 100000
#include "nord2_render_resources.inc"
#undef N2R_SITE_BASE
#define N2R_SITE_BASE 0
#define SRT 2304u
#define ATTR 2368u
#define T1 4096u
#define T2 5120u
#define PLANES 6144u
#define CONSTANT 7168u
#define ZERO 7680u
int n2r_clear(unsigned format,const double rgba[4],uint32_t out[4]){
 if(!rgba||!out||!n2r_bpp(format))return -1;
 uint32_t packed=0;
 if(format==115){
  uint16_t words[4];for(unsigned i=0;i<4;i++){
   if(rgba[i]!=rgba[i]||rgba[i]>65504||rgba[i]<-65504)return -1;
   _Float16 value=(_Float16)rgba[i];memcpy(words+i,&value,2);
  }
  memcpy(out,words,8);memcpy(out+2,words,8);return 0;
 }
 if(format==25||format==65){
  uint16_t h[4];for(unsigned i=0;i<4;i++){
   if(rgba[i]!=rgba[i]||rgba[i]>65504||rgba[i]<-65504)return -1;
   _Float16 value=(_Float16)rgba[i];memcpy(h+i,&value,2);
  }
  uint32_t word=(uint32_t)h[0]|((uint32_t)h[1]<<16);
  out[0]=out[1]=out[2]=out[3]=word;return 0;
 }
 if(format==55){
  float f[4];for(unsigned i=0;i<4;i++){if(rgba[i]!=rgba[i])return -1;f[i]=(float)rgba[i];}
  memcpy(out,&f[0],4);out[1]=out[2]=out[3]=out[0];return 0;
 }
 if(format==125){
  float f[4];for(unsigned i=0;i<4;i++){if(rgba[i]!=rgba[i])return -1;f[i]=(float)rgba[i];memcpy(out+i,&f[i],4);}return 0;
 }
 if(format==30){
  uint32_t word=0;for(unsigned i=0;i<2;i++){double v=rgba[i];if(v!=v)return -1;if(v<0)v=0;if(v>1)v=1;word|=(uint32_t)(v*255.0+0.5)<<(8*i);}
  out[0]=out[1]=out[2]=out[3]=word;return 0;
 }
 if(format==110){
  uint32_t w[2]={0,0};for(unsigned i=0;i<4;i++){double v=rgba[i];if(v!=v)return -1;if(v<0)v=0;if(v>1)v=1;w[i/2]|=(uint32_t)(v*65535.0+0.5)<<((i&1)*16);}
  out[0]=out[2]=w[0];out[1]=out[3]=w[1];return 0;
 }
 if(format==43){
  /* Mesa pan_pack_color, internal R5G5B5A1 tile layout {5.5,5.5,5.5,1.1},
   * not dithered: roundeven(c*(2^m-1)) << frac, R at bit 0. */
  static const unsigned shift[4]={5,15,25,31},bits[4]={5,5,5,1};
  uint32_t word=0;
  for(unsigned i=0;i<4;i++){double v=rgba[i];if(v!=v)return -1;if(v<0)v=0;if(v>1)v=1;
   /* Round half to even without libm (freestanding iOS objects). */
   double scaled=v*(double)((1u<<bits[i])-1u);uint32_t r=(uint32_t)scaled;double f=scaled-(double)r;
   if(f>0.5||(f==0.5&&(r&1u)))r++;
   word|=r<<shift[i];}
  out[0]=out[1]=out[2]=out[3]=word;return 0;
 }
 if(format==90||format==94){
  uint32_t word=0;for(unsigned i=0;i<3;i++){double v=rgba[i];if(v!=v)return -1;if(v<0)v=0;if(v>1)v=1;word|=(uint32_t)(v*1023.0+0.5)<<(10*i);}
  double a=rgba[3];if(a!=a)return -1;if(a<0)a=0;if(a>1)a=1;word|=(uint32_t)(a*3.0+0.5)<<30;
  out[0]=out[1]=out[2]=out[3]=word;return 0;
 }
 for(unsigned i=0;i<4;i++){
  double value=rgba[i];if(value!=value)return -1;
  if(format==10&&i==3)value=1;
  if(value<0)value=0;if(value>1)value=1;
  packed|=(uint32_t)(value*255.0+0.5)<<(8*i);
 }
 for(unsigned i=0;i<4;i++)out[i]=packed;
 return 0;
}
static int n2r_texture_resources_check(const struct tegu_generic_draw_request *q,
 const struct n2r_texture_resources *tr){
 /* Bound the embedded request array before any indexing.  A malformed count
  * must not reach the sidecar/request scans or gd_request_ok. */
 if(!q||q->binding_count>TEGU_GENERIC_DRAW_MAX_BINDINGS)N2R_FAIL(-1);
 if(!tr)return 0;
 if(tr->version!=N2R_TEXTURE_RESOURCES_VERSION||(!tr->bindings&&tr->count)||tr->count>TEGU_GENERIC_DRAW_MAX_BINDINGS)N2R_FAIL(-1);
 for(uint32_t x=0;x<tr->count;x++){
  const struct n2r_texture_binding *tb=&tr->bindings[x];
  if(tb->binding>=TEGU_GENERIC_DRAW_MAX_BINDINGS)N2R_FAIL(-1);
  for(uint32_t y=0;y<x;y++)if(tr->bindings[y].binding==tb->binding)N2R_FAIL(-1);
  uint32_t j=UINT32_MAX;
  for(uint32_t y=0;y<q->binding_count;y++)if(q->bindings[y].binding==tb->binding){j=y;break;}
  if(j==UINT32_MAX||tb->kind!=q->bindings[j].kind)N2R_FAIL(-1);
  switch(tb->kind){
  case TEGU_GENERIC_DRAW_COMBINED:case TEGU_GENERIC_DRAW_COMBINED_IMMUTABLE:
  case TEGU_GENERIC_DRAW_TEXTURE:
   if(!tb->backing_length||tb->backing_length>q->bindings[j].memory.bytes||
      tb->layout.version!=N2T_TEXTURE_RESOURCES_VERSION||!(tb->layout.access&N2T_ACCESS_SAMPLE)||
      (q->bindings[j].format&&tb->layout.format!=q->bindings[j].format)||
      n2t_layout_check(&tb->layout,0,tb->backing_length))N2R_FAIL(-1);
   if(tb->kind!=TEGU_GENERIC_DRAW_TEXTURE){
    if(tb->sampler.version!=N2T_TEXTURE_RESOURCES_VERSION||
       n2t_sampler_layout_check(&tb->sampler,&tb->layout))N2R_FAIL(-1);
   }else if(tb->sampler.version==N2T_TEXTURE_RESOURCES_VERSION&&
            n2t_sampler_layout_check(&tb->sampler,&tb->layout))N2R_FAIL(-1);
   break;
  case TEGU_GENERIC_DRAW_SAMPLER:
   if(tb->sampler.version!=N2T_TEXTURE_RESOURCES_VERSION||n2t_sampler_validate(&tb->sampler))N2R_FAIL(-1);
   break;
  default:N2R_FAIL(-1);
  }
 }
 return 0;
}
static int n2r_render_targets_check(const struct tegu_generic_draw_request *,const struct n2r_render_targets *);
/* Mesa pan_get_stack_shift: log2(ceil(bytes/16)); the v9 field stores the
 * shift, not the byte count. */
uint32_t n2r_thread_storage_shift(uint32_t bytes){
 uint32_t units=(bytes+15u)/16u,shift=0;while((1u<<shift)<units)shift++;return shift;
}
int n2r_thread_storage_total(uint32_t bytes,uint32_t threads_per_core,
 uint32_t cores,uint64_t *out){
 if(!out||bytes>N2R_THREAD_STORAGE_MAX||!threads_per_core||!cores){if(out)*out=0;return -1;}
 if(!bytes){*out=0;return 0;}
 uint64_t per=((uint64_t)bytes+15u)&~15ull,pow=1;
 while(pow<per)pow<<=1;per=pow;
 if(per>UINT64_MAX/threads_per_core)return -1;
 per*=threads_per_core;if(per>UINT64_MAX/cores)return -1;
 *out=per*cores;return 0;
}
static int n2r_validate_impl(const struct tegu_generic_draw_shader *s,const struct tegu_generic_draw_request *q,const struct n2r_texture_resources *tr,const struct n2r_draw_options *opts,const struct n2r_render_targets *rts){
 if(!s||!q||n2r_texture_resources_check(q,tr)||n2r_render_targets_check(q,rts)||!gd_request_ok(s,q,tr)||q->viewport_valid>1)N2R_FAIL(-1);
 if(opts) {
  if(opts->version!=N2R_DRAW_OPTIONS_VERSION||
     (opts->flags&~(N2R_DRAW_FLAT_SHADING|N2R_DRAW_WRITE_VERTEX_BASE|N2R_DRAW_WRITE_BASE_INSTANCE))||
     opts->thread_storage_bytes>N2R_THREAD_STORAGE_MAX||opts->thread_storage_reserved)N2R_FAIL(-1);
  if((opts->flags&N2R_DRAW_WRITE_VERTEX_BASE)&&s->vertex_fau_words<17)N2R_FAIL(-1);
  if((opts->flags&N2R_DRAW_WRITE_BASE_INSTANCE)&&s->vertex_fau_words<18)N2R_FAIL(-1);
  /* Pinned v9.xml has no DRAW.flat_shading_vertex.  Do not fabricate the
   * legacy P<=7 line-DRAW bit.  TILER_CONTEXT.first_provoking_vertex is the
   * arch9 control; flat line lists are explicitly unresolved in STATUS,
   * so they are refused here rather than drawn with the wrong vertex. */
  if((opts->flags&N2R_DRAW_FLAT_SHADING)&&q->primitive==TEGU_GENERIC_DRAW_LINES)N2R_FAIL(-1);
 }
 if(q->viewport_valid){
  for(unsigned i=0;i<6;i++)if(!(q->viewport[i]>=-16384&&q->viewport[i]<=16384))N2R_FAIL(-1);
  if(q->viewport[2]<=0||q->viewport[3]<=0||q->viewport[4]<0||q->viewport[4]>1||q->viewport[5]<0||q->viewport[5]>1)N2R_FAIL(-1);
 }
 return 0;
}
int n2r_validate(const struct tegu_generic_draw_shader *s,const struct tegu_generic_draw_request *q){
 return n2r_validate_impl(s,q,0,0,0);
}
int n2r_validate_v2(const struct tegu_generic_draw_shader *s,const struct tegu_generic_draw_request *q,const struct n2r_texture_resources *tr){
 return n2r_validate_impl(s,q,tr,0,0);
}
int n2r_validate_v3(const struct tegu_generic_draw_shader *s,const struct tegu_generic_draw_request *q,const struct n2r_texture_resources *tr,const struct n2r_draw_options *opts){
 return n2r_validate_impl(s,q,tr,opts,0);
}
int n2r_validate_v4(const struct tegu_generic_draw_shader *s,const struct tegu_generic_draw_request *q,const struct n2r_texture_resources *tr,const struct n2r_draw_options *opts,const struct n2r_render_targets *rts){
 return n2r_validate_impl(s,q,tr,opts,rts);
}
static uint32_t viewport_bound(float value,uint32_t limit){
 /* Mesa panfrost_emit_viewport (arch9): truncate, then clamp to the target.
  * n2r_validate bounds finite origins/extents before their sum reaches here. */
 if(value<=0)return 0;if(value>=limit)return limit;return (uint32_t)value;
}
static int viewport_scissor(const struct tegu_generic_draw_request *q,uint32_t box[4]){
 box[0]=q->scissor[0];box[1]=q->scissor[1];
 box[2]=box[0]+q->scissor[2];box[3]=box[1]+q->scissor[3];
 if(q->viewport_valid){
  for(unsigned axis=0;axis<2;axis++){
   uint32_t limit=axis?q->height:q->width;
   uint32_t lo=viewport_bound(q->viewport[axis],limit);
   uint32_t hi=viewport_bound(q->viewport[axis]+q->viewport[axis+2],limit);
   if(lo>box[axis])box[axis]=lo;
   if(hi<box[axis+2])box[axis+2]=hi;
  }
 }
 if(box[0]>=box[2]||box[1]>=box[3])return 0;
 box[2]--;box[3]--; /* hardware maxima are inclusive */
 return 1;
}
static const struct n2r_texture_binding *n2r_texture_lookup(
 const struct n2r_texture_resources *r,uint32_t binding){
 if(!r||r->version!=N2R_TEXTURE_RESOURCES_VERSION||(!r->bindings&&r->count)||
    r->count>TEGU_GENERIC_DRAW_MAX_BINDINGS)return 0;
 for(uint32_t i=0;i<r->count;i++)if(r->bindings[i].binding==binding)return &r->bindings[i];
 return 0;
}

static uint32_t n2r_target_format(const struct tegu_generic_draw_request *q,
 const struct n2r_render_targets *rts,uint32_t i)
{
 if(rts) return rts->targets[i].format?rts->targets[i].format:80;
 return q->target_format?q->target_format:80;
}
static int n2r_rt_format(unsigned f,unsigned *internal,unsigned *writeback,unsigned *swizzle){
 if(!internal||!writeback||!swizzle)return -1;
 switch(f){
 case 10:*internal=MALI_COLOR_BUFFER_INTERNAL_FORMAT_R8G8B8A8;*writeback=MALI_COLOR_FORMAT_R8;*swizzle=0x688;break;
 case 70:*internal=MALI_COLOR_BUFFER_INTERNAL_FORMAT_R8G8B8A8;*writeback=MALI_COLOR_FORMAT_R8G8B8A8;*swizzle=0x688;break;
 case 80:*internal=MALI_COLOR_BUFFER_INTERNAL_FORMAT_R8G8B8A8;*writeback=MALI_COLOR_FORMAT_R8G8B8A8;*swizzle=0x60a;break;
 case 115:*internal=MALI_COLOR_BUFFER_INTERNAL_FORMAT_RGBA16F;*writeback=MALI_FLOAT_COLOR_FORMAT_RGBA16F;*swizzle=0x688;break;
 case 90:*internal=MALI_COLOR_BUFFER_INTERNAL_FORMAT_R10G10B10A2;*writeback=MALI_COLOR_FORMAT_R10G10B10A2;*swizzle=0x688;break;
 case 94:*internal=MALI_COLOR_BUFFER_INTERNAL_FORMAT_R10G10B10A2;*writeback=MALI_COLOR_FORMAT_R10G10B10A2;*swizzle=0x60a;break;
 case 65:*internal=MALI_COLOR_BUFFER_INTERNAL_FORMAT_RG16F;*writeback=MALI_FLOAT_COLOR_FORMAT_RG16F;*swizzle=0x688;break;
 case 25:*internal=MALI_COLOR_BUFFER_INTERNAL_FORMAT_R16F;*writeback=MALI_FLOAT_COLOR_FORMAT_R16F;*swizzle=0x688;break;
 case 55:*internal=MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW32;*writeback=MALI_COLOR_FORMAT_RAW32;*swizzle=0x688;break;
 case 125:*internal=MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW128;*writeback=MALI_COLOR_FORMAT_RAW128;*swizzle=0x688;break;
 case 30:*internal=MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW16;*writeback=MALI_COLOR_FORMAT_RAW16;*swizzle=0x688;break;
 /* Mesa BFMT(B5G5R5A1_UNORM,R5G5B5A1): blendable, B in the low bits like BGRA8. */
 case 43:*internal=MALI_COLOR_BUFFER_INTERNAL_FORMAT_R5G5B5A1;*writeback=MALI_COLOR_FORMAT_R5G5B5A1;*swizzle=0x60a;break;
 case 110:*internal=MALI_COLOR_BUFFER_INTERNAL_FORMAT_RAW64;*writeback=MALI_COLOR_FORMAT_RAW64;*swizzle=0x688;break;
 default:return -1;
 }
 return 0;
}
static int n2r_blend_build_format(unsigned f,const struct tegu_blend_inputs *in,struct tegu_blend_state *out){
 int rc=tegu_blend_build_format(f,in,out);if(!rc)return 0;
 if(!in||in->version!=1||in->enabled||tegu_blend_build(in,out))return -1;
 if(in->write_mask!=15){out->reads_destination=1;out->opaque=0;out->words[0]|=1;out->words[2]=(out->words[2]&~3u)|2;}
 switch(f){
 case 90:case 94:out->words[3]=MALI_RGB10_A2_TB<<12;break;
 case 65:out->words[3]=MALI_RG16F<<12;break;
 case 25:out->words[3]=MALI_R16F<<12;break;
 case 55:out->words[3]=MALI_RGBA8_TB<<12;break; /* raw writeback, disabled blend */
 case 125:out->words[3]=MALI_RGBA8_TB<<12;break;
 case 30:out->words[3]=MALI_RGBA8_TB<<12;break;
 case 110:out->words[3]=MALI_RGBA8_TB<<12;break;
 default:return -1;
 }
 return 0;
}
static const struct n2r_resolve_target *n2r_resolve_for(
 const struct n2r_render_targets *rts,uint32_t i)
{
 return rts&&rts->version==N2R_RENDER_TARGETS_MSAA_VERSION&&rts->resolves&&
  rts->resolves[i].address?&rts->resolves[i]:0;
}
static uint32_t n2r_sample_count_for(const struct n2r_render_targets *rts)
{
 return rts&&rts->version==N2R_RENDER_TARGETS_MSAA_VERSION?rts->sample_counts[0]:1;
}
static int n2r_render_targets_check(const struct tegu_generic_draw_request *q,
 const struct n2r_render_targets *rts)
{
 if(!rts)return 0;
 if((rts->version!=N2R_RENDER_TARGETS_VERSION&&rts->version!=N2R_RENDER_TARGETS_BLEND_VERSION&&
     rts->version!=N2R_RENDER_TARGETS_MSAA_VERSION)||!rts->targets||rts->count==0||
    rts->count>N2R_MAX_RENDER_TARGETS)N2R_FAIL(-1);
 if(rts->version!=N2R_RENDER_TARGETS_VERSION&&rts->version!=N2R_RENDER_TARGETS_MSAA_VERSION&&!rts->blends)N2R_FAIL(-1);
 if(rts->version==N2R_RENDER_TARGETS_MSAA_VERSION&&!rts->sample_counts)N2R_FAIL(-1);
 uint32_t samples=1;
 if(rts->version==N2R_RENDER_TARGETS_MSAA_VERSION) {
  samples=rts->sample_counts[0];
  if(samples!=1&&samples!=4)N2R_FAIL(-1);
  for(uint32_t i=1;i<rts->count;i++)if(rts->sample_counts[i]!=samples)N2R_FAIL(-1);
 }
 if(rts->count>2&&!rts->sample_locations)N2R_FAIL(-1);
 if(rts->sample_locations&&(rts->sample_locations&63u))N2R_FAIL(-1);
 for(uint32_t i=0;i<rts->count;i++) {
  const struct n2r_render_target *t=&rts->targets[i];uint32_t f=n2r_target_format(q,rts,i);
  uint64_t bytes;
  if(!n2r_bpp(f)||!t->address||(t->address&63u)||t->address>(1ULL<<48)||t->load_action>2||
     (t->write_mask&~15u)||t->reserved||t->width!=q->width||t->height!=q->height||
     (t->row_bytes&63u)||t->row_bytes<q->width*n2r_bpp(f))N2R_FAIL(-1);
  bytes=(uint64_t)t->row_bytes*t->height*samples;
  if(!bytes||bytes>(1ULL<<48)||t->address>(1ULL<<48)-bytes)N2R_FAIL(-1);
  for(uint32_t j=0;j<i;j++)if(rts->targets[j].address==t->address)N2R_FAIL(-1);
  if(rts->version==N2R_RENDER_TARGETS_BLEND_VERSION||
     (rts->version==N2R_RENDER_TARGETS_MSAA_VERSION&&rts->blends)) {
   struct tegu_blend_state checked;
   if(rts->blends[i].write_mask!=t->write_mask||
      n2r_blend_build_format(f,&rts->blends[i],&checked))N2R_FAIL(-1);
  }
 }
 if(rts->version==N2R_RENDER_TARGETS_MSAA_VERSION) {
  if(rts->resolves)for(uint32_t i=0;i<rts->count;i++) {
   const struct n2r_render_target *t=&rts->targets[i];const struct n2r_resolve_target *r=&rts->resolves[i];
   uint32_t f=r->format?r->format:t->format;uint64_t bytes;
   if(!r->address)continue;
   if(samples!=4||f!=t->format||!n2r_bpp(f)||!r->width||!r->height||
      r->width!=q->width||r->height!=q->height||(r->row_bytes&63u)||
      r->row_bytes<q->width*n2r_bpp(f)||r->reserved||r->address>(1ULL<<48)||
      (r->address&63u))N2R_FAIL(-1);
   bytes=(uint64_t)r->row_bytes*r->height;
   if(!bytes||bytes>(1ULL<<48)||r->address>(1ULL<<48)-bytes)N2R_FAIL(-1);
   for(uint32_t j=0;j<rts->count;j++) {
    if(r->address==rts->targets[j].address||
       (j<i&&rts->resolves[j].address&&r->address==rts->resolves[j].address))N2R_FAIL(-1);
   }
  }
 }
 return 0;
}

static int resources(uint8_t *m,const struct tegu_generic_draw_shader *s,
 const struct tegu_generic_draw_request *q,const struct n2r_addresses *a,
 const struct n2r_texture_resources *tr,const struct n2r_draw_options *opts,
 uint32_t *payload_cursor){
 uint64_t va=a->command;
 if(n2r_texture_resources_check(q,tr))N2R_FAIL(-1);
 gd_resource(m+SRT,va+ATTR,18*32);gd_resource(m+SRT+16,va+T1,32*32);gd_resource(m+SRT+32,va+T2,32*32);
 uint32_t seen=0;
 for(unsigned i=0;i<s->attribute_count;i++){
  const struct tegu_vertex_attribute *v=&s->attributes[i];uint64_t end;
  struct tegu_vertex_draw draw={0,q->vertex_count,1,0};
  uint64_t attr_offset=v->offset;
  if(opts&&(opts->flags&N2R_DRAW_WRITE_BASE_INSTANCE)&&v->input_rate) {
   /* The hardware fetch table is per-instance; BaseInstance is not already
    * folded into q->vertex (render only folds vertexStart for per-vertex). */
   draw.base_instance=opts->base_instance;
   uint64_t add=(uint64_t)opts->base_instance*v->stride;
   if(add>0x7fffffffull-attr_offset)N2R_FAIL(-1);
   attr_offset+=add;
  }
  if(v->location>=16||(seen&(1u<<v->location))||v->buffer_index!=s->attributes[0].buffer_index||
     tegu_vertex_attribute_end(v,&draw,&end)||end>q->vertex.bytes||!a->vertex)N2R_FAIL(-1);
  seen|=1u<<v->location;
  uint32_t fmt;switch(v->format){case 83:fmt=(207u<<12)|16;break;case 97:fmt=223u<<12;break;case 100:fmt=(167u<<12)|16;break;
   case 103:fmt=(175u<<12)|16;break;case 106:fmt=(183u<<12)|16;break;case 109:fmt=191u<<12;break;default:N2R_FAIL(-1);}
  uint8_t *p=m+ATTR+v->location*32;
  gd_put32(p,5u|16u|(!v->input_rate?1u<<8:0)|(fmt<<10));gd_put32(p+4,v->input_rate<<6);
  gd_put32(p+8,(uint32_t)attr_offset);gd_put32(p+12,17);gd_put32(p+16,v->input_rate&&!v->divisor?0:v->stride);
 }
 gd_put32(m+ATTR+16*32,0x02a99901u);
 if(s->attribute_count)gd_buffer(m+ATTR+17*32,a->vertex,q->vertex.bytes);
 memcpy(m+CONSTANT,q->constant,q->constant_bytes);
 for(unsigned i=0;i<q->binding_count;i++){
  const struct tegu_generic_draw_binding *b=&q->bindings[i];uint8_t *t=m+T1+b->binding*32,*sam=m+T2+b->binding*32,*p=m+PLANES+b->binding*32;
  uint64_t at=a->binding[i],plane=va+PLANES+b->binding*32;
  const struct n2r_texture_binding *tb=n2r_texture_lookup(tr,b->binding);
  if(tb&&tb->kind!=b->kind)N2R_FAIL(-1);
  if(b->memory.token&&!at)N2R_FAIL(-1);
  switch(b->kind){
  case TEGU_GENERIC_DRAW_BUFFER:
   if(tb)N2R_FAIL(-1);gd_buffer(t,at,b->memory.bytes);break;
  case TEGU_GENERIC_DRAW_CONSTANT:
   if(tb)N2R_FAIL(-1);gd_buffer(t,va+CONSTANT,(q->constant_bytes+15)&~15u);break;
  case TEGU_GENERIC_DRAW_COMBINED:case TEGU_GENERIC_DRAW_COMBINED_IMMUTABLE:case TEGU_GENERIC_DRAW_TEXTURE:
   if(tb){
    struct n2t_packed_texture packed;struct n2t_format_info fi;
    uint64_t descriptor=va+T1+b->binding*32;
    /* The bridge resolves exactly memory.bytes from the binding base and
     * retains the whole BO.  The sidecar may not claim more than that span. */
    if(!at||!tb->backing_length||tb->backing_length>b->memory.bytes||
       tb->layout.version!=N2T_TEXTURE_RESOURCES_VERSION||
       (b->format&&tb->layout.format!=b->format)||!tb->layout.access||
       n2t_format_query(tb->layout.view_format?tb->layout.view_format:tb->layout.format,&fi))N2R_FAIL(-1);
    if(*payload_cursor>N2R_PAYLOAD_BYTES||
       n2t_layout_pack_sampled(&tb->layout,at,tb->backing_length,descriptor,
        va+N2R_PAYLOAD+*payload_cursor,m+N2R_PAYLOAD+*payload_cursor,
        N2R_PAYLOAD_BYTES-*payload_cursor,&packed))N2R_FAIL(-1);
    n2t_texture_copy_descriptor(&packed,t);
    if(b->kind!=TEGU_GENERIC_DRAW_TEXTURE){
     if(tb->sampler.version!=N2T_TEXTURE_RESOURCES_VERSION||
        n2t_sampler_layout_check(&tb->sampler,&tb->layout)||
        n2t_sampler_pack_for_format(&tb->sampler,&fi,sam))N2R_FAIL(-1);
    }else if(tb->sampler.version==N2T_TEXTURE_RESOURCES_VERSION){
     if(n2t_sampler_pack_for_format(&tb->sampler,&fi,sam))N2R_FAIL(-1);
    }else if(gd_sampler(sam,0))N2R_FAIL(-1);
    *payload_cursor+=packed.payload_bytes;
    break;
   }
   if(tr&&b->memory.bytes!=gd_texture_expected_bytes(b))N2R_FAIL(-1);
   gd_texture(t,p,plane,at,b->width,b->height,b->row_bytes,b->format);
   if(gd_sampler(sam,b->kind==TEGU_GENERIC_DRAW_TEXTURE?0:b->sampler))N2R_FAIL(-1);break;
  case TEGU_GENERIC_DRAW_SAMPLER:
   if(tb){
    if(tb->sampler.version!=N2T_TEXTURE_RESOURCES_VERSION||n2t_sampler_pack(&tb->sampler,sam))N2R_FAIL(-1);
   }else if(gd_sampler(sam,b->sampler))N2R_FAIL(-1);
   break;
  case TEGU_GENERIC_DRAW_NEUTRAL_IMAGE:
   if(tb)N2R_FAIL(-1);gd_texture(t,p,plane,va+ZERO,1,1,64,80);if(gd_sampler(sam,0))N2R_FAIL(-1);break;
  case TEGU_GENERIC_DRAW_NEUTRAL_BUFFER:
   if(tb)N2R_FAIL(-1);gd_buffer(t,va+ZERO,256);break;
  case TEGU_GENERIC_DRAW_ATTACHMENT:
   if(tb)N2R_FAIL(-1);
   if(!a->snapshot)N2R_FAIL(-1);gd_texture(t,p,plane,a->snapshot,q->width,q->height,q->row_bytes,q->target_format?q->target_format:80);
   if(gd_sampler(sam,0))N2R_FAIL(-1);break;
  case TEGU_GENERIC_DRAW_TILE_ATTACHMENT:
   if(tb)N2R_FAIL(-1);break;
  default:N2R_FAIL(-1);
  }
 }
 return 0;
}
static int n2r_build_impl(uint8_t *m,const struct tegu_generic_draw_shader *s,
 const struct tegu_generic_draw_request *q,const struct n2r_addresses *a,
 const struct n2r_texture_resources *tr,const struct n2r_draw_options *opts,
 const struct n2r_render_targets *rts,uint16_t index,uint64_t next){
 if(!m||!a||n2r_validate_impl(s,q,tr,opts,rts)||!a->target||!a->heap||a->heap_bytes<(2u<<20)||a->heap_bytes>UINT32_MAX||
    (a->heap_bytes&4095)||a->heap_chunk_enum1>4||(a->hierarchy_mask&~0xfffu)||a->no_secondary>1||
    !a->command||(a->command&16383)||!a->frame||(a->frame&16383)||!a->vertex_code||!a->fragment_code)N2R_FAIL(-1);
 if(opts&&opts->thread_storage_bytes&&(!a->tls||(a->tls&4095u)||
    (a->tls>>8)>0xffffffffffffull))N2R_FAIL(-1);
 uint32_t target_count=rts?rts->count:1;
 uint32_t samples=n2r_sample_count_for(rts);
 struct tegu_blend_state blends[N2R_MAX_RENDER_TARGETS];
 uint32_t target_mask=0;
 for(uint32_t i=0;i<target_count;i++) {
  struct tegu_blend_inputs bi=s->blend;
  if(rts&&(rts->version==N2R_RENDER_TARGETS_BLEND_VERSION||
      (rts->version==N2R_RENDER_TARGETS_MSAA_VERSION&&rts->blends)))bi=rts->blends[i];
  else if(rts)bi.write_mask=rts->targets[i].write_mask;
  if(n2r_blend_build_format(n2r_target_format(q,rts,i),&bi,&blends[i]))N2R_FAIL(-1);
  /* Mesa pan_blend_get_internal_desc(fmt, rt,...) stamps the target index in
   * MALI_BLEND.internal.fixed_function.rt.  Only the new version2 path has
   * per-target descriptors; version1 stays byte-identical (rt0). */
  if(rts&&(rts->version==N2R_RENDER_TARGETS_BLEND_VERSION||
      (rts->version==N2R_RENDER_TARGETS_MSAA_VERSION&&rts->blends)))
   blends[i].words[2]=(blends[i].words[2]&~0x000f0000u)|((i&15u)<<16);
  if(rts&&rts->targets[i].write_mask)target_mask|=1u<<i;
 }
 uint32_t payload_cursor=0;
 memset(m,0,N2R_BLOCK);if(resources(m,s,q,a,tr,opts,&payload_cursor))N2R_FAIL(-1);
 /* An explicit sample-location record must live inside this command block.
  * The default keeps the C2 layout: framebuffer points at frame+256 and the
  * record is written at command+256; the legacy single-RT LOAD preload job
  * uses command=frame+N2R_BLOCK, so the default must not be range-checked
  * against command (that rejected every LOAD draw on hardware). */
 int explicit_sl=rts&&rts->sample_locations;
 uint64_t sample_locations=explicit_sl?rts->sample_locations:a->frame+256;
 uint64_t sl_off=explicit_sl?sample_locations-a->command:256u;
 if(explicit_sl&&(sample_locations<a->command||sample_locations>a->command+N2R_BLOCK-256||
    (sample_locations&63u)))N2R_FAIL(-1);
 if(payload_cursor&&a->command+sl_off<a->command+N2R_PAYLOAD+payload_cursor+256&&
    a->command+sl_off+256>a->command+N2R_PAYLOAD)N2R_FAIL(-1);
 struct MALI_FRAMEBUFFER_PARAMETERS fb={MALI_FRAMEBUFFER_PARAMETERS_header};
 fb.width=q->width;fb.height=q->height;fb.bound_max_x=q->width-1;fb.bound_max_y=q->height-1;
 fb.sample_locations=sample_locations;fb.tiler=a->frame+512;fb.effective_tile_size=256;
 fb.render_target_count=target_count;
 unsigned qf=q->target_format?q->target_format:80;
 fb.color_buffer_allocation=rts?0u:((qf==10||qf==70||qf==80||qf==115)?
  (qf==115?2048:1024):n2r_tib_bytes(qf)*256u);
 /* Tile-buffer, not memory, bytes: an R8 or BGR5A1 target is 32-bit there. */
 if(rts)for(uint32_t i=0;i<target_count;i++)fb.color_buffer_allocation+=n2r_tib_bytes(n2r_target_format(q,rts,i))*256u*samples;
 if(rts)fb.color_buffer_allocation=(fb.color_buffer_allocation+1023u)&~1023u;
 if(rts&&rts->version==N2R_RENDER_TARGETS_MSAA_VERSION) {
  fb.sample_count=samples;
  fb.sample_pattern=samples==4?MALI_SAMPLE_PATTERN_ROTATED_4X_GRID:MALI_SAMPLE_PATTERN_SINGLE_SAMPLED;
 }
 fb.z_internal_format=MALI_Z_INTERNAL_FORMAT_D24;
 fb.tie_break_rule=MALI_TIE_BREAK_RULE_MINUS_180_IN_0_OUT;
 PACK(m,FRAMEBUFFER_PARAMETERS,fb);
 uint32_t tile_offset=0;
 for(uint32_t i=0;i<target_count;i++) {
  uint32_t f=n2r_target_format(q,rts,i);uint32_t bpp=n2r_bpp(f);
  const struct n2r_resolve_target *resolve=n2r_resolve_for(rts,i);
  uint32_t wf=resolve?(resolve->format?resolve->format:f):f;
  struct MALI_RGB_RENDER_TARGET rt={MALI_RGB_RENDER_TARGET_header};
  unsigned internal=0,writeback=0,swizzle=0;
  if(n2r_rt_format(wf,&internal,&writeback,&swizzle))N2R_FAIL(-1);
  rt.internal_buffer_offset=tile_offset;rt.internal_format=internal;rt.write_enable=true;
  rt.writeback_format=writeback;rt.writeback_float_format=writeback;rt.writeback_block_format=MALI_BLOCK_FORMAT_LINEAR;
  rt.writeback_msaa=samples==4?(resolve?MALI_MSAA_AVERAGE:MALI_MSAA_LAYERED):MALI_MSAA_SINGLE;
  rt.swizzle=swizzle;rt.clean_tile_write_enable=(rts?rts->targets[i].load_action:q->load_action)!=1;
  rt.writeback_buffer.base=resolve?resolve->address:(rts?rts->targets[i].address:a->target);
  rt.writeback_buffer.row_stride=resolve?resolve->row_bytes:(rts?rts->targets[i].row_bytes:q->row_bytes);
  rt.writeback_buffer.surface_stride=(resolve?resolve->row_bytes:(rts?rts->targets[i].row_bytes:q->row_bytes))*
   (resolve?resolve->height:(rts?rts->targets[i].height:q->height));
  if(rts) {
   rt.clear.color_0=rts->targets[i].clear_words[0];rt.clear.color_1=rts->targets[i].clear_words[1];
   rt.clear.color_2=rts->targets[i].clear_words[2];rt.clear.color_3=rts->targets[i].clear_words[3];
   tile_offset+=n2r_tib_bytes(f)*256u*samples;(void)bpp;
  } else {
   rt.clear.color_0=rt.clear.color_1=rt.clear.color_2=rt.clear.color_3=q->clear_color;
   if(q->target_format){rt.clear.color_0=q->clear_words[0];rt.clear.color_1=q->clear_words[1];rt.clear.color_2=q->clear_words[2];rt.clear.color_3=q->clear_words[3];}
  }
  PACK(m+128+64u*i,RGB_RENDER_TARGET,rt);
 }
 if(samples==4) {
  /* Mesa pan_samples.c rotated-4x LUT: four positions, origin at slot32. */
  static const uint32_t rotated4[64]={
   0x00500070u,0x007000b0u,0x00900050u,0x00b00090u,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0x00800080u,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
  };
  for(unsigned i=0;i<64;i++)gd_put32(m+sl_off+i*4,rotated4[i]);
 } else {
  for(unsigned i=0;i<64;i++)gd_put32(m+sl_off+i*4,0x00800080);
 }
 struct MALI_TILER_CONTEXT tiler={MALI_TILER_CONTEXT_header};
 tiler.fb_width=q->width;tiler.fb_height=q->height;tiler.hierarchy_mask=a->hierarchy_mask?a->hierarchy_mask:0x82;tiler.heap=a->frame+768;
 tiler.first_provoking_vertex=!!(opts&&(opts->flags&N2R_DRAW_FLAT_SHADING));
 PACK(m+512,TILER_CONTEXT,tiler);
 struct MALI_TILER_HEAP heap={MALI_TILER_HEAP_header};heap.size=a->heap_bytes;if(a->heap_chunk_enum1)heap.chunk_size=a->heap_chunk_enum1-1;
 heap.base=a->heap;heap.bottom=a->heap;heap.top=a->heap+a->heap_bytes;PACK(m+768,TILER_HEAP,heap);
 struct MALI_LOCAL_STORAGE tls={MALI_LOCAL_STORAGE_header};
 if(opts&&opts->thread_storage_bytes) {
  tls.tls_size=n2r_thread_storage_shift(opts->thread_storage_bytes);
  tls.tls_address_mode=MALI_ADDRESS_MODE_PACKED;
  tls.tls_base_pointer=a->tls>>8;
 }
 PACK(m+832,LOCAL_STORAGE,tls);
 struct MALI_DEPTH_STENCIL ds={MALI_DEPTH_STENCIL_header};ds.depth_function=MALI_FUNC_ALWAYS;
 ds.front_compare_function=ds.back_compare_function=MALI_FUNC_ALWAYS;PACK(m+896,DEPTH_STENCIL,ds);
 for(uint32_t i=0;i<target_count;i++)memcpy(m+928+16u*i,blends[i].words,16);
 if(s->flags&TEGU_GENERIC_SHADER_TILE_FETCH)gd_put32(m+928,blends[0].words[0]|1);
 struct MALI_SHADER_PROGRAM programs[3]={{MALI_SHADER_PROGRAM_header},{MALI_SHADER_PROGRAM_header},{MALI_SHADER_PROGRAM_header}};
 const uint32_t regs[3]={s->vertex_registers,s->secondary_registers,s->fragment_registers};
 const uint64_t preload[3]={s->vertex_preload,s->secondary_preload,s->fragment_preload};
 const uint64_t code[3]={a->vertex_code+s->position_offset,a->vertex_code+s->secondary_offset,a->fragment_code};
 for(unsigned i=0;i<3;i++){
  programs[i].stage=i==2?MALI_SHADER_STAGE_FRAGMENT:MALI_SHADER_STAGE_VERTEX;
  /* JM IDVS position and varying shaders have different warp limits.
   * Match Mesa's arch9 panfrost_upload_shader (also panvk shader SPDs):
   * position uses HALF; varying uses FULL. The zero default is FULL. */
  if(i<2)programs[i].vertex_warp_limit=i?MALI_WARP_LIMIT_FULL:MALI_WARP_LIMIT_HALF;
  programs[i].register_allocation=regs[i]<=32?MALI_SHADER_REGISTER_ALLOCATION_32_PER_THREAD:MALI_SHADER_REGISTER_ALLOCATION_64_PER_THREAD;
  programs[i].preload.r48_r63=preload[i]>>48;programs[i].binary=code[i];
  if(i==2){programs[i].fragment_coverage_bitmask_type=MALI_COVERAGE_BITMASK_TYPE_GL;programs[i].requires_helper_threads=true;}
  PACK(m+1536+32*i,SHADER_PROGRAM,programs[i]);
 }
 float hw=q->width*0.5f,hh=q->height*0.5f;float viewport[8]={hw,-hh,1,0,hw,hh,0,0};
 if(q->viewport_valid){viewport[0]=q->viewport[2]*.5f;viewport[1]=-q->viewport[3]*.5f;viewport[2]=q->viewport[5]-q->viewport[4];viewport[4]=q->viewport[0]+q->viewport[2]*.5f;viewport[5]=q->viewport[1]+q->viewport[3]*.5f;viewport[6]=q->viewport[4];}
 memcpy(m+1664,viewport,sizeof(viewport));
 if(opts&&(opts->flags&N2R_DRAW_WRITE_VERTEX_BASE))memcpy(m+1664+64,&opts->vertex_base,4);
 if(opts&&(opts->flags&N2R_DRAW_WRITE_BASE_INSTANCE))memcpy(m+1664+68,&opts->base_instance,4);
 memcpy(m+1664+128,q->vertex_push,q->vertex_push_bytes);
 memcpy(m+1920+128,q->fragment_push,q->fragment_push_bytes);memcpy(m+1920+48,blends[0].words+2,8);
 uint32_t clip[4];int visible=viewport_scissor(q,clip);
 /* Keep the dependency chain and fragment CLEAR/LOAD work even when this
  * draw is completely clipped. Never subtract one from an empty bound. */
 if(!visible){clip[0]=clip[1]=1;clip[2]=clip[3]=0;}
 struct MALI_JOB_HEADER header={MALI_JOB_HEADER_header};header.type=visible?MALI_JOB_TYPE_MALLOC_VERTEX:MALI_JOB_TYPE_NULL;header.index=index;
 header.dependency_1=index>1?index-1:0;header.next=next;PACK(m+N2R_JOB,JOB_HEADER,header);
 struct MALI_PRIMITIVE primitive={MALI_PRIMITIVE_header};primitive.draw_mode=q->primitive==1?MALI_DRAW_MODE_LINES:q->primitive==2?MALI_DRAW_MODE_TRIANGLE_STRIP:MALI_DRAW_MODE_TRIANGLES;
 primitive.index_type=q->index_size==2?MALI_INDEX_TYPE_UINT16:q->index_size==4?MALI_INDEX_TYPE_UINT32:MALI_INDEX_TYPE_NONE;
 primitive.index_count=q->vertex_count;primitive.secondary_shader=!a->no_secondary;PACK(m+N2R_JOB+32,PRIMITIVE,primitive);
 /* Mesa jm_emit_malloc_vertex_job: "no varyings" requires stride16/0. */
 gd_put32(m+N2R_JOB+48,1);gd_put32(m+N2R_JOB+52,a->no_secondary?16u:(s->varying_bytes+16)|(s->varying_bytes<<16));
 gd_put64(m+N2R_JOB+56,a->frame+512);gd_put32(m+N2R_JOB+104,clip[0]|(clip[1]<<16));
 gd_put32(m+N2R_JOB+108,clip[2]|(clip[3]<<16));
 gd_put32(m+N2R_JOB+112,0x3f800000);gd_put64(m+N2R_JOB+120,a->index);
 struct MALI_DRAW draw={MALI_DRAW_header};draw.vertex_array.packet=true;draw.maximum_z=1;
 if(q->viewport_valid){
  draw.minimum_z=q->viewport[4]<q->viewport[5]?q->viewport[4]:q->viewport[5];
  draw.maximum_z=q->viewport[4]>q->viewport[5]?q->viewport[4]:q->viewport[5];
 }
 draw.depth_stencil=a->command+896;draw.blend=a->command+928;draw.blend_count=target_count;
 draw.flags_0.pixel_kill_operation=MALI_PIXEL_KILL_FORCE_LATE;draw.flags_0.zs_update_operation=MALI_PIXEL_KILL_FORCE_LATE;
 draw.flags_0.shader_modifies_coverage=true;draw.flags_1.sample_mask=0xffff;draw.flags_1.render_target_mask=rts?target_mask:1;
 draw.shader.resources=(a->command+SRT)|4;draw.shader.shader=a->command+1600;draw.shader.thread_storage=a->frame+832;
 draw.shader.fau=a->command+1920;draw.shader.fau_count=(s->fragment_fau_words+1)/2;PACK(m+N2R_JOB+128,DRAW,draw);
 struct MALI_SHADER_ENVIRONMENT vs={MALI_SHADER_ENVIRONMENT_header};vs.resources=(a->command+SRT)|4;vs.shader=a->command+1536;
 vs.thread_storage=a->frame+832;vs.fau=a->command+1664;vs.fau_count=(s->vertex_fau_words+1)/2;PACK(m+N2R_JOB+256,SHADER_ENVIRONMENT,vs);
 vs.shader=a->command+1568;if(!a->no_secondary)PACK(m+N2R_JOB+320,SHADER_ENVIRONMENT,vs);
 struct MALI_FRAMEBUFFER_POINTER fp={MALI_FRAMEBUFFER_POINTER_header};fp.pointer=a->frame;fp.render_target_count=target_count;
 struct mali_framebuffer_pointer_packed fpp;PACK(&fpp,FRAMEBUFFER_POINTER,fp);uint64_t tagged;memcpy(&tagged,&fpp,8);
 header=(struct MALI_JOB_HEADER){MALI_JOB_HEADER_header};header.type=MALI_JOB_TYPE_FRAGMENT;header.index=1;PACK(m+N2R_FRAGMENT,JOB_HEADER,header);
 struct MALI_FRAGMENT_JOB_PAYLOAD fragment={MALI_FRAGMENT_JOB_PAYLOAD_header};fragment.bound_max_x=(q->width-1)>>4;fragment.bound_max_y=(q->height-1)>>4;fragment.framebuffer=tagged;
 PACK(m+N2R_FRAGMENT+32,FRAGMENT_JOB_PAYLOAD,fragment);return 0;
}
int n2r_build(uint8_t *m,const struct tegu_generic_draw_shader *s,
 const struct tegu_generic_draw_request *q,const struct n2r_addresses *a,uint16_t index,uint64_t next){
 return n2r_build_impl(m,s,q,a,0,0,0,index,next);
}
int n2r_build_v2(uint8_t *m,const struct tegu_generic_draw_shader *s,
 const struct tegu_generic_draw_request *q,const struct n2r_addresses *a,
 const struct n2r_texture_resources *tr,uint16_t index,uint64_t next){
 return n2r_build_impl(m,s,q,a,tr,0,0,index,next);
}
int n2r_build_v3(uint8_t *m,const struct tegu_generic_draw_shader *s,
 const struct tegu_generic_draw_request *q,const struct n2r_addresses *a,
 const struct n2r_texture_resources *tr,const struct n2r_draw_options *opts,
 uint16_t index,uint64_t next){
 return n2r_build_impl(m,s,q,a,tr,opts,0,index,next);
}
int n2r_build_v4(uint8_t *m,const struct tegu_generic_draw_shader *s,
 const struct tegu_generic_draw_request *q,const struct n2r_addresses *a,
 const struct n2r_texture_resources *tr,const struct n2r_draw_options *opts,
 const struct n2r_render_targets *rts,uint16_t index,uint64_t next){
 return n2r_build_impl(m,s,q,a,tr,opts,rts,index,next);
}
uint32_t n2r_mesa_hierarchy_mask(uint32_t width,uint32_t height,uint32_t tiler_features,uint64_t heap_bytes){
 /* Verbatim logic of Mesa 81daea66 src/panfrost/lib/pan_desc.c for
  * PAN_ARCH 9 with tile_size 0 (JM). max_levels as pan_props.c:29. */
 static const uint32_t default_mask[]={0,0x80,0x82,0xa2,0xaa,0xea,0xee,0xfe};
 unsigned max_levels=(tiler_features>>8)&15,maxwh=width>height?width:height,cover=(maxwh+15)/16,last=0;
 if(max_levels<2||!width||!height)return 0;
 while(cover>>last)last++;
 uint32_t mask;
 if(max_levels<8){mask=default_mask[max_levels];max_levels=8;}else mask=(1u<<max_levels)-1;
 if(last>max_levels)mask<<=last-max_levels;
 for(unsigned disable=0;disable<12;){
  unsigned bx=(width+15)/16,by=(height+15)/16;uint64_t enabled=0;
  for(unsigned i=0;i<12;i++){if(mask&(1u<<i))enabled+=(uint64_t)bx*by;bx=(bx+1)/2;by=(by+1)/2;}
  if((enabled+7)/8*8*8<heap_bytes)break;
  disable++;mask&=~((1u<<disable)-1);
 }
 return mask;
}
