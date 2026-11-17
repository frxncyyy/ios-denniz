#version 450
layout(location = 0) out vec4 vcolor;
void main() {
  vec2 p = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0,
                (gl_VertexIndex == 2) ? 3.0 : -1.0);
  gl_Position = vec4(p, 0.0, 1.0);
  vcolor = vec4(0.2, 0.6, 1.0, 1.0);
}
