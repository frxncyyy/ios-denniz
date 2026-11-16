#ifndef TEGU_RENDER_TARGET_REQUEST_H
#define TEGU_RENDER_TARGET_REQUEST_H
#include "render_vertex_request.h"
#include "render_state.h"

/* Selector 15 carries a versioned surface layout and a typed resource payload.
 * The kernel derives all GPU addresses from owned handles and validated byte
 * offsets. No user virtual/physical/GPU addresses cross this ABI. */
enum tegu_render_target_request_kind {
 TEGU_RENDER_TARGET_REQUEST_NONE=0,
 TEGU_RENDER_TARGET_REQUEST_VERTEX=1,
 TEGU_RENDER_TARGET_REQUEST_TEXTURE=2
};
struct tegu_render_target_request {
 uint32_t version,width,height,row_stride,kind,reserved[3];
 unsigned char payload[TEGU_RENDER_TEXTURE_REQUEST_BYTES];
};
#define TEGU_RENDER_TARGET_REQUEST_VERSION 1u
#define TEGU_RENDER_TARGET_REQUEST_BYTES 848u

#endif
