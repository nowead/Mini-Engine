#version 450

// Per-particle attributes (instanced, binding 0)
layout(location = 0) in vec3 inPosition;      // Particle world position
layout(location = 1) in float inLifetime;     // Remaining lifetime
layout(location = 2) in vec3 inVelocity;      // Velocity (unused in vertex shader)
layout(location = 3) in float inAge;          // Current age
layout(location = 4) in vec4 inColor;         // Particle color
layout(location = 5) in vec2 inSize;          // Particle size
layout(location = 6) in float inRotation;     // Rotation in degrees
layout(location = 7) in float inRotationSpeed; // Rotation speed (unused)

// Uniform buffer (camera matrices)
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

// Outputs to fragment shader
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;

// Quad vertices for billboard (expanded in vertex shader)
const vec2 quadVertices[6] = vec2[](
    vec2(-0.5, -0.5),  // Bottom-left
    vec2( 0.5, -0.5),  // Bottom-right
    vec2( 0.5,  0.5),  // Top-right
    vec2(-0.5, -0.5),  // Bottom-left
    vec2( 0.5,  0.5),  // Top-right
    vec2(-0.5,  0.5)   // Top-left
);

const vec2 quadTexCoords[6] = vec2[](
    vec2(0.0, 1.0),
    vec2(1.0, 1.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 0.0),
    vec2(0.0, 0.0)
);

void main() {
    // Get quad vertex for this instance
    int vertexId = gl_VertexIndex % 6;
    vec2 quadPos = quadVertices[vertexId];

    // Apply rotation
    float rad = radians(inRotation);
    float cosR = cos(rad);
    float sinR = sin(rad);
    vec2 rotatedPos = vec2(
        quadPos.x * cosR - quadPos.y * sinR,
        quadPos.x * sinR + quadPos.y * cosR
    );

    // Scale by particle size
    rotatedPos *= inSize;

    // Billboard: get camera right and up vectors from view matrix
    vec3 cameraRight = vec3(ubo.view[0][0], ubo.view[1][0], ubo.view[2][0]);
    vec3 cameraUp = vec3(ubo.view[0][1], ubo.view[1][1], ubo.view[2][1]);

    // Compute world position with billboard offset
    vec3 worldPos = inPosition +
                    cameraRight * rotatedPos.x +
                    cameraUp * rotatedPos.y;

    // Transform to clip space
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(worldPos, 1.0);

    // Pass to fragment shader
    fragColor = inColor;
    fragTexCoord = quadTexCoords[vertexId];
}
