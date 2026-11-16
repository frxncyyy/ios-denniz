#include "vertex_fetch.h"
#include "render_pipeline_format.h"
static void vertex_u64(unsigned char *out,uint64_t value) {
 tegu_render_put(out,(uint32_t)value);tegu_render_put(out+4,(uint32_t)(value>>32));
}
/* Mesa81daea6 pan_format.c and genxml/v10.xml: arch>=7 uses component-order
 * enums, NOT the old twelve-bit v6 swizzle. RGB1 also covers R001 and RG01. */
static int vertex_format(uint32_t format,uint32_t *hardware,uint32_t *bytes) {
 switch(format) {
 case 83:*hardware=(207u<<12)|16;*bytes=4;return 0;
 case 97:*hardware=223u<<12;*bytes=8;return 0;
 case 100:*hardware=(167u<<12)|16;*bytes=4;return 0;
 case 103:*hardware=(175u<<12)|16;*bytes=8;return 0;
 case 106:*hardware=(183u<<12)|16;*bytes=12;return 0;
 case 109:*hardware=191u<<12;*bytes=16;return 0;
 default:return -1;
 }
}
int tegu_vertex_fetch_build(uint32_t required,const struct tegu_vertex_attribute *attributes,size_t count,
 const struct tegu_vertex_buffer buffers[TEGU_VERTEX_SLOTS],const struct tegu_vertex_draw *draw,
 uint64_t descriptor_address,struct tegu_vertex_state *out) {
 if(!out||!draw||count>TEGU_VERTEX_SLOTS||(count&&(!attributes||!buffers))||
    (required&0x80000000u)||!draw->vertex_count||!draw->instance_count||
    (uint64_t)draw->first_vertex+draw->vertex_count-1>0xffffffffULL||
    (count&&(!descriptor_address||(descriptor_address&4095)||descriptor_address>((1ULL<<48)-4096))))return -1;
 uint32_t seen=0,used=0,formats[TEGU_VERTEX_SLOTS],offsets[TEGU_VERTEX_SLOTS],strides[TEGU_VERTEX_SLOTS];
 for(size_t i=0;i<count;i++) {
  const struct tegu_vertex_attribute *a=&attributes[i];uint32_t bytes;
  if(a->location>=TEGU_VERTEX_SLOTS||a->buffer_index>=TEGU_VERTEX_SLOTS||
     (seen&(1u<<a->location))||vertex_format(a->format,&formats[i],&bytes)||
     a->input_rate>1||(!a->input_rate&&a->divisor!=1)||(a->input_rate&&a->divisor>1)||
     (a->offset&3)||(a->stride&3))return -2;
  seen|=1u<<a->location;used|=1u<<a->buffer_index;
  // One Metal buffer layout supplies all attributes reading the same slot.
  for(size_t j=0;j<i;j++)if(attributes[j].buffer_index==a->buffer_index&&
    (attributes[j].stride!=a->stride||attributes[j].input_rate!=a->input_rate||attributes[j].divisor!=a->divisor))return -2;
  const struct tegu_vertex_buffer *b=&buffers[a->buffer_index];
  if(!b->address||(b->address&3)||!b->length||b->length>0xffffffffULL||b->address>((1ULL<<48)-b->length)||
     (b->address<descriptor_address+4096&&descriptor_address<b->address+b->length))return -3;
  uint64_t offset=a->offset;uint32_t stride=a->stride;
  if(a->input_rate) {
   offset+=(uint64_t)draw->base_instance*a->stride;
   if(!a->divisor)stride=0;
  }
  // ATTRIBUTE.offset is signed32. Validate using subtraction/division to
  // avoid overflow in lastElement*stride+offset+formatSize.
  uint64_t last=a->input_rate?(uint64_t)draw->instance_count-1:
   (uint64_t)draw->first_vertex+draw->vertex_count-1;
  if(offset>0x7fffffffULL||offset>b->length||bytes>b->length-offset||
     (stride&&last>(b->length-offset-bytes)/stride))return -4;
  offsets[i]=(uint32_t)offset;strides[i]=stride;
 }
 if(seen!=required)return -5;
 // All checks precede the first output store; no multi-KiB kernel stack temp.
 unsigned char *raw=(unsigned char *)out;for(size_t i=0;i<sizeof(*out);i++)raw[i]=0;
 if(!count)return 0;
 out->resource_table=descriptor_address|4;out->buffer_mask=used;
 unsigned char *d=out->descriptors;
 vertex_u64(d,(descriptor_address+0x100)|(1ULL<<56));vertex_u64(d+8,63u*32);
 for(size_t i=0;i<count;i++) {
  const struct tegu_vertex_attribute *a=&attributes[i];unsigned char *p=d+0x100+a->location*32;
  tegu_render_put(p,5u|(1u<<4)|(!a->input_rate?1u<<8:0)|(formats[i]<<10));
  tegu_render_put(p+4,a->input_rate<<6);tegu_render_put(p+8,offsets[i]);
  tegu_render_put(p+12,32+a->buffer_index);tegu_render_put(p+16,strides[i]);
 }
 for(unsigned i=0;i<TEGU_VERTEX_SLOTS;i++)if(used&(1u<<i)) {
  unsigned char *p=d+0x100+(32+i)*32;
  tegu_render_put(p,0x19);tegu_render_put(p+4,(uint32_t)buffers[i].length);vertex_u64(p+8,buffers[i].address);
 }
 return 0;
}
