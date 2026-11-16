/* Kernel-only native render submission. No user addresses or command buffers.
 * Caller owns/retains 256KiB scratch, a separate target span and all vertex
 * buffers represented by page_count, publishes
 * CPU stores before submission, and quarantines ALL backing if return==-100.
 * Aliases: scratch05100000..0513ffff; the target starts at05140000 and may
 * span up to191 pages before any resource aliases; readonly vertex/texture pages follow it in the same
 * existing 1MiB aperture. Scratch pages2/3 hold executable VS/FS code.
 * Readonly resource aliases are non-executable; scratch and target pages are
 * writable and non-executable. Commands are trusted output
 * of render_state.c in immutable kernel-owned memory, lifetime through return.
 */
#ifndef TEGU_RENDER_JOB_H
#define TEGU_RENDER_JOB_H
#include <stdint.h>
#define TEGU_RENDER_SCRATCH_BYTES 0x40000u
#define TEGU_RENDER_SCRATCH_VA 0x05100000ULL
#define TEGU_RENDER_TARGET_VA 0x05140000ULL
#define TEGU_RENDER_VERTEX_VA 0x05141000ULL
#define TEGU_RENDER_BASE_PAGES 65u
#define TEGU_RENDER_PAGES 256u
struct tegu_render_job {
 uint64_t pages[TEGU_RENDER_PAGES];
 const uint64_t *commands;
 uint32_t command_count,page_count;
 /* Target alias extent is dynamic; keep the 256-page aperture and old prefix. */
 uint32_t target_page_start,target_page_count;
 /* Optional trusted LOAD pages follow the target; masks are relative to start. */
 uint32_t load_page_start,load_page_count;
 uint8_t readonly_load_mask,executable_load_mask;
};
#endif
