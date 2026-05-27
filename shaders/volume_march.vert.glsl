#version 450

// Volume ray-march vertex shader — fullscreen triangle (no vertex buffer).
// Matches deferred_lighting.vert: CCW triangle from gl_VertexIndex 0,1,2.

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
