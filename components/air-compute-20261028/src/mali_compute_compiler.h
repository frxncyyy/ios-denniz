#ifndef TEGU_MALI_COMPUTE_COMPILER_H
#define TEGU_MALI_COMPUTE_COMPILER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Trusted-input prototype. Some unsupported backend IR can still assert.
 * Texture/image operations are explicitly refused: TGP1 supplies buffers only.
 * Caller serializes compilation. Input bytes may be unaligned. Output must be
 * empty on entry; success owns a TGP1 package disposed by tegu_mali_free.
 * No filesystem or GPU operation is performed by these functions. */
struct tegu_mali_blob { unsigned char *data; size_t size; };
int tegu_mali_compile(const void *spirv,size_t size,struct tegu_mali_blob *result,
                     char *error,size_t error_size);
/* Specialization bytes use SPIR-V little-endian scalar representation.
 * Each id appears once, size is 1/2/4/8, at most64 entries. */
struct tegu_mali_specialization { uint32_t id, size; uint8_t data[8]; };
int tegu_mali_compile_specialized(const void *,size_t,
 const struct tegu_mali_specialization *,size_t,struct tegu_mali_blob *,char *,size_t);
/* Additive TGP2 compiler. Same bounded buffer/push FAU layout as TGP1;
 * live sampled images/storage images use table1[binding], samplers table2[binding],
 * internal fetch/query sampler table2[256]. Output includes a 64-byte-per-resource
 * semantic manifest. Set0, non-array descriptors, buffers0..7/resources0..255.
 * Specialization IDs and scalar widths are checked against this module before
 * Mesa; input/output/error/table spans must not overlap. Empty output required.
 * Successful compilation proves code generation and ABI only, not that the
 * runtime supports every declared format/shape/access or has executed it. */
int tegu_mali_compile_compute_v2(const void *,size_t,
 const struct tegu_mali_specialization *,size_t,struct tegu_mali_blob *,char *,size_t);
/* AIR kernels (air-compute-opus; built with TEGU_MALI_AIR, air/ is the vendored
 * ansuz-metal AIR->NIR frontend, MIT). Same packet, manifest, FAU and push ABI
 * as tegu_mali_compile_specialized (flags 0, TGP1) / tegu_mali_compile_compute_v2
 * (TEGU_MALI_AIR_COMPUTE_V2, TGP2) for the SAME m2v translation: `metadata` is the
 * m2v metadata JSON the bridge returned with that translation's SPIR-V (base
 * pipeline or the dispatch variant whose plan is being instantiated), and
 * `constants` are that plan's region specialization values, keyed by the
 * metadata's specialization ids (LocalSize, MultiRegion, TgLength, SamplerState,
 * ConstantLanes/SparseTexture/Presence). The NIR comes from the AIR function and
 * is adapted to that ABI (set0 binding N of Buffer{Kernel,i} -> FAU/extended
 * record N with the SlotBindOffset residual; textures/samplers -> table1/table2
 * N; DispatchRegion push and MultiRegion for the grid builtins), so the runtime
 * plans, binds and dispatches exactly as for the SPIR-V package. Anything not
 * reproduced exactly is refused (nonzero status, message prefixed "air:").
 * Without TEGU_MALI_AIR the entry exists and always refuses (status 2). */
#define TEGU_MALI_AIR_COMPUTE_VERSION 1u
#define TEGU_MALI_AIR_COMPUTE_V2 1u
struct tegu_mali_air_kernel_constant {
 const char *name;      /* Metal function constant name, or NULL: match index */
 uint32_t index,size;   /* size 1/2/4/8 bytes */
 uint64_t bits;         /* little-endian value bits */
};
struct tegu_mali_air_compute {
 uint32_t version,flags;
 const void *bitcode;size_t size; /* one AIR module (0x0B17C0DE) or a whole metallib ("MTLB") */
 const char *entry;               /* kernel name (required for a metallib) */
 const struct tegu_mali_air_kernel_constant *constants;uint32_t constant_count,reserved;
 const char *metadata;size_t metadata_size;
};
int tegu_mali_compile_compute_air(const struct tegu_mali_air_compute *,
 const struct tegu_mali_specialization *,size_t,struct tegu_mali_blob *,char *,size_t);
void tegu_mali_free(struct tegu_mali_blob *result);
#ifdef __cplusplus
}
#endif
#endif
