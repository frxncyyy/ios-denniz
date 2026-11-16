#ifndef TEGU_RENDER_LOAD_H
#define TEGU_RENDER_LOAD_H
#include "render_state.h"
#include "render_job.h"
#include "render_target_request.h"

/* Research-only additive pass contract: version 2 keeps the selector15 size
 * and payload, and uses reserved[0] as a loadAction enum. */
#define TEGU_RENDER_TARGET_REQUEST_VERSION_LOAD 2u
#define TEGU_RENDER_PASS_LOAD 1u
#define TEGU_GPU_CAP_LOAD_PASS (1ULL<<12)

/* The load pre-pass reads the target through a Mesa-generated texture resource
 * graph and writes the existing target before the user draw. Fixed 16x16,
 * linear RGBA8 only; addresses are kernel-selected aliases, never user data. */
/* The draw tiler owns scratch pages16..63.  The target is alias page64, so
 * append three separately owned LOAD pages after it (65..67). */
#define TEGU_LOAD_PAGE_START 65u
#define TEGU_LOAD_PAGE_COUNT 3u
#define TEGU_LOAD_PAGE_BYTES (TEGU_LOAD_PAGE_COUNT*4096u)
#define TEGU_LOAD_STAGE_OFFSET TEGU_LOAD_PAGE_BYTES
#define TEGU_LOAD_STAGE_CAPACITY (4u*4096u)
#define TEGU_LOAD_RESOURCES_VA (TEGU_RENDER_SCRATCH_VA+0x41000ULL)
#define TEGU_LOAD_DCD_VA       (TEGU_RENDER_SCRATCH_VA+0x42000ULL)
#define TEGU_LOAD_SHADER_VA    (TEGU_RENDER_SCRATCH_VA+0x43000ULL)
struct tegu_render_load_state {
 unsigned char resources[4096];
 unsigned char dcd[4096];
 unsigned char shader_page[4096];
 uint8_t readonly_page_mask;
 uint8_t executable_page_mask;
 uint32_t page_start,page_count;
};
/* Writes into caller-owned temporary storage after validating every input and
 * rejecting CPU input/output aliasing. On failure both outputs remain intact.
 * Mutates only the FBP and RT descriptor of draw on success. Caller owns three
 * GPU pages at pages65..67 and maps them RO/NX, RO/NX, and RO/X to completion. */
int tegu_render_build_load_pass(struct tegu_render_state *draw,
 const void *package,size_t package_bytes,const struct tegu_render_addresses *addresses,
 const struct tegu_render_target_layout *layout,
 struct tegu_render_load_state *out);
#endif
