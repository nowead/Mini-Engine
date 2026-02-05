#version 450

// Per-vertex attributes (binding 0)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Per-instance attributes (binding 1) - 48 bytes stride
layout(location = 3) in vec3 instancePosition;   // offset 0, 12 bytes
layout(location = 4) in vec3 instanceColor;      // offset 12, 12 bytes (albedo)
layout(location = 5) in vec3 instanceScale;      // offset 24, 12 bytes
layout(location = 6) in float instanceMetallic;  // offset 36, 4 bytes
layout(location = 7) in float instanceRoughness; // offset 40, 4 bytes
layout(location = 8) in float instanceAO;        // offset 44, 4 bytes

// Uniform buffer (camera matrices + lighting + shadow)
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 sunDirection;
    float sunIntensity;
    vec3 sunColor;
    float ambientIntensity;
    vec3 cameraPos;
    float exposure;
    // Shadow mapping
    mat4 lightSpaceMatrix;
    vec2 shadowMapSize;
    float shadowBias;
    float shadowStrength;
} ubo;

// Outputs to fragment shader
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec4 fragPosLightSpace;
layout(location = 4) out float fragMetallic;
layout(location = 5) out float fragRoughness;
layout(location = 6) out float fragAO;

void main() {
    vec3 worldPos = inPosition * instanceScale + instancePosition;

    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(worldPos, 1.0);

    fragColor = instanceColor;
    fragNormal = inNormal;
    fragWorldPos = worldPos;
    fragPosLightSpace = ubo.lightSpaceMatrix * vec4(worldPos, 1.0);
    fragMetallic = instanceMetallic;
    fragRoughness = instanceRoughness;
    fragAO = instanceAO;
}
