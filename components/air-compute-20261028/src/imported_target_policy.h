/* Host-testable bounds and format policy shared with the IOSurface import path. */
#ifndef TEGU_GPU_IMPORTED_TARGET_POLICY_H
#define TEGU_GPU_IMPORTED_TARGET_POLICY_H
#include <stdint.h>
#define TEGU_IOSURFACE_PIXELFORMAT_RGBA 0x52474241u
#define TEGU_IOSURFACE_PIXELFORMAT_BGRA 0x42475241u
/* IOSurfacePixelFormat is a FOURCC, not an MTLPixelFormat enum. */
static inline int tegu_gpu_surface_format_to_metal(uint32_t surfaceFormat,uint32_t *metalFormat)
{
    if(!metalFormat)return 0;
    if(surfaceFormat==TEGU_IOSURFACE_PIXELFORMAT_RGBA)*metalFormat=70;
    else if(surfaceFormat==TEGU_IOSURFACE_PIXELFORMAT_BGRA)*metalFormat=80;
    else return 0;
    return 1;
}
static inline int tegu_gpu_import_layout_valid(uint32_t width,uint32_t height,
    uint32_t row,uint32_t offset,uint32_t format,uint32_t alloc,uint32_t length,
    uint32_t planes,uint16_t bpe,uint8_t ew,uint8_t eh,
    uint32_t pwidth,uint32_t pheight,uint32_t prow,uint32_t poffset,
    uint32_t psize,uint16_t pbpe,uint8_t pew,uint8_t peh)
{
    if (width!=16 || height!=16 || row!=64 || offset!=0 ||
        (format!=TEGU_IOSURFACE_PIXELFORMAT_RGBA && format!=TEGU_IOSURFACE_PIXELFORMAT_BGRA) || bpe!=4 || ew!=1 || eh!=1 ||
        planes>1 || alloc!=length || length<4096 || (length&4095) ||
        length>(256u<<20)) return 0;
    if (planes==1 && (pwidth!=16 || pheight!=16 || prow!=64 || poffset!=0 ||
        psize!=1024 || pbpe!=4 || pew!=1 || peh!=1)) return 0;
    return 1;
}
static inline int tegu_gpu_target_range_valid(uint64_t length,uint64_t offset)
{
    return !(offset&63) && offset<=length && length-offset>=1024 &&
        (offset&4095)<=3072;
}
static inline int tegu_gpu_target_format_matches(uint32_t pipeline,uint32_t target)
{
    return (pipeline==70 || pipeline==80) && pipeline==target;
}
static inline int tegu_gpu_import_handle_valid(uint32_t handle)
{
    return handle>=0xa0000001u && handle<=0xafffffffu;
}
#endif
