#include "fixed_ui_resources.h"
#include <stddef.h>
#include "tegu_memory.h"

#define TEGU_FIXED_UI_VA_LIMIT (1ull<<48)
static void fuir_put32(uint8_t *p,uint32_t x) {
 p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);p[2]=(uint8_t)(x>>16);p[3]=(uint8_t)(x>>24);
}
static void fuir_put64(uint8_t *p,uint64_t x) {fuir_put32(p,(uint32_t)x);fuir_put32(p+4,(uint32_t)(x>>32));}
static uint32_t fuir_get32(const uint8_t *p) {
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static int fuir_overlap(const void *a,size_t an,const void *b,size_t bn) {
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return an&&bn&&(x<=y?y-x<an:x-y<bn);
}
static int fuir_span_ok(struct tegu_fixed_ui_span s,uint64_t alignment,uint64_t minimum,uint64_t maximum) {
 return s.address&&!(s.address&(alignment-1))&&s.bytes>=minimum&&s.bytes<=maximum&&
  s.address<TEGU_FIXED_UI_VA_LIMIT&&s.bytes<=TEGU_FIXED_UI_VA_LIMIT-s.address;
}
static int fuir_span_overlap(struct tegu_fixed_ui_span a,uint64_t an,struct tegu_fixed_ui_span b,uint64_t bn) {
 return an&&bn&&a.address<b.address+bn&&b.address<a.address+an;
}
static void fuir_pack_resource(uint8_t *p,uint64_t address,uint64_t bytes,int contains) {
 fuir_put64(p,address&((1ull<<56)-1));
 fuir_put32(p+4,(uint32_t)((address>>32)&0x00ffffffull)|(contains?(1u<<24):0));
 fuir_put64(p+8,bytes);
}
static void fuir_pack_uniform_buffer(uint8_t *p,uint64_t address,uint32_t bytes) {
 memset(p,0,32);fuir_put32(p,9u|(1u<<4));fuir_put32(p+4,bytes);fuir_put64(p+8,address);
}
static int fuir_binding_ok(const struct tegu_ui1_bound_resource *b,uint32_t stage,uint32_t slot,
 uint32_t index,struct tegu_fixed_ui_allocation_span allocation,uint32_t push_residual) {
 return allocation.token&&allocation.object_id&&b->token==allocation.token&&b->object_id==allocation.object_id&&
  b->metal_stage==stage&&b->metal_slot==slot&&b->table==1&&b->index==index&&
  b->window_bytes==(stage==TEGU_FIXED_UI_RESOURCE_STAGE_VERTEX?64u:168u)&&
  b->push_offset==(stage==TEGU_FIXED_UI_RESOURCE_STAGE_VERTEX?0u:64u)&&
  b->push_residual==push_residual&&b->descriptor_bytes&&!(b->descriptor_offset&31)&&
  b->descriptor_offset<=allocation.bytes&&
  b->descriptor_bytes<=allocation.bytes-b->descriptor_offset&&
  b->push_residual<=b->descriptor_bytes&&
  b->window_bytes<=b->descriptor_bytes-b->push_residual;
}
static int fuir_binding_set_ok(const struct tegu_ui1_binding *b,
 struct tegu_fixed_ui_allocation_span vertex,struct tegu_fixed_ui_allocation_span fragment) {
 static const uint8_t constant[8]={0,126,0,126,0,126,0,126};
 if(!b||b->constant_stage!=TEGU_FIXED_UI_PUSH_STAGE_FRAGMENT||memcmp(b->constant,constant,8))return 0;
 uint32_t vr=fuir_get32(b->vertex_push),fr=fuir_get32(b->fragment_push);
 if(!fuir_binding_ok(&b->resources[0],TEGU_FIXED_UI_RESOURCE_STAGE_VERTEX,2,0,vertex,vr)||
    !fuir_binding_ok(&b->resources[1],TEGU_FIXED_UI_RESOURCE_STAGE_FRAGMENT,1,1,fragment,fr))return 0;
 return b->resources[0].token!=b->resources[1].token||
  b->resources[0].object_id!=b->resources[1].object_id||
  (vertex.address==fragment.address&&vertex.bytes==fragment.bytes);
}
int tegu_fixed_ui_resource_state_build(const struct tegu_fixed_ui_resource_input *in,
 struct tegu_fixed_ui_resource_state *out) {
 if(!in||!out||fuir_overlap(in,sizeof(*in),out,sizeof(*out))||in->version!=TEGU_FIXED_UI_STATE_VERSION||
    !in->binding||fuir_overlap(in->binding,sizeof(*in->binding),out,sizeof(*out)))return -1;
 if((in->table0.bytes?!fuir_span_ok(in->table0,64,1,UINT32_MAX):
      (in->table0.address>=TEGU_FIXED_UI_VA_LIMIT||(in->table0.address&63)))||
    !fuir_span_ok(in->resource_table,64,64,64)||!fuir_span_ok(in->descriptors,32,128,128)||
    !fuir_span_ok((struct tegu_fixed_ui_span){in->vertex_buffer.address,in->vertex_buffer.bytes},16,1,UINT32_MAX)||
    !fuir_span_ok((struct tegu_fixed_ui_span){in->fragment_buffer.address,in->fragment_buffer.bytes},16,1,UINT32_MAX)||
    !fuir_binding_set_ok(in->binding,in->vertex_buffer,in->fragment_buffer))return -2;
 struct tegu_fixed_ui_span outputs[2]={in->resource_table,in->descriptors};
 uint64_t output_bytes[2]={64,128};
 struct tegu_fixed_ui_span inputs[3]={in->table0,
  {in->vertex_buffer.address,in->vertex_buffer.bytes},
  {in->fragment_buffer.address,in->fragment_buffer.bytes}};
 uint64_t input_bytes[3]={in->table0.bytes,in->vertex_buffer.bytes,in->fragment_buffer.bytes};
 for(unsigned i=0;i<2;i++)for(unsigned j=i+1;j<2;j++)
  if(fuir_span_overlap(outputs[i],output_bytes[i],outputs[j],output_bytes[j]))return -3;
 for(unsigned i=0;i<2;i++)for(unsigned j=0;j<3;j++)
  if(fuir_span_overlap(outputs[i],output_bytes[i],inputs[j],input_bytes[j]))return -3;
 const struct tegu_ui1_bound_resource *vb=&in->binding->resources[0],*fb=&in->binding->resources[1];
 struct tegu_fixed_ui_resource_state tmp;memset(&tmp,0,sizeof(tmp));
 tmp.resource_table_handle=in->resource_table.address|4u;
 fuir_pack_resource(tmp.resource_table,in->table0.address,in->table0.bytes,in->table0.bytes!=0);
 fuir_pack_resource(tmp.resource_table+16,in->descriptors.address,128,1);
 fuir_pack_uniform_buffer(tmp.descriptors,in->vertex_buffer.address+vb->descriptor_offset,vb->descriptor_bytes);
 fuir_pack_uniform_buffer(tmp.descriptors+32,in->fragment_buffer.address+fb->descriptor_offset,fb->descriptor_bytes);
 fuir_pack_uniform_buffer(tmp.descriptors+64,in->descriptors.address+96,16);
 memcpy(tmp.descriptors+96,in->binding->constant,TEGU_FIXED_UI_CONSTANT_BYTES);
 memcpy(tmp.push_bytes,in->binding->vertex_push,4);
 memcpy(tmp.push_bytes+4,in->binding->fragment_push,8);
 memcpy(tmp.constant_blob,in->binding->constant,TEGU_FIXED_UI_CONSTANT_BYTES);
 tmp.vertex_resource_stage=TEGU_FIXED_UI_RESOURCE_STAGE_VERTEX;
 tmp.fragment_resource_stage=TEGU_FIXED_UI_RESOURCE_STAGE_FRAGMENT;
 tmp.vertex_push_stage=TEGU_FIXED_UI_PUSH_STAGE_VERTEX;
 tmp.fragment_push_stage=TEGU_FIXED_UI_PUSH_STAGE_FRAGMENT;
 tmp.vertex_binding=0;tmp.fragment_binding=1;
 tmp.vertex_push_offset=0;tmp.vertex_push_length=4;
 tmp.fragment_push_offset=64;tmp.fragment_push_length=8;
 memcpy(out,&tmp,sizeof(tmp));return 0;
}
