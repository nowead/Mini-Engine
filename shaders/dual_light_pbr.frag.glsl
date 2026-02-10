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

    // Point Light 1 (Blue - left)
    vec3 light1Position;
    float light1Intensity;
    vec3 light1Color;
    float light1Radius;

    // Point Light 2 (Red - right)
    vec3 light2Position;
    float light2Intensity;
    vec3 light2Color;
    float light2Radius;

    vec3 cameraPos;
    float exposure;
    vec3 albedo;
    float metallic;
    float roughness;
    float ao;
    vec3 ambientColor;
    float ambientIntensity;
} ubo;

// Output
layout(location = 0) out vec4 outColor;

// =============================================================================
// PBR Functions (from pbr_sphere.frag.glsl)
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
// Point Light Calculation
// =============================================================================

vec3 calculatePointLight(vec3 lightPos, vec3 lightColor, float lightIntensity, float lightRadius,
                         vec3 N, vec3 V, vec3 F0, vec3 albedo, float metallic, float roughness) {
    // Light direction and distance
    vec3 L = lightPos - fragWorldPos;
    float distance = length(L);
    L = normalize(L);
    vec3 H = normalize(V + L);

    // Attenuation with smooth cutoff
    float attenuation = 1.0 / (distance * distance);  // Inverse square law
    float cutoff = smoothstep(lightRadius, lightRadius * 0.8, distance);
    attenuation *= cutoff;

    // Radiance
    vec3 radiance = lightColor * lightIntensity * attenuation;

    // Cook-Torrance BRDF
    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    // Energy conservation
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;  // Metallic materials have no diffuse

    float NdotL = max(dot(N, L), 0.0);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

// =============================================================================
// Main Fragment Shader
// =============================================================================

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPos - fragWorldPos);

    // Material properties
    vec3 albedo = ubo.albedo;
    float metallic = ubo.metallic;
    float roughness = ubo.roughness;
    float ao = ubo.ao;

    // F0 (surface reflection at zero incidence)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // =========================================================================
    // Direct Lighting from both point lights
    // =========================================================================
    vec3 lighting = vec3(0.0);

    // Point Light 1 (Blue - left)
    lighting += calculatePointLight(ubo.light1Position, ubo.light1Color, ubo.light1Intensity, ubo.light1Radius,
                                    N, V, F0, albedo, metallic, roughness);

    // Point Light 2 (Red - right)
    lighting += calculatePointLight(ubo.light2Position, ubo.light2Color, ubo.light2Intensity, ubo.light2Radius,
                                    N, V, F0, albedo, metallic, roughness);

    // =========================================================================
    // Minimal Ambient
    // =========================================================================
    vec3 ambient = ubo.ambientColor * ubo.ambientIntensity * albedo * ao;

    // =========================================================================
    // Final Color
    // =========================================================================
    vec3 color = lighting + ambient;

    // Tone mapping
    color = ACESFilm(color * ubo.exposure);

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
