/* Shared GB34 page/charge policy for GPU-eligible IOSurface backing.
 * Host-testable; callers still obtain physical segments from IOMemoryDescriptor.
 */
#ifndef TEGU_SURFACE_GPU_PAGE_H
#define TEGU_SURFACE_GPU_PAGE_H
#include <stdint.h>
#define TEGU_SURFACE_GPU_PAGE_BYTES 0x1000ULL
#define TEGU_SURFACE_GPU_PA_MIN 0x880000000ULL
#define TEGU_SURFACE_GPU_PA_MAX 0x8bffff000ULL
#define TEGU_SURFACE_GPU_PHYSICAL_MASK 0x8bfffc000ULL
static inline int tegu_surface_gpu_page_valid(uint64_t offset,uint64_t length,
    uint64_t physical,uint64_t segment,uint64_t ram_base,uint64_t ram_size)
{
    const uint64_t page=TEGU_SURFACE_GPU_PAGE_BYTES;
    if ((offset&(page-1)) || offset>length || length-offset<page ||
        (physical&(page-1)) || segment<page || physical<TEGU_SURFACE_GPU_PA_MIN ||
        physical>TEGU_SURFACE_GPU_PA_MAX || physical<ram_base || ram_size<page ||
        physical-ram_base>ram_size-page) return 0;
    return 1;
}
static inline int tegu_surface_gpu_charge_fits(uint64_t current,uint64_t bytes,uint64_t maximum)
{
    return bytes!=0 && current<=maximum && bytes<=maximum-current;
}
static inline int tegu_surface_gpu_size_valid(uint64_t bytes,uint64_t maximum)
{
    return bytes!=0 && bytes<=maximum && !(bytes&(TEGU_SURFACE_GPU_PAGE_BYTES-1));
}
#endif
