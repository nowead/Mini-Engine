#version 450

// Shadow pass vertex shader - renders scene from light's perspective
// Uses same vertex layout as building shader for compatibility

// Per-vertex attributes (binding 0)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;      // Unused in shadow pass
layout(location = 2) in vec2 inTexCoord;    // Unused in shadow pass

// Per-instance attributes (binding 1)
layout(location = 3) in vec3 instancePosition;
layout(location = 4) in vec3 instanceColor;  // Unused in shadow pass
layout(location = 5) in vec3 instanceScale;

// Light space matrix uniform
layout(binding = 0) uniform LightSpaceUBO {
    mat4 lightSpaceMatrix;  // Light view-projection matrix
} ubo;

void main() {
    // Apply instance transform (same as building shader)
    vec3 worldPos = inPosition * instanceScale + instancePosition;

    // Transform to light clip space
    gl_Position = ubo.lightSpaceMatrix * vec4(worldPos, 1.0);
}
