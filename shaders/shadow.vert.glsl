#version 450

// Shadow pass vertex shader - renders scene from light's perspective
// Phase 2.1: Uses SSBO for per-object data (replaces instance vertex attributes)

// Per-vertex attributes (binding 0)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;      // Unused in shadow pass
layout(location = 2) in vec2 inTexCoord;    // Unused in shadow pass

// Light space matrix uniform
layout(set = 0, binding = 0) uniform LightSpaceUBO {
    mat4 lightSpaceMatrix;
} ubo;

// Phase 2.1: Per-object data via SSBO
struct ObjectData {
    mat4 worldMatrix;
    vec4 boundingBoxMin;
    vec4 boundingBoxMax;
    vec4 colorAndMetallic;
    vec4 roughnessAOPad;
};

layout(std430, set = 1, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} objectBuffer;

void main() {
    ObjectData obj = objectBuffer.objects[gl_InstanceIndex];

    vec4 worldPos = obj.worldMatrix * vec4(inPosition, 1.0);

    // Transform to light clip space
    gl_Position = ubo.lightSpaceMatrix * worldPos;
}
