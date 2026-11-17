#version 450
/* Apple XR IOSurface texels -> owned RGBA16F decode shadow, one full-target
 * draw per new or changed surface (compositor_submit.inc tegu_shadow_gpu_*;
 * A3). Exactly tegu_xr_decode_row: the generated half tables are looked up,
 * never recomputed, so the RGBA16F store is bit-identical to the CPU decode.
 *   binding 0: the surface rows as R32Uint (BGR10_XR: b | g<<10 | r<<20) or
 *              RGBA16Uint (BGRA10_XR: B, G, R, A codes in the high 10 bits)
 *   binding 1: R16Uint 1024x2, row 0 the colour table (tegu_xr_half_linear or
 *              tegu_xr_half_srgb), row 1 tegu_xr_half_alpha */
layout(set=0,binding=0) uniform usampler2D source;
layout(set=0,binding=1) uniform usampler2D tables;
layout(push_constant) uniform Params {
 ivec4 mode; /* x: 0 BGR10_XR (4 bytes), 1 BGRA10_XR (8 bytes) */
} p;
layout(location=0) out vec4 color;

float half_at(uint code,int row){
 return unpackHalf2x16(texelFetch(tables,ivec2(int(code),row),0).r).x;
}
void main(){
 ivec2 at=ivec2(gl_FragCoord.xy);
 uvec4 t=texelFetch(source,at,0);
 uint b,g,r;float a=1.0;
 if(p.mode.x==0){b=t.r&1023u;g=(t.r>>10)&1023u;r=(t.r>>20)&1023u;}
 else{b=t.r>>6;g=t.g>>6;r=t.b>>6;a=half_at(t.a>>6,1);}
 color=vec4(half_at(r,0),half_at(g,0),half_at(b,0),a);
}
