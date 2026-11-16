#ifndef TEGU_ATTACHMENT_CLEAR_RT1_STATE_H
#define TEGU_ATTACHMENT_CLEAR_RT1_STATE_H

#include <stddef.h>
#include <stdint.h>
#include "native_package_ac1.h"

/* Parameterized sibling of the immutable, phone-qualified R8 builder.  The
 * shader and resource ABI stays TAC1: RT0 is disabled, RT1 is the sole BGRA8
 * target, and table1 contains rect16 followed by matrix320. */
#define TEGU_AC1_RT1_LAYOUT_VERSION 1u
#define TEGU_AC1_RT1_SCRATCH_VA UINT64_C(0x06000000)
#define TEGU_AC1_RT1_SCRATCH_BYTES 0x70000u
#define TEGU_AC1_RT1_RESOURCE_BYTES 0x1000u
#define TEGU_AC1_RT1_HEAP_BYTES 0x200000u
#define TEGU_AC1_RT1_HIERARCHY_MASK 0x82u
#define TEGU_AC1_RT1_RECT_BYTES 16u
#define TEGU_AC1_RT1_MATRIX_BYTES 320u
#define TEGU_AC1_RT1_COMMAND_COUNT 59u
#define TEGU_AC1_RT1_MAX_DIMENSION 4096u
#define TEGU_AC1_RT1_MAX_TARGET_BYTES (16u*1024u*1024u)

struct tegu_ac1_rt1_layout {
 uint32_t version,width,height,row_stride;
 uint64_t target_length;
};

/* These aliases follow fullscreen_mapping's ordered leaves exactly:
 * 112 scratch pages, target pages, one resource page, then the 2 MiB heap. */
struct tegu_ac1_rt1_map {
 uint64_t target_va,resource_va,heap_va,target_bytes;
 uint32_t target_pages,resource_pages,heap_pages;
};

struct tegu_ac1_rt1_commands {
 uint64_t words[TEGU_AC1_RT1_COMMAND_COUNT];
 uint32_t count;
};

int tegu_ac1_rt1_map_build(const struct tegu_ac1_rt1_layout *,
 struct tegu_ac1_rt1_map *);

/* clear_rgba8 is copied to all four RT1 clear words. Caller maps the returned
 * aliases as described by tegu_ac1_rt1_map, zeroes the complete heap before
 * submission, and retains all backing through completion. Outputs are
 * unchanged on refusal. */
int tegu_ac1_rt1_state_build(const struct tegu_ac1_info *,
 const void *package,size_t package_bytes,
 const struct tegu_ac1_rt1_layout *,uint32_t clear_rgba8,
 const void *rect,size_t rect_bytes,const void *matrix,size_t matrix_bytes,
 void *scratch,size_t scratch_bytes,void *resources,size_t resource_bytes,
 struct tegu_ac1_rt1_map *,struct tegu_ac1_rt1_commands *);

#endif
