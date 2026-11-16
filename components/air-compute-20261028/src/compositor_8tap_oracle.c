#include "compositor_8tap_oracle.h"


static uint8_t normalized_to_unorm8(float value)
{
    if (value <= 0.0f) return 0;
    if (value >= 1.0f) return 255;
    float scaled=value*255.0f;
    unsigned rounded=(unsigned)(scaled+0.5f);
    return (uint8_t)(rounded>255u?255u:rounded);
}

static _Float16 half_fma(_Float16 a,_Float16 b,_Float16 c)
{
    /* SPIR-V uses Fma on v4half. Compute the exact fused result in float,
     * then round once to binary16 (round-to-nearest, ties-to-even). */
    return (_Float16)__builtin_fmaf((float)a,(float)b,(float)c);
}

int tegu_8tap_expected_constant_opaque(const uint8_t source[4],
    const uint8_t clear[4], uint8_t out[TEGU_8TAP_TARGET_PIXELS*4])
{
    if (!source || !clear || !out || source[3]!=255u) return -1;
    _Float16 accumulated[4]={0,0,0,0};
    const _Float16 weight=(_Float16)0.0625f;
    for (unsigned sample=0;sample<TEGU_8TAP_SAMPLE_COUNT;sample++) {
        for (unsigned c=0;c<4;c++) {
            _Float16 sampled=(_Float16)((float)source[c]/255.0f);
            /* The 16 coordinates may differ, but the authored texture is
             * constant so each sample is identical. Round each half-FMA as
             * the shader's v4half accumulator does. */
            accumulated[c]=half_fma(sampled,weight,accumulated[c]);
        }
    }
    float source_alpha=(float)accumulated[3];
    for (unsigned pixel=0;pixel<TEGU_8TAP_TARGET_PIXELS;pixel++) {
        float rgba[4];
        for (unsigned c=0;c<3;c++) {
            _Float16 opacity=(_Float16)1.0f; /* FS6 half is exactly 1.0. */
            _Float16 premultiplied=half_fma(accumulated[c],opacity,(_Float16)0.0f);
            float destination=(float)clear[c]/255.0f;
            rgba[c]=(float)premultiplied+destination*(1.0f-source_alpha);
        }
        rgba[3]=source_alpha+((float)clear[3]/255.0f)*(1.0f-source_alpha);
        out[pixel*4+0]=normalized_to_unorm8(rgba[2]);
        out[pixel*4+1]=normalized_to_unorm8(rgba[1]);
        out[pixel*4+2]=normalized_to_unorm8(rgba[0]);
        out[pixel*4+3]=normalized_to_unorm8(rgba[3]);
    }
    return 0;
}
