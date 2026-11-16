#ifndef TEGU_COMPOSITOR_NATIVE_PACKAGE_V7_H
#define TEGU_COMPOSITOR_NATIVE_PACKAGE_V7_H
#include <stddef.h>
#include <stdint.h>
#include "blend_state.h"
#define TEGU_NATIVE_V7_HEADER 512u
#define TEGU_NATIVE_V7_PUSH 512u
#define TEGU_NATIVE_V7_ATTR 768u
#define TEGU_NATIVE_V7_CODE 1664u
#define TEGU_NATIVE_V7_STAGE_MAX 32768u
#define TEGU_NATIVE_V7_VS_BYTES 1152u
#define TEGU_NATIVE_V7_FS_BYTES 25600u
#define TEGU_NATIVE_V7_BLEND_SOURCE_OVER 1u
#define TEGU_NATIVE_V7_BLEND_DISABLED 2u
#define TEGU_NATIVE_V7_PACKAGE_BYTES (TEGU_NATIVE_V7_CODE+TEGU_NATIVE_V7_VS_BYTES+TEGU_NATIVE_V7_FS_BYTES)
#define TEGU_NATIVE_V7_SCRATCH_BYTES 0x70000u
#define TEGU_NATIVE_V7_PACKAGE_MAX (TEGU_NATIVE_V7_CODE+TEGU_NATIVE_V7_VS_BYTES+TEGU_NATIVE_V7_FS_BYTES)
#define TEGU_NATIVE_V7_VS_PAGE_OFFSET 0x2000u
#define TEGU_NATIVE_V7_PAGE_BYTES 4096u
struct tegu_native_v7_stage {uint32_t bytes,offset,registers,fau_words,sysvals,push_bytes;uint64_t preload;};
struct tegu_native_v7_attachment {uint32_t format,write_mask;};
struct tegu_native_v7_info {
 uint32_t format,version,attr_mask,attr_count,position_offset,secondary_offset,secondary_registers,varying_bytes;
 uint64_t secondary_preload;
 uint32_t ubo_stage[4],ubo_metal_slot[4],ubo_window[4],ubo_declared_bytes[4],push_residual_offset[4];
 uint32_t texture_metal_slot,sampler_metal_slot,texel_format_push_offset,sample_count;
 uint32_t attachment_count;
 struct tegu_blend_inputs blend;
 uint32_t attributes[2][7];
 struct tegu_native_v7_attachment attachment[8];
 struct tegu_native_v7_stage stage[2];
};
/* Push snapshots and table handle are trusted-driver inputs, not user GPU VAs.
 * The function copies both snapshots and records the already-resolved table1
 * handle. It does not resolve/map memory, own lifetimes, or submit work. */
struct tegu_native_v7_binding {
 uint64_t table1_handle;
 uint8_t vertex_push[128],fragment_push[128];
 uint32_t ubo_offset[4],ubo_length[4];
};
struct tegu_native_v7_checked {
 uint64_t table1_handle;
 uint8_t vertex_push[128],fragment_push[128];
 uint32_t ubo_base_offset[4],ubo_window[4];
};
struct tegu_native_v7_layout {uint32_t vs_offset,vs_bytes,vs_pages,fs_offset,fs_bytes,fs_pages,scratch_bytes,vs_rx,fs_rx;};
#ifdef __cplusplus
extern "C" {
#endif
int tegu_native_v7_parse(const void *,size_t,struct tegu_native_v7_info *);
int tegu_native_v7_blend_class(const struct tegu_blend_inputs *);
int tegu_native_v7_bind(const void *,size_t,const struct tegu_native_v7_binding *,struct tegu_native_v7_checked *);
int tegu_native_v7_layout_build(const void *,size_t,uint32_t,struct tegu_native_v7_layout *);
/* Host-side copy/layout primitive; no PTE mapping or executable permission is set. */
int tegu_native_v7_stage_copy(const void *,size_t,void *,size_t,struct tegu_native_v7_layout *);
#ifdef __cplusplus
}
#endif
#endif
