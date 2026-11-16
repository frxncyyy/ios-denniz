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
void tegu_mali_free(struct tegu_mali_blob *result);
#ifdef __cplusplus
}
#endif
#endif
