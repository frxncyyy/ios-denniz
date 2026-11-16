#ifndef TEGU_COMPOSITOR_RESOURCE_STATE_H
#define TEGU_COMPOSITOR_RESOURCE_STATE_H
#include <stdint.h>
#define TEGU_RESOURCE_STATE_VERSION 1u
#define TEGU_RESOURCE_TABLE_BYTES 64u
#define TEGU_RESOURCE_DESC_BYTES 192u
#define TEGU_RESOURCE_PLANE_BYTES 32u
struct tegu_gpu_span { uint64_t address, bytes; };
struct tegu_compositor_resource_input {
 uint32_t version;
 struct tegu_gpu_span table0; /* driver/attribute table; remains table 0 */
 struct tegu_gpu_span resource_table; /* 4 RESOURCE entries, 64-byte aligned */
 struct tegu_gpu_span descriptors; /* set 0 bindings 0..4 combined texture */
 struct tegu_gpu_span plane; /* standalone generic-plane payload */
 struct tegu_gpu_span texels; /* 16x16 RGBA8 source */
 struct tegu_gpu_span ubos[4]; /* minimum rounded spans 64,16,80,16 */
};
struct tegu_compositor_resource_state {
 uint64_t resource_table_handle;
 uint8_t resource_table[TEGU_RESOURCE_TABLE_BYTES];
 uint8_t descriptors[TEGU_RESOURCE_DESC_BYTES];
 uint8_t plane[TEGU_RESOURCE_PLANE_BYTES];
};
/* Bounded resource class matching tegu_mali_compile_graphics_pair_resources:
 * four readonly UBOs, one 16x16 RGBA8 texture and nearest clamp sampler,
 * driver table0 and shader set0 in table1. This is not the native Metal
 * submission ABI. Trusted, kernel-resolved GPU spans only. No mapping, copying, lifetime or
 * synchronization is performed. Every failure leaves output untouched. */
int tegu_compositor_resource_state_build(const struct tegu_compositor_resource_input *, struct tegu_compositor_resource_state *);
int tegu_compositor_resource_state_build_v7(const struct tegu_compositor_resource_input *, struct tegu_compositor_resource_state *);
int tegu_compositor_resource_state_build_b3(const struct tegu_compositor_resource_input *,
 uint32_t,uint32_t,uint32_t,struct tegu_compositor_resource_state *);
#endif
