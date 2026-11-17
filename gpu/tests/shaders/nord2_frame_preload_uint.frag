#version 450
/* Pre-frame raw-target preload (no vertex stage): the target's texels by
 * pixel position through the unsigned-integer twin view, a bit copy. */
layout(set=0,binding=0) uniform usampler2D sourceImage;
layout(location=0) out uvec4 color;
void main(){color=texelFetch(sourceImage,ivec2(gl_FragCoord.xy),0);}
