#ifndef TEGU_COMPOSITOR_V7_SCRATCH_LAYOUT_H
#define TEGU_COMPOSITOR_V7_SCRATCH_LAYOUT_H
#include <stdint.h>

/* v7's larger fragment stage is isolated from the frozen v6 layout. */
#define TEGU_V7_PAGE 4096u
#define TEGU_V7_SCRATCH_PAGES 112u
#define TEGU_V7_SCRATCH_BYTES (TEGU_V7_SCRATCH_PAGES * TEGU_V7_PAGE)
#define TEGU_V7_VS_OFFSET 0x2000u
#define TEGU_V7_VS_PAGES 2u
#define TEGU_V7_FS_OFFSET 0x30000u
#define TEGU_V7_FS_PAGES 7u
#define TEGU_V7_GEOMETRY_OFFSET 0x10000u
#define TEGU_V7_GEOMETRY_BYTES 0x10000u
#define TEGU_V7_CHUNK_OFFSET 0x50000u
#define TEGU_V7_CHUNK_BYTES 0x20000u
#define TEGU_V7_LOAD_SHADER_OFFSET 0xe000u
#define TEGU_V7_LOAD_SHADER_BYTES 0x1000u
#define TEGU_V7_CLEAR_EXEC_MASK UINT64_C(0x007f00000000000c)
#define TEGU_V7_LOAD_EXEC_MASK  UINT64_C(0x007f00000000400c)
#define TEGU_B3_FS_PAGES 3u
#define TEGU_B3_CLEAR_EXEC_MASK UINT64_C(0x000700000000000c)

struct tegu_v7_scratch_layout {
    uint32_t scratch_bytes;
    uint32_t vs_offset, vs_capacity;
    uint32_t fs_offset, fs_capacity;
    uint32_t geometry_offset, geometry_bytes;
    uint32_t chunk_offset, chunk_bytes;
    uint32_t load_shader_offset, load_shader_bytes;
    uint64_t target_va;
    uint64_t exec_mask;
};

/* Validates real package stage sizes and returns the sole supported v7 map.
 * Stage offsets are fixed so aliases cannot be supplied by package data. */
int tegu_v7_scratch_layout_build(uint32_t vs_bytes, uint32_t fs_bytes,
    int load_pass, struct tegu_v7_scratch_layout *out);
int tegu_b3_scratch_layout_build(uint32_t vs_bytes,uint32_t fs_bytes,
    int load_pass,struct tegu_v7_scratch_layout *out);
int tegu_ui1_scratch_layout_build(uint32_t vs_bytes,uint32_t fs_bytes,
    int load_pass,struct tegu_v7_scratch_layout *out);
int tegu_ui2_scratch_layout_build(uint32_t vs_bytes,uint32_t fs_bytes,
    int load_pass,struct tegu_v7_scratch_layout *out);
/* Clears only v7 geometry/chunk working memory; never touches staged shaders,
 * descriptors, resource tables, CPU state, or the LOAD helper page. */
int tegu_v7_scratch_prepare(void *memory, uint32_t bytes);
#endif
