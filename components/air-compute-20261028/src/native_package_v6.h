#ifndef TEGU_COMPOSITOR_NATIVE_PACKAGE_V6_H
#define TEGU_COMPOSITOR_NATIVE_PACKAGE_V6_H
#include <stddef.h>
#include <stdint.h>
#include "blend_state.h"
#define TEGU_NATIVE_V6_HEADER 512u
#define TEGU_NATIVE_V6_PUSH 512u
#define TEGU_NATIVE_V6_ATTR 768u
#define TEGU_NATIVE_V6_CODE 1664u
#define TEGU_NATIVE_V6_STAGE_MAX 8192u
#define TEGU_NATIVE_V6_PACKAGE_MAX (TEGU_NATIVE_V6_CODE+2u*TEGU_NATIVE_V6_STAGE_MAX)
#define TEGU_NATIVE_V6_VS_PAGE_OFFSET 0x2000u
#define TEGU_NATIVE_V6_FS_PAGE_OFFSET 0xe000u
#define TEGU_NATIVE_V6_PAGE_BYTES 4096u
struct tegu_native_v6_stage {uint32_t bytes,offset,registers,fau_words,sysvals,push_bytes;uint64_t preload;};
struct tegu_native_v6_attachment {uint32_t format,write_mask;};
struct tegu_native_v6_info {
 uint32_t format,version,attr_mask,attr_count,position_offset,secondary_offset,secondary_registers,varying_bytes;
 uint64_t secondary_preload;
 uint32_t ubo_stage[4],ubo_metal_slot[4],ubo_window[4],push_residual_offset[4];
 uint32_t texture_metal_slot,sampler_metal_slot,texel_format_push_offset;
 uint32_t attachment_count;
 struct tegu_blend_inputs blend;
 uint32_t attributes[2][7];
 struct tegu_native_v6_attachment attachment[8];
 struct tegu_native_v6_stage stage[2];
};
/* Push snapshots and table handle are trusted-driver inputs, not user GPU VAs.
 * The function copies both snapshots and records the already-resolved table1
 * handle. It does not resolve/map memory, own lifetimes, or submit work. */
struct tegu_native_v6_binding {
 uint64_t table1_handle;
 uint8_t vertex_push[128],fragment_push[128];
 uint32_t ubo_offset[4],ubo_length[4];
};
struct tegu_native_v6_checked {
 uint64_t table1_handle;
 uint8_t vertex_push[128],fragment_push[128];
 uint32_t ubo_base_offset[4],ubo_window[4];
};
struct tegu_native_v6_layout {uint32_t vs_offset,vs_bytes,fs_offset,fs_bytes,vs_rx,fs_rx;};
int tegu_native_v6_parse(const void *,size_t,struct tegu_native_v6_info *);
int tegu_native_v6_bind(const void *,size_t,const struct tegu_native_v6_binding *,struct tegu_native_v6_checked *);
int tegu_native_v6_layout_build(const void *,size_t,uint32_t,struct tegu_native_v6_layout *);
/* Host-side copy/layout primitive; no PTE mapping or executable permission is set. */
int tegu_native_v6_stage_copy(const void *,size_t,void *,size_t,struct tegu_native_v6_layout *);
#endif
