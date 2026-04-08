#version 450

// G-Buffer fragment shader — Phase 3 Deferred Rendering
// Packs material properties into 3 MRT targets:
//   GBuffer0 (RGBA16Float): world-space Normal (xyz) + Roughness
//   GBuffer1 (RGBA8Unorm):  Albedo (rgb, linear) + Metallic
//   GBuffer2 (RGBA8Unorm):  AO (r), padding (gba)

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in float fragMetallic;
layout(location = 4) in float fragRoughness;
layout(location = 5) in float fragAO;
layout(location = 6) in vec3 fragAlbedo;

layout(location = 0) out vec4 gBuffer0;  // normal.xyz + roughness
layout(location = 1) out vec4 gBuffer1;  // albedo.rgb (linear) + metallic
layout(location = 2) out vec4 gBuffer2;  // ao + padding

void main() {
    // sRGB albedo → linear
    vec3 albedoLinear = pow(fragAlbedo, vec3(2.2));

    gBuffer0 = vec4(normalize(fragNormal), fragRoughness);
    gBuffer1 = vec4(albedoLinear, fragMetallic);
    gBuffer2 = vec4(fragAO, 0.0, 0.0, 1.0);
}
