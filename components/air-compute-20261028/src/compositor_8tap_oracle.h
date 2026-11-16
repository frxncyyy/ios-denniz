#ifndef TEGU_COMPOSITOR_8TAP_ORACLE_H
#define TEGU_COMPOSITOR_8TAP_ORACLE_H
#include <stdint.h>

#define TEGU_8TAP_SAMPLE_COUNT 16u
#define TEGU_8TAP_PIXEL_BYTES 4u
#define TEGU_8TAP_TARGET_PIXELS 256u

/* Independent CPU model for the constant opaque primary case. Each of the
 * shader's 16 samples returns the same RGBA8 texel and contributes 1/16; the
 * shader's FS6 opacity is 1, and One/OneMinusSourceAlpha blending therefore
 * replaces the clear pixel exactly. Output is target BGRA8 byte order. */
int tegu_8tap_expected_constant_opaque(const uint8_t source_rgba[4],
    const uint8_t clear_rgba[4], uint8_t out_bgra[TEGU_8TAP_TARGET_PIXELS*4]);
#endif
