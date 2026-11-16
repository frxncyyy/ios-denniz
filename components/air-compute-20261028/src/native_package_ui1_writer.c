#include "native_package_ui1_writer.h"
#include "native_package_ui1.h"
#include "tegu_memory.h"
#if defined(__cplusplus)
extern "C" {
#endif
void *malloc(size_t);
void free(void *);
#if defined(__cplusplus)
}
#endif

#define UI1_MAGIC 0x31554654u
#define UI1_PAIR_OFFSET 112u
#define UI1_ATTRIBUTE_OFFSET 272u
#define UI1_RESOURCE_OFFSET 384u
#define UI1_PUSH_OFFSET 504u
#define UI1_CONSTANT_OFFSET 536u
#define UI1_TARGET_OFFSET 552u
#define UI1_NAME_OFFSET 592u
static void wr32(uint8_t *p,uint32_t x) {p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);p[2]=(uint8_t)(x>>16);p[3]=(uint8_t)(x>>24);}
static void wr64(uint8_t *p,uint64_t x) {wr32(p,(uint32_t)x);wr32(p+4,(uint32_t)(x>>32));}
static uint32_t ui1w_rd32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int ui1w_overlap(const void *a,size_t an,const void *b,size_t bn) {uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return an&&bn&&(x<=y?y-x<an:x-y<bn);}
static int stage_ok(const struct tegu_mali_graphics *s,unsigned fragment) {
 return s&&s->code&&s->size&&s->size<=TEGU_UI1_STAGE_MAX&&!(s->size&127)&&
  s->stage==(fragment?2u:1u)&&s->registers&&s->registers<=64&&!s->tls&&
  s->fau_words&&s->fau_words<=64&&
  s->sysval_mask==(fragment?8u:3u)&&(fragment?(s->push_bytes==72u||!s->push_bytes):s->push_bytes==4u)&&
  !(s->preload&UINT64_C(0x0000ffffffffffff));
}
static int pair_ok(const struct tegu_mali_graphics_pair2 *q,const struct tegu_mali_graphics *vs) {
 if(!q||q->version!=2||q->flags>1||(q->position_offset&127)||(q->secondary_offset&127)||
    q->position_offset>=q->secondary_offset||q->secondary_offset>=vs->size||
    q->position_size!=q->secondary_offset-q->position_offset||q->secondary_size!=vs->size-q->secondary_offset||
    !q->secondary_registers||q->secondary_registers>64||!q->generic_size_B||q->generic_size_B>64||
    (q->generic_size_B&3)||q->secondary_preload!=UINT64_C(0x3800000000000000)||
    q->vs_attribute_mask!=3||!q->vs_varying_mask||q->vs_varying_mask!=q->fs_varying_mask||
    q->fs_uses_ld_var||q->noperspective_mask||q->needs_extended_fifo||!q->varying_count||q->varying_count>4)return 0;
 if(q->flags) {
  if(q->point_position_offset||!q->point_position_size||q->point_position_size!=q->position_offset||
     q->point_size_section!=1||q->point_size_offset||q->point_size_alu_type!=144)return 0;
 } else if(q->point_position_offset||q->point_position_size||q->point_size_section||q->point_size_offset||q->point_size_alu_type)return 0;
 uint32_t mask=0;for(unsigned i=0;i<4;i++) {
  const struct tegu_mali_varying_slot *v=&q->varyings[i];
  if(i<q->varying_count) {
   if(v->location>=32||(mask&(1u<<v->location))||v->components<1||v->components>4||v->alu_type!=160||
      v->offset>=q->generic_size_B||(v->offset&3)||v->components*4>q->generic_size_B-v->offset)return 0;
   mask|=1u<<v->location;
  } else if(v->location||v->offset||v->components||v->alu_type)return 0;
 }return mask==q->vs_varying_mask;
}
static int backend_ok(const struct tegu_mali_fixed_ui_plan *p) {
 static const struct tegu_mali_resource_ubo exact[3]={{1,0,0,1,0,320},{2,0,1,1,1,432},{2,0,2,1,2,0}};
 return p&&p->version==1&&p->table==1&&p->resource_count==3&&!p->texture_count&&
  p->constant_bytes==8&&p->constant_access_bytes==336&&
  !memcmp(p->ubos,exact,sizeof(exact));
}
static int frontend_ok(const TeguAIRFixedUiR80Plan *p) {
 if(!p||p->version!=1||p->binding_count!=2||p->push_count!=2||p->push_bytes_length!=12||
    p->constant_length!=8||(p->constant_stage!=1&&p->constant_stage!=16)||p->reserved[0]||p->reserved[1])return 0;
 static const uint32_t bindings[2][4]={{1,2,0,0},{2,1,0,1}};
 static const uint64_t lengths[2]={64,168};
 for(unsigned i=0;i<2;i++) {
  const TeguAIRFixedUiR80Binding *b=&p->bindings[i];
  if(b->stage!=bindings[i][0]||b->index!=bindings[i][1]||b->descriptor_set!=bindings[i][2]||
     b->binding!=bindings[i][3]||b->bind_offset>b->resource_length||
     b->descriptor_offset>b->bind_offset||(b->descriptor_offset&31)||(b->descriptor_length&31)||
     b->bind_offset-b->descriptor_offset>=32||b->descriptor_length>UINT32_MAX||
     b->descriptor_length< b->bind_offset-b->descriptor_offset+lengths[i]||
     b->descriptor_offset>b->resource_length||
     b->descriptor_length>b->resource_length-b->descriptor_offset)return 0;
 }
 if(p->pushes[0].stage!=1||p->pushes[0].offset||p->pushes[0].length!=4||p->pushes[0].bytes_offset||
    p->pushes[1].stage!=16||p->pushes[1].offset!=64||p->pushes[1].length!=8||p->pushes[1].bytes_offset!=4)return 0;
 return ui1w_rd32(p->push_bytes)==p->bindings[0].bind_offset-p->bindings[0].descriptor_offset&&
  ui1w_rd32(p->push_bytes+4)==p->bindings[1].bind_offset-p->bindings[1].descriptor_offset;
}
int tegu_ui1_build(const struct tegu_mali_graphics *vs,const struct tegu_mali_graphics *fs,
 const struct tegu_mali_graphics_pair2 *pair,const struct tegu_mali_fixed_ui_plan *backend,
 const TeguAIRFixedUiR80Plan *frontend,uint32_t blend,uint8_t **out,size_t *outn) {
 if(!vs||!fs||!pair||!backend||!frontend||blend>1||!out||!outn||*out||*outn||
    ui1w_overlap(out,sizeof(*out),outn,sizeof(*outn))||ui1w_overlap(out,sizeof(*out),vs->code,vs->size)||
    ui1w_overlap(out,sizeof(*out),fs->code,fs->size)||ui1w_overlap(outn,sizeof(*outn),vs->code,vs->size)||
    ui1w_overlap(outn,sizeof(*outn),fs->code,fs->size)||ui1w_overlap(vs->code,vs->size,fs->code,fs->size)||
    ui1w_overlap(out,sizeof(*out),vs,sizeof(*vs))||ui1w_overlap(out,sizeof(*out),fs,sizeof(*fs))||
    ui1w_overlap(out,sizeof(*out),pair,sizeof(*pair))||ui1w_overlap(out,sizeof(*out),backend,sizeof(*backend))||
    ui1w_overlap(out,sizeof(*out),frontend,sizeof(*frontend))||ui1w_overlap(outn,sizeof(*outn),vs,sizeof(*vs))||
    ui1w_overlap(outn,sizeof(*outn),fs,sizeof(*fs))||ui1w_overlap(outn,sizeof(*outn),pair,sizeof(*pair))||
    ui1w_overlap(outn,sizeof(*outn),backend,sizeof(*backend))||ui1w_overlap(outn,sizeof(*outn),frontend,sizeof(*frontend))||
    !stage_ok(vs,0)||!stage_ok(fs,1)||
    !pair_ok(pair,vs)||!backend_ok(backend)||!frontend_ok(frontend))return TEGU_UI1_EINVAL;
 size_t fo=TEGU_UI1_HEADER_BYTES+vs->size,n=fo+fs->size;
 uint8_t *p=(uint8_t *)malloc(n);if(!p)return TEGU_UI1_ERANGE;memset(p,0,n);
 const uint32_t head[8]={UI1_MAGIC,1,(uint32_t)n,TEGU_UI1_HEADER_BYTES,TEGU_UI1_HEADER_BYTES,(uint32_t)vs->size,(uint32_t)fo,(uint32_t)fs->size};
 for(unsigned k=0;k<8;k++)wr32(p+k*4,head[k]);
 const struct tegu_mali_graphics *stages[2]={vs,fs};const uint32_t offsets[2]={TEGU_UI1_HEADER_BYTES,(uint32_t)fo};
 for(unsigned i=0;i<2;i++) {const struct tegu_mali_graphics *s=stages[i];uint8_t *d=p+32+i*40;
  uint32_t w[8]={(uint32_t)s->size,offsets[i],s->stage,s->registers,s->tls,s->fau_words,s->sysval_mask,s->push_bytes};
  for(unsigned k=0;k<8;k++)wr32(d+k*4,w[k]);
  wr64(d+32,s->preload);
 }
 uint8_t *q=p+UI1_PAIR_OFFSET;const uint32_t q0[10]={pair->version,pair->flags,pair->position_offset,pair->position_size,
  pair->point_position_offset,pair->point_position_size,pair->secondary_offset,pair->secondary_size,pair->secondary_registers,pair->generic_size_B};
 for(unsigned k=0;k<10;k++)wr32(q+k*4,q0[k]);
 wr64(q+40,pair->secondary_preload);
 const uint32_t q1[10]={pair->vs_attribute_mask,pair->vs_varying_mask,pair->fs_varying_mask,pair->fs_uses_ld_var,
  pair->noperspective_mask,pair->needs_extended_fifo,pair->point_size_section,pair->point_size_offset,pair->point_size_alu_type,pair->varying_count};
 for(unsigned k=0;k<10;k++)wr32(q+48+k*4,q1[k]);
 for(unsigned i=0;i<4;i++){const uint32_t *v=(const uint32_t *)&pair->varyings[i];for(unsigned k=0;k<4;k++)wr32(q+88+i*16+k*4,v[k]);}
 static const uint32_t attrs[2][7]={{0,109,0,1,48,0,1},{1,97,32,1,48,0,1}};
 for(unsigned i=0;i<2;i++)for(unsigned k=0;k<7;k++)wr32(p+UI1_ATTRIBUTE_OFFSET+i*28+k*4,attrs[i][k]);
 static const uint32_t resources[3][10]={{1,1,2,0,0,1,0,320,64,0},{2,2,1,0,1,1,1,432,168,64},{2,0,0,0,2,1,2,0,8,0}};
 for(unsigned i=0;i<3;i++)for(unsigned k=0;k<10;k++)wr32(p+UI1_RESOURCE_OFFSET+i*40+k*4,resources[i][k]);
 static const uint32_t pushes[2][4]={{1,0,4,0},{16,64,8,0}};
 for(unsigned i=0;i<2;i++)for(unsigned k=0;k<4;k++)wr32(p+UI1_PUSH_OFFSET+i*16+k*4,pushes[i][k]);
 wr32(p+UI1_CONSTANT_OFFSET,frontend->constant_stage);wr32(p+UI1_CONSTANT_OFFSET+4,8);
 memcpy(p+UI1_CONSTANT_OFFSET+8,frontend->constant_bytes,8);
 const uint32_t target[10]={80,15,blend,1,TEGU_UI1_TOPOLOGY_TRIANGLE,7,3,4,2,2};
 for(unsigned k=0;k<10;k++)wr32(p+UI1_TARGET_OFFSET+k*4,target[k]);
 static const char vsname[32]="fixed_vert_lph_spc",fsname[32]="fixed_frag_lph_cph";
 memcpy(p+UI1_NAME_OFFSET,vsname,32);memcpy(p+UI1_NAME_OFFSET+32,fsname,32);
 memcpy(p+TEGU_UI1_HEADER_BYTES,vs->code,vs->size);memcpy(p+fo,fs->code,fs->size);
 struct tegu_ui1_info parsed;if(tegu_ui1_parse(p,n,&parsed)){free(p);return TEGU_UI1_EFORMAT;}
 *out=p;*outn=n;return 0;
}
void tegu_ui1_free(uint8_t **p,size_t *n) {if(p&&*p){free(*p);*p=NULL;}if(n)*n=0;}
