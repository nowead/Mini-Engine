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

    // Set depth to 1.0 (far plane) for proper depth testing
    gl_Position = vec4(pos, 1.0, 1.0);

    // Calculate world-space ray direction from clip-space position
    // Use z=1.0 to sample the far plane
    vec4 worldPos = ubo.invViewProj * vec4(pos, 1.0, 1.0);
    fragRayDir = normalize(worldPos.xyz / worldPos.w);
}
