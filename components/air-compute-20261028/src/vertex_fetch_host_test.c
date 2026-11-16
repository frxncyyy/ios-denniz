/* Compare descriptor bytes against Mesa's generated arch10/11 XML packers. */
#include "vertex_fetch.h"
#include "panfrost/genxml/v10_pack.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t hardware_format(uint32_t format) {
 switch(format) {
 case 97:return (MALI_RGBA16F<<12)|MALI_RGB_COMPONENT_ORDER_RGBA;
 case 100:return (MALI_R32F<<12)|MALI_RGB_COMPONENT_ORDER_RGB1;
 case 103:return (MALI_RG32F<<12)|MALI_RGB_COMPONENT_ORDER_RGB1;
 case 106:return (MALI_RGB32F<<12)|MALI_RGB_COMPONENT_ORDER_RGB1;
 case 109:return (MALI_RGBA32F<<12)|MALI_RGB_COMPONENT_ORDER_RGBA;
 default:assert(0);return 0;
 }
}
static void compare_oracle(const struct tegu_vertex_attribute *a,size_t count,
 const struct tegu_vertex_buffer *buffers,const struct tegu_vertex_draw *draw,
 uint64_t gpu,const struct tegu_vertex_state *state) {
 _Alignas(64) unsigned char expected[4096]={0};uint32_t used=0;
 pan_pack((struct mali_resource_packed *)expected,RESOURCE,cfg) {
  cfg.address=gpu+0x100;cfg.size=63*32;cfg.contains_descriptors=true;
 }
 for(size_t i=0;i<count;i++) {
  used|=1u<<a[i].buffer_index;
  pan_pack((struct mali_attribute_packed *)(expected+0x100+a[i].location*32),ATTRIBUTE,cfg) {
   cfg.format=hardware_format(a[i].format);cfg.table=0;cfg.buffer_index=32+a[i].buffer_index;
   cfg.offset=a[i].offset;
   cfg.attribute_type=MALI_ATTRIBUTE_TYPE_1D;cfg.stride=a[i].stride;
   if(!a[i].input_rate) {cfg.frequency=MALI_ATTRIBUTE_FREQUENCY_VERTEX;cfg.offset_enable=true;}
   else {
    cfg.offset+=(uint64_t)draw->base_instance*a[i].stride;
    cfg.frequency=MALI_ATTRIBUTE_FREQUENCY_INSTANCE;
    if(!a[i].divisor)cfg.stride=0;
   }
  }
 }
 for(unsigned i=0;i<31;i++)if(used&(1u<<i)) {
  pan_pack((struct mali_buffer_packed *)(expected+0x100+(32+i)*32),BUFFER,cfg) {cfg.address=buffers[i].address;cfg.size=buffers[i].length;}
 }
 assert(state->resource_table==(gpu|MALI_RESOURCE_TABLE_SIZE_ALIGNMENT));
 assert(state->buffer_mask==used&&!state->reserved);
 assert(!memcmp(expected,state->descriptors,sizeof(expected)));
}
int main(void) {
 struct tegu_vertex_attribute attrs[31]={{0,1,109,16,32,0,1},{7,4,103,0,16,1,1},{30,30,100,4,64,1,0}};
 struct tegu_vertex_buffer buffers[31]={0};
 for(unsigned i=0;i<31;i++)buffers[i]=(struct tegu_vertex_buffer){0x5200000ULL+i*0x10000,1024};
 struct tegu_vertex_draw draw={2,3,3,2};uint64_t gpu=0x5106000;
 struct guarded {uint64_t before[2];struct tegu_vertex_state state;uint64_t after[2];} g;
 memset(&g,0xa5,sizeof(g));
 uint32_t mask=(1u<<0)|(1u<<7)|(1u<<30);
 assert(!tegu_vertex_fetch_build(mask,attrs,3,buffers,&draw,gpu,&g.state));
 compare_oracle(attrs,3,buffers,&draw,gpu,&g.state);
 for(unsigned i=0;i<2;i++)assert(g.before[i]==0xa5a5a5a5a5a5a5a5ULL&&g.after[i]==g.before[i]);
 for(unsigned format=100;format<=109;format+=3) {
  attrs[0].format=format;
  assert(!tegu_vertex_fetch_build(mask,attrs,3,buffers,&draw,gpu,&g.state));
  compare_oracle(attrs,3,buffers,&draw,gpu,&g.state);
 }
 attrs[0].format=97;
 assert(!tegu_vertex_fetch_build(mask,attrs,3,buffers,&draw,gpu,&g.state));
 compare_oracle(attrs,3,buffers,&draw,gpu,&g.state);
 attrs[0].format=109;
 struct tegu_vertex_attribute backboard[4]={{0,1,109,0,48,0,1},{1,1,103,16,48,0,1},
  {2,1,103,24,48,0,1},{3,1,97,32,48,0,1}};
 struct tegu_vertex_draw backboardDraw={0,3,1,0};uint64_t end=0;
 buffers[1].length=136;
 assert(!tegu_vertex_fetch_build(15,backboard,4,buffers,&backboardDraw,gpu,&g.state));
 compare_oracle(backboard,4,buffers,&backboardDraw,gpu,&g.state);
 assert(!tegu_vertex_attribute_end(&backboard[3],&backboardDraw,&end)&&end==136);
 struct tegu_vertex_state backboardSaved=g.state;buffers[1].length=135;
 assert(tegu_vertex_fetch_build(15,backboard,4,buffers,&backboardDraw,gpu,&g.state));
 assert(!memcmp(&backboardSaved,&g.state,sizeof(g.state)));
 buffers[1].length=1024;
 // Exactly the last vertex fits; one byte less must fail. Divisor0 still
 // includes baseInstance*originalStride, as PanVK does for constant stepping.
 buffers[1].length=160;buffers[4].length=72;buffers[30].length=136;
 assert(!tegu_vertex_fetch_build(mask,attrs,3,buffers,&draw,gpu,&g.state));
 compare_oracle(attrs,3,buffers,&draw,gpu,&g.state);
 struct tegu_vertex_state saved=g.state;
 #define REFUSE(req,as,n,bs,dr,address) do { \
  assert(tegu_vertex_fetch_build(req,as,n,bs,dr,address,&g.state)); \
  assert(!memcmp(&saved,&g.state,sizeof(saved))); \
 }while(0)
 for(unsigned i=1;i<=30;i++)if(i==1||i==4||i==30) {
  buffers[i].length--;REFUSE(mask,attrs,3,buffers,&draw,gpu);buffers[i].length++;
 }
 REFUSE(mask,0,3,buffers,&draw,gpu);REFUSE(mask,attrs,32,buffers,&draw,gpu);
 REFUSE(mask,attrs,3,0,&draw,gpu);REFUSE(mask,attrs,3,buffers,0,gpu);
 REFUSE(mask,attrs,3,buffers,&draw,0);REFUSE(mask,attrs,3,buffers,&draw,gpu+1);
 REFUSE(mask,attrs,3,buffers,&draw,1ULL<<48);
 REFUSE(mask|0x80000000u,attrs,3,buffers,&draw,gpu);
 REFUSE(mask^1,attrs,3,buffers,&draw,gpu);
 uint32_t old;
 #define CHANGE(field,value) do {old=attrs[0].field;attrs[0].field=value; \
  REFUSE(mask,attrs,3,buffers,&draw,gpu);attrs[0].field=old;}while(0)
 CHANGE(location,31);CHANGE(location,7);CHANGE(buffer_index,31);
 CHANGE(format,37);CHANGE(offset,0x80000000u);CHANGE(offset,1);
 CHANGE(stride,1);CHANGE(stride,0xfffffffcu);CHANGE(input_rate,2);CHANGE(divisor,2);
 uint64_t oldAddress=buffers[1].address;
 buffers[1].address=gpu;REFUSE(mask,attrs,3,buffers,&draw,gpu);
 buffers[1].address=1ULL<<48;REFUSE(mask,attrs,3,buffers,&draw,gpu);
 buffers[1].address=oldAddress;
 uint64_t oldLength=buffers[1].length;
 buffers[1].length=1ULL<<32;REFUSE(mask,attrs,3,buffers,&draw,gpu);buffers[1].length=oldLength;
 draw.first_vertex=0xffffffffu;REFUSE(mask,attrs,3,buffers,&draw,gpu);draw.first_vertex=2;
 draw.vertex_count=0;REFUSE(mask,attrs,3,buffers,&draw,gpu);draw.vertex_count=3;
 draw.instance_count=0;REFUSE(mask,attrs,3,buffers,&draw,gpu);draw.instance_count=3;
 draw.base_instance=0xffffffffu;REFUSE(mask,attrs,3,buffers,&draw,gpu);draw.base_instance=2;
 assert(tegu_vertex_fetch_build(mask,attrs,3,buffers,&draw,gpu,0));
 // Full sparse slot range, multiple attributes sharing one buffer.
 for(unsigned i=0;i<31;i++)attrs[i]=(struct tegu_vertex_attribute){i,1,100,i*4,128,0,1};
 buffers[1].length=4096;
 assert(!tegu_vertex_fetch_build(0x7fffffffu,attrs,31,buffers,&draw,gpu,&g.state));
 compare_oracle(attrs,31,buffers,&draw,gpu,&g.state);
 saved=g.state;attrs[30].stride=64;REFUSE(0x7fffffffu,attrs,31,buffers,&draw,gpu);
 assert(!tegu_vertex_fetch_build(0,0,0,0,&draw,0,&g.state));
 struct tegu_vertex_state zero={0};assert(!memcmp(&g.state,&zero,sizeof(zero)));
 puts("PASS Mesa XML vertex descriptor oracle, all31slots, float formats, instancing, bounded fetches and output guards; no GPU execution");
 return 0;
}
