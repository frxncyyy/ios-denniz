#ifndef TEGU_COMPOSITOR_REQUEST_H
#define TEGU_COMPOSITOR_REQUEST_H
#include "render_vertex_request.h"
/* Additive compositor draw wire ABI. Every address is an owned connection
 * handle plus checked byte offset/available length; no pointers or GPU VAs.
 * UBO order follows the resource class in the imported pipeline, not caller
 * descriptor indices. Version1 supports a single bound color target, clear/
 * store, one nonindexed triangle, and a readonly16x16RGBA8 source. Requested
 * PSO attachment1 (mask0) remains part of the pipeline metadata; a pass that
 * binds attachment1 is refused rather than discarding its load/store actions. */
#define TEGU_COMPOSITOR_REQUEST_VERSION 1u
#define TEGU_COMPOSITOR_REQUEST_VERSION_LOAD 2u
#define TEGU_COMPOSITOR_REQUEST_BYTES 960u
#define TEGU_GPU_CAP_COMPOSITOR (1ULL<<14)
#define TEGU_GPU_CAP_COMPOSITOR_LOAD (1ULL<<13)
#define TEGU_GPU_CAP_COMPOSITOR_IOSURFACE (1ULL<<15)
#define TEGU_GPU_CAP_COMPOSITOR_V7 (1ULL<<16)
#define TEGU_COMPOSITOR_SAMPLER_NEAREST_CLAMP_LOD0 1u
#define TEGU_COMPOSITOR_LOAD_CLEAR 2u
#define TEGU_COMPOSITOR_LOAD_LOAD 1u
#define TEGU_COMPOSITOR_STORE_STORE 1u
struct tegu_compositor_request {
 uint32_t version,width,height,row_stride;
 uint32_t load_action,store_action,attachment_mask,reserved;
 struct tegu_render_vertex_request vertex;
 struct tegu_render_vertex_binding ubos[4],texture;
 uint32_t texture_format,texture_width,texture_height,texture_stride;
 /* This ABI describes mip level 0 only. texture length is exactly
  * texture_stride*texture_height; higher Metal mip backing stays unmapped. */
 uint32_t sampler_class,texture_reserved[3];
};
#endif
