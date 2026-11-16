#ifndef TEGU_ATTACHMENT_CLEAR_R8_STATE_H
#define TEGU_ATTACHMENT_CLEAR_R8_STATE_H

#include <stddef.h>
#include <stdint.h>
#include "native_package_ac1.h"

/* Exact 16x16 state qualified by the standalone R8 phone run.  This is not a
 * general MRT builder: all addresses below are offsets in the fixed fullscreen
 * aperture and the active BGRA target is logical attachment 1. */
#define TEGU_AC1_R8_SCRATCH_BYTES 0x70000u
#define TEGU_AC1_R8_RESOURCE_BYTES 0x6000u
#define TEGU_AC1_R8_RECT_BYTES 16u
#define TEGU_AC1_R8_MATRIX_BYTES 320u
#define TEGU_AC1_R8_COMMAND_COUNT 59u

struct tegu_ac1_r8_commands {
 uint64_t words[TEGU_AC1_R8_COMMAND_COUNT];
 uint32_t count;
};

/* Build only the device-qualified clear/store case: 16x16, row 64, RT0
 * disabled, RT1 active, clear 0x11223344, offsets zero.  The caller maps
 * scratch at aperture+0, RT1 at +0x70000, and resources at +0x71000. */
int tegu_ac1_r8_state_build(const struct tegu_ac1_info *,
 const void *package,size_t package_bytes,
 const void *rect,size_t rect_bytes,const void *matrix,size_t matrix_bytes,
 void *scratch,size_t scratch_bytes,void *resources,size_t resource_bytes,
 struct tegu_ac1_r8_commands *commands);

#endif
