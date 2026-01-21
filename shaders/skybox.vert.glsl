#version 450

// Fullscreen triangle - no vertex input needed
// Generates positions: (-1,-1), (3,-1), (-1,3)

layout(binding = 0) uniform UniformBufferObject {
    mat4 invViewProj;  // Inverse view-projection matrix
    vec3 sunDirection; // Normalized sun direction
    float time;        // For animation (optional)
} ubo;

layout(location = 0) out vec3 fragRayDir;

void main() {
    // Generate fullscreen triangle vertices
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0.9999, 1.0);  // Near far plane

    // Calculate world-space ray direction from clip-space position
    vec4 worldPos = ubo.invViewProj * vec4(pos, 1.0, 1.0);
    fragRayDir = worldPos.xyz / worldPos.w;
}
