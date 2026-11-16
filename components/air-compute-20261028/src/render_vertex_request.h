#ifndef TEGU_RENDER_VERTEX_REQUEST_H
#define TEGU_RENDER_VERTEX_REQUEST_H
#include "vertex_fetch.h"
/* Selector11 keeps selector10's four scalar args and adds this immutable
 * little-endian structure. Entries are indexed by Metal buffer slot. No GPU
 * addresses or user pointers. Unused entries and reserved words must be zero. */
struct tegu_render_vertex_binding {uint64_t handle,offset,length;};
struct tegu_render_vertex_request {
 uint32_t version,buffer_mask,reserved[2];
 struct tegu_vertex_draw draw;
 struct tegu_render_vertex_binding buffers[TEGU_VERTEX_SLOTS];
};
#define TEGU_RENDER_VERTEX_REQUEST_BYTES 776u
/* Selector12: fixed one-texture contract, in addition to selector11's vertex
 * request. Texture is one owned 1024-byte RGBA8 image at a page-aligned buffer
 * offset. The kernel constructs the normalized nearest/clamp sampler itself;
 * no sampler handles, CPU pointers or GPU addresses cross this boundary. */
struct tegu_render_texture_request {
 struct tegu_render_vertex_request vertex;
 uint32_t version,reserved[3];
 struct tegu_render_vertex_binding texture;
};
#define TEGU_RENDER_TEXTURE_REQUEST_BYTES 816u
#endif
