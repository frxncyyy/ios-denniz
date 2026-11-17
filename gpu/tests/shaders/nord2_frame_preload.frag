#version 450
/* Pre-frame colour preload: a frame shader has no vertex stage, so the target
 * is addressed by pixel position (Mesa pan_fb_nir.c load_pixel_coord + txf). */
layout(set=0,binding=0) uniform sampler2D sourceImage;
layout(location=0) out vec4 color;
void main(){color=texelFetch(sourceImage,ivec2(gl_FragCoord.xy),0);}
