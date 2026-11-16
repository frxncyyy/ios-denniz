#ifndef TEGU_SINGLE_PASS_BLUR_NATIVE_PACKAGE_B3_H
#define TEGU_SINGLE_PASS_BLUR_NATIVE_PACKAGE_B3_H
#include <stddef.h>
#include <stdint.h>
#include "blend_state.h"

/* Additive TGR class for downsample_blur_vert_lph +
 * single_pass_blur_3_lph. Version 8 keeps the established v7 staging aperture
 * while giving the distinct four-sample shader its own exact wire contract. */
#define TEGU_NATIVE_B3_VERSION 8u
#define TEGU_NATIVE_B3_HEADER 512u
#define TEGU_NATIVE_B3_PUSH 512u
#define TEGU_NATIVE_B3_ATTR 768u
#define TEGU_NATIVE_B3_CODE 1664u
#define TEGU_NATIVE_B3_VS_BYTES 1152u
#define TEGU_NATIVE_B3_FS_BYTES 8704u
#define TEGU_NATIVE_B3_PACKAGE_BYTES \
 (TEGU_NATIVE_B3_CODE+TEGU_NATIVE_B3_VS_BYTES+TEGU_NATIVE_B3_FS_BYTES)
#define TEGU_NATIVE_B3_SCRATCH_BYTES 0x70000u
#define TEGU_NATIVE_B3_VS_PAGE_OFFSET 0x2000u
#define TEGU_NATIVE_B3_FS_PAGE_OFFSET 0x30000u
#define TEGU_NATIVE_B3_PAGE_BYTES 4096u

struct tegu_native_b3_stage {
 uint32_t bytes,offset,registers,fau_words,sysvals,push_bytes;
 uint64_t preload;
};
struct tegu_native_b3_attachment {uint32_t format,write_mask;};
struct tegu_native_b3_info {
 uint32_t format,version,attr_mask,attr_count,position_offset,secondary_offset,secondary_registers,varying_bytes;
 uint64_t secondary_preload;
 uint32_t ubo_stage[4],ubo_metal_slot[4],ubo_window[4],ubo_declared_bytes[4],push_residual_offset[4];
 uint32_t texture_metal_slot,sampler_metal_slot,texel_format_push_offset,sample_count;
 uint32_t attachment_count;
 struct tegu_blend_inputs blend;
 uint32_t attributes[2][7];
 struct tegu_native_b3_attachment attachment[8];
 struct tegu_native_b3_stage stage[2];
};
struct tegu_native_b3_binding {
 uint64_t table1_handle;
 uint8_t vertex_push[128],fragment_push[128];
 uint32_t ubo_offset[4],ubo_length[4];
};
struct tegu_native_b3_checked {
 uint64_t table1_handle;
 uint8_t vertex_push[128],fragment_push[128];
 uint32_t ubo_base_offset[4],ubo_window[4];
};
struct tegu_native_b3_layout {
 uint32_t vs_offset,vs_bytes,vs_pages,fs_offset,fs_bytes,fs_pages,scratch_bytes,vs_rx,fs_rx;
};

#ifdef __cplusplus
extern "C" {
#endif
int tegu_native_b3_parse(const void *,size_t,struct tegu_native_b3_info *);
int tegu_native_b3_bind(const void *,size_t,const struct tegu_native_b3_binding *,struct tegu_native_b3_checked *);
int tegu_native_b3_layout_build(const void *,size_t,uint32_t,struct tegu_native_b3_layout *);
int tegu_native_b3_stage_copy(const void *,size_t,void *,size_t,struct tegu_native_b3_layout *);
#ifdef __cplusplus
}
#endif
#endif
