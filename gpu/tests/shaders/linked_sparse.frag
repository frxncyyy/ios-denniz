#version 450
layout(location=2) in vec3 color;
layout(location=7) in vec2 uv;
layout(location=0) out vec4 outColor;
void main(){outColor=vec4(color*uv.x,1.0);}
