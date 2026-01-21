#version 450

// Inputs from vertex shader
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;

// Uniform buffer (lighting parameters)
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
} ubo;

// Output
layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(ubo.sunDirection);
    vec3 V = normalize(ubo.cameraPos - fragWorldPos);
    vec3 H = normalize(L + V);

    // Ambient lighting
    vec3 ambient = ubo.ambientIntensity * fragColor;

    // Diffuse lighting (Lambertian)
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = ubo.sunIntensity * NdotL * ubo.sunColor * fragColor;

    // Specular lighting (Blinn-Phong)
    float NdotH = max(dot(N, H), 0.0);
    float shininess = 32.0;
    float spec = pow(NdotH, shininess);
    vec3 specular = ubo.sunIntensity * spec * ubo.sunColor * 0.3;

    // Combine lighting components
    vec3 finalColor = ambient + diffuse + specular;

    // Simple tone mapping
    finalColor = finalColor / (finalColor + vec3(1.0));

    outColor = vec4(finalColor, 1.0);
}
