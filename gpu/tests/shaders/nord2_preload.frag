#version 450
layout(location=0) in vec2 uv;
layout(set=0,binding=0) uniform sampler2D sourceImage;
layout(location=0) out vec4 color;
void main(){color=textureLod(sourceImage,uv,0.0);}
