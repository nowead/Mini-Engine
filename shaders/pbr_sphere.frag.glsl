#version 450

const float PI = 3.14159265359;

// Inputs from vertex shader
layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec2 fragTexCoord;

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
    vec3 albedo;
    float metallic;
    float roughness;
    float ao;
} ubo;

// IBL textures
layout(set = 0, binding = 1) uniform textureCube irradianceMap;
layout(set = 0, binding = 2) uniform textureCube prefilteredMap;
layout(set = 0, binding = 3) uniform texture2D brdfLUT;
layout(set = 0, binding = 4) uniform sampler iblSampler;

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

    return a2 / max(denom, 0.0001);
}

// Geometry Function: Schlick-GGX
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    return NdotV / (NdotV * (1.0 - k) + k);
}

// Geometry Function: Smith's method
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
// Main Fragment Shader
// =============================================================================

void main() {
    // Compute normal from world position (sphere center = model translation)
    vec3 sphereCenter = ubo.model[3].xyz;
    vec3 N = normalize(fragWorldPos - sphereCenter);
    vec3 V = normalize(ubo.cameraPos - fragWorldPos);
    vec3 L = normalize(-ubo.sunDirection);
    vec3 H = normalize(V + L);
    vec3 R = reflect(-V, N);

    // Material properties
    vec3 albedo = ubo.albedo;
    float metallic = ubo.metallic;
    float roughness = ubo.roughness;
    float ao = ubo.ao;

    // F0 (surface reflection at zero incidence)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // =========================================================================
    // Direct Lighting (Sun)
    // =========================================================================
    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    float NdotL = max(dot(N, L), 0.0);
    vec3 radiance = ubo.sunColor * ubo.sunIntensity;
    vec3 directLighting = (kD * albedo / PI + specular) * radiance * NdotL;

    // =========================================================================
    // Image-Based Lighting (IBL)
    // =========================================================================
    vec3 F_IBL = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kS_IBL = F_IBL;
    vec3 kD_IBL = 1.0 - kS_IBL;
    kD_IBL *= 1.0 - metallic;

    // Irradiance (diffuse IBL)
    vec3 irradiance = texture(samplerCube(irradianceMap, iblSampler), N).rgb;
    vec3 diffuse_IBL = kD_IBL * albedo * irradiance;

    // Prefiltered environment (specular IBL)
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(samplerCube(prefilteredMap, iblSampler), R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 envBRDF = texture(sampler2D(brdfLUT, iblSampler), vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular_IBL = prefilteredColor * (F_IBL * envBRDF.x + envBRDF.y);

    vec3 ambient = (diffuse_IBL + specular_IBL) * ao * ubo.ambientIntensity;

    // =========================================================================
    // Final Color
    // =========================================================================
    vec3 color = directLighting + ambient;

    // Tone mapping
    color = ACESFilm(color * ubo.exposure);

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
