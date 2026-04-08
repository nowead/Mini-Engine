#version 450

// Deferred Lighting vertex shader — fullscreen triangle (no vertex buffer)

// Generate a CCW fullscreen triangle from gl_VertexIndex 0,1,2
void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
