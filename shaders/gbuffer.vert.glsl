#version 450

// G-Buffer geometry pass — Phase 3 Deferred Rendering
// Outputs geometry data to 3 MRT targets; no lighting here.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;  // consumed by normal-map fragment path in a later sub-task

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 invView;
    mat4 invProj;
    vec3 sunDirection;
    float sunIntensity;
    vec3 sunColor;
    float ambientIntensity;
    vec3 cameraPos;
    float exposure;
    mat4 lightSpaceMatrices[4];
    vec4 cascadeSplits;
    vec2 shadowMapSize;
    float shadowBias;
    float shadowStrength;
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

layout(std430, set = 1, binding = 1) readonly buffer VisibleIndicesBuffer {
    uint indices[];
} visibleIndices;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out float fragMetallic;
layout(location = 4) out float fragRoughness;
layout(location = 5) out float fragAO;
layout(location = 6) out vec3 fragAlbedo;
layout(location = 7) out flat uint fragAlbedoIndex;     // baseColor bindless slot (roughnessAOPad.b)
// Showcase PBR (glTF) bindless slots. Buildings leave these at 0xFFFFFFFF so
// the fragment skips the extra samples and behaves exactly as before.
layout(location = 8)  out vec3      fragTangent;        // world-space, unnormalized (0 = no tangent)
layout(location = 9)  out flat uint fragNormalIndex;    // textureIndices.x
layout(location = 10) out flat uint fragMRIndex;        // textureIndices.y (G=roughness, B=metallic)
layout(location = 11) out flat uint fragEmissiveIndex;  // textureIndices.z
layout(location = 12) out flat uint fragAOIndex;        // textureIndices.w

void main() {
    uint actualIndex = visibleIndices.indices[gl_InstanceIndex];
    ObjectData obj   = objectBuffer.objects[actualIndex];

    vec4 worldPos4 = obj.worldMatrix * vec4(inPosition, 1.0);
    fragWorldPos   = worldPos4.xyz;

    // Transform normal to world space (assuming uniform scale)
    mat3 normalMatrix = transpose(inverse(mat3(obj.worldMatrix)));
    fragNormal    = normalize(normalMatrix * inNormal);

    fragTexCoord  = inTexCoord;
    fragAlbedo    = obj.colorAndMetallic.rgb;
    fragMetallic  = obj.colorAndMetallic.a;
    fragRoughness = obj.roughnessAOPad.r;
    fragAO        = obj.roughnessAOPad.g;

    // Phase 4: roughnessAOPad.b holds the baseColor bindless index as a
    // float-encoded uint. 0xFFFFFFFF indicates "no texture → procedural albedo".
    fragAlbedoIndex   = floatBitsToUint(obj.roughnessAOPad.b);

    // Showcase PBR bindless slots (glTF). Pass the tangent unnormalized so the
    // zero-vector sentinel (asset supplied no tangent, e.g. the procedural
    // cube) survives interpolation; the fragment checks length() before TBN.
    fragTangent       = mat3(obj.worldMatrix) * inTangent;
    fragNormalIndex   = obj.textureIndices.x;
    fragMRIndex       = obj.textureIndices.y;
    fragEmissiveIndex = obj.textureIndices.z;
    fragAOIndex       = obj.textureIndices.w;

    gl_Position = ubo.proj * ubo.view * ubo.model * worldPos4;
}
