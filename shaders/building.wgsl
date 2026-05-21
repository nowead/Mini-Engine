// Building shader with PBR (Cook-Torrance) and shadow mapping
// WebGPU WGSL version

const PI: f32 = 3.14159265359;

// Must match C++ UniformBufferObject (Vertex.hpp) exactly.
// MAX_POINT_LIGHTS = 128; PointLight = 32 bytes each.
struct PointLight {
    position:  vec3<f32>,
    radius:    f32,
    color:     vec3<f32>,
    intensity: f32,
}

struct UniformBufferObject {
    model:            mat4x4<f32>,           // offset   0
    view:             mat4x4<f32>,           // offset  64
    proj:             mat4x4<f32>,           // offset 128
    invView:          mat4x4<f32>,           // offset 192 — for world-pos reconstruction
    invProj:          mat4x4<f32>,           // offset 256 — for world-pos reconstruction
    sunDirection:     vec3<f32>,             // offset 320
    sunIntensity:     f32,
    sunColor:         vec3<f32>,             // offset 336
    ambientIntensity: f32,
    cameraPos:        vec3<f32>,             // offset 352
    exposure:         f32,
    // Phase 3 CSM: 4-cascade shadow maps
    lightSpaceMatrices: array<mat4x4<f32>, 4>, // offset 368
    cascadeSplits:    vec4<f32>,             // offset 624
    shadowMapSize:    vec2<f32>,             // offset 640
    shadowBias:       f32,
    shadowStrength:   f32,
    // Phase 4: dynamic point lights
    pointLights:      array<PointLight, 128>, // offset 656
    numPointLights:   u32,                    // offset 4752
    debugCascades:    f32,
    debugView:        i32,
    abSplitX:         f32,                    // 0 = off; (0,1) = A/B compare split position
}

@group(0) @binding(0) var<uniform> ubo: UniformBufferObject;
@group(0) @binding(1) var shadowMapTex: texture_depth_2d_array;
@group(0) @binding(2) var shadowMapSampler: sampler;
// IBL textures
@group(0) @binding(3) var irradianceMap: texture_cube<f32>;
@group(0) @binding(4) var prefilteredMap: texture_cube<f32>;
@group(0) @binding(5) var brdfLUT: texture_2d<f32>;
@group(0) @binding(6) var iblSampler: sampler;

// MUST match C++ ObjectData (InstancedRenderData.hpp) — 144 bytes.
// See that file for the field-by-field contract.
struct ObjectData {
    worldMatrix: mat4x4<f32>,
    boundingBoxMin: vec4<f32>,
    boundingBoxMax: vec4<f32>,
    colorAndMetallic: vec4<f32>,   // rgb = albedo, a = metallic
    roughnessAOPad: vec4<f32>,     // r = roughness, g = ao, b = legacy bindless albedo idx, a = pad
    textureIndices: vec4<u32>,     // x = baseColor, y = normal, z = MR, w = emissive (0xFFFFFFFF = none)
}

struct ObjectBuffer {
    objects: array<ObjectData>,
}

@group(1) @binding(0) var<storage, read> objectBuffer: ObjectBuffer;

// Phase 2.2: Visible indices from GPU frustum culling
struct VisibleIndicesBuffer {
    indices: array<u32>,
}
@group(1) @binding(1) var<storage, read> visibleIndices: VisibleIndicesBuffer;

// Vertex input (per-vertex only)
struct VertexInput {
    @builtin(instance_index) instanceIndex: u32,
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) texCoord: vec2<f32>,
    @location(3) tangent: vec3<f32>,  // consumed by normal-map fragment path in a later sub-task
}

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) worldPos: vec3<f32>,
    @location(3) posLightSpace: vec4<f32>,
    @location(4) metallic: f32,
    @location(5) roughness: f32,
    @location(6) ao: f32,
}

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;

    // Phase 2.2: Indirection through visible indices from frustum culling
    let actualIndex = visibleIndices.indices[input.instanceIndex];
    let obj = objectBuffer.objects[actualIndex];
    let worldPos4 = obj.worldMatrix * vec4<f32>(input.position, 1.0);
    let worldPos = worldPos4.xyz;

    output.position = ubo.proj * ubo.view * ubo.model * worldPos4;
    output.color = obj.colorAndMetallic.rgb;
    output.normal = input.normal;
    output.worldPos = worldPos;
    output.posLightSpace = ubo.lightSpaceMatrices[0] * worldPos4;
    output.metallic = obj.colorAndMetallic.a;
    output.roughness = obj.roughnessAOPad.r;
    output.ao = obj.roughnessAOPad.g;

    return output;
}

// =============================================================================
// PBR Functions
// =============================================================================

fn distributionGGX(N: vec3<f32>, H: vec3<f32>, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let NdotH = max(dot(N, H), 0.0);
    let NdotH2 = NdotH * NdotH;

    let denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

fn geometrySchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

fn geometrySmith(N: vec3<f32>, V: vec3<f32>, L: vec3<f32>, roughness: f32) -> f32 {
    let NdotV = max(dot(N, V), 0.0);
    let NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

fn fresnelSchlick(cosTheta: f32, F0: vec3<f32>) -> vec3<f32> {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

fn fresnelSchlickRoughness(cosTheta: f32, F0: vec3<f32>, roughness: f32) -> vec3<f32> {
    return F0 + (max(vec3<f32>(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// =============================================================================
// Shadow Calculation
// =============================================================================

fn calculateShadow(worldPos: vec3<f32>, normal: vec3<f32>, lightDir: vec3<f32>) -> f32 {
    // CSM cascade selection: pick cascade based on view-space depth
    let viewZ = -(ubo.view * vec4<f32>(worldPos, 1.0)).z;
    var cascadeIdx: i32 = 3;
    if (viewZ < ubo.cascadeSplits.x) { cascadeIdx = 0; }
    else if (viewZ < ubo.cascadeSplits.y) { cascadeIdx = 1; }
    else if (viewZ < ubo.cascadeSplits.z) { cascadeIdx = 2; }

    let posLS4 = ubo.lightSpaceMatrices[cascadeIdx] * vec4<f32>(worldPos, 1.0);
    var projCoords = posLS4.xyz / posLS4.w;
    projCoords.x = projCoords.x * 0.5 + 0.5;
    projCoords.y = (-projCoords.y) * 0.5 + 0.5;

    // Slope-scaled bias
    let cosTheta = clamp(dot(normalize(normal), normalize(lightDir)), 0.0, 1.0);
    let bias = ubo.shadowBias * mix(2.0, 1.0, cosTheta);
    let currentDepth = projCoords.z;
    let clampedCoords = clamp(projCoords.xy, vec2<f32>(0.0), vec2<f32>(1.0));
    let texelSize = 1.0 / ubo.shadowMapSize;
    let layer = cascadeIdx;

    // Unrolled PCF 3x3
    let d_m1_m1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(-1.0, -1.0) * texelSize, layer);
    let d_0_m1  = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>( 0.0, -1.0) * texelSize, layer);
    let d_p1_m1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>( 1.0, -1.0) * texelSize, layer);
    let d_m1_0  = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(-1.0,  0.0) * texelSize, layer);
    let d_0_0   = textureSample(shadowMapTex, shadowMapSampler, clampedCoords, layer);
    let d_p1_0  = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>( 1.0,  0.0) * texelSize, layer);
    let d_m1_p1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(-1.0,  1.0) * texelSize, layer);
    let d_0_p1  = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>( 0.0,  1.0) * texelSize, layer);
    let d_p1_p1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>( 1.0,  1.0) * texelSize, layer);

    let compDepth = currentDepth - bias;
    var shadow: f32 = 0.0;
    shadow += select(0.0, 1.0, compDepth > d_m1_m1);
    shadow += select(0.0, 1.0, compDepth > d_0_m1);
    shadow += select(0.0, 1.0, compDepth > d_p1_m1);
    shadow += select(0.0, 1.0, compDepth > d_m1_0);
    shadow += select(0.0, 1.0, compDepth > d_0_0);
    shadow += select(0.0, 1.0, compDepth > d_p1_0);
    shadow += select(0.0, 1.0, compDepth > d_m1_p1);
    shadow += select(0.0, 1.0, compDepth > d_0_p1);
    shadow += select(0.0, 1.0, compDepth > d_p1_p1);
    shadow /= 9.0;

    let outsideFrustum = projCoords.z > 1.0 || projCoords.z < 0.0 ||
                         projCoords.x < 0.0 || projCoords.x > 1.0 ||
                         projCoords.y < 0.0 || projCoords.y > 1.0;
    return select(shadow * ubo.shadowStrength, 0.0, outsideFrustum);
}

// =============================================================================
// Main
// =============================================================================

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    // sRGB to linear conversion for albedo
    let albedo = pow(input.color, vec3<f32>(2.2));
    let metallic = input.metallic;
    let roughness = input.roughness;
    let ao = input.ao;

    let N = normalize(input.normal);
    let V = normalize(ubo.cameraPos - input.worldPos);
    let L = normalize(ubo.sunDirection);
    let H = normalize(V + L);

    let NdotL = max(dot(N, L), 0.0);
    let NdotV = max(dot(N, V), 0.0);

    // Base reflectivity
    let F0 = mix(vec3<f32>(0.04), albedo, metallic);

    // Cook-Torrance BRDF
    let D = distributionGGX(N, H, roughness);
    let G = geometrySmith(N, V, L, roughness);
    let F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    let numerator = D * G * F;
    let denominator = 4.0 * NdotV * NdotL + 0.0001;
    let specular = numerator / denominator;

    // Energy conservation
    let kS = F;
    let kD = (vec3<f32>(1.0) - kS) * (1.0 - metallic);

    // Direct lighting (sun)
    let radiance = ubo.sunColor * ubo.sunIntensity;
    let Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // IBL Ambient Lighting
    let F_ibl = fresnelSchlickRoughness(NdotV, F0, roughness);
    let kS_ibl = F_ibl;
    let kD_ibl = (1.0 - kS_ibl) * (1.0 - metallic);

    // Diffuse IBL: irradiance map lookup
    let irradiance = textureSample(irradianceMap, iblSampler, N).rgb;
    let diffuseIBL = irradiance * albedo;

    // Specular IBL: prefiltered env map + BRDF LUT
    let MAX_REFLECTION_LOD: f32 = 4.0;
    let R = reflect(-V, N);
    let prefilteredColor = textureSampleLevel(prefilteredMap, iblSampler, R, roughness * MAX_REFLECTION_LOD).rgb;
    let brdf = textureSample(brdfLUT, iblSampler, vec2<f32>(NdotV, roughness)).rg;
    let specularIBL = prefilteredColor * (F_ibl * brdf.x + brdf.y);

    // Fallback: when no HDR environment is loaded, irradiance cubemap is black
    let iblStrength = max(irradiance.r, max(irradiance.g, irradiance.b));
    let iblAmbient = (kD_ibl * diffuseIBL + specularIBL) * ao;
    let fallbackAmbient = albedo * ao;
    let ambient = mix(fallbackAmbient, iblAmbient, step(0.001, iblStrength)) * ubo.ambientIntensity;

    // Shadow
    let shadow = calculateShadow(input.worldPos, N, L);

    // Final color — output HDR; tonemapping handled by separate tonemap pass
    var color = ambient + (1.0 - shadow) * Lo;
    let exp = select(ubo.exposure, 1.0, ubo.exposure <= 0.0);
    color = color * exp;

    return vec4<f32>(color, 1.0);
}
