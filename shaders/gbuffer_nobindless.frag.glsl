#version 450

// G-Buffer fragment shader — non-bindless fallback
// Used on devices that don't support VK_EXT_descriptor_indexing (e.g. lavapipe).
// Always uses procedural sRGB color from SSBO.  The fragAlbedoIndex input is
// accepted but ignored so the vertex shader interface is identical.

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in float fragMetallic;
layout(location = 4) in float fragRoughness;
layout(location = 5) in float fragAO;
layout(location = 6) in vec3 fragAlbedo;
layout(location = 7) in flat uint fragAlbedoIndex;  // ignored in this variant

layout(location = 0) out vec4 gBuffer0;
layout(location = 1) out vec4 gBuffer1;
layout(location = 2) out vec4 gBuffer2;

void main() {
    vec3 albedoLinear = pow(fragAlbedo, vec3(2.2));

    gBuffer0 = vec4(normalize(fragNormal), fragRoughness);
    gBuffer1 = vec4(albedoLinear, fragMetallic);
    // .gba carries emissive (deferred lighting adds gb2.gba to the final color);
    // this fallback path has no emissive, so keep it zero — a non-zero alpha
    // here would tint the whole scene.
    gBuffer2 = vec4(fragAO, 0.0, 0.0, 0.0);
}
