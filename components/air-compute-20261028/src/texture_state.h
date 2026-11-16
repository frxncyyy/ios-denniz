/* Bounded arch11 fragment sampled-texture descriptor packing.
 * GPU addresses must be resolved and retained by the kernel caller; never accept
 * user or compiler GPU addresses, nor CPU texel pointers. Does not map, copy
 * texels, publish caches, submit commands or own resource lifetimes.
 *
 * This is the donor arch11 fixed-UI path, not the active Nord2 arch9 render
 * backend.  The shared arch9 layout/sampler contract is
 * gpu/metal/nord2_texture_resources.h; do not cite this file as evidence for
 * arch9 mip/array/cube/MS support.  The existing 16x16 RGBA8/RGBA16F bytes are
 * preserved unchanged for the qualified fixed-UI probe.  Under
 * NORD2_JM_TRANSPORT this builder is unreachable in the active build and its
 * implementation explicitly returns -2 without touching output if linked. */
#ifndef TEGU_TEXTURE_STATE_H
#define TEGU_TEXTURE_STATE_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TEGU_TEXTURE_STATE_VERSION 1u
#define TEGU_TEXTURE_DESCRIPTOR_BYTES 4096u
#define TEGU_TEXTURE_TEXEL_BYTES 1024u
/* Contract version1, Metal enum numbering: RGBA8Unorm=70 (row64) or RGBA16Float=115 (row128), each 16x16, 1 level, 1 layer, exact source span 1024/2048 bytes. Sampler: min/mag Nearest=0, mip NotMipmapped=0,
 * R/S/T ClampToEdge=0, normalized=1, compare Never=0, anisotropy 1, LOD min/max
 * 0 (unsigned 8.8 fixed), options 0. Every field must match exactly. */
struct tegu_texture_input {
 uint32_t version,format,width,height,levels,layers,row_stride;
 uint32_t min_filter,mag_filter,mip_filter,address_r,address_s,address_t;
 uint32_t normalized,compare,max_anisotropy,lod_min_q8,lod_max_q8,options,reserved;
 uint64_t descriptor_address,texel_address,texel_length;
};
struct tegu_texture_state {
 uint64_t resource_table;
 unsigned char descriptors[TEGU_TEXTURE_DESCRIPTOR_BYTES];
};
/* descriptor_address is a distinct, 4KiB-aligned, nonzero 4096-byte GPU span:
 * four RESOURCE entries at0 (only entry0 used: sampler0/texture1, 64 bytes),
 * SAMPLER 0x100, TEXTURE 0x120, GENERIC_PLANE 0x140, all other bytes zero.
 * texel_address is 4KiB-aligned and nonzero; texel_length is in [1024,4096].
 * Both spans lie below 2^48 and do not overlap. The input and output objects
 * must not alias. Output unchanged on refusal. Caller copies descriptors to
 * descriptor_address and binds resource_table to the FRAGMENT_SRT only. */
int tegu_texture_state_build(const struct tegu_texture_input *,struct tegu_texture_state *);
#ifdef __cplusplus
}
#endif
#endif
