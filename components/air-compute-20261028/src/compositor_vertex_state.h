#ifndef TEGU_COMPOSITOR_VERTEX_STATE_H
#define TEGU_COMPOSITOR_VERTEX_STATE_H
#include "vertex_fetch.h"
#define TEGU_COMPOSITOR_DRIVER_BYTES (18u*32u)
/* The resource-aware compiler's table0 has16 attribute descriptors, its dummy
 * sampler at16 and one vertex buffer at17. The existing vertex builder uses
 *31attributes; preserve that legacy ABI and repack only this separate class.
 * `workspace` is caller-owned heap/scratch storage, not a kernel-stack object. */
struct tegu_compositor_vertex_state {
 struct tegu_vertex_state workspace;
 unsigned char driver[TEGU_COMPOSITOR_DRIVER_BYTES];
};
int tegu_compositor_vertex_build(const struct tegu_vertex_attribute *,size_t,
 const struct tegu_vertex_buffer *,const struct tegu_vertex_draw *,uint64_t,
 struct tegu_compositor_vertex_state *);
#endif
