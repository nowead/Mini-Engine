#version 450

// G-Buffer fragment shader — Phase 4 Bindless Textures
// Packs material properties into 3 MRT targets:
//   GBuffer0 (RGBA16Float): world-space Normal (xyz) + Roughness
//   GBuffer1 (RGBA8Unorm):  Albedo (rgb, linear) + Metallic
//   GBuffer2 (RGBA8Unorm):  AO (r), padding (gba)
//
// Phase 4: albedo is sampled from a bindless texture array (set 2, binding 0) when
// albedoIndex < BINDLESS_INVALID. Falls back to procedural color otherwise.

// Enable nonuniformEXT for wave-divergent texture indexing.
// "enable" (not "require") allows graceful compile on drivers without the extension.
#extension GL_EXT_nonuniform_qualifier : enable

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in float fragMetallic;
layout(location = 4) in float fragRoughness;
layout(location = 5) in float fragAO;
layout(location = 6) in vec3 fragAlbedo;
layout(location = 7) in flat uint fragAlbedoIndex;   // texture slot (0xFFFFFFFF = no texture)

// Phase 4: bindless texture array at set 2
// When the device doesn't support descriptor indexing, this set is not created,
// the pipeline layout excludes set 2, and fragAlbedoIndex will always be 0xFFFFFFFF.
layout(set = 2, binding = 0) uniform sampler2D allTextures[];

// ---------------------------------------------------------------------------
// Outputs
// ---------------------------------------------------------------------------

layout(location = 0) out vec4 gBuffer0;  // normal.xyz + roughness
layout(location = 1) out vec4 gBuffer1;  // albedo.rgb (linear) + metallic
layout(location = 2) out vec4 gBuffer2;  // ao + padding

// ---------------------------------------------------------------------------

const uint BINDLESS_INVALID = 0xFFFFFFFFu;

void main() {
    vec3 albedoLinear;

    if (fragAlbedoIndex != BINDLESS_INVALID) {
        // Sample from the registered material texture.
        // nonuniformEXT tells the driver that the index may differ across invocations
        // within a wave — required for correct execution on DX12 / Vulkan bindless.
        vec4 texSample = texture(allTextures[nonuniformEXT(fragAlbedoIndex)], fragTexCoord);
        albedoLinear = texSample.rgb;  // assume textures are stored in linear space
    } else {
        // Fallback: procedural sRGB color from SSBO → linear
        albedoLinear = pow(fragAlbedo, vec3(2.2));
    }

    gBuffer0 = vec4(normalize(fragNormal), fragRoughness);
    gBuffer1 = vec4(albedoLinear, fragMetallic);
    gBuffer2 = vec4(fragAO, 0.0, 0.0, 1.0);
}
