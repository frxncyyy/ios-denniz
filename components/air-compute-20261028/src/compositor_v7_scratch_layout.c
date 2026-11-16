#include "compositor_v7_scratch_layout.h"

static int disjoint(uint32_t a, uint32_t an, uint32_t b, uint32_t bn)
{
    return a <= UINT32_MAX - an && b <= UINT32_MAX - bn &&
        (a + an <= b || b + bn <= a);
}

static int region_set_valid(void)
{
    static const struct { uint32_t off, len; } r[] = {
        {0x0000,0x1000}, /* descriptor array */
        {0x1000,0x1000}, /* tiler context */
        {0x2000,0x2000}, /* VS, kept at the established two-page VA */
        {0x6000,0x0100}, /* driver descriptors */
        {0x7000,0x0220}, /* resource table, descriptor table, plane */
        {0x8000,0x4000}, /* CPU-built state temporary area */
        {0xe000,0x1000}, /* LOAD preloader only; distinct from v7 FS */
        {0x10000,0x10000}, /* tiler geometry */
        {0x30000,0x7000}, /* v7 FS executable pages 48..54 */
        {0x50000,0x20000} /* relocated tiler chunk */
    };
    for (unsigned i=0;i<sizeof(r)/sizeof(r[0]);i++) {
        if (r[i].off > TEGU_V7_SCRATCH_BYTES ||
            r[i].len > TEGU_V7_SCRATCH_BYTES-r[i].off) return 0;
        for (unsigned j=0;j<i;j++)
            if (!disjoint(r[i].off,r[i].len,r[j].off,r[j].len)) return 0;
    }
    return 1;
}

int tegu_v7_scratch_layout_build(uint32_t vs_bytes, uint32_t fs_bytes,
    int load_pass, struct tegu_v7_scratch_layout *out)
{
    if (!out || vs_bytes != 1152u || fs_bytes != 25600u ||
        (load_pass != 0 && load_pass != 1)) return 0;
    if (!disjoint(TEGU_V7_VS_OFFSET, TEGU_V7_VS_PAGES * TEGU_V7_PAGE,
                  TEGU_V7_LOAD_SHADER_OFFSET, TEGU_V7_LOAD_SHADER_BYTES) ||
        !disjoint(TEGU_V7_VS_OFFSET, TEGU_V7_VS_PAGES * TEGU_V7_PAGE,
                  TEGU_V7_FS_OFFSET, TEGU_V7_FS_PAGES * TEGU_V7_PAGE) ||
        !region_set_valid() ||
        TEGU_V7_CHUNK_OFFSET + TEGU_V7_CHUNK_BYTES != TEGU_V7_SCRATCH_BYTES)
        return 0;
    *out = (struct tegu_v7_scratch_layout) {
        .scratch_bytes = TEGU_V7_SCRATCH_BYTES,
        .vs_offset = TEGU_V7_VS_OFFSET,
        .vs_capacity = TEGU_V7_VS_PAGES * TEGU_V7_PAGE,
        .fs_offset = TEGU_V7_FS_OFFSET,
        .fs_capacity = TEGU_V7_FS_PAGES * TEGU_V7_PAGE,
        .geometry_offset = TEGU_V7_GEOMETRY_OFFSET,
        .geometry_bytes = TEGU_V7_GEOMETRY_BYTES,
        .chunk_offset = TEGU_V7_CHUNK_OFFSET,
        .chunk_bytes = TEGU_V7_CHUNK_BYTES,
        .load_shader_offset = TEGU_V7_LOAD_SHADER_OFFSET,
        .load_shader_bytes = TEGU_V7_LOAD_SHADER_BYTES,
        .target_va = UINT64_C(0x06000000) + TEGU_V7_SCRATCH_BYTES,
        .exec_mask = load_pass ? TEGU_V7_LOAD_EXEC_MASK : TEGU_V7_CLEAR_EXEC_MASK
    };
    return 1;
}

int tegu_b3_scratch_layout_build(uint32_t vs_bytes,uint32_t fs_bytes,
 int load_pass,struct tegu_v7_scratch_layout *out)
{
 if(!out||vs_bytes!=1152u||fs_bytes!=8704u||load_pass)return 0;
 if(!disjoint(TEGU_V7_VS_OFFSET,TEGU_V7_VS_PAGES*TEGU_V7_PAGE,
      TEGU_V7_FS_OFFSET,TEGU_B3_FS_PAGES*TEGU_V7_PAGE)||!region_set_valid()||
    TEGU_V7_CHUNK_OFFSET+TEGU_V7_CHUNK_BYTES!=TEGU_V7_SCRATCH_BYTES)return 0;
 *out=(struct tegu_v7_scratch_layout){
  .scratch_bytes=TEGU_V7_SCRATCH_BYTES,.vs_offset=TEGU_V7_VS_OFFSET,
  .vs_capacity=TEGU_V7_VS_PAGES*TEGU_V7_PAGE,.fs_offset=TEGU_V7_FS_OFFSET,
  .fs_capacity=TEGU_B3_FS_PAGES*TEGU_V7_PAGE,
  .geometry_offset=TEGU_V7_GEOMETRY_OFFSET,.geometry_bytes=TEGU_V7_GEOMETRY_BYTES,
  .chunk_offset=TEGU_V7_CHUNK_OFFSET,.chunk_bytes=TEGU_V7_CHUNK_BYTES,
  .load_shader_offset=TEGU_V7_LOAD_SHADER_OFFSET,.load_shader_bytes=TEGU_V7_LOAD_SHADER_BYTES,
  .target_va=UINT64_C(0x06000000)+TEGU_V7_SCRATCH_BYTES,
  .exec_mask=TEGU_B3_CLEAR_EXEC_MASK
 };
 return 1;
}

int tegu_ui1_scratch_layout_build(uint32_t vs_bytes,uint32_t fs_bytes,
 int load_pass,struct tegu_v7_scratch_layout *out)
{
 if(!out||vs_bytes!=4096u||fs_bytes!=1024u||(load_pass!=0&&load_pass!=1)||
    !disjoint(TEGU_V7_VS_OFFSET,TEGU_V7_VS_PAGES*TEGU_V7_PAGE,
      TEGU_V7_LOAD_SHADER_OFFSET,TEGU_V7_LOAD_SHADER_BYTES)||
    !disjoint(TEGU_V7_FS_OFFSET,TEGU_V7_PAGE,
      TEGU_V7_LOAD_SHADER_OFFSET,TEGU_V7_LOAD_SHADER_BYTES)||!region_set_valid())return 0;
 *out=(struct tegu_v7_scratch_layout){
  .scratch_bytes=TEGU_V7_SCRATCH_BYTES,.vs_offset=TEGU_V7_VS_OFFSET,
  .vs_capacity=TEGU_V7_VS_PAGES*TEGU_V7_PAGE,.fs_offset=TEGU_V7_FS_OFFSET,
  .fs_capacity=TEGU_V7_PAGE,.geometry_offset=TEGU_V7_GEOMETRY_OFFSET,
  .geometry_bytes=TEGU_V7_GEOMETRY_BYTES,.chunk_offset=TEGU_V7_CHUNK_OFFSET,
  .chunk_bytes=TEGU_V7_CHUNK_BYTES,.load_shader_offset=TEGU_V7_LOAD_SHADER_OFFSET,
  .load_shader_bytes=TEGU_V7_LOAD_SHADER_BYTES,
  .target_va=UINT64_C(0x06000000)+TEGU_V7_SCRATCH_BYTES,
  .exec_mask=load_pass?TEGU_V7_LOAD_EXEC_MASK:TEGU_V7_CLEAR_EXEC_MASK
 };
 return 1;
}

int tegu_ui2_scratch_layout_build(uint32_t vs_bytes,uint32_t fs_bytes,
 int load_pass,struct tegu_v7_scratch_layout *out)
{
 if(!out||vs_bytes!=4096u||fs_bytes!=1152u||(load_pass!=0&&load_pass!=1)||
    !disjoint(TEGU_V7_VS_OFFSET,TEGU_V7_VS_PAGES*TEGU_V7_PAGE,
      TEGU_V7_LOAD_SHADER_OFFSET,TEGU_V7_LOAD_SHADER_BYTES)||
    !disjoint(TEGU_V7_FS_OFFSET,TEGU_V7_PAGE,
      TEGU_V7_LOAD_SHADER_OFFSET,TEGU_V7_LOAD_SHADER_BYTES)||!region_set_valid())return 0;
 *out=(struct tegu_v7_scratch_layout){
  .scratch_bytes=TEGU_V7_SCRATCH_BYTES,.vs_offset=TEGU_V7_VS_OFFSET,
  .vs_capacity=TEGU_V7_VS_PAGES*TEGU_V7_PAGE,.fs_offset=TEGU_V7_FS_OFFSET,
  .fs_capacity=TEGU_V7_PAGE,.geometry_offset=TEGU_V7_GEOMETRY_OFFSET,
  .geometry_bytes=TEGU_V7_GEOMETRY_BYTES,.chunk_offset=TEGU_V7_CHUNK_OFFSET,
  .chunk_bytes=TEGU_V7_CHUNK_BYTES,.load_shader_offset=TEGU_V7_LOAD_SHADER_OFFSET,
  .load_shader_bytes=TEGU_V7_LOAD_SHADER_BYTES,
  .target_va=UINT64_C(0x06000000)+TEGU_V7_SCRATCH_BYTES,
  .exec_mask=load_pass?TEGU_V7_LOAD_EXEC_MASK:TEGU_V7_CLEAR_EXEC_MASK
 };
 return 1;
}

int tegu_v7_scratch_prepare(void *memory, uint32_t bytes)
{
    if (!memory || bytes < TEGU_V7_SCRATCH_BYTES || !region_set_valid()) return 0;
    uint8_t *p=(uint8_t *)memory;
    for (uint32_t i=0;i<TEGU_V7_GEOMETRY_BYTES;i++)
        p[TEGU_V7_GEOMETRY_OFFSET+i]=0;
    for (uint32_t i=0;i<TEGU_V7_CHUNK_BYTES;i++)
        p[TEGU_V7_CHUNK_OFFSET+i]=0;
    return 1;
}
