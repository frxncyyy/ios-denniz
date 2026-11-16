/* Independent arch9 XML oracle for the installed Icon linked half2 format.
 * Its [RG32F,RG16F,RGBA16F,RG32F] layout is inferred, not a captured draw. */
#include "vertex_fetch.h"
#include "panfrost/genxml/v9_pack.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void oracle(const struct tegu_vertex_attribute *attrs,unsigned count,
 const struct tegu_vertex_buffer *buffers,const struct tegu_vertex_draw *draw,
 uint64_t gpu,const struct tegu_vertex_state *actual) {
 unsigned char expected[4096]={0};unsigned used=0;
 pan_pack((struct mali_resource_packed *)expected,RESOURCE,cfg) {
  cfg.address=gpu+0x100;cfg.size=63*32;cfg.contains_descriptors=true;
 }
 for(unsigned i=0;i<count;i++) {
  const struct tegu_vertex_attribute *a=&attrs[i];used|=1u<<a->buffer_index;
  pan_pack((struct mali_attribute_packed *)(expected+0x100+a->location*32),ATTRIBUTE,cfg) {
   switch(a->format) {
   case 83:cfg.format=(MALI_RG16F<<12)|MALI_RGB_COMPONENT_ORDER_RGB1;break;
   case 97:cfg.format=(MALI_RGBA16F<<12)|MALI_RGB_COMPONENT_ORDER_RGBA;break;
   case 103:cfg.format=(MALI_RG32F<<12)|MALI_RGB_COMPONENT_ORDER_RGB1;break;
   default:assert(0);
   }
   cfg.attribute_type=MALI_ATTRIBUTE_TYPE_1D;cfg.buffer_index=32+a->buffer_index;
   cfg.offset=a->offset;cfg.stride=a->stride;
   if(a->input_rate) {
    cfg.frequency=MALI_ATTRIBUTE_FREQUENCY_INSTANCE;
    cfg.offset+=draw->base_instance*a->stride;
    if(!a->divisor)cfg.stride=0;
   } else {cfg.frequency=MALI_ATTRIBUTE_FREQUENCY_VERTEX;cfg.offset_enable=true;}
  }
 }
 for(unsigned i=0;i<31;i++)if(used&(1u<<i)) {
  pan_pack((struct mali_buffer_packed *)(expected+0x100+(32+i)*32),BUFFER,cfg) {
   cfg.address=buffers[i].address;cfg.size=buffers[i].length;
  }
 }
 assert(actual->resource_table==(gpu|MALI_RESOURCE_TABLE_SIZE_ALIGNMENT));
 assert(actual->buffer_mask==used&&!actual->reserved);
 assert(!memcmp(actual->descriptors,expected,sizeof(expected)));
}

int main(void) {
 uint64_t gpu=0x5106000,end=0;struct tegu_vertex_state output,saved;
 struct tegu_vertex_buffer b[31]={0};b[3]=(struct tegu_vertex_buffer){0x5200000,4};
 struct tegu_vertex_draw d={0,1,1,0};
 struct tegu_vertex_attribute a[4]={{30,3,83,0,4,0,1}};
 assert(tegu_vertex_format_bytes(83)==4);
 assert(!tegu_vertex_attribute_end(a,&d,&end)&&end==4);
 assert(!tegu_vertex_fetch_build(1u<<30,a,1,b,&d,gpu,&output));oracle(a,1,b,&d,gpu,&output);
 saved=output;
#define REFUSE(mask,count) do { \
 assert(tegu_vertex_fetch_build(mask,a,count,b,&d,gpu,&output)); \
 assert(!memcmp(&saved,&output,sizeof(saved))); \
} while(0)
 b[3].length=3;REFUSE(1u<<30,1);b[3].length=4;
 a[0].offset=2;REFUSE(1u<<30,1);a[0].offset=0;
 a[0].stride=2;REFUSE(1u<<30,1);a[0].stride=4;
 d.first_vertex=UINT32_MAX;d.vertex_count=2;REFUSE(1u<<30,1);
 assert(tegu_vertex_attribute_end(a,&d,&end));d.first_vertex=3;d.vertex_count=2;
 b[3].length=20;assert(!tegu_vertex_attribute_end(a,&d,&end)&&end==20);
 assert(!tegu_vertex_fetch_build(1u<<30,a,1,b,&d,gpu,&output));oracle(a,1,b,&d,gpu,&output);
 saved=output;b[3].length=19;REFUSE(1u<<30,1);b[3].length=20;
 a[0].stride=0xfffffffcu;REFUSE(1u<<30,1);a[0].stride=4;
 a[0].offset=0x80000000u;REFUSE(1u<<30,1);a[0].offset=0;
 d=(struct tegu_vertex_draw){0,1,3,2};a[0].input_rate=1;
 for(unsigned divisor=0;divisor<=1;divisor++) {
  a[0].divisor=divisor;b[3].length=divisor?20:12;
  assert(!tegu_vertex_attribute_end(a,&d,&end)&&end==b[3].length);
  assert(!tegu_vertex_fetch_build(1u<<30,a,1,b,&d,gpu,&output));oracle(a,1,b,&d,gpu,&output);
  saved=output;b[3].length--;REFUSE(1u<<30,1);
 }
 /* The four linked formats observed in the real Icon shader metadata. */
 a[0]=(struct tegu_vertex_attribute){0,3,103,0,28,0,1};
 a[1]=(struct tegu_vertex_attribute){1,3,83,8,28,0,1};
 a[2]=(struct tegu_vertex_attribute){2,3,97,12,28,0,1};
 a[3]=(struct tegu_vertex_attribute){3,3,103,20,28,0,1};
 d=(struct tegu_vertex_draw){0,3,1,0};b[3].length=84;
 assert(!tegu_vertex_fetch_build(15,a,4,b,&d,gpu,&output));oracle(a,4,b,&d,gpu,&output);
 saved=output;b[3].length=83;REFUSE(15,4);
 puts("PASS arch9 XML full descriptor oracle: half2, Icon linked formats, exact bounds, first vertex, divisors, unchanged refusals; no GPU execution");
 return 0;
}
