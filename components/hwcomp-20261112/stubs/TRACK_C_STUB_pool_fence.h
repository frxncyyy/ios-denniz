/*
 * TRACK C TEMPORARY STUB -- NOT the real interface headers.  Written by the Track C agent (patch 0396, hwfb-20261112) because
 * Track A's sweet_fb_hw_pool.h (0394) and Track B's nord2_gpu_fence.h (0395) did not exist yet.  These are the exact signatures
 * SweetFramebuffer.cpp (0396) declares when those headers are not on its include path (see the __has_include block there);
 * when the real headers are present SweetFramebuffer.cpp includes them instead, so the compiler checks the match.
 * All are extern "C", defined in SweetSurface.cpp (A) / Nord2GPU.cpp (B).
 */
#ifndef TRACK_C_STUB_POOL_FENCE_H
#define TRACK_C_STUB_POOL_FENCE_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* A (0394): true when [pa, pa+len) lies entirely inside the reserved display pool (0xC0000000..0xD0000000) AND is allocated. */
bool     sweet_fb_hw_pool_contains(uint64_t pa, uint64_t len);
/* A (0394): physical address of the surface's pixel memory when it is a pool surface, else 0. */
uint64_t sweet_surface_pool_pa(uint32_t surface_id);
/* B (0395): sequence number of the newest GPU job submitted that writes the surface (0 = never written by the GPU). */
uint64_t nord2_gpu_surface_write_seq(uint32_t surface_id);
/* B (0395): true while a GPU job that writes the surface is queued or running. */
bool     nord2_gpu_surface_write_pending(uint32_t surface_id);
/* B (0395): wait until no GPU write job on the surface is in flight; true = idle, false = timed out. (Not used by 0396: the
 * present thread polls, it never sleeps in the GPU driver.) */
bool     nord2_gpu_surface_wait_idle(uint32_t surface_id, uint32_t timeout_us);
#ifdef __cplusplus
}
#endif
#endif
