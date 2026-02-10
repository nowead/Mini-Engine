#version 450

// Vertex Input
layout(location = 0) in vec3 inPosition;
layout(location = 1)

 in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Vertex Output
layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out vec2 fragTexCoord;

// Uniform Buffer (updated for dual point lights)
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;

    // Point Light 1 (Blue)
    vec3 light1Position;
    float light1Intensity;
    vec3 light1Color;
    float light1Radius;

    // Point Light 2 (Red)
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

void main() {
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    fragNormal = mat3(transpose(inverse(ubo.model))) * inNormal;
    fragTexCoord = inTexCoord;

    gl_Position = ubo.proj * ubo.view * worldPos;
}
