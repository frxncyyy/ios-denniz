#include "texture_state.h"
#ifdef NORD2_JM_TRANSPORT
/* This arch11 donor builder is not part of the active Nord2 JM transport.
 * If a future build accidentally links it, refuse explicitly rather than
 * emitting an arch11/v10 descriptor into the arch9 path. */
int tegu_texture_state_build(const struct tegu_texture_input *in,struct tegu_texture_state *out) {
 (void)in;(void)out;return -2;
}
#else
static void texture_u32(unsigned char *p,uint32_t v) {
 p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);
}
static void texture_u64(unsigned char *p,uint64_t v) {texture_u32(p,(uint32_t)v);texture_u32(p+4,(uint32_t)(v>>32));}
static int texture_contract(const struct tegu_texture_input *in) {
 uint32_t expected_stride=in->format==70?64:in->format==115?128:0;
 uint64_t expected_bytes=in->format==70?1024:in->format==115?2048:0;
 return expected_bytes&&in->width==16&&in->height==16&&in->levels==1&&in->layers==1&&
  in->row_stride==expected_stride&&
  !in->min_filter&&!in->mag_filter&&!in->mip_filter&&!in->address_r&&
  !in->address_s&&!in->address_t&&in->normalized==1&&!in->compare&&in->max_anisotropy==1&&
  !in->lod_min_q8&&!in->lod_max_q8&&!in->options&&!in->reserved;
}
int tegu_texture_state_build(const struct tegu_texture_input *in,struct tegu_texture_state *out) {
 if(!in||!out)return -1;
 uintptr_t a=(uintptr_t)in,b=(uintptr_t)out;
 if(a<=b?b-a<sizeof(*in):a-b<sizeof(*out))return -1;
 if(in->version!=TEGU_TEXTURE_STATE_VERSION)return -1;
 if(!texture_contract(in))return -2;
 uint64_t d=in->descriptor_address,t=in->texel_address,n=in->texel_length;
 if((in->format==115&&n!=2048)||!d||(d&4095)||d>(1ULL<<48)-TEGU_TEXTURE_DESCRIPTOR_BYTES||!t||(t&4095)||
    n<TEGU_TEXTURE_TEXEL_BYTES||n>4096||t>(1ULL<<48)-n)return -3;
 if(t<d+TEGU_TEXTURE_DESCRIPTOR_BYTES&&d<t+n)return -4;
 // All checks precede the first output store; no multi-KiB kernel stack temp.
 unsigned char *raw=(unsigned char *)out;for(size_t i=0;i<sizeof(*out);i++)raw[i]=0;
 out->resource_table=d|4;unsigned char *p=out->descriptors;
 // RESOURCE: sampler index0 and texture index1, contains_descriptors bit56.
 texture_u64(p,(d+0x100)|(1ULL<<56));texture_u64(p+8,64);
 // SAMPLER (v10.xml): type1, clamp R/T/S=9, round-even, seamless cube,
 // normalized, array-clamp, min/mag nearest; LOD/compare/aniso fields zero.
 texture_u32(p+0x100,1u|(9u<<8)|(9u<<12)|(9u<<16)|(1u<<21)|(1u<<23)|(1u<<25)|(1u<<26)|(3u<<27));
 // TEXTURE: type2, 2D, RGBA8_UNORM=187 with component order RGBA=0,
 // identity swizzle 0x688, one level/layer/sample, surfaces at plane.
 uint32_t format_code=in->format==70?187u:223u;
 texture_u32(p+0x120,2u|(2u<<4)|((format_code<<12)<<10));texture_u32(p+0x124,15u|(15u<<16));
 texture_u32(p+0x128,0x688);texture_u64(p+0x130,d+0x140);
 // GENERIC_PLANE: type10, generic, linear clumps, RAW32; 1024-byte extent.
 uint32_t plane_bytes=in->format==70?1024u:2048u;
 uint32_t row_stride=in->format==70?64u:128u;
 uint32_t clump=in->format==70?2u:3u;
 texture_u32(p+0x140,10u|(1u<<4)|(2u<<8)|(clump<<24));texture_u32(p+0x144,plane_bytes);
 texture_u64(p+0x148,t);texture_u32(p+0x150,row_stride);
 /* Mesa's v10 2D linear producer emits zero slice_stride. Preserve the
  * qualified RGBA8 descriptor bytes; FP16 requires the producer form. */
 texture_u32(p+0x158,in->format==70?plane_bytes:0);
 texture_u32(p+0x15c,15u|(15u<<16));
 return 0;
}
#endif
