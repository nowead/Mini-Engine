// Particle shader - billboard quads with instancing
// WebGPU WGSL version

struct UniformBufferObject {
    model: mat4x4<f32>,
    view: mat4x4<f32>,
    proj: mat4x4<f32>,
}

@group(0) @binding(0) var<uniform> ubo: UniformBufferObject;

// Per-particle attributes (instanced)
struct VertexInput {
    @location(0) position: vec3<f32>,       // Particle world position
    @location(1) lifetime: f32,             // Remaining lifetime
    @location(2) velocity: vec3<f32>,       // Velocity (unused in vertex shader)
    @location(3) age: f32,                  // Current age
    @location(4) color: vec4<f32>,          // Particle color
    @location(5) size: vec2<f32>,           // Particle size
    @location(6) rotation: f32,             // Rotation in degrees
    @location(7) rotationSpeed: f32,        // Rotation speed (unused)
    @builtin(vertex_index) vertexIndex: u32,
}

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) texCoord: vec2<f32>,
}

// Quad vertices for billboard (expanded in vertex shader)
const quadVertices = array<vec2<f32>, 6>(
    vec2<f32>(-0.5, -0.5),  // Bottom-left
    vec2<f32>( 0.5, -0.5),  // Bottom-right
    vec2<f32>( 0.5,  0.5),  // Top-right
    vec2<f32>(-0.5, -0.5),  // Bottom-left
    vec2<f32>( 0.5,  0.5),  // Top-right
    vec2<f32>(-0.5,  0.5)   // Top-left
);

const quadTexCoords = array<vec2<f32>, 6>(
    vec2<f32>(0.0, 1.0),
    vec2<f32>(1.0, 1.0),
    vec2<f32>(1.0, 0.0),
    vec2<f32>(0.0, 1.0),
    vec2<f32>(1.0, 0.0),
    vec2<f32>(0.0, 0.0)
);

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;

    // Get quad vertex for this instance
    let vertexId = input.vertexIndex % 6u;
    var quadPos = quadVertices[vertexId];

    // Apply rotation
    let rad = radians(input.rotation);
    let cosR = cos(rad);
    let sinR = sin(rad);
    var rotatedPos = vec2<f32>(
        quadPos.x * cosR - quadPos.y * sinR,
        quadPos.x * sinR + quadPos.y * cosR
    );

    // Scale by particle size
    rotatedPos *= input.size;

    // Billboard: get camera right and up vectors from view matrix
    let cameraRight = vec3<f32>(ubo.view[0][0], ubo.view[1][0], ubo.view[2][0]);
    let cameraUp = vec3<f32>(ubo.view[0][1], ubo.view[1][1], ubo.view[2][1]);

    // Compute world position with billboard offset
    let worldPos = input.position +
                   cameraRight * rotatedPos.x +
                   cameraUp * rotatedPos.y;

    // Transform to clip space
    output.position = ubo.proj * ubo.view * ubo.model * vec4<f32>(worldPos, 1.0);

    // Pass to fragment shader
    output.color = input.color;
    output.texCoord = quadTexCoords[vertexId];

    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    // Simple circular particle shape
    let center = input.texCoord - vec2<f32>(0.5);
    let dist = length(center) * 2.0;

    // Soft circular falloff
    let alpha = 1.0 - smoothstep(0.0, 1.0, dist);

    // Apply particle color with alpha
    let outColor = vec4<f32>(input.color.rgb, input.color.a * alpha);

    // Discard fully transparent pixels
    if (outColor.a < 0.01) {
        discard;
    }

    return outColor;
}
