#include "compositor_v7_scratch_layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    struct tegu_v7_scratch_layout l;
    assert(tegu_v7_scratch_layout_build(1152, 25600, 0, &l));
    assert(l.scratch_bytes == 0x70000 && l.target_va == 0x06070000);
    assert(l.vs_offset == 0x2000 && l.vs_capacity == 0x2000);
    assert(l.fs_offset == 0x30000 && l.fs_capacity == 0x7000);
    assert(l.geometry_offset == 0x10000 && l.geometry_bytes == 0x10000);
    assert(l.chunk_offset == 0x50000 && l.chunk_bytes == 0x20000);
    assert(l.load_shader_offset == 0xe000 && l.exec_mask == TEGU_V7_CLEAR_EXEC_MASK);
    assert(tegu_v7_scratch_layout_build(1152, 25600, 1, &l));
    assert(l.exec_mask == TEGU_V7_LOAD_EXEC_MASK);
    assert(!tegu_v7_scratch_layout_build(1152, 25600, 2, &l));
    assert(!tegu_v7_scratch_layout_build(1152, 25601, 0, &l));
    assert(!tegu_v7_scratch_layout_build(1152, 28672 + 1, 0, &l));
    assert(!tegu_v7_scratch_layout_build(0, 25600, 0, &l));
    assert(!tegu_v7_scratch_layout_build(8193, 25600, 0, &l));
    assert(!tegu_v7_scratch_layout_build(1152, 25600, 0, NULL));
    assert(tegu_b3_scratch_layout_build(1152,8704,0,&l));
    assert(l.fs_offset==0x30000&&l.fs_capacity==0x3000&&
           l.exec_mask==TEGU_B3_CLEAR_EXEC_MASK&&l.scratch_bytes==0x70000);
    assert(!tegu_b3_scratch_layout_build(1152,8703,0,&l));
    assert(!tegu_b3_scratch_layout_build(1152,8705,0,&l));
    assert(!tegu_b3_scratch_layout_build(1151,8704,0,&l));
    assert(!tegu_b3_scratch_layout_build(1152,8704,1,&l));
    assert(!tegu_b3_scratch_layout_build(1152,25600,0,&l));
    assert(!tegu_v7_scratch_layout_build(1152,8704,0,&l));
    unsigned char *mem=(unsigned char *)malloc(TEGU_V7_SCRATCH_BYTES);
    assert(mem);
    memset(mem,0xa5,TEGU_V7_SCRATCH_BYTES);
    assert(!tegu_v7_scratch_prepare(mem,TEGU_V7_SCRATCH_BYTES-1));
    assert(mem[TEGU_V7_FS_OFFSET]==0xa5 && mem[TEGU_V7_CHUNK_OFFSET]==0xa5);
    assert(tegu_v7_scratch_prepare(mem,TEGU_V7_SCRATCH_BYTES));
    assert(mem[TEGU_V7_GEOMETRY_OFFSET]==0 &&
           mem[TEGU_V7_GEOMETRY_OFFSET+TEGU_V7_GEOMETRY_BYTES-1]==0 &&
           mem[TEGU_V7_CHUNK_OFFSET]==0 &&
           mem[TEGU_V7_CHUNK_OFFSET+TEGU_V7_CHUNK_BYTES-1]==0);
    assert(mem[TEGU_V7_FS_OFFSET]==0xa5 && mem[TEGU_V7_VS_OFFSET]==0xa5 &&
           mem[TEGU_V7_LOAD_SHADER_OFFSET]==0xa5 && mem[0x8000]==0xa5);
    free(mem);
    {
        /* UI1 and UI2 LOAD passes use the shared preload shader page and LOAD exec mask. */
        struct tegu_v7_scratch_layout ui={0};
        assert(tegu_ui1_scratch_layout_build(4096u, 1024u, 0, &ui) && ui.exec_mask == TEGU_V7_CLEAR_EXEC_MASK);
        assert(tegu_ui1_scratch_layout_build(4096u, 1024u, 1, &ui) && ui.exec_mask == TEGU_V7_LOAD_EXEC_MASK &&
               ui.load_shader_offset == TEGU_V7_LOAD_SHADER_OFFSET);
        assert(!tegu_ui1_scratch_layout_build(4096u, 1024u, 2, &ui) && !tegu_ui1_scratch_layout_build(4096u, 1152u, 1, &ui));
        assert(tegu_ui2_scratch_layout_build(4096u, 1152u, 1, &ui) && ui.exec_mask == TEGU_V7_LOAD_EXEC_MASK);
    }
    puts("v7 scratch layout tests passed");
    return 0;
}
