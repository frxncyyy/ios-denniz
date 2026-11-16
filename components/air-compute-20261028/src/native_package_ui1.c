#include "native_package_ui1.h"
#include "tegu_memory.h"

#define UI1_MAGIC 0x31554654u /* TFU1 */
#define UI1_PAIR_OFFSET 112u
#define UI1_ATTRIBUTE_OFFSET 272u
#define UI1_RESOURCE_OFFSET 384u
#define UI1_PUSH_OFFSET 504u
#define UI1_CONSTANT_OFFSET 536u
#define UI1_TARGET_OFFSET 552u
#define UI1_NAME_OFFSET 592u
#define UI1_VA_LIMIT (UINT64_C(1)<<48)

static uint32_t ui1_rd32(const uint8_t *p) {
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t ui1_rd64(const uint8_t *p) {return ui1_rd32(p)|(uint64_t)ui1_rd32(p+4)<<32;}
static int ui1_span(const void *p,size_t n) {return p&&n&&(uintptr_t)p<=UINTPTR_MAX-n;}
static int ui1_overlap(const void *a,size_t an,const void *b,size_t bn) {
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return an&&bn&&(x<=y?y-x<an:x-y<bn);
}
static int ui1_add64(uint64_t a,uint64_t b,uint64_t *out) {
 if(a>UINT64_MAX-b)return 0;
 *out=a+b;return 1;
}
static int ui1_range_overlap(uint64_t ao,uint64_t a,uint64_t an,uint64_t bo,uint64_t b,uint64_t bn) {
 uint64_t ae,be;if(ao!=bo)return 0;
 return !ui1_add64(a,an,&ae)||!ui1_add64(b,bn,&be)||((a<be)&&(b<ae));
}
static int ui1_stage_ok(const struct tegu_ui1_stage *s,unsigned fragment) {
 uint32_t stage=fragment?2u:1u,push=fragment?72u:4u,sys=fragment?8u:3u;
 return s&&s->bytes&&s->bytes<=TEGU_UI1_STAGE_MAX&&!(s->bytes&127)&&s->stage==stage&&
  s->registers&&s->registers<=64&&!s->tls&&s->fau_words&&s->fau_words<=64&&
  s->sysval_mask==sys&&(s->push_bytes==push||(fragment&&!s->push_bytes))&&!(s->preload&UINT64_C(0x0000ffffffffffff));
}
static int ui1_pair_ok(const struct tegu_ui1_pair *q,const struct tegu_ui1_stage *vs) {
 if(!q||!vs||q->version!=2||q->flags>1||(q->position_offset&127)||(q->secondary_offset&127)||
    q->position_offset>=q->secondary_offset||q->secondary_offset>=vs->bytes||
    q->position_size!=q->secondary_offset-q->position_offset||q->secondary_size!=vs->bytes-q->secondary_offset||
    !q->secondary_registers||q->secondary_registers>64||!q->generic_size_bytes||q->generic_size_bytes>64||
    (q->generic_size_bytes&3)||(q->secondary_preload&UINT64_C(0x0000ffffffffffff))||
    q->vertex_attribute_mask!=3||!q->vertex_varying_mask||q->vertex_varying_mask!=q->fragment_varying_mask||
    q->fragment_uses_ld_var||q->noperspective_mask||q->needs_extended_fifo||
    !q->varying_count||q->varying_count>4)return 0;
 if(q->flags) {
  if(q->point_position_offset||!q->point_position_size||q->point_position_size!=q->position_offset||
     q->point_size_section!=1||q->point_size_offset||q->point_size_alu_type!=144)return 0;
 } else if(q->point_position_offset||q->point_position_size||q->point_size_section||
           q->point_size_offset||q->point_size_alu_type)return 0;
 uint32_t mask=0;
 for(unsigned i=0;i<4;i++) {
  const struct tegu_ui1_varying *v=&q->varyings[i];
  if(i<q->varying_count) {
   if(v->location>=32||(mask&(1u<<v->location))||v->components<1||v->components>4||
      v->alu_type!=160||v->offset>=q->generic_size_bytes||(v->offset&3)||
      v->components*4>q->generic_size_bytes-v->offset)return 0;
   mask|=1u<<v->location;
  } else if(v->location||v->offset||v->components||v->alu_type)return 0;
 }
 return mask==q->vertex_varying_mask;
}
static int ui1_info_exact(const struct tegu_ui1_info *i) {
 static const uint8_t constant[8]={0,126,0,126,0,126,0,126};
 static const struct tegu_ui1_attribute attrs[2]={
  {0,109,0,1,48,0,1},{1,97,32,1,48,0,1}};
 static const struct tegu_ui1_resource resources[3]={
  {1,1,2,0,0,1,0,320,64,0},{2,2,1,0,1,1,1,432,168,64},
  {2,0,0,0,2,1,2,0,8,0}};
 static const struct tegu_ui1_push_span pushes[2]={{1,0,4,0},{16,64,8,0}};
 return i&&i->version==1&&i->total_bytes>=TEGU_UI1_HEADER_BYTES&&
  i->target_format==80&&i->target_write_mask==15&&i->target_blend_enabled<=1&&i->sample_count==1&&
  i->topology==TEGU_UI1_TOPOLOGY_TRIANGLE&&i->load_action_mask==7&&i->store_action_mask==3&&
  i->constant_stage==16&&i->constant_length==8&&
  !memcmp(i->constant,constant,8)&&
  ui1_stage_ok(&i->stages[0],0)&&ui1_stage_ok(&i->stages[1],1)&&ui1_pair_ok(&i->pair,&i->stages[0])&&
  !memcmp(i->attributes,attrs,sizeof(attrs))&&!memcmp(i->resources,resources,sizeof(resources))&&
  !memcmp(i->pushes,pushes,sizeof(pushes));
}
static struct tegu_ui1_stage ui1_read_stage(const uint8_t *p) {
 struct tegu_ui1_stage s={ui1_rd32(p),ui1_rd32(p+4),ui1_rd32(p+8),ui1_rd32(p+12),ui1_rd32(p+16),
  ui1_rd32(p+20),ui1_rd32(p+24),ui1_rd32(p+28),ui1_rd64(p+32)};return s;
}
int tegu_ui1_parse(const void *vp,size_t n,struct tegu_ui1_info *out) {
 if(!ui1_span(vp,n)||!ui1_span(out,sizeof(*out))||ui1_overlap(vp,n,out,sizeof(*out)))return TEGU_UI1_EINVAL;
 if(n<TEGU_UI1_HEADER_BYTES)return TEGU_UI1_EFORMAT;
 const uint8_t *p=(const uint8_t *)vp;
 if(ui1_rd32(p)!=UI1_MAGIC||ui1_rd32(p+4)!=1||ui1_rd32(p+8)!=n||ui1_rd32(p+12)!=TEGU_UI1_HEADER_BYTES)return TEGU_UI1_EFORMAT;
 uint32_t vo=ui1_rd32(p+16),vn=ui1_rd32(p+20),fo=ui1_rd32(p+24),fn=ui1_rd32(p+28);
 if(vo!=TEGU_UI1_HEADER_BYTES||!vn||vn>TEGU_UI1_STAGE_MAX||(vn&127)||
    fo!=vo+vn||!fn||fn>TEGU_UI1_STAGE_MAX||(fn&127)||n!=(size_t)fo+fn)return TEGU_UI1_EFORMAT;
 struct tegu_ui1_info t;memset(&t,0,sizeof(t));t.version=1;t.total_bytes=(uint32_t)n;
 t.stages[0]=ui1_read_stage(p+32);t.stages[1]=ui1_read_stage(p+72);
 if(t.stages[0].offset!=vo||t.stages[0].bytes!=vn||t.stages[1].offset!=fo||t.stages[1].bytes!=fn)return TEGU_UI1_EFORMAT;
 const uint8_t *q=p+UI1_PAIR_OFFSET;uint32_t *qw=(uint32_t *)&t.pair;
 /* Pair's first ten words, preload, then ten words have no host padding. */
 for(unsigned k=0;k<10;k++)qw[k]=ui1_rd32(q+4*k);
 t.pair.secondary_preload=ui1_rd64(q+40);
 uint32_t *tail=&t.pair.vertex_attribute_mask;for(unsigned k=0;k<10;k++)tail[k]=ui1_rd32(q+48+4*k);
 for(unsigned i=0;i<4;i++) {
  uint32_t *v=(uint32_t *)&t.pair.varyings[i];for(unsigned k=0;k<4;k++)v[k]=ui1_rd32(q+88+i*16+k*4);
 }
 for(unsigned i=0;i<TEGU_UI1_ATTRIBUTE_COUNT;i++){uint32_t *a=(uint32_t *)&t.attributes[i];for(unsigned k=0;k<7;k++)a[k]=ui1_rd32(p+UI1_ATTRIBUTE_OFFSET+i*28+k*4);}
 for(unsigned i=0;i<TEGU_UI1_RESOURCE_COUNT;i++){uint32_t *r=(uint32_t *)&t.resources[i];for(unsigned k=0;k<10;k++)r[k]=ui1_rd32(p+UI1_RESOURCE_OFFSET+i*40+k*4);}
 for(unsigned i=0;i<2;i++){uint32_t *s=(uint32_t *)&t.pushes[i];for(unsigned k=0;k<4;k++)s[k]=ui1_rd32(p+UI1_PUSH_OFFSET+i*16+k*4);}
 t.constant_stage=ui1_rd32(p+UI1_CONSTANT_OFFSET);t.constant_length=ui1_rd32(p+UI1_CONSTANT_OFFSET+4);
 memcpy(t.constant,p+UI1_CONSTANT_OFFSET+8,8);
 t.target_format=ui1_rd32(p+UI1_TARGET_OFFSET);t.target_write_mask=ui1_rd32(p+UI1_TARGET_OFFSET+4);
 t.target_blend_enabled=ui1_rd32(p+UI1_TARGET_OFFSET+8);t.sample_count=ui1_rd32(p+UI1_TARGET_OFFSET+12);
 t.topology=ui1_rd32(p+UI1_TARGET_OFFSET+16);t.load_action_mask=ui1_rd32(p+UI1_TARGET_OFFSET+20);
 t.store_action_mask=ui1_rd32(p+UI1_TARGET_OFFSET+24);
 if(ui1_rd32(p+UI1_TARGET_OFFSET+28)!=4||ui1_rd32(p+UI1_TARGET_OFFSET+32)!=2||ui1_rd32(p+UI1_TARGET_OFFSET+36)!=2)return TEGU_UI1_EFORMAT;
 static const char vsname[32]="fixed_vert_lph_spc",fsname[32]="fixed_frag_lph_cph";
 if(memcmp(p+UI1_NAME_OFFSET,vsname,32)||memcmp(p+UI1_NAME_OFFSET+32,fsname,32))return TEGU_UI1_ECONTRACT;
 for(unsigned k=264;k<272;k++)if(p[k])return TEGU_UI1_EFORMAT;
 for(unsigned k=328;k<384;k++)if(p[k])return TEGU_UI1_EFORMAT;
 for(unsigned k=656;k<TEGU_UI1_HEADER_BYTES;k++)if(p[k])return TEGU_UI1_EFORMAT;
 if(!ui1_info_exact(&t))return TEGU_UI1_ECONTRACT;
 *out=t;return TEGU_UI1_OK;
}
int tegu_ui1_copy_code(const struct tegu_ui1_info *i,const void *pkg,size_t n,
 void *vd,size_t vc,void *fd,size_t fc) {
 if(!ui1_span(i,sizeof(*i))||!ui1_span(pkg,n)||!ui1_span(vd,vc)||!ui1_span(fd,fc)||
    ui1_overlap(i,sizeof(*i),pkg,n)||ui1_overlap(i,sizeof(*i),vd,vc)||ui1_overlap(i,sizeof(*i),fd,fc)||
    ui1_overlap(pkg,n,vd,vc)||ui1_overlap(pkg,n,fd,fc)||ui1_overlap(vd,vc,fd,fc))return TEGU_UI1_EALIAS;
 if(!ui1_info_exact(i))return TEGU_UI1_ECONTRACT;
 struct tegu_ui1_info parsed;int rc=tegu_ui1_parse(pkg,n,&parsed);if(rc)return rc;
 if(memcmp(i,&parsed,sizeof(parsed)))return TEGU_UI1_ECONTRACT;
 if(vc<i->stages[0].bytes||fc<i->stages[1].bytes)return TEGU_UI1_ERANGE;
 memcpy(vd,(const uint8_t *)pkg+i->stages[0].offset,i->stages[0].bytes);
 memcpy(fd,(const uint8_t *)pkg+i->stages[1].offset,i->stages[1].bytes);return 0;
}
static int ui1_allocation_valid(const struct tegu_ui1_allocation *a,uint64_t need,uint64_t align) {
 return a->token&&a->object_id&&a->length&&a->offset<=a->length&&need<=a->length-a->offset&&
  !(a->offset&(align-1))&&a->length<UI1_VA_LIMIT;
}
static int ui1_rect(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t W,uint32_t H) {
 return w&&h&&x<=W&&y<=H&&w<=W-x&&h<=H-y;
}
int tegu_ui1_bind(const struct tegu_ui1_info *i,const struct tegu_ui1_bind_request *r,
 struct tegu_ui1_binding *out) {
 if(!ui1_span(i,sizeof(*i))||!ui1_span(r,sizeof(*r))||!ui1_span(out,sizeof(*out))||
    ui1_overlap(i,sizeof(*i),r,sizeof(*r))||ui1_overlap(i,sizeof(*i),out,sizeof(*out))||
    ui1_overlap(r,sizeof(*r),out,sizeof(*out)))return TEGU_UI1_EALIAS;
 if(!ui1_info_exact(i)||r->version!=1||!r->width||!r->height||r->width>8192||r->height>8192||
    r->topology!=TEGU_UI1_TOPOLOGY_TRIANGLE||r->indexed||!r->vertex_count||!r->instance_count||
    r->vertex_count>(1u<<24)||r->instance_count>(1u<<20)||r->vertex_start>UINT32_MAX-r->vertex_count||
    r->base_instance>UINT32_MAX-r->instance_count)return TEGU_UI1_ECONTRACT;
 uint64_t row_need=(uint64_t)r->width*4,target_need;
 if(r->target.format!=80||r->target.width!=r->width||r->target.height!=r->height||
    r->target.row_bytes<row_need||(r->target.row_bytes&63)||
    !ui1_add64((uint64_t)(r->height-1)*r->target.row_bytes,row_need,&target_need)||
    !ui1_allocation_valid(&r->target.allocation,target_need,64))return TEGU_UI1_ERANGE;
 if(r->vertex_slot!=1||r->vertex_stride!=48)return TEGU_UI1_ECONTRACT;
 uint64_t last=(uint64_t)r->vertex_start+r->vertex_count-1,vertex_need;
 if(last>(UINT64_MAX-40)/48||!ui1_add64(last*48,40,&vertex_need)||!ui1_allocation_valid(&r->vertex,vertex_need,4))return TEGU_UI1_ERANGE;
 if(!ui1_rect(r->pass.render_x,r->pass.render_y,r->pass.render_width,r->pass.render_height,r->width,r->height)||
    !ui1_rect(r->pass.viewport_x,r->pass.viewport_y,r->pass.viewport_width,r->pass.viewport_height,r->width,r->height)||
    !ui1_rect(r->pass.scissor_x,r->pass.scissor_y,r->pass.scissor_width,r->pass.scissor_height,r->width,r->height)||
    r->pass.load_action>TEGU_UI1_LOAD_CLEAR||r->pass.store_action>TEGU_UI1_STORE||
    !(i->load_action_mask&(1u<<r->pass.load_action))||!(i->store_action_mask&(1u<<r->pass.store_action)))return TEGU_UI1_ECONTRACT;
 struct tegu_ui1_binding t;memset(&t,0,sizeof(t));
 const uint8_t *push[2]={r->vertex_push,r->fragment_push};
 for(unsigned k=0;k<2;k++) {
  const struct tegu_ui1_resource *c=&i->resources[k];const struct tegu_ui1_buffer *b=&r->resources[k];
  if(b->metal_stage!=c->metal_stage||b->metal_slot!=c->metal_slot||!b->allocation.token||
     !b->allocation.object_id||b->allocation.offset>b->allocation.length||
     b->descriptor_offset>b->allocation.offset||(b->descriptor_offset&31)||
     !b->descriptor_length||b->descriptor_length>UINT32_MAX||
     b->descriptor_offset>b->allocation.length||b->descriptor_length>b->allocation.length-b->descriptor_offset||
     b->descriptor_length<b->allocation.offset-b->descriptor_offset+c->window_bytes||
     b->allocation.length>=UI1_VA_LIMIT)return TEGU_UI1_ECONTRACT;
  uint32_t residual=(uint32_t)(b->allocation.offset-b->descriptor_offset),supplied=ui1_rd32(push[k]);
  if(supplied!=residual)return TEGU_UI1_ERANGE;
  t.resources[k]=(struct tegu_ui1_bound_resource){b->allocation.token,b->allocation.object_id,b->descriptor_offset,
   (uint32_t)b->descriptor_length,c->window_bytes,c->metal_stage,c->metal_slot,c->table,c->index,c->push_offset,residual};
  if(ui1_range_overlap(r->target.allocation.object_id,r->target.allocation.offset,target_need,
      b->allocation.object_id,b->descriptor_offset,b->descriptor_length))return TEGU_UI1_EALIAS;
 }
 if(ui1_range_overlap(r->target.allocation.object_id,r->target.allocation.offset,target_need,
    r->vertex.object_id,r->vertex.offset,vertex_need))return TEGU_UI1_EALIAS;
 t.width=r->width;t.height=r->height;t.target_format=80;t.target_row_bytes=r->target.row_bytes;
 t.load_action=r->pass.load_action;t.store_action=r->pass.store_action;memcpy(t.clear_color,r->pass.clear_color,16);
 t.target_token=r->target.allocation.token;t.target_object_id=r->target.allocation.object_id;
 t.target_offset=r->target.allocation.offset;t.target_bytes=target_need;
 t.vertex_token=r->vertex.token;t.vertex_object_id=r->vertex.object_id;t.vertex_offset=r->vertex.offset;t.vertex_bytes=vertex_need;
 t.vertex_stride=48;t.topology=r->topology;t.vertex_start=r->vertex_start;t.vertex_count=r->vertex_count;
 t.instance_count=r->instance_count;t.base_instance=r->base_instance;
 t.render[0]=r->pass.render_x;t.render[1]=r->pass.render_y;t.render[2]=r->pass.render_width;t.render[3]=r->pass.render_height;
 t.viewport[0]=r->pass.viewport_x;t.viewport[1]=r->pass.viewport_y;t.viewport[2]=r->pass.viewport_width;t.viewport[3]=r->pass.viewport_height;
 t.scissor[0]=r->pass.scissor_x;t.scissor[1]=r->pass.scissor_y;t.scissor[2]=r->pass.scissor_width;t.scissor[3]=r->pass.scissor_height;
 memcpy(t.vertex_push,r->vertex_push,4);memcpy(t.fragment_push,r->fragment_push,8);
 memcpy(t.constant,i->constant,8);t.constant_stage=i->constant_stage;*out=t;return 0;
}
