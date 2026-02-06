#version 450

const float PI = 3.14159265359;

// Inputs from vertex shader
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec4 fragPosLightSpace;
layout(location = 4) in float fragMetallic;
layout(location = 5) in float fragRoughness;
layout(location = 6) in float fragAO;

// Uniform buffer
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 sunDirection;
    float sunIntensity;
    vec3 sunColor;
    float ambientIntensity;
    vec3 cameraPos;
    float exposure;
    // Shadow mapping
    mat4 lightSpaceMatrix;
    vec2 shadowMapSize;
    float shadowBias;
    float shadowStrength;
} ubo;

// Shadow map
layout(set = 0, binding = 1) uniform texture2D shadowMapTex;
layout(set = 0, binding = 2) uniform sampler shadowMapSampler;

// IBL textures
layout(set = 0, binding = 3) uniform textureCube irradianceMap;
layout(set = 0, binding = 4) uniform textureCube prefilteredMap;
layout(set = 0, binding = 5) uniform texture2D brdfLUT;
layout(set = 0, binding = 6) uniform sampler iblSampler;

// Output
layout(location = 0) out vec4 outColor;

// =============================================================================
// PBR Functions
// =============================================================================

// Normal Distribution Function: Trowbridge-Reitz GGX
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return a2 / denom;
}

// Geometry Function: Schlick-GGX
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    return NdotV / (NdotV * (1.0 - k) + k);
}

// Geometry Function: Smith's method (combination of view and light)
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Fresnel: Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel: Schlick with roughness (for IBL)
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ACES Filmic Tone Mapping
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// =============================================================================
// Shadow Calculation (unchanged)
// =============================================================================

float calculateShadow(vec4 posLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = posLightSpace.xyz / posLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    float bias = ubo.shadowBias * 0.01;
    float currentDepth = projCoords.z;

    float shadow = 0.0;
    vec2 texelSize = 1.0 / ubo.shadowMapSize;

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(sampler2D(shadowMapTex, shadowMapSampler), projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;

    return shadow * ubo.shadowStrength;
}

// =============================================================================
// Main
// =============================================================================

void main() {
    // sRGB to linear conversion for albedo
    vec3 albedo = pow(fragColor, vec3(2.2));
    float metallic = fragMetallic;
    float roughness = fragRoughness;
    float ao = fragAO;

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPos - fragWorldPos);
    vec3 L = normalize(ubo.sunDirection);
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    // Calculate base reflectivity (F0)
    // Dielectrics: 0.04, Metals: albedo color
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Cook-Torrance BRDF
    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specular = numerator / denominator;

    // Energy conservation
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    // Direct lighting (sun)
    vec3 radiance = ubo.sunColor * ubo.sunIntensity;
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // IBL Ambient Lighting
    vec3 F_ibl = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kS_ibl = F_ibl;
    vec3 kD_ibl = (1.0 - kS_ibl) * (1.0 - metallic);

    // Diffuse IBL: irradiance map lookup
    vec3 irradiance = texture(samplerCube(irradianceMap, iblSampler), N).rgb;
    vec3 diffuseIBL = irradiance * albedo;

    // Specular IBL: prefiltered env map + BRDF LUT
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 R = reflect(-V, N);
    vec3 prefilteredColor = textureLod(samplerCube(prefilteredMap, iblSampler), R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(sampler2D(brdfLUT, iblSampler), vec2(NdotV, roughness)).rg;
    vec3 specularIBL = prefilteredColor * (F_ibl * brdf.x + brdf.y);

    // Fallback: when no HDR environment is loaded, irradiance cubemap is black
    float iblStrength = max(irradiance.r, max(irradiance.g, irradiance.b));
    vec3 iblAmbient = (kD_ibl * diffuseIBL + specularIBL) * ao;
    vec3 fallbackAmbient = albedo * ao;
    vec3 ambient = mix(fallbackAmbient, iblAmbient, step(0.001, iblStrength)) * ubo.ambientIntensity;

    // Shadow
    float shadow = calculateShadow(fragPosLightSpace, N, L);

    // Final color: ambient (not shadowed) + direct light (shadowed)
    vec3 color = ambient + (1.0 - shadow) * Lo;

    // Tone mapping (ACES)
    float exp = ubo.exposure > 0.0 ? ubo.exposure : 1.0;
    color = ACESFilm(color * exp);

    outColor = vec4(color, 1.0);
}
