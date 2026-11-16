/* Native arch11 fixed-function vertex fetch descriptors. GPU addresses must be
 * resolved and retained by the kernel caller; never accept user GPU addresses.
 * Does not map, publish caches, submit commands or own buffer lifetimes. */
#ifndef TEGU_VERTEX_FETCH_H
#define TEGU_VERTEX_FETCH_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TEGU_VERTEX_SLOTS 31u
#define TEGU_VERTEX_DESCRIPTOR_BYTES 4096u
/* Compiler-linked Vulkan locations and Metal buffer slots are distinct. */
struct tegu_vertex_attribute {
 uint32_t location,buffer_index,format,offset,stride,input_rate,divisor;
};
struct tegu_vertex_buffer {uint64_t address,length;};
struct tegu_vertex_draw {uint32_t first_vertex,vertex_count,instance_count,base_instance;};
static inline uint32_t tegu_vertex_format_bytes(uint32_t format) {
 switch(format) {
 case 83:return 4;
 case 97:return 8;
 case 100:return 4;
 case 103:return 8;
 case 106:return 12;
 case 109:return 16;
 default:return 0;
 }
}
/* Required byte end relative to the bound buffer offset. Shared by encoder
 * snapshotting and kernel validation; does not inspect any GPU address. */
static inline int tegu_vertex_attribute_end(const struct tegu_vertex_attribute *a,
 const struct tegu_vertex_draw *draw,uint64_t *end) {
 uint32_t bytes=a?tegu_vertex_format_bytes(a->format):0;
 if(!a||!draw||!end||!bytes||!draw->vertex_count||!draw->instance_count||a->input_rate>1||a->divisor>1||
    (!a->input_rate&&a->divisor!=1)||(uint64_t)draw->first_vertex+draw->vertex_count-1>0xffffffffULL||
    a->offset>0x7fffffffU)return -1;
 uint64_t offset=a->offset,step=a->stride,last=(uint64_t)draw->first_vertex+draw->vertex_count-1;
 if(a->input_rate) {offset+=(uint64_t)draw->base_instance*step;last=draw->instance_count-1;if(!a->divisor)step=0;}
 if(offset>0x7fffffffULL||offset>~(uint64_t)0-bytes||
    (step&&last>(~(uint64_t)0-offset-bytes)/step))return -1;
 *end=offset+last*step+bytes;return 0;
}
struct tegu_vertex_state {
 uint64_t resource_table;
 uint32_t buffer_mask,reserved;
 unsigned char descriptors[TEGU_VERTEX_DESCRIPTOR_BYTES];
};
/* Required mask comes from the extended Mali compiler. Every required slot
 * needs exactly one attribute and a bounded buffer. Formats: Vulkan RG16F/RGBA16F
 * and R32/RG32/RGB32/RGBA32 float; per-vertex or per-instance(divisor0/1).
 * Other fetches are refused.
 * descriptor_address is a distinct,4KiB-aligned GPU span of4096 bytes, containing
 * four resource entries at0 and63 descriptors at0x100 (31attrs, null,31buffers).
 * Input spans and output must be valid/nonoverlapping. Output unchanged on
 * refusal. Caller copies descriptors to descriptor_address, binds resource_table
 * to IDVS VERTEX_SRT, and uses the same draw indices/counts as validated here. */
int tegu_vertex_fetch_build(uint32_t required,const struct tegu_vertex_attribute *,size_t count,
 const struct tegu_vertex_buffer buffers[TEGU_VERTEX_SLOTS],const struct tegu_vertex_draw *,
 uint64_t descriptor_address,struct tegu_vertex_state *);
#ifdef __cplusplus
}
#endif
#endif
