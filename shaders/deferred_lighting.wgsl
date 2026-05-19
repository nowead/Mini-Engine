// Deferred Lighting pass — WebGPU WGSL version
// Translates deferred_lighting.frag.glsl
// Reads G-Buffers + depth, reconstructs world position, applies PBR + CSM + IBL.
// Sky pixels (depth == 1.0) are discarded (skybox was already rendered into HDR).

const PI: f32 = 3.14159265359;

// =============================================================================
// Bind Group 0 — per-frame uniforms + G-Buffers + shadow + IBL (12 bindings)
// =============================================================================
// Must match C++ UniformBufferObject (Vertex.hpp) and deferred_lighting.frag.glsl
struct PointLight {
    position:  vec3<f32>,
    radius:    f32,
    color:     vec3<f32>,
    intensity: f32,
}

struct UniformBufferObject {
    model:              mat4x4<f32>,
    view:               mat4x4<f32>,
    proj:               mat4x4<f32>,
    invView:            mat4x4<f32>,
    invProj:            mat4x4<f32>,
    sunDirection:       vec3<f32>,
    sunIntensity:       f32,
    sunColor:           vec3<f32>,
    ambientIntensity:   f32,
    cameraPos:          vec3<f32>,
    exposure:           f32,
    lightSpaceMatrices: array<mat4x4<f32>, 4>,
    cascadeSplits:      vec4<f32>,
    shadowMapSize:      vec2<f32>,
    shadowBias:         f32,
    shadowStrength:     f32,
    pointLights:        array<PointLight, 128>,
    numPointLights:     u32,
    debugCascades:      f32,
    debugView:          i32,
    _pad2:              f32,
}

@group(0) @binding(0)  var<uniform> ubo:            UniformBufferObject;
@group(0) @binding(1)  var gBuffer0:       texture_2d<f32>;        // normal.xyz + roughness
@group(0) @binding(2)  var gBuffer1:       texture_2d<f32>;        // albedo.rgb + metallic
@group(0) @binding(3)  var gBuffer2:       texture_2d<f32>;        // ao + padding
@group(0) @binding(4)  var depthTex:       texture_depth_2d;
@group(0) @binding(5)  var gbufferSampler: sampler;
@group(0) @binding(6)  var shadowCsmArray: texture_depth_2d_array;
@group(0) @binding(7)  var shadowSampler:  sampler;
@group(0) @binding(8)  var irradianceMap:  texture_cube<f32>;
@group(0) @binding(9)  var prefilteredMap: texture_cube<f32>;
@group(0) @binding(10) var brdfLUT:        texture_2d<f32>;
@group(0) @binding(11) var iblSampler:     sampler;

// =============================================================================
// Vertex shader — fullscreen triangle (no vertex buffer)
// =============================================================================
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vertIdx: u32) -> VertexOutput {
    // Standard fullscreen-triangle-by-index trick.
    // Produces a triangle that fully covers NDC [-1,1]×[-1,1].
    let uv = vec2<f32>(f32((vertIdx << 1u) & 2u), f32(vertIdx & 2u));
    var out: VertexOutput;
    out.position = vec4<f32>(uv * 2.0 - 1.0, 0.0, 1.0);
    return out;
}

// =============================================================================
// PBR helpers
// =============================================================================

fn distributionGGX(N: vec3<f32>, H: vec3<f32>, roughness: f32) -> f32 {
    let a  = roughness * roughness;
    let a2 = a * a;
    let d  = max(dot(N, H), 0.0);
    let denom = d * d * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

fn geometrySchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

fn geometrySmith(N: vec3<f32>, V: vec3<f32>, L: vec3<f32>, roughness: f32) -> f32 {
    return geometrySchlickGGX(max(dot(N, V), 0.0), roughness)
         * geometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

fn fresnelSchlick(cosTheta: f32, F0: vec3<f32>) -> vec3<f32> {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

fn fresnelSchlickRoughness(cosTheta: f32, F0: vec3<f32>, roughness: f32) -> vec3<f32> {
    return F0 + (max(vec3<f32>(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// =============================================================================
// Soft shadows — PCSS on a single scene-fit shadow map
// =============================================================================
// ShadowRenderer now writes ONE camera-independent matrix into every cascade
// slot (all identical → slot 0). PCSS = blocker search → penumbra estimate →
// variable-radius Poisson PCF, giving contact-hardening soft shadows that are
// stable under any camera zoom/orbit (the matrix never changes per frame).

var<private> POISSON16: array<vec2<f32>, 16> = array<vec2<f32>, 16>(
    vec2<f32>(-0.942016, -0.399062), vec2<f32>( 0.945586, -0.768907),
    vec2<f32>(-0.094184, -0.929389), vec2<f32>( 0.344959,  0.293878),
    vec2<f32>(-0.915886,  0.457714), vec2<f32>(-0.815442, -0.879125),
    vec2<f32>(-0.382775,  0.276768), vec2<f32>( 0.974844,  0.756484),
    vec2<f32>( 0.443233, -0.975116), vec2<f32>( 0.537430, -0.473734),
    vec2<f32>(-0.264969, -0.418930), vec2<f32>( 0.791975,  0.190902),
    vec2<f32>(-0.241888,  0.997065), vec2<f32>(-0.814100,  0.914376),
    vec2<f32>( 0.199841,  0.786414), vec2<f32>( 0.143832, -0.141008)
);

fn calculateCSMShadow(worldPos: vec3<f32>) -> f32 {
    // Single scene-fit matrix (every slot identical → use 0).
    let posLightSpace = ubo.lightSpaceMatrices[0] * vec4<f32>(worldPos, 1.0);
    var projCoords = posLightSpace.xyz / posLightSpace.w;
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = -projCoords.y * 0.5 + 0.5;  // WebGPU: NDC Y=+1→texV=0, Y=-1→texV=1

    // Out-of-frustum: no shadow
    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    let receiver = projCoords.z;
    let bias     = ubo.shadowBias * 0.01;
    let texel    = 1.0 / ubo.shadowMapSize;          // vec2
    // Sun "size" as a UV search radius — larger = softer penumbra.
    let lightSizeUV = 3.0 * texel.x;

    // 1) Blocker search: average depth of texels closer to the light.
    var blockerSum   = 0.0;
    var blockerCount = 0.0;
    for (var i: i32 = 0; i < 16; i++) {
        let o = POISSON16[i] * lightSizeUV;
        let d = textureSampleLevel(shadowCsmArray, shadowSampler,
                                   projCoords.xy + o, 0, 0);
        if (d < receiver - bias) {
            blockerSum   += d;
            blockerCount += 1.0;
        }
    }
    if (blockerCount < 0.5) {
        return 0.0;                                  // no occluders → fully lit
    }
    let avgBlocker = blockerSum / blockerCount;

    // 2) Penumbra via similar triangles (contact-hardening).
    let penumbra = clamp((receiver - avgBlocker) / max(avgBlocker, 1e-4),
                         0.0, 1.0);
    let filterR  = max(penumbra * lightSizeUV, texel.x);  // ≥ 1 texel

    // 3) Variable-radius Poisson PCF.
    var shadow = 0.0;
    for (var i: i32 = 0; i < 16; i++) {
        let o = POISSON16[i] * filterR;
        let d = textureSampleLevel(shadowCsmArray, shadowSampler,
                                   projCoords.xy + o, 0, 0);
        shadow += select(0.0, 1.0, receiver - bias > d);
    }
    shadow /= 16.0;
    return shadow * ubo.shadowStrength;
}

// =============================================================================
// Fragment shader
// =============================================================================

@fragment
fn fs_main(@builtin(position) fragPos: vec4<f32>) -> @location(0) vec4<f32> {
    // Framebuffer coordinate → UV: (0,0) = top-left matches texture UV convention.
    let screenSize = vec2<f32>(textureDimensions(gBuffer0));
    let uv         = fragPos.xy / screenSize;
    let fragCoord  = vec2<i32>(i32(fragPos.x), i32(fragPos.y));

    // Read depth and discard sky pixels
    let depth = textureLoad(depthTex, fragCoord, 0);
    if (depth >= 1.0) { discard; }

    // Reconstruct world position from depth.
    // WebGPU: @builtin(position).y=0 is the top of the screen, NDC Y=+1 is also the top.
    // So: ndc.y = 1.0 - uv.y * 2.0  (uv.y=0→NDC+1, uv.y=1→NDC-1)
    // invProj is the inverse of the camera projection which has NO Y-flip for WebGPU.
    let ndc     = vec4<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    let viewPos = ubo.invProj * ndc;
    let worldPos = (ubo.invView * (viewPos / viewPos.w)).xyz;

    // Load G-Buffers at exact integer fragment coordinate — no sampler needed for deferred reads.
    let gb0 = textureLoad(gBuffer0, fragCoord, 0);
    let gb1 = textureLoad(gBuffer1, fragCoord, 0);
    let gb2 = textureLoad(gBuffer2, fragCoord, 0);

    let N         = normalize(gb0.xyz);
    let roughness = gb0.w;
    let albedo    = gb1.rgb;
    let metallic  = gb1.a;
    let ao        = gb2.r;

    // G-Buffer debug views
    if (ubo.debugView != 0) {
        var dbgColor: vec3<f32>;
        if (ubo.debugView == 1) {
            dbgColor = N * 0.5 + 0.5;
        } else if (ubo.debugView == 2) {
            dbgColor = albedo;
        } else if (ubo.debugView == 3) {
            dbgColor = vec3<f32>(metallic);
        } else if (ubo.debugView == 4) {
            dbgColor = vec3<f32>(roughness);
        } else if (ubo.debugView == 5) {
            dbgColor = vec3<f32>(ao);
        } else {
            let linearDepth = (2.0 * 0.1 * 1000.0) / (1000.0 + 0.1 - depth * (1000.0 - 0.1));
            dbgColor = vec3<f32>(linearDepth / 1000.0);
        }
        return vec4<f32>(dbgColor, 1.0);
    }

    let V     = normalize(ubo.cameraPos - worldPos);
    let L     = normalize(ubo.sunDirection);
    let H     = normalize(V + L);
    let NdotL = max(dot(N, L), 0.0);
    let NdotV = max(dot(N, V), 0.0);

    let F0 = mix(vec3<f32>(0.04), albedo, metallic);

    // Cook-Torrance BRDF (directional sun)
    let D = distributionGGX(N, H, roughness);
    let G = geometrySmith(N, V, L, roughness);
    let F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    let specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);
    let kD       = (vec3<f32>(1.0) - F) * (1.0 - metallic);
    let radiance = ubo.sunColor * ubo.sunIntensity;
    var Lo       = (kD * albedo / PI + specular) * radiance * NdotL;

    // IBL ambient
    let F_ibl  = fresnelSchlickRoughness(NdotV, F0, roughness);
    let kD_ibl = (1.0 - F_ibl) * (1.0 - metallic);

    let irradiance   = textureSample(irradianceMap, iblSampler, N).rgb;
    let iblStrength  = max(irradiance.r, max(irradiance.g, irradiance.b));
    let diffuseIBL   = irradiance * albedo;

    let R = reflect(-V, N);
    let prefilteredColor = textureSampleLevel(prefilteredMap, iblSampler, R, roughness * 4.0).rgb;
    let brdf         = textureSample(brdfLUT, iblSampler, vec2<f32>(NdotV, roughness)).rg;
    let specularIBL  = prefilteredColor * (F_ibl * brdf.x + brdf.y);

    let iblAmbient      = (kD_ibl * diffuseIBL + specularIBL) * ao;
    let fallbackAmbient = albedo * ao;
    let ambient = mix(fallbackAmbient, iblAmbient, step(0.001, iblStrength)) * ubo.ambientIntensity;

    // Directional sun shadow (single scene-fit map + PCSS)
    let shadow = calculateCSMShadow(worldPos);

    var color  = ambient + (1.0 - shadow) * Lo;

    // Dynamic point lights
    for (var i: u32 = 0u; i < ubo.numPointLights; i++) {
        let Lp   = ubo.pointLights[i].position - worldPos;
        let dist = length(Lp);
        let r    = max(ubo.pointLights[i].radius, 0.001);
        if (dist >= r) { continue; }

        let Ln   = normalize(Lp);
        var atten = clamp(1.0 - (dist / r), 0.0, 1.0);
        atten    *= atten;  // quadratic falloff

        let Hp     = normalize(V + Ln);
        let NdotLp = max(dot(N, Ln), 0.0);

        let Dp = distributionGGX(N, Hp, roughness);
        let Gp = geometrySmith(N, V, Ln, roughness);
        let Fp = fresnelSchlick(max(dot(Hp, V), 0.0), F0);

        let specP = (Dp * Gp * Fp) / (4.0 * NdotV * NdotLp + 0.0001);
        let kDp   = (vec3<f32>(1.0) - Fp) * (1.0 - metallic);

        let radianceP = ubo.pointLights[i].color * ubo.pointLights[i].intensity * atten;
        color += (kDp * albedo / PI + specP) * radianceP * NdotLp;
    }

    // CSM cascade debug visualization
    if (ubo.debugCascades > 0.5) {
        let posView2   = ubo.view * vec4<f32>(worldPos, 1.0);
        let viewDepth2 = -posView2.z;
        let cascadeColors = array<vec3<f32>, 4>(
            vec3<f32>(1.0, 0.2, 0.2),   // cascade 0: red   (0~10m)
            vec3<f32>(0.2, 1.0, 0.2),   // cascade 1: green (10~50m)
            vec3<f32>(0.2, 0.4, 1.0),   // cascade 2: blue  (50~200m)
            vec3<f32>(1.0, 0.9, 0.1),   // cascade 3: yellow (200~1000m)
        );
        var ci: i32 = 3;
        for (var i: i32 = 0; i < 4; i++) {
            if (viewDepth2 < ubo.cascadeSplits[i]) { ci = i; break; }
        }
        color = mix(color, cascadeColors[ci], 0.5);
    }

    return vec4<f32>(color, 1.0);
}
