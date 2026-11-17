#version 450
layout(location=0) in vec4 pos;
layout(location=2) out vec3 color;
layout(location=7) out vec2 uv;
void main(){gl_Position=pos;gl_PointSize=1.0;color=vec3(0.3,0.5,0.8)*pos.w;uv=pos.xy;}
