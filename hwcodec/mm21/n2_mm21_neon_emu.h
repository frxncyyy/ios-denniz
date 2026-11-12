/* SPDX-License-Identifier: MIT */
/* n2_mm21_neon_emu.h -- TEST ONLY: plain-C stand-ins for the NEON intrinsics n2_mm21.c uses, so that its NEON code path
 * (indexing, 4-tile groups, tails) is exercised on an x86 host.  Never used in a phone build. */
#ifndef N2_MM21_NEON_EMU_H
#define N2_MM21_NEON_EMU_H
#include <stdint.h>
#include <string.h>
typedef struct { uint8_t b[16]; } uint8x16_t;
static inline uint8x16_t vld1q_u8(const uint8_t *p) { uint8x16_t v; memcpy(v.b, p, 16); return v; }
static inline void vst1q_u8(uint8_t *p, uint8x16_t v) { memcpy(p, v.b, 16); }
#endif
