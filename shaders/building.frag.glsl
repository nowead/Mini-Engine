#version 450

// Inputs from vertex shader
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec4 fragPosLightSpace;

// Uniform buffer (lighting + shadow parameters)
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 sunDirection;      // Normalized direction TO the sun
    float sunIntensity;     // Sun light intensity
    vec3 sunColor;          // Sun light color
    float ambientIntensity; // Ambient light intensity
    vec3 cameraPos;         // Camera position for specular
    float _padding;
    // Shadow mapping
    mat4 lightSpaceMatrix;  // Light view-projection matrix
    vec2 shadowMapSize;     // Shadow map dimensions
    float shadowBias;       // Depth bias
    float shadowStrength;   // Shadow darkness (0-1)
} ubo;

// Shadow map (separate texture and sampler for Vulkan compatibility)
layout(binding = 1) uniform texture2D shadowMapTex;
layout(binding = 2) uniform sampler shadowMapSampler;

// Output
layout(location = 0) out vec4 outColor;

// Calculate shadow using PCF (Percentage Closer Filtering)
float calculateShadow(vec4 posLightSpace, vec3 normal, vec3 lightDir) {
    // Perspective divide
    vec3 projCoords = posLightSpace.xyz / posLightSpace.w;

    // Transform to [0,1] range (NDC to texture coords)
    projCoords = projCoords * 0.5 + 0.5;

    // Check if outside light frustum
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;  // No shadow outside light frustum
    }

    // Bias to prevent shadow acne (adjust based on surface angle)
    float bias = max(ubo.shadowBias * (1.0 - dot(normal, lightDir)), ubo.shadowBias * 0.1);

    // Current fragment depth from light's perspective
    float currentDepth = projCoords.z;

    // PCF: Sample neighboring texels for soft shadows
    float shadow = 0.0;
    vec2 texelSize = 1.0 / ubo.shadowMapSize;

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(sampler2D(shadowMapTex, shadowMapSampler), projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;  // Average 3x3 samples

    return shadow * ubo.shadowStrength;
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(ubo.sunDirection);
    vec3 V = normalize(ubo.cameraPos - fragWorldPos);
    vec3 H = normalize(L + V);

    // Calculate shadow factor
    float shadow = calculateShadow(fragPosLightSpace, N, L);

    // Ambient lighting (not affected by shadow)
    vec3 ambient = ubo.ambientIntensity * fragColor;

    // Diffuse lighting (Lambertian) - affected by shadow
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = ubo.sunIntensity * NdotL * ubo.sunColor * fragColor;

    // Specular lighting (Blinn-Phong) - affected by shadow
    float NdotH = max(dot(N, H), 0.0);
    float shininess = 32.0;
    float spec = pow(NdotH, shininess);
    vec3 specular = ubo.sunIntensity * spec * ubo.sunColor * 0.3;

    // Apply shadow: ambient is always visible, diffuse/specular reduced in shadow
    vec3 finalColor = ambient + (1.0 - shadow) * (diffuse + specular);

    // Simple tone mapping
    finalColor = finalColor / (finalColor + vec3(1.0));

    outColor = vec4(finalColor, 1.0);
}
