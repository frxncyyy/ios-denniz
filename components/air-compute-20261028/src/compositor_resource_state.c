#include "compositor_resource_state.h"
#include <stddef.h>
#include <string.h>
#define TEGU_COMP_RES_VA_LIMIT (1ull<<48)
static void tegu_comp_res_put32(uint8_t *p,uint32_t x){p[0]=x;p[1]=x>>8;p[2]=x>>16;p[3]=x>>24;}
static void tegu_comp_res_put64(uint8_t *p,uint64_t x){tegu_comp_res_put32(p,x);tegu_comp_res_put32(p+4,x>>32);}
static int tegu_comp_res_range_ok(struct tegu_gpu_span s,uint64_t align,uint64_t minimum,uint64_t maximum){
 return s.address && !(s.address&(align-1)) && s.bytes>=minimum && s.bytes<=maximum && s.address<TEGU_COMP_RES_VA_LIMIT && s.bytes<=TEGU_COMP_RES_VA_LIMIT-s.address;
}
static int tegu_comp_res_overlap(struct tegu_gpu_span a, uint64_t alen, struct tegu_gpu_span b, uint64_t blen){return alen&&blen&&a.address < b.address+blen && b.address < a.address+alen;}
static void tegu_comp_res_pack_resource(uint8_t *p,uint64_t address,uint64_t size,int contains){
 tegu_comp_res_put64(p,address & ((1ull<<56)-1));
 uint64_t hi=(address>>32)&0x00ffffffull;
 if(contains)hi|=1ull<<24;
 tegu_comp_res_put32(p+4,(uint32_t)hi);tegu_comp_res_put64(p+8,size);
}
static void tegu_comp_res_pack_buffer(uint8_t *p,uint64_t address,uint64_t size){
 memset(p,0,32);tegu_comp_res_put32(p,9u|(1u<<4)); /* BUFFER, SIMPLE */
 tegu_comp_res_put32(p+4,(uint32_t)size);tegu_comp_res_put64(p+8,address);
}
/* Generated v10 descriptor bit layouts: enum constants/field positions match
 * Mesa v10_pack.h MALI_SAMPLER/TEXTURE/GENERIC_PLANE packers. */
static void tegu_comp_res_pack_sampler(uint8_t *p){
 memset(p,0,32);tegu_comp_res_put32(p,1u|(9u<<8)|(9u<<12)|(9u<<16)|(1u<<21)|(1u<<23)|(1u<<25)|(1u<<26)|(1u<<27)|(1u<<28));
}
static void tegu_comp_res_pack_texture(uint8_t *p,uint64_t plane,uint32_t width,uint32_t height,
 uint32_t metal_format){
 memset(p,0,32);tegu_comp_res_put32(p,2u|(2u<<4)|((187u<<12)<<10)|
  (metal_format==80?0x1000u:0u));
 tegu_comp_res_put32(p+4,(width-1u)|((height-1u)<<16));tegu_comp_res_put32(p+8,0x688u);tegu_comp_res_put64(p+16,plane);
}
static void tegu_comp_res_pack_plane(uint8_t *p,uint64_t texels,uint32_t width,uint32_t height,
 uint32_t stride,uint32_t bytes){
 memset(p,0,32);tegu_comp_res_put32(p,10u|(1u<<4)|(2u<<8)|(2u<<24));tegu_comp_res_put32(p+4,bytes);
 tegu_comp_res_put64(p+8,texels);tegu_comp_res_put32(p+16,stride);
 tegu_comp_res_put32(p+28,(width-1u)|((height-1u)<<16));
}
static int tegu_compositor_resource_state_build_class(const struct tegu_compositor_resource_input *in,
 struct tegu_compositor_resource_state *out,unsigned version,uint32_t width,uint32_t height,uint32_t stride){
 if(!in||!out)return -1;
 uintptr_t a=(uintptr_t)in,b=(uintptr_t)out;
 if(a<=b?b-a<sizeof(*in):a-b<sizeof(*out))return -1;
 if(in->version!=TEGU_RESOURCE_STATE_VERSION)return -1;
 const uint64_t minbytes[4]={64,16,version==7?32u:version==8?96u:80u,16};
 uint64_t texel_bytes=(uint64_t)stride*height;
 if(!width||!height||width>4096u||height>4096u||(version!=8&&(stride&63u))||
    stride<(uint64_t)width*4u||stride>4096u*16u||!texel_bytes||
    texel_bytes>(16u*1024u*1024u)||texel_bytes>UINT32_MAX)return -2;
 if((in->table0.bytes ? !tegu_comp_res_range_ok(in->table0,64,1,UINT32_MAX) :
      (in->table0.address && (in->table0.address>=TEGU_COMP_RES_VA_LIMIT || (in->table0.address&63)))) ||
    !tegu_comp_res_range_ok(in->resource_table,64,64,64) ||
    !tegu_comp_res_range_ok(in->descriptors,32,192,192) || !tegu_comp_res_range_ok(in->plane,32,32,32) ||
    !tegu_comp_res_range_ok(in->texels,4096,texel_bytes,texel_bytes))return -2;
 for(unsigned i=0;i<4;i++)if(!tegu_comp_res_range_ok(in->ubos[i],16,minbytes[i],UINT32_MAX) || (in->ubos[i].bytes&15))return -3;
 struct tegu_gpu_span outputs[4]={in->resource_table,in->descriptors,in->plane,{0,0}};
 uint64_t outbytes[4]={64,192,32,0};
 struct tegu_gpu_span inputs[6]={in->table0,in->ubos[0],in->ubos[1],in->ubos[2],in->ubos[3],in->texels};
 uint64_t inputbytes[6]={in->table0.bytes,in->ubos[0].bytes,in->ubos[1].bytes,in->ubos[2].bytes,in->ubos[3].bytes,texel_bytes};
 for(unsigned i=0;i<3;i++)for(unsigned j=i+1;j<3;j++)if(tegu_comp_res_overlap(outputs[i],outbytes[i],outputs[j],outbytes[j]))return -4;
 for(unsigned i=0;i<3;i++)for(unsigned j=0;j<6;j++)if(tegu_comp_res_overlap(outputs[i],outbytes[i],inputs[j],inputbytes[j]))return -4;
 for(unsigned i=0;i<6;i++)for(unsigned j=i+1;j<6;j++)if(tegu_comp_res_overlap(inputs[i],inputbytes[i],inputs[j],inputbytes[j]))return -4;
 if(tegu_comp_res_overlap(in->plane,32,in->texels,texel_bytes))return -4;
 struct tegu_compositor_resource_state tmp;memset(&tmp,0,sizeof(tmp));
 tmp.resource_table_handle=in->resource_table.address|4u;
 tegu_comp_res_pack_resource(tmp.resource_table,in->table0.address,in->table0.bytes,in->table0.bytes!=0);
 tegu_comp_res_pack_resource(tmp.resource_table+16,in->descriptors.address,192,1);
 for(unsigned i=0;i<4;i++)tegu_comp_res_pack_buffer(tmp.descriptors+i*32,in->ubos[i].address,in->ubos[i].bytes);
 tegu_comp_res_pack_texture(tmp.descriptors+128,in->plane.address,width,height,version==8?80u:70u);
 tegu_comp_res_pack_sampler(tmp.descriptors+160);
 tegu_comp_res_pack_plane(tmp.plane,in->texels.address,width,height,stride,(uint32_t)texel_bytes);
 memcpy(out,&tmp,sizeof(tmp));return 0;
}

int tegu_compositor_resource_state_build(const struct tegu_compositor_resource_input *in,struct tegu_compositor_resource_state *out){return tegu_compositor_resource_state_build_class(in,out,6,16,16,64);}
int tegu_compositor_resource_state_build_v7(const struct tegu_compositor_resource_input *in,struct tegu_compositor_resource_state *out){return tegu_compositor_resource_state_build_class(in,out,7,16,16,64);}
int tegu_compositor_resource_state_build_b3(const struct tegu_compositor_resource_input *in,
 uint32_t width,uint32_t height,uint32_t stride,struct tegu_compositor_resource_state *out){
 return tegu_compositor_resource_state_build_class(in,out,8,width,height,stride);
}
