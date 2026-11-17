#version 450
/* Raw-target LOAD / clear preload: the target's texels read through their
 * unsigned-integer twin view and stored back through the twin's blend
 * conversion, a bit copy for integer, snorm, 16-bit unorm and float32
 * targets. Clamped texel coordinates serve a same-size target texture
 * (LOAD) and the one-texel clear colour (n2mb_clear) alike. */
layout(set=0,binding=0) uniform usampler2D sourceImage;
layout(location=0) out uvec4 color;
void main(){ivec2 last=textureSize(sourceImage,0)-ivec2(1);color=texelFetch(sourceImage,min(ivec2(gl_FragCoord.xy),last),0);}
