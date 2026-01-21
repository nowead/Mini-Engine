#version 450

// Per-vertex attributes (binding 0)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Per-instance attributes (binding 1)
layout(location = 3) in vec3 instancePosition;   // offset 0, 12 bytes
layout(location = 4) in vec3 instanceColor;      // offset 12, 12 bytes
layout(location = 5) in vec3 instanceScale;      // offset 24, 12 bytes (X/Z base, Y height)
// padding at offset 36, 4 bytes - total 40 bytes

// Uniform buffer (camera matrices + lighting)
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 sunDirection;    // Normalized direction TO the sun
    float sunIntensity;   // Sun light intensity
    vec3 sunColor;        // Sun light color
    float ambientIntensity; // Ambient light intensity
    vec3 cameraPos;       // Camera position for specular
    float _padding;
} ubo;

// Outputs to fragment shader
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPos;

void main() {
    // Apply instance transform with independent scale per axis
    // This allows small base (X/Z) but tall height (Y)
    vec3 worldPos = inPosition * instanceScale + instancePosition;

    // Transform to clip space
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(worldPos, 1.0);

    // Pass color, normal, and world position to fragment shader
    fragColor = instanceColor;
    fragNormal = inNormal;
    fragWorldPos = worldPos;
}
