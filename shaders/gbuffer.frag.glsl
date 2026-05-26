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
layout(location = 7) in flat uint fragAlbedoIndex;     // baseColor slot (0xFFFFFFFF = none)
// Showcase PBR (glTF) bindless slots. 0xFFFFFFFF = slot absent → use scalar.
layout(location = 8)  in vec3      fragTangent;        // world-space, unnormalized
layout(location = 9)  in flat uint fragNormalIndex;
layout(location = 10) in flat uint fragMRIndex;        // G=roughness, B=metallic
layout(location = 11) in flat uint fragEmissiveIndex;
layout(location = 12) in flat uint fragAOIndex;
layout(location = 13) in vec4 fragCurrClip;   // TAA: current clip pos (pre-divide)
layout(location = 14) in vec4 fragPrevClip;   // TAA: previous clip pos (pre-divide)

// Phase 4: bindless texture array at set 2
// When the device doesn't support descriptor indexing, this set is not created,
// the pipeline layout excludes set 2, and fragAlbedoIndex will always be 0xFFFFFFFF.
layout(set = 2, binding = 0) uniform sampler2D allTextures[];

// ---------------------------------------------------------------------------
// Outputs
// ---------------------------------------------------------------------------

layout(location = 0) out vec4 gBuffer0;  // normal.xyz + roughness
layout(location = 1) out vec4 gBuffer1;  // albedo.rgb (linear) + metallic
layout(location = 2) out vec4 gBuffer2;  // ao (r) + emissive (gba)
layout(location = 3) out vec2 gBuffer3;  // TAA screen-space velocity (curr - prev UV)

// ---------------------------------------------------------------------------

const uint BINDLESS_INVALID = 0xFFFFFFFFu;

void main() {
    // ---- Albedo --------------------------------------------------------
    // baseColor textures are sRGB-format views, so the sample is already
    // linearized by the hardware. Buildings supply a 1×1 linear solid colour;
    // the procedural fallback decodes the SSBO sRGB factor to linear.
    // The glTF baseColorFactor (white for the helmet) is folded into the
    // factor-only path only, matching the existing building behaviour.
    vec3 albedoLinear;
    if (fragAlbedoIndex != BINDLESS_INVALID) {
        // nonuniformEXT: the index may differ across invocations within a wave.
        albedoLinear = texture(allTextures[nonuniformEXT(fragAlbedoIndex)], fragTexCoord).rgb;
    } else {
        albedoLinear = pow(fragAlbedo, vec3(2.2));
    }

    // ---- Normal (tangent-space normal mapping) -------------------------
    // Path A — normal map + per-vertex tangent present: Gram-Schmidt TBN.
    // Path B — no normal map or no tangent: interpolated vertex normal.
    vec3 N = normalize(fragNormal);
    if (fragNormalIndex != BINDLESS_INVALID && length(fragTangent) > 0.01) {
        vec3 nTan = texture(allTextures[nonuniformEXT(fragNormalIndex)], fragTexCoord).rgb
                    * 2.0 - 1.0;
        vec3 T      = normalize(fragTangent);
        vec3 Tortho = normalize(T - N * dot(N, T));
        vec3 B      = cross(N, Tortho);
        mat3 TBN    = mat3(Tortho, B, N);
        N = normalize(TBN * nTan);
    }

    // ---- Metallic-roughness (G=roughness, B=metallic) ------------------
    float roughness = fragRoughness;
    float metallic  = fragMetallic;
    if (fragMRIndex != BINDLESS_INVALID) {
        vec4 mr   = texture(allTextures[nonuniformEXT(fragMRIndex)], fragTexCoord);
        roughness = fragRoughness * mr.g;
        metallic  = fragMetallic  * mr.b;
    }

    // ---- Occlusion -----------------------------------------------------
    float ao = fragAO;
    if (fragAOIndex != BINDLESS_INVALID) {
        ao = fragAO * texture(allTextures[nonuniformEXT(fragAOIndex)], fragTexCoord).r;
    }

    // ---- Emissive (sRGB view → linear sample) --------------------------
    vec3 emissive = vec3(0.0);
    if (fragEmissiveIndex != BINDLESS_INVALID) {
        emissive = texture(allTextures[nonuniformEXT(fragEmissiveIndex)], fragTexCoord).rgb;
    }

    gBuffer0 = vec4(N,            roughness);
    gBuffer1 = vec4(albedoLinear, metallic);
    gBuffer2 = vec4(ao, emissive);  // .r = ao, .gba = emissive RGB (clamped to [0,1])

    // TAA motion vector: per-fragment perspective divide of curr/prev clip pos
    // -> NDC -> [0,1] UV, store the delta. History is sampled at (uv - velocity)
    // = previous-frame UV. Y matches the Vulkan-flipped projection in both terms.
    vec2 currUV = (fragCurrClip.xy / fragCurrClip.w) * 0.5 + 0.5;
    vec2 prevUV = (fragPrevClip.xy / fragPrevClip.w) * 0.5 + 0.5;
    gBuffer3 = currUV - prevUV;
}
