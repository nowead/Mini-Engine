#version 450

// Fullscreen triangle tonemap vertex shader (Vulkan)
// No vertex buffer — position derived from gl_VertexIndex.
// Draw with 3 vertices to cover the entire screen.

layout(location = 0) out vec2 fragUV;

void main() {
    // Vertex 0: NDC (-1,-1) = top-left   (Vulkan NDC: y=-1 is top)
    // Vertex 1: NDC ( 3,-1) = top-right  (extends past edge)
    // Vertex 2: NDC (-1, 3) = bottom-left (extends past edge)
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0.0, 1.0);

    // Vulkan UV: (0,0) = top-left; NDC y=-1 = top → uv = (pos + 1) / 2
    fragUV = (pos + 1.0) * 0.5;
}
