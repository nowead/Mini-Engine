#version 450

// Deferred Lighting fragment shader — Phase 3
// Reads G-Buffers + depth, reconstructs world position, applies PBR + CSM shadows + IBL.
// Sky pixels (depth == 1.0) are discarded (skybox was already rendered into HDR).

const float PI = 3.14159265359;

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------

struct PointLight {
    vec3 position;
    float radius;
    vec3 color;
    float intensity;
};

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
    PointLight pointLights[128];
    uint numPointLights;
    float debugCascades;  // 1.0 = visualize CSM cascade regions
    int   debugView;      // 0=normal, 1=normals, 2=albedo, 3=metallic, 4=roughness, 5=ao, 6=depth
    float _pad2;
} ubo;

// G-Buffers
layout(set = 0, binding = 1) uniform texture2D gBuffer0;   // normal.xyz + roughness
layout(set = 0, binding = 2) uniform texture2D gBuffer1;   // albedo.rgb + metallic
layout(set = 0, binding = 3) uniform texture2D gBuffer2;   // ao + padding
layout(set = 0, binding = 4) uniform texture2D depthTex;
layout(set = 0, binding = 5) uniform sampler   gbufferSampler;

// CSM shadow
layout(set = 0, binding = 6) uniform texture2DArray shadowCsmArray;
layout(set = 0, binding = 7) uniform sampler         shadowSampler;

// IBL
layout(set = 0, binding = 8)  uniform textureCube irradianceMap;
layout(set = 0, binding = 9)  uniform textureCube prefilteredMap;
layout(set = 0, binding = 10) uniform texture2D   brdfLUT;
layout(set = 0, binding = 11) uniform sampler     iblSampler;

layout(location = 0) out vec4 outColor;

// ---------------------------------------------------------------------------
// PBR helpers (same as building.frag.glsl)
// ---------------------------------------------------------------------------

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = max(dot(N, H), 0.0);
    float denom = (d * d * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return geometrySchlickGGX(max(dot(N, V), 0.0), roughness)
         * geometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ---------------------------------------------------------------------------
// CSM shadow (PCF 3×3 on selected cascade)
// ---------------------------------------------------------------------------

float calculateCSMShadow(vec3 worldPos, vec3 normal, vec3 lightDir) {
    // Select cascade by view-space depth
    vec4 posView = ubo.view * vec4(worldPos, 1.0);
    float viewDepth = -posView.z;  // positive depth in view space

    int cascadeIdx = 3;
    for (int i = 0; i < 4; ++i) {
        if (viewDepth < ubo.cascadeSplits[i]) {
            cascadeIdx = i;
            break;
        }
    }

    vec4 posLightSpace = ubo.lightSpaceMatrices[cascadeIdx] * vec4(worldPos, 1.0);
    vec3 projCoords = posLightSpace.xyz / posLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0)
        return 0.0;

    // Hardware PCF: the shadow sampler is a comparison sampler with linear
    // filtering, so each tap is a bilinear 2x2 depth comparison returning the
    // lit fraction. A 3x3 loop of these filters ~6x6 texels and smooths the
    // shadow-map grid that nearest-PCF produced on the large flat ground.
    float bias       = ubo.shadowBias * 0.01;
    float ref        = projCoords.z - bias;   // biased fragment depth
    float lit        = 0.0;
    vec2  texelSize  = 1.0 / ubo.shadowMapSize;

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 uv = projCoords.xy + vec2(x, y) * texelSize;
            lit += texture(sampler2DArrayShadow(shadowCsmArray, shadowSampler),
                           vec4(uv, float(cascadeIdx), ref));
        }
    }
    lit /= 9.0;
    return (1.0 - lit) * ubo.shadowStrength;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

void main() {
    // Derive UV from fragment coordinate
    ivec2 fragCoord = ivec2(gl_FragCoord.xy);
    vec2  screenSize = vec2(textureSize(sampler2D(gBuffer0, gbufferSampler), 0));
    vec2  uv = vec2(fragCoord) / screenSize;

    // Discard sky pixels
    float depth = texelFetch(sampler2D(depthTex, gbufferSampler), fragCoord, 0).r;
    if (depth >= 1.0) discard;

    // Reconstruct world position from depth
    vec4 ndcPos  = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = ubo.invProj * ndcPos;
    viewPos     /= viewPos.w;
    vec3 worldPos = vec3(ubo.invView * viewPos);

    // Sample G-Buffers
    vec4 gb0 = texelFetch(sampler2D(gBuffer0, gbufferSampler), fragCoord, 0);
    vec4 gb1 = texelFetch(sampler2D(gBuffer1, gbufferSampler), fragCoord, 0);
    vec4 gb2 = texelFetch(sampler2D(gBuffer2, gbufferSampler), fragCoord, 0);

    vec3  N         = normalize(gb0.xyz);
    float roughness = gb0.w;
    vec3  albedo    = gb1.rgb;
    float metallic  = gb1.a;
    float ao        = gb2.r;
    vec3  emissive  = gb2.gba;  // self-emission packed by the G-Buffer pass

    // G-Buffer debug views: bypass PBR and output raw channel data
    if (ubo.debugView != 0) {
        vec3 dbgColor;
        if (ubo.debugView == 1) {
            // Normals: remap [-1,1] → [0,1] for visualization
            dbgColor = N * 0.5 + 0.5;
        } else if (ubo.debugView == 2) {
            // Albedo
            dbgColor = albedo;
        } else if (ubo.debugView == 3) {
            // Metallic (grayscale)
            dbgColor = vec3(metallic);
        } else if (ubo.debugView == 4) {
            // Roughness (grayscale)
            dbgColor = vec3(roughness);
        } else if (ubo.debugView == 5) {
            // Material AO from G-Buffer (grayscale)
            dbgColor = vec3(ao);
        } else {
            // Depth: linearize using a fixed near/far matching the scene camera
            float near = 0.1;
            float far  = 1000.0;
            float linearDepth = (2.0 * near * far) / (far + near - depth * (far - near));
            dbgColor = vec3(linearDepth / far);
        }
        outColor = vec4(dbgColor, 1.0);
        return;
    }

    vec3 V    = normalize(ubo.cameraPos - worldPos);
    vec3 L    = normalize(ubo.sunDirection);
    vec3 H    = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Cook-Torrance BRDF
    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);
    vec3 kD       = (vec3(1.0) - F) * (1.0 - metallic);

    vec3 radiance = ubo.sunColor * ubo.sunIntensity;
    vec3 Lo       = (kD * albedo / PI + specular) * radiance * NdotL;

    // IBL ambient
    vec3 F_ibl  = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD_ibl = (1.0 - F_ibl) * (1.0 - metallic);

    vec3 irradiance    = texture(samplerCube(irradianceMap, iblSampler), N).rgb;
    float iblStrength  = max(irradiance.r, max(irradiance.g, irradiance.b));
    vec3 diffuseIBL    = irradiance * albedo;

    vec3 R              = reflect(-V, N);
    const float MAX_LOD = 4.0;
    vec3 prefilteredColor = textureLod(samplerCube(prefilteredMap, iblSampler), R, roughness * MAX_LOD).rgb;
    vec2 brdf           = texture(sampler2D(brdfLUT, iblSampler), vec2(NdotV, roughness)).rg;
    vec3 specularIBL    = prefilteredColor * (F_ibl * brdf.x + brdf.y);

    vec3 iblAmbient     = (kD_ibl * diffuseIBL + specularIBL) * ao;
    vec3 fallbackAmbient = albedo * ao;
    vec3 ambient        = mix(fallbackAmbient, iblAmbient, step(0.001, iblStrength)) * ubo.ambientIntensity;

    // CSM shadow (directional sun only)
    float shadow = calculateCSMShadow(worldPos, N, L);

    vec3 color = ambient + (1.0 - shadow) * Lo;

    // Dynamic point lights (deferred rendering advantage: O(lights) not O(lights*geometry))
    for (uint i = 0u; i < ubo.numPointLights; ++i) {
        vec3  Lp   = ubo.pointLights[i].position - worldPos;
        float dist = length(Lp);
        float r    = max(ubo.pointLights[i].radius, 0.001);
        if (dist >= r) continue;

        Lp = normalize(Lp);
        float atten = clamp(1.0 - (dist / r), 0.0, 1.0);
        atten *= atten; // quadratic falloff

        vec3  Hp     = normalize(V + Lp);
        float NdotLp = max(dot(N, Lp), 0.0);
        float NdotVp = max(dot(N, V),  0.0);

        float Dp = distributionGGX(N, Hp, roughness);
        float Gp = geometrySmith(N, V, Lp, roughness);
        vec3  Fp = fresnelSchlick(max(dot(Hp, V), 0.0), F0);

        vec3 specP = (Dp * Gp * Fp) / (4.0 * NdotVp * NdotLp + 0.0001);
        vec3 kDp   = (vec3(1.0) - Fp) * (1.0 - metallic);

        vec3 radianceP = ubo.pointLights[i].color * ubo.pointLights[i].intensity * atten;
        color += (kDp * albedo / PI + specP) * radianceP * NdotLp;
    }

    // Self-emission (glTF emissive). Added after all lighting so emissive
    // surfaces glow regardless of incident light. LDR-clamped: gBuffer2 is
    // RGBA8Unorm so values arrive in [0,1] (HDR emissive is a known limit).
    color += emissive;

    // CSM cascade debug visualization
    if (ubo.debugCascades > 0.5) {
        vec4 posView = ubo.view * vec4(worldPos, 1.0);
        float viewDepth = -posView.z;
        vec3 cascadeColors[4] = vec3[](
            vec3(1.0, 0.2, 0.2),   // cascade 0: red   (0~10m)
            vec3(0.2, 1.0, 0.2),   // cascade 1: green (10~50m)
            vec3(0.2, 0.4, 1.0),   // cascade 2: blue  (50~200m)
            vec3(1.0, 0.9, 0.1)    // cascade 3: yellow(200~1000m)
        );
        int ci = 3;
        for (int i = 0; i < 4; ++i) {
            if (viewDepth < ubo.cascadeSplits[i]) { ci = i; break; }
        }
        color = mix(color, cascadeColors[ci], 0.5);
    }

    outColor = vec4(color, 1.0);
}
