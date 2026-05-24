#version 450

// Shadow pass vertex shader - renders scene from light's perspective
// Phase 2.1: Uses SSBO for per-object data (replaces instance vertex attributes)

// Per-vertex attributes (binding 0)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;      // Unused in shadow pass
layout(location = 2) in vec2 inTexCoord;    // Unused in shadow pass
layout(location = 3) in vec3 inTangent;     // Unused in shadow pass; declared to match shared vertex layout

// Light space matrix uniform
layout(set = 0, binding = 0) uniform LightSpaceUBO {
    mat4 lightSpaceMatrix;
} ubo;

// MUST match C++ ObjectData (InstancedRenderData.hpp) — 144 bytes.
struct ObjectData {
    mat4  worldMatrix;
    vec4  boundingBoxMin;
    vec4  boundingBoxMax;
    vec4  colorAndMetallic;
    vec4  roughnessAOPad;
    uvec4 textureIndices;
};

layout(std430, set = 1, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} objectBuffer;

void main() {
    ObjectData obj = objectBuffer.objects[gl_InstanceIndex];

    // The ground is an enormous flat quad (AABB span ~100000); it must NOT cast
    // shadows. Rendering it into the shadow map self-shadows the whole receiver
    // as acne over the map's footprint, which flickers as the camera moves (the
    // receiver's reconstructed depth wobbles against the stored ground depth).
    // Detect it by its huge horizontal extent and emit a clipped vertex
    // (NDC z = -2 < 0 -> discarded) so it writes nothing. Mirrors shadow.wgsl;
    // the GLSL copy had been missing this cull (Vulkan-only artifact).
    vec3 ext = obj.boundingBoxMax.xyz - obj.boundingBoxMin.xyz;
    if (max(ext.x, ext.z) > 10000.0) {
        gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
        return;
    }

    vec4 worldPos = obj.worldMatrix * vec4(inPosition, 1.0);

    // Transform to light clip space
    gl_Position = ubo.lightSpaceMatrix * worldPos;
}
