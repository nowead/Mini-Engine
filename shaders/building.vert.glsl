#version 450

// Per-vertex attributes (binding 0)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Uniform buffer (camera matrices + lighting + shadow)
layout(set = 0, binding = 0) uniform UniformBufferObject {
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

// Phase 2.1: Per-object data via SSBO (replaces per-instance vertex attributes)
struct ObjectData {
    mat4 worldMatrix;
    vec4 boundingBoxMin;
    vec4 boundingBoxMax;
    vec4 colorAndMetallic;   // rgb = albedo, a = metallic
    vec4 roughnessAOPad;     // r = roughness, g = ao
};

layout(std430, set = 1, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} objectBuffer;

// Phase 2.2: Visible indices from GPU frustum culling
layout(std430, set = 1, binding = 1) readonly buffer VisibleIndicesBuffer {
    uint indices[];
} visibleIndices;

// Outputs to fragment shader
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec4 fragPosLightSpace;
layout(location = 4) out float fragMetallic;
layout(location = 5) out float fragRoughness;
layout(location = 6) out float fragAO;

void main() {
    // Phase 2.2: Indirection through visible indices from frustum culling
    uint actualIndex = visibleIndices.indices[gl_InstanceIndex];
    ObjectData obj = objectBuffer.objects[actualIndex];

    vec4 worldPos4 = obj.worldMatrix * vec4(inPosition, 1.0);
    vec3 worldPos = worldPos4.xyz;

    gl_Position = ubo.proj * ubo.view * ubo.model * worldPos4;

    fragColor = obj.colorAndMetallic.rgb;
    fragNormal = inNormal;
    fragWorldPos = worldPos;
    fragPosLightSpace = ubo.lightSpaceMatrix * worldPos4;
    fragMetallic = obj.colorAndMetallic.a;
    fragRoughness = obj.roughnessAOPad.r;
    fragAO = obj.roughnessAOPad.g;
}
