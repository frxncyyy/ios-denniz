#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "TeguSurfaceGPUImportCandidate.h"
#include "TeguSurfaceGPUPage.h"
#include "compositor_iosurface_target_policy.h"

struct page_probe { unsigned calls; unsigned reject_at; uint64_t first_pa; };
static int resolve_gb34(void *opaque, uint64_t offset, uint64_t *pa)
{
    struct page_probe *p=(struct page_probe *)opaque;
    unsigned index=(unsigned)(offset/4096u);
    ++p->calls;
    uint64_t candidate=p->first_pa+(uint64_t)index*4096u;
    if(index==p->reject_at || !tegu_surface_gpu_page_valid(offset,16384,
        candidate,4096,0x882000000ULL,0x40000000ULL)) return 0;
    *pa=candidate;
    return 1;
}

int main(void)
{
    /* Ordinary property-free IOBuffer backing is only a candidate; the same
     * pre-existing descriptor/page policy still has to qualify it below. */
    assert(tegu_surface_gpu_import_candidate(0,1,0));
    assert(tegu_surface_gpu_import_candidate(1,1,1));
    assert(!tegu_surface_gpu_import_candidate(1,1,0));
    assert(!tegu_surface_gpu_import_candidate(0,1,1));
    assert(!tegu_surface_gpu_import_candidate(0,0,0)); /* physical framebuffer */
    assert(!tegu_surface_gpu_import_candidate(1,0,1));

    struct tegu_compositor_iosurface_layout surface={
        .width=64,.height=64,.row_bytes=256,.offset=0,
        .pixel_format=TEGU_IOSURFACE_PIXELFORMAT_BGRA,.allocation_size=16384,
        .plane_count=0,.bytes_per_element=4,.element_width=1,.element_height=1
    };
    struct tegu_fs_surface_layout checked={0};
    struct page_probe p={0,UINT32_MAX,0x883000000ULL};
    assert(tegu_compositor_iosurface_pages_validate(&surface,16384,&checked,
        resolve_gb34,&p));
    assert(checked.target_pages==4 && p.calls==4);

    p=(struct page_probe){0,2,0x883000000ULL};
    assert(!tegu_compositor_iosurface_pages_validate(&surface,16384,&checked,
        resolve_gb34,&p));
    assert(p.calls==3); /* stop on the first rejected page */
    p=(struct page_probe){0,UINT32_MAX,0x881000000ULL};
    assert(!tegu_compositor_iosurface_pages_validate(&surface,16384,&checked,
        resolve_gb34,&p)); /* outside qualified GB34 */
    p=(struct page_probe){0,UINT32_MAX,0x8c0000000ULL};
    assert(!tegu_compositor_iosurface_pages_validate(&surface,16384,&checked,
        resolve_gb34,&p)); /* at/above the exclusive physical ceiling */
    assert(!tegu_compositor_iosurface_pages_validate(&surface,16383,&checked,
        resolve_gb34,&p));
    puts("PASS ordinary IOSurface candidate gate and all-page GB34/layout policy");
    return 0;
}
