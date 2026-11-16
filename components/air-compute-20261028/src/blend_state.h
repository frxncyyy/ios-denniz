/* Bounded fixed-function RGBA8 blend state. No GPU addresses or submission.
 * Metal enum numbers match the translator's metal/generated.rs. This is one
 * full-mask attachment, ADD/SUB/REVSUB with factors Zero..InvDestinationAlpha
 * and the blend-colour factors (11..14, one homogeneous constant, unorm
 * targets); min/max, SourceAlphaSaturated, dual-source and blend shaders are
 * refused. */
#ifndef TEGU_BLEND_STATE_H
#define TEGU_BLEND_STATE_H
#include <stdint.h>
struct tegu_blend_inputs {
 uint32_t version,enabled,rgb_op,rgb_src,rgb_dst,alpha_op,alpha_src,alpha_dst,write_mask;
};
struct tegu_blend_state {
 uint32_t words[4],reads_destination,opaque;
};
/* Output unchanged on refusal. Disabled/opaque-equivalent state produces the
 * qualified replacement descriptor. Caller must also copy words2/3 into the
 * fragment FAU blend system value and disable forward pixel killing when the
 * destination is read. Do not use this RGBA8 conversion for other formats. */
int tegu_blend_build(const struct tegu_blend_inputs *,struct tegu_blend_state *);
/* RGBA8Unorm and BGRA8Unorm share logical RGBA tile/blend channels. Format
 * differences belong to render-target writeback, not blend math. */
int tegu_blend_build_format(uint32_t metal_format,const struct tegu_blend_inputs *,struct tegu_blend_state *);
/* Blend-colour components the equation reads (R1 G2 B4 A8); 0 = none. */
unsigned tegu_blend_constant_mask(const struct tegu_blend_inputs *);
/* UNORM bits of the fixed-function constant for a target format; 0 = no
 * fixed-function constant (float/raw targets). */
unsigned tegu_blend_constant_bits(uint32_t metal_format);
/* Pack Metal's blend colour into BLEND word0 bits 16..31 (blend_constant).
 * 0 also when the equation reads no constant (word0 unchanged); -2 when
 * the read components differ (one scalar constant: needs a blend shader). */
int tegu_blend_constant_word(uint32_t metal_format,const struct tegu_blend_inputs *,const float rgba[4],uint32_t *word0);
#endif
