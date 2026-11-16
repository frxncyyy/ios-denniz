#ifndef TEGU_ATTACHMENT_CLEAR_REQUEST_H
#define TEGU_ATTACHMENT_CLEAR_REQUEST_H
#include "render_vertex_request.h"

/* Additive wire contract for the one qualified TAC1 pipeline class.  It keeps
 * both logical color targets and the two reflected vertex resources explicit;
 * no field is a userspace pointer or GPU virtual address. */
#define TEGU_GPU_CAP_ATTACHMENT_CLEAR (1ULL<<17)
#define TEGU_ATTACHMENT_CLEAR_SELECTOR 18u
#define TEGU_ATTACHMENT_CLEAR_REQUEST_VERSION 1u
#define TEGU_ATTACHMENT_CLEAR_REQUEST_BYTES 224u
#define TEGU_ATTACHMENT_CLEAR_TARGETS 2u
#define TEGU_ATTACHMENT_CLEAR_RT0_MASK 0u
#define TEGU_ATTACHMENT_CLEAR_RT1_MASK 15u
#define TEGU_ATTACHMENT_CLEAR_TARGET_MASK 2u
#define TEGU_ATTACHMENT_CLEAR_RECT_SLOT 1u
#define TEGU_ATTACHMENT_CLEAR_MATRIX_SLOT 2u
#define TEGU_ATTACHMENT_CLEAR_RECT_BYTES 16u
#define TEGU_ATTACHMENT_CLEAR_MATRIX_BYTES 320u

struct tegu_attachment_clear_target_request {
 /* For color targets length is the page-rounded mapping extent starting at a
  * page-aligned allocation offset.  row_stride*height remains the active span. */
 struct tegu_render_vertex_binding memory;
 uint32_t format,row_stride,load_action,store_action;
 uint32_t write_mask,clear_rgba8,reserved[2];
};

struct tegu_attachment_clear_request {
 uint32_t version,width,height,sample_count;
 uint32_t attachment_count,target_mask,draw_write_mask,draw_read_mask;
 struct tegu_attachment_clear_target_request targets[TEGU_ATTACHMENT_CLEAR_TARGETS];
 struct tegu_render_vertex_binding rect,matrix;
 uint32_t primitive_type,vertex_start,vertex_count,instance_count;
 uint32_t base_instance,indexed,reserved[2];
};

#if defined(__cplusplus)
static_assert(sizeof(struct tegu_attachment_clear_target_request)==56,"attachment clear target ABI");
static_assert(sizeof(struct tegu_attachment_clear_request)==TEGU_ATTACHMENT_CLEAR_REQUEST_BYTES,"attachment clear request ABI");
#else
_Static_assert(sizeof(struct tegu_attachment_clear_target_request)==56,"attachment clear target ABI");
_Static_assert(sizeof(struct tegu_attachment_clear_request)==TEGU_ATTACHMENT_CLEAR_REQUEST_BYTES,"attachment clear request ABI");
#endif
#endif
