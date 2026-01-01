#version 450

// Per-vertex attributes (binding 0)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Per-instance attributes (binding 1)
layout(location = 3) in vec3 instancePosition;
layout(location = 4) in vec3 instanceColor;
layout(location = 5) in float instanceScale;

// Uniform buffer (camera matrices)
layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
} camera;

// Outputs to fragment shader
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;

void main() {
    // Apply instance transform
    vec3 worldPos = inPosition * instanceScale + instancePosition;

    // Transform to clip space
    gl_Position = camera.proj * camera.view * vec4(worldPos, 1.0);

    // Pass color and normal to fragment shader
    fragColor = instanceColor;
    fragNormal = inNormal;
}
