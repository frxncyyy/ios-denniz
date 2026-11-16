#ifndef TEGU_GPU_CODECHECK_MAP_H
#define TEGU_GPU_CODECHECK_MAP_H
#include "fullscreen_map.h"

struct tegu_codecheck_leaf {
    uint64_t va,pa,expected,actual;
    unsigned role,match;
};

/* Shared by the real BSD diagnostic and host test: index is the actual
 * scratch-page index, not the ordinal within an inspected range. */
static int tegu_codecheck_fs_leaf(const struct fs_request *r,unsigned index,
    struct tegu_codecheck_leaf *out)
{
    if(!r||!out||!r->scratch||!r->l3||index>=r->scratch_count)return -1;
    unsigned ti=index/FS_L3_ENTRIES,slot=index%FS_L3_ENTRIES;
    if(ti>=r->l3_count||!r->l3[ti].entries)return -2;
    out->va=FS_APERTURE_VA+(uint64_t)index*FS_PAGE_SIZE;
    out->pa=r->scratch[index].pa;
    out->expected=fs_leaf_value(r,index);
    out->actual=r->l3[ti].entries[slot];
    /* v7 has 112 scratch pages, but the execute-role mask tracks only the
     * low 64 entries. Match fs_leaf_value() semantics for higher indexes. */
    out->role=index<64u?(unsigned)((r->scratch_exec_mask>>index)&1u):0u;
    out->match=(unsigned)(out->actual==out->expected);
    return 0;
}
#endif
